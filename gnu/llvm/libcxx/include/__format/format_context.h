// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___FORMAT_FORMAT_CONTEXT_H
#define _LIBCPP___FORMAT_FORMAT_CONTEXT_H

#include <__concepts/same_as.h>
#include <__config>
#include <__format/buffer.h>
#include <__format/format_arg.h>
#include <__format/format_arg_store.h>
#include <__format/format_args.h>
#include <__format/format_error.h>
#include <__fwd/format.h>
#include <__iterator/back_insert_iterator.h>
#include <__iterator/concepts.h>
#include <__memory/addressof.h>
#include <__utility/move.h>
#include <__variant/monostate.h>
#include <cstddef>

#ifndef _LIBCPP_HAS_NO_LOCALIZATION
#  include <__locale>
#  include <optional>
#endif

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

template <class _OutIt, class _CharT>
  requires output_iterator<_OutIt, const _CharT&>
class _LIBCPP_TEMPLATE_VIS basic_format_context;

#  ifndef _LIBCPP_HAS_NO_LOCALIZATION
/**
 * Helper to create a basic_format_context.
 *
 * This is needed since the constructor is private.
 */
template <class _OutIt, class _CharT>
_LIBCPP_HIDE_FROM_ABI basic_format_context<_OutIt, _CharT>
__format_context_create(_OutIt __out_it,
                        basic_format_args<basic_format_context<_OutIt, _CharT>> __args,
                        optional<std::locale>&& __loc = nullopt) {
  return std::basic_format_context(std::move(__out_it), __args, std::move(__loc));
}
#  else
template <class _OutIt, class _CharT>
_LIBCPP_HIDE_FROM_ABI basic_format_context<_OutIt, _CharT>
__format_context_create(_OutIt __out_it, basic_format_args<basic_format_context<_OutIt, _CharT>> __args) {
  return std::basic_format_context(std::move(__out_it), __args);
}
#  endif

using format_context = basic_format_context<back_insert_iterator<__format::__output_buffer<char>>, char>;
#  ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
using wformat_context = basic_format_context< back_insert_iterator<__format::__output_buffer<wchar_t>>, wchar_t>;
#  endif

template <class _OutIt, class _CharT>
  requires output_iterator<_OutIt, const _CharT&>
class
    // clang-format off
    _LIBCPP_TEMPLATE_VIS
    _LIBCPP_PREFERRED_NAME(format_context)
    _LIBCPP_IF_WIDE_CHARACTERS(_LIBCPP_PREFERRED_NAME(wformat_context))
    // clang-format on
    basic_format_context {
public:
  using iterator  = _OutIt;
  using char_type = _CharT;
  template <class _Tp>
  using formatter_type = formatter<_Tp, _CharT>;

  _LIBCPP_HIDE_FROM_ABI basic_format_arg<basic_format_context> arg(size_t __id) const noexcept {
    return __args_.get(__id);
  }
#  ifndef _LIBCPP_HAS_NO_LOCALIZATION
  _LIBCPP_HIDE_FROM_ABI std::locale locale() {
    if (!__loc_)
      __loc_ = std::locale{};
    return *__loc_;
  }
#  endif
  _LIBCPP_HIDE_FROM_ABI iterator out() { return std::move(__out_it_); }
  _LIBCPP_HIDE_FROM_ABI void advance_to(iterator __it) { __out_it_ = std::move(__it); }

private:
  iterator __out_it_;
  basic_format_args<basic_format_context> __args_;
#  ifndef _LIBCPP_HAS_NO_LOCALIZATION

  // The Standard doesn't specify how the locale is stored.
  // [format.context]/6
  // std::locale locale();
  //   Returns: The locale passed to the formatting function if the latter
  //   takes one, and std::locale() otherwise.
  // This is done by storing the locale of the constructor in this optional. If
  // locale() is called and the optional has no value the value will be created.
  // This allows the implementation to lazily create the locale.
  // TODO FMT Validate whether lazy creation is the best solution.
  optional<std::locale> __loc_;

  template <class _OtherOutIt, class _OtherCharT>
  friend _LIBCPP_HIDE_FROM_ABI basic_format_context<_OtherOutIt, _OtherCharT> __format_context_create(
      _OtherOutIt, basic_format_args<basic_format_context<_OtherOutIt, _OtherCharT>>, optional<std::locale>&&);

  // Note: the Standard doesn't specify the required constructors.
  _LIBCPP_HIDE_FROM_ABI explicit basic_format_context(
      _OutIt __out_it, basic_format_args<basic_format_context> __args, optional<std::locale>&& __loc)
      : __out_it_(std::move(__out_it)), __args_(__args), __loc_(std::move(__loc)) {}
#  else
  template <class _OtherOutIt, class _OtherCharT>
  friend _LIBCPP_HIDE_FROM_ABI basic_format_context<_OtherOutIt, _OtherCharT>
      __format_context_create(_OtherOutIt, basic_format_args<basic_format_context<_OtherOutIt, _OtherCharT>>);

  _LIBCPP_HIDE_FROM_ABI explicit basic_format_context(_OutIt __out_it, basic_format_args<basic_format_context> __args)
      : __out_it_(std::move(__out_it)), __args_(__args) {}
#  endif

  basic_format_context(const basic_format_context&)            = delete;
  basic_format_context& operator=(const basic_format_context&) = delete;
};

// A specialization for __retarget_buffer
//
// See __retarget_buffer for the motivation for this specialization.
//
// This context holds a reference to the instance of the basic_format_context
// that is retargeted. It converts a formatting argument when it is requested
// during formatting. It is expected that the usage of the arguments is rare so
// the lookups are not expected to be used often. An alternative would be to
// convert all elements during construction.
//
// The elements of the retargets context are only used when an underlying
// formatter uses a locale specific formatting or an formatting argument is
// part for the format spec. For example
//   format("{:256:{}}", input, 8);
// Here the width of an element in input is determined dynamically.
// Note when the top-level element has no width the retargeting is not needed.
template <class _CharT>
class _LIBCPP_TEMPLATE_VIS basic_format_context<typename __format::__retarget_buffer<_CharT>::__iterator, _CharT> {
public:
  using iterator  = typename __format::__retarget_buffer<_CharT>::__iterator;
  using char_type = _CharT;
  template <class _Tp>
  using formatter_type = formatter<_Tp, _CharT>;

  template <class _Context>
  _LIBCPP_HIDE_FROM_ABI explicit basic_format_context(iterator __out_it, _Context& __ctx)
      : __out_it_(std::move(__out_it)),
#  ifndef _LIBCPP_HAS_NO_LOCALIZATION
        __loc_([](void* __c) { return static_cast<_Context*>(__c)->locale(); }),
#  endif
        __ctx_(std::addressof(__ctx)),
        __arg_([](void* __c, size_t __id) {
          auto __visitor = [&](auto __arg) -> basic_format_arg<basic_format_context> {
            if constexpr (same_as<decltype(__arg), monostate>)
              return {};
            else if constexpr (same_as<decltype(__arg), typename basic_format_arg<_Context>::handle>)
              // At the moment it's not possible for formatting to use a re-targeted handle.
              // TODO FMT add this when support is needed.
              std::__throw_format_error("Re-targeting handle not supported");
            else
              return basic_format_arg<basic_format_context>{
                  __format::__determine_arg_t<basic_format_context, decltype(__arg)>(),
                  __basic_format_arg_value<basic_format_context>(__arg)};
          };
#  if _LIBCPP_STD_VER >= 26 && defined(_LIBCPP_HAS_EXPLICIT_THIS_PARAMETER)
          return static_cast<_Context*>(__c)->arg(__id).visit(std::move(__visitor));
#  else
          _LIBCPP_SUPPRESS_DEPRECATED_PUSH
          return std::visit_format_arg(std::move(__visitor), static_cast<_Context*>(__c)->arg(__id));
          _LIBCPP_SUPPRESS_DEPRECATED_POP
#  endif // _LIBCPP_STD_VER >= 26 && defined(_LIBCPP_HAS_EXPLICIT_THIS_PARAMETER)
        }) {
  }

  _LIBCPP_HIDE_FROM_ABI basic_format_arg<basic_format_context> arg(size_t __id) const noexcept {
    return __arg_(__ctx_, __id);
  }
#  ifndef _LIBCPP_HAS_NO_LOCALIZATION
  _LIBCPP_HIDE_FROM_ABI std::locale locale() { return __loc_(__ctx_); }
#  endif
  _LIBCPP_HIDE_FROM_ABI iterator out() { return std::move(__out_it_); }
  _LIBCPP_HIDE_FROM_ABI void advance_to(iterator __it) { __out_it_ = std::move(__it); }

private:
  iterator __out_it_;

#  ifndef _LIBCPP_HAS_NO_LOCALIZATION
  std::locale (*__loc_)(void* __ctx);
#  endif

  void* __ctx_;
  basic_format_arg<basic_format_context> (*__arg_)(void* __ctx, size_t __id);
};

_LIBCPP_CTAD_SUPPORTED_FOR_TYPE(basic_format_context);
#endif //_LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___FORMAT_FORMAT_CONTEXT_H
