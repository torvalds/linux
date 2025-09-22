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
 * @file standard_policies.hpp
 * Contains standard policies for containers.
 */

#ifndef PB_DS_STANDARD_POLICIES_HPP
#define PB_DS_STANDARD_POLICIES_HPP

#include <memory>
#include <ext/pb_ds/hash_policy.hpp>
#include <ext/pb_ds/list_update_policy.hpp>
#include <ext/pb_ds/tree_policy.hpp>
#include <ext/pb_ds/detail/basic_tree_policy/null_node_metadata.hpp>
#include <ext/pb_ds/trie_policy.hpp>
#include <ext/pb_ds/tag_and_trait.hpp>
#include <ext/hash_map>

namespace pb_ds
{
  namespace detail
  {
    template<typename Key>
    struct default_hash_fn
    {
      typedef __gnu_cxx::hash< Key> type;
    };

    template<typename Key>
    struct default_eq_fn
    {
      typedef std::equal_to< Key> type;
    };

    enum
      {
	default_store_hash = false
      };

    struct default_comb_hash_fn
    {
      typedef pb_ds::direct_mask_range_hashing<> type;
    };

    template<typename Comb_Hash_Fn>
    struct default_resize_policy
    {
    private:
      typedef typename Comb_Hash_Fn::size_type size_type;

      typedef
      typename __conditional_type<
	is_same<
	pb_ds::direct_mask_range_hashing<
	size_type>,
	Comb_Hash_Fn>::value,
	pb_ds::hash_exponential_size_policy<
	size_type>,
	pb_ds::hash_prime_size_policy>::__type
      size_policy_type;

    public:
      typedef
      pb_ds::hash_standard_resize_policy<
      size_policy_type,
      pb_ds::hash_load_check_resize_trigger<
      false,
      size_type>,
      false,
      size_type>
      type;
    };

    struct default_update_policy
    {
      typedef pb_ds::move_to_front_lu_policy<> type;
    };

    template<typename Comb_Probe_Fn>
    struct default_probe_fn
    {
    private:
      typedef typename Comb_Probe_Fn::size_type size_type;

    public:
      typedef
      typename __conditional_type<
      is_same<
      pb_ds::direct_mask_range_hashing<size_t>,
      Comb_Probe_Fn>::value,
      pb_ds::linear_probe_fn<
      size_type>,
      pb_ds::quadratic_probe_fn<
      size_type> >::__type
      type;
    };

    template<typename Key>
    struct default_trie_e_access_traits;

    template<typename Char, class Char_Traits>
    struct default_trie_e_access_traits<
      std::basic_string<
      Char,
      Char_Traits,
      std::allocator<
      char> > >
    {
      typedef
      pb_ds::string_trie_e_access_traits<
	std::basic_string<
	Char,
	Char_Traits,
	std::allocator<
	char> > >
      type;
    };

  } // namespace detail
} // namespace pb_ds

#endif // #ifndef PB_DS_STANDARD_POLICIES_HPP

