//===-- PDBLocationToDWARFExpression.cpp ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PDBLocationToDWARFExpression.h"

#include "lldb/Core/Section.h"
#include "lldb/Core/dwarf.h"
#include "lldb/Expression/DWARFExpression.h"
#include "lldb/Symbol/Variable.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/StreamBuffer.h"

#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/PDB/IPDBSession.h"
#include "llvm/DebugInfo/PDB/PDBSymbolData.h"

#include "Plugins/SymbolFile/NativePDB/CodeViewRegisterMapping.h"
#include "Plugins/SymbolFile/NativePDB/PdbFPOProgramToDWARFExpression.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::npdb;
using namespace lldb_private::dwarf;
using namespace llvm::pdb;

static std::unique_ptr<IPDBFrameData>
GetCorrespondingFrameData(const IPDBSession &session,
                          const Variable::RangeList &ranges) {
  auto enumFrameData = session.getFrameData();
  if (!enumFrameData)
    return nullptr;

  std::unique_ptr<IPDBFrameData> found;
  while (auto fd = enumFrameData->getNext()) {
    Range<lldb::addr_t, lldb::addr_t> fdRange(fd->getVirtualAddress(),
                                              fd->getLengthBlock());

    for (size_t i = 0; i < ranges.GetSize(); i++) {
      auto range = ranges.GetEntryAtIndex(i);
      if (!range)
        continue;

      if (!range->DoesIntersect(fdRange))
        continue;

      found = std::move(fd);

      break;
    }
  }

  return found;
}

static bool EmitVFrameEvaluationDWARFExpression(
    llvm::StringRef program, llvm::Triple::ArchType arch_type, Stream &stream) {
  // VFrame value always stored in $TO pseudo-register
  return TranslateFPOProgramToDWARFExpression(program, "$T0", arch_type,
                                              stream);
}

DWARFExpression ConvertPDBLocationToDWARFExpression(
    ModuleSP module, const PDBSymbolData &symbol,
    const Variable::RangeList &ranges, bool &is_constant) {
  is_constant = true;

  if (!module)
    return DWARFExpression();

  const ArchSpec &architecture = module->GetArchitecture();
  llvm::Triple::ArchType arch_type = architecture.GetMachine();
  ByteOrder byte_order = architecture.GetByteOrder();
  uint32_t address_size = architecture.GetAddressByteSize();
  uint32_t byte_size = architecture.GetDataByteSize();
  if (byte_order == eByteOrderInvalid || address_size == 0)
    return DWARFExpression();

  RegisterKind register_kind = eRegisterKindDWARF;
  StreamBuffer<32> stream(Stream::eBinary, address_size, byte_order);
  switch (symbol.getLocationType()) {
  case PDB_LocType::Static:
  case PDB_LocType::TLS: {
    stream.PutHex8(DW_OP_addr);

    SectionList *section_list = module->GetSectionList();
    if (!section_list)
      return DWARFExpression();

    uint32_t section_id = symbol.getAddressSection();

    auto section = section_list->FindSectionByID(section_id);
    if (!section)
      return DWARFExpression();

    uint32_t offset = symbol.getAddressOffset();
    stream.PutMaxHex64(section->GetFileAddress() + offset, address_size,
                       byte_order);

    is_constant = false;

    break;
  }
  case PDB_LocType::RegRel: {
    uint32_t reg_num;
    auto reg_id = symbol.getRegisterId();
    if (reg_id == llvm::codeview::RegisterId::VFRAME) {
      if (auto fd = GetCorrespondingFrameData(symbol.getSession(), ranges)) {
        if (EmitVFrameEvaluationDWARFExpression(fd->getProgram(), arch_type,
                                                stream)) {
          int32_t offset = symbol.getOffset();
          stream.PutHex8(DW_OP_consts);
          stream.PutSLEB128(offset);
          stream.PutHex8(DW_OP_plus);

          register_kind = eRegisterKindLLDB;

          is_constant = false;
          break;
        }
      }

      register_kind = eRegisterKindGeneric;
      reg_num = LLDB_REGNUM_GENERIC_FP;
    } else {
      register_kind = eRegisterKindLLDB;
      reg_num = GetLLDBRegisterNumber(arch_type, reg_id);
      if (reg_num == LLDB_INVALID_REGNUM)
        return DWARFExpression();
    }

    if (reg_num > 31) {
      stream.PutHex8(DW_OP_bregx);
      stream.PutULEB128(reg_num);
    } else
      stream.PutHex8(DW_OP_breg0 + reg_num);

    int32_t offset = symbol.getOffset();
    stream.PutSLEB128(offset);

    is_constant = false;

    break;
  }
  case PDB_LocType::Enregistered: {
    register_kind = eRegisterKindLLDB;
    uint32_t reg_num = GetLLDBRegisterNumber(arch_type, symbol.getRegisterId());
    if (reg_num == LLDB_INVALID_REGNUM)
      return DWARFExpression();

    if (reg_num > 31) {
      stream.PutHex8(DW_OP_regx);
      stream.PutULEB128(reg_num);
    } else
      stream.PutHex8(DW_OP_reg0 + reg_num);

    is_constant = false;

    break;
  }
  case PDB_LocType::Constant: {
    Variant value = symbol.getValue();
    stream.PutRawBytes(&value.Value, sizeof(value.Value),
                       endian::InlHostByteOrder());
    break;
  }
  default:
    return DWARFExpression();
  }

  DataBufferSP buffer =
      std::make_shared<DataBufferHeap>(stream.GetData(), stream.GetSize());
  DataExtractor extractor(buffer, byte_order, address_size, byte_size);
  DWARFExpression result(extractor);
  result.SetRegisterKind(register_kind);

  return result;
}
