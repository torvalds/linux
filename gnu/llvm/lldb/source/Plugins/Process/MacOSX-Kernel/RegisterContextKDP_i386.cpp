//===-- RegisterContextKDP_i386.cpp ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RegisterContextKDP_i386.h"
#include "ProcessKDP.h"
#include "ThreadKDP.h"

using namespace lldb;
using namespace lldb_private;

RegisterContextKDP_i386::RegisterContextKDP_i386(ThreadKDP &thread,
                                                 uint32_t concrete_frame_idx)
    : RegisterContextDarwin_i386(thread, concrete_frame_idx),
      m_kdp_thread(thread) {}

RegisterContextKDP_i386::~RegisterContextKDP_i386() = default;

int RegisterContextKDP_i386::DoReadGPR(lldb::tid_t tid, int flavor, GPR &gpr) {
  ProcessSP process_sp(CalculateProcess());
  if (process_sp) {
    Status error;
    if (static_cast<ProcessKDP *>(process_sp.get())
            ->GetCommunication()
            .SendRequestReadRegisters(tid, GPRRegSet, &gpr, sizeof(gpr),
                                      error)) {
      if (error.Success())
        return 0;
    }
  }
  return -1;
}

int RegisterContextKDP_i386::DoReadFPU(lldb::tid_t tid, int flavor, FPU &fpu) {
  ProcessSP process_sp(CalculateProcess());
  if (process_sp) {
    Status error;
    if (static_cast<ProcessKDP *>(process_sp.get())
            ->GetCommunication()
            .SendRequestReadRegisters(tid, FPURegSet, &fpu, sizeof(fpu),
                                      error)) {
      if (error.Success())
        return 0;
    }
  }
  return -1;
}

int RegisterContextKDP_i386::DoReadEXC(lldb::tid_t tid, int flavor, EXC &exc) {
  ProcessSP process_sp(CalculateProcess());
  if (process_sp) {
    Status error;
    if (static_cast<ProcessKDP *>(process_sp.get())
            ->GetCommunication()
            .SendRequestReadRegisters(tid, EXCRegSet, &exc, sizeof(exc),
                                      error)) {
      if (error.Success())
        return 0;
    }
  }
  return -1;
}

int RegisterContextKDP_i386::DoWriteGPR(lldb::tid_t tid, int flavor,
                                        const GPR &gpr) {
  ProcessSP process_sp(CalculateProcess());
  if (process_sp) {
    Status error;
    if (static_cast<ProcessKDP *>(process_sp.get())
            ->GetCommunication()
            .SendRequestWriteRegisters(tid, GPRRegSet, &gpr, sizeof(gpr),
                                       error)) {
      if (error.Success())
        return 0;
    }
  }
  return -1;
}

int RegisterContextKDP_i386::DoWriteFPU(lldb::tid_t tid, int flavor,
                                        const FPU &fpu) {
  ProcessSP process_sp(CalculateProcess());
  if (process_sp) {
    Status error;
    if (static_cast<ProcessKDP *>(process_sp.get())
            ->GetCommunication()
            .SendRequestWriteRegisters(tid, FPURegSet, &fpu, sizeof(fpu),
                                       error)) {
      if (error.Success())
        return 0;
    }
  }
  return -1;
}

int RegisterContextKDP_i386::DoWriteEXC(lldb::tid_t tid, int flavor,
                                        const EXC &exc) {
  ProcessSP process_sp(CalculateProcess());
  if (process_sp) {
    Status error;
    if (static_cast<ProcessKDP *>(process_sp.get())
            ->GetCommunication()
            .SendRequestWriteRegisters(tid, EXCRegSet, &exc, sizeof(exc),
                                       error)) {
      if (error.Success())
        return 0;
    }
  }
  return -1;
}
