// 2001-02-11 gdr
// Origin: Craig Rodrigues <rodrigc@mediaone.net>

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

// 21.1.2: char_traits typedefs

#include <string>

int main()
{
  // 21.1.3: char_traits<char>::int_type == int
  // dg-options -ansi -pedantic-err
  std::char_traits<char>::int_type* p = 0;
  int* q = p;                   // dg-do compile

  return 0;
}
