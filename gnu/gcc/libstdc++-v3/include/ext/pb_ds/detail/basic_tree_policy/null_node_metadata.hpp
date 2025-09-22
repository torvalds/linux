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
 * @file null_node_metadata.hpp
 * Contains an implementation class for tree-like classes.
 */

#ifndef PB_DS_NULL_NODE_METADATA_HPP
#define PB_DS_NULL_NODE_METADATA_HPP

#include <ext/pb_ds/detail/types_traits.hpp>

namespace pb_ds
{
  namespace detail
  {
    template<typename Key, class Data, class Allocator>
    struct dumconst_node_iterator
    {
    private:
      typedef typename types_traits<Key, Data, Allocator, false>::pointer const_iterator;

    public:
      typedef const_iterator value_type;
      typedef const_iterator const_reference;
      typedef const_reference reference;
    };

    struct null_node_metadata
    { };
  } // namespace detail
} // namespace pb_ds

#endif 
