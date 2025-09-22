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
 * @file left_child_next_sibling_heap_.hpp
 * Contains an implementation class for a basic heap.
 */

#ifndef PB_DS_LEFT_CHILD_NEXT_SIBLING_HEAP_HPP
#define PB_DS_LEFT_CHILD_NEXT_SIBLING_HEAP_HPP

/*
 * Based on CLRS.
 */

#include <iterator>
#include <ext/pb_ds/detail/cond_dealtor.hpp>
#include <ext/pb_ds/detail/type_utils.hpp>
#include <ext/pb_ds/detail/left_child_next_sibling_heap_/node.hpp>
#include <ext/pb_ds/detail/left_child_next_sibling_heap_/const_point_iterator.hpp>
#include <ext/pb_ds/detail/left_child_next_sibling_heap_/const_iterator.hpp>
#ifdef PB_DS_LC_NS_HEAP_TRACE_
#include <iostream>
#endif 
#include <debug/debug.h>

namespace pb_ds
{
  namespace detail
  {

#ifdef _GLIBCXX_DEBUG
#define PB_DS_CLASS_T_DEC						\
    template<								\
						typename Value_Type,	\
						class Cmp_Fn,		\
						typename Node_Metadata,	\
						class Allocator,	\
						bool Single_Link_Roots>
#else 
#define PB_DS_CLASS_T_DEC						\
    template<								\
						typename Value_Type,	\
						class Cmp_Fn,		\
						typename Node_Metadata,	\
						class Allocator>
#endif 

#ifdef _GLIBCXX_DEBUG
#define PB_DS_CLASS_C_DEC						\
    left_child_next_sibling_heap_<					\
							Value_Type,	\
							Cmp_Fn,		\
							Node_Metadata,	\
							Allocator,	\
							Single_Link_Roots>
#else 
#define PB_DS_CLASS_C_DEC						\
    left_child_next_sibling_heap_<					\
							Value_Type,	\
							Cmp_Fn,		\
							Node_Metadata,	\
							Allocator>
#endif 

    /**
     * class description = "Base class for some types of h3ap$">
     **/
#ifdef _GLIBCXX_DEBUG
    template<typename Value_Type,
	     class Cmp_Fn,
	     typename Node_Metadata,
	     class Allocator,
	     bool Single_Link_Roots>
#else 
    template<typename Value_Type,
	     class Cmp_Fn,
	     typename Node_Metadata,
	     class Allocator>
#endif 
    class left_child_next_sibling_heap_ : public Cmp_Fn
    {

    protected:
      typedef
      typename Allocator::template rebind<
      left_child_next_sibling_heap_node_<
      Value_Type,
      Node_Metadata,
      Allocator> >::other
      node_allocator;

      typedef typename node_allocator::value_type node;

      typedef typename node_allocator::pointer node_pointer;

      typedef typename node_allocator::const_pointer const_node_pointer;

      typedef Node_Metadata node_metadata;

      typedef std::pair< node_pointer, node_pointer> node_pointer_pair;

    private:
      typedef cond_dealtor< node, Allocator> cond_dealtor_t;

      enum
	{
	  simple_value = is_simple<Value_Type>::value
	};

      typedef integral_constant<int, simple_value> no_throw_copies_t;

    public:

      typedef typename Allocator::size_type size_type;

      typedef typename Allocator::difference_type difference_type;

      typedef Value_Type value_type;

      typedef
      typename Allocator::template rebind<
	value_type>::other::pointer
      pointer;

      typedef
      typename Allocator::template rebind<
	value_type>::other::const_pointer
      const_pointer;

      typedef
      typename Allocator::template rebind<
	value_type>::other::reference
      reference;

      typedef
      typename Allocator::template rebind<
	value_type>::other::const_reference
      const_reference;

      typedef
      left_child_next_sibling_heap_node_const_point_iterator_<
	node,
	Allocator>
      const_point_iterator;

      typedef const_point_iterator point_iterator;

      typedef
      left_child_next_sibling_heap_const_iterator_<
	node,
	Allocator>
      const_iterator;

      typedef const_iterator iterator;

      typedef Cmp_Fn cmp_fn;

      typedef Allocator allocator;

    public:

      left_child_next_sibling_heap_();

      left_child_next_sibling_heap_(const Cmp_Fn& r_cmp_fn);

      left_child_next_sibling_heap_(const PB_DS_CLASS_C_DEC& other);

      void
      swap(PB_DS_CLASS_C_DEC& other);

      ~left_child_next_sibling_heap_();

      inline bool
      empty() const;

      inline size_type
      size() const;

      inline size_type
      max_size() const;

      Cmp_Fn& 
      get_cmp_fn();

      const Cmp_Fn& 
      get_cmp_fn() const;

      inline iterator
      begin();

      inline const_iterator
      begin() const;

      inline iterator
      end();

      inline const_iterator
      end() const;

      void
      clear();

#ifdef PB_DS_LC_NS_HEAP_TRACE_
      void
      trace() const;
#endif 

    protected:

      inline node_pointer
      get_new_node_for_insert(const_reference r_val);

      inline static void
      make_child_of(node_pointer p_nd, node_pointer p_new_parent);

      void
      value_swap(PB_DS_CLASS_C_DEC& other);

      inline static node_pointer
      parent(node_pointer p_nd);

      inline void
      swap_with_parent(node_pointer p_nd, node_pointer p_parent);

      void
      bubble_to_top(node_pointer p_nd);

      inline void
      actual_erase_node(node_pointer p_nd);

      void
      clear_imp(node_pointer p_nd);

      void
      to_linked_list();

      template<typename Pred>
      node_pointer
      prune(Pred pred);

#ifdef _GLIBCXX_DEBUG
      void
      assert_valid() const;

      void
      assert_node_consistent(const_node_pointer p_nd, bool single_link) const;

      static size_type
      size_under_node(const_node_pointer p_nd);

      static size_type
      degree(const_node_pointer p_nd);
#endif 

#ifdef PB_DS_LC_NS_HEAP_TRACE_
      static void
      trace_node(const_node_pointer, size_type level);
#endif 

    protected:
      node_pointer m_p_root;

      size_type m_size;

    private:
#ifdef _GLIBCXX_DEBUG
      void
      assert_iterators() const;

      void
      assert_size() const;

      static size_type
      size_from_node(const_node_pointer p_nd);
#endif 

      node_pointer
      recursive_copy_node(const_node_pointer p_nd);

      inline node_pointer
      get_new_node_for_insert(const_reference r_val, false_type);

      inline node_pointer
      get_new_node_for_insert(const_reference r_val, true_type);

#ifdef PB_DS_LC_NS_HEAP_TRACE_
      template<typename Metadata_>
      static void
      trace_node_metadata(const_node_pointer p_nd, type_to_type<Metadata_>);

      static void
      trace_node_metadata(const_node_pointer, type_to_type<null_left_child_next_sibling_heap_node_metadata>);
#endif 

    private:
      static node_allocator s_node_allocator;

      static no_throw_copies_t s_no_throw_copies_ind;
    };

#include <ext/pb_ds/detail/left_child_next_sibling_heap_/constructors_destructor_fn_imps.hpp>
#include <ext/pb_ds/detail/left_child_next_sibling_heap_/iterators_fn_imps.hpp>
#include <ext/pb_ds/detail/left_child_next_sibling_heap_/debug_fn_imps.hpp>
#include <ext/pb_ds/detail/left_child_next_sibling_heap_/trace_fn_imps.hpp>
#include <ext/pb_ds/detail/left_child_next_sibling_heap_/insert_fn_imps.hpp>
#include <ext/pb_ds/detail/left_child_next_sibling_heap_/erase_fn_imps.hpp>
#include <ext/pb_ds/detail/left_child_next_sibling_heap_/info_fn_imps.hpp>
#include <ext/pb_ds/detail/left_child_next_sibling_heap_/policy_access_fn_imps.hpp>

#undef PB_DS_CLASS_C_DEC
#undef PB_DS_CLASS_T_DEC

  } // namespace detail
} // namespace pb_ds

#endif 
