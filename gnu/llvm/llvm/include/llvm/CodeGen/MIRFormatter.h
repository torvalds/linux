//===-- llvm/CodeGen/MIRFormatter.h -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the MIRFormatter class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MIRFORMATTER_H
#define LLVM_CODEGEN_MIRFORMATTER_H

#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdint>
#include <optional>

namespace llvm {

class MachineFunction;
class MachineInstr;
class ModuleSlotTracker;
struct PerFunctionMIParsingState;
class Twine;
class Value;

/// MIRFormater - Interface to format MIR operand based on target
class MIRFormatter {
public:
  typedef function_ref<bool(StringRef::iterator Loc, const Twine &)>
      ErrorCallbackType;

  MIRFormatter() = default;
  virtual ~MIRFormatter() = default;

  /// Implement target specific printing for machine operand immediate value, so
  /// that we can have more meaningful mnemonic than a 64-bit integer. Passing
  /// std::nullopt to OpIdx means the index is unknown.
  virtual void printImm(raw_ostream &OS, const MachineInstr &MI,
                        std::optional<unsigned> OpIdx, int64_t Imm) const {
    OS << Imm;
  }

  /// Implement target specific parsing of immediate mnemonics. The mnemonic is
  /// dot separated strings.
  virtual bool parseImmMnemonic(const unsigned OpCode, const unsigned OpIdx,
                                StringRef Src, int64_t &Imm,
                                ErrorCallbackType ErrorCallback) const {
    llvm_unreachable("target did not implement parsing MIR immediate mnemonic");
  }

  /// Implement target specific printing of target custom pseudo source value.
  /// Default implementation is not necessarily the correct MIR serialization
  /// format.
  virtual void
  printCustomPseudoSourceValue(raw_ostream &OS, ModuleSlotTracker &MST,
                               const PseudoSourceValue &PSV) const {
    PSV.printCustom(OS);
  }

  /// Implement target specific parsing of target custom pseudo source value.
  virtual bool parseCustomPseudoSourceValue(
      StringRef Src, MachineFunction &MF, PerFunctionMIParsingState &PFS,
      const PseudoSourceValue *&PSV, ErrorCallbackType ErrorCallback) const {
    llvm_unreachable(
        "target did not implement parsing MIR custom pseudo source value");
  }

  /// Helper functions to print IR value as MIR serialization format which will
  /// be useful for target specific printer, e.g. for printing IR value in
  /// custom pseudo source value.
  static void printIRValue(raw_ostream &OS, const Value &V,
                           ModuleSlotTracker &MST);

  /// Helper functions to parse IR value from MIR serialization format which
  /// will be useful for target specific parser, e.g. for parsing IR value for
  /// custom pseudo source value.
  static bool parseIRValue(StringRef Src, MachineFunction &MF,
                           PerFunctionMIParsingState &PFS, const Value *&V,
                           ErrorCallbackType ErrorCallback);
};

} // end namespace llvm

#endif
