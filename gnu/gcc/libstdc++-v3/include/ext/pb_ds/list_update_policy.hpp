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
 * @file list_update_policy.hpp
 * Contains policies for list update containers.
 */

#ifndef PB_DS_LU_POLICY_HPP
#define PB_DS_LU_POLICY_HPP

#include <ext/pb_ds/detail/list_update_policy/counter_lu_metadata.hpp>

namespace pb_ds
{
  // A null type that means that each link in a list-based container
  // does not actually need metadata.
  struct null_lu_metadata
  { };

#define PB_DS_CLASS_T_DEC template<typename Allocator>
#define PB_DS_CLASS_C_DEC move_to_front_lu_policy<Allocator>

  // A list-update policy that unconditionally moves elements to the
  // front of the list.
  template<typename Allocator = std::allocator<char> >
  class move_to_front_lu_policy
  {
  public:
    typedef Allocator allocator;
      
    // Metadata on which this functor operates.
    typedef null_lu_metadata metadata_type;
      
    // Reference to metadata on which this functor operates.
    typedef typename allocator::template rebind<metadata_type>::other metadata_rebind;
    typedef typename metadata_rebind::reference metadata_reference;
      
    // Creates a metadata object.
    metadata_type
    operator()() const;
      
    // Decides whether a metadata object should be moved to the front
    // of the list.
    inline bool
    operator()(metadata_reference r_metadata) const;
      
  private:
    static null_lu_metadata s_metadata;
  };
  
#include <ext/pb_ds/detail/list_update_policy/mtf_lu_policy_imp.hpp>

#undef PB_DS_CLASS_T_DEC
#undef PB_DS_CLASS_C_DEC

#define PB_DS_CLASS_T_DEC template<size_t Max_Count, class Allocator>
#define PB_DS_CLASS_C_DEC counter_lu_policy<Max_Count, Allocator>

  // A list-update policy that moves elements to the front of the list
  // based on the counter algorithm.
  template<size_t Max_Count = 5, typename Allocator = std::allocator<char> >
  class counter_lu_policy 
  : private detail::counter_lu_policy_base<typename Allocator::size_type>
  {
  public:
    typedef Allocator allocator;

    enum
      {
	max_count = Max_Count
      };

    typedef typename allocator::size_type size_type;

    // Metadata on which this functor operates.
    typedef detail::counter_lu_metadata<size_type> metadata_type;

    // Reference to metadata on which this functor operates.
    typedef typename Allocator::template rebind<metadata_type>::other metadata_rebind;
    typedef typename metadata_rebind::reference metadata_reference;

    // Creates a metadata object.
    metadata_type
    operator()() const;

    // Decides whether a metadata object should be moved to the front
    // of the list.
    bool
    operator()(metadata_reference r_metadata) const;

  private:
    typedef detail::counter_lu_policy_base<typename Allocator::size_type> base_type;
  };

#include <ext/pb_ds/detail/list_update_policy/counter_lu_policy_imp.hpp>

#undef PB_DS_CLASS_T_DEC
#undef PB_DS_CLASS_C_DEC

} // namespace pb_ds

#endif
