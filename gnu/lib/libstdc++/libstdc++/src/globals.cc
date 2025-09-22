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

#include "bits/c++config.h"
#include "bits/gthr.h"
#include <fstream>
#include <istream>
#include <ostream>
#include <locale>
#include <ext/stdio_filebuf.h>

// On AIX, and perhaps other systems, library initialization order is
// not guaranteed.  For example, the static initializers for the main
// program might run before the static initializers for this library.
// That means that we cannot rely on static initialization in the
// library; there is no guarantee that things will get initialized in
// time.  This file contains definitions of all global variables that
// require initialization as arrays of characters.

// Because <iostream> declares the standard streams to be [io]stream
// types instead of say [io]fstream types, it is also necessary to
// allocate the actual file buffers in this file.
namespace __gnu_cxx
{
  using namespace std;
 
  typedef char fake_facet_name[sizeof(char*)]
  __attribute__ ((aligned(__alignof__(char*))));
  fake_facet_name facet_name[6 + _GLIBCPP_NUM_CATEGORIES];

  typedef char fake_locale_Impl[sizeof(locale::_Impl)]
  __attribute__ ((aligned(__alignof__(locale::_Impl))));
  fake_locale_Impl c_locale_impl;


  // NB: The asm directives renames these non-exported, namespace
  // __gnu_cxx symbols into the mistakenly exported, namespace std
  // symbols in GLIBCPP_3.2.
  // The rename syntax is 
  //   asm (".symver currentname,oldname@@GLIBCPP_3.2")
  // At the same time, these new __gnu_cxx symbols are not exported.
  // In the future, GLIBCXX_ABI > 5 should remove all uses of
  // _GLIBCPP_ASM_SYMVER in this file.
  typedef char fake_locale[sizeof(locale)]
  __attribute__ ((aligned(__alignof__(locale))));
  fake_locale c_locale;
  _GLIBCPP_ASM_SYMVER(_ZN9__gnu_cxx8c_localeE, _ZSt8c_locale, GLIBCPP_3.2)

  // GLIBCXX_ABI > 5 will not need this symbol at all.
  // It's here just as a placeholder, as the size of this exported
  // object changed. The new symbol is not exported.
  const int o = sizeof(locale::_Impl) - sizeof(char*[_GLIBCPP_NUM_CATEGORIES]);
  typedef char fake_locale_Impl_compat[o]
  __attribute__ ((aligned(__alignof__(o))));
  fake_locale_Impl_compat  c_locale_impl_compat;
  _GLIBCPP_ASM_SYMVER(_ZN9__gnu_cxx20c_locale_impl_compatE, _ZSt13c_locale_impl, GLIBCPP_3.2)

  typedef char fake_facet_vec[sizeof(locale::facet*)]
  __attribute__ ((aligned(__alignof__(locale::facet*))));
  fake_facet_vec facet_vec[_GLIBCPP_NUM_FACETS];
  _GLIBCPP_ASM_SYMVER(_ZN9__gnu_cxx9facet_vecE, _ZSt9facet_vec, GLIBCPP_3.2)

  // To support combined facets and caches in facet array
  typedef char fake_facet_cache_vec[sizeof(locale::facet*)]
  __attribute__ ((aligned(__alignof__(locale::facet*))));
  fake_facet_cache_vec facet_cache_vec[2 * _GLIBCPP_NUM_FACETS];

  typedef char fake_ctype_c[sizeof(std::ctype<char>)]
  __attribute__ ((aligned(__alignof__(std::ctype<char>))));
  fake_ctype_c ctype_c;
  _GLIBCPP_ASM_SYMVER(_ZN9__gnu_cxx7ctype_cE, _ZSt7ctype_c, GLIBCPP_3.2)

  typedef char fake_collate_c[sizeof(std::collate<char>)]
  __attribute__ ((aligned(__alignof__(std::collate<char>))));
  fake_collate_c collate_c;
  _GLIBCPP_ASM_SYMVER(_ZN9__gnu_cxx9collate_cE, _ZSt9collate_c, GLIBCPP_3.2)

  typedef char fake_numpunct_c[sizeof(numpunct<char>)]
  __attribute__ ((aligned(__alignof__(numpunct<char>))));
  fake_numpunct_c numpunct_c;
  _GLIBCPP_ASM_SYMVER(_ZN9__gnu_cxx10numpunct_cE, _ZSt10numpunct_c, GLIBCPP_3.2)

  typedef char fake_num_get_c[sizeof(num_get<char>)]
  __attribute__ ((aligned(__alignof__(num_get<char>))));
  fake_num_get_c num_get_c;
  _GLIBCPP_ASM_SYMVER(_ZN9__gnu_cxx9num_get_cE, _ZSt9num_get_c, GLIBCPP_3.2)

  typedef char fake_num_put_c[sizeof(num_put<char>)]
  __attribute__ ((aligned(__alignof__(num_put<char>))));
  fake_num_put_c num_put_c;
  _GLIBCPP_ASM_SYMVER(_ZN9__gnu_cxx9num_put_cE, _ZSt9num_put_c, GLIBCPP_3.2)

  typedef char fake_codecvt_c[sizeof(codecvt<char, char, mbstate_t>)]
  __attribute__ ((aligned(__alignof__(codecvt<char, char, mbstate_t>))));
  fake_codecvt_c codecvt_c;
  _GLIBCPP_ASM_SYMVER(_ZN9__gnu_cxx9codecvt_cE, _ZSt9codecvt_c, GLIBCPP_3.2)

  typedef char fake_moneypunct_c[sizeof(moneypunct<char, true>)]
  __attribute__ ((aligned(__alignof__(moneypunct<char, true>))));
  fake_moneypunct_c moneypunct_tc;
  fake_moneypunct_c moneypunct_fc;
  _GLIBCPP_ASM_SYMVER(_ZN9__gnu_cxx13moneypunct_tcE,\
        _ZSt13moneypunct_tc, GLIBCPP_3.2)
  _GLIBCPP_ASM_SYMVER(_ZN9__gnu_cxx13moneypunct_fcE,\
        _ZSt13moneypunct_fc, GLIBCPP_3.2)

  typedef char fake_money_get_c[sizeof(money_get<char>)]
  __attribute__ ((aligned(__alignof__(money_get<char>))));
  fake_money_get_c money_get_c;
  _GLIBCPP_ASM_SYMVER(_ZN9__gnu_cxx11money_get_cE, _ZSt11money_get_c, GLIBCPP_3.2)
  
  typedef char fake_money_put_c[sizeof(money_put<char>)]
  __attribute__ ((aligned(__alignof__(money_put<char>))));
  fake_money_put_c money_put_c;
  _GLIBCPP_ASM_SYMVER(_ZN9__gnu_cxx11money_put_cE, _ZSt11money_put_c, GLIBCPP_3.2)

  typedef char fake_timepunct_c[sizeof(__timepunct<char>)]
  __attribute__ ((aligned(__alignof__(__timepunct<char>))));
  fake_timepunct_c timepunct_c;
  _GLIBCPP_ASM_SYMVER(_ZN9__gnu_cxx11timepunct_cE, _ZSt11timepunct_c, GLIBCPP_3.2)

  typedef char fake_time_get_c[sizeof(time_get<char>)]
  __attribute__ ((aligned(__alignof__(time_get<char>))));
  fake_time_get_c time_get_c;
  _GLIBCPP_ASM_SYMVER(_ZN9__gnu_cxx10time_get_cE, _ZSt10time_get_c, GLIBCPP_3.2)

  typedef char fake_time_put_c[sizeof(time_put<char>)]
  __attribute__ ((aligned(__alignof__(time_put<char>))));
  fake_time_put_c time_put_c;
  _GLIBCPP_ASM_SYMVER(_ZN9__gnu_cxx10time_put_cE, _ZSt10time_put_c, GLIBCPP_3.2)

  typedef char fake_messages_c[sizeof(messages<char>)]
  __attribute__ ((aligned(__alignof__(messages<char>))));
  fake_messages_c messages_c;
  _GLIBCPP_ASM_SYMVER(_ZN9__gnu_cxx10messages_cE, _ZSt10messages_c, GLIBCPP_3.2)

#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  typedef char fake_wtype_w[sizeof(std::ctype<wchar_t>)]
  __attribute__ ((aligned(__alignof__(std::ctype<wchar_t>))));
  fake_wtype_w ctype_w;
  _GLIBCPP_ASM_SYMVER(_ZN9__gnu_cxx7ctype_wE, _ZSt7ctype_w, GLIBCPP_3.2)

  typedef char fake_wollate_w[sizeof(std::collate<wchar_t>)]
  __attribute__ ((aligned(__alignof__(std::collate<wchar_t>))));
  fake_wollate_w collate_w;
  _GLIBCPP_ASM_SYMVER(_ZN9__gnu_cxx9collate_wE, _ZSt9collate_w, GLIBCPP_3.2)

  typedef char fake_numpunct_w[sizeof(numpunct<wchar_t>)]
  __attribute__ ((aligned(__alignof__(numpunct<wchar_t>))));
  fake_numpunct_w numpunct_w;
  _GLIBCPP_ASM_SYMVER(_ZN9__gnu_cxx10numpunct_wE, _ZSt10numpunct_w, GLIBCPP_3.2)

  typedef char fake_num_get_w[sizeof(num_get<wchar_t>)]
  __attribute__ ((aligned(__alignof__(num_get<wchar_t>))));
  fake_num_get_w num_get_w;
  _GLIBCPP_ASM_SYMVER(_ZN9__gnu_cxx9num_get_wE, _ZSt9num_get_w, GLIBCPP_3.2)

  typedef char fake_num_put_w[sizeof(num_put<wchar_t>)]
  __attribute__ ((aligned(__alignof__(num_put<wchar_t>))));
  fake_num_put_w num_put_w;
  _GLIBCPP_ASM_SYMVER(_ZN9__gnu_cxx9num_put_wE, _ZSt9num_put_w, GLIBCPP_3.2)

  typedef char fake_wodecvt_w[sizeof(codecvt<wchar_t, char, mbstate_t>)]
  __attribute__ ((aligned(__alignof__(codecvt<wchar_t, char, mbstate_t>))));
  fake_wodecvt_w codecvt_w;
  _GLIBCPP_ASM_SYMVER(_ZN9__gnu_cxx9codecvt_wE, _ZSt9codecvt_w, GLIBCPP_3.2)

  typedef char fake_moneypunct_w[sizeof(moneypunct<wchar_t, true>)]
  __attribute__ ((aligned(__alignof__(moneypunct<wchar_t, true>))));
  fake_moneypunct_w moneypunct_tw;
  fake_moneypunct_w moneypunct_fw;
  _GLIBCPP_ASM_SYMVER(_ZN9__gnu_cxx13moneypunct_twE,\
        _ZSt13moneypunct_tw, GLIBCPP_3.2)
  _GLIBCPP_ASM_SYMVER(_ZN9__gnu_cxx13moneypunct_fwE,\
        _ZSt13moneypunct_fw, GLIBCPP_3.2)

  typedef char fake_money_get_w[sizeof(money_get<wchar_t>)]
  __attribute__ ((aligned(__alignof__(money_get<wchar_t>))));
  fake_money_get_w money_get_w;
  _GLIBCPP_ASM_SYMVER(_ZN9__gnu_cxx11money_get_wE, _ZSt11money_get_w, GLIBCPP_3.2)
  
  typedef char fake_money_put_w[sizeof(money_put<wchar_t>)]
  __attribute__ ((aligned(__alignof__(money_put<wchar_t>))));
  fake_money_put_w money_put_w;
  _GLIBCPP_ASM_SYMVER(_ZN9__gnu_cxx11money_put_wE, _ZSt11money_put_w, GLIBCPP_3.2)

  typedef char fake_timepunct_w[sizeof(__timepunct<wchar_t>)]
  __attribute__ ((aligned(__alignof__(__timepunct<wchar_t>))));
  fake_timepunct_w timepunct_w;
  _GLIBCPP_ASM_SYMVER(_ZN9__gnu_cxx11timepunct_wE, _ZSt11timepunct_w, GLIBCPP_3.2)

  typedef char fake_time_get_w[sizeof(time_get<wchar_t>)]
  __attribute__ ((aligned(__alignof__(time_get<wchar_t>))));
  fake_time_get_w time_get_w;
  _GLIBCPP_ASM_SYMVER(_ZN9__gnu_cxx10time_get_wE, _ZSt10time_get_w, GLIBCPP_3.2)

  typedef char fake_time_put_w[sizeof(time_put<wchar_t>)]
  __attribute__ ((aligned(__alignof__(time_put<wchar_t>))));
  fake_time_put_w time_put_w;
  _GLIBCPP_ASM_SYMVER(_ZN9__gnu_cxx10time_put_wE, _ZSt10time_put_w, GLIBCPP_3.2)

  typedef char fake_messages_w[sizeof(messages<wchar_t>)]
  __attribute__ ((aligned(__alignof__(messages<wchar_t>))));
  fake_messages_w messages_w;
  _GLIBCPP_ASM_SYMVER(_ZN9__gnu_cxx10messages_wE, _ZSt10messages_w, GLIBCPP_3.2)
#endif

  // Storage for static C locale caches
  typedef char fake_locale_cache_np_c[sizeof(std::__locale_cache<numpunct<char> >)]
  __attribute__ ((aligned(__alignof__(std::__locale_cache<numpunct<char> >))));
  fake_locale_cache_np_c locale_cache_np_c;

#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  typedef char fake_locale_cache_np_w[sizeof(std::__locale_cache<numpunct<wchar_t> >)]
  __attribute__ ((aligned(__alignof__(std::__locale_cache<numpunct<wchar_t> >))));
  fake_locale_cache_np_w locale_cache_np_w;
#endif

  typedef char fake_filebuf[sizeof(stdio_filebuf<char>)]
  __attribute__ ((aligned(__alignof__(stdio_filebuf<char>))));
  fake_filebuf buf_cout;
  fake_filebuf buf_cin;
  fake_filebuf buf_cerr;
  _GLIBCPP_ASM_SYMVER(_ZN9__gnu_cxx8buf_coutE, _ZSt8buf_cout, GLIBCPP_3.2)
  _GLIBCPP_ASM_SYMVER(_ZN9__gnu_cxx7buf_cinE, _ZSt7buf_cin, GLIBCPP_3.2)
  _GLIBCPP_ASM_SYMVER(_ZN9__gnu_cxx8buf_cerrE, _ZSt8buf_cerr, GLIBCPP_3.2)

#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  typedef char fake_wfilebuf[sizeof(stdio_filebuf<wchar_t>)]
  __attribute__ ((aligned(__alignof__(stdio_filebuf<wchar_t>))));
  fake_wfilebuf buf_wcout;
  fake_wfilebuf buf_wcin;
  fake_wfilebuf buf_wcerr;
  _GLIBCPP_ASM_SYMVER(_ZN9__gnu_cxx9buf_wcoutE, _ZSt9buf_wcout, GLIBCPP_3.2)
  _GLIBCPP_ASM_SYMVER(_ZN9__gnu_cxx8buf_wcinE, _ZSt8buf_wcin, GLIBCPP_3.2)
  _GLIBCPP_ASM_SYMVER(_ZN9__gnu_cxx9buf_wcerrE, _ZSt9buf_wcerr, GLIBCPP_3.2)
#endif

  // Globals for once-only runtime initialization of mutex objects.  This
  // allows static initialization of these objects on systems that need a
  // function call to initialize a mutex.  For example, see stl_threads.h.
#ifdef __GTHREAD_MUTEX_INIT
  // Need to provide explicit instantiations of static data for
  // systems with broken weak linkage support.
  template __gthread_mutex_t _Swap_lock_struct<0>::_S_swap_lock;
#elif defined(__GTHREAD_MUTEX_INIT_FUNCTION)
  __gthread_once_t _GLIBCPP_once = __GTHREAD_ONCE_INIT;
  __gthread_mutex_t _GLIBCPP_mutex;
  __gthread_mutex_t *_GLIBCPP_mutex_address;
  
  // Once-only initializer function for _GLIBCPP_mutex.  
  void
  _GLIBCPP_mutex_init ()
  { __GTHREAD_MUTEX_INIT_FUNCTION (&_GLIBCPP_mutex); }

  // Once-only initializer function for _GLIBCPP_mutex_address.  
  void
  _GLIBCPP_mutex_address_init ()
  { __GTHREAD_MUTEX_INIT_FUNCTION (_GLIBCPP_mutex_address); }
#endif

  // GLIBCXX_ABI.
  struct __compat
  {
    static const char _S_atoms[];
  };
  const char __compat::_S_atoms[] = "0123456789eEabcdfABCDF";
  _GLIBCPP_ASM_SYMVER(_ZN9__gnu_cxx8__compat8_S_atomsE, _ZNSt10__num_base8_S_atomsE, GLIBCPP_3.2)
} // namespace __gnu_cxx

namespace std
{
  // Standard stream objects.
  typedef char fake_istream[sizeof(istream)]
  __attribute__ ((aligned(__alignof__(istream))));
  typedef char fake_ostream[sizeof(ostream)] 
  __attribute__ ((aligned(__alignof__(ostream))));
  fake_istream cin;
  fake_ostream cout;
  fake_ostream cerr;
  fake_ostream clog;

#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  typedef char fake_wistream[sizeof(wistream)] 
  __attribute__ ((aligned(__alignof__(wistream))));
  typedef char fake_wostream[sizeof(wostream)] 
  __attribute__ ((aligned(__alignof__(wostream))));
  fake_wistream wcin;
  fake_wostream wcout;
  fake_wostream wcerr;
  fake_wostream wclog;
#endif
} // namespace std
