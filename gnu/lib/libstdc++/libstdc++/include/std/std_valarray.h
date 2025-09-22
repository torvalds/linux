// The template and inlines for the -*- C++ -*- valarray class.

// Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002
// Free Software Foundation, Inc.
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

// Written by Gabriel Dos Reis <Gabriel.Dos-Reis@DPTMaths.ENS-Cachan.Fr>

/** @file valarray
 *  This is a Standard C++ Library header.  You should @c #include this header
 *  in your programs, rather than any of the "st[dl]_*.h" implementation files.
 */

#ifndef _CPP_VALARRAY
#define _CPP_VALARRAY 1

#pragma GCC system_header

#include <bits/c++config.h>
#include <cstddef>
#include <cmath>
#include <cstdlib>
#include <numeric>
#include <algorithm>

namespace std
{
  template<class _Clos, typename _Tp> 
    class _Expr;

  template<typename _Tp1, typename _Tp2> 
    class _ValArray;    

  template<class _Oper, template<class, class> class _Meta, class _Dom>
    struct _UnClos;

  template<class _Oper,
        template<class, class> class _Meta1,
        template<class, class> class _Meta2,
        class _Dom1, class _Dom2> 
    class _BinClos;

  template<template<class, class> class _Meta, class _Dom> 
    class _SClos;

  template<template<class, class> class _Meta, class _Dom> 
    class _GClos;
    
  template<template<class, class> class _Meta, class _Dom> 
    class _IClos;
    
  template<template<class, class> class _Meta, class _Dom> 
    class _ValFunClos;
  
  template<template<class, class> class _Meta, class _Dom> 
    class _RefFunClos;

  template<class _Tp> class valarray;   // An array of type _Tp
  class slice;                          // BLAS-like slice out of an array
  template<class _Tp> class slice_array;
  class gslice;                         // generalized slice out of an array
  template<class _Tp> class gslice_array;
  template<class _Tp> class mask_array;     // masked array
  template<class _Tp> class indirect_array; // indirected array

} // namespace std

#include <bits/valarray_array.h>
#include <bits/valarray_meta.h>
  
namespace std
{
  template<class _Tp> 
    class valarray
    {
      template<class _Op>
	struct _UnaryOp 
	{
	  typedef typename __fun<_Op, _Tp>::result_type __rt;
	  typedef _Expr<_UnClos<_Op, _ValArray, _Tp>, __rt> _Rt;
	};
    public:
      typedef _Tp value_type;
      
	// _lib.valarray.cons_ construct/destroy:
      valarray();
      explicit valarray(size_t);
      valarray(const _Tp&, size_t);
      valarray(const _Tp* __restrict__, size_t);
      valarray(const valarray&);
      valarray(const slice_array<_Tp>&);
      valarray(const gslice_array<_Tp>&);
      valarray(const mask_array<_Tp>&);
      valarray(const indirect_array<_Tp>&);
      template<class _Dom>
	valarray(const _Expr<_Dom,_Tp>& __e);
      ~valarray();

      // _lib.valarray.assign_ assignment:
      valarray<_Tp>& operator=(const valarray<_Tp>&);
      valarray<_Tp>& operator=(const _Tp&);
      valarray<_Tp>& operator=(const slice_array<_Tp>&);
      valarray<_Tp>& operator=(const gslice_array<_Tp>&);
      valarray<_Tp>& operator=(const mask_array<_Tp>&);
      valarray<_Tp>& operator=(const indirect_array<_Tp>&);

      template<class _Dom> valarray<_Tp>&
	operator= (const _Expr<_Dom,_Tp>&);

      // _lib.valarray.access_ element access:
      // XXX: LWG to be resolved.
      const _Tp&                 operator[](size_t) const;
      _Tp&                operator[](size_t);		
      // _lib.valarray.sub_ subset operations:
      _Expr<_SClos<_ValArray,_Tp>, _Tp> operator[](slice) const;
      slice_array<_Tp>    operator[](slice);
      _Expr<_GClos<_ValArray,_Tp>, _Tp> operator[](const gslice&) const;
      gslice_array<_Tp>   operator[](const gslice&);
      valarray<_Tp>     	 operator[](const valarray<bool>&) const;
      mask_array<_Tp>     operator[](const valarray<bool>&);
      _Expr<_IClos<_ValArray, _Tp>, _Tp>
        operator[](const valarray<size_t>&) const;
      indirect_array<_Tp> operator[](const valarray<size_t>&);

      // _lib.valarray.unary_ unary operators:
      typename _UnaryOp<__unary_plus>::_Rt  operator+() const;
      typename _UnaryOp<__negate>::_Rt      operator-() const;
      typename _UnaryOp<__bitwise_not>::_Rt operator~() const;
      typename _UnaryOp<__logical_not>::_Rt operator!() const;

      // _lib.valarray.cassign_ computed assignment:
      valarray<_Tp>& operator*=(const _Tp&);
      valarray<_Tp>& operator/=(const _Tp&);
      valarray<_Tp>& operator%=(const _Tp&);
      valarray<_Tp>& operator+=(const _Tp&);
      valarray<_Tp>& operator-=(const _Tp&);
      valarray<_Tp>& operator^=(const _Tp&);
      valarray<_Tp>& operator&=(const _Tp&);
      valarray<_Tp>& operator|=(const _Tp&);
      valarray<_Tp>& operator<<=(const _Tp&);
      valarray<_Tp>& operator>>=(const _Tp&);
      valarray<_Tp>& operator*=(const valarray<_Tp>&);
      valarray<_Tp>& operator/=(const valarray<_Tp>&);
      valarray<_Tp>& operator%=(const valarray<_Tp>&);
      valarray<_Tp>& operator+=(const valarray<_Tp>&);
      valarray<_Tp>& operator-=(const valarray<_Tp>&);
      valarray<_Tp>& operator^=(const valarray<_Tp>&);
      valarray<_Tp>& operator|=(const valarray<_Tp>&);
      valarray<_Tp>& operator&=(const valarray<_Tp>&);
      valarray<_Tp>& operator<<=(const valarray<_Tp>&);
      valarray<_Tp>& operator>>=(const valarray<_Tp>&);

      template<class _Dom>
	valarray<_Tp>& operator*=(const _Expr<_Dom,_Tp>&);
      template<class _Dom>
	valarray<_Tp>& operator/=(const _Expr<_Dom,_Tp>&);
      template<class _Dom>
	valarray<_Tp>& operator%=(const _Expr<_Dom,_Tp>&);
      template<class _Dom>
	valarray<_Tp>& operator+=(const _Expr<_Dom,_Tp>&);
      template<class _Dom>
	valarray<_Tp>& operator-=(const _Expr<_Dom,_Tp>&);
      template<class _Dom>
	valarray<_Tp>& operator^=(const _Expr<_Dom,_Tp>&);
      template<class _Dom>
	valarray<_Tp>& operator|=(const _Expr<_Dom,_Tp>&);
      template<class _Dom>
	valarray<_Tp>& operator&=(const _Expr<_Dom,_Tp>&);
      template<class _Dom>
      valarray<_Tp>& operator<<=(const _Expr<_Dom,_Tp>&);
      template<class _Dom>
	valarray<_Tp>& operator>>=(const _Expr<_Dom,_Tp>&);


      // _lib.valarray.members_ member functions:
      size_t size() const;
      _Tp    sum() const;	
      _Tp    min() const;	
      _Tp    max() const;	

  //           // FIXME: Extension
  //       _Tp    product () const;

      valarray<_Tp> shift (int) const;
      valarray<_Tp> cshift(int) const;
      _Expr<_ValFunClos<_ValArray,_Tp>,_Tp> apply(_Tp func(_Tp)) const;
      _Expr<_RefFunClos<_ValArray,_Tp>,_Tp> apply(_Tp func(const _Tp&)) const;
      void resize(size_t __size, _Tp __c = _Tp());

    private:
      size_t _M_size;
      _Tp* __restrict__ _M_data;
      
      friend class _Array<_Tp>;
    };
  
  template<typename _Tp>
    inline const _Tp&
    valarray<_Tp>::operator[](size_t __i) const
    { return _M_data[__i]; }

  template<typename _Tp>
    inline _Tp&
    valarray<_Tp>::operator[](size_t __i)
    { return _M_data[__i]; }

} // std::
      
#include <bits/slice_array.h>
#include <bits/gslice.h>
#include <bits/gslice_array.h>
#include <bits/mask_array.h>
#include <bits/indirect_array.h>

namespace std
{
  template<typename _Tp>
    inline
    valarray<_Tp>::valarray() : _M_size(0), _M_data(0) {}

  template<typename _Tp>
    inline 
    valarray<_Tp>::valarray(size_t __n) 
	: _M_size(__n), _M_data(__valarray_get_storage<_Tp>(__n))
    { __valarray_default_construct(_M_data, _M_data + __n); }

  template<typename _Tp>
    inline
    valarray<_Tp>::valarray(const _Tp& __t, size_t __n)
      : _M_size(__n), _M_data(__valarray_get_storage<_Tp>(__n))
    { __valarray_fill_construct(_M_data, _M_data + __n, __t); }

  template<typename _Tp>
    inline
    valarray<_Tp>::valarray(const _Tp* __restrict__ __p, size_t __n)
      : _M_size(__n), _M_data(__valarray_get_storage<_Tp>(__n))
    { __valarray_copy_construct(__p, __p + __n, _M_data); }

  template<typename _Tp>
    inline
    valarray<_Tp>::valarray(const valarray<_Tp>& __v)
      : _M_size(__v._M_size), _M_data(__valarray_get_storage<_Tp>(__v._M_size))
    { __valarray_copy_construct(__v._M_data, __v._M_data + _M_size, _M_data); }

  template<typename _Tp>
    inline
    valarray<_Tp>::valarray(const slice_array<_Tp>& __sa)
      : _M_size(__sa._M_sz), _M_data(__valarray_get_storage<_Tp>(__sa._M_sz))
    {
      __valarray_copy
	(__sa._M_array, __sa._M_sz, __sa._M_stride, _Array<_Tp>(_M_data));
    }

  template<typename _Tp>
    inline
    valarray<_Tp>::valarray(const gslice_array<_Tp>& __ga)
      : _M_size(__ga._M_index.size()),
	_M_data(__valarray_get_storage<_Tp>(_M_size))
    {
      __valarray_copy
	(__ga._M_array, _Array<size_t>(__ga._M_index),
	 _Array<_Tp>(_M_data), _M_size);
    }

  template<typename _Tp>
    inline
    valarray<_Tp>::valarray(const mask_array<_Tp>& __ma)
      : _M_size(__ma._M_sz), _M_data(__valarray_get_storage<_Tp>(__ma._M_sz))
    {
      __valarray_copy
	(__ma._M_array, __ma._M_mask, _Array<_Tp>(_M_data), _M_size);
    }

  template<typename _Tp>
    inline
    valarray<_Tp>::valarray(const indirect_array<_Tp>& __ia)
      : _M_size(__ia._M_sz), _M_data(__valarray_get_storage<_Tp>(__ia._M_sz))
    {
      __valarray_copy
	(__ia._M_array, __ia._M_index, _Array<_Tp>(_M_data), _M_size);
    }

  template<typename _Tp> template<class _Dom>
    inline
    valarray<_Tp>::valarray(const _Expr<_Dom, _Tp>& __e)
      : _M_size(__e.size()), _M_data(__valarray_get_storage<_Tp>(_M_size))
    { __valarray_copy(__e, _M_size, _Array<_Tp>(_M_data)); }

  template<typename _Tp>
    inline
    valarray<_Tp>::~valarray()
    {
      __valarray_destroy_elements(_M_data, _M_data + _M_size);
      __valarray_release_memory(_M_data);
    }

  template<typename _Tp>
    inline valarray<_Tp>&
    valarray<_Tp>::operator=(const valarray<_Tp>& __v)
    {
      __valarray_copy(__v._M_data, _M_size, _M_data);
      return *this;
    }

  template<typename _Tp>
    inline valarray<_Tp>&
    valarray<_Tp>::operator=(const _Tp& __t)
    {
      __valarray_fill(_M_data, _M_size, __t);
      return *this;
    }

  template<typename _Tp>
    inline valarray<_Tp>&
    valarray<_Tp>::operator=(const slice_array<_Tp>& __sa)
    {
      __valarray_copy(__sa._M_array, __sa._M_sz,
		      __sa._M_stride, _Array<_Tp>(_M_data));
      return *this;
    }

  template<typename _Tp>
    inline valarray<_Tp>&
    valarray<_Tp>::operator=(const gslice_array<_Tp>& __ga)
    {
      __valarray_copy(__ga._M_array, _Array<size_t>(__ga._M_index),
		      _Array<_Tp>(_M_data), _M_size);
      return *this;
    }

  template<typename _Tp>
    inline valarray<_Tp>&
    valarray<_Tp>::operator=(const mask_array<_Tp>& __ma)
    {
      __valarray_copy(__ma._M_array, __ma._M_mask,
		      _Array<_Tp>(_M_data), _M_size);
      return *this;
    }

  template<typename _Tp>
    inline valarray<_Tp>&
    valarray<_Tp>::operator=(const indirect_array<_Tp>& __ia)
    {
      __valarray_copy(__ia._M_array, __ia._M_index,
		       _Array<_Tp>(_M_data), _M_size);
      return *this;
    }

  template<typename _Tp> template<class _Dom>
    inline valarray<_Tp>&
    valarray<_Tp>::operator=(const _Expr<_Dom, _Tp>& __e)
    {
      __valarray_copy(__e, _M_size, _Array<_Tp>(_M_data));
	return *this;
    }

  template<typename _Tp>
    inline _Expr<_SClos<_ValArray,_Tp>, _Tp>
    valarray<_Tp>::operator[](slice __s) const
    {
      typedef _SClos<_ValArray,_Tp> _Closure;
      return _Expr<_Closure, _Tp>(_Closure (_Array<_Tp>(_M_data), __s));
    }

  template<typename _Tp>
    inline slice_array<_Tp>
    valarray<_Tp>::operator[](slice __s)
    {
      return slice_array<_Tp>(_Array<_Tp>(_M_data), __s);
    }

  template<typename _Tp>
    inline _Expr<_GClos<_ValArray,_Tp>, _Tp>
    valarray<_Tp>::operator[](const gslice& __gs) const
    {
      typedef _GClos<_ValArray,_Tp> _Closure;
      return _Expr<_Closure, _Tp>
	(_Closure(_Array<_Tp>(_M_data), __gs._M_index->_M_index));
    }

  template<typename _Tp>
    inline gslice_array<_Tp>
    valarray<_Tp>::operator[](const gslice& __gs)
    {
      return gslice_array<_Tp>
	(_Array<_Tp>(_M_data), __gs._M_index->_M_index);
    }

  template<typename _Tp>
    inline valarray<_Tp>
    valarray<_Tp>::operator[](const valarray<bool>& __m) const
    {
      size_t __s = 0;
      size_t __e = __m.size();
      for (size_t __i=0; __i<__e; ++__i)
	if (__m[__i]) ++__s;
      return valarray<_Tp>(mask_array<_Tp>(_Array<_Tp>(_M_data), __s,
					   _Array<bool> (__m)));
    }

  template<typename _Tp>
    inline mask_array<_Tp>
    valarray<_Tp>::operator[](const valarray<bool>& __m)
    {
      size_t __s = 0;
      size_t __e = __m.size();
      for (size_t __i=0; __i<__e; ++__i)
	if (__m[__i]) ++__s;
      return mask_array<_Tp>(_Array<_Tp>(_M_data), __s, _Array<bool>(__m));
    }

  template<typename _Tp>
    inline _Expr<_IClos<_ValArray,_Tp>, _Tp>
    valarray<_Tp>::operator[](const valarray<size_t>& __i) const
    {
      typedef _IClos<_ValArray,_Tp> _Closure;
      return _Expr<_Closure, _Tp>(_Closure(*this, __i));
    }

  template<typename _Tp>
    inline indirect_array<_Tp>
    valarray<_Tp>::operator[](const valarray<size_t>& __i)
    {
      return indirect_array<_Tp>(_Array<_Tp>(_M_data), __i.size(),
				 _Array<size_t>(__i));
    }

  template<class _Tp>
    inline size_t 
    valarray<_Tp>::size() const
    { return _M_size; }

  template<class _Tp>
    inline _Tp
    valarray<_Tp>::sum() const
    {
      return __valarray_sum(_M_data, _M_data + _M_size);
    }

//   template<typename _Tp>
//   inline _Tp
//   valarray<_Tp>::product () const
//   {
//       return __valarray_product(_M_data, _M_data + _M_size);
//   }

  template <class _Tp>
     inline valarray<_Tp>
     valarray<_Tp>::shift(int __n) const
     {
       _Tp* const __a = static_cast<_Tp*>
         (__builtin_alloca(sizeof(_Tp) * _M_size));
       if (__n == 0)                          // no shift
         __valarray_copy_construct(_M_data, _M_data + _M_size, __a);
       else if (__n > 0)         // __n > 0: shift left
         {                 
           if (size_t(__n) > _M_size)
             __valarray_default_construct(__a, __a + __n);
           else
             {
               __valarray_copy_construct(_M_data+__n, _M_data + _M_size, __a);
               __valarray_default_construct(__a+_M_size-__n, __a + _M_size);
             }
         }
       else                        // __n < 0: shift right
         {                          
           __valarray_copy_construct (_M_data, _M_data+_M_size+__n, __a-__n);
           __valarray_default_construct(__a, __a - __n);
         }
       return valarray<_Tp> (__a, _M_size);
     }

  template <class _Tp>
     inline valarray<_Tp>
     valarray<_Tp>::cshift (int __n) const
     {
       _Tp* const __a = static_cast<_Tp*>
         (__builtin_alloca (sizeof(_Tp) * _M_size));
       if (__n == 0)               // no cshift
         __valarray_copy_construct(_M_data, _M_data + _M_size, __a);
       else if (__n > 0)           // cshift left
         {               
           __valarray_copy_construct(_M_data, _M_data+__n, __a+_M_size-__n);
           __valarray_copy_construct(_M_data+__n, _M_data + _M_size, __a);
         }
       else                        // cshift right
         {                       
           __valarray_copy_construct
             (_M_data + _M_size+__n, _M_data + _M_size, __a);
           __valarray_copy_construct
             (_M_data, _M_data + _M_size+__n, __a - __n);
         }
       return valarray<_Tp>(__a, _M_size);
     }

  template <class _Tp>
    inline void
    valarray<_Tp>::resize (size_t __n, _Tp __c)
    {
      // This complication is so to make valarray<valarray<T> > work
      // even though it is not required by the standard.  Nobody should
      // be saying valarray<valarray<T> > anyway.  See the specs.
      __valarray_destroy_elements(_M_data, _M_data + _M_size);
      if (_M_size != __n)
	{
	  __valarray_release_memory(_M_data);
	  _M_size = __n;
	  _M_data = __valarray_get_storage<_Tp>(__n);
	}
      __valarray_fill_construct(_M_data, _M_data + __n, __c);
    }
    
  template<typename _Tp>
    inline _Tp
    valarray<_Tp>::min() const
    {
      return *min_element (_M_data, _M_data+_M_size);
    }

  template<typename _Tp>
    inline _Tp
    valarray<_Tp>::max() const
    {
      return *max_element (_M_data, _M_data+_M_size);
    }
  
  template<class _Tp>
    inline _Expr<_ValFunClos<_ValArray,_Tp>,_Tp>
    valarray<_Tp>::apply(_Tp func(_Tp)) const
    {
      typedef _ValFunClos<_ValArray,_Tp> _Closure;
      return _Expr<_Closure,_Tp>(_Closure(*this, func));
    }

  template<class _Tp>
    inline _Expr<_RefFunClos<_ValArray,_Tp>,_Tp>
    valarray<_Tp>::apply(_Tp func(const _Tp &)) const
    {
      typedef _RefFunClos<_ValArray,_Tp> _Closure;
      return _Expr<_Closure,_Tp>(_Closure(*this, func));
    }

#define _DEFINE_VALARRAY_UNARY_OPERATOR(_Op, _Name)                     \
  template<typename _Tp>						\
  inline typename valarray<_Tp>::template _UnaryOp<_Name>::_Rt         	\
  valarray<_Tp>::operator _Op() const					\
  {									\
    typedef _UnClos<_Name,_ValArray,_Tp> _Closure;	                \
    typedef typename __fun<_Name, _Tp>::result_type _Rt;                \
    return _Expr<_Closure, _Rt>(_Closure(*this));			\
  }

    _DEFINE_VALARRAY_UNARY_OPERATOR(+, __unary_plus)
    _DEFINE_VALARRAY_UNARY_OPERATOR(-, __negate)
    _DEFINE_VALARRAY_UNARY_OPERATOR(~, __bitwise_not)
    _DEFINE_VALARRAY_UNARY_OPERATOR (!, __logical_not)

#undef _DEFINE_VALARRAY_UNARY_OPERATOR

#define _DEFINE_VALARRAY_AUGMENTED_ASSIGNMENT(_Op, _Name)               \
  template<class _Tp>							\
    inline valarray<_Tp>&						\
    valarray<_Tp>::operator _Op##=(const _Tp &__t)			\
    {									\
      _Array_augmented_##_Name(_Array<_Tp>(_M_data), _M_size, __t);	\
      return *this;							\
    }									\
									\
  template<class _Tp>							\
    inline valarray<_Tp>&						\
    valarray<_Tp>::operator _Op##=(const valarray<_Tp> &__v)		\
    {									\
      _Array_augmented_##_Name(_Array<_Tp>(_M_data), _M_size, 		\
			       _Array<_Tp>(__v._M_data));		\
      return *this;							\
    }

_DEFINE_VALARRAY_AUGMENTED_ASSIGNMENT(+, __plus)
_DEFINE_VALARRAY_AUGMENTED_ASSIGNMENT(-, __minus)
_DEFINE_VALARRAY_AUGMENTED_ASSIGNMENT(*, __multiplies)
_DEFINE_VALARRAY_AUGMENTED_ASSIGNMENT(/, __divides)
_DEFINE_VALARRAY_AUGMENTED_ASSIGNMENT(%, __modulus)
_DEFINE_VALARRAY_AUGMENTED_ASSIGNMENT(^, __bitwise_xor)
_DEFINE_VALARRAY_AUGMENTED_ASSIGNMENT(&, __bitwise_and)
_DEFINE_VALARRAY_AUGMENTED_ASSIGNMENT(|, __bitwise_or)
_DEFINE_VALARRAY_AUGMENTED_ASSIGNMENT(<<, __shift_left)
_DEFINE_VALARRAY_AUGMENTED_ASSIGNMENT(>>, __shift_right)

#undef _DEFINE_VALARRAY_AUGMENTED_ASSIGNMENT

#define _DEFINE_VALARRAY_EXPR_AUGMENTED_ASSIGNMENT(_Op, _Name)          \
  template<class _Tp> template<class _Dom>				\
    inline valarray<_Tp>&						\
    valarray<_Tp>::operator _Op##=(const _Expr<_Dom,_Tp>& __e)		\
    {									\
      _Array_augmented_##_Name(_Array<_Tp>(_M_data), __e, _M_size);	\
      return *this;							\
    }

_DEFINE_VALARRAY_EXPR_AUGMENTED_ASSIGNMENT(+, __plus)
_DEFINE_VALARRAY_EXPR_AUGMENTED_ASSIGNMENT(-, __minus)
_DEFINE_VALARRAY_EXPR_AUGMENTED_ASSIGNMENT(*, __multiplies)
_DEFINE_VALARRAY_EXPR_AUGMENTED_ASSIGNMENT(/, __divides)
_DEFINE_VALARRAY_EXPR_AUGMENTED_ASSIGNMENT(%, __modulus)
_DEFINE_VALARRAY_EXPR_AUGMENTED_ASSIGNMENT(^, __bitwise_xor)
_DEFINE_VALARRAY_EXPR_AUGMENTED_ASSIGNMENT(&, __bitwise_and)
_DEFINE_VALARRAY_EXPR_AUGMENTED_ASSIGNMENT(|, __bitwise_or)
_DEFINE_VALARRAY_EXPR_AUGMENTED_ASSIGNMENT(<<, __shift_left)
_DEFINE_VALARRAY_EXPR_AUGMENTED_ASSIGNMENT(>>, __shift_right)

#undef _DEFINE_VALARRAY_EXPR_AUGMENTED_ASSIGNMENT
    

#define _DEFINE_BINARY_OPERATOR(_Op, _Name)				\
  template<typename _Tp>						\
    inline _Expr<_BinClos<_Name,_ValArray,_ValArray,_Tp,_Tp>,           \
                 typename __fun<_Name, _Tp>::result_type>               \
    operator _Op(const valarray<_Tp>& __v, const valarray<_Tp>& __w)	\
    {									\
      typedef _BinClos<_Name,_ValArray,_ValArray,_Tp,_Tp> _Closure;     \
      typedef typename __fun<_Name, _Tp>::result_type _Rt;              \
      return _Expr<_Closure, _Rt>(_Closure(__v, __w));                  \
    }									\
									\
  template<typename _Tp>						\
  inline _Expr<_BinClos<_Name,_ValArray,_Constant,_Tp,_Tp>,             \
               typename __fun<_Name, _Tp>::result_type>                 \
  operator _Op(const valarray<_Tp>& __v, const _Tp& __t)		\
  {									\
    typedef _BinClos<_Name,_ValArray,_Constant,_Tp,_Tp> _Closure;	\
    typedef typename __fun<_Name, _Tp>::result_type _Rt;                \
    return _Expr<_Closure, _Rt>(_Closure(__v, __t));	                \
  }									\
									\
  template<typename _Tp>						\
  inline _Expr<_BinClos<_Name,_Constant,_ValArray,_Tp,_Tp>,             \
               typename __fun<_Name, _Tp>::result_type>                 \
  operator _Op(const _Tp& __t, const valarray<_Tp>& __v)		\
  {									\
    typedef _BinClos<_Name,_Constant,_ValArray,_Tp,_Tp> _Closure;       \
    typedef typename __fun<_Name, _Tp>::result_type _Rt;                \
    return _Expr<_Closure, _Tp>(_Closure(__t, __v));        	        \
  }

_DEFINE_BINARY_OPERATOR(+, __plus)
_DEFINE_BINARY_OPERATOR(-, __minus)
_DEFINE_BINARY_OPERATOR(*, __multiplies)
_DEFINE_BINARY_OPERATOR(/, __divides)
_DEFINE_BINARY_OPERATOR(%, __modulus)
_DEFINE_BINARY_OPERATOR(^, __bitwise_xor)
_DEFINE_BINARY_OPERATOR(&, __bitwise_and)
_DEFINE_BINARY_OPERATOR(|, __bitwise_or)
_DEFINE_BINARY_OPERATOR(<<, __shift_left)
_DEFINE_BINARY_OPERATOR(>>, __shift_right)
_DEFINE_BINARY_OPERATOR(&&, __logical_and)
_DEFINE_BINARY_OPERATOR(||, __logical_or)
_DEFINE_BINARY_OPERATOR(==, __equal_to)
_DEFINE_BINARY_OPERATOR(!=, __not_equal_to)
_DEFINE_BINARY_OPERATOR(<, __less)
_DEFINE_BINARY_OPERATOR(>, __greater)
_DEFINE_BINARY_OPERATOR(<=, __less_equal)
_DEFINE_BINARY_OPERATOR(>=, __greater_equal)

} // namespace std

#endif // _CPP_VALARRAY

// Local Variables:
// mode:c++
// End:
