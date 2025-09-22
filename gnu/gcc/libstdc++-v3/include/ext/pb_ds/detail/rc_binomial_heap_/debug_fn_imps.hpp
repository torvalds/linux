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
 * Contains an implementation for rc_binomial_heap_.
 */

#ifdef _GLIBCXX_DEBUG

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
assert_valid() const
{
  base_type::assert_valid(false);
  if (!base_type::empty())
    {
      _GLIBCXX_DEBUG_ASSERT(base_type::m_p_max != NULL);
      base_type::assert_max();
    }

  m_rc.assert_valid();

  if (m_rc.empty())
    {
      base_type::assert_valid(true);
      _GLIBCXX_DEBUG_ASSERT(next_2_pointer(base_type::m_p_root) == NULL);
      return;
    }

  const_node_pointer p_nd = next_2_pointer(base_type::m_p_root);
  typename rc_t::const_iterator it = m_rc.end();
  --it;

  while (p_nd != NULL)
    {
      _GLIBCXX_DEBUG_ASSERT(*it == p_nd);
      const_node_pointer p_next = p_nd->m_p_next_sibling;
      _GLIBCXX_DEBUG_ASSERT(p_next != NULL);
      _GLIBCXX_DEBUG_ASSERT(p_nd->m_metadata == p_next->m_metadata);
      _GLIBCXX_DEBUG_ASSERT(p_next->m_p_next_sibling == NULL ||
		       p_next->m_metadata < p_next->m_p_next_sibling->m_metadata);

      --it;
      p_nd = next_2_pointer(next_after_0_pointer(p_nd));
    }
  _GLIBCXX_DEBUG_ASSERT(it + 1 == m_rc.begin());
}

PB_DS_CLASS_T_DEC
typename PB_DS_CLASS_C_DEC::const_node_pointer
PB_DS_CLASS_C_DEC::
next_2_pointer(const_node_pointer p_nd)
{
  if (p_nd == NULL)
    return NULL;

  node_pointer p_next = p_nd->m_p_next_sibling;

  if (p_next == NULL)
    return NULL;

  if (p_nd->m_metadata == p_next->m_metadata)
    return p_nd;

  return next_2_pointer(p_next);
}

PB_DS_CLASS_T_DEC
typename PB_DS_CLASS_C_DEC::const_node_pointer
PB_DS_CLASS_C_DEC::
next_after_0_pointer(const_node_pointer p_nd)
{
  if (p_nd == NULL)
    return NULL;

  node_pointer p_next = p_nd->m_p_next_sibling;

  if (p_next == NULL)
    return NULL;

  if (p_nd->m_metadata < p_next->m_metadata)
    return p_next;

  return next_after_0_pointer(p_next);
}

#endif 
