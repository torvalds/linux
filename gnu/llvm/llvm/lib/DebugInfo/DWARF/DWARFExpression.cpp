//===-- DWARFExpression.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/DWARF/DWARFExpression.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include "llvm/Support/Format.h"
#include <cassert>
#include <cstdint>
#include <vector>

using namespace llvm;
using namespace dwarf;

namespace llvm {

typedef DWARFExpression::Operation Op;
typedef Op::Description Desc;

static std::vector<Desc> getOpDescriptions() {
  std::vector<Desc> Descriptions;
  Descriptions.resize(0xff);
  Descriptions[DW_OP_addr] = Desc(Op::Dwarf2, Op::SizeAddr);
  Descriptions[DW_OP_deref] = Desc(Op::Dwarf2);
  Descriptions[DW_OP_const1u] = Desc(Op::Dwarf2, Op::Size1);
  Descriptions[DW_OP_const1s] = Desc(Op::Dwarf2, Op::SignedSize1);
  Descriptions[DW_OP_const2u] = Desc(Op::Dwarf2, Op::Size2);
  Descriptions[DW_OP_const2s] = Desc(Op::Dwarf2, Op::SignedSize2);
  Descriptions[DW_OP_const4u] = Desc(Op::Dwarf2, Op::Size4);
  Descriptions[DW_OP_const4s] = Desc(Op::Dwarf2, Op::SignedSize4);
  Descriptions[DW_OP_const8u] = Desc(Op::Dwarf2, Op::Size8);
  Descriptions[DW_OP_const8s] = Desc(Op::Dwarf2, Op::SignedSize8);
  Descriptions[DW_OP_constu] = Desc(Op::Dwarf2, Op::SizeLEB);
  Descriptions[DW_OP_consts] = Desc(Op::Dwarf2, Op::SignedSizeLEB);
  Descriptions[DW_OP_dup] = Desc(Op::Dwarf2);
  Descriptions[DW_OP_drop] = Desc(Op::Dwarf2);
  Descriptions[DW_OP_over] = Desc(Op::Dwarf2);
  Descriptions[DW_OP_pick] = Desc(Op::Dwarf2, Op::Size1);
  Descriptions[DW_OP_swap] = Desc(Op::Dwarf2);
  Descriptions[DW_OP_rot] = Desc(Op::Dwarf2);
  Descriptions[DW_OP_xderef] = Desc(Op::Dwarf2);
  Descriptions[DW_OP_abs] = Desc(Op::Dwarf2);
  Descriptions[DW_OP_and] = Desc(Op::Dwarf2);
  Descriptions[DW_OP_div] = Desc(Op::Dwarf2);
  Descriptions[DW_OP_minus] = Desc(Op::Dwarf2);
  Descriptions[DW_OP_mod] = Desc(Op::Dwarf2);
  Descriptions[DW_OP_mul] = Desc(Op::Dwarf2);
  Descriptions[DW_OP_neg] = Desc(Op::Dwarf2);
  Descriptions[DW_OP_not] = Desc(Op::Dwarf2);
  Descriptions[DW_OP_or] = Desc(Op::Dwarf2);
  Descriptions[DW_OP_plus] = Desc(Op::Dwarf2);
  Descriptions[DW_OP_plus_uconst] = Desc(Op::Dwarf2, Op::SizeLEB);
  Descriptions[DW_OP_shl] = Desc(Op::Dwarf2);
  Descriptions[DW_OP_shr] = Desc(Op::Dwarf2);
  Descriptions[DW_OP_shra] = Desc(Op::Dwarf2);
  Descriptions[DW_OP_xor] = Desc(Op::Dwarf2);
  Descriptions[DW_OP_skip] = Desc(Op::Dwarf2, Op::SignedSize2);
  Descriptions[DW_OP_bra] = Desc(Op::Dwarf2, Op::SignedSize2);
  Descriptions[DW_OP_eq] = Desc(Op::Dwarf2);
  Descriptions[DW_OP_ge] = Desc(Op::Dwarf2);
  Descriptions[DW_OP_gt] = Desc(Op::Dwarf2);
  Descriptions[DW_OP_le] = Desc(Op::Dwarf2);
  Descriptions[DW_OP_lt] = Desc(Op::Dwarf2);
  Descriptions[DW_OP_ne] = Desc(Op::Dwarf2);
  for (uint16_t LA = DW_OP_lit0; LA <= DW_OP_lit31; ++LA)
    Descriptions[LA] = Desc(Op::Dwarf2);
  for (uint16_t LA = DW_OP_reg0; LA <= DW_OP_reg31; ++LA)
    Descriptions[LA] = Desc(Op::Dwarf2);
  for (uint16_t LA = DW_OP_breg0; LA <= DW_OP_breg31; ++LA)
    Descriptions[LA] = Desc(Op::Dwarf2, Op::SignedSizeLEB);
  Descriptions[DW_OP_regx] = Desc(Op::Dwarf2, Op::SizeLEB);
  Descriptions[DW_OP_fbreg] = Desc(Op::Dwarf2, Op::SignedSizeLEB);
  Descriptions[DW_OP_bregx] = Desc(Op::Dwarf2, Op::SizeLEB, Op::SignedSizeLEB);
  Descriptions[DW_OP_piece] = Desc(Op::Dwarf2, Op::SizeLEB);
  Descriptions[DW_OP_deref_size] = Desc(Op::Dwarf2, Op::Size1);
  Descriptions[DW_OP_xderef_size] = Desc(Op::Dwarf2, Op::Size1);
  Descriptions[DW_OP_nop] = Desc(Op::Dwarf2);
  Descriptions[DW_OP_push_object_address] = Desc(Op::Dwarf3);
  Descriptions[DW_OP_call2] = Desc(Op::Dwarf3, Op::Size2);
  Descriptions[DW_OP_call4] = Desc(Op::Dwarf3, Op::Size4);
  Descriptions[DW_OP_call_ref] = Desc(Op::Dwarf3, Op::SizeRefAddr);
  Descriptions[DW_OP_form_tls_address] = Desc(Op::Dwarf3);
  Descriptions[DW_OP_call_frame_cfa] = Desc(Op::Dwarf3);
  Descriptions[DW_OP_bit_piece] = Desc(Op::Dwarf3, Op::SizeLEB, Op::SizeLEB);
  Descriptions[DW_OP_implicit_value] =
      Desc(Op::Dwarf3, Op::SizeLEB, Op::SizeBlock);
  Descriptions[DW_OP_stack_value] = Desc(Op::Dwarf3);
  Descriptions[DW_OP_WASM_location] =
      Desc(Op::Dwarf4, Op::SizeLEB, Op::WasmLocationArg);
  Descriptions[DW_OP_GNU_push_tls_address] = Desc(Op::Dwarf3);
  Descriptions[DW_OP_GNU_addr_index] = Desc(Op::Dwarf4, Op::SizeLEB);
  Descriptions[DW_OP_GNU_const_index] = Desc(Op::Dwarf4, Op::SizeLEB);
  Descriptions[DW_OP_GNU_entry_value] = Desc(Op::Dwarf4, Op::SizeLEB);
  Descriptions[DW_OP_addrx] = Desc(Op::Dwarf5, Op::SizeLEB);
  Descriptions[DW_OP_constx] = Desc(Op::Dwarf5, Op::SizeLEB);
  Descriptions[DW_OP_convert] = Desc(Op::Dwarf5, Op::BaseTypeRef);
  Descriptions[DW_OP_entry_value] = Desc(Op::Dwarf5, Op::SizeLEB);
  Descriptions[DW_OP_regval_type] =
      Desc(Op::Dwarf5, Op::SizeLEB, Op::BaseTypeRef);
  // This Description acts as a marker that getSubOpDesc must be called
  // to fetch the final Description for the operation. Each such final
  // Description must share the same first SizeSubOpLEB operand.
  Descriptions[DW_OP_LLVM_user] = Desc(Op::Dwarf5, Op::SizeSubOpLEB);
  return Descriptions;
}

static Desc getDescImpl(ArrayRef<Desc> Descriptions, unsigned Opcode) {
  // Handle possible corrupted or unsupported operation.
  if (Opcode >= Descriptions.size())
    return {};
  return Descriptions[Opcode];
}

static Desc getOpDesc(unsigned Opcode) {
  static std::vector<Desc> Descriptions = getOpDescriptions();
  return getDescImpl(Descriptions, Opcode);
}

static std::vector<Desc> getSubOpDescriptions() {
  static constexpr unsigned LlvmUserDescriptionsSize = 1
#define HANDLE_DW_OP_LLVM_USEROP(ID, NAME) +1
#include "llvm/BinaryFormat/Dwarf.def"
      ;
  std::vector<Desc> Descriptions;
  Descriptions.resize(LlvmUserDescriptionsSize);
  Descriptions[DW_OP_LLVM_nop] = Desc(Op::Dwarf5, Op::SizeSubOpLEB);
  return Descriptions;
}

static Desc getSubOpDesc(unsigned Opcode, unsigned SubOpcode) {
  assert(Opcode == DW_OP_LLVM_user);
  static std::vector<Desc> Descriptions = getSubOpDescriptions();
  return getDescImpl(Descriptions, SubOpcode);
}

bool DWARFExpression::Operation::extract(DataExtractor Data,
                                         uint8_t AddressSize, uint64_t Offset,
                                         std::optional<DwarfFormat> Format) {
  EndOffset = Offset;
  Opcode = Data.getU8(&Offset);

  Desc = getOpDesc(Opcode);
  if (Desc.Version == Operation::DwarfNA)
    return false;

  Operands.resize(Desc.Op.size());
  OperandEndOffsets.resize(Desc.Op.size());
  for (unsigned Operand = 0; Operand < Desc.Op.size(); ++Operand) {
    unsigned Size = Desc.Op[Operand];
    unsigned Signed = Size & Operation::SignBit;

    switch (Size & ~Operation::SignBit) {
    case Operation::SizeSubOpLEB:
      assert(Operand == 0 && "SubOp operand must be the first operand");
      Operands[Operand] = Data.getULEB128(&Offset);
      Desc = getSubOpDesc(Opcode, Operands[Operand]);
      if (Desc.Version == Operation::DwarfNA)
        return false;
      assert(Desc.Op[Operand] == Operation::SizeSubOpLEB &&
             "SizeSubOpLEB Description must begin with SizeSubOpLEB operand");
      break;
    case Operation::Size1:
      Operands[Operand] = Data.getU8(&Offset);
      if (Signed)
        Operands[Operand] = (int8_t)Operands[Operand];
      break;
    case Operation::Size2:
      Operands[Operand] = Data.getU16(&Offset);
      if (Signed)
        Operands[Operand] = (int16_t)Operands[Operand];
      break;
    case Operation::Size4:
      Operands[Operand] = Data.getU32(&Offset);
      if (Signed)
        Operands[Operand] = (int32_t)Operands[Operand];
      break;
    case Operation::Size8:
      Operands[Operand] = Data.getU64(&Offset);
      break;
    case Operation::SizeAddr:
      Operands[Operand] = Data.getUnsigned(&Offset, AddressSize);
      break;
    case Operation::SizeRefAddr:
      if (!Format)
        return false;
      Operands[Operand] =
          Data.getUnsigned(&Offset, dwarf::getDwarfOffsetByteSize(*Format));
      break;
    case Operation::SizeLEB:
      if (Signed)
        Operands[Operand] = Data.getSLEB128(&Offset);
      else
        Operands[Operand] = Data.getULEB128(&Offset);
      break;
    case Operation::BaseTypeRef:
      Operands[Operand] = Data.getULEB128(&Offset);
      break;
    case Operation::WasmLocationArg:
      assert(Operand == 1);
      switch (Operands[0]) {
      case 0:
      case 1:
      case 2:
      case 4:
        Operands[Operand] = Data.getULEB128(&Offset);
        break;
      case 3: // global as uint32
         Operands[Operand] = Data.getU32(&Offset);
         break;
      default:
        return false; // Unknown Wasm location
      }
      break;
    case Operation::SizeBlock:
      // We need a size, so this cannot be the first operand
      if (Operand == 0)
        return false;
      // Store the offset of the block as the value.
      Operands[Operand] = Offset;
      Offset += Operands[Operand - 1];
      break;
    default:
      llvm_unreachable("Unknown DWARFExpression Op size");
    }

    OperandEndOffsets[Operand] = Offset;
  }

  EndOffset = Offset;
  return true;
}

static void prettyPrintBaseTypeRef(DWARFUnit *U, raw_ostream &OS,
                                   DIDumpOptions DumpOpts,
                                   ArrayRef<uint64_t> Operands,
                                   unsigned Operand) {
  assert(Operand < Operands.size() && "operand out of bounds");
  if (!U) {
    OS << format(" <base_type ref: 0x%" PRIx64 ">", Operands[Operand]);
    return;
  }
  auto Die = U->getDIEForOffset(U->getOffset() + Operands[Operand]);
  if (Die && Die.getTag() == dwarf::DW_TAG_base_type) {
    OS << " (";
    if (DumpOpts.Verbose)
      OS << format("0x%08" PRIx64 " -> ", Operands[Operand]);
    OS << format("0x%08" PRIx64 ")", U->getOffset() + Operands[Operand]);
    if (auto Name = dwarf::toString(Die.find(dwarf::DW_AT_name)))
      OS << " \"" << *Name << "\"";
  } else {
    OS << format(" <invalid base_type ref: 0x%" PRIx64 ">", Operands[Operand]);
  }
}

bool DWARFExpression::prettyPrintRegisterOp(DWARFUnit *U, raw_ostream &OS,
                                            DIDumpOptions DumpOpts,
                                            uint8_t Opcode,
                                            ArrayRef<uint64_t> Operands) {
  if (!DumpOpts.GetNameForDWARFReg)
    return false;

  uint64_t DwarfRegNum;
  unsigned OpNum = 0;

  if (Opcode == DW_OP_bregx || Opcode == DW_OP_regx ||
      Opcode == DW_OP_regval_type)
    DwarfRegNum = Operands[OpNum++];
  else if (Opcode >= DW_OP_breg0 && Opcode < DW_OP_bregx)
    DwarfRegNum = Opcode - DW_OP_breg0;
  else
    DwarfRegNum = Opcode - DW_OP_reg0;

  auto RegName = DumpOpts.GetNameForDWARFReg(DwarfRegNum, DumpOpts.IsEH);
  if (!RegName.empty()) {
    if ((Opcode >= DW_OP_breg0 && Opcode <= DW_OP_breg31) ||
        Opcode == DW_OP_bregx)
      OS << ' ' << RegName << format("%+" PRId64, Operands[OpNum]);
    else
      OS << ' ' << RegName.data();

    if (Opcode == DW_OP_regval_type)
      prettyPrintBaseTypeRef(U, OS, DumpOpts, Operands, 1);
    return true;
  }

  return false;
}

std::optional<unsigned> DWARFExpression::Operation::getSubCode() const {
  if (!Desc.Op.size() || Desc.Op[0] != Operation::SizeSubOpLEB)
    return std::nullopt;
  return Operands[0];
}

bool DWARFExpression::Operation::print(raw_ostream &OS, DIDumpOptions DumpOpts,
                                       const DWARFExpression *Expr,
                                       DWARFUnit *U) const {
  if (Error) {
    OS << "<decoding error>";
    return false;
  }

  StringRef Name = OperationEncodingString(Opcode);
  assert(!Name.empty() && "DW_OP has no name!");
  OS << Name;

  if ((Opcode >= DW_OP_breg0 && Opcode <= DW_OP_breg31) ||
      (Opcode >= DW_OP_reg0 && Opcode <= DW_OP_reg31) ||
      Opcode == DW_OP_bregx || Opcode == DW_OP_regx ||
      Opcode == DW_OP_regval_type)
    if (prettyPrintRegisterOp(U, OS, DumpOpts, Opcode, Operands))
      return true;

  for (unsigned Operand = 0; Operand < Desc.Op.size(); ++Operand) {
    unsigned Size = Desc.Op[Operand];
    unsigned Signed = Size & Operation::SignBit;

    if (Size == Operation::SizeSubOpLEB) {
      StringRef SubName = SubOperationEncodingString(Opcode, Operands[Operand]);
      assert(!SubName.empty() && "DW_OP SubOp has no name!");
      OS << " " << SubName;
    } else if (Size == Operation::BaseTypeRef && U) {
      // For DW_OP_convert the operand may be 0 to indicate that conversion to
      // the generic type should be done. The same holds for DW_OP_reinterpret,
      // which is currently not supported.
      if (Opcode == DW_OP_convert && Operands[Operand] == 0)
        OS << " 0x0";
      else
        prettyPrintBaseTypeRef(U, OS, DumpOpts, Operands, Operand);
    } else if (Size == Operation::WasmLocationArg) {
      assert(Operand == 1);
      switch (Operands[0]) {
      case 0:
      case 1:
      case 2:
      case 3: // global as uint32
      case 4:
        OS << format(" 0x%" PRIx64, Operands[Operand]);
        break;
      default: assert(false);
      }
    } else if (Size == Operation::SizeBlock) {
      uint64_t Offset = Operands[Operand];
      for (unsigned i = 0; i < Operands[Operand - 1]; ++i)
        OS << format(" 0x%02x", Expr->Data.getU8(&Offset));
    } else {
      if (Signed)
        OS << format(" %+" PRId64, (int64_t)Operands[Operand]);
      else if (Opcode != DW_OP_entry_value &&
               Opcode != DW_OP_GNU_entry_value)
        OS << format(" 0x%" PRIx64, Operands[Operand]);
    }
  }
  return true;
}

void DWARFExpression::print(raw_ostream &OS, DIDumpOptions DumpOpts,
                            DWARFUnit *U, bool IsEH) const {
  uint32_t EntryValExprSize = 0;
  uint64_t EntryValStartOffset = 0;
  if (Data.getData().empty())
    OS << "<empty>";

  for (auto &Op : *this) {
    DumpOpts.IsEH = IsEH;
    if (!Op.print(OS, DumpOpts, this, U)) {
      uint64_t FailOffset = Op.getEndOffset();
      while (FailOffset < Data.getData().size())
        OS << format(" %02x", Data.getU8(&FailOffset));
      return;
    }

    if (Op.getCode() == DW_OP_entry_value ||
        Op.getCode() == DW_OP_GNU_entry_value) {
      OS << "(";
      EntryValExprSize = Op.getRawOperand(0);
      EntryValStartOffset = Op.getEndOffset();
      continue;
    }

    if (EntryValExprSize) {
      EntryValExprSize -= Op.getEndOffset() - EntryValStartOffset;
      if (EntryValExprSize == 0)
        OS << ")";
    }

    if (Op.getEndOffset() < Data.getData().size())
      OS << ", ";
  }
}

bool DWARFExpression::Operation::verify(const Operation &Op, DWARFUnit *U) {
  for (unsigned Operand = 0; Operand < Op.Desc.Op.size(); ++Operand) {
    unsigned Size = Op.Desc.Op[Operand];

    if (Size == Operation::BaseTypeRef) {
      // For DW_OP_convert the operand may be 0 to indicate that conversion to
      // the generic type should be done, so don't look up a base type in that
      // case. The same holds for DW_OP_reinterpret, which is currently not
      // supported.
      if (Op.Opcode == DW_OP_convert && Op.Operands[Operand] == 0)
        continue;
      auto Die = U->getDIEForOffset(U->getOffset() + Op.Operands[Operand]);
      if (!Die || Die.getTag() != dwarf::DW_TAG_base_type)
        return false;
    }
  }

  return true;
}

bool DWARFExpression::verify(DWARFUnit *U) {
  for (auto &Op : *this)
    if (!Operation::verify(Op, U))
      return false;

  return true;
}

/// A user-facing string representation of a DWARF expression. This might be an
/// Address expression, in which case it will be implicitly dereferenced, or a
/// Value expression.
struct PrintedExpr {
  enum ExprKind {
    Address,
    Value,
  };
  ExprKind Kind;
  SmallString<16> String;

  PrintedExpr(ExprKind K = Address) : Kind(K) {}
};

static bool printCompactDWARFExpr(
    raw_ostream &OS, DWARFExpression::iterator I,
    const DWARFExpression::iterator E,
    std::function<StringRef(uint64_t RegNum, bool IsEH)> GetNameForDWARFReg =
        nullptr) {
  SmallVector<PrintedExpr, 4> Stack;

  while (I != E) {
    const DWARFExpression::Operation &Op = *I;
    uint8_t Opcode = Op.getCode();
    switch (Opcode) {
    case dwarf::DW_OP_regx: {
      // DW_OP_regx: A register, with the register num given as an operand.
      // Printed as the plain register name.
      uint64_t DwarfRegNum = Op.getRawOperand(0);
      auto RegName = GetNameForDWARFReg(DwarfRegNum, false);
      if (RegName.empty())
        return false;
      raw_svector_ostream S(Stack.emplace_back(PrintedExpr::Value).String);
      S << RegName;
      break;
    }
    case dwarf::DW_OP_bregx: {
      int DwarfRegNum = Op.getRawOperand(0);
      int64_t Offset = Op.getRawOperand(1);
      auto RegName = GetNameForDWARFReg(DwarfRegNum, false);
      if (RegName.empty())
        return false;
      raw_svector_ostream S(Stack.emplace_back().String);
      S << RegName;
      if (Offset)
        S << format("%+" PRId64, Offset);
      break;
    }
    case dwarf::DW_OP_entry_value:
    case dwarf::DW_OP_GNU_entry_value: {
      // DW_OP_entry_value contains a sub-expression which must be rendered
      // separately.
      uint64_t SubExprLength = Op.getRawOperand(0);
      DWARFExpression::iterator SubExprEnd = I.skipBytes(SubExprLength);
      ++I;
      raw_svector_ostream S(Stack.emplace_back().String);
      S << "entry(";
      printCompactDWARFExpr(S, I, SubExprEnd, GetNameForDWARFReg);
      S << ")";
      I = SubExprEnd;
      continue;
    }
    case dwarf::DW_OP_stack_value: {
      // The top stack entry should be treated as the actual value of tne
      // variable, rather than the address of the variable in memory.
      assert(!Stack.empty());
      Stack.back().Kind = PrintedExpr::Value;
      break;
    }
    case dwarf::DW_OP_nop: {
      break;
    }
    case dwarf::DW_OP_LLVM_user: {
      assert(Op.getSubCode() && *Op.getSubCode() == dwarf::DW_OP_LLVM_nop);
      break;
    }
    default:
      if (Opcode >= dwarf::DW_OP_reg0 && Opcode <= dwarf::DW_OP_reg31) {
        // DW_OP_reg<N>: A register, with the register num implied by the
        // opcode. Printed as the plain register name.
        uint64_t DwarfRegNum = Opcode - dwarf::DW_OP_reg0;
        auto RegName = GetNameForDWARFReg(DwarfRegNum, false);
        if (RegName.empty())
          return false;
        raw_svector_ostream S(Stack.emplace_back(PrintedExpr::Value).String);
        S << RegName;
      } else if (Opcode >= dwarf::DW_OP_breg0 &&
                 Opcode <= dwarf::DW_OP_breg31) {
        int DwarfRegNum = Opcode - dwarf::DW_OP_breg0;
        int64_t Offset = Op.getRawOperand(0);
        auto RegName = GetNameForDWARFReg(DwarfRegNum, false);
        if (RegName.empty())
          return false;
        raw_svector_ostream S(Stack.emplace_back().String);
        S << RegName;
        if (Offset)
          S << format("%+" PRId64, Offset);
      } else {
        // If we hit an unknown operand, we don't know its effect on the stack,
        // so bail out on the whole expression.
        OS << "<unknown op " << dwarf::OperationEncodingString(Opcode) << " ("
           << (int)Opcode << ")>";
        return false;
      }
      break;
    }
    ++I;
  }

  if (Stack.size() != 1) {
    OS << "<stack of size " << Stack.size() << ", expected 1>";
    return false;
  }

  if (Stack.front().Kind == PrintedExpr::Address)
    OS << "[" << Stack.front().String << "]";
  else
    OS << Stack.front().String;

  return true;
}

bool DWARFExpression::printCompact(
    raw_ostream &OS,
    std::function<StringRef(uint64_t RegNum, bool IsEH)> GetNameForDWARFReg) {
  return printCompactDWARFExpr(OS, begin(), end(), GetNameForDWARFReg);
}

bool DWARFExpression::operator==(const DWARFExpression &RHS) const {
  if (AddressSize != RHS.AddressSize || Format != RHS.Format)
    return false;
  return Data.getData() == RHS.Data.getData();
}

} // namespace llvm
