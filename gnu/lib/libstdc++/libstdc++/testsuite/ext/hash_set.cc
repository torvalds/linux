// 2002-04-28  Paolo Carlini  <pcarlini@unitus.it>
//             Peter Schmid  <schmid@snake.iap.physik.tu-darmstadt.de>

// Copyright (C) 2002 Free Software Foundation, Inc.
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

// hash_set (SGI extension)

#include <ext/hash_set>

void
test01()
{
  bool test = true;
  const int werte[] = { 1, 25, 9, 16, -36};
  const int anzahl = sizeof(werte) / sizeof(int);
  __gnu_cxx::hash_set<int> intTable(werte, werte + anzahl);
}

int main()
{
  test01();
  return 0;
}
