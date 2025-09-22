//===-- NSException.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/AST/DeclCXX.h"

#include "Cocoa.h"

#include "lldb/Core/ValueObject.h"
#include "lldb/Core/ValueObjectConstResult.h"
#include "lldb/DataFormatters/FormattersHelpers.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/Endian.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/Stream.h"

#include "Plugins/Language/ObjC/NSString.h"
#include "Plugins/LanguageRuntime/ObjC/ObjCLanguageRuntime.h"
#include "Plugins/TypeSystem/Clang/TypeSystemClang.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::formatters;

static bool ExtractFields(ValueObject &valobj, ValueObjectSP *name_sp,
                          ValueObjectSP *reason_sp, ValueObjectSP *userinfo_sp,
                          ValueObjectSP *reserved_sp) {
  ProcessSP process_sp(valobj.GetProcessSP());
  if (!process_sp)
    return false;

  lldb::addr_t ptr = LLDB_INVALID_ADDRESS;

  CompilerType valobj_type(valobj.GetCompilerType());
  Flags type_flags(valobj_type.GetTypeInfo());
  if (type_flags.AllClear(eTypeHasValue)) {
    if (valobj.IsBaseClass() && valobj.GetParent())
      ptr = valobj.GetParent()->GetValueAsUnsigned(LLDB_INVALID_ADDRESS);
  } else {
    ptr = valobj.GetValueAsUnsigned(LLDB_INVALID_ADDRESS);
  }

  if (ptr == LLDB_INVALID_ADDRESS)
    return false;
  size_t ptr_size = process_sp->GetAddressByteSize();

  Status error;
  auto name = process_sp->ReadPointerFromMemory(ptr + 1 * ptr_size, error);
  if (error.Fail() || name == LLDB_INVALID_ADDRESS)
    return false;
  auto reason = process_sp->ReadPointerFromMemory(ptr + 2 * ptr_size, error);
  if (error.Fail() || reason == LLDB_INVALID_ADDRESS)
    return false;
  auto userinfo = process_sp->ReadPointerFromMemory(ptr + 3 * ptr_size, error);
  if (error.Fail() || userinfo == LLDB_INVALID_ADDRESS)
    return false;
  auto reserved = process_sp->ReadPointerFromMemory(ptr + 4 * ptr_size, error);
  if (error.Fail() || reserved == LLDB_INVALID_ADDRESS)
    return false;

  InferiorSizedWord name_isw(name, *process_sp);
  InferiorSizedWord reason_isw(reason, *process_sp);
  InferiorSizedWord userinfo_isw(userinfo, *process_sp);
  InferiorSizedWord reserved_isw(reserved, *process_sp);

  TypeSystemClangSP scratch_ts_sp =
      ScratchTypeSystemClang::GetForTarget(process_sp->GetTarget());
  if (!scratch_ts_sp)
    return false;

  CompilerType voidstar =
      scratch_ts_sp->GetBasicType(lldb::eBasicTypeVoid).GetPointerType();

  if (name_sp)
    *name_sp = ValueObject::CreateValueObjectFromData(
        "name", name_isw.GetAsData(process_sp->GetByteOrder()),
        valobj.GetExecutionContextRef(), voidstar);
  if (reason_sp)
    *reason_sp = ValueObject::CreateValueObjectFromData(
        "reason", reason_isw.GetAsData(process_sp->GetByteOrder()),
        valobj.GetExecutionContextRef(), voidstar);
  if (userinfo_sp)
    *userinfo_sp = ValueObject::CreateValueObjectFromData(
        "userInfo", userinfo_isw.GetAsData(process_sp->GetByteOrder()),
        valobj.GetExecutionContextRef(), voidstar);
  if (reserved_sp)
    *reserved_sp = ValueObject::CreateValueObjectFromData(
        "reserved", reserved_isw.GetAsData(process_sp->GetByteOrder()),
        valobj.GetExecutionContextRef(), voidstar);

  return true;
}

bool lldb_private::formatters::NSException_SummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options) {
  lldb::ValueObjectSP reason_sp;
  if (!ExtractFields(valobj, nullptr, &reason_sp, nullptr, nullptr))
    return false;

  if (!reason_sp) {
    stream.Printf("No reason");
    return false;
  }

  StreamString reason_str_summary;
  if (NSStringSummaryProvider(*reason_sp, reason_str_summary, options) &&
      !reason_str_summary.Empty()) {
    stream.Printf("%s", reason_str_summary.GetData());
    return true;
  } else
    return false;
}

class NSExceptionSyntheticFrontEnd : public SyntheticChildrenFrontEnd {
public:
  NSExceptionSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp)
      : SyntheticChildrenFrontEnd(*valobj_sp) {}

  ~NSExceptionSyntheticFrontEnd() override = default;

  llvm::Expected<uint32_t> CalculateNumChildren() override { return 4; }

  lldb::ValueObjectSP GetChildAtIndex(uint32_t idx) override {
    switch (idx) {
      case 0: return m_name_sp;
      case 1: return m_reason_sp;
      case 2: return m_userinfo_sp;
      case 3: return m_reserved_sp;
    }
    return lldb::ValueObjectSP();
  }

  lldb::ChildCacheState Update() override {
    m_name_sp.reset();
    m_reason_sp.reset();
    m_userinfo_sp.reset();
    m_reserved_sp.reset();

    const auto ret = ExtractFields(m_backend, &m_name_sp, &m_reason_sp,
                                   &m_userinfo_sp, &m_reserved_sp);

    return ret ? lldb::ChildCacheState::eReuse
               : lldb::ChildCacheState::eRefetch;
  }

  bool MightHaveChildren() override { return true; }

  size_t GetIndexOfChildWithName(ConstString name) override {
    // NSException has 4 members:
    //   NSString *name;
    //   NSString *reason;
    //   NSDictionary *userInfo;
    //   id reserved;
    static ConstString g_name("name");
    static ConstString g_reason("reason");
    static ConstString g_userInfo("userInfo");
    static ConstString g_reserved("reserved");
    if (name == g_name) return 0;
    if (name == g_reason) return 1;
    if (name == g_userInfo) return 2;
    if (name == g_reserved) return 3;
    return UINT32_MAX;
  }

private:
  ValueObjectSP m_name_sp;
  ValueObjectSP m_reason_sp;
  ValueObjectSP m_userinfo_sp;
  ValueObjectSP m_reserved_sp;
};

SyntheticChildrenFrontEnd *
lldb_private::formatters::NSExceptionSyntheticFrontEndCreator(
    CXXSyntheticChildren *, lldb::ValueObjectSP valobj_sp) {
  lldb::ProcessSP process_sp(valobj_sp->GetProcessSP());
  if (!process_sp)
    return nullptr;
  ObjCLanguageRuntime *runtime = ObjCLanguageRuntime::Get(*process_sp);
  if (!runtime)
    return nullptr;

  ObjCLanguageRuntime::ClassDescriptorSP descriptor(
      runtime->GetClassDescriptor(*valobj_sp.get()));

  if (!descriptor.get() || !descriptor->IsValid())
    return nullptr;

  const char *class_name = descriptor->GetClassName().GetCString();

  if (!class_name || !*class_name)
    return nullptr;

  if (!strcmp(class_name, "NSException"))
    return (new NSExceptionSyntheticFrontEnd(valobj_sp));
  else if (!strcmp(class_name, "NSCFException"))
    return (new NSExceptionSyntheticFrontEnd(valobj_sp));
  else if (!strcmp(class_name, "__NSCFException"))
    return (new NSExceptionSyntheticFrontEnd(valobj_sp));

  return nullptr;
}
