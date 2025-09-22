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
 * @file node_iterators.hpp
 * Contains an implementation class for pat_trie_.
 */

#ifndef PB_DS_PAT_TRIE_NODE_ITERATORS_HPP
#define PB_DS_PAT_TRIE_NODE_ITERATORS_HPP

#include <debug/debug.h>

namespace pb_ds
{
  namespace detail
  {

#define PB_DS_PAT_TRIE_CONST_NODE_ITERATOR_C_DEC			\
    pat_trie_const_node_it_<						\
							Node,		\
							Leaf,		\
							Head,		\
							Internal_Node,	\
							Const_Iterator,	\
							Iterator,	\
							E_Access_Traits, \
							Allocator>

#define PB_DS_PAT_TRIE_NODE_ITERATOR_C_DEC			\
    pat_trie_node_it_<						\
					Node,			\
					Leaf,			\
					Head,			\
					Internal_Node,		\
					Const_Iterator,		\
					Iterator,		\
					E_Access_Traits,	\
					Allocator>

    // Const node iterator.
    template<typename Node,
	     class Leaf,
	     class Head,
	     class Internal_Node,
	     class Const_Iterator,
	     class Iterator,
	     class E_Access_Traits,
	     class Allocator>
    class pat_trie_const_node_it_
    {
    protected:
      typedef
      typename Allocator::template rebind<
      Node>::other::pointer
      node_pointer;

      typedef
      typename Allocator::template rebind<
	Leaf>::other::const_pointer
      const_leaf_pointer;

      typedef
      typename Allocator::template rebind<
	Leaf>::other::pointer
      leaf_pointer;

      typedef
      typename Allocator::template rebind<
	Internal_Node>::other::pointer
      internal_node_pointer;

      typedef
      typename Allocator::template rebind<
	Internal_Node>::other::const_pointer
      const_internal_node_pointer;

      typedef
      typename Allocator::template rebind<
	E_Access_Traits>::other::const_pointer
      const_e_access_traits_pointer;

    private:
      inline typename E_Access_Traits::const_iterator
      pref_begin() const
      {
	if (m_p_nd->m_type == pat_trie_leaf_node_type)
	  return (m_p_traits->begin(
				    m_p_traits->extract_key(
							    static_cast<const_leaf_pointer>(m_p_nd)->value())));

	_GLIBCXX_DEBUG_ASSERT(m_p_nd->m_type == pat_trie_internal_node_type);

	return (static_cast<const_internal_node_pointer>(m_p_nd)->pref_b_it());
      }

      inline typename E_Access_Traits::const_iterator
      pref_end() const
      {
	if (m_p_nd->m_type == pat_trie_leaf_node_type)
	  return (m_p_traits->end(
				  m_p_traits->extract_key(
							  static_cast<const_leaf_pointer>(m_p_nd)->value())));

	_GLIBCXX_DEBUG_ASSERT(m_p_nd->m_type == pat_trie_internal_node_type);

	return (static_cast<const_internal_node_pointer>(m_p_nd)->pref_e_it());
      }

    public:

      // Size type.
      typedef typename Allocator::size_type size_type;

      // Category.
      typedef trivial_iterator_tag iterator_category;

      // Difference type.
      typedef trivial_iterator_difference_type difference_type;

      // __Iterator's value type.
      typedef Const_Iterator value_type;

      // __Iterator's reference type.
      typedef value_type reference;

      // __Iterator's __const reference type.
      typedef value_type const_reference;

      // Element access traits.
      typedef E_Access_Traits e_access_traits;

      // A key's element __const iterator.
      typedef typename e_access_traits::const_iterator const_e_iterator;

      // Metadata type.
      typedef typename Node::metadata_type metadata_type;

      // Const metadata reference type.
      typedef
      typename Allocator::template rebind<
	metadata_type>::other::const_reference
      const_metadata_reference;

      // Default constructor.
      /*
	inline
	pat_trie_const_node_it_()
      */
      inline
      pat_trie_const_node_it_(node_pointer p_nd = NULL,  
			      const_e_access_traits_pointer p_traits = NULL) 
      : m_p_nd(const_cast<node_pointer>(p_nd)), m_p_traits(p_traits)
      { }

      // Subtree valid prefix.
      inline std::pair<const_e_iterator, const_e_iterator>
      valid_prefix() const
      { return std::make_pair(pref_begin(), pref_end()); }

      // Const access; returns the __const iterator* associated with
      // the current leaf.
      inline const_reference
      operator*() const
      {
	_GLIBCXX_DEBUG_ASSERT(num_children() == 0);
	return Const_Iterator(m_p_nd);
      }

      // Metadata access.
      inline const_metadata_reference
      get_metadata() const
      { return m_p_nd->get_metadata(); }

      // Returns the number of children in the corresponding node.
      inline size_type
      num_children() const
      {
	if (m_p_nd->m_type == pat_trie_leaf_node_type)
	  return 0;
	_GLIBCXX_DEBUG_ASSERT(m_p_nd->m_type == pat_trie_internal_node_type);
	return std::distance(static_cast<internal_node_pointer>(m_p_nd)->begin(),  static_cast<internal_node_pointer>(m_p_nd)->end());
      }

      // Returns a __const node __iterator to the corresponding node's
      // i-th child.
      PB_DS_PAT_TRIE_CONST_NODE_ITERATOR_C_DEC
      get_child(size_type i) const
      {
	_GLIBCXX_DEBUG_ASSERT(m_p_nd->m_type == pat_trie_internal_node_type);
	typename Internal_Node::iterator it =
	  static_cast<internal_node_pointer>(m_p_nd)->begin();

	std::advance(it, i);
	return PB_DS_PAT_TRIE_CONST_NODE_ITERATOR_C_DEC(*it, m_p_traits);
      }

      // Compares content to a different iterator object.
      inline bool
      operator==(const PB_DS_PAT_TRIE_CONST_NODE_ITERATOR_C_DEC& other) const
      { return (m_p_nd == other.m_p_nd); }

      // Compares content (negatively) to a different iterator object.
      inline bool
      operator!=(const PB_DS_PAT_TRIE_CONST_NODE_ITERATOR_C_DEC& other) const
      { return m_p_nd != other.m_p_nd; }

    private:

      friend class PB_DS_CLASS_C_DEC;

    public:
      node_pointer m_p_nd;

      const_e_access_traits_pointer m_p_traits;
    };

    // Node iterator.
    template<typename Node,
	     class Leaf,
	     class Head,
	     class Internal_Node,
	     class Const_Iterator,
	     class Iterator,
	     class E_Access_Traits,
	     class Allocator>
    class pat_trie_node_it_ : 
      public PB_DS_PAT_TRIE_CONST_NODE_ITERATOR_C_DEC

    {
    private:
      typedef
      typename Allocator::template rebind<
      Node>::other::pointer
      node_pointer;

      typedef Iterator iterator;

      typedef PB_DS_PAT_TRIE_CONST_NODE_ITERATOR_C_DEC base_type;

      typedef
      typename base_type::const_e_access_traits_pointer
      const_e_access_traits_pointer;

      typedef typename base_type::internal_node_pointer internal_node_pointer;

    public:

      // Size type.
      typedef
      typename PB_DS_PAT_TRIE_CONST_NODE_ITERATOR_C_DEC::size_type
      size_type;

      // __Iterator's value type.
      typedef Iterator value_type;

      // __Iterator's reference type.
      typedef value_type reference;

      // __Iterator's __const reference type.
      typedef value_type const_reference;

      // Default constructor.
      /*
	inline
	pat_trie_node_it_() ;
      */

      inline
      pat_trie_node_it_(node_pointer p_nd = NULL,  const_e_access_traits_pointer p_traits = NULL) : base_type(p_nd, p_traits)
      { }

      // Access; returns the iterator*  associated with the current leaf.
      inline reference
      operator*() const
      {
	_GLIBCXX_DEBUG_ASSERT(base_type::num_children() == 0);
	return Iterator(base_type::m_p_nd);

      }

      // Returns a node __iterator to the corresponding node's i-th child.
      PB_DS_PAT_TRIE_NODE_ITERATOR_C_DEC
      get_child(size_type i) const
      {
	_GLIBCXX_DEBUG_ASSERT(base_type::m_p_nd->m_type == pat_trie_internal_node_type);

	typename Internal_Node::iterator it =
	  static_cast<internal_node_pointer>(base_type::m_p_nd)->begin();

	std::advance(it, i);
	return PB_DS_PAT_TRIE_NODE_ITERATOR_C_DEC(*it, base_type::m_p_traits);
      }

    private:
      friend class PB_DS_CLASS_C_DEC;
    };

#undef PB_DS_PAT_TRIE_CONST_NODE_ITERATOR_C_DEC
#undef PB_DS_PAT_TRIE_NODE_ITERATOR_C_DEC

  } // namespace detail
} // namespace pb_ds

#endif 

