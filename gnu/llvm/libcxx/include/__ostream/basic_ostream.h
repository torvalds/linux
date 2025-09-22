//===---------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//

#ifndef _LIBCPP___OSTREAM_BASIC_OSTREAM_H
#define _LIBCPP___OSTREAM_BASIC_OSTREAM_H

#include <__config>
#include <__exception/operations.h>
#include <__memory/shared_ptr.h>
#include <__memory/unique_ptr.h>
#include <__system_error/error_code.h>
#include <__type_traits/conjunction.h>
#include <__type_traits/enable_if.h>
#include <__type_traits/is_base_of.h>
#include <__type_traits/void_t.h>
#include <__utility/declval.h>
#include <bitset>
#include <cstddef>
#include <ios>
#include <locale>
#include <new> // for __throw_bad_alloc
#include <streambuf>
#include <string_view>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _CharT, class _Traits>
class _LIBCPP_TEMPLATE_VIS basic_ostream : virtual public basic_ios<_CharT, _Traits> {
public:
  // types (inherited from basic_ios (27.5.4)):
  typedef _CharT char_type;
  typedef _Traits traits_type;
  typedef typename traits_type::int_type int_type;
  typedef typename traits_type::pos_type pos_type;
  typedef typename traits_type::off_type off_type;

  // 27.7.2.2 Constructor/destructor:
  inline _LIBCPP_HIDE_FROM_ABI_AFTER_V1 explicit basic_ostream(basic_streambuf<char_type, traits_type>* __sb) {
    this->init(__sb);
  }
  ~basic_ostream() override;

  basic_ostream(const basic_ostream& __rhs)            = delete;
  basic_ostream& operator=(const basic_ostream& __rhs) = delete;

protected:
  inline _LIBCPP_HIDE_FROM_ABI basic_ostream(basic_ostream&& __rhs);

  // 27.7.2.3 Assign/swap
  inline _LIBCPP_HIDE_FROM_ABI basic_ostream& operator=(basic_ostream&& __rhs);

  inline _LIBCPP_HIDE_FROM_ABI_AFTER_V1 void swap(basic_ostream& __rhs) {
    basic_ios<char_type, traits_type>::swap(__rhs);
  }

public:
  // 27.7.2.4 Prefix/suffix:
  class _LIBCPP_TEMPLATE_VIS sentry;

  // 27.7.2.6 Formatted output:
  inline _LIBCPP_HIDE_FROM_ABI_AFTER_V1 basic_ostream& operator<<(basic_ostream& (*__pf)(basic_ostream&)) {
    return __pf(*this);
  }

  inline _LIBCPP_HIDE_FROM_ABI_AFTER_V1 basic_ostream&
  operator<<(basic_ios<char_type, traits_type>& (*__pf)(basic_ios<char_type, traits_type>&)) {
    __pf(*this);
    return *this;
  }

  inline _LIBCPP_HIDE_FROM_ABI_AFTER_V1 basic_ostream& operator<<(ios_base& (*__pf)(ios_base&)) {
    __pf(*this);
    return *this;
  }

  basic_ostream& operator<<(bool __n);
  basic_ostream& operator<<(short __n);
  basic_ostream& operator<<(unsigned short __n);
  basic_ostream& operator<<(int __n);
  basic_ostream& operator<<(unsigned int __n);
  basic_ostream& operator<<(long __n);
  basic_ostream& operator<<(unsigned long __n);
  basic_ostream& operator<<(long long __n);
  basic_ostream& operator<<(unsigned long long __n);
  basic_ostream& operator<<(float __f);
  basic_ostream& operator<<(double __f);
  basic_ostream& operator<<(long double __f);
  basic_ostream& operator<<(const void* __p);

#if _LIBCPP_STD_VER >= 23
  _LIBCPP_HIDE_FROM_ABI basic_ostream& operator<<(const volatile void* __p) {
    return operator<<(const_cast<const void*>(__p));
  }
#endif

  basic_ostream& operator<<(basic_streambuf<char_type, traits_type>* __sb);

#if _LIBCPP_STD_VER >= 17
  // LWG 2221 - nullptr. This is not backported to older standards modes.
  // See https://reviews.llvm.org/D127033 for more info on the rationale.
  _LIBCPP_HIDE_FROM_ABI basic_ostream& operator<<(nullptr_t) { return *this << "nullptr"; }
#endif

  // 27.7.2.7 Unformatted output:
  basic_ostream& put(char_type __c);
  basic_ostream& write(const char_type* __s, streamsize __n);
  basic_ostream& flush();

  // 27.7.2.5 seeks:
  inline _LIBCPP_HIDE_FROM_ABI_AFTER_V1 pos_type tellp();
  inline _LIBCPP_HIDE_FROM_ABI_AFTER_V1 basic_ostream& seekp(pos_type __pos);
  inline _LIBCPP_HIDE_FROM_ABI_AFTER_V1 basic_ostream& seekp(off_type __off, ios_base::seekdir __dir);

protected:
  _LIBCPP_HIDE_FROM_ABI basic_ostream() {} // extension, intentially does not initialize
};

template <class _CharT, class _Traits>
class _LIBCPP_TEMPLATE_VIS basic_ostream<_CharT, _Traits>::sentry {
  bool __ok_;
  basic_ostream<_CharT, _Traits>& __os_;

public:
  explicit sentry(basic_ostream<_CharT, _Traits>& __os);
  ~sentry();
  sentry(const sentry&)            = delete;
  sentry& operator=(const sentry&) = delete;

  _LIBCPP_HIDE_FROM_ABI explicit operator bool() const { return __ok_; }
};

template <class _CharT, class _Traits>
basic_ostream<_CharT, _Traits>::sentry::sentry(basic_ostream<_CharT, _Traits>& __os) : __ok_(false), __os_(__os) {
  if (__os.good()) {
    if (__os.tie())
      __os.tie()->flush();
    __ok_ = true;
  }
}

template <class _CharT, class _Traits>
basic_ostream<_CharT, _Traits>::sentry::~sentry() {
  if (__os_.rdbuf() && __os_.good() && (__os_.flags() & ios_base::unitbuf) && !uncaught_exception()) {
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
    try {
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
      if (__os_.rdbuf()->pubsync() == -1)
        __os_.setstate(ios_base::badbit);
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
    } catch (...) {
    }
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
  }
}

template <class _CharT, class _Traits>
basic_ostream<_CharT, _Traits>::basic_ostream(basic_ostream&& __rhs) {
  this->move(__rhs);
}

template <class _CharT, class _Traits>
basic_ostream<_CharT, _Traits>& basic_ostream<_CharT, _Traits>::operator=(basic_ostream&& __rhs) {
  swap(__rhs);
  return *this;
}

template <class _CharT, class _Traits>
basic_ostream<_CharT, _Traits>::~basic_ostream() {}

template <class _CharT, class _Traits>
basic_ostream<_CharT, _Traits>&
basic_ostream<_CharT, _Traits>::operator<<(basic_streambuf<char_type, traits_type>* __sb) {
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  try {
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
    sentry __s(*this);
    if (__s) {
      if (__sb) {
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
        try {
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
          typedef istreambuf_iterator<_CharT, _Traits> _Ip;
          typedef ostreambuf_iterator<_CharT, _Traits> _Op;
          _Ip __i(__sb);
          _Ip __eof;
          _Op __o(*this);
          size_t __c = 0;
          for (; __i != __eof; ++__i, ++__o, ++__c) {
            *__o = *__i;
            if (__o.failed())
              break;
          }
          if (__c == 0)
            this->setstate(ios_base::failbit);
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
        } catch (...) {
          this->__set_failbit_and_consider_rethrow();
        }
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
      } else
        this->setstate(ios_base::badbit);
    }
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  } catch (...) {
    this->__set_badbit_and_consider_rethrow();
  }
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
  return *this;
}

template <class _CharT, class _Traits>
basic_ostream<_CharT, _Traits>& basic_ostream<_CharT, _Traits>::operator<<(bool __n) {
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  try {
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
    sentry __s(*this);
    if (__s) {
      typedef num_put<char_type, ostreambuf_iterator<char_type, traits_type> > _Fp;
      const _Fp& __f = std::use_facet<_Fp>(this->getloc());
      if (__f.put(*this, *this, this->fill(), __n).failed())
        this->setstate(ios_base::badbit | ios_base::failbit);
    }
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  } catch (...) {
    this->__set_badbit_and_consider_rethrow();
  }
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
  return *this;
}

template <class _CharT, class _Traits>
basic_ostream<_CharT, _Traits>& basic_ostream<_CharT, _Traits>::operator<<(short __n) {
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  try {
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
    sentry __s(*this);
    if (__s) {
      ios_base::fmtflags __flags = ios_base::flags() & ios_base::basefield;
      typedef num_put<char_type, ostreambuf_iterator<char_type, traits_type> > _Fp;
      const _Fp& __f = std::use_facet<_Fp>(this->getloc());
      if (__f.put(*this,
                  *this,
                  this->fill(),
                  __flags == ios_base::oct || __flags == ios_base::hex
                      ? static_cast<long>(static_cast<unsigned short>(__n))
                      : static_cast<long>(__n))
              .failed())
        this->setstate(ios_base::badbit | ios_base::failbit);
    }
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  } catch (...) {
    this->__set_badbit_and_consider_rethrow();
  }
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
  return *this;
}

template <class _CharT, class _Traits>
basic_ostream<_CharT, _Traits>& basic_ostream<_CharT, _Traits>::operator<<(unsigned short __n) {
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  try {
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
    sentry __s(*this);
    if (__s) {
      typedef num_put<char_type, ostreambuf_iterator<char_type, traits_type> > _Fp;
      const _Fp& __f = std::use_facet<_Fp>(this->getloc());
      if (__f.put(*this, *this, this->fill(), static_cast<unsigned long>(__n)).failed())
        this->setstate(ios_base::badbit | ios_base::failbit);
    }
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  } catch (...) {
    this->__set_badbit_and_consider_rethrow();
  }
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
  return *this;
}

template <class _CharT, class _Traits>
basic_ostream<_CharT, _Traits>& basic_ostream<_CharT, _Traits>::operator<<(int __n) {
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  try {
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
    sentry __s(*this);
    if (__s) {
      ios_base::fmtflags __flags = ios_base::flags() & ios_base::basefield;
      typedef num_put<char_type, ostreambuf_iterator<char_type, traits_type> > _Fp;
      const _Fp& __f = std::use_facet<_Fp>(this->getloc());
      if (__f.put(*this,
                  *this,
                  this->fill(),
                  __flags == ios_base::oct || __flags == ios_base::hex
                      ? static_cast<long>(static_cast<unsigned int>(__n))
                      : static_cast<long>(__n))
              .failed())
        this->setstate(ios_base::badbit | ios_base::failbit);
    }
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  } catch (...) {
    this->__set_badbit_and_consider_rethrow();
  }
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
  return *this;
}

template <class _CharT, class _Traits>
basic_ostream<_CharT, _Traits>& basic_ostream<_CharT, _Traits>::operator<<(unsigned int __n) {
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  try {
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
    sentry __s(*this);
    if (__s) {
      typedef num_put<char_type, ostreambuf_iterator<char_type, traits_type> > _Fp;
      const _Fp& __f = std::use_facet<_Fp>(this->getloc());
      if (__f.put(*this, *this, this->fill(), static_cast<unsigned long>(__n)).failed())
        this->setstate(ios_base::badbit | ios_base::failbit);
    }
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  } catch (...) {
    this->__set_badbit_and_consider_rethrow();
  }
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
  return *this;
}

template <class _CharT, class _Traits>
basic_ostream<_CharT, _Traits>& basic_ostream<_CharT, _Traits>::operator<<(long __n) {
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  try {
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
    sentry __s(*this);
    if (__s) {
      typedef num_put<char_type, ostreambuf_iterator<char_type, traits_type> > _Fp;
      const _Fp& __f = std::use_facet<_Fp>(this->getloc());
      if (__f.put(*this, *this, this->fill(), __n).failed())
        this->setstate(ios_base::badbit | ios_base::failbit);
    }
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  } catch (...) {
    this->__set_badbit_and_consider_rethrow();
  }
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
  return *this;
}

template <class _CharT, class _Traits>
basic_ostream<_CharT, _Traits>& basic_ostream<_CharT, _Traits>::operator<<(unsigned long __n) {
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  try {
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
    sentry __s(*this);
    if (__s) {
      typedef num_put<char_type, ostreambuf_iterator<char_type, traits_type> > _Fp;
      const _Fp& __f = std::use_facet<_Fp>(this->getloc());
      if (__f.put(*this, *this, this->fill(), __n).failed())
        this->setstate(ios_base::badbit | ios_base::failbit);
    }
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  } catch (...) {
    this->__set_badbit_and_consider_rethrow();
  }
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
  return *this;
}

template <class _CharT, class _Traits>
basic_ostream<_CharT, _Traits>& basic_ostream<_CharT, _Traits>::operator<<(long long __n) {
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  try {
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
    sentry __s(*this);
    if (__s) {
      typedef num_put<char_type, ostreambuf_iterator<char_type, traits_type> > _Fp;
      const _Fp& __f = std::use_facet<_Fp>(this->getloc());
      if (__f.put(*this, *this, this->fill(), __n).failed())
        this->setstate(ios_base::badbit | ios_base::failbit);
    }
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  } catch (...) {
    this->__set_badbit_and_consider_rethrow();
  }
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
  return *this;
}

template <class _CharT, class _Traits>
basic_ostream<_CharT, _Traits>& basic_ostream<_CharT, _Traits>::operator<<(unsigned long long __n) {
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  try {
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
    sentry __s(*this);
    if (__s) {
      typedef num_put<char_type, ostreambuf_iterator<char_type, traits_type> > _Fp;
      const _Fp& __f = std::use_facet<_Fp>(this->getloc());
      if (__f.put(*this, *this, this->fill(), __n).failed())
        this->setstate(ios_base::badbit | ios_base::failbit);
    }
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  } catch (...) {
    this->__set_badbit_and_consider_rethrow();
  }
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
  return *this;
}

template <class _CharT, class _Traits>
basic_ostream<_CharT, _Traits>& basic_ostream<_CharT, _Traits>::operator<<(float __n) {
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  try {
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
    sentry __s(*this);
    if (__s) {
      typedef num_put<char_type, ostreambuf_iterator<char_type, traits_type> > _Fp;
      const _Fp& __f = std::use_facet<_Fp>(this->getloc());
      if (__f.put(*this, *this, this->fill(), static_cast<double>(__n)).failed())
        this->setstate(ios_base::badbit | ios_base::failbit);
    }
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  } catch (...) {
    this->__set_badbit_and_consider_rethrow();
  }
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
  return *this;
}

template <class _CharT, class _Traits>
basic_ostream<_CharT, _Traits>& basic_ostream<_CharT, _Traits>::operator<<(double __n) {
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  try {
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
    sentry __s(*this);
    if (__s) {
      typedef num_put<char_type, ostreambuf_iterator<char_type, traits_type> > _Fp;
      const _Fp& __f = std::use_facet<_Fp>(this->getloc());
      if (__f.put(*this, *this, this->fill(), __n).failed())
        this->setstate(ios_base::badbit | ios_base::failbit);
    }
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  } catch (...) {
    this->__set_badbit_and_consider_rethrow();
  }
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
  return *this;
}

template <class _CharT, class _Traits>
basic_ostream<_CharT, _Traits>& basic_ostream<_CharT, _Traits>::operator<<(long double __n) {
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  try {
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
    sentry __s(*this);
    if (__s) {
      typedef num_put<char_type, ostreambuf_iterator<char_type, traits_type> > _Fp;
      const _Fp& __f = std::use_facet<_Fp>(this->getloc());
      if (__f.put(*this, *this, this->fill(), __n).failed())
        this->setstate(ios_base::badbit | ios_base::failbit);
    }
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  } catch (...) {
    this->__set_badbit_and_consider_rethrow();
  }
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
  return *this;
}

template <class _CharT, class _Traits>
basic_ostream<_CharT, _Traits>& basic_ostream<_CharT, _Traits>::operator<<(const void* __n) {
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  try {
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
    sentry __s(*this);
    if (__s) {
      typedef num_put<char_type, ostreambuf_iterator<char_type, traits_type> > _Fp;
      const _Fp& __f = std::use_facet<_Fp>(this->getloc());
      if (__f.put(*this, *this, this->fill(), __n).failed())
        this->setstate(ios_base::badbit | ios_base::failbit);
    }
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  } catch (...) {
    this->__set_badbit_and_consider_rethrow();
  }
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
  return *this;
}

template <class _CharT, class _Traits>
_LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>&
__put_character_sequence(basic_ostream<_CharT, _Traits>& __os, const _CharT* __str, size_t __len) {
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  try {
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
    typename basic_ostream<_CharT, _Traits>::sentry __s(__os);
    if (__s) {
      typedef ostreambuf_iterator<_CharT, _Traits> _Ip;
      if (std::__pad_and_output(
              _Ip(__os),
              __str,
              (__os.flags() & ios_base::adjustfield) == ios_base::left ? __str + __len : __str,
              __str + __len,
              __os,
              __os.fill())
              .failed())
        __os.setstate(ios_base::badbit | ios_base::failbit);
    }
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  } catch (...) {
    __os.__set_badbit_and_consider_rethrow();
  }
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
  return __os;
}

template <class _CharT, class _Traits>
_LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>& operator<<(basic_ostream<_CharT, _Traits>& __os, _CharT __c) {
  return std::__put_character_sequence(__os, &__c, 1);
}

template <class _CharT, class _Traits>
_LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>& operator<<(basic_ostream<_CharT, _Traits>& __os, char __cn) {
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  try {
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
    typename basic_ostream<_CharT, _Traits>::sentry __s(__os);
    if (__s) {
      _CharT __c = __os.widen(__cn);
      typedef ostreambuf_iterator<_CharT, _Traits> _Ip;
      if (std::__pad_and_output(
              _Ip(__os),
              &__c,
              (__os.flags() & ios_base::adjustfield) == ios_base::left ? &__c + 1 : &__c,
              &__c + 1,
              __os,
              __os.fill())
              .failed())
        __os.setstate(ios_base::badbit | ios_base::failbit);
    }
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  } catch (...) {
    __os.__set_badbit_and_consider_rethrow();
  }
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
  return __os;
}

template <class _Traits>
_LIBCPP_HIDE_FROM_ABI basic_ostream<char, _Traits>& operator<<(basic_ostream<char, _Traits>& __os, char __c) {
  return std::__put_character_sequence(__os, &__c, 1);
}

template <class _Traits>
_LIBCPP_HIDE_FROM_ABI basic_ostream<char, _Traits>& operator<<(basic_ostream<char, _Traits>& __os, signed char __c) {
  return std::__put_character_sequence(__os, (char*)&__c, 1);
}

template <class _Traits>
_LIBCPP_HIDE_FROM_ABI basic_ostream<char, _Traits>& operator<<(basic_ostream<char, _Traits>& __os, unsigned char __c) {
  return std::__put_character_sequence(__os, (char*)&__c, 1);
}

template <class _CharT, class _Traits>
_LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os, const _CharT* __str) {
  return std::__put_character_sequence(__os, __str, _Traits::length(__str));
}

template <class _CharT, class _Traits>
_LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os, const char* __strn) {
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  try {
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
    typename basic_ostream<_CharT, _Traits>::sentry __s(__os);
    if (__s) {
      typedef ostreambuf_iterator<_CharT, _Traits> _Ip;
      size_t __len   = char_traits<char>::length(__strn);
      const int __bs = 100;
      _CharT __wbb[__bs];
      _CharT* __wb = __wbb;
      unique_ptr<_CharT, void (*)(void*)> __h(0, free);
      if (__len > __bs) {
        __wb = (_CharT*)malloc(__len * sizeof(_CharT));
        if (__wb == 0)
          __throw_bad_alloc();
        __h.reset(__wb);
      }
      for (_CharT* __p = __wb; *__strn != '\0'; ++__strn, ++__p)
        *__p = __os.widen(*__strn);
      if (std::__pad_and_output(
              _Ip(__os),
              __wb,
              (__os.flags() & ios_base::adjustfield) == ios_base::left ? __wb + __len : __wb,
              __wb + __len,
              __os,
              __os.fill())
              .failed())
        __os.setstate(ios_base::badbit | ios_base::failbit);
    }
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  } catch (...) {
    __os.__set_badbit_and_consider_rethrow();
  }
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
  return __os;
}

template <class _Traits>
_LIBCPP_HIDE_FROM_ABI basic_ostream<char, _Traits>& operator<<(basic_ostream<char, _Traits>& __os, const char* __str) {
  return std::__put_character_sequence(__os, __str, _Traits::length(__str));
}

template <class _Traits>
_LIBCPP_HIDE_FROM_ABI basic_ostream<char, _Traits>&
operator<<(basic_ostream<char, _Traits>& __os, const signed char* __str) {
  const char* __s = (const char*)__str;
  return std::__put_character_sequence(__os, __s, _Traits::length(__s));
}

template <class _Traits>
_LIBCPP_HIDE_FROM_ABI basic_ostream<char, _Traits>&
operator<<(basic_ostream<char, _Traits>& __os, const unsigned char* __str) {
  const char* __s = (const char*)__str;
  return std::__put_character_sequence(__os, __s, _Traits::length(__s));
}

template <class _CharT, class _Traits>
basic_ostream<_CharT, _Traits>& basic_ostream<_CharT, _Traits>::put(char_type __c) {
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  try {
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
    sentry __s(*this);
    if (__s) {
      typedef ostreambuf_iterator<_CharT, _Traits> _Op;
      _Op __o(*this);
      *__o = __c;
      if (__o.failed())
        this->setstate(ios_base::badbit);
    }
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  } catch (...) {
    this->__set_badbit_and_consider_rethrow();
  }
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
  return *this;
}

template <class _CharT, class _Traits>
basic_ostream<_CharT, _Traits>& basic_ostream<_CharT, _Traits>::write(const char_type* __s, streamsize __n) {
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  try {
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
    sentry __sen(*this);
    if (__sen && __n) {
      if (this->rdbuf()->sputn(__s, __n) != __n)
        this->setstate(ios_base::badbit);
    }
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  } catch (...) {
    this->__set_badbit_and_consider_rethrow();
  }
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
  return *this;
}

template <class _CharT, class _Traits>
basic_ostream<_CharT, _Traits>& basic_ostream<_CharT, _Traits>::flush() {
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  try {
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
    if (this->rdbuf()) {
      sentry __s(*this);
      if (__s) {
        if (this->rdbuf()->pubsync() == -1)
          this->setstate(ios_base::badbit);
      }
    }
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  } catch (...) {
    this->__set_badbit_and_consider_rethrow();
  }
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
  return *this;
}

template <class _CharT, class _Traits>
typename basic_ostream<_CharT, _Traits>::pos_type basic_ostream<_CharT, _Traits>::tellp() {
  if (this->fail())
    return pos_type(-1);
  return this->rdbuf()->pubseekoff(0, ios_base::cur, ios_base::out);
}

template <class _CharT, class _Traits>
basic_ostream<_CharT, _Traits>& basic_ostream<_CharT, _Traits>::seekp(pos_type __pos) {
  sentry __s(*this);
  if (!this->fail()) {
    if (this->rdbuf()->pubseekpos(__pos, ios_base::out) == pos_type(-1))
      this->setstate(ios_base::failbit);
  }
  return *this;
}

template <class _CharT, class _Traits>
basic_ostream<_CharT, _Traits>& basic_ostream<_CharT, _Traits>::seekp(off_type __off, ios_base::seekdir __dir) {
  sentry __s(*this);
  if (!this->fail()) {
    if (this->rdbuf()->pubseekoff(__off, __dir, ios_base::out) == pos_type(-1))
      this->setstate(ios_base::failbit);
  }
  return *this;
}

template <class _CharT, class _Traits>
_LIBCPP_HIDE_FROM_ABI inline basic_ostream<_CharT, _Traits>& endl(basic_ostream<_CharT, _Traits>& __os) {
  __os.put(__os.widen('\n'));
  __os.flush();
  return __os;
}

template <class _CharT, class _Traits>
_LIBCPP_HIDE_FROM_ABI inline basic_ostream<_CharT, _Traits>& ends(basic_ostream<_CharT, _Traits>& __os) {
  __os.put(_CharT());
  return __os;
}

template <class _CharT, class _Traits>
_LIBCPP_HIDE_FROM_ABI inline basic_ostream<_CharT, _Traits>& flush(basic_ostream<_CharT, _Traits>& __os) {
  __os.flush();
  return __os;
}

template <class _Stream, class _Tp, class = void>
struct __is_ostreamable : false_type {};

template <class _Stream, class _Tp>
struct __is_ostreamable<_Stream, _Tp, decltype(std::declval<_Stream>() << std::declval<_Tp>(), void())> : true_type {};

template <class _Stream,
          class _Tp,
          __enable_if_t<_And<is_base_of<ios_base, _Stream>, __is_ostreamable<_Stream&, const _Tp&> >::value, int> = 0>
_LIBCPP_HIDE_FROM_ABI _Stream&& operator<<(_Stream&& __os, const _Tp& __x) {
  __os << __x;
  return std::move(__os);
}

template <class _CharT, class _Traits, class _Allocator>
basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os, const basic_string<_CharT, _Traits, _Allocator>& __str) {
  return std::__put_character_sequence(__os, __str.data(), __str.size());
}

template <class _CharT, class _Traits>
_LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os, basic_string_view<_CharT, _Traits> __sv) {
  return std::__put_character_sequence(__os, __sv.data(), __sv.size());
}

template <class _CharT, class _Traits>
inline _LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os, const error_code& __ec) {
  return __os << __ec.category().name() << ':' << __ec.value();
}

template <class _CharT, class _Traits, class _Yp>
inline _LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os, shared_ptr<_Yp> const& __p) {
  return __os << __p.get();
}

template <
    class _CharT,
    class _Traits,
    class _Yp,
    class _Dp,
    __enable_if_t<is_same<void,
                          __void_t<decltype((std::declval<basic_ostream<_CharT, _Traits>&>()
                                             << std::declval<typename unique_ptr<_Yp, _Dp>::pointer>()))> >::value,
                  int> = 0>
inline _LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os, unique_ptr<_Yp, _Dp> const& __p) {
  return __os << __p.get();
}

template <class _CharT, class _Traits, size_t _Size>
_LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os, const bitset<_Size>& __x) {
  return __os << __x.template to_string<_CharT, _Traits>(std::use_facet<ctype<_CharT> >(__os.getloc()).widen('0'),
                                                         std::use_facet<ctype<_CharT> >(__os.getloc()).widen('1'));
}

#if _LIBCPP_STD_VER >= 20

#  ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
template <class _Traits>
basic_ostream<char, _Traits>& operator<<(basic_ostream<char, _Traits>&, wchar_t) = delete;

template <class _Traits>
basic_ostream<char, _Traits>& operator<<(basic_ostream<char, _Traits>&, const wchar_t*) = delete;

template <class _Traits>
basic_ostream<wchar_t, _Traits>& operator<<(basic_ostream<wchar_t, _Traits>&, char16_t) = delete;

template <class _Traits>
basic_ostream<wchar_t, _Traits>& operator<<(basic_ostream<wchar_t, _Traits>&, char32_t) = delete;

template <class _Traits>
basic_ostream<wchar_t, _Traits>& operator<<(basic_ostream<wchar_t, _Traits>&, const char16_t*) = delete;

template <class _Traits>
basic_ostream<wchar_t, _Traits>& operator<<(basic_ostream<wchar_t, _Traits>&, const char32_t*) = delete;

#  endif // _LIBCPP_HAS_NO_WIDE_CHARACTERS

#  ifndef _LIBCPP_HAS_NO_CHAR8_T
template <class _Traits>
basic_ostream<char, _Traits>& operator<<(basic_ostream<char, _Traits>&, char8_t) = delete;

template <class _Traits>
basic_ostream<wchar_t, _Traits>& operator<<(basic_ostream<wchar_t, _Traits>&, char8_t) = delete;

template <class _Traits>
basic_ostream<char, _Traits>& operator<<(basic_ostream<char, _Traits>&, const char8_t*) = delete;

template <class _Traits>
basic_ostream<wchar_t, _Traits>& operator<<(basic_ostream<wchar_t, _Traits>&, const char8_t*) = delete;
#  endif

template <class _Traits>
basic_ostream<char, _Traits>& operator<<(basic_ostream<char, _Traits>&, char16_t) = delete;

template <class _Traits>
basic_ostream<char, _Traits>& operator<<(basic_ostream<char, _Traits>&, char32_t) = delete;

template <class _Traits>
basic_ostream<char, _Traits>& operator<<(basic_ostream<char, _Traits>&, const char16_t*) = delete;

template <class _Traits>
basic_ostream<char, _Traits>& operator<<(basic_ostream<char, _Traits>&, const char32_t*) = delete;

#endif // _LIBCPP_STD_VER >= 20

extern template class _LIBCPP_EXTERN_TEMPLATE_TYPE_VIS basic_ostream<char>;
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
extern template class _LIBCPP_EXTERN_TEMPLATE_TYPE_VIS basic_ostream<wchar_t>;
#endif

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___OSTREAM_BASIC_OSTREAM_H
