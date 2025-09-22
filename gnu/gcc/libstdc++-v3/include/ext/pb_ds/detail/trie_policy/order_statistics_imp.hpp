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
 * @file order_statistics_imp.hpp
 * Contains forward declarations for order_statistics_key
 */

PB_DS_CLASS_T_DEC
inline typename PB_DS_CLASS_C_DEC::iterator
PB_DS_CLASS_C_DEC::
find_by_order(size_type order)
{
  if (empty())
    return (end());

  ++order;

  node_iterator nd_it = node_begin();

  node_iterator end_nd_it = node_end();

  while (true)
    {
      if (order > nd_it.get_metadata())
	return (++base_type::rightmost_it(nd_it));

      const size_type num_children = nd_it.num_children();

      if (num_children == 0)
	return (*nd_it);

      for (size_type i = 0; i < num_children; ++i)
        {
	  node_iterator child_nd_it = nd_it.get_child(i);

	  if (order <= child_nd_it.get_metadata())
            {
	      i = num_children;

	      nd_it = child_nd_it;
            }
	  else
	    order -= child_nd_it.get_metadata();
        }
    }
}

PB_DS_CLASS_T_DEC
inline typename PB_DS_CLASS_C_DEC::const_iterator
PB_DS_CLASS_C_DEC::
find_by_order(size_type order) const
{
  return (const_cast<PB_DS_CLASS_C_DEC* >(this)->find_by_order(order));
}

PB_DS_CLASS_T_DEC
inline typename PB_DS_CLASS_C_DEC::size_type
PB_DS_CLASS_C_DEC::
order_of_key(const_key_reference r_key) const
{
  const E_Access_Traits& r_traits =
    const_cast<PB_DS_CLASS_C_DEC* >(this)->get_e_access_traits();

  return (order_of_prefix(
			  r_traits.begin(r_key),
			  r_traits.end(r_key)));
}

PB_DS_CLASS_T_DEC
inline typename PB_DS_CLASS_C_DEC::size_type
PB_DS_CLASS_C_DEC::
order_of_prefix(typename e_access_traits::const_iterator b, typename e_access_traits::const_iterator e) const
{
  if (empty())
    return (0);

  const E_Access_Traits& r_traits =
    const_cast<PB_DS_CLASS_C_DEC* >(this)->get_e_access_traits();

  const_node_iterator nd_it = node_begin();

  const_node_iterator end_nd_it = node_end();

  size_type ord = 0;

  while (true)
    {
      const size_type num_children = nd_it.num_children();

      if (num_children == 0)
        {
	  const_key_reference r_key =
	    base_type::extract_key(*(*nd_it));

	  typename e_access_traits::const_iterator key_b =
	    r_traits.begin(r_key);

	  typename e_access_traits::const_iterator key_e =
	    r_traits.end(r_key);

	  return ((base_type::less(                    key_b, key_e,  b, e,  r_traits))?
		  ord + 1 :
		  ord);
        }

      const_node_iterator next_nd_it = end_nd_it;

      size_type i = num_children - 1;

      do
        {
	  const_node_iterator child_nd_it = nd_it.get_child(i);

	  if (next_nd_it != end_nd_it)
	    ord += child_nd_it.get_metadata();
	  else if (!base_type::less(
				    b, e,
				    child_nd_it.valid_prefix().first,
				    child_nd_it.valid_prefix().second,
				    r_traits))
	    next_nd_it = child_nd_it;
        }
      while (i-- > 0);

      if (next_nd_it == end_nd_it)
	return (ord);

      nd_it = next_nd_it;
    }
}

PB_DS_CLASS_T_DEC
inline void
PB_DS_CLASS_C_DEC::
operator()(node_iterator nd_it, const_node_iterator /*end_nd_it*/) const
{
  const size_type num_children = nd_it.num_children();

  size_type children_rank = 0;

  for (size_type i = 0; i < num_children; ++i)
    children_rank += nd_it.get_child(i).get_metadata();

  const_cast<size_type& >(nd_it.get_metadata()) =(num_children == 0)? 1 : children_rank;
}

PB_DS_CLASS_T_DEC
PB_DS_CLASS_C_DEC::
~trie_order_statistics_node_update()
{ }
