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
 * @file tree_policy.hpp
 * Contains tree-related policies.
 */

#ifndef PB_DS_TREE_POLICY_HPP
#define PB_DS_TREE_POLICY_HPP

#include <iterator>
#include <ext/pb_ds/detail/type_utils.hpp>
#include <ext/pb_ds/detail/basic_tree_policy/basic_tree_policy_base.hpp>

namespace pb_ds
{
  // A null node updator, indicating that no node updates are required.
  template<typename Const_Node_Iterator,
	   typename Node_Iterator,
	   typename Cmp_Fn,
	   typename Allocator>
  struct null_tree_node_update
  { };

#define PB_DS_CLASS_T_DEC \
  template<typename Const_Node_Iterator, class Node_Iterator, class Cmp_Fn, class Allocator>

#define PB_DS_CLASS_C_DEC \
  tree_order_statistics_node_update<Const_Node_Iterator, Node_Iterator, Cmp_Fn, Allocator>

#define PB_DS_BASE_C_DEC						\
  detail::basic_tree_policy_base<Const_Node_Iterator, Node_Iterator, Allocator>

  // Functor updating ranks of entrees.
  template<typename Const_Node_Iterator, typename Node_Iterator, 
	   typename Cmp_Fn, typename Allocator>
  class tree_order_statistics_node_update : private PB_DS_BASE_C_DEC
  {
  private:
    typedef PB_DS_BASE_C_DEC base_type;

  public:
    typedef Cmp_Fn cmp_fn;
    typedef Allocator allocator;
    typedef typename allocator::size_type size_type;
    typedef typename base_type::key_type key_type;
    typedef typename base_type::const_key_reference const_key_reference;

    typedef size_type metadata_type;
    typedef Const_Node_Iterator const_node_iterator;
    typedef Node_Iterator node_iterator;
    typedef typename const_node_iterator::value_type const_iterator;
    typedef typename node_iterator::value_type iterator;

    // Finds an entry by __order. Returns a const_iterator to the
    // entry with the __order order, or a const_iterator to the
    // container object's end if order is at least the size of the
    // container object.
    inline const_iterator
    find_by_order(size_type order) const;

    // Finds an entry by __order. Returns an iterator to the entry
    // with the __order order, or an iterator to the container
    // object's end if order is at least the size of the container
    // object.
    inline iterator
    find_by_order(size_type order);

    // Returns the order of a key within a sequence. For exapmle, if
    // r_key is the smallest key, this method will return 0; if r_key
    // is a key between the smallest and next key, this method will
    // return 1; if r_key is a key larger than the largest key, this
    // method will return the size of r_c.
    inline size_type
    order_of_key(const_key_reference r_key) const;

  private:
    // Const reference to the container's value-type.
    typedef typename base_type::const_reference const_reference;

    // Const pointer to the container's value-type.
    typedef typename base_type::const_pointer const_pointer;

    typedef typename allocator::template rebind<metadata_type>::other metadata_rebind;
    // Const metadata reference.
    typedef typename metadata_rebind::const_reference const_metadata_reference;

    // Metadata reference.
    typedef typename metadata_rebind::reference metadata_reference;

    // Returns the const_node_iterator associated with the tree's root node.
    virtual const_node_iterator
    node_begin() const = 0;

    // Returns the node_iterator associated with the tree's root node.
    virtual node_iterator
    node_begin() = 0;

    // Returns the const_node_iterator associated with a just-after leaf node.
    virtual const_node_iterator
    node_end() const = 0;

    // Returns the node_iterator associated with a just-after leaf node.
    virtual node_iterator
    node_end() = 0;

    // Access to the cmp_fn object.
    virtual cmp_fn& 
    get_cmp_fn() = 0;

  protected:
    // Updates the rank of a node through a node_iterator node_it;
    // end_nd_it is the end node iterator.
    inline void
    operator()(node_iterator node_it, const_node_iterator end_nd_it) const;

    virtual
    ~tree_order_statistics_node_update();
  };

#include <ext/pb_ds/detail/tree_policy/order_statistics_imp.hpp>

#undef PB_DS_CLASS_T_DEC
#undef PB_DS_CLASS_C_DEC
#undef PB_DS_BASE_C_DEC

} // namespace pb_ds

#endif 
