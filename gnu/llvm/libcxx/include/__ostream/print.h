//===---------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//

#ifndef _LIBCPP___OSTREAM_PRINT_H
#define _LIBCPP___OSTREAM_PRINT_H

#include <__config>
#include <__fwd/ostream.h>
#include <__iterator/ostreambuf_iterator.h>
#include <__ostream/basic_ostream.h>
#include <format>
#include <ios>
#include <locale>
#include <print>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 23

template <class = void> // TODO PRINT template or availability markup fires too eagerly (http://llvm.org/PR61563).
_LIBCPP_HIDE_FROM_ABI inline void
__vprint_nonunicode(ostream& __os, string_view __fmt, format_args __args, bool __write_nl) {
  // [ostream.formatted.print]/3
  // Effects: Behaves as a formatted output function
  // ([ostream.formatted.reqmts]) of os, except that:
  // - failure to generate output is reported as specified below, and
  // - any exception thrown by the call to vformat is propagated without regard
  //   to the value of os.exceptions() and without turning on ios_base::badbit
  //   in the error state of os.
  // After constructing a sentry object, the function initializes an automatic
  // variable via
  //   string out = vformat(os.getloc(), fmt, args);

  ostream::sentry __s(__os);
  if (__s) {
    string __o = std::vformat(__os.getloc(), __fmt, __args);
    if (__write_nl)
      __o += '\n';

    const char* __str = __o.data();
    size_t __len      = __o.size();

#  ifndef _LIBCPP_HAS_NO_EXCEPTIONS
    try {
#  endif // _LIBCPP_HAS_NO_EXCEPTIONS
      typedef ostreambuf_iterator<char> _Ip;
      if (std::__pad_and_output(
              _Ip(__os),
              __str,
              (__os.flags() & ios_base::adjustfield) == ios_base::left ? __str + __len : __str,
              __str + __len,
              __os,
              __os.fill())
              .failed())
        __os.setstate(ios_base::badbit | ios_base::failbit);

#  ifndef _LIBCPP_HAS_NO_EXCEPTIONS
    } catch (...) {
      __os.__set_badbit_and_consider_rethrow();
    }
#  endif // _LIBCPP_HAS_NO_EXCEPTIONS
  }
}

template <class = void> // TODO PRINT template or availability markup fires too eagerly (http://llvm.org/PR61563).
_LIBCPP_HIDE_FROM_ABI inline void vprint_nonunicode(ostream& __os, string_view __fmt, format_args __args) {
  std::__vprint_nonunicode(__os, __fmt, __args, false);
}

// Returns the FILE* associated with the __os.
// Returns a nullptr when no FILE* is associated with __os.
// This function is in the dylib since the type of the buffer associated
// with std::cout, std::cerr, and std::clog is only known in the dylib.
//
// This function implements part of the implementation-defined behavior
// of [ostream.formatted.print]/3
//   If the function is vprint_unicode and os is a stream that refers to
//   a terminal capable of displaying Unicode which is determined in an
//   implementation-defined manner, writes out to the terminal using the
//   native Unicode API;
// Whether the returned FILE* is "a terminal capable of displaying Unicode"
// is determined in the same way as the print(FILE*, ...) overloads.
_LIBCPP_EXPORTED_FROM_ABI FILE* __get_ostream_file(ostream& __os);

#  ifndef _LIBCPP_HAS_NO_UNICODE
template <class = void> // TODO PRINT template or availability markup fires too eagerly (http://llvm.org/PR61563).
_LIBCPP_HIDE_FROM_ABI void __vprint_unicode(ostream& __os, string_view __fmt, format_args __args, bool __write_nl) {
#    if _LIBCPP_AVAILABILITY_HAS_PRINT == 0
  return std::__vprint_nonunicode(__os, __fmt, __args, __write_nl);
#    else
  FILE* __file = std::__get_ostream_file(__os);
  if (!__file || !__print::__is_terminal(__file))
    return std::__vprint_nonunicode(__os, __fmt, __args, __write_nl);

  // [ostream.formatted.print]/3
  //    If the function is vprint_unicode and os is a stream that refers to a
  //    terminal capable of displaying Unicode which is determined in an
  //    implementation-defined manner, writes out to the terminal using the
  //    native Unicode API; if out contains invalid code units, the behavior is
  //    undefined and implementations are encouraged to diagnose it. If the
  //    native Unicode API is used, the function flushes os before writing out.
  //
  // This is the path for the native API, start with flushing.
  __os.flush();

#      ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  try {
#      endif // _LIBCPP_HAS_NO_EXCEPTIONS
    ostream::sentry __s(__os);
    if (__s) {
#      ifndef _LIBCPP_WIN32API
      __print::__vprint_unicode_posix(__file, __fmt, __args, __write_nl, true);
#      elif !defined(_LIBCPP_HAS_NO_WIDE_CHARACTERS)
    __print::__vprint_unicode_windows(__file, __fmt, __args, __write_nl, true);
#      else
#        error "Windows builds with wchar_t disabled are not supported."
#      endif
    }

#      ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  } catch (...) {
    __os.__set_badbit_and_consider_rethrow();
  }
#      endif // _LIBCPP_HAS_NO_EXCEPTIONS
#    endif   // _LIBCPP_AVAILABILITY_HAS_PRINT
}

template <class = void> // TODO PRINT template or availability markup fires too eagerly (http://llvm.org/PR61563).
_LIBCPP_HIDE_FROM_ABI inline void vprint_unicode(ostream& __os, string_view __fmt, format_args __args) {
  std::__vprint_unicode(__os, __fmt, __args, false);
}
#  endif // _LIBCPP_HAS_NO_UNICODE

template <class... _Args>
_LIBCPP_HIDE_FROM_ABI void print(ostream& __os, format_string<_Args...> __fmt, _Args&&... __args) {
#  ifndef _LIBCPP_HAS_NO_UNICODE
  if constexpr (__print::__use_unicode_execution_charset)
    std::__vprint_unicode(__os, __fmt.get(), std::make_format_args(__args...), false);
  else
    std::__vprint_nonunicode(__os, __fmt.get(), std::make_format_args(__args...), false);
#  else  // _LIBCPP_HAS_NO_UNICODE
  std::__vprint_nonunicode(__os, __fmt.get(), std::make_format_args(__args...), false);
#  endif // _LIBCPP_HAS_NO_UNICODE
}

template <class... _Args>
_LIBCPP_HIDE_FROM_ABI void println(ostream& __os, format_string<_Args...> __fmt, _Args&&... __args) {
#  ifndef _LIBCPP_HAS_NO_UNICODE
  // Note the wording in the Standard is inefficient. The output of
  // std::format is a std::string which is then copied. This solution
  // just appends a newline at the end of the output.
  if constexpr (__print::__use_unicode_execution_charset)
    std::__vprint_unicode(__os, __fmt.get(), std::make_format_args(__args...), true);
  else
    std::__vprint_nonunicode(__os, __fmt.get(), std::make_format_args(__args...), true);
#  else  // _LIBCPP_HAS_NO_UNICODE
  std::__vprint_nonunicode(__os, __fmt.get(), std::make_format_args(__args...), true);
#  endif // _LIBCPP_HAS_NO_UNICODE
}

template <class = void> // TODO PRINT template or availability markup fires too eagerly (http://llvm.org/PR61563).
_LIBCPP_HIDE_FROM_ABI inline void println(ostream& __os) {
  std::print(__os, "\n");
}

#endif // _LIBCPP_STD_VER >= 23

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___OSTREAM_PRINT_H
