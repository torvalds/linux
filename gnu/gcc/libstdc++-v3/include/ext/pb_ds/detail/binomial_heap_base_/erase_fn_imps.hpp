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
 * @file erase_fn_imps.hpp
 * Contains an implementation class for a base of binomial heaps.
 */

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
pop()
{
  _GLIBCXX_DEBUG_ONLY(assert_valid(true);)
    _GLIBCXX_DEBUG_ASSERT(!base_type::empty());

  if (m_p_max == NULL)
    find_max();

  _GLIBCXX_DEBUG_ASSERT(m_p_max != NULL);

  node_pointer p_nd = m_p_max;

  remove_parentless_node(m_p_max);

  base_type::actual_erase_node(p_nd);

  m_p_max = NULL;

  _GLIBCXX_DEBUG_ONLY(assert_valid(true);)
    }

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
remove_parentless_node(node_pointer p_nd)
{
  _GLIBCXX_DEBUG_ASSERT(p_nd != NULL);
  _GLIBCXX_DEBUG_ASSERT(base_type::parent(p_nd) == NULL);

  node_pointer p_cur_root = p_nd == base_type::m_p_root?
    p_nd->m_p_next_sibling :
    base_type::m_p_root;

  if (p_cur_root != NULL)
    p_cur_root->m_p_prev_or_parent = NULL;

  if (p_nd->m_p_prev_or_parent != NULL)
    p_nd->m_p_prev_or_parent->m_p_next_sibling = p_nd->m_p_next_sibling;

  if (p_nd->m_p_next_sibling != NULL)
    p_nd->m_p_next_sibling->m_p_prev_or_parent = p_nd->m_p_prev_or_parent;

  node_pointer p_child = p_nd->m_p_l_child;

  if (p_child != NULL)
    {
      p_child->m_p_prev_or_parent = NULL;

      while (p_child->m_p_next_sibling != NULL)
	p_child = p_child->m_p_next_sibling;
    }

  m_p_max = NULL;

  base_type::m_p_root = join(p_cur_root, p_child);
}

PB_DS_CLASS_T_DEC
inline void
PB_DS_CLASS_C_DEC::
clear()
{
  base_type::clear();

  m_p_max = NULL;
}

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
erase(point_iterator it)
{
  _GLIBCXX_DEBUG_ONLY(assert_valid(true);)
    _GLIBCXX_DEBUG_ASSERT(!base_type::empty());

  base_type::bubble_to_top(it.m_p_nd);

  remove_parentless_node(it.m_p_nd);

  base_type::actual_erase_node(it.m_p_nd);

  m_p_max = NULL;

  _GLIBCXX_DEBUG_ONLY(assert_valid(true);)
    }

PB_DS_CLASS_T_DEC
template<typename Pred>
typename PB_DS_CLASS_C_DEC::size_type
PB_DS_CLASS_C_DEC::
erase_if(Pred pred)
{
  _GLIBCXX_DEBUG_ONLY(assert_valid(true);)

    if (base_type::empty())
      {
        _GLIBCXX_DEBUG_ONLY(assert_valid(true);)

	  return 0;
      }

  base_type::to_linked_list();

  node_pointer p_out = base_type::prune(pred);

  size_type ersd = 0;

  while (p_out != NULL)
    {
      ++ersd;

      node_pointer p_next = p_out->m_p_next_sibling;

      base_type::actual_erase_node(p_out);

      p_out = p_next;
    }

  node_pointer p_cur = base_type::m_p_root;

  base_type::m_p_root = NULL;

  while (p_cur != NULL)
    {
      node_pointer p_next = p_cur->m_p_next_sibling;

      p_cur->m_p_l_child = p_cur->m_p_prev_or_parent = NULL;

      p_cur->m_metadata = 0;

      p_cur->m_p_next_sibling = base_type::m_p_root;

      if (base_type::m_p_root != NULL)
	base_type::m_p_root->m_p_prev_or_parent = p_cur;

      base_type::m_p_root = p_cur;

      base_type::m_p_root = fix(base_type::m_p_root);

      p_cur = p_next;
    }

  m_p_max = NULL;

  _GLIBCXX_DEBUG_ONLY(assert_valid(true);)

    return ersd;
}

