//===-- GenericOptional.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//

#include "Generic.h"
#include "LibCxx.h"
#include "LibStdcpp.h"
#include "Plugins/TypeSystem/Clang/TypeSystemClang.h"
#include "lldb/DataFormatters/FormattersHelpers.h"
#include "lldb/Target/Target.h"

using namespace lldb;
using namespace lldb_private;

bool lldb_private::formatters::GenericOptionalSummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options) {
  stream.Printf(" Has Value=%s ",
                valobj.GetNumChildrenIgnoringErrors() == 0 ? "false" : "true");

  return true;
}

// Synthetic Children Provider
namespace {

class GenericOptionalFrontend : public SyntheticChildrenFrontEnd {
public:
  enum class StdLib {
    LibCxx,
    LibStdcpp,
  };

  GenericOptionalFrontend(ValueObject &valobj, StdLib stdlib);

  size_t GetIndexOfChildWithName(ConstString name) override {
    return formatters::ExtractIndexFromString(name.GetCString());
  }

  bool MightHaveChildren() override { return true; }
  llvm::Expected<uint32_t> CalculateNumChildren() override {
    return m_has_value ? 1U : 0U;
  }

  ValueObjectSP GetChildAtIndex(uint32_t idx) override;
  lldb::ChildCacheState Update() override;

private:
  bool m_has_value = false;
  StdLib m_stdlib;
};

} // namespace

GenericOptionalFrontend::GenericOptionalFrontend(ValueObject &valobj,
                                                 StdLib stdlib)
    : SyntheticChildrenFrontEnd(valobj), m_stdlib(stdlib) {
  if (auto target_sp = m_backend.GetTargetSP()) {
    Update();
  }
}

lldb::ChildCacheState GenericOptionalFrontend::Update() {
  ValueObjectSP engaged_sp;

  if (m_stdlib == StdLib::LibCxx)
    engaged_sp = m_backend.GetChildMemberWithName("__engaged_");
  else if (m_stdlib == StdLib::LibStdcpp)
    engaged_sp = m_backend.GetChildMemberWithName("_M_payload")
                     ->GetChildMemberWithName("_M_engaged");

  if (!engaged_sp)
    return lldb::ChildCacheState::eRefetch;

  // _M_engaged/__engaged is a bool flag and is true if the optional contains a
  // value. Converting it to unsigned gives us a size of 1 if it contains a
  // value and 0 if not.
  m_has_value = engaged_sp->GetValueAsUnsigned(0) != 0;

  return lldb::ChildCacheState::eRefetch;
}

ValueObjectSP GenericOptionalFrontend::GetChildAtIndex(uint32_t _idx) {
  if (!m_has_value)
    return ValueObjectSP();

  ValueObjectSP val_sp;

  if (m_stdlib == StdLib::LibCxx)
    // __val_ contains the underlying value of an optional if it has one.
    // Currently because it is part of an anonymous union
    // GetChildMemberWithName() does not peer through and find it unless we are
    // at the parent itself. We can obtain the parent through __engaged_.
    val_sp = m_backend.GetChildMemberWithName("__engaged_")
                 ->GetParent()
                 ->GetChildAtIndex(0)
                 ->GetChildMemberWithName("__val_");
  else if (m_stdlib == StdLib::LibStdcpp) {
    val_sp = m_backend.GetChildMemberWithName("_M_payload")
                 ->GetChildMemberWithName("_M_payload");

    // In some implementations, _M_value contains the underlying value of an
    // optional, and in other versions, it's in the payload member.
    ValueObjectSP candidate = val_sp->GetChildMemberWithName("_M_value");
    if (candidate)
      val_sp = candidate;
  }

  if (!val_sp)
    return ValueObjectSP();

  CompilerType holder_type = val_sp->GetCompilerType();

  if (!holder_type)
    return ValueObjectSP();

  return val_sp->Clone(ConstString("Value"));
}

SyntheticChildrenFrontEnd *
formatters::LibStdcppOptionalSyntheticFrontEndCreator(
    CXXSyntheticChildren *, lldb::ValueObjectSP valobj_sp) {
  if (valobj_sp)
    return new GenericOptionalFrontend(
        *valobj_sp, GenericOptionalFrontend::StdLib::LibStdcpp);
  return nullptr;
}

SyntheticChildrenFrontEnd *formatters::LibcxxOptionalSyntheticFrontEndCreator(
    CXXSyntheticChildren *, lldb::ValueObjectSP valobj_sp) {
  if (valobj_sp)
    return new GenericOptionalFrontend(*valobj_sp,
                                       GenericOptionalFrontend::StdLib::LibCxx);
  return nullptr;
}
