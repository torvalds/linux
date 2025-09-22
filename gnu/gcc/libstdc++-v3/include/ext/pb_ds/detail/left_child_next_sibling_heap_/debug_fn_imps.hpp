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
 * @file debug_fn_imps.hpp
 * Contains an implementation class for left_child_next_sibling_heap_.
 */

#ifdef _GLIBCXX_DEBUG

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
assert_valid() const
{
  _GLIBCXX_DEBUG_ASSERT(m_p_root == NULL || m_p_root->m_p_prev_or_parent == NULL);

  if (m_p_root != NULL)
    assert_node_consistent(m_p_root, Single_Link_Roots);
  assert_size();
  assert_iterators();
}

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
assert_node_consistent(const_node_pointer p_nd, bool single_link) const
{
  if (p_nd == NULL)
    return;

  assert_node_consistent(p_nd->m_p_l_child, false);
  assert_node_consistent(p_nd->m_p_next_sibling, single_link);

  if (single_link)
    _GLIBCXX_DEBUG_ASSERT(p_nd->m_p_prev_or_parent == NULL);
  else if (p_nd->m_p_next_sibling != NULL)
    _GLIBCXX_DEBUG_ASSERT(p_nd->m_p_next_sibling->m_p_prev_or_parent == p_nd);

  if (p_nd->m_p_l_child == NULL)
    return;

  const_node_pointer p_child = p_nd->m_p_l_child;
  while (p_child != NULL)
    {
      const_node_pointer p_next_child = p_child->m_p_next_sibling;
      _GLIBCXX_DEBUG_ASSERT(!Cmp_Fn::operator()(p_nd->m_value, p_child->m_value));
      p_child = p_next_child;
    }
  _GLIBCXX_DEBUG_ASSERT(p_nd->m_p_l_child->m_p_prev_or_parent == p_nd);
}

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
assert_iterators() const
{
  const size_type calc_size = std::distance(begin(), end());
  if (calc_size == size())
    return;
  _GLIBCXX_DEBUG_ASSERT(0);
}

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
assert_size() const
{
  if (size_from_node(m_p_root) == m_size)
    return;
  _GLIBCXX_DEBUG_ASSERT(0);
}

PB_DS_CLASS_T_DEC
typename PB_DS_CLASS_C_DEC::size_type
PB_DS_CLASS_C_DEC::
size_under_node(const_node_pointer p_nd)
{ return 1 + size_from_node(p_nd->m_p_l_child); }

PB_DS_CLASS_T_DEC
typename PB_DS_CLASS_C_DEC::size_type
PB_DS_CLASS_C_DEC::
size_from_node(const_node_pointer p_nd)
{
  size_type ret = 0;
  while (p_nd != NULL)
    {
      ret += 1 + size_from_node(p_nd->m_p_l_child);
      p_nd = p_nd->m_p_next_sibling;
    }
  return ret;
}

PB_DS_CLASS_T_DEC
typename PB_DS_CLASS_C_DEC::size_type
PB_DS_CLASS_C_DEC::
degree(const_node_pointer p_nd)
{
  size_type ret = 0;
  const_node_pointer p_child = p_nd->m_p_l_child;
  while (p_child != NULL)
    {
      ++ret;
      p_child = p_child->m_p_next_sibling;
    }
  return ret;
}

#endif 
