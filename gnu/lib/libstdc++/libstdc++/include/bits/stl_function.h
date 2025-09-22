// Functor implementations -*- C++ -*-

// Copyright (C) 2001, 2002 Free Software Foundation, Inc.
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
 * Copyright (c) 1996-1998
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

/** @file stl_function.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

#ifndef __GLIBCPP_INTERNAL_FUNCTION_H
#define __GLIBCPP_INTERNAL_FUNCTION_H

namespace std
{
// 20.3.1 base classes
/** @defgroup s20_3_1_base Functor Base Classes
 *  Function objects, or @e functors, are objects with an @c operator()
 *  defined and accessible.  They can be passed as arguments to algorithm
 *  templates and used in place of a function pointer.  Not only is the
 *  resulting expressiveness of the library increased, but the generated
 *  code can be more efficient than what you might write by hand.  When we
 *  refer to "functors," then, generally we include function pointers in
 *  the description as well.
 *
 *  Often, functors are only created as temporaries passed to algorithm
 *  calls, rather than being created as named variables.
 *
 *  Two examples taken from the standard itself follow.  To perform a
 *  by-element addition of two vectors @c a and @c b containing @c double,
 *  and put the result in @c a, use
 *  \code
 *  transform (a.begin(), a.end(), b.begin(), a.begin(), plus<double>());
 *  \endcode
 *  To negate every element in @c a, use
 *  \code
 *  transform(a.begin(), a.end(), a.begin(), negate<double>());
 *  \endcode
 *  The addition and negation functions will be inlined directly.
 *
 *  The standard functiors are derived from structs named @c unary_function
 *  and @c binary_function.  These two classes contain nothing but typedefs,
 *  to aid in generic (template) programming.  If you write your own
 *  functors, you might consider doing the same.
 *
 *  @{
*/
/**
 *  This is one of the @link s20_3_1_base functor base classes@endlink.
*/
template <class _Arg, class _Result>
struct unary_function {
  typedef _Arg argument_type;   ///< @c argument_type is the type of the argument (no surprises here)
  typedef _Result result_type;  ///< @c result_type is the return type
};

/**
 *  This is one of the @link s20_3_1_base functor base classes@endlink.
*/
template <class _Arg1, class _Arg2, class _Result>
struct binary_function {
  typedef _Arg1 first_argument_type;   ///< the type of the first argument (no surprises here)
  typedef _Arg2 second_argument_type;  ///< the type of the second argument
  typedef _Result result_type;         ///< type of the return type
};      
/** @}  */

// 20.3.2 arithmetic
/** @defgroup s20_3_2_arithmetic Arithmetic Classes
 *  Because basic math often needs to be done during an algorithm, the library
 *  provides functors for those operations.  See the documentation for
 *  @link s20_3_1_base the base classes@endlink for examples of their use.
 *
 *  @{
*/
/// One of the @link s20_3_2_arithmetic math functors@endlink.
template <class _Tp>
struct plus : public binary_function<_Tp,_Tp,_Tp> {
  _Tp operator()(const _Tp& __x, const _Tp& __y) const { return __x + __y; }
};

/// One of the @link s20_3_2_arithmetic math functors@endlink.
template <class _Tp>
struct minus : public binary_function<_Tp,_Tp,_Tp> {
  _Tp operator()(const _Tp& __x, const _Tp& __y) const { return __x - __y; }
};

/// One of the @link s20_3_2_arithmetic math functors@endlink.
template <class _Tp>
struct multiplies : public binary_function<_Tp,_Tp,_Tp> {
  _Tp operator()(const _Tp& __x, const _Tp& __y) const { return __x * __y; }
};

/// One of the @link s20_3_2_arithmetic math functors@endlink.
template <class _Tp>
struct divides : public binary_function<_Tp,_Tp,_Tp> {
  _Tp operator()(const _Tp& __x, const _Tp& __y) const { return __x / __y; }
};

/// One of the @link s20_3_2_arithmetic math functors@endlink.
template <class _Tp>
struct modulus : public binary_function<_Tp,_Tp,_Tp> 
{
  _Tp operator()(const _Tp& __x, const _Tp& __y) const { return __x % __y; }
};

/// One of the @link s20_3_2_arithmetic math functors@endlink.
template <class _Tp>
struct negate : public unary_function<_Tp,_Tp> 
{
  _Tp operator()(const _Tp& __x) const { return -__x; }
};
/** @}  */

// 20.3.3 comparisons
/** @defgroup s20_3_3_comparisons Comparison Classes
 *  The library provides six wrapper functors for all the basic comparisons
 *  in C++, like @c <.
 *
 *  @{
*/
/// One of the @link s20_3_3_comparisons comparison functors@endlink.
template <class _Tp>
struct equal_to : public binary_function<_Tp,_Tp,bool> 
{
  bool operator()(const _Tp& __x, const _Tp& __y) const { return __x == __y; }
};

/// One of the @link s20_3_3_comparisons comparison functors@endlink.
template <class _Tp>
struct not_equal_to : public binary_function<_Tp,_Tp,bool> 
{
  bool operator()(const _Tp& __x, const _Tp& __y) const { return __x != __y; }
};

/// One of the @link s20_3_3_comparisons comparison functors@endlink.
template <class _Tp>
struct greater : public binary_function<_Tp,_Tp,bool> 
{
  bool operator()(const _Tp& __x, const _Tp& __y) const { return __x > __y; }
};

/// One of the @link s20_3_3_comparisons comparison functors@endlink.
template <class _Tp>
struct less : public binary_function<_Tp,_Tp,bool> 
{
  bool operator()(const _Tp& __x, const _Tp& __y) const { return __x < __y; }
};

/// One of the @link s20_3_3_comparisons comparison functors@endlink.
template <class _Tp>
struct greater_equal : public binary_function<_Tp,_Tp,bool>
{
  bool operator()(const _Tp& __x, const _Tp& __y) const { return __x >= __y; }
};

/// One of the @link s20_3_3_comparisons comparison functors@endlink.
template <class _Tp>
struct less_equal : public binary_function<_Tp,_Tp,bool> 
{
  bool operator()(const _Tp& __x, const _Tp& __y) const { return __x <= __y; }
};
/** @}  */

// 20.3.4 logical operations
/** @defgroup s20_3_4_logical Boolean Operations Classes
 *  Here are wrapper functors for Boolean operations:  @c &&, @c ||, and @c !.
 *
 *  @{
*/
/// One of the @link s20_3_4_logical Boolean operations functors@endlink.
template <class _Tp>
struct logical_and : public binary_function<_Tp,_Tp,bool>
{
  bool operator()(const _Tp& __x, const _Tp& __y) const { return __x && __y; }
};

/// One of the @link s20_3_4_logical Boolean operations functors@endlink.
template <class _Tp>
struct logical_or : public binary_function<_Tp,_Tp,bool>
{
  bool operator()(const _Tp& __x, const _Tp& __y) const { return __x || __y; }
};

/// One of the @link s20_3_4_logical Boolean operations functors@endlink.
template <class _Tp>
struct logical_not : public unary_function<_Tp,bool>
{
  bool operator()(const _Tp& __x) const { return !__x; }
};
/** @}  */

// 20.3.5 negators
/** @defgroup s20_3_5_negators Negators
 *  The functions @c not1 and @c not2 each take a predicate functor
 *  and return an instance of @c unary_negate or
 *  @c binary_negate, respectively.  These classes are functors whose
 *  @c operator() performs the stored predicate function and then returns
 *  the negation of the result.
 *
 *  For example, given a vector of integers and a trivial predicate,
 *  \code
 *  struct IntGreaterThanThree
 *    : public std::unary_function<int, bool>
 *  {
 *      bool operator() (int x) { return x > 3; }
 *  };
 *  
 *  std::find_if (v.begin(), v.end(), not1(IntGreaterThanThree()));
 *  \endcode
 *  The call to @c find_if will locate the first index (i) of @c v for which
 *  "!(v[i] > 3)" is true.
 *
 *  The not1/unary_negate combination works on predicates taking a single
 *  argument.  The not2/binary_negate combination works on predicates which
 *  take two arguments.
 *
 *  @{
*/
/// One of the @link s20_3_5_negators negation functors@endlink.
template <class _Predicate>
class unary_negate
  : public unary_function<typename _Predicate::argument_type, bool> {
protected:
  _Predicate _M_pred;
public:
  explicit unary_negate(const _Predicate& __x) : _M_pred(__x) {}
  bool operator()(const typename _Predicate::argument_type& __x) const {
    return !_M_pred(__x);
  }
};

/// One of the @link s20_3_5_negators negation functors@endlink.
template <class _Predicate>
inline unary_negate<_Predicate> 
not1(const _Predicate& __pred)
{
  return unary_negate<_Predicate>(__pred);
}

/// One of the @link s20_3_5_negators negation functors@endlink.
template <class _Predicate> 
class binary_negate 
  : public binary_function<typename _Predicate::first_argument_type,
                           typename _Predicate::second_argument_type,
                           bool> {
protected:
  _Predicate _M_pred;
public:
  explicit binary_negate(const _Predicate& __x) : _M_pred(__x) {}
  bool operator()(const typename _Predicate::first_argument_type& __x, 
                  const typename _Predicate::second_argument_type& __y) const
  {
    return !_M_pred(__x, __y); 
  }
};

/// One of the @link s20_3_5_negators negation functors@endlink.
template <class _Predicate>
inline binary_negate<_Predicate> 
not2(const _Predicate& __pred)
{
  return binary_negate<_Predicate>(__pred);
}
/** @}  */

// 20.3.6 binders
/** @defgroup s20_3_6_binder Binder Classes
 *  Binders turn functions/functors with two arguments into functors with
 *  a single argument, storing an argument to be applied later.  For
 *  example, an variable @c B of type @c binder1st is constructed from a functor
 *  @c f and an argument @c x.  Later, B's @c operator() is called with a
 *  single argument @c y.  The return value is the value of @c f(x,y).
 *  @c B can be "called" with various arguments (y1, y2, ...) and will in
 *  turn call @c f(x,y1), @c f(x,y2), ...
 *
 *  The function @c bind1st is provided to save some typing.  It takes the
 *  function and an argument as parameters, and returns an instance of
 *  @c binder1st.
 *
 *  The type @c binder2nd and its creator function @c bind2nd do the same
 *  thing, but the stored argument is passed as the second parameter instead
 *  of the first, e.g., @c bind2nd(std::minus<float>,1.3) will create a
 *  functor whose @c operator() accepts a floating-point number, subtracts
 *  1.3 from it, and returns the result.  (If @c bind1st had been used,
 *  the functor would perform "1.3 - x" instead.
 *
 *  Creator-wrapper functions like @c bind1st are intended to be used in
 *  calling algorithms.  Their return values will be temporary objects.
 *  (The goal is to not require you to type names like
 *  @c std::binder1st<std::plus<int>> for declaring a variable to hold the
 *  return value from @c bind1st(std::plus<int>,5).
 *
 *  These become more useful when combined with the composition functions.
 *
 *  @{
*/
/// One of the @link s20_3_6_binder binder functors@endlink.
template <class _Operation> 
class binder1st
  : public unary_function<typename _Operation::second_argument_type,
                          typename _Operation::result_type> {
protected:
  _Operation op;
  typename _Operation::first_argument_type value;
public:
  binder1st(const _Operation& __x,
            const typename _Operation::first_argument_type& __y)
      : op(__x), value(__y) {}
  typename _Operation::result_type
  operator()(const typename _Operation::second_argument_type& __x) const {
    return op(value, __x); 
  }
#ifdef _GLIBCPP_RESOLVE_LIB_DEFECTS
  //109.  Missing binders for non-const sequence elements
  typename _Operation::result_type
  operator()(typename _Operation::second_argument_type& __x) const {
    return op(value, __x); 
  }
#endif
};

/// One of the @link s20_3_6_binder binder functors@endlink.
template <class _Operation, class _Tp>
inline binder1st<_Operation> 
bind1st(const _Operation& __fn, const _Tp& __x) 
{
  typedef typename _Operation::first_argument_type _Arg1_type;
  return binder1st<_Operation>(__fn, _Arg1_type(__x));
}

/// One of the @link s20_3_6_binder binder functors@endlink.
template <class _Operation> 
class binder2nd
  : public unary_function<typename _Operation::first_argument_type,
                          typename _Operation::result_type> {
protected:
  _Operation op;
  typename _Operation::second_argument_type value;
public:
  binder2nd(const _Operation& __x,
            const typename _Operation::second_argument_type& __y) 
      : op(__x), value(__y) {}
  typename _Operation::result_type
  operator()(const typename _Operation::first_argument_type& __x) const {
    return op(__x, value); 
  }
#ifdef _GLIBCPP_RESOLVE_LIB_DEFECTS
  //109.  Missing binders for non-const sequence elements
  typename _Operation::result_type
  operator()(typename _Operation::first_argument_type& __x) const {
    return op(__x, value); 
  }
#endif
};

/// One of the @link s20_3_6_binder binder functors@endlink.
template <class _Operation, class _Tp>
inline binder2nd<_Operation> 
bind2nd(const _Operation& __fn, const _Tp& __x) 
{
  typedef typename _Operation::second_argument_type _Arg2_type;
  return binder2nd<_Operation>(__fn, _Arg2_type(__x));
}
/** @}  */

// 20.3.7 adaptors pointers functions
/** @defgroup s20_3_7_adaptors Adaptors for pointers to functions
 *  The advantage of function objects over pointers to functions is that
 *  the objects in the standard library declare nested typedefs describing
 *  their argument and result types with uniform names (e.g., @c result_type
 *  from the base classes @c unary_function and @c binary_function).
 *  Sometimes those typedefs are required, not just optional.
 *
 *  Adaptors are provided to turn pointers to unary (single-argument) and
 *  binary (double-argument) functions into function objects.  The long-winded
 *  functor @c pointer_to_unary_function is constructed with a function
 *  pointer @c f, and its @c operator() called with argument @c x returns
 *  @c f(x).  The functor @c pointer_to_binary_function does the same thing,
 *  but with a double-argument @c f and @c operator().
 *
 *  The function @c ptr_fun takes a pointer-to-function @c f and constructs
 *  an instance of the appropriate functor.
 *
 *  @{
*/
/// One of the @link s20_3_7_adaptors adaptors for function pointers@endlink.
template <class _Arg, class _Result>
class pointer_to_unary_function : public unary_function<_Arg, _Result> {
protected:
  _Result (*_M_ptr)(_Arg);
public:
  pointer_to_unary_function() {}
  explicit pointer_to_unary_function(_Result (*__x)(_Arg)) : _M_ptr(__x) {}
  _Result operator()(_Arg __x) const { return _M_ptr(__x); }
};

/// One of the @link s20_3_7_adaptors adaptors for function pointers@endlink.
template <class _Arg, class _Result>
inline pointer_to_unary_function<_Arg, _Result> ptr_fun(_Result (*__x)(_Arg))
{
  return pointer_to_unary_function<_Arg, _Result>(__x);
}

/// One of the @link s20_3_7_adaptors adaptors for function pointers@endlink.
template <class _Arg1, class _Arg2, class _Result>
class pointer_to_binary_function : 
  public binary_function<_Arg1,_Arg2,_Result> {
protected:
    _Result (*_M_ptr)(_Arg1, _Arg2);
public:
    pointer_to_binary_function() {}
    explicit pointer_to_binary_function(_Result (*__x)(_Arg1, _Arg2)) 
      : _M_ptr(__x) {}
    _Result operator()(_Arg1 __x, _Arg2 __y) const {
      return _M_ptr(__x, __y);
    }
};

/// One of the @link s20_3_7_adaptors adaptors for function pointers@endlink.
template <class _Arg1, class _Arg2, class _Result>
inline pointer_to_binary_function<_Arg1,_Arg2,_Result> 
ptr_fun(_Result (*__x)(_Arg1, _Arg2)) {
  return pointer_to_binary_function<_Arg1,_Arg2,_Result>(__x);
}
/** @}  */

template <class _Tp>
struct _Identity : public unary_function<_Tp,_Tp> {
  _Tp& operator()(_Tp& __x) const { return __x; }
  const _Tp& operator()(const _Tp& __x) const { return __x; }
};

template <class _Pair>
struct _Select1st : public unary_function<_Pair, typename _Pair::first_type> {
  typename _Pair::first_type& operator()(_Pair& __x) const {
    return __x.first;
  }
  const typename _Pair::first_type& operator()(const _Pair& __x) const {
    return __x.first;
  }
};

template <class _Pair>
struct _Select2nd : public unary_function<_Pair, typename _Pair::second_type>
{
  typename _Pair::second_type& operator()(_Pair& __x) const {
    return __x.second;
  }
  const typename _Pair::second_type& operator()(const _Pair& __x) const {
    return __x.second;
  }
};

// 20.3.8 adaptors pointers members
/** @defgroup s20_3_8_memadaptors Adaptors for pointers to members
 *  There are a total of 16 = 2^4 function objects in this family.
 *   (1) Member functions taking no arguments vs member functions taking
 *        one argument.
 *   (2) Call through pointer vs call through reference.
 *   (3) Member function with void return type vs member function with
 *       non-void return type.
 *   (4) Const vs non-const member function.
 *
 *  Note that choice (3) is nothing more than a workaround: according
 *   to the draft, compilers should handle void and non-void the same way.
 *   This feature is not yet widely implemented, though.  You can only use
 *   member functions returning void if your compiler supports partial
 *   specialization.
 *
 *  All of this complexity is in the function objects themselves.  You can
 *   ignore it by using the helper function mem_fun and mem_fun_ref,
 *   which create whichever type of adaptor is appropriate.
 *
 *  @{
*/
/// One of the @link s20_3_8_memadaptors adaptors for member pointers@endlink.
template <class _Ret, class _Tp>
class mem_fun_t : public unary_function<_Tp*,_Ret> {
public:
  explicit mem_fun_t(_Ret (_Tp::*__pf)()) : _M_f(__pf) {}
  _Ret operator()(_Tp* __p) const { return (__p->*_M_f)(); }
private:
  _Ret (_Tp::*_M_f)();
};

/// One of the @link s20_3_8_memadaptors adaptors for member pointers@endlink.
template <class _Ret, class _Tp>
class const_mem_fun_t : public unary_function<const _Tp*,_Ret> {
public:
  explicit const_mem_fun_t(_Ret (_Tp::*__pf)() const) : _M_f(__pf) {}
  _Ret operator()(const _Tp* __p) const { return (__p->*_M_f)(); }
private:
  _Ret (_Tp::*_M_f)() const;
};

/// One of the @link s20_3_8_memadaptors adaptors for member pointers@endlink.
template <class _Ret, class _Tp>
class mem_fun_ref_t : public unary_function<_Tp,_Ret> {
public:
  explicit mem_fun_ref_t(_Ret (_Tp::*__pf)()) : _M_f(__pf) {}
  _Ret operator()(_Tp& __r) const { return (__r.*_M_f)(); }
private:
  _Ret (_Tp::*_M_f)();
};

/// One of the @link s20_3_8_memadaptors adaptors for member pointers@endlink.
template <class _Ret, class _Tp>
class const_mem_fun_ref_t : public unary_function<_Tp,_Ret> {
public:
  explicit const_mem_fun_ref_t(_Ret (_Tp::*__pf)() const) : _M_f(__pf) {}
  _Ret operator()(const _Tp& __r) const { return (__r.*_M_f)(); }
private:
  _Ret (_Tp::*_M_f)() const;
};

/// One of the @link s20_3_8_memadaptors adaptors for member pointers@endlink.
template <class _Ret, class _Tp, class _Arg>
class mem_fun1_t : public binary_function<_Tp*,_Arg,_Ret> {
public:
  explicit mem_fun1_t(_Ret (_Tp::*__pf)(_Arg)) : _M_f(__pf) {}
  _Ret operator()(_Tp* __p, _Arg __x) const { return (__p->*_M_f)(__x); }
private:
  _Ret (_Tp::*_M_f)(_Arg);
};

/// One of the @link s20_3_8_memadaptors adaptors for member pointers@endlink.
template <class _Ret, class _Tp, class _Arg>
class const_mem_fun1_t : public binary_function<const _Tp*,_Arg,_Ret> {
public:
  explicit const_mem_fun1_t(_Ret (_Tp::*__pf)(_Arg) const) : _M_f(__pf) {}
  _Ret operator()(const _Tp* __p, _Arg __x) const
    { return (__p->*_M_f)(__x); }
private:
  _Ret (_Tp::*_M_f)(_Arg) const;
};

/// One of the @link s20_3_8_memadaptors adaptors for member pointers@endlink.
template <class _Ret, class _Tp, class _Arg>
class mem_fun1_ref_t : public binary_function<_Tp,_Arg,_Ret> {
public:
  explicit mem_fun1_ref_t(_Ret (_Tp::*__pf)(_Arg)) : _M_f(__pf) {}
  _Ret operator()(_Tp& __r, _Arg __x) const { return (__r.*_M_f)(__x); }
private:
  _Ret (_Tp::*_M_f)(_Arg);
};

/// One of the @link s20_3_8_memadaptors adaptors for member pointers@endlink.
template <class _Ret, class _Tp, class _Arg>
class const_mem_fun1_ref_t : public binary_function<_Tp,_Arg,_Ret> {
public:
  explicit const_mem_fun1_ref_t(_Ret (_Tp::*__pf)(_Arg) const) : _M_f(__pf) {}
  _Ret operator()(const _Tp& __r, _Arg __x) const { return (__r.*_M_f)(__x); }
private:
  _Ret (_Tp::*_M_f)(_Arg) const;
};

/// One of the @link s20_3_8_memadaptors adaptors for member pointers@endlink.
template <class _Tp>
class mem_fun_t<void, _Tp> : public unary_function<_Tp*,void> {
public:
  explicit mem_fun_t(void (_Tp::*__pf)()) : _M_f(__pf) {}
  void operator()(_Tp* __p) const { (__p->*_M_f)(); }
private:
  void (_Tp::*_M_f)();
};

/// One of the @link s20_3_8_memadaptors adaptors for member pointers@endlink.
template <class _Tp>
class const_mem_fun_t<void, _Tp> : public unary_function<const _Tp*,void> {
public:
  explicit const_mem_fun_t(void (_Tp::*__pf)() const) : _M_f(__pf) {}
  void operator()(const _Tp* __p) const { (__p->*_M_f)(); }
private:
  void (_Tp::*_M_f)() const;
};

/// One of the @link s20_3_8_memadaptors adaptors for member pointers@endlink.
template <class _Tp>
class mem_fun_ref_t<void, _Tp> : public unary_function<_Tp,void> {
public:
  explicit mem_fun_ref_t(void (_Tp::*__pf)()) : _M_f(__pf) {}
  void operator()(_Tp& __r) const { (__r.*_M_f)(); }
private:
  void (_Tp::*_M_f)();
};

/// One of the @link s20_3_8_memadaptors adaptors for member pointers@endlink.
template <class _Tp>
class const_mem_fun_ref_t<void, _Tp> : public unary_function<_Tp,void> {
public:
  explicit const_mem_fun_ref_t(void (_Tp::*__pf)() const) : _M_f(__pf) {}
  void operator()(const _Tp& __r) const { (__r.*_M_f)(); }
private:
  void (_Tp::*_M_f)() const;
};

/// One of the @link s20_3_8_memadaptors adaptors for member pointers@endlink.
template <class _Tp, class _Arg>
class mem_fun1_t<void, _Tp, _Arg> : public binary_function<_Tp*,_Arg,void> {
public:
  explicit mem_fun1_t(void (_Tp::*__pf)(_Arg)) : _M_f(__pf) {}
  void operator()(_Tp* __p, _Arg __x) const { (__p->*_M_f)(__x); }
private:
  void (_Tp::*_M_f)(_Arg);
};

/// One of the @link s20_3_8_memadaptors adaptors for member pointers@endlink.
template <class _Tp, class _Arg>
class const_mem_fun1_t<void, _Tp, _Arg> 
  : public binary_function<const _Tp*,_Arg,void> {
public:
  explicit const_mem_fun1_t(void (_Tp::*__pf)(_Arg) const) : _M_f(__pf) {}
  void operator()(const _Tp* __p, _Arg __x) const { (__p->*_M_f)(__x); }
private:
  void (_Tp::*_M_f)(_Arg) const;
};

/// One of the @link s20_3_8_memadaptors adaptors for member pointers@endlink.
template <class _Tp, class _Arg>
class mem_fun1_ref_t<void, _Tp, _Arg>
  : public binary_function<_Tp,_Arg,void> {
public:
  explicit mem_fun1_ref_t(void (_Tp::*__pf)(_Arg)) : _M_f(__pf) {}
  void operator()(_Tp& __r, _Arg __x) const { (__r.*_M_f)(__x); }
private:
  void (_Tp::*_M_f)(_Arg);
};

/// One of the @link s20_3_8_memadaptors adaptors for member pointers@endlink.
template <class _Tp, class _Arg>
class const_mem_fun1_ref_t<void, _Tp, _Arg>
  : public binary_function<_Tp,_Arg,void> {
public:
  explicit const_mem_fun1_ref_t(void (_Tp::*__pf)(_Arg) const) : _M_f(__pf) {}
  void operator()(const _Tp& __r, _Arg __x) const { (__r.*_M_f)(__x); }
private:
  void (_Tp::*_M_f)(_Arg) const;
};


// Mem_fun adaptor helper functions.  There are only two:
// mem_fun and mem_fun_ref.

template <class _Ret, class _Tp>
inline mem_fun_t<_Ret,_Tp> mem_fun(_Ret (_Tp::*__f)())
  { return mem_fun_t<_Ret,_Tp>(__f); }

template <class _Ret, class _Tp>
inline const_mem_fun_t<_Ret,_Tp> mem_fun(_Ret (_Tp::*__f)() const)
  { return const_mem_fun_t<_Ret,_Tp>(__f); }

template <class _Ret, class _Tp>
inline mem_fun_ref_t<_Ret,_Tp> mem_fun_ref(_Ret (_Tp::*__f)()) 
  { return mem_fun_ref_t<_Ret,_Tp>(__f); }

template <class _Ret, class _Tp>
inline const_mem_fun_ref_t<_Ret,_Tp> mem_fun_ref(_Ret (_Tp::*__f)() const)
  { return const_mem_fun_ref_t<_Ret,_Tp>(__f); }

template <class _Ret, class _Tp, class _Arg>
inline mem_fun1_t<_Ret,_Tp,_Arg> mem_fun(_Ret (_Tp::*__f)(_Arg))
  { return mem_fun1_t<_Ret,_Tp,_Arg>(__f); }

template <class _Ret, class _Tp, class _Arg>
inline const_mem_fun1_t<_Ret,_Tp,_Arg> mem_fun(_Ret (_Tp::*__f)(_Arg) const)
  { return const_mem_fun1_t<_Ret,_Tp,_Arg>(__f); }

template <class _Ret, class _Tp, class _Arg>
inline mem_fun1_ref_t<_Ret,_Tp,_Arg> mem_fun_ref(_Ret (_Tp::*__f)(_Arg))
  { return mem_fun1_ref_t<_Ret,_Tp,_Arg>(__f); }

template <class _Ret, class _Tp, class _Arg>
inline const_mem_fun1_ref_t<_Ret,_Tp,_Arg>
mem_fun_ref(_Ret (_Tp::*__f)(_Arg) const)
  { return const_mem_fun1_ref_t<_Ret,_Tp,_Arg>(__f); }

/** @}  */

} // namespace std

#endif /* __GLIBCPP_INTERNAL_FUNCTION_H */

// Local Variables:
// mode:C++
// End:
