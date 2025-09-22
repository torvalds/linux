//===-- EmulateInstructionARM.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_INSTRUCTION_ARM_EMULATEINSTRUCTIONARM_H
#define LLDB_SOURCE_PLUGINS_INSTRUCTION_ARM_EMULATEINSTRUCTIONARM_H

#include "Plugins/Process/Utility/ARMDefines.h"
#include "lldb/Core/EmulateInstruction.h"
#include "lldb/Utility/Status.h"
#include <optional>

namespace lldb_private {

// ITSession - Keep track of the IT Block progression.
class ITSession {
public:
  ITSession() = default;
  ~ITSession() = default;

  // InitIT - Initializes ITCounter/ITState.
  bool InitIT(uint32_t bits7_0);

  // ITAdvance - Updates ITCounter/ITState as IT Block progresses.
  void ITAdvance();

  // InITBlock - Returns true if we're inside an IT Block.
  bool InITBlock();

  // LastInITBlock - Returns true if we're the last instruction inside an IT
  // Block.
  bool LastInITBlock();

  // GetCond - Gets condition bits for the current thumb instruction.
  uint32_t GetCond();

private:
  uint32_t ITCounter = 0; // Possible values: 0, 1, 2, 3, 4.
  uint32_t ITState = 0;   // A2.5.2 Consists of IT[7:5] and IT[4:0] initially.
};

class EmulateInstructionARM : public EmulateInstruction {
public:
  enum ARMEncoding {
    eEncodingA1,
    eEncodingA2,
    eEncodingA3,
    eEncodingA4,
    eEncodingA5,
    eEncodingT1,
    eEncodingT2,
    eEncodingT3,
    eEncodingT4,
    eEncodingT5
  };

  static void Initialize();

  static void Terminate();

  static llvm::StringRef GetPluginNameStatic() { return "arm"; }

  static llvm::StringRef GetPluginDescriptionStatic();

  static lldb_private::EmulateInstruction *
  CreateInstance(const lldb_private::ArchSpec &arch, InstructionType inst_type);

  static bool
  SupportsEmulatingInstructionsOfTypeStatic(InstructionType inst_type) {
    switch (inst_type) {
    case eInstructionTypeAny:
    case eInstructionTypePrologueEpilogue:
    case eInstructionTypePCModifying:
      return true;

    case eInstructionTypeAll:
      return false;
    }
    return false;
  }

  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

  bool SetTargetTriple(const ArchSpec &arch) override;

  enum Mode { eModeInvalid = -1, eModeARM, eModeThumb };

  EmulateInstructionARM(const ArchSpec &arch)
      : EmulateInstruction(arch), m_arm_isa(0), m_opcode_mode(eModeInvalid),
        m_opcode_cpsr(0), m_new_inst_cpsr(0), m_it_session(),
        m_ignore_conditions(false) {
    SetArchitecture(arch);
  }

  //    EmulateInstructionARM (const ArchSpec &arch,
  //                           bool ignore_conditions,
  //                           void *baton,
  //                           ReadMemory read_mem_callback,
  //                           WriteMemory write_mem_callback,
  //                           ReadRegister read_reg_callback,
  //                           WriteRegister write_reg_callback) :
  //        EmulateInstruction (arch,
  //                            ignore_conditions,
  //                            baton,
  //                            read_mem_callback,
  //                            write_mem_callback,
  //                            read_reg_callback,
  //                            write_reg_callback),
  //        m_arm_isa (0),
  //        m_opcode_mode (eModeInvalid),
  //        m_opcode_cpsr (0),
  //        m_it_session ()
  //    {
  //    }

  bool SupportsEmulatingInstructionsOfType(InstructionType inst_type) override {
    return SupportsEmulatingInstructionsOfTypeStatic(inst_type);
  }

  virtual bool SetArchitecture(const ArchSpec &arch);

  bool ReadInstruction() override;

  bool SetInstruction(const Opcode &insn_opcode, const Address &inst_addr,
                      Target *target) override;

  bool EvaluateInstruction(uint32_t evaluate_options) override;

  InstructionCondition GetInstructionCondition() override;

  bool TestEmulation(Stream &out_stream, ArchSpec &arch,
                     OptionValueDictionary *test_data) override;

  std::optional<RegisterInfo> GetRegisterInfo(lldb::RegisterKind reg_kind,
                                              uint32_t reg_num) override;

  bool CreateFunctionEntryUnwind(UnwindPlan &unwind_plan) override;

  uint32_t ArchVersion();

  bool ConditionPassed(const uint32_t opcode);

  uint32_t CurrentCond(const uint32_t opcode);

  // InITBlock - Returns true if we're in Thumb mode and inside an IT Block.
  bool InITBlock();

  // LastInITBlock - Returns true if we're in Thumb mode and the last
  // instruction inside an IT Block.
  bool LastInITBlock();

  bool BadMode(uint32_t mode);

  bool CurrentModeIsPrivileged();

  void CPSRWriteByInstr(uint32_t value, uint32_t bytemask,
                        bool affect_execstate);

  bool BranchWritePC(const Context &context, uint32_t addr);

  bool BXWritePC(Context &context, uint32_t addr);

  bool LoadWritePC(Context &context, uint32_t addr);

  bool ALUWritePC(Context &context, uint32_t addr);

  Mode CurrentInstrSet();

  bool SelectInstrSet(Mode arm_or_thumb);

  bool WriteBits32Unknown(int n);

  bool WriteBits32UnknownToMemory(lldb::addr_t address);

  bool UnalignedSupport();

  typedef struct {
    uint32_t result;
    uint8_t carry_out;
    uint8_t overflow;
  } AddWithCarryResult;

  AddWithCarryResult AddWithCarry(uint32_t x, uint32_t y, uint8_t carry_in);

  // Helper method to read the content of an ARM core register.
  uint32_t ReadCoreReg(uint32_t regnum, bool *success);

  // See A8.6.96 MOV (immediate) Operation.
  // Default arguments are specified for carry and overflow parameters, which
  // means
  // not to update the respective flags even if setflags is true.
  bool WriteCoreRegOptionalFlags(Context &context, const uint32_t result,
                                 const uint32_t Rd, bool setflags,
                                 const uint32_t carry = ~0u,
                                 const uint32_t overflow = ~0u);

  bool WriteCoreReg(Context &context, const uint32_t result,
                    const uint32_t Rd) {
    // Don't set the flags.
    return WriteCoreRegOptionalFlags(context, result, Rd, false);
  }

  // See A8.6.35 CMP (immediate) Operation.
  // Default arguments are specified for carry and overflow parameters, which
  // means
  // not to update the respective flags.
  bool WriteFlags(Context &context, const uint32_t result,
                  const uint32_t carry = ~0u, const uint32_t overflow = ~0u);

  inline uint64_t MemARead(EmulateInstruction::Context &context,
                           lldb::addr_t address, uint32_t size,
                           uint64_t fail_value, bool *success_ptr) {
    // This is a stub function corresponding to "MemA[]" in the ARM manual
    // pseudocode, for
    // aligned reads from memory.  Since we are not trying to write a full
    // hardware simulator, and since
    // we are running in User mode (rather than Kernel mode) and therefore won't
    // have access to many of the
    // system registers we would need in order to fully implement this function,
    // we will just call
    // ReadMemoryUnsigned from here.  In the future, if we decide we do need to
    // do more faithful emulation of
    // the hardware, we can update this function appropriately.

    return ReadMemoryUnsigned(context, address, size, fail_value, success_ptr);
  }

  inline bool MemAWrite(EmulateInstruction::Context &context,
                        lldb::addr_t address, uint64_t data_val, uint32_t size)

  {
    // This is a stub function corresponding to "MemA[]" in the ARM manual
    // pseudocode, for
    // aligned writes to memory.  Since we are not trying to write a full
    // hardware simulator, and since
    // we are running in User mode (rather than Kernel mode) and therefore won't
    // have access to many of the
    // system registers we would need in order to fully implement this function,
    // we will just call
    // WriteMemoryUnsigned from here.  In the future, if we decide we do need to
    // do more faithful emulation of
    // the hardware, we can update this function appropriately.

    return WriteMemoryUnsigned(context, address, data_val, size);
  }

  inline uint64_t MemURead(EmulateInstruction::Context &context,
                           lldb::addr_t address, uint32_t size,
                           uint64_t fail_value, bool *success_ptr) {
    // This is a stub function corresponding to "MemU[]" in the ARM manual
    // pseudocode, for
    // unaligned reads from memory.  Since we are not trying to write a full
    // hardware simulator, and since
    // we are running in User mode (rather than Kernel mode) and therefore won't
    // have access to many of the
    // system registers we would need in order to fully implement this function,
    // we will just call
    // ReadMemoryUnsigned from here.  In the future, if we decide we do need to
    // do more faithful emulation of
    // the hardware, we can update this function appropriately.

    return ReadMemoryUnsigned(context, address, size, fail_value, success_ptr);
  }

  inline bool MemUWrite(EmulateInstruction::Context &context,
                        lldb::addr_t address, uint64_t data_val, uint32_t size)

  {
    // This is a stub function corresponding to "MemU[]" in the ARM manual
    // pseudocode, for
    // unaligned writes to memory.  Since we are not trying to write a full
    // hardware simulator, and since
    // we are running in User mode (rather than Kernel mode) and therefore won't
    // have access to many of the
    // system registers we would need in order to fully implement this function,
    // we will just call
    // WriteMemoryUnsigned from here.  In the future, if we decide we do need to
    // do more faithful emulation of
    // the hardware, we can update this function appropriately.

    return WriteMemoryUnsigned(context, address, data_val, size);
  }

protected:
  // Typedef for the callback function used during the emulation.
  // Pass along (ARMEncoding)encoding as the callback data.
  enum ARMInstrSize { eSize16, eSize32 };

  typedef struct {
    uint32_t mask;
    uint32_t value;
    uint32_t variants;
    EmulateInstructionARM::ARMEncoding encoding;
    uint32_t vfp_variants;
    ARMInstrSize size;
    bool (EmulateInstructionARM::*callback)(
        const uint32_t opcode,
        const EmulateInstructionARM::ARMEncoding encoding);
    const char *name;
  } ARMOpcode;

  uint32_t GetFramePointerRegisterNumber() const;

  uint32_t GetFramePointerDWARFRegisterNumber() const;

  static ARMOpcode *GetARMOpcodeForInstruction(const uint32_t opcode,
                                               uint32_t isa_mask);

  static ARMOpcode *GetThumbOpcodeForInstruction(const uint32_t opcode,
                                                 uint32_t isa_mask);

  // A8.6.123 PUSH
  bool EmulatePUSH(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.122 POP
  bool EmulatePOP(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.8 ADD (SP plus immediate)
  bool EmulateADDRdSPImm(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.97 MOV (register) -- Rd == r7|ip and Rm == sp
  bool EmulateMOVRdSP(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.97 MOV (register) -- move from r8-r15 to r0-r7
  bool EmulateMOVLowHigh(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.59 LDR (literal)
  bool EmulateLDRRtPCRelative(const uint32_t opcode,
                              const ARMEncoding encoding);

  // A8.6.8 ADD (SP plus immediate)
  bool EmulateADDSPImm(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.9 ADD (SP plus register)
  bool EmulateADDSPRm(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.23 BL, BLX (immediate)
  bool EmulateBLXImmediate(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.24 BLX (register)
  bool EmulateBLXRm(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.25 BX
  bool EmulateBXRm(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.26 BXJ
  bool EmulateBXJRm(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.212 SUB (immediate, ARM) -- Rd == r7 and Rm == ip
  bool EmulateSUBR7IPImm(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.215 SUB (SP minus immediate) -- Rd == ip
  bool EmulateSUBIPSPImm(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.215 SUB (SP minus immediate)
  bool EmulateSUBSPImm(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.216 SUB (SP minus register)
  bool EmulateSUBSPReg(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.194 STR (immediate, ARM) -- Rn == sp
  bool EmulateSTRRtSP(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.355 VPUSH
  bool EmulateVPUSH(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.354 VPOP
  bool EmulateVPOP(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.218 SVC (previously SWI)
  bool EmulateSVC(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.50 IT
  bool EmulateIT(const uint32_t opcode, const ARMEncoding encoding);

  // NOP
  bool EmulateNop(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.16 B
  bool EmulateB(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.27 CBNZ, CBZ
  bool EmulateCB(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.226 TBB, TBH
  bool EmulateTB(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.4 ADD (immediate, Thumb)
  bool EmulateADDImmThumb(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.5 ADD (immediate, ARM)
  bool EmulateADDImmARM(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.6 ADD (register)
  bool EmulateADDReg(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.7 ADD (register-shifted register)
  bool EmulateADDRegShift(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.97 MOV (register)
  bool EmulateMOVRdRm(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.96 MOV (immediate)
  bool EmulateMOVRdImm(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.35 CMP (immediate)
  bool EmulateCMPImm(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.36 CMP (register)
  bool EmulateCMPReg(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.14 ASR (immediate)
  bool EmulateASRImm(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.15 ASR (register)
  bool EmulateASRReg(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.88 LSL (immediate)
  bool EmulateLSLImm(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.89 LSL (register)
  bool EmulateLSLReg(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.90 LSR (immediate)
  bool EmulateLSRImm(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.91 LSR (register)
  bool EmulateLSRReg(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.139 ROR (immediate)
  bool EmulateRORImm(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.140 ROR (register)
  bool EmulateRORReg(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.141 RRX
  bool EmulateRRX(const uint32_t opcode, const ARMEncoding encoding);

  // Helper method for ASR, LSL, LSR, ROR (immediate), and RRX
  bool EmulateShiftImm(const uint32_t opcode, const ARMEncoding encoding,
                       ARM_ShifterType shift_type);

  // Helper method for ASR, LSL, LSR, and ROR (register)
  bool EmulateShiftReg(const uint32_t opcode, const ARMEncoding encoding,
                       ARM_ShifterType shift_type);

  // LOAD FUNCTIONS

  // A8.6.53 LDM/LDMIA/LDMFD
  bool EmulateLDM(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.54 LDMDA/LDMFA
  bool EmulateLDMDA(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.55 LDMDB/LDMEA
  bool EmulateLDMDB(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.56 LDMIB/LDMED
  bool EmulateLDMIB(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.57 LDR (immediate, Thumb) -- Encoding T1
  bool EmulateLDRRtRnImm(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.58 LDR (immediate, ARM) - Encoding A1
  bool EmulateLDRImmediateARM(const uint32_t opcode,
                              const ARMEncoding encoding);

  // A8.6.59 LDR (literal)
  bool EmulateLDRLiteral(const uint32_t, const ARMEncoding encoding);

  // A8.6.60 LDR (register) - Encoding T1, T2, A1
  bool EmulateLDRRegister(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.61 LDRB (immediate, Thumb) - Encoding T1, T2, T3
  bool EmulateLDRBImmediate(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.62 LDRB (immediate, ARM)
  bool EmulateLDRBImmediateARM(const uint32_t opcode,
                               const ARMEncoding encoding);

  // A8.6.63 LDRB (literal) - Encoding T1, A1
  bool EmulateLDRBLiteral(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.64 LDRB (register) - Encoding T1, T2, A1
  bool EmulateLDRBRegister(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.65 LDRBT
  bool EmulateLDRBT(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.66 LDRD (immediate)
  bool EmulateLDRDImmediate(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.67
  bool EmulateLDRDLiteral(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.68 LDRD (register)
  bool EmulateLDRDRegister(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.69 LDREX
  bool EmulateLDREX(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.70 LDREXB
  bool EmulateLDREXB(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.71 LDREXD
  bool EmulateLDREXD(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.72 LDREXH
  bool EmulateLDREXH(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.73 LDRH (immediate, Thumb) - Encoding T1, T2, T3
  bool EmulateLDRHImmediate(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.74 LDRS (immediate, ARM)
  bool EmulateLDRHImmediateARM(const uint32_t opcode,
                               const ARMEncoding encoding);

  // A8.6.75 LDRH (literal) - Encoding T1, A1
  bool EmulateLDRHLiteral(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.76 LDRH (register) - Encoding T1, T2, A1
  bool EmulateLDRHRegister(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.77 LDRHT
  bool EmulateLDRHT(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.78 LDRSB (immediate) - Encoding T1, T2, A1
  bool EmulateLDRSBImmediate(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.79 LDRSB (literal) - Encoding T1, A1
  bool EmulateLDRSBLiteral(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.80 LDRSB (register) - Encoding T1, T2, A1
  bool EmulateLDRSBRegister(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.81 LDRSBT
  bool EmulateLDRSBT(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.82 LDRSH (immediate) - Encoding T1, T2, A1
  bool EmulateLDRSHImmediate(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.83 LDRSH (literal) - Encoding T1, A1
  bool EmulateLDRSHLiteral(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.84 LDRSH (register) - Encoding T1, T2, A1
  bool EmulateLDRSHRegister(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.85 LDRSHT
  bool EmulateLDRSHT(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.86
  bool EmulateLDRT(const uint32_t opcode, const ARMEncoding encoding);

  // STORE FUNCTIONS

  // A8.6.189 STM/STMIA/STMEA
  bool EmulateSTM(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.190 STMDA/STMED
  bool EmulateSTMDA(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.191 STMDB/STMFD
  bool EmulateSTMDB(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.192 STMIB/STMFA
  bool EmulateSTMIB(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.193 STR (immediate, Thumb)
  bool EmulateSTRThumb(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.194 STR (immediate, ARM)
  bool EmulateSTRImmARM(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.195 STR (register)
  bool EmulateSTRRegister(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.196 STRB (immediate, Thumb)
  bool EmulateSTRBThumb(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.197 STRB (immediate, ARM)
  bool EmulateSTRBImmARM(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.198 STRB (register)
  bool EmulateSTRBReg(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.199 STRBT
  bool EmulateSTRBT(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.200 STRD (immediate)
  bool EmulateSTRDImm(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.201 STRD (register)
  bool EmulateSTRDReg(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.202 STREX
  bool EmulateSTREX(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.203 STREXB
  bool EmulateSTREXB(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.204 STREXD
  bool EmulateSTREXD(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.205 STREXH
  bool EmulateSTREXH(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.206 STRH (immediate, Thumb)
  bool EmulateSTRHImmThumb(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.207 STRH (immediate, ARM)
  bool EmulateSTRHImmARM(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.208 STRH (register)
  bool EmulateSTRHRegister(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.209 STRHT
  bool EmulateSTRHT(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.210 STRT
  bool EmulateSTRT(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.1 ADC (immediate)
  bool EmulateADCImm(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.2 ADC (Register)
  bool EmulateADCReg(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.10 ADR
  bool EmulateADR(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.11 AND (immediate)
  bool EmulateANDImm(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.12 AND (register)
  bool EmulateANDReg(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.19 BIC (immediate)
  bool EmulateBICImm(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.20 BIC (register)
  bool EmulateBICReg(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.26 BXJ
  bool EmulateBXJ(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.32 CMN (immediate)
  bool EmulateCMNImm(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.33 CMN (register)
  bool EmulateCMNReg(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.44 EOR (immediate)
  bool EmulateEORImm(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.45 EOR (register)
  bool EmulateEORReg(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.105 MUL
  bool EmulateMUL(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.106 MVN (immediate)
  bool EmulateMVNImm(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.107 MVN (register)
  bool EmulateMVNReg(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.113 ORR (immediate)
  bool EmulateORRImm(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.114 ORR (register)
  bool EmulateORRReg(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.117 PLD (immediate, literal) - Encoding T1, T2, T3, A1
  bool EmulatePLDImmediate(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.119 PLI (immediate,literal) - Encoding T3, A1
  bool EmulatePLIImmediate(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.120 PLI (register) - Encoding T1, A1
  bool EmulatePLIRegister(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.141 RSB (immediate)
  bool EmulateRSBImm(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.142 RSB (register)
  bool EmulateRSBReg(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.144 RSC (immediate)
  bool EmulateRSCImm(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.145 RSC (register)
  bool EmulateRSCReg(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.150 SBC (immediate)
  bool EmulateSBCImm(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.151 SBC (register)
  bool EmulateSBCReg(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.211 SUB (immediate, Thumb)
  bool EmulateSUBImmThumb(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.212 SUB (immediate, ARM)
  bool EmulateSUBImmARM(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.213 SUB (register)
  bool EmulateSUBReg(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.214 SUB (register-shifted register)
  bool EmulateSUBRegShift(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.222 SXTB  - Encoding T1
  bool EmulateSXTB(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.224 SXTH  - EncodingT1
  bool EmulateSXTH(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.227 TEQ (immediate) - Encoding A1
  bool EmulateTEQImm(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.228 TEQ (register)  - Encoding A1
  bool EmulateTEQReg(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.230 TST (immediate) - Encoding A1
  bool EmulateTSTImm(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.231 TST (register)  - Encoding T1, A1
  bool EmulateTSTReg(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.262 UXTB  - Encoding T1
  bool EmulateUXTB(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.264 UXTH  - Encoding T1
  bool EmulateUXTH(const uint32_t opcode, const ARMEncoding encoding);

  // B6.1.8  RFE
  bool EmulateRFE(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.319 VLDM
  bool EmulateVLDM(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.399 VSTM
  bool EmulateVSTM(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.307 VLD1 (multiple single elements)
  bool EmulateVLD1Multiple(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.308 VLD1 (single element to one lane)
  bool EmulateVLD1Single(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.309 VLD1 (single element to all lanes)
  bool EmulateVLD1SingleAll(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.391 VST1 (multiple single elements)
  bool EmulateVST1Multiple(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.392 VST1 (single element from one lane)
  bool EmulateVST1Single(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.317 VLDR
  bool EmulateVLDR(const uint32_t opcode, const ARMEncoding encoding);

  // A8.6.400 VSTR
  bool EmulateVSTR(const uint32_t opcode, const ARMEncoding encoding);

  // B6.2.13 SUBS PC, LR and related instructions
  bool EmulateSUBSPcLrEtc(const uint32_t opcode, const ARMEncoding encoding);

  uint32_t m_arm_isa;
  Mode m_opcode_mode;
  uint32_t m_opcode_cpsr;
  uint32_t m_new_inst_cpsr; // This can get updated by the opcode.
  ITSession m_it_session;
  bool m_ignore_conditions;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_INSTRUCTION_ARM_EMULATEINSTRUCTIONARM_H
