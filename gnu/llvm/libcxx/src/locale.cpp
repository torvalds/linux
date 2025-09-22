//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <__utility/no_destroy.h>
#include <algorithm>
#include <clocale>
#include <codecvt>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <locale>
#include <new>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
#  include <cwctype>
#endif

#if defined(_AIX)
#  include <sys/localedef.h> // for __lc_ctype_ptr
#endif

#if defined(_LIBCPP_MSVCRT)
#  define _CTYPE_DISABLE_MACROS
#endif

#if !defined(_LIBCPP_MSVCRT) && !defined(__MINGW32__) && !defined(__BIONIC__) && !defined(__NuttX__)
#  include <langinfo.h>
#endif

#include "include/atomic_support.h"
#include "include/sso_allocator.h"

// On Linux, wint_t and wchar_t have different signed-ness, and this causes
// lots of noise in the build log, but no bugs that I know of.
_LIBCPP_CLANG_DIAGNOSTIC_IGNORED("-Wsign-conversion")

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

struct __libcpp_unique_locale {
  __libcpp_unique_locale(const char* nm) : __loc_(newlocale(LC_ALL_MASK, nm, 0)) {}

  ~__libcpp_unique_locale() {
    if (__loc_)
      freelocale(__loc_);
  }

  explicit operator bool() const { return __loc_; }

  locale_t& get() { return __loc_; }

  locale_t __loc_;

private:
  __libcpp_unique_locale(__libcpp_unique_locale const&);
  __libcpp_unique_locale& operator=(__libcpp_unique_locale const&);
};

#ifdef __cloc_defined
locale_t __cloc() {
  // In theory this could create a race condition. In practice
  // the race condition is non-fatal since it will just create
  // a little resource leak. Better approach would be appreciated.
  static locale_t result = newlocale(LC_ALL_MASK, "C", 0);
  return result;
}
#endif // __cloc_defined

namespace {

struct releaser {
  void operator()(locale::facet* p) { p->__release_shared(); }
};

template <class T, class... Args>
T& make(Args... args) {
  alignas(T) static std::byte buf[sizeof(T)];
  auto* obj = ::new (&buf) T(args...);
  return *obj;
}

template <typename T, size_t N>
inline constexpr size_t countof(const T (&)[N]) {
  return N;
}

template <typename T>
inline constexpr size_t countof(const T* const begin, const T* const end) {
  return static_cast<size_t>(end - begin);
}

string build_name(const string& other, const string& one, locale::category c) {
  if (other == "*" || one == "*")
    return "*";
  if (c == locale::none || other == one)
    return other;

  // FIXME: Handle the more complicated cases, such as when the locale has
  // different names for different categories.
  return "*";
}

} // namespace

const locale::category locale::none;
const locale::category locale::collate;
const locale::category locale::ctype;
const locale::category locale::monetary;
const locale::category locale::numeric;
const locale::category locale::time;
const locale::category locale::messages;
const locale::category locale::all;

class _LIBCPP_HIDDEN locale::__imp : public facet {
  enum { N = 30 };
  vector<facet*, __sso_allocator<facet*, N> > facets_;
  string name_;

public:
  explicit __imp(size_t refs = 0);
  explicit __imp(const string& name, size_t refs = 0);
  __imp(const __imp&);
  __imp(const __imp&, const string&, locale::category c);
  __imp(const __imp& other, const __imp& one, locale::category c);
  __imp(const __imp&, facet* f, long id);
  ~__imp();

  const string& name() const { return name_; }
  bool has_facet(long id) const { return static_cast<size_t>(id) < facets_.size() && facets_[static_cast<size_t>(id)]; }
  const locale::facet* use_facet(long id) const;

  void acquire();
  void release();
  static __no_destroy<__imp> classic_locale_imp_;

private:
  void install(facet* f, long id);
  template <class F>
  void install(F* f) {
    install(f, f->id.__get());
  }
  template <class F>
  void install_from(const __imp& other);
};

locale::__imp::__imp(size_t refs) : facet(refs), facets_(N), name_("C") {
  facets_.clear();
  install(&make<std::collate<char> >(1u));
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  install(&make<std::collate<wchar_t> >(1u));
#endif
  install(&make<std::ctype<char> >(nullptr, false, 1u));
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  install(&make<std::ctype<wchar_t> >(1u));
#endif
  install(&make<codecvt<char, char, mbstate_t> >(1u));
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  install(&make<codecvt<wchar_t, char, mbstate_t> >(1u));
#endif
  _LIBCPP_SUPPRESS_DEPRECATED_PUSH
  install(&make<codecvt<char16_t, char, mbstate_t> >(1u));
  install(&make<codecvt<char32_t, char, mbstate_t> >(1u));
  _LIBCPP_SUPPRESS_DEPRECATED_POP
#ifndef _LIBCPP_HAS_NO_CHAR8_T
  install(&make<codecvt<char16_t, char8_t, mbstate_t> >(1u));
  install(&make<codecvt<char32_t, char8_t, mbstate_t> >(1u));
#endif
  install(&make<numpunct<char> >(1u));
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  install(&make<numpunct<wchar_t> >(1u));
#endif
  install(&make<num_get<char> >(1u));
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  install(&make<num_get<wchar_t> >(1u));
#endif
  install(&make<num_put<char> >(1u));
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  install(&make<num_put<wchar_t> >(1u));
#endif
  install(&make<moneypunct<char, false> >(1u));
  install(&make<moneypunct<char, true> >(1u));
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  install(&make<moneypunct<wchar_t, false> >(1u));
  install(&make<moneypunct<wchar_t, true> >(1u));
#endif
  install(&make<money_get<char> >(1u));
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  install(&make<money_get<wchar_t> >(1u));
#endif
  install(&make<money_put<char> >(1u));
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  install(&make<money_put<wchar_t> >(1u));
#endif
  install(&make<time_get<char> >(1u));
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  install(&make<time_get<wchar_t> >(1u));
#endif
  install(&make<time_put<char> >(1u));
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  install(&make<time_put<wchar_t> >(1u));
#endif
  install(&make<std::messages<char> >(1u));
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  install(&make<std::messages<wchar_t> >(1u));
#endif
}

locale::__imp::__imp(const string& name, size_t refs) : facet(refs), facets_(N), name_(name) {
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  try {
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
    facets_ = locale::classic().__locale_->facets_;
    for (unsigned i = 0; i < facets_.size(); ++i)
      if (facets_[i])
        facets_[i]->__add_shared();
    install(new collate_byname<char>(name_));
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
    install(new collate_byname<wchar_t>(name_));
#endif
    install(new ctype_byname<char>(name_));
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
    install(new ctype_byname<wchar_t>(name_));
#endif
    install(new codecvt_byname<char, char, mbstate_t>(name_));
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
    install(new codecvt_byname<wchar_t, char, mbstate_t>(name_));
#endif
    _LIBCPP_SUPPRESS_DEPRECATED_PUSH
    install(new codecvt_byname<char16_t, char, mbstate_t>(name_));
    install(new codecvt_byname<char32_t, char, mbstate_t>(name_));
    _LIBCPP_SUPPRESS_DEPRECATED_POP
#ifndef _LIBCPP_HAS_NO_CHAR8_T
    install(new codecvt_byname<char16_t, char8_t, mbstate_t>(name_));
    install(new codecvt_byname<char32_t, char8_t, mbstate_t>(name_));
#endif
    install(new numpunct_byname<char>(name_));
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
    install(new numpunct_byname<wchar_t>(name_));
#endif
    install(new moneypunct_byname<char, false>(name_));
    install(new moneypunct_byname<char, true>(name_));
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
    install(new moneypunct_byname<wchar_t, false>(name_));
    install(new moneypunct_byname<wchar_t, true>(name_));
#endif
    install(new time_get_byname<char>(name_));
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
    install(new time_get_byname<wchar_t>(name_));
#endif
    install(new time_put_byname<char>(name_));
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
    install(new time_put_byname<wchar_t>(name_));
#endif
    install(new messages_byname<char>(name_));
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
    install(new messages_byname<wchar_t>(name_));
#endif
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  } catch (...) {
    for (unsigned i = 0; i < facets_.size(); ++i)
      if (facets_[i])
        facets_[i]->__release_shared();
    throw;
  }
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
}

locale::__imp::__imp(const __imp& other) : facets_(max<size_t>(N, other.facets_.size())), name_(other.name_) {
  facets_ = other.facets_;
  for (unsigned i = 0; i < facets_.size(); ++i)
    if (facets_[i])
      facets_[i]->__add_shared();
}

locale::__imp::__imp(const __imp& other, const string& name, locale::category c)
    : facets_(N), name_(build_name(other.name_, name, c)) {
  facets_ = other.facets_;
  for (unsigned i = 0; i < facets_.size(); ++i)
    if (facets_[i])
      facets_[i]->__add_shared();
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  try {
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
    if (c & locale::collate) {
      install(new collate_byname<char>(name));
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
      install(new collate_byname<wchar_t>(name));
#endif
    }
    if (c & locale::ctype) {
      install(new ctype_byname<char>(name));
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
      install(new ctype_byname<wchar_t>(name));
#endif
      install(new codecvt_byname<char, char, mbstate_t>(name));
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
      install(new codecvt_byname<wchar_t, char, mbstate_t>(name));
#endif
      _LIBCPP_SUPPRESS_DEPRECATED_PUSH
      install(new codecvt_byname<char16_t, char, mbstate_t>(name));
      install(new codecvt_byname<char32_t, char, mbstate_t>(name));
      _LIBCPP_SUPPRESS_DEPRECATED_POP
#ifndef _LIBCPP_HAS_NO_CHAR8_T
      install(new codecvt_byname<char16_t, char8_t, mbstate_t>(name));
      install(new codecvt_byname<char32_t, char8_t, mbstate_t>(name));
#endif
    }
    if (c & locale::monetary) {
      install(new moneypunct_byname<char, false>(name));
      install(new moneypunct_byname<char, true>(name));
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
      install(new moneypunct_byname<wchar_t, false>(name));
      install(new moneypunct_byname<wchar_t, true>(name));
#endif
    }
    if (c & locale::numeric) {
      install(new numpunct_byname<char>(name));
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
      install(new numpunct_byname<wchar_t>(name));
#endif
    }
    if (c & locale::time) {
      install(new time_get_byname<char>(name));
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
      install(new time_get_byname<wchar_t>(name));
#endif
      install(new time_put_byname<char>(name));
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
      install(new time_put_byname<wchar_t>(name));
#endif
    }
    if (c & locale::messages) {
      install(new messages_byname<char>(name));
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
      install(new messages_byname<wchar_t>(name));
#endif
    }
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  } catch (...) {
    for (unsigned i = 0; i < facets_.size(); ++i)
      if (facets_[i])
        facets_[i]->__release_shared();
    throw;
  }
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
}

template <class F>
inline void locale::__imp::install_from(const locale::__imp& one) {
  long id = F::id.__get();
  install(const_cast<F*>(static_cast<const F*>(one.use_facet(id))), id);
}

locale::__imp::__imp(const __imp& other, const __imp& one, locale::category c)
    : facets_(N), name_(build_name(other.name_, one.name_, c)) {
  facets_ = other.facets_;
  for (unsigned i = 0; i < facets_.size(); ++i)
    if (facets_[i])
      facets_[i]->__add_shared();
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  try {
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
    if (c & locale::collate) {
      install_from<std::collate<char> >(one);
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
      install_from<std::collate<wchar_t> >(one);
#endif
    }
    if (c & locale::ctype) {
      install_from<std::ctype<char> >(one);
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
      install_from<std::ctype<wchar_t> >(one);
#endif
      install_from<std::codecvt<char, char, mbstate_t> >(one);
      _LIBCPP_SUPPRESS_DEPRECATED_PUSH
      install_from<std::codecvt<char16_t, char, mbstate_t> >(one);
      install_from<std::codecvt<char32_t, char, mbstate_t> >(one);
      _LIBCPP_SUPPRESS_DEPRECATED_POP
#ifndef _LIBCPP_HAS_NO_CHAR8_T
      install_from<std::codecvt<char16_t, char8_t, mbstate_t> >(one);
      install_from<std::codecvt<char32_t, char8_t, mbstate_t> >(one);
#endif
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
      install_from<std::codecvt<wchar_t, char, mbstate_t> >(one);
#endif
    }
    if (c & locale::monetary) {
      install_from<moneypunct<char, false> >(one);
      install_from<moneypunct<char, true> >(one);
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
      install_from<moneypunct<wchar_t, false> >(one);
      install_from<moneypunct<wchar_t, true> >(one);
#endif
      install_from<money_get<char> >(one);
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
      install_from<money_get<wchar_t> >(one);
#endif
      install_from<money_put<char> >(one);
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
      install_from<money_put<wchar_t> >(one);
#endif
    }
    if (c & locale::numeric) {
      install_from<numpunct<char> >(one);
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
      install_from<numpunct<wchar_t> >(one);
#endif
      install_from<num_get<char> >(one);
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
      install_from<num_get<wchar_t> >(one);
#endif
      install_from<num_put<char> >(one);
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
      install_from<num_put<wchar_t> >(one);
#endif
    }
    if (c & locale::time) {
      install_from<time_get<char> >(one);
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
      install_from<time_get<wchar_t> >(one);
#endif
      install_from<time_put<char> >(one);
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
      install_from<time_put<wchar_t> >(one);
#endif
    }
    if (c & locale::messages) {
      install_from<std::messages<char> >(one);
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
      install_from<std::messages<wchar_t> >(one);
#endif
    }
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  } catch (...) {
    for (unsigned i = 0; i < facets_.size(); ++i)
      if (facets_[i])
        facets_[i]->__release_shared();
    throw;
  }
#endif // _LIBCPP_HAS_NO_EXCEPTIONS
}

locale::__imp::__imp(const __imp& other, facet* f, long id)
    : facets_(max<size_t>(N, other.facets_.size() + 1)), name_("*") {
  f->__add_shared();
  unique_ptr<facet, releaser> hold(f);
  facets_ = other.facets_;
  for (unsigned i = 0; i < other.facets_.size(); ++i)
    if (facets_[i])
      facets_[i]->__add_shared();
  install(hold.get(), id);
}

locale::__imp::~__imp() {
  for (unsigned i = 0; i < facets_.size(); ++i)
    if (facets_[i])
      facets_[i]->__release_shared();
}

void locale::__imp::install(facet* f, long id) {
  f->__add_shared();
  unique_ptr<facet, releaser> hold(f);
  if (static_cast<size_t>(id) >= facets_.size())
    facets_.resize(static_cast<size_t>(id + 1));
  if (facets_[static_cast<size_t>(id)])
    facets_[static_cast<size_t>(id)]->__release_shared();
  facets_[static_cast<size_t>(id)] = hold.release();
}

const locale::facet* locale::__imp::use_facet(long id) const {
  if (!has_facet(id))
    __throw_bad_cast();
  return facets_[static_cast<size_t>(id)];
}

// locale

// We don't do reference counting on the classic locale.
// It's never destroyed anyway, but atomic reference counting may be very
// expensive in parallel applications. The classic locale is used by default
// in all streams. Note: if a new global locale is installed, then we lose
// the benefit of no reference counting.
constinit __no_destroy<locale::__imp>
    locale::__imp::classic_locale_imp_(__uninitialized_tag{}); // initialized below in classic()

const locale& locale::classic() {
  static const __no_destroy<locale> classic_locale(__private_constructor_tag{}, [] {
    // executed exactly once on first initialization of `classic_locale`
    locale::__imp::classic_locale_imp_.__emplace(1u);
    return &locale::__imp::classic_locale_imp_.__get();
  }());
  return classic_locale.__get();
}

locale& locale::__global() {
  static __no_destroy<locale> g(locale::classic());
  return g.__get();
}

void locale::__imp::acquire() {
  if (this != &locale::__imp::classic_locale_imp_.__get())
    __add_shared();
}

void locale::__imp::release() {
  if (this != &locale::__imp::classic_locale_imp_.__get())
    __release_shared();
}

locale::locale() noexcept : __locale_(__global().__locale_) { __locale_->acquire(); }

locale::locale(const locale& l) noexcept : __locale_(l.__locale_) { __locale_->acquire(); }

locale::~locale() { __locale_->release(); }

const locale& locale::operator=(const locale& other) noexcept {
  other.__locale_->acquire();
  __locale_->release();
  __locale_ = other.__locale_;
  return *this;
}

locale::locale(const char* name)
    : __locale_(name ? new __imp(name) : (__throw_runtime_error("locale constructed with null"), nullptr)) {
  __locale_->acquire();
}

locale::locale(const string& name) : __locale_(new __imp(name)) { __locale_->acquire(); }

locale::locale(const locale& other, const char* name, category c)
    : __locale_(name ? new __imp(*other.__locale_, name, c)
                     : (__throw_runtime_error("locale constructed with null"), nullptr)) {
  __locale_->acquire();
}

locale::locale(const locale& other, const string& name, category c) : __locale_(new __imp(*other.__locale_, name, c)) {
  __locale_->acquire();
}

locale::locale(const locale& other, const locale& one, category c)
    : __locale_(new __imp(*other.__locale_, *one.__locale_, c)) {
  __locale_->acquire();
}

string locale::name() const { return __locale_->name(); }

void locale::__install_ctor(const locale& other, facet* f, long facet_id) {
  if (f)
    __locale_ = new __imp(*other.__locale_, f, facet_id);
  else
    __locale_ = other.__locale_;
  __locale_->acquire();
}

locale locale::global(const locale& loc) {
  locale& g = __global();
  locale r  = g;
  g         = loc;
  if (g.name() != "*")
    setlocale(LC_ALL, g.name().c_str());
  return r;
}

bool locale::has_facet(id& x) const { return __locale_->has_facet(x.__get()); }

const locale::facet* locale::use_facet(id& x) const { return __locale_->use_facet(x.__get()); }

bool locale::operator==(const locale& y) const {
  return (__locale_ == y.__locale_) || (__locale_->name() != "*" && __locale_->name() == y.__locale_->name());
}

// locale::facet

locale::facet::~facet() {}

void locale::facet::__on_zero_shared() noexcept { delete this; }

// locale::id

constinit int32_t locale::id::__next_id = 0;

long locale::id::__get() {
  call_once(__flag_, [&] { __id_ = __libcpp_atomic_add(&__next_id, 1); });
  return __id_ - 1;
}

// template <> class collate_byname<char>

collate_byname<char>::collate_byname(const char* n, size_t refs)
    : collate<char>(refs), __l_(newlocale(LC_ALL_MASK, n, 0)) {
  if (__l_ == 0)
    __throw_runtime_error(
        ("collate_byname<char>::collate_byname"
         " failed to construct for " +
         string(n))
            .c_str());
}

collate_byname<char>::collate_byname(const string& name, size_t refs)
    : collate<char>(refs), __l_(newlocale(LC_ALL_MASK, name.c_str(), 0)) {
  if (__l_ == 0)
    __throw_runtime_error(
        ("collate_byname<char>::collate_byname"
         " failed to construct for " +
         name)
            .c_str());
}

collate_byname<char>::~collate_byname() { freelocale(__l_); }

int collate_byname<char>::do_compare(
    const char_type* __lo1, const char_type* __hi1, const char_type* __lo2, const char_type* __hi2) const {
  string_type lhs(__lo1, __hi1);
  string_type rhs(__lo2, __hi2);
  int r = strcoll_l(lhs.c_str(), rhs.c_str(), __l_);
  if (r < 0)
    return -1;
  if (r > 0)
    return 1;
  return r;
}

collate_byname<char>::string_type collate_byname<char>::do_transform(const char_type* lo, const char_type* hi) const {
  const string_type in(lo, hi);
  string_type out(strxfrm_l(0, in.c_str(), 0, __l_), char());
  strxfrm_l(const_cast<char*>(out.c_str()), in.c_str(), out.size() + 1, __l_);
  return out;
}

// template <> class collate_byname<wchar_t>

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
collate_byname<wchar_t>::collate_byname(const char* n, size_t refs)
    : collate<wchar_t>(refs), __l_(newlocale(LC_ALL_MASK, n, 0)) {
  if (__l_ == 0)
    __throw_runtime_error(
        ("collate_byname<wchar_t>::collate_byname(size_t refs)"
         " failed to construct for " +
         string(n))
            .c_str());
}

collate_byname<wchar_t>::collate_byname(const string& name, size_t refs)
    : collate<wchar_t>(refs), __l_(newlocale(LC_ALL_MASK, name.c_str(), 0)) {
  if (__l_ == 0)
    __throw_runtime_error(
        ("collate_byname<wchar_t>::collate_byname(size_t refs)"
         " failed to construct for " +
         name)
            .c_str());
}

collate_byname<wchar_t>::~collate_byname() { freelocale(__l_); }

int collate_byname<wchar_t>::do_compare(
    const char_type* __lo1, const char_type* __hi1, const char_type* __lo2, const char_type* __hi2) const {
  string_type lhs(__lo1, __hi1);
  string_type rhs(__lo2, __hi2);
  int r = wcscoll_l(lhs.c_str(), rhs.c_str(), __l_);
  if (r < 0)
    return -1;
  if (r > 0)
    return 1;
  return r;
}

collate_byname<wchar_t>::string_type
collate_byname<wchar_t>::do_transform(const char_type* lo, const char_type* hi) const {
  const string_type in(lo, hi);
  string_type out(wcsxfrm_l(0, in.c_str(), 0, __l_), wchar_t());
  wcsxfrm_l(const_cast<wchar_t*>(out.c_str()), in.c_str(), out.size() + 1, __l_);
  return out;
}
#endif // _LIBCPP_HAS_NO_WIDE_CHARACTERS

const ctype_base::mask ctype_base::space;
const ctype_base::mask ctype_base::print;
const ctype_base::mask ctype_base::cntrl;
const ctype_base::mask ctype_base::upper;
const ctype_base::mask ctype_base::lower;
const ctype_base::mask ctype_base::alpha;
const ctype_base::mask ctype_base::digit;
const ctype_base::mask ctype_base::punct;
const ctype_base::mask ctype_base::xdigit;
const ctype_base::mask ctype_base::blank;
const ctype_base::mask ctype_base::alnum;
const ctype_base::mask ctype_base::graph;

// template <> class ctype<wchar_t>;

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
constinit locale::id ctype<wchar_t>::id;

ctype<wchar_t>::~ctype() {}

bool ctype<wchar_t>::do_is(mask m, char_type c) const {
  return isascii(c) ? (ctype<char>::classic_table()[c] & m) != 0 : false;
}

const wchar_t* ctype<wchar_t>::do_is(const char_type* low, const char_type* high, mask* vec) const {
  for (; low != high; ++low, ++vec)
    *vec = static_cast<mask>(isascii(*low) ? ctype<char>::classic_table()[*low] : 0);
  return low;
}

const wchar_t* ctype<wchar_t>::do_scan_is(mask m, const char_type* low, const char_type* high) const {
  for (; low != high; ++low)
    if (isascii(*low) && (ctype<char>::classic_table()[*low] & m))
      break;
  return low;
}

const wchar_t* ctype<wchar_t>::do_scan_not(mask m, const char_type* low, const char_type* high) const {
  for (; low != high; ++low)
    if (!(isascii(*low) && (ctype<char>::classic_table()[*low] & m)))
      break;
  return low;
}

wchar_t ctype<wchar_t>::do_toupper(char_type c) const {
#  ifdef _LIBCPP_HAS_DEFAULTRUNELOCALE
  return isascii(c) ? _DefaultRuneLocale.__mapupper[c] : c;
#  elif defined(__GLIBC__) || defined(__EMSCRIPTEN__) || defined(__NetBSD__) || defined(__MVS__)
  return isascii(c) ? ctype<char>::__classic_upper_table()[c] : c;
#  else
  return (isascii(c) && iswlower_l(c, _LIBCPP_GET_C_LOCALE)) ? c - L'a' + L'A' : c;
#  endif
}

const wchar_t* ctype<wchar_t>::do_toupper(char_type* low, const char_type* high) const {
  for (; low != high; ++low)
#  ifdef _LIBCPP_HAS_DEFAULTRUNELOCALE
    *low = isascii(*low) ? _DefaultRuneLocale.__mapupper[*low] : *low;
#  elif defined(__GLIBC__) || defined(__EMSCRIPTEN__) || defined(__NetBSD__) || defined(__MVS__)
    *low = isascii(*low) ? ctype<char>::__classic_upper_table()[*low] : *low;
#  else
    *low = (isascii(*low) && islower_l(*low, _LIBCPP_GET_C_LOCALE)) ? (*low - L'a' + L'A') : *low;
#  endif
  return low;
}

wchar_t ctype<wchar_t>::do_tolower(char_type c) const {
#  ifdef _LIBCPP_HAS_DEFAULTRUNELOCALE
  return isascii(c) ? _DefaultRuneLocale.__maplower[c] : c;
#  elif defined(__GLIBC__) || defined(__EMSCRIPTEN__) || defined(__NetBSD__) || defined(__MVS__)
  return isascii(c) ? ctype<char>::__classic_lower_table()[c] : c;
#  else
  return (isascii(c) && isupper_l(c, _LIBCPP_GET_C_LOCALE)) ? c - L'A' + 'a' : c;
#  endif
}

const wchar_t* ctype<wchar_t>::do_tolower(char_type* low, const char_type* high) const {
  for (; low != high; ++low)
#  ifdef _LIBCPP_HAS_DEFAULTRUNELOCALE
    *low = isascii(*low) ? _DefaultRuneLocale.__maplower[*low] : *low;
#  elif defined(__GLIBC__) || defined(__EMSCRIPTEN__) || defined(__NetBSD__) || defined(__MVS__)
    *low = isascii(*low) ? ctype<char>::__classic_lower_table()[*low] : *low;
#  else
    *low = (isascii(*low) && isupper_l(*low, _LIBCPP_GET_C_LOCALE)) ? *low - L'A' + L'a' : *low;
#  endif
  return low;
}

wchar_t ctype<wchar_t>::do_widen(char c) const { return c; }

const char* ctype<wchar_t>::do_widen(const char* low, const char* high, char_type* dest) const {
  for (; low != high; ++low, ++dest)
    *dest = *low;
  return low;
}

char ctype<wchar_t>::do_narrow(char_type c, char dfault) const {
  if (isascii(c))
    return static_cast<char>(c);
  return dfault;
}

const wchar_t* ctype<wchar_t>::do_narrow(const char_type* low, const char_type* high, char dfault, char* dest) const {
  for (; low != high; ++low, ++dest)
    if (isascii(*low))
      *dest = static_cast<char>(*low);
    else
      *dest = dfault;
  return low;
}
#endif // _LIBCPP_HAS_NO_WIDE_CHARACTERS

// template <> class ctype<char>;

constinit locale::id ctype<char>::id;

const size_t ctype<char>::table_size;

ctype<char>::ctype(const mask* tab, bool del, size_t refs) : locale::facet(refs), __tab_(tab), __del_(del) {
  if (__tab_ == 0)
    __tab_ = classic_table();
}

ctype<char>::~ctype() {
  if (__tab_ && __del_)
    delete[] __tab_;
}

char ctype<char>::do_toupper(char_type c) const {
#ifdef _LIBCPP_HAS_DEFAULTRUNELOCALE
  return isascii(c) ? static_cast<char>(_DefaultRuneLocale.__mapupper[static_cast<ptrdiff_t>(c)]) : c;
#elif defined(__NetBSD__)
  return static_cast<char>(__classic_upper_table()[static_cast<unsigned char>(c)]);
#elif defined(__GLIBC__) || defined(__EMSCRIPTEN__) || defined(__MVS__)
  return isascii(c) ? static_cast<char>(__classic_upper_table()[static_cast<unsigned char>(c)]) : c;
#else
  return (isascii(c) && islower_l(c, _LIBCPP_GET_C_LOCALE)) ? c - 'a' + 'A' : c;
#endif
}

const char* ctype<char>::do_toupper(char_type* low, const char_type* high) const {
  for (; low != high; ++low)
#ifdef _LIBCPP_HAS_DEFAULTRUNELOCALE
    *low = isascii(*low) ? static_cast<char>(_DefaultRuneLocale.__mapupper[static_cast<ptrdiff_t>(*low)]) : *low;
#elif defined(__NetBSD__)
    *low = static_cast<char>(__classic_upper_table()[static_cast<unsigned char>(*low)]);
#elif defined(__GLIBC__) || defined(__EMSCRIPTEN__) || defined(__MVS__)
    *low = isascii(*low) ? static_cast<char>(__classic_upper_table()[static_cast<size_t>(*low)]) : *low;
#else
    *low = (isascii(*low) && islower_l(*low, _LIBCPP_GET_C_LOCALE)) ? *low - 'a' + 'A' : *low;
#endif
  return low;
}

char ctype<char>::do_tolower(char_type c) const {
#ifdef _LIBCPP_HAS_DEFAULTRUNELOCALE
  return isascii(c) ? static_cast<char>(_DefaultRuneLocale.__maplower[static_cast<ptrdiff_t>(c)]) : c;
#elif defined(__NetBSD__)
  return static_cast<char>(__classic_lower_table()[static_cast<unsigned char>(c)]);
#elif defined(__GLIBC__) || defined(__EMSCRIPTEN__) || defined(__MVS__)
  return isascii(c) ? static_cast<char>(__classic_lower_table()[static_cast<size_t>(c)]) : c;
#else
  return (isascii(c) && isupper_l(c, _LIBCPP_GET_C_LOCALE)) ? c - 'A' + 'a' : c;
#endif
}

const char* ctype<char>::do_tolower(char_type* low, const char_type* high) const {
  for (; low != high; ++low)
#ifdef _LIBCPP_HAS_DEFAULTRUNELOCALE
    *low = isascii(*low) ? static_cast<char>(_DefaultRuneLocale.__maplower[static_cast<ptrdiff_t>(*low)]) : *low;
#elif defined(__NetBSD__)
    *low = static_cast<char>(__classic_lower_table()[static_cast<unsigned char>(*low)]);
#elif defined(__GLIBC__) || defined(__EMSCRIPTEN__) || defined(__MVS__)
    *low = isascii(*low) ? static_cast<char>(__classic_lower_table()[static_cast<size_t>(*low)]) : *low;
#else
    *low = (isascii(*low) && isupper_l(*low, _LIBCPP_GET_C_LOCALE)) ? *low - 'A' + 'a' : *low;
#endif
  return low;
}

char ctype<char>::do_widen(char c) const { return c; }

const char* ctype<char>::do_widen(const char* low, const char* high, char_type* dest) const {
  for (; low != high; ++low, ++dest)
    *dest = *low;
  return low;
}

char ctype<char>::do_narrow(char_type c, char dfault) const {
  if (isascii(c))
    return static_cast<char>(c);
  return dfault;
}

const char* ctype<char>::do_narrow(const char_type* low, const char_type* high, char dfault, char* dest) const {
  for (; low != high; ++low, ++dest)
    if (isascii(*low))
      *dest = *low;
    else
      *dest = dfault;
  return low;
}

#if defined(__EMSCRIPTEN__)
extern "C" const unsigned short** __ctype_b_loc();
extern "C" const int** __ctype_tolower_loc();
extern "C" const int** __ctype_toupper_loc();
#endif

#ifdef _LIBCPP_PROVIDES_DEFAULT_RUNE_TABLE
const ctype<char>::mask* ctype<char>::classic_table() noexcept {
  // clang-format off
    static constexpr const ctype<char>::mask builtin_table[table_size] = {
        cntrl,                          cntrl,
        cntrl,                          cntrl,
        cntrl,                          cntrl,
        cntrl,                          cntrl,
        cntrl,                          cntrl | space | blank,
        cntrl | space,                  cntrl | space,
        cntrl | space,                  cntrl | space,
        cntrl,                          cntrl,
        cntrl,                          cntrl,
        cntrl,                          cntrl,
        cntrl,                          cntrl,
        cntrl,                          cntrl,
        cntrl,                          cntrl,
        cntrl,                          cntrl,
        cntrl,                          cntrl,
        cntrl,                          cntrl,
        space | blank | print,          punct | print,
        punct | print,                  punct | print,
        punct | print,                  punct | print,
        punct | print,                  punct | print,
        punct | print,                  punct | print,
        punct | print,                  punct | print,
        punct | print,                  punct | print,
        punct | print,                  punct | print,
        digit | print | xdigit,         digit | print | xdigit,
        digit | print | xdigit,         digit | print | xdigit,
        digit | print | xdigit,         digit | print | xdigit,
        digit | print | xdigit,         digit | print | xdigit,
        digit | print | xdigit,         digit | print | xdigit,
        punct | print,                  punct | print,
        punct | print,                  punct | print,
        punct | print,                  punct | print,
        punct | print,                  upper | xdigit | print | alpha,
        upper | xdigit | print | alpha, upper | xdigit | print | alpha,
        upper | xdigit | print | alpha, upper | xdigit | print | alpha,
        upper | xdigit | print | alpha, upper | print | alpha,
        upper | print | alpha,          upper | print | alpha,
        upper | print | alpha,          upper | print | alpha,
        upper | print | alpha,          upper | print | alpha,
        upper | print | alpha,          upper | print | alpha,
        upper | print | alpha,          upper | print | alpha,
        upper | print | alpha,          upper | print | alpha,
        upper | print | alpha,          upper | print | alpha,
        upper | print | alpha,          upper | print | alpha,
        upper | print | alpha,          upper | print | alpha,
        upper | print | alpha,          punct | print,
        punct | print,                  punct | print,
        punct | print,                  punct | print,
        punct | print,                  lower | xdigit | print | alpha,
        lower | xdigit | print | alpha, lower | xdigit | print | alpha,
        lower | xdigit | print | alpha, lower | xdigit | print | alpha,
        lower | xdigit | print | alpha, lower | print | alpha,
        lower | print | alpha,          lower | print | alpha,
        lower | print | alpha,          lower | print | alpha,
        lower | print | alpha,          lower | print | alpha,
        lower | print | alpha,          lower | print | alpha,
        lower | print | alpha,          lower | print | alpha,
        lower | print | alpha,          lower | print | alpha,
        lower | print | alpha,          lower | print | alpha,
        lower | print | alpha,          lower | print | alpha,
        lower | print | alpha,          lower | print | alpha,
        lower | print | alpha,          punct | print,
        punct | print,                  punct | print,
        punct | print,                  cntrl,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };
  // clang-format on
  return builtin_table;
}
#else
const ctype<char>::mask* ctype<char>::classic_table() noexcept {
#  if defined(__APPLE__) || defined(__FreeBSD__)
  return _DefaultRuneLocale.__runetype;
#  elif defined(__NetBSD__)
  return _C_ctype_tab_ + 1;
#  elif defined(__GLIBC__)
  return _LIBCPP_GET_C_LOCALE->__ctype_b;
#  elif defined(_LIBCPP_MSVCRT) || defined(__MINGW32__)
  return __pctype_func();
#  elif defined(__EMSCRIPTEN__)
  return *__ctype_b_loc();
#  elif defined(_NEWLIB_VERSION)
  // Newlib has a 257-entry table in ctype_.c, where (char)0 starts at [1].
  return _ctype_ + 1;
#  elif defined(_AIX)
  return (const unsigned int*)__lc_ctype_ptr->obj->mask;
#  elif defined(__MVS__)
#    if defined(__NATIVE_ASCII_F)
  return const_cast<const ctype<char>::mask*>(__OBJ_DATA(__lc_ctype_a)->mask);
#    else
  return const_cast<const ctype<char>::mask*>(__ctypec);
#    endif
#  else
  // Platform not supported: abort so the person doing the port knows what to
  // fix
#    warning ctype<char>::classic_table() is not implemented
  printf("ctype<char>::classic_table() is not implemented\n");
  abort();
  return NULL;
#  endif
}
#endif

#if defined(__GLIBC__)
const int* ctype<char>::__classic_lower_table() noexcept { return _LIBCPP_GET_C_LOCALE->__ctype_tolower; }

const int* ctype<char>::__classic_upper_table() noexcept { return _LIBCPP_GET_C_LOCALE->__ctype_toupper; }
#elif defined(__NetBSD__)
const short* ctype<char>::__classic_lower_table() noexcept { return _C_tolower_tab_ + 1; }

const short* ctype<char>::__classic_upper_table() noexcept { return _C_toupper_tab_ + 1; }

#elif defined(__EMSCRIPTEN__)
const int* ctype<char>::__classic_lower_table() noexcept { return *__ctype_tolower_loc(); }

const int* ctype<char>::__classic_upper_table() noexcept { return *__ctype_toupper_loc(); }
#elif defined(__MVS__)
const unsigned short* ctype<char>::__classic_lower_table() _NOEXCEPT {
#  if defined(__NATIVE_ASCII_F)
  return const_cast<const unsigned short*>(__OBJ_DATA(__lc_ctype_a)->lower);
#  else
  return const_cast<const unsigned short*>(__ctype + __TOLOWER_INDEX);
#  endif
}
const unsigned short* ctype<char>::__classic_upper_table() _NOEXCEPT {
#  if defined(__NATIVE_ASCII_F)
  return const_cast<const unsigned short*>(__OBJ_DATA(__lc_ctype_a)->upper);
#  else
  return const_cast<const unsigned short*>(__ctype + __TOUPPER_INDEX);
#  endif
}
#endif // __GLIBC__ || __NETBSD__ || __EMSCRIPTEN__ || __MVS__

// template <> class ctype_byname<char>

ctype_byname<char>::ctype_byname(const char* name, size_t refs)
    : ctype<char>(0, false, refs), __l_(newlocale(LC_ALL_MASK, name, 0)) {
  if (__l_ == 0)
    __throw_runtime_error(
        ("ctype_byname<char>::ctype_byname"
         " failed to construct for " +
         string(name))
            .c_str());
}

ctype_byname<char>::ctype_byname(const string& name, size_t refs)
    : ctype<char>(0, false, refs), __l_(newlocale(LC_ALL_MASK, name.c_str(), 0)) {
  if (__l_ == 0)
    __throw_runtime_error(
        ("ctype_byname<char>::ctype_byname"
         " failed to construct for " +
         name)
            .c_str());
}

ctype_byname<char>::~ctype_byname() { freelocale(__l_); }

char ctype_byname<char>::do_toupper(char_type c) const {
  return static_cast<char>(toupper_l(static_cast<unsigned char>(c), __l_));
}

const char* ctype_byname<char>::do_toupper(char_type* low, const char_type* high) const {
  for (; low != high; ++low)
    *low = static_cast<char>(toupper_l(static_cast<unsigned char>(*low), __l_));
  return low;
}

char ctype_byname<char>::do_tolower(char_type c) const {
  return static_cast<char>(tolower_l(static_cast<unsigned char>(c), __l_));
}

const char* ctype_byname<char>::do_tolower(char_type* low, const char_type* high) const {
  for (; low != high; ++low)
    *low = static_cast<char>(tolower_l(static_cast<unsigned char>(*low), __l_));
  return low;
}

// template <> class ctype_byname<wchar_t>

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
ctype_byname<wchar_t>::ctype_byname(const char* name, size_t refs)
    : ctype<wchar_t>(refs), __l_(newlocale(LC_ALL_MASK, name, 0)) {
  if (__l_ == 0)
    __throw_runtime_error(
        ("ctype_byname<wchar_t>::ctype_byname"
         " failed to construct for " +
         string(name))
            .c_str());
}

ctype_byname<wchar_t>::ctype_byname(const string& name, size_t refs)
    : ctype<wchar_t>(refs), __l_(newlocale(LC_ALL_MASK, name.c_str(), 0)) {
  if (__l_ == 0)
    __throw_runtime_error(
        ("ctype_byname<wchar_t>::ctype_byname"
         " failed to construct for " +
         name)
            .c_str());
}

ctype_byname<wchar_t>::~ctype_byname() { freelocale(__l_); }

bool ctype_byname<wchar_t>::do_is(mask m, char_type c) const {
#  ifdef _LIBCPP_WCTYPE_IS_MASK
  return static_cast<bool>(iswctype_l(c, m, __l_));
#  else
  bool result = false;
  wint_t ch   = static_cast<wint_t>(c);
  if ((m & space) == space)
    result |= (iswspace_l(ch, __l_) != 0);
  if ((m & print) == print)
    result |= (iswprint_l(ch, __l_) != 0);
  if ((m & cntrl) == cntrl)
    result |= (iswcntrl_l(ch, __l_) != 0);
  if ((m & upper) == upper)
    result |= (iswupper_l(ch, __l_) != 0);
  if ((m & lower) == lower)
    result |= (iswlower_l(ch, __l_) != 0);
  if ((m & alpha) == alpha)
    result |= (iswalpha_l(ch, __l_) != 0);
  if ((m & digit) == digit)
    result |= (iswdigit_l(ch, __l_) != 0);
  if ((m & punct) == punct)
    result |= (iswpunct_l(ch, __l_) != 0);
  if ((m & xdigit) == xdigit)
    result |= (iswxdigit_l(ch, __l_) != 0);
  if ((m & blank) == blank)
    result |= (iswblank_l(ch, __l_) != 0);
  return result;
#  endif
}

const wchar_t* ctype_byname<wchar_t>::do_is(const char_type* low, const char_type* high, mask* vec) const {
  for (; low != high; ++low, ++vec) {
    if (isascii(*low))
      *vec = static_cast<mask>(ctype<char>::classic_table()[*low]);
    else {
      *vec      = 0;
      wint_t ch = static_cast<wint_t>(*low);
      if (iswspace_l(ch, __l_))
        *vec |= space;
#  ifndef _LIBCPP_CTYPE_MASK_IS_COMPOSITE_PRINT
      if (iswprint_l(ch, __l_))
        *vec |= print;
#  endif
      if (iswcntrl_l(ch, __l_))
        *vec |= cntrl;
      if (iswupper_l(ch, __l_))
        *vec |= upper;
      if (iswlower_l(ch, __l_))
        *vec |= lower;
#  ifndef _LIBCPP_CTYPE_MASK_IS_COMPOSITE_ALPHA
      if (iswalpha_l(ch, __l_))
        *vec |= alpha;
#  endif
      if (iswdigit_l(ch, __l_))
        *vec |= digit;
      if (iswpunct_l(ch, __l_))
        *vec |= punct;
#  ifndef _LIBCPP_CTYPE_MASK_IS_COMPOSITE_XDIGIT
      if (iswxdigit_l(ch, __l_))
        *vec |= xdigit;
#  endif
      if (iswblank_l(ch, __l_))
        *vec |= blank;
    }
  }
  return low;
}

const wchar_t* ctype_byname<wchar_t>::do_scan_is(mask m, const char_type* low, const char_type* high) const {
  for (; low != high; ++low) {
#  ifdef _LIBCPP_WCTYPE_IS_MASK
    if (iswctype_l(*low, m, __l_))
      break;
#  else
    wint_t ch = static_cast<wint_t>(*low);
    if ((m & space) == space && iswspace_l(ch, __l_))
      break;
    if ((m & print) == print && iswprint_l(ch, __l_))
      break;
    if ((m & cntrl) == cntrl && iswcntrl_l(ch, __l_))
      break;
    if ((m & upper) == upper && iswupper_l(ch, __l_))
      break;
    if ((m & lower) == lower && iswlower_l(ch, __l_))
      break;
    if ((m & alpha) == alpha && iswalpha_l(ch, __l_))
      break;
    if ((m & digit) == digit && iswdigit_l(ch, __l_))
      break;
    if ((m & punct) == punct && iswpunct_l(ch, __l_))
      break;
    if ((m & xdigit) == xdigit && iswxdigit_l(ch, __l_))
      break;
    if ((m & blank) == blank && iswblank_l(ch, __l_))
      break;
#  endif
  }
  return low;
}

const wchar_t* ctype_byname<wchar_t>::do_scan_not(mask m, const char_type* low, const char_type* high) const {
  for (; low != high; ++low) {
#  ifdef _LIBCPP_WCTYPE_IS_MASK
    if (!iswctype_l(*low, m, __l_))
      break;
#  else
    wint_t ch = static_cast<wint_t>(*low);
    if ((m & space) == space && iswspace_l(ch, __l_))
      continue;
    if ((m & print) == print && iswprint_l(ch, __l_))
      continue;
    if ((m & cntrl) == cntrl && iswcntrl_l(ch, __l_))
      continue;
    if ((m & upper) == upper && iswupper_l(ch, __l_))
      continue;
    if ((m & lower) == lower && iswlower_l(ch, __l_))
      continue;
    if ((m & alpha) == alpha && iswalpha_l(ch, __l_))
      continue;
    if ((m & digit) == digit && iswdigit_l(ch, __l_))
      continue;
    if ((m & punct) == punct && iswpunct_l(ch, __l_))
      continue;
    if ((m & xdigit) == xdigit && iswxdigit_l(ch, __l_))
      continue;
    if ((m & blank) == blank && iswblank_l(ch, __l_))
      continue;
    break;
#  endif
  }
  return low;
}

wchar_t ctype_byname<wchar_t>::do_toupper(char_type c) const { return towupper_l(c, __l_); }

const wchar_t* ctype_byname<wchar_t>::do_toupper(char_type* low, const char_type* high) const {
  for (; low != high; ++low)
    *low = towupper_l(*low, __l_);
  return low;
}

wchar_t ctype_byname<wchar_t>::do_tolower(char_type c) const { return towlower_l(c, __l_); }

const wchar_t* ctype_byname<wchar_t>::do_tolower(char_type* low, const char_type* high) const {
  for (; low != high; ++low)
    *low = towlower_l(*low, __l_);
  return low;
}

wchar_t ctype_byname<wchar_t>::do_widen(char c) const { return __libcpp_btowc_l(c, __l_); }

const char* ctype_byname<wchar_t>::do_widen(const char* low, const char* high, char_type* dest) const {
  for (; low != high; ++low, ++dest)
    *dest = __libcpp_btowc_l(*low, __l_);
  return low;
}

char ctype_byname<wchar_t>::do_narrow(char_type c, char dfault) const {
  int r = __libcpp_wctob_l(c, __l_);
  return (r != EOF) ? static_cast<char>(r) : dfault;
}

const wchar_t*
ctype_byname<wchar_t>::do_narrow(const char_type* low, const char_type* high, char dfault, char* dest) const {
  for (; low != high; ++low, ++dest) {
    int r = __libcpp_wctob_l(*low, __l_);
    *dest = (r != EOF) ? static_cast<char>(r) : dfault;
  }
  return low;
}
#endif // _LIBCPP_HAS_NO_WIDE_CHARACTERS

// template <> class codecvt<char, char, mbstate_t>

constinit locale::id codecvt<char, char, mbstate_t>::id;

codecvt<char, char, mbstate_t>::~codecvt() {}

codecvt<char, char, mbstate_t>::result codecvt<char, char, mbstate_t>::do_out(
    state_type&,
    const intern_type* frm,
    const intern_type*,
    const intern_type*& frm_nxt,
    extern_type* to,
    extern_type*,
    extern_type*& to_nxt) const {
  frm_nxt = frm;
  to_nxt  = to;
  return noconv;
}

codecvt<char, char, mbstate_t>::result codecvt<char, char, mbstate_t>::do_in(
    state_type&,
    const extern_type* frm,
    const extern_type*,
    const extern_type*& frm_nxt,
    intern_type* to,
    intern_type*,
    intern_type*& to_nxt) const {
  frm_nxt = frm;
  to_nxt  = to;
  return noconv;
}

codecvt<char, char, mbstate_t>::result
codecvt<char, char, mbstate_t>::do_unshift(state_type&, extern_type* to, extern_type*, extern_type*& to_nxt) const {
  to_nxt = to;
  return noconv;
}

int codecvt<char, char, mbstate_t>::do_encoding() const noexcept { return 1; }

bool codecvt<char, char, mbstate_t>::do_always_noconv() const noexcept { return true; }

int codecvt<char, char, mbstate_t>::do_length(
    state_type&, const extern_type* frm, const extern_type* end, size_t mx) const {
  return static_cast<int>(min<size_t>(mx, static_cast<size_t>(end - frm)));
}

int codecvt<char, char, mbstate_t>::do_max_length() const noexcept { return 1; }

// template <> class codecvt<wchar_t, char, mbstate_t>

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
constinit locale::id codecvt<wchar_t, char, mbstate_t>::id;

codecvt<wchar_t, char, mbstate_t>::codecvt(size_t refs) : locale::facet(refs), __l_(_LIBCPP_GET_C_LOCALE) {}

codecvt<wchar_t, char, mbstate_t>::codecvt(const char* nm, size_t refs)
    : locale::facet(refs), __l_(newlocale(LC_ALL_MASK, nm, 0)) {
  if (__l_ == 0)
    __throw_runtime_error(
        ("codecvt_byname<wchar_t, char, mbstate_t>::codecvt_byname"
         " failed to construct for " +
         string(nm))
            .c_str());
}

codecvt<wchar_t, char, mbstate_t>::~codecvt() {
  if (__l_ != _LIBCPP_GET_C_LOCALE)
    freelocale(__l_);
}

codecvt<wchar_t, char, mbstate_t>::result codecvt<wchar_t, char, mbstate_t>::do_out(
    state_type& st,
    const intern_type* frm,
    const intern_type* frm_end,
    const intern_type*& frm_nxt,
    extern_type* to,
    extern_type* to_end,
    extern_type*& to_nxt) const {
  // look for first internal null in frm
  const intern_type* fend = frm;
  for (; fend != frm_end; ++fend)
    if (*fend == 0)
      break;
  // loop over all null-terminated sequences in frm
  to_nxt = to;
  for (frm_nxt = frm; frm != frm_end && to != to_end; frm = frm_nxt, to = to_nxt) {
    // save state in case it is needed to recover to_nxt on error
    mbstate_t save_state = st;
    size_t n             = __libcpp_wcsnrtombs_l(
        to, &frm_nxt, static_cast<size_t>(fend - frm), static_cast<size_t>(to_end - to), &st, __l_);
    if (n == size_t(-1)) {
      // need to recover to_nxt
      for (to_nxt = to; frm != frm_nxt; ++frm) {
        n = __libcpp_wcrtomb_l(to_nxt, *frm, &save_state, __l_);
        if (n == size_t(-1))
          break;
        to_nxt += n;
      }
      frm_nxt = frm;
      return error;
    }
    if (n == 0)
      return partial;
    to_nxt += n;
    if (to_nxt == to_end)
      break;
    if (fend != frm_end) // set up next null terminated sequence
    {
      // Try to write the terminating null
      extern_type tmp[MB_LEN_MAX];
      n = __libcpp_wcrtomb_l(tmp, intern_type(), &st, __l_);
      if (n == size_t(-1)) // on error
        return error;
      if (n > static_cast<size_t>(to_end - to_nxt)) // is there room?
        return partial;
      for (extern_type* p = tmp; n; --n) // write it
        *to_nxt++ = *p++;
      ++frm_nxt;
      // look for next null in frm
      for (fend = frm_nxt; fend != frm_end; ++fend)
        if (*fend == 0)
          break;
    }
  }
  return frm_nxt == frm_end ? ok : partial;
}

codecvt<wchar_t, char, mbstate_t>::result codecvt<wchar_t, char, mbstate_t>::do_in(
    state_type& st,
    const extern_type* frm,
    const extern_type* frm_end,
    const extern_type*& frm_nxt,
    intern_type* to,
    intern_type* to_end,
    intern_type*& to_nxt) const {
  // look for first internal null in frm
  const extern_type* fend = frm;
  for (; fend != frm_end; ++fend)
    if (*fend == 0)
      break;
  // loop over all null-terminated sequences in frm
  to_nxt = to;
  for (frm_nxt = frm; frm != frm_end && to != to_end; frm = frm_nxt, to = to_nxt) {
    // save state in case it is needed to recover to_nxt on error
    mbstate_t save_state = st;
    size_t n             = __libcpp_mbsnrtowcs_l(
        to, &frm_nxt, static_cast<size_t>(fend - frm), static_cast<size_t>(to_end - to), &st, __l_);
    if (n == size_t(-1)) {
      // need to recover to_nxt
      for (to_nxt = to; frm != frm_nxt; ++to_nxt) {
        n = __libcpp_mbrtowc_l(to_nxt, frm, static_cast<size_t>(fend - frm), &save_state, __l_);
        switch (n) {
        case 0:
          ++frm;
          break;
        case size_t(-1):
          frm_nxt = frm;
          return error;
        case size_t(-2):
          frm_nxt = frm;
          return partial;
        default:
          frm += n;
          break;
        }
      }
      frm_nxt = frm;
      return frm_nxt == frm_end ? ok : partial;
    }
    if (n == size_t(-1))
      return error;
    to_nxt += n;
    if (to_nxt == to_end)
      break;
    if (fend != frm_end) // set up next null terminated sequence
    {
      // Try to write the terminating null
      n = __libcpp_mbrtowc_l(to_nxt, frm_nxt, 1, &st, __l_);
      if (n != 0) // on error
        return error;
      ++to_nxt;
      ++frm_nxt;
      // look for next null in frm
      for (fend = frm_nxt; fend != frm_end; ++fend)
        if (*fend == 0)
          break;
    }
  }
  return frm_nxt == frm_end ? ok : partial;
}

codecvt<wchar_t, char, mbstate_t>::result codecvt<wchar_t, char, mbstate_t>::do_unshift(
    state_type& st, extern_type* to, extern_type* to_end, extern_type*& to_nxt) const {
  to_nxt = to;
  extern_type tmp[MB_LEN_MAX];
  size_t n = __libcpp_wcrtomb_l(tmp, intern_type(), &st, __l_);
  if (n == size_t(-1) || n == 0) // on error
    return error;
  --n;
  if (n > static_cast<size_t>(to_end - to_nxt)) // is there room?
    return partial;
  for (extern_type* p = tmp; n; --n) // write it
    *to_nxt++ = *p++;
  return ok;
}

int codecvt<wchar_t, char, mbstate_t>::do_encoding() const noexcept {
  if (__libcpp_mbtowc_l(nullptr, nullptr, MB_LEN_MAX, __l_) != 0)
    return -1;

  // stateless encoding
  if (__l_ == 0 || __libcpp_mb_cur_max_l(__l_) == 1) // there are no known constant length encodings
    return 1;                                        // which take more than 1 char to form a wchar_t
  return 0;
}

bool codecvt<wchar_t, char, mbstate_t>::do_always_noconv() const noexcept { return false; }

int codecvt<wchar_t, char, mbstate_t>::do_length(
    state_type& st, const extern_type* frm, const extern_type* frm_end, size_t mx) const {
  int nbytes = 0;
  for (size_t nwchar_t = 0; nwchar_t < mx && frm != frm_end; ++nwchar_t) {
    size_t n = __libcpp_mbrlen_l(frm, static_cast<size_t>(frm_end - frm), &st, __l_);
    switch (n) {
    case 0:
      ++nbytes;
      ++frm;
      break;
    case size_t(-1):
    case size_t(-2):
      return nbytes;
    default:
      nbytes += n;
      frm += n;
      break;
    }
  }
  return nbytes;
}

int codecvt<wchar_t, char, mbstate_t>::do_max_length() const noexcept {
  return __l_ == 0 ? 1 : static_cast<int>(__libcpp_mb_cur_max_l(__l_));
}
#endif // _LIBCPP_HAS_NO_WIDE_CHARACTERS

//                                     Valid UTF ranges
//     UTF-32               UTF-16                          UTF-8               # of code points
//                     first      second       first   second    third   fourth
// 000000 - 00007F  0000 - 007F               00 - 7F                                 127
// 000080 - 0007FF  0080 - 07FF               C2 - DF, 80 - BF                       1920
// 000800 - 000FFF  0800 - 0FFF               E0 - E0, A0 - BF, 80 - BF              2048
// 001000 - 00CFFF  1000 - CFFF               E1 - EC, 80 - BF, 80 - BF             49152
// 00D000 - 00D7FF  D000 - D7FF               ED - ED, 80 - 9F, 80 - BF              2048
// 00D800 - 00DFFF                invalid
// 00E000 - 00FFFF  E000 - FFFF               EE - EF, 80 - BF, 80 - BF              8192
// 010000 - 03FFFF  D800 - D8BF, DC00 - DFFF  F0 - F0, 90 - BF, 80 - BF, 80 - BF   196608
// 040000 - 0FFFFF  D8C0 - DBBF, DC00 - DFFF  F1 - F3, 80 - BF, 80 - BF, 80 - BF   786432
// 100000 - 10FFFF  DBC0 - DBFF, DC00 - DFFF  F4 - F4, 80 - 8F, 80 - BF, 80 - BF    65536

_LIBCPP_SUPPRESS_DEPRECATED_PUSH
static codecvt_base::result utf16_to_utf8(
    const uint16_t* frm,
    const uint16_t* frm_end,
    const uint16_t*& frm_nxt,
    uint8_t* to,
    uint8_t* to_end,
    uint8_t*& to_nxt,
    unsigned long Maxcode = 0x10FFFF,
    codecvt_mode mode     = codecvt_mode(0)) {
  frm_nxt = frm;
  to_nxt  = to;
  if (mode & generate_header) {
    if (to_end - to_nxt < 3)
      return codecvt_base::partial;
    *to_nxt++ = static_cast<uint8_t>(0xEF);
    *to_nxt++ = static_cast<uint8_t>(0xBB);
    *to_nxt++ = static_cast<uint8_t>(0xBF);
  }
  for (; frm_nxt < frm_end; ++frm_nxt) {
    uint16_t wc1 = *frm_nxt;
    if (wc1 > Maxcode)
      return codecvt_base::error;
    if (wc1 < 0x0080) {
      if (to_end - to_nxt < 1)
        return codecvt_base::partial;
      *to_nxt++ = static_cast<uint8_t>(wc1);
    } else if (wc1 < 0x0800) {
      if (to_end - to_nxt < 2)
        return codecvt_base::partial;
      *to_nxt++ = static_cast<uint8_t>(0xC0 | (wc1 >> 6));
      *to_nxt++ = static_cast<uint8_t>(0x80 | (wc1 & 0x03F));
    } else if (wc1 < 0xD800) {
      if (to_end - to_nxt < 3)
        return codecvt_base::partial;
      *to_nxt++ = static_cast<uint8_t>(0xE0 | (wc1 >> 12));
      *to_nxt++ = static_cast<uint8_t>(0x80 | ((wc1 & 0x0FC0) >> 6));
      *to_nxt++ = static_cast<uint8_t>(0x80 | (wc1 & 0x003F));
    } else if (wc1 < 0xDC00) {
      if (frm_end - frm_nxt < 2)
        return codecvt_base::partial;
      uint16_t wc2 = frm_nxt[1];
      if ((wc2 & 0xFC00) != 0xDC00)
        return codecvt_base::error;
      if (to_end - to_nxt < 4)
        return codecvt_base::partial;
      if (((((wc1 & 0x03C0UL) >> 6) + 1) << 16) + ((wc1 & 0x003FUL) << 10) + (wc2 & 0x03FF) > Maxcode)
        return codecvt_base::error;
      ++frm_nxt;
      uint8_t z = ((wc1 & 0x03C0) >> 6) + 1;
      *to_nxt++ = static_cast<uint8_t>(0xF0 | (z >> 2));
      *to_nxt++ = static_cast<uint8_t>(0x80 | ((z & 0x03) << 4) | ((wc1 & 0x003C) >> 2));
      *to_nxt++ = static_cast<uint8_t>(0x80 | ((wc1 & 0x0003) << 4) | ((wc2 & 0x03C0) >> 6));
      *to_nxt++ = static_cast<uint8_t>(0x80 | (wc2 & 0x003F));
    } else if (wc1 < 0xE000) {
      return codecvt_base::error;
    } else {
      if (to_end - to_nxt < 3)
        return codecvt_base::partial;
      *to_nxt++ = static_cast<uint8_t>(0xE0 | (wc1 >> 12));
      *to_nxt++ = static_cast<uint8_t>(0x80 | ((wc1 & 0x0FC0) >> 6));
      *to_nxt++ = static_cast<uint8_t>(0x80 | (wc1 & 0x003F));
    }
  }
  return codecvt_base::ok;
}

static codecvt_base::result utf16_to_utf8(
    const uint32_t* frm,
    const uint32_t* frm_end,
    const uint32_t*& frm_nxt,
    uint8_t* to,
    uint8_t* to_end,
    uint8_t*& to_nxt,
    unsigned long Maxcode = 0x10FFFF,
    codecvt_mode mode     = codecvt_mode(0)) {
  frm_nxt = frm;
  to_nxt  = to;
  if (mode & generate_header) {
    if (to_end - to_nxt < 3)
      return codecvt_base::partial;
    *to_nxt++ = static_cast<uint8_t>(0xEF);
    *to_nxt++ = static_cast<uint8_t>(0xBB);
    *to_nxt++ = static_cast<uint8_t>(0xBF);
  }
  for (; frm_nxt < frm_end; ++frm_nxt) {
    uint16_t wc1 = static_cast<uint16_t>(*frm_nxt);
    if (wc1 > Maxcode)
      return codecvt_base::error;
    if (wc1 < 0x0080) {
      if (to_end - to_nxt < 1)
        return codecvt_base::partial;
      *to_nxt++ = static_cast<uint8_t>(wc1);
    } else if (wc1 < 0x0800) {
      if (to_end - to_nxt < 2)
        return codecvt_base::partial;
      *to_nxt++ = static_cast<uint8_t>(0xC0 | (wc1 >> 6));
      *to_nxt++ = static_cast<uint8_t>(0x80 | (wc1 & 0x03F));
    } else if (wc1 < 0xD800) {
      if (to_end - to_nxt < 3)
        return codecvt_base::partial;
      *to_nxt++ = static_cast<uint8_t>(0xE0 | (wc1 >> 12));
      *to_nxt++ = static_cast<uint8_t>(0x80 | ((wc1 & 0x0FC0) >> 6));
      *to_nxt++ = static_cast<uint8_t>(0x80 | (wc1 & 0x003F));
    } else if (wc1 < 0xDC00) {
      if (frm_end - frm_nxt < 2)
        return codecvt_base::partial;
      uint16_t wc2 = static_cast<uint16_t>(frm_nxt[1]);
      if ((wc2 & 0xFC00) != 0xDC00)
        return codecvt_base::error;
      if (to_end - to_nxt < 4)
        return codecvt_base::partial;
      if (((((wc1 & 0x03C0UL) >> 6) + 1) << 16) + ((wc1 & 0x003FUL) << 10) + (wc2 & 0x03FF) > Maxcode)
        return codecvt_base::error;
      ++frm_nxt;
      uint8_t z = ((wc1 & 0x03C0) >> 6) + 1;
      *to_nxt++ = static_cast<uint8_t>(0xF0 | (z >> 2));
      *to_nxt++ = static_cast<uint8_t>(0x80 | ((z & 0x03) << 4) | ((wc1 & 0x003C) >> 2));
      *to_nxt++ = static_cast<uint8_t>(0x80 | ((wc1 & 0x0003) << 4) | ((wc2 & 0x03C0) >> 6));
      *to_nxt++ = static_cast<uint8_t>(0x80 | (wc2 & 0x003F));
    } else if (wc1 < 0xE000) {
      return codecvt_base::error;
    } else {
      if (to_end - to_nxt < 3)
        return codecvt_base::partial;
      *to_nxt++ = static_cast<uint8_t>(0xE0 | (wc1 >> 12));
      *to_nxt++ = static_cast<uint8_t>(0x80 | ((wc1 & 0x0FC0) >> 6));
      *to_nxt++ = static_cast<uint8_t>(0x80 | (wc1 & 0x003F));
    }
  }
  return codecvt_base::ok;
}

static codecvt_base::result utf8_to_utf16(
    const uint8_t* frm,
    const uint8_t* frm_end,
    const uint8_t*& frm_nxt,
    uint16_t* to,
    uint16_t* to_end,
    uint16_t*& to_nxt,
    unsigned long Maxcode = 0x10FFFF,
    codecvt_mode mode     = codecvt_mode(0)) {
  frm_nxt = frm;
  to_nxt  = to;
  if (mode & consume_header) {
    if (frm_end - frm_nxt >= 3 && frm_nxt[0] == 0xEF && frm_nxt[1] == 0xBB && frm_nxt[2] == 0xBF)
      frm_nxt += 3;
  }
  for (; frm_nxt < frm_end && to_nxt < to_end; ++to_nxt) {
    uint8_t c1 = *frm_nxt;
    if (c1 > Maxcode)
      return codecvt_base::error;
    if (c1 < 0x80) {
      *to_nxt = static_cast<uint16_t>(c1);
      ++frm_nxt;
    } else if (c1 < 0xC2) {
      return codecvt_base::error;
    } else if (c1 < 0xE0) {
      if (frm_end - frm_nxt < 2)
        return codecvt_base::partial;
      uint8_t c2 = frm_nxt[1];
      if ((c2 & 0xC0) != 0x80)
        return codecvt_base::error;
      uint16_t t = static_cast<uint16_t>(((c1 & 0x1F) << 6) | (c2 & 0x3F));
      if (t > Maxcode)
        return codecvt_base::error;
      *to_nxt = t;
      frm_nxt += 2;
    } else if (c1 < 0xF0) {
      if (frm_end - frm_nxt < 2)
        return codecvt_base::partial;
      uint8_t c2 = frm_nxt[1];
      switch (c1) {
      case 0xE0:
        if ((c2 & 0xE0) != 0xA0)
          return codecvt_base::error;
        break;
      case 0xED:
        if ((c2 & 0xE0) != 0x80)
          return codecvt_base::error;
        break;
      default:
        if ((c2 & 0xC0) != 0x80)
          return codecvt_base::error;
        break;
      }
      if (frm_end - frm_nxt < 3)
        return codecvt_base::partial;
      uint8_t c3 = frm_nxt[2];
      if ((c3 & 0xC0) != 0x80)
        return codecvt_base::error;
      uint16_t t = static_cast<uint16_t>(((c1 & 0x0F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F));
      if (t > Maxcode)
        return codecvt_base::error;
      *to_nxt = t;
      frm_nxt += 3;
    } else if (c1 < 0xF5) {
      if (frm_end - frm_nxt < 2)
        return codecvt_base::partial;
      uint8_t c2 = frm_nxt[1];
      switch (c1) {
      case 0xF0:
        if (!(0x90 <= c2 && c2 <= 0xBF))
          return codecvt_base::error;
        break;
      case 0xF4:
        if ((c2 & 0xF0) != 0x80)
          return codecvt_base::error;
        break;
      default:
        if ((c2 & 0xC0) != 0x80)
          return codecvt_base::error;
        break;
      }
      if (frm_end - frm_nxt < 3)
        return codecvt_base::partial;
      uint8_t c3 = frm_nxt[2];
      if ((c3 & 0xC0) != 0x80)
        return codecvt_base::error;
      if (frm_end - frm_nxt < 4)
        return codecvt_base::partial;
      uint8_t c4 = frm_nxt[3];
      if ((c4 & 0xC0) != 0x80)
        return codecvt_base::error;
      if (to_end - to_nxt < 2)
        return codecvt_base::partial;
      if ((((c1 & 7UL) << 18) + ((c2 & 0x3FUL) << 12) + ((c3 & 0x3FUL) << 6) + (c4 & 0x3F)) > Maxcode)
        return codecvt_base::error;
      *to_nxt = static_cast<uint16_t>(
          0xD800 | (((((c1 & 0x07) << 2) | ((c2 & 0x30) >> 4)) - 1) << 6) | ((c2 & 0x0F) << 2) | ((c3 & 0x30) >> 4));
      *++to_nxt = static_cast<uint16_t>(0xDC00 | ((c3 & 0x0F) << 6) | (c4 & 0x3F));
      frm_nxt += 4;
    } else {
      return codecvt_base::error;
    }
  }
  return frm_nxt < frm_end ? codecvt_base::partial : codecvt_base::ok;
}

static codecvt_base::result utf8_to_utf16(
    const uint8_t* frm,
    const uint8_t* frm_end,
    const uint8_t*& frm_nxt,
    uint32_t* to,
    uint32_t* to_end,
    uint32_t*& to_nxt,
    unsigned long Maxcode = 0x10FFFF,
    codecvt_mode mode     = codecvt_mode(0)) {
  frm_nxt = frm;
  to_nxt  = to;
  if (mode & consume_header) {
    if (frm_end - frm_nxt >= 3 && frm_nxt[0] == 0xEF && frm_nxt[1] == 0xBB && frm_nxt[2] == 0xBF)
      frm_nxt += 3;
  }
  for (; frm_nxt < frm_end && to_nxt < to_end; ++to_nxt) {
    uint8_t c1 = *frm_nxt;
    if (c1 > Maxcode)
      return codecvt_base::error;
    if (c1 < 0x80) {
      *to_nxt = static_cast<uint32_t>(c1);
      ++frm_nxt;
    } else if (c1 < 0xC2) {
      return codecvt_base::error;
    } else if (c1 < 0xE0) {
      if (frm_end - frm_nxt < 2)
        return codecvt_base::partial;
      uint8_t c2 = frm_nxt[1];
      if ((c2 & 0xC0) != 0x80)
        return codecvt_base::error;
      uint16_t t = static_cast<uint16_t>(((c1 & 0x1F) << 6) | (c2 & 0x3F));
      if (t > Maxcode)
        return codecvt_base::error;
      *to_nxt = static_cast<uint32_t>(t);
      frm_nxt += 2;
    } else if (c1 < 0xF0) {
      if (frm_end - frm_nxt < 2)
        return codecvt_base::partial;
      uint8_t c2 = frm_nxt[1];
      switch (c1) {
      case 0xE0:
        if ((c2 & 0xE0) != 0xA0)
          return codecvt_base::error;
        break;
      case 0xED:
        if ((c2 & 0xE0) != 0x80)
          return codecvt_base::error;
        break;
      default:
        if ((c2 & 0xC0) != 0x80)
          return codecvt_base::error;
        break;
      }
      if (frm_end - frm_nxt < 3)
        return codecvt_base::partial;
      uint8_t c3 = frm_nxt[2];
      if ((c3 & 0xC0) != 0x80)
        return codecvt_base::error;
      uint16_t t = static_cast<uint16_t>(((c1 & 0x0F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F));
      if (t > Maxcode)
        return codecvt_base::error;
      *to_nxt = static_cast<uint32_t>(t);
      frm_nxt += 3;
    } else if (c1 < 0xF5) {
      if (frm_end - frm_nxt < 2)
        return codecvt_base::partial;
      uint8_t c2 = frm_nxt[1];
      switch (c1) {
      case 0xF0:
        if (!(0x90 <= c2 && c2 <= 0xBF))
          return codecvt_base::error;
        break;
      case 0xF4:
        if ((c2 & 0xF0) != 0x80)
          return codecvt_base::error;
        break;
      default:
        if ((c2 & 0xC0) != 0x80)
          return codecvt_base::error;
        break;
      }
      if (frm_end - frm_nxt < 3)
        return codecvt_base::partial;
      uint8_t c3 = frm_nxt[2];
      if ((c3 & 0xC0) != 0x80)
        return codecvt_base::error;
      if (frm_end - frm_nxt < 4)
        return codecvt_base::partial;
      uint8_t c4 = frm_nxt[3];
      if ((c4 & 0xC0) != 0x80)
        return codecvt_base::error;
      if (to_end - to_nxt < 2)
        return codecvt_base::partial;
      if ((((c1 & 7UL) << 18) + ((c2 & 0x3FUL) << 12) + ((c3 & 0x3FUL) << 6) + (c4 & 0x3F)) > Maxcode)
        return codecvt_base::error;
      *to_nxt = static_cast<uint32_t>(
          0xD800 | (((((c1 & 0x07) << 2) | ((c2 & 0x30) >> 4)) - 1) << 6) | ((c2 & 0x0F) << 2) | ((c3 & 0x30) >> 4));
      *++to_nxt = static_cast<uint32_t>(0xDC00 | ((c3 & 0x0F) << 6) | (c4 & 0x3F));
      frm_nxt += 4;
    } else {
      return codecvt_base::error;
    }
  }
  return frm_nxt < frm_end ? codecvt_base::partial : codecvt_base::ok;
}

static int utf8_to_utf16_length(
    const uint8_t* frm,
    const uint8_t* frm_end,
    size_t mx,
    unsigned long Maxcode = 0x10FFFF,
    codecvt_mode mode     = codecvt_mode(0)) {
  const uint8_t* frm_nxt = frm;
  if (mode & consume_header) {
    if (frm_end - frm_nxt >= 3 && frm_nxt[0] == 0xEF && frm_nxt[1] == 0xBB && frm_nxt[2] == 0xBF)
      frm_nxt += 3;
  }
  for (size_t nchar16_t = 0; frm_nxt < frm_end && nchar16_t < mx; ++nchar16_t) {
    uint8_t c1 = *frm_nxt;
    if (c1 > Maxcode)
      break;
    if (c1 < 0x80) {
      ++frm_nxt;
    } else if (c1 < 0xC2) {
      break;
    } else if (c1 < 0xE0) {
      if ((frm_end - frm_nxt < 2) || (frm_nxt[1] & 0xC0) != 0x80)
        break;
      uint16_t t = static_cast<uint16_t>(((c1 & 0x1F) << 6) | (frm_nxt[1] & 0x3F));
      if (t > Maxcode)
        break;
      frm_nxt += 2;
    } else if (c1 < 0xF0) {
      if (frm_end - frm_nxt < 3)
        break;
      uint8_t c2 = frm_nxt[1];
      uint8_t c3 = frm_nxt[2];
      switch (c1) {
      case 0xE0:
        if ((c2 & 0xE0) != 0xA0)
          return static_cast<int>(frm_nxt - frm);
        break;
      case 0xED:
        if ((c2 & 0xE0) != 0x80)
          return static_cast<int>(frm_nxt - frm);
        break;
      default:
        if ((c2 & 0xC0) != 0x80)
          return static_cast<int>(frm_nxt - frm);
        break;
      }
      if ((c3 & 0xC0) != 0x80)
        break;
      if ((((c1 & 0x0Fu) << 12) | ((c2 & 0x3Fu) << 6) | (c3 & 0x3Fu)) > Maxcode)
        break;
      frm_nxt += 3;
    } else if (c1 < 0xF5) {
      if (frm_end - frm_nxt < 4 || mx - nchar16_t < 2)
        break;
      uint8_t c2 = frm_nxt[1];
      uint8_t c3 = frm_nxt[2];
      uint8_t c4 = frm_nxt[3];
      switch (c1) {
      case 0xF0:
        if (!(0x90 <= c2 && c2 <= 0xBF))
          return static_cast<int>(frm_nxt - frm);
        break;
      case 0xF4:
        if ((c2 & 0xF0) != 0x80)
          return static_cast<int>(frm_nxt - frm);
        break;
      default:
        if ((c2 & 0xC0) != 0x80)
          return static_cast<int>(frm_nxt - frm);
        break;
      }
      if ((c3 & 0xC0) != 0x80 || (c4 & 0xC0) != 0x80)
        break;
      if ((((c1 & 7UL) << 18) + ((c2 & 0x3FUL) << 12) + ((c3 & 0x3FUL) << 6) + (c4 & 0x3F)) > Maxcode)
        break;
      ++nchar16_t;
      frm_nxt += 4;
    } else {
      break;
    }
  }
  return static_cast<int>(frm_nxt - frm);
}

static codecvt_base::result ucs4_to_utf8(
    const uint32_t* frm,
    const uint32_t* frm_end,
    const uint32_t*& frm_nxt,
    uint8_t* to,
    uint8_t* to_end,
    uint8_t*& to_nxt,
    unsigned long Maxcode = 0x10FFFF,
    codecvt_mode mode     = codecvt_mode(0)) {
  frm_nxt = frm;
  to_nxt  = to;
  if (mode & generate_header) {
    if (to_end - to_nxt < 3)
      return codecvt_base::partial;
    *to_nxt++ = static_cast<uint8_t>(0xEF);
    *to_nxt++ = static_cast<uint8_t>(0xBB);
    *to_nxt++ = static_cast<uint8_t>(0xBF);
  }
  for (; frm_nxt < frm_end; ++frm_nxt) {
    uint32_t wc = *frm_nxt;
    if ((wc & 0xFFFFF800) == 0x00D800 || wc > Maxcode)
      return codecvt_base::error;
    if (wc < 0x000080) {
      if (to_end - to_nxt < 1)
        return codecvt_base::partial;
      *to_nxt++ = static_cast<uint8_t>(wc);
    } else if (wc < 0x000800) {
      if (to_end - to_nxt < 2)
        return codecvt_base::partial;
      *to_nxt++ = static_cast<uint8_t>(0xC0 | (wc >> 6));
      *to_nxt++ = static_cast<uint8_t>(0x80 | (wc & 0x03F));
    } else if (wc < 0x010000) {
      if (to_end - to_nxt < 3)
        return codecvt_base::partial;
      *to_nxt++ = static_cast<uint8_t>(0xE0 | (wc >> 12));
      *to_nxt++ = static_cast<uint8_t>(0x80 | ((wc & 0x0FC0) >> 6));
      *to_nxt++ = static_cast<uint8_t>(0x80 | (wc & 0x003F));
    } else // if (wc < 0x110000)
    {
      if (to_end - to_nxt < 4)
        return codecvt_base::partial;
      *to_nxt++ = static_cast<uint8_t>(0xF0 | (wc >> 18));
      *to_nxt++ = static_cast<uint8_t>(0x80 | ((wc & 0x03F000) >> 12));
      *to_nxt++ = static_cast<uint8_t>(0x80 | ((wc & 0x000FC0) >> 6));
      *to_nxt++ = static_cast<uint8_t>(0x80 | (wc & 0x00003F));
    }
  }
  return codecvt_base::ok;
}

static codecvt_base::result utf8_to_ucs4(
    const uint8_t* frm,
    const uint8_t* frm_end,
    const uint8_t*& frm_nxt,
    uint32_t* to,
    uint32_t* to_end,
    uint32_t*& to_nxt,
    unsigned long Maxcode = 0x10FFFF,
    codecvt_mode mode     = codecvt_mode(0)) {
  frm_nxt = frm;
  to_nxt  = to;
  if (mode & consume_header) {
    if (frm_end - frm_nxt >= 3 && frm_nxt[0] == 0xEF && frm_nxt[1] == 0xBB && frm_nxt[2] == 0xBF)
      frm_nxt += 3;
  }
  for (; frm_nxt < frm_end && to_nxt < to_end; ++to_nxt) {
    uint8_t c1 = static_cast<uint8_t>(*frm_nxt);
    if (c1 < 0x80) {
      if (c1 > Maxcode)
        return codecvt_base::error;
      *to_nxt = static_cast<uint32_t>(c1);
      ++frm_nxt;
    } else if (c1 < 0xC2) {
      return codecvt_base::error;
    } else if (c1 < 0xE0) {
      if (frm_end - frm_nxt < 2)
        return codecvt_base::partial;
      uint8_t c2 = frm_nxt[1];
      if ((c2 & 0xC0) != 0x80)
        return codecvt_base::error;
      uint32_t t = static_cast<uint32_t>(((c1 & 0x1F) << 6) | (c2 & 0x3F));
      if (t > Maxcode)
        return codecvt_base::error;
      *to_nxt = t;
      frm_nxt += 2;
    } else if (c1 < 0xF0) {
      if (frm_end - frm_nxt < 2)
        return codecvt_base::partial;
      uint8_t c2 = frm_nxt[1];
      switch (c1) {
      case 0xE0:
        if ((c2 & 0xE0) != 0xA0)
          return codecvt_base::error;
        break;
      case 0xED:
        if ((c2 & 0xE0) != 0x80)
          return codecvt_base::error;
        break;
      default:
        if ((c2 & 0xC0) != 0x80)
          return codecvt_base::error;
        break;
      }
      if (frm_end - frm_nxt < 3)
        return codecvt_base::partial;
      uint8_t c3 = frm_nxt[2];
      if ((c3 & 0xC0) != 0x80)
        return codecvt_base::error;
      uint32_t t = static_cast<uint32_t>(((c1 & 0x0F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F));
      if (t > Maxcode)
        return codecvt_base::error;
      *to_nxt = t;
      frm_nxt += 3;
    } else if (c1 < 0xF5) {
      if (frm_end - frm_nxt < 2)
        return codecvt_base::partial;
      uint8_t c2 = frm_nxt[1];
      switch (c1) {
      case 0xF0:
        if (!(0x90 <= c2 && c2 <= 0xBF))
          return codecvt_base::error;
        break;
      case 0xF4:
        if ((c2 & 0xF0) != 0x80)
          return codecvt_base::error;
        break;
      default:
        if ((c2 & 0xC0) != 0x80)
          return codecvt_base::error;
        break;
      }
      if (frm_end - frm_nxt < 3)
        return codecvt_base::partial;
      uint8_t c3 = frm_nxt[2];
      if ((c3 & 0xC0) != 0x80)
        return codecvt_base::error;
      if (frm_end - frm_nxt < 4)
        return codecvt_base::partial;
      uint8_t c4 = frm_nxt[3];
      if ((c4 & 0xC0) != 0x80)
        return codecvt_base::error;
      uint32_t t = static_cast<uint32_t>(((c1 & 0x07) << 18) | ((c2 & 0x3F) << 12) | ((c3 & 0x3F) << 6) | (c4 & 0x3F));
      if (t > Maxcode)
        return codecvt_base::error;
      *to_nxt = t;
      frm_nxt += 4;
    } else {
      return codecvt_base::error;
    }
  }
  return frm_nxt < frm_end ? codecvt_base::partial : codecvt_base::ok;
}

static int utf8_to_ucs4_length(
    const uint8_t* frm,
    const uint8_t* frm_end,
    size_t mx,
    unsigned long Maxcode = 0x10FFFF,
    codecvt_mode mode     = codecvt_mode(0)) {
  const uint8_t* frm_nxt = frm;
  if (mode & consume_header) {
    if (frm_end - frm_nxt >= 3 && frm_nxt[0] == 0xEF && frm_nxt[1] == 0xBB && frm_nxt[2] == 0xBF)
      frm_nxt += 3;
  }
  for (size_t nchar32_t = 0; frm_nxt < frm_end && nchar32_t < mx; ++nchar32_t) {
    uint8_t c1 = static_cast<uint8_t>(*frm_nxt);
    if (c1 < 0x80) {
      if (c1 > Maxcode)
        break;
      ++frm_nxt;
    } else if (c1 < 0xC2) {
      break;
    } else if (c1 < 0xE0) {
      if ((frm_end - frm_nxt < 2) || ((frm_nxt[1] & 0xC0) != 0x80))
        break;
      if ((((c1 & 0x1Fu) << 6) | (frm_nxt[1] & 0x3Fu)) > Maxcode)
        break;
      frm_nxt += 2;
    } else if (c1 < 0xF0) {
      if (frm_end - frm_nxt < 3)
        break;
      uint8_t c2 = frm_nxt[1];
      uint8_t c3 = frm_nxt[2];
      switch (c1) {
      case 0xE0:
        if ((c2 & 0xE0) != 0xA0)
          return static_cast<int>(frm_nxt - frm);
        break;
      case 0xED:
        if ((c2 & 0xE0) != 0x80)
          return static_cast<int>(frm_nxt - frm);
        break;
      default:
        if ((c2 & 0xC0) != 0x80)
          return static_cast<int>(frm_nxt - frm);
        break;
      }
      if ((c3 & 0xC0) != 0x80)
        break;
      if ((((c1 & 0x0Fu) << 12) | ((c2 & 0x3Fu) << 6) | (c3 & 0x3Fu)) > Maxcode)
        break;
      frm_nxt += 3;
    } else if (c1 < 0xF5) {
      if (frm_end - frm_nxt < 4)
        break;
      uint8_t c2 = frm_nxt[1];
      uint8_t c3 = frm_nxt[2];
      uint8_t c4 = frm_nxt[3];
      switch (c1) {
      case 0xF0:
        if (!(0x90 <= c2 && c2 <= 0xBF))
          return static_cast<int>(frm_nxt - frm);
        break;
      case 0xF4:
        if ((c2 & 0xF0) != 0x80)
          return static_cast<int>(frm_nxt - frm);
        break;
      default:
        if ((c2 & 0xC0) != 0x80)
          return static_cast<int>(frm_nxt - frm);
        break;
      }
      if ((c3 & 0xC0) != 0x80 || (c4 & 0xC0) != 0x80)
        break;
      if ((((c1 & 0x07u) << 18) | ((c2 & 0x3Fu) << 12) | ((c3 & 0x3Fu) << 6) | (c4 & 0x3Fu)) > Maxcode)
        break;
      frm_nxt += 4;
    } else {
      break;
    }
  }
  return static_cast<int>(frm_nxt - frm);
}

static codecvt_base::result ucs2_to_utf8(
    const uint16_t* frm,
    const uint16_t* frm_end,
    const uint16_t*& frm_nxt,
    uint8_t* to,
    uint8_t* to_end,
    uint8_t*& to_nxt,
    unsigned long Maxcode = 0x10FFFF,
    codecvt_mode mode     = codecvt_mode(0)) {
  frm_nxt = frm;
  to_nxt  = to;
  if (mode & generate_header) {
    if (to_end - to_nxt < 3)
      return codecvt_base::partial;
    *to_nxt++ = static_cast<uint8_t>(0xEF);
    *to_nxt++ = static_cast<uint8_t>(0xBB);
    *to_nxt++ = static_cast<uint8_t>(0xBF);
  }
  for (; frm_nxt < frm_end; ++frm_nxt) {
    uint16_t wc = *frm_nxt;
    if ((wc & 0xF800) == 0xD800 || wc > Maxcode)
      return codecvt_base::error;
    if (wc < 0x0080) {
      if (to_end - to_nxt < 1)
        return codecvt_base::partial;
      *to_nxt++ = static_cast<uint8_t>(wc);
    } else if (wc < 0x0800) {
      if (to_end - to_nxt < 2)
        return codecvt_base::partial;
      *to_nxt++ = static_cast<uint8_t>(0xC0 | (wc >> 6));
      *to_nxt++ = static_cast<uint8_t>(0x80 | (wc & 0x03F));
    } else // if (wc <= 0xFFFF)
    {
      if (to_end - to_nxt < 3)
        return codecvt_base::partial;
      *to_nxt++ = static_cast<uint8_t>(0xE0 | (wc >> 12));
      *to_nxt++ = static_cast<uint8_t>(0x80 | ((wc & 0x0FC0) >> 6));
      *to_nxt++ = static_cast<uint8_t>(0x80 | (wc & 0x003F));
    }
  }
  return codecvt_base::ok;
}

static codecvt_base::result utf8_to_ucs2(
    const uint8_t* frm,
    const uint8_t* frm_end,
    const uint8_t*& frm_nxt,
    uint16_t* to,
    uint16_t* to_end,
    uint16_t*& to_nxt,
    unsigned long Maxcode = 0x10FFFF,
    codecvt_mode mode     = codecvt_mode(0)) {
  frm_nxt = frm;
  to_nxt  = to;
  if (mode & consume_header) {
    if (frm_end - frm_nxt >= 3 && frm_nxt[0] == 0xEF && frm_nxt[1] == 0xBB && frm_nxt[2] == 0xBF)
      frm_nxt += 3;
  }
  for (; frm_nxt < frm_end && to_nxt < to_end; ++to_nxt) {
    uint8_t c1 = static_cast<uint8_t>(*frm_nxt);
    if (c1 < 0x80) {
      if (c1 > Maxcode)
        return codecvt_base::error;
      *to_nxt = static_cast<uint16_t>(c1);
      ++frm_nxt;
    } else if (c1 < 0xC2) {
      return codecvt_base::error;
    } else if (c1 < 0xE0) {
      if (frm_end - frm_nxt < 2)
        return codecvt_base::partial;
      uint8_t c2 = frm_nxt[1];
      if ((c2 & 0xC0) != 0x80)
        return codecvt_base::error;
      uint16_t t = static_cast<uint16_t>(((c1 & 0x1F) << 6) | (c2 & 0x3F));
      if (t > Maxcode)
        return codecvt_base::error;
      *to_nxt = t;
      frm_nxt += 2;
    } else if (c1 < 0xF0) {
      if (frm_end - frm_nxt < 2)
        return codecvt_base::partial;
      uint8_t c2 = frm_nxt[1];
      switch (c1) {
      case 0xE0:
        if ((c2 & 0xE0) != 0xA0)
          return codecvt_base::error;
        break;
      case 0xED:
        if ((c2 & 0xE0) != 0x80)
          return codecvt_base::error;
        break;
      default:
        if ((c2 & 0xC0) != 0x80)
          return codecvt_base::error;
        break;
      }
      if (frm_end - frm_nxt < 3)
        return codecvt_base::partial;
      uint8_t c3 = frm_nxt[2];
      if ((c3 & 0xC0) != 0x80)
        return codecvt_base::error;
      uint16_t t = static_cast<uint16_t>(((c1 & 0x0F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F));
      if (t > Maxcode)
        return codecvt_base::error;
      *to_nxt = t;
      frm_nxt += 3;
    } else {
      return codecvt_base::error;
    }
  }
  return frm_nxt < frm_end ? codecvt_base::partial : codecvt_base::ok;
}

static int utf8_to_ucs2_length(
    const uint8_t* frm,
    const uint8_t* frm_end,
    size_t mx,
    unsigned long Maxcode = 0x10FFFF,
    codecvt_mode mode     = codecvt_mode(0)) {
  const uint8_t* frm_nxt = frm;
  if (mode & consume_header) {
    if (frm_end - frm_nxt >= 3 && frm_nxt[0] == 0xEF && frm_nxt[1] == 0xBB && frm_nxt[2] == 0xBF)
      frm_nxt += 3;
  }
  for (size_t nchar32_t = 0; frm_nxt < frm_end && nchar32_t < mx; ++nchar32_t) {
    uint8_t c1 = static_cast<uint8_t>(*frm_nxt);
    if (c1 < 0x80) {
      if (c1 > Maxcode)
        break;
      ++frm_nxt;
    } else if (c1 < 0xC2) {
      break;
    } else if (c1 < 0xE0) {
      if ((frm_end - frm_nxt < 2) || ((frm_nxt[1] & 0xC0) != 0x80))
        break;
      if ((((c1 & 0x1Fu) << 6) | (frm_nxt[1] & 0x3Fu)) > Maxcode)
        break;
      frm_nxt += 2;
    } else if (c1 < 0xF0) {
      if (frm_end - frm_nxt < 3)
        break;
      uint8_t c2 = frm_nxt[1];
      uint8_t c3 = frm_nxt[2];
      switch (c1) {
      case 0xE0:
        if ((c2 & 0xE0) != 0xA0)
          return static_cast<int>(frm_nxt - frm);
        break;
      case 0xED:
        if ((c2 & 0xE0) != 0x80)
          return static_cast<int>(frm_nxt - frm);
        break;
      default:
        if ((c2 & 0xC0) != 0x80)
          return static_cast<int>(frm_nxt - frm);
        break;
      }
      if ((c3 & 0xC0) != 0x80)
        break;
      if ((((c1 & 0x0Fu) << 12) | ((c2 & 0x3Fu) << 6) | (c3 & 0x3Fu)) > Maxcode)
        break;
      frm_nxt += 3;
    } else {
      break;
    }
  }
  return static_cast<int>(frm_nxt - frm);
}

static codecvt_base::result ucs4_to_utf16be(
    const uint32_t* frm,
    const uint32_t* frm_end,
    const uint32_t*& frm_nxt,
    uint8_t* to,
    uint8_t* to_end,
    uint8_t*& to_nxt,
    unsigned long Maxcode = 0x10FFFF,
    codecvt_mode mode     = codecvt_mode(0)) {
  frm_nxt = frm;
  to_nxt  = to;
  if (mode & generate_header) {
    if (to_end - to_nxt < 2)
      return codecvt_base::partial;
    *to_nxt++ = static_cast<uint8_t>(0xFE);
    *to_nxt++ = static_cast<uint8_t>(0xFF);
  }
  for (; frm_nxt < frm_end; ++frm_nxt) {
    uint32_t wc = *frm_nxt;
    if ((wc & 0xFFFFF800) == 0x00D800 || wc > Maxcode)
      return codecvt_base::error;
    if (wc < 0x010000) {
      if (to_end - to_nxt < 2)
        return codecvt_base::partial;
      *to_nxt++ = static_cast<uint8_t>(wc >> 8);
      *to_nxt++ = static_cast<uint8_t>(wc);
    } else {
      if (to_end - to_nxt < 4)
        return codecvt_base::partial;
      uint16_t t = static_cast<uint16_t>(0xD800 | ((((wc & 0x1F0000) >> 16) - 1) << 6) | ((wc & 0x00FC00) >> 10));
      *to_nxt++  = static_cast<uint8_t>(t >> 8);
      *to_nxt++  = static_cast<uint8_t>(t);
      t          = static_cast<uint16_t>(0xDC00 | (wc & 0x03FF));
      *to_nxt++  = static_cast<uint8_t>(t >> 8);
      *to_nxt++  = static_cast<uint8_t>(t);
    }
  }
  return codecvt_base::ok;
}

static codecvt_base::result utf16be_to_ucs4(
    const uint8_t* frm,
    const uint8_t* frm_end,
    const uint8_t*& frm_nxt,
    uint32_t* to,
    uint32_t* to_end,
    uint32_t*& to_nxt,
    unsigned long Maxcode = 0x10FFFF,
    codecvt_mode mode     = codecvt_mode(0)) {
  frm_nxt = frm;
  to_nxt  = to;
  if (mode & consume_header) {
    if (frm_end - frm_nxt >= 2 && frm_nxt[0] == 0xFE && frm_nxt[1] == 0xFF)
      frm_nxt += 2;
  }
  for (; frm_nxt < frm_end - 1 && to_nxt < to_end; ++to_nxt) {
    uint16_t c1 = static_cast<uint16_t>(frm_nxt[0] << 8 | frm_nxt[1]);
    if ((c1 & 0xFC00) == 0xDC00)
      return codecvt_base::error;
    if ((c1 & 0xFC00) != 0xD800) {
      if (c1 > Maxcode)
        return codecvt_base::error;
      *to_nxt = static_cast<uint32_t>(c1);
      frm_nxt += 2;
    } else {
      if (frm_end - frm_nxt < 4)
        return codecvt_base::partial;
      uint16_t c2 = static_cast<uint16_t>(frm_nxt[2] << 8 | frm_nxt[3]);
      if ((c2 & 0xFC00) != 0xDC00)
        return codecvt_base::error;
      uint32_t t = static_cast<uint32_t>(((((c1 & 0x03C0) >> 6) + 1) << 16) | ((c1 & 0x003F) << 10) | (c2 & 0x03FF));
      if (t > Maxcode)
        return codecvt_base::error;
      *to_nxt = t;
      frm_nxt += 4;
    }
  }
  return frm_nxt < frm_end ? codecvt_base::partial : codecvt_base::ok;
}

static int utf16be_to_ucs4_length(
    const uint8_t* frm,
    const uint8_t* frm_end,
    size_t mx,
    unsigned long Maxcode = 0x10FFFF,
    codecvt_mode mode     = codecvt_mode(0)) {
  const uint8_t* frm_nxt = frm;
  if (mode & consume_header) {
    if (frm_end - frm_nxt >= 2 && frm_nxt[0] == 0xFE && frm_nxt[1] == 0xFF)
      frm_nxt += 2;
  }
  for (size_t nchar32_t = 0; frm_nxt < frm_end - 1 && nchar32_t < mx; ++nchar32_t) {
    uint16_t c1 = static_cast<uint16_t>(frm_nxt[0] << 8 | frm_nxt[1]);
    if ((c1 & 0xFC00) == 0xDC00)
      break;
    if ((c1 & 0xFC00) != 0xD800) {
      if (c1 > Maxcode)
        break;
      frm_nxt += 2;
    } else {
      if (frm_end - frm_nxt < 4)
        break;
      uint16_t c2 = static_cast<uint16_t>(frm_nxt[2] << 8 | frm_nxt[3]);
      if ((c2 & 0xFC00) != 0xDC00)
        break;
      uint32_t t = static_cast<uint32_t>(((((c1 & 0x03C0) >> 6) + 1) << 16) | ((c1 & 0x003F) << 10) | (c2 & 0x03FF));
      if (t > Maxcode)
        break;
      frm_nxt += 4;
    }
  }
  return static_cast<int>(frm_nxt - frm);
}

static codecvt_base::result ucs4_to_utf16le(
    const uint32_t* frm,
    const uint32_t* frm_end,
    const uint32_t*& frm_nxt,
    uint8_t* to,
    uint8_t* to_end,
    uint8_t*& to_nxt,
    unsigned long Maxcode = 0x10FFFF,
    codecvt_mode mode     = codecvt_mode(0)) {
  frm_nxt = frm;
  to_nxt  = to;
  if (mode & generate_header) {
    if (to_end - to_nxt < 2)
      return codecvt_base::partial;
    *to_nxt++ = static_cast<uint8_t>(0xFF);
    *to_nxt++ = static_cast<uint8_t>(0xFE);
  }
  for (; frm_nxt < frm_end; ++frm_nxt) {
    uint32_t wc = *frm_nxt;
    if ((wc & 0xFFFFF800) == 0x00D800 || wc > Maxcode)
      return codecvt_base::error;
    if (wc < 0x010000) {
      if (to_end - to_nxt < 2)
        return codecvt_base::partial;
      *to_nxt++ = static_cast<uint8_t>(wc);
      *to_nxt++ = static_cast<uint8_t>(wc >> 8);
    } else {
      if (to_end - to_nxt < 4)
        return codecvt_base::partial;
      uint16_t t = static_cast<uint16_t>(0xD800 | ((((wc & 0x1F0000) >> 16) - 1) << 6) | ((wc & 0x00FC00) >> 10));
      *to_nxt++  = static_cast<uint8_t>(t);
      *to_nxt++  = static_cast<uint8_t>(t >> 8);
      t          = static_cast<uint16_t>(0xDC00 | (wc & 0x03FF));
      *to_nxt++  = static_cast<uint8_t>(t);
      *to_nxt++  = static_cast<uint8_t>(t >> 8);
    }
  }
  return codecvt_base::ok;
}

static codecvt_base::result utf16le_to_ucs4(
    const uint8_t* frm,
    const uint8_t* frm_end,
    const uint8_t*& frm_nxt,
    uint32_t* to,
    uint32_t* to_end,
    uint32_t*& to_nxt,
    unsigned long Maxcode = 0x10FFFF,
    codecvt_mode mode     = codecvt_mode(0)) {
  frm_nxt = frm;
  to_nxt  = to;
  if (mode & consume_header) {
    if (frm_end - frm_nxt >= 2 && frm_nxt[0] == 0xFF && frm_nxt[1] == 0xFE)
      frm_nxt += 2;
  }
  for (; frm_nxt < frm_end - 1 && to_nxt < to_end; ++to_nxt) {
    uint16_t c1 = static_cast<uint16_t>(frm_nxt[1] << 8 | frm_nxt[0]);
    if ((c1 & 0xFC00) == 0xDC00)
      return codecvt_base::error;
    if ((c1 & 0xFC00) != 0xD800) {
      if (c1 > Maxcode)
        return codecvt_base::error;
      *to_nxt = static_cast<uint32_t>(c1);
      frm_nxt += 2;
    } else {
      if (frm_end - frm_nxt < 4)
        return codecvt_base::partial;
      uint16_t c2 = static_cast<uint16_t>(frm_nxt[3] << 8 | frm_nxt[2]);
      if ((c2 & 0xFC00) != 0xDC00)
        return codecvt_base::error;
      uint32_t t = static_cast<uint32_t>(((((c1 & 0x03C0) >> 6) + 1) << 16) | ((c1 & 0x003F) << 10) | (c2 & 0x03FF));
      if (t > Maxcode)
        return codecvt_base::error;
      *to_nxt = t;
      frm_nxt += 4;
    }
  }
  return frm_nxt < frm_end ? codecvt_base::partial : codecvt_base::ok;
}

static int utf16le_to_ucs4_length(
    const uint8_t* frm,
    const uint8_t* frm_end,
    size_t mx,
    unsigned long Maxcode = 0x10FFFF,
    codecvt_mode mode     = codecvt_mode(0)) {
  const uint8_t* frm_nxt = frm;
  if (mode & consume_header) {
    if (frm_end - frm_nxt >= 2 && frm_nxt[0] == 0xFF && frm_nxt[1] == 0xFE)
      frm_nxt += 2;
  }
  for (size_t nchar32_t = 0; frm_nxt < frm_end - 1 && nchar32_t < mx; ++nchar32_t) {
    uint16_t c1 = static_cast<uint16_t>(frm_nxt[1] << 8 | frm_nxt[0]);
    if ((c1 & 0xFC00) == 0xDC00)
      break;
    if ((c1 & 0xFC00) != 0xD800) {
      if (c1 > Maxcode)
        break;
      frm_nxt += 2;
    } else {
      if (frm_end - frm_nxt < 4)
        break;
      uint16_t c2 = static_cast<uint16_t>(frm_nxt[3] << 8 | frm_nxt[2]);
      if ((c2 & 0xFC00) != 0xDC00)
        break;
      uint32_t t = static_cast<uint32_t>(((((c1 & 0x03C0) >> 6) + 1) << 16) | ((c1 & 0x003F) << 10) | (c2 & 0x03FF));
      if (t > Maxcode)
        break;
      frm_nxt += 4;
    }
  }
  return static_cast<int>(frm_nxt - frm);
}

static codecvt_base::result ucs2_to_utf16be(
    const uint16_t* frm,
    const uint16_t* frm_end,
    const uint16_t*& frm_nxt,
    uint8_t* to,
    uint8_t* to_end,
    uint8_t*& to_nxt,
    unsigned long Maxcode = 0x10FFFF,
    codecvt_mode mode     = codecvt_mode(0)) {
  frm_nxt = frm;
  to_nxt  = to;
  if (mode & generate_header) {
    if (to_end - to_nxt < 2)
      return codecvt_base::partial;
    *to_nxt++ = static_cast<uint8_t>(0xFE);
    *to_nxt++ = static_cast<uint8_t>(0xFF);
  }
  for (; frm_nxt < frm_end; ++frm_nxt) {
    uint16_t wc = *frm_nxt;
    if ((wc & 0xF800) == 0xD800 || wc > Maxcode)
      return codecvt_base::error;
    if (to_end - to_nxt < 2)
      return codecvt_base::partial;
    *to_nxt++ = static_cast<uint8_t>(wc >> 8);
    *to_nxt++ = static_cast<uint8_t>(wc);
  }
  return codecvt_base::ok;
}

static codecvt_base::result utf16be_to_ucs2(
    const uint8_t* frm,
    const uint8_t* frm_end,
    const uint8_t*& frm_nxt,
    uint16_t* to,
    uint16_t* to_end,
    uint16_t*& to_nxt,
    unsigned long Maxcode = 0x10FFFF,
    codecvt_mode mode     = codecvt_mode(0)) {
  frm_nxt = frm;
  to_nxt  = to;
  if (mode & consume_header) {
    if (frm_end - frm_nxt >= 2 && frm_nxt[0] == 0xFE && frm_nxt[1] == 0xFF)
      frm_nxt += 2;
  }
  for (; frm_nxt < frm_end - 1 && to_nxt < to_end; ++to_nxt) {
    uint16_t c1 = static_cast<uint16_t>(frm_nxt[0] << 8 | frm_nxt[1]);
    if ((c1 & 0xF800) == 0xD800 || c1 > Maxcode)
      return codecvt_base::error;
    *to_nxt = c1;
    frm_nxt += 2;
  }
  return frm_nxt < frm_end ? codecvt_base::partial : codecvt_base::ok;
}

static int utf16be_to_ucs2_length(
    const uint8_t* frm,
    const uint8_t* frm_end,
    size_t mx,
    unsigned long Maxcode = 0x10FFFF,
    codecvt_mode mode     = codecvt_mode(0)) {
  const uint8_t* frm_nxt = frm;
  if (mode & consume_header) {
    if (frm_end - frm_nxt >= 2 && frm_nxt[0] == 0xFE && frm_nxt[1] == 0xFF)
      frm_nxt += 2;
  }
  for (size_t nchar16_t = 0; frm_nxt < frm_end - 1 && nchar16_t < mx; ++nchar16_t) {
    uint16_t c1 = static_cast<uint16_t>(frm_nxt[0] << 8 | frm_nxt[1]);
    if ((c1 & 0xF800) == 0xD800 || c1 > Maxcode)
      break;
    frm_nxt += 2;
  }
  return static_cast<int>(frm_nxt - frm);
}

static codecvt_base::result ucs2_to_utf16le(
    const uint16_t* frm,
    const uint16_t* frm_end,
    const uint16_t*& frm_nxt,
    uint8_t* to,
    uint8_t* to_end,
    uint8_t*& to_nxt,
    unsigned long Maxcode = 0x10FFFF,
    codecvt_mode mode     = codecvt_mode(0)) {
  frm_nxt = frm;
  to_nxt  = to;
  if (mode & generate_header) {
    if (to_end - to_nxt < 2)
      return codecvt_base::partial;
    *to_nxt++ = static_cast<uint8_t>(0xFF);
    *to_nxt++ = static_cast<uint8_t>(0xFE);
  }
  for (; frm_nxt < frm_end; ++frm_nxt) {
    uint16_t wc = *frm_nxt;
    if ((wc & 0xF800) == 0xD800 || wc > Maxcode)
      return codecvt_base::error;
    if (to_end - to_nxt < 2)
      return codecvt_base::partial;
    *to_nxt++ = static_cast<uint8_t>(wc);
    *to_nxt++ = static_cast<uint8_t>(wc >> 8);
  }
  return codecvt_base::ok;
}

static codecvt_base::result utf16le_to_ucs2(
    const uint8_t* frm,
    const uint8_t* frm_end,
    const uint8_t*& frm_nxt,
    uint16_t* to,
    uint16_t* to_end,
    uint16_t*& to_nxt,
    unsigned long Maxcode = 0x10FFFF,
    codecvt_mode mode     = codecvt_mode(0)) {
  frm_nxt = frm;
  to_nxt  = to;
  if (mode & consume_header) {
    if (frm_end - frm_nxt >= 2 && frm_nxt[0] == 0xFF && frm_nxt[1] == 0xFE)
      frm_nxt += 2;
  }
  for (; frm_nxt < frm_end - 1 && to_nxt < to_end; ++to_nxt) {
    uint16_t c1 = static_cast<uint16_t>(frm_nxt[1] << 8 | frm_nxt[0]);
    if ((c1 & 0xF800) == 0xD800 || c1 > Maxcode)
      return codecvt_base::error;
    *to_nxt = c1;
    frm_nxt += 2;
  }
  return frm_nxt < frm_end ? codecvt_base::partial : codecvt_base::ok;
}

static int utf16le_to_ucs2_length(
    const uint8_t* frm,
    const uint8_t* frm_end,
    size_t mx,
    unsigned long Maxcode = 0x10FFFF,
    codecvt_mode mode     = codecvt_mode(0)) {
  const uint8_t* frm_nxt = frm;
  frm_nxt                = frm;
  if (mode & consume_header) {
    if (frm_end - frm_nxt >= 2 && frm_nxt[0] == 0xFF && frm_nxt[1] == 0xFE)
      frm_nxt += 2;
  }
  for (size_t nchar16_t = 0; frm_nxt < frm_end - 1 && nchar16_t < mx; ++nchar16_t) {
    uint16_t c1 = static_cast<uint16_t>(frm_nxt[1] << 8 | frm_nxt[0]);
    if ((c1 & 0xF800) == 0xD800 || c1 > Maxcode)
      break;
    frm_nxt += 2;
  }
  return static_cast<int>(frm_nxt - frm);
}

_LIBCPP_SUPPRESS_DEPRECATED_POP

// template <> class codecvt<char16_t, char, mbstate_t>

constinit locale::id codecvt<char16_t, char, mbstate_t>::id;

codecvt<char16_t, char, mbstate_t>::~codecvt() {}

codecvt<char16_t, char, mbstate_t>::result codecvt<char16_t, char, mbstate_t>::do_out(
    state_type&,
    const intern_type* frm,
    const intern_type* frm_end,
    const intern_type*& frm_nxt,
    extern_type* to,
    extern_type* to_end,
    extern_type*& to_nxt) const {
  const uint16_t* _frm     = reinterpret_cast<const uint16_t*>(frm);
  const uint16_t* _frm_end = reinterpret_cast<const uint16_t*>(frm_end);
  const uint16_t* _frm_nxt = _frm;
  uint8_t* _to             = reinterpret_cast<uint8_t*>(to);
  uint8_t* _to_end         = reinterpret_cast<uint8_t*>(to_end);
  uint8_t* _to_nxt         = _to;
  result r                 = utf16_to_utf8(_frm, _frm_end, _frm_nxt, _to, _to_end, _to_nxt);
  frm_nxt                  = frm + (_frm_nxt - _frm);
  to_nxt                   = to + (_to_nxt - _to);
  return r;
}

codecvt<char16_t, char, mbstate_t>::result codecvt<char16_t, char, mbstate_t>::do_in(
    state_type&,
    const extern_type* frm,
    const extern_type* frm_end,
    const extern_type*& frm_nxt,
    intern_type* to,
    intern_type* to_end,
    intern_type*& to_nxt) const {
  const uint8_t* _frm     = reinterpret_cast<const uint8_t*>(frm);
  const uint8_t* _frm_end = reinterpret_cast<const uint8_t*>(frm_end);
  const uint8_t* _frm_nxt = _frm;
  uint16_t* _to           = reinterpret_cast<uint16_t*>(to);
  uint16_t* _to_end       = reinterpret_cast<uint16_t*>(to_end);
  uint16_t* _to_nxt       = _to;
  result r                = utf8_to_utf16(_frm, _frm_end, _frm_nxt, _to, _to_end, _to_nxt);
  frm_nxt                 = frm + (_frm_nxt - _frm);
  to_nxt                  = to + (_to_nxt - _to);
  return r;
}

codecvt<char16_t, char, mbstate_t>::result
codecvt<char16_t, char, mbstate_t>::do_unshift(state_type&, extern_type* to, extern_type*, extern_type*& to_nxt) const {
  to_nxt = to;
  return noconv;
}

int codecvt<char16_t, char, mbstate_t>::do_encoding() const noexcept { return 0; }

bool codecvt<char16_t, char, mbstate_t>::do_always_noconv() const noexcept { return false; }

int codecvt<char16_t, char, mbstate_t>::do_length(
    state_type&, const extern_type* frm, const extern_type* frm_end, size_t mx) const {
  const uint8_t* _frm     = reinterpret_cast<const uint8_t*>(frm);
  const uint8_t* _frm_end = reinterpret_cast<const uint8_t*>(frm_end);
  return utf8_to_utf16_length(_frm, _frm_end, mx);
}

int codecvt<char16_t, char, mbstate_t>::do_max_length() const noexcept { return 4; }

#ifndef _LIBCPP_HAS_NO_CHAR8_T

// template <> class codecvt<char16_t, char8_t, mbstate_t>

constinit locale::id codecvt<char16_t, char8_t, mbstate_t>::id;

codecvt<char16_t, char8_t, mbstate_t>::~codecvt() {}

codecvt<char16_t, char8_t, mbstate_t>::result codecvt<char16_t, char8_t, mbstate_t>::do_out(
    state_type&,
    const intern_type* frm,
    const intern_type* frm_end,
    const intern_type*& frm_nxt,
    extern_type* to,
    extern_type* to_end,
    extern_type*& to_nxt) const {
  const uint16_t* _frm     = reinterpret_cast<const uint16_t*>(frm);
  const uint16_t* _frm_end = reinterpret_cast<const uint16_t*>(frm_end);
  const uint16_t* _frm_nxt = _frm;
  uint8_t* _to             = reinterpret_cast<uint8_t*>(to);
  uint8_t* _to_end         = reinterpret_cast<uint8_t*>(to_end);
  uint8_t* _to_nxt         = _to;
  result r                 = utf16_to_utf8(_frm, _frm_end, _frm_nxt, _to, _to_end, _to_nxt);
  frm_nxt                  = frm + (_frm_nxt - _frm);
  to_nxt                   = to + (_to_nxt - _to);
  return r;
}

codecvt<char16_t, char8_t, mbstate_t>::result codecvt<char16_t, char8_t, mbstate_t>::do_in(
    state_type&,
    const extern_type* frm,
    const extern_type* frm_end,
    const extern_type*& frm_nxt,
    intern_type* to,
    intern_type* to_end,
    intern_type*& to_nxt) const {
  const uint8_t* _frm     = reinterpret_cast<const uint8_t*>(frm);
  const uint8_t* _frm_end = reinterpret_cast<const uint8_t*>(frm_end);
  const uint8_t* _frm_nxt = _frm;
  uint16_t* _to           = reinterpret_cast<uint16_t*>(to);
  uint16_t* _to_end       = reinterpret_cast<uint16_t*>(to_end);
  uint16_t* _to_nxt       = _to;
  result r                = utf8_to_utf16(_frm, _frm_end, _frm_nxt, _to, _to_end, _to_nxt);
  frm_nxt                 = frm + (_frm_nxt - _frm);
  to_nxt                  = to + (_to_nxt - _to);
  return r;
}

codecvt<char16_t, char8_t, mbstate_t>::result codecvt<char16_t, char8_t, mbstate_t>::do_unshift(
    state_type&, extern_type* to, extern_type*, extern_type*& to_nxt) const {
  to_nxt = to;
  return noconv;
}

int codecvt<char16_t, char8_t, mbstate_t>::do_encoding() const noexcept { return 0; }

bool codecvt<char16_t, char8_t, mbstate_t>::do_always_noconv() const noexcept { return false; }

int codecvt<char16_t, char8_t, mbstate_t>::do_length(
    state_type&, const extern_type* frm, const extern_type* frm_end, size_t mx) const {
  const uint8_t* _frm     = reinterpret_cast<const uint8_t*>(frm);
  const uint8_t* _frm_end = reinterpret_cast<const uint8_t*>(frm_end);
  return utf8_to_utf16_length(_frm, _frm_end, mx);
}

int codecvt<char16_t, char8_t, mbstate_t>::do_max_length() const noexcept { return 4; }

#endif

// template <> class codecvt<char32_t, char, mbstate_t>

constinit locale::id codecvt<char32_t, char, mbstate_t>::id;

codecvt<char32_t, char, mbstate_t>::~codecvt() {}

codecvt<char32_t, char, mbstate_t>::result codecvt<char32_t, char, mbstate_t>::do_out(
    state_type&,
    const intern_type* frm,
    const intern_type* frm_end,
    const intern_type*& frm_nxt,
    extern_type* to,
    extern_type* to_end,
    extern_type*& to_nxt) const {
  const uint32_t* _frm     = reinterpret_cast<const uint32_t*>(frm);
  const uint32_t* _frm_end = reinterpret_cast<const uint32_t*>(frm_end);
  const uint32_t* _frm_nxt = _frm;
  uint8_t* _to             = reinterpret_cast<uint8_t*>(to);
  uint8_t* _to_end         = reinterpret_cast<uint8_t*>(to_end);
  uint8_t* _to_nxt         = _to;
  result r                 = ucs4_to_utf8(_frm, _frm_end, _frm_nxt, _to, _to_end, _to_nxt);
  frm_nxt                  = frm + (_frm_nxt - _frm);
  to_nxt                   = to + (_to_nxt - _to);
  return r;
}

codecvt<char32_t, char, mbstate_t>::result codecvt<char32_t, char, mbstate_t>::do_in(
    state_type&,
    const extern_type* frm,
    const extern_type* frm_end,
    const extern_type*& frm_nxt,
    intern_type* to,
    intern_type* to_end,
    intern_type*& to_nxt) const {
  const uint8_t* _frm     = reinterpret_cast<const uint8_t*>(frm);
  const uint8_t* _frm_end = reinterpret_cast<const uint8_t*>(frm_end);
  const uint8_t* _frm_nxt = _frm;
  uint32_t* _to           = reinterpret_cast<uint32_t*>(to);
  uint32_t* _to_end       = reinterpret_cast<uint32_t*>(to_end);
  uint32_t* _to_nxt       = _to;
  result r                = utf8_to_ucs4(_frm, _frm_end, _frm_nxt, _to, _to_end, _to_nxt);
  frm_nxt                 = frm + (_frm_nxt - _frm);
  to_nxt                  = to + (_to_nxt - _to);
  return r;
}

codecvt<char32_t, char, mbstate_t>::result
codecvt<char32_t, char, mbstate_t>::do_unshift(state_type&, extern_type* to, extern_type*, extern_type*& to_nxt) const {
  to_nxt = to;
  return noconv;
}

int codecvt<char32_t, char, mbstate_t>::do_encoding() const noexcept { return 0; }

bool codecvt<char32_t, char, mbstate_t>::do_always_noconv() const noexcept { return false; }

int codecvt<char32_t, char, mbstate_t>::do_length(
    state_type&, const extern_type* frm, const extern_type* frm_end, size_t mx) const {
  const uint8_t* _frm     = reinterpret_cast<const uint8_t*>(frm);
  const uint8_t* _frm_end = reinterpret_cast<const uint8_t*>(frm_end);
  return utf8_to_ucs4_length(_frm, _frm_end, mx);
}

int codecvt<char32_t, char, mbstate_t>::do_max_length() const noexcept { return 4; }

#ifndef _LIBCPP_HAS_NO_CHAR8_T

// template <> class codecvt<char32_t, char8_t, mbstate_t>

constinit locale::id codecvt<char32_t, char8_t, mbstate_t>::id;

codecvt<char32_t, char8_t, mbstate_t>::~codecvt() {}

codecvt<char32_t, char8_t, mbstate_t>::result codecvt<char32_t, char8_t, mbstate_t>::do_out(
    state_type&,
    const intern_type* frm,
    const intern_type* frm_end,
    const intern_type*& frm_nxt,
    extern_type* to,
    extern_type* to_end,
    extern_type*& to_nxt) const {
  const uint32_t* _frm     = reinterpret_cast<const uint32_t*>(frm);
  const uint32_t* _frm_end = reinterpret_cast<const uint32_t*>(frm_end);
  const uint32_t* _frm_nxt = _frm;
  uint8_t* _to             = reinterpret_cast<uint8_t*>(to);
  uint8_t* _to_end         = reinterpret_cast<uint8_t*>(to_end);
  uint8_t* _to_nxt         = _to;
  result r                 = ucs4_to_utf8(_frm, _frm_end, _frm_nxt, _to, _to_end, _to_nxt);
  frm_nxt                  = frm + (_frm_nxt - _frm);
  to_nxt                   = to + (_to_nxt - _to);
  return r;
}

codecvt<char32_t, char8_t, mbstate_t>::result codecvt<char32_t, char8_t, mbstate_t>::do_in(
    state_type&,
    const extern_type* frm,
    const extern_type* frm_end,
    const extern_type*& frm_nxt,
    intern_type* to,
    intern_type* to_end,
    intern_type*& to_nxt) const {
  const uint8_t* _frm     = reinterpret_cast<const uint8_t*>(frm);
  const uint8_t* _frm_end = reinterpret_cast<const uint8_t*>(frm_end);
  const uint8_t* _frm_nxt = _frm;
  uint32_t* _to           = reinterpret_cast<uint32_t*>(to);
  uint32_t* _to_end       = reinterpret_cast<uint32_t*>(to_end);
  uint32_t* _to_nxt       = _to;
  result r                = utf8_to_ucs4(_frm, _frm_end, _frm_nxt, _to, _to_end, _to_nxt);
  frm_nxt                 = frm + (_frm_nxt - _frm);
  to_nxt                  = to + (_to_nxt - _to);
  return r;
}

codecvt<char32_t, char8_t, mbstate_t>::result codecvt<char32_t, char8_t, mbstate_t>::do_unshift(
    state_type&, extern_type* to, extern_type*, extern_type*& to_nxt) const {
  to_nxt = to;
  return noconv;
}

int codecvt<char32_t, char8_t, mbstate_t>::do_encoding() const noexcept { return 0; }

bool codecvt<char32_t, char8_t, mbstate_t>::do_always_noconv() const noexcept { return false; }

int codecvt<char32_t, char8_t, mbstate_t>::do_length(
    state_type&, const extern_type* frm, const extern_type* frm_end, size_t mx) const {
  const uint8_t* _frm     = reinterpret_cast<const uint8_t*>(frm);
  const uint8_t* _frm_end = reinterpret_cast<const uint8_t*>(frm_end);
  return utf8_to_ucs4_length(_frm, _frm_end, mx);
}

int codecvt<char32_t, char8_t, mbstate_t>::do_max_length() const noexcept { return 4; }

#endif

// __codecvt_utf8<wchar_t>

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
__codecvt_utf8<wchar_t>::result __codecvt_utf8<wchar_t>::do_out(
    state_type&,
    const intern_type* frm,
    const intern_type* frm_end,
    const intern_type*& frm_nxt,
    extern_type* to,
    extern_type* to_end,
    extern_type*& to_nxt) const {
#  if defined(_LIBCPP_SHORT_WCHAR)
  const uint16_t* _frm     = reinterpret_cast<const uint16_t*>(frm);
  const uint16_t* _frm_end = reinterpret_cast<const uint16_t*>(frm_end);
  const uint16_t* _frm_nxt = _frm;
#  else
  const uint32_t* _frm     = reinterpret_cast<const uint32_t*>(frm);
  const uint32_t* _frm_end = reinterpret_cast<const uint32_t*>(frm_end);
  const uint32_t* _frm_nxt = _frm;
#  endif
  uint8_t* _to     = reinterpret_cast<uint8_t*>(to);
  uint8_t* _to_end = reinterpret_cast<uint8_t*>(to_end);
  uint8_t* _to_nxt = _to;
#  if defined(_LIBCPP_SHORT_WCHAR)
  result r = ucs2_to_utf8(_frm, _frm_end, _frm_nxt, _to, _to_end, _to_nxt, __maxcode_, __mode_);
#  else
  result r = ucs4_to_utf8(_frm, _frm_end, _frm_nxt, _to, _to_end, _to_nxt, __maxcode_, __mode_);
#  endif
  frm_nxt = frm + (_frm_nxt - _frm);
  to_nxt  = to + (_to_nxt - _to);
  return r;
}

__codecvt_utf8<wchar_t>::result __codecvt_utf8<wchar_t>::do_in(
    state_type&,
    const extern_type* frm,
    const extern_type* frm_end,
    const extern_type*& frm_nxt,
    intern_type* to,
    intern_type* to_end,
    intern_type*& to_nxt) const {
  const uint8_t* _frm     = reinterpret_cast<const uint8_t*>(frm);
  const uint8_t* _frm_end = reinterpret_cast<const uint8_t*>(frm_end);
  const uint8_t* _frm_nxt = _frm;
#  if defined(_LIBCPP_SHORT_WCHAR)
  uint16_t* _to     = reinterpret_cast<uint16_t*>(to);
  uint16_t* _to_end = reinterpret_cast<uint16_t*>(to_end);
  uint16_t* _to_nxt = _to;
  result r          = utf8_to_ucs2(_frm, _frm_end, _frm_nxt, _to, _to_end, _to_nxt, __maxcode_, __mode_);
#  else
  uint32_t* _to     = reinterpret_cast<uint32_t*>(to);
  uint32_t* _to_end = reinterpret_cast<uint32_t*>(to_end);
  uint32_t* _to_nxt = _to;
  result r          = utf8_to_ucs4(_frm, _frm_end, _frm_nxt, _to, _to_end, _to_nxt, __maxcode_, __mode_);
#  endif
  frm_nxt = frm + (_frm_nxt - _frm);
  to_nxt  = to + (_to_nxt - _to);
  return r;
}

__codecvt_utf8<wchar_t>::result
__codecvt_utf8<wchar_t>::do_unshift(state_type&, extern_type* to, extern_type*, extern_type*& to_nxt) const {
  to_nxt = to;
  return noconv;
}

int __codecvt_utf8<wchar_t>::do_encoding() const noexcept { return 0; }

bool __codecvt_utf8<wchar_t>::do_always_noconv() const noexcept { return false; }

int __codecvt_utf8<wchar_t>::do_length(
    state_type&, const extern_type* frm, const extern_type* frm_end, size_t mx) const {
  const uint8_t* _frm     = reinterpret_cast<const uint8_t*>(frm);
  const uint8_t* _frm_end = reinterpret_cast<const uint8_t*>(frm_end);
#  if defined(_LIBCPP_SHORT_WCHAR)
  return utf8_to_ucs2_length(_frm, _frm_end, mx, __maxcode_, __mode_);
#  else
  return utf8_to_ucs4_length(_frm, _frm_end, mx, __maxcode_, __mode_);
#  endif
}

_LIBCPP_SUPPRESS_DEPRECATED_PUSH
int __codecvt_utf8<wchar_t>::do_max_length() const noexcept {
#  if defined(_LIBCPP_SHORT_WCHAR)
  if (__mode_ & consume_header)
    return 6;
  return 3;
#  else
  if (__mode_ & consume_header)
    return 7;
  return 4;
#  endif
}
#endif // _LIBCPP_HAS_NO_WIDE_CHARACTERS

// __codecvt_utf8<char16_t>

__codecvt_utf8<char16_t>::result __codecvt_utf8<char16_t>::do_out(
    state_type&,
    const intern_type* frm,
    const intern_type* frm_end,
    const intern_type*& frm_nxt,
    extern_type* to,
    extern_type* to_end,
    extern_type*& to_nxt) const {
  const uint16_t* _frm     = reinterpret_cast<const uint16_t*>(frm);
  const uint16_t* _frm_end = reinterpret_cast<const uint16_t*>(frm_end);
  const uint16_t* _frm_nxt = _frm;
  uint8_t* _to             = reinterpret_cast<uint8_t*>(to);
  uint8_t* _to_end         = reinterpret_cast<uint8_t*>(to_end);
  uint8_t* _to_nxt         = _to;
  result r                 = ucs2_to_utf8(_frm, _frm_end, _frm_nxt, _to, _to_end, _to_nxt, __maxcode_, __mode_);
  frm_nxt                  = frm + (_frm_nxt - _frm);
  to_nxt                   = to + (_to_nxt - _to);
  return r;
}

__codecvt_utf8<char16_t>::result __codecvt_utf8<char16_t>::do_in(
    state_type&,
    const extern_type* frm,
    const extern_type* frm_end,
    const extern_type*& frm_nxt,
    intern_type* to,
    intern_type* to_end,
    intern_type*& to_nxt) const {
  const uint8_t* _frm     = reinterpret_cast<const uint8_t*>(frm);
  const uint8_t* _frm_end = reinterpret_cast<const uint8_t*>(frm_end);
  const uint8_t* _frm_nxt = _frm;
  uint16_t* _to           = reinterpret_cast<uint16_t*>(to);
  uint16_t* _to_end       = reinterpret_cast<uint16_t*>(to_end);
  uint16_t* _to_nxt       = _to;
  result r                = utf8_to_ucs2(_frm, _frm_end, _frm_nxt, _to, _to_end, _to_nxt, __maxcode_, __mode_);
  frm_nxt                 = frm + (_frm_nxt - _frm);
  to_nxt                  = to + (_to_nxt - _to);
  return r;
}

__codecvt_utf8<char16_t>::result
__codecvt_utf8<char16_t>::do_unshift(state_type&, extern_type* to, extern_type*, extern_type*& to_nxt) const {
  to_nxt = to;
  return noconv;
}

int __codecvt_utf8<char16_t>::do_encoding() const noexcept { return 0; }

bool __codecvt_utf8<char16_t>::do_always_noconv() const noexcept { return false; }

int __codecvt_utf8<char16_t>::do_length(
    state_type&, const extern_type* frm, const extern_type* frm_end, size_t mx) const {
  const uint8_t* _frm     = reinterpret_cast<const uint8_t*>(frm);
  const uint8_t* _frm_end = reinterpret_cast<const uint8_t*>(frm_end);
  return utf8_to_ucs2_length(_frm, _frm_end, mx, __maxcode_, __mode_);
}

_LIBCPP_SUPPRESS_DEPRECATED_PUSH
int __codecvt_utf8<char16_t>::do_max_length() const noexcept {
  if (__mode_ & consume_header)
    return 6;
  return 3;
}
_LIBCPP_SUPPRESS_DEPRECATED_POP

// __codecvt_utf8<char32_t>

__codecvt_utf8<char32_t>::result __codecvt_utf8<char32_t>::do_out(
    state_type&,
    const intern_type* frm,
    const intern_type* frm_end,
    const intern_type*& frm_nxt,
    extern_type* to,
    extern_type* to_end,
    extern_type*& to_nxt) const {
  const uint32_t* _frm     = reinterpret_cast<const uint32_t*>(frm);
  const uint32_t* _frm_end = reinterpret_cast<const uint32_t*>(frm_end);
  const uint32_t* _frm_nxt = _frm;
  uint8_t* _to             = reinterpret_cast<uint8_t*>(to);
  uint8_t* _to_end         = reinterpret_cast<uint8_t*>(to_end);
  uint8_t* _to_nxt         = _to;
  result r                 = ucs4_to_utf8(_frm, _frm_end, _frm_nxt, _to, _to_end, _to_nxt, __maxcode_, __mode_);
  frm_nxt                  = frm + (_frm_nxt - _frm);
  to_nxt                   = to + (_to_nxt - _to);
  return r;
}

__codecvt_utf8<char32_t>::result __codecvt_utf8<char32_t>::do_in(
    state_type&,
    const extern_type* frm,
    const extern_type* frm_end,
    const extern_type*& frm_nxt,
    intern_type* to,
    intern_type* to_end,
    intern_type*& to_nxt) const {
  const uint8_t* _frm     = reinterpret_cast<const uint8_t*>(frm);
  const uint8_t* _frm_end = reinterpret_cast<const uint8_t*>(frm_end);
  const uint8_t* _frm_nxt = _frm;
  uint32_t* _to           = reinterpret_cast<uint32_t*>(to);
  uint32_t* _to_end       = reinterpret_cast<uint32_t*>(to_end);
  uint32_t* _to_nxt       = _to;
  result r                = utf8_to_ucs4(_frm, _frm_end, _frm_nxt, _to, _to_end, _to_nxt, __maxcode_, __mode_);
  frm_nxt                 = frm + (_frm_nxt - _frm);
  to_nxt                  = to + (_to_nxt - _to);
  return r;
}

__codecvt_utf8<char32_t>::result
__codecvt_utf8<char32_t>::do_unshift(state_type&, extern_type* to, extern_type*, extern_type*& to_nxt) const {
  to_nxt = to;
  return noconv;
}

int __codecvt_utf8<char32_t>::do_encoding() const noexcept { return 0; }

bool __codecvt_utf8<char32_t>::do_always_noconv() const noexcept { return false; }

int __codecvt_utf8<char32_t>::do_length(
    state_type&, const extern_type* frm, const extern_type* frm_end, size_t mx) const {
  const uint8_t* _frm     = reinterpret_cast<const uint8_t*>(frm);
  const uint8_t* _frm_end = reinterpret_cast<const uint8_t*>(frm_end);
  return utf8_to_ucs4_length(_frm, _frm_end, mx, __maxcode_, __mode_);
}

_LIBCPP_SUPPRESS_DEPRECATED_PUSH
int __codecvt_utf8<char32_t>::do_max_length() const noexcept {
  if (__mode_ & consume_header)
    return 7;
  return 4;
}
_LIBCPP_SUPPRESS_DEPRECATED_POP

// __codecvt_utf16<wchar_t, false>

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
__codecvt_utf16<wchar_t, false>::result __codecvt_utf16<wchar_t, false>::do_out(
    state_type&,
    const intern_type* frm,
    const intern_type* frm_end,
    const intern_type*& frm_nxt,
    extern_type* to,
    extern_type* to_end,
    extern_type*& to_nxt) const {
#  if defined(_LIBCPP_SHORT_WCHAR)
  const uint16_t* _frm     = reinterpret_cast<const uint16_t*>(frm);
  const uint16_t* _frm_end = reinterpret_cast<const uint16_t*>(frm_end);
  const uint16_t* _frm_nxt = _frm;
#  else
  const uint32_t* _frm     = reinterpret_cast<const uint32_t*>(frm);
  const uint32_t* _frm_end = reinterpret_cast<const uint32_t*>(frm_end);
  const uint32_t* _frm_nxt = _frm;
#  endif
  uint8_t* _to     = reinterpret_cast<uint8_t*>(to);
  uint8_t* _to_end = reinterpret_cast<uint8_t*>(to_end);
  uint8_t* _to_nxt = _to;
#  if defined(_LIBCPP_SHORT_WCHAR)
  result r = ucs2_to_utf16be(_frm, _frm_end, _frm_nxt, _to, _to_end, _to_nxt, __maxcode_, __mode_);
#  else
  result r = ucs4_to_utf16be(_frm, _frm_end, _frm_nxt, _to, _to_end, _to_nxt, __maxcode_, __mode_);
#  endif
  frm_nxt = frm + (_frm_nxt - _frm);
  to_nxt  = to + (_to_nxt - _to);
  return r;
}

__codecvt_utf16<wchar_t, false>::result __codecvt_utf16<wchar_t, false>::do_in(
    state_type&,
    const extern_type* frm,
    const extern_type* frm_end,
    const extern_type*& frm_nxt,
    intern_type* to,
    intern_type* to_end,
    intern_type*& to_nxt) const {
  const uint8_t* _frm     = reinterpret_cast<const uint8_t*>(frm);
  const uint8_t* _frm_end = reinterpret_cast<const uint8_t*>(frm_end);
  const uint8_t* _frm_nxt = _frm;
#  if defined(_LIBCPP_SHORT_WCHAR)
  uint16_t* _to     = reinterpret_cast<uint16_t*>(to);
  uint16_t* _to_end = reinterpret_cast<uint16_t*>(to_end);
  uint16_t* _to_nxt = _to;
  result r          = utf16be_to_ucs2(_frm, _frm_end, _frm_nxt, _to, _to_end, _to_nxt, __maxcode_, __mode_);
#  else
  uint32_t* _to     = reinterpret_cast<uint32_t*>(to);
  uint32_t* _to_end = reinterpret_cast<uint32_t*>(to_end);
  uint32_t* _to_nxt = _to;
  result r          = utf16be_to_ucs4(_frm, _frm_end, _frm_nxt, _to, _to_end, _to_nxt, __maxcode_, __mode_);
#  endif
  frm_nxt = frm + (_frm_nxt - _frm);
  to_nxt  = to + (_to_nxt - _to);
  return r;
}

__codecvt_utf16<wchar_t, false>::result
__codecvt_utf16<wchar_t, false>::do_unshift(state_type&, extern_type* to, extern_type*, extern_type*& to_nxt) const {
  to_nxt = to;
  return noconv;
}

int __codecvt_utf16<wchar_t, false>::do_encoding() const noexcept { return 0; }

bool __codecvt_utf16<wchar_t, false>::do_always_noconv() const noexcept { return false; }

int __codecvt_utf16<wchar_t, false>::do_length(
    state_type&, const extern_type* frm, const extern_type* frm_end, size_t mx) const {
  const uint8_t* _frm     = reinterpret_cast<const uint8_t*>(frm);
  const uint8_t* _frm_end = reinterpret_cast<const uint8_t*>(frm_end);
#  if defined(_LIBCPP_SHORT_WCHAR)
  return utf16be_to_ucs2_length(_frm, _frm_end, mx, __maxcode_, __mode_);
#  else
  return utf16be_to_ucs4_length(_frm, _frm_end, mx, __maxcode_, __mode_);
#  endif
}

int __codecvt_utf16<wchar_t, false>::do_max_length() const noexcept {
#  if defined(_LIBCPP_SHORT_WCHAR)
  if (__mode_ & consume_header)
    return 4;
  return 2;
#  else
  if (__mode_ & consume_header)
    return 6;
  return 4;
#  endif
}

// __codecvt_utf16<wchar_t, true>

__codecvt_utf16<wchar_t, true>::result __codecvt_utf16<wchar_t, true>::do_out(
    state_type&,
    const intern_type* frm,
    const intern_type* frm_end,
    const intern_type*& frm_nxt,
    extern_type* to,
    extern_type* to_end,
    extern_type*& to_nxt) const {
#  if defined(_LIBCPP_SHORT_WCHAR)
  const uint16_t* _frm     = reinterpret_cast<const uint16_t*>(frm);
  const uint16_t* _frm_end = reinterpret_cast<const uint16_t*>(frm_end);
  const uint16_t* _frm_nxt = _frm;
#  else
  const uint32_t* _frm     = reinterpret_cast<const uint32_t*>(frm);
  const uint32_t* _frm_end = reinterpret_cast<const uint32_t*>(frm_end);
  const uint32_t* _frm_nxt = _frm;
#  endif
  uint8_t* _to     = reinterpret_cast<uint8_t*>(to);
  uint8_t* _to_end = reinterpret_cast<uint8_t*>(to_end);
  uint8_t* _to_nxt = _to;
#  if defined(_LIBCPP_SHORT_WCHAR)
  result r = ucs2_to_utf16le(_frm, _frm_end, _frm_nxt, _to, _to_end, _to_nxt, __maxcode_, __mode_);
#  else
  result r = ucs4_to_utf16le(_frm, _frm_end, _frm_nxt, _to, _to_end, _to_nxt, __maxcode_, __mode_);
#  endif
  frm_nxt = frm + (_frm_nxt - _frm);
  to_nxt  = to + (_to_nxt - _to);
  return r;
}

__codecvt_utf16<wchar_t, true>::result __codecvt_utf16<wchar_t, true>::do_in(
    state_type&,
    const extern_type* frm,
    const extern_type* frm_end,
    const extern_type*& frm_nxt,
    intern_type* to,
    intern_type* to_end,
    intern_type*& to_nxt) const {
  const uint8_t* _frm     = reinterpret_cast<const uint8_t*>(frm);
  const uint8_t* _frm_end = reinterpret_cast<const uint8_t*>(frm_end);
  const uint8_t* _frm_nxt = _frm;
#  if defined(_LIBCPP_SHORT_WCHAR)
  uint16_t* _to     = reinterpret_cast<uint16_t*>(to);
  uint16_t* _to_end = reinterpret_cast<uint16_t*>(to_end);
  uint16_t* _to_nxt = _to;
  result r          = utf16le_to_ucs2(_frm, _frm_end, _frm_nxt, _to, _to_end, _to_nxt, __maxcode_, __mode_);
#  else
  uint32_t* _to     = reinterpret_cast<uint32_t*>(to);
  uint32_t* _to_end = reinterpret_cast<uint32_t*>(to_end);
  uint32_t* _to_nxt = _to;
  result r          = utf16le_to_ucs4(_frm, _frm_end, _frm_nxt, _to, _to_end, _to_nxt, __maxcode_, __mode_);
#  endif
  frm_nxt = frm + (_frm_nxt - _frm);
  to_nxt  = to + (_to_nxt - _to);
  return r;
}

__codecvt_utf16<wchar_t, true>::result
__codecvt_utf16<wchar_t, true>::do_unshift(state_type&, extern_type* to, extern_type*, extern_type*& to_nxt) const {
  to_nxt = to;
  return noconv;
}

int __codecvt_utf16<wchar_t, true>::do_encoding() const noexcept { return 0; }

bool __codecvt_utf16<wchar_t, true>::do_always_noconv() const noexcept { return false; }

int __codecvt_utf16<wchar_t, true>::do_length(
    state_type&, const extern_type* frm, const extern_type* frm_end, size_t mx) const {
  const uint8_t* _frm     = reinterpret_cast<const uint8_t*>(frm);
  const uint8_t* _frm_end = reinterpret_cast<const uint8_t*>(frm_end);
#  if defined(_LIBCPP_SHORT_WCHAR)
  return utf16le_to_ucs2_length(_frm, _frm_end, mx, __maxcode_, __mode_);
#  else
  return utf16le_to_ucs4_length(_frm, _frm_end, mx, __maxcode_, __mode_);
#  endif
}

int __codecvt_utf16<wchar_t, true>::do_max_length() const noexcept {
#  if defined(_LIBCPP_SHORT_WCHAR)
  if (__mode_ & consume_header)
    return 4;
  return 2;
#  else
  if (__mode_ & consume_header)
    return 6;
  return 4;
#  endif
}
#endif // _LIBCPP_HAS_NO_WIDE_CHARACTERS

// __codecvt_utf16<char16_t, false>

__codecvt_utf16<char16_t, false>::result __codecvt_utf16<char16_t, false>::do_out(
    state_type&,
    const intern_type* frm,
    const intern_type* frm_end,
    const intern_type*& frm_nxt,
    extern_type* to,
    extern_type* to_end,
    extern_type*& to_nxt) const {
  const uint16_t* _frm     = reinterpret_cast<const uint16_t*>(frm);
  const uint16_t* _frm_end = reinterpret_cast<const uint16_t*>(frm_end);
  const uint16_t* _frm_nxt = _frm;
  uint8_t* _to             = reinterpret_cast<uint8_t*>(to);
  uint8_t* _to_end         = reinterpret_cast<uint8_t*>(to_end);
  uint8_t* _to_nxt         = _to;
  result r                 = ucs2_to_utf16be(_frm, _frm_end, _frm_nxt, _to, _to_end, _to_nxt, __maxcode_, __mode_);
  frm_nxt                  = frm + (_frm_nxt - _frm);
  to_nxt                   = to + (_to_nxt - _to);
  return r;
}

__codecvt_utf16<char16_t, false>::result __codecvt_utf16<char16_t, false>::do_in(
    state_type&,
    const extern_type* frm,
    const extern_type* frm_end,
    const extern_type*& frm_nxt,
    intern_type* to,
    intern_type* to_end,
    intern_type*& to_nxt) const {
  const uint8_t* _frm     = reinterpret_cast<const uint8_t*>(frm);
  const uint8_t* _frm_end = reinterpret_cast<const uint8_t*>(frm_end);
  const uint8_t* _frm_nxt = _frm;
  uint16_t* _to           = reinterpret_cast<uint16_t*>(to);
  uint16_t* _to_end       = reinterpret_cast<uint16_t*>(to_end);
  uint16_t* _to_nxt       = _to;
  result r                = utf16be_to_ucs2(_frm, _frm_end, _frm_nxt, _to, _to_end, _to_nxt, __maxcode_, __mode_);
  frm_nxt                 = frm + (_frm_nxt - _frm);
  to_nxt                  = to + (_to_nxt - _to);
  return r;
}

__codecvt_utf16<char16_t, false>::result
__codecvt_utf16<char16_t, false>::do_unshift(state_type&, extern_type* to, extern_type*, extern_type*& to_nxt) const {
  to_nxt = to;
  return noconv;
}

int __codecvt_utf16<char16_t, false>::do_encoding() const noexcept { return 0; }

bool __codecvt_utf16<char16_t, false>::do_always_noconv() const noexcept { return false; }

int __codecvt_utf16<char16_t, false>::do_length(
    state_type&, const extern_type* frm, const extern_type* frm_end, size_t mx) const {
  const uint8_t* _frm     = reinterpret_cast<const uint8_t*>(frm);
  const uint8_t* _frm_end = reinterpret_cast<const uint8_t*>(frm_end);
  return utf16be_to_ucs2_length(_frm, _frm_end, mx, __maxcode_, __mode_);
}

_LIBCPP_SUPPRESS_DEPRECATED_PUSH
int __codecvt_utf16<char16_t, false>::do_max_length() const noexcept {
  if (__mode_ & consume_header)
    return 4;
  return 2;
}
_LIBCPP_SUPPRESS_DEPRECATED_POP

// __codecvt_utf16<char16_t, true>

__codecvt_utf16<char16_t, true>::result __codecvt_utf16<char16_t, true>::do_out(
    state_type&,
    const intern_type* frm,
    const intern_type* frm_end,
    const intern_type*& frm_nxt,
    extern_type* to,
    extern_type* to_end,
    extern_type*& to_nxt) const {
  const uint16_t* _frm     = reinterpret_cast<const uint16_t*>(frm);
  const uint16_t* _frm_end = reinterpret_cast<const uint16_t*>(frm_end);
  const uint16_t* _frm_nxt = _frm;
  uint8_t* _to             = reinterpret_cast<uint8_t*>(to);
  uint8_t* _to_end         = reinterpret_cast<uint8_t*>(to_end);
  uint8_t* _to_nxt         = _to;
  result r                 = ucs2_to_utf16le(_frm, _frm_end, _frm_nxt, _to, _to_end, _to_nxt, __maxcode_, __mode_);
  frm_nxt                  = frm + (_frm_nxt - _frm);
  to_nxt                   = to + (_to_nxt - _to);
  return r;
}

__codecvt_utf16<char16_t, true>::result __codecvt_utf16<char16_t, true>::do_in(
    state_type&,
    const extern_type* frm,
    const extern_type* frm_end,
    const extern_type*& frm_nxt,
    intern_type* to,
    intern_type* to_end,
    intern_type*& to_nxt) const {
  const uint8_t* _frm     = reinterpret_cast<const uint8_t*>(frm);
  const uint8_t* _frm_end = reinterpret_cast<const uint8_t*>(frm_end);
  const uint8_t* _frm_nxt = _frm;
  uint16_t* _to           = reinterpret_cast<uint16_t*>(to);
  uint16_t* _to_end       = reinterpret_cast<uint16_t*>(to_end);
  uint16_t* _to_nxt       = _to;
  result r                = utf16le_to_ucs2(_frm, _frm_end, _frm_nxt, _to, _to_end, _to_nxt, __maxcode_, __mode_);
  frm_nxt                 = frm + (_frm_nxt - _frm);
  to_nxt                  = to + (_to_nxt - _to);
  return r;
}

__codecvt_utf16<char16_t, true>::result
__codecvt_utf16<char16_t, true>::do_unshift(state_type&, extern_type* to, extern_type*, extern_type*& to_nxt) const {
  to_nxt = to;
  return noconv;
}

int __codecvt_utf16<char16_t, true>::do_encoding() const noexcept { return 0; }

bool __codecvt_utf16<char16_t, true>::do_always_noconv() const noexcept { return false; }

int __codecvt_utf16<char16_t, true>::do_length(
    state_type&, const extern_type* frm, const extern_type* frm_end, size_t mx) const {
  const uint8_t* _frm     = reinterpret_cast<const uint8_t*>(frm);
  const uint8_t* _frm_end = reinterpret_cast<const uint8_t*>(frm_end);
  return utf16le_to_ucs2_length(_frm, _frm_end, mx, __maxcode_, __mode_);
}

_LIBCPP_SUPPRESS_DEPRECATED_PUSH
int __codecvt_utf16<char16_t, true>::do_max_length() const noexcept {
  if (__mode_ & consume_header)
    return 4;
  return 2;
}
_LIBCPP_SUPPRESS_DEPRECATED_POP

// __codecvt_utf16<char32_t, false>

__codecvt_utf16<char32_t, false>::result __codecvt_utf16<char32_t, false>::do_out(
    state_type&,
    const intern_type* frm,
    const intern_type* frm_end,
    const intern_type*& frm_nxt,
    extern_type* to,
    extern_type* to_end,
    extern_type*& to_nxt) const {
  const uint32_t* _frm     = reinterpret_cast<const uint32_t*>(frm);
  const uint32_t* _frm_end = reinterpret_cast<const uint32_t*>(frm_end);
  const uint32_t* _frm_nxt = _frm;
  uint8_t* _to             = reinterpret_cast<uint8_t*>(to);
  uint8_t* _to_end         = reinterpret_cast<uint8_t*>(to_end);
  uint8_t* _to_nxt         = _to;
  result r                 = ucs4_to_utf16be(_frm, _frm_end, _frm_nxt, _to, _to_end, _to_nxt, __maxcode_, __mode_);
  frm_nxt                  = frm + (_frm_nxt - _frm);
  to_nxt                   = to + (_to_nxt - _to);
  return r;
}

__codecvt_utf16<char32_t, false>::result __codecvt_utf16<char32_t, false>::do_in(
    state_type&,
    const extern_type* frm,
    const extern_type* frm_end,
    const extern_type*& frm_nxt,
    intern_type* to,
    intern_type* to_end,
    intern_type*& to_nxt) const {
  const uint8_t* _frm     = reinterpret_cast<const uint8_t*>(frm);
  const uint8_t* _frm_end = reinterpret_cast<const uint8_t*>(frm_end);
  const uint8_t* _frm_nxt = _frm;
  uint32_t* _to           = reinterpret_cast<uint32_t*>(to);
  uint32_t* _to_end       = reinterpret_cast<uint32_t*>(to_end);
  uint32_t* _to_nxt       = _to;
  result r                = utf16be_to_ucs4(_frm, _frm_end, _frm_nxt, _to, _to_end, _to_nxt, __maxcode_, __mode_);
  frm_nxt                 = frm + (_frm_nxt - _frm);
  to_nxt                  = to + (_to_nxt - _to);
  return r;
}

__codecvt_utf16<char32_t, false>::result
__codecvt_utf16<char32_t, false>::do_unshift(state_type&, extern_type* to, extern_type*, extern_type*& to_nxt) const {
  to_nxt = to;
  return noconv;
}

int __codecvt_utf16<char32_t, false>::do_encoding() const noexcept { return 0; }

bool __codecvt_utf16<char32_t, false>::do_always_noconv() const noexcept { return false; }

int __codecvt_utf16<char32_t, false>::do_length(
    state_type&, const extern_type* frm, const extern_type* frm_end, size_t mx) const {
  const uint8_t* _frm     = reinterpret_cast<const uint8_t*>(frm);
  const uint8_t* _frm_end = reinterpret_cast<const uint8_t*>(frm_end);
  return utf16be_to_ucs4_length(_frm, _frm_end, mx, __maxcode_, __mode_);
}

_LIBCPP_SUPPRESS_DEPRECATED_PUSH
int __codecvt_utf16<char32_t, false>::do_max_length() const noexcept {
  if (__mode_ & consume_header)
    return 6;
  return 4;
}
_LIBCPP_SUPPRESS_DEPRECATED_POP

// __codecvt_utf16<char32_t, true>

__codecvt_utf16<char32_t, true>::result __codecvt_utf16<char32_t, true>::do_out(
    state_type&,
    const intern_type* frm,
    const intern_type* frm_end,
    const intern_type*& frm_nxt,
    extern_type* to,
    extern_type* to_end,
    extern_type*& to_nxt) const {
  const uint32_t* _frm     = reinterpret_cast<const uint32_t*>(frm);
  const uint32_t* _frm_end = reinterpret_cast<const uint32_t*>(frm_end);
  const uint32_t* _frm_nxt = _frm;
  uint8_t* _to             = reinterpret_cast<uint8_t*>(to);
  uint8_t* _to_end         = reinterpret_cast<uint8_t*>(to_end);
  uint8_t* _to_nxt         = _to;
  result r                 = ucs4_to_utf16le(_frm, _frm_end, _frm_nxt, _to, _to_end, _to_nxt, __maxcode_, __mode_);
  frm_nxt                  = frm + (_frm_nxt - _frm);
  to_nxt                   = to + (_to_nxt - _to);
  return r;
}

__codecvt_utf16<char32_t, true>::result __codecvt_utf16<char32_t, true>::do_in(
    state_type&,
    const extern_type* frm,
    const extern_type* frm_end,
    const extern_type*& frm_nxt,
    intern_type* to,
    intern_type* to_end,
    intern_type*& to_nxt) const {
  const uint8_t* _frm     = reinterpret_cast<const uint8_t*>(frm);
  const uint8_t* _frm_end = reinterpret_cast<const uint8_t*>(frm_end);
  const uint8_t* _frm_nxt = _frm;
  uint32_t* _to           = reinterpret_cast<uint32_t*>(to);
  uint32_t* _to_end       = reinterpret_cast<uint32_t*>(to_end);
  uint32_t* _to_nxt       = _to;
  result r                = utf16le_to_ucs4(_frm, _frm_end, _frm_nxt, _to, _to_end, _to_nxt, __maxcode_, __mode_);
  frm_nxt                 = frm + (_frm_nxt - _frm);
  to_nxt                  = to + (_to_nxt - _to);
  return r;
}

__codecvt_utf16<char32_t, true>::result
__codecvt_utf16<char32_t, true>::do_unshift(state_type&, extern_type* to, extern_type*, extern_type*& to_nxt) const {
  to_nxt = to;
  return noconv;
}

int __codecvt_utf16<char32_t, true>::do_encoding() const noexcept { return 0; }

bool __codecvt_utf16<char32_t, true>::do_always_noconv() const noexcept { return false; }

int __codecvt_utf16<char32_t, true>::do_length(
    state_type&, const extern_type* frm, const extern_type* frm_end, size_t mx) const {
  const uint8_t* _frm     = reinterpret_cast<const uint8_t*>(frm);
  const uint8_t* _frm_end = reinterpret_cast<const uint8_t*>(frm_end);
  return utf16le_to_ucs4_length(_frm, _frm_end, mx, __maxcode_, __mode_);
}

_LIBCPP_SUPPRESS_DEPRECATED_PUSH
int __codecvt_utf16<char32_t, true>::do_max_length() const noexcept {
  if (__mode_ & consume_header)
    return 6;
  return 4;
}
_LIBCPP_SUPPRESS_DEPRECATED_POP

// __codecvt_utf8_utf16<wchar_t>

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
__codecvt_utf8_utf16<wchar_t>::result __codecvt_utf8_utf16<wchar_t>::do_out(
    state_type&,
    const intern_type* frm,
    const intern_type* frm_end,
    const intern_type*& frm_nxt,
    extern_type* to,
    extern_type* to_end,
    extern_type*& to_nxt) const {
#  if defined(_LIBCPP_SHORT_WCHAR)
  const uint16_t* _frm     = reinterpret_cast<const uint16_t*>(frm);
  const uint16_t* _frm_end = reinterpret_cast<const uint16_t*>(frm_end);
  const uint16_t* _frm_nxt = _frm;
#  else
  const uint32_t* _frm     = reinterpret_cast<const uint32_t*>(frm);
  const uint32_t* _frm_end = reinterpret_cast<const uint32_t*>(frm_end);
  const uint32_t* _frm_nxt = _frm;
#  endif
  uint8_t* _to     = reinterpret_cast<uint8_t*>(to);
  uint8_t* _to_end = reinterpret_cast<uint8_t*>(to_end);
  uint8_t* _to_nxt = _to;
  result r         = utf16_to_utf8(_frm, _frm_end, _frm_nxt, _to, _to_end, _to_nxt, __maxcode_, __mode_);
  frm_nxt          = frm + (_frm_nxt - _frm);
  to_nxt           = to + (_to_nxt - _to);
  return r;
}

__codecvt_utf8_utf16<wchar_t>::result __codecvt_utf8_utf16<wchar_t>::do_in(
    state_type&,
    const extern_type* frm,
    const extern_type* frm_end,
    const extern_type*& frm_nxt,
    intern_type* to,
    intern_type* to_end,
    intern_type*& to_nxt) const {
  const uint8_t* _frm     = reinterpret_cast<const uint8_t*>(frm);
  const uint8_t* _frm_end = reinterpret_cast<const uint8_t*>(frm_end);
  const uint8_t* _frm_nxt = _frm;
#  if defined(_LIBCPP_SHORT_WCHAR)
  uint16_t* _to     = reinterpret_cast<uint16_t*>(to);
  uint16_t* _to_end = reinterpret_cast<uint16_t*>(to_end);
  uint16_t* _to_nxt = _to;
#  else
  uint32_t* _to     = reinterpret_cast<uint32_t*>(to);
  uint32_t* _to_end = reinterpret_cast<uint32_t*>(to_end);
  uint32_t* _to_nxt = _to;
#  endif
  result r = utf8_to_utf16(_frm, _frm_end, _frm_nxt, _to, _to_end, _to_nxt, __maxcode_, __mode_);
  frm_nxt  = frm + (_frm_nxt - _frm);
  to_nxt   = to + (_to_nxt - _to);
  return r;
}

__codecvt_utf8_utf16<wchar_t>::result
__codecvt_utf8_utf16<wchar_t>::do_unshift(state_type&, extern_type* to, extern_type*, extern_type*& to_nxt) const {
  to_nxt = to;
  return noconv;
}

int __codecvt_utf8_utf16<wchar_t>::do_encoding() const noexcept { return 0; }

bool __codecvt_utf8_utf16<wchar_t>::do_always_noconv() const noexcept { return false; }

int __codecvt_utf8_utf16<wchar_t>::do_length(
    state_type&, const extern_type* frm, const extern_type* frm_end, size_t mx) const {
  const uint8_t* _frm     = reinterpret_cast<const uint8_t*>(frm);
  const uint8_t* _frm_end = reinterpret_cast<const uint8_t*>(frm_end);
  return utf8_to_utf16_length(_frm, _frm_end, mx, __maxcode_, __mode_);
}

int __codecvt_utf8_utf16<wchar_t>::do_max_length() const noexcept {
  if (__mode_ & consume_header)
    return 7;
  return 4;
}
#endif // _LIBCPP_HAS_NO_WIDE_CHARACTERS

// __codecvt_utf8_utf16<char16_t>

__codecvt_utf8_utf16<char16_t>::result __codecvt_utf8_utf16<char16_t>::do_out(
    state_type&,
    const intern_type* frm,
    const intern_type* frm_end,
    const intern_type*& frm_nxt,
    extern_type* to,
    extern_type* to_end,
    extern_type*& to_nxt) const {
  const uint16_t* _frm     = reinterpret_cast<const uint16_t*>(frm);
  const uint16_t* _frm_end = reinterpret_cast<const uint16_t*>(frm_end);
  const uint16_t* _frm_nxt = _frm;
  uint8_t* _to             = reinterpret_cast<uint8_t*>(to);
  uint8_t* _to_end         = reinterpret_cast<uint8_t*>(to_end);
  uint8_t* _to_nxt         = _to;
  result r                 = utf16_to_utf8(_frm, _frm_end, _frm_nxt, _to, _to_end, _to_nxt, __maxcode_, __mode_);
  frm_nxt                  = frm + (_frm_nxt - _frm);
  to_nxt                   = to + (_to_nxt - _to);
  return r;
}

__codecvt_utf8_utf16<char16_t>::result __codecvt_utf8_utf16<char16_t>::do_in(
    state_type&,
    const extern_type* frm,
    const extern_type* frm_end,
    const extern_type*& frm_nxt,
    intern_type* to,
    intern_type* to_end,
    intern_type*& to_nxt) const {
  const uint8_t* _frm     = reinterpret_cast<const uint8_t*>(frm);
  const uint8_t* _frm_end = reinterpret_cast<const uint8_t*>(frm_end);
  const uint8_t* _frm_nxt = _frm;
  uint16_t* _to           = reinterpret_cast<uint16_t*>(to);
  uint16_t* _to_end       = reinterpret_cast<uint16_t*>(to_end);
  uint16_t* _to_nxt       = _to;
  result r                = utf8_to_utf16(_frm, _frm_end, _frm_nxt, _to, _to_end, _to_nxt, __maxcode_, __mode_);
  frm_nxt                 = frm + (_frm_nxt - _frm);
  to_nxt                  = to + (_to_nxt - _to);
  return r;
}

__codecvt_utf8_utf16<char16_t>::result
__codecvt_utf8_utf16<char16_t>::do_unshift(state_type&, extern_type* to, extern_type*, extern_type*& to_nxt) const {
  to_nxt = to;
  return noconv;
}

int __codecvt_utf8_utf16<char16_t>::do_encoding() const noexcept { return 0; }

bool __codecvt_utf8_utf16<char16_t>::do_always_noconv() const noexcept { return false; }

int __codecvt_utf8_utf16<char16_t>::do_length(
    state_type&, const extern_type* frm, const extern_type* frm_end, size_t mx) const {
  const uint8_t* _frm     = reinterpret_cast<const uint8_t*>(frm);
  const uint8_t* _frm_end = reinterpret_cast<const uint8_t*>(frm_end);
  return utf8_to_utf16_length(_frm, _frm_end, mx, __maxcode_, __mode_);
}

_LIBCPP_SUPPRESS_DEPRECATED_PUSH
int __codecvt_utf8_utf16<char16_t>::do_max_length() const noexcept {
  if (__mode_ & consume_header)
    return 7;
  return 4;
}
_LIBCPP_SUPPRESS_DEPRECATED_POP

// __codecvt_utf8_utf16<char32_t>

__codecvt_utf8_utf16<char32_t>::result __codecvt_utf8_utf16<char32_t>::do_out(
    state_type&,
    const intern_type* frm,
    const intern_type* frm_end,
    const intern_type*& frm_nxt,
    extern_type* to,
    extern_type* to_end,
    extern_type*& to_nxt) const {
  const uint32_t* _frm     = reinterpret_cast<const uint32_t*>(frm);
  const uint32_t* _frm_end = reinterpret_cast<const uint32_t*>(frm_end);
  const uint32_t* _frm_nxt = _frm;
  uint8_t* _to             = reinterpret_cast<uint8_t*>(to);
  uint8_t* _to_end         = reinterpret_cast<uint8_t*>(to_end);
  uint8_t* _to_nxt         = _to;
  result r                 = utf16_to_utf8(_frm, _frm_end, _frm_nxt, _to, _to_end, _to_nxt, __maxcode_, __mode_);
  frm_nxt                  = frm + (_frm_nxt - _frm);
  to_nxt                   = to + (_to_nxt - _to);
  return r;
}

__codecvt_utf8_utf16<char32_t>::result __codecvt_utf8_utf16<char32_t>::do_in(
    state_type&,
    const extern_type* frm,
    const extern_type* frm_end,
    const extern_type*& frm_nxt,
    intern_type* to,
    intern_type* to_end,
    intern_type*& to_nxt) const {
  const uint8_t* _frm     = reinterpret_cast<const uint8_t*>(frm);
  const uint8_t* _frm_end = reinterpret_cast<const uint8_t*>(frm_end);
  const uint8_t* _frm_nxt = _frm;
  uint32_t* _to           = reinterpret_cast<uint32_t*>(to);
  uint32_t* _to_end       = reinterpret_cast<uint32_t*>(to_end);
  uint32_t* _to_nxt       = _to;
  result r                = utf8_to_utf16(_frm, _frm_end, _frm_nxt, _to, _to_end, _to_nxt, __maxcode_, __mode_);
  frm_nxt                 = frm + (_frm_nxt - _frm);
  to_nxt                  = to + (_to_nxt - _to);
  return r;
}

__codecvt_utf8_utf16<char32_t>::result
__codecvt_utf8_utf16<char32_t>::do_unshift(state_type&, extern_type* to, extern_type*, extern_type*& to_nxt) const {
  to_nxt = to;
  return noconv;
}

int __codecvt_utf8_utf16<char32_t>::do_encoding() const noexcept { return 0; }

bool __codecvt_utf8_utf16<char32_t>::do_always_noconv() const noexcept { return false; }

int __codecvt_utf8_utf16<char32_t>::do_length(
    state_type&, const extern_type* frm, const extern_type* frm_end, size_t mx) const {
  const uint8_t* _frm     = reinterpret_cast<const uint8_t*>(frm);
  const uint8_t* _frm_end = reinterpret_cast<const uint8_t*>(frm_end);
  return utf8_to_utf16_length(_frm, _frm_end, mx, __maxcode_, __mode_);
}

_LIBCPP_SUPPRESS_DEPRECATED_PUSH
int __codecvt_utf8_utf16<char32_t>::do_max_length() const noexcept {
  if (__mode_ & consume_header)
    return 7;
  return 4;
}
_LIBCPP_SUPPRESS_DEPRECATED_POP

// __narrow_to_utf8<16>

__narrow_to_utf8<16>::~__narrow_to_utf8() {}

// __narrow_to_utf8<32>

__narrow_to_utf8<32>::~__narrow_to_utf8() {}

// __widen_from_utf8<16>

__widen_from_utf8<16>::~__widen_from_utf8() {}

// __widen_from_utf8<32>

__widen_from_utf8<32>::~__widen_from_utf8() {}

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
static bool checked_string_to_wchar_convert(wchar_t& dest, const char* ptr, locale_t loc) {
  if (*ptr == '\0')
    return false;
  mbstate_t mb = {};
  wchar_t out;
  size_t ret = __libcpp_mbrtowc_l(&out, ptr, strlen(ptr), &mb, loc);
  if (ret == static_cast<size_t>(-1) || ret == static_cast<size_t>(-2)) {
    return false;
  }
  dest = out;
  return true;
}
#endif // _LIBCPP_HAS_NO_WIDE_CHARACTERS

#ifdef _LIBCPP_HAS_NO_WIDE_CHARACTERS
static bool is_narrow_non_breaking_space(const char* ptr) {
  // https://www.fileformat.info/info/unicode/char/202f/index.htm
  return ptr[0] == '\xe2' && ptr[1] == '\x80' && ptr[2] == '\xaf';
}

static bool is_non_breaking_space(const char* ptr) {
  // https://www.fileformat.info/info/unicode/char/0a/index.htm
  return ptr[0] == '\xc2' && ptr[1] == '\xa0';
}
#endif // _LIBCPP_HAS_NO_WIDE_CHARACTERS

static bool checked_string_to_char_convert(char& dest, const char* ptr, locale_t __loc) {
  if (*ptr == '\0')
    return false;
  if (!ptr[1]) {
    dest = *ptr;
    return true;
  }

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  // First convert the MBS into a wide char then attempt to narrow it using
  // wctob_l.
  wchar_t wout;
  if (!checked_string_to_wchar_convert(wout, ptr, __loc))
    return false;
  int res;
  if ((res = __libcpp_wctob_l(wout, __loc)) != char_traits<char>::eof()) {
    dest = res;
    return true;
  }
  // FIXME: Work around specific multibyte sequences that we can reasonably
  // translate into a different single byte.
  switch (wout) {
  case L'\u202F': // narrow non-breaking space
  case L'\u00A0': // non-breaking space
    dest = ' ';
    return true;
  default:
    return false;
  }
#else  // _LIBCPP_HAS_NO_WIDE_CHARACTERS
  // FIXME: Work around specific multibyte sequences that we can reasonably
  // translate into a different single byte.
  if (is_narrow_non_breaking_space(ptr) || is_non_breaking_space(ptr)) {
    dest = ' ';
    return true;
  }

  return false;
#endif // _LIBCPP_HAS_NO_WIDE_CHARACTERS
  __libcpp_unreachable();
}

// numpunct<char> && numpunct<wchar_t>

constinit locale::id numpunct<char>::id;
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
constinit locale::id numpunct<wchar_t>::id;
#endif

numpunct<char>::numpunct(size_t refs) : locale::facet(refs), __decimal_point_('.'), __thousands_sep_(',') {}

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
numpunct<wchar_t>::numpunct(size_t refs) : locale::facet(refs), __decimal_point_(L'.'), __thousands_sep_(L',') {}
#endif

numpunct<char>::~numpunct() {}

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
numpunct<wchar_t>::~numpunct() {}
#endif

char numpunct< char >::do_decimal_point() const { return __decimal_point_; }
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
wchar_t numpunct<wchar_t>::do_decimal_point() const { return __decimal_point_; }
#endif

char numpunct< char >::do_thousands_sep() const { return __thousands_sep_; }
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
wchar_t numpunct<wchar_t>::do_thousands_sep() const { return __thousands_sep_; }
#endif

string numpunct< char >::do_grouping() const { return __grouping_; }
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
string numpunct<wchar_t>::do_grouping() const { return __grouping_; }
#endif

string numpunct< char >::do_truename() const { return "true"; }
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
wstring numpunct<wchar_t>::do_truename() const { return L"true"; }
#endif

string numpunct< char >::do_falsename() const { return "false"; }
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
wstring numpunct<wchar_t>::do_falsename() const { return L"false"; }
#endif

// numpunct_byname<char>

numpunct_byname<char>::numpunct_byname(const char* nm, size_t refs) : numpunct<char>(refs) { __init(nm); }

numpunct_byname<char>::numpunct_byname(const string& nm, size_t refs) : numpunct<char>(refs) { __init(nm.c_str()); }

numpunct_byname<char>::~numpunct_byname() {}

void numpunct_byname<char>::__init(const char* nm) {
  typedef numpunct<char> base;
  if (strcmp(nm, "C") != 0) {
    __libcpp_unique_locale loc(nm);
    if (!loc)
      __throw_runtime_error(
          ("numpunct_byname<char>::numpunct_byname"
           " failed to construct for " +
           string(nm))
              .c_str());

    lconv* lc = __libcpp_localeconv_l(loc.get());
    if (!checked_string_to_char_convert(__decimal_point_, lc->decimal_point, loc.get()))
      __decimal_point_ = base::do_decimal_point();
    if (!checked_string_to_char_convert(__thousands_sep_, lc->thousands_sep, loc.get()))
      __thousands_sep_ = base::do_thousands_sep();
    __grouping_ = lc->grouping;
    // localization for truename and falsename is not available
  }
}

// numpunct_byname<wchar_t>

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
numpunct_byname<wchar_t>::numpunct_byname(const char* nm, size_t refs) : numpunct<wchar_t>(refs) { __init(nm); }

numpunct_byname<wchar_t>::numpunct_byname(const string& nm, size_t refs) : numpunct<wchar_t>(refs) {
  __init(nm.c_str());
}

numpunct_byname<wchar_t>::~numpunct_byname() {}

void numpunct_byname<wchar_t>::__init(const char* nm) {
  if (strcmp(nm, "C") != 0) {
    __libcpp_unique_locale loc(nm);
    if (!loc)
      __throw_runtime_error(
          ("numpunct_byname<wchar_t>::numpunct_byname"
           " failed to construct for " +
           string(nm))
              .c_str());

    lconv* lc = __libcpp_localeconv_l(loc.get());
    checked_string_to_wchar_convert(__decimal_point_, lc->decimal_point, loc.get());
    checked_string_to_wchar_convert(__thousands_sep_, lc->thousands_sep, loc.get());
    __grouping_ = lc->grouping;
    // localization for truename and falsename is not available
  }
}
#endif // _LIBCPP_HAS_NO_WIDE_CHARACTERS

// num_get helpers

int __num_get_base::__get_base(ios_base& iob) {
  ios_base::fmtflags __basefield = iob.flags() & ios_base::basefield;
  if (__basefield == ios_base::oct)
    return 8;
  else if (__basefield == ios_base::hex)
    return 16;
  else if (__basefield == 0)
    return 0;
  return 10;
}

const char __num_get_base::__src[33] = "0123456789abcdefABCDEFxX+-pPiInN";

void __check_grouping(const string& __grouping, unsigned* __g, unsigned* __g_end, ios_base::iostate& __err) {
  //  if the grouping pattern is empty _or_ there are no grouping bits, then do nothing
  //  we always have at least a single entry in [__g, __g_end); the end of the input sequence
  if (__grouping.size() != 0 && __g_end - __g > 1) {
    reverse(__g, __g_end);
    const char* __ig = __grouping.data();
    const char* __eg = __ig + __grouping.size();
    for (unsigned* __r = __g; __r < __g_end - 1; ++__r) {
      if (0 < *__ig && *__ig < numeric_limits<char>::max()) {
        if (static_cast<unsigned>(*__ig) != *__r) {
          __err = ios_base::failbit;
          return;
        }
      }
      if (__eg - __ig > 1)
        ++__ig;
    }
    if (0 < *__ig && *__ig < numeric_limits<char>::max()) {
      if (static_cast<unsigned>(*__ig) < __g_end[-1] || __g_end[-1] == 0)
        __err = ios_base::failbit;
    }
  }
}

void __num_put_base::__format_int(char* __fmtp, const char* __len, bool __signd, ios_base::fmtflags __flags) {
  if ((__flags & ios_base::showpos) && (__flags & ios_base::basefield) != ios_base::oct &&
      (__flags & ios_base::basefield) != ios_base::hex && __signd)
    *__fmtp++ = '+';
  if (__flags & ios_base::showbase)
    *__fmtp++ = '#';
  while (*__len)
    *__fmtp++ = *__len++;
  if ((__flags & ios_base::basefield) == ios_base::oct)
    *__fmtp = 'o';
  else if ((__flags & ios_base::basefield) == ios_base::hex) {
    if (__flags & ios_base::uppercase)
      *__fmtp = 'X';
    else
      *__fmtp = 'x';
  } else if (__signd)
    *__fmtp = 'd';
  else
    *__fmtp = 'u';
}

bool __num_put_base::__format_float(char* __fmtp, const char* __len, ios_base::fmtflags __flags) {
  bool specify_precision = true;
  if (__flags & ios_base::showpos)
    *__fmtp++ = '+';
  if (__flags & ios_base::showpoint)
    *__fmtp++ = '#';
  ios_base::fmtflags floatfield = __flags & ios_base::floatfield;
  bool uppercase                = (__flags & ios_base::uppercase) != 0;
  if (floatfield == (ios_base::fixed | ios_base::scientific))
    specify_precision = false;
  else {
    *__fmtp++ = '.';
    *__fmtp++ = '*';
  }
  while (*__len)
    *__fmtp++ = *__len++;
  if (floatfield == ios_base::fixed) {
    if (uppercase)
      *__fmtp = 'F';
    else
      *__fmtp = 'f';
  } else if (floatfield == ios_base::scientific) {
    if (uppercase)
      *__fmtp = 'E';
    else
      *__fmtp = 'e';
  } else if (floatfield == (ios_base::fixed | ios_base::scientific)) {
    if (uppercase)
      *__fmtp = 'A';
    else
      *__fmtp = 'a';
  } else {
    if (uppercase)
      *__fmtp = 'G';
    else
      *__fmtp = 'g';
  }
  return specify_precision;
}

char* __num_put_base::__identify_padding(char* __nb, char* __ne, const ios_base& __iob) {
  switch (__iob.flags() & ios_base::adjustfield) {
  case ios_base::internal:
    if (__nb[0] == '-' || __nb[0] == '+')
      return __nb + 1;
    if (__ne - __nb >= 2 && __nb[0] == '0' && (__nb[1] == 'x' || __nb[1] == 'X'))
      return __nb + 2;
    break;
  case ios_base::left:
    return __ne;
  case ios_base::right:
  default:
    break;
  }
  return __nb;
}

// time_get

static string* init_weeks() {
  static string weeks[14];
  weeks[0]  = "Sunday";
  weeks[1]  = "Monday";
  weeks[2]  = "Tuesday";
  weeks[3]  = "Wednesday";
  weeks[4]  = "Thursday";
  weeks[5]  = "Friday";
  weeks[6]  = "Saturday";
  weeks[7]  = "Sun";
  weeks[8]  = "Mon";
  weeks[9]  = "Tue";
  weeks[10] = "Wed";
  weeks[11] = "Thu";
  weeks[12] = "Fri";
  weeks[13] = "Sat";
  return weeks;
}

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
static wstring* init_wweeks() {
  static wstring weeks[14];
  weeks[0]  = L"Sunday";
  weeks[1]  = L"Monday";
  weeks[2]  = L"Tuesday";
  weeks[3]  = L"Wednesday";
  weeks[4]  = L"Thursday";
  weeks[5]  = L"Friday";
  weeks[6]  = L"Saturday";
  weeks[7]  = L"Sun";
  weeks[8]  = L"Mon";
  weeks[9]  = L"Tue";
  weeks[10] = L"Wed";
  weeks[11] = L"Thu";
  weeks[12] = L"Fri";
  weeks[13] = L"Sat";
  return weeks;
}
#endif

template <>
const string* __time_get_c_storage<char>::__weeks() const {
  static const string* weeks = init_weeks();
  return weeks;
}

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
template <>
const wstring* __time_get_c_storage<wchar_t>::__weeks() const {
  static const wstring* weeks = init_wweeks();
  return weeks;
}
#endif

static string* init_months() {
  static string months[24];
  months[0]  = "January";
  months[1]  = "February";
  months[2]  = "March";
  months[3]  = "April";
  months[4]  = "May";
  months[5]  = "June";
  months[6]  = "July";
  months[7]  = "August";
  months[8]  = "September";
  months[9]  = "October";
  months[10] = "November";
  months[11] = "December";
  months[12] = "Jan";
  months[13] = "Feb";
  months[14] = "Mar";
  months[15] = "Apr";
  months[16] = "May";
  months[17] = "Jun";
  months[18] = "Jul";
  months[19] = "Aug";
  months[20] = "Sep";
  months[21] = "Oct";
  months[22] = "Nov";
  months[23] = "Dec";
  return months;
}

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
static wstring* init_wmonths() {
  static wstring months[24];
  months[0]  = L"January";
  months[1]  = L"February";
  months[2]  = L"March";
  months[3]  = L"April";
  months[4]  = L"May";
  months[5]  = L"June";
  months[6]  = L"July";
  months[7]  = L"August";
  months[8]  = L"September";
  months[9]  = L"October";
  months[10] = L"November";
  months[11] = L"December";
  months[12] = L"Jan";
  months[13] = L"Feb";
  months[14] = L"Mar";
  months[15] = L"Apr";
  months[16] = L"May";
  months[17] = L"Jun";
  months[18] = L"Jul";
  months[19] = L"Aug";
  months[20] = L"Sep";
  months[21] = L"Oct";
  months[22] = L"Nov";
  months[23] = L"Dec";
  return months;
}
#endif

template <>
const string* __time_get_c_storage<char>::__months() const {
  static const string* months = init_months();
  return months;
}

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
template <>
const wstring* __time_get_c_storage<wchar_t>::__months() const {
  static const wstring* months = init_wmonths();
  return months;
}
#endif

static string* init_am_pm() {
  static string am_pm[2];
  am_pm[0] = "AM";
  am_pm[1] = "PM";
  return am_pm;
}

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
static wstring* init_wam_pm() {
  static wstring am_pm[2];
  am_pm[0] = L"AM";
  am_pm[1] = L"PM";
  return am_pm;
}
#endif

template <>
const string* __time_get_c_storage<char>::__am_pm() const {
  static const string* am_pm = init_am_pm();
  return am_pm;
}

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
template <>
const wstring* __time_get_c_storage<wchar_t>::__am_pm() const {
  static const wstring* am_pm = init_wam_pm();
  return am_pm;
}
#endif

template <>
const string& __time_get_c_storage<char>::__x() const {
  static string s("%m/%d/%y");
  return s;
}

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
template <>
const wstring& __time_get_c_storage<wchar_t>::__x() const {
  static wstring s(L"%m/%d/%y");
  return s;
}
#endif

template <>
const string& __time_get_c_storage<char>::__X() const {
  static string s("%H:%M:%S");
  return s;
}

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
template <>
const wstring& __time_get_c_storage<wchar_t>::__X() const {
  static wstring s(L"%H:%M:%S");
  return s;
}
#endif

template <>
const string& __time_get_c_storage<char>::__c() const {
  static string s("%a %b %d %H:%M:%S %Y");
  return s;
}

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
template <>
const wstring& __time_get_c_storage<wchar_t>::__c() const {
  static wstring s(L"%a %b %d %H:%M:%S %Y");
  return s;
}
#endif

template <>
const string& __time_get_c_storage<char>::__r() const {
  static string s("%I:%M:%S %p");
  return s;
}

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
template <>
const wstring& __time_get_c_storage<wchar_t>::__r() const {
  static wstring s(L"%I:%M:%S %p");
  return s;
}
#endif

// time_get_byname

__time_get::__time_get(const char* nm) : __loc_(newlocale(LC_ALL_MASK, nm, 0)) {
  if (__loc_ == 0)
    __throw_runtime_error(("time_get_byname failed to construct for " + string(nm)).c_str());
}

__time_get::__time_get(const string& nm) : __loc_(newlocale(LC_ALL_MASK, nm.c_str(), 0)) {
  if (__loc_ == 0)
    __throw_runtime_error(("time_get_byname failed to construct for " + nm).c_str());
}

__time_get::~__time_get() { freelocale(__loc_); }

_LIBCPP_CLANG_DIAGNOSTIC_IGNORED("-Wmissing-field-initializers")

template <>
string __time_get_storage<char>::__analyze(char fmt, const ctype<char>& ct) {
  tm t       = {0};
  t.tm_sec   = 59;
  t.tm_min   = 55;
  t.tm_hour  = 23;
  t.tm_mday  = 31;
  t.tm_mon   = 11;
  t.tm_year  = 161;
  t.tm_wday  = 6;
  t.tm_yday  = 364;
  t.tm_isdst = -1;
  char buf[100];
  char f[3] = {0};
  f[0]      = '%';
  f[1]      = fmt;
  size_t n  = strftime_l(buf, countof(buf), f, &t, __loc_);
  char* bb  = buf;
  char* be  = buf + n;
  string result;
  while (bb != be) {
    if (ct.is(ctype_base::space, *bb)) {
      result.push_back(' ');
      for (++bb; bb != be && ct.is(ctype_base::space, *bb); ++bb)
        ;
      continue;
    }
    char* w               = bb;
    ios_base::iostate err = ios_base::goodbit;
    ptrdiff_t i           = __scan_keyword(w, be, this->__weeks_, this->__weeks_ + 14, ct, err, false) - this->__weeks_;
    if (i < 14) {
      result.push_back('%');
      if (i < 7)
        result.push_back('A');
      else
        result.push_back('a');
      bb = w;
      continue;
    }
    w = bb;
    i = __scan_keyword(w, be, this->__months_, this->__months_ + 24, ct, err, false) - this->__months_;
    if (i < 24) {
      result.push_back('%');
      if (i < 12)
        result.push_back('B');
      else
        result.push_back('b');
      if (fmt == 'x' && ct.is(ctype_base::digit, this->__months_[i][0]))
        result.back() = 'm';
      bb = w;
      continue;
    }
    if (this->__am_pm_[0].size() + this->__am_pm_[1].size() > 0) {
      w = bb;
      i = __scan_keyword(w, be, this->__am_pm_, this->__am_pm_ + 2, ct, err, false) - this->__am_pm_;
      if (i < 2) {
        result.push_back('%');
        result.push_back('p');
        bb = w;
        continue;
      }
    }
    w = bb;
    if (ct.is(ctype_base::digit, *bb)) {
      switch (__get_up_to_n_digits(bb, be, err, ct, 4)) {
      case 6:
        result.push_back('%');
        result.push_back('w');
        break;
      case 7:
        result.push_back('%');
        result.push_back('u');
        break;
      case 11:
        result.push_back('%');
        result.push_back('I');
        break;
      case 12:
        result.push_back('%');
        result.push_back('m');
        break;
      case 23:
        result.push_back('%');
        result.push_back('H');
        break;
      case 31:
        result.push_back('%');
        result.push_back('d');
        break;
      case 55:
        result.push_back('%');
        result.push_back('M');
        break;
      case 59:
        result.push_back('%');
        result.push_back('S');
        break;
      case 61:
        result.push_back('%');
        result.push_back('y');
        break;
      case 364:
        result.push_back('%');
        result.push_back('j');
        break;
      case 2061:
        result.push_back('%');
        result.push_back('Y');
        break;
      default:
        for (; w != bb; ++w)
          result.push_back(*w);
        break;
      }
      continue;
    }
    if (*bb == '%') {
      result.push_back('%');
      result.push_back('%');
      ++bb;
      continue;
    }
    result.push_back(*bb);
    ++bb;
  }
  return result;
}

_LIBCPP_CLANG_DIAGNOSTIC_IGNORED("-Wmissing-braces")

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
template <>
wstring __time_get_storage<wchar_t>::__analyze(char fmt, const ctype<wchar_t>& ct) {
  tm t       = {0};
  t.tm_sec   = 59;
  t.tm_min   = 55;
  t.tm_hour  = 23;
  t.tm_mday  = 31;
  t.tm_mon   = 11;
  t.tm_year  = 161;
  t.tm_wday  = 6;
  t.tm_yday  = 364;
  t.tm_isdst = -1;
  char buf[100];
  char f[3] = {0};
  f[0]      = '%';
  f[1]      = fmt;
  strftime_l(buf, countof(buf), f, &t, __loc_);
  wchar_t wbuf[100];
  wchar_t* wbb   = wbuf;
  mbstate_t mb   = {0};
  const char* bb = buf;
  size_t j       = __libcpp_mbsrtowcs_l(wbb, &bb, countof(wbuf), &mb, __loc_);
  if (j == size_t(-1))
    __throw_runtime_error("locale not supported");
  wchar_t* wbe = wbb + j;
  wstring result;
  while (wbb != wbe) {
    if (ct.is(ctype_base::space, *wbb)) {
      result.push_back(L' ');
      for (++wbb; wbb != wbe && ct.is(ctype_base::space, *wbb); ++wbb)
        ;
      continue;
    }
    wchar_t* w            = wbb;
    ios_base::iostate err = ios_base::goodbit;
    ptrdiff_t i = __scan_keyword(w, wbe, this->__weeks_, this->__weeks_ + 14, ct, err, false) - this->__weeks_;
    if (i < 14) {
      result.push_back(L'%');
      if (i < 7)
        result.push_back(L'A');
      else
        result.push_back(L'a');
      wbb = w;
      continue;
    }
    w = wbb;
    i = __scan_keyword(w, wbe, this->__months_, this->__months_ + 24, ct, err, false) - this->__months_;
    if (i < 24) {
      result.push_back(L'%');
      if (i < 12)
        result.push_back(L'B');
      else
        result.push_back(L'b');
      if (fmt == 'x' && ct.is(ctype_base::digit, this->__months_[i][0]))
        result.back() = L'm';
      wbb = w;
      continue;
    }
    if (this->__am_pm_[0].size() + this->__am_pm_[1].size() > 0) {
      w = wbb;
      i = __scan_keyword(w, wbe, this->__am_pm_, this->__am_pm_ + 2, ct, err, false) - this->__am_pm_;
      if (i < 2) {
        result.push_back(L'%');
        result.push_back(L'p');
        wbb = w;
        continue;
      }
    }
    w = wbb;
    if (ct.is(ctype_base::digit, *wbb)) {
      switch (__get_up_to_n_digits(wbb, wbe, err, ct, 4)) {
      case 6:
        result.push_back(L'%');
        result.push_back(L'w');
        break;
      case 7:
        result.push_back(L'%');
        result.push_back(L'u');
        break;
      case 11:
        result.push_back(L'%');
        result.push_back(L'I');
        break;
      case 12:
        result.push_back(L'%');
        result.push_back(L'm');
        break;
      case 23:
        result.push_back(L'%');
        result.push_back(L'H');
        break;
      case 31:
        result.push_back(L'%');
        result.push_back(L'd');
        break;
      case 55:
        result.push_back(L'%');
        result.push_back(L'M');
        break;
      case 59:
        result.push_back(L'%');
        result.push_back(L'S');
        break;
      case 61:
        result.push_back(L'%');
        result.push_back(L'y');
        break;
      case 364:
        result.push_back(L'%');
        result.push_back(L'j');
        break;
      case 2061:
        result.push_back(L'%');
        result.push_back(L'Y');
        break;
      default:
        for (; w != wbb; ++w)
          result.push_back(*w);
        break;
      }
      continue;
    }
    if (ct.narrow(*wbb, 0) == '%') {
      result.push_back(L'%');
      result.push_back(L'%');
      ++wbb;
      continue;
    }
    result.push_back(*wbb);
    ++wbb;
  }
  return result;
}
#endif // _LIBCPP_HAS_NO_WIDE_CHARACTERS

template <>
void __time_get_storage<char>::init(const ctype<char>& ct) {
  tm t = {0};
  char buf[100];
  // __weeks_
  for (int i = 0; i < 7; ++i) {
    t.tm_wday = i;
    strftime_l(buf, countof(buf), "%A", &t, __loc_);
    __weeks_[i] = buf;
    strftime_l(buf, countof(buf), "%a", &t, __loc_);
    __weeks_[i + 7] = buf;
  }
  // __months_
  for (int i = 0; i < 12; ++i) {
    t.tm_mon = i;
    strftime_l(buf, countof(buf), "%B", &t, __loc_);
    __months_[i] = buf;
    strftime_l(buf, countof(buf), "%b", &t, __loc_);
    __months_[i + 12] = buf;
  }
  // __am_pm_
  t.tm_hour = 1;
  strftime_l(buf, countof(buf), "%p", &t, __loc_);
  __am_pm_[0] = buf;
  t.tm_hour   = 13;
  strftime_l(buf, countof(buf), "%p", &t, __loc_);
  __am_pm_[1] = buf;
  __c_        = __analyze('c', ct);
  __r_        = __analyze('r', ct);
  __x_        = __analyze('x', ct);
  __X_        = __analyze('X', ct);
}

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
template <>
void __time_get_storage<wchar_t>::init(const ctype<wchar_t>& ct) {
  tm t = {0};
  char buf[100];
  wchar_t wbuf[100];
  wchar_t* wbe;
  mbstate_t mb = {0};
  // __weeks_
  for (int i = 0; i < 7; ++i) {
    t.tm_wday = i;
    strftime_l(buf, countof(buf), "%A", &t, __loc_);
    mb             = mbstate_t();
    const char* bb = buf;
    size_t j       = __libcpp_mbsrtowcs_l(wbuf, &bb, countof(wbuf), &mb, __loc_);
    if (j == size_t(-1) || j == 0)
      __throw_runtime_error("locale not supported");
    wbe = wbuf + j;
    __weeks_[i].assign(wbuf, wbe);
    strftime_l(buf, countof(buf), "%a", &t, __loc_);
    mb = mbstate_t();
    bb = buf;
    j  = __libcpp_mbsrtowcs_l(wbuf, &bb, countof(wbuf), &mb, __loc_);
    if (j == size_t(-1) || j == 0)
      __throw_runtime_error("locale not supported");
    wbe = wbuf + j;
    __weeks_[i + 7].assign(wbuf, wbe);
  }
  // __months_
  for (int i = 0; i < 12; ++i) {
    t.tm_mon = i;
    strftime_l(buf, countof(buf), "%B", &t, __loc_);
    mb             = mbstate_t();
    const char* bb = buf;
    size_t j       = __libcpp_mbsrtowcs_l(wbuf, &bb, countof(wbuf), &mb, __loc_);
    if (j == size_t(-1) || j == 0)
      __throw_runtime_error("locale not supported");
    wbe = wbuf + j;
    __months_[i].assign(wbuf, wbe);
    strftime_l(buf, countof(buf), "%b", &t, __loc_);
    mb = mbstate_t();
    bb = buf;
    j  = __libcpp_mbsrtowcs_l(wbuf, &bb, countof(wbuf), &mb, __loc_);
    if (j == size_t(-1) || j == 0)
      __throw_runtime_error("locale not supported");
    wbe = wbuf + j;
    __months_[i + 12].assign(wbuf, wbe);
  }
  // __am_pm_
  t.tm_hour = 1;
  strftime_l(buf, countof(buf), "%p", &t, __loc_);
  mb             = mbstate_t();
  const char* bb = buf;
  size_t j       = __libcpp_mbsrtowcs_l(wbuf, &bb, countof(wbuf), &mb, __loc_);
  if (j == size_t(-1))
    __throw_runtime_error("locale not supported");
  wbe = wbuf + j;
  __am_pm_[0].assign(wbuf, wbe);
  t.tm_hour = 13;
  strftime_l(buf, countof(buf), "%p", &t, __loc_);
  mb = mbstate_t();
  bb = buf;
  j  = __libcpp_mbsrtowcs_l(wbuf, &bb, countof(wbuf), &mb, __loc_);
  if (j == size_t(-1))
    __throw_runtime_error("locale not supported");
  wbe = wbuf + j;
  __am_pm_[1].assign(wbuf, wbe);
  __c_ = __analyze('c', ct);
  __r_ = __analyze('r', ct);
  __x_ = __analyze('x', ct);
  __X_ = __analyze('X', ct);
}
#endif // _LIBCPP_HAS_NO_WIDE_CHARACTERS

template <class CharT>
struct _LIBCPP_HIDDEN __time_get_temp : public ctype_byname<CharT> {
  explicit __time_get_temp(const char* nm) : ctype_byname<CharT>(nm, 1) {}
  explicit __time_get_temp(const string& nm) : ctype_byname<CharT>(nm, 1) {}
};

template <>
__time_get_storage<char>::__time_get_storage(const char* __nm) : __time_get(__nm) {
  const __time_get_temp<char> ct(__nm);
  init(ct);
}

template <>
__time_get_storage<char>::__time_get_storage(const string& __nm) : __time_get(__nm) {
  const __time_get_temp<char> ct(__nm);
  init(ct);
}

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
template <>
__time_get_storage<wchar_t>::__time_get_storage(const char* __nm) : __time_get(__nm) {
  const __time_get_temp<wchar_t> ct(__nm);
  init(ct);
}

template <>
__time_get_storage<wchar_t>::__time_get_storage(const string& __nm) : __time_get(__nm) {
  const __time_get_temp<wchar_t> ct(__nm);
  init(ct);
}
#endif // _LIBCPP_HAS_NO_WIDE_CHARACTERS

template <>
time_base::dateorder __time_get_storage<char>::__do_date_order() const {
  unsigned i;
  for (i = 0; i < __x_.size(); ++i)
    if (__x_[i] == '%')
      break;
  ++i;
  switch (__x_[i]) {
  case 'y':
  case 'Y':
    for (++i; i < __x_.size(); ++i)
      if (__x_[i] == '%')
        break;
    if (i == __x_.size())
      break;
    ++i;
    switch (__x_[i]) {
    case 'm':
      for (++i; i < __x_.size(); ++i)
        if (__x_[i] == '%')
          break;
      if (i == __x_.size())
        break;
      ++i;
      if (__x_[i] == 'd')
        return time_base::ymd;
      break;
    case 'd':
      for (++i; i < __x_.size(); ++i)
        if (__x_[i] == '%')
          break;
      if (i == __x_.size())
        break;
      ++i;
      if (__x_[i] == 'm')
        return time_base::ydm;
      break;
    }
    break;
  case 'm':
    for (++i; i < __x_.size(); ++i)
      if (__x_[i] == '%')
        break;
    if (i == __x_.size())
      break;
    ++i;
    if (__x_[i] == 'd') {
      for (++i; i < __x_.size(); ++i)
        if (__x_[i] == '%')
          break;
      if (i == __x_.size())
        break;
      ++i;
      if (__x_[i] == 'y' || __x_[i] == 'Y')
        return time_base::mdy;
      break;
    }
    break;
  case 'd':
    for (++i; i < __x_.size(); ++i)
      if (__x_[i] == '%')
        break;
    if (i == __x_.size())
      break;
    ++i;
    if (__x_[i] == 'm') {
      for (++i; i < __x_.size(); ++i)
        if (__x_[i] == '%')
          break;
      if (i == __x_.size())
        break;
      ++i;
      if (__x_[i] == 'y' || __x_[i] == 'Y')
        return time_base::dmy;
      break;
    }
    break;
  }
  return time_base::no_order;
}

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
template <>
time_base::dateorder __time_get_storage<wchar_t>::__do_date_order() const {
  unsigned i;
  for (i = 0; i < __x_.size(); ++i)
    if (__x_[i] == L'%')
      break;
  ++i;
  switch (__x_[i]) {
  case L'y':
  case L'Y':
    for (++i; i < __x_.size(); ++i)
      if (__x_[i] == L'%')
        break;
    if (i == __x_.size())
      break;
    ++i;
    switch (__x_[i]) {
    case L'm':
      for (++i; i < __x_.size(); ++i)
        if (__x_[i] == L'%')
          break;
      if (i == __x_.size())
        break;
      ++i;
      if (__x_[i] == L'd')
        return time_base::ymd;
      break;
    case L'd':
      for (++i; i < __x_.size(); ++i)
        if (__x_[i] == L'%')
          break;
      if (i == __x_.size())
        break;
      ++i;
      if (__x_[i] == L'm')
        return time_base::ydm;
      break;
    }
    break;
  case L'm':
    for (++i; i < __x_.size(); ++i)
      if (__x_[i] == L'%')
        break;
    if (i == __x_.size())
      break;
    ++i;
    if (__x_[i] == L'd') {
      for (++i; i < __x_.size(); ++i)
        if (__x_[i] == L'%')
          break;
      if (i == __x_.size())
        break;
      ++i;
      if (__x_[i] == L'y' || __x_[i] == L'Y')
        return time_base::mdy;
      break;
    }
    break;
  case L'd':
    for (++i; i < __x_.size(); ++i)
      if (__x_[i] == L'%')
        break;
    if (i == __x_.size())
      break;
    ++i;
    if (__x_[i] == L'm') {
      for (++i; i < __x_.size(); ++i)
        if (__x_[i] == L'%')
          break;
      if (i == __x_.size())
        break;
      ++i;
      if (__x_[i] == L'y' || __x_[i] == L'Y')
        return time_base::dmy;
      break;
    }
    break;
  }
  return time_base::no_order;
}
#endif // _LIBCPP_HAS_NO_WIDE_CHARACTERS

// time_put

__time_put::__time_put(const char* nm) : __loc_(newlocale(LC_ALL_MASK, nm, 0)) {
  if (__loc_ == 0)
    __throw_runtime_error(("time_put_byname failed to construct for " + string(nm)).c_str());
}

__time_put::__time_put(const string& nm) : __loc_(newlocale(LC_ALL_MASK, nm.c_str(), 0)) {
  if (__loc_ == 0)
    __throw_runtime_error(("time_put_byname failed to construct for " + nm).c_str());
}

__time_put::~__time_put() {
  if (__loc_ != _LIBCPP_GET_C_LOCALE)
    freelocale(__loc_);
}

void __time_put::__do_put(char* __nb, char*& __ne, const tm* __tm, char __fmt, char __mod) const {
  char fmt[] = {'%', __fmt, __mod, 0};
  if (__mod != 0)
    swap(fmt[1], fmt[2]);
  size_t n = strftime_l(__nb, countof(__nb, __ne), fmt, __tm, __loc_);
  __ne     = __nb + n;
}

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
void __time_put::__do_put(wchar_t* __wb, wchar_t*& __we, const tm* __tm, char __fmt, char __mod) const {
  char __nar[100];
  char* __ne = __nar + 100;
  __do_put(__nar, __ne, __tm, __fmt, __mod);
  mbstate_t mb     = {0};
  const char* __nb = __nar;
  size_t j         = __libcpp_mbsrtowcs_l(__wb, &__nb, countof(__wb, __we), &mb, __loc_);
  if (j == size_t(-1))
    __throw_runtime_error("locale not supported");
  __we = __wb + j;
}
#endif // _LIBCPP_HAS_NO_WIDE_CHARACTERS

// moneypunct_byname

template <class charT>
static void __init_pat(
    money_base::pattern& pat,
    basic_string<charT>& __curr_symbol_,
    bool intl,
    char cs_precedes,
    char sep_by_space,
    char sign_posn,
    charT space_char) {
  const char sign                = static_cast<char>(money_base::sign);
  const char space               = static_cast<char>(money_base::space);
  const char none                = static_cast<char>(money_base::none);
  const char symbol              = static_cast<char>(money_base::symbol);
  const char value               = static_cast<char>(money_base::value);
  const bool symbol_contains_sep = intl && __curr_symbol_.size() == 4;

  // Comments on case branches reflect 'C11 7.11.2.1 The localeconv
  // function'. "Space between sign and symbol or value" means that
  // if the sign is adjacent to the symbol, there's a space between
  // them, and otherwise there's a space between the sign and value.
  //
  // C11's localeconv specifies that the fourth character of an
  // international curr_symbol is used to separate the sign and
  // value when sep_by_space says to do so. C++ can't represent
  // that, so we just use a space.  When sep_by_space says to
  // separate the symbol and value-or-sign with a space, we rearrange the
  // curr_symbol to put its spacing character on the correct side of
  // the symbol.
  //
  // We also need to avoid adding an extra space between the sign
  // and value when the currency symbol is suppressed (by not
  // setting showbase).  We match glibc's strfmon by interpreting
  // sep_by_space==1 as "omit the space when the currency symbol is
  // absent".
  //
  // Users who want to get this right should use ICU instead.

  switch (cs_precedes) {
  case 0: // value before curr_symbol
    if (symbol_contains_sep) {
      // Move the separator to before the symbol, to place it
      // between the value and symbol.
      rotate(__curr_symbol_.begin(), __curr_symbol_.begin() + 3, __curr_symbol_.end());
    }
    switch (sign_posn) {
    case 0: // Parentheses surround the quantity and currency symbol.
      pat.field[0] = sign;
      pat.field[1] = value;
      pat.field[2] = none; // Any space appears in the symbol.
      pat.field[3] = symbol;
      switch (sep_by_space) {
      case 0: // No space separates the currency symbol and value.
              // This case may have changed between C99 and C11;
              // assume the currency symbol matches the intention.
      case 2: // Space between sign and currency or value.
        // The "sign" is two parentheses, so no space here either.
        return;
      case 1: // Space between currency-and-sign or currency and value.
        if (!symbol_contains_sep) {
          // We insert the space into the symbol instead of
          // setting pat.field[2]=space so that when
          // showbase is not set, the space goes away too.
          __curr_symbol_.insert(0, 1, space_char);
        }
        return;
      default:
        break;
      }
      break;
    case 1: // The sign string precedes the quantity and currency symbol.
      pat.field[0] = sign;
      pat.field[3] = symbol;
      switch (sep_by_space) {
      case 0: // No space separates the currency symbol and value.
        pat.field[1] = value;
        pat.field[2] = none;
        return;
      case 1: // Space between currency-and-sign or currency and value.
        pat.field[1] = value;
        pat.field[2] = none;
        if (!symbol_contains_sep) {
          // We insert the space into the symbol instead of
          // setting pat.field[2]=space so that when
          // showbase is not set, the space goes away too.
          __curr_symbol_.insert(0, 1, space_char);
        }
        return;
      case 2: // Space between sign and currency or value.
        pat.field[1] = space;
        pat.field[2] = value;
        if (symbol_contains_sep) {
          // Remove the separator from the symbol, since it
          // has already appeared after the sign.
          __curr_symbol_.erase(__curr_symbol_.begin());
        }
        return;
      default:
        break;
      }
      break;
    case 2: // The sign string succeeds the quantity and currency symbol.
      pat.field[0] = value;
      pat.field[3] = sign;
      switch (sep_by_space) {
      case 0: // No space separates the currency symbol and value.
        pat.field[1] = none;
        pat.field[2] = symbol;
        return;
      case 1: // Space between currency-and-sign or currency and value.
        if (!symbol_contains_sep) {
          // We insert the space into the symbol instead of
          // setting pat.field[1]=space so that when
          // showbase is not set, the space goes away too.
          __curr_symbol_.insert(0, 1, space_char);
        }
        pat.field[1] = none;
        pat.field[2] = symbol;
        return;
      case 2: // Space between sign and currency or value.
        pat.field[1] = symbol;
        pat.field[2] = space;
        if (symbol_contains_sep) {
          // Remove the separator from the symbol, since it
          // should not be removed if showbase is absent.
          __curr_symbol_.erase(__curr_symbol_.begin());
        }
        return;
      default:
        break;
      }
      break;
    case 3: // The sign string immediately precedes the currency symbol.
      pat.field[0] = value;
      pat.field[3] = symbol;
      switch (sep_by_space) {
      case 0: // No space separates the currency symbol and value.
        pat.field[1] = none;
        pat.field[2] = sign;
        return;
      case 1: // Space between currency-and-sign or currency and value.
        pat.field[1] = space;
        pat.field[2] = sign;
        if (symbol_contains_sep) {
          // Remove the separator from the symbol, since it
          // has already appeared before the sign.
          __curr_symbol_.erase(__curr_symbol_.begin());
        }
        return;
      case 2: // Space between sign and currency or value.
        pat.field[1] = sign;
        pat.field[2] = none;
        if (!symbol_contains_sep) {
          // We insert the space into the symbol instead of
          // setting pat.field[2]=space so that when
          // showbase is not set, the space goes away too.
          __curr_symbol_.insert(0, 1, space_char);
        }
        return;
      default:
        break;
      }
      break;
    case 4: // The sign string immediately succeeds the currency symbol.
      pat.field[0] = value;
      pat.field[3] = sign;
      switch (sep_by_space) {
      case 0: // No space separates the currency symbol and value.
        pat.field[1] = none;
        pat.field[2] = symbol;
        return;
      case 1: // Space between currency-and-sign or currency and value.
        pat.field[1] = none;
        pat.field[2] = symbol;
        if (!symbol_contains_sep) {
          // We insert the space into the symbol instead of
          // setting pat.field[1]=space so that when
          // showbase is not set, the space goes away too.
          __curr_symbol_.insert(0, 1, space_char);
        }
        return;
      case 2: // Space between sign and currency or value.
        pat.field[1] = symbol;
        pat.field[2] = space;
        if (symbol_contains_sep) {
          // Remove the separator from the symbol, since it
          // should not disappear when showbase is absent.
          __curr_symbol_.erase(__curr_symbol_.begin());
        }
        return;
      default:
        break;
      }
      break;
    default:
      break;
    }
    break;
  case 1: // curr_symbol before value
    switch (sign_posn) {
    case 0: // Parentheses surround the quantity and currency symbol.
      pat.field[0] = sign;
      pat.field[1] = symbol;
      pat.field[2] = none; // Any space appears in the symbol.
      pat.field[3] = value;
      switch (sep_by_space) {
      case 0: // No space separates the currency symbol and value.
              // This case may have changed between C99 and C11;
              // assume the currency symbol matches the intention.
      case 2: // Space between sign and currency or value.
        // The "sign" is two parentheses, so no space here either.
        return;
      case 1: // Space between currency-and-sign or currency and value.
        if (!symbol_contains_sep) {
          // We insert the space into the symbol instead of
          // setting pat.field[2]=space so that when
          // showbase is not set, the space goes away too.
          __curr_symbol_.insert(0, 1, space_char);
        }
        return;
      default:
        break;
      }
      break;
    case 1: // The sign string precedes the quantity and currency symbol.
      pat.field[0] = sign;
      pat.field[3] = value;
      switch (sep_by_space) {
      case 0: // No space separates the currency symbol and value.
        pat.field[1] = symbol;
        pat.field[2] = none;
        return;
      case 1: // Space between currency-and-sign or currency and value.
        pat.field[1] = symbol;
        pat.field[2] = none;
        if (!symbol_contains_sep) {
          // We insert the space into the symbol instead of
          // setting pat.field[2]=space so that when
          // showbase is not set, the space goes away too.
          __curr_symbol_.push_back(space_char);
        }
        return;
      case 2: // Space between sign and currency or value.
        pat.field[1] = space;
        pat.field[2] = symbol;
        if (symbol_contains_sep) {
          // Remove the separator from the symbol, since it
          // has already appeared after the sign.
          __curr_symbol_.pop_back();
        }
        return;
      default:
        break;
      }
      break;
    case 2: // The sign string succeeds the quantity and currency symbol.
      pat.field[0] = symbol;
      pat.field[3] = sign;
      switch (sep_by_space) {
      case 0: // No space separates the currency symbol and value.
        pat.field[1] = none;
        pat.field[2] = value;
        return;
      case 1: // Space between currency-and-sign or currency and value.
        pat.field[1] = none;
        pat.field[2] = value;
        if (!symbol_contains_sep) {
          // We insert the space into the symbol instead of
          // setting pat.field[1]=space so that when
          // showbase is not set, the space goes away too.
          __curr_symbol_.push_back(space_char);
        }
        return;
      case 2: // Space between sign and currency or value.
        pat.field[1] = value;
        pat.field[2] = space;
        if (symbol_contains_sep) {
          // Remove the separator from the symbol, since it
          // will appear before the sign.
          __curr_symbol_.pop_back();
        }
        return;
      default:
        break;
      }
      break;
    case 3: // The sign string immediately precedes the currency symbol.
      pat.field[0] = sign;
      pat.field[3] = value;
      switch (sep_by_space) {
      case 0: // No space separates the currency symbol and value.
        pat.field[1] = symbol;
        pat.field[2] = none;
        return;
      case 1: // Space between currency-and-sign or currency and value.
        pat.field[1] = symbol;
        pat.field[2] = none;
        if (!symbol_contains_sep) {
          // We insert the space into the symbol instead of
          // setting pat.field[2]=space so that when
          // showbase is not set, the space goes away too.
          __curr_symbol_.push_back(space_char);
        }
        return;
      case 2: // Space between sign and currency or value.
        pat.field[1] = space;
        pat.field[2] = symbol;
        if (symbol_contains_sep) {
          // Remove the separator from the symbol, since it
          // has already appeared after the sign.
          __curr_symbol_.pop_back();
        }
        return;
      default:
        break;
      }
      break;
    case 4: // The sign string immediately succeeds the currency symbol.
      pat.field[0] = symbol;
      pat.field[3] = value;
      switch (sep_by_space) {
      case 0: // No space separates the currency symbol and value.
        pat.field[1] = sign;
        pat.field[2] = none;
        return;
      case 1: // Space between currency-and-sign or currency and value.
        pat.field[1] = sign;
        pat.field[2] = space;
        if (symbol_contains_sep) {
          // Remove the separator from the symbol, since it
          // should not disappear when showbase is absent.
          __curr_symbol_.pop_back();
        }
        return;
      case 2: // Space between sign and currency or value.
        pat.field[1] = none;
        pat.field[2] = sign;
        if (!symbol_contains_sep) {
          // We insert the space into the symbol instead of
          // setting pat.field[1]=space so that when
          // showbase is not set, the space goes away too.
          __curr_symbol_.push_back(space_char);
        }
        return;
      default:
        break;
      }
      break;
    default:
      break;
    }
    break;
  default:
    break;
  }
  pat.field[0] = symbol;
  pat.field[1] = sign;
  pat.field[2] = none;
  pat.field[3] = value;
}

template <>
void moneypunct_byname<char, false>::init(const char* nm) {
  typedef moneypunct<char, false> base;
  __libcpp_unique_locale loc(nm);
  if (!loc)
    __throw_runtime_error(("moneypunct_byname failed to construct for " + string(nm)).c_str());

  lconv* lc = __libcpp_localeconv_l(loc.get());
  if (!checked_string_to_char_convert(__decimal_point_, lc->mon_decimal_point, loc.get()))
    __decimal_point_ = base::do_decimal_point();
  if (!checked_string_to_char_convert(__thousands_sep_, lc->mon_thousands_sep, loc.get()))
    __thousands_sep_ = base::do_thousands_sep();

  __grouping_    = lc->mon_grouping;
  __curr_symbol_ = lc->currency_symbol;
  if (lc->frac_digits != CHAR_MAX)
    __frac_digits_ = lc->frac_digits;
  else
    __frac_digits_ = base::do_frac_digits();
  if (lc->p_sign_posn == 0)
    __positive_sign_ = "()";
  else
    __positive_sign_ = lc->positive_sign;
  if (lc->n_sign_posn == 0)
    __negative_sign_ = "()";
  else
    __negative_sign_ = lc->negative_sign;
  // Assume the positive and negative formats will want spaces in
  // the same places in curr_symbol since there's no way to
  // represent anything else.
  string_type __dummy_curr_symbol = __curr_symbol_;
  __init_pat(__pos_format_, __dummy_curr_symbol, false, lc->p_cs_precedes, lc->p_sep_by_space, lc->p_sign_posn, ' ');
  __init_pat(__neg_format_, __curr_symbol_, false, lc->n_cs_precedes, lc->n_sep_by_space, lc->n_sign_posn, ' ');
}

template <>
void moneypunct_byname<char, true>::init(const char* nm) {
  typedef moneypunct<char, true> base;
  __libcpp_unique_locale loc(nm);
  if (!loc)
    __throw_runtime_error(("moneypunct_byname failed to construct for " + string(nm)).c_str());

  lconv* lc = __libcpp_localeconv_l(loc.get());
  if (!checked_string_to_char_convert(__decimal_point_, lc->mon_decimal_point, loc.get()))
    __decimal_point_ = base::do_decimal_point();
  if (!checked_string_to_char_convert(__thousands_sep_, lc->mon_thousands_sep, loc.get()))
    __thousands_sep_ = base::do_thousands_sep();
  __grouping_    = lc->mon_grouping;
  __curr_symbol_ = lc->int_curr_symbol;
  if (lc->int_frac_digits != CHAR_MAX)
    __frac_digits_ = lc->int_frac_digits;
  else
    __frac_digits_ = base::do_frac_digits();
#if defined(_LIBCPP_MSVCRT) || defined(__MINGW32__)
  if (lc->p_sign_posn == 0)
#else  // _LIBCPP_MSVCRT
  if (lc->int_p_sign_posn == 0)
#endif // !_LIBCPP_MSVCRT
    __positive_sign_ = "()";
  else
    __positive_sign_ = lc->positive_sign;
#if defined(_LIBCPP_MSVCRT) || defined(__MINGW32__)
  if (lc->n_sign_posn == 0)
#else  // _LIBCPP_MSVCRT
  if (lc->int_n_sign_posn == 0)
#endif // !_LIBCPP_MSVCRT
    __negative_sign_ = "()";
  else
    __negative_sign_ = lc->negative_sign;
  // Assume the positive and negative formats will want spaces in
  // the same places in curr_symbol since there's no way to
  // represent anything else.
  string_type __dummy_curr_symbol = __curr_symbol_;
#if defined(_LIBCPP_MSVCRT) || defined(__MINGW32__)
  __init_pat(__pos_format_, __dummy_curr_symbol, true, lc->p_cs_precedes, lc->p_sep_by_space, lc->p_sign_posn, ' ');
  __init_pat(__neg_format_, __curr_symbol_, true, lc->n_cs_precedes, lc->n_sep_by_space, lc->n_sign_posn, ' ');
#else  // _LIBCPP_MSVCRT
  __init_pat(
      __pos_format_,
      __dummy_curr_symbol,
      true,
      lc->int_p_cs_precedes,
      lc->int_p_sep_by_space,
      lc->int_p_sign_posn,
      ' ');
  __init_pat(
      __neg_format_, __curr_symbol_, true, lc->int_n_cs_precedes, lc->int_n_sep_by_space, lc->int_n_sign_posn, ' ');
#endif // !_LIBCPP_MSVCRT
}

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
template <>
void moneypunct_byname<wchar_t, false>::init(const char* nm) {
  typedef moneypunct<wchar_t, false> base;
  __libcpp_unique_locale loc(nm);
  if (!loc)
    __throw_runtime_error(("moneypunct_byname failed to construct for " + string(nm)).c_str());
  lconv* lc = __libcpp_localeconv_l(loc.get());
  if (!checked_string_to_wchar_convert(__decimal_point_, lc->mon_decimal_point, loc.get()))
    __decimal_point_ = base::do_decimal_point();
  if (!checked_string_to_wchar_convert(__thousands_sep_, lc->mon_thousands_sep, loc.get()))
    __thousands_sep_ = base::do_thousands_sep();
  __grouping_ = lc->mon_grouping;
  wchar_t wbuf[100];
  mbstate_t mb   = {0};
  const char* bb = lc->currency_symbol;
  size_t j       = __libcpp_mbsrtowcs_l(wbuf, &bb, countof(wbuf), &mb, loc.get());
  if (j == size_t(-1))
    __throw_runtime_error("locale not supported");
  wchar_t* wbe = wbuf + j;
  __curr_symbol_.assign(wbuf, wbe);
  if (lc->frac_digits != CHAR_MAX)
    __frac_digits_ = lc->frac_digits;
  else
    __frac_digits_ = base::do_frac_digits();
  if (lc->p_sign_posn == 0)
    __positive_sign_ = L"()";
  else {
    mb = mbstate_t();
    bb = lc->positive_sign;
    j  = __libcpp_mbsrtowcs_l(wbuf, &bb, countof(wbuf), &mb, loc.get());
    if (j == size_t(-1))
      __throw_runtime_error("locale not supported");
    wbe = wbuf + j;
    __positive_sign_.assign(wbuf, wbe);
  }
  if (lc->n_sign_posn == 0)
    __negative_sign_ = L"()";
  else {
    mb = mbstate_t();
    bb = lc->negative_sign;
    j  = __libcpp_mbsrtowcs_l(wbuf, &bb, countof(wbuf), &mb, loc.get());
    if (j == size_t(-1))
      __throw_runtime_error("locale not supported");
    wbe = wbuf + j;
    __negative_sign_.assign(wbuf, wbe);
  }
  // Assume the positive and negative formats will want spaces in
  // the same places in curr_symbol since there's no way to
  // represent anything else.
  string_type __dummy_curr_symbol = __curr_symbol_;
  __init_pat(__pos_format_, __dummy_curr_symbol, false, lc->p_cs_precedes, lc->p_sep_by_space, lc->p_sign_posn, L' ');
  __init_pat(__neg_format_, __curr_symbol_, false, lc->n_cs_precedes, lc->n_sep_by_space, lc->n_sign_posn, L' ');
}

template <>
void moneypunct_byname<wchar_t, true>::init(const char* nm) {
  typedef moneypunct<wchar_t, true> base;
  __libcpp_unique_locale loc(nm);
  if (!loc)
    __throw_runtime_error(("moneypunct_byname failed to construct for " + string(nm)).c_str());

  lconv* lc = __libcpp_localeconv_l(loc.get());
  if (!checked_string_to_wchar_convert(__decimal_point_, lc->mon_decimal_point, loc.get()))
    __decimal_point_ = base::do_decimal_point();
  if (!checked_string_to_wchar_convert(__thousands_sep_, lc->mon_thousands_sep, loc.get()))
    __thousands_sep_ = base::do_thousands_sep();
  __grouping_ = lc->mon_grouping;
  wchar_t wbuf[100];
  mbstate_t mb   = {0};
  const char* bb = lc->int_curr_symbol;
  size_t j       = __libcpp_mbsrtowcs_l(wbuf, &bb, countof(wbuf), &mb, loc.get());
  if (j == size_t(-1))
    __throw_runtime_error("locale not supported");
  wchar_t* wbe = wbuf + j;
  __curr_symbol_.assign(wbuf, wbe);
  if (lc->int_frac_digits != CHAR_MAX)
    __frac_digits_ = lc->int_frac_digits;
  else
    __frac_digits_ = base::do_frac_digits();
#  if defined(_LIBCPP_MSVCRT) || defined(__MINGW32__)
  if (lc->p_sign_posn == 0)
#  else  // _LIBCPP_MSVCRT
  if (lc->int_p_sign_posn == 0)
#  endif // !_LIBCPP_MSVCRT
    __positive_sign_ = L"()";
  else {
    mb = mbstate_t();
    bb = lc->positive_sign;
    j  = __libcpp_mbsrtowcs_l(wbuf, &bb, countof(wbuf), &mb, loc.get());
    if (j == size_t(-1))
      __throw_runtime_error("locale not supported");
    wbe = wbuf + j;
    __positive_sign_.assign(wbuf, wbe);
  }
#  if defined(_LIBCPP_MSVCRT) || defined(__MINGW32__)
  if (lc->n_sign_posn == 0)
#  else  // _LIBCPP_MSVCRT
  if (lc->int_n_sign_posn == 0)
#  endif // !_LIBCPP_MSVCRT
    __negative_sign_ = L"()";
  else {
    mb = mbstate_t();
    bb = lc->negative_sign;
    j  = __libcpp_mbsrtowcs_l(wbuf, &bb, countof(wbuf), &mb, loc.get());
    if (j == size_t(-1))
      __throw_runtime_error("locale not supported");
    wbe = wbuf + j;
    __negative_sign_.assign(wbuf, wbe);
  }
  // Assume the positive and negative formats will want spaces in
  // the same places in curr_symbol since there's no way to
  // represent anything else.
  string_type __dummy_curr_symbol = __curr_symbol_;
#  if defined(_LIBCPP_MSVCRT) || defined(__MINGW32__)
  __init_pat(__pos_format_, __dummy_curr_symbol, true, lc->p_cs_precedes, lc->p_sep_by_space, lc->p_sign_posn, L' ');
  __init_pat(__neg_format_, __curr_symbol_, true, lc->n_cs_precedes, lc->n_sep_by_space, lc->n_sign_posn, L' ');
#  else  // _LIBCPP_MSVCRT
  __init_pat(
      __pos_format_,
      __dummy_curr_symbol,
      true,
      lc->int_p_cs_precedes,
      lc->int_p_sep_by_space,
      lc->int_p_sign_posn,
      L' ');
  __init_pat(
      __neg_format_, __curr_symbol_, true, lc->int_n_cs_precedes, lc->int_n_sep_by_space, lc->int_n_sign_posn, L' ');
#  endif // !_LIBCPP_MSVCRT
}
#endif // _LIBCPP_HAS_NO_WIDE_CHARACTERS

void __do_nothing(void*) {}

template class _LIBCPP_CLASS_TEMPLATE_INSTANTIATION_VIS collate<char>;
_LIBCPP_IF_WIDE_CHARACTERS(template class _LIBCPP_CLASS_TEMPLATE_INSTANTIATION_VIS collate<wchar_t>;)

template class _LIBCPP_CLASS_TEMPLATE_INSTANTIATION_VIS num_get<char>;
_LIBCPP_IF_WIDE_CHARACTERS(template class _LIBCPP_CLASS_TEMPLATE_INSTANTIATION_VIS num_get<wchar_t>;)

template struct _LIBCPP_CLASS_TEMPLATE_INSTANTIATION_VIS __num_get<char>;
_LIBCPP_IF_WIDE_CHARACTERS(template struct _LIBCPP_CLASS_TEMPLATE_INSTANTIATION_VIS __num_get<wchar_t>;)

template class _LIBCPP_CLASS_TEMPLATE_INSTANTIATION_VIS num_put<char>;
_LIBCPP_IF_WIDE_CHARACTERS(template class _LIBCPP_CLASS_TEMPLATE_INSTANTIATION_VIS num_put<wchar_t>;)

template struct _LIBCPP_CLASS_TEMPLATE_INSTANTIATION_VIS __num_put<char>;
_LIBCPP_IF_WIDE_CHARACTERS(template struct _LIBCPP_CLASS_TEMPLATE_INSTANTIATION_VIS __num_put<wchar_t>;)

template class _LIBCPP_CLASS_TEMPLATE_INSTANTIATION_VIS time_get<char>;
_LIBCPP_IF_WIDE_CHARACTERS(template class _LIBCPP_CLASS_TEMPLATE_INSTANTIATION_VIS time_get<wchar_t>;)

template class _LIBCPP_CLASS_TEMPLATE_INSTANTIATION_VIS time_get_byname<char>;
_LIBCPP_IF_WIDE_CHARACTERS(template class _LIBCPP_CLASS_TEMPLATE_INSTANTIATION_VIS time_get_byname<wchar_t>;)

template class _LIBCPP_CLASS_TEMPLATE_INSTANTIATION_VIS time_put<char>;
_LIBCPP_IF_WIDE_CHARACTERS(template class _LIBCPP_CLASS_TEMPLATE_INSTANTIATION_VIS time_put<wchar_t>;)

template class _LIBCPP_CLASS_TEMPLATE_INSTANTIATION_VIS time_put_byname<char>;
_LIBCPP_IF_WIDE_CHARACTERS(template class _LIBCPP_CLASS_TEMPLATE_INSTANTIATION_VIS time_put_byname<wchar_t>;)

template class _LIBCPP_CLASS_TEMPLATE_INSTANTIATION_VIS moneypunct<char, false>;
template class _LIBCPP_CLASS_TEMPLATE_INSTANTIATION_VIS moneypunct<char, true>;
_LIBCPP_IF_WIDE_CHARACTERS(template class _LIBCPP_CLASS_TEMPLATE_INSTANTIATION_VIS moneypunct<wchar_t, false>;)
_LIBCPP_IF_WIDE_CHARACTERS(template class _LIBCPP_CLASS_TEMPLATE_INSTANTIATION_VIS moneypunct<wchar_t, true>;)

template class _LIBCPP_CLASS_TEMPLATE_INSTANTIATION_VIS moneypunct_byname<char, false>;
template class _LIBCPP_CLASS_TEMPLATE_INSTANTIATION_VIS moneypunct_byname<char, true>;
_LIBCPP_IF_WIDE_CHARACTERS(template class _LIBCPP_CLASS_TEMPLATE_INSTANTIATION_VIS moneypunct_byname<wchar_t, false>;)
_LIBCPP_IF_WIDE_CHARACTERS(template class _LIBCPP_CLASS_TEMPLATE_INSTANTIATION_VIS moneypunct_byname<wchar_t, true>;)

template class _LIBCPP_CLASS_TEMPLATE_INSTANTIATION_VIS money_get<char>;
_LIBCPP_IF_WIDE_CHARACTERS(template class _LIBCPP_CLASS_TEMPLATE_INSTANTIATION_VIS money_get<wchar_t>;)

template class _LIBCPP_CLASS_TEMPLATE_INSTANTIATION_VIS __money_get<char>;
_LIBCPP_IF_WIDE_CHARACTERS(template class _LIBCPP_CLASS_TEMPLATE_INSTANTIATION_VIS __money_get<wchar_t>;)

template class _LIBCPP_CLASS_TEMPLATE_INSTANTIATION_VIS money_put<char>;
_LIBCPP_IF_WIDE_CHARACTERS(template class _LIBCPP_CLASS_TEMPLATE_INSTANTIATION_VIS money_put<wchar_t>;)

template class _LIBCPP_CLASS_TEMPLATE_INSTANTIATION_VIS __money_put<char>;
_LIBCPP_IF_WIDE_CHARACTERS(template class _LIBCPP_CLASS_TEMPLATE_INSTANTIATION_VIS __money_put<wchar_t>;)

template class _LIBCPP_CLASS_TEMPLATE_INSTANTIATION_VIS messages<char>;
_LIBCPP_IF_WIDE_CHARACTERS(template class _LIBCPP_CLASS_TEMPLATE_INSTANTIATION_VIS messages<wchar_t>;)

template class _LIBCPP_CLASS_TEMPLATE_INSTANTIATION_VIS messages_byname<char>;
_LIBCPP_IF_WIDE_CHARACTERS(template class _LIBCPP_CLASS_TEMPLATE_INSTANTIATION_VIS messages_byname<wchar_t>;)

template class _LIBCPP_CLASS_TEMPLATE_INSTANTIATION_VIS codecvt_byname<char, char, mbstate_t>;
_LIBCPP_IF_WIDE_CHARACTERS(
    template class _LIBCPP_CLASS_TEMPLATE_INSTANTIATION_VIS codecvt_byname<wchar_t, char, mbstate_t>;)
template class _LIBCPP_DEPRECATED_IN_CXX20 _LIBCPP_CLASS_TEMPLATE_INSTANTIATION_VIS
    codecvt_byname<char16_t, char, mbstate_t>;
template class _LIBCPP_DEPRECATED_IN_CXX20 _LIBCPP_CLASS_TEMPLATE_INSTANTIATION_VIS
    codecvt_byname<char32_t, char, mbstate_t>;
#ifndef _LIBCPP_HAS_NO_CHAR8_T
template class _LIBCPP_CLASS_TEMPLATE_INSTANTIATION_VIS codecvt_byname<char16_t, char8_t, mbstate_t>;
template class _LIBCPP_CLASS_TEMPLATE_INSTANTIATION_VIS codecvt_byname<char32_t, char8_t, mbstate_t>;
#endif

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS
