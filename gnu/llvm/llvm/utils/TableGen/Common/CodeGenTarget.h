//===- CodeGenTarget.h - Target Class Wrapper -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines wrappers for the Target class and related global
// functionality.  This makes it easier to access the data and provides a single
// place that needs to check it for validity.  All of these classes abort
// on error conditions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_UTILS_TABLEGEN_CODEGENTARGET_H
#define LLVM_UTILS_TABLEGEN_CODEGENTARGET_H

#include "Basic/SDNodeProperties.h"
#include "CodeGenHwModes.h"
#include "CodeGenInstruction.h"
#include "InfoByHwMode.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include <cassert>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace llvm {

class RecordKeeper;
class Record;
class CodeGenRegBank;
class CodeGenRegister;
class CodeGenRegisterClass;
class CodeGenSchedModels;
class CodeGenSubRegIndex;

/// getValueType - Return the MVT::SimpleValueType that the specified TableGen
/// record corresponds to.
MVT::SimpleValueType getValueType(const Record *Rec);

StringRef getName(MVT::SimpleValueType T);
StringRef getEnumName(MVT::SimpleValueType T);

/// getQualifiedName - Return the name of the specified record, with a
/// namespace qualifier if the record contains one.
std::string getQualifiedName(const Record *R);

/// CodeGenTarget - This class corresponds to the Target class in the .td files.
///
class CodeGenTarget {
  RecordKeeper &Records;
  Record *TargetRec;

  mutable DenseMap<const Record *, std::unique_ptr<CodeGenInstruction>>
      Instructions;
  mutable std::unique_ptr<CodeGenRegBank> RegBank;
  mutable std::vector<Record *> RegAltNameIndices;
  mutable SmallVector<ValueTypeByHwMode, 8> LegalValueTypes;
  CodeGenHwModes CGH;
  std::vector<Record *> MacroFusions;
  mutable bool HasVariableLengthEncodings = false;

  void ReadRegAltNameIndices() const;
  void ReadInstructions() const;
  void ReadLegalValueTypes() const;

  mutable std::unique_ptr<CodeGenSchedModels> SchedModels;

  mutable StringRef InstNamespace;
  mutable std::vector<const CodeGenInstruction *> InstrsByEnum;
  mutable unsigned NumPseudoInstructions = 0;

public:
  CodeGenTarget(RecordKeeper &Records);
  ~CodeGenTarget();

  Record *getTargetRecord() const { return TargetRec; }
  StringRef getName() const;

  /// getInstNamespace - Return the target-specific instruction namespace.
  ///
  StringRef getInstNamespace() const;

  /// getRegNamespace - Return the target-specific register namespace.
  StringRef getRegNamespace() const;

  /// getInstructionSet - Return the InstructionSet object.
  ///
  Record *getInstructionSet() const;

  /// getAllowRegisterRenaming - Return the AllowRegisterRenaming flag value for
  /// this target.
  ///
  bool getAllowRegisterRenaming() const;

  /// getAsmParser - Return the AssemblyParser definition for this target.
  ///
  Record *getAsmParser() const;

  /// getAsmParserVariant - Return the AssemblyParserVariant definition for
  /// this target.
  ///
  Record *getAsmParserVariant(unsigned i) const;

  /// getAsmParserVariantCount - Return the AssemblyParserVariant definition
  /// available for this target.
  ///
  unsigned getAsmParserVariantCount() const;

  /// getAsmWriter - Return the AssemblyWriter definition for this target.
  ///
  Record *getAsmWriter() const;

  /// getRegBank - Return the register bank description.
  CodeGenRegBank &getRegBank() const;

  /// Return the largest register class on \p RegBank which supports \p Ty and
  /// covers \p SubIdx if it exists.
  std::optional<CodeGenRegisterClass *>
  getSuperRegForSubReg(const ValueTypeByHwMode &Ty, CodeGenRegBank &RegBank,
                       const CodeGenSubRegIndex *SubIdx,
                       bool MustBeAllocatable = false) const;

  /// getRegisterByName - If there is a register with the specific AsmName,
  /// return it.
  const CodeGenRegister *getRegisterByName(StringRef Name) const;

  const std::vector<Record *> &getRegAltNameIndices() const {
    if (RegAltNameIndices.empty())
      ReadRegAltNameIndices();
    return RegAltNameIndices;
  }

  const CodeGenRegisterClass &getRegisterClass(Record *R) const;

  /// getRegisterVTs - Find the union of all possible SimpleValueTypes for the
  /// specified physical register.
  std::vector<ValueTypeByHwMode> getRegisterVTs(Record *R) const;

  ArrayRef<ValueTypeByHwMode> getLegalValueTypes() const {
    if (LegalValueTypes.empty())
      ReadLegalValueTypes();
    return LegalValueTypes;
  }

  CodeGenSchedModels &getSchedModels() const;

  const CodeGenHwModes &getHwModes() const { return CGH; }

  bool hasMacroFusion() const { return !MacroFusions.empty(); }

  const std::vector<Record *> getMacroFusions() const { return MacroFusions; }

private:
  DenseMap<const Record *, std::unique_ptr<CodeGenInstruction>> &
  getInstructions() const {
    if (Instructions.empty())
      ReadInstructions();
    return Instructions;
  }

public:
  CodeGenInstruction &getInstruction(const Record *InstRec) const {
    if (Instructions.empty())
      ReadInstructions();
    auto I = Instructions.find(InstRec);
    assert(I != Instructions.end() && "Not an instruction");
    return *I->second;
  }

  /// Returns the number of predefined instructions.
  static unsigned getNumFixedInstructions();

  /// Returns the number of pseudo instructions.
  unsigned getNumPseudoInstructions() const {
    if (InstrsByEnum.empty())
      ComputeInstrsByEnum();
    return NumPseudoInstructions;
  }

  /// Return all of the instructions defined by the target, ordered by their
  /// enum value.
  /// The following order of instructions is also guaranteed:
  /// - fixed / generic instructions as declared in TargetOpcodes.def, in order;
  /// - pseudo instructions in lexicographical order sorted by name;
  /// - other instructions in lexicographical order sorted by name.
  ArrayRef<const CodeGenInstruction *> getInstructionsByEnumValue() const {
    if (InstrsByEnum.empty())
      ComputeInstrsByEnum();
    return InstrsByEnum;
  }

  /// Return the integer enum value corresponding to this instruction record.
  unsigned getInstrIntValue(const Record *R) const {
    if (InstrsByEnum.empty())
      ComputeInstrsByEnum();
    return getInstruction(R).EnumVal;
  }

  typedef ArrayRef<const CodeGenInstruction *>::const_iterator inst_iterator;
  inst_iterator inst_begin() const {
    return getInstructionsByEnumValue().begin();
  }
  inst_iterator inst_end() const { return getInstructionsByEnumValue().end(); }

  /// Return whether instructions have variable length encodings on this target.
  bool hasVariableLengthEncodings() const { return HasVariableLengthEncodings; }

  /// isLittleEndianEncoding - are instruction bit patterns defined as  [0..n]?
  ///
  bool isLittleEndianEncoding() const;

  /// reverseBitsForLittleEndianEncoding - For little-endian instruction bit
  /// encodings, reverse the bit order of all instructions.
  void reverseBitsForLittleEndianEncoding();

  /// guessInstructionProperties - should we just guess unset instruction
  /// properties?
  bool guessInstructionProperties() const;

private:
  void ComputeInstrsByEnum() const;
};

/// ComplexPattern - ComplexPattern info, corresponding to the ComplexPattern
/// tablegen class in TargetSelectionDAG.td
class ComplexPattern {
  Record *Ty;
  unsigned NumOperands;
  std::string SelectFunc;
  std::vector<Record *> RootNodes;
  unsigned Properties; // Node properties
  unsigned Complexity;

public:
  ComplexPattern(Record *R);

  Record *getValueType() const { return Ty; }
  unsigned getNumOperands() const { return NumOperands; }
  const std::string &getSelectFunc() const { return SelectFunc; }
  const std::vector<Record *> &getRootNodes() const { return RootNodes; }
  bool hasProperty(enum SDNP Prop) const { return Properties & (1 << Prop); }
  unsigned getComplexity() const { return Complexity; }
};

} // namespace llvm

#endif
