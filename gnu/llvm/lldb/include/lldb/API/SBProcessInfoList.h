//===-- SBProcessInfoList.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBPROCESSINSTANCEINFOLIST_H
#define LLDB_API_SBPROCESSINSTANCEINFOLIST_H

#include "lldb/API/SBDefines.h"

#include <memory>

namespace lldb_private {
class ProcessInfoList;
} // namespace lldb_private

namespace lldb {

class LLDB_API SBProcessInfoList {
public:
  SBProcessInfoList();
  ~SBProcessInfoList();

  SBProcessInfoList(const lldb::SBProcessInfoList &rhs);

  const lldb::SBProcessInfoList &operator=(const lldb::SBProcessInfoList &rhs);

  uint32_t GetSize() const;

  bool GetProcessInfoAtIndex(uint32_t idx, SBProcessInfo &info);

  void Clear();

private:
  friend SBPlatform;

  SBProcessInfoList(const lldb_private::ProcessInfoList &impl);
  std::unique_ptr<lldb_private::ProcessInfoList> m_opaque_up;
};

} // namespace lldb

#endif // LLDB_API_SBPROCESSINSTANCEINFOLIST_H
