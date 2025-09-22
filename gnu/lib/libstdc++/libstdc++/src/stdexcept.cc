// Methods for Exception Support for -*- C++ -*-

// Copyright (C) 1997, 1999, 2001 Free Software Foundation, Inc.
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

//
// ISO C++ 14882: 19.1  Exception classes
//

#include <string>
#include <stdexcept>

namespace std 
{
  logic_error::logic_error(const string& __arg) 
  : exception(), _M_msg(__arg) { }

  logic_error::~logic_error() throw() { };

  const char*
  logic_error::what() const throw()
  { return _M_msg.c_str(); }

  domain_error::domain_error(const string& __arg)
  : logic_error(__arg) { }

  invalid_argument::invalid_argument(const string& __arg)
  : logic_error(__arg) { }

  length_error::length_error(const string& __arg)
  : logic_error(__arg) { }

  out_of_range::out_of_range(const string& __arg)
  : logic_error(__arg) { }

  runtime_error::runtime_error(const string& __arg) 
  : exception(), _M_msg(__arg) { }

  runtime_error::~runtime_error() throw() { };

  const char*
  runtime_error::what() const throw()
  { return _M_msg.c_str(); }

  range_error::range_error(const string& __arg)
  : runtime_error(__arg) { }

  overflow_error::overflow_error(const string& __arg)
  : runtime_error(__arg) { }

  underflow_error::underflow_error(const string& __arg)
  : runtime_error(__arg) { }
} // namespace std

