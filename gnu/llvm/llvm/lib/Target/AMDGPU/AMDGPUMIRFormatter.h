//===-- llvm/Target/AMDGPU/AMDGPUMIRFormatter.h -----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// AMDGPU specific overrides of MIRFormatter.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPUMIRFORMATTER_H
#define LLVM_LIB_TARGET_AMDGPUMIRFORMATTER_H

#include "llvm/CodeGen/MIRFormatter.h"

namespace llvm {

class MachineFunction;
struct PerFunctionMIParsingState;

class AMDGPUMIRFormatter final : public MIRFormatter {
public:
  AMDGPUMIRFormatter() = default;
  virtual ~AMDGPUMIRFormatter() = default;

  /// Implement target specific printing for machine operand immediate value, so
  /// that we can have more meaningful mnemonic than a 64-bit integer. Passing
  /// None to OpIdx means the index is unknown.
  virtual void printImm(raw_ostream &OS, const MachineInstr &MI,
                        std::optional<unsigned> OpIdx,
                        int64_t Imm) const override;

  /// Implement target specific parsing of immediate mnemonics. The mnemonic is
  /// a string with a leading dot.
  virtual bool parseImmMnemonic(const unsigned OpCode, const unsigned OpIdx,
                                StringRef Src, int64_t &Imm,
                                ErrorCallbackType ErrorCallback) const override;

  /// Implement target specific parsing of target custom pseudo source value.
  bool
  parseCustomPseudoSourceValue(StringRef Src, MachineFunction &MF,
                               PerFunctionMIParsingState &PFS,
                               const PseudoSourceValue *&PSV,
                               ErrorCallbackType ErrorCallback) const override;

private:
  /// Print the string to represent s_delay_alu immediate value
  void printSDelayAluImm(int64_t Imm, llvm::raw_ostream &OS) const;

  /// Parse the immediate pseudo literal for s_delay_alu
  bool parseSDelayAluImmMnemonic(
      const unsigned int OpIdx, int64_t &Imm, llvm::StringRef &Src,
      llvm::MIRFormatter::ErrorCallbackType &ErrorCallback) const;

};

} // end namespace llvm

#endif
