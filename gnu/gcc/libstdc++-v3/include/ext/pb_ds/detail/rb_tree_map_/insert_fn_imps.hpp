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
 * @file insert_fn_imps.hpp
 * Contains an implementation for rb_tree_.
 */

PB_DS_CLASS_T_DEC
inline std::pair<typename PB_DS_CLASS_C_DEC::point_iterator, bool>
PB_DS_CLASS_C_DEC::
insert(const_reference r_value)
{
  _GLIBCXX_DEBUG_ONLY(assert_valid();)
  std::pair<point_iterator, bool> ins_pair = base_type::insert_leaf(r_value);
  if (ins_pair.second == true)
    {
      ins_pair.first.m_p_nd->m_red = true;
      _GLIBCXX_DEBUG_ONLY(this->structure_only_assert_valid();)
      insert_fixup(ins_pair.first.m_p_nd);
    }

  _GLIBCXX_DEBUG_ONLY(assert_valid();)
  return ins_pair;
}

PB_DS_CLASS_T_DEC
inline void
PB_DS_CLASS_C_DEC::
insert_fixup(node_pointer p_nd)
{
  _GLIBCXX_DEBUG_ASSERT(p_nd->m_red == true);
  while (p_nd != base_type::m_p_head->m_p_parent && p_nd->m_p_parent->m_red)
    {
      if (p_nd->m_p_parent == p_nd->m_p_parent->m_p_parent->m_p_left)
        {
	  node_pointer p_y = p_nd->m_p_parent->m_p_parent->m_p_right;
	  if (p_y != NULL && p_y->m_red)
            {
	      p_nd->m_p_parent->m_red = false;
	      p_y->m_red = false;
	      p_nd->m_p_parent->m_p_parent->m_red = true;
	      p_nd = p_nd->m_p_parent->m_p_parent;
            }
	  else
            {
	      if (p_nd == p_nd->m_p_parent->m_p_right)
                {
		  p_nd = p_nd->m_p_parent;
		  base_type::rotate_left(p_nd);
                }
	      p_nd->m_p_parent->m_red = false;
	      p_nd->m_p_parent->m_p_parent->m_red = true;
	      base_type::rotate_right(p_nd->m_p_parent->m_p_parent);
            }
        }
      else
        {
	  node_pointer p_y = p_nd->m_p_parent->m_p_parent->m_p_left;
	  if (p_y != NULL && p_y->m_red)
            {
	      p_nd->m_p_parent->m_red = false;
	      p_y->m_red = false;
	      p_nd->m_p_parent->m_p_parent->m_red = true;
	      p_nd = p_nd->m_p_parent->m_p_parent;
            }
	  else
            {
	      if (p_nd == p_nd->m_p_parent->m_p_left)
                {
		  p_nd = p_nd->m_p_parent;
		  base_type::rotate_right(p_nd);
                }
	      p_nd->m_p_parent->m_red = false;
	      p_nd->m_p_parent->m_p_parent->m_red = true;
	      base_type::rotate_left(p_nd->m_p_parent->m_p_parent);
            }
        }
    }

  base_type::update_to_top(p_nd, (node_update* )this);
  base_type::m_p_head->m_p_parent->m_red = false;
}
