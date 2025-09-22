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
 * @file lu_map_.hpp
 * Contains a list update map.
 */

#include <utility>
#include <iterator>
#include <ext/pb_ds/detail/cond_dealtor.hpp>
#include <ext/pb_ds/tag_and_trait.hpp>
#include <ext/pb_ds/detail/types_traits.hpp>
#include <ext/pb_ds/detail/list_update_map_/entry_metadata_base.hpp>
#include <ext/pb_ds/exception.hpp>
#ifdef _GLIBCXX_DEBUG
#include <ext/pb_ds/detail/map_debug_base.hpp>
#endif 
#ifdef PB_DS_LU_MAP_TRACE_
#include <iostream>
#endif 
#include <debug/debug.h>

namespace pb_ds
{
  namespace detail
  {
#define PB_DS_CLASS_T_DEC \
    template<typename Key, typename Mapped, class Eq_Fn, \
	     class Allocator, class Update_Policy>

#ifdef PB_DS_DATA_TRUE_INDICATOR
#define PB_DS_CLASS_NAME lu_map_data_
#endif 

#ifdef PB_DS_DATA_FALSE_INDICATOR
#define PB_DS_CLASS_NAME lu_map_no_data_
#endif 

#define PB_DS_CLASS_C_DEC \
    PB_DS_CLASS_NAME<Key, Mapped, Eq_Fn, Allocator, Update_Policy>

#define PB_DS_TYPES_TRAITS_C_DEC \
    types_traits<Key, Mapped, Allocator, false>

#ifdef _GLIBCXX_DEBUG
#define PB_DS_MAP_DEBUG_BASE_C_DEC \
    map_debug_base<Key, Eq_Fn, \
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

    /* Skip to the lu, my darling. */
    // list-based (with updates) associative container.
    template<typename Key,
	     typename Mapped,
	     class Eq_Fn,
	     class Allocator,
	     class Update_Policy>
    class PB_DS_CLASS_NAME :
#ifdef _GLIBCXX_DEBUG
      protected PB_DS_MAP_DEBUG_BASE_C_DEC,
#endif 
      public PB_DS_TYPES_TRAITS_C_DEC
    {
    private:
      typedef PB_DS_TYPES_TRAITS_C_DEC traits_base;

      struct entry 
     : public lu_map_entry_metadata_base<typename Update_Policy::metadata_type>
      {
	typename traits_base::value_type m_value;
	typename Allocator::template rebind<entry>::other::pointer m_p_next;
      };

      typedef typename Allocator::template rebind<entry>::other entry_allocator;
      typedef typename entry_allocator::pointer entry_pointer;
      typedef typename entry_allocator::const_pointer const_entry_pointer;
      typedef typename entry_allocator::reference entry_reference;
      typedef typename entry_allocator::const_reference const_entry_reference;

      typedef typename Allocator::template rebind<entry_pointer>::other entry_pointer_allocator;
      typedef typename entry_pointer_allocator::pointer entry_pointer_array;

      typedef typename traits_base::value_type value_type_;
      typedef typename traits_base::pointer pointer_;
      typedef typename traits_base::const_pointer const_pointer_;
      typedef typename traits_base::reference reference_;
      typedef typename traits_base::const_reference const_reference_;

#define PB_DS_GEN_POS entry_pointer

#include <ext/pb_ds/detail/unordered_iterator/const_point_iterator.hpp>
#include <ext/pb_ds/detail/unordered_iterator/point_iterator.hpp>
#include <ext/pb_ds/detail/unordered_iterator/const_iterator.hpp>
#include <ext/pb_ds/detail/unordered_iterator/iterator.hpp>

#undef PB_DS_GEN_POS


#ifdef _GLIBCXX_DEBUG
      typedef PB_DS_MAP_DEBUG_BASE_C_DEC map_debug_base;
#endif 

      typedef cond_dealtor<entry, Allocator> cond_dealtor_t;

    public:
      typedef Allocator allocator;
      typedef typename Allocator::size_type size_type;
      typedef typename Allocator::difference_type difference_type;
      typedef Eq_Fn eq_fn;
      typedef Update_Policy update_policy;
      typedef typename Update_Policy::metadata_type update_metadata;
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

#ifdef PB_DS_DATA_TRUE_INDICATOR
      typedef point_iterator_ point_iterator;
#endif 

#ifdef PB_DS_DATA_FALSE_INDICATOR
      typedef const_point_iterator_ point_iterator;
#endif 

      typedef const_point_iterator_ const_point_iterator;

#ifdef PB_DS_DATA_TRUE_INDICATOR
      typedef iterator_ iterator;
#endif 

#ifdef PB_DS_DATA_FALSE_INDICATOR
      typedef const_iterator_ iterator;
#endif 

      typedef const_iterator_ const_iterator;

    public:
      PB_DS_CLASS_NAME();

      PB_DS_CLASS_NAME(const PB_DS_CLASS_C_DEC&);

      virtual
      ~PB_DS_CLASS_NAME();

      template<typename It>
      PB_DS_CLASS_NAME(It first_it, It last_it);

      void
      swap(PB_DS_CLASS_C_DEC&);

      inline size_type
      size() const;

      inline size_type
      max_size() const;

      inline bool
      empty() const;

      inline mapped_reference
      operator[](const_key_reference r_key)
      {
#ifdef PB_DS_DATA_TRUE_INDICATOR
	_GLIBCXX_DEBUG_ONLY(assert_valid();)
	return insert(std::make_pair(r_key, mapped_type())).first->second;
#else 
	insert(r_key);
	return traits_base::s_null_mapped;
#endif 
      }

      inline std::pair<point_iterator, bool>
      insert(const_reference);

      inline point_iterator
      find(const_key_reference r_key)
      {
	_GLIBCXX_DEBUG_ONLY(assert_valid();)
	entry_pointer p_e = find_imp(r_key);
	return point_iterator(p_e == NULL ? NULL: &p_e->m_value);
      }

      inline const_point_iterator
      find(const_key_reference r_key) const
      {
	_GLIBCXX_DEBUG_ONLY(assert_valid();)
	entry_pointer p_e = find_imp(r_key);
	return const_point_iterator(p_e == NULL ? NULL: &p_e->m_value);
      }

      inline bool
      erase(const_key_reference);

      template<typename Pred>
      inline size_type
      erase_if(Pred);

      void
      clear();

      inline iterator
      begin();

      inline const_iterator
      begin() const;

      inline iterator
      end();

      inline const_iterator
      end() const;

#ifdef _GLIBCXX_DEBUG
      void
      assert_valid() const;
#endif 

#ifdef PB_DS_LU_MAP_TRACE_
      void
      trace() const;
#endif

    protected:

      template<typename It>
      void
      copy_from_range(It, It);

    private:
#ifdef PB_DS_DATA_TRUE_INDICATOR
      friend class iterator_;
#endif 

      friend class const_iterator_;

      inline entry_pointer
      allocate_new_entry(const_reference, false_type);

      inline entry_pointer
      allocate_new_entry(const_reference, true_type);

      template<typename Metadata>
      inline static void
      init_entry_metadata(entry_pointer, type_to_type<Metadata>);

      inline static void
      init_entry_metadata(entry_pointer, type_to_type<null_lu_metadata>);

      void
      deallocate_all();

      void
      erase_next(entry_pointer);

      void
      actual_erase_entry(entry_pointer);

      void
      inc_it_state(const_pointer& r_p_value, entry_pointer& r_pos) const
      {
	r_pos = r_pos->m_p_next;
	r_p_value = (r_pos == NULL) ? NULL : &r_pos->m_value;
      }

      template<typename Metadata>
      inline static bool
      apply_update(entry_pointer, type_to_type<Metadata>);

      inline static bool
      apply_update(entry_pointer, type_to_type<null_lu_metadata>);

      inline entry_pointer
      find_imp(const_key_reference) const;

      static entry_allocator s_entry_allocator;
      static Eq_Fn s_eq_fn;
      static Update_Policy s_update_policy;
      static type_to_type<update_metadata> s_metadata_type_indicator;
      static null_lu_metadata s_null_lu_metadata;

      mutable entry_pointer m_p_l;
    };

#include <ext/pb_ds/detail/list_update_map_/constructor_destructor_fn_imps.hpp>
#include <ext/pb_ds/detail/list_update_map_/info_fn_imps.hpp>
#include <ext/pb_ds/detail/list_update_map_/debug_fn_imps.hpp>
#include <ext/pb_ds/detail/list_update_map_/iterators_fn_imps.hpp>
#include <ext/pb_ds/detail/list_update_map_/erase_fn_imps.hpp>
#include <ext/pb_ds/detail/list_update_map_/find_fn_imps.hpp>
#include <ext/pb_ds/detail/list_update_map_/insert_fn_imps.hpp>
#include <ext/pb_ds/detail/list_update_map_/trace_fn_imps.hpp>

#undef PB_DS_CLASS_T_DEC
#undef PB_DS_CLASS_C_DEC
#undef  PB_DS_TYPES_TRAITS_C_DEC
#undef PB_DS_MAP_DEBUG_BASE_C_DEC
#undef PB_DS_CLASS_NAME
#undef PB_DS_V2F
#undef PB_DS_EP2VP
#undef PB_DS_V2S

  } // namespace detail
} // namespace pb_ds
