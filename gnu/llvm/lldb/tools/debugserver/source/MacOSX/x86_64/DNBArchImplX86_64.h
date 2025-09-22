//===-- DNBArchImplX86_64.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Created by Greg Clayton on 6/25/07.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_DEBUGSERVER_SOURCE_MACOSX_X86_64_DNBARCHIMPLX86_64_H
#define LLDB_TOOLS_DEBUGSERVER_SOURCE_MACOSX_X86_64_DNBARCHIMPLX86_64_H

#if defined(__i386__) || defined(__x86_64__)
#include "DNBArch.h"
#include "MachRegisterStatesX86_64.h"

#include <map>

class MachThread;

class DNBArchImplX86_64 : public DNBArchProtocol {
public:
  DNBArchImplX86_64(MachThread *thread)
      : DNBArchProtocol(), m_thread(thread), m_state(), m_2pc_dbg_checkpoint(),
        m_2pc_trans_state(Trans_Done), m_saved_register_states() {}
  virtual ~DNBArchImplX86_64() {}

  static void Initialize();

  bool GetRegisterValue(uint32_t set, uint32_t reg,
                        DNBRegisterValue *value) override;
  bool SetRegisterValue(uint32_t set, uint32_t reg,
                        const DNBRegisterValue *value) override;
  nub_size_t GetRegisterContext(void *buf, nub_size_t buf_len) override;
  nub_size_t SetRegisterContext(const void *buf, nub_size_t buf_len) override;
  uint32_t SaveRegisterState() override;
  bool RestoreRegisterState(uint32_t save_id) override;

  kern_return_t GetRegisterState(int set, bool force) override;
  kern_return_t SetRegisterState(int set) override;
  bool RegisterSetStateIsValid(int set) const override;

  uint64_t GetPC(uint64_t failValue) override; // Get program counter
  kern_return_t SetPC(uint64_t value) override;
  uint64_t GetSP(uint64_t failValue) override; // Get stack pointer
  void ThreadWillResume() override;
  bool ThreadDidStop() override;
  bool NotifyException(MachException::Data &exc) override;

  uint32_t NumSupportedHardwareBreakpoints() override;
  uint32_t NumSupportedHardwareWatchpoints() override;

  uint32_t EnableHardwareBreakpoint(nub_addr_t addr, nub_size_t size,
                                    bool also_set_on_task) override;
  bool DisableHardwareBreakpoint(uint32_t hw_break_index,
                                 bool also_set_on_task) override;
  uint32_t EnableHardwareWatchpoint(nub_addr_t addr, nub_size_t size,
                                    bool read, bool write,
                                    bool also_set_on_task) override;
  bool DisableHardwareWatchpoint(uint32_t hw_break_index,
                                 bool also_set_on_task) override;
  uint32_t GetHardwareWatchpointHit(nub_addr_t &addr) override;

protected:
  kern_return_t EnableHardwareSingleStep(bool enable);

  typedef __x86_64_thread_state_t GPR;
  typedef __x86_64_float_state_t FPU;
  typedef __x86_64_exception_state_t EXC;
  typedef __x86_64_avx_state_t AVX;
  typedef __x86_64_debug_state_t DBG;

  static const DNBRegisterInfo g_gpr_registers[];
  static const DNBRegisterInfo g_fpu_registers_no_avx[];
  static const DNBRegisterInfo g_fpu_registers_avx[];
  static const DNBRegisterInfo g_exc_registers[];
  static const DNBRegisterSetInfo g_reg_sets_no_avx[];
  static const DNBRegisterSetInfo g_reg_sets_avx[];
  static const size_t k_num_gpr_registers;
  static const size_t k_num_fpu_registers_no_avx;
  static const size_t k_num_fpu_registers_avx;
  static const size_t k_num_exc_registers;
  static const size_t k_num_all_registers_no_avx;
  static const size_t k_num_all_registers_avx;
  static const size_t k_num_register_sets;

  typedef __x86_64_avx512f_state_t AVX512F;
  static const DNBRegisterInfo g_fpu_registers_avx512f[];
  static const DNBRegisterSetInfo g_reg_sets_avx512f[];
  static const size_t k_num_fpu_registers_avx512f;
  static const size_t k_num_all_registers_avx512f;

  enum RegisterSet {
    e_regSetALL = REGISTER_SET_ALL,
    e_regSetGPR,
    e_regSetFPU,
    e_regSetEXC,
    e_regSetDBG,
    kNumRegisterSets
  };

  enum RegisterSetWordSize {
    e_regSetWordSizeGPR = sizeof(GPR) / sizeof(int),
    e_regSetWordSizeFPU = sizeof(FPU) / sizeof(int),
    e_regSetWordSizeEXC = sizeof(EXC) / sizeof(int),
    e_regSetWordSizeAVX = sizeof(AVX) / sizeof(int),
    e_regSetWordSizeAVX512f = sizeof(AVX512F) / sizeof(int),
    e_regSetWordSizeDBG = sizeof(DBG) / sizeof(int)
  };

  enum { Read = 0, Write = 1, kNumErrors = 2 };

  struct Context {
    GPR gpr;
    union {
      FPU no_avx;
      AVX avx;
      AVX512F avx512f;
    } fpu;
    EXC exc;
    DBG dbg;
  };

  struct State {
    Context context;
    kern_return_t gpr_errs[2]; // Read/Write errors
    kern_return_t fpu_errs[2]; // Read/Write errors
    kern_return_t exc_errs[2]; // Read/Write errors
    kern_return_t dbg_errs[2]; // Read/Write errors

    State() {
      uint32_t i;
      for (i = 0; i < kNumErrors; i++) {
        gpr_errs[i] = -1;
        fpu_errs[i] = -1;
        exc_errs[i] = -1;
        dbg_errs[i] = -1;
      }
    }

    void InvalidateAllRegisterStates() { SetError(e_regSetALL, Read, -1); }

    kern_return_t GetError(int flavor, uint32_t err_idx) const {
      if (err_idx < kNumErrors) {
        switch (flavor) {
        // When getting all errors, just OR all values together to see if
        // we got any kind of error.
        case e_regSetALL:
          return gpr_errs[err_idx] | fpu_errs[err_idx] | exc_errs[err_idx];
        case e_regSetGPR:
          return gpr_errs[err_idx];
        case e_regSetFPU:
          return fpu_errs[err_idx];
        case e_regSetEXC:
          return exc_errs[err_idx];
        case e_regSetDBG:
          return dbg_errs[err_idx];
        default:
          break;
        }
      }
      return -1;
    }

    bool SetError(int flavor, uint32_t err_idx, kern_return_t err) {
      if (err_idx < kNumErrors) {
        switch (flavor) {
        case e_regSetALL:
          gpr_errs[err_idx] = fpu_errs[err_idx] = exc_errs[err_idx] =
              dbg_errs[err_idx] = err;
          return true;

        case e_regSetGPR:
          gpr_errs[err_idx] = err;
          return true;

        case e_regSetFPU:
          fpu_errs[err_idx] = err;
          return true;

        case e_regSetEXC:
          exc_errs[err_idx] = err;
          return true;

        case e_regSetDBG:
          dbg_errs[err_idx] = err;
          return true;

        default:
          break;
        }
      }
      return false;
    }

    bool RegsAreValid(int flavor) const {
      return GetError(flavor, Read) == KERN_SUCCESS;
    }
  };

  kern_return_t GetGPRState(bool force);
  kern_return_t GetFPUState(bool force);
  kern_return_t GetEXCState(bool force);
  kern_return_t GetDBGState(bool force);

  kern_return_t SetGPRState();
  kern_return_t SetFPUState();
  kern_return_t SetEXCState();
  kern_return_t SetDBGState(bool also_set_on_task);

  static DNBArchProtocol *Create(MachThread *thread);

  static const uint8_t *SoftwareBreakpointOpcode(nub_size_t byte_size);

  static const DNBRegisterSetInfo *GetRegisterSetInfo(nub_size_t *num_reg_sets);

  static uint32_t GetRegisterContextSize();

  static void SetHardwareBreakpoint(DBG &debug_state, uint32_t hw_index,
                                    nub_addr_t addr, nub_size_t size);

  // Helper functions for watchpoint manipulations.
  static void SetWatchpoint(DBG &debug_state, uint32_t hw_index,
                            nub_addr_t addr, nub_size_t size, bool read,
                            bool write);
  static void ClearWatchpoint(DBG &debug_state, uint32_t hw_index);
  static bool IsWatchpointVacant(const DBG &debug_state, uint32_t hw_index);
  static void ClearWatchpointHits(DBG &debug_state);
  static bool IsWatchpointHit(const DBG &debug_state, uint32_t hw_index);
  static nub_addr_t GetWatchAddress(const DBG &debug_state, uint32_t hw_index);

  bool StartTransForHWP() override;
  bool RollbackTransForHWP() override;
  bool FinishTransForHWP() override;
  DBG GetDBGCheckpoint();

  MachThread *m_thread;
  State m_state;
  DBG m_2pc_dbg_checkpoint;
  uint32_t m_2pc_trans_state; // Is transaction of DBG state change: Pedning
                              // (0), Done (1), or Rolled Back (2)?
  typedef std::map<uint32_t, Context> SaveRegisterStates;
  SaveRegisterStates m_saved_register_states;
};

#endif // #if defined (__i386__) || defined (__x86_64__)
#endif // LLDB_TOOLS_DEBUGSERVER_SOURCE_MACOSX_X86_64_DNBARCHIMPLX86_64_H
