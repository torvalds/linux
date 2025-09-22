//===-- StopInfoMachException.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_STOPINFOMACHEXCEPTION_H
#define LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_STOPINFOMACHEXCEPTION_H

#include <optional>
#include <string>

#include "lldb/Target/StopInfo.h"

#if defined(__APPLE__)
// Needed for the EXC_* defines
#include <mach/exception.h>
#endif

namespace lldb_private {

class StopInfoMachException : public StopInfo {
  /// Determine the pointer-authentication related failure that caused this
  /// exception. Returns true and fills out the failure description if there
  /// is auth-related failure, and returns false otherwise.
  bool DeterminePtrauthFailure(ExecutionContext &exe_ctx);

public:
  // Constructors and Destructors
  StopInfoMachException(Thread &thread, uint32_t exc_type,
                        uint32_t exc_data_count, uint64_t exc_code,
                        uint64_t exc_subcode,
                        bool not_stepping_but_got_singlestep_exception)
      : StopInfo(thread, exc_type), m_exc_data_count(exc_data_count),
        m_exc_code(exc_code), m_exc_subcode(exc_subcode),
        m_not_stepping_but_got_singlestep_exception(
            not_stepping_but_got_singlestep_exception) {}

  ~StopInfoMachException() override = default;

  lldb::StopReason GetStopReason() const override {
    return lldb::eStopReasonException;
  }

  const char *GetDescription() override;

#if defined(__APPLE__)
  struct MachException {
    static const char *Name(exception_type_t exc_type);
    static std::optional<exception_type_t> ExceptionCode(const char *name);
  };
#endif

  // Since some mach exceptions will be reported as breakpoints, signals,
  // or trace, we use this static accessor which will translate the mach
  // exception into the correct StopInfo.
  static lldb::StopInfoSP CreateStopReasonWithMachException(
      Thread &thread, uint32_t exc_type, uint32_t exc_data_count,
      uint64_t exc_code, uint64_t exc_sub_code, uint64_t exc_sub_sub_code,
      bool pc_already_adjusted = true, bool adjust_pc_if_needed = false);

  bool WasContinueInterrupted(Thread &thread) override;

protected:
  uint32_t m_exc_data_count;
  uint64_t m_exc_code;
  uint64_t m_exc_subcode;

  bool m_not_stepping_but_got_singlestep_exception;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_STOPINFOMACHEXCEPTION_H
