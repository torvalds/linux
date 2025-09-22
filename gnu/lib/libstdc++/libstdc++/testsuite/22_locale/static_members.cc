// 2000-09-13 Benjamin Kosnik <bkoz@redhat.com>

// Copyright (C) 2000, 2002 Free Software Foundation
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

// 22.1.1.5 locale static members [lib.locale.statics]

#include <cwchar> // for mbstate_t
#include <locale>
#include <iostream>
#include <testsuite_hooks.h>

typedef std::codecvt<char, char, std::mbstate_t> ccodecvt;
class gnu_codecvt: public ccodecvt { }; 

void test01()
{
  using namespace std;
  bool test = true;

  string str1, str2;

  // Construct a locale object with the C facet.
  const locale loc01 = locale::classic();

  // Construct a locale object with the specialized facet.
  locale loc02(locale::classic(), new gnu_codecvt);
  VERIFY ( loc01 != loc02 );
  VERIFY ( !(loc01 == loc02) );

  // classic
  locale loc06("C");
  VERIFY (loc06 == loc01);
  str1 = loc06.name();
  VERIFY( str1 == "C" );

  // global
  locale loc03;
  VERIFY ( loc03 == loc01);
  locale global_orig = locale::global(loc02);
  locale loc05;
  VERIFY (loc05 != loc03);
  VERIFY (loc05 == loc02);

  // Reset global settings.
  locale::global(global_orig);
}

// Sanity check locale::global(loc) and setlocale.
void test02()
{
  using namespace std;
  bool test = true;
  
  const string ph("en_PH");
  const string mx("es_MX");
  const char* orig = setlocale(LC_ALL, NULL);
  const char* testph = setlocale(LC_ALL, ph.c_str());
  const char* testmx = setlocale(LC_ALL, mx.c_str());
  setlocale(LC_ALL, orig);

  // If the underlying locale doesn't support these names, setlocale
  // won't be reset. Therefore, disable unless we know these specific
  // named locales work.
  if (testph && testmx)
    {
      const locale loc_ph(ph.c_str());
      const locale loc_mx(mx.c_str());
      
      // Use setlocale between two calls to locale("")
      const locale loc_env_1("");
      setlocale(LC_ALL, ph.c_str());
      const locale loc_env_2("");
      VERIFY( loc_env_1 == loc_env_2 );
      
      // Change global locale.
      locale global_orig = locale::global(loc_mx);
      const char* lc_all_mx = setlocale(LC_ALL, NULL);
      if (lc_all_mx)
	{
	  cout << "lc_all_mx is " << lc_all_mx << endl;
	  VERIFY( mx == lc_all_mx );
	}
      
      // Restore global settings.
      locale::global(global_orig);
    }
}

// Static counter for use in checking ctors/dtors.
static std::size_t counter;

class surf : public std::locale::facet
{
public:
  static std::locale::id 	       	id;
  surf(size_t refs = 0): std::locale::facet(refs) { ++counter; }
  ~surf() { --counter; }
};

std::locale::id surf::id;

typedef surf facet_type;

// Verify lifetimes of global objects.
void test03()
{
  using namespace std;
  bool test = true;

  string name;
  locale global_orig;
  // 1: Destroyed when out of scope.
  {
    {
      {
	VERIFY( counter == 0 );
	{
	  locale loc01(locale::classic(), new facet_type);
	  VERIFY( counter == 1 );
	  global_orig = locale::global(loc01);
	  name = loc01.name();
	}
	VERIFY( counter == 1 );
	locale loc02 = locale();
	// Weak, but it's something...
	VERIFY( loc02.name() == name );
      }
      VERIFY( counter == 1 );
      // NB: loc03 should be a copy of the previous global locale.
      locale loc03 = locale::global(global_orig);
      VERIFY( counter == 1 );
      VERIFY( loc03.name() == name );
    }
    VERIFY( counter == 0 );
    locale loc04 = locale();
    VERIFY( loc04 == global_orig );
  }

  // 2: Not destroyed when out of scope, deliberately leaked.
  {
    {
      {
	VERIFY( counter == 0 );
	{
	  locale loc01(locale::classic(), new facet_type(1));
	  VERIFY( counter == 1 );
	  global_orig = locale::global(loc01);
	  name = loc01.name();
	}
	VERIFY( counter == 1 );
	locale loc02 = locale();
	// Weak, but it's something...
	VERIFY( loc02.name() == name );
      }
      VERIFY( counter == 1 );
      // NB: loc03 should be a copy of the previous global locale.
      locale loc03 = locale::global(global_orig);
      VERIFY( counter == 1 );
      VERIFY( loc03.name() == name );
    }
    VERIFY( counter == 1 );
    locale loc04 = locale();
    VERIFY( loc04 == global_orig );
  }
  VERIFY( counter == 1 );

  // Restore global settings.
  locale::global(global_orig);
}

int main ()
{
  test01();
  test02();

  test03();
  return 0;
}
