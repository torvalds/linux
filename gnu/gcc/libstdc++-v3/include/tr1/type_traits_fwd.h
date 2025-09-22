// TR1 type_traits -*- C++ -*-

// Copyright (C) 2004, 2005, 2006 Free Software Foundation, Inc.
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

/** @file tr1/type_traits_fwd.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

#ifndef _TYPE_TRAITS_FWD_H
#define _TYPE_TRAITS_FWD_H 1

#include <cstddef>

// namespace std::tr1
namespace std
{
_GLIBCXX_BEGIN_NAMESPACE(tr1)

  /// @brief  helper classes [4.3].
  template<typename _Tp, _Tp __v>
    struct integral_constant;
  typedef integral_constant<bool, true>     true_type;
  typedef integral_constant<bool, false>    false_type;

  /// @brief  primary type categories [4.5.1].
  template<typename _Tp>
    struct is_void;

  template<typename _Tp>
    struct is_integral;

  template<typename _Tp>
    struct is_floating_point;

  template<typename _Tp>
    struct is_array;
  
  template<typename _Tp>
    struct is_pointer;
 
  template<typename _Tp>
    struct is_reference;

  template<typename _Tp>
    struct is_member_object_pointer;
  
  template<typename _Tp>
    struct is_member_function_pointer;   

  template<typename _Tp>
    struct is_enum;
  
  template<typename _Tp>
    struct is_union;
  
  template<typename _Tp>
    struct is_class;

  template<typename _Tp>
    struct is_function;

  /// @brief  composite type traits [4.5.2].
  template<typename _Tp>
    struct is_arithmetic;

  template<typename _Tp>
    struct is_fundamental;

  template<typename _Tp>
    struct is_object;

  template<typename _Tp>
    struct is_scalar;

  template<typename _Tp>
    struct is_compound;

  template<typename _Tp>
    struct is_member_pointer;

  // Extension.
  template<typename _Tp>
    struct __is_union_or_class;
   
  /// @brief  type properties [4.5.3].
  template<typename _Tp>
    struct is_const;
  
  template<typename _Tp>
    struct is_volatile;

  template<typename _Tp>
    struct is_pod;
  
  template<typename _Tp>
    struct is_empty;
  
  template<typename _Tp>
    struct is_polymorphic;
  
  template<typename _Tp>
    struct is_abstract;
  
  template<typename _Tp>
    struct has_trivial_constructor;
  
  template<typename _Tp>
    struct has_trivial_copy;

  template<typename _Tp>
    struct has_trivial_assign;
  
  template<typename _Tp>
    struct has_trivial_destructor;
  
  template<typename _Tp>
    struct has_nothrow_constructor;
  
  template<typename _Tp>
    struct has_nothrow_copy;

  template<typename _Tp>
    struct has_nothrow_assign;
  
  template<typename _Tp>
    struct has_virtual_destructor;
  
  template<typename _Tp>
    struct is_signed;
  
  template<typename _Tp>
    struct is_unsigned;
   
  template<typename _Tp>
    struct alignment_of;
  
  template<typename _Tp>
    struct rank;
  
  template<typename _Tp, unsigned _Uint = 0>
    struct extent;
  
  /// @brief  relationships between types [4.6].
  template<typename _Tp, typename _Up>
    struct is_same;

  template<typename _From, typename _To>
    struct is_convertible;

  template<typename _Base, typename _Derived>
    struct is_base_of;

  /// @brief  const-volatile modifications [4.7.1].
  template<typename _Tp>
    struct remove_const;
  
  template<typename _Tp>
    struct remove_volatile;
  
  template<typename _Tp>
    struct remove_cv;
  
  template<typename _Tp>
    struct add_const;
   
  template<typename _Tp>
    struct add_volatile;
  
  template<typename _Tp>
    struct add_cv;

  /// @brief  reference modifications [4.7.2].
  template<typename _Tp>
    struct remove_reference;
  
  template<typename _Tp>
    struct add_reference;

  /// @brief  array modifications [4.7.3].
  template<typename _Tp>
    struct remove_extent;

  template<typename _Tp>
    struct remove_all_extents;

  /// @brief  pointer modifications [4.7.4].
  template<typename _Tp>
    struct remove_pointer;
  
  template<typename _Tp>
    struct add_pointer;

  /// @brief  other transformations [4.8].
  template<std::size_t _Len, std::size_t _Align>
    struct aligned_storage;

_GLIBCXX_END_NAMESPACE
}

#endif
