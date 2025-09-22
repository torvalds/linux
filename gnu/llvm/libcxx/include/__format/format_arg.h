// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___FORMAT_FORMAT_ARG_H
#define _LIBCPP___FORMAT_FORMAT_ARG_H

#include <__assert>
#include <__concepts/arithmetic.h>
#include <__config>
#include <__format/concepts.h>
#include <__format/format_parse_context.h>
#include <__functional/invoke.h>
#include <__fwd/format.h>
#include <__memory/addressof.h>
#include <__type_traits/conditional.h>
#include <__type_traits/remove_const.h>
#include <__utility/forward.h>
#include <__utility/move.h>
#include <__utility/unreachable.h>
#include <__variant/monostate.h>
#include <cstdint>
#include <string_view>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

namespace __format {
/// The type stored in @ref basic_format_arg.
///
/// @note The 128-bit types are unconditionally in the list to avoid the values
/// of the enums to depend on the availability of 128-bit integers.
///
/// @note The value is stored as a 5-bit value in the __packed_arg_t_bits. This
/// limits the maximum number of elements to 32.
/// When modifying update the test
/// test/libcxx/utilities/format/format.arguments/format.arg/arg_t.compile.pass.cpp
/// It could be packed in 4-bits but that means a new type directly becomes an
/// ABI break. The packed type is 64-bit so this reduces the maximum number of
/// packed elements from 16 to 12.
///
/// @note Some members of this enum are an extension. These extensions need
/// special behaviour in visit_format_arg. There they need to be wrapped in a
/// handle to satisfy the user observable behaviour. The internal function
/// __visit_format_arg doesn't do this wrapping. So in the format functions
/// this function is used to avoid unneeded overhead.
enum class __arg_t : uint8_t {
  __none,
  __boolean,
  __char_type,
  __int,
  __long_long,
  __i128, // extension
  __unsigned,
  __unsigned_long_long,
  __u128, // extension
  __float,
  __double,
  __long_double,
  __const_char_type_ptr,
  __string_view,
  __ptr,
  __handle
};

inline constexpr unsigned __packed_arg_t_bits = 5;
inline constexpr uint8_t __packed_arg_t_mask  = 0x1f;

inline constexpr unsigned __packed_types_storage_bits = 64;
inline constexpr unsigned __packed_types_max          = __packed_types_storage_bits / __packed_arg_t_bits;

_LIBCPP_HIDE_FROM_ABI constexpr bool __use_packed_format_arg_store(size_t __size) {
  return __size <= __packed_types_max;
}

_LIBCPP_HIDE_FROM_ABI constexpr __arg_t __get_packed_type(uint64_t __types, size_t __id) {
  _LIBCPP_ASSERT_INTERNAL(__id <= __packed_types_max, "");

  if (__id > 0)
    __types >>= __id * __packed_arg_t_bits;

  return static_cast<__format::__arg_t>(__types & __packed_arg_t_mask);
}

} // namespace __format

// This function is not user observable, so it can directly use the non-standard
// types of the "variant". See __arg_t for more details.
template <class _Visitor, class _Context>
_LIBCPP_HIDE_FROM_ABI decltype(auto) __visit_format_arg(_Visitor&& __vis, basic_format_arg<_Context> __arg) {
  switch (__arg.__type_) {
  case __format::__arg_t::__none:
    return std::invoke(std::forward<_Visitor>(__vis), __arg.__value_.__monostate_);
  case __format::__arg_t::__boolean:
    return std::invoke(std::forward<_Visitor>(__vis), __arg.__value_.__boolean_);
  case __format::__arg_t::__char_type:
    return std::invoke(std::forward<_Visitor>(__vis), __arg.__value_.__char_type_);
  case __format::__arg_t::__int:
    return std::invoke(std::forward<_Visitor>(__vis), __arg.__value_.__int_);
  case __format::__arg_t::__long_long:
    return std::invoke(std::forward<_Visitor>(__vis), __arg.__value_.__long_long_);
  case __format::__arg_t::__i128:
#  ifndef _LIBCPP_HAS_NO_INT128
    return std::invoke(std::forward<_Visitor>(__vis), __arg.__value_.__i128_);
#  else
    __libcpp_unreachable();
#  endif
  case __format::__arg_t::__unsigned:
    return std::invoke(std::forward<_Visitor>(__vis), __arg.__value_.__unsigned_);
  case __format::__arg_t::__unsigned_long_long:
    return std::invoke(std::forward<_Visitor>(__vis), __arg.__value_.__unsigned_long_long_);
  case __format::__arg_t::__u128:
#  ifndef _LIBCPP_HAS_NO_INT128
    return std::invoke(std::forward<_Visitor>(__vis), __arg.__value_.__u128_);
#  else
    __libcpp_unreachable();
#  endif
  case __format::__arg_t::__float:
    return std::invoke(std::forward<_Visitor>(__vis), __arg.__value_.__float_);
  case __format::__arg_t::__double:
    return std::invoke(std::forward<_Visitor>(__vis), __arg.__value_.__double_);
  case __format::__arg_t::__long_double:
    return std::invoke(std::forward<_Visitor>(__vis), __arg.__value_.__long_double_);
  case __format::__arg_t::__const_char_type_ptr:
    return std::invoke(std::forward<_Visitor>(__vis), __arg.__value_.__const_char_type_ptr_);
  case __format::__arg_t::__string_view:
    return std::invoke(std::forward<_Visitor>(__vis), __arg.__value_.__string_view_);
  case __format::__arg_t::__ptr:
    return std::invoke(std::forward<_Visitor>(__vis), __arg.__value_.__ptr_);
  case __format::__arg_t::__handle:
    return std::invoke(
        std::forward<_Visitor>(__vis), typename basic_format_arg<_Context>::handle{__arg.__value_.__handle_});
  }

  __libcpp_unreachable();
}

#  if _LIBCPP_STD_VER >= 26 && defined(_LIBCPP_HAS_EXPLICIT_THIS_PARAMETER)

template <class _Rp, class _Visitor, class _Context>
_LIBCPP_HIDE_FROM_ABI _Rp __visit_format_arg(_Visitor&& __vis, basic_format_arg<_Context> __arg) {
  switch (__arg.__type_) {
  case __format::__arg_t::__none:
    return std::invoke_r<_Rp>(std::forward<_Visitor>(__vis), __arg.__value_.__monostate_);
  case __format::__arg_t::__boolean:
    return std::invoke_r<_Rp>(std::forward<_Visitor>(__vis), __arg.__value_.__boolean_);
  case __format::__arg_t::__char_type:
    return std::invoke_r<_Rp>(std::forward<_Visitor>(__vis), __arg.__value_.__char_type_);
  case __format::__arg_t::__int:
    return std::invoke_r<_Rp>(std::forward<_Visitor>(__vis), __arg.__value_.__int_);
  case __format::__arg_t::__long_long:
    return std::invoke_r<_Rp>(std::forward<_Visitor>(__vis), __arg.__value_.__long_long_);
  case __format::__arg_t::__i128:
#    ifndef _LIBCPP_HAS_NO_INT128
    return std::invoke_r<_Rp>(std::forward<_Visitor>(__vis), __arg.__value_.__i128_);
#    else
    __libcpp_unreachable();
#    endif
  case __format::__arg_t::__unsigned:
    return std::invoke_r<_Rp>(std::forward<_Visitor>(__vis), __arg.__value_.__unsigned_);
  case __format::__arg_t::__unsigned_long_long:
    return std::invoke_r<_Rp>(std::forward<_Visitor>(__vis), __arg.__value_.__unsigned_long_long_);
  case __format::__arg_t::__u128:
#    ifndef _LIBCPP_HAS_NO_INT128
    return std::invoke_r<_Rp>(std::forward<_Visitor>(__vis), __arg.__value_.__u128_);
#    else
    __libcpp_unreachable();
#    endif
  case __format::__arg_t::__float:
    return std::invoke_r<_Rp>(std::forward<_Visitor>(__vis), __arg.__value_.__float_);
  case __format::__arg_t::__double:
    return std::invoke_r<_Rp>(std::forward<_Visitor>(__vis), __arg.__value_.__double_);
  case __format::__arg_t::__long_double:
    return std::invoke_r<_Rp>(std::forward<_Visitor>(__vis), __arg.__value_.__long_double_);
  case __format::__arg_t::__const_char_type_ptr:
    return std::invoke_r<_Rp>(std::forward<_Visitor>(__vis), __arg.__value_.__const_char_type_ptr_);
  case __format::__arg_t::__string_view:
    return std::invoke_r<_Rp>(std::forward<_Visitor>(__vis), __arg.__value_.__string_view_);
  case __format::__arg_t::__ptr:
    return std::invoke_r<_Rp>(std::forward<_Visitor>(__vis), __arg.__value_.__ptr_);
  case __format::__arg_t::__handle:
    return std::invoke_r<_Rp>(
        std::forward<_Visitor>(__vis), typename basic_format_arg<_Context>::handle{__arg.__value_.__handle_});
  }

  __libcpp_unreachable();
}

#  endif // _LIBCPP_STD_VER >= 26 && defined(_LIBCPP_HAS_EXPLICIT_THIS_PARAMETER)

/// Contains the values used in basic_format_arg.
///
/// This is a separate type so it's possible to store the values and types in
/// separate arrays.
template <class _Context>
class __basic_format_arg_value {
  using _CharT = typename _Context::char_type;

public:
  /// Contains the implementation for basic_format_arg::handle.
  struct __handle {
    template <class _Tp>
    _LIBCPP_HIDE_FROM_ABI explicit __handle(_Tp& __v) noexcept
        : __ptr_(std::addressof(__v)),
          __format_([](basic_format_parse_context<_CharT>& __parse_ctx, _Context& __ctx, const void* __ptr) {
            using _Dp = remove_const_t<_Tp>;
            using _Qp = conditional_t<__formattable_with<const _Dp, _Context>, const _Dp, _Dp>;
            static_assert(__formattable_with<_Qp, _Context>, "Mandated by [format.arg]/10");

            typename _Context::template formatter_type<_Dp> __f;
            __parse_ctx.advance_to(__f.parse(__parse_ctx));
            __ctx.advance_to(__f.format(*const_cast<_Qp*>(static_cast<const _Dp*>(__ptr)), __ctx));
          }) {}

    const void* __ptr_;
    void (*__format_)(basic_format_parse_context<_CharT>&, _Context&, const void*);
  };

  union {
    monostate __monostate_;
    bool __boolean_;
    _CharT __char_type_;
    int __int_;
    unsigned __unsigned_;
    long long __long_long_;
    unsigned long long __unsigned_long_long_;
#  ifndef _LIBCPP_HAS_NO_INT128
    __int128_t __i128_;
    __uint128_t __u128_;
#  endif
    float __float_;
    double __double_;
    long double __long_double_;
    const _CharT* __const_char_type_ptr_;
    basic_string_view<_CharT> __string_view_;
    const void* __ptr_;
    __handle __handle_;
  };

  // These constructors contain the exact storage type used. If adjustments are
  // required, these will be done in __create_format_arg.

  _LIBCPP_HIDE_FROM_ABI __basic_format_arg_value() noexcept : __monostate_() {}
  _LIBCPP_HIDE_FROM_ABI __basic_format_arg_value(bool __value) noexcept : __boolean_(__value) {}
  _LIBCPP_HIDE_FROM_ABI __basic_format_arg_value(_CharT __value) noexcept : __char_type_(__value) {}
  _LIBCPP_HIDE_FROM_ABI __basic_format_arg_value(int __value) noexcept : __int_(__value) {}
  _LIBCPP_HIDE_FROM_ABI __basic_format_arg_value(unsigned __value) noexcept : __unsigned_(__value) {}
  _LIBCPP_HIDE_FROM_ABI __basic_format_arg_value(long long __value) noexcept : __long_long_(__value) {}
  _LIBCPP_HIDE_FROM_ABI __basic_format_arg_value(unsigned long long __value) noexcept
      : __unsigned_long_long_(__value) {}
#  ifndef _LIBCPP_HAS_NO_INT128
  _LIBCPP_HIDE_FROM_ABI __basic_format_arg_value(__int128_t __value) noexcept : __i128_(__value) {}
  _LIBCPP_HIDE_FROM_ABI __basic_format_arg_value(__uint128_t __value) noexcept : __u128_(__value) {}
#  endif
  _LIBCPP_HIDE_FROM_ABI __basic_format_arg_value(float __value) noexcept : __float_(__value) {}
  _LIBCPP_HIDE_FROM_ABI __basic_format_arg_value(double __value) noexcept : __double_(__value) {}
  _LIBCPP_HIDE_FROM_ABI __basic_format_arg_value(long double __value) noexcept : __long_double_(__value) {}
  _LIBCPP_HIDE_FROM_ABI __basic_format_arg_value(const _CharT* __value) noexcept : __const_char_type_ptr_(__value) {}
  _LIBCPP_HIDE_FROM_ABI __basic_format_arg_value(basic_string_view<_CharT> __value) noexcept
      : __string_view_(__value) {}
  _LIBCPP_HIDE_FROM_ABI __basic_format_arg_value(const void* __value) noexcept : __ptr_(__value) {}
  _LIBCPP_HIDE_FROM_ABI __basic_format_arg_value(__handle&& __value) noexcept : __handle_(std::move(__value)) {}
};

template <class _Context>
class _LIBCPP_TEMPLATE_VIS basic_format_arg {
public:
  class _LIBCPP_TEMPLATE_VIS handle;

  _LIBCPP_HIDE_FROM_ABI basic_format_arg() noexcept : __type_{__format::__arg_t::__none} {}

  _LIBCPP_HIDE_FROM_ABI explicit operator bool() const noexcept { return __type_ != __format::__arg_t::__none; }

#  if _LIBCPP_STD_VER >= 26 && defined(_LIBCPP_HAS_EXPLICIT_THIS_PARAMETER)

  // This function is user facing, so it must wrap the non-standard types of
  // the "variant" in a handle to stay conforming. See __arg_t for more details.
  template <class _Visitor>
  _LIBCPP_HIDE_FROM_ABI decltype(auto) visit(this basic_format_arg __arg, _Visitor&& __vis) {
    switch (__arg.__type_) {
#    ifndef _LIBCPP_HAS_NO_INT128
    case __format::__arg_t::__i128: {
      typename __basic_format_arg_value<_Context>::__handle __h{__arg.__value_.__i128_};
      return std::invoke(std::forward<_Visitor>(__vis), typename basic_format_arg<_Context>::handle{__h});
    }

    case __format::__arg_t::__u128: {
      typename __basic_format_arg_value<_Context>::__handle __h{__arg.__value_.__u128_};
      return std::invoke(std::forward<_Visitor>(__vis), typename basic_format_arg<_Context>::handle{__h});
    }
#    endif
    default:
      return std::__visit_format_arg(std::forward<_Visitor>(__vis), __arg);
    }
  }

  // This function is user facing, so it must wrap the non-standard types of
  // the "variant" in a handle to stay conforming. See __arg_t for more details.
  template <class _Rp, class _Visitor>
  _LIBCPP_HIDE_FROM_ABI _Rp visit(this basic_format_arg __arg, _Visitor&& __vis) {
    switch (__arg.__type_) {
#    ifndef _LIBCPP_HAS_NO_INT128
    case __format::__arg_t::__i128: {
      typename __basic_format_arg_value<_Context>::__handle __h{__arg.__value_.__i128_};
      return std::invoke_r<_Rp>(std::forward<_Visitor>(__vis), typename basic_format_arg<_Context>::handle{__h});
    }

    case __format::__arg_t::__u128: {
      typename __basic_format_arg_value<_Context>::__handle __h{__arg.__value_.__u128_};
      return std::invoke_r<_Rp>(std::forward<_Visitor>(__vis), typename basic_format_arg<_Context>::handle{__h});
    }
#    endif
    default:
      return std::__visit_format_arg<_Rp>(std::forward<_Visitor>(__vis), __arg);
    }
  }

#  endif // _LIBCPP_STD_VER >= 26 && defined(_LIBCPP_HAS_EXPLICIT_THIS_PARAMETER)

private:
  using char_type = typename _Context::char_type;

  // TODO FMT Implement constrain [format.arg]/4
  // Constraints: The template specialization
  //   typename Context::template formatter_type<T>
  // meets the Formatter requirements ([formatter.requirements]).  The extent
  // to which an implementation determines that the specialization meets the
  // Formatter requirements is unspecified, except that as a minimum the
  // expression
  //   typename Context::template formatter_type<T>()
  //    .format(declval<const T&>(), declval<Context&>())
  // shall be well-formed when treated as an unevaluated operand.

public:
  __basic_format_arg_value<_Context> __value_;
  __format::__arg_t __type_;

  _LIBCPP_HIDE_FROM_ABI explicit basic_format_arg(__format::__arg_t __type,
                                                  __basic_format_arg_value<_Context> __value) noexcept
      : __value_(__value), __type_(__type) {}
};

template <class _Context>
class _LIBCPP_TEMPLATE_VIS basic_format_arg<_Context>::handle {
public:
  _LIBCPP_HIDE_FROM_ABI void format(basic_format_parse_context<char_type>& __parse_ctx, _Context& __ctx) const {
    __handle_.__format_(__parse_ctx, __ctx, __handle_.__ptr_);
  }

  _LIBCPP_HIDE_FROM_ABI explicit handle(typename __basic_format_arg_value<_Context>::__handle& __handle) noexcept
      : __handle_(__handle) {}

private:
  typename __basic_format_arg_value<_Context>::__handle& __handle_;
};

// This function is user facing, so it must wrap the non-standard types of
// the "variant" in a handle to stay conforming. See __arg_t for more details.
template <class _Visitor, class _Context>
#  if _LIBCPP_STD_VER >= 26 && defined(_LIBCPP_HAS_EXPLICIT_THIS_PARAMETER)
_LIBCPP_DEPRECATED_IN_CXX26
#  endif
    _LIBCPP_HIDE_FROM_ABI decltype(auto)
    visit_format_arg(_Visitor&& __vis, basic_format_arg<_Context> __arg) {
  switch (__arg.__type_) {
#  ifndef _LIBCPP_HAS_NO_INT128
  case __format::__arg_t::__i128: {
    typename __basic_format_arg_value<_Context>::__handle __h{__arg.__value_.__i128_};
    return std::invoke(std::forward<_Visitor>(__vis), typename basic_format_arg<_Context>::handle{__h});
  }

  case __format::__arg_t::__u128: {
    typename __basic_format_arg_value<_Context>::__handle __h{__arg.__value_.__u128_};
    return std::invoke(std::forward<_Visitor>(__vis), typename basic_format_arg<_Context>::handle{__h});
  }
#  endif // _LIBCPP_STD_VER >= 26 && defined(_LIBCPP_HAS_EXPLICIT_THIS_PARAMETER)
  default:
    return std::__visit_format_arg(std::forward<_Visitor>(__vis), __arg);
  }
}

#endif //_LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___FORMAT_FORMAT_ARG_H
