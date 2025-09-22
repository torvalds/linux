// 2003-02-12  Paolo Carlini  <pcarlini@unitus.it>

// Copyright (C) 2003 Free Software Foundation, Inc.
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

// 27.6.2.3 class basic_ostream::sentry

#include <ostream>
#include <testsuite_hooks.h>

// libstdc++/9563
struct buf: std::streambuf
{
  std::ios *io_;
  
  buf (std::ios *io): io_ (io) { }
  
  virtual int sync ()
  {
    io_->setstate (std::ios::failbit);
    return 0;
  }
};

void
test01()
{
  bool test = true;

  buf b(0);
  std::ostream strm(&b);
  
  buf tb(&strm);
  std::ostream tied(&tb);

  strm.tie(&tied);
  std::ostream::sentry s(strm);

  VERIFY( !s );
}

int main() 
{
  test01();
  return 0;
}
