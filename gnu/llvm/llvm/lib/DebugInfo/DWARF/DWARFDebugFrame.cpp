//===- DWARFDebugFrame.h - Parsing of .debug_frame ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/DWARF/DWARFDebugFrame.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/DIContext.h"
#include "llvm/DebugInfo/DWARF/DWARFDataExtractor.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DataExtractor.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cinttypes>
#include <cstdint>
#include <optional>

using namespace llvm;
using namespace dwarf;

static void printRegister(raw_ostream &OS, DIDumpOptions DumpOpts,
                          unsigned RegNum) {
  if (DumpOpts.GetNameForDWARFReg) {
    auto RegName = DumpOpts.GetNameForDWARFReg(RegNum, DumpOpts.IsEH);
    if (!RegName.empty()) {
      OS << RegName;
      return;
    }
  }
  OS << "reg" << RegNum;
}

UnwindLocation UnwindLocation::createUnspecified() { return {Unspecified}; }

UnwindLocation UnwindLocation::createUndefined() { return {Undefined}; }

UnwindLocation UnwindLocation::createSame() { return {Same}; }

UnwindLocation UnwindLocation::createIsConstant(int32_t Value) {
  return {Constant, InvalidRegisterNumber, Value, std::nullopt, false};
}

UnwindLocation UnwindLocation::createIsCFAPlusOffset(int32_t Offset) {
  return {CFAPlusOffset, InvalidRegisterNumber, Offset, std::nullopt, false};
}

UnwindLocation UnwindLocation::createAtCFAPlusOffset(int32_t Offset) {
  return {CFAPlusOffset, InvalidRegisterNumber, Offset, std::nullopt, true};
}

UnwindLocation
UnwindLocation::createIsRegisterPlusOffset(uint32_t RegNum, int32_t Offset,
                                           std::optional<uint32_t> AddrSpace) {
  return {RegPlusOffset, RegNum, Offset, AddrSpace, false};
}

UnwindLocation
UnwindLocation::createAtRegisterPlusOffset(uint32_t RegNum, int32_t Offset,
                                           std::optional<uint32_t> AddrSpace) {
  return {RegPlusOffset, RegNum, Offset, AddrSpace, true};
}

UnwindLocation UnwindLocation::createIsDWARFExpression(DWARFExpression Expr) {
  return {Expr, false};
}

UnwindLocation UnwindLocation::createAtDWARFExpression(DWARFExpression Expr) {
  return {Expr, true};
}

void UnwindLocation::dump(raw_ostream &OS, DIDumpOptions DumpOpts) const {
  if (Dereference)
    OS << '[';
  switch (Kind) {
  case Unspecified:
    OS << "unspecified";
    break;
  case Undefined:
    OS << "undefined";
    break;
  case Same:
    OS << "same";
    break;
  case CFAPlusOffset:
    OS << "CFA";
    if (Offset == 0)
      break;
    if (Offset > 0)
      OS << "+";
    OS << Offset;
    break;
  case RegPlusOffset:
    printRegister(OS, DumpOpts, RegNum);
    if (Offset == 0 && !AddrSpace)
      break;
    if (Offset >= 0)
      OS << "+";
    OS << Offset;
    if (AddrSpace)
      OS << " in addrspace" << *AddrSpace;
    break;
  case DWARFExpr: {
    Expr->print(OS, DumpOpts, nullptr);
    break;
  }
  case Constant:
    OS << Offset;
    break;
  }
  if (Dereference)
    OS << ']';
}

raw_ostream &llvm::dwarf::operator<<(raw_ostream &OS,
                                     const UnwindLocation &UL) {
  auto DumpOpts = DIDumpOptions();
  UL.dump(OS, DumpOpts);
  return OS;
}

bool UnwindLocation::operator==(const UnwindLocation &RHS) const {
  if (Kind != RHS.Kind)
    return false;
  switch (Kind) {
  case Unspecified:
  case Undefined:
  case Same:
    return true;
  case CFAPlusOffset:
    return Offset == RHS.Offset && Dereference == RHS.Dereference;
  case RegPlusOffset:
    return RegNum == RHS.RegNum && Offset == RHS.Offset &&
           Dereference == RHS.Dereference;
  case DWARFExpr:
    return *Expr == *RHS.Expr && Dereference == RHS.Dereference;
  case Constant:
    return Offset == RHS.Offset;
  }
  return false;
}

void RegisterLocations::dump(raw_ostream &OS, DIDumpOptions DumpOpts) const {
  bool First = true;
  for (const auto &RegLocPair : Locations) {
    if (First)
      First = false;
    else
      OS << ", ";
    printRegister(OS, DumpOpts, RegLocPair.first);
    OS << '=';
    RegLocPair.second.dump(OS, DumpOpts);
  }
}

raw_ostream &llvm::dwarf::operator<<(raw_ostream &OS,
                                     const RegisterLocations &RL) {
  auto DumpOpts = DIDumpOptions();
  RL.dump(OS, DumpOpts);
  return OS;
}

void UnwindRow::dump(raw_ostream &OS, DIDumpOptions DumpOpts,
                     unsigned IndentLevel) const {
  OS.indent(2 * IndentLevel);
  if (hasAddress())
    OS << format("0x%" PRIx64 ": ", *Address);
  OS << "CFA=";
  CFAValue.dump(OS, DumpOpts);
  if (RegLocs.hasLocations()) {
    OS << ": ";
    RegLocs.dump(OS, DumpOpts);
  }
  OS << "\n";
}

raw_ostream &llvm::dwarf::operator<<(raw_ostream &OS, const UnwindRow &Row) {
  auto DumpOpts = DIDumpOptions();
  Row.dump(OS, DumpOpts, 0);
  return OS;
}

void UnwindTable::dump(raw_ostream &OS, DIDumpOptions DumpOpts,
                       unsigned IndentLevel) const {
  for (const UnwindRow &Row : Rows)
    Row.dump(OS, DumpOpts, IndentLevel);
}

raw_ostream &llvm::dwarf::operator<<(raw_ostream &OS, const UnwindTable &Rows) {
  auto DumpOpts = DIDumpOptions();
  Rows.dump(OS, DumpOpts, 0);
  return OS;
}

Expected<UnwindTable> UnwindTable::create(const FDE *Fde) {
  const CIE *Cie = Fde->getLinkedCIE();
  if (Cie == nullptr)
    return createStringError(errc::invalid_argument,
                             "unable to get CIE for FDE at offset 0x%" PRIx64,
                             Fde->getOffset());

  // Rows will be empty if there are no CFI instructions.
  if (Cie->cfis().empty() && Fde->cfis().empty())
    return UnwindTable();

  UnwindTable UT;
  UnwindRow Row;
  Row.setAddress(Fde->getInitialLocation());
  UT.EndAddress = Fde->getInitialLocation() + Fde->getAddressRange();
  if (Error CieError = UT.parseRows(Cie->cfis(), Row, nullptr))
    return std::move(CieError);
  // We need to save the initial locations of registers from the CIE parsing
  // in case we run into DW_CFA_restore or DW_CFA_restore_extended opcodes.
  const RegisterLocations InitialLocs = Row.getRegisterLocations();
  if (Error FdeError = UT.parseRows(Fde->cfis(), Row, &InitialLocs))
    return std::move(FdeError);
  // May be all the CFI instructions were DW_CFA_nop amd Row becomes empty.
  // Do not add that to the unwind table.
  if (Row.getRegisterLocations().hasLocations() ||
      Row.getCFAValue().getLocation() != UnwindLocation::Unspecified)
    UT.Rows.push_back(Row);
  return UT;
}

Expected<UnwindTable> UnwindTable::create(const CIE *Cie) {
  // Rows will be empty if there are no CFI instructions.
  if (Cie->cfis().empty())
    return UnwindTable();

  UnwindTable UT;
  UnwindRow Row;
  if (Error CieError = UT.parseRows(Cie->cfis(), Row, nullptr))
    return std::move(CieError);
  // May be all the CFI instructions were DW_CFA_nop amd Row becomes empty.
  // Do not add that to the unwind table.
  if (Row.getRegisterLocations().hasLocations() ||
      Row.getCFAValue().getLocation() != UnwindLocation::Unspecified)
    UT.Rows.push_back(Row);
  return UT;
}

// See DWARF standard v3, section 7.23
const uint8_t DWARF_CFI_PRIMARY_OPCODE_MASK = 0xc0;
const uint8_t DWARF_CFI_PRIMARY_OPERAND_MASK = 0x3f;

Error CFIProgram::parse(DWARFDataExtractor Data, uint64_t *Offset,
                        uint64_t EndOffset) {
  DataExtractor::Cursor C(*Offset);
  while (C && C.tell() < EndOffset) {
    uint8_t Opcode = Data.getRelocatedValue(C, 1);
    if (!C)
      break;

    // Some instructions have a primary opcode encoded in the top bits.
    if (uint8_t Primary = Opcode & DWARF_CFI_PRIMARY_OPCODE_MASK) {
      // If it's a primary opcode, the first operand is encoded in the bottom
      // bits of the opcode itself.
      uint64_t Op1 = Opcode & DWARF_CFI_PRIMARY_OPERAND_MASK;
      switch (Primary) {
      case DW_CFA_advance_loc:
      case DW_CFA_restore:
        addInstruction(Primary, Op1);
        break;
      case DW_CFA_offset:
        addInstruction(Primary, Op1, Data.getULEB128(C));
        break;
      default:
        llvm_unreachable("invalid primary CFI opcode");
      }
      continue;
    }

    // Extended opcode - its value is Opcode itself.
    switch (Opcode) {
    default:
      return createStringError(errc::illegal_byte_sequence,
                               "invalid extended CFI opcode 0x%" PRIx8, Opcode);
    case DW_CFA_nop:
    case DW_CFA_remember_state:
    case DW_CFA_restore_state:
    case DW_CFA_GNU_window_save:
      // No operands
      addInstruction(Opcode);
      break;
    case DW_CFA_set_loc:
      // Operands: Address
      addInstruction(Opcode, Data.getRelocatedAddress(C));
      break;
    case DW_CFA_advance_loc1:
      // Operands: 1-byte delta
      addInstruction(Opcode, Data.getRelocatedValue(C, 1));
      break;
    case DW_CFA_advance_loc2:
      // Operands: 2-byte delta
      addInstruction(Opcode, Data.getRelocatedValue(C, 2));
      break;
    case DW_CFA_advance_loc4:
      // Operands: 4-byte delta
      addInstruction(Opcode, Data.getRelocatedValue(C, 4));
      break;
    case DW_CFA_restore_extended:
    case DW_CFA_undefined:
    case DW_CFA_same_value:
    case DW_CFA_def_cfa_register:
    case DW_CFA_def_cfa_offset:
    case DW_CFA_GNU_args_size:
      // Operands: ULEB128
      addInstruction(Opcode, Data.getULEB128(C));
      break;
    case DW_CFA_def_cfa_offset_sf:
      // Operands: SLEB128
      addInstruction(Opcode, Data.getSLEB128(C));
      break;
    case DW_CFA_LLVM_def_aspace_cfa:
    case DW_CFA_LLVM_def_aspace_cfa_sf: {
      auto RegNum = Data.getULEB128(C);
      auto CfaOffset = Opcode == DW_CFA_LLVM_def_aspace_cfa
                           ? Data.getULEB128(C)
                           : Data.getSLEB128(C);
      auto AddressSpace = Data.getULEB128(C);
      addInstruction(Opcode, RegNum, CfaOffset, AddressSpace);
      break;
    }
    case DW_CFA_offset_extended:
    case DW_CFA_register:
    case DW_CFA_def_cfa:
    case DW_CFA_val_offset: {
      // Operands: ULEB128, ULEB128
      // Note: We can not embed getULEB128 directly into function
      // argument list. getULEB128 changes Offset and order of evaluation
      // for arguments is unspecified.
      uint64_t op1 = Data.getULEB128(C);
      uint64_t op2 = Data.getULEB128(C);
      addInstruction(Opcode, op1, op2);
      break;
    }
    case DW_CFA_offset_extended_sf:
    case DW_CFA_def_cfa_sf:
    case DW_CFA_val_offset_sf: {
      // Operands: ULEB128, SLEB128
      // Note: see comment for the previous case
      uint64_t op1 = Data.getULEB128(C);
      uint64_t op2 = (uint64_t)Data.getSLEB128(C);
      addInstruction(Opcode, op1, op2);
      break;
    }
    case DW_CFA_def_cfa_expression: {
      uint64_t ExprLength = Data.getULEB128(C);
      addInstruction(Opcode, 0);
      StringRef Expression = Data.getBytes(C, ExprLength);

      DataExtractor Extractor(Expression, Data.isLittleEndian(),
                              Data.getAddressSize());
      // Note. We do not pass the DWARF format to DWARFExpression, because
      // DW_OP_call_ref, the only operation which depends on the format, is
      // prohibited in call frame instructions, see sec. 6.4.2 in DWARFv5.
      Instructions.back().Expression =
          DWARFExpression(Extractor, Data.getAddressSize());
      break;
    }
    case DW_CFA_expression:
    case DW_CFA_val_expression: {
      uint64_t RegNum = Data.getULEB128(C);
      addInstruction(Opcode, RegNum, 0);

      uint64_t BlockLength = Data.getULEB128(C);
      StringRef Expression = Data.getBytes(C, BlockLength);
      DataExtractor Extractor(Expression, Data.isLittleEndian(),
                              Data.getAddressSize());
      // Note. We do not pass the DWARF format to DWARFExpression, because
      // DW_OP_call_ref, the only operation which depends on the format, is
      // prohibited in call frame instructions, see sec. 6.4.2 in DWARFv5.
      Instructions.back().Expression =
          DWARFExpression(Extractor, Data.getAddressSize());
      break;
    }
    }
  }

  *Offset = C.tell();
  return C.takeError();
}

StringRef CFIProgram::callFrameString(unsigned Opcode) const {
  return dwarf::CallFrameString(Opcode, Arch);
}

const char *CFIProgram::operandTypeString(CFIProgram::OperandType OT) {
#define ENUM_TO_CSTR(e)                                                        \
  case e:                                                                      \
    return #e;
  switch (OT) {
    ENUM_TO_CSTR(OT_Unset);
    ENUM_TO_CSTR(OT_None);
    ENUM_TO_CSTR(OT_Address);
    ENUM_TO_CSTR(OT_Offset);
    ENUM_TO_CSTR(OT_FactoredCodeOffset);
    ENUM_TO_CSTR(OT_SignedFactDataOffset);
    ENUM_TO_CSTR(OT_UnsignedFactDataOffset);
    ENUM_TO_CSTR(OT_Register);
    ENUM_TO_CSTR(OT_AddressSpace);
    ENUM_TO_CSTR(OT_Expression);
  }
  return "<unknown CFIProgram::OperandType>";
}

llvm::Expected<uint64_t>
CFIProgram::Instruction::getOperandAsUnsigned(const CFIProgram &CFIP,
                                              uint32_t OperandIdx) const {
  if (OperandIdx >= MaxOperands)
    return createStringError(errc::invalid_argument,
                             "operand index %" PRIu32 " is not valid",
                             OperandIdx);
  OperandType Type = CFIP.getOperandTypes()[Opcode][OperandIdx];
  uint64_t Operand = Ops[OperandIdx];
  switch (Type) {
  case OT_Unset:
  case OT_None:
  case OT_Expression:
    return createStringError(errc::invalid_argument,
                             "op[%" PRIu32 "] has type %s which has no value",
                             OperandIdx, CFIProgram::operandTypeString(Type));

  case OT_Offset:
  case OT_SignedFactDataOffset:
  case OT_UnsignedFactDataOffset:
    return createStringError(
        errc::invalid_argument,
        "op[%" PRIu32 "] has OperandType OT_Offset which produces a signed "
        "result, call getOperandAsSigned instead",
        OperandIdx);

  case OT_Address:
  case OT_Register:
  case OT_AddressSpace:
    return Operand;

  case OT_FactoredCodeOffset: {
    const uint64_t CodeAlignmentFactor = CFIP.codeAlign();
    if (CodeAlignmentFactor == 0)
      return createStringError(
          errc::invalid_argument,
          "op[%" PRIu32 "] has type OT_FactoredCodeOffset but code alignment "
          "is zero",
          OperandIdx);
    return Operand * CodeAlignmentFactor;
  }
  }
  llvm_unreachable("invalid operand type");
}

llvm::Expected<int64_t>
CFIProgram::Instruction::getOperandAsSigned(const CFIProgram &CFIP,
                                            uint32_t OperandIdx) const {
  if (OperandIdx >= MaxOperands)
    return createStringError(errc::invalid_argument,
                             "operand index %" PRIu32 " is not valid",
                             OperandIdx);
  OperandType Type = CFIP.getOperandTypes()[Opcode][OperandIdx];
  uint64_t Operand = Ops[OperandIdx];
  switch (Type) {
  case OT_Unset:
  case OT_None:
  case OT_Expression:
    return createStringError(errc::invalid_argument,
                             "op[%" PRIu32 "] has type %s which has no value",
                             OperandIdx, CFIProgram::operandTypeString(Type));

  case OT_Address:
  case OT_Register:
  case OT_AddressSpace:
    return createStringError(
        errc::invalid_argument,
        "op[%" PRIu32 "] has OperandType %s which produces an unsigned result, "
        "call getOperandAsUnsigned instead",
        OperandIdx, CFIProgram::operandTypeString(Type));

  case OT_Offset:
    return (int64_t)Operand;

  case OT_FactoredCodeOffset:
  case OT_SignedFactDataOffset: {
    const int64_t DataAlignmentFactor = CFIP.dataAlign();
    if (DataAlignmentFactor == 0)
      return createStringError(errc::invalid_argument,
                               "op[%" PRIu32 "] has type %s but data "
                               "alignment is zero",
                               OperandIdx, CFIProgram::operandTypeString(Type));
    return int64_t(Operand) * DataAlignmentFactor;
  }

  case OT_UnsignedFactDataOffset: {
    const int64_t DataAlignmentFactor = CFIP.dataAlign();
    if (DataAlignmentFactor == 0)
      return createStringError(errc::invalid_argument,
                               "op[%" PRIu32
                               "] has type OT_UnsignedFactDataOffset but data "
                               "alignment is zero",
                               OperandIdx);
    return Operand * DataAlignmentFactor;
  }
  }
  llvm_unreachable("invalid operand type");
}

Error UnwindTable::parseRows(const CFIProgram &CFIP, UnwindRow &Row,
                             const RegisterLocations *InitialLocs) {
  // State consists of CFA value and register locations.
  std::vector<std::pair<UnwindLocation, RegisterLocations>> States;
  for (const CFIProgram::Instruction &Inst : CFIP) {
    switch (Inst.Opcode) {
    case dwarf::DW_CFA_set_loc: {
      // The DW_CFA_set_loc instruction takes a single operand that
      // represents a target address. The required action is to create a new
      // table row using the specified address as the location. All other
      // values in the new row are initially identical to the current row.
      // The new location value is always greater than the current one. If
      // the segment_size field of this FDE's CIE is non- zero, the initial
      // location is preceded by a segment selector of the given length
      llvm::Expected<uint64_t> NewAddress = Inst.getOperandAsUnsigned(CFIP, 0);
      if (!NewAddress)
        return NewAddress.takeError();
      if (*NewAddress <= Row.getAddress())
        return createStringError(
            errc::invalid_argument,
            "%s with adrress 0x%" PRIx64 " which must be greater than the "
            "current row address 0x%" PRIx64,
            CFIP.callFrameString(Inst.Opcode).str().c_str(), *NewAddress,
            Row.getAddress());
      Rows.push_back(Row);
      Row.setAddress(*NewAddress);
      break;
    }

    case dwarf::DW_CFA_advance_loc:
    case dwarf::DW_CFA_advance_loc1:
    case dwarf::DW_CFA_advance_loc2:
    case dwarf::DW_CFA_advance_loc4: {
      // The DW_CFA_advance instruction takes a single operand that
      // represents a constant delta. The required action is to create a new
      // table row with a location value that is computed by taking the
      // current entry’s location value and adding the value of delta *
      // code_alignment_factor. All other values in the new row are initially
      // identical to the current row.
      Rows.push_back(Row);
      llvm::Expected<uint64_t> Offset = Inst.getOperandAsUnsigned(CFIP, 0);
      if (!Offset)
        return Offset.takeError();
      Row.slideAddress(*Offset);
      break;
    }

    case dwarf::DW_CFA_restore:
    case dwarf::DW_CFA_restore_extended: {
      // The DW_CFA_restore instruction takes a single operand (encoded with
      // the opcode) that represents a register number. The required action
      // is to change the rule for the indicated register to the rule
      // assigned it by the initial_instructions in the CIE.
      if (InitialLocs == nullptr)
        return createStringError(
            errc::invalid_argument, "%s encountered while parsing a CIE",
            CFIP.callFrameString(Inst.Opcode).str().c_str());
      llvm::Expected<uint64_t> RegNum = Inst.getOperandAsUnsigned(CFIP, 0);
      if (!RegNum)
        return RegNum.takeError();
      if (std::optional<UnwindLocation> O =
              InitialLocs->getRegisterLocation(*RegNum))
        Row.getRegisterLocations().setRegisterLocation(*RegNum, *O);
      else
        Row.getRegisterLocations().removeRegisterLocation(*RegNum);
      break;
    }

    case dwarf::DW_CFA_offset:
    case dwarf::DW_CFA_offset_extended:
    case dwarf::DW_CFA_offset_extended_sf: {
      llvm::Expected<uint64_t> RegNum = Inst.getOperandAsUnsigned(CFIP, 0);
      if (!RegNum)
        return RegNum.takeError();
      llvm::Expected<int64_t> Offset = Inst.getOperandAsSigned(CFIP, 1);
      if (!Offset)
        return Offset.takeError();
      Row.getRegisterLocations().setRegisterLocation(
          *RegNum, UnwindLocation::createAtCFAPlusOffset(*Offset));
      break;
    }

    case dwarf::DW_CFA_nop:
      break;

    case dwarf::DW_CFA_remember_state:
      States.push_back(
          std::make_pair(Row.getCFAValue(), Row.getRegisterLocations()));
      break;

    case dwarf::DW_CFA_restore_state:
      if (States.empty())
        return createStringError(errc::invalid_argument,
                                 "DW_CFA_restore_state without a matching "
                                 "previous DW_CFA_remember_state");
      Row.getCFAValue() = States.back().first;
      Row.getRegisterLocations() = States.back().second;
      States.pop_back();
      break;

    case dwarf::DW_CFA_GNU_window_save:
      switch (CFIP.triple()) {
      case Triple::aarch64:
      case Triple::aarch64_be:
      case Triple::aarch64_32: {
        // DW_CFA_GNU_window_save is used for different things on different
        // architectures. For aarch64 it is known as
        // DW_CFA_AARCH64_negate_ra_state. The action is to toggle the
        // value of the return address state between 1 and 0. If there is
        // no rule for the AARCH64_DWARF_PAUTH_RA_STATE register, then it
        // should be initially set to 1.
        constexpr uint32_t AArch64DWARFPAuthRaState = 34;
        auto LRLoc = Row.getRegisterLocations().getRegisterLocation(
            AArch64DWARFPAuthRaState);
        if (LRLoc) {
          if (LRLoc->getLocation() == UnwindLocation::Constant) {
            // Toggle the constant value from 0 to 1 or 1 to 0.
            LRLoc->setConstant(LRLoc->getConstant() ^ 1);
            Row.getRegisterLocations().setRegisterLocation(
                AArch64DWARFPAuthRaState, *LRLoc);
          } else {
            return createStringError(
                errc::invalid_argument,
                "%s encountered when existing rule for this register is not "
                "a constant",
                CFIP.callFrameString(Inst.Opcode).str().c_str());
          }
        } else {
          Row.getRegisterLocations().setRegisterLocation(
              AArch64DWARFPAuthRaState, UnwindLocation::createIsConstant(1));
        }
        break;
      }

      case Triple::sparc:
      case Triple::sparcv9:
      case Triple::sparcel:
        for (uint32_t RegNum = 16; RegNum < 32; ++RegNum) {
          Row.getRegisterLocations().setRegisterLocation(
              RegNum, UnwindLocation::createAtCFAPlusOffset((RegNum - 16) * 8));
        }
        break;

      default: {
        return createStringError(
            errc::not_supported,
            "DW_CFA opcode %#x is not supported for architecture %s",
            Inst.Opcode, Triple::getArchTypeName(CFIP.triple()).str().c_str());

        break;
      }
      }
      break;

    case dwarf::DW_CFA_undefined: {
      llvm::Expected<uint64_t> RegNum = Inst.getOperandAsUnsigned(CFIP, 0);
      if (!RegNum)
        return RegNum.takeError();
      Row.getRegisterLocations().setRegisterLocation(
          *RegNum, UnwindLocation::createUndefined());
      break;
    }

    case dwarf::DW_CFA_same_value: {
      llvm::Expected<uint64_t> RegNum = Inst.getOperandAsUnsigned(CFIP, 0);
      if (!RegNum)
        return RegNum.takeError();
      Row.getRegisterLocations().setRegisterLocation(
          *RegNum, UnwindLocation::createSame());
      break;
    }

    case dwarf::DW_CFA_GNU_args_size:
      break;

    case dwarf::DW_CFA_register: {
      llvm::Expected<uint64_t> RegNum = Inst.getOperandAsUnsigned(CFIP, 0);
      if (!RegNum)
        return RegNum.takeError();
      llvm::Expected<uint64_t> NewRegNum = Inst.getOperandAsUnsigned(CFIP, 1);
      if (!NewRegNum)
        return NewRegNum.takeError();
      Row.getRegisterLocations().setRegisterLocation(
          *RegNum, UnwindLocation::createIsRegisterPlusOffset(*NewRegNum, 0));
      break;
    }

    case dwarf::DW_CFA_val_offset:
    case dwarf::DW_CFA_val_offset_sf: {
      llvm::Expected<uint64_t> RegNum = Inst.getOperandAsUnsigned(CFIP, 0);
      if (!RegNum)
        return RegNum.takeError();
      llvm::Expected<int64_t> Offset = Inst.getOperandAsSigned(CFIP, 1);
      if (!Offset)
        return Offset.takeError();
      Row.getRegisterLocations().setRegisterLocation(
          *RegNum, UnwindLocation::createIsCFAPlusOffset(*Offset));
      break;
    }

    case dwarf::DW_CFA_expression: {
      llvm::Expected<uint64_t> RegNum = Inst.getOperandAsUnsigned(CFIP, 0);
      if (!RegNum)
        return RegNum.takeError();
      Row.getRegisterLocations().setRegisterLocation(
          *RegNum, UnwindLocation::createAtDWARFExpression(*Inst.Expression));
      break;
    }

    case dwarf::DW_CFA_val_expression: {
      llvm::Expected<uint64_t> RegNum = Inst.getOperandAsUnsigned(CFIP, 0);
      if (!RegNum)
        return RegNum.takeError();
      Row.getRegisterLocations().setRegisterLocation(
          *RegNum, UnwindLocation::createIsDWARFExpression(*Inst.Expression));
      break;
    }

    case dwarf::DW_CFA_def_cfa_register: {
      llvm::Expected<uint64_t> RegNum = Inst.getOperandAsUnsigned(CFIP, 0);
      if (!RegNum)
        return RegNum.takeError();
      if (Row.getCFAValue().getLocation() != UnwindLocation::RegPlusOffset)
        Row.getCFAValue() =
            UnwindLocation::createIsRegisterPlusOffset(*RegNum, 0);
      else
        Row.getCFAValue().setRegister(*RegNum);
      break;
    }

    case dwarf::DW_CFA_def_cfa_offset:
    case dwarf::DW_CFA_def_cfa_offset_sf: {
      llvm::Expected<int64_t> Offset = Inst.getOperandAsSigned(CFIP, 0);
      if (!Offset)
        return Offset.takeError();
      if (Row.getCFAValue().getLocation() != UnwindLocation::RegPlusOffset) {
        return createStringError(
            errc::invalid_argument,
            "%s found when CFA rule was not RegPlusOffset",
            CFIP.callFrameString(Inst.Opcode).str().c_str());
      }
      Row.getCFAValue().setOffset(*Offset);
      break;
    }

    case dwarf::DW_CFA_def_cfa:
    case dwarf::DW_CFA_def_cfa_sf: {
      llvm::Expected<uint64_t> RegNum = Inst.getOperandAsUnsigned(CFIP, 0);
      if (!RegNum)
        return RegNum.takeError();
      llvm::Expected<int64_t> Offset = Inst.getOperandAsSigned(CFIP, 1);
      if (!Offset)
        return Offset.takeError();
      Row.getCFAValue() =
          UnwindLocation::createIsRegisterPlusOffset(*RegNum, *Offset);
      break;
    }

    case dwarf::DW_CFA_LLVM_def_aspace_cfa:
    case dwarf::DW_CFA_LLVM_def_aspace_cfa_sf: {
      llvm::Expected<uint64_t> RegNum = Inst.getOperandAsUnsigned(CFIP, 0);
      if (!RegNum)
        return RegNum.takeError();
      llvm::Expected<int64_t> Offset = Inst.getOperandAsSigned(CFIP, 1);
      if (!Offset)
        return Offset.takeError();
      llvm::Expected<uint32_t> CFAAddrSpace =
          Inst.getOperandAsUnsigned(CFIP, 2);
      if (!CFAAddrSpace)
        return CFAAddrSpace.takeError();
      Row.getCFAValue() = UnwindLocation::createIsRegisterPlusOffset(
          *RegNum, *Offset, *CFAAddrSpace);
      break;
    }

    case dwarf::DW_CFA_def_cfa_expression:
      Row.getCFAValue() =
          UnwindLocation::createIsDWARFExpression(*Inst.Expression);
      break;
    }
  }
  return Error::success();
}

ArrayRef<CFIProgram::OperandType[CFIProgram::MaxOperands]>
CFIProgram::getOperandTypes() {
  static OperandType OpTypes[DW_CFA_restore + 1][MaxOperands];
  static bool Initialized = false;
  if (Initialized) {
    return ArrayRef<OperandType[MaxOperands]>(&OpTypes[0], DW_CFA_restore + 1);
  }
  Initialized = true;

#define DECLARE_OP3(OP, OPTYPE0, OPTYPE1, OPTYPE2)                             \
  do {                                                                         \
    OpTypes[OP][0] = OPTYPE0;                                                  \
    OpTypes[OP][1] = OPTYPE1;                                                  \
    OpTypes[OP][2] = OPTYPE2;                                                  \
  } while (false)
#define DECLARE_OP2(OP, OPTYPE0, OPTYPE1)                                      \
  DECLARE_OP3(OP, OPTYPE0, OPTYPE1, OT_None)
#define DECLARE_OP1(OP, OPTYPE0) DECLARE_OP2(OP, OPTYPE0, OT_None)
#define DECLARE_OP0(OP) DECLARE_OP1(OP, OT_None)

  DECLARE_OP1(DW_CFA_set_loc, OT_Address);
  DECLARE_OP1(DW_CFA_advance_loc, OT_FactoredCodeOffset);
  DECLARE_OP1(DW_CFA_advance_loc1, OT_FactoredCodeOffset);
  DECLARE_OP1(DW_CFA_advance_loc2, OT_FactoredCodeOffset);
  DECLARE_OP1(DW_CFA_advance_loc4, OT_FactoredCodeOffset);
  DECLARE_OP1(DW_CFA_MIPS_advance_loc8, OT_FactoredCodeOffset);
  DECLARE_OP2(DW_CFA_def_cfa, OT_Register, OT_Offset);
  DECLARE_OP2(DW_CFA_def_cfa_sf, OT_Register, OT_SignedFactDataOffset);
  DECLARE_OP1(DW_CFA_def_cfa_register, OT_Register);
  DECLARE_OP3(DW_CFA_LLVM_def_aspace_cfa, OT_Register, OT_Offset,
              OT_AddressSpace);
  DECLARE_OP3(DW_CFA_LLVM_def_aspace_cfa_sf, OT_Register,
              OT_SignedFactDataOffset, OT_AddressSpace);
  DECLARE_OP1(DW_CFA_def_cfa_offset, OT_Offset);
  DECLARE_OP1(DW_CFA_def_cfa_offset_sf, OT_SignedFactDataOffset);
  DECLARE_OP1(DW_CFA_def_cfa_expression, OT_Expression);
  DECLARE_OP1(DW_CFA_undefined, OT_Register);
  DECLARE_OP1(DW_CFA_same_value, OT_Register);
  DECLARE_OP2(DW_CFA_offset, OT_Register, OT_UnsignedFactDataOffset);
  DECLARE_OP2(DW_CFA_offset_extended, OT_Register, OT_UnsignedFactDataOffset);
  DECLARE_OP2(DW_CFA_offset_extended_sf, OT_Register, OT_SignedFactDataOffset);
  DECLARE_OP2(DW_CFA_val_offset, OT_Register, OT_UnsignedFactDataOffset);
  DECLARE_OP2(DW_CFA_val_offset_sf, OT_Register, OT_SignedFactDataOffset);
  DECLARE_OP2(DW_CFA_register, OT_Register, OT_Register);
  DECLARE_OP2(DW_CFA_expression, OT_Register, OT_Expression);
  DECLARE_OP2(DW_CFA_val_expression, OT_Register, OT_Expression);
  DECLARE_OP1(DW_CFA_restore, OT_Register);
  DECLARE_OP1(DW_CFA_restore_extended, OT_Register);
  DECLARE_OP0(DW_CFA_remember_state);
  DECLARE_OP0(DW_CFA_restore_state);
  DECLARE_OP0(DW_CFA_GNU_window_save);
  DECLARE_OP1(DW_CFA_GNU_args_size, OT_Offset);
  DECLARE_OP0(DW_CFA_nop);

#undef DECLARE_OP0
#undef DECLARE_OP1
#undef DECLARE_OP2

  return ArrayRef<OperandType[MaxOperands]>(&OpTypes[0], DW_CFA_restore + 1);
}

/// Print \p Opcode's operand number \p OperandIdx which has value \p Operand.
void CFIProgram::printOperand(raw_ostream &OS, DIDumpOptions DumpOpts,
                              const Instruction &Instr, unsigned OperandIdx,
                              uint64_t Operand,
                              std::optional<uint64_t> &Address) const {
  assert(OperandIdx < MaxOperands);
  uint8_t Opcode = Instr.Opcode;
  OperandType Type = getOperandTypes()[Opcode][OperandIdx];

  switch (Type) {
  case OT_Unset: {
    OS << " Unsupported " << (OperandIdx ? "second" : "first") << " operand to";
    auto OpcodeName = callFrameString(Opcode);
    if (!OpcodeName.empty())
      OS << " " << OpcodeName;
    else
      OS << format(" Opcode %x",  Opcode);
    break;
  }
  case OT_None:
    break;
  case OT_Address:
    OS << format(" %" PRIx64, Operand);
    Address = Operand;
    break;
  case OT_Offset:
    // The offsets are all encoded in a unsigned form, but in practice
    // consumers use them signed. It's most certainly legacy due to
    // the lack of signed variants in the first Dwarf standards.
    OS << format(" %+" PRId64, int64_t(Operand));
    break;
  case OT_FactoredCodeOffset: // Always Unsigned
    if (CodeAlignmentFactor)
      OS << format(" %" PRId64, Operand * CodeAlignmentFactor);
    else
      OS << format(" %" PRId64 "*code_alignment_factor", Operand);
    if (Address && CodeAlignmentFactor) {
      *Address += Operand * CodeAlignmentFactor;
      OS << format(" to 0x%" PRIx64, *Address);
    }
    break;
  case OT_SignedFactDataOffset:
    if (DataAlignmentFactor)
      OS << format(" %" PRId64, int64_t(Operand) * DataAlignmentFactor);
    else
      OS << format(" %" PRId64 "*data_alignment_factor" , int64_t(Operand));
    break;
  case OT_UnsignedFactDataOffset:
    if (DataAlignmentFactor)
      OS << format(" %" PRId64, Operand * DataAlignmentFactor);
    else
      OS << format(" %" PRId64 "*data_alignment_factor" , Operand);
    break;
  case OT_Register:
    OS << ' ';
    printRegister(OS, DumpOpts, Operand);
    break;
  case OT_AddressSpace:
    OS << format(" in addrspace%" PRId64, Operand);
    break;
  case OT_Expression:
    assert(Instr.Expression && "missing DWARFExpression object");
    OS << " ";
    Instr.Expression->print(OS, DumpOpts, nullptr);
    break;
  }
}

void CFIProgram::dump(raw_ostream &OS, DIDumpOptions DumpOpts,
                      unsigned IndentLevel,
                      std::optional<uint64_t> Address) const {
  for (const auto &Instr : Instructions) {
    uint8_t Opcode = Instr.Opcode;
    OS.indent(2 * IndentLevel);
    OS << callFrameString(Opcode) << ":";
    for (unsigned i = 0; i < Instr.Ops.size(); ++i)
      printOperand(OS, DumpOpts, Instr, i, Instr.Ops[i], Address);
    OS << '\n';
  }
}

// Returns the CIE identifier to be used by the requested format.
// CIE ids for .debug_frame sections are defined in Section 7.24 of DWARFv5.
// For CIE ID in .eh_frame sections see
// https://refspecs.linuxfoundation.org/LSB_5.0.0/LSB-Core-generic/LSB-Core-generic/ehframechpt.html
constexpr uint64_t getCIEId(bool IsDWARF64, bool IsEH) {
  if (IsEH)
    return 0;
  if (IsDWARF64)
    return DW64_CIE_ID;
  return DW_CIE_ID;
}

void CIE::dump(raw_ostream &OS, DIDumpOptions DumpOpts) const {
  // A CIE with a zero length is a terminator entry in the .eh_frame section.
  if (DumpOpts.IsEH && Length == 0) {
    OS << format("%08" PRIx64, Offset) << " ZERO terminator\n";
    return;
  }

  OS << format("%08" PRIx64, Offset)
     << format(" %0*" PRIx64, IsDWARF64 ? 16 : 8, Length)
     << format(" %0*" PRIx64, IsDWARF64 && !DumpOpts.IsEH ? 16 : 8,
               getCIEId(IsDWARF64, DumpOpts.IsEH))
     << " CIE\n"
     << "  Format:                " << FormatString(IsDWARF64) << "\n";
  if (DumpOpts.IsEH && Version != 1)
    OS << "WARNING: unsupported CIE version\n";
  OS << format("  Version:               %d\n", Version)
     << "  Augmentation:          \"" << Augmentation << "\"\n";
  if (Version >= 4) {
    OS << format("  Address size:          %u\n", (uint32_t)AddressSize);
    OS << format("  Segment desc size:     %u\n",
                 (uint32_t)SegmentDescriptorSize);
  }
  OS << format("  Code alignment factor: %u\n", (uint32_t)CodeAlignmentFactor);
  OS << format("  Data alignment factor: %d\n", (int32_t)DataAlignmentFactor);
  OS << format("  Return address column: %d\n", (int32_t)ReturnAddressRegister);
  if (Personality)
    OS << format("  Personality Address: %016" PRIx64 "\n", *Personality);
  if (!AugmentationData.empty()) {
    OS << "  Augmentation data:    ";
    for (uint8_t Byte : AugmentationData)
      OS << ' ' << hexdigit(Byte >> 4) << hexdigit(Byte & 0xf);
    OS << "\n";
  }
  OS << "\n";
  CFIs.dump(OS, DumpOpts, /*IndentLevel=*/1, /*InitialLocation=*/{});
  OS << "\n";

  if (Expected<UnwindTable> RowsOrErr = UnwindTable::create(this))
    RowsOrErr->dump(OS, DumpOpts, 1);
  else {
    DumpOpts.RecoverableErrorHandler(joinErrors(
        createStringError(errc::invalid_argument,
                          "decoding the CIE opcodes into rows failed"),
        RowsOrErr.takeError()));
  }
  OS << "\n";
}

void FDE::dump(raw_ostream &OS, DIDumpOptions DumpOpts) const {
  OS << format("%08" PRIx64, Offset)
     << format(" %0*" PRIx64, IsDWARF64 ? 16 : 8, Length)
     << format(" %0*" PRIx64, IsDWARF64 && !DumpOpts.IsEH ? 16 : 8, CIEPointer)
     << " FDE cie=";
  if (LinkedCIE)
    OS << format("%08" PRIx64, LinkedCIE->getOffset());
  else
    OS << "<invalid offset>";
  OS << format(" pc=%08" PRIx64 "...%08" PRIx64 "\n", InitialLocation,
               InitialLocation + AddressRange);
  OS << "  Format:       " << FormatString(IsDWARF64) << "\n";
  if (LSDAAddress)
    OS << format("  LSDA Address: %016" PRIx64 "\n", *LSDAAddress);
  CFIs.dump(OS, DumpOpts, /*IndentLevel=*/1, InitialLocation);
  OS << "\n";

  if (Expected<UnwindTable> RowsOrErr = UnwindTable::create(this))
    RowsOrErr->dump(OS, DumpOpts, 1);
  else {
    DumpOpts.RecoverableErrorHandler(joinErrors(
        createStringError(errc::invalid_argument,
                          "decoding the FDE opcodes into rows failed"),
        RowsOrErr.takeError()));
  }
  OS << "\n";
}

DWARFDebugFrame::DWARFDebugFrame(Triple::ArchType Arch,
    bool IsEH, uint64_t EHFrameAddress)
    : Arch(Arch), IsEH(IsEH), EHFrameAddress(EHFrameAddress) {}

DWARFDebugFrame::~DWARFDebugFrame() = default;

static void LLVM_ATTRIBUTE_UNUSED dumpDataAux(DataExtractor Data,
                                              uint64_t Offset, int Length) {
  errs() << "DUMP: ";
  for (int i = 0; i < Length; ++i) {
    uint8_t c = Data.getU8(&Offset);
    errs().write_hex(c); errs() << " ";
  }
  errs() << "\n";
}

Error DWARFDebugFrame::parse(DWARFDataExtractor Data) {
  uint64_t Offset = 0;
  DenseMap<uint64_t, CIE *> CIEs;

  while (Data.isValidOffset(Offset)) {
    uint64_t StartOffset = Offset;

    uint64_t Length;
    DwarfFormat Format;
    std::tie(Length, Format) = Data.getInitialLength(&Offset);
    bool IsDWARF64 = Format == DWARF64;

    // If the Length is 0, then this CIE is a terminator. We add it because some
    // dumper tools might need it to print something special for such entries
    // (e.g. llvm-objdump --dwarf=frames prints "ZERO terminator").
    if (Length == 0) {
      auto Cie = std::make_unique<CIE>(
          IsDWARF64, StartOffset, 0, 0, SmallString<8>(), 0, 0, 0, 0, 0,
          SmallString<8>(), 0, 0, std::nullopt, std::nullopt, Arch);
      CIEs[StartOffset] = Cie.get();
      Entries.push_back(std::move(Cie));
      break;
    }

    // At this point, Offset points to the next field after Length.
    // Length is the structure size excluding itself. Compute an offset one
    // past the end of the structure (needed to know how many instructions to
    // read).
    uint64_t StartStructureOffset = Offset;
    uint64_t EndStructureOffset = Offset + Length;

    // The Id field's size depends on the DWARF format
    Error Err = Error::success();
    uint64_t Id = Data.getRelocatedValue((IsDWARF64 && !IsEH) ? 8 : 4, &Offset,
                                         /*SectionIndex=*/nullptr, &Err);
    if (Err)
      return Err;

    if (Id == getCIEId(IsDWARF64, IsEH)) {
      uint8_t Version = Data.getU8(&Offset);
      const char *Augmentation = Data.getCStr(&Offset);
      StringRef AugmentationString(Augmentation ? Augmentation : "");
      uint8_t AddressSize = Version < 4 ? Data.getAddressSize() :
                                          Data.getU8(&Offset);
      Data.setAddressSize(AddressSize);
      uint8_t SegmentDescriptorSize = Version < 4 ? 0 : Data.getU8(&Offset);
      uint64_t CodeAlignmentFactor = Data.getULEB128(&Offset);
      int64_t DataAlignmentFactor = Data.getSLEB128(&Offset);
      uint64_t ReturnAddressRegister =
          Version == 1 ? Data.getU8(&Offset) : Data.getULEB128(&Offset);

      // Parse the augmentation data for EH CIEs
      StringRef AugmentationData("");
      uint32_t FDEPointerEncoding = DW_EH_PE_absptr;
      uint32_t LSDAPointerEncoding = DW_EH_PE_omit;
      std::optional<uint64_t> Personality;
      std::optional<uint32_t> PersonalityEncoding;
      if (IsEH) {
        std::optional<uint64_t> AugmentationLength;
        uint64_t StartAugmentationOffset;
        uint64_t EndAugmentationOffset;

        // Walk the augmentation string to get all the augmentation data.
        for (unsigned i = 0, e = AugmentationString.size(); i != e; ++i) {
          switch (AugmentationString[i]) {
          default:
            return createStringError(
                errc::invalid_argument,
                "unknown augmentation character %c in entry at 0x%" PRIx64,
                AugmentationString[i], StartOffset);
          case 'L':
            LSDAPointerEncoding = Data.getU8(&Offset);
            break;
          case 'P': {
            if (Personality)
              return createStringError(
                  errc::invalid_argument,
                  "duplicate personality in entry at 0x%" PRIx64, StartOffset);
            PersonalityEncoding = Data.getU8(&Offset);
            Personality = Data.getEncodedPointer(
                &Offset, *PersonalityEncoding,
                EHFrameAddress ? EHFrameAddress + Offset : 0);
            break;
          }
          case 'R':
            FDEPointerEncoding = Data.getU8(&Offset);
            break;
          case 'S':
            // Current frame is a signal trampoline.
            break;
          case 'z':
            if (i)
              return createStringError(
                  errc::invalid_argument,
                  "'z' must be the first character at 0x%" PRIx64, StartOffset);
            // Parse the augmentation length first.  We only parse it if
            // the string contains a 'z'.
            AugmentationLength = Data.getULEB128(&Offset);
            StartAugmentationOffset = Offset;
            EndAugmentationOffset = Offset + *AugmentationLength;
            break;
          case 'B':
            // B-Key is used for signing functions associated with this
            // augmentation string
            break;
            // This stack frame contains MTE tagged data, so needs to be
            // untagged on unwind.
          case 'G':
            break;
          }
        }

        if (AugmentationLength) {
          if (Offset != EndAugmentationOffset)
            return createStringError(errc::invalid_argument,
                                     "parsing augmentation data at 0x%" PRIx64
                                     " failed",
                                     StartOffset);
          AugmentationData = Data.getData().slice(StartAugmentationOffset,
                                                  EndAugmentationOffset);
        }
      }

      auto Cie = std::make_unique<CIE>(
          IsDWARF64, StartOffset, Length, Version, AugmentationString,
          AddressSize, SegmentDescriptorSize, CodeAlignmentFactor,
          DataAlignmentFactor, ReturnAddressRegister, AugmentationData,
          FDEPointerEncoding, LSDAPointerEncoding, Personality,
          PersonalityEncoding, Arch);
      CIEs[StartOffset] = Cie.get();
      Entries.emplace_back(std::move(Cie));
    } else {
      // FDE
      uint64_t CIEPointer = Id;
      uint64_t InitialLocation = 0;
      uint64_t AddressRange = 0;
      std::optional<uint64_t> LSDAAddress;
      CIE *Cie = CIEs[IsEH ? (StartStructureOffset - CIEPointer) : CIEPointer];

      if (IsEH) {
        // The address size is encoded in the CIE we reference.
        if (!Cie)
          return createStringError(errc::invalid_argument,
                                   "parsing FDE data at 0x%" PRIx64
                                   " failed due to missing CIE",
                                   StartOffset);
        if (auto Val =
                Data.getEncodedPointer(&Offset, Cie->getFDEPointerEncoding(),
                                       EHFrameAddress + Offset)) {
          InitialLocation = *Val;
        }
        if (auto Val = Data.getEncodedPointer(
                &Offset, Cie->getFDEPointerEncoding(), 0)) {
          AddressRange = *Val;
        }

        StringRef AugmentationString = Cie->getAugmentationString();
        if (!AugmentationString.empty()) {
          // Parse the augmentation length and data for this FDE.
          uint64_t AugmentationLength = Data.getULEB128(&Offset);

          uint64_t EndAugmentationOffset = Offset + AugmentationLength;

          // Decode the LSDA if the CIE augmentation string said we should.
          if (Cie->getLSDAPointerEncoding() != DW_EH_PE_omit) {
            LSDAAddress = Data.getEncodedPointer(
                &Offset, Cie->getLSDAPointerEncoding(),
                EHFrameAddress ? Offset + EHFrameAddress : 0);
          }

          if (Offset != EndAugmentationOffset)
            return createStringError(errc::invalid_argument,
                                     "parsing augmentation data at 0x%" PRIx64
                                     " failed",
                                     StartOffset);
        }
      } else {
        InitialLocation = Data.getRelocatedAddress(&Offset);
        AddressRange = Data.getRelocatedAddress(&Offset);
      }

      Entries.emplace_back(new FDE(IsDWARF64, StartOffset, Length, CIEPointer,
                                   InitialLocation, AddressRange, Cie,
                                   LSDAAddress, Arch));
    }

    if (Error E =
            Entries.back()->cfis().parse(Data, &Offset, EndStructureOffset))
      return E;

    if (Offset != EndStructureOffset)
      return createStringError(
          errc::invalid_argument,
          "parsing entry instructions at 0x%" PRIx64 " failed", StartOffset);
  }

  return Error::success();
}

FrameEntry *DWARFDebugFrame::getEntryAtOffset(uint64_t Offset) const {
  auto It = partition_point(Entries, [=](const std::unique_ptr<FrameEntry> &E) {
    return E->getOffset() < Offset;
  });
  if (It != Entries.end() && (*It)->getOffset() == Offset)
    return It->get();
  return nullptr;
}

void DWARFDebugFrame::dump(raw_ostream &OS, DIDumpOptions DumpOpts,
                           std::optional<uint64_t> Offset) const {
  DumpOpts.IsEH = IsEH;
  if (Offset) {
    if (auto *Entry = getEntryAtOffset(*Offset))
      Entry->dump(OS, DumpOpts);
    return;
  }

  OS << "\n";
  for (const auto &Entry : Entries)
    Entry->dump(OS, DumpOpts);
}
