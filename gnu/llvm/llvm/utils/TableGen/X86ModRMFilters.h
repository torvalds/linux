//===- X86ModRMFilters.h - Disassembler ModR/M filterss ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is part of the X86 Disassembler Emitter.
// It contains ModR/M filters that determine which values of the ModR/M byte
//  are valid for a partiuclar instruction.
// Documentation for the disassembler emitter in general can be found in
//  X86DisassemblerEmitter.h.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_UTILS_TABLEGEN_X86MODRMFILTERS_H
#define LLVM_UTILS_TABLEGEN_X86MODRMFILTERS_H

#include <cstdint>

namespace llvm {

namespace X86Disassembler {

/// ModRMFilter - Abstract base class for clases that recognize patterns in
///   ModR/M bytes.
class ModRMFilter {
  virtual void anchor();

public:
  /// Destructor    - Override as necessary.
  virtual ~ModRMFilter() {}

  /// isDumb        - Indicates whether this filter returns the same value for
  ///                 any value of the ModR/M byte.
  ///
  /// @result       - True if the filter returns the same value for any ModR/M
  ///                 byte; false if not.
  virtual bool isDumb() const { return false; }

  /// accepts       - Indicates whether the filter accepts a particular ModR/M
  ///                 byte value.
  ///
  /// @result       - True if the filter accepts the ModR/M byte; false if not.
  virtual bool accepts(uint8_t modRM) const = 0;
};

/// DumbFilter - Accepts any ModR/M byte.  Used for instructions that do not
///   require a ModR/M byte or instructions where the entire ModR/M byte is used
///   for operands.
class DumbFilter : public ModRMFilter {
  void anchor() override;

public:
  bool isDumb() const override { return true; }

  bool accepts(uint8_t modRM) const override { return true; }
};

/// ModFilter - Filters based on the mod bits [bits 7-6] of the ModR/M byte.
///   Some instructions are classified based on whether they are 11 or anything
///   else.  This filter performs that classification.
class ModFilter : public ModRMFilter {
  void anchor() override;
  bool R;

public:
  /// Constructor
  ///
  /// \param r        True if the mod bits of the ModR/M byte must be 11; false
  ///                 otherwise.  The name r derives from the fact that the mod
  ///                 bits indicate whether the R/M bits [bits 2-0] signify a
  ///                 register or a memory operand.
  ModFilter(bool r) : R(r) {}

  bool accepts(uint8_t modRM) const override {
    return (R == ((modRM & 0xc0) == 0xc0));
  }
};

/// ExtendedFilter - Extended opcodes are classified based on the value of the
///   mod field [bits 7-6] and the value of the nnn field [bits 5-3].
class ExtendedFilter : public ModRMFilter {
  void anchor() override;
  bool R;
  uint8_t NNN;

public:
  /// Constructor
  ///
  /// \param r   True if the mod field must be set to 11; false otherwise.
  ///            The name is explained at ModFilter.
  /// \param nnn The required value of the nnn field.
  ExtendedFilter(bool r, uint8_t nnn) : R(r), NNN(nnn) {}

  bool accepts(uint8_t modRM) const override {
    return (
        ((R && ((modRM & 0xc0) == 0xc0)) || (!R && ((modRM & 0xc0) != 0xc0))) &&
        (((modRM & 0x38) >> 3) == NNN));
  }
};

/// ExtendedRMFilter - Extended opcodes are classified based on the value of the
///   mod field [bits 7-6] and the value of the nnn field [bits 2-0].
class ExtendedRMFilter : public ModRMFilter {
  void anchor() override;
  bool R;
  uint8_t NNN;

public:
  /// Constructor
  ///
  /// \param r   True if the mod field must be set to 11; false otherwise.
  ///            The name is explained at ModFilter.
  /// \param nnn The required value of the nnn field.
  ExtendedRMFilter(bool r, uint8_t nnn) : R(r), NNN(nnn) {}

  bool accepts(uint8_t modRM) const override {
    return ((R && ((modRM & 0xc0) == 0xc0)) && ((modRM & 0x7) == NNN));
  }
};
/// ExactFilter - The occasional extended opcode (such as VMCALL or MONITOR)
///   requires the ModR/M byte to have a specific value.
class ExactFilter : public ModRMFilter {
  void anchor() override;
  uint8_t ModRM;

public:
  /// Constructor
  ///
  /// \param modRM The required value of the full ModR/M byte.
  ExactFilter(uint8_t modRM) : ModRM(modRM) {}

  bool accepts(uint8_t modRM) const override { return (ModRM == modRM); }
};

} // namespace X86Disassembler

} // namespace llvm

#endif
