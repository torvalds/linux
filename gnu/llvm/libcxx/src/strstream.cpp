//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <__assert>
#include <__utility/unreachable.h>
#include <algorithm>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <strstream>

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

strstreambuf::strstreambuf(streamsize __alsize)
    : __strmode_(__dynamic), __alsize_(__alsize), __palloc_(nullptr), __pfree_(nullptr) {}

strstreambuf::strstreambuf(void* (*__palloc)(size_t), void (*__pfree)(void*))
    : __strmode_(__dynamic), __alsize_(__default_alsize), __palloc_(__palloc), __pfree_(__pfree) {}

void strstreambuf::__init(char* __gnext, streamsize __n, char* __pbeg) {
  if (__n == 0)
    __n = static_cast<streamsize>(strlen(__gnext));
  else if (__n < 0)
    __n = INT_MAX;
  if (__pbeg == nullptr)
    setg(__gnext, __gnext, __gnext + __n);
  else {
    setg(__gnext, __gnext, __pbeg);
    setp(__pbeg, __pbeg + __n);
  }
}

strstreambuf::strstreambuf(char* __gnext, streamsize __n, char* __pbeg)
    : __strmode_(), __alsize_(__default_alsize), __palloc_(nullptr), __pfree_(nullptr) {
  __init(__gnext, __n, __pbeg);
}

strstreambuf::strstreambuf(const char* __gnext, streamsize __n)
    : __strmode_(__constant), __alsize_(__default_alsize), __palloc_(nullptr), __pfree_(nullptr) {
  __init(const_cast<char*>(__gnext), __n, nullptr);
}

strstreambuf::strstreambuf(signed char* __gnext, streamsize __n, signed char* __pbeg)
    : __strmode_(), __alsize_(__default_alsize), __palloc_(nullptr), __pfree_(nullptr) {
  __init(const_cast<char*>(reinterpret_cast<const char*>(__gnext)), __n, reinterpret_cast<char*>(__pbeg));
}

strstreambuf::strstreambuf(const signed char* __gnext, streamsize __n)
    : __strmode_(__constant), __alsize_(__default_alsize), __palloc_(nullptr), __pfree_(nullptr) {
  __init(const_cast<char*>(reinterpret_cast<const char*>(__gnext)), __n, nullptr);
}

strstreambuf::strstreambuf(unsigned char* __gnext, streamsize __n, unsigned char* __pbeg)
    : __strmode_(), __alsize_(__default_alsize), __palloc_(nullptr), __pfree_(nullptr) {
  __init(const_cast<char*>(reinterpret_cast<const char*>(__gnext)), __n, reinterpret_cast<char*>(__pbeg));
}

strstreambuf::strstreambuf(const unsigned char* __gnext, streamsize __n)
    : __strmode_(__constant), __alsize_(__default_alsize), __palloc_(nullptr), __pfree_(nullptr) {
  __init(const_cast<char*>(reinterpret_cast<const char*>(__gnext)), __n, nullptr);
}

strstreambuf::~strstreambuf() {
  if (eback() && (__strmode_ & __allocated) != 0 && (__strmode_ & __frozen) == 0) {
    if (__pfree_)
      __pfree_(eback());
    else
      delete[] eback();
  }
}

void strstreambuf::swap(strstreambuf& __rhs) {
  streambuf::swap(__rhs);
  std::swap(__strmode_, __rhs.__strmode_);
  std::swap(__alsize_, __rhs.__alsize_);
  std::swap(__palloc_, __rhs.__palloc_);
  std::swap(__pfree_, __rhs.__pfree_);
}

void strstreambuf::freeze(bool __freezefl) {
  if (__strmode_ & __dynamic) {
    if (__freezefl)
      __strmode_ |= __frozen;
    else
      __strmode_ &= ~__frozen;
  }
}

char* strstreambuf::str() {
  if (__strmode_ & __dynamic)
    __strmode_ |= __frozen;
  return eback();
}

int strstreambuf::pcount() const { return static_cast<int>(pptr() - pbase()); }

strstreambuf::int_type strstreambuf::overflow(int_type __c) {
  if (__c == EOF)
    return int_type(0);
  if (pptr() == epptr()) {
    if ((__strmode_ & __dynamic) == 0 || (__strmode_ & __frozen) != 0)
      return int_type(EOF);
    size_t old_size = static_cast<size_t>((epptr() ? epptr() : egptr()) - eback());
    size_t new_size = max<size_t>(static_cast<size_t>(__alsize_), 2 * old_size);
    if (new_size == 0)
      new_size = __default_alsize;
    char* buf = nullptr;
    if (__palloc_)
      buf = static_cast<char*>(__palloc_(new_size));
    else
      buf = new char[new_size];
    if (buf == nullptr)
      return int_type(EOF);
    if (old_size != 0) {
      _LIBCPP_ASSERT_INTERNAL(eback(), "strstreambuf::overflow reallocating but the get area is a null pointer");
      memcpy(buf, eback(), static_cast<size_t>(old_size));
    }
    ptrdiff_t ninp = gptr() - eback();
    ptrdiff_t einp = egptr() - eback();
    ptrdiff_t nout = pptr() - pbase();
    if (__strmode_ & __allocated) {
      if (__pfree_)
        __pfree_(eback());
      else
        delete[] eback();
    }
    setg(buf, buf + ninp, buf + einp);
    setp(buf + einp, buf + new_size);
    __pbump(nout);
    __strmode_ |= __allocated;
  }
  *pptr() = static_cast<char>(__c);
  pbump(1);
  return int_type(static_cast<unsigned char>(__c));
}

strstreambuf::int_type strstreambuf::pbackfail(int_type __c) {
  if (eback() == gptr())
    return EOF;
  if (__c == EOF) {
    gbump(-1);
    return int_type(0);
  }
  if (__strmode_ & __constant) {
    if (gptr()[-1] == static_cast<char>(__c)) {
      gbump(-1);
      return __c;
    }
    return EOF;
  }
  gbump(-1);
  *gptr() = static_cast<char>(__c);
  return __c;
}

strstreambuf::int_type strstreambuf::underflow() {
  if (gptr() == egptr()) {
    if (egptr() >= pptr())
      return EOF;
    setg(eback(), gptr(), pptr());
  }
  return int_type(static_cast<unsigned char>(*gptr()));
}

strstreambuf::pos_type strstreambuf::seekoff(off_type __off, ios_base::seekdir __way, ios_base::openmode __which) {
  bool pos_in  = (__which & ios::in) != 0;
  bool pos_out = (__which & ios::out) != 0;
  switch (__way) {
  case ios::beg:
  case ios::end:
    if (!pos_in && !pos_out)
      return pos_type(off_type(-1));
    break;
  case ios::cur:
    if (pos_in == pos_out)
      return pos_type(off_type(-1));
    break;
  }

  if (pos_in && gptr() == nullptr)
    return pos_type(off_type(-1));
  if (pos_out && pptr() == nullptr)
    return pos_type(off_type(-1));

  off_type newoff;
  char* seekhigh = epptr() ? epptr() : egptr();
  switch (__way) {
  case ios::beg:
    newoff = 0;
    break;
  case ios::cur:
    newoff = (pos_in ? gptr() : pptr()) - eback();
    break;
  case ios::end:
    newoff = seekhigh - eback();
    break;
  default:
    __libcpp_unreachable();
  }
  newoff += __off;
  if (newoff < 0 || newoff > seekhigh - eback())
    return pos_type(off_type(-1));

  char* newpos = eback() + newoff;
  if (pos_in)
    setg(eback(), newpos, std::max(newpos, egptr()));
  if (pos_out) {
    // min(pbase, newpos), newpos, epptr()
    __off = epptr() - newpos;
    setp(min(pbase(), newpos), epptr());
    __pbump((epptr() - pbase()) - __off);
  }
  return pos_type(newoff);
}

strstreambuf::pos_type strstreambuf::seekpos(pos_type __sp, ios_base::openmode __which) {
  bool pos_in  = (__which & ios::in) != 0;
  bool pos_out = (__which & ios::out) != 0;
  if (!pos_in && !pos_out)
    return pos_type(off_type(-1));

  if ((pos_in && gptr() == nullptr) || (pos_out && pptr() == nullptr))
    return pos_type(off_type(-1));

  off_type newoff = __sp;
  char* seekhigh  = epptr() ? epptr() : egptr();
  if (newoff < 0 || newoff > seekhigh - eback())
    return pos_type(off_type(-1));

  char* newpos = eback() + newoff;
  if (pos_in)
    setg(eback(), newpos, std::max(newpos, egptr()));
  if (pos_out) {
    // min(pbase, newpos), newpos, epptr()
    off_type temp = epptr() - newpos;
    setp(min(pbase(), newpos), epptr());
    __pbump((epptr() - pbase()) - temp);
  }
  return pos_type(newoff);
}

istrstream::~istrstream() {}

ostrstream::~ostrstream() {}

strstream::~strstream() {}

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS
