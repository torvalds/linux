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
 * Contains an implementation for thin_heap_.
 */

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
pop()
{
  _GLIBCXX_DEBUG_ONLY(assert_valid();)
    _GLIBCXX_DEBUG_ASSERT(!base_type::empty());

  _GLIBCXX_DEBUG_ASSERT(m_p_max != NULL);

  node_pointer p_nd = m_p_max;

  remove_max_node();

  base_type::actual_erase_node(p_nd);

  _GLIBCXX_DEBUG_ONLY(assert_valid();)
    }

PB_DS_CLASS_T_DEC
inline void
PB_DS_CLASS_C_DEC::
remove_max_node()
{
  to_aux_except_max();

  make_from_aux();
}

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
to_aux_except_max()
{
  node_pointer p_add = base_type::m_p_root;

  while (p_add != m_p_max)
    {
      node_pointer p_next_add = p_add->m_p_next_sibling;

      add_to_aux(p_add);

      p_add = p_next_add;
    }

  p_add = m_p_max->m_p_l_child;

  while (p_add != NULL)
    {
      node_pointer p_next_add = p_add->m_p_next_sibling;

      p_add->m_metadata = p_add->m_p_l_child == NULL?
	0 :
	p_add->m_p_l_child->m_metadata + 1;

      add_to_aux(p_add);

      p_add = p_next_add;
    }

  p_add = m_p_max->m_p_next_sibling;

  while (p_add != NULL)
    {
      node_pointer p_next_add = p_add->m_p_next_sibling;

      add_to_aux(p_add);

      p_add = p_next_add;
    }
}

PB_DS_CLASS_T_DEC
inline void
PB_DS_CLASS_C_DEC::
add_to_aux(node_pointer p_nd)
{
  size_type r = p_nd->m_metadata;

  while (m_a_aux[r] != NULL)
    {
      _GLIBCXX_DEBUG_ASSERT(p_nd->m_metadata < rank_bound());

      if (Cmp_Fn::operator()(m_a_aux[r]->m_value, p_nd->m_value))
	make_child_of(m_a_aux[r], p_nd);
      else
        {
	  make_child_of(p_nd, m_a_aux[r]);

	  p_nd = m_a_aux[r];
        }

      m_a_aux[r] = NULL;

      ++r;
    }

  _GLIBCXX_DEBUG_ASSERT(p_nd->m_metadata < rank_bound());

  m_a_aux[r] = p_nd;
}

PB_DS_CLASS_T_DEC
inline void
PB_DS_CLASS_C_DEC::
make_child_of(node_pointer p_nd, node_pointer p_new_parent)
{
  _GLIBCXX_DEBUG_ASSERT(p_nd->m_metadata == p_new_parent->m_metadata);
  _GLIBCXX_DEBUG_ASSERT(m_a_aux[p_nd->m_metadata] == p_nd ||
		   m_a_aux[p_nd->m_metadata] == p_new_parent);

  ++p_new_parent->m_metadata;

  base_type::make_child_of(p_nd, p_new_parent);
}

PB_DS_CLASS_T_DEC
inline void
PB_DS_CLASS_C_DEC::
make_from_aux()
{
  base_type::m_p_root = m_p_max = NULL;

  const size_type rnk_bnd = rank_bound();

  size_type i = 0;

  while (i < rnk_bnd)
    {
      if (m_a_aux[i] != NULL)
        {
	  make_root_and_link(m_a_aux[i]);

	  m_a_aux[i] = NULL;
        }

      ++i;
    }

  _GLIBCXX_DEBUG_ONLY(assert_aux_null();)
    }

PB_DS_CLASS_T_DEC
inline void
PB_DS_CLASS_C_DEC::
remove_node(node_pointer p_nd)
{
  node_pointer p_parent = p_nd;
  while (base_type::parent(p_parent) != NULL)
    p_parent = base_type::parent(p_parent);

  base_type::bubble_to_top(p_nd);

  m_p_max = p_nd;

  node_pointer p_fix = base_type::m_p_root;
  while (p_fix != NULL&&  p_fix->m_p_next_sibling != p_parent)
    p_fix = p_fix->m_p_next_sibling;

  if (p_fix != NULL)
    p_fix->m_p_next_sibling = p_nd;

  remove_max_node();
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
  _GLIBCXX_DEBUG_ONLY(assert_valid();)
    _GLIBCXX_DEBUG_ASSERT(!base_type::empty());

  node_pointer p_nd = it.m_p_nd;

  remove_node(p_nd);

  base_type::actual_erase_node(p_nd);

  _GLIBCXX_DEBUG_ONLY(assert_valid();)
    }

PB_DS_CLASS_T_DEC
template<typename Pred>
typename PB_DS_CLASS_C_DEC::size_type
PB_DS_CLASS_C_DEC::
erase_if(Pred pred)
{
  _GLIBCXX_DEBUG_ONLY(assert_valid();)

    if (base_type::empty())
      {
        _GLIBCXX_DEBUG_ONLY(assert_valid();)

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

  m_p_max = base_type::m_p_root = NULL;

  while (p_cur != NULL)
    {
      node_pointer p_next = p_cur->m_p_next_sibling;

      make_root_and_link(p_cur);

      p_cur = p_next;
    }

  _GLIBCXX_DEBUG_ONLY(assert_valid();)

    return ersd;
}

PB_DS_CLASS_T_DEC
inline typename PB_DS_CLASS_C_DEC::size_type
PB_DS_CLASS_C_DEC::
rank_bound()
{
  const std::size_t* const p_upper =
    std::upper_bound(            g_a_rank_bounds, g_a_rank_bounds + num_distinct_rank_bounds, base_type::m_size);

  if (p_upper == g_a_rank_bounds + num_distinct_rank_bounds)
    return max_rank;

  return (p_upper - g_a_rank_bounds);
}

