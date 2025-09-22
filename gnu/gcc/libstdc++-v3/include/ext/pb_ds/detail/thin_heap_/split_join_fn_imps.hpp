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
 * @file split_join_fn_imps.hpp
 * Contains an implementation for thin_heap_.
 */

PB_DS_CLASS_T_DEC
template<typename Pred>
void
PB_DS_CLASS_C_DEC::
split(Pred pred, PB_DS_CLASS_C_DEC& other)
{
  _GLIBCXX_DEBUG_ONLY(assert_valid();)
    _GLIBCXX_DEBUG_ONLY(other.assert_valid();)

    other.clear();

  if (base_type::empty())
    {
      _GLIBCXX_DEBUG_ONLY(assert_valid();)
        _GLIBCXX_DEBUG_ONLY(other.assert_valid();)

        return;
    }

  base_type::to_linked_list();

  node_pointer p_out = base_type::prune(pred);

  while (p_out != NULL)
    {
      _GLIBCXX_DEBUG_ASSERT(base_type::m_size > 0);
      --base_type::m_size;

      ++other.m_size;

      node_pointer p_next = p_out->m_p_next_sibling;

      other.make_root_and_link(p_out);

      p_out = p_next;
    }

  _GLIBCXX_DEBUG_ONLY(other.assert_valid();)

    node_pointer p_cur = base_type::m_p_root;

  m_p_max = NULL;

  base_type::m_p_root = NULL;

  while (p_cur != NULL)
    {
      node_pointer p_next = p_cur->m_p_next_sibling;

      make_root_and_link(p_cur);

      p_cur = p_next;
    }

  _GLIBCXX_DEBUG_ONLY(assert_valid();)
    _GLIBCXX_DEBUG_ONLY(other.assert_valid();)
    }

PB_DS_CLASS_T_DEC
inline void
PB_DS_CLASS_C_DEC::
join(PB_DS_CLASS_C_DEC& other)
{
  _GLIBCXX_DEBUG_ONLY(assert_valid();)
    _GLIBCXX_DEBUG_ONLY(other.assert_valid();)

    node_pointer p_other = other.m_p_root;

  while (p_other != NULL)
    {
      node_pointer p_next = p_other->m_p_next_sibling;

      make_root_and_link(p_other);

      p_other = p_next;
    }

  base_type::m_size += other.m_size;

  other.m_p_root = NULL;
  other.m_size = 0;
  other.m_p_max = NULL;

  _GLIBCXX_DEBUG_ONLY(assert_valid();)
    _GLIBCXX_DEBUG_ONLY(other.assert_valid();)
    }
