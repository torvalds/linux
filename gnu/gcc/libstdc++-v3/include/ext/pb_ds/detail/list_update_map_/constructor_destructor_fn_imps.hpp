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
 * @file constructor_destructor_fn_imps.hpp
 * Contains implementations of PB_DS_CLASS_NAME.
 */

PB_DS_CLASS_T_DEC
typename PB_DS_CLASS_C_DEC::entry_allocator
PB_DS_CLASS_C_DEC::s_entry_allocator;

PB_DS_CLASS_T_DEC
Eq_Fn PB_DS_CLASS_C_DEC::s_eq_fn;

PB_DS_CLASS_T_DEC
null_lu_metadata PB_DS_CLASS_C_DEC::s_null_lu_metadata;

PB_DS_CLASS_T_DEC
Update_Policy PB_DS_CLASS_C_DEC::s_update_policy;

PB_DS_CLASS_T_DEC
type_to_type<
  typename PB_DS_CLASS_C_DEC::update_metadata> PB_DS_CLASS_C_DEC::s_metadata_type_indicator;

PB_DS_CLASS_T_DEC
template<typename It>
void
PB_DS_CLASS_C_DEC::
copy_from_range(It first_it, It last_it)
{
  while (first_it != last_it)
    insert(*(first_it++));
}

PB_DS_CLASS_T_DEC
PB_DS_CLASS_C_DEC::
PB_DS_CLASS_NAME() : m_p_l(NULL)
{ _GLIBCXX_DEBUG_ONLY(assert_valid();) }

PB_DS_CLASS_T_DEC
template<typename It>
PB_DS_CLASS_C_DEC::
PB_DS_CLASS_NAME(It first_it, It last_it) : m_p_l(NULL)
{
  copy_from_range(first_it, last_it);
  _GLIBCXX_DEBUG_ONLY(assert_valid(););
}

PB_DS_CLASS_T_DEC
PB_DS_CLASS_C_DEC::
PB_DS_CLASS_NAME(const PB_DS_CLASS_C_DEC& other) : 
#ifdef _GLIBCXX_DEBUG
  map_debug_base(),
#endif
m_p_l(NULL)
{
  try
    {
      for (const_iterator it = other.begin(); it != other.end(); ++it)
        {
	  entry_pointer p_l = allocate_new_entry(*it, 
			PB_DS_TYPES_TRAITS_C_DEC::m_no_throw_copies_indicator);

	  p_l->m_p_next = m_p_l;
	  m_p_l = p_l;
        }
    }
  catch(...)
    {
      deallocate_all();
      __throw_exception_again;
    }
  _GLIBCXX_DEBUG_ONLY(assert_valid();)
}

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
swap(PB_DS_CLASS_C_DEC& other)
{
  _GLIBCXX_DEBUG_ONLY(assert_valid();)
  _GLIBCXX_DEBUG_ONLY(other.assert_valid();)
  _GLIBCXX_DEBUG_ONLY(map_debug_base::swap(other);)
  std::swap(m_p_l, other.m_p_l);
  _GLIBCXX_DEBUG_ONLY(assert_valid();)
  _GLIBCXX_DEBUG_ONLY(other.assert_valid();)
}

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
deallocate_all()
{
  entry_pointer p_l = m_p_l;
  while (p_l != NULL)
    {
      entry_pointer p_next_l = p_l->m_p_next;
      actual_erase_entry(p_l);
      p_l = p_next_l;
    }
  m_p_l = NULL;
}

PB_DS_CLASS_T_DEC
PB_DS_CLASS_C_DEC::
~PB_DS_CLASS_NAME()
{ deallocate_all(); }

