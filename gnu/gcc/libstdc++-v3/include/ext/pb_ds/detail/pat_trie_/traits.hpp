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
 * Contains an implementation class for pat_trie_.
 */

#ifndef PB_DS_PAT_TRIE_NODE_AND_IT_TRAITS_HPP
#define PB_DS_PAT_TRIE_NODE_AND_IT_TRAITS_HPP

#include <ext/pb_ds/detail/pat_trie_/node_base.hpp>
#include <ext/pb_ds/detail/pat_trie_/head.hpp>
#include <ext/pb_ds/detail/pat_trie_/leaf.hpp>
#include <ext/pb_ds/detail/pat_trie_/internal_node.hpp>
#include <ext/pb_ds/detail/pat_trie_/point_iterators.hpp>
#include <ext/pb_ds/detail/pat_trie_/node_iterators.hpp>
#include <ext/pb_ds/detail/pat_trie_/synth_e_access_traits.hpp>

namespace pb_ds
{
  namespace detail
  {

    template<typename Key,
	     typename Mapped,
	     class E_Access_Traits,
	     template<typename Const_Node_Iterator,
		      class Node_Iterator,
		      class Cmp_Fn_,
		      class Allocator_>
    class Node_Update,
	     class Allocator>
    struct trie_traits<
      Key,
      Mapped,
      E_Access_Traits,
      Node_Update,
      pat_trie_tag,
      Allocator>
    {
    private:
      typedef types_traits< Key, Mapped, Allocator, false> type_traits;

    public:
      typedef
      typename trie_node_metadata_selector<
      Key,
      Mapped,
      E_Access_Traits,
      Node_Update,
      Allocator>::type
      metadata_type;

      typedef E_Access_Traits e_access_traits;

      typedef
      synth_e_access_traits<
	type_traits,
	false,
	e_access_traits>
      synth_e_access_traits;

      typedef
      pat_trie_node_base<
	type_traits,
	synth_e_access_traits,
	metadata_type,
	Allocator>
      node;

      typedef
      pat_trie_leaf<
	type_traits,
	synth_e_access_traits,
	metadata_type,
	Allocator>
      leaf;

      typedef
      pat_trie_head<
	type_traits,
	synth_e_access_traits,
	metadata_type,
	Allocator>
      head;

      typedef
      pat_trie_internal_node<
	type_traits,
	synth_e_access_traits,
	metadata_type,
	Allocator>
      internal_node;

      typedef
      pat_trie_const_it_<
	type_traits,
	node,
	leaf,
	head,
	internal_node,
	true,
	Allocator>
      const_iterator;

      typedef
      pat_trie_it_<
	type_traits,
	node,
	leaf,
	head,
	internal_node,
	true,
	Allocator>
      iterator;

      typedef
      pat_trie_const_it_<
	type_traits,
	node,
	leaf,
	head,
	internal_node,
	false,
	Allocator>
      const_reverse_iterator;

      typedef
      pat_trie_it_<
	type_traits,
	node,
	leaf,
	head,
	internal_node,
	false,
	Allocator>
      reverse_iterator;

      typedef
      pat_trie_const_node_it_<
	node,
	leaf,
	head,
	internal_node,
	const_iterator,
	iterator,
	synth_e_access_traits,
	Allocator>
      const_node_iterator;

      typedef
      pat_trie_node_it_<
	node,
	leaf,
	head,
	internal_node,
	const_iterator,
	iterator,
	synth_e_access_traits,
	Allocator>
      node_iterator;

      typedef
      Node_Update<
	const_node_iterator,
	node_iterator,
	E_Access_Traits,
	Allocator>
      node_update;

      typedef
      pb_ds::null_trie_node_update<
	const_node_iterator,
	node_iterator,
	E_Access_Traits,
	Allocator>* 
      null_node_update_pointer;
    };

    template<typename Key,
	     class E_Access_Traits,
	     template<typename Const_Node_Iterator,
		      class Node_Iterator,
		      class Cmp_Fn_,
		      class Allocator_>
    class Node_Update,
	     class Allocator>
    struct trie_traits<
      Key,
      null_mapped_type,
      E_Access_Traits,
      Node_Update,
      pat_trie_tag,
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
      typedef
      typename trie_node_metadata_selector<
      Key,
      null_mapped_type,
      E_Access_Traits,
      Node_Update,
      Allocator>::type
      metadata_type;

      typedef E_Access_Traits e_access_traits;

      typedef
      synth_e_access_traits<
	type_traits,
	true,
	e_access_traits>
      synth_e_access_traits;

      typedef
      pat_trie_node_base<
	type_traits,
	synth_e_access_traits,
	metadata_type,
	Allocator>
      node;

      typedef
      pat_trie_leaf<
	type_traits,
	synth_e_access_traits,
	metadata_type,
	Allocator>
      leaf;

      typedef
      pat_trie_head<
	type_traits,
	synth_e_access_traits,
	metadata_type,
	Allocator>
      head;

      typedef
      pat_trie_internal_node<
	type_traits,
	synth_e_access_traits,
	metadata_type,
	Allocator>
      internal_node;

      typedef
      pat_trie_const_it_<
	type_traits,
	node,
	leaf,
	head,
	internal_node,
	true,
	Allocator>
      const_iterator;

      typedef const_iterator iterator;

      typedef
      pat_trie_const_it_<
	type_traits,
	node,
	leaf,
	head,
	internal_node,
	false,
	Allocator>
      const_reverse_iterator;

      typedef const_reverse_iterator reverse_iterator;

      typedef
      pat_trie_const_node_it_<
	node,
	leaf,
	head,
	internal_node,
	const_iterator,
	iterator,
	synth_e_access_traits,
	Allocator>
      const_node_iterator;

      typedef const_node_iterator node_iterator;

      typedef
      Node_Update<
	const_node_iterator,
	node_iterator,
	E_Access_Traits,
	Allocator>
      node_update;

      typedef
      pb_ds::null_trie_node_update<
	const_node_iterator,
	const_node_iterator,
	E_Access_Traits,
	Allocator>* 
      null_node_update_pointer;
    };

  } // namespace detail
} // namespace pb_ds

#endif // #ifndef PB_DS_PAT_TRIE_NODE_AND_IT_TRAITS_HPP

