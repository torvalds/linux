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
 * @file find_fn_imps.hpp
 * Contains an implementation class for bin_search_tree_.
 */

PB_DS_CLASS_T_DEC
inline typename PB_DS_CLASS_C_DEC::point_iterator
PB_DS_CLASS_C_DEC::
find(const_key_reference r_key)
{
  _GLIBCXX_DEBUG_ONLY(assert_valid();)
  node_pointer p_nd = find_imp(r_key);

  if (p_nd == NULL || p_nd->m_type != pat_trie_leaf_node_type)
    {
      _GLIBCXX_DEBUG_ONLY(map_debug_base::check_key_does_not_exist(r_key);)
      return end();
    }

  if (synth_e_access_traits::equal_keys(PB_DS_V2F(static_cast<leaf_pointer>(p_nd)->value()), r_key))
    {
      _GLIBCXX_DEBUG_ONLY(map_debug_base::check_key_exists(r_key));
      return iterator(p_nd);
    }

  _GLIBCXX_DEBUG_ONLY(map_debug_base::check_key_does_not_exist(r_key);)
  return end();
}

PB_DS_CLASS_T_DEC
inline typename PB_DS_CLASS_C_DEC::const_point_iterator
PB_DS_CLASS_C_DEC::
find(const_key_reference r_key) const
{
  _GLIBCXX_DEBUG_ONLY(assert_valid();)

  const_node_pointer p_nd = const_cast<PB_DS_CLASS_C_DEC* >(this)->find_imp(r_key);

  if (p_nd == NULL || p_nd->m_type != pat_trie_leaf_node_type)
    {
      _GLIBCXX_DEBUG_ONLY(map_debug_base::check_key_does_not_exist(r_key);)
      return end();
    }

  if (synth_e_access_traits::equal_keys(PB_DS_V2F(static_cast<const_leaf_pointer>(p_nd)->value()), r_key))
    {
      _GLIBCXX_DEBUG_ONLY(map_debug_base::check_key_exists(r_key));
      return const_iterator(const_cast<node_pointer>(p_nd));
    }

  _GLIBCXX_DEBUG_ONLY(map_debug_base::check_key_does_not_exist(r_key);)
  return end();
}

PB_DS_CLASS_T_DEC
inline typename PB_DS_CLASS_C_DEC::node_pointer
PB_DS_CLASS_C_DEC::
find_imp(const_key_reference r_key)
{
  if (empty())
    return (NULL);

  typename synth_e_access_traits::const_iterator b_it =
    synth_e_access_traits::begin(r_key);
  typename synth_e_access_traits::const_iterator e_it =
    synth_e_access_traits::end(r_key);

  node_pointer p_nd = m_p_head->m_p_parent;
  _GLIBCXX_DEBUG_ASSERT(p_nd != NULL);

  while (p_nd->m_type != pat_trie_leaf_node_type)
    {
      _GLIBCXX_DEBUG_ASSERT(p_nd->m_type == pat_trie_internal_node_type);
      node_pointer p_next_nd = static_cast<internal_node_pointer>(p_nd)->get_child_node(b_it,  e_it,  this);

      if (p_next_nd == NULL)
	return p_nd;
      p_nd = p_next_nd;
    }
  return p_nd;
}

PB_DS_CLASS_T_DEC
inline typename PB_DS_CLASS_C_DEC::node_pointer
PB_DS_CLASS_C_DEC::
lower_bound_imp(const_key_reference r_key)
{
  if (empty())
    return (m_p_head);

  node_pointer p_nd = m_p_head->m_p_parent;
  _GLIBCXX_DEBUG_ASSERT(p_nd != NULL);

  typename PB_DS_CLASS_C_DEC::const_e_iterator b_it =
    synth_e_access_traits::begin(r_key);

  typename PB_DS_CLASS_C_DEC::const_e_iterator e_it =
    synth_e_access_traits::end(r_key);

  size_type checked_ind = 0;
  while (true)
    {
      if (p_nd->m_type == pat_trie_leaf_node_type)
        {
	  if (!synth_e_access_traits::cmp_keys(PB_DS_V2F(static_cast<const_leaf_pointer>(p_nd)->value()), r_key))
	    return p_nd;
	  iterator it(p_nd);
	  ++it;
	  return it.m_p_nd;
        }

      _GLIBCXX_DEBUG_ASSERT(p_nd->m_type == pat_trie_internal_node_type);
      const size_type new_checked_ind =
	static_cast<internal_node_pointer>(p_nd)->get_e_ind();

      p_nd =
	static_cast<internal_node_pointer>(p_nd)->get_lower_bound_child_node(                b_it, e_it, checked_ind, this);
      checked_ind = new_checked_ind;
    }
}

PB_DS_CLASS_T_DEC
inline typename PB_DS_CLASS_C_DEC::point_iterator
PB_DS_CLASS_C_DEC::
lower_bound(const_key_reference r_key)
{ return point_iterator(lower_bound_imp(r_key)); }

PB_DS_CLASS_T_DEC
inline typename PB_DS_CLASS_C_DEC::const_point_iterator
PB_DS_CLASS_C_DEC::
lower_bound(const_key_reference r_key) const
{
  return const_point_iterator(const_cast<PB_DS_CLASS_C_DEC* >(this)->lower_bound_imp(r_key));
}

PB_DS_CLASS_T_DEC
inline typename PB_DS_CLASS_C_DEC::point_iterator
PB_DS_CLASS_C_DEC::
upper_bound(const_key_reference r_key)
{
  point_iterator l_bound_it = lower_bound(r_key);

  _GLIBCXX_DEBUG_ASSERT(l_bound_it == end() ||
		   !synth_e_access_traits::cmp_keys(PB_DS_V2F(*l_bound_it),
						    r_key));

  if (l_bound_it == end() ||
      synth_e_access_traits::cmp_keys(r_key, PB_DS_V2F(*l_bound_it)))
    return l_bound_it;

  return ++l_bound_it;
}

PB_DS_CLASS_T_DEC
inline typename PB_DS_CLASS_C_DEC::const_point_iterator
PB_DS_CLASS_C_DEC::
upper_bound(const_key_reference r_key) const
{
  const_point_iterator l_bound_it = lower_bound(r_key);

  _GLIBCXX_DEBUG_ASSERT(l_bound_it == end() ||
		   !synth_e_access_traits::cmp_keys(PB_DS_V2F(*l_bound_it),
						    r_key));

  if (l_bound_it == end() ||
      synth_e_access_traits::cmp_keys(r_key, PB_DS_V2F(*l_bound_it)))
    return l_bound_it;
  return ++l_bound_it;
}

PB_DS_CLASS_T_DEC
inline typename PB_DS_CLASS_C_DEC::const_e_iterator
PB_DS_CLASS_C_DEC::
pref_begin(const_node_pointer p_nd)
{
  if (p_nd->m_type == pat_trie_leaf_node_type)
    return (synth_e_access_traits::begin(PB_DS_V2F(static_cast<const_leaf_pointer>(p_nd)->value())));

  _GLIBCXX_DEBUG_ASSERT(p_nd->m_type == pat_trie_internal_node_type);
  return static_cast<const_internal_node_pointer>(p_nd)->pref_b_it();
}

PB_DS_CLASS_T_DEC
inline typename PB_DS_CLASS_C_DEC::const_e_iterator
PB_DS_CLASS_C_DEC::
pref_end(const_node_pointer p_nd)
{
  if (p_nd->m_type == pat_trie_leaf_node_type)
    return (synth_e_access_traits::end(PB_DS_V2F(static_cast<const_leaf_pointer>(p_nd)->value())));

  _GLIBCXX_DEBUG_ASSERT(p_nd->m_type == pat_trie_internal_node_type);
  return static_cast<const_internal_node_pointer>(p_nd)->pref_e_it();
}

PB_DS_CLASS_T_DEC
inline typename PB_DS_CLASS_C_DEC::const_leaf_pointer
PB_DS_CLASS_C_DEC::
leftmost_descendant(const_node_pointer p_nd)
{
  if (p_nd->m_type == pat_trie_leaf_node_type)
    return static_cast<const_leaf_pointer>(p_nd);
  return static_cast<const_internal_node_pointer>(p_nd)->leftmost_descendant();
}

PB_DS_CLASS_T_DEC
inline typename PB_DS_CLASS_C_DEC::leaf_pointer
PB_DS_CLASS_C_DEC::
leftmost_descendant(node_pointer p_nd)
{
  if (p_nd->m_type == pat_trie_leaf_node_type)
    return static_cast<leaf_pointer>(p_nd);
  return static_cast<internal_node_pointer>(p_nd)->leftmost_descendant();
}

PB_DS_CLASS_T_DEC
inline typename PB_DS_CLASS_C_DEC::const_leaf_pointer
PB_DS_CLASS_C_DEC::
rightmost_descendant(const_node_pointer p_nd)
{
  if (p_nd->m_type == pat_trie_leaf_node_type)
    return static_cast<const_leaf_pointer>(p_nd);
  return static_cast<const_internal_node_pointer>(p_nd)->rightmost_descendant();
}

PB_DS_CLASS_T_DEC
inline typename PB_DS_CLASS_C_DEC::leaf_pointer
PB_DS_CLASS_C_DEC::
rightmost_descendant(node_pointer p_nd)
{
  if (p_nd->m_type == pat_trie_leaf_node_type)
    return static_cast<leaf_pointer>(p_nd);
  return static_cast<internal_node_pointer>(p_nd)->rightmost_descendant();
}

