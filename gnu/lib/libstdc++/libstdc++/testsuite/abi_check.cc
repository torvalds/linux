// Utility for libstdc++ ABI analysis -*- C++ -*-

// Copyright (C) 2002, 2003, 2004 Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2, or (at your option)
// any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License along
// with this library; see the file COPYING.  If not, write to the Free
// Software Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307,
// USA.

// As a special exception, you may use this file as part of a free software
// library without restriction.  Specifically, if other files instantiate
// templates or use macros or inline functions from this file, or you compile
// this file and link it with other files to produce an executable, this
// file does not by itself cause the resulting executable to be covered by
// the GNU General Public License.  This exception does not however
// invalidate any other reasons why the executable file might be covered by
// the GNU General Public License.

// Benjamin Kosnik  <bkoz@redhat.com>
// Blame subsequent hacks on Loren J. Rittle <ljrittle@acm.org>, Phil
// Edwards <pme@gcc.gnu.org>, and a cast of dozens at libstdc++@gcc.gnu.org.

#include <string>
#include <ext/hash_map>
#include <deque>
#include <sstream>
#include <fstream>
#include <iostream>
#include <cxxabi.h>
#include <stdlib.h>    // for system(3)
#include <unistd.h>    // for access(2)

struct symbol_info
{
  enum category { none, function, object, error };
  category 	type;  
  std::string 	name;
  std::string 	demangled_name;
  int 		size;
  std::string 	version_name;

  symbol_info() : type(none), size(0) { }

  symbol_info(const symbol_info& other) 
  : type(other.type), name(other.name), demangled_name(other.demangled_name), 
   size(other.size), version_name(other.version_name) { }
};

namespace __gnu_cxx
{
  using namespace std;

  template<> 
    struct hash<string>
    {
      size_t operator()(const string& s) const 
      { 
	const collate<char>& c = use_facet<collate<char> >(locale::classic());
	return c.hash(s.c_str(), s.c_str() + s.size());
      }
    }; 
}

typedef std::deque<std::string>				symbol_names;
typedef __gnu_cxx::hash_map<std::string, symbol_info> 	symbol_infos;


bool
check_version(const symbol_info& test, bool added = false)
{
  typedef std::vector<std::string> compat_list;
  static compat_list known_versions;
  if (known_versions.empty())
    {
      known_versions.push_back("GLIBCPP_3.2"); // base version
      known_versions.push_back("GLIBCPP_3.2.1");
      known_versions.push_back("GLIBCPP_3.2.2");
      known_versions.push_back("GLIBCPP_3.2.3"); // gcc-3.3.0
      known_versions.push_back("GLIBCPP_3.2.4"); // gcc-3.3.4
      known_versions.push_back("CXXABI_1.2");
      known_versions.push_back("CXXABI_1.2.1");
      known_versions.push_back("CXXABI_1.2.2");
    }
  compat_list::iterator begin = known_versions.begin();
  compat_list::iterator end = known_versions.end();

  // Check version names for compatibility...
  compat_list::iterator it1 = find(begin, end, test.version_name);
  
  // Check for weak label.
  compat_list::iterator it2 = find(begin, end, test.name);

  // Check that added symbols aren't added in the base version.
  bool compat = true;
  if (added && test.version_name == known_versions[0])
    compat = false;

  if (it1 == end && it2 == end)
    compat = false;

  return compat;
}

bool 
check_compatible(const symbol_info& lhs, const symbol_info& rhs, 
		 bool verbose = false)
{
  using namespace std;
  bool ret = true;
  const char tab = '\t';

  // Check to see if symbol_infos are compatible.
  if (lhs.type != rhs.type)
    {
      ret = false;
      if (verbose)
	{
	  cout << tab << "incompatible types" << endl;
	}
    }
  
  if (lhs.name != rhs.name)
    {
      ret = false;
      if (verbose)
	{
	  cout << tab << "incompatible names" << endl;
	}
    }

  if (lhs.size != rhs.size)
    {
      ret = false;
      if (verbose)
	{
	  cout << tab << "incompatible sizes" << endl;
	  cout << tab << lhs.size << endl;
	  cout << tab << rhs.size << endl;
	}
    }

  if (lhs.version_name != rhs.version_name 
      && !check_version(lhs) && !check_version(rhs))
    {
      ret = false;
      if (verbose)
	{
	  cout << tab << "incompatible versions" << endl;
	  cout << tab << lhs.version_name << endl;
	  cout << tab << rhs.version_name << endl;
	}
    }

  if (verbose)
    cout << endl;

  return ret;
}

const char*
demangle(const std::string& mangled)
{
  const char* name;
  if (mangled[0] != '_' || mangled[1] != 'Z')
    {
      // This is not a mangled symbol, thus has "C" linkage.
      name = mangled.c_str();
    }
  else
    {
      // Use __cxa_demangle to demangle.
      int status = 0;
      name = abi::__cxa_demangle(mangled.c_str(), 0, 0, &status);
      if (!name)
	{
	  switch (status)
	    {
	    case 0:
	      name = "error code = 0: success";
	      break;
	    case -1:
	      name = "error code = -1: memory allocation failure";
	      break;
	    case -2:
	      name = "error code = -2: invalid mangled name";
	      break;
	    case -3:
	      name = "error code = -3: invalid arguments";
	      break;
	    default:
	      name = "error code unknown - who knows what happened";
	    }
	}
    }
  return name;
}

void 
line_to_symbol_info(std::string& input, symbol_info& output)
{
  using namespace std;
  const char delim = ':';
  const char version_delim = '@';
  const string::size_type npos = string::npos;
  string::size_type n = 0;

  // Set the type.
  if (input.find("FUNC") == 0)
    output.type = symbol_info::function;
  else if (input.find("OBJECT") == 0)
    output.type = symbol_info::object;
  else
    output.type = symbol_info::error;
  n = input.find_first_of(delim);
  if (n != npos)
    input.erase(input.begin(), input.begin() + n + 1);

  // Iff object, get size info.
  if (output.type == symbol_info::object)
    {
      n = input.find_first_of(delim);
      if (n != npos)
	{
	  string size(input.begin(), input.begin() + n);
	  istringstream iss(size);
	  int x;
	  iss >> x;
	  if (!iss.fail())
	    output.size = x;
	  input.erase(input.begin(), input.begin() + n + 1);
	}
    }

  // Set the name.
  n = input.find_first_of(version_delim);
  if (n != npos)
    {
      // Found version string.
      output.name = string(input.begin(), input.begin() + n);
      n = input.find_last_of(version_delim);
      input.erase(input.begin(), input.begin() + n + 1);

      // Set version name.
      output.version_name = input;
    }
  else
    {
      // No versioning info.
      output.name = string(input.begin(), input.end());
      input.erase(input.begin(), input.end());
    }

  // Set the demangled name.
  output.demangled_name = demangle(output.name);
}

void
create_symbol_data(const char* file, symbol_infos& symbols, 
		   symbol_names& names)
{
  // Parse list of symbols in file into vectors of symbol_info.
  // For 3.2.0 on x86/linux, this usually is
  // 947 non-weak symbols
  // 2084 weak symbols
  using namespace std;
  ifstream ifs(file); 
  if (ifs.is_open())
    {
      // Organize input into container of symbol_info objects.
      const string empty;
      string line = empty;
      while (getline(ifs, line).good())
	{
	  symbol_info symbol;
	  line_to_symbol_info(line, symbol);
	  symbols[symbol.name] = symbol;
	  names.push_back(symbol.name);
	  line = empty;
	}
    }
}

void
report_symbol_info(const symbol_info& symbol, std::size_t n, bool ret = true)
{
  using namespace std;
  const char tab = '\t';

  // Add any other information to display here.
  cout << tab << symbol.demangled_name << endl;
  cout << tab << symbol.name << endl;
  cout << tab << symbol.version_name << endl;

  if (ret)
    cout << endl;
}


int
main(int argc, char** argv)
{
  using namespace std;

  // Get arguments.  (Heading towards getopt_long, I can feel it.)
  bool verbose = false;
  string argv1 = argc > 1 ? argv[1] : "";
  if (argv1 == "--help" || argc < 4)
    {
      cerr << "usage: abi_check --check current baseline\n"
              "                 --check-verbose current baseline\n"
              "                 --help\n\n"
              "Where CURRENT is a file containing the current results from\n"
              "extract_symvers, and BASELINE is one from config/abi.\n"
	   << endl;
      exit(1);
    }
  else if (argv1 == "--check-verbose")
    verbose = true;

  // Quick sanity/setup check for arguments.
  const char* test_file = argv[2];
  const char* baseline_file = argv[3];
  if (access(test_file, R_OK) != 0)
    {
      cerr << "Cannot read symbols file " << test_file
           << ", did you forget to build first?" << endl;
      exit(1);
    }
  if (access(baseline_file, R_OK) != 0)
    {
      cerr << "Cannot read baseline file " << baseline_file << endl;
      exit(1);
    }

  // Input both lists of symbols into container.
  symbol_infos  baseline_symbols;
  symbol_names  baseline_names;
  symbol_infos  test_symbols;
  symbol_names  test_names;
  create_symbol_data(baseline_file, baseline_symbols, baseline_names);
  create_symbol_data(test_file, test_symbols, test_names);

  //  Sanity check results.
  const symbol_names::size_type baseline_size = baseline_names.size();
  const symbol_names::size_type test_size = test_names.size();
  if (!baseline_size || !test_size)
    {
      cerr << "Problems parsing the list of exported symbols." << endl;
      exit(2);
    }

  // Sort out names.
  // Assuming baseline_names, test_names are both unique w/ no duplicates.
  //
  // The names added to missing_names are baseline_names not found in
  // test_names 
  // -> symbols that have been deleted.
  //
  // The names added to added_names are test_names are names not in
  // baseline_names
  // -> symbols that have been added.
  symbol_names shared_names;
  symbol_names missing_names;
  symbol_names added_names = test_names;
  for (size_t i = 0; i < baseline_size; ++i)
    {
      string what(baseline_names[i]);
      symbol_names::iterator end = added_names.end();
      symbol_names::iterator it = find(added_names.begin(), end, what);
      if (it != end)
	{
	  // Found.
	  shared_names.push_back(what);
	  added_names.erase(it);
	}
      else
	  missing_names.push_back(what);
    }

  // Check missing names for compatibility.
  typedef pair<symbol_info, symbol_info> symbol_pair;
  vector<symbol_pair> incompatible;
  for (size_t i = 0; i < missing_names.size(); ++i)
    {
      symbol_info base = baseline_symbols[missing_names[i]];
      incompatible.push_back(symbol_pair(base, base));
    }

  // Check shared names for compatibility.
  for (size_t i = 0; i < shared_names.size(); ++i)
    {
      symbol_info base = baseline_symbols[shared_names[i]];
      symbol_info test = test_symbols[shared_names[i]];
      if (!check_compatible(base, test))
	incompatible.push_back(symbol_pair(base, test));
    }

  // Check added names for compatibility.
  for (size_t i = 0; i < added_names.size(); ++i)
    {
      symbol_info test = test_symbols[added_names[i]];
      if (!check_version(test, true))
	incompatible.push_back(symbol_pair(test, test));
    }

  // Report results.
  if (verbose && added_names.size())
    {
      cout << added_names.size() << " added symbols " << endl;
      for (size_t j = 0; j < added_names.size() ; ++j)
	report_symbol_info(test_symbols[added_names[j]], j + 1);
    }
  
  if (verbose && missing_names.size())
    {
      cout << missing_names.size() << " missing symbols " << endl;
      for (size_t j = 0; j < missing_names.size() ; ++j)
	report_symbol_info(baseline_symbols[missing_names[j]], j + 1);
    }
  
  if (verbose && incompatible.size())
    {
      cout << incompatible.size() << " incompatible symbols " << endl;
      for (size_t j = 0; j < incompatible.size() ; ++j)
	{
	  // First, report name.
	  const symbol_info& base = incompatible[j].first;
	  const symbol_info& test = incompatible[j].second;
	  report_symbol_info(test, j + 1, false);
	  
	  // Second, report reason or reasons incompatible.
	  check_compatible(base, test, true);
	}
    }
  
  cout << "\n\t\t=== libstdc++-v3 check-abi Summary ===" << endl;
  cout << endl;
  cout << "# of added symbols:\t\t " << added_names.size() << endl;
  cout << "# of missing symbols:\t\t " << missing_names.size() << endl;
  cout << "# of incompatible symbols:\t " << incompatible.size() << endl;
  cout << endl;
  cout << "using: " << baseline_file << endl;

  return 0;
}
