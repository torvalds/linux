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
 * @file basic_tree_policy_base.hpp
 * Contains a base class for tree_like policies.
 */

#ifndef PB_DS_TREE_LIKE_POLICY_BASE_HPP
#define PB_DS_TREE_LIKE_POLICY_BASE_HPP

namespace pb_ds
{
  namespace detail
  {

#define PB_DS_CLASS_C_DEC						\
    basic_tree_policy_base<						\
							Const_Node_Iterator, \
							Node_Iterator,	\
							Allocator>

    template<typename Const_Node_Iterator,
	     typename Node_Iterator,
	     typename Allocator>
    struct basic_tree_policy_base
    {
    protected:
      typedef typename Node_Iterator::value_type it_type;

      typedef typename std::iterator_traits< it_type>::value_type value_type;

      typedef typename value_type::first_type key_type;

      typedef
      typename Allocator::template rebind<
	typename remove_const<
	key_type>::type>::other::const_reference
      const_key_reference;

      typedef
      typename Allocator::template rebind<
	typename remove_const<
	value_type>::type>::other::const_reference
      const_reference;

      typedef
      typename Allocator::template rebind<
	typename remove_const<
	value_type>::type>::other::reference
      reference;

      typedef
      typename Allocator::template rebind<
	typename remove_const<
	value_type>::type>::other::const_pointer
      const_pointer;

      static inline const_key_reference
      extract_key(const_reference r_val)
      {
	return (r_val.first);
      }

      virtual it_type
      end() = 0;

      it_type
      end_iterator() const
      {
	return (const_cast<PB_DS_CLASS_C_DEC* >(this)->end());
      }

      virtual
      ~basic_tree_policy_base()
      { }
    };

    template<typename Const_Node_Iterator, typename Allocator>
    struct basic_tree_policy_base<
      Const_Node_Iterator,
      Const_Node_Iterator,
      Allocator>
    {
    protected:
      typedef typename Const_Node_Iterator::value_type it_type;

      typedef typename std::iterator_traits< it_type>::value_type value_type;

      typedef value_type key_type;

      typedef
      typename Allocator::template rebind<
	typename remove_const<
	key_type>::type>::other::const_reference
      const_key_reference;

      typedef
      typename Allocator::template rebind<
	typename remove_const<
	value_type>::type>::other::const_reference
      const_reference;

      typedef
      typename Allocator::template rebind<
	typename remove_const<
	value_type>::type>::other::reference
      reference;

      typedef
      typename Allocator::template rebind<
	typename remove_const<
	value_type>::type>::other::const_pointer
      const_pointer;

      static inline const_key_reference
      extract_key(const_reference r_val)
      {
	return (r_val);
      }

      virtual it_type
      end() const = 0;

      it_type
      end_iterator() const
      {
	return (end());
      }

      virtual
      ~basic_tree_policy_base()
      { }
    };

#undef PB_DS_CLASS_C_DEC

  } // namespace detail
} // namespace pb_ds

#endif // #ifndef PB_DS_TREE_LIKE_POLICY_BASE_HPP
