// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef _LIBCPP___EXPECTED_EXPECTED_H
#define _LIBCPP___EXPECTED_EXPECTED_H

#include <__assert>
#include <__config>
#include <__expected/bad_expected_access.h>
#include <__expected/unexpect.h>
#include <__expected/unexpected.h>
#include <__functional/invoke.h>
#include <__memory/addressof.h>
#include <__memory/construct_at.h>
#include <__type_traits/conjunction.h>
#include <__type_traits/disjunction.h>
#include <__type_traits/integral_constant.h>
#include <__type_traits/is_assignable.h>
#include <__type_traits/is_constructible.h>
#include <__type_traits/is_convertible.h>
#include <__type_traits/is_function.h>
#include <__type_traits/is_nothrow_assignable.h>
#include <__type_traits/is_nothrow_constructible.h>
#include <__type_traits/is_reference.h>
#include <__type_traits/is_same.h>
#include <__type_traits/is_swappable.h>
#include <__type_traits/is_trivially_constructible.h>
#include <__type_traits/is_trivially_destructible.h>
#include <__type_traits/is_trivially_relocatable.h>
#include <__type_traits/is_void.h>
#include <__type_traits/lazy.h>
#include <__type_traits/negation.h>
#include <__type_traits/remove_cv.h>
#include <__type_traits/remove_cvref.h>
#include <__utility/as_const.h>
#include <__utility/exception_guard.h>
#include <__utility/forward.h>
#include <__utility/in_place.h>
#include <__utility/move.h>
#include <__utility/swap.h>
#include <__verbose_abort>
#include <initializer_list>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

#if _LIBCPP_STD_VER >= 23

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Tp, class _Err>
class expected;

template <class _Tp>
struct __is_std_expected : false_type {};

template <class _Tp, class _Err>
struct __is_std_expected<expected<_Tp, _Err>> : true_type {};

struct __expected_construct_in_place_from_invoke_tag {};
struct __expected_construct_unexpected_from_invoke_tag {};

template <class _Err, class _Arg>
_LIBCPP_HIDE_FROM_ABI void __throw_bad_expected_access(_Arg&& __arg) {
#  ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  throw bad_expected_access<_Err>(std::forward<_Arg>(__arg));
#  else
  (void)__arg;
  _LIBCPP_VERBOSE_ABORT("bad_expected_access was thrown in -fno-exceptions mode");
#  endif
}

// If parameter type `_Tp` of `__conditional_no_unique_address` is neither
// copyable nor movable, a constructor with this tag is provided. For that
// constructor, the user has to provide a function and arguments. The function
// must return an object of type `_Tp`. When the function is invoked by the
// constructor, guaranteed copy elision kicks in and the `_Tp` is constructed
// in place.
struct __conditional_no_unique_address_invoke_tag {};

// This class implements an object with `[[no_unique_address]]` conditionally applied to it,
// based on the value of `_NoUnique`.
//
// A member of this class must always have `[[no_unique_address]]` applied to
// it. Otherwise, the `[[no_unique_address]]` in the "`_NoUnique == true`" case
// would not have any effect. In the `false` case, the `__v` is not
// `[[no_unique_address]]`, so nullifies the effects of the "outer"
// `[[no_unique_address]]` regarding data layout.
//
// If we had a language feature, this class would basically be replaced by `[[no_unique_address(condition)]]`.
template <bool _NoUnique, class _Tp>
struct __conditional_no_unique_address;

template <class _Tp>
struct __conditional_no_unique_address<true, _Tp> {
  template <class... _Args>
  _LIBCPP_HIDE_FROM_ABI constexpr explicit __conditional_no_unique_address(in_place_t, _Args&&... __args)
      : __v(std::forward<_Args>(__args)...) {}

  template <class _Func, class... _Args>
  _LIBCPP_HIDE_FROM_ABI constexpr explicit __conditional_no_unique_address(
      __conditional_no_unique_address_invoke_tag, _Func&& __f, _Args&&... __args)
      : __v(std::invoke(std::forward<_Func>(__f), std::forward<_Args>(__args)...)) {}

  _LIBCPP_NO_UNIQUE_ADDRESS _Tp __v;
};

template <class _Tp>
struct __conditional_no_unique_address<false, _Tp> {
  template <class... _Args>
  _LIBCPP_HIDE_FROM_ABI constexpr explicit __conditional_no_unique_address(in_place_t, _Args&&... __args)
      : __v(std::forward<_Args>(__args)...) {}

  template <class _Func, class... _Args>
  _LIBCPP_HIDE_FROM_ABI constexpr explicit __conditional_no_unique_address(
      __conditional_no_unique_address_invoke_tag, _Func&& __f, _Args&&... __args)
      : __v(std::invoke(std::forward<_Func>(__f), std::forward<_Args>(__args)...)) {}

  _Tp __v;
};

// This function returns whether the type `_Second` can be stuffed into the tail padding
// of the `_First` type if both of them are given `[[no_unique_address]]`.
template <class _First, class _Second>
inline constexpr bool __fits_in_tail_padding = []() {
  struct __x {
    _LIBCPP_NO_UNIQUE_ADDRESS _First __first;
    _LIBCPP_NO_UNIQUE_ADDRESS _Second __second;
  };
  return sizeof(__x) == sizeof(_First);
}();

// This class implements the storage used by `std::expected`. We have a few
// goals for this storage:
// 1. Whenever the underlying {_Tp | _Unex} combination has free bytes in its
//    tail padding, we should reuse it to store the bool discriminator of the
//    expected, so as to save space.
// 2. Whenever the `expected<_Tp, _Unex>` as a whole has free bytes in its tail
//    padding, we should allow an object following the expected to be stored in
//    its tail padding.
// 3. However, we never want a user object (say `X`) that would follow an
//    `expected<_Tp, _Unex>` to be stored in the padding bytes of the
//    underlying {_Tp | _Unex} union, if any. That is because we use
//    `construct_at` on that union, which would end up overwriting the `X`
//    member if it is stored in the tail padding of the union.
//
// To achieve this, `__expected_base`'s logic is implemented in an inner
// `__repr` class. `__expected_base` holds one `__repr` member which is
// conditionally `[[no_unique_address]]`. The `__repr` class holds the
// underlying {_Tp | _Unex} union and a boolean "has value" flag.
//
// Which one of the `__repr_`/`__union_` members is `[[no_unique_address]]`
// depends on whether the "has value" boolean fits into the tail padding of
// the underlying {_Tp | _Unex} union:
//
// - In case the "has value" bool fits into the tail padding of the union, the
//   whole `__repr_` member is _not_ `[[no_unique_address]]` as it needs to be
//   transparently replaced on `emplace()`/`swap()` etc.
// - In case the "has value" bool does not fit into the tail padding of the
//   union, only the union member must be transparently replaced (therefore is
//   _not_ `[[no_unique_address]]`) and the "has value" flag must be adjusted
//   manually.
//
// This way, the member that is transparently replaced on mutating operations
// is never `[[no_unique_address]]`, satisfying the requirements from
// "[basic.life]" in the standard.
//
// Stripped away of all superfluous elements, the layout of `__expected_base`
// then looks like this:
//
//     template <class Tp, class Err>
//     class expected_base {
//       union union_t {
//         [[no_unique_address]] Tp val;
//         [[no_unique_address]] Err unex;
//       };
//
//       static constexpr bool put_flag_in_tail                    = fits_in_tail_padding<union_t, bool>;
//       static constexpr bool allow_reusing_expected_tail_padding = !put_flag_in_tail;
//
//       struct repr {
//       private:
//         // If "has value" fits into the tail, this should be
//         // `[[no_unique_address]]`, otherwise not.
//         [[no_unique_address]] conditional_no_unique_address<
//             put_flag_in_tail,
//             union_t>::type union_;
//         [[no_unique_address]] bool has_val_;
//       };
//
//     protected:
//       // If "has value" fits into the tail, this must _not_ be
//       // `[[no_unique_address]]` so that we fill out the
//       // complete `expected` object.
//       [[no_unique_address]] conditional_no_unique_address<
//           allow_reusing_expected_tail_padding,
//           repr>::type repr_;
//     };
//
template <class _Tp, class _Err>
class __expected_base {
  // use named union because [[no_unique_address]] cannot be applied to an unnamed union,
  // also guaranteed elision into a potentially-overlapping subobject is unsettled (and
  // it's not clear that it's implementable, given that the function is allowed to clobber
  // the tail padding) - see https://github.com/itanium-cxx-abi/cxx-abi/issues/107.
  union __union_t {
    _LIBCPP_HIDE_FROM_ABI constexpr __union_t(const __union_t&) = delete;
    _LIBCPP_HIDE_FROM_ABI constexpr __union_t(const __union_t&)
      requires(is_copy_constructible_v<_Tp> && is_copy_constructible_v<_Err> &&
               is_trivially_copy_constructible_v<_Tp> && is_trivially_copy_constructible_v<_Err>)
    = default;
    _LIBCPP_HIDE_FROM_ABI constexpr __union_t(__union_t&&) = delete;
    _LIBCPP_HIDE_FROM_ABI constexpr __union_t(__union_t&&)
      requires(is_move_constructible_v<_Tp> && is_move_constructible_v<_Err> &&
               is_trivially_move_constructible_v<_Tp> && is_trivially_move_constructible_v<_Err>)
    = default;
    _LIBCPP_HIDE_FROM_ABI constexpr __union_t& operator=(const __union_t&) = delete;
    _LIBCPP_HIDE_FROM_ABI constexpr __union_t& operator=(__union_t&&)      = delete;

    template <class... _Args>
    _LIBCPP_HIDE_FROM_ABI constexpr explicit __union_t(in_place_t, _Args&&... __args)
        : __val_(std::forward<_Args>(__args)...) {}

    template <class... _Args>
    _LIBCPP_HIDE_FROM_ABI constexpr explicit __union_t(unexpect_t, _Args&&... __args)
        : __unex_(std::forward<_Args>(__args)...) {}

    template <class _Func, class... _Args>
    _LIBCPP_HIDE_FROM_ABI constexpr explicit __union_t(
        std::__expected_construct_in_place_from_invoke_tag, _Func&& __f, _Args&&... __args)
        : __val_(std::invoke(std::forward<_Func>(__f), std::forward<_Args>(__args)...)) {}

    template <class _Func, class... _Args>
    _LIBCPP_HIDE_FROM_ABI constexpr explicit __union_t(
        std::__expected_construct_unexpected_from_invoke_tag, _Func&& __f, _Args&&... __args)
        : __unex_(std::invoke(std::forward<_Func>(__f), std::forward<_Args>(__args)...)) {}

    _LIBCPP_HIDE_FROM_ABI constexpr ~__union_t()
      requires(is_trivially_destructible_v<_Tp> && is_trivially_destructible_v<_Err>)
    = default;

    // __repr's destructor handles this
    _LIBCPP_HIDE_FROM_ABI constexpr ~__union_t() {}

    _LIBCPP_NO_UNIQUE_ADDRESS _Tp __val_;
    _LIBCPP_NO_UNIQUE_ADDRESS _Err __unex_;
  };

  static constexpr bool __put_flag_in_tail                    = __fits_in_tail_padding<__union_t, bool>;
  static constexpr bool __allow_reusing_expected_tail_padding = !__put_flag_in_tail;

  struct __repr {
    _LIBCPP_HIDE_FROM_ABI constexpr explicit __repr() = delete;

    template <class... _Args>
    _LIBCPP_HIDE_FROM_ABI constexpr explicit __repr(in_place_t __tag, _Args&&... __args)
        : __union_(in_place, __tag, std::forward<_Args>(__args)...), __has_val_(true) {}

    template <class... _Args>
    _LIBCPP_HIDE_FROM_ABI constexpr explicit __repr(unexpect_t __tag, _Args&&... __args)
        : __union_(in_place, __tag, std::forward<_Args>(__args)...), __has_val_(false) {}

    template <class... _Args>
    _LIBCPP_HIDE_FROM_ABI constexpr explicit __repr(std::__expected_construct_in_place_from_invoke_tag __tag,
                                                    _Args&&... __args)
        : __union_(in_place, __tag, std::forward<_Args>(__args)...), __has_val_(true) {}

    template <class... _Args>
    _LIBCPP_HIDE_FROM_ABI constexpr explicit __repr(std::__expected_construct_unexpected_from_invoke_tag __tag,
                                                    _Args&&... __args)
        : __union_(in_place, __tag, std::forward<_Args>(__args)...), __has_val_(false) {}

    // The return value of `__make_union` must be constructed in place in the
    // `__v` member of `__union_`, relying on guaranteed copy elision. To do
    // this, the `__conditional_no_unique_address_invoke_tag` constructor is
    // called with a lambda that is immediately called inside
    // `__conditional_no_unique_address`'s constructor.
    template <class _OtherUnion>
    _LIBCPP_HIDE_FROM_ABI constexpr explicit __repr(bool __has_val, _OtherUnion&& __other)
      requires(__allow_reusing_expected_tail_padding)
        : __union_(__conditional_no_unique_address_invoke_tag{},
                   [&] { return __make_union(__has_val, std::forward<_OtherUnion>(__other)); }),
          __has_val_(__has_val) {}

    _LIBCPP_HIDE_FROM_ABI constexpr __repr(const __repr&) = delete;
    _LIBCPP_HIDE_FROM_ABI constexpr __repr(const __repr&)
      requires(is_copy_constructible_v<_Tp> && is_copy_constructible_v<_Err> &&
               is_trivially_copy_constructible_v<_Tp> && is_trivially_copy_constructible_v<_Err>)
    = default;
    _LIBCPP_HIDE_FROM_ABI constexpr __repr(__repr&&) = delete;
    _LIBCPP_HIDE_FROM_ABI constexpr __repr(__repr&&)
      requires(is_move_constructible_v<_Tp> && is_move_constructible_v<_Err> &&
               is_trivially_move_constructible_v<_Tp> && is_trivially_move_constructible_v<_Err>)
    = default;

    _LIBCPP_HIDE_FROM_ABI constexpr __repr& operator=(const __repr&) = delete;
    _LIBCPP_HIDE_FROM_ABI constexpr __repr& operator=(__repr&&)      = delete;

    _LIBCPP_HIDE_FROM_ABI constexpr ~__repr()
      requires(is_trivially_destructible_v<_Tp> && is_trivially_destructible_v<_Err>)
    = default;

    _LIBCPP_HIDE_FROM_ABI constexpr ~__repr()
      requires(!is_trivially_destructible_v<_Tp> || !is_trivially_destructible_v<_Err>)
    {
      __destroy_union_member();
    }

    _LIBCPP_HIDE_FROM_ABI constexpr void __destroy_union()
      requires(__allow_reusing_expected_tail_padding &&
               (is_trivially_destructible_v<_Tp> && is_trivially_destructible_v<_Err>))
    {
      // Note: Since the destructor of the union is trivial, this does nothing
      // except to end the lifetime of the union.
      std::destroy_at(&__union_.__v);
    }

    _LIBCPP_HIDE_FROM_ABI constexpr void __destroy_union()
      requires(__allow_reusing_expected_tail_padding &&
               (!is_trivially_destructible_v<_Tp> || !is_trivially_destructible_v<_Err>))
    {
      __destroy_union_member();
      std::destroy_at(&__union_.__v);
    }

    template <class... _Args>
    _LIBCPP_HIDE_FROM_ABI constexpr void __construct_union(in_place_t, _Args&&... __args)
      requires(__allow_reusing_expected_tail_padding)
    {
      std::construct_at(&__union_.__v, in_place, std::forward<_Args>(__args)...);
      __has_val_ = true;
    }

    template <class... _Args>
    _LIBCPP_HIDE_FROM_ABI constexpr void __construct_union(unexpect_t, _Args&&... __args)
      requires(__allow_reusing_expected_tail_padding)
    {
      std::construct_at(&__union_.__v, unexpect, std::forward<_Args>(__args)...);
      __has_val_ = false;
    }

  private:
    template <class, class>
    friend class __expected_base;

    _LIBCPP_HIDE_FROM_ABI constexpr void __destroy_union_member()
      requires(!is_trivially_destructible_v<_Tp> || !is_trivially_destructible_v<_Err>)
    {
      if (__has_val_) {
        std::destroy_at(std::addressof(__union_.__v.__val_));
      } else {
        std::destroy_at(std::addressof(__union_.__v.__unex_));
      }
    }

    template <class _OtherUnion>
    _LIBCPP_HIDE_FROM_ABI static constexpr __union_t __make_union(bool __has_val, _OtherUnion&& __other)
      requires(__allow_reusing_expected_tail_padding)
    {
      if (__has_val)
        return __union_t(in_place, std::forward<_OtherUnion>(__other).__val_);
      else
        return __union_t(unexpect, std::forward<_OtherUnion>(__other).__unex_);
    }

    _LIBCPP_NO_UNIQUE_ADDRESS __conditional_no_unique_address<__put_flag_in_tail, __union_t> __union_;
    _LIBCPP_NO_UNIQUE_ADDRESS bool __has_val_;
  };

  template <class _OtherUnion>
  _LIBCPP_HIDE_FROM_ABI static constexpr __repr __make_repr(bool __has_val, _OtherUnion&& __other)
    requires(__put_flag_in_tail)
  {
    if (__has_val)
      return __repr(in_place, std::forward<_OtherUnion>(__other).__val_);
    else
      return __repr(unexpect, std::forward<_OtherUnion>(__other).__unex_);
  }

protected:
  template <class... _Args>
  _LIBCPP_HIDE_FROM_ABI constexpr explicit __expected_base(_Args&&... __args)
      : __repr_(in_place, std::forward<_Args>(__args)...) {}

  // In case we copy/move construct from another `expected` we need to create
  // our `expected` so that it either has a value or not, depending on the "has
  // value" flag of the other `expected`. To do this without falling back on
  // `std::construct_at` we rely on guaranteed copy elision using two helper
  // functions `__make_repr` and `__make_union`. There have to be two since
  // there are two data layouts with different members being
  // `[[no_unique_address]]`. GCC (as of version 13) does not do guaranteed
  // copy elision when initializing `[[no_unique_address]]` members. The two
  // cases are:
  //
  // - `__make_repr`: This is used when the "has value" flag lives in the tail
  //   of the union. In this case, the `__repr` member is _not_
  //   `[[no_unique_address]]`.
  // - `__make_union`: When the "has value" flag does _not_ fit in the tail of
  //   the union, the `__repr` member is `[[no_unique_address]]` and the union
  //   is not.
  //
  // This constructor "catches" the first case and leaves the second case to
  // `__union_t`, its constructors and `__make_union`.
  template <class _OtherUnion>
  _LIBCPP_HIDE_FROM_ABI constexpr explicit __expected_base(bool __has_val, _OtherUnion&& __other)
    requires(__put_flag_in_tail)
      : __repr_(__conditional_no_unique_address_invoke_tag{},
                [&] { return __make_repr(__has_val, std::forward<_OtherUnion>(__other)); }) {}

  _LIBCPP_HIDE_FROM_ABI constexpr void __destroy() {
    if constexpr (__put_flag_in_tail)
      std::destroy_at(&__repr_.__v);
    else
      __repr_.__v.__destroy_union();
  }

  template <class _Tag, class... _Args>
  _LIBCPP_HIDE_FROM_ABI constexpr void __construct(_Tag __tag, _Args&&... __args) {
    if constexpr (__put_flag_in_tail)
      std::construct_at(&__repr_.__v, __tag, std::forward<_Args>(__args)...);
    else
      __repr_.__v.__construct_union(__tag, std::forward<_Args>(__args)...);
  }

  _LIBCPP_HIDE_FROM_ABI constexpr bool __has_val() const { return __repr_.__v.__has_val_; }
  _LIBCPP_HIDE_FROM_ABI constexpr __union_t& __union() { return __repr_.__v.__union_.__v; }
  _LIBCPP_HIDE_FROM_ABI constexpr const __union_t& __union() const { return __repr_.__v.__union_.__v; }
  _LIBCPP_HIDE_FROM_ABI constexpr _Tp& __val() { return __repr_.__v.__union_.__v.__val_; }
  _LIBCPP_HIDE_FROM_ABI constexpr const _Tp& __val() const { return __repr_.__v.__union_.__v.__val_; }
  _LIBCPP_HIDE_FROM_ABI constexpr _Err& __unex() { return __repr_.__v.__union_.__v.__unex_; }
  _LIBCPP_HIDE_FROM_ABI constexpr const _Err& __unex() const { return __repr_.__v.__union_.__v.__unex_; }

private:
  _LIBCPP_NO_UNIQUE_ADDRESS __conditional_no_unique_address<__allow_reusing_expected_tail_padding, __repr> __repr_;
};

template <class _Tp, class _Err>
class expected : private __expected_base<_Tp, _Err> {
  static_assert(!is_reference_v<_Tp> && !is_function_v<_Tp> && !is_same_v<remove_cv_t<_Tp>, in_place_t> &&
                    !is_same_v<remove_cv_t<_Tp>, unexpect_t> && !__is_std_unexpected<remove_cv_t<_Tp>>::value &&
                    __valid_std_unexpected<_Err>::value,
                "[expected.object.general] A program that instantiates the definition of template expected<T, E> for a "
                "reference type, a function type, or for possibly cv-qualified types in_place_t, unexpect_t, or a "
                "specialization of unexpected for the T parameter is ill-formed. A program that instantiates the "
                "definition of the template expected<T, E> with a type for the E parameter that is not a valid "
                "template argument for unexpected is ill-formed.");

  template <class _Up, class _OtherErr>
  friend class expected;

  using __base = __expected_base<_Tp, _Err>;

public:
  using value_type      = _Tp;
  using error_type      = _Err;
  using unexpected_type = unexpected<_Err>;

  using __trivially_relocatable =
      __conditional_t<__libcpp_is_trivially_relocatable<_Tp>::value && __libcpp_is_trivially_relocatable<_Err>::value,
                      expected,
                      void>;

  template <class _Up>
  using rebind = expected<_Up, error_type>;

  // [expected.object.ctor], constructors
  _LIBCPP_HIDE_FROM_ABI constexpr expected() noexcept(is_nothrow_default_constructible_v<_Tp>) // strengthened
    requires is_default_constructible_v<_Tp>
      : __base(in_place) {}

  _LIBCPP_HIDE_FROM_ABI constexpr expected(const expected&) = delete;

  _LIBCPP_HIDE_FROM_ABI constexpr expected(const expected&)
    requires(is_copy_constructible_v<_Tp> && is_copy_constructible_v<_Err> && is_trivially_copy_constructible_v<_Tp> &&
             is_trivially_copy_constructible_v<_Err>)
  = default;

  _LIBCPP_HIDE_FROM_ABI constexpr expected(const expected& __other) noexcept(
      is_nothrow_copy_constructible_v<_Tp> && is_nothrow_copy_constructible_v<_Err>) // strengthened
    requires(is_copy_constructible_v<_Tp> && is_copy_constructible_v<_Err> &&
             !(is_trivially_copy_constructible_v<_Tp> && is_trivially_copy_constructible_v<_Err>))
      : __base(__other.__has_val(), __other.__union()) {}

  _LIBCPP_HIDE_FROM_ABI constexpr expected(expected&&)
    requires(is_move_constructible_v<_Tp> && is_move_constructible_v<_Err> && is_trivially_move_constructible_v<_Tp> &&
             is_trivially_move_constructible_v<_Err>)
  = default;

  _LIBCPP_HIDE_FROM_ABI constexpr expected(expected&& __other) noexcept(
      is_nothrow_move_constructible_v<_Tp> && is_nothrow_move_constructible_v<_Err>)
    requires(is_move_constructible_v<_Tp> && is_move_constructible_v<_Err> &&
             !(is_trivially_move_constructible_v<_Tp> && is_trivially_move_constructible_v<_Err>))
      : __base(__other.__has_val(), std::move(__other.__union())) {}

private:
  template <class _Up, class _OtherErr, class _UfQual, class _OtherErrQual>
  using __can_convert =
      _And< is_constructible<_Tp, _UfQual>,
            is_constructible<_Err, _OtherErrQual>,
            _If<_Not<is_same<remove_cv_t<_Tp>, bool>>::value,
                _And< 
                      _Not<_And<is_same<_Tp, _Up>, is_same<_Err, _OtherErr>>>, // use the copy constructor instead, see #92676
                      _Not<is_constructible<_Tp, expected<_Up, _OtherErr>&>>,
                      _Not<is_constructible<_Tp, expected<_Up, _OtherErr>>>,
                      _Not<is_constructible<_Tp, const expected<_Up, _OtherErr>&>>,
                      _Not<is_constructible<_Tp, const expected<_Up, _OtherErr>>>,
                      _Not<is_convertible<expected<_Up, _OtherErr>&, _Tp>>,
                      _Not<is_convertible<expected<_Up, _OtherErr>&&, _Tp>>,
                      _Not<is_convertible<const expected<_Up, _OtherErr>&, _Tp>>,
                      _Not<is_convertible<const expected<_Up, _OtherErr>&&, _Tp>>>,
                true_type>,
            _Not<is_constructible<unexpected<_Err>, expected<_Up, _OtherErr>&>>,
            _Not<is_constructible<unexpected<_Err>, expected<_Up, _OtherErr>>>,
            _Not<is_constructible<unexpected<_Err>, const expected<_Up, _OtherErr>&>>,
            _Not<is_constructible<unexpected<_Err>, const expected<_Up, _OtherErr>>> >;

  template <class _Func, class... _Args>
  _LIBCPP_HIDE_FROM_ABI constexpr explicit expected(
      std::__expected_construct_in_place_from_invoke_tag __tag, _Func&& __f, _Args&&... __args)
      : __base(__tag, std::forward<_Func>(__f), std::forward<_Args>(__args)...) {}

  template <class _Func, class... _Args>
  _LIBCPP_HIDE_FROM_ABI constexpr explicit expected(
      std::__expected_construct_unexpected_from_invoke_tag __tag, _Func&& __f, _Args&&... __args)
      : __base(__tag, std::forward<_Func>(__f), std::forward<_Args>(__args)...) {}

public:
  template <class _Up, class _OtherErr>
    requires __can_convert<_Up, _OtherErr, const _Up&, const _OtherErr&>::value
  _LIBCPP_HIDE_FROM_ABI constexpr explicit(!is_convertible_v<const _Up&, _Tp> ||
                                           !is_convertible_v<const _OtherErr&, _Err>)
      expected(const expected<_Up, _OtherErr>& __other) noexcept(
          is_nothrow_constructible_v<_Tp, const _Up&> &&
          is_nothrow_constructible_v<_Err, const _OtherErr&>) // strengthened
      : __base(__other.__has_val(), __other.__union()) {}

  template <class _Up, class _OtherErr>
    requires __can_convert<_Up, _OtherErr, _Up, _OtherErr>::value
  _LIBCPP_HIDE_FROM_ABI constexpr explicit(!is_convertible_v<_Up, _Tp> || !is_convertible_v<_OtherErr, _Err>)
      expected(expected<_Up, _OtherErr>&& __other) noexcept(
          is_nothrow_constructible_v<_Tp, _Up> && is_nothrow_constructible_v<_Err, _OtherErr>) // strengthened
      : __base(__other.__has_val(), std::move(__other.__union())) {}

  template <class _Up = _Tp>
    requires(!is_same_v<remove_cvref_t<_Up>, in_place_t> && !is_same_v<expected, remove_cvref_t<_Up>> &&
             is_constructible_v<_Tp, _Up> && !__is_std_unexpected<remove_cvref_t<_Up>>::value &&
             (!is_same_v<remove_cv_t<_Tp>, bool> || !__is_std_expected<remove_cvref_t<_Up>>::value))
  _LIBCPP_HIDE_FROM_ABI constexpr explicit(!is_convertible_v<_Up, _Tp>)
      expected(_Up&& __u) noexcept(is_nothrow_constructible_v<_Tp, _Up>) // strengthened
      : __base(in_place, std::forward<_Up>(__u)) {}

  template <class _OtherErr>
    requires is_constructible_v<_Err, const _OtherErr&>
  _LIBCPP_HIDE_FROM_ABI constexpr explicit(!is_convertible_v<const _OtherErr&, _Err>) expected(
      const unexpected<_OtherErr>& __unex) noexcept(is_nothrow_constructible_v<_Err, const _OtherErr&>) // strengthened
      : __base(unexpect, __unex.error()) {}

  template <class _OtherErr>
    requires is_constructible_v<_Err, _OtherErr>
  _LIBCPP_HIDE_FROM_ABI constexpr explicit(!is_convertible_v<_OtherErr, _Err>)
      expected(unexpected<_OtherErr>&& __unex) noexcept(is_nothrow_constructible_v<_Err, _OtherErr>) // strengthened
      : __base(unexpect, std::move(__unex.error())) {}

  template <class... _Args>
    requires is_constructible_v<_Tp, _Args...>
  _LIBCPP_HIDE_FROM_ABI constexpr explicit expected(in_place_t, _Args&&... __args) noexcept(
      is_nothrow_constructible_v<_Tp, _Args...>) // strengthened
      : __base(in_place, std::forward<_Args>(__args)...) {}

  template <class _Up, class... _Args>
    requires is_constructible_v< _Tp, initializer_list<_Up>&, _Args... >
  _LIBCPP_HIDE_FROM_ABI constexpr explicit expected(in_place_t, initializer_list<_Up> __il, _Args&&... __args) noexcept(
      is_nothrow_constructible_v<_Tp, initializer_list<_Up>&, _Args...>) // strengthened
      : __base(in_place, __il, std::forward<_Args>(__args)...) {}

  template <class... _Args>
    requires is_constructible_v<_Err, _Args...>
  _LIBCPP_HIDE_FROM_ABI constexpr explicit expected(unexpect_t, _Args&&... __args) noexcept(
      is_nothrow_constructible_v<_Err, _Args...>) // strengthened
      : __base(unexpect, std::forward<_Args>(__args)...) {}

  template <class _Up, class... _Args>
    requires is_constructible_v< _Err, initializer_list<_Up>&, _Args... >
  _LIBCPP_HIDE_FROM_ABI constexpr explicit expected(unexpect_t, initializer_list<_Up> __il, _Args&&... __args) noexcept(
      is_nothrow_constructible_v<_Err, initializer_list<_Up>&, _Args...>) // strengthened
      : __base(unexpect, __il, std::forward<_Args>(__args)...) {}

  // [expected.object.dtor], destructor

  _LIBCPP_HIDE_FROM_ABI constexpr ~expected() = default;

private:
  template <class _Tag, class _OtherTag, class _T1, class _T2, class... _Args>
  _LIBCPP_HIDE_FROM_ABI constexpr void __reinit_expected(_T2& __oldval, _Args&&... __args) {
    if constexpr (is_nothrow_constructible_v<_T1, _Args...>) {
      this->__destroy();
      this->__construct(_Tag{}, std::forward<_Args>(__args)...);
    } else if constexpr (is_nothrow_move_constructible_v<_T1>) {
      _T1 __tmp(std::forward<_Args>(__args)...);
      this->__destroy();
      this->__construct(_Tag{}, std::move(__tmp));
    } else {
      static_assert(
          is_nothrow_move_constructible_v<_T2>,
          "To provide strong exception guarantee, T2 has to satisfy `is_nothrow_move_constructible_v` so that it can "
          "be reverted to the previous state in case an exception is thrown during the assignment.");
      _T2 __tmp(std::move(__oldval));
      this->__destroy();
      auto __trans = std::__make_exception_guard([&] { this->__construct(_OtherTag{}, std::move(__tmp)); });
      this->__construct(_Tag{}, std::forward<_Args>(__args)...);
      __trans.__complete();
    }
  }

public:
  // [expected.object.assign], assignment
  _LIBCPP_HIDE_FROM_ABI constexpr expected& operator=(const expected&) = delete;

  _LIBCPP_HIDE_FROM_ABI constexpr expected& operator=(const expected& __rhs) noexcept(
      is_nothrow_copy_assignable_v<_Tp> && is_nothrow_copy_constructible_v<_Tp> && is_nothrow_copy_assignable_v<_Err> &&
      is_nothrow_copy_constructible_v<_Err>) // strengthened
    requires(is_copy_assignable_v<_Tp> && is_copy_constructible_v<_Tp> && is_copy_assignable_v<_Err> &&
             is_copy_constructible_v<_Err> &&
             (is_nothrow_move_constructible_v<_Tp> || is_nothrow_move_constructible_v<_Err>))
  {
    if (this->__has_val() && __rhs.__has_val()) {
      this->__val() = __rhs.__val();
    } else if (this->__has_val()) {
      __reinit_expected<unexpect_t, in_place_t, _Err, _Tp>(this->__val(), __rhs.__unex());
    } else if (__rhs.__has_val()) {
      __reinit_expected<in_place_t, unexpect_t, _Tp, _Err>(this->__unex(), __rhs.__val());
    } else {
      this->__unex() = __rhs.__unex();
    }
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr expected&
  operator=(expected&& __rhs) noexcept(is_nothrow_move_assignable_v<_Tp> && is_nothrow_move_constructible_v<_Tp> &&
                                       is_nothrow_move_assignable_v<_Err> && is_nothrow_move_constructible_v<_Err>)
    requires(is_move_constructible_v<_Tp> && is_move_assignable_v<_Tp> && is_move_constructible_v<_Err> &&
             is_move_assignable_v<_Err> &&
             (is_nothrow_move_constructible_v<_Tp> || is_nothrow_move_constructible_v<_Err>))
  {
    if (this->__has_val() && __rhs.__has_val()) {
      this->__val() = std::move(__rhs.__val());
    } else if (this->__has_val()) {
      __reinit_expected<unexpect_t, in_place_t, _Err, _Tp>(this->__val(), std::move(__rhs.__unex()));
    } else if (__rhs.__has_val()) {
      __reinit_expected<in_place_t, unexpect_t, _Tp, _Err>(this->__unex(), std::move(__rhs.__val()));
    } else {
      this->__unex() = std::move(__rhs.__unex());
    }
    return *this;
  }

  template <class _Up = _Tp>
  _LIBCPP_HIDE_FROM_ABI constexpr expected& operator=(_Up&& __v)
    requires(!is_same_v<expected, remove_cvref_t<_Up>> && !__is_std_unexpected<remove_cvref_t<_Up>>::value &&
             is_constructible_v<_Tp, _Up> && is_assignable_v<_Tp&, _Up> &&
             (is_nothrow_constructible_v<_Tp, _Up> || is_nothrow_move_constructible_v<_Tp> ||
              is_nothrow_move_constructible_v<_Err>))
  {
    if (this->__has_val()) {
      this->__val() = std::forward<_Up>(__v);
    } else {
      __reinit_expected<in_place_t, unexpect_t, _Tp, _Err>(this->__unex(), std::forward<_Up>(__v));
    }
    return *this;
  }

private:
  template <class _OtherErrQual>
  static constexpr bool __can_assign_from_unexpected =
      _And< is_constructible<_Err, _OtherErrQual>,
            is_assignable<_Err&, _OtherErrQual>,
            _Lazy<_Or,
                  is_nothrow_constructible<_Err, _OtherErrQual>,
                  is_nothrow_move_constructible<_Tp>,
                  is_nothrow_move_constructible<_Err>> >::value;

public:
  template <class _OtherErr>
    requires(__can_assign_from_unexpected<const _OtherErr&>)
  _LIBCPP_HIDE_FROM_ABI constexpr expected& operator=(const unexpected<_OtherErr>& __un) {
    if (this->__has_val()) {
      __reinit_expected<unexpect_t, in_place_t, _Err, _Tp>(this->__val(), __un.error());
    } else {
      this->__unex() = __un.error();
    }
    return *this;
  }

  template <class _OtherErr>
    requires(__can_assign_from_unexpected<_OtherErr>)
  _LIBCPP_HIDE_FROM_ABI constexpr expected& operator=(unexpected<_OtherErr>&& __un) {
    if (this->__has_val()) {
      __reinit_expected<unexpect_t, in_place_t, _Err, _Tp>(this->__val(), std::move(__un.error()));
    } else {
      this->__unex() = std::move(__un.error());
    }
    return *this;
  }

  template <class... _Args>
    requires is_nothrow_constructible_v<_Tp, _Args...>
  _LIBCPP_HIDE_FROM_ABI constexpr _Tp& emplace(_Args&&... __args) noexcept {
    this->__destroy();
    this->__construct(in_place, std::forward<_Args>(__args)...);
    return this->__val();
  }

  template <class _Up, class... _Args>
    requires is_nothrow_constructible_v<_Tp, initializer_list<_Up>&, _Args...>
  _LIBCPP_HIDE_FROM_ABI constexpr _Tp& emplace(initializer_list<_Up> __il, _Args&&... __args) noexcept {
    this->__destroy();
    this->__construct(in_place, __il, std::forward<_Args>(__args)...);
    return this->__val();
  }

public:
  // [expected.object.swap], swap
  _LIBCPP_HIDE_FROM_ABI constexpr void
  swap(expected& __rhs) noexcept(is_nothrow_move_constructible_v<_Tp> && is_nothrow_swappable_v<_Tp> &&
                                 is_nothrow_move_constructible_v<_Err> && is_nothrow_swappable_v<_Err>)
    requires(is_swappable_v<_Tp> && is_swappable_v<_Err> && is_move_constructible_v<_Tp> &&
             is_move_constructible_v<_Err> &&
             (is_nothrow_move_constructible_v<_Tp> || is_nothrow_move_constructible_v<_Err>))
  {
    auto __swap_val_unex_impl = [](expected& __with_val, expected& __with_err) {
      if constexpr (is_nothrow_move_constructible_v<_Err>) {
        _Err __tmp(std::move(__with_err.__unex()));
        __with_err.__destroy();
        auto __trans = std::__make_exception_guard([&] { __with_err.__construct(unexpect, std::move(__tmp)); });
        __with_err.__construct(in_place, std::move(__with_val.__val()));
        __trans.__complete();
        __with_val.__destroy();
        __with_val.__construct(unexpect, std::move(__tmp));
      } else {
        static_assert(is_nothrow_move_constructible_v<_Tp>,
                      "To provide strong exception guarantee, Tp has to satisfy `is_nothrow_move_constructible_v` so "
                      "that it can be reverted to the previous state in case an exception is thrown during swap.");
        _Tp __tmp(std::move(__with_val.__val()));
        __with_val.__destroy();
        auto __trans = std::__make_exception_guard([&] { __with_val.__construct(in_place, std::move(__tmp)); });
        __with_val.__construct(unexpect, std::move(__with_err.__unex()));
        __trans.__complete();
        __with_err.__destroy();
        __with_err.__construct(in_place, std::move(__tmp));
      }
    };

    if (this->__has_val()) {
      if (__rhs.__has_val()) {
        using std::swap;
        swap(this->__val(), __rhs.__val());
      } else {
        __swap_val_unex_impl(*this, __rhs);
      }
    } else {
      if (__rhs.__has_val()) {
        __swap_val_unex_impl(__rhs, *this);
      } else {
        using std::swap;
        swap(this->__unex(), __rhs.__unex());
      }
    }
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr void swap(expected& __x, expected& __y) noexcept(noexcept(__x.swap(__y)))
    requires requires { __x.swap(__y); }
  {
    __x.swap(__y);
  }

  // [expected.object.obs], observers
  _LIBCPP_HIDE_FROM_ABI constexpr const _Tp* operator->() const noexcept {
    _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(
        this->__has_val(), "expected::operator-> requires the expected to contain a value");
    return std::addressof(this->__val());
  }

  _LIBCPP_HIDE_FROM_ABI constexpr _Tp* operator->() noexcept {
    _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(
        this->__has_val(), "expected::operator-> requires the expected to contain a value");
    return std::addressof(this->__val());
  }

  _LIBCPP_HIDE_FROM_ABI constexpr const _Tp& operator*() const& noexcept {
    _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(
        this->__has_val(), "expected::operator* requires the expected to contain a value");
    return this->__val();
  }

  _LIBCPP_HIDE_FROM_ABI constexpr _Tp& operator*() & noexcept {
    _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(
        this->__has_val(), "expected::operator* requires the expected to contain a value");
    return this->__val();
  }

  _LIBCPP_HIDE_FROM_ABI constexpr const _Tp&& operator*() const&& noexcept {
    _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(
        this->__has_val(), "expected::operator* requires the expected to contain a value");
    return std::move(this->__val());
  }

  _LIBCPP_HIDE_FROM_ABI constexpr _Tp&& operator*() && noexcept {
    _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(
        this->__has_val(), "expected::operator* requires the expected to contain a value");
    return std::move(this->__val());
  }

  _LIBCPP_HIDE_FROM_ABI constexpr explicit operator bool() const noexcept { return this->__has_val(); }

  _LIBCPP_HIDE_FROM_ABI constexpr bool has_value() const noexcept { return this->__has_val(); }

  _LIBCPP_HIDE_FROM_ABI constexpr const _Tp& value() const& {
    static_assert(is_copy_constructible_v<_Err>, "error_type has to be copy constructible");
    if (!this->__has_val()) {
      std::__throw_bad_expected_access<_Err>(std::as_const(error()));
    }
    return this->__val();
  }

  _LIBCPP_HIDE_FROM_ABI constexpr _Tp& value() & {
    static_assert(is_copy_constructible_v<_Err>, "error_type has to be copy constructible");
    if (!this->__has_val()) {
      std::__throw_bad_expected_access<_Err>(std::as_const(error()));
    }
    return this->__val();
  }

  _LIBCPP_HIDE_FROM_ABI constexpr const _Tp&& value() const&& {
    static_assert(is_copy_constructible_v<_Err> && is_constructible_v<_Err, decltype(std::move(error()))>,
                  "error_type has to be both copy constructible and constructible from decltype(std::move(error()))");
    if (!this->__has_val()) {
      std::__throw_bad_expected_access<_Err>(std::move(error()));
    }
    return std::move(this->__val());
  }

  _LIBCPP_HIDE_FROM_ABI constexpr _Tp&& value() && {
    static_assert(is_copy_constructible_v<_Err> && is_constructible_v<_Err, decltype(std::move(error()))>,
                  "error_type has to be both copy constructible and constructible from decltype(std::move(error()))");
    if (!this->__has_val()) {
      std::__throw_bad_expected_access<_Err>(std::move(error()));
    }
    return std::move(this->__val());
  }

  _LIBCPP_HIDE_FROM_ABI constexpr const _Err& error() const& noexcept {
    _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(
        !this->__has_val(), "expected::error requires the expected to contain an error");
    return this->__unex();
  }

  _LIBCPP_HIDE_FROM_ABI constexpr _Err& error() & noexcept {
    _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(
        !this->__has_val(), "expected::error requires the expected to contain an error");
    return this->__unex();
  }

  _LIBCPP_HIDE_FROM_ABI constexpr const _Err&& error() const&& noexcept {
    _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(
        !this->__has_val(), "expected::error requires the expected to contain an error");
    return std::move(this->__unex());
  }

  _LIBCPP_HIDE_FROM_ABI constexpr _Err&& error() && noexcept {
    _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(
        !this->__has_val(), "expected::error requires the expected to contain an error");
    return std::move(this->__unex());
  }

  template <class _Up>
  _LIBCPP_HIDE_FROM_ABI constexpr _Tp value_or(_Up&& __v) const& {
    static_assert(is_copy_constructible_v<_Tp>, "value_type has to be copy constructible");
    static_assert(is_convertible_v<_Up, _Tp>, "argument has to be convertible to value_type");
    return this->__has_val() ? this->__val() : static_cast<_Tp>(std::forward<_Up>(__v));
  }

  template <class _Up>
  _LIBCPP_HIDE_FROM_ABI constexpr _Tp value_or(_Up&& __v) && {
    static_assert(is_move_constructible_v<_Tp>, "value_type has to be move constructible");
    static_assert(is_convertible_v<_Up, _Tp>, "argument has to be convertible to value_type");
    return this->__has_val() ? std::move(this->__val()) : static_cast<_Tp>(std::forward<_Up>(__v));
  }

  template <class _Up = _Err>
  _LIBCPP_HIDE_FROM_ABI constexpr _Err error_or(_Up&& __error) const& {
    static_assert(is_copy_constructible_v<_Err>, "error_type has to be copy constructible");
    static_assert(is_convertible_v<_Up, _Err>, "argument has to be convertible to error_type");
    if (has_value())
      return std::forward<_Up>(__error);
    return error();
  }

  template <class _Up = _Err>
  _LIBCPP_HIDE_FROM_ABI constexpr _Err error_or(_Up&& __error) && {
    static_assert(is_move_constructible_v<_Err>, "error_type has to be move constructible");
    static_assert(is_convertible_v<_Up, _Err>, "argument has to be convertible to error_type");
    if (has_value())
      return std::forward<_Up>(__error);
    return std::move(error());
  }

  // [expected.void.monadic], monadic
  template <class _Func>
    requires is_constructible_v<_Err, _Err&>
  _LIBCPP_HIDE_FROM_ABI constexpr auto and_then(_Func&& __f) & {
    using _Up = remove_cvref_t<invoke_result_t<_Func, _Tp&>>;
    static_assert(__is_std_expected<_Up>::value, "The result of f(**this) must be a specialization of std::expected");
    static_assert(is_same_v<typename _Up::error_type, _Err>,
                  "The result of f(**this) must have the same error_type as this expected");
    if (has_value()) {
      return std::invoke(std::forward<_Func>(__f), this->__val());
    }
    return _Up(unexpect, error());
  }

  template <class _Func>
    requires is_constructible_v<_Err, const _Err&>
  _LIBCPP_HIDE_FROM_ABI constexpr auto and_then(_Func&& __f) const& {
    using _Up = remove_cvref_t<invoke_result_t<_Func, const _Tp&>>;
    static_assert(__is_std_expected<_Up>::value, "The result of f(**this) must be a specialization of std::expected");
    static_assert(is_same_v<typename _Up::error_type, _Err>,
                  "The result of f(**this) must have the same error_type as this expected");
    if (has_value()) {
      return std::invoke(std::forward<_Func>(__f), this->__val());
    }
    return _Up(unexpect, error());
  }

  template <class _Func>
    requires is_constructible_v<_Err, _Err&&>
  _LIBCPP_HIDE_FROM_ABI constexpr auto and_then(_Func&& __f) && {
    using _Up = remove_cvref_t<invoke_result_t<_Func, _Tp&&>>;
    static_assert(
        __is_std_expected<_Up>::value, "The result of f(std::move(**this)) must be a specialization of std::expected");
    static_assert(is_same_v<typename _Up::error_type, _Err>,
                  "The result of f(std::move(**this)) must have the same error_type as this expected");
    if (has_value()) {
      return std::invoke(std::forward<_Func>(__f), std::move(this->__val()));
    }
    return _Up(unexpect, std::move(error()));
  }

  template <class _Func>
    requires is_constructible_v<_Err, const _Err&&>
  _LIBCPP_HIDE_FROM_ABI constexpr auto and_then(_Func&& __f) const&& {
    using _Up = remove_cvref_t<invoke_result_t<_Func, const _Tp&&>>;
    static_assert(
        __is_std_expected<_Up>::value, "The result of f(std::move(**this)) must be a specialization of std::expected");
    static_assert(is_same_v<typename _Up::error_type, _Err>,
                  "The result of f(std::move(**this)) must have the same error_type as this expected");
    if (has_value()) {
      return std::invoke(std::forward<_Func>(__f), std::move(this->__val()));
    }
    return _Up(unexpect, std::move(error()));
  }

  template <class _Func>
    requires is_constructible_v<_Tp, _Tp&>
  _LIBCPP_HIDE_FROM_ABI constexpr auto or_else(_Func&& __f) & {
    using _Gp = remove_cvref_t<invoke_result_t<_Func, _Err&>>;
    static_assert(__is_std_expected<_Gp>::value, "The result of f(error()) must be a specialization of std::expected");
    static_assert(is_same_v<typename _Gp::value_type, _Tp>,
                  "The result of f(error()) must have the same value_type as this expected");
    if (has_value()) {
      return _Gp(in_place, this->__val());
    }
    return std::invoke(std::forward<_Func>(__f), error());
  }

  template <class _Func>
    requires is_constructible_v<_Tp, const _Tp&>
  _LIBCPP_HIDE_FROM_ABI constexpr auto or_else(_Func&& __f) const& {
    using _Gp = remove_cvref_t<invoke_result_t<_Func, const _Err&>>;
    static_assert(__is_std_expected<_Gp>::value, "The result of f(error()) must be a specialization of std::expected");
    static_assert(is_same_v<typename _Gp::value_type, _Tp>,
                  "The result of f(error()) must have the same value_type as this expected");
    if (has_value()) {
      return _Gp(in_place, this->__val());
    }
    return std::invoke(std::forward<_Func>(__f), error());
  }

  template <class _Func>
    requires is_constructible_v<_Tp, _Tp&&>
  _LIBCPP_HIDE_FROM_ABI constexpr auto or_else(_Func&& __f) && {
    using _Gp = remove_cvref_t<invoke_result_t<_Func, _Err&&>>;
    static_assert(
        __is_std_expected<_Gp>::value, "The result of f(std::move(error())) must be a specialization of std::expected");
    static_assert(is_same_v<typename _Gp::value_type, _Tp>,
                  "The result of f(std::move(error())) must have the same value_type as this expected");
    if (has_value()) {
      return _Gp(in_place, std::move(this->__val()));
    }
    return std::invoke(std::forward<_Func>(__f), std::move(error()));
  }

  template <class _Func>
    requires is_constructible_v<_Tp, const _Tp&&>
  _LIBCPP_HIDE_FROM_ABI constexpr auto or_else(_Func&& __f) const&& {
    using _Gp = remove_cvref_t<invoke_result_t<_Func, const _Err&&>>;
    static_assert(
        __is_std_expected<_Gp>::value, "The result of f(std::move(error())) must be a specialization of std::expected");
    static_assert(is_same_v<typename _Gp::value_type, _Tp>,
                  "The result of f(std::move(error())) must have the same value_type as this expected");
    if (has_value()) {
      return _Gp(in_place, std::move(this->__val()));
    }
    return std::invoke(std::forward<_Func>(__f), std::move(error()));
  }

  template <class _Func>
    requires is_constructible_v<_Err, _Err&>
  _LIBCPP_HIDE_FROM_ABI constexpr auto transform(_Func&& __f) & {
    using _Up = remove_cv_t<invoke_result_t<_Func, _Tp&>>;
    if (!has_value()) {
      return expected<_Up, _Err>(unexpect, error());
    }
    if constexpr (!is_void_v<_Up>) {
      return expected<_Up, _Err>(
          __expected_construct_in_place_from_invoke_tag{}, std::forward<_Func>(__f), this->__val());
    } else {
      std::invoke(std::forward<_Func>(__f), this->__val());
      return expected<_Up, _Err>();
    }
  }

  template <class _Func>
    requires is_constructible_v<_Err, const _Err&>
  _LIBCPP_HIDE_FROM_ABI constexpr auto transform(_Func&& __f) const& {
    using _Up = remove_cv_t<invoke_result_t<_Func, const _Tp&>>;
    if (!has_value()) {
      return expected<_Up, _Err>(unexpect, error());
    }
    if constexpr (!is_void_v<_Up>) {
      return expected<_Up, _Err>(
          __expected_construct_in_place_from_invoke_tag{}, std::forward<_Func>(__f), this->__val());
    } else {
      std::invoke(std::forward<_Func>(__f), this->__val());
      return expected<_Up, _Err>();
    }
  }

  template <class _Func>
    requires is_constructible_v<_Err, _Err&&>
  _LIBCPP_HIDE_FROM_ABI constexpr auto transform(_Func&& __f) && {
    using _Up = remove_cv_t<invoke_result_t<_Func, _Tp&&>>;
    if (!has_value()) {
      return expected<_Up, _Err>(unexpect, std::move(error()));
    }
    if constexpr (!is_void_v<_Up>) {
      return expected<_Up, _Err>(
          __expected_construct_in_place_from_invoke_tag{}, std::forward<_Func>(__f), std::move(this->__val()));
    } else {
      std::invoke(std::forward<_Func>(__f), std::move(this->__val()));
      return expected<_Up, _Err>();
    }
  }

  template <class _Func>
    requires is_constructible_v<_Err, const _Err&&>
  _LIBCPP_HIDE_FROM_ABI constexpr auto transform(_Func&& __f) const&& {
    using _Up = remove_cv_t<invoke_result_t<_Func, const _Tp&&>>;
    if (!has_value()) {
      return expected<_Up, _Err>(unexpect, std::move(error()));
    }
    if constexpr (!is_void_v<_Up>) {
      return expected<_Up, _Err>(
          __expected_construct_in_place_from_invoke_tag{}, std::forward<_Func>(__f), std::move(this->__val()));
    } else {
      std::invoke(std::forward<_Func>(__f), std::move(this->__val()));
      return expected<_Up, _Err>();
    }
  }

  template <class _Func>
    requires is_constructible_v<_Tp, _Tp&>
  _LIBCPP_HIDE_FROM_ABI constexpr auto transform_error(_Func&& __f) & {
    using _Gp = remove_cv_t<invoke_result_t<_Func, _Err&>>;
    static_assert(__valid_std_unexpected<_Gp>::value,
                  "The result of f(error()) must be a valid template argument for unexpected");
    if (has_value()) {
      return expected<_Tp, _Gp>(in_place, this->__val());
    }
    return expected<_Tp, _Gp>(__expected_construct_unexpected_from_invoke_tag{}, std::forward<_Func>(__f), error());
  }

  template <class _Func>
    requires is_constructible_v<_Tp, const _Tp&>
  _LIBCPP_HIDE_FROM_ABI constexpr auto transform_error(_Func&& __f) const& {
    using _Gp = remove_cv_t<invoke_result_t<_Func, const _Err&>>;
    static_assert(__valid_std_unexpected<_Gp>::value,
                  "The result of f(error()) must be a valid template argument for unexpected");
    if (has_value()) {
      return expected<_Tp, _Gp>(in_place, this->__val());
    }
    return expected<_Tp, _Gp>(__expected_construct_unexpected_from_invoke_tag{}, std::forward<_Func>(__f), error());
  }

  template <class _Func>
    requires is_constructible_v<_Tp, _Tp&&>
  _LIBCPP_HIDE_FROM_ABI constexpr auto transform_error(_Func&& __f) && {
    using _Gp = remove_cv_t<invoke_result_t<_Func, _Err&&>>;
    static_assert(__valid_std_unexpected<_Gp>::value,
                  "The result of f(std::move(error())) must be a valid template argument for unexpected");
    if (has_value()) {
      return expected<_Tp, _Gp>(in_place, std::move(this->__val()));
    }
    return expected<_Tp, _Gp>(
        __expected_construct_unexpected_from_invoke_tag{}, std::forward<_Func>(__f), std::move(error()));
  }

  template <class _Func>
    requires is_constructible_v<_Tp, const _Tp&&>
  _LIBCPP_HIDE_FROM_ABI constexpr auto transform_error(_Func&& __f) const&& {
    using _Gp = remove_cv_t<invoke_result_t<_Func, const _Err&&>>;
    static_assert(__valid_std_unexpected<_Gp>::value,
                  "The result of f(std::move(error())) must be a valid template argument for unexpected");
    if (has_value()) {
      return expected<_Tp, _Gp>(in_place, std::move(this->__val()));
    }
    return expected<_Tp, _Gp>(
        __expected_construct_unexpected_from_invoke_tag{}, std::forward<_Func>(__f), std::move(error()));
  }

  // [expected.object.eq], equality operators
  template <class _T2, class _E2>
    requires(!is_void_v<_T2>)
  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator==(const expected& __x, const expected<_T2, _E2>& __y) {
    if (__x.__has_val() != __y.__has_val()) {
      return false;
    } else {
      if (__x.__has_val()) {
        return __x.__val() == __y.__val();
      } else {
        return __x.__unex() == __y.__unex();
      }
    }
  }

  template <class _T2>
  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator==(const expected& __x, const _T2& __v) {
    return __x.__has_val() && static_cast<bool>(__x.__val() == __v);
  }

  template <class _E2>
  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator==(const expected& __x, const unexpected<_E2>& __e) {
    return !__x.__has_val() && static_cast<bool>(__x.__unex() == __e.error());
  }
};

template <class _Err>
class __expected_void_base {
  struct __empty_t {};
  // use named union because [[no_unique_address]] cannot be applied to an unnamed union,
  // also guaranteed elision into a potentially-overlapping subobject is unsettled (and
  // it's not clear that it's implementable, given that the function is allowed to clobber
  // the tail padding) - see https://github.com/itanium-cxx-abi/cxx-abi/issues/107.
  union __union_t {
    _LIBCPP_HIDE_FROM_ABI constexpr __union_t(const __union_t&) = delete;
    _LIBCPP_HIDE_FROM_ABI constexpr __union_t(const __union_t&)
      requires(is_copy_constructible_v<_Err> && is_trivially_copy_constructible_v<_Err>)
    = default;
    _LIBCPP_HIDE_FROM_ABI constexpr __union_t(__union_t&&) = delete;
    _LIBCPP_HIDE_FROM_ABI constexpr __union_t(__union_t&&)
      requires(is_move_constructible_v<_Err> && is_trivially_move_constructible_v<_Err>)
    = default;
    _LIBCPP_HIDE_FROM_ABI constexpr __union_t& operator=(const __union_t&) = delete;
    _LIBCPP_HIDE_FROM_ABI constexpr __union_t& operator=(__union_t&&)      = delete;

    _LIBCPP_HIDE_FROM_ABI constexpr explicit __union_t(in_place_t) : __empty_() {}

    template <class... _Args>
    _LIBCPP_HIDE_FROM_ABI constexpr explicit __union_t(unexpect_t, _Args&&... __args)
        : __unex_(std::forward<_Args>(__args)...) {}

    template <class _Func, class... _Args>
    _LIBCPP_HIDE_FROM_ABI constexpr explicit __union_t(
        __expected_construct_unexpected_from_invoke_tag, _Func&& __f, _Args&&... __args)
        : __unex_(std::invoke(std::forward<_Func>(__f), std::forward<_Args>(__args)...)) {}

    _LIBCPP_HIDE_FROM_ABI constexpr ~__union_t()
      requires(is_trivially_destructible_v<_Err>)
    = default;

    // __repr's destructor handles this
    _LIBCPP_HIDE_FROM_ABI constexpr ~__union_t()
      requires(!is_trivially_destructible_v<_Err>)
    {}

    _LIBCPP_NO_UNIQUE_ADDRESS __empty_t __empty_;
    _LIBCPP_NO_UNIQUE_ADDRESS _Err __unex_;
  };

  static constexpr bool __put_flag_in_tail                    = __fits_in_tail_padding<__union_t, bool>;
  static constexpr bool __allow_reusing_expected_tail_padding = !__put_flag_in_tail;

  struct __repr {
    _LIBCPP_HIDE_FROM_ABI constexpr explicit __repr() = delete;

    template <class... _Args>
    _LIBCPP_HIDE_FROM_ABI constexpr explicit __repr(in_place_t __tag) : __union_(in_place, __tag), __has_val_(true) {}

    template <class... _Args>
    _LIBCPP_HIDE_FROM_ABI constexpr explicit __repr(unexpect_t __tag, _Args&&... __args)
        : __union_(in_place, __tag, std::forward<_Args>(__args)...), __has_val_(false) {}

    template <class... _Args>
    _LIBCPP_HIDE_FROM_ABI constexpr explicit __repr(std::__expected_construct_unexpected_from_invoke_tag __tag,
                                                    _Args&&... __args)
        : __union_(in_place, __tag, std::forward<_Args>(__args)...), __has_val_(false) {}

    template <class _OtherUnion>
    _LIBCPP_HIDE_FROM_ABI constexpr explicit __repr(bool __has_val, _OtherUnion&& __other)
      requires(__allow_reusing_expected_tail_padding)
        : __union_(__conditional_no_unique_address_invoke_tag{},
                   [&] { return __make_union(__has_val, std::forward<_OtherUnion>(__other)); }),
          __has_val_(__has_val) {}

    _LIBCPP_HIDE_FROM_ABI constexpr __repr(const __repr&) = delete;
    _LIBCPP_HIDE_FROM_ABI constexpr __repr(const __repr&)
      requires(is_copy_constructible_v<_Err> && is_trivially_copy_constructible_v<_Err>)
    = default;
    _LIBCPP_HIDE_FROM_ABI constexpr __repr(__repr&&) = delete;
    _LIBCPP_HIDE_FROM_ABI constexpr __repr(__repr&&)
      requires(is_move_constructible_v<_Err> && is_trivially_move_constructible_v<_Err>)
    = default;

    _LIBCPP_HIDE_FROM_ABI constexpr __repr& operator=(const __repr&) = delete;
    _LIBCPP_HIDE_FROM_ABI constexpr __repr& operator=(__repr&&)      = delete;

    _LIBCPP_HIDE_FROM_ABI constexpr ~__repr()
      requires(is_trivially_destructible_v<_Err>)
    = default;

    _LIBCPP_HIDE_FROM_ABI constexpr ~__repr()
      requires(!is_trivially_destructible_v<_Err>)
    {
      __destroy_union_member();
    }

    _LIBCPP_HIDE_FROM_ABI constexpr void __destroy_union()
      requires(__allow_reusing_expected_tail_padding && is_trivially_destructible_v<_Err>)
    {
      std::destroy_at(&__union_.__v);
    }

    _LIBCPP_HIDE_FROM_ABI constexpr void __destroy_union()
      requires(__allow_reusing_expected_tail_padding && !is_trivially_destructible_v<_Err>)
    {
      __destroy_union_member();
      std::destroy_at(&__union_.__v);
    }

    _LIBCPP_HIDE_FROM_ABI constexpr void __construct_union(in_place_t)
      requires(__allow_reusing_expected_tail_padding)
    {
      std::construct_at(&__union_.__v, in_place);
      __has_val_ = true;
    }

    template <class... _Args>
    _LIBCPP_HIDE_FROM_ABI constexpr void __construct_union(unexpect_t, _Args&&... __args)
      requires(__allow_reusing_expected_tail_padding)
    {
      std::construct_at(&__union_.__v, unexpect, std::forward<_Args>(__args)...);
      __has_val_ = false;
    }

  private:
    template <class>
    friend class __expected_void_base;

    _LIBCPP_HIDE_FROM_ABI constexpr void __destroy_union_member()
      requires(!is_trivially_destructible_v<_Err>)
    {
      if (!__has_val_)
        std::destroy_at(std::addressof(__union_.__v.__unex_));
    }

    template <class _OtherUnion>
    _LIBCPP_HIDE_FROM_ABI static constexpr __union_t __make_union(bool __has_val, _OtherUnion&& __other)
      requires(__allow_reusing_expected_tail_padding)
    {
      if (__has_val)
        return __union_t(in_place);
      else
        return __union_t(unexpect, std::forward<_OtherUnion>(__other).__unex_);
    }

    _LIBCPP_NO_UNIQUE_ADDRESS __conditional_no_unique_address<__put_flag_in_tail, __union_t> __union_;
    _LIBCPP_NO_UNIQUE_ADDRESS bool __has_val_;
  };

  template <class _OtherUnion>
  _LIBCPP_HIDE_FROM_ABI static constexpr __repr __make_repr(bool __has_val, _OtherUnion&& __other)
    requires(__put_flag_in_tail)
  {
    if (__has_val)
      return __repr(in_place);
    else
      return __repr(unexpect, std::forward<_OtherUnion>(__other).__unex_);
  }

protected:
  template <class... _Args>
  _LIBCPP_HIDE_FROM_ABI constexpr explicit __expected_void_base(_Args&&... __args)
      : __repr_(in_place, std::forward<_Args>(__args)...) {}

  template <class _OtherUnion>
  _LIBCPP_HIDE_FROM_ABI constexpr explicit __expected_void_base(bool __has_val, _OtherUnion&& __other)
    requires(__put_flag_in_tail)
      : __repr_(__conditional_no_unique_address_invoke_tag{},
                [&] { return __make_repr(__has_val, std::forward<_OtherUnion>(__other)); }) {}

  _LIBCPP_HIDE_FROM_ABI constexpr void __destroy() {
    if constexpr (__put_flag_in_tail)
      std::destroy_at(&__repr_.__v);
    else
      __repr_.__v.__destroy_union();
  }

  template <class _Tag, class... _Args>
  _LIBCPP_HIDE_FROM_ABI constexpr void __construct(_Tag __tag, _Args&&... __args) {
    if constexpr (__put_flag_in_tail)
      std::construct_at(&__repr_.__v, __tag, std::forward<_Args>(__args)...);
    else
      __repr_.__v.__construct_union(__tag, std::forward<_Args>(__args)...);
  }

  _LIBCPP_HIDE_FROM_ABI constexpr bool __has_val() const { return __repr_.__v.__has_val_; }
  _LIBCPP_HIDE_FROM_ABI constexpr __union_t& __union() { return __repr_.__v.__union_.__v; }
  _LIBCPP_HIDE_FROM_ABI constexpr const __union_t& __union() const { return __repr_.__v.__union_.__v; }
  _LIBCPP_HIDE_FROM_ABI constexpr _Err& __unex() { return __repr_.__v.__union_.__v.__unex_; }
  _LIBCPP_HIDE_FROM_ABI constexpr const _Err& __unex() const { return __repr_.__v.__union_.__v.__unex_; }

private:
  _LIBCPP_NO_UNIQUE_ADDRESS __conditional_no_unique_address<__allow_reusing_expected_tail_padding, __repr> __repr_;
};

template <class _Tp, class _Err>
  requires is_void_v<_Tp>
class expected<_Tp, _Err> : private __expected_void_base<_Err> {
  static_assert(__valid_std_unexpected<_Err>::value,
                "[expected.void.general] A program that instantiates expected<T, E> with a E that is not a "
                "valid argument for unexpected<E> is ill-formed");

  template <class, class>
  friend class expected;

  template <class _Up, class _OtherErr, class _OtherErrQual>
  using __can_convert =
      _And< is_void<_Up>,
            is_constructible<_Err, _OtherErrQual>,
            _Not<is_constructible<unexpected<_Err>, expected<_Up, _OtherErr>&>>,
            _Not<is_constructible<unexpected<_Err>, expected<_Up, _OtherErr>>>,
            _Not<is_constructible<unexpected<_Err>, const expected<_Up, _OtherErr>&>>,
            _Not<is_constructible<unexpected<_Err>, const expected<_Up, _OtherErr>>>>;

  using __base = __expected_void_base<_Err>;

public:
  using value_type      = _Tp;
  using error_type      = _Err;
  using unexpected_type = unexpected<_Err>;

  template <class _Up>
  using rebind = expected<_Up, error_type>;

  // [expected.void.ctor], constructors
  _LIBCPP_HIDE_FROM_ABI constexpr expected() noexcept : __base(in_place) {}

  _LIBCPP_HIDE_FROM_ABI constexpr expected(const expected&) = delete;

  _LIBCPP_HIDE_FROM_ABI constexpr expected(const expected&)
    requires(is_copy_constructible_v<_Err> && is_trivially_copy_constructible_v<_Err>)
  = default;

  _LIBCPP_HIDE_FROM_ABI constexpr expected(const expected& __rhs) noexcept(
      is_nothrow_copy_constructible_v<_Err>) // strengthened
    requires(is_copy_constructible_v<_Err> && !is_trivially_copy_constructible_v<_Err>)
      : __base(__rhs.__has_val(), __rhs.__union()) {}

  _LIBCPP_HIDE_FROM_ABI constexpr expected(expected&&)
    requires(is_move_constructible_v<_Err> && is_trivially_move_constructible_v<_Err>)
  = default;

  _LIBCPP_HIDE_FROM_ABI constexpr expected(expected&& __rhs) noexcept(is_nothrow_move_constructible_v<_Err>)
    requires(is_move_constructible_v<_Err> && !is_trivially_move_constructible_v<_Err>)
      : __base(__rhs.__has_val(), std::move(__rhs.__union())) {}

  template <class _Up, class _OtherErr>
    requires __can_convert<_Up, _OtherErr, const _OtherErr&>::value
  _LIBCPP_HIDE_FROM_ABI constexpr explicit(!is_convertible_v<const _OtherErr&, _Err>)
      expected(const expected<_Up, _OtherErr>& __rhs) noexcept(
          is_nothrow_constructible_v<_Err, const _OtherErr&>) // strengthened
      : __base(__rhs.__has_val(), __rhs.__union()) {}

  template <class _Up, class _OtherErr>
    requires __can_convert<_Up, _OtherErr, _OtherErr>::value
  _LIBCPP_HIDE_FROM_ABI constexpr explicit(!is_convertible_v<_OtherErr, _Err>)
      expected(expected<_Up, _OtherErr>&& __rhs) noexcept(is_nothrow_constructible_v<_Err, _OtherErr>) // strengthened
      : __base(__rhs.__has_val(), std::move(__rhs.__union())) {}

  template <class _OtherErr>
    requires is_constructible_v<_Err, const _OtherErr&>
  _LIBCPP_HIDE_FROM_ABI constexpr explicit(!is_convertible_v<const _OtherErr&, _Err>) expected(
      const unexpected<_OtherErr>& __unex) noexcept(is_nothrow_constructible_v<_Err, const _OtherErr&>) // strengthened
      : __base(unexpect, __unex.error()) {}

  template <class _OtherErr>
    requires is_constructible_v<_Err, _OtherErr>
  _LIBCPP_HIDE_FROM_ABI constexpr explicit(!is_convertible_v<_OtherErr, _Err>)
      expected(unexpected<_OtherErr>&& __unex) noexcept(is_nothrow_constructible_v<_Err, _OtherErr>) // strengthened
      : __base(unexpect, std::move(__unex.error())) {}

  _LIBCPP_HIDE_FROM_ABI constexpr explicit expected(in_place_t) noexcept : __base(in_place) {}

  template <class... _Args>
    requires is_constructible_v<_Err, _Args...>
  _LIBCPP_HIDE_FROM_ABI constexpr explicit expected(unexpect_t, _Args&&... __args) noexcept(
      is_nothrow_constructible_v<_Err, _Args...>) // strengthened
      : __base(unexpect, std::forward<_Args>(__args)...) {}

  template <class _Up, class... _Args>
    requires is_constructible_v< _Err, initializer_list<_Up>&, _Args... >
  _LIBCPP_HIDE_FROM_ABI constexpr explicit expected(unexpect_t, initializer_list<_Up> __il, _Args&&... __args) noexcept(
      is_nothrow_constructible_v<_Err, initializer_list<_Up>&, _Args...>) // strengthened
      : __base(unexpect, __il, std::forward<_Args>(__args)...) {}

private:
  template <class _Func, class... _Args>
  _LIBCPP_HIDE_FROM_ABI constexpr explicit expected(
      __expected_construct_unexpected_from_invoke_tag __tag, _Func&& __f, _Args&&... __args)
      : __base(__tag, std::forward<_Func>(__f), std::forward<_Args>(__args)...) {}

public:
  // [expected.void.dtor], destructor

  _LIBCPP_HIDE_FROM_ABI constexpr ~expected() = default;

private:
  template <class... _Args>
  _LIBCPP_HIDE_FROM_ABI constexpr void __reinit_expected(unexpect_t, _Args&&... __args) {
    _LIBCPP_ASSERT_INTERNAL(this->__has_val(), "__reinit_expected(unexpect_t, ...) needs value to be set");

    this->__destroy();
    auto __trans = std::__make_exception_guard([&] { this->__construct(in_place); });
    this->__construct(unexpect, std::forward<_Args>(__args)...);
    __trans.__complete();
  }

  _LIBCPP_HIDE_FROM_ABI constexpr void __reinit_expected(in_place_t) {
    _LIBCPP_ASSERT_INTERNAL(!this->__has_val(), "__reinit_expected(in_place_t, ...) needs value to be unset");

    this->__destroy();
    this->__construct(in_place);
  }

public:
  // [expected.void.assign], assignment
  _LIBCPP_HIDE_FROM_ABI constexpr expected& operator=(const expected&) = delete;

  _LIBCPP_HIDE_FROM_ABI constexpr expected& operator=(const expected& __rhs) noexcept(
      is_nothrow_copy_assignable_v<_Err> && is_nothrow_copy_constructible_v<_Err>) // strengthened
    requires(is_copy_assignable_v<_Err> && is_copy_constructible_v<_Err>)
  {
    if (this->__has_val()) {
      if (!__rhs.__has_val()) {
        __reinit_expected(unexpect, __rhs.__unex());
      }
    } else {
      if (__rhs.__has_val()) {
        __reinit_expected(in_place);
      } else {
        this->__unex() = __rhs.__unex();
      }
    }
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr expected& operator=(expected&&) = delete;

  _LIBCPP_HIDE_FROM_ABI constexpr expected&
  operator=(expected&& __rhs) noexcept(is_nothrow_move_assignable_v<_Err> && is_nothrow_move_constructible_v<_Err>)
    requires(is_move_assignable_v<_Err> && is_move_constructible_v<_Err>)
  {
    if (this->__has_val()) {
      if (!__rhs.__has_val()) {
        __reinit_expected(unexpect, std::move(__rhs.__unex()));
      }
    } else {
      if (__rhs.__has_val()) {
        __reinit_expected(in_place);
      } else {
        this->__unex() = std::move(__rhs.__unex());
      }
    }
    return *this;
  }

  template <class _OtherErr>
    requires(is_constructible_v<_Err, const _OtherErr&> && is_assignable_v<_Err&, const _OtherErr&>)
  _LIBCPP_HIDE_FROM_ABI constexpr expected& operator=(const unexpected<_OtherErr>& __un) {
    if (this->__has_val()) {
      __reinit_expected(unexpect, __un.error());
    } else {
      this->__unex() = __un.error();
    }
    return *this;
  }

  template <class _OtherErr>
    requires(is_constructible_v<_Err, _OtherErr> && is_assignable_v<_Err&, _OtherErr>)
  _LIBCPP_HIDE_FROM_ABI constexpr expected& operator=(unexpected<_OtherErr>&& __un) {
    if (this->__has_val()) {
      __reinit_expected(unexpect, std::move(__un.error()));
    } else {
      this->__unex() = std::move(__un.error());
    }
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr void emplace() noexcept {
    if (!this->__has_val()) {
      __reinit_expected(in_place);
    }
  }

  // [expected.void.swap], swap
  _LIBCPP_HIDE_FROM_ABI constexpr void
  swap(expected& __rhs) noexcept(is_nothrow_move_constructible_v<_Err> && is_nothrow_swappable_v<_Err>)
    requires(is_swappable_v<_Err> && is_move_constructible_v<_Err>)
  {
    auto __swap_val_unex_impl = [](expected& __with_val, expected& __with_err) {
      // May throw, but will re-engage `__with_val` in that case.
      __with_val.__reinit_expected(unexpect, std::move(__with_err.__unex()));
      // Will not throw.
      __with_err.__reinit_expected(in_place);
    };

    if (this->__has_val()) {
      if (!__rhs.__has_val()) {
        __swap_val_unex_impl(*this, __rhs);
      }
    } else {
      if (__rhs.__has_val()) {
        __swap_val_unex_impl(__rhs, *this);
      } else {
        using std::swap;
        swap(this->__unex(), __rhs.__unex());
      }
    }
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr void swap(expected& __x, expected& __y) noexcept(noexcept(__x.swap(__y)))
    requires requires { __x.swap(__y); }
  {
    __x.swap(__y);
  }

  // [expected.void.obs], observers
  _LIBCPP_HIDE_FROM_ABI constexpr explicit operator bool() const noexcept { return this->__has_val(); }

  _LIBCPP_HIDE_FROM_ABI constexpr bool has_value() const noexcept { return this->__has_val(); }

  _LIBCPP_HIDE_FROM_ABI constexpr void operator*() const noexcept {
    _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(
        this->__has_val(), "expected::operator* requires the expected to contain a value");
  }

  _LIBCPP_HIDE_FROM_ABI constexpr void value() const& {
    static_assert(is_copy_constructible_v<_Err>);
    if (!this->__has_val()) {
      std::__throw_bad_expected_access<_Err>(this->__unex());
    }
  }

  _LIBCPP_HIDE_FROM_ABI constexpr void value() && {
    static_assert(is_copy_constructible_v<_Err> && is_move_constructible_v<_Err>);
    if (!this->__has_val()) {
      std::__throw_bad_expected_access<_Err>(std::move(this->__unex()));
    }
  }

  _LIBCPP_HIDE_FROM_ABI constexpr const _Err& error() const& noexcept {
    _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(
        !this->__has_val(), "expected::error requires the expected to contain an error");
    return this->__unex();
  }

  _LIBCPP_HIDE_FROM_ABI constexpr _Err& error() & noexcept {
    _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(
        !this->__has_val(), "expected::error requires the expected to contain an error");
    return this->__unex();
  }

  _LIBCPP_HIDE_FROM_ABI constexpr const _Err&& error() const&& noexcept {
    _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(
        !this->__has_val(), "expected::error requires the expected to contain an error");
    return std::move(this->__unex());
  }

  _LIBCPP_HIDE_FROM_ABI constexpr _Err&& error() && noexcept {
    _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(
        !this->__has_val(), "expected::error requires the expected to contain an error");
    return std::move(this->__unex());
  }

  template <class _Up = _Err>
  _LIBCPP_HIDE_FROM_ABI constexpr _Err error_or(_Up&& __error) const& {
    static_assert(is_copy_constructible_v<_Err>, "error_type has to be copy constructible");
    static_assert(is_convertible_v<_Up, _Err>, "argument has to be convertible to error_type");
    if (has_value()) {
      return std::forward<_Up>(__error);
    }
    return error();
  }

  template <class _Up = _Err>
  _LIBCPP_HIDE_FROM_ABI constexpr _Err error_or(_Up&& __error) && {
    static_assert(is_move_constructible_v<_Err>, "error_type has to be move constructible");
    static_assert(is_convertible_v<_Up, _Err>, "argument has to be convertible to error_type");
    if (has_value()) {
      return std::forward<_Up>(__error);
    }
    return std::move(error());
  }

  // [expected.void.monadic], monadic
  template <class _Func>
    requires is_constructible_v<_Err, _Err&>
  _LIBCPP_HIDE_FROM_ABI constexpr auto and_then(_Func&& __f) & {
    using _Up = remove_cvref_t<invoke_result_t<_Func>>;
    static_assert(__is_std_expected<_Up>::value, "The result of f() must be a specialization of std::expected");
    static_assert(
        is_same_v<typename _Up::error_type, _Err>, "The result of f() must have the same error_type as this expected");
    if (has_value()) {
      return std::invoke(std::forward<_Func>(__f));
    }
    return _Up(unexpect, error());
  }

  template <class _Func>
    requires is_constructible_v<_Err, const _Err&>
  _LIBCPP_HIDE_FROM_ABI constexpr auto and_then(_Func&& __f) const& {
    using _Up = remove_cvref_t<invoke_result_t<_Func>>;
    static_assert(__is_std_expected<_Up>::value, "The result of f() must be a specialization of std::expected");
    static_assert(
        is_same_v<typename _Up::error_type, _Err>, "The result of f() must have the same error_type as this expected");
    if (has_value()) {
      return std::invoke(std::forward<_Func>(__f));
    }
    return _Up(unexpect, error());
  }

  template <class _Func>
    requires is_constructible_v<_Err, _Err&&>
  _LIBCPP_HIDE_FROM_ABI constexpr auto and_then(_Func&& __f) && {
    using _Up = remove_cvref_t<invoke_result_t<_Func>>;
    static_assert(__is_std_expected<_Up>::value, "The result of f() must be a specialization of std::expected");
    static_assert(
        is_same_v<typename _Up::error_type, _Err>, "The result of f() must have the same error_type as this expected");
    if (has_value()) {
      return std::invoke(std::forward<_Func>(__f));
    }
    return _Up(unexpect, std::move(error()));
  }

  template <class _Func>
    requires is_constructible_v<_Err, const _Err&&>
  _LIBCPP_HIDE_FROM_ABI constexpr auto and_then(_Func&& __f) const&& {
    using _Up = remove_cvref_t<invoke_result_t<_Func>>;
    static_assert(__is_std_expected<_Up>::value, "The result of f() must be a specialization of std::expected");
    static_assert(
        is_same_v<typename _Up::error_type, _Err>, "The result of f() must have the same error_type as this expected");
    if (has_value()) {
      return std::invoke(std::forward<_Func>(__f));
    }
    return _Up(unexpect, std::move(error()));
  }

  template <class _Func>
  _LIBCPP_HIDE_FROM_ABI constexpr auto or_else(_Func&& __f) & {
    using _Gp = remove_cvref_t<invoke_result_t<_Func, _Err&>>;
    static_assert(__is_std_expected<_Gp>::value, "The result of f(error()) must be a specialization of std::expected");
    static_assert(is_same_v<typename _Gp::value_type, _Tp>,
                  "The result of f(error()) must have the same value_type as this expected");
    if (has_value()) {
      return _Gp();
    }
    return std::invoke(std::forward<_Func>(__f), error());
  }

  template <class _Func>
  _LIBCPP_HIDE_FROM_ABI constexpr auto or_else(_Func&& __f) const& {
    using _Gp = remove_cvref_t<invoke_result_t<_Func, const _Err&>>;
    static_assert(__is_std_expected<_Gp>::value, "The result of f(error()) must be a specialization of std::expected");
    static_assert(is_same_v<typename _Gp::value_type, _Tp>,
                  "The result of f(error()) must have the same value_type as this expected");
    if (has_value()) {
      return _Gp();
    }
    return std::invoke(std::forward<_Func>(__f), error());
  }

  template <class _Func>
  _LIBCPP_HIDE_FROM_ABI constexpr auto or_else(_Func&& __f) && {
    using _Gp = remove_cvref_t<invoke_result_t<_Func, _Err&&>>;
    static_assert(
        __is_std_expected<_Gp>::value, "The result of f(std::move(error())) must be a specialization of std::expected");
    static_assert(is_same_v<typename _Gp::value_type, _Tp>,
                  "The result of f(std::move(error())) must have the same value_type as this expected");
    if (has_value()) {
      return _Gp();
    }
    return std::invoke(std::forward<_Func>(__f), std::move(error()));
  }

  template <class _Func>
  _LIBCPP_HIDE_FROM_ABI constexpr auto or_else(_Func&& __f) const&& {
    using _Gp = remove_cvref_t<invoke_result_t<_Func, const _Err&&>>;
    static_assert(
        __is_std_expected<_Gp>::value, "The result of f(std::move(error())) must be a specialization of std::expected");
    static_assert(is_same_v<typename _Gp::value_type, _Tp>,
                  "The result of f(std::move(error())) must have the same value_type as this expected");
    if (has_value()) {
      return _Gp();
    }
    return std::invoke(std::forward<_Func>(__f), std::move(error()));
  }

  template <class _Func>
    requires is_constructible_v<_Err, _Err&>
  _LIBCPP_HIDE_FROM_ABI constexpr auto transform(_Func&& __f) & {
    using _Up = remove_cv_t<invoke_result_t<_Func>>;
    if (!has_value()) {
      return expected<_Up, _Err>(unexpect, error());
    }
    if constexpr (!is_void_v<_Up>) {
      return expected<_Up, _Err>(__expected_construct_in_place_from_invoke_tag{}, std::forward<_Func>(__f));
    } else {
      std::invoke(std::forward<_Func>(__f));
      return expected<_Up, _Err>();
    }
  }

  template <class _Func>
    requires is_constructible_v<_Err, const _Err&>
  _LIBCPP_HIDE_FROM_ABI constexpr auto transform(_Func&& __f) const& {
    using _Up = remove_cv_t<invoke_result_t<_Func>>;
    if (!has_value()) {
      return expected<_Up, _Err>(unexpect, error());
    }
    if constexpr (!is_void_v<_Up>) {
      return expected<_Up, _Err>(__expected_construct_in_place_from_invoke_tag{}, std::forward<_Func>(__f));
    } else {
      std::invoke(std::forward<_Func>(__f));
      return expected<_Up, _Err>();
    }
  }

  template <class _Func>
    requires is_constructible_v<_Err, _Err&&>
  _LIBCPP_HIDE_FROM_ABI constexpr auto transform(_Func&& __f) && {
    using _Up = remove_cv_t<invoke_result_t<_Func>>;
    if (!has_value()) {
      return expected<_Up, _Err>(unexpect, std::move(error()));
    }
    if constexpr (!is_void_v<_Up>) {
      return expected<_Up, _Err>(__expected_construct_in_place_from_invoke_tag{}, std::forward<_Func>(__f));
    } else {
      std::invoke(std::forward<_Func>(__f));
      return expected<_Up, _Err>();
    }
  }

  template <class _Func>
    requires is_constructible_v<_Err, const _Err&&>
  _LIBCPP_HIDE_FROM_ABI constexpr auto transform(_Func&& __f) const&& {
    using _Up = remove_cv_t<invoke_result_t<_Func>>;
    if (!has_value()) {
      return expected<_Up, _Err>(unexpect, std::move(error()));
    }
    if constexpr (!is_void_v<_Up>) {
      return expected<_Up, _Err>(__expected_construct_in_place_from_invoke_tag{}, std::forward<_Func>(__f));
    } else {
      std::invoke(std::forward<_Func>(__f));
      return expected<_Up, _Err>();
    }
  }

  template <class _Func>
  _LIBCPP_HIDE_FROM_ABI constexpr auto transform_error(_Func&& __f) & {
    using _Gp = remove_cv_t<invoke_result_t<_Func, _Err&>>;
    static_assert(__valid_std_unexpected<_Gp>::value,
                  "The result of f(error()) must be a valid template argument for unexpected");
    if (has_value()) {
      return expected<_Tp, _Gp>();
    }
    return expected<_Tp, _Gp>(__expected_construct_unexpected_from_invoke_tag{}, std::forward<_Func>(__f), error());
  }

  template <class _Func>
  _LIBCPP_HIDE_FROM_ABI constexpr auto transform_error(_Func&& __f) const& {
    using _Gp = remove_cv_t<invoke_result_t<_Func, const _Err&>>;
    static_assert(__valid_std_unexpected<_Gp>::value,
                  "The result of f(error()) must be a valid template argument for unexpected");
    if (has_value()) {
      return expected<_Tp, _Gp>();
    }
    return expected<_Tp, _Gp>(__expected_construct_unexpected_from_invoke_tag{}, std::forward<_Func>(__f), error());
  }

  template <class _Func>
  _LIBCPP_HIDE_FROM_ABI constexpr auto transform_error(_Func&& __f) && {
    using _Gp = remove_cv_t<invoke_result_t<_Func, _Err&&>>;
    static_assert(__valid_std_unexpected<_Gp>::value,
                  "The result of f(std::move(error())) must be a valid template argument for unexpected");
    if (has_value()) {
      return expected<_Tp, _Gp>();
    }
    return expected<_Tp, _Gp>(
        __expected_construct_unexpected_from_invoke_tag{}, std::forward<_Func>(__f), std::move(error()));
  }

  template <class _Func>
  _LIBCPP_HIDE_FROM_ABI constexpr auto transform_error(_Func&& __f) const&& {
    using _Gp = remove_cv_t<invoke_result_t<_Func, const _Err&&>>;
    static_assert(__valid_std_unexpected<_Gp>::value,
                  "The result of f(std::move(error())) must be a valid template argument for unexpected");
    if (has_value()) {
      return expected<_Tp, _Gp>();
    }
    return expected<_Tp, _Gp>(
        __expected_construct_unexpected_from_invoke_tag{}, std::forward<_Func>(__f), std::move(error()));
  }

  // [expected.void.eq], equality operators
  template <class _T2, class _E2>
    requires is_void_v<_T2>
  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator==(const expected& __x, const expected<_T2, _E2>& __y) {
    if (__x.__has_val() != __y.__has_val()) {
      return false;
    } else {
      return __x.__has_val() || static_cast<bool>(__x.__unex() == __y.__unex());
    }
  }

  template <class _E2>
  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator==(const expected& __x, const unexpected<_E2>& __y) {
    return !__x.__has_val() && static_cast<bool>(__x.__unex() == __y.error());
  }
};

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 23

_LIBCPP_POP_MACROS

#endif // _LIBCPP___EXPECTED_EXPECTED_H
