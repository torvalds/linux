// -*- C++ -*-

// Copyright (C) 2005, 2006 Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software
// Foundation; either version 2, or (at your option) any later
// version.

// This library is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this library; see the file COPYING.  If not, write to
// the Free Software Foundation, 59 Temple Place - Suite 330, Boston,
// MA 02111-1307, USA.

// As a special exception, you may use this file as part of a free
// software library without restriction.  Specifically, if other files
// instantiate templates or use macros or inline functions from this
// file, or you compile this file and link it with other files to
// produce an executable, this file does not by itself cause the
// resulting executable to be covered by the GNU General Public
// License.  This exception does not however invalidate any other
// reasons why the executable file might be covered by the GNU General
// Public License.

// Copyright (C) 2004 Ami Tavory and Vladimir Dreizin, IBM-HRL.

// Permission to use, copy, modify, sell, and distribute this software
// is hereby granted without fee, provided that the above copyright
// notice appears in all copies, and that both that copyright notice
// and this permission notice appear in supporting documentation. None
// of the above authors, nor IBM Haifa Research Laboratories, make any
// representation about the suitability of this software for any
// purpose. It is provided "as is" without express or implied
// warranty.

/**
 * @file hash_exponential_size_policy_imp.hpp
 * Contains a resize size policy implementation.
 */

PB_DS_CLASS_T_DEC
PB_DS_CLASS_C_DEC::
hash_exponential_size_policy(size_type start_size, size_type grow_factor) :
  m_start_size(start_size),
  m_grow_factor(grow_factor)
{ }

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
swap(PB_DS_CLASS_C_DEC& other)
{
  std::swap(m_start_size, other.m_start_size);
  std::swap(m_grow_factor, other.m_grow_factor);
}

PB_DS_CLASS_T_DEC
typename PB_DS_CLASS_C_DEC::size_type
PB_DS_CLASS_C_DEC::
get_nearest_larger_size(size_type size) const
{
  size_type ret = m_start_size;
  while (ret <= size)
    {
      const size_type next_ret = ret*  m_grow_factor;
      if (next_ret < ret)
	__throw_insert_error();
      ret = next_ret;
    }
  return ret;
}

PB_DS_CLASS_T_DEC
typename PB_DS_CLASS_C_DEC::size_type
PB_DS_CLASS_C_DEC::
get_nearest_smaller_size(size_type size) const
{
  size_type ret = m_start_size;
  while (true)
    {
      const size_type next_ret = ret*  m_grow_factor;
      if (next_ret < ret)
	__throw_resize_error();
      if (next_ret >= size)
	return (ret);
      ret = next_ret;
    }
  return ret;
}

