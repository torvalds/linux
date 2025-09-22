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
 * @file synth_e_access_traits.hpp
 * Contains an implementation class for a patricia tree.
 */

#ifndef PB_DS_SYNTH_E_ACCESS_TRAITS_HPP
#define PB_DS_SYNTH_E_ACCESS_TRAITS_HPP

#include <ext/pb_ds/detail/type_utils.hpp>

namespace pb_ds
{
  namespace detail
  {

#define PB_DS_SYNTH_E_ACCESS_TRAITS_T_DEC				\
    template<typename Type_Traits, bool Set, class E_Access_Traits>

#define PB_DS_SYNTH_E_ACCESS_TRAITS_C_DEC				\
    synth_e_access_traits<						\
						Type_Traits,		\
						Set,			\
						E_Access_Traits>

    template<typename Type_Traits, bool Set, class E_Access_Traits>
    struct synth_e_access_traits : public E_Access_Traits
    {

    private:
      typedef E_Access_Traits base_type;

      typedef Type_Traits type_traits;

      typedef typename type_traits::const_key_reference const_key_reference;

      typedef typename type_traits::const_reference const_reference;

    public:
      synth_e_access_traits();

      synth_e_access_traits(const E_Access_Traits& r_traits);

      inline bool
      equal_prefixes(typename base_type::const_iterator b_l, typename base_type::const_iterator e_l, typename base_type::const_iterator b_r, typename base_type::const_iterator e_r, bool compare_after = true) const;

      bool
      equal_keys(const_key_reference r_lhs_key, const_key_reference r_rhs_key) const;

      bool
      cmp_prefixes(typename base_type::const_iterator b_l, typename base_type::const_iterator e_l, typename base_type::const_iterator b_r, typename base_type::const_iterator e_r, bool compare_after = false) const;

      bool
      cmp_keys(const_key_reference r_lhs_key, const_key_reference r_rhs_key) const;

      inline static const_key_reference
      extract_key(const_reference r_val);

#ifdef _GLIBCXX_DEBUG
      bool
      operator()(const_key_reference r_lhs, const_key_reference r_rhs);
#endif

    private:
      inline static const_key_reference
      extract_key(const_reference r_val, true_type);

      inline static const_key_reference
      extract_key(const_reference r_val, false_type);

    private:
      static integral_constant<int,Set> s_set_ind;
    };

    PB_DS_SYNTH_E_ACCESS_TRAITS_T_DEC
    integral_constant<int,Set>
    PB_DS_SYNTH_E_ACCESS_TRAITS_C_DEC::s_set_ind;

    PB_DS_SYNTH_E_ACCESS_TRAITS_T_DEC
    PB_DS_SYNTH_E_ACCESS_TRAITS_C_DEC::
    synth_e_access_traits()
    { }

    PB_DS_SYNTH_E_ACCESS_TRAITS_T_DEC
    PB_DS_SYNTH_E_ACCESS_TRAITS_C_DEC::
    synth_e_access_traits(const E_Access_Traits& r_traits) :
      E_Access_Traits(r_traits)
    { }

    PB_DS_SYNTH_E_ACCESS_TRAITS_T_DEC
    inline bool
    PB_DS_SYNTH_E_ACCESS_TRAITS_C_DEC::
    equal_prefixes(typename base_type::const_iterator b_l, typename base_type::const_iterator e_l, typename base_type::const_iterator b_r, typename base_type::const_iterator e_r, bool compare_after /*= false */) const
    {
      while (b_l != e_l)
	{
	  if (b_r == e_r)
	    return (false);
	  if (base_type::e_pos(*b_l) != base_type::e_pos(*b_r))
	    return (false);
	  ++b_l;
	  ++b_r;
	}
      return (!compare_after || b_r == e_r);
    }

    PB_DS_SYNTH_E_ACCESS_TRAITS_T_DEC
    bool
    PB_DS_SYNTH_E_ACCESS_TRAITS_C_DEC::
    equal_keys(const_key_reference r_lhs_key, const_key_reference r_rhs_key) const
    {
      return (equal_prefixes(base_type::begin(r_lhs_key),
			     base_type::end(r_lhs_key),
			     base_type::begin(r_rhs_key),
			     base_type::end(r_rhs_key),
			     true));
    }

    PB_DS_SYNTH_E_ACCESS_TRAITS_T_DEC
    bool
    PB_DS_SYNTH_E_ACCESS_TRAITS_C_DEC::
    cmp_prefixes(typename base_type::const_iterator b_l, typename base_type::const_iterator e_l, typename base_type::const_iterator b_r, typename base_type::const_iterator e_r, bool compare_after /* = false*/) const
    {
      while (b_l != e_l)
	{
	  if (b_r == e_r)
	    return (false);
	  const typename base_type::size_type l_pos =
	    base_type::e_pos(*b_l);
	  const typename base_type::size_type r_pos =
	    base_type::e_pos(*b_r);
	  if (l_pos != r_pos)
	    return (l_pos < r_pos);
	  ++b_l;
	  ++b_r;
	}

      if (!compare_after)
	return (false);
      return (b_r != e_r);
    }

    PB_DS_SYNTH_E_ACCESS_TRAITS_T_DEC
    bool
    PB_DS_SYNTH_E_ACCESS_TRAITS_C_DEC::
    cmp_keys(const_key_reference r_lhs_key, const_key_reference r_rhs_key) const
    {
      return (cmp_prefixes(base_type::begin(r_lhs_key),
			   base_type::end(r_lhs_key),
			   base_type::begin(r_rhs_key),
			   base_type::end(r_rhs_key),
			   true));
    }

    PB_DS_SYNTH_E_ACCESS_TRAITS_T_DEC
    inline typename PB_DS_SYNTH_E_ACCESS_TRAITS_C_DEC::const_key_reference
    PB_DS_SYNTH_E_ACCESS_TRAITS_C_DEC::
    extract_key(const_reference r_val)
    {
      return (extract_key(r_val, s_set_ind));
    }

    PB_DS_SYNTH_E_ACCESS_TRAITS_T_DEC
    inline typename PB_DS_SYNTH_E_ACCESS_TRAITS_C_DEC::const_key_reference
    PB_DS_SYNTH_E_ACCESS_TRAITS_C_DEC::
    extract_key(const_reference r_val, true_type)
    {
      return (r_val);
    }

    PB_DS_SYNTH_E_ACCESS_TRAITS_T_DEC
    inline typename PB_DS_SYNTH_E_ACCESS_TRAITS_C_DEC::const_key_reference
    PB_DS_SYNTH_E_ACCESS_TRAITS_C_DEC::
    extract_key(const_reference r_val, false_type)
    {
      return (r_val.first);
    }

#ifdef _GLIBCXX_DEBUG
    PB_DS_SYNTH_E_ACCESS_TRAITS_T_DEC
    bool
    PB_DS_SYNTH_E_ACCESS_TRAITS_C_DEC::
    operator()(const_key_reference r_lhs, const_key_reference r_rhs)
    {
      return (cmp_keys(r_lhs, r_rhs));
    }
#endif 

#undef PB_DS_SYNTH_E_ACCESS_TRAITS_T_DEC
#undef PB_DS_SYNTH_E_ACCESS_TRAITS_C_DEC

  } // namespace detail
} // namespace pb_ds

#endif 
