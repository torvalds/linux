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
 * @file trie_policy_base.hpp
 * Contains an implementation of trie_policy_base.
 */

#ifndef PB_DS_TRIE_POLICY_BASE_HPP
#define PB_DS_TRIE_POLICY_BASE_HPP

#include <ext/pb_ds/detail/basic_tree_policy/basic_tree_policy_base.hpp>

namespace pb_ds
{
  namespace detail
  {

#define PB_DS_CLASS_T_DEC						\
    template<								\
						class Const_Node_Iterator, \
						class Node_Iterator,	\
						class E_Access_Traits,	\
						typename Allocator>

#define PB_DS_CLASS_C_DEC						\
    trie_policy_base<							\
						Const_Node_Iterator,	\
						Node_Iterator,		\
						E_Access_Traits,	\
						Allocator>

#define PB_DS_BASE_C_DEC						\
    basic_tree_policy_base<				\
								Const_Node_Iterator, \
								Node_Iterator, \
								Allocator>

    template<typename Const_Node_Iterator,
	     class Node_Iterator,
	     class E_Access_Traits,
	     class Allocator>
    class trie_policy_base : public PB_DS_BASE_C_DEC
    {

    public:

      typedef E_Access_Traits e_access_traits;

      typedef Allocator allocator;

      typedef typename allocator::size_type size_type;

      typedef null_node_metadata metadata_type;

      typedef Const_Node_Iterator const_node_iterator;

      typedef Node_Iterator node_iterator;

      typedef typename const_node_iterator::value_type const_iterator;

      typedef typename node_iterator::value_type iterator;

    public:

      typedef typename PB_DS_BASE_C_DEC::key_type key_type;

      typedef
      typename PB_DS_BASE_C_DEC::const_key_reference
      const_key_reference;

    protected:

      virtual const_iterator
      end() const = 0;

      virtual iterator
      end() = 0;

      virtual const_node_iterator
      node_begin() const = 0;

      virtual node_iterator
      node_begin() = 0;

      virtual const_node_iterator
      node_end() const = 0;

      virtual node_iterator
      node_end() = 0;

      virtual const e_access_traits& 
      get_e_access_traits() const = 0;

    private:
      typedef
      std::pair<
      typename e_access_traits::const_iterator,
      typename e_access_traits::const_iterator>
      prefix_range_t;

      typedef PB_DS_BASE_C_DEC base_type;

    protected:
      static size_type
      common_prefix_len(node_iterator nd_it, typename e_access_traits::const_iterator b_r, typename e_access_traits::const_iterator e_r, const e_access_traits& r_traits);

      static iterator
      leftmost_it(node_iterator nd_it);

      static iterator
      rightmost_it(node_iterator nd_it);

      static bool
      less(typename e_access_traits::const_iterator b_l, typename e_access_traits::const_iterator e_l, typename e_access_traits::const_iterator b_r, typename e_access_traits::const_iterator e_r, const e_access_traits& r_traits);
    };

    PB_DS_CLASS_T_DEC
    typename PB_DS_CLASS_C_DEC::size_type
    PB_DS_CLASS_C_DEC::
    common_prefix_len(node_iterator nd_it, typename e_access_traits::const_iterator b_r, typename e_access_traits::const_iterator e_r, const e_access_traits& r_traits)
    {
      prefix_range_t pref_range = nd_it.valid_prefix();

      typename e_access_traits::const_iterator b_l = pref_range.first;
      typename e_access_traits::const_iterator e_l = pref_range.second;

      const size_type range_length_l =
	std::distance(b_l, e_l);

      const size_type range_length_r =
	std::distance(b_r, e_r);

      if (range_length_r < range_length_l)
	{
	  std::swap(b_l, b_r);

	  std::swap(e_l, e_r);
	}

      size_type ret = 0;

      while (b_l != e_l)
	{
	  if (r_traits.e_pos(*b_l) != r_traits.e_pos(*b_r))
	    return (ret);

	  ++ret;

	  ++b_l;

	  ++b_r;
	}

      return (ret);
    }

    PB_DS_CLASS_T_DEC
    typename PB_DS_CLASS_C_DEC::iterator
    PB_DS_CLASS_C_DEC::
    leftmost_it(node_iterator nd_it)
    {
      if (nd_it.num_children() == 0)
	return (*nd_it);

      return (leftmost_it(nd_it.get_child(0)));
    }

    PB_DS_CLASS_T_DEC
    typename PB_DS_CLASS_C_DEC::iterator
    PB_DS_CLASS_C_DEC::
    rightmost_it(node_iterator nd_it)
    {
      const size_type num_children = nd_it.num_children();

      if (num_children == 0)
	return (*nd_it);

      return (rightmost_it(nd_it.get_child(num_children - 1)));
    }

    PB_DS_CLASS_T_DEC
    bool
    PB_DS_CLASS_C_DEC::
    less(typename e_access_traits::const_iterator b_l, typename e_access_traits::const_iterator e_l, typename e_access_traits::const_iterator b_r, typename e_access_traits::const_iterator e_r, const e_access_traits& r_traits)
    {
      while (b_l != e_l)
	{
	  if (b_r == e_r)
	    return (false);

	  size_type l_pos =
	    r_traits.e_pos(*b_l);
	  size_type r_pos =
	    r_traits.e_pos(*b_r);

	  if (l_pos != r_pos)
	    return (l_pos < r_pos);

	  ++b_l;
	  ++b_r;
	}

      return (b_r != e_r);
    }

#undef PB_DS_CLASS_T_DEC

#undef PB_DS_CLASS_C_DEC

#undef PB_DS_BASE_C_DEC

  } // namespace detail
} // namespace pb_ds

#endif // #ifndef PB_DS_TRIE_POLICY_BASE_HPP

