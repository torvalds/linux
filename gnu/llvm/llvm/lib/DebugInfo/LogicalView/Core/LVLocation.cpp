//===-- LVLocation.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This implements the LVOperation and LVLocation classes.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/LogicalView/Core/LVLocation.h"
#include "llvm/DebugInfo/LogicalView/Core/LVReader.h"
#include "llvm/DebugInfo/LogicalView/Core/LVScope.h"
#include "llvm/DebugInfo/LogicalView/Core/LVSymbol.h"

using namespace llvm;
using namespace llvm::logicalview;

#define DEBUG_TYPE "Location"

void LVOperation::print(raw_ostream &OS, bool Full) const {}

// Identify the most common type of operations and print them using a high
// level format, trying to isolate the DWARF complexity.
std::string LVOperation::getOperandsDWARFInfo() {
  std::string String;
  raw_string_ostream Stream(String);

  auto PrintRegisterInfo = [&](LVSmall Code) {
    //-----------------------------------------------------------------------
    // 2.5.1.1 Literal encodings.
    //-----------------------------------------------------------------------
    if (dwarf::DW_OP_lit0 <= Code && Code <= dwarf::DW_OP_lit31) {
      Stream << format("lit%d", Code - dwarf::DW_OP_lit0);
      return;
    }

    //-----------------------------------------------------------------------
    // 2.5.1.2 Register values.
    //-----------------------------------------------------------------------
    if (dwarf::DW_OP_breg0 <= Code && Code <= dwarf::DW_OP_breg31) {
      std::string RegisterName(getReader().getRegisterName(Code, Operands));
      Stream << format("breg%d+%d%s", Code - dwarf::DW_OP_breg0, Operands[0],
                       RegisterName.c_str());
      return;
    }

    //-----------------------------------------------------------------------
    // 2.6.1.1.3 Register location descriptions.
    //-----------------------------------------------------------------------
    if (dwarf::DW_OP_reg0 <= Code && Code <= dwarf::DW_OP_reg31) {
      std::string RegisterName(getReader().getRegisterName(Code, Operands));
      Stream << format("reg%d%s", Code - dwarf::DW_OP_reg0,
                       RegisterName.c_str());
      return;
    }

    Stream << format("#0x%02x ", Code) << hexString(Operands[0]) << " "
           << hexString(Operands[1]) << "#";
  };

  switch (Opcode) {
  //-------------------------------------------------------------------------
  // 2.5.1.1 Literal encodings.
  //-------------------------------------------------------------------------
  case dwarf::DW_OP_addr:
    Stream << "addr " << hexString(Operands[0]);
    break;
  case dwarf::DW_OP_constu:
  case dwarf::DW_OP_const1u:
  case dwarf::DW_OP_const2u:
  case dwarf::DW_OP_const4u:
  case dwarf::DW_OP_const8u:
    Stream << "const_u " << unsigned(Operands[0]);
    break;
  case dwarf::DW_OP_consts:
  case dwarf::DW_OP_const1s:
  case dwarf::DW_OP_const2s:
  case dwarf::DW_OP_const4s:
  case dwarf::DW_OP_const8s:
    Stream << "const_s " << int(Operands[0]);
    break;
  case dwarf::DW_OP_addrx:
    Stream << "addrx " << unsigned(Operands[0]);
    break;
  case dwarf::DW_OP_constx:
    Stream << "constx " << unsigned(Operands[0]);
    break;
  case dwarf::DW_OP_const_type:
    Stream << "TODO: DW_OP_const_type";
    break;

  //-------------------------------------------------------------------------
  // 2.5.1.2 Register values.
  //-------------------------------------------------------------------------
  case dwarf::DW_OP_fbreg:
    Stream << "fbreg " << int(Operands[0]);
    break;
  case dwarf::DW_OP_bregx: {
    std::string RegisterName(getReader().getRegisterName(Opcode, Operands));
    Stream << format("bregx %d%s+%d", Operands[0], RegisterName.c_str(),
                     unsigned(Operands[1]));
    break;
  }
  case dwarf::DW_OP_regval_type: {
    std::string RegisterName(getReader().getRegisterName(Opcode, Operands));
    Stream << format("regval_type %d%s+%d", Operands[0], RegisterName.c_str(),
                     unsigned(Operands[1]));
    break;
  }

  //-------------------------------------------------------------------------
  // 2.5.1.3 Stack operations.
  //-------------------------------------------------------------------------
  case dwarf::DW_OP_dup:
    Stream << "dup";
    break;
  case dwarf::DW_OP_drop:
    Stream << "drop";
    break;
  case dwarf::DW_OP_pick:
    Stream << "pick " << unsigned(Operands[0]);
    break;
  case dwarf::DW_OP_over:
    Stream << "over";
    break;
  case dwarf::DW_OP_swap:
    Stream << "swap";
    break;
  case dwarf::DW_OP_rot:
    Stream << "rot";
    break;
  case dwarf::DW_OP_deref:
    Stream << "deref";
    break;
  case dwarf::DW_OP_deref_size:
    Stream << "deref_size " << unsigned(Operands[0]);
    break;
  case dwarf::DW_OP_deref_type:
    Stream << "deref_type " << unsigned(Operands[0]) << " DIE offset "
           << hexString(Operands[1]);
    break;
  case dwarf::DW_OP_xderef:
    Stream << "xderef";
    break;
  case dwarf::DW_OP_xderef_size:
    Stream << "xderef_size " << unsigned(Operands[0]);
    break;
  case dwarf::DW_OP_xderef_type:
    Stream << "xderef_type " << unsigned(Operands[0]) << " DIE offset "
           << hexString(Operands[1]);
    break;
  case dwarf::DW_OP_push_object_address:
    Stream << "push_object_address";
    break;
  case dwarf::DW_OP_form_tls_address:
    Stream << "form_tls_address " << hexString(Operands[0]);
    break;
  case dwarf::DW_OP_call_frame_cfa:
    Stream << "call_frame_cfa";
    break;

  //-------------------------------------------------------------------------
  // 2.5.1.4 Arithmetic and Logical Operations.
  //-------------------------------------------------------------------------
  case dwarf::DW_OP_abs:
    Stream << "abs";
    break;
  case dwarf::DW_OP_and:
    Stream << "and";
    break;
  case dwarf::DW_OP_div:
    Stream << "div";
    break;
  case dwarf::DW_OP_minus:
    Stream << "minus";
    break;
  case dwarf::DW_OP_mod:
    Stream << "mod";
    break;
  case dwarf::DW_OP_mul:
    Stream << "mul";
    break;
  case dwarf::DW_OP_neg:
    Stream << "neg";
    break;
  case dwarf::DW_OP_not:
    Stream << "not";
    break;
  case dwarf::DW_OP_or:
    Stream << "or";
    break;
  case dwarf::DW_OP_plus:
    Stream << "plus";
    break;
  case dwarf::DW_OP_plus_uconst:
    Stream << "plus_uconst " << unsigned(Operands[0]);
    break;
  case dwarf::DW_OP_shl:
    Stream << "shl";
    break;
  case dwarf::DW_OP_shr:
    Stream << "shr";
    break;
  case dwarf::DW_OP_shra:
    Stream << "shra";
    break;
  case dwarf::DW_OP_xor:
    Stream << "xor";
    break;

  //-------------------------------------------------------------------------
  // 2.5.1.5 Control Flow Operations.
  //-------------------------------------------------------------------------
  case dwarf::DW_OP_le:
    Stream << "le";
    break;
  case dwarf::DW_OP_ge:
    Stream << "ge";
    break;
  case dwarf::DW_OP_eq:
    Stream << "eq";
    break;
  case dwarf::DW_OP_lt:
    Stream << "lt";
    break;
  case dwarf::DW_OP_gt:
    Stream << "gt";
    break;
  case dwarf::DW_OP_ne:
    Stream << "ne";
    break;
  case dwarf::DW_OP_skip:
    Stream << "skip " << signed(Operands[0]);
    break;
  case dwarf::DW_OP_bra:
    Stream << "bra " << signed(Operands[0]);
    break;
  case dwarf::DW_OP_call2:
    Stream << "call2 DIE offset " << hexString(Operands[0]);
    break;
  case dwarf::DW_OP_call4:
    Stream << "call4 DIE offset " << hexString(Operands[0]);
    break;
  case dwarf::DW_OP_call_ref:
    Stream << "call_ref DIE offset " << hexString(Operands[0]);
    break;

  //-------------------------------------------------------------------------
  // 2.5.1.6 Type Conversions.
  //-------------------------------------------------------------------------
  case dwarf::DW_OP_convert:
    Stream << "convert DIE offset " << hexString(Operands[0]);
    break;
  case dwarf::DW_OP_reinterpret:
    Stream << "reinterpret DIE offset " << hexString(Operands[0]);
    break;

  //-------------------------------------------------------------------------
  // 2.5.1.7 Special Operations.
  //-------------------------------------------------------------------------
  case dwarf::DW_OP_nop:
    Stream << "nop";
    break;
  case dwarf::DW_OP_entry_value:
    Stream << "TODO: DW_OP_entry_value";
    break;

  //-------------------------------------------------------------------------
  // 2.6.1.1.3 Register location descriptions.
  //-------------------------------------------------------------------------
  case dwarf::DW_OP_regx:
    Stream << "regx" << getReader().getRegisterName(Opcode, Operands);
    break;

  //-------------------------------------------------------------------------
  // 2.6.1.1.4 Implicit location descriptions.
  //-------------------------------------------------------------------------
  case dwarf::DW_OP_stack_value:
    Stream << "stack_value";
    break;
  case dwarf::DW_OP_implicit_value:
    Stream << "TODO: DW_OP_implicit_value";
    break;
  case dwarf::DW_OP_implicit_pointer:
    Stream << "implicit_pointer DIE offset " << hexString(Operands[0]) << " "
           << int(Operands[1]);
    break;

  //-------------------------------------------------------------------------
  // 2.6.1.2 Composite location descriptions.
  //-------------------------------------------------------------------------
  case dwarf::DW_OP_piece:
    Stream << "piece " << int(Operands[0]);
    break;
  case dwarf::DW_OP_bit_piece:
    Stream << "bit_piece " << int(Operands[0]) << " offset "
           << int(Operands[1]);
    break;

  //-------------------------------------------------------------------------
  // GNU extensions.
  //-------------------------------------------------------------------------
  case dwarf::DW_OP_GNU_entry_value:
    Stream << "gnu_entry_value ";
    PrintRegisterInfo(dwarf::DW_OP_reg0);
    break;
  case dwarf::DW_OP_GNU_push_tls_address:
    Stream << "gnu_push_tls_address " << hexString(Operands[0]);
    break;
  case dwarf::DW_OP_GNU_addr_index:
    Stream << "gnu_addr_index " << unsigned(Operands[0]);
    break;
  case dwarf::DW_OP_GNU_const_index:
    Stream << "gnu_const_index " << unsigned(Operands[0]);
    break;

  //-------------------------------------------------------------------------
  // Member location.
  //-------------------------------------------------------------------------
  case LVLocationMemberOffset:
    Stream << "offset " << int(Operands[0]);
    break;

  //-------------------------------------------------------------------------
  // Missing location.
  //-------------------------------------------------------------------------
  case dwarf::DW_OP_hi_user:
    Stream << "missing";
    break;

  //-------------------------------------------------------------------------
  // Register values.
  //-------------------------------------------------------------------------
  default:
    PrintRegisterInfo(Opcode);
    break;
  }

  return String;
}

// Identify the most common type of operations and print them using a high
// level format, trying to isolate the CodeView complexity.
std::string LVOperation::getOperandsCodeViewInfo() {
  std::string String;
  raw_string_ostream Stream(String);

  // Get original CodeView operation code.
  uint16_t OperationCode = getCodeViewOperationCode(Opcode);

  switch (OperationCode) {
  // Operands: [Offset].
  case codeview::SymbolKind::S_DEFRANGE_FRAMEPOINTER_REL:
    Stream << "frame_pointer_rel " << int(Operands[0]);
    break;
  case codeview::SymbolKind::S_DEFRANGE_FRAMEPOINTER_REL_FULL_SCOPE:
    Stream << "frame_pointer_rel_full_scope " << int(Operands[0]);
    break;

  // Operands: [Register].
  case codeview::SymbolKind::S_DEFRANGE_REGISTER:
    Stream << "register " << getReader().getRegisterName(Opcode, Operands);
    break;
  case codeview::SymbolKind::S_DEFRANGE_SUBFIELD_REGISTER:
    Stream << "subfield_register "
           << getReader().getRegisterName(Opcode, Operands);
    break;

  // Operands: [Register, Offset].
  case codeview::SymbolKind::S_DEFRANGE_REGISTER_REL:
    Stream << "register_rel " << getReader().getRegisterName(Opcode, Operands)
           << " offset " << int(Operands[1]);
    break;

  // Operands: [Program].
  case codeview::SymbolKind::S_DEFRANGE:
    Stream << "frame " << int(Operands[0]);
    break;
  case codeview::SymbolKind::S_DEFRANGE_SUBFIELD:
    Stream << "subfield " << int(Operands[0]);
    break;

  default:
    Stream << format("#0x%02x: ", Opcode) << hexString(Operands[0]) << " "
           << hexString(Operands[1]) << "#";
    break;
  }

  return String;
}

namespace {
const char *const KindBaseClassOffset = "BaseClassOffset";
const char *const KindBaseClassStep = "BaseClassStep";
const char *const KindClassOffset = "ClassOffset";
const char *const KindFixedAddress = "FixedAddress";
const char *const KindMissingInfo = "Missing";
const char *const KindOperation = "Operation";
const char *const KindOperationList = "OperationList";
const char *const KindRegister = "Register";
const char *const KindUndefined = "Undefined";
} // end anonymous namespace

//===----------------------------------------------------------------------===//
// DWARF location information.
//===----------------------------------------------------------------------===//
const char *LVLocation::kind() const {
  const char *Kind = KindUndefined;
  if (getIsBaseClassOffset())
    Kind = KindBaseClassOffset;
  else if (getIsBaseClassStep())
    Kind = KindBaseClassStep;
  else if (getIsClassOffset())
    Kind = KindClassOffset;
  else if (getIsFixedAddress())
    Kind = KindFixedAddress;
  else if (getIsGapEntry())
    Kind = KindMissingInfo;
  else if (getIsOperation())
    Kind = KindOperation;
  else if (getIsOperationList())
    Kind = KindOperationList;
  else if (getIsRegister())
    Kind = KindRegister;
  return Kind;
}

std::string LVLocation::getIntervalInfo() const {
  static const char *const Question = "?";
  std::string String;
  raw_string_ostream Stream(String);
  if (getIsAddressRange())
    Stream << "{Range}";

  auto PrintLine = [&](const LVLine *Line) {
    if (Line) {
      std::string TheLine;
      TheLine = Line->lineNumberAsStringStripped();
      Stream << TheLine.c_str();
    } else {
      Stream << Question;
    }
  };

  Stream << " Lines ";
  PrintLine(getLowerLine());
  Stream << ":";
  PrintLine(getUpperLine());

  if (options().getAttributeOffset())
    // Print the active range (low pc and high pc).
    Stream << " [" << hexString(getLowerAddress()) << ":"
           << hexString(getUpperAddress()) << "]";

  return String;
}

// Validate the ranges associated with the location.
bool LVLocation::validateRanges() {
  // Traverse the locations and validate them against the address to line
  // mapping in the current compile unit. Record those invalid ranges.
  // A valid range must meet the following conditions:
  // a) line(lopc) <= line(hipc)
  // b) line(lopc) and line(hipc) are valid.

  if (!hasAssociatedRange())
    return true;

  LVLineRange Range = getReaderCompileUnit()->lineRange(this);
  LVLine *LowLine = Range.first;
  LVLine *HighLine = Range.second;
  if (LowLine)
    setLowerLine(LowLine);
  else {
    setIsInvalidLower();
    return false;
  }
  if (HighLine)
    setUpperLine(HighLine);
  else {
    setIsInvalidUpper();
    return false;
  }
  // Check for a valid interval.
  if (LowLine->getLineNumber() > HighLine->getLineNumber()) {
    setIsInvalidRange();
    return false;
  }

  return true;
}

bool LVLocation::calculateCoverage(LVLocations *Locations, unsigned &Factor,
                                   float &Percentage) {
  if (!options().getAttributeCoverage() && !Locations)
    return false;

  // Calculate the coverage depending on the kind of location. We have
  // the simple and composed locations.
  if (Locations->size() == 1) {
    // Simple: fixed address, class offset, stack offset.
    LVLocation *Location = Locations->front();
    // Some types of locations do not have specific kind. Now is the time
    // to set those types, depending on the operation type.
    Location->updateKind();
    if (Location->getIsLocationSimple()) {
      Factor = 100;
      Percentage = 100;
      return true;
    }
  }

  // Composed locations.
  LVAddress LowerAddress = 0;
  LVAddress UpperAddress = 0;
  for (const LVLocation *Location : *Locations)
    // Do not include locations representing a gap.
    if (!Location->getIsGapEntry()) {
      LowerAddress = Location->getLowerAddress();
      UpperAddress = Location->getUpperAddress();
      Factor += (UpperAddress > LowerAddress) ? UpperAddress - LowerAddress
                                              : LowerAddress - UpperAddress;
    }

  Percentage = 0;
  return false;
}

void LVLocation::printRaw(raw_ostream &OS, bool Full) const {
  // Print the active range (low pc and high pc).
  OS << " [" << hexString(getLowerAddress()) << ":"
     << hexString(getUpperAddress()) << "]\n";
  // Print any DWARF operations.
  printRawExtra(OS, Full);
}

void LVLocation::printInterval(raw_ostream &OS, bool Full) const {
  if (hasAssociatedRange())
    OS << getIntervalInfo();
}

void LVLocation::print(raw_ostream &OS, bool Full) const {
  if (getReader().doPrintLocation(this)) {
    LVObject::print(OS, Full);
    printExtra(OS, Full);
  }
}

void LVLocation::printExtra(raw_ostream &OS, bool Full) const {
  printInterval(OS, Full);
  OS << "\n";
}

//===----------------------------------------------------------------------===//
// DWARF location for a symbol.
//===----------------------------------------------------------------------===//
// Add a Location Entry.
void LVLocationSymbol::addObject(LVAddress LowPC, LVAddress HighPC,
                                 LVUnsigned SectionOffset,
                                 uint64_t LocDescOffset) {
  setLowerAddress(LowPC);
  setUpperAddress(HighPC);

  // Record the offset where the location information begins.
  setOffset(LocDescOffset ? LocDescOffset : SectionOffset);

  // A -1 HighPC value, indicates no range.
  if (HighPC == LVAddress(UINT64_MAX))
    setIsDiscardedRange();

  // Update the location kind, using the DWARF attribute.
  setKind();
}

// Add a Location Record.
void LVLocationSymbol::addObject(LVSmall Opcode,
                                 ArrayRef<LVUnsigned> Operands) {
  if (!Entries)
    Entries = std::make_unique<LVOperations>();
  Entries->push_back(getReader().createOperation(Opcode, Operands));
}

// Based on the DWARF attribute, define the location kind.
void LVLocation::setKind() {
  switch (getAttr()) {
  case dwarf::DW_AT_data_member_location:
    setIsClassOffset();
    break;
  case dwarf::DW_AT_location:
    // Depending on the operand, we have a fixed address.
    setIsFixedAddress();
    break;
  default:
    break;
  }
  // For those symbols with absolute location information, ignore any
  // gaps in their location description; that is the case with absolute
  // memory addresses and members located at specific offsets.
  if (hasAssociatedRange())
    getParentSymbol()->setFillGaps();
}

void LVLocationSymbol::updateKind() {
  // Update the location type for simple ones.
  if (Entries && Entries->size() == 1) {
    if (dwarf::DW_OP_fbreg == Entries->front()->getOpcode())
      setIsStackOffset();
  }
}

void LVLocationSymbol::printRawExtra(raw_ostream &OS, bool Full) const {
  if (Entries)
    for (const LVOperation *Operation : *Entries)
      Operation->print(OS, Full);
}

// Print location (formatted version).
void LVLocation::print(LVLocations *Locations, raw_ostream &OS, bool Full) {
  if (!Locations || Locations->empty())
    return;

  // Print the symbol coverage.
  if (options().getAttributeCoverage()) {
    // The location entries are contained within a symbol. Get a location,
    // to access basic information about indentation, parent, etc.
    LVLocation *Location = Locations->front();
    LVSymbol *Symbol = Location->getParentSymbol();
    float Percentage = Symbol->getCoveragePercentage();

    // The coverage is dependent on the kind of location.
    std::string String;
    raw_string_ostream Stream(String);
    Stream << format("%.2f%%", Percentage);
    if (!Location->getIsLocationSimple())
      Stream << format(" (%d/%d)", Symbol->getCoverageFactor(),
                       Symbol->getParentScope()->getCoverageFactor());
    Symbol->printAttributes(OS, Full, "{Coverage} ", Symbol, StringRef(String),
                            /*UseQuotes=*/false,
                            /*PrintRef=*/false);
  }

  // Print the symbol location, including the missing entries.
  if (getReader().doPrintLocation(/*Location=*/nullptr))
    for (const LVLocation *Location : *Locations)
      Location->print(OS, Full);
}

void LVLocationSymbol::printExtra(raw_ostream &OS, bool Full) const {
  OS << "{Location}";
  if (getIsCallSite())
    OS << " -> CallSite";
  printInterval(OS, Full);
  OS << "\n";

  // Print location entries.
  if (Full && Entries) {
    bool CodeViewLocation = getParentSymbol()->getHasCodeViewLocation();
    std::stringstream Stream;
    std::string Leading;
    for (LVOperation *Operation : *Entries) {
      Stream << Leading
             << (CodeViewLocation ? Operation->getOperandsCodeViewInfo()
                                  : Operation->getOperandsDWARFInfo());
      Leading = ", ";
    }
    printAttributes(OS, Full, "{Entry} ", const_cast<LVLocationSymbol *>(this),
                    StringRef(Stream.str()),
                    /*UseQuotes=*/false,
                    /*PrintRef=*/false);
  }
}
