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
 * @file thin_heap_.hpp
 * Contains an implementation class for a thin heap.
 */

#ifndef PB_DS_THIN_HEAP_HPP
#define PB_DS_THIN_HEAP_HPP

/*
 * Thin heaps.
 * Tarjan and Kaplan.
 */

#include <algorithm>
#include <ext/pb_ds/detail/cond_dealtor.hpp>
#include <ext/pb_ds/detail/type_utils.hpp>
#include <ext/pb_ds/detail/left_child_next_sibling_heap_/left_child_next_sibling_heap_.hpp>
#include <ext/pb_ds/detail/left_child_next_sibling_heap_/null_metadata.hpp>
#include <debug/debug.h>

namespace pb_ds
{
  namespace detail
  {

#define PB_DS_CLASS_T_DEC \
    template<typename Value_Type, class Cmp_Fn, class Allocator>

#define PB_DS_CLASS_C_DEC \
    thin_heap_<Value_Type, Cmp_Fn, Allocator>

#ifdef _GLIBCXX_DEBUG
#define PB_DS_BASE_C_DEC \
    left_child_next_sibling_heap_<Value_Type, Cmp_Fn,	\
			        typename Allocator::size_type, Allocator, true>
#else 
#define PB_DS_BASE_C_DEC						\
    left_child_next_sibling_heap_<Value_Type, Cmp_Fn, \
				  typename Allocator::size_type, Allocator>
#endif 

    /**
     * class description = "t|-|i|\| h34p">
     **/
    template<typename Value_Type, class Cmp_Fn, class Allocator>
    class thin_heap_ : public PB_DS_BASE_C_DEC
    {

    private:
      typedef PB_DS_BASE_C_DEC base_type;

    protected:
      typedef typename base_type::node node;

      typedef typename base_type::node_pointer node_pointer;

      typedef typename base_type::const_node_pointer const_node_pointer;

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
      typename PB_DS_BASE_C_DEC::const_point_iterator
      const_point_iterator;

      typedef typename PB_DS_BASE_C_DEC::point_iterator point_iterator;

      typedef typename PB_DS_BASE_C_DEC::const_iterator const_iterator;

      typedef typename PB_DS_BASE_C_DEC::iterator iterator;

      typedef Cmp_Fn cmp_fn;

      typedef Allocator allocator;

    public:

      inline point_iterator
      push(const_reference r_val);

      void
      modify(point_iterator it, const_reference r_new_val);

      inline const_reference
      top() const;

      void
      pop();

      void
      erase(point_iterator it);

      inline void
      clear();

      template<typename Pred>
      size_type
      erase_if(Pred pred);

      template<typename Pred>
      void
      split(Pred pred, PB_DS_CLASS_C_DEC& other);

      void
      join(PB_DS_CLASS_C_DEC& other);

    protected:

      thin_heap_();

      thin_heap_(const Cmp_Fn& r_cmp_fn);

      thin_heap_(const PB_DS_CLASS_C_DEC& other);

      void
      swap(PB_DS_CLASS_C_DEC& other);

      ~thin_heap_();

      template<typename It>
      void
      copy_from_range(It first_it, It last_it);

#ifdef _GLIBCXX_DEBUG
      void
      assert_valid() const;

      void
      assert_max() const;
#endif 

#ifdef PB_DS_THIN_HEAP_TRACE_
      void
      trace() const;
#endif 

    private:
      enum
	{
	  max_rank = (sizeof(size_type) << 4) + 2
	};

    private:

      void
      initialize();

      inline void
      update_max(node_pointer p_nd);

      inline void
      fix(node_pointer p_nd);

      inline void
      fix_root(node_pointer p_y);

      inline void
      fix_sibling_rank_1_unmarked(node_pointer p_y);

      inline void
      fix_sibling_rank_1_marked(node_pointer p_y);

      inline void
      fix_sibling_general_unmarked(node_pointer p_y);

      inline void
      fix_sibling_general_marked(node_pointer p_y);

      inline void
      fix_child(node_pointer p_y);

      inline static void
      make_root(node_pointer p_nd);

      inline void
      make_root_and_link(node_pointer p_nd);

      inline void
      remove_max_node();

      void
      to_aux_except_max();

      inline void
      add_to_aux(node_pointer p_nd);

      inline void
      make_from_aux();

      inline size_type
      rank_bound();

      inline void
      make_child_of(node_pointer p_nd, node_pointer p_new_parent);

      inline void
      remove_node(node_pointer p_nd);

      inline node_pointer
      join(node_pointer p_lhs, node_pointer p_rhs) const;

#ifdef _GLIBCXX_DEBUG
      void
      assert_node_consistent(const_node_pointer p_nd, bool root) const;

      void
      assert_aux_null() const;
#endif 

    private:
      node_pointer m_p_max;

      node_pointer m_a_aux[max_rank];
    };

    enum
      {
	num_distinct_rank_bounds = 48
      };

    // Taken from the SGI implementation; acknowledged in the docs.
    static const std::size_t g_a_rank_bounds[num_distinct_rank_bounds] =
      {
	/* Dealing cards... */
	/* 0     */ 0ul,
	/* 1     */ 1ul,
	/* 2     */ 1ul,
	/* 3     */ 2ul,
	/* 4     */ 4ul,
	/* 5     */ 6ul,
	/* 6     */ 11ul,
	/* 7     */ 17ul,
	/* 8     */ 29ul,
	/* 9     */ 46ul,
	/* 10    */ 76ul,
	/* 11    */ 122ul,
	/* 12    */ 199ul,
	/* 13    */ 321ul,
	/* 14    */ 521ul,
	/* 15    */ 842ul,
	/* 16    */ 1364ul,
	/* 17    */ 2206ul,
	/* 18    */ 3571ul,
	/* 19    */ 5777ul,
	/* 20    */ 9349ul,
	/* 21    */ 15126ul,
	/* 22    */ 24476ul,
	/* 23    */ 39602ul,
	/* 24    */ 64079ul,
	/* 25    */ 103681ul,
	/* 26    */ 167761ul,
	/* 27    */ 271442ul,
	/* 28    */ 439204ul,
	/* 29    */ 710646ul,
	/* 30    */ 1149851ul,
	/* 31    */ 1860497ul,
	/* 32    */ 3010349ul,
	/* 33    */ 4870846ul,
	/* 34    */ 7881196ul,
	/* 35    */ 12752042ul,
	/* 36    */ 20633239ul,
	/* 37    */ 33385282ul,
	/* 38    */ 54018521ul,
	/* 39    */ 87403803ul,
	/* 40    */ 141422324ul,
	/* 41    */ 228826127ul,
	/* 42    */ 370248451ul,
	/* 43    */ 599074578ul,
	/* 44    */ 969323029ul,
	/* 45    */ 1568397607ul,
	/* 46    */ 2537720636ul,
	/* 47    */ 4106118243ul
	/* Pot's good, let's play */
      };

#include <ext/pb_ds/detail/thin_heap_/constructors_destructor_fn_imps.hpp>
#include <ext/pb_ds/detail/thin_heap_/debug_fn_imps.hpp>
#include <ext/pb_ds/detail/thin_heap_/trace_fn_imps.hpp>
#include <ext/pb_ds/detail/thin_heap_/find_fn_imps.hpp>
#include <ext/pb_ds/detail/thin_heap_/insert_fn_imps.hpp>
#include <ext/pb_ds/detail/thin_heap_/erase_fn_imps.hpp>
#include <ext/pb_ds/detail/thin_heap_/split_join_fn_imps.hpp>

#undef PB_DS_CLASS_C_DEC
#undef PB_DS_CLASS_T_DEC
#undef PB_DS_BASE_C_DEC

  } // namespace detail
} // namespace pb_ds

#endif 
