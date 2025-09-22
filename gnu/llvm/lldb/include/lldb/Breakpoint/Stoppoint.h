//===-- Stoppoint.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_BREAKPOINT_STOPPOINT_H
#define LLDB_BREAKPOINT_STOPPOINT_H

#include "lldb/Utility/UserID.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

class Stoppoint {
public:
  // Constructors and Destructors
  Stoppoint();

  virtual ~Stoppoint();

  // Methods
  virtual void Dump(Stream *) = 0;

  virtual bool IsEnabled() = 0;

  virtual void SetEnabled(bool enable) = 0;

  lldb::break_id_t GetID() const;

  void SetID(lldb::break_id_t bid);

protected:
  lldb::break_id_t m_bid = LLDB_INVALID_BREAK_ID;

private:
  // For Stoppoint only
  Stoppoint(const Stoppoint &) = delete;
  const Stoppoint &operator=(const Stoppoint &) = delete;
};

} // namespace lldb_private

#endif // LLDB_BREAKPOINT_STOPPOINT_H
