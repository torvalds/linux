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
 * @file traits.hpp
 * Contains an implementation for bin_search_tree_.
 */

#ifndef PB_DS_BIN_SEARCH_TREE_NODE_AND_IT_TRAITS_HPP
#define PB_DS_BIN_SEARCH_TREE_NODE_AND_IT_TRAITS_HPP

#include <ext/pb_ds/detail/bin_search_tree_/point_iterators.hpp>
#include <ext/pb_ds/detail/bin_search_tree_/node_iterators.hpp>

namespace pb_ds
{
  namespace detail
  {

    template<typename Key,
	     typename Mapped,
	     class Cmp_Fn,
	     template<typename Const_Node_Iterator,
		      class Node_Iterator,
		      class Cmp_Fn,
		      class Allocator>
    class Node_Update,
	     class Node,
	     class Allocator>
    struct bin_search_tree_traits
    {
    private:
      typedef
      types_traits<
      Key,
      Mapped,
      Allocator,
      false>
      type_traits;

    public:
      typedef Node node;

      typedef
      bin_search_tree_const_it_<
	typename Allocator::template rebind<
	node>::other::pointer,
	typename type_traits::value_type,
	typename type_traits::pointer,
	typename type_traits::const_pointer,
	typename type_traits::reference,
	typename type_traits::const_reference,
	true,
	Allocator>
      const_point_iterator;

      typedef
      bin_search_tree_it_<
	typename Allocator::template rebind<
	node>::other::pointer,
	typename type_traits::value_type,
	typename type_traits::pointer,
	typename type_traits::const_pointer,
	typename type_traits::reference,
	typename type_traits::const_reference,
	true,
	Allocator>
      point_iterator;

      typedef
      bin_search_tree_const_it_<
	typename Allocator::template rebind<
	node>::other::pointer,
	typename type_traits::value_type,
	typename type_traits::pointer,
	typename type_traits::const_pointer,
	typename type_traits::reference,
	typename type_traits::const_reference,
	false,
	Allocator>
      const_reverse_iterator;

      typedef
      bin_search_tree_it_<
	typename Allocator::template rebind<
	node>::other::pointer,
	typename type_traits::value_type,
	typename type_traits::pointer,
	typename type_traits::const_pointer,
	typename type_traits::reference,
	typename type_traits::const_reference,
	false,
	Allocator>
      reverse_iterator;

      typedef
      bin_search_tree_const_node_it_<
	Node,
	const_point_iterator,
	point_iterator,
	Allocator>
      const_node_iterator;

      typedef
      bin_search_tree_node_it_<
	Node,
	const_point_iterator,
	point_iterator,
	Allocator>
      node_iterator;

      typedef
      Node_Update<
	const_node_iterator,
	node_iterator,
	Cmp_Fn,
	Allocator>
      node_update;

      typedef
      pb_ds::null_tree_node_update<
	const_node_iterator,
	node_iterator,
	Cmp_Fn,
	Allocator>* 
      null_node_update_pointer;
    };

    template<typename Key,
	     class Cmp_Fn,
	     template<typename Const_Node_Iterator,
		      class Node_Iterator,
		      class Cmp_Fn,
		      class Allocator>
    class Node_Update,
	     class Node,
	     class Allocator>
    struct bin_search_tree_traits<
      Key,
      null_mapped_type,
      Cmp_Fn,
      Node_Update,
      Node,
      Allocator>
    {
    private:
      typedef
      types_traits<
      Key,
      null_mapped_type,
      Allocator,
      false>
      type_traits;

    public:
      typedef Node node;

      typedef
      bin_search_tree_const_it_<
	typename Allocator::template rebind<
	node>::other::pointer,
	typename type_traits::value_type,
	typename type_traits::pointer,
	typename type_traits::const_pointer,
	typename type_traits::reference,
	typename type_traits::const_reference,
	true,
	Allocator>
      const_point_iterator;

      typedef const_point_iterator point_iterator;

      typedef
      bin_search_tree_const_it_<
	typename Allocator::template rebind<
	node>::other::pointer,
	typename type_traits::value_type,
	typename type_traits::pointer,
	typename type_traits::const_pointer,
	typename type_traits::reference,
	typename type_traits::const_reference,
	false,
	Allocator>
      const_reverse_iterator;

      typedef const_reverse_iterator reverse_iterator;

      typedef
      bin_search_tree_const_node_it_<
	Node,
	const_point_iterator,
	point_iterator,
	Allocator>
      const_node_iterator;

      typedef const_node_iterator node_iterator;

      typedef
      Node_Update<
	const_node_iterator,
	node_iterator,
	Cmp_Fn,
	Allocator>
      node_update;

      typedef
      pb_ds::null_tree_node_update<
	const_node_iterator,
	node_iterator,
	Cmp_Fn,
	Allocator>* 
      null_node_update_pointer;
    };

  } // namespace detail
} // namespace pb_ds

#endif // #ifndef PB_DS_BIN_SEARCH_TREE_NODE_AND_IT_TRAITS_HPP
