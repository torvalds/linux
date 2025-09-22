// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// For information see https://libcxx.llvm.org/DesignDocs/TimeZone.html

#ifndef _LIBCPP___CHRONO_ZONED_TIME_H
#define _LIBCPP___CHRONO_ZONED_TIME_H

#include <version>
// Enable the contents of the header only when libc++ was built with experimental features enabled.
#if !defined(_LIBCPP_HAS_NO_EXPERIMENTAL_TZDB)

#  include <__chrono/calendar.h>
#  include <__chrono/duration.h>
#  include <__chrono/sys_info.h>
#  include <__chrono/system_clock.h>
#  include <__chrono/time_zone.h>
#  include <__chrono/tzdb_list.h>
#  include <__config>
#  include <__fwd/string_view.h>
#  include <__type_traits/common_type.h>
#  include <__type_traits/conditional.h>
#  include <__type_traits/remove_cvref.h>
#  include <__utility/move.h>

#  if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#    pragma GCC system_header
#  endif

_LIBCPP_PUSH_MACROS
#  include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#  if _LIBCPP_STD_VER >= 20 && !defined(_LIBCPP_HAS_NO_TIME_ZONE_DATABASE) && !defined(_LIBCPP_HAS_NO_FILESYSTEM) &&   \
      !defined(_LIBCPP_HAS_NO_LOCALIZATION)

namespace chrono {

template <class>
struct zoned_traits {};

template <>
struct zoned_traits<const time_zone*> {
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI static const time_zone* default_zone() { return chrono::locate_zone("UTC"); }
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI static const time_zone* locate_zone(string_view __name) {
    return chrono::locate_zone(__name);
  }
};

template <class _Duration, class _TimeZonePtr = const time_zone*>
class zoned_time {
  // [time.zone.zonedtime.ctor]/2
  static_assert(__is_duration<_Duration>::value,
                "the program is ill-formed since _Duration is not a specialization of std::chrono::duration");

  // The wording uses the constraints like
  //   constructible_from<zoned_time, decltype(__traits::locate_zone(string_view{}))>
  // Using these constraints in the code causes the compiler to give an
  // error that the constraint depends on itself. To avoid that issue use
  // the fact it is possible to create this object from a _TimeZonePtr.
  using __traits = zoned_traits<_TimeZonePtr>;

public:
  using duration = common_type_t<_Duration, seconds>;

  _LIBCPP_HIDE_FROM_ABI zoned_time()
    requires requires { __traits::default_zone(); }
      : __zone_{__traits::default_zone()}, __tp_{} {}

  _LIBCPP_HIDE_FROM_ABI zoned_time(const zoned_time&)            = default;
  _LIBCPP_HIDE_FROM_ABI zoned_time& operator=(const zoned_time&) = default;

  _LIBCPP_HIDE_FROM_ABI zoned_time(const sys_time<_Duration>& __tp)
    requires requires { __traits::default_zone(); }
      : __zone_{__traits::default_zone()}, __tp_{__tp} {}

  _LIBCPP_HIDE_FROM_ABI explicit zoned_time(_TimeZonePtr __zone) : __zone_{std::move(__zone)}, __tp_{} {}

  _LIBCPP_HIDE_FROM_ABI explicit zoned_time(string_view __name)
    requires(requires { __traits::locate_zone(string_view{}); } &&
             constructible_from<_TimeZonePtr, decltype(__traits::locate_zone(string_view{}))>)
      : __zone_{__traits::locate_zone(__name)}, __tp_{} {}

  template <class _Duration2>
  _LIBCPP_HIDE_FROM_ABI zoned_time(const zoned_time<_Duration2, _TimeZonePtr>& __zt)
    requires is_convertible_v<sys_time<_Duration2>, sys_time<_Duration>>
      : __zone_{__zt.get_time_zone()}, __tp_{__zt.get_sys_time()} {}

  _LIBCPP_HIDE_FROM_ABI zoned_time(_TimeZonePtr __zone, const sys_time<_Duration>& __tp)
      : __zone_{std::move(__zone)}, __tp_{__tp} {}

  _LIBCPP_HIDE_FROM_ABI zoned_time(string_view __name, const sys_time<_Duration>& __tp)
    requires requires { _TimeZonePtr{__traits::locate_zone(string_view{})}; }
      : zoned_time{__traits::locate_zone(__name), __tp} {}

  _LIBCPP_HIDE_FROM_ABI zoned_time(_TimeZonePtr __zone, const local_time<_Duration>& __tp)
    requires(is_convertible_v<decltype(std::declval<_TimeZonePtr&>() -> to_sys(local_time<_Duration>{})),
                              sys_time<duration>>)
      : __zone_{std::move(__zone)}, __tp_{__zone_->to_sys(__tp)} {}

  _LIBCPP_HIDE_FROM_ABI zoned_time(string_view __name, const local_time<_Duration>& __tp)
    requires(requires {
      _TimeZonePtr{__traits::locate_zone(string_view{})};
    } && is_convertible_v<decltype(std::declval<_TimeZonePtr&>() -> to_sys(local_time<_Duration>{})),
                          sys_time<duration>>)
      : zoned_time{__traits::locate_zone(__name), __tp} {}

  _LIBCPP_HIDE_FROM_ABI zoned_time(_TimeZonePtr __zone, const local_time<_Duration>& __tp, choose __c)
    requires(is_convertible_v<
                decltype(std::declval<_TimeZonePtr&>() -> to_sys(local_time<_Duration>{}, choose::earliest)),
                sys_time<duration>>)
      : __zone_{std::move(__zone)}, __tp_{__zone_->to_sys(__tp, __c)} {}

  _LIBCPP_HIDE_FROM_ABI zoned_time(string_view __name, const local_time<_Duration>& __tp, choose __c)
    requires(requires {
      _TimeZonePtr{__traits::locate_zone(string_view{})};
    } && is_convertible_v<decltype(std::declval<_TimeZonePtr&>() -> to_sys(local_time<_Duration>{}, choose::earliest)),
                          sys_time<duration>>)
      : zoned_time{__traits::locate_zone(__name), __tp, __c} {}

  template <class _Duration2, class _TimeZonePtr2>
  _LIBCPP_HIDE_FROM_ABI zoned_time(_TimeZonePtr __zone, const zoned_time<_Duration2, _TimeZonePtr2>& __zt)
    requires is_convertible_v<sys_time<_Duration2>, sys_time<_Duration>>
      : __zone_{std::move(__zone)}, __tp_{__zt.get_sys_time()} {}

  // per wording choose has no effect
  template <class _Duration2, class _TimeZonePtr2>
  _LIBCPP_HIDE_FROM_ABI zoned_time(_TimeZonePtr __zone, const zoned_time<_Duration2, _TimeZonePtr2>& __zt, choose)
    requires is_convertible_v<sys_time<_Duration2>, sys_time<_Duration>>
      : __zone_{std::move(__zone)}, __tp_{__zt.get_sys_time()} {}

  template <class _Duration2, class _TimeZonePtr2>
  _LIBCPP_HIDE_FROM_ABI zoned_time(string_view __name, const zoned_time<_Duration2, _TimeZonePtr2>& __zt)
    requires(requires {
      _TimeZonePtr{__traits::locate_zone(string_view{})};
    } && is_convertible_v<sys_time<_Duration2>, sys_time<_Duration>>)
      : zoned_time{__traits::locate_zone(__name), __zt} {}

  template <class _Duration2, class _TimeZonePtr2>
  _LIBCPP_HIDE_FROM_ABI zoned_time(string_view __name, const zoned_time<_Duration2, _TimeZonePtr2>& __zt, choose __c)
    requires(requires {
      _TimeZonePtr{__traits::locate_zone(string_view{})};
    } && is_convertible_v<sys_time<_Duration2>, sys_time<_Duration>>)
      : zoned_time{__traits::locate_zone(__name), __zt, __c} {}

  _LIBCPP_HIDE_FROM_ABI zoned_time& operator=(const sys_time<_Duration>& __tp) {
    __tp_ = __tp;
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI zoned_time& operator=(const local_time<_Duration>& __tp) {
    // TODO TZDB This seems wrong.
    // Assigning a non-existent or ambiguous time will throw and not satisfy
    // the post condition. This seems quite odd; I constructed an object with
    // choose::earliest and that choice is not respected.
    // what did LEWG do with this.
    // MSVC STL and libstdc++ behave the same
    __tp_ = __zone_->to_sys(__tp);
    return *this;
  }

  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI operator sys_time<duration>() const { return get_sys_time(); }
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI explicit operator local_time<duration>() const { return get_local_time(); }

  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI _TimeZonePtr get_time_zone() const { return __zone_; }
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI local_time<duration> get_local_time() const { return __zone_->to_local(__tp_); }
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI sys_time<duration> get_sys_time() const { return __tp_; }
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI sys_info get_info() const { return __zone_->get_info(__tp_); }

private:
  _TimeZonePtr __zone_;
  sys_time<duration> __tp_;
};

zoned_time() -> zoned_time<seconds>;

template <class _Duration>
zoned_time(sys_time<_Duration>) -> zoned_time<common_type_t<_Duration, seconds>>;

template <class _TimeZonePtrOrName>
using __time_zone_representation =
    conditional_t<is_convertible_v<_TimeZonePtrOrName, string_view>,
                  const time_zone*,
                  remove_cvref_t<_TimeZonePtrOrName>>;

template <class _TimeZonePtrOrName>
zoned_time(_TimeZonePtrOrName&&) -> zoned_time<seconds, __time_zone_representation<_TimeZonePtrOrName>>;

template <class _TimeZonePtrOrName, class _Duration>
zoned_time(_TimeZonePtrOrName&&, sys_time<_Duration>)
    -> zoned_time<common_type_t<_Duration, seconds>, __time_zone_representation<_TimeZonePtrOrName>>;

template <class _TimeZonePtrOrName, class _Duration>
zoned_time(_TimeZonePtrOrName&&, local_time<_Duration>, choose = choose::earliest)
    -> zoned_time<common_type_t<_Duration, seconds>, __time_zone_representation<_TimeZonePtrOrName>>;

template <class _Duration, class _TimeZonePtrOrName, class TimeZonePtr2>
zoned_time(_TimeZonePtrOrName&&, zoned_time<_Duration, TimeZonePtr2>, choose = choose::earliest)
    -> zoned_time<common_type_t<_Duration, seconds>, __time_zone_representation<_TimeZonePtrOrName>>;

using zoned_seconds = zoned_time<seconds>;

template <class _Duration1, class _Duration2, class _TimeZonePtr>
_LIBCPP_HIDE_FROM_ABI bool
operator==(const zoned_time<_Duration1, _TimeZonePtr>& __lhs, const zoned_time<_Duration2, _TimeZonePtr>& __rhs) {
  return __lhs.get_time_zone() == __rhs.get_time_zone() && __lhs.get_sys_time() == __rhs.get_sys_time();
}

} // namespace chrono

#  endif // _LIBCPP_STD_VER >= 20 && !defined(_LIBCPP_HAS_NO_TIME_ZONE_DATABASE) && !defined(_LIBCPP_HAS_NO_FILESYSTEM)
         // && !defined(_LIBCPP_HAS_NO_LOCALIZATION)

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // !defined(_LIBCPP_HAS_NO_EXPERIMENTAL_TZDB)

#endif // _LIBCPP___CHRONO_ZONED_TIME_H
