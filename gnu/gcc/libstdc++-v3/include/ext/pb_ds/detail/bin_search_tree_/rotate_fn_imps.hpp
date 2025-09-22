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
 * @file rotate_fn_imps.hpp
 * Contains imps for rotating nodes.
 */

PB_DS_CLASS_T_DEC
inline void
PB_DS_CLASS_C_DEC::
rotate_left(node_pointer p_x)
{
  node_pointer p_y = p_x->m_p_right;

  p_x->m_p_right = p_y->m_p_left;

  if (p_y->m_p_left != NULL)
    p_y->m_p_left->m_p_parent = p_x;

  p_y->m_p_parent = p_x->m_p_parent;

  if (p_x == m_p_head->m_p_parent)
    m_p_head->m_p_parent = p_y;
  else if (p_x == p_x->m_p_parent->m_p_left)
    p_x->m_p_parent->m_p_left = p_y;
  else
    p_x->m_p_parent->m_p_right = p_y;

  p_y->m_p_left = p_x;
  p_x->m_p_parent = p_y;

  _GLIBCXX_DEBUG_ONLY(assert_node_consistent(p_x);)
    _GLIBCXX_DEBUG_ONLY(assert_node_consistent(p_y);)

    apply_update(p_x, (node_update* )this);
  apply_update(p_x->m_p_parent, (node_update* )this);
}

PB_DS_CLASS_T_DEC
inline void
PB_DS_CLASS_C_DEC::
rotate_right(node_pointer p_x)
{
  node_pointer p_y = p_x->m_p_left;

  p_x->m_p_left = p_y->m_p_right;

  if (p_y->m_p_right != NULL)
    p_y->m_p_right->m_p_parent = p_x;

  p_y->m_p_parent = p_x->m_p_parent;

  if (p_x == m_p_head->m_p_parent)
    m_p_head->m_p_parent = p_y;
  else if (p_x == p_x->m_p_parent->m_p_right)
    p_x->m_p_parent->m_p_right = p_y;
  else
    p_x->m_p_parent->m_p_left = p_y;

  p_y->m_p_right = p_x;
  p_x->m_p_parent = p_y;

  _GLIBCXX_DEBUG_ONLY(assert_node_consistent(p_x);)
    _GLIBCXX_DEBUG_ONLY(assert_node_consistent(p_y);)

    apply_update(p_x, (node_update* )this);
  apply_update(p_x->m_p_parent, (node_update* )this);
}

PB_DS_CLASS_T_DEC
inline void
PB_DS_CLASS_C_DEC::
rotate_parent(node_pointer p_nd)
{
  node_pointer p_parent = p_nd->m_p_parent;

  if (p_nd == p_parent->m_p_left)
    rotate_right(p_parent);
  else
    rotate_left(p_parent);

  _GLIBCXX_DEBUG_ASSERT(p_parent->m_p_parent = p_nd);
  _GLIBCXX_DEBUG_ASSERT(p_nd->m_p_left == p_parent ||
		   p_nd->m_p_right == p_parent);
}

PB_DS_CLASS_T_DEC
inline void
PB_DS_CLASS_C_DEC::
apply_update(node_pointer /*p_nd*/, null_node_update_pointer /*p_update*/)
{ }

PB_DS_CLASS_T_DEC
template<typename Node_Update_>
inline void
PB_DS_CLASS_C_DEC::
apply_update(node_pointer p_nd, Node_Update_*  /*p_update*/)
{
  node_update::operator()(
			   node_iterator(p_nd),
			   const_node_iterator(static_cast<node_pointer>(NULL)));
}

PB_DS_CLASS_T_DEC
template<typename Node_Update_>
inline void
PB_DS_CLASS_C_DEC::
update_to_top(node_pointer p_nd, Node_Update_* p_update)
{
  while (p_nd != m_p_head)
    {
      apply_update(p_nd, p_update);

      p_nd = p_nd->m_p_parent;
    }
}

PB_DS_CLASS_T_DEC
inline void
PB_DS_CLASS_C_DEC::
update_to_top(node_pointer /*p_nd*/, null_node_update_pointer /*p_update*/)
{ }

