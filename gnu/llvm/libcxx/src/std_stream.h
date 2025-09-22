// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP_STD_STREAM_H
#define _LIBCPP_STD_STREAM_H

#include <__config>
#include <__locale>
#include <cstdio>
#include <istream>
#include <ostream>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

static const int __limit = 8;

// __stdinbuf

template <class _CharT>
class _LIBCPP_HIDDEN __stdinbuf : public basic_streambuf<_CharT, char_traits<_CharT> > {
public:
  typedef _CharT char_type;
  typedef char_traits<char_type> traits_type;
  typedef typename traits_type::int_type int_type;
  typedef typename traits_type::pos_type pos_type;
  typedef typename traits_type::off_type off_type;
  typedef typename traits_type::state_type state_type;

  __stdinbuf(FILE* __fp, state_type* __st);

protected:
  virtual int_type underflow();
  virtual int_type uflow();
  virtual int_type pbackfail(int_type __c = traits_type::eof());
  virtual void imbue(const locale& __loc);

private:
  FILE* __file_;
  const codecvt<char_type, char, state_type>* __cv_;
  state_type* __st_;
  int __encoding_;
  int_type __last_consumed_;
  bool __last_consumed_is_next_;
  bool __always_noconv_;

#if defined(_LIBCPP_WIN32API)
  static constexpr bool __is_win32api_wide_char = !is_same_v<_CharT, char>;
#else
  static constexpr bool __is_win32api_wide_char = false;
#endif

  __stdinbuf(const __stdinbuf&);
  __stdinbuf& operator=(const __stdinbuf&);

  int_type __getchar(bool __consume);
};

template <class _CharT>
__stdinbuf<_CharT>::__stdinbuf(FILE* __fp, state_type* __st)
    : __file_(__fp), __st_(__st), __last_consumed_(traits_type::eof()), __last_consumed_is_next_(false) {
  imbue(this->getloc());
  // On Windows, in wchar_t mode, ignore the codecvt from the locale by
  // default and assume noconv; this passes wchar_t through unmodified from
  // getwc. If the user sets a custom locale with imbue(), that gets honored,
  // the IO is done with getc() and converted with the provided codecvt.
  if constexpr (__is_win32api_wide_char)
    __always_noconv_ = true;
}

template <class _CharT>
void __stdinbuf<_CharT>::imbue(const locale& __loc) {
  __cv_            = &use_facet<codecvt<char_type, char, state_type> >(__loc);
  __encoding_      = __cv_->encoding();
  __always_noconv_ = __cv_->always_noconv();
  if (__encoding_ > __limit)
    __throw_runtime_error("unsupported locale for standard input");
}

template <class _CharT>
typename __stdinbuf<_CharT>::int_type __stdinbuf<_CharT>::underflow() {
  return __getchar(false);
}

template <class _CharT>
typename __stdinbuf<_CharT>::int_type __stdinbuf<_CharT>::uflow() {
  return __getchar(true);
}

inline bool __do_getc(FILE* __fp, char* __pbuf) {
  int __c = getc(__fp);
  if (__c == EOF)
    return false;
  *__pbuf = static_cast<char>(__c);
  return true;
}
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
inline bool __do_getc(FILE* __fp, wchar_t* __pbuf) {
  wint_t __c = getwc(__fp);
  if (__c == WEOF)
    return false;
  *__pbuf = static_cast<wchar_t>(__c);
  return true;
}
#endif

inline bool __do_ungetc(int __c, FILE* __fp, char __dummy) {
  if (ungetc(__c, __fp) == EOF)
    return false;
  return true;
}
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
inline bool __do_ungetc(std::wint_t __c, FILE* __fp, wchar_t __dummy) {
  if (ungetwc(__c, __fp) == WEOF)
    return false;
  return true;
}
#endif

template <class _CharT>
typename __stdinbuf<_CharT>::int_type __stdinbuf<_CharT>::__getchar(bool __consume) {
  if (__last_consumed_is_next_) {
    int_type __result = __last_consumed_;
    if (__consume) {
      __last_consumed_         = traits_type::eof();
      __last_consumed_is_next_ = false;
    }
    return __result;
  }
  if (__always_noconv_) {
    char_type __1buf;
    if (!__do_getc(__file_, &__1buf))
      return traits_type::eof();
    if (!__consume) {
      if (!__do_ungetc(traits_type::to_int_type(__1buf), __file_, __1buf))
        return traits_type::eof();
    } else
      __last_consumed_ = traits_type::to_int_type(__1buf);
    return traits_type::to_int_type(__1buf);
  }

  char __extbuf[__limit];
  int __nread = std::max(1, __encoding_);
  for (int __i = 0; __i < __nread; ++__i) {
    int __c = getc(__file_);
    if (__c == EOF)
      return traits_type::eof();
    __extbuf[__i] = static_cast<char>(__c);
  }
  char_type __1buf;
  const char* __enxt;
  char_type* __inxt;
  codecvt_base::result __r;
  do {
    state_type __sv_st = *__st_;
    __r                = __cv_->in(*__st_, __extbuf, __extbuf + __nread, __enxt, &__1buf, &__1buf + 1, __inxt);
    switch (__r) {
    case std::codecvt_base::ok:
      break;
    case codecvt_base::partial:
      *__st_ = __sv_st;
      if (__nread == sizeof(__extbuf))
        return traits_type::eof();
      {
        int __c = getc(__file_);
        if (__c == EOF)
          return traits_type::eof();
        __extbuf[__nread] = static_cast<char>(__c);
      }
      ++__nread;
      break;
    case codecvt_base::error:
      return traits_type::eof();
    case std::codecvt_base::noconv:
      __1buf = static_cast<char_type>(__extbuf[0]);
      break;
    }
  } while (__r == std::codecvt_base::partial);
  if (!__consume) {
    for (int __i = __nread; __i > 0;) {
      if (ungetc(traits_type::to_int_type(__extbuf[--__i]), __file_) == EOF)
        return traits_type::eof();
    }
  } else
    __last_consumed_ = traits_type::to_int_type(__1buf);
  return traits_type::to_int_type(__1buf);
}

template <class _CharT>
typename __stdinbuf<_CharT>::int_type __stdinbuf<_CharT>::pbackfail(int_type __c) {
  if (traits_type::eq_int_type(__c, traits_type::eof())) {
    if (!__last_consumed_is_next_) {
      __c                      = __last_consumed_;
      __last_consumed_is_next_ = !traits_type::eq_int_type(__last_consumed_, traits_type::eof());
    }
    return __c;
  }
  if (__always_noconv_ && __last_consumed_is_next_) {
    if (!__do_ungetc(__last_consumed_, __file_, traits_type::to_char_type(__last_consumed_)))
      return traits_type::eof();
  } else if (__last_consumed_is_next_) {
    char __extbuf[__limit];
    char* __enxt;
    const char_type __ci = traits_type::to_char_type(__last_consumed_);
    const char_type* __inxt;
    switch (__cv_->out(*__st_, &__ci, &__ci + 1, __inxt, __extbuf, __extbuf + sizeof(__extbuf), __enxt)) {
    case std::codecvt_base::ok:
      break;
    case std::codecvt_base::noconv:
      __extbuf[0] = static_cast<char>(__last_consumed_);
      __enxt      = __extbuf + 1;
      break;
    case codecvt_base::partial:
    case codecvt_base::error:
      return traits_type::eof();
    }
    while (__enxt > __extbuf)
      if (ungetc(*--__enxt, __file_) == EOF)
        return traits_type::eof();
  }
  __last_consumed_         = __c;
  __last_consumed_is_next_ = true;
  return __c;
}

// __stdoutbuf

template <class _CharT>
class _LIBCPP_HIDDEN __stdoutbuf : public basic_streambuf<_CharT, char_traits<_CharT> > {
public:
  typedef _CharT char_type;
  typedef char_traits<char_type> traits_type;
  typedef typename traits_type::int_type int_type;
  typedef typename traits_type::pos_type pos_type;
  typedef typename traits_type::off_type off_type;
  typedef typename traits_type::state_type state_type;

  __stdoutbuf(FILE* __fp, state_type* __st);

protected:
  virtual int_type overflow(int_type __c = traits_type::eof());
  virtual streamsize xsputn(const char_type* __s, streamsize __n);
  virtual int sync();
  virtual void imbue(const locale& __loc);

private:
  FILE* __file_;
  const codecvt<char_type, char, state_type>* __cv_;
  state_type* __st_;
  bool __always_noconv_;

#if defined(_LIBCPP_WIN32API)
  static constexpr bool __is_win32api_wide_char = !is_same_v<_CharT, char>;
#else
  static constexpr bool __is_win32api_wide_char = false;
#endif

  __stdoutbuf(const __stdoutbuf&);
  __stdoutbuf& operator=(const __stdoutbuf&);

  _LIBCPP_EXPORTED_FROM_ABI friend FILE* __get_ostream_file(ostream&);
};

template <class _CharT>
__stdoutbuf<_CharT>::__stdoutbuf(FILE* __fp, state_type* __st)
    : __file_(__fp),
      __cv_(&use_facet<codecvt<char_type, char, state_type> >(this->getloc())),
      __st_(__st),
      __always_noconv_(__cv_->always_noconv()) {
  // On Windows, in wchar_t mode, ignore the codecvt from the locale by
  // default and assume noconv; this passes wchar_t through unmodified to
  // fputwc, which handles it correctly depending on the actual mode of the
  // output stream. If the user sets a custom locale with imbue(), that
  // gets honored.
  if constexpr (__is_win32api_wide_char)
    __always_noconv_ = true;
}

inline bool __do_fputc(char __c, FILE* __fp) {
  if (fwrite(&__c, sizeof(__c), 1, __fp) != 1)
    return false;
  return true;
}
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
inline bool __do_fputc(wchar_t __c, FILE* __fp) {
  // fputwc works regardless of wide/narrow mode of stdout, while
  // fwrite of wchar_t only works if the stream actually has been set
  // into wide mode.
  if (fputwc(__c, __fp) == WEOF)
    return false;
  return true;
}
#endif

template <class _CharT>
typename __stdoutbuf<_CharT>::int_type __stdoutbuf<_CharT>::overflow(int_type __c) {
  char __extbuf[__limit];
  char_type __1buf;
  if (!traits_type::eq_int_type(__c, traits_type::eof())) {
    __1buf = traits_type::to_char_type(__c);
    if (__always_noconv_) {
      if (!__do_fputc(__1buf, __file_))
        return traits_type::eof();
    } else {
      char* __extbe = __extbuf;
      codecvt_base::result __r;
      char_type* pbase = &__1buf;
      char_type* pptr  = pbase + 1;
      do {
        const char_type* __e;
        __r = __cv_->out(*__st_, pbase, pptr, __e, __extbuf, __extbuf + sizeof(__extbuf), __extbe);
        if (__e == pbase)
          return traits_type::eof();
        if (__r == codecvt_base::noconv) {
          if (fwrite(pbase, 1, 1, __file_) != 1)
            return traits_type::eof();
        } else if (__r == codecvt_base::ok || __r == codecvt_base::partial) {
          size_t __nmemb = static_cast<size_t>(__extbe - __extbuf);
          if (fwrite(__extbuf, 1, __nmemb, __file_) != __nmemb)
            return traits_type::eof();
          if (__r == codecvt_base::partial) {
            pbase = const_cast<char_type*>(__e);
          }
        } else
          return traits_type::eof();
      } while (__r == codecvt_base::partial);
    }
  }
  return traits_type::not_eof(__c);
}

template <class _CharT>
streamsize __stdoutbuf<_CharT>::xsputn(const char_type* __s, streamsize __n) {
  // For wchar_t on Windows, don't call fwrite(), but write characters one
  // at a time with fputwc(); that works both when stdout is in the default
  // mode and when it is set to Unicode mode.
  if (__always_noconv_ && !__is_win32api_wide_char)
    return fwrite(__s, sizeof(char_type), __n, __file_);
  streamsize __i = 0;
  for (; __i < __n; ++__i, ++__s)
    if (overflow(traits_type::to_int_type(*__s)) == traits_type::eof())
      break;
  return __i;
}

template <class _CharT>
int __stdoutbuf<_CharT>::sync() {
  char __extbuf[__limit];
  codecvt_base::result __r;
  do {
    char* __extbe;
    __r            = __cv_->unshift(*__st_, __extbuf, __extbuf + sizeof(__extbuf), __extbe);
    size_t __nmemb = static_cast<size_t>(__extbe - __extbuf);
    if (fwrite(__extbuf, 1, __nmemb, __file_) != __nmemb)
      return -1;
  } while (__r == codecvt_base::partial);
  if (__r == codecvt_base::error)
    return -1;
  if (fflush(__file_))
    return -1;
  return 0;
}

template <class _CharT>
void __stdoutbuf<_CharT>::imbue(const locale& __loc) {
  sync();
  __cv_            = &use_facet<codecvt<char_type, char, state_type> >(__loc);
  __always_noconv_ = __cv_->always_noconv();
}

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP_STD_STREAM_H
