// The template and inlines for the -*- C++ -*- slice_array class.

// Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002, 2004, 2005
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

/** @file slice_array.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

// Written by Gabriel Dos Reis <Gabriel.Dos-Reis@DPTMaths.ENS-Cachan.Fr>

#ifndef _SLICE_ARRAY_H
#define _SLICE_ARRAY_H 1

#pragma GCC system_header

_GLIBCXX_BEGIN_NAMESPACE(std)

  /**
   *  @brief  Class defining one-dimensional subset of an array.
   *
   *  The slice class represents a one-dimensional subset of an array,
   *  specified by three parameters: start offset, size, and stride.  The
   *  start offset is the index of the first element of the array that is part
   *  of the subset.  The size is the total number of elements in the subset.
   *  Stride is the distance between each successive array element to include
   *  in the subset.
   *
   *  For example, with an array of size 10, and a slice with offset 1, size 3
   *  and stride 2, the subset consists of array elements 1, 3, and 5.
   */
  class slice
  {
  public:
    ///  Construct an empty slice.
    slice();

    /**
     *  @brief  Construct a slice.
     *
     *  @param  o  Offset in array of first element.
     *  @param  d  Number of elements in slice.
     *  @param  s  Stride between array elements.
     */
    slice(size_t, size_t, size_t);

    ///  Return array offset of first slice element.
    size_t start() const;
    ///  Return size of slice.
    size_t size() const;
    ///  Return array stride of slice.
    size_t stride() const;

  private:
    size_t _M_off;                      // offset
    size_t _M_sz;			// size
    size_t _M_st;			// stride unit
  };

  // The default constructor constructor is not required to initialize
  // data members with any meaningful values, so we choose to do nothing.
  inline
  slice::slice() {}

  inline
  slice::slice(size_t __o, size_t __d, size_t __s)
  : _M_off(__o), _M_sz(__d), _M_st(__s) {}

  inline size_t
  slice::start() const
  { return _M_off; }

  inline size_t
  slice::size() const
  { return _M_sz; }

  inline size_t
  slice::stride() const
  { return _M_st; }

  /**
   *  @brief  Reference to one-dimensional subset of an array.
   *
   *  A slice_array is a reference to the actual elements of an array
   *  specified by a slice.  The way to get a slice_array is to call
   *  operator[](slice) on a valarray.  The returned slice_array then permits
   *  carrying operations out on the referenced subset of elements in the
   *  original valarray.  For example, operator+=(valarray) will add values
   *  to the subset of elements in the underlying valarray this slice_array
   *  refers to.
   *
   *  @param  Tp  Element type.
   */
  template<typename _Tp>
    class slice_array
    {
    public:
      typedef _Tp value_type;

      // _GLIBCXX_RESOLVE_LIB_DEFECTS
      // 253. valarray helper functions are almost entirely useless

      ///  Copy constructor.  Both slices refer to the same underlying array.
      slice_array(const slice_array&);

      ///  Assignment operator.  Assigns slice elements to corresponding
      ///  elements of @a a.
      slice_array& operator=(const slice_array&);

      ///  Assign slice elements to corresponding elements of @a v.
      void operator=(const valarray<_Tp>&) const;
      ///  Multiply slice elements by corresponding elements of @a v.
      void operator*=(const valarray<_Tp>&) const;
      ///  Divide slice elements by corresponding elements of @a v.
      void operator/=(const valarray<_Tp>&) const;
      ///  Modulo slice elements by corresponding elements of @a v.
      void operator%=(const valarray<_Tp>&) const;
      ///  Add corresponding elements of @a v to slice elements.
      void operator+=(const valarray<_Tp>&) const;
      ///  Subtract corresponding elements of @a v from slice elements.
      void operator-=(const valarray<_Tp>&) const;
      ///  Logical xor slice elements with corresponding elements of @a v.
      void operator^=(const valarray<_Tp>&) const;
      ///  Logical and slice elements with corresponding elements of @a v.
      void operator&=(const valarray<_Tp>&) const;
      ///  Logical or slice elements with corresponding elements of @a v.
      void operator|=(const valarray<_Tp>&) const;
      ///  Left shift slice elements by corresponding elements of @a v.
      void operator<<=(const valarray<_Tp>&) const;
      ///  Right shift slice elements by corresponding elements of @a v.
      void operator>>=(const valarray<_Tp>&) const;
      ///  Assign all slice elements to @a t.
      void operator=(const _Tp &) const;
      //        ~slice_array ();

      template<class _Dom>
        void operator=(const _Expr<_Dom, _Tp>&) const;
      template<class _Dom>
	void operator*=(const _Expr<_Dom, _Tp>&) const;
      template<class _Dom>
	void operator/=(const _Expr<_Dom, _Tp>&) const;
      template<class _Dom>
	void operator%=(const _Expr<_Dom, _Tp>&) const;
      template<class _Dom>
	void operator+=(const _Expr<_Dom, _Tp>&) const;
      template<class _Dom>
	void operator-=(const _Expr<_Dom, _Tp>&) const;
      template<class _Dom>
	void operator^=(const _Expr<_Dom, _Tp>&) const;
      template<class _Dom>
	void operator&=(const _Expr<_Dom, _Tp>&) const;
      template<class _Dom>
	void operator|=(const _Expr<_Dom, _Tp>&) const;
      template<class _Dom>
	void operator<<=(const _Expr<_Dom, _Tp>&) const;
      template<class _Dom>
	void operator>>=(const _Expr<_Dom, _Tp>&) const;

    private:
      friend class valarray<_Tp>;
      slice_array(_Array<_Tp>, const slice&);

      const size_t      _M_sz;
      const size_t      _M_stride;
      const _Array<_Tp> _M_array;

      // not implemented
      slice_array();
    };

  template<typename _Tp>
    inline
    slice_array<_Tp>::slice_array(_Array<_Tp> __a, const slice& __s)
    : _M_sz(__s.size()), _M_stride(__s.stride()),
      _M_array(__a.begin() + __s.start()) {}

  template<typename _Tp>
    inline
    slice_array<_Tp>::slice_array(const slice_array<_Tp>& a)
    : _M_sz(a._M_sz), _M_stride(a._M_stride), _M_array(a._M_array) {}

  //    template<typename _Tp>
  //    inline slice_array<_Tp>::~slice_array () {}

  template<typename _Tp>
    inline slice_array<_Tp>&
    slice_array<_Tp>::operator=(const slice_array<_Tp>& __a)
    {
      std::__valarray_copy(__a._M_array, __a._M_sz, __a._M_stride,
			   _M_array, _M_stride);
      return *this;
    }

  template<typename _Tp>
    inline void
    slice_array<_Tp>::operator=(const _Tp& __t) const
    { std::__valarray_fill(_M_array, _M_sz, _M_stride, __t); }

  template<typename _Tp>
    inline void
    slice_array<_Tp>::operator=(const valarray<_Tp>& __v) const
    { std::__valarray_copy(_Array<_Tp>(__v), _M_array, _M_sz, _M_stride); }

  template<typename _Tp>
  template<class _Dom>
    inline void
    slice_array<_Tp>::operator=(const _Expr<_Dom,_Tp>& __e) const
    { std::__valarray_copy(__e, _M_sz, _M_array, _M_stride); }

#undef _DEFINE_VALARRAY_OPERATOR
#define _DEFINE_VALARRAY_OPERATOR(_Op,_Name)				\
  template<typename _Tp>						\
    inline void								\
    slice_array<_Tp>::operator _Op##=(const valarray<_Tp>& __v) const	\
    {									\
      _Array_augmented_##_Name(_M_array, _M_sz, _M_stride, _Array<_Tp>(__v));\
    }									\
									\
  template<typename _Tp>                                                \
    template<class _Dom>				                \
      inline void							\
      slice_array<_Tp>::operator _Op##=(const _Expr<_Dom,_Tp>& __e) const\
      {									\
	  _Array_augmented_##_Name(_M_array, _M_stride, __e, _M_sz);	\
      }


_DEFINE_VALARRAY_OPERATOR(*, __multiplies)
_DEFINE_VALARRAY_OPERATOR(/, __divides)
_DEFINE_VALARRAY_OPERATOR(%, __modulus)
_DEFINE_VALARRAY_OPERATOR(+, __plus)
_DEFINE_VALARRAY_OPERATOR(-, __minus)
_DEFINE_VALARRAY_OPERATOR(^, __bitwise_xor)
_DEFINE_VALARRAY_OPERATOR(&, __bitwise_and)
_DEFINE_VALARRAY_OPERATOR(|, __bitwise_or)
_DEFINE_VALARRAY_OPERATOR(<<, __shift_left)
_DEFINE_VALARRAY_OPERATOR(>>, __shift_right)

#undef _DEFINE_VALARRAY_OPERATOR

_GLIBCXX_END_NAMESPACE

#endif /* _SLICE_ARRAY_H */
