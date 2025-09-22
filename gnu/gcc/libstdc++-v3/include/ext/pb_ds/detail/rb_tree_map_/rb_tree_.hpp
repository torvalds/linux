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
 * @file rb_tree_.hpp
 * Contains an implementation for rb_tree_.
 */
/*
 * This implementation uses an idea from the SGI STL (using a "header" node
 *    which is needed for efficient iteration).
 */

#ifdef PB_DS_DATA_TRUE_INDICATOR
#ifndef PB_DS_BIN_SEARCH_TREE_HPP__DATA_TRUE_INDICATOR
#define PB_DS_BIN_SEARCH_TREE_HPP__DATA_TRUE_INDICATOR
#include <ext/pb_ds/detail/bin_search_tree_/bin_search_tree_.hpp>
#endif 
#endif 

#ifdef PB_DS_DATA_FALSE_INDICATOR
#ifndef PB_DS_BIN_SEARCH_TREE_HPP__DATA_FALSE_INDICATOR
#define PB_DS_BIN_SEARCH_TREE_HPP__DATA_FALSE_INDICATOR
#include <ext/pb_ds/detail/bin_search_tree_/bin_search_tree_.hpp>
#endif 
#endif 

#include <ext/pb_ds/detail/standard_policies.hpp>
#include <ext/pb_ds/detail/basic_types.hpp>
#include <utility>
#include <vector>
#include <assert.h>
#include <debug/debug.h>

namespace pb_ds
{
  namespace detail
  {
#define PB_DS_CLASS_T_DEC \
    template<typename Key, typename Mapped, typename Cmp_Fn, \
	     typename Node_And_It_Traits, typename Allocator>

#ifdef PB_DS_DATA_TRUE_INDICATOR
#define PB_DS_CLASS_NAME rb_tree_data_
#endif 

#ifdef PB_DS_DATA_TRUE_INDICATOR
#define PB_DS_BASE_CLASS_NAME bin_search_tree_data_
#endif 

#ifdef PB_DS_DATA_FALSE_INDICATOR
#define PB_DS_CLASS_NAME rb_tree_no_data_
#endif 

#ifdef PB_DS_DATA_FALSE_INDICATOR
#define PB_DS_BASE_CLASS_NAME bin_search_tree_no_data_
#endif 

#define PB_DS_CLASS_C_DEC \
    PB_DS_CLASS_NAME<Key, Mapped, Cmp_Fn, Node_And_It_Traits, Allocator>

#define PB_DS_BASE_C_DEC \
    PB_DS_BASE_CLASS_NAME<Key, Mapped, Cmp_Fn, Node_And_It_Traits, Allocator>

#ifdef PB_DS_DATA_TRUE_INDICATOR
#define PB_DS_V2F(X) (X).first
#define PB_DS_V2S(X) (X).second
#define PB_DS_EP2VP(X)& ((X)->m_value)
#endif 

#ifdef PB_DS_DATA_FALSE_INDICATOR
#define PB_DS_V2F(X) (X)
#define PB_DS_V2S(X) Mapped_Data()
#define PB_DS_EP2VP(X)& ((X)->m_value.first)
#endif 

    template<typename Key,
	     typename Mapped,
	     typename Cmp_Fn,
	     typename Node_And_It_Traits,
	     typename Allocator>
    class PB_DS_CLASS_NAME : public PB_DS_BASE_C_DEC
    {
    private:
      typedef PB_DS_BASE_C_DEC base_type;
      typedef typename base_type::node_pointer node_pointer;

    public:
      typedef Cmp_Fn cmp_fn;
      typedef Allocator allocator;
      typedef typename Allocator::size_type size_type;
      typedef typename Allocator::difference_type difference_type;
      typedef typename base_type::key_type key_type;
      typedef typename base_type::key_pointer key_pointer;
      typedef typename base_type::const_key_pointer const_key_pointer;
      typedef typename base_type::key_reference key_reference;
      typedef typename base_type::const_key_reference const_key_reference;
      typedef typename base_type::mapped_type mapped_type;
      typedef typename base_type::mapped_pointer mapped_pointer;
      typedef typename base_type::const_mapped_pointer const_mapped_pointer;
      typedef typename base_type::mapped_reference mapped_reference;
      typedef typename base_type::const_mapped_reference const_mapped_reference;
      typedef typename base_type::value_type value_type;
      typedef typename base_type::pointer pointer;
      typedef typename base_type::const_pointer const_pointer;
      typedef typename base_type::reference reference;
      typedef typename base_type::const_reference const_reference;
      typedef typename base_type::point_iterator point_iterator;
      typedef typename base_type::const_iterator const_point_iterator;
      typedef typename base_type::iterator iterator;
      typedef typename base_type::const_iterator const_iterator;
      typedef typename base_type::reverse_iterator reverse_iterator;
      typedef typename base_type::const_reverse_iterator const_reverse_iterator;
      typedef typename base_type::node_update node_update;


      PB_DS_CLASS_NAME();

      PB_DS_CLASS_NAME(const Cmp_Fn&);

      PB_DS_CLASS_NAME(const Cmp_Fn&, const node_update&);

      PB_DS_CLASS_NAME(const PB_DS_CLASS_C_DEC&);

      void
      swap(PB_DS_CLASS_C_DEC&);

      template<typename It>
      void
      copy_from_range(It, It);

      inline std::pair<point_iterator, bool>
      insert(const_reference);

      inline mapped_reference
      operator[](const_key_reference r_key)
      {
#ifdef PB_DS_DATA_TRUE_INDICATOR
	_GLIBCXX_DEBUG_ONLY(assert_valid();)
	std::pair<point_iterator, bool> ins_pair =
	base_type::insert_leaf(value_type(r_key, mapped_type()));

	if (ins_pair.second == true)
	  {
	    ins_pair.first.m_p_nd->m_red = true;
	    _GLIBCXX_DEBUG_ONLY(this->structure_only_assert_valid();)
	    insert_fixup(ins_pair.first.m_p_nd);
	  }
	_GLIBCXX_DEBUG_ONLY(assert_valid();)
	return ins_pair.first.m_p_nd->m_value.second;
#else 
	insert(r_key);
	return base_type::s_null_mapped;
#endif 
      }

      inline bool
      erase(const_key_reference);

      inline iterator
      erase(iterator);

      inline reverse_iterator
      erase(reverse_iterator);

      template<typename Pred>
      inline size_type
      erase_if(Pred);

      void
      join(PB_DS_CLASS_C_DEC&);

      void
      split(const_key_reference, PB_DS_CLASS_C_DEC&);

    protected:

    private:

#ifdef _GLIBCXX_DEBUG
      void
      assert_valid() const;

      size_type
      assert_node_consistent(const node_pointer) const;
#endif 

      inline static bool
      is_effectively_black(const node_pointer);

      void
      initialize();

      void
      insert_fixup(node_pointer);

      void
      erase_node(node_pointer);

      void
      remove_node(node_pointer);

      void
      remove_fixup(node_pointer, node_pointer);

      void
      split_imp(node_pointer, PB_DS_CLASS_C_DEC&);

      inline node_pointer
      split_min();

      std::pair<node_pointer, node_pointer>
      split_min_imp();

      void
      join_imp(node_pointer, node_pointer);

      std::pair<node_pointer, node_pointer>
      find_join_pos_right(node_pointer, size_type, size_type);

      std::pair<node_pointer, node_pointer>
      find_join_pos_left(node_pointer, size_type, size_type);

      inline size_type
      black_height(node_pointer);

      void
      split_at_node(node_pointer, PB_DS_CLASS_C_DEC&);
    };

#include <ext/pb_ds/detail/rb_tree_map_/constructors_destructor_fn_imps.hpp>
#include <ext/pb_ds/detail/rb_tree_map_/insert_fn_imps.hpp>
#include <ext/pb_ds/detail/rb_tree_map_/erase_fn_imps.hpp>
#include <ext/pb_ds/detail/rb_tree_map_/debug_fn_imps.hpp>
#include <ext/pb_ds/detail/rb_tree_map_/split_join_fn_imps.hpp>
#include <ext/pb_ds/detail/rb_tree_map_/info_fn_imps.hpp>

#undef PB_DS_CLASS_T_DEC
#undef PB_DS_CLASS_C_DEC
#undef PB_DS_CLASS_NAME
#undef PB_DS_BASE_CLASS_NAME
#undef PB_DS_BASE_C_DEC
#undef PB_DS_V2F
#undef PB_DS_EP2VP
#undef PB_DS_V2S

  } // namespace detail
} // namespace pb_ds

