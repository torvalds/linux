//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "std_stream.h"
#include <__locale>
#include <new>
#include <string>

#ifdef _LIBCPP_MSVCRT_LIKE
#  include <__locale_dir/locale_base_api/locale_guard.h>
#endif

#define _str(s) #s
#define str(s) _str(s)
#define _LIBCPP_ABI_NAMESPACE_STR str(_LIBCPP_ABI_NAMESPACE)

_LIBCPP_BEGIN_NAMESPACE_STD

alignas(istream) _LIBCPP_EXPORTED_FROM_ABI char cin[sizeof(istream)]
#if defined(_LIBCPP_ABI_MICROSOFT) && defined(__clang__)
    __asm__("?cin@" _LIBCPP_ABI_NAMESPACE_STR "@std@@3V?$basic_istream@DU?$char_traits@D@" _LIBCPP_ABI_NAMESPACE_STR
            "@std@@@12@A")
#endif
        ;
alignas(__stdinbuf<char>) static char __cin[sizeof(__stdinbuf<char>)];
static mbstate_t mb_cin;

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
alignas(wistream) _LIBCPP_EXPORTED_FROM_ABI char wcin[sizeof(wistream)]
#  if defined(_LIBCPP_ABI_MICROSOFT) && defined(__clang__)
    __asm__("?wcin@" _LIBCPP_ABI_NAMESPACE_STR "@std@@3V?$basic_istream@_WU?$char_traits@_W@" _LIBCPP_ABI_NAMESPACE_STR
            "@std@@@12@A")
#  endif
        ;
alignas(__stdinbuf<wchar_t>) static char __wcin[sizeof(__stdinbuf<wchar_t>)];
static mbstate_t mb_wcin;
#endif // _LIBCPP_HAS_NO_WIDE_CHARACTERS

alignas(ostream) _LIBCPP_EXPORTED_FROM_ABI char cout[sizeof(ostream)]
#if defined(_LIBCPP_ABI_MICROSOFT) && defined(__clang__)
    __asm__("?cout@" _LIBCPP_ABI_NAMESPACE_STR "@std@@3V?$basic_ostream@DU?$char_traits@D@" _LIBCPP_ABI_NAMESPACE_STR
            "@std@@@12@A")
#endif
        ;
alignas(__stdoutbuf<char>) static char __cout[sizeof(__stdoutbuf<char>)];
static mbstate_t mb_cout;

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
alignas(wostream) _LIBCPP_EXPORTED_FROM_ABI char wcout[sizeof(wostream)]
#  if defined(_LIBCPP_ABI_MICROSOFT) && defined(__clang__)
    __asm__("?wcout@" _LIBCPP_ABI_NAMESPACE_STR "@std@@3V?$basic_ostream@_WU?$char_traits@_W@" _LIBCPP_ABI_NAMESPACE_STR
            "@std@@@12@A")
#  endif
        ;
alignas(__stdoutbuf<wchar_t>) static char __wcout[sizeof(__stdoutbuf<wchar_t>)];
static mbstate_t mb_wcout;
#endif // _LIBCPP_HAS_NO_WIDE_CHARACTERS

alignas(ostream) _LIBCPP_EXPORTED_FROM_ABI char cerr[sizeof(ostream)]
#if defined(_LIBCPP_ABI_MICROSOFT) && defined(__clang__)
    __asm__("?cerr@" _LIBCPP_ABI_NAMESPACE_STR "@std@@3V?$basic_ostream@DU?$char_traits@D@" _LIBCPP_ABI_NAMESPACE_STR
            "@std@@@12@A")
#endif
        ;
alignas(__stdoutbuf<char>) static char __cerr[sizeof(__stdoutbuf<char>)];
static mbstate_t mb_cerr;

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
alignas(wostream) _LIBCPP_EXPORTED_FROM_ABI char wcerr[sizeof(wostream)]
#  if defined(_LIBCPP_ABI_MICROSOFT) && defined(__clang__)
    __asm__("?wcerr@" _LIBCPP_ABI_NAMESPACE_STR "@std@@3V?$basic_ostream@_WU?$char_traits@_W@" _LIBCPP_ABI_NAMESPACE_STR
            "@std@@@12@A")
#  endif
        ;
alignas(__stdoutbuf<wchar_t>) static char __wcerr[sizeof(__stdoutbuf<wchar_t>)];
static mbstate_t mb_wcerr;
#endif // _LIBCPP_HAS_NO_WIDE_CHARACTERS

alignas(ostream) _LIBCPP_EXPORTED_FROM_ABI char clog[sizeof(ostream)]
#if defined(_LIBCPP_ABI_MICROSOFT) && defined(__clang__)
    __asm__("?clog@" _LIBCPP_ABI_NAMESPACE_STR "@std@@3V?$basic_ostream@DU?$char_traits@D@" _LIBCPP_ABI_NAMESPACE_STR
            "@std@@@12@A")
#endif
        ;

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
alignas(wostream) _LIBCPP_EXPORTED_FROM_ABI char wclog[sizeof(wostream)]
#  if defined(_LIBCPP_ABI_MICROSOFT) && defined(__clang__)
    __asm__("?wclog@" _LIBCPP_ABI_NAMESPACE_STR "@std@@3V?$basic_ostream@_WU?$char_traits@_W@" _LIBCPP_ABI_NAMESPACE_STR
            "@std@@@12@A")
#  endif
        ;
#endif // _LIBCPP_HAS_NO_WIDE_CHARACTERS

// Pretend we're inside a system header so the compiler doesn't flag the use of the init_priority
// attribute with a value that's reserved for the implementation (we're the implementation).
#include "iostream_init.h"

// On Windows the TLS storage for locales needs to be initialized before we create
// the standard streams, otherwise it may not be alive during program termination
// when we flush the streams.
static void force_locale_initialization() {
#if defined(_LIBCPP_MSVCRT_LIKE)
  static bool once = []() {
    auto loc = newlocale(LC_ALL_MASK, "C", 0);
    {
      __libcpp_locale_guard g(loc); // forces initialization of locale TLS
      ((void)g);
    }
    freelocale(loc);
    return true;
  }();
  ((void)once);
#endif
}

class DoIOSInit {
public:
  DoIOSInit();
  ~DoIOSInit();
};

DoIOSInit::DoIOSInit() {
  force_locale_initialization();

  istream* cin_ptr  = ::new (cin) istream(::new (__cin) __stdinbuf<char>(stdin, &mb_cin));
  ostream* cout_ptr = ::new (cout) ostream(::new (__cout) __stdoutbuf<char>(stdout, &mb_cout));
  ostream* cerr_ptr = ::new (cerr) ostream(::new (__cerr) __stdoutbuf<char>(stderr, &mb_cerr));
  ::new (clog) ostream(cerr_ptr->rdbuf());
  cin_ptr->tie(cout_ptr);
  std::unitbuf(*cerr_ptr);
  cerr_ptr->tie(cout_ptr);

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  wistream* wcin_ptr  = ::new (wcin) wistream(::new (__wcin) __stdinbuf<wchar_t>(stdin, &mb_wcin));
  wostream* wcout_ptr = ::new (wcout) wostream(::new (__wcout) __stdoutbuf<wchar_t>(stdout, &mb_wcout));
  wostream* wcerr_ptr = ::new (wcerr) wostream(::new (__wcerr) __stdoutbuf<wchar_t>(stderr, &mb_wcerr));
  ::new (wclog) wostream(wcerr_ptr->rdbuf());

  wcin_ptr->tie(wcout_ptr);
  std::unitbuf(*wcerr_ptr);
  wcerr_ptr->tie(wcout_ptr);
#endif
}

DoIOSInit::~DoIOSInit() {
  ostream* cout_ptr = reinterpret_cast<ostream*>(cout);
  cout_ptr->flush();
  ostream* clog_ptr = reinterpret_cast<ostream*>(clog);
  clog_ptr->flush();

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  wostream* wcout_ptr = reinterpret_cast<wostream*>(wcout);
  wcout_ptr->flush();
  wostream* wclog_ptr = reinterpret_cast<wostream*>(wclog);
  wclog_ptr->flush();
#endif
}

ios_base::Init::Init() {
  static DoIOSInit init_the_streams; // gets initialized once
}

ios_base::Init::~Init() {}

_LIBCPP_END_NAMESPACE_STD
