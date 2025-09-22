// -*- C++ -*-

// Copyright (C) 2004, 2005 Free Software Foundation, Inc.
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

// (C) Copyright Jeremy Siek 2000. Permission to copy, use, modify,
// sell and distribute this software is granted provided this
// copyright notice appears in all copies. This software is provided
// "as is" without express or implied warranty, and with no claim as
// to its suitability for any purpose.
//

/** @file boost_concept_check.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

// GCC Note:  based on version 1.12.0 of the Boost library.

#ifndef _BOOST_CONCEPT_CHECK_H
#define _BOOST_CONCEPT_CHECK_H 1

#pragma GCC system_header

#include <cstddef>                // for ptrdiff_t, used next
#include <bits/stl_iterator_base_types.h>    // for traits and tags
#include <utility>                           // for pair<>

_GLIBCXX_BEGIN_NAMESPACE(__gnu_cxx)

#define _IsUnused __attribute__ ((__unused__))

// When the C-C code is in use, we would like this function to do as little
// as possible at runtime, use as few resources as possible, and hopefully
// be elided out of existence... hmmm.
template <class _Concept>
inline void __function_requires()
{
  void (_Concept::*__x)() _IsUnused = &_Concept::__constraints;
}

// No definition: if this is referenced, there's a problem with
// the instantiating type not being one of the required integer types.
// Unfortunately, this results in a link-time error, not a compile-time error.
void __error_type_must_be_an_integer_type();
void __error_type_must_be_an_unsigned_integer_type();
void __error_type_must_be_a_signed_integer_type();

// ??? Should the "concept_checking*" structs begin with more than _ ?
#define _GLIBCXX_CLASS_REQUIRES(_type_var, _ns, _concept) \
  typedef void (_ns::_concept <_type_var>::* _func##_type_var##_concept)(); \
  template <_func##_type_var##_concept _Tp1> \
  struct _concept_checking##_type_var##_concept { }; \
  typedef _concept_checking##_type_var##_concept< \
    &_ns::_concept <_type_var>::__constraints> \
    _concept_checking_typedef##_type_var##_concept

#define _GLIBCXX_CLASS_REQUIRES2(_type_var1, _type_var2, _ns, _concept) \
  typedef void (_ns::_concept <_type_var1,_type_var2>::* _func##_type_var1##_type_var2##_concept)(); \
  template <_func##_type_var1##_type_var2##_concept _Tp1> \
  struct _concept_checking##_type_var1##_type_var2##_concept { }; \
  typedef _concept_checking##_type_var1##_type_var2##_concept< \
    &_ns::_concept <_type_var1,_type_var2>::__constraints> \
    _concept_checking_typedef##_type_var1##_type_var2##_concept

#define _GLIBCXX_CLASS_REQUIRES3(_type_var1, _type_var2, _type_var3, _ns, _concept) \
  typedef void (_ns::_concept <_type_var1,_type_var2,_type_var3>::* _func##_type_var1##_type_var2##_type_var3##_concept)(); \
  template <_func##_type_var1##_type_var2##_type_var3##_concept _Tp1> \
  struct _concept_checking##_type_var1##_type_var2##_type_var3##_concept { }; \
  typedef _concept_checking##_type_var1##_type_var2##_type_var3##_concept< \
    &_ns::_concept <_type_var1,_type_var2,_type_var3>::__constraints>  \
  _concept_checking_typedef##_type_var1##_type_var2##_type_var3##_concept

#define _GLIBCXX_CLASS_REQUIRES4(_type_var1, _type_var2, _type_var3, _type_var4, _ns, _concept) \
  typedef void (_ns::_concept <_type_var1,_type_var2,_type_var3,_type_var4>::* _func##_type_var1##_type_var2##_type_var3##_type_var4##_concept)(); \
  template <_func##_type_var1##_type_var2##_type_var3##_type_var4##_concept _Tp1> \
  struct _concept_checking##_type_var1##_type_var2##_type_var3##_type_var4##_concept { }; \
  typedef _concept_checking##_type_var1##_type_var2##_type_var3##_type_var4##_concept< \
  &_ns::_concept <_type_var1,_type_var2,_type_var3,_type_var4>::__constraints> \
    _concept_checking_typedef##_type_var1##_type_var2##_type_var3##_type_var4##_concept


template <class _Tp1, class _Tp2>
struct _Aux_require_same { };

template <class _Tp>
struct _Aux_require_same<_Tp,_Tp> { typedef _Tp _Type; };

  template <class _Tp1, class _Tp2>
  struct _SameTypeConcept
  {
    void __constraints() {
      typedef typename _Aux_require_same<_Tp1, _Tp2>::_Type _Required;
    }
  };

  template <class _Tp>
  struct _IntegerConcept {
    void __constraints() {
      __error_type_must_be_an_integer_type();
    }
  };
  template <> struct _IntegerConcept<short> { void __constraints() {} };
  template <> struct _IntegerConcept<unsigned short> { void __constraints(){} };
  template <> struct _IntegerConcept<int> { void __constraints() {} };
  template <> struct _IntegerConcept<unsigned int> { void __constraints() {} };
  template <> struct _IntegerConcept<long> { void __constraints() {} };
  template <> struct _IntegerConcept<unsigned long> { void __constraints() {} };
  template <> struct _IntegerConcept<long long> { void __constraints() {} };
  template <> struct _IntegerConcept<unsigned long long>
                                                { void __constraints() {} };

  template <class _Tp>
  struct _SignedIntegerConcept {
    void __constraints() {
      __error_type_must_be_a_signed_integer_type();
    }
  };
  template <> struct _SignedIntegerConcept<short> { void __constraints() {} };
  template <> struct _SignedIntegerConcept<int> { void __constraints() {} };
  template <> struct _SignedIntegerConcept<long> { void __constraints() {} };
  template <> struct _SignedIntegerConcept<long long> { void __constraints(){}};

  template <class _Tp>
  struct _UnsignedIntegerConcept {
    void __constraints() {
      __error_type_must_be_an_unsigned_integer_type();
    }
  };
  template <> struct _UnsignedIntegerConcept<unsigned short>
    { void __constraints() {} };
  template <> struct _UnsignedIntegerConcept<unsigned int>
    { void __constraints() {} };
  template <> struct _UnsignedIntegerConcept<unsigned long>
    { void __constraints() {} };
  template <> struct _UnsignedIntegerConcept<unsigned long long>
    { void __constraints() {} };

  //===========================================================================
  // Basic Concepts

  template <class _Tp>
  struct _DefaultConstructibleConcept
  {
    void __constraints() {
      _Tp __a _IsUnused;                // require default constructor
    }
  };

  template <class _Tp>
  struct _AssignableConcept
  {
    void __constraints() {
      __a = __a;                        // require assignment operator
      __const_constraints(__a);
    }
    void __const_constraints(const _Tp& __b) {
      __a = __b;                   // const required for argument to assignment
    }
    _Tp __a;
    // possibly should be "Tp* a;" and then dereference "a" in constraint
    // functions?  present way would require a default ctor, i think...
  };

  template <class _Tp>
  struct _CopyConstructibleConcept
  {
    void __constraints() {
      _Tp __a(__b);                     // require copy constructor
      _Tp* __ptr _IsUnused = &__a;      // require address of operator
      __const_constraints(__a);
    }
    void __const_constraints(const _Tp& __a) {
      _Tp __c _IsUnused(__a);           // require const copy constructor
      const _Tp* __ptr _IsUnused = &__a; // require const address of operator
    }
    _Tp __b;
  };

  // The SGI STL version of Assignable requires copy constructor and operator=
  template <class _Tp>
  struct _SGIAssignableConcept
  {
    void __constraints() {
      _Tp __b _IsUnused(__a);
      __a = __a;                        // require assignment operator
      __const_constraints(__a);
    }
    void __const_constraints(const _Tp& __b) {
      _Tp __c _IsUnused(__b);
      __a = __b;              // const required for argument to assignment
    }
    _Tp __a;
  };

  template <class _From, class _To>
  struct _ConvertibleConcept
  {
    void __constraints() {
      _To __y _IsUnused = __x;
    }
    _From __x;
  };

  // The C++ standard requirements for many concepts talk about return
  // types that must be "convertible to bool".  The problem with this
  // requirement is that it leaves the door open for evil proxies that
  // define things like operator|| with strange return types.  Two
  // possible solutions are:
  // 1) require the return type to be exactly bool
  // 2) stay with convertible to bool, and also
  //    specify stuff about all the logical operators.
  // For now we just test for convertible to bool.
  template <class _Tp>
  void __aux_require_boolean_expr(const _Tp& __t) {
    bool __x _IsUnused = __t;
  }

// FIXME
  template <class _Tp>
  struct _EqualityComparableConcept
  {
    void __constraints() {
      __aux_require_boolean_expr(__a == __b);
    }
    _Tp __a, __b;
  };

  template <class _Tp>
  struct _LessThanComparableConcept
  {
    void __constraints() {
      __aux_require_boolean_expr(__a < __b);
    }
    _Tp __a, __b;
  };

  // This is equivalent to SGI STL's LessThanComparable.
  template <class _Tp>
  struct _ComparableConcept
  {
    void __constraints() {
      __aux_require_boolean_expr(__a < __b);
      __aux_require_boolean_expr(__a > __b);
      __aux_require_boolean_expr(__a <= __b);
      __aux_require_boolean_expr(__a >= __b);
    }
    _Tp __a, __b;
  };

#define _GLIBCXX_DEFINE_BINARY_PREDICATE_OP_CONSTRAINT(_OP,_NAME) \
  template <class _First, class _Second> \
  struct _NAME { \
    void __constraints() { (void)__constraints_(); } \
    bool __constraints_() {  \
      return  __a _OP __b; \
    } \
    _First __a; \
    _Second __b; \
  }

#define _GLIBCXX_DEFINE_BINARY_OPERATOR_CONSTRAINT(_OP,_NAME) \
  template <class _Ret, class _First, class _Second> \
  struct _NAME { \
    void __constraints() { (void)__constraints_(); } \
    _Ret __constraints_() {  \
      return __a _OP __b; \
    } \
    _First __a; \
    _Second __b; \
  }

  _GLIBCXX_DEFINE_BINARY_PREDICATE_OP_CONSTRAINT(==, _EqualOpConcept);
  _GLIBCXX_DEFINE_BINARY_PREDICATE_OP_CONSTRAINT(!=, _NotEqualOpConcept);
  _GLIBCXX_DEFINE_BINARY_PREDICATE_OP_CONSTRAINT(<, _LessThanOpConcept);
  _GLIBCXX_DEFINE_BINARY_PREDICATE_OP_CONSTRAINT(<=, _LessEqualOpConcept);
  _GLIBCXX_DEFINE_BINARY_PREDICATE_OP_CONSTRAINT(>, _GreaterThanOpConcept);
  _GLIBCXX_DEFINE_BINARY_PREDICATE_OP_CONSTRAINT(>=, _GreaterEqualOpConcept);

  _GLIBCXX_DEFINE_BINARY_OPERATOR_CONSTRAINT(+, _PlusOpConcept);
  _GLIBCXX_DEFINE_BINARY_OPERATOR_CONSTRAINT(*, _TimesOpConcept);
  _GLIBCXX_DEFINE_BINARY_OPERATOR_CONSTRAINT(/, _DivideOpConcept);
  _GLIBCXX_DEFINE_BINARY_OPERATOR_CONSTRAINT(-, _SubtractOpConcept);
  _GLIBCXX_DEFINE_BINARY_OPERATOR_CONSTRAINT(%, _ModOpConcept);

#undef _GLIBCXX_DEFINE_BINARY_PREDICATE_OP_CONSTRAINT
#undef _GLIBCXX_DEFINE_BINARY_OPERATOR_CONSTRAINT

  //===========================================================================
  // Function Object Concepts

  template <class _Func, class _Return>
  struct _GeneratorConcept
  {
    void __constraints() {
      const _Return& __r _IsUnused = __f();// require operator() member function
    }
    _Func __f;
  };


  template <class _Func>
  struct _GeneratorConcept<_Func,void>
  {
    void __constraints() {
      __f();                            // require operator() member function
    }
    _Func __f;
  };

  template <class _Func, class _Return, class _Arg>
  struct _UnaryFunctionConcept
  {
    void __constraints() {
      __r = __f(__arg);                  // require operator()
    }
    _Func __f;
    _Arg __arg;
    _Return __r;
  };

  template <class _Func, class _Arg>
  struct _UnaryFunctionConcept<_Func, void, _Arg> {
    void __constraints() {
      __f(__arg);                       // require operator()
    }
    _Func __f;
    _Arg __arg;
  };

  template <class _Func, class _Return, class _First, class _Second>
  struct _BinaryFunctionConcept
  {
    void __constraints() {
      __r = __f(__first, __second);     // require operator()
    }
    _Func __f;
    _First __first;
    _Second __second;
    _Return __r;
  };

  template <class _Func, class _First, class _Second>
  struct _BinaryFunctionConcept<_Func, void, _First, _Second>
  {
    void __constraints() {
      __f(__first, __second);           // require operator()
    }
    _Func __f;
    _First __first;
    _Second __second;
  };

  template <class _Func, class _Arg>
  struct _UnaryPredicateConcept
  {
    void __constraints() {
      __aux_require_boolean_expr(__f(__arg)); // require op() returning bool
    }
    _Func __f;
    _Arg __arg;
  };

  template <class _Func, class _First, class _Second>
  struct _BinaryPredicateConcept
  {
    void __constraints() {
      __aux_require_boolean_expr(__f(__a, __b)); // require op() returning bool
    }
    _Func __f;
    _First __a;
    _Second __b;
  };

  // use this when functor is used inside a container class like std::set
  template <class _Func, class _First, class _Second>
  struct _Const_BinaryPredicateConcept {
    void __constraints() {
      __const_constraints(__f);
    }
    void __const_constraints(const _Func& __fun) {
      __function_requires<_BinaryPredicateConcept<_Func, _First, _Second> >();
      // operator() must be a const member function
      __aux_require_boolean_expr(__fun(__a, __b));
    }
    _Func __f;
    _First __a;
    _Second __b;
  };

  //===========================================================================
  // Iterator Concepts

  template <class _Tp>
  struct _TrivialIteratorConcept
  {
    void __constraints() {
//    __function_requires< _DefaultConstructibleConcept<_Tp> >();
      __function_requires< _AssignableConcept<_Tp> >();
      __function_requires< _EqualityComparableConcept<_Tp> >();
//      typedef typename std::iterator_traits<_Tp>::value_type _V;
      (void)*__i;                       // require dereference operator
    }
    _Tp __i;
  };

  template <class _Tp>
  struct _Mutable_TrivialIteratorConcept
  {
    void __constraints() {
      __function_requires< _TrivialIteratorConcept<_Tp> >();
      *__i = *__j;                      // require dereference and assignment
    }
    _Tp __i, __j;
  };

  template <class _Tp>
  struct _InputIteratorConcept
  {
    void __constraints() {
      __function_requires< _TrivialIteratorConcept<_Tp> >();
      // require iterator_traits typedef's
      typedef typename std::iterator_traits<_Tp>::difference_type _Diff;
//      __function_requires< _SignedIntegerConcept<_Diff> >();
      typedef typename std::iterator_traits<_Tp>::reference _Ref;
      typedef typename std::iterator_traits<_Tp>::pointer _Pt;
      typedef typename std::iterator_traits<_Tp>::iterator_category _Cat;
      __function_requires< _ConvertibleConcept<
        typename std::iterator_traits<_Tp>::iterator_category,
        std::input_iterator_tag> >();
      ++__i;                            // require preincrement operator
      __i++;                            // require postincrement operator
    }
    _Tp __i;
  };

  template <class _Tp, class _ValueT>
  struct _OutputIteratorConcept
  {
    void __constraints() {
      __function_requires< _AssignableConcept<_Tp> >();
      ++__i;                            // require preincrement operator
      __i++;                            // require postincrement operator
      *__i++ = __t;                     // require postincrement and assignment
    }
    _Tp __i;
    _ValueT __t;
  };

  template <class _Tp>
  struct _ForwardIteratorConcept
  {
    void __constraints() {
      __function_requires< _InputIteratorConcept<_Tp> >();
      __function_requires< _DefaultConstructibleConcept<_Tp> >();
      __function_requires< _ConvertibleConcept<
        typename std::iterator_traits<_Tp>::iterator_category,
        std::forward_iterator_tag> >();
      typedef typename std::iterator_traits<_Tp>::reference _Ref;
      _Ref __r _IsUnused = *__i;
    }
    _Tp __i;
  };

  template <class _Tp>
  struct _Mutable_ForwardIteratorConcept
  {
    void __constraints() {
      __function_requires< _ForwardIteratorConcept<_Tp> >();
      *__i++ = *__i;                    // require postincrement and assignment
    }
    _Tp __i;
  };

  template <class _Tp>
  struct _BidirectionalIteratorConcept
  {
    void __constraints() {
      __function_requires< _ForwardIteratorConcept<_Tp> >();
      __function_requires< _ConvertibleConcept<
        typename std::iterator_traits<_Tp>::iterator_category,
        std::bidirectional_iterator_tag> >();
      --__i;                            // require predecrement operator
      __i--;                            // require postdecrement operator
    }
    _Tp __i;
  };

  template <class _Tp>
  struct _Mutable_BidirectionalIteratorConcept
  {
    void __constraints() {
      __function_requires< _BidirectionalIteratorConcept<_Tp> >();
      __function_requires< _Mutable_ForwardIteratorConcept<_Tp> >();
      *__i-- = *__i;                    // require postdecrement and assignment
    }
    _Tp __i;
  };


  template <class _Tp>
  struct _RandomAccessIteratorConcept
  {
    void __constraints() {
      __function_requires< _BidirectionalIteratorConcept<_Tp> >();
      __function_requires< _ComparableConcept<_Tp> >();
      __function_requires< _ConvertibleConcept<
        typename std::iterator_traits<_Tp>::iterator_category,
        std::random_access_iterator_tag> >();
      // ??? We don't use _Ref, are we just checking for "referenceability"?
      typedef typename std::iterator_traits<_Tp>::reference _Ref;

      __i += __n;                       // require assignment addition operator
      __i = __i + __n; __i = __n + __i; // require addition with difference type
      __i -= __n;                       // require assignment subtraction op
      __i = __i - __n;                  // require subtraction with
                                        //            difference type
      __n = __i - __j;                  // require difference operator
      (void)__i[__n];                   // require element access operator
    }
    _Tp __a, __b;
    _Tp __i, __j;
    typename std::iterator_traits<_Tp>::difference_type __n;
  };

  template <class _Tp>
  struct _Mutable_RandomAccessIteratorConcept
  {
    void __constraints() {
      __function_requires< _RandomAccessIteratorConcept<_Tp> >();
      __function_requires< _Mutable_BidirectionalIteratorConcept<_Tp> >();
      __i[__n] = *__i;                  // require element access and assignment
    }
    _Tp __i;
    typename std::iterator_traits<_Tp>::difference_type __n;
  };

  //===========================================================================
  // Container Concepts

  template <class _Container>
  struct _ContainerConcept
  {
    typedef typename _Container::value_type _Value_type;
    typedef typename _Container::difference_type _Difference_type;
    typedef typename _Container::size_type _Size_type;
    typedef typename _Container::const_reference _Const_reference;
    typedef typename _Container::const_pointer _Const_pointer;
    typedef typename _Container::const_iterator _Const_iterator;

    void __constraints() {
      __function_requires< _InputIteratorConcept<_Const_iterator> >();
      __function_requires< _AssignableConcept<_Container> >();
      const _Container __c;
      __i = __c.begin();
      __i = __c.end();
      __n = __c.size();
      __n = __c.max_size();
      __b = __c.empty();
    }
    bool __b;
    _Const_iterator __i;
    _Size_type __n;
  };

  template <class _Container>
  struct _Mutable_ContainerConcept
  {
    typedef typename _Container::value_type _Value_type;
    typedef typename _Container::reference _Reference;
    typedef typename _Container::iterator _Iterator;
    typedef typename _Container::pointer _Pointer;

    void __constraints() {
      __function_requires< _ContainerConcept<_Container> >();
      __function_requires< _AssignableConcept<_Value_type> >();
      __function_requires< _InputIteratorConcept<_Iterator> >();

      __i = __c.begin();
      __i = __c.end();
      __c.swap(__c2);
    }
    _Iterator __i;
    _Container __c, __c2;
  };

  template <class _ForwardContainer>
  struct _ForwardContainerConcept
  {
    void __constraints() {
      __function_requires< _ContainerConcept<_ForwardContainer> >();
      typedef typename _ForwardContainer::const_iterator _Const_iterator;
      __function_requires< _ForwardIteratorConcept<_Const_iterator> >();
    }
  };

  template <class _ForwardContainer>
  struct _Mutable_ForwardContainerConcept
  {
    void __constraints() {
      __function_requires< _ForwardContainerConcept<_ForwardContainer> >();
      __function_requires< _Mutable_ContainerConcept<_ForwardContainer> >();
      typedef typename _ForwardContainer::iterator _Iterator;
      __function_requires< _Mutable_ForwardIteratorConcept<_Iterator> >();
    }
  };

  template <class _ReversibleContainer>
  struct _ReversibleContainerConcept
  {
    typedef typename _ReversibleContainer::const_iterator _Const_iterator;
    typedef typename _ReversibleContainer::const_reverse_iterator
      _Const_reverse_iterator;

    void __constraints() {
      __function_requires< _ForwardContainerConcept<_ReversibleContainer> >();
      __function_requires< _BidirectionalIteratorConcept<_Const_iterator> >();
      __function_requires<
        _BidirectionalIteratorConcept<_Const_reverse_iterator> >();

      const _ReversibleContainer __c;
      _Const_reverse_iterator __i = __c.rbegin();
      __i = __c.rend();
    }
  };

  template <class _ReversibleContainer>
  struct _Mutable_ReversibleContainerConcept
  {
    typedef typename _ReversibleContainer::iterator _Iterator;
    typedef typename _ReversibleContainer::reverse_iterator _Reverse_iterator;

    void __constraints() {
      __function_requires<_ReversibleContainerConcept<_ReversibleContainer> >();
      __function_requires<
        _Mutable_ForwardContainerConcept<_ReversibleContainer> >();
      __function_requires<_Mutable_BidirectionalIteratorConcept<_Iterator> >();
      __function_requires<
        _Mutable_BidirectionalIteratorConcept<_Reverse_iterator> >();

      _Reverse_iterator __i = __c.rbegin();
      __i = __c.rend();
    }
    _ReversibleContainer __c;
  };

  template <class _RandomAccessContainer>
  struct _RandomAccessContainerConcept
  {
    typedef typename _RandomAccessContainer::size_type _Size_type;
    typedef typename _RandomAccessContainer::const_reference _Const_reference;
    typedef typename _RandomAccessContainer::const_iterator _Const_iterator;
    typedef typename _RandomAccessContainer::const_reverse_iterator
      _Const_reverse_iterator;

    void __constraints() {
      __function_requires<
        _ReversibleContainerConcept<_RandomAccessContainer> >();
      __function_requires< _RandomAccessIteratorConcept<_Const_iterator> >();
      __function_requires<
        _RandomAccessIteratorConcept<_Const_reverse_iterator> >();

      const _RandomAccessContainer __c;
      _Const_reference __r _IsUnused = __c[__n];
    }
    _Size_type __n;
  };

  template <class _RandomAccessContainer>
  struct _Mutable_RandomAccessContainerConcept
  {
    typedef typename _RandomAccessContainer::size_type _Size_type;
    typedef typename _RandomAccessContainer::reference _Reference;
    typedef typename _RandomAccessContainer::iterator _Iterator;
    typedef typename _RandomAccessContainer::reverse_iterator _Reverse_iterator;

    void __constraints() {
      __function_requires<
        _RandomAccessContainerConcept<_RandomAccessContainer> >();
      __function_requires<
        _Mutable_ReversibleContainerConcept<_RandomAccessContainer> >();
      __function_requires< _Mutable_RandomAccessIteratorConcept<_Iterator> >();
      __function_requires<
        _Mutable_RandomAccessIteratorConcept<_Reverse_iterator> >();

      _Reference __r _IsUnused = __c[__i];
    }
    _Size_type __i;
    _RandomAccessContainer __c;
  };

  // A Sequence is inherently mutable
  template <class _Sequence>
  struct _SequenceConcept
  {
    typedef typename _Sequence::reference _Reference;
    typedef typename _Sequence::const_reference _Const_reference;

    void __constraints() {
      // Matt Austern's book puts DefaultConstructible here, the C++
      // standard places it in Container
      //    function_requires< DefaultConstructible<Sequence> >();
      __function_requires< _Mutable_ForwardContainerConcept<_Sequence> >();
      __function_requires< _DefaultConstructibleConcept<_Sequence> >();

      _Sequence
	__c _IsUnused(__n, __t),
        __c2 _IsUnused(__first, __last);

      __c.insert(__p, __t);
      __c.insert(__p, __n, __t);
      __c.insert(__p, __first, __last);

      __c.erase(__p);
      __c.erase(__p, __q);

      _Reference __r _IsUnused = __c.front();

      __const_constraints(__c);
    }
    void __const_constraints(const _Sequence& __c) {
      _Const_reference __r _IsUnused = __c.front();
    }
    typename _Sequence::value_type __t;
    typename _Sequence::size_type __n;
    typename _Sequence::value_type *__first, *__last;
    typename _Sequence::iterator __p, __q;
  };

  template <class _FrontInsertionSequence>
  struct _FrontInsertionSequenceConcept
  {
    void __constraints() {
      __function_requires< _SequenceConcept<_FrontInsertionSequence> >();

      __c.push_front(__t);
      __c.pop_front();
    }
    _FrontInsertionSequence __c;
    typename _FrontInsertionSequence::value_type __t;
  };

  template <class _BackInsertionSequence>
  struct _BackInsertionSequenceConcept
  {
    typedef typename _BackInsertionSequence::reference _Reference;
    typedef typename _BackInsertionSequence::const_reference _Const_reference;

    void __constraints() {
      __function_requires< _SequenceConcept<_BackInsertionSequence> >();

      __c.push_back(__t);
      __c.pop_back();
      _Reference __r _IsUnused = __c.back();
    }
    void __const_constraints(const _BackInsertionSequence& __c) {
      _Const_reference __r _IsUnused = __c.back();
    };
    _BackInsertionSequence __c;
    typename _BackInsertionSequence::value_type __t;
  };

  template <class _AssociativeContainer>
  struct _AssociativeContainerConcept
  {
    void __constraints() {
      __function_requires< _ForwardContainerConcept<_AssociativeContainer> >();
      __function_requires<
        _DefaultConstructibleConcept<_AssociativeContainer> >();

      __i = __c.find(__k);
      __r = __c.equal_range(__k);
      __c.erase(__k);
      __c.erase(__i);
      __c.erase(__r.first, __r.second);
      __const_constraints(__c);
    }
    void __const_constraints(const _AssociativeContainer& __c) {
      __ci = __c.find(__k);
      __n = __c.count(__k);
      __cr = __c.equal_range(__k);
    }
    typedef typename _AssociativeContainer::iterator _Iterator;
    typedef typename _AssociativeContainer::const_iterator _Const_iterator;

    _AssociativeContainer __c;
    _Iterator __i;
    std::pair<_Iterator,_Iterator> __r;
    _Const_iterator __ci;
    std::pair<_Const_iterator,_Const_iterator> __cr;
    typename _AssociativeContainer::key_type __k;
    typename _AssociativeContainer::size_type __n;
  };

  template <class _UniqueAssociativeContainer>
  struct _UniqueAssociativeContainerConcept
  {
    void __constraints() {
      __function_requires<
        _AssociativeContainerConcept<_UniqueAssociativeContainer> >();

      _UniqueAssociativeContainer __c(__first, __last);

      __pos_flag = __c.insert(__t);
      __c.insert(__first, __last);
    }
    std::pair<typename _UniqueAssociativeContainer::iterator, bool> __pos_flag;
    typename _UniqueAssociativeContainer::value_type __t;
    typename _UniqueAssociativeContainer::value_type *__first, *__last;
  };

  template <class _MultipleAssociativeContainer>
  struct _MultipleAssociativeContainerConcept
  {
    void __constraints() {
      __function_requires<
        _AssociativeContainerConcept<_MultipleAssociativeContainer> >();

      _MultipleAssociativeContainer __c(__first, __last);

      __pos = __c.insert(__t);
      __c.insert(__first, __last);

    }
    typename _MultipleAssociativeContainer::iterator __pos;
    typename _MultipleAssociativeContainer::value_type __t;
    typename _MultipleAssociativeContainer::value_type *__first, *__last;
  };

  template <class _SimpleAssociativeContainer>
  struct _SimpleAssociativeContainerConcept
  {
    void __constraints() {
      __function_requires<
        _AssociativeContainerConcept<_SimpleAssociativeContainer> >();
      typedef typename _SimpleAssociativeContainer::key_type _Key_type;
      typedef typename _SimpleAssociativeContainer::value_type _Value_type;
      typedef typename _Aux_require_same<_Key_type, _Value_type>::_Type
        _Required;
    }
  };

  template <class _SimpleAssociativeContainer>
  struct _PairAssociativeContainerConcept
  {
    void __constraints() {
      __function_requires<
        _AssociativeContainerConcept<_SimpleAssociativeContainer> >();
      typedef typename _SimpleAssociativeContainer::key_type _Key_type;
      typedef typename _SimpleAssociativeContainer::value_type _Value_type;
      typedef typename _SimpleAssociativeContainer::mapped_type _Mapped_type;
      typedef std::pair<const _Key_type, _Mapped_type> _Required_value_type;
      typedef typename _Aux_require_same<_Value_type,
        _Required_value_type>::_Type _Required;
    }
  };

  template <class _SortedAssociativeContainer>
  struct _SortedAssociativeContainerConcept
  {
    void __constraints() {
      __function_requires<
        _AssociativeContainerConcept<_SortedAssociativeContainer> >();
      __function_requires<
        _ReversibleContainerConcept<_SortedAssociativeContainer> >();

      _SortedAssociativeContainer
        __c _IsUnused(__kc),
        __c2 _IsUnused(__first, __last),
        __c3 _IsUnused(__first, __last, __kc);

      __p = __c.upper_bound(__k);
      __p = __c.lower_bound(__k);
      __r = __c.equal_range(__k);

      __c.insert(__p, __t);
    }
    void __const_constraints(const _SortedAssociativeContainer& __c) {
      __kc = __c.key_comp();
      __vc = __c.value_comp();

      __cp = __c.upper_bound(__k);
      __cp = __c.lower_bound(__k);
      __cr = __c.equal_range(__k);
    }
    typename _SortedAssociativeContainer::key_compare __kc;
    typename _SortedAssociativeContainer::value_compare __vc;
    typename _SortedAssociativeContainer::value_type __t;
    typename _SortedAssociativeContainer::key_type __k;
    typedef typename _SortedAssociativeContainer::iterator _Iterator;
    typedef typename _SortedAssociativeContainer::const_iterator
      _Const_iterator;

    _Iterator __p;
    _Const_iterator __cp;
    std::pair<_Iterator,_Iterator> __r;
    std::pair<_Const_iterator,_Const_iterator> __cr;
    typename _SortedAssociativeContainer::value_type *__first, *__last;
  };

  // HashedAssociativeContainer

_GLIBCXX_END_NAMESPACE

#undef _IsUnused

#endif // _GLIBCXX_BOOST_CONCEPT_CHECK


