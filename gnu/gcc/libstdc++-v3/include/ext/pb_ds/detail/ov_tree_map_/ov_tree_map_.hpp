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
 * @file ov_tree_map_.hpp
 * Contains an implementation class for ov_tree_.
 */

#include <map>
#include <set>
#include <ext/pb_ds/tree_policy.hpp>
#include <ext/pb_ds/detail/eq_fn/eq_by_less.hpp>
#include <ext/pb_ds/detail/types_traits.hpp>
#include <ext/pb_ds/detail/map_debug_base.hpp>
#include <ext/pb_ds/detail/type_utils.hpp>
#include <ext/pb_ds/exception.hpp>
#include <ext/pb_ds/detail/tree_trace_base.hpp>
#include <utility>
#include <functional>
#include <algorithm>
#include <vector>
#include <assert.h>
#include <debug/debug.h>

namespace pb_ds
{
  namespace detail
  {
#define PB_DS_CLASS_T_DEC \
    template<typename Key, typename Mapped, class Cmp_Fn, \
	     class Node_And_It_Traits, class Allocator>

#ifdef PB_DS_DATA_TRUE_INDICATOR
#define PB_DS_OV_TREE_CLASS_NAME ov_tree_data_
#endif 

#ifdef PB_DS_DATA_FALSE_INDICATOR
#define PB_DS_OV_TREE_CLASS_NAME ov_tree_no_data_
#endif 

#ifdef PB_DS_DATA_TRUE_INDICATOR
#define PB_DS_CONST_NODE_ITERATOR_NAME ov_tree_const_node_iterator_data_
#else 
#define PB_DS_CONST_NODE_ITERATOR_NAME ov_tree_const_node_iterator_no_data_
#endif 

#define PB_DS_CLASS_C_DEC \
   PB_DS_OV_TREE_CLASS_NAME<Key, Mapped, Cmp_Fn, Node_And_It_Traits, Allocator>

#define PB_DS_TYPES_TRAITS_C_DEC \
    types_traits<Key, Mapped, Allocator, false>

#ifdef _GLIBCXX_DEBUG
#define PB_DS_MAP_DEBUG_BASE_C_DEC \
    map_debug_base<Key, eq_by_less<Key, Cmp_Fn>, \
       	typename Allocator::template rebind<Key>::other::const_reference>
#endif 

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

#ifdef PB_DS_TREE_TRACE
#define PB_DS_TREE_TRACE_BASE_C_DEC \
    tree_trace_base<typename Node_And_It_Traits::const_node_iterator, \
		    typename Node_And_It_Traits::node_iterator, \
		    Cmp_Fn, false, Allocator>
#endif 

    // Ordered-vector tree associative-container.
    template<typename Key, typename Mapped, class Cmp_Fn,
	     class Node_And_It_Traits, class Allocator>
    class PB_DS_OV_TREE_CLASS_NAME :
#ifdef _GLIBCXX_DEBUG
      protected PB_DS_MAP_DEBUG_BASE_C_DEC,
#endif 
#ifdef PB_DS_TREE_TRACE
      public PB_DS_TREE_TRACE_BASE_C_DEC,
#endif 
      public Cmp_Fn,
      public Node_And_It_Traits::node_update,
      public PB_DS_TYPES_TRAITS_C_DEC
    {
    private:
      typedef PB_DS_TYPES_TRAITS_C_DEC traits_base;

      typedef typename remove_const<typename traits_base::value_type>::type non_const_value_type;

      typedef typename Allocator::template rebind<non_const_value_type>::other value_allocator;
      typedef typename value_allocator::pointer value_vector;


      typedef Cmp_Fn cmp_fn_base;

#ifdef _GLIBCXX_DEBUG
      typedef PB_DS_MAP_DEBUG_BASE_C_DEC map_debug_base;
#endif 

      typedef typename traits_base::pointer mapped_pointer_;
      typedef typename traits_base::const_pointer const_mapped_pointer_;

      typedef typename Node_And_It_Traits::metadata_type metadata_type;

      typedef typename Allocator::template rebind<metadata_type>::other metadata_allocator;
      typedef typename metadata_allocator::pointer metadata_pointer;
      typedef typename metadata_allocator::const_reference const_metadata_reference;
      typedef typename metadata_allocator::reference metadata_reference;

      typedef
      typename Node_And_It_Traits::null_node_update_pointer
      null_node_update_pointer;

    public:

      typedef Allocator allocator;
      typedef typename Allocator::size_type size_type;
      typedef typename Allocator::difference_type difference_type;

      typedef Cmp_Fn cmp_fn;

      typedef typename Node_And_It_Traits::node_update node_update;

      typedef typename traits_base::key_type key_type;
      typedef typename traits_base::key_pointer key_pointer;
      typedef typename traits_base::const_key_pointer const_key_pointer;
      typedef typename traits_base::key_reference key_reference;
      typedef typename traits_base::const_key_reference const_key_reference;
      typedef typename traits_base::mapped_type mapped_type;
      typedef typename traits_base::mapped_pointer mapped_pointer;
      typedef typename traits_base::const_mapped_pointer const_mapped_pointer;
      typedef typename traits_base::mapped_reference mapped_reference;
      typedef typename traits_base::const_mapped_reference const_mapped_reference;
      typedef typename traits_base::value_type value_type;
      typedef typename traits_base::pointer pointer;
      typedef typename traits_base::const_pointer const_pointer;
      typedef typename traits_base::reference reference;
      typedef typename traits_base::const_reference const_reference;

      typedef const_pointer const_point_iterator;

#ifdef PB_DS_DATA_TRUE_INDICATOR
      typedef pointer point_iterator;
#else 
      typedef const_point_iterator point_iterator;
#endif 

      typedef const_point_iterator const_iterator;

      typedef point_iterator iterator;

#include <ext/pb_ds/detail/ov_tree_map_/cond_dtor.hpp>

      typedef
      typename Node_And_It_Traits::const_node_iterator
      const_node_iterator;

      typedef typename Node_And_It_Traits::node_iterator node_iterator;

    public:

      PB_DS_OV_TREE_CLASS_NAME();

      PB_DS_OV_TREE_CLASS_NAME(const Cmp_Fn&);

      PB_DS_OV_TREE_CLASS_NAME(const Cmp_Fn&, const node_update&);

      PB_DS_OV_TREE_CLASS_NAME(const PB_DS_CLASS_C_DEC&);

      ~PB_DS_OV_TREE_CLASS_NAME();

      void
      swap(PB_DS_CLASS_C_DEC&);

      template<typename It>
      void
      copy_from_range(It, It);

      inline size_type
      max_size() const;

      inline bool
      empty() const;

      inline size_type
      size() const;

      Cmp_Fn& 
      get_cmp_fn();

      const Cmp_Fn& 
      get_cmp_fn() const;

      inline mapped_reference
      operator[](const_key_reference r_key)
      {
#ifdef PB_DS_DATA_TRUE_INDICATOR
	_GLIBCXX_DEBUG_ONLY(assert_valid();)
	point_iterator it = lower_bound(r_key);
	if (it != end() && !Cmp_Fn::operator()(r_key, PB_DS_V2F(*it)))
	  {
	    _GLIBCXX_DEBUG_ONLY(map_debug_base::check_key_exists(r_key));
	    _GLIBCXX_DEBUG_ONLY(assert_valid();)
	     return it->second;
	  }

	_GLIBCXX_DEBUG_ONLY(assert_valid();)
	return (insert_new_val(it, std::make_pair(r_key, mapped_type()))->second);
#else 
	insert(r_key);
	return traits_base::s_null_mapped;
#endif 
      }

      inline std::pair<point_iterator, bool>
      insert(const_reference r_value)
      {
	_GLIBCXX_DEBUG_ONLY(assert_valid();)
	const_key_reference r_key = PB_DS_V2F(r_value);
	point_iterator it = lower_bound(r_key);

	if (it != end()&&  !Cmp_Fn::operator()(r_key, PB_DS_V2F(*it)))
	  {
	    _GLIBCXX_DEBUG_ONLY(assert_valid();)
	    _GLIBCXX_DEBUG_ONLY(map_debug_base::check_key_exists(r_key));
	    return std::make_pair(it, false);
	  }

	_GLIBCXX_DEBUG_ONLY(assert_valid();)
	return std::make_pair(insert_new_val(it, r_value), true);
      }

      inline point_iterator
      lower_bound(const_key_reference r_key)
      {
	pointer it = m_a_values;
	pointer e_it = m_a_values + m_size;
	while (it != e_it)
	  {
	    pointer mid_it = it + ((e_it - it) >> 1);
	    if (cmp_fn_base::operator()(PB_DS_V2F(*mid_it), r_key))
	      it = ++mid_it;
	    else
	      e_it = mid_it;
	  }
	return it;
      }

      inline const_point_iterator
      lower_bound(const_key_reference r_key) const
      { return const_cast<PB_DS_CLASS_C_DEC& >(*this).lower_bound(r_key); }

      inline point_iterator
      upper_bound(const_key_reference r_key)
      {
	iterator pot_it = lower_bound(r_key);
	if (pot_it != end()&&  !Cmp_Fn::operator()(r_key, PB_DS_V2F(*pot_it)))
	  {
	    _GLIBCXX_DEBUG_ONLY(map_debug_base::check_key_exists(r_key));
	    return ++pot_it;
	  }

	_GLIBCXX_DEBUG_ONLY(map_debug_base::check_key_does_not_exist(r_key));
	return pot_it;
      }

      inline const_point_iterator
      upper_bound(const_key_reference r_key) const
      { return const_cast<PB_DS_CLASS_C_DEC&>(*this).upper_bound(r_key); }

      inline point_iterator
      find(const_key_reference r_key)
      {
	_GLIBCXX_DEBUG_ONLY(assert_valid();)
	iterator pot_it = lower_bound(r_key);
	if (pot_it != end() && !Cmp_Fn::operator()(r_key, PB_DS_V2F(*pot_it)))
	  {
	    _GLIBCXX_DEBUG_ONLY(map_debug_base::check_key_exists(r_key));
	    return pot_it;
	  }

	_GLIBCXX_DEBUG_ONLY(map_debug_base::check_key_does_not_exist(r_key));
	return end();
      }

      inline const_point_iterator
      find(const_key_reference r_key) const
      { return (const_cast<PB_DS_CLASS_C_DEC& >(*this).find(r_key)); }

      bool
      erase(const_key_reference);

      template<typename Pred>
      inline size_type
      erase_if(Pred);

      inline iterator
      erase(iterator it)
      { return erase_imp<iterator>(it); }

      void
      clear();

      void
      join(PB_DS_CLASS_C_DEC&);

      void
      split(const_key_reference, PB_DS_CLASS_C_DEC&);

      inline iterator
      begin()
      { return m_a_values; }

      inline const_iterator
      begin() const
      { return m_a_values; }

      inline iterator
      end()
      { return m_end_it; }

      inline const_iterator
      end() const
      { return m_end_it; }

      inline const_node_iterator
      node_begin() const;

      inline const_node_iterator
      node_end() const;

      inline node_iterator
      node_begin();

      inline node_iterator
      node_end();

    private:

      inline void
      update(node_iterator /*it*/, null_node_update_pointer);

      template<typename Node_Update>
      void
      update(node_iterator, Node_Update*);

      void
      reallocate_metadata(null_node_update_pointer, size_type);

      template<typename Node_Update_>
      void
      reallocate_metadata(Node_Update_*, size_type);

      template<typename It>
      void
      copy_from_ordered_range(It, It);

      void
      value_swap(PB_DS_CLASS_C_DEC&);

      template<typename It>
      void
      copy_from_ordered_range(It, It, It, It);

      template<typename Ptr>
      inline static Ptr
      mid_pointer(Ptr p_begin, Ptr p_end)
      {
	_GLIBCXX_DEBUG_ASSERT(p_end >= p_begin);
	return (p_begin + (p_end - p_begin) / 2);
      }

      inline iterator
      insert_new_val(iterator it, const_reference r_value)
      {
	_GLIBCXX_DEBUG_ONLY(assert_valid();)
#ifdef PB_DS_REGRESSION
	  typename Allocator::group_throw_prob_adjustor adjust(m_size);
#endif 

	_GLIBCXX_DEBUG_ONLY(map_debug_base::check_key_does_not_exist(PB_DS_V2F(r_value)));

	value_vector a_values = s_value_alloc.allocate(m_size + 1);

	iterator source_it = begin();
	iterator source_end_it = end();
	iterator target_it = a_values;
	iterator ret_it;

	cond_dtor<size_type> cd(a_values, target_it, m_size + 1);
	while (source_it != it)
	  {
	    new (const_cast<void* >(static_cast<const void* >(target_it)))
	      value_type(*source_it++);
	    ++target_it;
	  }

	new (const_cast<void* >(static_cast<const void* >(ret_it = target_it)))
	  value_type(r_value);
	++target_it;

	while (source_it != source_end_it)
	  {
	    new (const_cast<void* >(static_cast<const void* >(target_it)))
	      value_type(*source_it++);
	    ++target_it;
	  }

	reallocate_metadata((node_update* )this, m_size + 1);
	cd.set_no_action();
	if (m_size != 0)
	  {
	    cond_dtor<size_type> cd1(m_a_values, m_end_it, m_size);
	  }

	++m_size;
	m_a_values = a_values;
	m_end_it = m_a_values + m_size;
	_GLIBCXX_DEBUG_ONLY(map_debug_base::insert_new(PB_DS_V2F(r_value)));
	update(node_begin(), (node_update* )this);
	_GLIBCXX_DEBUG_ONLY(PB_DS_CLASS_C_DEC::assert_valid();)
	return ret_it;
      }

#ifdef _GLIBCXX_DEBUG
      void
      assert_valid() const;

      void
      assert_iterators() const;
#endif 

      template<typename It>
      It
      erase_imp(It it);

      inline const_node_iterator
      PB_DS_node_begin_imp() const;

      inline const_node_iterator
      PB_DS_node_end_imp() const;

      inline node_iterator
      PB_DS_node_begin_imp();

      inline node_iterator
      PB_DS_node_end_imp();

    private:
      static value_allocator s_value_alloc;
      static metadata_allocator s_metadata_alloc;

      value_vector m_a_values;
      metadata_pointer m_a_metadata;
      iterator m_end_it;
      size_type m_size;
    };

#include <ext/pb_ds/detail/ov_tree_map_/constructors_destructor_fn_imps.hpp>
#include <ext/pb_ds/detail/ov_tree_map_/iterators_fn_imps.hpp>
#include <ext/pb_ds/detail/ov_tree_map_/debug_fn_imps.hpp>
#include <ext/pb_ds/detail/ov_tree_map_/erase_fn_imps.hpp>
#include <ext/pb_ds/detail/ov_tree_map_/insert_fn_imps.hpp>
#include <ext/pb_ds/detail/ov_tree_map_/info_fn_imps.hpp>
#include <ext/pb_ds/detail/ov_tree_map_/split_join_fn_imps.hpp>
#include <ext/pb_ds/detail/bin_search_tree_/policy_access_fn_imps.hpp>

#undef PB_DS_CLASS_C_DEC
#undef PB_DS_CLASS_T_DEC
#undef PB_DS_OV_TREE_CLASS_NAME
#undef PB_DS_TYPES_TRAITS_C_DEC
#undef PB_DS_MAP_DEBUG_BASE_C_DEC
#ifdef PB_DS_TREE_TRACE
#undef PB_DS_TREE_TRACE_BASE_C_DEC
#endif 

#undef PB_DS_V2F
#undef PB_DS_EP2VP
#undef PB_DS_V2S
#undef PB_DS_CONST_NODE_ITERATOR_NAME

  } // namespace detail
} // namespace pb_ds
