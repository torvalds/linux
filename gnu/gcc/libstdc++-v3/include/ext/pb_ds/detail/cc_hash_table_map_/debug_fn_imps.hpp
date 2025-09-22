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
 * @file debug_fn_imps.hpp
 * Contains implementations of cc_ht_map_'s debug-mode functions.
 */

#ifdef _GLIBCXX_DEBUG

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
assert_valid() const
{
  map_debug_base::check_size(m_num_used_e);
  assert_entry_pointer_array_valid(m_entries);
}

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
assert_entry_pointer_array_valid(const entry_pointer_array a_p_entries) const
{
  size_type iterated_num_used_e = 0;
  for (size_type pos = 0; pos < m_num_e; ++pos)
    {
      entry_pointer p_e = a_p_entries[pos];
      while (p_e != NULL)
        {
	  ++iterated_num_used_e;
	  assert_entry_pointer_valid(p_e, traits_base::m_store_hash_indicator);
	  p_e = p_e->m_p_next;
        }
    }
  _GLIBCXX_DEBUG_ASSERT(iterated_num_used_e == m_num_used_e);
}

#include <ext/pb_ds/detail/cc_hash_table_map_/debug_store_hash_fn_imps.hpp>
#include <ext/pb_ds/detail/cc_hash_table_map_/debug_no_store_hash_fn_imps.hpp>

#endif 
