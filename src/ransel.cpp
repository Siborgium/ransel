
#include <iostream>
#include <charconv>
#include <string_view>
#include <string>
#include <optional>
#include <random>
#include <filesystem>
#include <algorithm>

struct Param {
    using callback_type = void(*)(Param&, unsigned int);
    std::string_view  alias;
    std::string_view  full;
    bool              settable;
    unsigned int      defval;
    callback_type     callback_ptr;
};

enum class Flag { none, terse, verbose };

// utility
auto urand(unsigned int from, unsigned int to) -> unsigned int;
auto starts_with(std::string_view lhs, std::u8string_view rhs) -> bool;
auto trailing_int(std::string_view str) -> std::optional<unsigned int>;
void parse(Param* params, std::size_t params_count,
	   char** argv, std::size_t argc,
	   std::string& dirname);

// callbacks 
void help_call(Param& dummy1, unsigned int dummy2);
void copy_call(Param& copy, unsigned int dummy2);
void list_call(Param& copy, unsigned int dummy2);
void count_call(Param& count, unsigned int val);

const char* const help_message = R"(Usage: ransel [OPTIONS] DIRECTORY
Select random files from DIRECTORY.
Example: ransel --count=15 example)

Options:
  -h  --help  Display this message and quit
  -l  --list  List all selected files to stdout
              Enabled by default, set to 0 in order to disable
  -c  --copy  Copy selected files to the directory
              Directory name is 32-characters long random character sequence
              Enabled by default, set to 0 in order to disable
  -C  --count Count of files to select
              Set to 10 by default)";
    
int main(int argc, char* argv[]) {
    namespace fs = std::filesystem;

    if(argc <= 1) {
	std::cout << "Run this program with '--help' argument to get help\n";
    	std::exit(0);
    }

    constexpr std::size_t params_count = 4;
    Param parameters[params_count] = { { "-h", "--help", false, 0, &help_call },
				       { "-c", "--copy", false, 0, &copy_call },
				       { "-l", "--list", false, 0, &list_call },
				       { "-C", "--count", true, 10, &count_call } };
    
    auto& copy = parameters[1].defval;
    auto& list = parameters[2].defval;
    auto& req_count = parameters[3].defval;

    auto dirname_src = std::string{ "" };
    parse(parameters, 4, argv, argc, dirname_src);

    if(dirname_src.empty()){
	std::cerr << "ERROR: No directory specified\n";
	std::exit(-1);
    }

    if(req_count == 0) {
	std::cerr << "ERROR: Requested count is expected to be above zero\n";
	std::exit(-1);
    }
    
    auto dirname = std::string_view{ dirname_src };
    auto dir = fs::absolute(dirname);

    //counting files
    auto iters = std::vector<fs::directory_entry>(); // optimize here if possible
    {   // no scope pollution
	auto count_iter = fs::directory_iterator(dir);
	for(auto& ci : count_iter){ if(fs::is_regular_file(ci)) { iters.push_back(ci); } }
    }

    const auto file_count = iters.size();
    const auto count = std::min(file_count,
			  static_cast<decltype(iters)::size_type>(req_count));
    if(file_count == 0) { // todo: check if there is more clean solution
	std::cerr << "ERROR: Directory is empty";
	std::exit(-1);
    }
    
    //filling vector with random generated numbers
    // todo: force indices to be unique
    auto indices = std::vector<unsigned int>{ count };
    if(file_count == 1) {
	indices[0] = 0;
    } else {
	for(auto& index : indices) {
	    index = urand(1, count - 1);
	}
    }
    
    //resizing string, appending random generated name for new subdirectory
    //1 / 64^26 name collision chance is small enough to ignore
    auto destination = fs::path{};
    {
	auto destination_src = fs::current_path().string() + fs::path::preferred_separator;
	destination_src.resize(destination_src.size() + 32u);
	for(auto iter = destination_src.end() - 32; iter < destination_src.end() - 1; iter++) {
	    *iter = urand(static_cast<unsigned char>('a'),
			  static_cast<unsigned char>('z')); 
	}
	destination_src.back() = fs::path::preferred_separator;
	destination = fs::path{ std::move(destination_src) };
    }
    
    //parsing directory
    //if index of file exists in our vector, then copy this file (if needed) and write its name (if needed)
    {
	std::size_t id = 0;
	if(copy) {
	    auto ec = std::error_code{};
	    if(auto succ = fs::create_directory(destination, ec); !succ) {
		std::cerr << "ERROR: Failed to create directory " << destination << '\n';
		std::exit(1);
	    }
	}
	auto from = fs::directory_iterator(dir);
	std::cerr << "Starting to process indices...\n";
	for(auto& index : indices) {
	    if(index >= iters.size()) {
		std::cerr << "Index " << index << " is out of bounds [0, " << iters.size() - 1 << "]\n";
		std::exit(-1);
	    }
	    auto& iter = iters.at(index);
	    std::cerr << "Index " << index << "\nIter path " << iter.path().string() << '\n';
	    if(copy) fs::copy(iter.path(), destination);
	    if(list) std::cout << iter.path().string() << '\n';
	}
    }
    std::cout << '\n';
    return 0;
}

auto starts_with(std::string_view lhs, std::string_view rhs) -> bool {
    auto a = lhs.begin(), b = rhs.begin();
    while(b < rhs.end()) {
	if(*a != *b || a == lhs.end()) return false;
	a++; b++;
    }
    return true;
}

auto trailing_int(std::string_view str) -> std::optional<unsigned int> {
    auto e = str.end() - 1, b = str.begin();
    int ret = 0, mul = 1; bool succ = false;
    while(*e <= '9' && *e >= '0' && e >= b) {
	ret += static_cast<int>(*e - '0') * mul;
	mul *= 10; succ = true;
	e--;
    }
    return succ ? std::optional<unsigned int>{ ret } : std::optional<unsigned int>{ std::nullopt };
}

auto urand(unsigned int from, unsigned int to) -> unsigned int {
    static std::random_device rd;
    static std::default_random_engine gen{ rd() };
    std::uniform_int_distribution<unsigned int> dist(from, to);
    return dist(gen);
}

void parse(Param* params, std::size_t params_count,
	   char** argv, std::size_t argc,
	   std::string& dirname) {
    namespace fs = std::filesystem;
    
    for(std::size_t i = 1; i < argc; i++) {
	auto argument = std::string_view{ argv[i] };
	auto kind_of = starts_with(argument, "--") ? Flag::verbose : starts_with(argument, "-")
	                                           ? Flag::terse : Flag::none;

	if(kind_of == Flag::none) {
	    auto dir = fs::path(argument);
	    if(fs::exists(dir)) {
		if(fs::is_directory(dir)) {
		    dirname = dir.string();
		} else {
		    std::cerr << "ERROR: " << argument << " is not a directory\n";
		    std::exit(-1);
		}
	    } else {
		std::cerr << "ERROR: directory" << argument << " does not exist\n";
		std::exit(-1);
	    }
	} else {
	    for(std::size_t j = 0; j < params_count; j++) {
		auto& param = params[j];
		auto& [alias, fullname, settable, defvalue, callback] = param;
	    
		bool is_alias = argument == alias;
		bool is_fullname = starts_with(argument, fullname);
		if(is_alias || is_fullname) { // if we found a match
		    if(!settable) {
			callback(param, defvalue);
		    } else {
			switch(kind_of) {
			case Flag::terse: {
			    if(i + 1 < argc) {
				// argument is an alias, therefore the next argument
				// contains value for this one
				auto next = std::string_view{ argv[i + 1] };
				auto value = 0u;
				if(auto [p, e] = std::from_chars(next.data(),
								 next.data() + next.size(),
								 value);
				   e != std::errc()) {
				    std::cerr <<
					"Failed to decode value of " <<
					next << " for flag " << argument << '\n';
				    std::exit(-1);
				}
				callback(param, value);
			    } else { // argument is an alias and the last in the list
				// it's value isn't settable and was not provided
				// therefore it's an error
				std::cerr << "ERROR: No value provided for flag " << argument << '\n';
				std::exit(-1);
			    }
			    i++;
			} break;
		    
			case Flag::verbose: {
			    if(auto res = trailing_int(argument); res) {
				callback(param, res.value());
			    } else {
				std::cerr << "ERROR: Failed to decode value for " << argument << '\n';
				std::exit(-1);
			    }
			} break;

			case Flag::none: break; //unreachable
			} // switch
		    } 
		}
	    }
	}
    }
}


void help_call(Param& dummy1, unsigned int dummy2) {
    std::cout << help_message << '\n';
    std::exit(0);
}

void copy_call(Param& copy, unsigned int dummy2) {
    copy.defval = 1;
}

void list_call(Param& list, unsigned int dummy2) {
    list.defval = 1;
}

void count_call(Param& count, unsigned int val) {
    count.defval = val;
}