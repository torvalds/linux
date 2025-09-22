// -*- C++ -*-
// Utility subroutines for the C++ library testsuite. 
//
// Copyright (C) 2002, 2003 Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2, or (at your option)
// any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this library; see the file COPYING.  If not, write to the Free
// Software Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307,
// USA.
//
// As a special exception, you may use this file as part of a free software
// library without restriction.  Specifically, if other files instantiate
// templates or use macros or inline functions from this file, or you compile
// this file and link it with other files to produce an executable, this
// file does not by itself cause the resulting executable to be covered by
// the GNU General Public License.  This exception does not however
// invalidate any other reasons why the executable file might be covered by
// the GNU General Public License.

#include <testsuite_hooks.h>

#ifdef _GLIBCPP_MEM_LIMITS
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>
#endif
#include <list>
#include <string>
#include <stdexcept>
#include <clocale>
#include <locale>

namespace __gnu_cxx_test
{
#ifdef _GLIBCPP_MEM_LIMITS
  void 
  set_memory_limits(float size)
  {
    struct rlimit r;
    // Cater to the absence of rlim_t.
    __typeof__ (r.rlim_cur) limit = (__typeof__ (r.rlim_cur))(size * 1048576);

    // Heap size, seems to be common.
#if _GLIBCPP_HAVE_MEMLIMIT_DATA
    getrlimit(RLIMIT_DATA, &r);
    r.rlim_cur = limit;
    setrlimit(RLIMIT_DATA, &r);
#endif

    // Resident set size.
#if _GLIBCPP_HAVE_MEMLIMIT_RSS
    getrlimit(RLIMIT_RSS, &r);
    r.rlim_cur = limit;
    setrlimit(RLIMIT_RSS, &r);
#endif

    // Mapped memory (brk + mmap).
#if _GLIBCPP_HAVE_MEMLIMIT_VMEM
    getrlimit(RLIMIT_VMEM, &r);
    r.rlim_cur = limit;
    setrlimit(RLIMIT_VMEM, &r);
#endif

    // Virtual memory.
#if _GLIBCPP_HAVE_MEMLIMIT_AS
    getrlimit(RLIMIT_AS, &r);
    r.rlim_cur = limit;
    setrlimit(RLIMIT_AS, &r);
#endif
  }

#else
  void
  set_memory_limits(float) { }
#endif 

  // Useful exceptions.
  class locale_data : public std::runtime_error 
  {
  public:
    explicit 
    locale_data(const std::string&  __arg) : runtime_error(__arg) { }
  };

  class environment_variable: public std::runtime_error 
  {
  public:
    explicit 
    environment_variable(const std::string&  __arg) : runtime_error(__arg) { }
  };

  class not_found : public std::runtime_error 
  {
  public:
    explicit 
    not_found(const std::string&  __arg) : runtime_error(__arg) { }
  };

  void 
  run_tests_wrapped_locale(const char* name, const func_callback& l)
  {
    using namespace std;
    bool test = true;
    
    // Set the global locale. 
    locale loc_name(name);
    locale orig = locale::global(loc_name);
    
    const char* res = setlocale(LC_ALL, name);
    if (res != NULL)
      {
	string preLC_ALL = res;
	for (func_callback::const_iterator i = l.begin(); i != l.end(); ++i)
	  (*i)();
	string postLC_ALL= setlocale(LC_ALL, NULL);
	VERIFY( preLC_ALL == postLC_ALL );
      }
    else
      throw environment_variable(string("LC_ALL for") + string(name));
  }
  
  void 
  run_tests_wrapped_env(const char* name, const char* env,
			const func_callback& l)
  {
    using namespace std;
    bool test = true;
    
#ifdef _GLIBCPP_HAVE_SETENV 
    // Set the global locale. 
    locale loc_name(name);
    locale orig = locale::global(loc_name);

    // Set environment variable env to value in name. 
    const char* oldENV = getenv(env);
    if (!setenv(env, name, 1))
      {
	for (func_callback::const_iterator i = l.begin(); i != l.end(); ++i)
	  (*i)();
	setenv(env, oldENV ? oldENV : "", 1);
      }
    else
      throw environment_variable(string(env) + string(" to ") + string(name));
#else
    throw not_found("setenv");
#endif
  }

  counter::size_type  counter::count = 0;
  unsigned int copy_constructor::count_ = 0;
  unsigned int copy_constructor::throw_on_ = 0;
  unsigned int assignment_operator::count_ = 0;
  unsigned int assignment_operator::throw_on_ = 0;
  unsigned int destructor::_M_count = 0;
  int copy_tracker::next_id_ = 0;
}; // namespace __cxx_test
