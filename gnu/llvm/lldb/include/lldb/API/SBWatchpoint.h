//===-- SBWatchpoint.h ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBWATCHPOINT_H
#define LLDB_API_SBWATCHPOINT_H

#include "lldb/API/SBDefines.h"
#include "lldb/API/SBType.h"

namespace lldb_private {
namespace python {
class SWIGBridge;
}
namespace lua {
class SWIGBridge;
}
} // namespace lldb_private

namespace lldb {

class LLDB_API SBWatchpoint {
public:
  SBWatchpoint();

  SBWatchpoint(const lldb::SBWatchpoint &rhs);

  ~SBWatchpoint();

  const lldb::SBWatchpoint &operator=(const lldb::SBWatchpoint &rhs);

  explicit operator bool() const;

  bool operator==(const SBWatchpoint &rhs) const;

  bool operator!=(const SBWatchpoint &rhs) const;

  bool IsValid() const;

  SBError GetError();

  watch_id_t GetID();

  LLDB_DEPRECATED("Hardware index is not available, always returns -1")
  int32_t GetHardwareIndex();

  lldb::addr_t GetWatchAddress();

  size_t GetWatchSize();

  void SetEnabled(bool enabled);

  bool IsEnabled();

  uint32_t GetHitCount();

  uint32_t GetIgnoreCount();

  void SetIgnoreCount(uint32_t n);

  const char *GetCondition();

  void SetCondition(const char *condition);

  bool GetDescription(lldb::SBStream &description, DescriptionLevel level);

  void Clear();

  static bool EventIsWatchpointEvent(const lldb::SBEvent &event);

  static lldb::WatchpointEventType
  GetWatchpointEventTypeFromEvent(const lldb::SBEvent &event);

  static lldb::SBWatchpoint GetWatchpointFromEvent(const lldb::SBEvent &event);

  lldb::SBType GetType();

  WatchpointValueKind GetWatchValueKind();

  const char *GetWatchSpec();

  bool IsWatchingReads();

  bool IsWatchingWrites();

protected:
  friend class lldb_private::python::SWIGBridge;
  friend class lldb_private::lua::SWIGBridge;

  SBWatchpoint(const lldb::WatchpointSP &wp_sp);

  lldb::WatchpointSP GetSP() const;

  void SetSP(const lldb::WatchpointSP &sp);

private:
  friend class SBTarget;
  friend class SBValue;

  std::weak_ptr<lldb_private::Watchpoint> m_opaque_wp;
};

} // namespace lldb

#endif // LLDB_API_SBWATCHPOINT_H
