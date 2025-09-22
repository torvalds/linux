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
 * @file hash_eq_fn.hpp
 * Contains 2 eqivalence functions, one employing a hash value,
 *    and one ignoring it.
 */

#ifndef PB_DS_HASH_EQ_FN_HPP
#define PB_DS_HASH_EQ_FN_HPP

#include <utility>
#include <debug/debug.h>

namespace pb_ds
{
  namespace detail
  {
    template<typename Key, class Eq_Fn, class Allocator, bool Store_Hash>
    struct hash_eq_fn;

#define PB_DS_CLASS_T_DEC \
    template<typename Key, class Eq_Fn, class Allocator>

#define PB_DS_CLASS_C_DEC \
    hash_eq_fn<Key, Eq_Fn, Allocator, false>

    /**
     * Specialization 1- The client requests that hash values not be stored.
     **/
    template<typename Key, class Eq_Fn, class Allocator>
    struct hash_eq_fn<Key, Eq_Fn, Allocator, false> : public Eq_Fn
    {
      typedef Eq_Fn eq_fn_base;

      typedef typename Allocator::template rebind<Key>::other key_allocator;

      typedef typename key_allocator::const_reference const_key_reference;

      hash_eq_fn();

      hash_eq_fn(const Eq_Fn& r_eq_fn);

      inline bool
      operator()(const_key_reference r_lhs_key, const_key_reference r_rhs_key) const;

      inline void
      swap(const PB_DS_CLASS_C_DEC& other);
    };

    PB_DS_CLASS_T_DEC
    PB_DS_CLASS_C_DEC::
    hash_eq_fn()
    { }

    PB_DS_CLASS_T_DEC
    inline void
    PB_DS_CLASS_C_DEC::
    swap(const PB_DS_CLASS_C_DEC& other)
    { std::swap((Eq_Fn& )(*this), (Eq_Fn& )other); }

    PB_DS_CLASS_T_DEC
    PB_DS_CLASS_C_DEC::
    hash_eq_fn(const Eq_Fn& r_eq_fn) :
      Eq_Fn(r_eq_fn)
    { }

    PB_DS_CLASS_T_DEC
    inline bool
    PB_DS_CLASS_C_DEC::
    operator()(const_key_reference r_lhs_key, const_key_reference r_rhs_key) const
    { return (eq_fn_base::operator()(r_lhs_key, r_rhs_key)); }

#undef PB_DS_CLASS_T_DEC
#undef PB_DS_CLASS_C_DEC

#define PB_DS_CLASS_T_DEC \
    template<typename Key, class Eq_Fn, class Allocator>

#define PB_DS_CLASS_C_DEC \
    hash_eq_fn<Key, Eq_Fn, Allocator, true>

    /**
     * Specialization 2- The client requests that hash values be stored.
     **/
    template<typename Key, class Eq_Fn, class Allocator>
    struct hash_eq_fn<Key, Eq_Fn, Allocator, true> :
      public Eq_Fn
    {
      typedef typename Allocator::size_type size_type;

      typedef Eq_Fn eq_fn_base;

      typedef typename Allocator::template rebind<Key>::other key_allocator;

      typedef typename key_allocator::const_reference const_key_reference;

      hash_eq_fn();

      hash_eq_fn(const Eq_Fn& r_eq_fn);

      inline bool
      operator()(const_key_reference r_lhs_key, size_type lhs_hash, 
		 const_key_reference r_rhs_key, size_type rhs_hash) const;

      inline void
      swap(const PB_DS_CLASS_C_DEC& other);
    };

    PB_DS_CLASS_T_DEC
    PB_DS_CLASS_C_DEC::
    hash_eq_fn()
    { }

    PB_DS_CLASS_T_DEC
    PB_DS_CLASS_C_DEC::
    hash_eq_fn(const Eq_Fn& r_eq_fn) :
      Eq_Fn(r_eq_fn)
    { }

    PB_DS_CLASS_T_DEC
    inline bool
    PB_DS_CLASS_C_DEC::
    operator()(const_key_reference r_lhs_key, size_type lhs_hash, 
	       const_key_reference r_rhs_key, size_type rhs_hash) const
    {
      _GLIBCXX_DEBUG_ASSERT(!eq_fn_base::operator()(r_lhs_key, r_rhs_key) 
		            || lhs_hash == rhs_hash);

      return (lhs_hash == rhs_hash && 
	      eq_fn_base::operator()(r_lhs_key, r_rhs_key));
    }

    PB_DS_CLASS_T_DEC
    inline void
    PB_DS_CLASS_C_DEC::
    swap(const PB_DS_CLASS_C_DEC& other)
    { std::swap((Eq_Fn& )(*this), (Eq_Fn& )(other)); }

#undef PB_DS_CLASS_T_DEC
#undef PB_DS_CLASS_C_DEC

  } // namespace detail
} // namespace pb_ds

#endif 
