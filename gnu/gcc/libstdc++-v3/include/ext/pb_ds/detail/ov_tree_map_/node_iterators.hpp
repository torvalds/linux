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
 * Contains an implementation class for ov_tree_.
 */

#ifndef PB_DS_OV_TREE_NODE_ITERATORS_HPP
#define PB_DS_OV_TREE_NODE_ITERATORS_HPP

#include <ext/pb_ds/tag_and_trait.hpp>
#include <ext/pb_ds/detail/type_utils.hpp>
#include <debug/debug.h>

namespace pb_ds
{
  namespace detail
  {
#define PB_DS_STATIC_ASSERT(UNIQUE, E)					\
    typedef								\
    static_assert_dumclass<sizeof(static_assert<(bool)(E)>)> \
    UNIQUE##static_assert_type

#define PB_DS_OV_TREE_CONST_NODE_ITERATOR_C_DEC			\
    ov_tree_node_const_it_<Value_Type, Metadata_Type, Allocator>

    // Const node reference.
    template<typename Value_Type, typename Metadata_Type, class Allocator>
    class ov_tree_node_const_it_
    {

    protected:
      typedef
      typename Allocator::template rebind<
      Value_Type>::other::pointer
      pointer;

      typedef
      typename Allocator::template rebind<
	Value_Type>::other::const_pointer
      const_pointer;

      typedef
      typename Allocator::template rebind<
	Metadata_Type>::other::const_pointer
      const_metadata_pointer;

      typedef PB_DS_OV_TREE_CONST_NODE_ITERATOR_C_DEC this_type;

    protected:

      template<typename Ptr>
      inline static Ptr
      mid_pointer(Ptr p_begin, Ptr p_end)
      {
	_GLIBCXX_DEBUG_ASSERT(p_end >= p_begin);
	return (p_begin + (p_end - p_begin) / 2);
      }

    public:

      typedef trivial_iterator_tag iterator_category;

      typedef trivial_iterator_difference_type difference_type;

      typedef
      typename Allocator::template rebind<
	Value_Type>::other::const_pointer
      value_type;

      typedef
      typename Allocator::template rebind<
	typename remove_const<
	Value_Type>::type>::other::const_pointer
      reference;

      typedef
      typename Allocator::template rebind<
	typename remove_const<
	Value_Type>::type>::other::const_pointer
      const_reference;

      typedef Metadata_Type metadata_type;

      typedef
      typename Allocator::template rebind<
	metadata_type>::other::const_reference
      const_metadata_reference;

    public:
      inline
      ov_tree_node_const_it_(const_pointer p_nd = NULL,  const_pointer p_begin_nd = NULL,  const_pointer p_end_nd = NULL,  const_metadata_pointer p_metadata = NULL) : m_p_value(const_cast<pointer>(p_nd)), m_p_begin_value(const_cast<pointer>(p_begin_nd)), m_p_end_value(const_cast<pointer>(p_end_nd)), m_p_metadata(p_metadata)
      { }

      inline const_reference
      operator*() const
      { return m_p_value; }

      inline const_metadata_reference
      get_metadata() const
      {
	enum
	  {
	    has_metadata = !is_same<Metadata_Type, null_node_metadata>::value
	  };

	PB_DS_STATIC_ASSERT(should_have_metadata, has_metadata);
	_GLIBCXX_DEBUG_ASSERT(m_p_metadata != NULL);
	return *m_p_metadata;
      }

      inline this_type
      get_l_child() const
      {
	if (m_p_begin_value == m_p_value)
	  return (this_type(m_p_begin_value, m_p_begin_value, m_p_begin_value));

	const_metadata_pointer p_begin_metadata =
	  m_p_metadata - (m_p_value - m_p_begin_value);

	return (this_type(mid_pointer(m_p_begin_value, m_p_value),
			  m_p_begin_value,
			  m_p_value,
			  mid_pointer(p_begin_metadata, m_p_metadata)));
      }

      inline this_type
      get_r_child() const
      {
	if (m_p_value == m_p_end_value)
	  return (this_type(m_p_end_value,  m_p_end_value,  m_p_end_value));

	const_metadata_pointer p_end_metadata =
	  m_p_metadata + (m_p_end_value - m_p_value);

	return (this_type(mid_pointer(m_p_value + 1, m_p_end_value),
			  m_p_value + 1,
			  m_p_end_value,(m_p_metadata == NULL) ?
			  NULL : mid_pointer(m_p_metadata + 1, p_end_metadata)));
      }

      inline bool
      operator==(const this_type& other) const
      {
	const bool is_end = m_p_begin_value == m_p_end_value;
	const bool is_other_end = other.m_p_begin_value == other.m_p_end_value;

	if (is_end)
	  return (is_other_end);

	if (is_other_end)
	  return (is_end);

	return m_p_value == other.m_p_value;
      }

      inline bool
      operator!=(const this_type& other) const
      { return !operator==(other); }

    public:
      pointer m_p_value;
      pointer m_p_begin_value;
      pointer m_p_end_value;

      const_metadata_pointer m_p_metadata;
    };

#define PB_DS_OV_TREE_NODE_ITERATOR_C_DEC			\
    ov_tree_node_it_<Value_Type, Metadata_Type, Allocator>

    // Node reference.
    template<typename Value_Type, typename Metadata_Type, class Allocator>
    class ov_tree_node_it_ : public PB_DS_OV_TREE_CONST_NODE_ITERATOR_C_DEC
    {

    private:
      typedef PB_DS_OV_TREE_NODE_ITERATOR_C_DEC this_type;

      typedef PB_DS_OV_TREE_CONST_NODE_ITERATOR_C_DEC base_type;

      typedef typename base_type::pointer pointer;

      typedef typename base_type::const_pointer const_pointer;

      typedef
      typename base_type::const_metadata_pointer
      const_metadata_pointer;

    public:

      typedef trivial_iterator_tag iterator_category;

      typedef trivial_iterator_difference_type difference_type;

      typedef
      typename Allocator::template rebind<
	Value_Type>::other::pointer
      value_type;

      typedef
      typename Allocator::template rebind<
	typename remove_const<
	Value_Type>::type>::other::pointer
      reference;

      typedef
      typename Allocator::template rebind<
	typename remove_const<
	Value_Type>::type>::other::pointer
      const_reference;

    public:
      inline
      ov_tree_node_it_(const_pointer p_nd = NULL,  const_pointer p_begin_nd = NULL,  const_pointer p_end_nd = NULL,  const_metadata_pointer p_metadata = NULL) : base_type(                p_nd,  p_begin_nd,  p_end_nd,  p_metadata)
      { }

      // Access.
      inline reference
      operator*() const
      { return reference(base_type::m_p_value); }

      // Returns the node reference associated with the left node.
      inline ov_tree_node_it_
      get_l_child() const
      {
	if (base_type::m_p_begin_value == base_type::m_p_value)
	  return (this_type(base_type::m_p_begin_value,  base_type::m_p_begin_value,  base_type::m_p_begin_value));

	const_metadata_pointer p_begin_metadata =
	  base_type::m_p_metadata - (base_type::m_p_value - base_type::m_p_begin_value);

	return (this_type(base_type::mid_pointer(base_type::m_p_begin_value, base_type::m_p_value),
			  base_type::m_p_begin_value,
			  base_type::m_p_value,
			  base_type::mid_pointer(p_begin_metadata, base_type::m_p_metadata)));
      }

      // Returns the node reference associated with the right node.
      inline ov_tree_node_it_
      get_r_child() const
      {
	if (base_type::m_p_value == base_type::m_p_end_value)
	  return (this_type(base_type::m_p_end_value,  base_type::m_p_end_value,  base_type::m_p_end_value));

	const_metadata_pointer p_end_metadata =
	  base_type::m_p_metadata + (base_type::m_p_end_value - base_type::m_p_value);

	return (this_type(base_type::mid_pointer(base_type::m_p_value + 1, base_type::m_p_end_value),
			  base_type::m_p_value + 1,
			  base_type::m_p_end_value,(base_type::m_p_metadata == NULL)?
			  NULL : base_type::mid_pointer(base_type::m_p_metadata + 1, p_end_metadata)));
      }

    };

#undef PB_DS_OV_TREE_NODE_ITERATOR_C_DEC
#undef PB_DS_OV_TREE_CONST_NODE_ITERATOR_C_DEC
#undef PB_DS_STATIC_ASSERT

} // namespace detail
} // namespace pb_ds

#endif 
