//===-- DWARFLocationExpression.cpp ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DWARFLocationExpression.h"

#include "lldb/Core/Module.h"
#include "lldb/Core/Section.h"
#include "lldb/Expression/DWARFExpression.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/StreamBuffer.h"

#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/CodeView/TypeDeserializer.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/DebugInfo/PDB/Native/TpiStream.h"
#include "llvm/Support/Endian.h"

#include "PdbUtil.h"
#include "CodeViewRegisterMapping.h"
#include "PdbFPOProgramToDWARFExpression.h"
#include <optional>

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::npdb;
using namespace llvm::codeview;
using namespace llvm::pdb;

uint32_t GetGenericRegisterNumber(llvm::codeview::RegisterId register_id) {
  if (register_id == llvm::codeview::RegisterId::VFRAME)
    return LLDB_REGNUM_GENERIC_FP;

  return LLDB_INVALID_REGNUM;
}

static uint32_t GetRegisterNumber(llvm::Triple::ArchType arch_type,
                                  llvm::codeview::RegisterId register_id,
                                  RegisterKind &register_kind) {
  register_kind = eRegisterKindLLDB;
  uint32_t reg_num = GetLLDBRegisterNumber(arch_type, register_id);
  if (reg_num != LLDB_INVALID_REGNUM)
    return reg_num;

  register_kind = eRegisterKindGeneric;
  return GetGenericRegisterNumber(register_id);
}

static bool IsSimpleTypeSignedInteger(SimpleTypeKind kind) {
  switch (kind) {
  case SimpleTypeKind::Int128:
  case SimpleTypeKind::Int64:
  case SimpleTypeKind::Int64Quad:
  case SimpleTypeKind::Int32:
  case SimpleTypeKind::Int32Long:
  case SimpleTypeKind::Int16:
  case SimpleTypeKind::Int16Short:
  case SimpleTypeKind::Float128:
  case SimpleTypeKind::Float80:
  case SimpleTypeKind::Float64:
  case SimpleTypeKind::Float32:
  case SimpleTypeKind::Float16:
  case SimpleTypeKind::NarrowCharacter:
  case SimpleTypeKind::SignedCharacter:
  case SimpleTypeKind::SByte:
    return true;
  default:
    return false;
  }
}

static std::pair<size_t, bool> GetIntegralTypeInfo(TypeIndex ti,
                                                   TpiStream &tpi) {
  if (ti.isSimple()) {
    SimpleTypeKind stk = ti.getSimpleKind();
    return {GetTypeSizeForSimpleKind(stk), IsSimpleTypeSignedInteger(stk)};
  }

  CVType cvt = tpi.getType(ti);
  switch (cvt.kind()) {
  case LF_MODIFIER: {
    ModifierRecord mfr;
    llvm::cantFail(TypeDeserializer::deserializeAs<ModifierRecord>(cvt, mfr));
    return GetIntegralTypeInfo(mfr.ModifiedType, tpi);
  }
  case LF_POINTER: {
    PointerRecord pr;
    llvm::cantFail(TypeDeserializer::deserializeAs<PointerRecord>(cvt, pr));
    return GetIntegralTypeInfo(pr.ReferentType, tpi);
  }
  case LF_ENUM: {
    EnumRecord er;
    llvm::cantFail(TypeDeserializer::deserializeAs<EnumRecord>(cvt, er));
    return GetIntegralTypeInfo(er.UnderlyingType, tpi);
  }
  default:
    assert(false && "Type is not integral!");
    return {0, false};
  }
}

template <typename StreamWriter>
static DWARFExpression MakeLocationExpressionInternal(lldb::ModuleSP module,
                                                      StreamWriter &&writer) {
  const ArchSpec &architecture = module->GetArchitecture();
  ByteOrder byte_order = architecture.GetByteOrder();
  uint32_t address_size = architecture.GetAddressByteSize();
  uint32_t byte_size = architecture.GetDataByteSize();
  if (byte_order == eByteOrderInvalid || address_size == 0)
    return DWARFExpression();

  RegisterKind register_kind = eRegisterKindDWARF;
  StreamBuffer<32> stream(Stream::eBinary, address_size, byte_order);

  if (!writer(stream, register_kind))
    return DWARFExpression();

  DataBufferSP buffer =
      std::make_shared<DataBufferHeap>(stream.GetData(), stream.GetSize());
  DataExtractor extractor(buffer, byte_order, address_size, byte_size);
  DWARFExpression result(extractor);
  result.SetRegisterKind(register_kind);

  return result;
}

static bool MakeRegisterBasedLocationExpressionInternal(
    Stream &stream, llvm::codeview::RegisterId reg, RegisterKind &register_kind,
    std::optional<int32_t> relative_offset, lldb::ModuleSP module) {
  uint32_t reg_num = GetRegisterNumber(module->GetArchitecture().GetMachine(),
                                       reg, register_kind);
  if (reg_num == LLDB_INVALID_REGNUM)
    return false;

  if (reg_num > 31) {
    llvm::dwarf::LocationAtom base =
        relative_offset ? llvm::dwarf::DW_OP_bregx : llvm::dwarf::DW_OP_regx;
    stream.PutHex8(base);
    stream.PutULEB128(reg_num);
  } else {
    llvm::dwarf::LocationAtom base =
        relative_offset ? llvm::dwarf::DW_OP_breg0 : llvm::dwarf::DW_OP_reg0;
    stream.PutHex8(base + reg_num);
  }

  if (relative_offset)
    stream.PutSLEB128(*relative_offset);

  return true;
}

static DWARFExpression MakeRegisterBasedLocationExpressionInternal(
    llvm::codeview::RegisterId reg, std::optional<int32_t> relative_offset,
    lldb::ModuleSP module) {
  return MakeLocationExpressionInternal(
      module, [&](Stream &stream, RegisterKind &register_kind) -> bool {
        return MakeRegisterBasedLocationExpressionInternal(
            stream, reg, register_kind, relative_offset, module);
      });
}

DWARFExpression lldb_private::npdb::MakeEnregisteredLocationExpression(
    llvm::codeview::RegisterId reg, lldb::ModuleSP module) {
  return MakeRegisterBasedLocationExpressionInternal(reg, std::nullopt, module);
}

DWARFExpression lldb_private::npdb::MakeRegRelLocationExpression(
    llvm::codeview::RegisterId reg, int32_t offset, lldb::ModuleSP module) {
  return MakeRegisterBasedLocationExpressionInternal(reg, offset, module);
}

static bool EmitVFrameEvaluationDWARFExpression(
    llvm::StringRef program, llvm::Triple::ArchType arch_type, Stream &stream) {
  // VFrame value always stored in $TO pseudo-register
  return TranslateFPOProgramToDWARFExpression(program, "$T0", arch_type,
                                              stream);
}

DWARFExpression lldb_private::npdb::MakeVFrameRelLocationExpression(
    llvm::StringRef fpo_program, int32_t offset, lldb::ModuleSP module) {
  return MakeLocationExpressionInternal(
      module, [&](Stream &stream, RegisterKind &register_kind) -> bool {
        const ArchSpec &architecture = module->GetArchitecture();

        if (!EmitVFrameEvaluationDWARFExpression(fpo_program, architecture.GetMachine(),
                                                 stream))
          return false;

        stream.PutHex8(llvm::dwarf::DW_OP_consts);
        stream.PutSLEB128(offset);
        stream.PutHex8(llvm::dwarf::DW_OP_plus);

        register_kind = eRegisterKindLLDB;

        return true;
      });
}

DWARFExpression lldb_private::npdb::MakeGlobalLocationExpression(
    uint16_t section, uint32_t offset, ModuleSP module) {
  assert(section > 0);
  assert(module);

  return MakeLocationExpressionInternal(
      module, [&](Stream &stream, RegisterKind &register_kind) -> bool {
        stream.PutHex8(llvm::dwarf::DW_OP_addr);

        SectionList *section_list = module->GetSectionList();
        assert(section_list);

        auto section_ptr = section_list->FindSectionByID(section);
        if (!section_ptr)
          return false;

        stream.PutMaxHex64(section_ptr->GetFileAddress() + offset,
                           stream.GetAddressByteSize(), stream.GetByteOrder());

        return true;
      });
}

DWARFExpression lldb_private::npdb::MakeConstantLocationExpression(
    TypeIndex underlying_ti, TpiStream &tpi, const llvm::APSInt &constant,
    ModuleSP module) {
  const ArchSpec &architecture = module->GetArchitecture();
  uint32_t address_size = architecture.GetAddressByteSize();

  size_t size = 0;
  bool is_signed = false;
  std::tie(size, is_signed) = GetIntegralTypeInfo(underlying_ti, tpi);

  union {
    llvm::support::little64_t I;
    llvm::support::ulittle64_t U;
  } Value;

  std::shared_ptr<DataBufferHeap> buffer = std::make_shared<DataBufferHeap>();
  buffer->SetByteSize(size);

  llvm::ArrayRef<uint8_t> bytes;
  if (is_signed) {
    Value.I = constant.getSExtValue();
  } else {
    Value.U = constant.getZExtValue();
  }

  bytes = llvm::ArrayRef(reinterpret_cast<const uint8_t *>(&Value), 8)
              .take_front(size);
  buffer->CopyData(bytes.data(), size);
  DataExtractor extractor(buffer, lldb::eByteOrderLittle, address_size);
  DWARFExpression result(extractor);
  return result;
}

DWARFExpression
lldb_private::npdb::MakeEnregisteredLocationExpressionForComposite(
    const std::map<uint64_t, MemberValLocation> &offset_to_location,
    std::map<uint64_t, size_t> &offset_to_size, size_t total_size,
    lldb::ModuleSP module) {
  return MakeLocationExpressionInternal(
      module, [&](Stream &stream, RegisterKind &register_kind) -> bool {
        size_t cur_offset = 0;
        bool is_simple_type = offset_to_size.empty();
        // Iterate through offset_to_location because offset_to_size might be
        // empty if the variable is a simple type.
        for (const auto &offset_loc : offset_to_location) {
          if (cur_offset < offset_loc.first) {
            stream.PutHex8(llvm::dwarf::DW_OP_piece);
            stream.PutULEB128(offset_loc.first - cur_offset);
            cur_offset = offset_loc.first;
          }
          MemberValLocation loc = offset_loc.second;
          std::optional<int32_t> offset =
              loc.is_at_reg ? std::nullopt
                            : std::optional<int32_t>(loc.reg_offset);
          if (!MakeRegisterBasedLocationExpressionInternal(
                  stream, (RegisterId)loc.reg_id, register_kind, offset,
                  module))
            return false;
          if (!is_simple_type) {
            stream.PutHex8(llvm::dwarf::DW_OP_piece);
            stream.PutULEB128(offset_to_size[offset_loc.first]);
            cur_offset = offset_loc.first + offset_to_size[offset_loc.first];
          }
        }
        // For simple type, it specifies the byte size of the value described by
        // the previous dwarf expr. For udt, it's the remaining byte size at end
        // of a struct.
        if (total_size > cur_offset) {
          stream.PutHex8(llvm::dwarf::DW_OP_piece);
          stream.PutULEB128(total_size - cur_offset);
        }
        return true;
      });
}
