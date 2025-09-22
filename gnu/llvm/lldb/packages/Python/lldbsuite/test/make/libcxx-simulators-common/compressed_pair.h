#ifndef STD_LLDB_COMPRESSED_PAIR_H
#define STD_LLDB_COMPRESSED_PAIR_H

#include <type_traits>
#include <utility> // for std::forward

namespace std {
namespace __lldb {

// Post-c88580c layout
struct __value_init_tag {};
struct __default_init_tag {};

template <class _Tp, int _Idx,
          bool _CanBeEmptyBase =
              std::is_empty<_Tp>::value && !std::is_final<_Tp>::value>
struct __compressed_pair_elem {
  explicit __compressed_pair_elem(__default_init_tag) {}
  explicit __compressed_pair_elem(__value_init_tag) : __value_() {}

  explicit __compressed_pair_elem(_Tp __t) : __value_(__t) {}

  _Tp &__get() { return __value_; }

private:
  _Tp __value_;
};

template <class _Tp, int _Idx>
struct __compressed_pair_elem<_Tp, _Idx, true> : private _Tp {
  explicit __compressed_pair_elem(_Tp __t) : _Tp(__t) {}
  explicit __compressed_pair_elem(__default_init_tag) {}
  explicit __compressed_pair_elem(__value_init_tag) : _Tp() {}

  _Tp &__get() { return *this; }
};

template <class _T1, class _T2>
class __compressed_pair : private __compressed_pair_elem<_T1, 0>,
                          private __compressed_pair_elem<_T2, 1> {
public:
  using _Base1 = __compressed_pair_elem<_T1, 0>;
  using _Base2 = __compressed_pair_elem<_T2, 1>;

  explicit __compressed_pair(_T1 __t1, _T2 __t2) : _Base1(__t1), _Base2(__t2) {}
  explicit __compressed_pair()
      : _Base1(__value_init_tag()), _Base2(__value_init_tag()) {}

  template <class _U1, class _U2>
  explicit __compressed_pair(_U1 &&__t1, _U2 &&__t2)
      : _Base1(std::forward<_U1>(__t1)), _Base2(std::forward<_U2>(__t2)) {}

  _T1 &first() { return static_cast<_Base1 &>(*this).__get(); }
};
} // namespace __lldb
} // namespace std

#endif // _H
