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
 * @file priority_queue.hpp
 * Contains priority_queues.
 */

#ifndef PB_DS_PRIORITY_QUEUE_HPP
#define PB_DS_PRIORITY_QUEUE_HPP

#include <ext/pb_ds/tag_and_trait.hpp>
#include <ext/pb_ds/detail/priority_queue_base_dispatch.hpp>
#include <ext/pb_ds/detail/standard_policies.hpp>

namespace pb_ds
{
  // A priority queue.
  template<typename Value_Type, 
	   typename Cmp_Fn = std::less<Value_Type>,
	   typename Tag = pairing_heap_tag,
	   typename Allocator = std::allocator<char> >
  class priority_queue 
    : public detail::priority_queue_base_dispatch<Value_Type,Cmp_Fn,Tag,Allocator>::type
  {
  private:
    typedef typename detail::priority_queue_base_dispatch<Value_Type,Cmp_Fn,Tag,Allocator>::type base_type;

  public:
    typedef Value_Type 					value_type;
    typedef Cmp_Fn 					cmp_fn;
    typedef Tag 					container_category;
    typedef Allocator 					allocator;
    typedef typename allocator::size_type 		size_type;
    typedef typename allocator::difference_type 	difference_type;

    typedef typename allocator::template rebind<value_type>::other value_rebind;
    typedef typename value_rebind::reference 		reference;
    typedef typename value_rebind::const_reference 	const_reference;
    typedef typename value_rebind::pointer 	   	pointer;
    typedef typename value_rebind::const_pointer 	const_pointer;

    typedef typename base_type::const_point_iterator const_point_iterator;
    typedef typename base_type::point_iterator 		point_iterator;
    typedef typename base_type::const_iterator 		const_iterator;
    typedef typename base_type::iterator 		iterator;

    priority_queue() { }

    // Constructor taking some policy objects. r_cmp_fn will be copied
    // by the Cmp_Fn object of the container object.
    priority_queue(const cmp_fn& r_cmp_fn) : base_type(r_cmp_fn) { }

    // Constructor taking __iterators to a range of value_types. The
    // value_types between first_it and last_it will be inserted into
    // the container object.
    template<typename It>
    priority_queue(It first_it, It last_it)
    { base_type::copy_from_range(first_it, last_it); }

    // Constructor taking __iterators to a range of value_types and
    // some policy objects The value_types between first_it and
    // last_it will be inserted into the container object. r_cmp_fn
    // will be copied by the cmp_fn object of the container object.
    template<typename It>
    priority_queue(It first_it, It last_it, const cmp_fn& r_cmp_fn)
    : base_type(r_cmp_fn)
    { base_type::copy_from_range(first_it, last_it); }

    priority_queue(const priority_queue& other)
    : base_type((const base_type& )other) { }

    virtual
    ~priority_queue() { }

    priority_queue& 
    operator=(const priority_queue& other)
    {
      if (this !=& other)
	{
	  priority_queue tmp(other);
	  swap(tmp);
	}
      return *this;
    }

    void
    swap(priority_queue& other)
    { base_type::swap(other); }
  };
} // namespace pb_ds

#endif 
