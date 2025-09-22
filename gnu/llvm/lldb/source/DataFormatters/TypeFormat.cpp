//===-- TypeFormat.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/DataFormatters/TypeFormat.h"




#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-public.h"

#include "lldb/Core/DumpDataExtractor.h"
#include "lldb/DataFormatters/FormatManager.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Symbol/TypeList.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/StreamString.h"
#include <optional>

using namespace lldb;
using namespace lldb_private;

TypeFormatImpl::TypeFormatImpl(const Flags &flags) : m_flags(flags) {}

TypeFormatImpl::~TypeFormatImpl() = default;

TypeFormatImpl_Format::TypeFormatImpl_Format(lldb::Format f,
                                             const TypeFormatImpl::Flags &flags)
    : TypeFormatImpl(flags), m_format(f) {}

TypeFormatImpl_Format::~TypeFormatImpl_Format() = default;

bool TypeFormatImpl_Format::FormatObject(ValueObject *valobj,
                                         std::string &dest) const {
  if (!valobj)
    return false;
  if (valobj->CanProvideValue()) {
    Value &value(valobj->GetValue());
    const Value::ContextType context_type = value.GetContextType();
    ExecutionContext exe_ctx(valobj->GetExecutionContextRef());
    DataExtractor data;

    if (context_type == Value::ContextType::RegisterInfo) {
      const RegisterInfo *reg_info = value.GetRegisterInfo();
      if (reg_info) {
        Status error;
        valobj->GetData(data, error);
        if (error.Fail())
          return false;

        StreamString reg_sstr;
        DumpDataExtractor(data, &reg_sstr, 0, GetFormat(), reg_info->byte_size,
                          1, UINT32_MAX, LLDB_INVALID_ADDRESS, 0, 0,
                          exe_ctx.GetBestExecutionContextScope());
        dest = std::string(reg_sstr.GetString());
      }
    } else {
      CompilerType compiler_type = value.GetCompilerType();
      if (compiler_type) {
        // put custom bytes to display in the DataExtractor to override the
        // default value logic
        if (GetFormat() == eFormatCString) {
          lldb_private::Flags type_flags(compiler_type.GetTypeInfo(
              nullptr)); // disambiguate w.r.t. TypeFormatImpl::Flags
          if (type_flags.Test(eTypeIsPointer) &&
              !type_flags.Test(eTypeIsObjC)) {
            // if we are dumping a pointer as a c-string, get the pointee data
            // as a string
            TargetSP target_sp(valobj->GetTargetSP());
            if (target_sp) {
              size_t max_len = target_sp->GetMaximumSizeOfStringSummary();
              Status error;
              WritableDataBufferSP buffer_sp(
                  new DataBufferHeap(max_len + 1, 0));
              Address address(valobj->GetPointerValue());
              target_sp->ReadCStringFromMemory(
                  address, (char *)buffer_sp->GetBytes(), max_len, error);
              if (error.Success())
                data.SetData(buffer_sp);
            }
          }
        } else {
          Status error;
          valobj->GetData(data, error);
          if (error.Fail())
            return false;
        }

        ExecutionContextScope *exe_scope =
            exe_ctx.GetBestExecutionContextScope();
        std::optional<uint64_t> size = compiler_type.GetByteSize(exe_scope);
        if (!size)
          return false;
        StreamString sstr;
        compiler_type.DumpTypeValue(
            &sstr,                          // The stream to use for display
            GetFormat(),                    // Format to display this type with
            data,                           // Data to extract from
            0,                              // Byte offset into "m_data"
            *size,                          // Byte size of item in "m_data"
            valobj->GetBitfieldBitSize(),   // Bitfield bit size
            valobj->GetBitfieldBitOffset(), // Bitfield bit offset
            exe_scope);
        // Given that we do not want to set the ValueObject's m_error for a
        // formatting error (or else we wouldn't be able to reformat until a
        // next update), an empty string is treated as a "false" return from
        // here, but that's about as severe as we get
        // CompilerType::DumpTypeValue() should always return something, even
        // if that something is an error message
        dest = std::string(sstr.GetString());
      }
    }
    return !dest.empty();
  } else
    return false;
}

std::string TypeFormatImpl_Format::GetDescription() {
  StreamString sstr;
  sstr.Printf("%s%s%s%s", FormatManager::GetFormatAsCString(GetFormat()),
              Cascades() ? "" : " (not cascading)",
              SkipsPointers() ? " (skip pointers)" : "",
              SkipsReferences() ? " (skip references)" : "");
  return std::string(sstr.GetString());
}

TypeFormatImpl_EnumType::TypeFormatImpl_EnumType(
    ConstString type_name, const TypeFormatImpl::Flags &flags)
    : TypeFormatImpl(flags), m_enum_type(type_name), m_types() {}

TypeFormatImpl_EnumType::~TypeFormatImpl_EnumType() = default;

bool TypeFormatImpl_EnumType::FormatObject(ValueObject *valobj,
                                           std::string &dest) const {
  dest.clear();
  if (!valobj)
    return false;
  if (!valobj->CanProvideValue())
    return false;
  ProcessSP process_sp;
  TargetSP target_sp;
  void *valobj_key = (process_sp = valobj->GetProcessSP()).get();
  if (!valobj_key)
    valobj_key = (target_sp = valobj->GetTargetSP()).get();
  else
    target_sp = process_sp->GetTarget().shared_from_this();
  if (!valobj_key)
    return false;
  auto iter = m_types.find(valobj_key), end = m_types.end();
  CompilerType valobj_enum_type;
  if (iter == end) {
    // probably a redundant check
    if (!target_sp)
      return false;
    const ModuleList &images(target_sp->GetImages());
    TypeQuery query(m_enum_type.GetStringRef());
    TypeResults results;
    images.FindTypes(nullptr, query, results);
    if (results.GetTypeMap().Empty())
      return false;
    for (lldb::TypeSP type_sp : results.GetTypeMap().Types()) {
      if (!type_sp)
        continue;
      if ((type_sp->GetForwardCompilerType().GetTypeInfo() &
           eTypeIsEnumeration) == eTypeIsEnumeration) {
        valobj_enum_type = type_sp->GetFullCompilerType();
        m_types.emplace(valobj_key, valobj_enum_type);
        break;
      }
    }
  } else
    valobj_enum_type = iter->second;
  if (!valobj_enum_type.IsValid())
    return false;
  DataExtractor data;
  Status error;
  valobj->GetData(data, error);
  if (error.Fail())
    return false;
  ExecutionContext exe_ctx(valobj->GetExecutionContextRef());
  StreamString sstr;
  valobj_enum_type.DumpTypeValue(&sstr, lldb::eFormatEnum, data, 0,
                                 data.GetByteSize(), 0, 0,
                                 exe_ctx.GetBestExecutionContextScope());
  if (!sstr.GetString().empty())
    dest = std::string(sstr.GetString());
  return !dest.empty();
}

std::string TypeFormatImpl_EnumType::GetDescription() {
  StreamString sstr;
  sstr.Printf("as type %s%s%s%s", m_enum_type.AsCString("<invalid type>"),
              Cascades() ? "" : " (not cascading)",
              SkipsPointers() ? " (skip pointers)" : "",
              SkipsReferences() ? " (skip references)" : "");
  return std::string(sstr.GetString());
}
