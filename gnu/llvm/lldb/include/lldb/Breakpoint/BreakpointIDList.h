//===-- BreakpointIDList.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_BREAKPOINT_BREAKPOINTIDLIST_H
#define LLDB_BREAKPOINT_BREAKPOINTIDLIST_H

#include <utility>
#include <vector>

#include "lldb/Breakpoint/BreakpointID.h"
#include "lldb/Breakpoint/BreakpointName.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-private.h"

#include "llvm/Support/Error.h"

namespace lldb_private {

// class BreakpointIDList

class BreakpointIDList {
public:
  // TODO: Convert this class to StringRef.
  typedef std::vector<BreakpointID> BreakpointIDArray;

  BreakpointIDList();

  virtual ~BreakpointIDList();

  size_t GetSize() const;

  BreakpointID GetBreakpointIDAtIndex(size_t index) const;

  bool RemoveBreakpointIDAtIndex(size_t index);

  void Clear();

  bool AddBreakpointID(BreakpointID bp_id);

  bool Contains(BreakpointID bp_id) const;

  // Returns a pair consisting of the beginning and end of a breakpoint
  // ID range expression.  If the input string is not a valid specification,
  // returns an empty pair.
  static std::pair<llvm::StringRef, llvm::StringRef>
  SplitIDRangeExpression(llvm::StringRef in_string);

  static llvm::Error
  FindAndReplaceIDRanges(Args &old_args, Target *target, bool allow_locations,
                         BreakpointName::Permissions ::PermissionKinds purpose,
                         Args &new_args);

private:
  BreakpointIDArray m_breakpoint_ids;

  BreakpointIDList(const BreakpointIDList &) = delete;
  const BreakpointIDList &operator=(const BreakpointIDList &) = delete;
};

} // namespace lldb_private

#endif // LLDB_BREAKPOINT_BREAKPOINTIDLIST_H
