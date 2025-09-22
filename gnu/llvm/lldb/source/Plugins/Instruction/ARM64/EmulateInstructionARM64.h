//===-- EmulateInstructionARM64.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_INSTRUCTION_ARM64_EMULATEINSTRUCTIONARM64_H
#define LLDB_SOURCE_PLUGINS_INSTRUCTION_ARM64_EMULATEINSTRUCTIONARM64_H

#include "Plugins/Process/Utility/ARMDefines.h"
#include "lldb/Core/EmulateInstruction.h"
#include "lldb/Interpreter/OptionValue.h"
#include "lldb/Utility/Status.h"
#include <optional>

class EmulateInstructionARM64 : public lldb_private::EmulateInstruction {
public:
  EmulateInstructionARM64(const lldb_private::ArchSpec &arch)
      : EmulateInstruction(arch), m_opcode_pstate(), m_emulated_pstate(),
        m_ignore_conditions(false) {}

  static void Initialize();

  static void Terminate();

  static llvm::StringRef GetPluginNameStatic() { return "arm64"; }

  static llvm::StringRef GetPluginDescriptionStatic();

  static lldb_private::EmulateInstruction *
  CreateInstance(const lldb_private::ArchSpec &arch,
                 lldb_private::InstructionType inst_type);

  static bool SupportsEmulatingInstructionsOfTypeStatic(
      lldb_private::InstructionType inst_type) {
    switch (inst_type) {
    case lldb_private::eInstructionTypeAny:
    case lldb_private::eInstructionTypePrologueEpilogue:
      return true;

    case lldb_private::eInstructionTypePCModifying:
    case lldb_private::eInstructionTypeAll:
      return false;
    }
    return false;
  }

  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

  bool SetTargetTriple(const lldb_private::ArchSpec &arch) override;

  bool SupportsEmulatingInstructionsOfType(
      lldb_private::InstructionType inst_type) override {
    return SupportsEmulatingInstructionsOfTypeStatic(inst_type);
  }

  bool ReadInstruction() override;

  bool EvaluateInstruction(uint32_t evaluate_options) override;

  bool TestEmulation(lldb_private::Stream &out_stream,
                     lldb_private::ArchSpec &arch,
                     lldb_private::OptionValueDictionary *test_data) override {
    return false;
  }

  std::optional<lldb_private::RegisterInfo>
  GetRegisterInfo(lldb::RegisterKind reg_kind, uint32_t reg_num) override;

  bool
  CreateFunctionEntryUnwind(lldb_private::UnwindPlan &unwind_plan) override;

  enum AddrMode { AddrMode_OFF, AddrMode_PRE, AddrMode_POST };

  enum BranchType {
    BranchType_CALL,
    BranchType_ERET,
    BranchType_DRET,
    BranchType_RET,
    BranchType_JMP
  };

  enum CountOp { CountOp_CLZ, CountOp_CLS, CountOp_CNT };

  enum RevOp { RevOp_RBIT, RevOp_REV16, RevOp_REV32, RevOp_REV64 };

  enum BitwiseOp { BitwiseOp_NOT, BitwiseOp_RBIT };

  enum ExceptionLevel { EL0 = 0, EL1 = 1, EL2 = 2, EL3 = 3 };

  enum ExtendType {
    ExtendType_SXTB,
    ExtendType_SXTH,
    ExtendType_SXTW,
    ExtendType_SXTX,
    ExtendType_UXTB,
    ExtendType_UXTH,
    ExtendType_UXTW,
    ExtendType_UXTX
  };

  enum ExtractType { ExtractType_LEFT, ExtractType_RIGHT };

  enum LogicalOp { LogicalOp_AND, LogicalOp_EOR, LogicalOp_ORR };

  enum MemOp { MemOp_LOAD, MemOp_STORE, MemOp_PREFETCH, MemOp_NOP };

  enum MoveWideOp { MoveWideOp_N, MoveWideOp_Z, MoveWideOp_K };

  enum ShiftType { ShiftType_LSL, ShiftType_LSR, ShiftType_ASR, ShiftType_ROR };

  enum StackPointerSelection { SP0 = 0, SPx = 1 };

  enum Unpredictable { Unpredictable_WBOVERLAP, Unpredictable_LDPOVERLAP };

  enum ConstraintType {
    Constraint_NONE,
    Constraint_UNKNOWN,
    Constraint_SUPPRESSWB,
    Constraint_NOP
  };

  enum AccType {
    AccType_NORMAL,
    AccType_UNPRIV,
    AccType_STREAM,
    AccType_ALIGNED,
    AccType_ORDERED
  };

  typedef struct {
    uint32_t N : 1, V : 1, C : 1,
        Z : 1,  // condition code flags – can also be accessed as
                // PSTATE.[N,Z,C,V]
        Q : 1,  // AArch32 only – CSPR.Q bit
        IT : 8, // AArch32 only – CPSR.IT bits
        J : 1,  // AArch32 only – CSPR.J bit
        T : 1,  // AArch32 only – CPSR.T bit
        SS : 1, // Single step process state bit
        IL : 1, // Illegal state bit
        D : 1, A : 1, I : 1,
        F : 1,  // Interrupt masks – can also be accessed as PSTATE.[D,A,I,F]
        E : 1,  // AArch32 only – CSPR.E bit
        M : 5,  // AArch32 only – mode encodings
        RW : 1, // Current register width – 0 is AArch64, 1 is AArch32
        EL : 2, // Current exception level (see ExceptionLevel enum)
        SP : 1; // AArch64 only - Stack Pointer selection (see
                // StackPointerSelection enum)
  } ProcState;

protected:
  static uint64_t AddWithCarry(uint32_t N, uint64_t x, uint64_t y, bool carry_in,
                               EmulateInstructionARM64::ProcState &proc_state);

  typedef struct {
    uint32_t mask;
    uint32_t value;
    uint32_t vfp_variants;
    bool (EmulateInstructionARM64::*callback)(const uint32_t opcode);
    const char *name;
  } Opcode;

  static Opcode *GetOpcodeForInstruction(const uint32_t opcode);

  uint32_t GetFramePointerRegisterNumber() const;

  bool BranchTo(const Context &context, uint32_t N, lldb::addr_t target);

  bool ConditionHolds(const uint32_t cond);

  bool UsingAArch32();

  bool EmulateADDSUBImm(const uint32_t opcode);

  template <AddrMode a_mode> bool EmulateLDPSTP(const uint32_t opcode);

  template <AddrMode a_mode> bool EmulateLDRSTRImm(const uint32_t opcode);

  bool EmulateB(const uint32_t opcode);

  bool EmulateBcond(const uint32_t opcode);

  bool EmulateCBZ(const uint32_t opcode);

  bool EmulateTBZ(const uint32_t opcode);

  ProcState m_opcode_pstate;
  ProcState m_emulated_pstate; // This can get updated by the opcode.
  bool m_ignore_conditions;
};

#endif // LLDB_SOURCE_PLUGINS_INSTRUCTION_ARM64_EMULATEINSTRUCTIONARM64_H
