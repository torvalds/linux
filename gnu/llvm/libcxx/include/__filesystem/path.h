// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___FILESYSTEM_PATH_H
#define _LIBCPP___FILESYSTEM_PATH_H

#include <__algorithm/replace.h>
#include <__algorithm/replace_copy.h>
#include <__config>
#include <__functional/unary_function.h>
#include <__fwd/functional.h>
#include <__iterator/back_insert_iterator.h>
#include <__iterator/iterator_traits.h>
#include <__type_traits/decay.h>
#include <__type_traits/is_pointer.h>
#include <__type_traits/remove_const.h>
#include <__type_traits/remove_pointer.h>
#include <cstddef>
#include <string>
#include <string_view>

#if !defined(_LIBCPP_HAS_NO_LOCALIZATION)
#  include <iomanip> // for quoted
#  include <locale>
#endif

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

#if _LIBCPP_STD_VER >= 17

_LIBCPP_BEGIN_NAMESPACE_FILESYSTEM

_LIBCPP_AVAILABILITY_FILESYSTEM_LIBRARY_PUSH

template <class _Tp>
struct __can_convert_char {
  static const bool value = false;
};
template <class _Tp>
struct __can_convert_char<const _Tp> : public __can_convert_char<_Tp> {};
template <>
struct __can_convert_char<char> {
  static const bool value = true;
  using __char_type       = char;
};
template <>
struct __can_convert_char<wchar_t> {
  static const bool value = true;
  using __char_type       = wchar_t;
};
#  ifndef _LIBCPP_HAS_NO_CHAR8_T
template <>
struct __can_convert_char<char8_t> {
  static const bool value = true;
  using __char_type       = char8_t;
};
#  endif
template <>
struct __can_convert_char<char16_t> {
  static const bool value = true;
  using __char_type       = char16_t;
};
template <>
struct __can_convert_char<char32_t> {
  static const bool value = true;
  using __char_type       = char32_t;
};

template <class _ECharT, __enable_if_t<__can_convert_char<_ECharT>::value, int> = 0>
_LIBCPP_HIDE_FROM_ABI bool __is_separator(_ECharT __e) {
#  if defined(_LIBCPP_WIN32API)
  return __e == _ECharT('/') || __e == _ECharT('\\');
#  else
  return __e == _ECharT('/');
#  endif
}

#  ifndef _LIBCPP_HAS_NO_CHAR8_T
typedef u8string __u8_string;
#  else
typedef string __u8_string;
#  endif

struct _NullSentinel {};

template <class _Tp>
using _Void = void;

template <class _Tp, class = void>
struct __is_pathable_string : public false_type {};

template <class _ECharT, class _Traits, class _Alloc>
struct __is_pathable_string< basic_string<_ECharT, _Traits, _Alloc>,
                             _Void<typename __can_convert_char<_ECharT>::__char_type> >
    : public __can_convert_char<_ECharT> {
  using _Str = basic_string<_ECharT, _Traits, _Alloc>;

  _LIBCPP_HIDE_FROM_ABI static _ECharT const* __range_begin(_Str const& __s) { return __s.data(); }

  _LIBCPP_HIDE_FROM_ABI static _ECharT const* __range_end(_Str const& __s) { return __s.data() + __s.length(); }

  _LIBCPP_HIDE_FROM_ABI static _ECharT __first_or_null(_Str const& __s) { return __s.empty() ? _ECharT{} : __s[0]; }
};

template <class _ECharT, class _Traits>
struct __is_pathable_string< basic_string_view<_ECharT, _Traits>,
                             _Void<typename __can_convert_char<_ECharT>::__char_type> >
    : public __can_convert_char<_ECharT> {
  using _Str = basic_string_view<_ECharT, _Traits>;

  _LIBCPP_HIDE_FROM_ABI static _ECharT const* __range_begin(_Str const& __s) { return __s.data(); }

  _LIBCPP_HIDE_FROM_ABI static _ECharT const* __range_end(_Str const& __s) { return __s.data() + __s.length(); }

  _LIBCPP_HIDE_FROM_ABI static _ECharT __first_or_null(_Str const& __s) { return __s.empty() ? _ECharT{} : __s[0]; }
};

template <class _Source,
          class _DS            = __decay_t<_Source>,
          class _UnqualPtrType = __remove_const_t<__remove_pointer_t<_DS> >,
          bool _IsCharPtr      = is_pointer<_DS>::value && __can_convert_char<_UnqualPtrType>::value>
struct __is_pathable_char_array : false_type {};

template <class _Source, class _ECharT, class _UPtr>
struct __is_pathable_char_array<_Source, _ECharT*, _UPtr, true> : __can_convert_char<__remove_const_t<_ECharT> > {
  _LIBCPP_HIDE_FROM_ABI static _ECharT const* __range_begin(const _ECharT* __b) { return __b; }

  _LIBCPP_HIDE_FROM_ABI static _ECharT const* __range_end(const _ECharT* __b) {
    using _Iter              = const _ECharT*;
    const _ECharT __sentinel = _ECharT{};
    _Iter __e                = __b;
    for (; *__e != __sentinel; ++__e)
      ;
    return __e;
  }

  _LIBCPP_HIDE_FROM_ABI static _ECharT __first_or_null(const _ECharT* __b) { return *__b; }
};

template <class _Iter, bool _IsIt = __has_input_iterator_category<_Iter>::value, class = void>
struct __is_pathable_iter : false_type {};

template <class _Iter>
struct __is_pathable_iter<
    _Iter,
    true,
    _Void<typename __can_convert_char< typename iterator_traits<_Iter>::value_type>::__char_type> >
    : __can_convert_char<typename iterator_traits<_Iter>::value_type> {
  using _ECharT = typename iterator_traits<_Iter>::value_type;

  _LIBCPP_HIDE_FROM_ABI static _Iter __range_begin(_Iter __b) { return __b; }

  _LIBCPP_HIDE_FROM_ABI static _NullSentinel __range_end(_Iter) { return _NullSentinel{}; }

  _LIBCPP_HIDE_FROM_ABI static _ECharT __first_or_null(_Iter __b) { return *__b; }
};

template <class _Tp,
          bool _IsStringT   = __is_pathable_string<_Tp>::value,
          bool _IsCharIterT = __is_pathable_char_array<_Tp>::value,
          bool _IsIterT     = !_IsCharIterT && __is_pathable_iter<_Tp>::value>
struct __is_pathable : false_type {
  static_assert(!_IsStringT && !_IsCharIterT && !_IsIterT, "Must all be false");
};

template <class _Tp>
struct __is_pathable<_Tp, true, false, false> : __is_pathable_string<_Tp> {};

template <class _Tp>
struct __is_pathable<_Tp, false, true, false> : __is_pathable_char_array<_Tp> {};

template <class _Tp>
struct __is_pathable<_Tp, false, false, true> : __is_pathable_iter<_Tp> {};

#  if defined(_LIBCPP_WIN32API)
typedef wstring __path_string;
typedef wchar_t __path_value;
#  else
typedef string __path_string;
typedef char __path_value;
#  endif

#  if defined(_LIBCPP_WIN32API)
_LIBCPP_EXPORTED_FROM_ABI size_t __wide_to_char(const wstring&, char*, size_t);
_LIBCPP_EXPORTED_FROM_ABI size_t __char_to_wide(const string&, wchar_t*, size_t);
#  endif

template <class _ECharT>
struct _PathCVT;

#  if !defined(_LIBCPP_HAS_NO_LOCALIZATION)
template <class _ECharT>
struct _PathCVT {
  static_assert(__can_convert_char<_ECharT>::value, "Char type not convertible");

  typedef __narrow_to_utf8<sizeof(_ECharT) * __CHAR_BIT__> _Narrower;
#    if defined(_LIBCPP_WIN32API)
  typedef __widen_from_utf8<sizeof(wchar_t) * __CHAR_BIT__> _Widener;
#    endif

  _LIBCPP_HIDE_FROM_ABI static void __append_range(__path_string& __dest, _ECharT const* __b, _ECharT const* __e) {
#    if defined(_LIBCPP_WIN32API)
    string __utf8;
    _Narrower()(back_inserter(__utf8), __b, __e);
    _Widener()(back_inserter(__dest), __utf8.data(), __utf8.data() + __utf8.size());
#    else
    _Narrower()(back_inserter(__dest), __b, __e);
#    endif
  }

  template <class _Iter>
  _LIBCPP_HIDE_FROM_ABI static void __append_range(__path_string& __dest, _Iter __b, _Iter __e) {
    static_assert(!is_same<_Iter, _ECharT*>::value, "Call const overload");
    if (__b == __e)
      return;
    basic_string<_ECharT> __tmp(__b, __e);
#    if defined(_LIBCPP_WIN32API)
    string __utf8;
    _Narrower()(back_inserter(__utf8), __tmp.data(), __tmp.data() + __tmp.length());
    _Widener()(back_inserter(__dest), __utf8.data(), __utf8.data() + __utf8.size());
#    else
    _Narrower()(back_inserter(__dest), __tmp.data(), __tmp.data() + __tmp.length());
#    endif
  }

  template <class _Iter>
  _LIBCPP_HIDE_FROM_ABI static void __append_range(__path_string& __dest, _Iter __b, _NullSentinel) {
    static_assert(!is_same<_Iter, _ECharT*>::value, "Call const overload");
    const _ECharT __sentinel = _ECharT{};
    if (*__b == __sentinel)
      return;
    basic_string<_ECharT> __tmp;
    for (; *__b != __sentinel; ++__b)
      __tmp.push_back(*__b);
#    if defined(_LIBCPP_WIN32API)
    string __utf8;
    _Narrower()(back_inserter(__utf8), __tmp.data(), __tmp.data() + __tmp.length());
    _Widener()(back_inserter(__dest), __utf8.data(), __utf8.data() + __utf8.size());
#    else
    _Narrower()(back_inserter(__dest), __tmp.data(), __tmp.data() + __tmp.length());
#    endif
  }

  template <class _Source>
  _LIBCPP_HIDE_FROM_ABI static void __append_source(__path_string& __dest, _Source const& __s) {
    using _Traits = __is_pathable<_Source>;
    __append_range(__dest, _Traits::__range_begin(__s), _Traits::__range_end(__s));
  }
};
#  endif // !_LIBCPP_HAS_NO_LOCALIZATION

template <>
struct _PathCVT<__path_value> {
  template <class _Iter, __enable_if_t<__has_exactly_input_iterator_category<_Iter>::value, int> = 0>
  _LIBCPP_HIDE_FROM_ABI static void __append_range(__path_string& __dest, _Iter __b, _Iter __e) {
    for (; __b != __e; ++__b)
      __dest.push_back(*__b);
  }

  template <class _Iter, __enable_if_t<__has_forward_iterator_category<_Iter>::value, int> = 0>
  _LIBCPP_HIDE_FROM_ABI static void __append_range(__path_string& __dest, _Iter __b, _Iter __e) {
    __dest.append(__b, __e);
  }

  template <class _Iter>
  _LIBCPP_HIDE_FROM_ABI static void __append_range(__path_string& __dest, _Iter __b, _NullSentinel) {
    const char __sentinel = char{};
    for (; *__b != __sentinel; ++__b)
      __dest.push_back(*__b);
  }

  template <class _Source>
  _LIBCPP_HIDE_FROM_ABI static void __append_source(__path_string& __dest, _Source const& __s) {
    using _Traits = __is_pathable<_Source>;
    __append_range(__dest, _Traits::__range_begin(__s), _Traits::__range_end(__s));
  }
};

#  if defined(_LIBCPP_WIN32API)
template <>
struct _PathCVT<char> {
  _LIBCPP_HIDE_FROM_ABI static void __append_string(__path_string& __dest, const basic_string<char>& __str) {
    size_t __size = __char_to_wide(__str, nullptr, 0);
    size_t __pos  = __dest.size();
    __dest.resize(__pos + __size);
    __char_to_wide(__str, const_cast<__path_value*>(__dest.data()) + __pos, __size);
  }

  template <class _Iter, __enable_if_t<__has_exactly_input_iterator_category<_Iter>::value, int> = 0>
  _LIBCPP_HIDE_FROM_ABI static void __append_range(__path_string& __dest, _Iter __b, _Iter __e) {
    basic_string<char> __tmp(__b, __e);
    __append_string(__dest, __tmp);
  }

  template <class _Iter, __enable_if_t<__has_forward_iterator_category<_Iter>::value, int> = 0>
  _LIBCPP_HIDE_FROM_ABI static void __append_range(__path_string& __dest, _Iter __b, _Iter __e) {
    basic_string<char> __tmp(__b, __e);
    __append_string(__dest, __tmp);
  }

  template <class _Iter>
  _LIBCPP_HIDE_FROM_ABI static void __append_range(__path_string& __dest, _Iter __b, _NullSentinel) {
    const char __sentinel = char{};
    basic_string<char> __tmp;
    for (; *__b != __sentinel; ++__b)
      __tmp.push_back(*__b);
    __append_string(__dest, __tmp);
  }

  template <class _Source>
  _LIBCPP_HIDE_FROM_ABI static void __append_source(__path_string& __dest, _Source const& __s) {
    using _Traits = __is_pathable<_Source>;
    __append_range(__dest, _Traits::__range_begin(__s), _Traits::__range_end(__s));
  }
};

template <class _ECharT>
struct _PathExport {
  typedef __narrow_to_utf8<sizeof(wchar_t) * __CHAR_BIT__> _Narrower;
  typedef __widen_from_utf8<sizeof(_ECharT) * __CHAR_BIT__> _Widener;

  template <class _Str>
  _LIBCPP_HIDE_FROM_ABI static void __append(_Str& __dest, const __path_string& __src) {
    string __utf8;
    _Narrower()(back_inserter(__utf8), __src.data(), __src.data() + __src.size());
    _Widener()(back_inserter(__dest), __utf8.data(), __utf8.data() + __utf8.size());
  }
};

template <>
struct _PathExport<char> {
  template <class _Str>
  _LIBCPP_HIDE_FROM_ABI static void __append(_Str& __dest, const __path_string& __src) {
    size_t __size = __wide_to_char(__src, nullptr, 0);
    size_t __pos  = __dest.size();
    __dest.resize(__size);
    __wide_to_char(__src, const_cast<char*>(__dest.data()) + __pos, __size);
  }
};

template <>
struct _PathExport<wchar_t> {
  template <class _Str>
  _LIBCPP_HIDE_FROM_ABI static void __append(_Str& __dest, const __path_string& __src) {
    __dest.append(__src.begin(), __src.end());
  }
};

template <>
struct _PathExport<char16_t> {
  template <class _Str>
  _LIBCPP_HIDE_FROM_ABI static void __append(_Str& __dest, const __path_string& __src) {
    __dest.append(__src.begin(), __src.end());
  }
};

#    ifndef _LIBCPP_HAS_NO_CHAR8_T
template <>
struct _PathExport<char8_t> {
  typedef __narrow_to_utf8<sizeof(wchar_t) * __CHAR_BIT__> _Narrower;

  template <class _Str>
  _LIBCPP_HIDE_FROM_ABI static void __append(_Str& __dest, const __path_string& __src) {
    _Narrower()(back_inserter(__dest), __src.data(), __src.data() + __src.size());
  }
};
#    endif /* !_LIBCPP_HAS_NO_CHAR8_T */
#  endif   /* _LIBCPP_WIN32API */

class _LIBCPP_EXPORTED_FROM_ABI path {
  template <class _SourceOrIter, class _Tp = path&>
  using _EnableIfPathable = __enable_if_t<__is_pathable<_SourceOrIter>::value, _Tp>;

  template <class _Tp>
  using _SourceChar = typename __is_pathable<_Tp>::__char_type;

  template <class _Tp>
  using _SourceCVT = _PathCVT<_SourceChar<_Tp> >;

public:
#  if defined(_LIBCPP_WIN32API)
  typedef wchar_t value_type;
  static constexpr value_type preferred_separator = L'\\';
#  else
  typedef char value_type;
  static constexpr value_type preferred_separator = '/';
#  endif
  typedef basic_string<value_type> string_type;
  typedef basic_string_view<value_type> __string_view;

  enum format : unsigned char { auto_format, native_format, generic_format };

  // constructors and destructor
  _LIBCPP_HIDE_FROM_ABI path() noexcept {}
  _LIBCPP_HIDE_FROM_ABI path(const path& __p) : __pn_(__p.__pn_) {}
  _LIBCPP_HIDE_FROM_ABI path(path&& __p) noexcept : __pn_(std::move(__p.__pn_)) {}

  _LIBCPP_HIDE_FROM_ABI path(string_type&& __s, format = format::auto_format) noexcept : __pn_(std::move(__s)) {}

  template <class _Source, class = _EnableIfPathable<_Source, void> >
  _LIBCPP_HIDE_FROM_ABI path(const _Source& __src, format = format::auto_format) {
    _SourceCVT<_Source>::__append_source(__pn_, __src);
  }

  template <class _InputIt>
  _LIBCPP_HIDE_FROM_ABI path(_InputIt __first, _InputIt __last, format = format::auto_format) {
    typedef typename iterator_traits<_InputIt>::value_type _ItVal;
    _PathCVT<_ItVal>::__append_range(__pn_, __first, __last);
  }

  /*
  #if !defined(_LIBCPP_HAS_NO_LOCALIZATION)
    // TODO Implement locale conversions.
    template <class _Source, class = _EnableIfPathable<_Source, void> >
    path(const _Source& __src, const locale& __loc, format = format::auto_format);
    template <class _InputIt>
    path(_InputIt __first, _InputIt _last, const locale& __loc,
         format = format::auto_format);
  #endif
  */

  _LIBCPP_HIDE_FROM_ABI ~path() = default;

  // assignments
  _LIBCPP_HIDE_FROM_ABI path& operator=(const path& __p) {
    __pn_ = __p.__pn_;
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI path& operator=(path&& __p) noexcept {
    __pn_ = std::move(__p.__pn_);
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI path& operator=(string_type&& __s) noexcept {
    __pn_ = std::move(__s);
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI path& assign(string_type&& __s) noexcept {
    __pn_ = std::move(__s);
    return *this;
  }

  template <class _Source>
  _LIBCPP_HIDE_FROM_ABI _EnableIfPathable<_Source> operator=(const _Source& __src) {
    return this->assign(__src);
  }

  template <class _Source>
  _LIBCPP_HIDE_FROM_ABI _EnableIfPathable<_Source> assign(const _Source& __src) {
    __pn_.clear();
    _SourceCVT<_Source>::__append_source(__pn_, __src);
    return *this;
  }

  template <class _InputIt>
  _LIBCPP_HIDE_FROM_ABI path& assign(_InputIt __first, _InputIt __last) {
    typedef typename iterator_traits<_InputIt>::value_type _ItVal;
    __pn_.clear();
    _PathCVT<_ItVal>::__append_range(__pn_, __first, __last);
    return *this;
  }

public:
  // appends
#  if defined(_LIBCPP_WIN32API)
  _LIBCPP_HIDE_FROM_ABI path& operator/=(const path& __p) {
    auto __p_root_name      = __p.__root_name();
    auto __p_root_name_size = __p_root_name.size();
    if (__p.is_absolute() || (!__p_root_name.empty() && __p_root_name != __string_view(root_name().__pn_))) {
      __pn_ = __p.__pn_;
      return *this;
    }
    if (__p.has_root_directory()) {
      path __root_name_str = root_name();
      __pn_                = __root_name_str.native();
      __pn_ += __string_view(__p.__pn_).substr(__p_root_name_size);
      return *this;
    }
    if (has_filename() || (!has_root_directory() && is_absolute()))
      __pn_ += preferred_separator;
    __pn_ += __string_view(__p.__pn_).substr(__p_root_name_size);
    return *this;
  }
  template <class _Source>
  _LIBCPP_HIDE_FROM_ABI _EnableIfPathable<_Source> operator/=(const _Source& __src) {
    return operator/=(path(__src));
  }

  template <class _Source>
  _LIBCPP_HIDE_FROM_ABI _EnableIfPathable<_Source> append(const _Source& __src) {
    return operator/=(path(__src));
  }

  template <class _InputIt>
  _LIBCPP_HIDE_FROM_ABI path& append(_InputIt __first, _InputIt __last) {
    return operator/=(path(__first, __last));
  }
#  else
  _LIBCPP_HIDE_FROM_ABI path& operator/=(const path& __p) {
    if (__p.is_absolute()) {
      __pn_ = __p.__pn_;
      return *this;
    }
    if (has_filename())
      __pn_ += preferred_separator;
    __pn_ += __p.native();
    return *this;
  }

  // FIXME: Use _LIBCPP_DIAGNOSE_WARNING to produce a diagnostic when __src
  // is known at compile time to be "/' since the user almost certainly intended
  // to append a separator instead of overwriting the path with "/"
  template <class _Source>
  _LIBCPP_HIDE_FROM_ABI _EnableIfPathable<_Source> operator/=(const _Source& __src) {
    return this->append(__src);
  }

  template <class _Source>
  _LIBCPP_HIDE_FROM_ABI _EnableIfPathable<_Source> append(const _Source& __src) {
    using _Traits             = __is_pathable<_Source>;
    using _CVT                = _PathCVT<_SourceChar<_Source> >;
    bool __source_is_absolute = filesystem::__is_separator(_Traits::__first_or_null(__src));
    if (__source_is_absolute)
      __pn_.clear();
    else if (has_filename())
      __pn_ += preferred_separator;
    _CVT::__append_source(__pn_, __src);
    return *this;
  }

  template <class _InputIt>
  _LIBCPP_HIDE_FROM_ABI path& append(_InputIt __first, _InputIt __last) {
    typedef typename iterator_traits<_InputIt>::value_type _ItVal;
    static_assert(__can_convert_char<_ItVal>::value, "Must convertible");
    using _CVT = _PathCVT<_ItVal>;
    if (__first != __last && filesystem::__is_separator(*__first))
      __pn_.clear();
    else if (has_filename())
      __pn_ += preferred_separator;
    _CVT::__append_range(__pn_, __first, __last);
    return *this;
  }
#  endif

  // concatenation
  _LIBCPP_HIDE_FROM_ABI path& operator+=(const path& __x) {
    __pn_ += __x.__pn_;
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI path& operator+=(const string_type& __x) {
    __pn_ += __x;
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI path& operator+=(__string_view __x) {
    __pn_ += __x;
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI path& operator+=(const value_type* __x) {
    __pn_ += __x;
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI path& operator+=(value_type __x) {
    __pn_ += __x;
    return *this;
  }

  template <class _ECharT, __enable_if_t<__can_convert_char<_ECharT>::value, int> = 0>
  _LIBCPP_HIDE_FROM_ABI path& operator+=(_ECharT __x) {
    _PathCVT<_ECharT>::__append_source(__pn_, basic_string_view<_ECharT>(&__x, 1));
    return *this;
  }

  template <class _Source>
  _LIBCPP_HIDE_FROM_ABI _EnableIfPathable<_Source> operator+=(const _Source& __x) {
    return this->concat(__x);
  }

  template <class _Source>
  _LIBCPP_HIDE_FROM_ABI _EnableIfPathable<_Source> concat(const _Source& __x) {
    _SourceCVT<_Source>::__append_source(__pn_, __x);
    return *this;
  }

  template <class _InputIt>
  _LIBCPP_HIDE_FROM_ABI path& concat(_InputIt __first, _InputIt __last) {
    typedef typename iterator_traits<_InputIt>::value_type _ItVal;
    _PathCVT<_ItVal>::__append_range(__pn_, __first, __last);
    return *this;
  }

  // modifiers
  _LIBCPP_HIDE_FROM_ABI void clear() noexcept { __pn_.clear(); }

  _LIBCPP_HIDE_FROM_ABI path& make_preferred() {
#  if defined(_LIBCPP_WIN32API)
    std::replace(__pn_.begin(), __pn_.end(), L'/', L'\\');
#  endif
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI path& remove_filename() {
    auto __fname = __filename();
    if (!__fname.empty())
      __pn_.erase(__fname.data() - __pn_.data());
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI path& replace_filename(const path& __replacement) {
    remove_filename();
    return (*this /= __replacement);
  }

  path& replace_extension(const path& __replacement = path());

  friend _LIBCPP_HIDE_FROM_ABI bool operator==(const path& __lhs, const path& __rhs) noexcept {
    return __lhs.__compare(__rhs.__pn_) == 0;
  }
#  if _LIBCPP_STD_VER <= 17
  friend _LIBCPP_HIDE_FROM_ABI bool operator!=(const path& __lhs, const path& __rhs) noexcept {
    return __lhs.__compare(__rhs.__pn_) != 0;
  }
  friend _LIBCPP_HIDE_FROM_ABI bool operator<(const path& __lhs, const path& __rhs) noexcept {
    return __lhs.__compare(__rhs.__pn_) < 0;
  }
  friend _LIBCPP_HIDE_FROM_ABI bool operator<=(const path& __lhs, const path& __rhs) noexcept {
    return __lhs.__compare(__rhs.__pn_) <= 0;
  }
  friend _LIBCPP_HIDE_FROM_ABI bool operator>(const path& __lhs, const path& __rhs) noexcept {
    return __lhs.__compare(__rhs.__pn_) > 0;
  }
  friend _LIBCPP_HIDE_FROM_ABI bool operator>=(const path& __lhs, const path& __rhs) noexcept {
    return __lhs.__compare(__rhs.__pn_) >= 0;
  }
#  else  // _LIBCPP_STD_VER <= 17
  friend _LIBCPP_HIDE_FROM_ABI strong_ordering operator<=>(const path& __lhs, const path& __rhs) noexcept {
    return __lhs.__compare(__rhs.__pn_) <=> 0;
  }
#  endif // _LIBCPP_STD_VER <= 17

  friend _LIBCPP_HIDE_FROM_ABI path operator/(const path& __lhs, const path& __rhs) {
    path __result(__lhs);
    __result /= __rhs;
    return __result;
  }

  _LIBCPP_HIDE_FROM_ABI void swap(path& __rhs) noexcept { __pn_.swap(__rhs.__pn_); }

  // private helper to allow reserving memory in the path
  _LIBCPP_HIDE_FROM_ABI void __reserve(size_t __s) { __pn_.reserve(__s); }

  // native format observers
  _LIBCPP_HIDE_FROM_ABI const string_type& native() const noexcept { return __pn_; }

  _LIBCPP_HIDE_FROM_ABI const value_type* c_str() const noexcept { return __pn_.c_str(); }

  _LIBCPP_HIDE_FROM_ABI operator string_type() const { return __pn_; }

#  if defined(_LIBCPP_WIN32API)
  _LIBCPP_HIDE_FROM_ABI std::wstring wstring() const { return __pn_; }

  _LIBCPP_HIDE_FROM_ABI std::wstring generic_wstring() const {
    std::wstring __s;
    __s.resize(__pn_.size());
    std::replace_copy(__pn_.begin(), __pn_.end(), __s.begin(), '\\', '/');
    return __s;
  }

#    if !defined(_LIBCPP_HAS_NO_LOCALIZATION)
  template <class _ECharT, class _Traits = char_traits<_ECharT>, class _Allocator = allocator<_ECharT> >
  _LIBCPP_HIDE_FROM_ABI basic_string<_ECharT, _Traits, _Allocator> string(const _Allocator& __a = _Allocator()) const {
    using _Str = basic_string<_ECharT, _Traits, _Allocator>;
    _Str __s(__a);
    __s.reserve(__pn_.size());
    _PathExport<_ECharT>::__append(__s, __pn_);
    return __s;
  }

  _LIBCPP_HIDE_FROM_ABI std::string string() const { return string<char>(); }
  _LIBCPP_HIDE_FROM_ABI __u8_string u8string() const {
    using _CVT = __narrow_to_utf8<sizeof(wchar_t) * __CHAR_BIT__>;
    __u8_string __s;
    __s.reserve(__pn_.size());
    _CVT()(back_inserter(__s), __pn_.data(), __pn_.data() + __pn_.size());
    return __s;
  }

  _LIBCPP_HIDE_FROM_ABI std::u16string u16string() const { return string<char16_t>(); }
  _LIBCPP_HIDE_FROM_ABI std::u32string u32string() const { return string<char32_t>(); }

  // generic format observers
  template <class _ECharT, class _Traits = char_traits<_ECharT>, class _Allocator = allocator<_ECharT> >
  _LIBCPP_HIDE_FROM_ABI basic_string<_ECharT, _Traits, _Allocator>
  generic_string(const _Allocator& __a = _Allocator()) const {
    using _Str = basic_string<_ECharT, _Traits, _Allocator>;
    _Str __s   = string<_ECharT, _Traits, _Allocator>(__a);
    // Note: This (and generic_u8string below) is slightly suboptimal as
    // it iterates twice over the string; once to convert it to the right
    // character type, and once to replace path delimiters.
    std::replace(__s.begin(), __s.end(), static_cast<_ECharT>('\\'), static_cast<_ECharT>('/'));
    return __s;
  }

  _LIBCPP_HIDE_FROM_ABI std::string generic_string() const { return generic_string<char>(); }
  _LIBCPP_HIDE_FROM_ABI std::u16string generic_u16string() const { return generic_string<char16_t>(); }
  _LIBCPP_HIDE_FROM_ABI std::u32string generic_u32string() const { return generic_string<char32_t>(); }
  _LIBCPP_HIDE_FROM_ABI __u8_string generic_u8string() const {
    __u8_string __s = u8string();
    std::replace(__s.begin(), __s.end(), '\\', '/');
    return __s;
  }
#    endif /* !_LIBCPP_HAS_NO_LOCALIZATION */
#  else    /* _LIBCPP_WIN32API */

  _LIBCPP_HIDE_FROM_ABI std::string string() const { return __pn_; }
#    ifndef _LIBCPP_HAS_NO_CHAR8_T
  _LIBCPP_HIDE_FROM_ABI std::u8string u8string() const { return std::u8string(__pn_.begin(), __pn_.end()); }
#    else
  _LIBCPP_HIDE_FROM_ABI std::string u8string() const { return __pn_; }
#    endif

#    if !defined(_LIBCPP_HAS_NO_LOCALIZATION)
  template <class _ECharT, class _Traits = char_traits<_ECharT>, class _Allocator = allocator<_ECharT> >
  _LIBCPP_HIDE_FROM_ABI basic_string<_ECharT, _Traits, _Allocator> string(const _Allocator& __a = _Allocator()) const {
    using _CVT = __widen_from_utf8<sizeof(_ECharT) * __CHAR_BIT__>;
    using _Str = basic_string<_ECharT, _Traits, _Allocator>;
    _Str __s(__a);
    __s.reserve(__pn_.size());
    _CVT()(std::back_inserter(__s), __pn_.data(), __pn_.data() + __pn_.size());
    return __s;
  }

#      ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  _LIBCPP_HIDE_FROM_ABI std::wstring wstring() const { return string<wchar_t>(); }
#      endif
  _LIBCPP_HIDE_FROM_ABI std::u16string u16string() const { return string<char16_t>(); }
  _LIBCPP_HIDE_FROM_ABI std::u32string u32string() const { return string<char32_t>(); }
#    endif /* !_LIBCPP_HAS_NO_LOCALIZATION */

  // generic format observers
  _LIBCPP_HIDE_FROM_ABI std::string generic_string() const { return __pn_; }
#    ifndef _LIBCPP_HAS_NO_CHAR8_T
  _LIBCPP_HIDE_FROM_ABI std::u8string generic_u8string() const { return std::u8string(__pn_.begin(), __pn_.end()); }
#    else
  _LIBCPP_HIDE_FROM_ABI std::string generic_u8string() const { return __pn_; }
#    endif

#    if !defined(_LIBCPP_HAS_NO_LOCALIZATION)
  template <class _ECharT, class _Traits = char_traits<_ECharT>, class _Allocator = allocator<_ECharT> >
  _LIBCPP_HIDE_FROM_ABI basic_string<_ECharT, _Traits, _Allocator>
  generic_string(const _Allocator& __a = _Allocator()) const {
    return string<_ECharT, _Traits, _Allocator>(__a);
  }

#      ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  _LIBCPP_HIDE_FROM_ABI std::wstring generic_wstring() const { return string<wchar_t>(); }
#      endif
  _LIBCPP_HIDE_FROM_ABI std::u16string generic_u16string() const { return string<char16_t>(); }
  _LIBCPP_HIDE_FROM_ABI std::u32string generic_u32string() const { return string<char32_t>(); }
#    endif /* !_LIBCPP_HAS_NO_LOCALIZATION */
#  endif   /* !_LIBCPP_WIN32API */

private:
  int __compare(__string_view) const;
  __string_view __root_name() const;
  __string_view __root_directory() const;
  __string_view __root_path_raw() const;
  __string_view __relative_path() const;
  __string_view __parent_path() const;
  __string_view __filename() const;
  __string_view __stem() const;
  __string_view __extension() const;

public:
  // compare
  _LIBCPP_HIDE_FROM_ABI int compare(const path& __p) const noexcept { return __compare(__p.__pn_); }
  _LIBCPP_HIDE_FROM_ABI int compare(const string_type& __s) const { return __compare(__s); }
  _LIBCPP_HIDE_FROM_ABI int compare(__string_view __s) const { return __compare(__s); }
  _LIBCPP_HIDE_FROM_ABI int compare(const value_type* __s) const { return __compare(__s); }

  // decomposition
  _LIBCPP_HIDE_FROM_ABI path root_name() const { return string_type(__root_name()); }
  _LIBCPP_HIDE_FROM_ABI path root_directory() const { return string_type(__root_directory()); }
  _LIBCPP_HIDE_FROM_ABI path root_path() const {
#  if defined(_LIBCPP_WIN32API)
    return string_type(__root_path_raw());
#  else
    return root_name().append(string_type(__root_directory()));
#  endif
  }
  _LIBCPP_HIDE_FROM_ABI path relative_path() const { return string_type(__relative_path()); }
  _LIBCPP_HIDE_FROM_ABI path parent_path() const { return string_type(__parent_path()); }
  _LIBCPP_HIDE_FROM_ABI path filename() const { return string_type(__filename()); }
  _LIBCPP_HIDE_FROM_ABI path stem() const { return string_type(__stem()); }
  _LIBCPP_HIDE_FROM_ABI path extension() const { return string_type(__extension()); }

  // query
  _LIBCPP_NODISCARD _LIBCPP_HIDE_FROM_ABI bool empty() const noexcept { return __pn_.empty(); }

  _LIBCPP_HIDE_FROM_ABI bool has_root_name() const { return !__root_name().empty(); }
  _LIBCPP_HIDE_FROM_ABI bool has_root_directory() const { return !__root_directory().empty(); }
  _LIBCPP_HIDE_FROM_ABI bool has_root_path() const { return !__root_path_raw().empty(); }
  _LIBCPP_HIDE_FROM_ABI bool has_relative_path() const { return !__relative_path().empty(); }
  _LIBCPP_HIDE_FROM_ABI bool has_parent_path() const { return !__parent_path().empty(); }
  _LIBCPP_HIDE_FROM_ABI bool has_filename() const { return !__filename().empty(); }
  _LIBCPP_HIDE_FROM_ABI bool has_stem() const { return !__stem().empty(); }
  _LIBCPP_HIDE_FROM_ABI bool has_extension() const { return !__extension().empty(); }

  _LIBCPP_HIDE_FROM_ABI bool is_absolute() const {
#  if defined(_LIBCPP_WIN32API)
    __string_view __root_name_str = __root_name();
    __string_view __root_dir      = __root_directory();
    if (__root_name_str.size() == 2 && __root_name_str[1] == ':') {
      // A drive letter with no root directory is relative, e.g. x:example.
      return !__root_dir.empty();
    }
    // If no root name, it's relative, e.g. \example is relative to the current drive
    if (__root_name_str.empty())
      return false;
    if (__root_name_str.size() < 3)
      return false;
    // A server root name, like \\server, is always absolute
    if (__root_name_str[0] != '/' && __root_name_str[0] != '\\')
      return false;
    if (__root_name_str[1] != '/' && __root_name_str[1] != '\\')
      return false;
    // Seems to be a server root name
    return true;
#  else
    return has_root_directory();
#  endif
  }
  _LIBCPP_HIDE_FROM_ABI bool is_relative() const { return !is_absolute(); }

  // relative paths
  path lexically_normal() const;
  path lexically_relative(const path& __base) const;

  _LIBCPP_HIDE_FROM_ABI path lexically_proximate(const path& __base) const {
    path __result = this->lexically_relative(__base);
    if (__result.native().empty())
      return *this;
    return __result;
  }

  // iterators
  class _LIBCPP_EXPORTED_FROM_ABI iterator;
  typedef iterator const_iterator;

  iterator begin() const;
  iterator end() const;

#  if !defined(_LIBCPP_HAS_NO_LOCALIZATION)
  template <
      class _CharT,
      class _Traits,
      __enable_if_t<is_same<_CharT, value_type>::value && is_same<_Traits, char_traits<value_type> >::value, int> = 0>
  _LIBCPP_HIDE_FROM_ABI friend basic_ostream<_CharT, _Traits>&
  operator<<(basic_ostream<_CharT, _Traits>& __os, const path& __p) {
    __os << std::__quoted(__p.native());
    return __os;
  }

  template <
      class _CharT,
      class _Traits,
      __enable_if_t<!is_same<_CharT, value_type>::value || !is_same<_Traits, char_traits<value_type> >::value, int> = 0>
  _LIBCPP_HIDE_FROM_ABI friend basic_ostream<_CharT, _Traits>&
  operator<<(basic_ostream<_CharT, _Traits>& __os, const path& __p) {
    __os << std::__quoted(__p.string<_CharT, _Traits>());
    return __os;
  }

  template <class _CharT, class _Traits>
  _LIBCPP_HIDE_FROM_ABI friend basic_istream<_CharT, _Traits>&
  operator>>(basic_istream<_CharT, _Traits>& __is, path& __p) {
    basic_string<_CharT, _Traits> __tmp;
    __is >> std::__quoted(__tmp);
    __p = __tmp;
    return __is;
  }
#  endif // !_LIBCPP_HAS_NO_LOCALIZATION

private:
  inline _LIBCPP_HIDE_FROM_ABI path& __assign_view(__string_view const& __s) {
    __pn_ = string_type(__s);
    return *this;
  }
  string_type __pn_;
};

inline _LIBCPP_HIDE_FROM_ABI void swap(path& __lhs, path& __rhs) noexcept { __lhs.swap(__rhs); }

_LIBCPP_EXPORTED_FROM_ABI size_t hash_value(const path& __p) noexcept;

_LIBCPP_AVAILABILITY_FILESYSTEM_LIBRARY_POP

_LIBCPP_END_NAMESPACE_FILESYSTEM

_LIBCPP_BEGIN_NAMESPACE_STD

template <>
struct _LIBCPP_AVAILABILITY_FILESYSTEM_LIBRARY hash<filesystem::path> : __unary_function<filesystem::path, size_t> {
  _LIBCPP_HIDE_FROM_ABI size_t operator()(filesystem::path const& __p) const noexcept {
    return filesystem::hash_value(__p);
  }
};

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 17

_LIBCPP_POP_MACROS

#endif // _LIBCPP___FILESYSTEM_PATH_H
