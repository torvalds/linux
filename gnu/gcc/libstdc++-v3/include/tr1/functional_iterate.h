// TR1 functional -*- C++ -*-

// Copyright (C) 2005, 2006 Free Software Foundation, Inc.
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

/** @file tr1/functional_iterate.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

namespace std
{
_GLIBCXX_BEGIN_NAMESPACE(tr1)

template<typename _Res _GLIBCXX_COMMA _GLIBCXX_TEMPLATE_PARAMS>
  struct _Weak_result_type_impl<_Res(_GLIBCXX_TEMPLATE_ARGS)>
  {
    typedef _Res result_type;
  };

template<typename _Res _GLIBCXX_COMMA _GLIBCXX_TEMPLATE_PARAMS>
  struct _Weak_result_type_impl<_Res (&)(_GLIBCXX_TEMPLATE_ARGS)>
  {
    typedef _Res result_type;
  };

template<typename _Res _GLIBCXX_COMMA _GLIBCXX_TEMPLATE_PARAMS>
  struct _Weak_result_type_impl<_Res (*)(_GLIBCXX_TEMPLATE_ARGS)>
  {
    typedef _Res result_type;
  };

#if _GLIBCXX_NUM_ARGS > 0
template<typename _Res, typename _Class _GLIBCXX_COMMA_SHIFTED
         _GLIBCXX_TEMPLATE_PARAMS_SHIFTED>
  struct _Weak_result_type_impl<
           _Res (_Class::*)(_GLIBCXX_TEMPLATE_ARGS_SHIFTED)>
  {
    typedef _Res result_type;
  };

template<typename _Res, typename _Class _GLIBCXX_COMMA_SHIFTED
         _GLIBCXX_TEMPLATE_PARAMS_SHIFTED>
  struct _Weak_result_type_impl<
           _Res (_Class::*)(_GLIBCXX_TEMPLATE_ARGS_SHIFTED) const>
  {
    typedef _Res result_type;
  };

template<typename _Res, typename _Class _GLIBCXX_COMMA_SHIFTED
         _GLIBCXX_TEMPLATE_PARAMS_SHIFTED>
  struct _Weak_result_type_impl<
           _Res (_Class::*)(_GLIBCXX_TEMPLATE_ARGS_SHIFTED) volatile>
  {
    typedef _Res result_type;
  };

template<typename _Res, typename _Class _GLIBCXX_COMMA_SHIFTED
         _GLIBCXX_TEMPLATE_PARAMS_SHIFTED>
  struct _Weak_result_type_impl<
           _Res (_Class::*)(_GLIBCXX_TEMPLATE_ARGS_SHIFTED) const volatile>
  {
    typedef _Res result_type;
  };
#endif

template<typename _Functor _GLIBCXX_COMMA _GLIBCXX_TEMPLATE_PARAMS>
  class result_of<_Functor(_GLIBCXX_TEMPLATE_ARGS)>
    : public _Result_of_impl<
               _Has_result_type<_Weak_result_type<_Functor> >::value,
             _Functor(_GLIBCXX_TEMPLATE_ARGS)>
  { };

template<typename _Functor _GLIBCXX_COMMA _GLIBCXX_TEMPLATE_PARAMS>
  struct _Result_of_impl<true, _Functor(_GLIBCXX_TEMPLATE_ARGS)>
  {
    typedef typename _Weak_result_type<_Functor>::result_type type;
  };

template<typename _Functor _GLIBCXX_COMMA _GLIBCXX_TEMPLATE_PARAMS>
  struct _Result_of_impl<false, _Functor(_GLIBCXX_TEMPLATE_ARGS)>
  {
#if _GLIBCXX_NUM_ARGS > 0
    typedef typename _Functor
              ::template result<_Functor(_GLIBCXX_TEMPLATE_ARGS)>::type type;
#else
    typedef void type;
#endif
  };

/**
 * @if maint
 * Invoke a function object, which may be either a member pointer or a
 * function object. The first parameter will tell which.
 * @endif
 */
template<typename _Functor _GLIBCXX_COMMA _GLIBCXX_TEMPLATE_PARAMS>
  inline
  typename __gnu_cxx::__enable_if<(!is_member_pointer<_Functor>::value
			&& !is_function<_Functor>::value
              && !is_function<typename remove_pointer<_Functor>::type>::value),
           typename result_of<_Functor(_GLIBCXX_TEMPLATE_ARGS)>::type>::__type
  __invoke(_Functor& __f _GLIBCXX_COMMA _GLIBCXX_REF_PARAMS)
  {
    return __f(_GLIBCXX_ARGS);
  }

#if _GLIBCXX_NUM_ARGS > 0
template<typename _Functor _GLIBCXX_COMMA _GLIBCXX_TEMPLATE_PARAMS>
  inline
  typename __gnu_cxx::__enable_if<(is_member_pointer<_Functor>::value
			&& !is_function<_Functor>::value
              && !is_function<typename remove_pointer<_Functor>::type>::value),
             typename result_of<_Functor(_GLIBCXX_TEMPLATE_ARGS)>::type
           >::__type
  __invoke(_Functor& __f _GLIBCXX_COMMA _GLIBCXX_REF_PARAMS)
  {
    return mem_fn(__f)(_GLIBCXX_ARGS);
  }
#endif

// To pick up function references (that will become function pointers)
template<typename _Functor _GLIBCXX_COMMA _GLIBCXX_TEMPLATE_PARAMS>
  inline
  typename __gnu_cxx::__enable_if<(is_pointer<_Functor>::value
	&& is_function<typename remove_pointer<_Functor>::type>::value),
             typename result_of<_Functor(_GLIBCXX_TEMPLATE_ARGS)>::type
           >::__type
  __invoke(_Functor __f _GLIBCXX_COMMA _GLIBCXX_REF_PARAMS)
  {
    return __f(_GLIBCXX_ARGS);
  }

/**
 * @if maint
 * Implementation of reference_wrapper::operator()
 * @endif
*/
#if _GLIBCXX_NUM_ARGS > 0
template<typename _Tp>
template<_GLIBCXX_TEMPLATE_PARAMS>
  typename result_of<
   typename reference_wrapper<_Tp>::_M_func_type(_GLIBCXX_TEMPLATE_ARGS)>::type
  reference_wrapper<_Tp>::operator()(_GLIBCXX_REF_PARAMS) const
  {
    return __invoke(get(), _GLIBCXX_ARGS);
  }
#endif

#if _GLIBCXX_NUM_ARGS > 0
template<typename _Res, typename _Class _GLIBCXX_COMMA_SHIFTED
         _GLIBCXX_TEMPLATE_PARAMS_SHIFTED>
  class _Mem_fn<_Res (_Class::*)(_GLIBCXX_TEMPLATE_ARGS_SHIFTED)>
#if _GLIBCXX_NUM_ARGS == 1
  : public unary_function<_Class*, _Res>
#elif _GLIBCXX_NUM_ARGS == 2
    : public binary_function<_Class*, _T1, _Res>
#endif
  {
    typedef _Res (_Class::*_Functor)(_GLIBCXX_TEMPLATE_ARGS_SHIFTED);

    template<typename _Tp>
      _Res
      _M_call(_Tp& __object, const volatile _Class * _GLIBCXX_COMMA_SHIFTED
              _GLIBCXX_PARAMS_SHIFTED) const
      { return (__object.*__pmf)(_GLIBCXX_ARGS_SHIFTED); }

    template<typename _Tp>
      _Res
      _M_call(_Tp& __ptr, const volatile void * _GLIBCXX_COMMA_SHIFTED
              _GLIBCXX_PARAMS_SHIFTED) const
      {  return ((*__ptr).*__pmf)(_GLIBCXX_ARGS_SHIFTED); }

  public:
    typedef _Res result_type;

    explicit _Mem_fn(_Functor __pf) : __pmf(__pf) { }

    // Handle objects
    _Res
    operator()(_Class& __object _GLIBCXX_COMMA_SHIFTED
               _GLIBCXX_PARAMS_SHIFTED) const
    { return (__object.*__pmf)(_GLIBCXX_ARGS_SHIFTED); }

    // Handle pointers
    _Res
    operator()(_Class* __object _GLIBCXX_COMMA_SHIFTED
               _GLIBCXX_PARAMS_SHIFTED) const
    { return (__object->*__pmf)(_GLIBCXX_ARGS_SHIFTED); }

    // Handle smart pointers, references and pointers to derived
    template<typename _Tp>
      _Res
      operator()(_Tp& __object _GLIBCXX_COMMA_SHIFTED
                 _GLIBCXX_PARAMS_SHIFTED) const
      {
        return _M_call(__object, &__object _GLIBCXX_COMMA_SHIFTED
                       _GLIBCXX_ARGS_SHIFTED);
      }

  private:
    _Functor __pmf;
  };

template<typename _Res, typename _Class _GLIBCXX_COMMA_SHIFTED
         _GLIBCXX_TEMPLATE_PARAMS_SHIFTED>
  class _Mem_fn<_Res (_Class::*)(_GLIBCXX_TEMPLATE_ARGS_SHIFTED) const>
#if _GLIBCXX_NUM_ARGS == 1
  : public unary_function<const _Class*, _Res>
#elif _GLIBCXX_NUM_ARGS == 2
    : public binary_function<const _Class*, _T1, _Res>
#endif
  {
    typedef _Res (_Class::*_Functor)(_GLIBCXX_TEMPLATE_ARGS_SHIFTED) const;

     template<typename _Tp>
      _Res
      _M_call(_Tp& __object, const volatile _Class * _GLIBCXX_COMMA_SHIFTED
              _GLIBCXX_PARAMS_SHIFTED) const
      { return (__object.*__pmf)(_GLIBCXX_ARGS_SHIFTED); }

    template<typename _Tp>
      _Res
      _M_call(_Tp& __ptr, const volatile void * _GLIBCXX_COMMA_SHIFTED
              _GLIBCXX_PARAMS_SHIFTED) const
      {  return ((*__ptr).*__pmf)(_GLIBCXX_ARGS_SHIFTED); }

  public:
    typedef _Res result_type;

    explicit _Mem_fn(_Functor __pf) : __pmf(__pf) { }

    // Handle objects
    _Res
    operator()(const _Class& __object _GLIBCXX_COMMA_SHIFTED
               _GLIBCXX_PARAMS_SHIFTED) const
    { return (__object.*__pmf)(_GLIBCXX_ARGS_SHIFTED); }

    // Handle pointers
    _Res
    operator()(const _Class* __object _GLIBCXX_COMMA_SHIFTED
               _GLIBCXX_PARAMS_SHIFTED) const
    { return (__object->*__pmf)(_GLIBCXX_ARGS_SHIFTED); }

    // Handle smart pointers, references and pointers to derived
    template<typename _Tp>
      _Res
      operator()(_Tp& __object _GLIBCXX_COMMA_SHIFTED
                 _GLIBCXX_PARAMS_SHIFTED) const
      {
        return _M_call(__object, &__object _GLIBCXX_COMMA_SHIFTED
                       _GLIBCXX_ARGS_SHIFTED);
      }

  private:
    _Functor __pmf;
  };

template<typename _Res, typename _Class _GLIBCXX_COMMA_SHIFTED
         _GLIBCXX_TEMPLATE_PARAMS_SHIFTED>
  class _Mem_fn<_Res (_Class::*)(_GLIBCXX_TEMPLATE_ARGS_SHIFTED) volatile>
#if _GLIBCXX_NUM_ARGS == 1
  : public unary_function<volatile _Class*, _Res>
#elif _GLIBCXX_NUM_ARGS == 2
    : public binary_function<volatile _Class*, _T1, _Res>
#endif
  {
    typedef _Res (_Class::*_Functor)(_GLIBCXX_TEMPLATE_ARGS_SHIFTED) volatile;

    template<typename _Tp>
      _Res
      _M_call(_Tp& __object, const volatile _Class * _GLIBCXX_COMMA_SHIFTED
              _GLIBCXX_PARAMS_SHIFTED) const
      { return (__object.*__pmf)(_GLIBCXX_ARGS_SHIFTED); }

    template<typename _Tp>
      _Res
      _M_call(_Tp& __ptr, const volatile void * _GLIBCXX_COMMA_SHIFTED
              _GLIBCXX_PARAMS_SHIFTED) const
      {  return ((*__ptr).*__pmf)(_GLIBCXX_ARGS_SHIFTED); }

  public:
    typedef _Res result_type;

    explicit _Mem_fn(_Functor __pf) : __pmf(__pf) { }

    // Handle objects
    _Res
    operator()(volatile _Class& __object _GLIBCXX_COMMA_SHIFTED
               _GLIBCXX_PARAMS_SHIFTED) const
    { return (__object.*__pmf)(_GLIBCXX_ARGS_SHIFTED); }

    // Handle pointers
    _Res
    operator()(volatile _Class* __object _GLIBCXX_COMMA_SHIFTED
               _GLIBCXX_PARAMS_SHIFTED) const
    { return (__object->*__pmf)(_GLIBCXX_ARGS_SHIFTED); }

    // Handle smart pointers, references and pointers to derived
    template<typename _Tp>
      _Res
      operator()(_Tp& __object _GLIBCXX_COMMA_SHIFTED
                 _GLIBCXX_PARAMS_SHIFTED) const
      {
        return _M_call(__object, &__object _GLIBCXX_COMMA_SHIFTED
                       _GLIBCXX_ARGS_SHIFTED);
      }
  private:
    _Functor __pmf;
  };

template<typename _Res, typename _Class _GLIBCXX_COMMA_SHIFTED
         _GLIBCXX_TEMPLATE_PARAMS_SHIFTED>
  class _Mem_fn<_Res(_Class::*)(_GLIBCXX_TEMPLATE_ARGS_SHIFTED) const volatile>
#if _GLIBCXX_NUM_ARGS == 1
  : public unary_function<const volatile _Class*, _Res>
#elif _GLIBCXX_NUM_ARGS == 2
    : public binary_function<const volatile _Class*, _T1, _Res>
#endif
  {
    typedef _Res (_Class::*_Functor)(_GLIBCXX_TEMPLATE_ARGS_SHIFTED)
              const volatile;

    template<typename _Tp>
      _Res
      _M_call(_Tp& __object, const volatile _Class * _GLIBCXX_COMMA_SHIFTED
              _GLIBCXX_PARAMS_SHIFTED) const
      { return (__object.*__pmf)(_GLIBCXX_ARGS_SHIFTED); }

    template<typename _Tp>
      _Res
      _M_call(_Tp& __ptr, const volatile void * _GLIBCXX_COMMA_SHIFTED
              _GLIBCXX_PARAMS_SHIFTED) const
      {  return ((*__ptr).*__pmf)(_GLIBCXX_ARGS_SHIFTED); }

  public:
    typedef _Res result_type;

    explicit _Mem_fn(_Functor __pf) : __pmf(__pf) { }

    // Handle objects
    _Res
    operator()(const volatile _Class& __object _GLIBCXX_COMMA_SHIFTED
               _GLIBCXX_PARAMS_SHIFTED) const
    { return (__object.*__pmf)(_GLIBCXX_ARGS_SHIFTED); }

    // Handle pointers
    _Res
    operator()(const volatile _Class* __object _GLIBCXX_COMMA_SHIFTED
               _GLIBCXX_PARAMS_SHIFTED) const
    { return (__object->*__pmf)(_GLIBCXX_ARGS_SHIFTED); }

    // Handle smart pointers, references and pointers to derived
    template<typename _Tp>
      _Res
      operator()(_Tp& __object _GLIBCXX_COMMA_SHIFTED
                 _GLIBCXX_PARAMS_SHIFTED) const
      {
        return _M_call(__object, &__object _GLIBCXX_COMMA_SHIFTED
                       _GLIBCXX_ARGS_SHIFTED);
      }

  private:
    _Functor __pmf;
  };
#endif

#if _GLIBCXX_NUM_ARGS > 0
namespace placeholders
{
namespace
{
   _Placeholder<_GLIBCXX_NUM_ARGS> _GLIBCXX_JOIN(_,_GLIBCXX_NUM_ARGS);
} // anonymous namespace
}
#endif

template<typename _Functor _GLIBCXX_COMMA _GLIBCXX_TEMPLATE_PARAMS>
class _Bind<_Functor(_GLIBCXX_TEMPLATE_ARGS)>
  : public _Weak_result_type<_Functor>
{
  typedef _Bind __self_type;

  _Functor _M_f;
  _GLIBCXX_BIND_MEMBERS

 public:
#if _GLIBCXX_NUM_ARGS == 0
  explicit
#endif
  _Bind(_Functor __f _GLIBCXX_COMMA _GLIBCXX_PARAMS)
    : _M_f(__f) _GLIBCXX_COMMA _GLIBCXX_BIND_MEMBERS_INIT { }

#define _GLIBCXX_BIND_REPEAT_HEADER <tr1/bind_iterate.h>
#include <tr1/bind_repeat.h>
#undef _GLIBCXX_BIND_REPEAT_HEADER
};

template<typename _Result, typename _Functor
         _GLIBCXX_COMMA _GLIBCXX_TEMPLATE_PARAMS>
class _Bind_result<_Result, _Functor(_GLIBCXX_TEMPLATE_ARGS)>
{
  _Functor _M_f;
  _GLIBCXX_BIND_MEMBERS

 public:
  typedef _Result result_type;

#if _GLIBCXX_NUM_ARGS == 0
  explicit
#endif
  _Bind_result(_Functor __f _GLIBCXX_COMMA _GLIBCXX_PARAMS)
    : _M_f(__f) _GLIBCXX_COMMA _GLIBCXX_BIND_MEMBERS_INIT { }

#define _GLIBCXX_BIND_REPEAT_HEADER <tr1/bind_iterate.h>
#define _GLIBCXX_BIND_HAS_RESULT_TYPE
#include <tr1/bind_repeat.h>
#undef _GLIBCXX_BIND_HAS_RESULT_TYPE
#undef _GLIBCXX_BIND_REPEAT_HEADER
};

// Handle arbitrary function objects
template<typename _Functor _GLIBCXX_COMMA _GLIBCXX_TEMPLATE_PARAMS>
inline
_Bind<typename _Maybe_wrap_member_pointer<_Functor>::type
        (_GLIBCXX_TEMPLATE_ARGS)>
bind(_Functor __f _GLIBCXX_COMMA _GLIBCXX_PARAMS)
{
  typedef _Maybe_wrap_member_pointer<_Functor> __maybe_type;
  typedef typename __maybe_type::type __functor_type;
  typedef _Bind<__functor_type(_GLIBCXX_TEMPLATE_ARGS)> __result_type;
  return __result_type(__maybe_type::__do_wrap(__f)
                       _GLIBCXX_COMMA _GLIBCXX_ARGS);
}

template<typename _Result, typename _Functor
         _GLIBCXX_COMMA _GLIBCXX_TEMPLATE_PARAMS>
inline
_Bind_result<_Result,
             typename _Maybe_wrap_member_pointer<_Functor>::type
               (_GLIBCXX_TEMPLATE_ARGS)>
bind(_Functor __f _GLIBCXX_COMMA _GLIBCXX_PARAMS)
{
  typedef _Maybe_wrap_member_pointer<_Functor> __maybe_type;
  typedef typename __maybe_type::type __functor_type;
  typedef _Bind_result<_Result, __functor_type(_GLIBCXX_TEMPLATE_ARGS)>
    __result_type;
  return __result_type(__maybe_type::__do_wrap(__f)
                       _GLIBCXX_COMMA _GLIBCXX_ARGS);
}

template<typename _Res, typename _Functor _GLIBCXX_COMMA
         _GLIBCXX_TEMPLATE_PARAMS>
class _Function_handler<_Res(_GLIBCXX_TEMPLATE_ARGS), _Functor>
  : public _Function_base::_Base_manager<_Functor>
{
  typedef _Function_base::_Base_manager<_Functor> _Base;

 public:
  static _Res
  _M_invoke(const _Any_data& __functor _GLIBCXX_COMMA _GLIBCXX_PARAMS)
  {
    return (*_Base::_M_get_pointer(__functor))(_GLIBCXX_ARGS);
  }
};

template<typename _Functor _GLIBCXX_COMMA _GLIBCXX_TEMPLATE_PARAMS>
class _Function_handler<void(_GLIBCXX_TEMPLATE_ARGS), _Functor>
  : public _Function_base::_Base_manager<_Functor>
{
  typedef _Function_base::_Base_manager<_Functor> _Base;

 public:
  static void
  _M_invoke(const _Any_data& __functor _GLIBCXX_COMMA _GLIBCXX_PARAMS)
  {
    (*_Base::_M_get_pointer(__functor))(_GLIBCXX_ARGS);
  }
};

template<typename _Res, typename _Functor _GLIBCXX_COMMA
         _GLIBCXX_TEMPLATE_PARAMS>
class _Function_handler<_Res(_GLIBCXX_TEMPLATE_ARGS),
                        reference_wrapper<_Functor> >
  : public _Function_base::_Ref_manager<_Functor>
{
  typedef _Function_base::_Ref_manager<_Functor> _Base;

 public:
  static _Res
  _M_invoke(const _Any_data& __functor _GLIBCXX_COMMA _GLIBCXX_PARAMS)
  {
    return __callable_functor(**_Base::_M_get_pointer(__functor))
             (_GLIBCXX_ARGS);
  }
};

template<typename _Functor _GLIBCXX_COMMA _GLIBCXX_TEMPLATE_PARAMS>
class _Function_handler<void(_GLIBCXX_TEMPLATE_ARGS),
                        reference_wrapper<_Functor> >
  : public _Function_base::_Ref_manager<_Functor>
{
  typedef _Function_base::_Ref_manager<_Functor> _Base;

 public:
  static void
  _M_invoke(const _Any_data& __functor _GLIBCXX_COMMA _GLIBCXX_PARAMS)
  {
    __callable_functor(**_Base::_M_get_pointer(__functor))(_GLIBCXX_ARGS);
  }
};

template<typename _Class, typename _Member, typename _Res
         _GLIBCXX_COMMA _GLIBCXX_TEMPLATE_PARAMS>
class _Function_handler<_Res(_GLIBCXX_TEMPLATE_ARGS), _Member _Class::*>
  : public _Function_handler<void(_GLIBCXX_TEMPLATE_ARGS), _Member _Class::*>
{
  typedef _Function_handler<void(_GLIBCXX_TEMPLATE_ARGS), _Member _Class::*>
    _Base;

 public:
  static _Res
  _M_invoke(const _Any_data& __functor _GLIBCXX_COMMA _GLIBCXX_PARAMS)
  {
    return std::tr1::mem_fn(_Base::_M_get_pointer(__functor)->__value)
             (_GLIBCXX_ARGS);
  }
};

template<typename _Class, typename _Member
         _GLIBCXX_COMMA _GLIBCXX_TEMPLATE_PARAMS>
class _Function_handler<void(_GLIBCXX_TEMPLATE_ARGS), _Member _Class::*>
  : public _Function_base::_Base_manager<
             _Simple_type_wrapper< _Member _Class::* > >
{
  typedef _Member _Class::* _Functor;
  typedef _Simple_type_wrapper< _Functor > _Wrapper;
  typedef _Function_base::_Base_manager<_Wrapper> _Base;

 public:
  static bool
  _M_manager(_Any_data& __dest, const _Any_data& __source,
             _Manager_operation __op)
  {
    switch (__op) {
    case __get_type_info:
      __dest._M_access<const type_info*>() = &typeid(_Functor);
      break;

    case __get_functor_ptr:
      __dest._M_access<_Functor*>() =
        &_Base::_M_get_pointer(__source)->__value;
      break;

    default:
      _Base::_M_manager(__dest, __source, __op);
    }
    return false;
  }

  static void
  _M_invoke(const _Any_data& __functor _GLIBCXX_COMMA _GLIBCXX_PARAMS)
  {
    std::tr1::mem_fn(_Base::_M_get_pointer(__functor)->__value)
      (_GLIBCXX_ARGS);
  }
};

template<typename _Res _GLIBCXX_COMMA _GLIBCXX_TEMPLATE_PARAMS>
class function<_Res(_GLIBCXX_TEMPLATE_ARGS)>
#if _GLIBCXX_NUM_ARGS == 1
  : public unary_function<_T1, _Res>, private _Function_base
#elif _GLIBCXX_NUM_ARGS == 2
  : public binary_function<_T1, _T2, _Res>, private _Function_base
#else
  : private _Function_base
#endif
{
  /**
   *  @if maint
   *  This class is used to implement the safe_bool idiom.
   *  @endif
   */
  struct _Hidden_type
  {
    _Hidden_type* _M_bool;
  };

  /**
   *  @if maint
   *  This typedef is used to implement the safe_bool idiom.
   *  @endif
   */
  typedef _Hidden_type* _Hidden_type::* _Safe_bool;

  typedef _Res _Signature_type(_GLIBCXX_TEMPLATE_ARGS);

  struct _Useless {};

 public:
  typedef _Res result_type;

  // [3.7.2.1] construct/copy/destroy

  /**
   *  @brief Default construct creates an empty function call wrapper.
   *  @post @c !(bool)*this
   */
  function() : _Function_base() { }

  /**
   *  @brief Default construct creates an empty function call wrapper.
   *  @post @c !(bool)*this
   */
  function(_M_clear_type*) : _Function_base() { }

  /**
   *  @brief %Function copy constructor.
   *  @param x A %function object with identical call signature.
   *  @pre @c (bool)*this == (bool)x
   *
   *  The newly-created %function contains a copy of the target of @a
   *  x (if it has one).
   */
  function(const function& __x);

  /**
   *  @brief Builds a %function that targets a copy of the incoming
   *  function object.
   *  @param f A %function object that is callable with parameters of
   *  type @c T1, @c T2, ..., @c TN and returns a value convertible
   *  to @c Res.
   *
   *  The newly-created %function object will target a copy of @a
   *  f. If @a f is @c reference_wrapper<F>, then this function
   *  object will contain a reference to the function object @c
   *  f.get(). If @a f is a NULL function pointer or NULL
   *  pointer-to-member, the newly-created object will be empty.
   *
   *  If @a f is a non-NULL function pointer or an object of type @c
   *  reference_wrapper<F>, this function will not throw.
   */
  template<typename _Functor>
    function(_Functor __f,
             typename __gnu_cxx::__enable_if<!is_integral<_Functor>::value, _Useless>::__type = _Useless());

  /**
   *  @brief %Function assignment operator.
   *  @param x A %function with identical call signature.
   *  @post @c (bool)*this == (bool)x
   *  @returns @c *this
   *
   *  The target of @a x is copied to @c *this. If @a x has no
   *  target, then @c *this will be empty.
   *
   *  If @a x targets a function pointer or a reference to a function
   *  object, then this operation will not throw an exception.
   */
  function& operator=(const function& __x)
    {
      function(__x).swap(*this);
      return *this;
    }

  /**
   *  @brief %Function assignment to zero.
   *  @post @c !(bool)*this
   *  @returns @c *this
   *
   *  The target of @a *this is deallocated, leaving it empty.
   */
  function& operator=(_M_clear_type*)
    {
      if (_M_manager) {
        _M_manager(_M_functor, _M_functor, __destroy_functor);
        _M_manager = 0;
        _M_invoker = 0;
      }
      return *this;
    }

  /**
   *  @brief %Function assignment to a new target.
   *  @param f A %function object that is callable with parameters of
   *  type @c T1, @c T2, ..., @c TN and returns a value convertible
   *  to @c Res.
   *  @return @c *this
   *
   *  This  %function object wrapper will target a copy of @a
   *  f. If @a f is @c reference_wrapper<F>, then this function
   *  object will contain a reference to the function object @c
   *  f.get(). If @a f is a NULL function pointer or NULL
   *  pointer-to-member, @c this object will be empty.
   *
   *  If @a f is a non-NULL function pointer or an object of type @c
   *  reference_wrapper<F>, this function will not throw.
   */
  template<typename _Functor>
    typename __gnu_cxx::__enable_if<!is_integral<_Functor>::value, function&>::__type
    operator=(_Functor __f)
    {
      function(__f).swap(*this);
      return *this;
    }

  // [3.7.2.2] function modifiers

  /**
   *  @brief Swap the targets of two %function objects.
   *  @param f A %function with identical call signature.
   *
   *  Swap the targets of @c this function object and @a f. This
   *  function will not throw an exception.
   */
  void swap(function& __x)
  {
    _Any_data __old_functor = _M_functor;
    _M_functor = __x._M_functor;
    __x._M_functor = __old_functor;
    _Manager_type __old_manager = _M_manager;
    _M_manager = __x._M_manager;
    __x._M_manager = __old_manager;
    _Invoker_type __old_invoker = _M_invoker;
    _M_invoker = __x._M_invoker;
    __x._M_invoker = __old_invoker;
  }

  // [3.7.2.3] function capacity

  /**
   *  @brief Determine if the %function wrapper has a target.
   *
   *  @return @c true when this %function object contains a target,
   *  or @c false when it is empty.
   *
   *  This function will not throw an exception.
   */
  operator _Safe_bool() const
    {
      if (_M_empty())
        {
          return 0;
        }
      else
        {
          return &_Hidden_type::_M_bool;
        }
    }

  // [3.7.2.4] function invocation

  /**
   *  @brief Invokes the function targeted by @c *this.
   *  @returns the result of the target.
   *  @throws bad_function_call when @c !(bool)*this
   *
   *  The function call operator invokes the target function object
   *  stored by @c this.
   */
  _Res operator()(_GLIBCXX_PARAMS) const;

  // [3.7.2.5] function target access
  /**
   *  @brief Determine the type of the target of this function object
   *  wrapper.
   *
   *  @returns the type identifier of the target function object, or
   *  @c typeid(void) if @c !(bool)*this.
   *
   *  This function will not throw an exception.
   */
  const type_info& target_type() const;

  /**
   *  @brief Access the stored target function object.
   *
   *  @return Returns a pointer to the stored target function object,
   *  if @c typeid(Functor).equals(target_type()); otherwise, a NULL
   *  pointer.
   *
   * This function will not throw an exception.
   */
  template<typename _Functor>       _Functor* target();

  /**
   *  @overload
   */
  template<typename _Functor> const _Functor* target() const;

 private:
  // [3.7.2.6] undefined operators
  template<typename _Function>
    void operator==(const function<_Function>&) const;
  template<typename _Function>
    void operator!=(const function<_Function>&) const;

  typedef _Res (*_Invoker_type)(const _Any_data& _GLIBCXX_COMMA
                                _GLIBCXX_PARAMS);
  _Invoker_type _M_invoker;
};

template<typename _Res _GLIBCXX_COMMA _GLIBCXX_TEMPLATE_PARAMS>
  function<_Res(_GLIBCXX_TEMPLATE_ARGS)>::function(const function& __x)
    : _Function_base()
  {
    if (__x) {
      _M_invoker = __x._M_invoker;
      _M_manager = __x._M_manager;
      __x._M_manager(_M_functor, __x._M_functor, __clone_functor);
    }
  }

template<typename _Res _GLIBCXX_COMMA _GLIBCXX_TEMPLATE_PARAMS>
template<typename _Functor>
  function<_Res(_GLIBCXX_TEMPLATE_ARGS)>
  ::function(_Functor __f,
        typename __gnu_cxx::__enable_if<!is_integral<_Functor>::value, _Useless>::__type)
    : _Function_base()
{
  typedef _Function_handler<_Signature_type, _Functor> _My_handler;
  if (_My_handler::_M_not_empty_function(__f)) {
    _M_invoker = &_My_handler::_M_invoke;
    _M_manager = &_My_handler::_M_manager;
    _My_handler::_M_init_functor(_M_functor, __f);
  }
}

template<typename _Res _GLIBCXX_COMMA _GLIBCXX_TEMPLATE_PARAMS>
  _Res
  function<_Res(_GLIBCXX_TEMPLATE_ARGS)>::operator()(_GLIBCXX_PARAMS) const
  {
    if (_M_empty())
      {
#if __EXCEPTIONS
        throw bad_function_call();
#else
        std::abort();
#endif
      }
    return _M_invoker(_M_functor _GLIBCXX_COMMA _GLIBCXX_ARGS);
  }

template<typename _Res _GLIBCXX_COMMA _GLIBCXX_TEMPLATE_PARAMS>
  const type_info&
  function<_Res(_GLIBCXX_TEMPLATE_ARGS)>::target_type() const
  {
    if (_M_manager)
      {
        _Any_data __typeinfo_result;
        _M_manager(__typeinfo_result, _M_functor, __get_type_info);
        return *__typeinfo_result._M_access<const type_info*>();
      }
    else
      {
        return typeid(void);
      }
  }

template<typename _Res _GLIBCXX_COMMA _GLIBCXX_TEMPLATE_PARAMS>
template<typename _Functor>
  _Functor*
  function<_Res(_GLIBCXX_TEMPLATE_ARGS)>::target()
  {
    if (typeid(_Functor) == target_type() && _M_manager)
      {
        _Any_data __ptr;
        if (_M_manager(__ptr, _M_functor, __get_functor_ptr)
            && !is_const<_Functor>::value)
          return 0;
        else
          return __ptr._M_access<_Functor*>();
      }
    else
      {
        return 0;
      }
  }

template<typename _Res _GLIBCXX_COMMA _GLIBCXX_TEMPLATE_PARAMS>
template<typename _Functor>
  const _Functor*
  function<_Res(_GLIBCXX_TEMPLATE_ARGS)>::target() const
  {
    if (typeid(_Functor) == target_type() && _M_manager)
      {
        _Any_data __ptr;
        _M_manager(__ptr, _M_functor, __get_functor_ptr);
        return __ptr._M_access<const _Functor*>();
      }
    else
      {
        return 0;
      }
  }

_GLIBCXX_END_NAMESPACE
}
