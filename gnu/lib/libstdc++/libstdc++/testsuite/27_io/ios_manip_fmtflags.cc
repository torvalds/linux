// 981027 ncm work with libstdc++v3

// Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002
// Free Software Foundation, Inc.
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

#include <sstream>
#include <locale>
#include <iomanip>
#include <testsuite_hooks.h>

struct MyNP : std::numpunct<char>
{
  std::string do_truename() const;
  std::string do_falsename() const;
};

std::string MyNP::do_truename()  const 
{ 
  std::string s("yea"); 
  return s; 
}

std::string MyNP::do_falsename() const 
{ 
  std::string s("nay"); 
  return s; 
}

void
test01()
{
  bool test = true;
  const char lit[] = "1 0\ntrue false\n:  true:\n:true  :\n: false:\n:  1:"
    		     "\n:1  :\n:  0:\nyea nay\n:   yea:\n:yea   :\n:   nay:\n";
  std::ostringstream oss;
  oss << true << " " << false << std::endl;
  oss << std::boolalpha;
  oss << true << " " << false << std::endl;

  oss << ":" << std::setw(6) << std::internal << true << ":" << std::endl;
  oss << ":" << std::setw(6) << std::left << true << ":" << std::endl;
  oss << ":" << std::setw(6) << std::right << false << ":" << std::endl;
  oss << std::noboolalpha;
  oss << ":" << std::setw(3) << std::internal << true << ":" << std::endl;
  oss << ":" << std::setw(3) << std::left << true << ":" << std::endl;
  oss << ":" << std::setw(3) << std::right << false << ":" << std::endl;

  std::locale loc = std::locale (std::locale::classic(), new MyNP);
  oss.imbue(loc);

  oss << std::boolalpha;
  oss << true << " " << false << std::endl;

  oss << ":" << std::setw(6) << std::internal << true << ":" << std::endl;
  oss << ":" << std::setw(6) << std::left << true << ":" << std::endl;
  oss << ":" << std::setw(6) << std::right << false << ":" << std::endl;

  VERIFY( oss.good() );
  VERIFY( oss.str() == lit );
}

void
test02()
{
  bool test = true;
  const std::string strue("true");
  const std::string sfalse("false");
  std::string str01;
  std::string str02;

  std::locale loc("");
  std::ostringstream ostr01;
  ostr01.imbue(loc);
  std::ios_base::fmtflags ff = ostr01.flags(std::ios_base::boolalpha);

  ostr01 << true;
  str02 = ostr01.str();
  VERIFY( str02 == strue );

  ostr01.str(str01);
  ostr01 << false;
  str02 = ostr01.str();
  VERIFY( str02 == sfalse );

  VERIFY( test );
}

int 
main() 
{
  test01();
  test02();
  return 0;
}

// Projected output:
/*
1 0
true false
:  true:
:true  :
: false:
:  1:
:1  :
:  0:
yea nay
:   yea:
:yea   :
:   nay:
*/
