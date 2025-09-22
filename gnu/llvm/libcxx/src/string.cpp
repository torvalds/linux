//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <__assert>
#include <cerrno>
#include <charconv>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
#  include <cwchar>
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#ifndef _LIBCPP_ABI_DO_NOT_EXPORT_BASIC_STRING_COMMON

template <bool>
struct __basic_string_common;

// The struct isn't declared anymore in the headers. It's only here for ABI compatibility.
template <>
struct __basic_string_common<true> {
  _LIBCPP_NORETURN _LIBCPP_EXPORTED_FROM_ABI void __throw_length_error() const;
  _LIBCPP_NORETURN _LIBCPP_EXPORTED_FROM_ABI void __throw_out_of_range() const;
};

void __basic_string_common<true>::__throw_length_error() const { std::__throw_length_error("basic_string"); }
void __basic_string_common<true>::__throw_out_of_range() const { std::__throw_out_of_range("basic_string"); }

#endif // _LIBCPP_ABI_DO_NOT_EXPORT_BASIC_STRING_COMMON

#define _LIBCPP_EXTERN_TEMPLATE_DEFINE(...) template __VA_ARGS__;
#ifdef _LIBCPP_ABI_STRING_OPTIMIZED_EXTERNAL_INSTANTIATION
_LIBCPP_STRING_UNSTABLE_EXTERN_TEMPLATE_LIST(_LIBCPP_EXTERN_TEMPLATE_DEFINE, char)
#  ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
_LIBCPP_STRING_UNSTABLE_EXTERN_TEMPLATE_LIST(_LIBCPP_EXTERN_TEMPLATE_DEFINE, wchar_t)
#  endif
#else
_LIBCPP_STRING_V1_EXTERN_TEMPLATE_LIST(_LIBCPP_EXTERN_TEMPLATE_DEFINE, char)
#  ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
_LIBCPP_STRING_V1_EXTERN_TEMPLATE_LIST(_LIBCPP_EXTERN_TEMPLATE_DEFINE, wchar_t)
#  endif
#endif
#undef _LIBCPP_EXTERN_TEMPLATE_DEFINE

template string operator+ <char, char_traits<char>, allocator<char>>(char const*, string const&);

namespace {

inline void throw_from_string_out_of_range(const string& func) {
  std::__throw_out_of_range((func + ": out of range").c_str());
}

inline void throw_from_string_invalid_arg(const string& func) {
  std::__throw_invalid_argument((func + ": no conversion").c_str());
}

// as_integer

template <typename V, typename S, typename F>
inline V as_integer_helper(const string& func, const S& str, size_t* idx, int base, F f) {
  typename S::value_type* ptr                             = nullptr;
  const typename S::value_type* const p                   = str.c_str();
  __libcpp_remove_reference_t<decltype(errno)> errno_save = errno;
  errno                                                   = 0;
  V r                                                     = f(p, &ptr, base);
  swap(errno, errno_save);
  if (errno_save == ERANGE)
    throw_from_string_out_of_range(func);
  if (ptr == p)
    throw_from_string_invalid_arg(func);
  if (idx)
    *idx = static_cast<size_t>(ptr - p);
  return r;
}

template <typename V, typename S>
inline V as_integer(const string& func, const S& s, size_t* idx, int base);

// string
template <>
inline int as_integer(const string& func, const string& s, size_t* idx, int base) {
  // Use long as no Standard string to integer exists.
  long r = as_integer_helper<long>(func, s, idx, base, strtol);
  if (r < numeric_limits<int>::min() || numeric_limits<int>::max() < r)
    throw_from_string_out_of_range(func);
  return static_cast<int>(r);
}

template <>
inline long as_integer(const string& func, const string& s, size_t* idx, int base) {
  return as_integer_helper<long>(func, s, idx, base, strtol);
}

template <>
inline unsigned long as_integer(const string& func, const string& s, size_t* idx, int base) {
  return as_integer_helper<unsigned long>(func, s, idx, base, strtoul);
}

template <>
inline long long as_integer(const string& func, const string& s, size_t* idx, int base) {
  return as_integer_helper<long long>(func, s, idx, base, strtoll);
}

template <>
inline unsigned long long as_integer(const string& func, const string& s, size_t* idx, int base) {
  return as_integer_helper<unsigned long long>(func, s, idx, base, strtoull);
}

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
// wstring
template <>
inline int as_integer(const string& func, const wstring& s, size_t* idx, int base) {
  // Use long as no Stantard string to integer exists.
  long r = as_integer_helper<long>(func, s, idx, base, wcstol);
  if (r < numeric_limits<int>::min() || numeric_limits<int>::max() < r)
    throw_from_string_out_of_range(func);
  return static_cast<int>(r);
}

template <>
inline long as_integer(const string& func, const wstring& s, size_t* idx, int base) {
  return as_integer_helper<long>(func, s, idx, base, wcstol);
}

template <>
inline unsigned long as_integer(const string& func, const wstring& s, size_t* idx, int base) {
  return as_integer_helper<unsigned long>(func, s, idx, base, wcstoul);
}

template <>
inline long long as_integer(const string& func, const wstring& s, size_t* idx, int base) {
  return as_integer_helper<long long>(func, s, idx, base, wcstoll);
}

template <>
inline unsigned long long as_integer(const string& func, const wstring& s, size_t* idx, int base) {
  return as_integer_helper<unsigned long long>(func, s, idx, base, wcstoull);
}
#endif // _LIBCPP_HAS_NO_WIDE_CHARACTERS

// as_float

template <typename V, typename S, typename F>
inline V as_float_helper(const string& func, const S& str, size_t* idx, F f) {
  typename S::value_type* ptr                             = nullptr;
  const typename S::value_type* const p                   = str.c_str();
  __libcpp_remove_reference_t<decltype(errno)> errno_save = errno;
  errno                                                   = 0;
  V r                                                     = f(p, &ptr);
  swap(errno, errno_save);
  if (errno_save == ERANGE)
    throw_from_string_out_of_range(func);
  if (ptr == p)
    throw_from_string_invalid_arg(func);
  if (idx)
    *idx = static_cast<size_t>(ptr - p);
  return r;
}

template <typename V, typename S>
inline V as_float(const string& func, const S& s, size_t* idx = nullptr);

template <>
inline float as_float(const string& func, const string& s, size_t* idx) {
  return as_float_helper<float>(func, s, idx, strtof);
}

template <>
inline double as_float(const string& func, const string& s, size_t* idx) {
  return as_float_helper<double>(func, s, idx, strtod);
}

template <>
inline long double as_float(const string& func, const string& s, size_t* idx) {
  return as_float_helper<long double>(func, s, idx, strtold);
}

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
template <>
inline float as_float(const string& func, const wstring& s, size_t* idx) {
  return as_float_helper<float>(func, s, idx, wcstof);
}

template <>
inline double as_float(const string& func, const wstring& s, size_t* idx) {
  return as_float_helper<double>(func, s, idx, wcstod);
}

template <>
inline long double as_float(const string& func, const wstring& s, size_t* idx) {
  return as_float_helper<long double>(func, s, idx, wcstold);
}
#endif // _LIBCPP_HAS_NO_WIDE_CHARACTERS

} // unnamed namespace

int stoi(const string& str, size_t* idx, int base) { return as_integer<int>("stoi", str, idx, base); }

long stol(const string& str, size_t* idx, int base) { return as_integer<long>("stol", str, idx, base); }

unsigned long stoul(const string& str, size_t* idx, int base) {
  return as_integer<unsigned long>("stoul", str, idx, base);
}

long long stoll(const string& str, size_t* idx, int base) { return as_integer<long long>("stoll", str, idx, base); }

unsigned long long stoull(const string& str, size_t* idx, int base) {
  return as_integer<unsigned long long>("stoull", str, idx, base);
}

float stof(const string& str, size_t* idx) { return as_float<float>("stof", str, idx); }

double stod(const string& str, size_t* idx) { return as_float<double>("stod", str, idx); }

long double stold(const string& str, size_t* idx) { return as_float<long double>("stold", str, idx); }

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
int stoi(const wstring& str, size_t* idx, int base) { return as_integer<int>("stoi", str, idx, base); }

long stol(const wstring& str, size_t* idx, int base) { return as_integer<long>("stol", str, idx, base); }

unsigned long stoul(const wstring& str, size_t* idx, int base) {
  return as_integer<unsigned long>("stoul", str, idx, base);
}

long long stoll(const wstring& str, size_t* idx, int base) { return as_integer<long long>("stoll", str, idx, base); }

unsigned long long stoull(const wstring& str, size_t* idx, int base) {
  return as_integer<unsigned long long>("stoull", str, idx, base);
}

float stof(const wstring& str, size_t* idx) { return as_float<float>("stof", str, idx); }

double stod(const wstring& str, size_t* idx) { return as_float<double>("stod", str, idx); }

long double stold(const wstring& str, size_t* idx) { return as_float<long double>("stold", str, idx); }
#endif // !_LIBCPP_HAS_NO_WIDE_CHARACTERS

// to_string

namespace {

// as_string

template <typename S, typename P, typename V >
inline S as_string(P sprintf_like, S s, const typename S::value_type* fmt, V a) {
  typedef typename S::size_type size_type;
  size_type available = s.size();
  while (true) {
    int status = sprintf_like(&s[0], available + 1, fmt, a);
    if (status >= 0) {
      size_type used = static_cast<size_type>(status);
      if (used <= available) {
        s.resize(used);
        break;
      }
      available = used; // Assume this is advice of how much space we need.
    } else
      available = available * 2 + 1;
    s.resize(available);
  }
  return s;
}

template <class S>
struct initial_string;

template <>
struct initial_string<string> {
  string operator()() const {
    string s;
    s.resize(s.capacity());
    return s;
  }
};

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
template <>
struct initial_string<wstring> {
  wstring operator()() const {
    wstring s(20, wchar_t());
    s.resize(s.capacity());
    return s;
  }
};

typedef int (*wide_printf)(wchar_t* __restrict, size_t, const wchar_t* __restrict, ...);

inline wide_printf get_swprintf() {
#  ifndef _LIBCPP_MSVCRT
  return swprintf;
#  else
  return static_cast<int(__cdecl*)(wchar_t* __restrict, size_t, const wchar_t* __restrict, ...)>(_snwprintf);
#  endif
}
#endif // _LIBCPP_HAS_NO_WIDE_CHARACTERS

template <typename S, typename V>
S i_to_string(V v) {
  //  numeric_limits::digits10 returns value less on 1 than desired for unsigned numbers.
  //  For example, for 1-byte unsigned value digits10 is 2 (999 can not be represented),
  //  so we need +1 here.
  constexpr size_t bufsize = numeric_limits<V>::digits10 + 2; // +1 for minus, +1 for digits10
  char buf[bufsize];
  const auto res = to_chars(buf, buf + bufsize, v);
  _LIBCPP_ASSERT_INTERNAL(res.ec == errc(), "bufsize must be large enough to accomodate the value");
  return S(buf, res.ptr);
}

} // unnamed namespace

string to_string(int val) { return i_to_string< string>(val); }
string to_string(long val) { return i_to_string< string>(val); }
string to_string(long long val) { return i_to_string< string>(val); }
string to_string(unsigned val) { return i_to_string< string>(val); }
string to_string(unsigned long val) { return i_to_string< string>(val); }
string to_string(unsigned long long val) { return i_to_string< string>(val); }

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
wstring to_wstring(int val) { return i_to_string<wstring>(val); }
wstring to_wstring(long val) { return i_to_string<wstring>(val); }
wstring to_wstring(long long val) { return i_to_string<wstring>(val); }
wstring to_wstring(unsigned val) { return i_to_string<wstring>(val); }
wstring to_wstring(unsigned long val) { return i_to_string<wstring>(val); }
wstring to_wstring(unsigned long long val) { return i_to_string<wstring>(val); }
#endif

string to_string(float val) { return as_string(snprintf, initial_string< string>()(), "%f", val); }
string to_string(double val) { return as_string(snprintf, initial_string< string>()(), "%f", val); }
string to_string(long double val) { return as_string(snprintf, initial_string< string>()(), "%Lf", val); }

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
wstring to_wstring(float val) { return as_string(get_swprintf(), initial_string<wstring>()(), L"%f", val); }
wstring to_wstring(double val) { return as_string(get_swprintf(), initial_string<wstring>()(), L"%f", val); }
wstring to_wstring(long double val) { return as_string(get_swprintf(), initial_string<wstring>()(), L"%Lf", val); }
#endif

_LIBCPP_END_NAMESPACE_STD
