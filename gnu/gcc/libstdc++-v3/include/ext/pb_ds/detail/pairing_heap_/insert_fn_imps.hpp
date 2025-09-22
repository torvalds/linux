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
 * @file insert_fn_imps.hpp
 * Contains an implementation class for a pairing heap.
 */

PB_DS_CLASS_T_DEC
inline typename PB_DS_CLASS_C_DEC::point_iterator
PB_DS_CLASS_C_DEC::
push(const_reference r_val)
{
  _GLIBCXX_DEBUG_ONLY(assert_valid();)

    node_pointer p_new_nd = base_type::get_new_node_for_insert(r_val);

  push_imp(p_new_nd);

  _GLIBCXX_DEBUG_ONLY(assert_valid();)

    return point_iterator(p_new_nd);
}

PB_DS_CLASS_T_DEC
inline void
PB_DS_CLASS_C_DEC::
push_imp(node_pointer p_nd)
{
  p_nd->m_p_l_child = NULL;

  if (base_type::m_p_root == NULL)
    {
      p_nd->m_p_next_sibling = p_nd->m_p_prev_or_parent = NULL;

      base_type::m_p_root = p_nd;
    }
  else if (Cmp_Fn::operator()(base_type::m_p_root->m_value, p_nd->m_value))
    {
      p_nd->m_p_next_sibling = p_nd->m_p_prev_or_parent = NULL;

      base_type::make_child_of(base_type::m_p_root, p_nd);
      _GLIBCXX_DEBUG_ONLY(base_type::assert_node_consistent(p_nd, false));

      base_type::m_p_root = p_nd;
    }
  else
    {
      base_type::make_child_of(p_nd, base_type::m_p_root);
      _GLIBCXX_DEBUG_ONLY(base_type::assert_node_consistent(base_type::m_p_root, false));
    }
}

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
modify(point_iterator it, const_reference r_new_val)
{
  _GLIBCXX_DEBUG_ONLY(assert_valid();)

    remove_node(it.m_p_nd);

  it.m_p_nd->m_value = r_new_val;

  push_imp(it.m_p_nd);

  _GLIBCXX_DEBUG_ONLY(assert_valid();)
    }

