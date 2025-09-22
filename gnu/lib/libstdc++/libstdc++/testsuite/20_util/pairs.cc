// 2001-06-18  Benjamin Kosnik  <bkoz@redhat.com>

// Copyright (C) 2001 Free Software Foundation, Inc.
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

// 20.2.2 Pairs

#include <utility>
#include <testsuite_hooks.h>

class gnu_obj
{
  int i;
public:
  gnu_obj(int arg = 0): i(arg) { }
  bool operator==(const gnu_obj& rhs) const { return i == rhs.i; }
  bool operator<(const gnu_obj& rhs) const { return i < rhs.i; }
};

template<typename T>
  struct gnu_t
  {
    bool b;
  public:
    gnu_t(bool arg = 0): b(arg) { }
    bool operator==(const gnu_t& rhs) const { return b == rhs.b; }
    bool operator<(const gnu_t& rhs) const { return int(b) < int(rhs.b); }
  };


// heterogeneous
void test01()
{
  bool test = true;

  std::pair<bool, long> p_bl_1(true, 433);
  std::pair<bool, long> p_bl_2 = std::make_pair(true, 433);
  VERIFY( p_bl_1 == p_bl_2 );
  VERIFY( !(p_bl_1 < p_bl_2) );

  std::pair<const char*, float> p_sf_1("total enlightenment", 433.00);
  std::pair<const char*, float> p_sf_2 = std::make_pair("total enlightenment", 
							433.00);
  VERIFY( p_sf_1 == p_sf_2 );
  VERIFY( !(p_sf_1 < p_sf_2) );

  std::pair<const char*, gnu_obj> p_sg_1("enlightenment", gnu_obj(5));
  std::pair<const char*, gnu_obj> p_sg_2 = std::make_pair("enlightenment", 
							  gnu_obj(5));
  VERIFY( p_sg_1 == p_sg_2 );
  VERIFY( !(p_sg_1 < p_sg_2) );

  std::pair<gnu_t<long>, gnu_obj> p_st_1(gnu_t<long>(false), gnu_obj(5));
  std::pair<gnu_t<long>, gnu_obj> p_st_2 = std::make_pair(gnu_t<long>(false),
							  gnu_obj(5));
  VERIFY( p_st_1 == p_st_2 );
  VERIFY( !(p_st_1 < p_st_2) );
}

// homogeneous
void test02()
{
  bool test = true;

  std::pair<bool, bool> p_bb_1(true, false);
  std::pair<bool, bool> p_bb_2 = std::make_pair(true, false);
  VERIFY( p_bb_1 == p_bb_2 );
  VERIFY( !(p_bb_1 < p_bb_2) );
}


// const
void test03()
{
  bool test = true;

  const std::pair<bool, long> p_bl_1(true, 433);
  const std::pair<bool, long> p_bl_2 = std::make_pair(true, 433);
  VERIFY( p_bl_1 == p_bl_2 );
  VERIFY( !(p_bl_1 < p_bl_2) );

  const std::pair<const char*, float> p_sf_1("total enlightenment", 433.00);
  const std::pair<const char*, float> p_sf_2 = 
    std::make_pair("total enlightenment", 433.00);
  VERIFY( p_sf_1 == p_sf_2 );
  VERIFY( !(p_sf_1 < p_sf_2) );

  const std::pair<const char*, gnu_obj> p_sg_1("enlightenment", gnu_obj(5));
  const std::pair<const char*, gnu_obj> p_sg_2 = 
    std::make_pair("enlightenment", gnu_obj(5));
  VERIFY( p_sg_1 == p_sg_2 );
  VERIFY( !(p_sg_1 < p_sg_2) );

  const std::pair<gnu_t<long>, gnu_obj> p_st_1(gnu_t<long>(false), gnu_obj(5));
  const std::pair<gnu_t<long>, gnu_obj> p_st_2 = 
    std::make_pair(gnu_t<long>(false), gnu_obj(5));
  VERIFY( p_st_1 == p_st_2 );
  VERIFY( !(p_st_1 < p_st_2) );
}

// const&
void test04()
{
  bool test = true;
  const gnu_obj& obj1 = gnu_obj(5);
  const std::pair<const char*, gnu_obj> p_sg_1("enlightenment", obj1);
  const std::pair<const char*, gnu_obj> p_sg_2 = 
    std::make_pair("enlightenment", obj1);
  VERIFY( p_sg_1 == p_sg_2 );
  VERIFY( !(p_sg_1 < p_sg_2) );

  const gnu_t<long>& tmpl1 = gnu_t<long>(false);
  const std::pair<gnu_t<long>, gnu_obj> p_st_1(tmpl1, obj1);
  const std::pair<gnu_t<long>, gnu_obj> p_st_2 = std::make_pair(tmpl1, obj1);
  VERIFY( p_st_1 == p_st_2 );
  VERIFY( !(p_st_1 < p_st_2) );
}

int main() 
{ 
  test01(); 
  test02();
  test03();
  test04();
}
