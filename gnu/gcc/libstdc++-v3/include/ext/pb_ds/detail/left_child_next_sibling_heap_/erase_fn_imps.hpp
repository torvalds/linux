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
 * Contains an implementation class for left_child_next_sibling_heap_.
 */

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
clear()
{
  clear_imp(m_p_root);
  _GLIBCXX_DEBUG_ASSERT(m_size == 0);
  m_p_root = NULL;
}

PB_DS_CLASS_T_DEC
inline void
PB_DS_CLASS_C_DEC::
actual_erase_node(node_pointer p_nd)
{
  _GLIBCXX_DEBUG_ASSERT(m_size > 0);
  --m_size;
  p_nd->~node();
  s_node_allocator.deallocate(p_nd, 1);
}

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
clear_imp(node_pointer p_nd)
{
  while (p_nd != NULL)
    {
      clear_imp(p_nd->m_p_l_child);
      node_pointer p_next = p_nd->m_p_next_sibling;
      actual_erase_node(p_nd);
      p_nd = p_next;
    }
}

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
to_linked_list()
{
  _GLIBCXX_DEBUG_ONLY(assert_valid();)
  node_pointer p_cur = m_p_root;
  while (p_cur != NULL)
    if (p_cur->m_p_l_child != NULL)
      {
	node_pointer p_child_next = p_cur->m_p_l_child->m_p_next_sibling;
	p_cur->m_p_l_child->m_p_next_sibling = p_cur->m_p_next_sibling;
	p_cur->m_p_next_sibling = p_cur->m_p_l_child;
	p_cur->m_p_l_child = p_child_next;
      }
    else
      p_cur = p_cur->m_p_next_sibling;

#ifdef _GLIBCXX_DEBUG
  const_node_pointer p_counter = m_p_root;
  size_type count = 0;
  while (p_counter != NULL)
    {
      ++count;
      _GLIBCXX_DEBUG_ASSERT(p_counter->m_p_l_child == NULL);
      p_counter = p_counter->m_p_next_sibling;
    }
  _GLIBCXX_DEBUG_ASSERT(count == m_size);
#endif 
}

PB_DS_CLASS_T_DEC
template<typename Pred>
typename PB_DS_CLASS_C_DEC::node_pointer
PB_DS_CLASS_C_DEC::
prune(Pred pred)
{
  node_pointer p_cur = m_p_root;
  m_p_root = NULL;
  node_pointer p_out = NULL;
  while (p_cur != NULL)
    {
      node_pointer p_next = p_cur->m_p_next_sibling;
      if (pred(p_cur->m_value))
        {
	  p_cur->m_p_next_sibling = p_out;
	  if (p_out != NULL)
	    p_out->m_p_prev_or_parent = p_cur;
	  p_out = p_cur;
        }
      else
        {
	  p_cur->m_p_next_sibling = m_p_root;
	  if (m_p_root != NULL)
	    m_p_root->m_p_prev_or_parent = p_cur;
	  m_p_root = p_cur;
        }
      p_cur = p_next;
    }
  return p_out;
}

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
bubble_to_top(node_pointer p_nd)
{
  node_pointer p_parent = parent(p_nd);
  while (p_parent != NULL)
    {
      swap_with_parent(p_nd, p_parent);
      p_parent = parent(p_nd);
    }
}

