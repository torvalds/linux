// TR1 reference_wrapper -*- C++ -*-

// Copyright (C) 2005 Free Software Foundation, Inc.
// Written by Douglas Gregor <doug.gregor -at- gmail.com>
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
// Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,
// USA.

// As a special exception, you may use this file as part of a free software
// library without restriction.  Specifically, if other files instantiate
// templates or use macros or inline functions from this file, or you compile
// this file and link it with other files to produce an executable, this
// file does not by itself cause the resulting executable to be covered by
// the GNU General Public License.  This exception does not however
// invalidate any other reasons why the executable file might be covered by
// the GNU General Public License.

/** @file tr1/ref_wrap_iterate.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

#if _GLIBCXX_NUM_ARGS > 0
template<_GLIBCXX_TEMPLATE_PARAMS>
  typename result_of<_M_func_type(_GLIBCXX_TEMPLATE_ARGS)>::type
  operator()(_GLIBCXX_REF_PARAMS) const;
#else
typename result_of<_M_func_type()>::type
operator()() const
{ return get()(); }
#endif
