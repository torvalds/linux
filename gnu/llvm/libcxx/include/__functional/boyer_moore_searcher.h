//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___FUNCTIONAL_BOYER_MOORE_SEARCHER_H
#define _LIBCPP___FUNCTIONAL_BOYER_MOORE_SEARCHER_H

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#include <__algorithm/fill_n.h>
#include <__config>
#include <__functional/hash.h>
#include <__functional/operations.h>
#include <__iterator/distance.h>
#include <__iterator/iterator_traits.h>
#include <__memory/shared_ptr.h>
#include <__type_traits/make_unsigned.h>
#include <__utility/pair.h>
#include <array>
#include <unordered_map>
#include <vector>

#if _LIBCPP_STD_VER >= 17

_LIBCPP_PUSH_MACROS
#  include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Key, class _Value, class _Hash, class _BinaryPredicate, bool /*useArray*/>
class _BMSkipTable;

// General case for BM data searching; use a map
template <class _Key, class _Value, class _Hash, class _BinaryPredicate>
class _BMSkipTable<_Key, _Value, _Hash, _BinaryPredicate, false> {
private:
  using value_type = _Value;
  using key_type   = _Key;

  const value_type __default_value_;
  unordered_map<_Key, _Value, _Hash, _BinaryPredicate> __table_;

public:
  _LIBCPP_HIDE_FROM_ABI explicit _BMSkipTable(
      size_t __sz, value_type __default_value, _Hash __hash, _BinaryPredicate __pred)
      : __default_value_(__default_value), __table_(__sz, __hash, __pred) {}

  _LIBCPP_HIDE_FROM_ABI void insert(const key_type& __key, value_type __val) { __table_[__key] = __val; }

  _LIBCPP_HIDE_FROM_ABI value_type operator[](const key_type& __key) const {
    auto __it = __table_.find(__key);
    return __it == __table_.end() ? __default_value_ : __it->second;
  }
};

// Special case small numeric values; use an array
template <class _Key, class _Value, class _Hash, class _BinaryPredicate>
class _BMSkipTable<_Key, _Value, _Hash, _BinaryPredicate, true> {
private:
  using value_type = _Value;
  using key_type   = _Key;

  using unsigned_key_type = make_unsigned_t<key_type>;
  std::array<value_type, 256> __table_;
  static_assert(numeric_limits<unsigned_key_type>::max() < 256);

public:
  _LIBCPP_HIDE_FROM_ABI explicit _BMSkipTable(size_t, value_type __default_value, _Hash, _BinaryPredicate) {
    std::fill_n(__table_.data(), __table_.size(), __default_value);
  }

  _LIBCPP_HIDE_FROM_ABI void insert(key_type __key, value_type __val) {
    __table_[static_cast<unsigned_key_type>(__key)] = __val;
  }

  _LIBCPP_HIDE_FROM_ABI value_type operator[](key_type __key) const {
    return __table_[static_cast<unsigned_key_type>(__key)];
  }
};

template <class _RandomAccessIterator1,
          class _Hash            = hash<typename iterator_traits<_RandomAccessIterator1>::value_type>,
          class _BinaryPredicate = equal_to<>>
class _LIBCPP_TEMPLATE_VIS boyer_moore_searcher {
private:
  using difference_type = typename std::iterator_traits<_RandomAccessIterator1>::difference_type;
  using value_type      = typename std::iterator_traits<_RandomAccessIterator1>::value_type;
  using __skip_table_type =
      _BMSkipTable<value_type,
                   difference_type,
                   _Hash,
                   _BinaryPredicate,
                   is_integral_v<value_type> && sizeof(value_type) == 1 && is_same_v<_Hash, hash<value_type>> &&
                       is_same_v<_BinaryPredicate, equal_to<>>>;

public:
  _LIBCPP_HIDE_FROM_ABI boyer_moore_searcher(
      _RandomAccessIterator1 __first,
      _RandomAccessIterator1 __last,
      _Hash __hash            = _Hash(),
      _BinaryPredicate __pred = _BinaryPredicate())
      : __first_(__first),
        __last_(__last),
        __pred_(__pred),
        __pattern_length_(__last - __first),
        __skip_table_(std::make_shared<__skip_table_type>(__pattern_length_, -1, __hash, __pred_)),
        __suffix_(std::__allocate_shared_unbounded_array<difference_type[]>(
            allocator<difference_type>(), __pattern_length_ + 1)) {
    difference_type __i = 0;
    while (__first != __last) {
      __skip_table_->insert(*__first, __i);
      ++__first;
      ++__i;
    }
    __build_suffix_table(__first_, __last_, __pred_);
  }

  template <class _RandomAccessIterator2>
  _LIBCPP_HIDE_FROM_ABI pair<_RandomAccessIterator2, _RandomAccessIterator2>
  operator()(_RandomAccessIterator2 __first, _RandomAccessIterator2 __last) const {
    static_assert(__is_same_uncvref<typename iterator_traits<_RandomAccessIterator1>::value_type,
                                    typename iterator_traits<_RandomAccessIterator2>::value_type>::value,
                  "Corpus and Pattern iterators must point to the same type");
    if (__first == __last)
      return std::make_pair(__last, __last);
    if (__first_ == __last_)
      return std::make_pair(__first, __first);

    if (__pattern_length_ > (__last - __first))
      return std::make_pair(__last, __last);
    return __search(__first, __last);
  }

private:
  _RandomAccessIterator1 __first_;
  _RandomAccessIterator1 __last_;
  _BinaryPredicate __pred_;
  difference_type __pattern_length_;
  shared_ptr<__skip_table_type> __skip_table_;
  shared_ptr<difference_type[]> __suffix_;

  template <class _RandomAccessIterator2>
  _LIBCPP_HIDE_FROM_ABI pair<_RandomAccessIterator2, _RandomAccessIterator2>
  __search(_RandomAccessIterator2 __f, _RandomAccessIterator2 __l) const {
    _RandomAccessIterator2 __current      = __f;
    const _RandomAccessIterator2 __last   = __l - __pattern_length_;
    const __skip_table_type& __skip_table = *__skip_table_;

    while (__current <= __last) {
      difference_type __j = __pattern_length_;
      while (__pred_(__first_[__j - 1], __current[__j - 1])) {
        --__j;
        if (__j == 0)
          return std::make_pair(__current, __current + __pattern_length_);
      }

      difference_type __k = __skip_table[__current[__j - 1]];
      difference_type __m = __j - __k - 1;
      if (__k < __j && __m > __suffix_[__j])
        __current += __m;
      else
        __current += __suffix_[__j];
    }
    return std::make_pair(__l, __l);
  }

  template <class _Iterator, class _Container>
  _LIBCPP_HIDE_FROM_ABI void
  __compute_bm_prefix(_Iterator __first, _Iterator __last, _BinaryPredicate __pred, _Container& __prefix) {
    const size_t __count = __last - __first;

    __prefix[0] = 0;
    size_t __k  = 0;

    for (size_t __i = 1; __i != __count; ++__i) {
      while (__k > 0 && !__pred(__first[__k], __first[__i]))
        __k = __prefix[__k - 1];

      if (__pred(__first[__k], __first[__i]))
        ++__k;
      __prefix[__i] = __k;
    }
  }

  _LIBCPP_HIDE_FROM_ABI void
  __build_suffix_table(_RandomAccessIterator1 __first, _RandomAccessIterator1 __last, _BinaryPredicate __pred) {
    const size_t __count = __last - __first;

    if (__count == 0)
      return;

    vector<difference_type> __scratch(__count);

    __compute_bm_prefix(__first, __last, __pred, __scratch);
    for (size_t __i = 0; __i <= __count; ++__i)
      __suffix_[__i] = __count - __scratch[__count - 1];

    using _ReverseIter = reverse_iterator<_RandomAccessIterator1>;
    __compute_bm_prefix(_ReverseIter(__last), _ReverseIter(__first), __pred, __scratch);

    for (size_t __i = 0; __i != __count; ++__i) {
      const size_t __j          = __count - __scratch[__i];
      const difference_type __k = __i - __scratch[__i] + 1;

      if (__suffix_[__j] > __k)
        __suffix_[__j] = __k;
    }
  }
};
_LIBCPP_CTAD_SUPPORTED_FOR_TYPE(boyer_moore_searcher);

template <class _RandomAccessIterator1,
          class _Hash            = hash<typename iterator_traits<_RandomAccessIterator1>::value_type>,
          class _BinaryPredicate = equal_to<>>
class _LIBCPP_TEMPLATE_VIS boyer_moore_horspool_searcher {
private:
  using difference_type = typename iterator_traits<_RandomAccessIterator1>::difference_type;
  using value_type      = typename iterator_traits<_RandomAccessIterator1>::value_type;
  using __skip_table_type =
      _BMSkipTable<value_type,
                   difference_type,
                   _Hash,
                   _BinaryPredicate,
                   is_integral_v<value_type> && sizeof(value_type) == 1 && is_same_v<_Hash, hash<value_type>> &&
                       is_same_v<_BinaryPredicate, equal_to<>>>;

public:
  _LIBCPP_HIDE_FROM_ABI boyer_moore_horspool_searcher(
      _RandomAccessIterator1 __first,
      _RandomAccessIterator1 __last,
      _Hash __hash            = _Hash(),
      _BinaryPredicate __pred = _BinaryPredicate())
      : __first_(__first),
        __last_(__last),
        __pred_(__pred),
        __pattern_length_(__last - __first),
        __skip_table_(std::make_shared<__skip_table_type>(__pattern_length_, __pattern_length_, __hash, __pred_)) {
    if (__first == __last)
      return;
    --__last;
    difference_type __i = 0;
    while (__first != __last) {
      __skip_table_->insert(*__first, __pattern_length_ - 1 - __i);
      ++__first;
      ++__i;
    }
  }

  template <class _RandomAccessIterator2>
  _LIBCPP_HIDE_FROM_ABI pair<_RandomAccessIterator2, _RandomAccessIterator2>
  operator()(_RandomAccessIterator2 __first, _RandomAccessIterator2 __last) const {
    static_assert(__is_same_uncvref<typename std::iterator_traits<_RandomAccessIterator1>::value_type,
                                    typename std::iterator_traits<_RandomAccessIterator2>::value_type>::value,
                  "Corpus and Pattern iterators must point to the same type");
    if (__first == __last)
      return std::make_pair(__last, __last);
    if (__first_ == __last_)
      return std::make_pair(__first, __first);

    if (__pattern_length_ > __last - __first)
      return std::make_pair(__last, __last);

    return __search(__first, __last);
  }

private:
  _RandomAccessIterator1 __first_;
  _RandomAccessIterator1 __last_;
  _BinaryPredicate __pred_;
  difference_type __pattern_length_;
  shared_ptr<__skip_table_type> __skip_table_;

  template <class _RandomAccessIterator2>
  _LIBCPP_HIDE_FROM_ABI pair<_RandomAccessIterator2, _RandomAccessIterator2>
  __search(_RandomAccessIterator2 __f, _RandomAccessIterator2 __l) const {
    _RandomAccessIterator2 __current      = __f;
    const _RandomAccessIterator2 __last   = __l - __pattern_length_;
    const __skip_table_type& __skip_table = *__skip_table_;

    while (__current <= __last) {
      difference_type __j = __pattern_length_;
      while (__pred_(__first_[__j - 1], __current[__j - 1])) {
        --__j;
        if (__j == 0)
          return std::make_pair(__current, __current + __pattern_length_);
      }
      __current += __skip_table[__current[__pattern_length_ - 1]];
    }
    return std::make_pair(__l, __l);
  }
};
_LIBCPP_CTAD_SUPPORTED_FOR_TYPE(boyer_moore_horspool_searcher);

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP_STD_VER >= 17

#endif // _LIBCPP___FUNCTIONAL_BOYER_MOORE_SEARCHER_H
