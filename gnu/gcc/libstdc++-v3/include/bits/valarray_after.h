// The template and inlines for the -*- C++ -*- internal _Meta class.

// Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005
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

/** @file valarray_after.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

// Written by Gabriel Dos Reis <Gabriel.Dos-Reis@cmla.ens-cachan.fr>

#ifndef _VALARRAY_AFTER_H
#define _VALARRAY_AFTER_H 1

#pragma GCC system_header

_GLIBCXX_BEGIN_NAMESPACE(std)

  //
  // gslice_array closure.
  //
  template<class _Dom>
    class _GBase
    {
    public:
      typedef typename _Dom::value_type value_type;
      
      _GBase (const _Dom& __e, const valarray<size_t>& __i)
      : _M_expr (__e), _M_index(__i) {}
      
      value_type
      operator[] (size_t __i) const
      { return _M_expr[_M_index[__i]]; }
      
      size_t
      size () const
      { return _M_index.size(); }

    private:
      const _Dom&	      _M_expr;
      const valarray<size_t>& _M_index;
    };

  template<typename _Tp>
    class _GBase<_Array<_Tp> >
    {
    public:
      typedef _Tp value_type;
      
      _GBase (_Array<_Tp> __a, const valarray<size_t>& __i)
      : _M_array (__a), _M_index(__i) {}
      
      value_type
      operator[] (size_t __i) const
      { return _M_array._M_data[_M_index[__i]]; }
      
      size_t
      size () const
      { return _M_index.size(); }

    private:
      const _Array<_Tp>       _M_array;
      const valarray<size_t>& _M_index;
    };

  template<class _Dom>
    struct _GClos<_Expr, _Dom>
    : _GBase<_Dom>
    {
      typedef _GBase<_Dom> _Base;
      typedef typename _Base::value_type value_type;
      
      _GClos (const _Dom& __e, const valarray<size_t>& __i)
      : _Base (__e, __i) {}
    };

  template<typename _Tp>
    struct _GClos<_ValArray, _Tp>
    : _GBase<_Array<_Tp> >
    {
      typedef _GBase<_Array<_Tp> > _Base;
      typedef typename _Base::value_type value_type;
      
      _GClos (_Array<_Tp> __a, const valarray<size_t>& __i)
      : _Base (__a, __i) {}
    };

  //
  // indirect_array closure
  //
  template<class _Dom>
    class _IBase
    {
    public:
      typedef typename _Dom::value_type value_type;

      _IBase (const _Dom& __e, const valarray<size_t>& __i)
      : _M_expr (__e), _M_index (__i) {}
      
      value_type
      operator[] (size_t __i) const
      { return _M_expr[_M_index[__i]]; }
      
      size_t
      size() const
      { return _M_index.size(); }

    private:
      const _Dom&	      _M_expr;
      const valarray<size_t>& _M_index;
    };

  template<class _Dom>
    struct _IClos<_Expr, _Dom>
    : _IBase<_Dom>
    {
      typedef _IBase<_Dom> _Base;
      typedef typename _Base::value_type value_type;
      
      _IClos (const _Dom& __e, const valarray<size_t>& __i)
      : _Base (__e, __i) {}
    };

  template<typename _Tp>
    struct _IClos<_ValArray, _Tp>
    : _IBase<valarray<_Tp> >
    {
      typedef _IBase<valarray<_Tp> > _Base;
      typedef _Tp value_type;
      
      _IClos (const valarray<_Tp>& __a, const valarray<size_t>& __i)
      : _Base (__a, __i) {}
    };
  
  //
  // class _Expr
  //
  template<class _Clos, typename _Tp>
    class _Expr
    {
    public:
      typedef _Tp value_type;

      _Expr(const _Clos&);

      const _Clos& operator()() const;

      value_type operator[](size_t) const;
      valarray<value_type> operator[](slice) const;
      valarray<value_type> operator[](const gslice&) const;
      valarray<value_type> operator[](const valarray<bool>&) const;
      valarray<value_type> operator[](const valarray<size_t>&) const;

      _Expr<_UnClos<__unary_plus, std::_Expr, _Clos>, value_type>
      operator+() const;

      _Expr<_UnClos<__negate, std::_Expr, _Clos>, value_type>
      operator-() const;

      _Expr<_UnClos<__bitwise_not, std::_Expr, _Clos>, value_type>
      operator~() const;

      _Expr<_UnClos<__logical_not, std::_Expr, _Clos>, bool>
      operator!() const;

      size_t size() const;
      value_type sum() const;

      valarray<value_type> shift(int) const;
      valarray<value_type> cshift(int) const;

      value_type min() const;
      value_type max() const;

      valarray<value_type> apply(value_type (*)(const value_type&)) const;
      valarray<value_type> apply(value_type (*)(value_type)) const;

    private:
      const _Clos _M_closure;
    };

  template<class _Clos, typename _Tp>
    inline
    _Expr<_Clos, _Tp>::_Expr(const _Clos& __c) : _M_closure(__c) {}

  template<class _Clos, typename _Tp>
    inline const _Clos&
    _Expr<_Clos, _Tp>::operator()() const
    { return _M_closure; }

  template<class _Clos, typename _Tp>
    inline _Tp
    _Expr<_Clos, _Tp>::operator[](size_t __i) const
    { return _M_closure[__i]; }

  template<class _Clos, typename _Tp>
    inline valarray<_Tp>
    _Expr<_Clos, _Tp>::operator[](slice __s) const
    {
      valarray<_Tp> __v = valarray<_Tp>(*this)[__s];
      return __v;
    }

  template<class _Clos, typename _Tp>
    inline valarray<_Tp>
    _Expr<_Clos, _Tp>::operator[](const gslice& __gs) const
    {
      valarray<_Tp> __v = valarray<_Tp>(*this)[__gs];
      return __v;
    }

  template<class _Clos, typename _Tp>
    inline valarray<_Tp>
    _Expr<_Clos, _Tp>::operator[](const valarray<bool>& __m) const
    {
      valarray<_Tp> __v = valarray<_Tp>(*this)[__m];
      return __v;
    }

  template<class _Clos, typename _Tp>
    inline valarray<_Tp>
    _Expr<_Clos, _Tp>::operator[](const valarray<size_t>& __i) const
    {
      valarray<_Tp> __v = valarray<_Tp>(*this)[__i];
      return __v;
    }

  template<class _Clos, typename _Tp>
    inline size_t
    _Expr<_Clos, _Tp>::size() const
    { return _M_closure.size(); }

  template<class _Clos, typename _Tp>
    inline valarray<_Tp>
    _Expr<_Clos, _Tp>::shift(int __n) const
    {
      valarray<_Tp> __v = valarray<_Tp>(*this).shift(__n);
      return __v;
    }

  template<class _Clos, typename _Tp>
    inline valarray<_Tp>
    _Expr<_Clos, _Tp>::cshift(int __n) const
    {
      valarray<_Tp> __v = valarray<_Tp>(*this).cshift(__n);
      return __v;
    }

  template<class _Clos, typename _Tp>
    inline valarray<_Tp>
    _Expr<_Clos, _Tp>::apply(_Tp __f(const _Tp&)) const
    {
      valarray<_Tp> __v = valarray<_Tp>(*this).apply(__f);
      return __v;
    }

  template<class _Clos, typename _Tp>
    inline valarray<_Tp>
    _Expr<_Clos, _Tp>::apply(_Tp __f(_Tp)) const
    {
      valarray<_Tp> __v = valarray<_Tp>(*this).apply(__f);
      return __v;
    }

  // XXX: replace this with a more robust summation algorithm.
  template<class _Clos, typename _Tp>
    inline _Tp
    _Expr<_Clos, _Tp>::sum() const
    {
      size_t __n = _M_closure.size();
      if (__n == 0)
	return _Tp();
      else
	{
	  _Tp __s = _M_closure[--__n];
	  while (__n != 0)
	    __s += _M_closure[--__n];
	  return __s;
        }
    }

  template<class _Clos, typename _Tp>
    inline _Tp
    _Expr<_Clos, _Tp>::min() const
    { return __valarray_min(_M_closure); }

  template<class _Clos, typename _Tp>
    inline _Tp
    _Expr<_Clos, _Tp>::max() const
    { return __valarray_max(_M_closure); }

  template<class _Dom, typename _Tp>
    inline _Expr<_UnClos<__logical_not, _Expr, _Dom>, bool>
    _Expr<_Dom, _Tp>::operator!() const
    {
      typedef _UnClos<__logical_not, std::_Expr, _Dom> _Closure;
      return _Expr<_Closure, _Tp>(_Closure(this->_M_closure));
    }

#define _DEFINE_EXPR_UNARY_OPERATOR(_Op, _Name)                           \
  template<class _Dom, typename _Tp>                                      \
    inline _Expr<_UnClos<_Name, std::_Expr, _Dom>, _Tp>                   \
    _Expr<_Dom, _Tp>::operator _Op() const                                \
    {                                                                     \
      typedef _UnClos<_Name, std::_Expr, _Dom> _Closure;                  \
      return _Expr<_Closure, _Tp>(_Closure(this->_M_closure));            \
    }

    _DEFINE_EXPR_UNARY_OPERATOR(+, __unary_plus)
    _DEFINE_EXPR_UNARY_OPERATOR(-, __negate)
    _DEFINE_EXPR_UNARY_OPERATOR(~, __bitwise_not)

#undef _DEFINE_EXPR_UNARY_OPERATOR

#define _DEFINE_EXPR_BINARY_OPERATOR(_Op, _Name)                        \
  template<class _Dom1, class _Dom2>					\
    inline _Expr<_BinClos<_Name, _Expr, _Expr, _Dom1, _Dom2>,           \
           typename __fun<_Name, typename _Dom1::value_type>::result_type> \
    operator _Op(const _Expr<_Dom1, typename _Dom1::value_type>& __v,   \
	         const _Expr<_Dom2, typename _Dom2::value_type>& __w)   \
    {                                                                   \
      typedef typename _Dom1::value_type _Arg;                          \
      typedef typename __fun<_Name, _Arg>::result_type _Value;          \
      typedef _BinClos<_Name, _Expr, _Expr, _Dom1, _Dom2> _Closure;     \
      return _Expr<_Closure, _Value>(_Closure(__v(), __w()));           \
    }                                                                   \
                                                                        \
  template<class _Dom>                                                  \
    inline _Expr<_BinClos<_Name, _Expr, _Constant, _Dom,                \
                          typename _Dom::value_type>,                   \
             typename __fun<_Name, typename _Dom::value_type>::result_type> \
    operator _Op(const _Expr<_Dom, typename _Dom::value_type>& __v,     \
                 const typename _Dom::value_type& __t)                  \
    {                                                                   \
      typedef typename _Dom::value_type _Arg;                           \
      typedef typename __fun<_Name, _Arg>::result_type _Value;          \
      typedef _BinClos<_Name, _Expr, _Constant, _Dom, _Arg> _Closure;   \
      return _Expr<_Closure, _Value>(_Closure(__v(), __t));             \
    }                                                                   \
                                                                        \
  template<class _Dom>                                                  \
    inline _Expr<_BinClos<_Name, _Constant, _Expr,                      \
                          typename _Dom::value_type, _Dom>,             \
             typename __fun<_Name, typename _Dom::value_type>::result_type> \
    operator _Op(const typename _Dom::value_type& __t,                  \
                 const _Expr<_Dom, typename _Dom::value_type>& __v)     \
    {                                                                   \
      typedef typename _Dom::value_type _Arg;                           \
      typedef typename __fun<_Name, _Arg>::result_type _Value;          \
      typedef _BinClos<_Name, _Constant, _Expr, _Arg, _Dom> _Closure;   \
      return _Expr<_Closure, _Value>(_Closure(__t, __v()));             \
    }                                                                   \
                                                                        \
  template<class _Dom>                                                  \
    inline _Expr<_BinClos<_Name, _Expr, _ValArray,                      \
                          _Dom, typename _Dom::value_type>,             \
             typename __fun<_Name, typename _Dom::value_type>::result_type> \
    operator _Op(const _Expr<_Dom,typename _Dom::value_type>& __e,      \
                 const valarray<typename _Dom::value_type>& __v)        \
    {                                                                   \
      typedef typename _Dom::value_type _Arg;                           \
      typedef typename __fun<_Name, _Arg>::result_type _Value;          \
      typedef _BinClos<_Name, _Expr, _ValArray, _Dom, _Arg> _Closure;   \
      return _Expr<_Closure, _Value>(_Closure(__e(), __v));             \
    }                                                                   \
                                                                        \
  template<class _Dom>                                                  \
    inline _Expr<_BinClos<_Name, _ValArray, _Expr,                      \
                 typename _Dom::value_type, _Dom>,                      \
             typename __fun<_Name, typename _Dom::value_type>::result_type> \
    operator _Op(const valarray<typename _Dom::value_type>& __v,        \
                 const _Expr<_Dom, typename _Dom::value_type>& __e)     \
    {                                                                   \
      typedef typename _Dom::value_type _Tp;                            \
      typedef typename __fun<_Name, _Tp>::result_type _Value;           \
      typedef _BinClos<_Name, _ValArray, _Expr, _Tp, _Dom> _Closure;    \
      return _Expr<_Closure, _Value>(_Closure(__v, __e ()));            \
    }

    _DEFINE_EXPR_BINARY_OPERATOR(+, __plus)
    _DEFINE_EXPR_BINARY_OPERATOR(-, __minus)
    _DEFINE_EXPR_BINARY_OPERATOR(*, __multiplies)
    _DEFINE_EXPR_BINARY_OPERATOR(/, __divides)
    _DEFINE_EXPR_BINARY_OPERATOR(%, __modulus)
    _DEFINE_EXPR_BINARY_OPERATOR(^, __bitwise_xor)
    _DEFINE_EXPR_BINARY_OPERATOR(&, __bitwise_and)
    _DEFINE_EXPR_BINARY_OPERATOR(|, __bitwise_or)
    _DEFINE_EXPR_BINARY_OPERATOR(<<, __shift_left)
    _DEFINE_EXPR_BINARY_OPERATOR(>>, __shift_right)
    _DEFINE_EXPR_BINARY_OPERATOR(&&, __logical_and)
    _DEFINE_EXPR_BINARY_OPERATOR(||, __logical_or)
    _DEFINE_EXPR_BINARY_OPERATOR(==, __equal_to)
    _DEFINE_EXPR_BINARY_OPERATOR(!=, __not_equal_to)
    _DEFINE_EXPR_BINARY_OPERATOR(<, __less)
    _DEFINE_EXPR_BINARY_OPERATOR(>, __greater)
    _DEFINE_EXPR_BINARY_OPERATOR(<=, __less_equal)
    _DEFINE_EXPR_BINARY_OPERATOR(>=, __greater_equal)

#undef _DEFINE_EXPR_BINARY_OPERATOR

#define _DEFINE_EXPR_UNARY_FUNCTION(_Name)                               \
  template<class _Dom>                                                   \
    inline _Expr<_UnClos<__##_Name, _Expr, _Dom>,                        \
                 typename _Dom::value_type>                              \
    _Name(const _Expr<_Dom, typename _Dom::value_type>& __e)             \
    {                                                                    \
      typedef typename _Dom::value_type _Tp;                             \
      typedef _UnClos<__##_Name, _Expr, _Dom> _Closure;                  \
      return _Expr<_Closure, _Tp>(_Closure(__e()));                      \
    }                                                                    \
                                                                         \
  template<typename _Tp>                                                 \
    inline _Expr<_UnClos<__##_Name, _ValArray, _Tp>, _Tp>                \
    _Name(const valarray<_Tp>& __v)                                      \
    {                                                                    \
      typedef _UnClos<__##_Name, _ValArray, _Tp> _Closure;               \
      return _Expr<_Closure, _Tp>(_Closure(__v));                        \
    }

    _DEFINE_EXPR_UNARY_FUNCTION(abs)
    _DEFINE_EXPR_UNARY_FUNCTION(cos)
    _DEFINE_EXPR_UNARY_FUNCTION(acos)
    _DEFINE_EXPR_UNARY_FUNCTION(cosh)
    _DEFINE_EXPR_UNARY_FUNCTION(sin)
    _DEFINE_EXPR_UNARY_FUNCTION(asin)
    _DEFINE_EXPR_UNARY_FUNCTION(sinh)
    _DEFINE_EXPR_UNARY_FUNCTION(tan)
    _DEFINE_EXPR_UNARY_FUNCTION(tanh)
    _DEFINE_EXPR_UNARY_FUNCTION(atan)
    _DEFINE_EXPR_UNARY_FUNCTION(exp)
    _DEFINE_EXPR_UNARY_FUNCTION(log)
    _DEFINE_EXPR_UNARY_FUNCTION(log10)
    _DEFINE_EXPR_UNARY_FUNCTION(sqrt)

#undef _DEFINE_EXPR_UNARY_FUNCTION

#define _DEFINE_EXPR_BINARY_FUNCTION(_Fun)                             \
  template<class _Dom1, class _Dom2>                                   \
    inline _Expr<_BinClos<__##_Fun, _Expr, _Expr, _Dom1, _Dom2>,       \
		 typename _Dom1::value_type>                           \
    _Fun(const _Expr<_Dom1, typename _Dom1::value_type>& __e1,         \
	  const _Expr<_Dom2, typename _Dom2::value_type>& __e2)        \
    {                                                                  \
      typedef typename _Dom1::value_type _Tp;                          \
      typedef _BinClos<__##_Fun, _Expr, _Expr, _Dom1, _Dom2> _Closure; \
      return _Expr<_Closure, _Tp>(_Closure(__e1(), __e2()));           \
    }                                                                  \
                                                                       \
  template<class _Dom>                                                 \
    inline _Expr<_BinClos<__##_Fun, _Expr, _ValArray, _Dom,            \
			  typename _Dom::value_type>,                  \
		 typename _Dom::value_type>                            \
    _Fun(const _Expr<_Dom, typename _Dom::value_type>& __e,            \
	 const valarray<typename _Dom::value_type>& __v)               \
    {                                                                  \
      typedef typename _Dom::value_type _Tp;                           \
      typedef _BinClos<__##_Fun, _Expr, _ValArray, _Dom, _Tp> _Closure; \
      return _Expr<_Closure, _Tp>(_Closure(__e(), __v));               \
    }                                                                  \
                                                                       \
  template<class _Dom>                                                 \
    inline _Expr<_BinClos<__##_Fun, _ValArray, _Expr,                  \
			  typename _Dom::value_type, _Dom>,            \
		 typename _Dom::value_type>                            \
    _Fun(const valarray<typename _Dom::valarray>& __v,                 \
	 const _Expr<_Dom, typename _Dom::value_type>& __e)            \
    {                                                                  \
      typedef typename _Dom::value_type _Tp;                           \
      typedef _BinClos<__##_Fun, _ValArray, _Expr, _Tp, _Dom> _Closure; \
      return _Expr<_Closure, _Tp>(_Closure(__v, __e()));               \
    }                                                                  \
                                                                       \
  template<class _Dom>                                                 \
    inline _Expr<_BinClos<__##_Fun, _Expr, _Constant, _Dom,            \
			  typename _Dom::value_type>,                  \
		 typename _Dom::value_type>                            \
    _Fun(const _Expr<_Dom, typename _Dom::value_type>& __e,            \
	 const typename _Dom::value_type& __t)                         \
    {                                                                  \
      typedef typename _Dom::value_type _Tp;                           \
      typedef _BinClos<__##_Fun, _Expr, _Constant, _Dom, _Tp> _Closure;\
      return _Expr<_Closure, _Tp>(_Closure(__e(), __t));               \
    }                                                                  \
                                                                       \
  template<class _Dom>                                                 \
    inline _Expr<_BinClos<__##_Fun, _Constant, _Expr,                  \
			  typename _Dom::value_type, _Dom>,            \
		 typename _Dom::value_type>                            \
    _Fun(const typename _Dom::value_type& __t,                         \
	 const _Expr<_Dom, typename _Dom::value_type>& __e)            \
    {                                                                  \
      typedef typename _Dom::value_type _Tp;                           \
      typedef _BinClos<__##_Fun, _Constant, _Expr, _Tp, _Dom> _Closure; \
      return _Expr<_Closure, _Tp>(_Closure(__t, __e()));               \
    }                                                                  \
                                                                       \
  template<typename _Tp>                                               \
    inline _Expr<_BinClos<__##_Fun, _ValArray, _ValArray, _Tp, _Tp>, _Tp> \
    _Fun(const valarray<_Tp>& __v, const valarray<_Tp>& __w)           \
    {                                                                  \
      typedef _BinClos<__##_Fun, _ValArray, _ValArray, _Tp, _Tp> _Closure; \
      return _Expr<_Closure, _Tp>(_Closure(__v, __w));                 \
    }                                                                  \
                                                                       \
  template<typename _Tp>                                               \
    inline _Expr<_BinClos<__##_Fun, _ValArray, _Constant, _Tp, _Tp>, _Tp> \
    _Fun(const valarray<_Tp>& __v, const _Tp& __t)                     \
    {                                                                  \
      typedef _BinClos<__##_Fun, _ValArray, _Constant, _Tp, _Tp> _Closure; \
      return _Expr<_Closure, _Tp>(_Closure(__v, __t));                 \
    }                                                                  \
								       \
  template<typename _Tp>                                               \
    inline _Expr<_BinClos<__##_Fun, _Constant, _ValArray, _Tp, _Tp>, _Tp> \
    _Fun(const _Tp& __t, const valarray<_Tp>& __v)                     \
    {                                                                  \
      typedef _BinClos<__##_Fun, _Constant, _ValArray, _Tp, _Tp> _Closure; \
      return _Expr<_Closure, _Tp>(_Closure(__t, __v));                 \
    }

_DEFINE_EXPR_BINARY_FUNCTION(atan2)
_DEFINE_EXPR_BINARY_FUNCTION(pow)

#undef _DEFINE_EXPR_BINARY_FUNCTION

_GLIBCXX_END_NAMESPACE

#endif /* _CPP_VALARRAY_AFTER_H */
