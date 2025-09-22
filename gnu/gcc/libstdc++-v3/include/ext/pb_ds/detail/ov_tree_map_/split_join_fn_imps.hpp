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
 * @file split_join_fn_imps.hpp
 * Contains an implementation class for ov_tree_.
 */

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
split(const_key_reference r_key, PB_DS_CLASS_C_DEC& other)
{
  _GLIBCXX_DEBUG_ONLY(assert_valid();)
  _GLIBCXX_DEBUG_ONLY(other.assert_valid();)

  if (m_size == 0)
    {
      other.clear();
      _GLIBCXX_DEBUG_ONLY(assert_valid();)
      _GLIBCXX_DEBUG_ONLY(other.assert_valid();)
      return;
    }

  if (Cmp_Fn::operator()(r_key, PB_DS_V2F(*begin())))
    {
      value_swap(other);
      _GLIBCXX_DEBUG_ONLY(assert_valid();)
      _GLIBCXX_DEBUG_ONLY(other.assert_valid();)
      return;
    }

  if (!Cmp_Fn::operator()(r_key, PB_DS_V2F(*(end() - 1))))
    {
      _GLIBCXX_DEBUG_ONLY(assert_valid();)
      _GLIBCXX_DEBUG_ONLY(other.assert_valid();)
      return;
    }

  if (m_size == 1)
    {
      value_swap(other);
      _GLIBCXX_DEBUG_ONLY(assert_valid();)
      _GLIBCXX_DEBUG_ONLY(other.assert_valid();)
      return;
    }

  _GLIBCXX_DEBUG_ONLY(map_debug_base::join(other);)
  iterator it = upper_bound(r_key);
  PB_DS_CLASS_C_DEC new_other(other, other);
  new_other.copy_from_ordered_range(it, end());
  PB_DS_CLASS_C_DEC new_this(*this, * this);
  new_this.copy_from_ordered_range(begin(), it);

  // No exceptions from this point.
  _GLIBCXX_DEBUG_ONLY(map_debug_base::split(r_key,(Cmp_Fn& )(*this), other);)
  other.update(other.node_begin(), (node_update* )(&other));
  update(node_begin(), (node_update* )this);
  other.value_swap(new_other);
  value_swap(new_this);
  _GLIBCXX_DEBUG_ONLY(assert_valid();)
  _GLIBCXX_DEBUG_ONLY(other.assert_valid();)
}

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
join(PB_DS_CLASS_C_DEC& other)
{
  _GLIBCXX_DEBUG_ONLY(assert_valid();)
  _GLIBCXX_DEBUG_ONLY(other.assert_valid();)
  if (other.m_size == 0)
    return;

  if (m_size == 0)
    {
      value_swap(other);
      return;
    }

  const bool greater = Cmp_Fn::operator()(PB_DS_V2F(*(end() - 1)),
					  PB_DS_V2F(*other.begin()));

  const bool lesser = Cmp_Fn::operator()(PB_DS_V2F(*(other.end() - 1)),
					 PB_DS_V2F(*begin()));

  if (!greater && !lesser)
    __throw_join_error();

  PB_DS_CLASS_C_DEC new_this(*this, *this);

  if (greater)
    new_this.copy_from_ordered_range(begin(), end(), 
				     other.begin(), other.end());
  else
    new_this.copy_from_ordered_range(other.begin(), other.end(),
				     begin(), end());

  // No exceptions from this point.
  _GLIBCXX_DEBUG_ONLY(map_debug_base::join(other);)
  value_swap(new_this);
  other.clear();
  _GLIBCXX_DEBUG_ONLY(assert_valid();)
  _GLIBCXX_DEBUG_ONLY(other.assert_valid();)
}
