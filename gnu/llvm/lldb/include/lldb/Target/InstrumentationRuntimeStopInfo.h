//===-- InstrumentationRuntimeStopInfo.h ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TARGET_INSTRUMENTATIONRUNTIMESTOPINFO_H
#define LLDB_TARGET_INSTRUMENTATIONRUNTIMESTOPINFO_H

#include <string>

#include "lldb/Target/StopInfo.h"
#include "lldb/Utility/StructuredData.h"

namespace lldb_private {

class InstrumentationRuntimeStopInfo : public StopInfo {
public:
  ~InstrumentationRuntimeStopInfo() override = default;

  lldb::StopReason GetStopReason() const override {
    return lldb::eStopReasonInstrumentation;
  }

  const char *GetDescription() override;

  bool DoShouldNotify(Event *event_ptr) override { return true; }

  static lldb::StopInfoSP CreateStopReasonWithInstrumentationData(
      Thread &thread, std::string description,
      StructuredData::ObjectSP additional_data);

private:
  InstrumentationRuntimeStopInfo(Thread &thread, std::string description,
                                 StructuredData::ObjectSP additional_data);
};

} // namespace lldb_private

#endif // LLDB_TARGET_INSTRUMENTATIONRUNTIMESTOPINFO_H
