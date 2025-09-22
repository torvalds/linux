// Backward-compat support -*- C++ -*-

// Copyright (C) 2001 Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2, or (at your option)
// any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License along
// with this library; see the file COPYING.  If not, write to the Free
// Software Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307,
// USA.

// As a special exception, you may use this file as part of a free software
// library without restriction.  Specifically, if other files instantiate
// templates or use macros or inline functions from this file, or you compile
// this file and link it with other files to produce an executable, this
// file does not by itself cause the resulting executable to be covered by
// the GNU General Public License.  This exception does not however
// invalidate any other reasons why the executable file might be covered by
// the GNU General Public License.

/*
 *
 * Copyright (c) 1994
 * Hewlett-Packard Company
 *
 * Permission to use, copy, modify, distribute and sell this software
 * and its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice appear in all copies and
 * that both that copyright notice and this permission notice appear
 * in supporting documentation.  Hewlett-Packard Company makes no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 *
 * Copyright (c) 1996,1997
 * Silicon Graphics Computer Systems, Inc.
 *
 * Permission to use, copy, modify, distribute and sell this software
 * and its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice appear in all copies and
 * that both that copyright notice and this permission notice appear
 * in supporting documentation.  Silicon Graphics makes no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 */

#ifndef _CPP_BACKWARD_ALGO_H
#define _CPP_BACKWARD_ALGO_H 1

#include "backward_warning.h"
#include "algobase.h"
#include "tempbuf.h"
#include "iterator.h"
#include <bits/stl_algo.h>
#include <bits/stl_numeric.h>
#include <ext/algorithm>
#include <ext/numeric>

// Names from <stl_algo.h>
using std::for_each; 
using std::find; 
using std::find_if; 
using std::adjacent_find; 
using std::count; 
using std::count_if; 
using std::search; 
using std::search_n; 
using std::swap_ranges; 
using std::transform; 
using std::replace; 
using std::replace_if; 
using std::replace_copy; 
using std::replace_copy_if; 
using std::generate; 
using std::generate_n; 
using std::remove; 
using std::remove_if; 
using std::remove_copy; 
using std::remove_copy_if; 
using std::unique; 
using std::unique_copy; 
using std::reverse; 
using std::reverse_copy; 
using std::rotate; 
using std::rotate_copy; 
using std::random_shuffle; 
using std::partition; 
using std::stable_partition; 
using std::sort; 
using std::stable_sort; 
using std::partial_sort; 
using std::partial_sort_copy; 
using std::nth_element; 
using std::lower_bound; 
using std::upper_bound; 
using std::equal_range; 
using std::binary_search; 
using std::merge; 
using std::inplace_merge; 
using std::includes; 
using std::set_union; 
using std::set_intersection; 
using std::set_difference; 
using std::set_symmetric_difference; 
using std::min_element; 
using std::max_element; 
using std::next_permutation; 
using std::prev_permutation; 
using std::find_first_of; 
using std::find_end; 

// Names from stl_heap.h
using std::push_heap;
using std::pop_heap;
using std::make_heap;
using std::sort_heap;

// Names from stl_numeric.h
using std::accumulate; 
using std::inner_product; 
using std::partial_sum; 
using std::adjacent_difference; 

// Names from ext/algorithm
using __gnu_cxx::random_sample; 
using __gnu_cxx::random_sample_n;
using __gnu_cxx::is_sorted; 
using __gnu_cxx::is_heap;
using __gnu_cxx::count;   // Extension returning void
using __gnu_cxx::count_if;   // Extension returning void

// Names from ext/numeric
using __gnu_cxx::power; 
using __gnu_cxx::iota; 

#endif /* _CPP_BACKWARD_ALGO_H */

// Local Variables:
// mode:C++
// End:
