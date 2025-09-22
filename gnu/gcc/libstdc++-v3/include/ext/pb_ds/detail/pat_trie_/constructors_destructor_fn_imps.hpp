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
 * @file constructors_destructor_fn_imps.hpp
 * Contains an implementation class for bin_search_tree_.
 */

PB_DS_CLASS_T_DEC
typename PB_DS_CLASS_C_DEC::head_allocator
PB_DS_CLASS_C_DEC::s_head_allocator;

PB_DS_CLASS_T_DEC
typename PB_DS_CLASS_C_DEC::internal_node_allocator
PB_DS_CLASS_C_DEC::s_internal_node_allocator;

PB_DS_CLASS_T_DEC
typename PB_DS_CLASS_C_DEC::leaf_allocator
PB_DS_CLASS_C_DEC::s_leaf_allocator;

PB_DS_CLASS_T_DEC
PB_DS_CLASS_C_DEC::
PB_DS_CLASS_NAME() :
  m_p_head(s_head_allocator.allocate(1)),
  m_size(0)
{
  initialize();
  _GLIBCXX_DEBUG_ONLY(assert_valid();)
}

PB_DS_CLASS_T_DEC
PB_DS_CLASS_C_DEC::
PB_DS_CLASS_NAME(const e_access_traits& r_e_access_traits) :
  synth_e_access_traits(r_e_access_traits),
  m_p_head(s_head_allocator.allocate(1)),
  m_size(0)
{
  initialize();
  _GLIBCXX_DEBUG_ONLY(assert_valid();)
}

PB_DS_CLASS_T_DEC
PB_DS_CLASS_C_DEC::
PB_DS_CLASS_NAME(const PB_DS_CLASS_C_DEC& other) :
#ifdef _GLIBCXX_DEBUG
  map_debug_base(other),
#endif 
  synth_e_access_traits(other),
  node_update(other),
  m_p_head(s_head_allocator.allocate(1)),
  m_size(0)
{
  initialize();
  m_size = other.m_size;
  _GLIBCXX_DEBUG_ONLY(other.assert_valid();)
    if (other.m_p_head->m_p_parent == NULL)
      {
        _GLIBCXX_DEBUG_ONLY(assert_valid();)
        return;
      }
  try
    {
      m_p_head->m_p_parent = recursive_copy_node(other.m_p_head->m_p_parent);
    }
  catch(...)
    {
      s_head_allocator.deallocate(m_p_head, 1);
      __throw_exception_again;
    }

  m_p_head->m_p_min = leftmost_descendant(m_p_head->m_p_parent);
  m_p_head->m_p_max = rightmost_descendant(m_p_head->m_p_parent);
  m_p_head->m_p_parent->m_p_parent = m_p_head;
  _GLIBCXX_DEBUG_ONLY(assert_valid();)
}

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
swap(PB_DS_CLASS_C_DEC& other)
{
  _GLIBCXX_DEBUG_ONLY(assert_valid();)
  _GLIBCXX_DEBUG_ONLY(other.assert_valid();)
  value_swap(other);
  std::swap((e_access_traits& )(*this), (e_access_traits& )other);
  _GLIBCXX_DEBUG_ONLY(assert_valid();)
  _GLIBCXX_DEBUG_ONLY(other.assert_valid();)
}

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
value_swap(PB_DS_CLASS_C_DEC& other)
{
  _GLIBCXX_DEBUG_ONLY(map_debug_base::swap(other);)
  std::swap(m_p_head, other.m_p_head);
  std::swap(m_size, other.m_size);
}

PB_DS_CLASS_T_DEC
PB_DS_CLASS_C_DEC::
~PB_DS_CLASS_NAME()
{
  clear();
  s_head_allocator.deallocate(m_p_head, 1);
}

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
initialize()
{
  new (m_p_head) head();
  m_p_head->m_p_parent = NULL;
  m_p_head->m_p_min = m_p_head;
  m_p_head->m_p_max = m_p_head;
  m_size = 0;
}

PB_DS_CLASS_T_DEC
template<typename It>
void
PB_DS_CLASS_C_DEC::
copy_from_range(It first_it, It last_it)
{
  while (first_it != last_it)
    insert(*(first_it++));
}

PB_DS_CLASS_T_DEC
typename PB_DS_CLASS_C_DEC::node_pointer
PB_DS_CLASS_C_DEC::
recursive_copy_node(const_node_pointer p_other_nd)
{
  _GLIBCXX_DEBUG_ASSERT(p_other_nd != NULL);
  if (p_other_nd->m_type == pat_trie_leaf_node_type)
    {
      const_leaf_pointer p_other_leaf = static_cast<const_leaf_pointer>(p_other_nd);

      leaf_pointer p_new_lf = s_leaf_allocator.allocate(1);
      cond_dealtor cond(p_new_lf);
      new (p_new_lf) leaf(p_other_leaf->value());
      apply_update(p_new_lf, (node_update* )this);
      cond.set_no_action_dtor();
      return (p_new_lf);
    }

  _GLIBCXX_DEBUG_ASSERT(p_other_nd->m_type == pat_trie_internal_node_type);
  node_pointer a_p_children[internal_node::arr_size];
  size_type child_i = 0;
  const_internal_node_pointer p_other_internal_nd =
    static_cast<const_internal_node_pointer>(p_other_nd);

  typename internal_node::const_iterator child_it =
    p_other_internal_nd->begin();

  internal_node_pointer p_ret;
  try
    {
      while (child_it != p_other_internal_nd->end())
	a_p_children[child_i++] = recursive_copy_node(*(child_it++));
      p_ret = s_internal_node_allocator.allocate(1);
    }
  catch(...)
    {
      while (child_i-- > 0)
	clear_imp(a_p_children[child_i]);
      __throw_exception_again;
    }

  new (p_ret) internal_node(p_other_internal_nd->get_e_ind(),
			    pref_begin(a_p_children[0]));

  --child_i;
  _GLIBCXX_DEBUG_ASSERT(child_i > 1);
  do
    p_ret->add_child(a_p_children[child_i], pref_begin(a_p_children[child_i]),
		     pref_end(a_p_children[child_i]), this);
  while (child_i-- > 0);
  apply_update(p_ret, (node_update* )this);
  return p_ret;
}
