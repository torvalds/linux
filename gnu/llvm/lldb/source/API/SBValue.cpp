//===-- SBValue.cpp -------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBValue.h"
#include "lldb/Utility/Instrumentation.h"

#include "lldb/API/SBDeclaration.h"
#include "lldb/API/SBStream.h"
#include "lldb/API/SBTypeFilter.h"
#include "lldb/API/SBTypeFormat.h"
#include "lldb/API/SBTypeSummary.h"
#include "lldb/API/SBTypeSynthetic.h"

#include "lldb/Breakpoint/Watchpoint.h"
#include "lldb/Core/Declaration.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/Section.h"
#include "lldb/Core/Value.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/Core/ValueObjectConstResult.h"
#include "lldb/DataFormatters/DataVisualization.h"
#include "lldb/DataFormatters/DumpValueObjectOptions.h"
#include "lldb/Symbol/Block.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Symbol/Variable.h"
#include "lldb/Symbol/VariableList.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Scalar.h"
#include "lldb/Utility/Stream.h"

#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBExpressionOptions.h"
#include "lldb/API/SBFrame.h"
#include "lldb/API/SBProcess.h"
#include "lldb/API/SBTarget.h"
#include "lldb/API/SBThread.h"

#include <memory>

using namespace lldb;
using namespace lldb_private;

class ValueImpl {
public:
  ValueImpl() = default;

  ValueImpl(lldb::ValueObjectSP in_valobj_sp,
            lldb::DynamicValueType use_dynamic, bool use_synthetic,
            const char *name = nullptr)
      : m_use_dynamic(use_dynamic), m_use_synthetic(use_synthetic),
        m_name(name) {
    if (in_valobj_sp) {
      if ((m_valobj_sp = in_valobj_sp->GetQualifiedRepresentationIfAvailable(
               lldb::eNoDynamicValues, false))) {
        if (!m_name.IsEmpty())
          m_valobj_sp->SetName(m_name);
      }
    }
  }

  ValueImpl(const ValueImpl &rhs) = default;

  ValueImpl &operator=(const ValueImpl &rhs) {
    if (this != &rhs) {
      m_valobj_sp = rhs.m_valobj_sp;
      m_use_dynamic = rhs.m_use_dynamic;
      m_use_synthetic = rhs.m_use_synthetic;
      m_name = rhs.m_name;
    }
    return *this;
  }

  bool IsValid() {
    if (m_valobj_sp.get() == nullptr)
      return false;
    else {
      // FIXME: This check is necessary but not sufficient.  We for sure don't
      // want to touch SBValues whose owning
      // targets have gone away.  This check is a little weak in that it
      // enforces that restriction when you call IsValid, but since IsValid
      // doesn't lock the target, you have no guarantee that the SBValue won't
      // go invalid after you call this... Also, an SBValue could depend on
      // data from one of the modules in the target, and those could go away
      // independently of the target, for instance if a module is unloaded.
      // But right now, neither SBValues nor ValueObjects know which modules
      // they depend on.  So I have no good way to make that check without
      // tracking that in all the ValueObject subclasses.
      TargetSP target_sp = m_valobj_sp->GetTargetSP();
      return target_sp && target_sp->IsValid();
    }
  }

  lldb::ValueObjectSP GetRootSP() { return m_valobj_sp; }

  lldb::ValueObjectSP GetSP(Process::StopLocker &stop_locker,
                            std::unique_lock<std::recursive_mutex> &lock,
                            Status &error) {
    if (!m_valobj_sp) {
      error.SetErrorString("invalid value object");
      return m_valobj_sp;
    }

    lldb::ValueObjectSP value_sp = m_valobj_sp;

    Target *target = value_sp->GetTargetSP().get();
    // If this ValueObject holds an error, then it is valuable for that.
    if (value_sp->GetError().Fail())
      return value_sp;

    if (!target)
      return ValueObjectSP();

    lock = std::unique_lock<std::recursive_mutex>(target->GetAPIMutex());

    ProcessSP process_sp(value_sp->GetProcessSP());
    if (process_sp && !stop_locker.TryLock(&process_sp->GetRunLock())) {
      // We don't allow people to play around with ValueObject if the process
      // is running. If you want to look at values, pause the process, then
      // look.
      error.SetErrorString("process must be stopped.");
      return ValueObjectSP();
    }

    if (m_use_dynamic != eNoDynamicValues) {
      ValueObjectSP dynamic_sp = value_sp->GetDynamicValue(m_use_dynamic);
      if (dynamic_sp)
        value_sp = dynamic_sp;
    }

    if (m_use_synthetic) {
      ValueObjectSP synthetic_sp = value_sp->GetSyntheticValue();
      if (synthetic_sp)
        value_sp = synthetic_sp;
    }

    if (!value_sp)
      error.SetErrorString("invalid value object");
    if (!m_name.IsEmpty())
      value_sp->SetName(m_name);

    return value_sp;
  }

  void SetUseDynamic(lldb::DynamicValueType use_dynamic) {
    m_use_dynamic = use_dynamic;
  }

  void SetUseSynthetic(bool use_synthetic) { m_use_synthetic = use_synthetic; }

  lldb::DynamicValueType GetUseDynamic() { return m_use_dynamic; }

  bool GetUseSynthetic() { return m_use_synthetic; }

  // All the derived values that we would make from the m_valobj_sp will share
  // the ExecutionContext with m_valobj_sp, so we don't need to do the
  // calculations in GetSP to return the Target, Process, Thread or Frame.  It
  // is convenient to provide simple accessors for these, which I do here.
  TargetSP GetTargetSP() {
    if (m_valobj_sp)
      return m_valobj_sp->GetTargetSP();
    else
      return TargetSP();
  }

  ProcessSP GetProcessSP() {
    if (m_valobj_sp)
      return m_valobj_sp->GetProcessSP();
    else
      return ProcessSP();
  }

  ThreadSP GetThreadSP() {
    if (m_valobj_sp)
      return m_valobj_sp->GetThreadSP();
    else
      return ThreadSP();
  }

  StackFrameSP GetFrameSP() {
    if (m_valobj_sp)
      return m_valobj_sp->GetFrameSP();
    else
      return StackFrameSP();
  }

private:
  lldb::ValueObjectSP m_valobj_sp;
  lldb::DynamicValueType m_use_dynamic;
  bool m_use_synthetic;
  ConstString m_name;
};

class ValueLocker {
public:
  ValueLocker() = default;

  ValueObjectSP GetLockedSP(ValueImpl &in_value) {
    return in_value.GetSP(m_stop_locker, m_lock, m_lock_error);
  }

  Status &GetError() { return m_lock_error; }

private:
  Process::StopLocker m_stop_locker;
  std::unique_lock<std::recursive_mutex> m_lock;
  Status m_lock_error;
};

SBValue::SBValue() { LLDB_INSTRUMENT_VA(this); }

SBValue::SBValue(const lldb::ValueObjectSP &value_sp) {
  LLDB_INSTRUMENT_VA(this, value_sp);

  SetSP(value_sp);
}

SBValue::SBValue(const SBValue &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  SetSP(rhs.m_opaque_sp);
}

SBValue &SBValue::operator=(const SBValue &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (this != &rhs) {
    SetSP(rhs.m_opaque_sp);
  }
  return *this;
}

SBValue::~SBValue() = default;

bool SBValue::IsValid() {
  LLDB_INSTRUMENT_VA(this);
  return this->operator bool();
}
SBValue::operator bool() const {
  LLDB_INSTRUMENT_VA(this);

  // If this function ever changes to anything that does more than just check
  // if the opaque shared pointer is non NULL, then we need to update all "if
  // (m_opaque_sp)" code in this file.
  return m_opaque_sp.get() != nullptr && m_opaque_sp->IsValid() &&
         m_opaque_sp->GetRootSP().get() != nullptr;
}

void SBValue::Clear() {
  LLDB_INSTRUMENT_VA(this);

  m_opaque_sp.reset();
}

SBError SBValue::GetError() {
  LLDB_INSTRUMENT_VA(this);

  SBError sb_error;

  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp)
    sb_error.SetError(value_sp->GetError());
  else
    sb_error.SetErrorStringWithFormat("error: %s",
                                      locker.GetError().AsCString());

  return sb_error;
}

user_id_t SBValue::GetID() {
  LLDB_INSTRUMENT_VA(this);

  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp)
    return value_sp->GetID();
  return LLDB_INVALID_UID;
}

const char *SBValue::GetName() {
  LLDB_INSTRUMENT_VA(this);

  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (!value_sp)
    return nullptr;

  return value_sp->GetName().GetCString();
}

const char *SBValue::GetTypeName() {
  LLDB_INSTRUMENT_VA(this);

  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (!value_sp)
    return nullptr;

  return value_sp->GetQualifiedTypeName().GetCString();
}

const char *SBValue::GetDisplayTypeName() {
  LLDB_INSTRUMENT_VA(this);

  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (!value_sp)
    return nullptr;

  return value_sp->GetDisplayTypeName().GetCString();
}

size_t SBValue::GetByteSize() {
  LLDB_INSTRUMENT_VA(this);

  size_t result = 0;

  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    result = value_sp->GetByteSize().value_or(0);
  }

  return result;
}

bool SBValue::IsInScope() {
  LLDB_INSTRUMENT_VA(this);

  bool result = false;

  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    result = value_sp->IsInScope();
  }

  return result;
}

const char *SBValue::GetValue() {
  LLDB_INSTRUMENT_VA(this);

  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (!value_sp)
    return nullptr;
  return ConstString(value_sp->GetValueAsCString()).GetCString();
}

ValueType SBValue::GetValueType() {
  LLDB_INSTRUMENT_VA(this);

  ValueType result = eValueTypeInvalid;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp)
    result = value_sp->GetValueType();

  return result;
}

const char *SBValue::GetObjectDescription() {
  LLDB_INSTRUMENT_VA(this);

  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (!value_sp)
    return nullptr;

  llvm::Expected<std::string> str = value_sp->GetObjectDescription();
  if (!str)
    return ConstString("error: " + toString(str.takeError())).AsCString();
  return ConstString(*str).AsCString();
}

SBType SBValue::GetType() {
  LLDB_INSTRUMENT_VA(this);

  SBType sb_type;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  TypeImplSP type_sp;
  if (value_sp) {
    type_sp = std::make_shared<TypeImpl>(value_sp->GetTypeImpl());
    sb_type.SetSP(type_sp);
  }

  return sb_type;
}

bool SBValue::GetValueDidChange() {
  LLDB_INSTRUMENT_VA(this);

  bool result = false;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    if (value_sp->UpdateValueIfNeeded(false))
      result = value_sp->GetValueDidChange();
  }

  return result;
}

const char *SBValue::GetSummary() {
  LLDB_INSTRUMENT_VA(this);

  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (!value_sp)
    return nullptr;

  return ConstString(value_sp->GetSummaryAsCString()).GetCString();
}

const char *SBValue::GetSummary(lldb::SBStream &stream,
                                lldb::SBTypeSummaryOptions &options) {
  LLDB_INSTRUMENT_VA(this, stream, options);

  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    std::string buffer;
    if (value_sp->GetSummaryAsCString(buffer, options.ref()) && !buffer.empty())
      stream.Printf("%s", buffer.c_str());
  }
  return ConstString(stream.GetData()).GetCString();
}

const char *SBValue::GetLocation() {
  LLDB_INSTRUMENT_VA(this);

  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (!value_sp)
    return nullptr;

  return ConstString(value_sp->GetLocationAsCString()).GetCString();
}

// Deprecated - use the one that takes an lldb::SBError
bool SBValue::SetValueFromCString(const char *value_str) {
  LLDB_INSTRUMENT_VA(this, value_str);

  lldb::SBError dummy;
  return SetValueFromCString(value_str, dummy);
}

bool SBValue::SetValueFromCString(const char *value_str, lldb::SBError &error) {
  LLDB_INSTRUMENT_VA(this, value_str, error);

  bool success = false;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    success = value_sp->SetValueFromCString(value_str, error.ref());
  } else
    error.SetErrorStringWithFormat("Could not get value: %s",
                                   locker.GetError().AsCString());

  return success;
}

lldb::SBTypeFormat SBValue::GetTypeFormat() {
  LLDB_INSTRUMENT_VA(this);

  lldb::SBTypeFormat format;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    if (value_sp->UpdateValueIfNeeded(true)) {
      lldb::TypeFormatImplSP format_sp = value_sp->GetValueFormat();
      if (format_sp)
        format.SetSP(format_sp);
    }
  }
  return format;
}

lldb::SBTypeSummary SBValue::GetTypeSummary() {
  LLDB_INSTRUMENT_VA(this);

  lldb::SBTypeSummary summary;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    if (value_sp->UpdateValueIfNeeded(true)) {
      lldb::TypeSummaryImplSP summary_sp = value_sp->GetSummaryFormat();
      if (summary_sp)
        summary.SetSP(summary_sp);
    }
  }
  return summary;
}

lldb::SBTypeFilter SBValue::GetTypeFilter() {
  LLDB_INSTRUMENT_VA(this);

  lldb::SBTypeFilter filter;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    if (value_sp->UpdateValueIfNeeded(true)) {
      lldb::SyntheticChildrenSP synthetic_sp = value_sp->GetSyntheticChildren();

      if (synthetic_sp && !synthetic_sp->IsScripted()) {
        TypeFilterImplSP filter_sp =
            std::static_pointer_cast<TypeFilterImpl>(synthetic_sp);
        filter.SetSP(filter_sp);
      }
    }
  }
  return filter;
}

lldb::SBTypeSynthetic SBValue::GetTypeSynthetic() {
  LLDB_INSTRUMENT_VA(this);

  lldb::SBTypeSynthetic synthetic;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    if (value_sp->UpdateValueIfNeeded(true)) {
      lldb::SyntheticChildrenSP children_sp = value_sp->GetSyntheticChildren();

      if (children_sp && children_sp->IsScripted()) {
        ScriptedSyntheticChildrenSP synth_sp =
            std::static_pointer_cast<ScriptedSyntheticChildren>(children_sp);
        synthetic.SetSP(synth_sp);
      }
    }
  }
  return synthetic;
}

lldb::SBValue SBValue::CreateChildAtOffset(const char *name, uint32_t offset,
                                           SBType type) {
  LLDB_INSTRUMENT_VA(this, name, offset, type);

  lldb::SBValue sb_value;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  lldb::ValueObjectSP new_value_sp;
  if (value_sp) {
    TypeImplSP type_sp(type.GetSP());
    if (type.IsValid()) {
      sb_value.SetSP(value_sp->GetSyntheticChildAtOffset(
                         offset, type_sp->GetCompilerType(false), true),
                     GetPreferDynamicValue(), GetPreferSyntheticValue(), name);
    }
  }
  return sb_value;
}

lldb::SBValue SBValue::Cast(SBType type) {
  LLDB_INSTRUMENT_VA(this, type);

  lldb::SBValue sb_value;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  TypeImplSP type_sp(type.GetSP());
  if (value_sp && type_sp)
    sb_value.SetSP(value_sp->Cast(type_sp->GetCompilerType(false)),
                   GetPreferDynamicValue(), GetPreferSyntheticValue());
  return sb_value;
}

lldb::SBValue SBValue::CreateValueFromExpression(const char *name,
                                                 const char *expression) {
  LLDB_INSTRUMENT_VA(this, name, expression);

  SBExpressionOptions options;
  options.ref().SetKeepInMemory(true);
  return CreateValueFromExpression(name, expression, options);
}

lldb::SBValue SBValue::CreateValueFromExpression(const char *name,
                                                 const char *expression,
                                                 SBExpressionOptions &options) {
  LLDB_INSTRUMENT_VA(this, name, expression, options);

  lldb::SBValue sb_value;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  lldb::ValueObjectSP new_value_sp;
  if (value_sp) {
    ExecutionContext exe_ctx(value_sp->GetExecutionContextRef());
    new_value_sp = ValueObject::CreateValueObjectFromExpression(
        name, expression, exe_ctx, options.ref());
    if (new_value_sp)
      new_value_sp->SetName(ConstString(name));
  }
  sb_value.SetSP(new_value_sp);
  return sb_value;
}

lldb::SBValue SBValue::CreateValueFromAddress(const char *name,
                                              lldb::addr_t address,
                                              SBType sb_type) {
  LLDB_INSTRUMENT_VA(this, name, address, sb_type);

  lldb::SBValue sb_value;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  lldb::ValueObjectSP new_value_sp;
  lldb::TypeImplSP type_impl_sp(sb_type.GetSP());
  if (value_sp && type_impl_sp) {
    CompilerType ast_type(type_impl_sp->GetCompilerType(true));
    ExecutionContext exe_ctx(value_sp->GetExecutionContextRef());
    new_value_sp = ValueObject::CreateValueObjectFromAddress(name, address,
                                                             exe_ctx, ast_type);
  }
  sb_value.SetSP(new_value_sp);
  return sb_value;
}

lldb::SBValue SBValue::CreateValueFromData(const char *name, SBData data,
                                           SBType sb_type) {
  LLDB_INSTRUMENT_VA(this, name, data, sb_type);

  lldb::SBValue sb_value;
  lldb::ValueObjectSP new_value_sp;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  lldb::TypeImplSP type_impl_sp(sb_type.GetSP());
  if (value_sp && type_impl_sp) {
    ExecutionContext exe_ctx(value_sp->GetExecutionContextRef());
    new_value_sp = ValueObject::CreateValueObjectFromData(
        name, **data, exe_ctx, type_impl_sp->GetCompilerType(true));
    new_value_sp->SetAddressTypeOfChildren(eAddressTypeLoad);
  }
  sb_value.SetSP(new_value_sp);
  return sb_value;
}

SBValue SBValue::GetChildAtIndex(uint32_t idx) {
  LLDB_INSTRUMENT_VA(this, idx);

  const bool can_create_synthetic = false;
  lldb::DynamicValueType use_dynamic = eNoDynamicValues;
  TargetSP target_sp;
  if (m_opaque_sp)
    target_sp = m_opaque_sp->GetTargetSP();

  if (target_sp)
    use_dynamic = target_sp->GetPreferDynamicValue();

  return GetChildAtIndex(idx, use_dynamic, can_create_synthetic);
}

SBValue SBValue::GetChildAtIndex(uint32_t idx,
                                 lldb::DynamicValueType use_dynamic,
                                 bool can_create_synthetic) {
  LLDB_INSTRUMENT_VA(this, idx, use_dynamic, can_create_synthetic);

  lldb::ValueObjectSP child_sp;

  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    const bool can_create = true;
    child_sp = value_sp->GetChildAtIndex(idx);
    if (can_create_synthetic && !child_sp) {
      child_sp = value_sp->GetSyntheticArrayMember(idx, can_create);
    }
  }

  SBValue sb_value;
  sb_value.SetSP(child_sp, use_dynamic, GetPreferSyntheticValue());

  return sb_value;
}

uint32_t SBValue::GetIndexOfChildWithName(const char *name) {
  LLDB_INSTRUMENT_VA(this, name);

  uint32_t idx = UINT32_MAX;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    idx = value_sp->GetIndexOfChildWithName(name);
  }
  return idx;
}

SBValue SBValue::GetChildMemberWithName(const char *name) {
  LLDB_INSTRUMENT_VA(this, name);

  lldb::DynamicValueType use_dynamic_value = eNoDynamicValues;
  TargetSP target_sp;
  if (m_opaque_sp)
    target_sp = m_opaque_sp->GetTargetSP();

  if (target_sp)
    use_dynamic_value = target_sp->GetPreferDynamicValue();
  return GetChildMemberWithName(name, use_dynamic_value);
}

SBValue
SBValue::GetChildMemberWithName(const char *name,
                                lldb::DynamicValueType use_dynamic_value) {
  LLDB_INSTRUMENT_VA(this, name, use_dynamic_value);

  lldb::ValueObjectSP child_sp;

  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    child_sp = value_sp->GetChildMemberWithName(name);
  }

  SBValue sb_value;
  sb_value.SetSP(child_sp, use_dynamic_value, GetPreferSyntheticValue());

  return sb_value;
}

lldb::SBValue SBValue::GetDynamicValue(lldb::DynamicValueType use_dynamic) {
  LLDB_INSTRUMENT_VA(this, use_dynamic);

  SBValue value_sb;
  if (IsValid()) {
    ValueImplSP proxy_sp(new ValueImpl(m_opaque_sp->GetRootSP(), use_dynamic,
                                       m_opaque_sp->GetUseSynthetic()));
    value_sb.SetSP(proxy_sp);
  }
  return value_sb;
}

lldb::SBValue SBValue::GetStaticValue() {
  LLDB_INSTRUMENT_VA(this);

  SBValue value_sb;
  if (IsValid()) {
    ValueImplSP proxy_sp(new ValueImpl(m_opaque_sp->GetRootSP(),
                                       eNoDynamicValues,
                                       m_opaque_sp->GetUseSynthetic()));
    value_sb.SetSP(proxy_sp);
  }
  return value_sb;
}

lldb::SBValue SBValue::GetNonSyntheticValue() {
  LLDB_INSTRUMENT_VA(this);

  SBValue value_sb;
  if (IsValid()) {
    ValueImplSP proxy_sp(new ValueImpl(m_opaque_sp->GetRootSP(),
                                       m_opaque_sp->GetUseDynamic(), false));
    value_sb.SetSP(proxy_sp);
  }
  return value_sb;
}

lldb::SBValue SBValue::GetSyntheticValue() {
  LLDB_INSTRUMENT_VA(this);

  SBValue value_sb;
  if (IsValid()) {
    ValueImplSP proxy_sp(new ValueImpl(m_opaque_sp->GetRootSP(),
                                       m_opaque_sp->GetUseDynamic(), true));
    value_sb.SetSP(proxy_sp);
    if (!value_sb.IsSynthetic()) {
      return {};
    }
  }
  return value_sb;
}

lldb::DynamicValueType SBValue::GetPreferDynamicValue() {
  LLDB_INSTRUMENT_VA(this);

  if (!IsValid())
    return eNoDynamicValues;
  return m_opaque_sp->GetUseDynamic();
}

void SBValue::SetPreferDynamicValue(lldb::DynamicValueType use_dynamic) {
  LLDB_INSTRUMENT_VA(this, use_dynamic);

  if (IsValid())
    return m_opaque_sp->SetUseDynamic(use_dynamic);
}

bool SBValue::GetPreferSyntheticValue() {
  LLDB_INSTRUMENT_VA(this);

  if (!IsValid())
    return false;
  return m_opaque_sp->GetUseSynthetic();
}

void SBValue::SetPreferSyntheticValue(bool use_synthetic) {
  LLDB_INSTRUMENT_VA(this, use_synthetic);

  if (IsValid())
    return m_opaque_sp->SetUseSynthetic(use_synthetic);
}

bool SBValue::IsDynamic() {
  LLDB_INSTRUMENT_VA(this);

  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp)
    return value_sp->IsDynamic();
  return false;
}

bool SBValue::IsSynthetic() {
  LLDB_INSTRUMENT_VA(this);

  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp)
    return value_sp->IsSynthetic();
  return false;
}

bool SBValue::IsSyntheticChildrenGenerated() {
  LLDB_INSTRUMENT_VA(this);

  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp)
    return value_sp->IsSyntheticChildrenGenerated();
  return false;
}

void SBValue::SetSyntheticChildrenGenerated(bool is) {
  LLDB_INSTRUMENT_VA(this, is);

  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp)
    return value_sp->SetSyntheticChildrenGenerated(is);
}

lldb::SBValue SBValue::GetValueForExpressionPath(const char *expr_path) {
  LLDB_INSTRUMENT_VA(this, expr_path);

  lldb::ValueObjectSP child_sp;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    // using default values for all the fancy options, just do it if you can
    child_sp = value_sp->GetValueForExpressionPath(expr_path);
  }

  SBValue sb_value;
  sb_value.SetSP(child_sp, GetPreferDynamicValue(), GetPreferSyntheticValue());

  return sb_value;
}

int64_t SBValue::GetValueAsSigned(SBError &error, int64_t fail_value) {
  LLDB_INSTRUMENT_VA(this, error, fail_value);

  error.Clear();
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    bool success = true;
    uint64_t ret_val = fail_value;
    ret_val = value_sp->GetValueAsSigned(fail_value, &success);
    if (!success)
      error.SetErrorString("could not resolve value");
    return ret_val;
  } else
    error.SetErrorStringWithFormat("could not get SBValue: %s",
                                   locker.GetError().AsCString());

  return fail_value;
}

uint64_t SBValue::GetValueAsUnsigned(SBError &error, uint64_t fail_value) {
  LLDB_INSTRUMENT_VA(this, error, fail_value);

  error.Clear();
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    bool success = true;
    uint64_t ret_val = fail_value;
    ret_val = value_sp->GetValueAsUnsigned(fail_value, &success);
    if (!success)
      error.SetErrorString("could not resolve value");
    return ret_val;
  } else
    error.SetErrorStringWithFormat("could not get SBValue: %s",
                                   locker.GetError().AsCString());

  return fail_value;
}

int64_t SBValue::GetValueAsSigned(int64_t fail_value) {
  LLDB_INSTRUMENT_VA(this, fail_value);

  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    return value_sp->GetValueAsSigned(fail_value);
  }
  return fail_value;
}

uint64_t SBValue::GetValueAsUnsigned(uint64_t fail_value) {
  LLDB_INSTRUMENT_VA(this, fail_value);

  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    return value_sp->GetValueAsUnsigned(fail_value);
  }
  return fail_value;
}

lldb::addr_t SBValue::GetValueAsAddress() {
  addr_t fail_value = LLDB_INVALID_ADDRESS;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    bool success = true;
    uint64_t ret_val = fail_value;
    ret_val = value_sp->GetValueAsUnsigned(fail_value, &success);
    if (!success)
      return fail_value;
    ProcessSP process_sp = m_opaque_sp->GetProcessSP();
    if (!process_sp)
      return ret_val;
    return process_sp->FixDataAddress(ret_val);
  }

  return fail_value;
}

bool SBValue::MightHaveChildren() {
  LLDB_INSTRUMENT_VA(this);

  bool has_children = false;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp)
    has_children = value_sp->MightHaveChildren();

  return has_children;
}

bool SBValue::IsRuntimeSupportValue() {
  LLDB_INSTRUMENT_VA(this);

  bool is_support = false;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp)
    is_support = value_sp->IsRuntimeSupportValue();

  return is_support;
}

uint32_t SBValue::GetNumChildren() {
  LLDB_INSTRUMENT_VA(this);

  return GetNumChildren(UINT32_MAX);
}

uint32_t SBValue::GetNumChildren(uint32_t max) {
  LLDB_INSTRUMENT_VA(this, max);

  uint32_t num_children = 0;

  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp)
    num_children = value_sp->GetNumChildrenIgnoringErrors(max);

  return num_children;
}

SBValue SBValue::Dereference() {
  LLDB_INSTRUMENT_VA(this);

  SBValue sb_value;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    Status error;
    sb_value = value_sp->Dereference(error);
  }

  return sb_value;
}

// Deprecated - please use GetType().IsPointerType() instead.
bool SBValue::TypeIsPointerType() {
  LLDB_INSTRUMENT_VA(this);

  return GetType().IsPointerType();
}

void *SBValue::GetOpaqueType() {
  LLDB_INSTRUMENT_VA(this);

  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp)
    return value_sp->GetCompilerType().GetOpaqueQualType();
  return nullptr;
}

lldb::SBTarget SBValue::GetTarget() {
  LLDB_INSTRUMENT_VA(this);

  SBTarget sb_target;
  TargetSP target_sp;
  if (m_opaque_sp) {
    target_sp = m_opaque_sp->GetTargetSP();
    sb_target.SetSP(target_sp);
  }

  return sb_target;
}

lldb::SBProcess SBValue::GetProcess() {
  LLDB_INSTRUMENT_VA(this);

  SBProcess sb_process;
  ProcessSP process_sp;
  if (m_opaque_sp) {
    process_sp = m_opaque_sp->GetProcessSP();
    sb_process.SetSP(process_sp);
  }

  return sb_process;
}

lldb::SBThread SBValue::GetThread() {
  LLDB_INSTRUMENT_VA(this);

  SBThread sb_thread;
  ThreadSP thread_sp;
  if (m_opaque_sp) {
    thread_sp = m_opaque_sp->GetThreadSP();
    sb_thread.SetThread(thread_sp);
  }

  return sb_thread;
}

lldb::SBFrame SBValue::GetFrame() {
  LLDB_INSTRUMENT_VA(this);

  SBFrame sb_frame;
  StackFrameSP frame_sp;
  if (m_opaque_sp) {
    frame_sp = m_opaque_sp->GetFrameSP();
    sb_frame.SetFrameSP(frame_sp);
  }

  return sb_frame;
}

lldb::ValueObjectSP SBValue::GetSP(ValueLocker &locker) const {
  // IsValid means that the SBValue has a value in it.  But that's not the
  // only time that ValueObjects are useful.  We also want to return the value
  // if there's an error state in it.
  if (!m_opaque_sp || (!m_opaque_sp->IsValid()
      && (m_opaque_sp->GetRootSP()
          && !m_opaque_sp->GetRootSP()->GetError().Fail()))) {
    locker.GetError().SetErrorString("No value");
    return ValueObjectSP();
  }
  return locker.GetLockedSP(*m_opaque_sp.get());
}

lldb::ValueObjectSP SBValue::GetSP() const {
  LLDB_INSTRUMENT_VA(this);

  ValueLocker locker;
  return GetSP(locker);
}

void SBValue::SetSP(ValueImplSP impl_sp) { m_opaque_sp = impl_sp; }

void SBValue::SetSP(const lldb::ValueObjectSP &sp) {
  if (sp) {
    lldb::TargetSP target_sp(sp->GetTargetSP());
    if (target_sp) {
      lldb::DynamicValueType use_dynamic = target_sp->GetPreferDynamicValue();
      bool use_synthetic =
          target_sp->TargetProperties::GetEnableSyntheticValue();
      m_opaque_sp = ValueImplSP(new ValueImpl(sp, use_dynamic, use_synthetic));
    } else
      m_opaque_sp = ValueImplSP(new ValueImpl(sp, eNoDynamicValues, true));
  } else
    m_opaque_sp = ValueImplSP(new ValueImpl(sp, eNoDynamicValues, false));
}

void SBValue::SetSP(const lldb::ValueObjectSP &sp,
                    lldb::DynamicValueType use_dynamic) {
  if (sp) {
    lldb::TargetSP target_sp(sp->GetTargetSP());
    if (target_sp) {
      bool use_synthetic =
          target_sp->TargetProperties::GetEnableSyntheticValue();
      SetSP(sp, use_dynamic, use_synthetic);
    } else
      SetSP(sp, use_dynamic, true);
  } else
    SetSP(sp, use_dynamic, false);
}

void SBValue::SetSP(const lldb::ValueObjectSP &sp, bool use_synthetic) {
  if (sp) {
    lldb::TargetSP target_sp(sp->GetTargetSP());
    if (target_sp) {
      lldb::DynamicValueType use_dynamic = target_sp->GetPreferDynamicValue();
      SetSP(sp, use_dynamic, use_synthetic);
    } else
      SetSP(sp, eNoDynamicValues, use_synthetic);
  } else
    SetSP(sp, eNoDynamicValues, use_synthetic);
}

void SBValue::SetSP(const lldb::ValueObjectSP &sp,
                    lldb::DynamicValueType use_dynamic, bool use_synthetic) {
  m_opaque_sp = ValueImplSP(new ValueImpl(sp, use_dynamic, use_synthetic));
}

void SBValue::SetSP(const lldb::ValueObjectSP &sp,
                    lldb::DynamicValueType use_dynamic, bool use_synthetic,
                    const char *name) {
  m_opaque_sp =
      ValueImplSP(new ValueImpl(sp, use_dynamic, use_synthetic, name));
}

bool SBValue::GetExpressionPath(SBStream &description) {
  LLDB_INSTRUMENT_VA(this, description);

  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    value_sp->GetExpressionPath(description.ref());
    return true;
  }
  return false;
}

bool SBValue::GetExpressionPath(SBStream &description,
                                bool qualify_cxx_base_classes) {
  LLDB_INSTRUMENT_VA(this, description, qualify_cxx_base_classes);

  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    value_sp->GetExpressionPath(description.ref());
    return true;
  }
  return false;
}

lldb::SBValue SBValue::EvaluateExpression(const char *expr) const {
  LLDB_INSTRUMENT_VA(this, expr);

  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (!value_sp)
    return SBValue();

  lldb::TargetSP target_sp = value_sp->GetTargetSP();
  if (!target_sp)
    return SBValue();

  lldb::SBExpressionOptions options;
  options.SetFetchDynamicValue(target_sp->GetPreferDynamicValue());
  options.SetUnwindOnError(true);
  options.SetIgnoreBreakpoints(true);

  return EvaluateExpression(expr, options, nullptr);
}

lldb::SBValue
SBValue::EvaluateExpression(const char *expr,
                            const SBExpressionOptions &options) const {
  LLDB_INSTRUMENT_VA(this, expr, options);

  return EvaluateExpression(expr, options, nullptr);
}

lldb::SBValue SBValue::EvaluateExpression(const char *expr,
                                          const SBExpressionOptions &options,
                                          const char *name) const {
  LLDB_INSTRUMENT_VA(this, expr, options, name);

  if (!expr || expr[0] == '\0') {
    return SBValue();
  }


  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (!value_sp) {
    return SBValue();
  }

  lldb::TargetSP target_sp = value_sp->GetTargetSP();
  if (!target_sp) {
    return SBValue();
  }

  std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
  ExecutionContext exe_ctx(target_sp.get());

  StackFrame *frame = exe_ctx.GetFramePtr();
  if (!frame) {
    return SBValue();
  }

  ValueObjectSP res_val_sp;
  target_sp->EvaluateExpression(expr, frame, res_val_sp, options.ref(), nullptr,
                                value_sp.get());

  if (name)
    res_val_sp->SetName(ConstString(name));

  SBValue result;
  result.SetSP(res_val_sp, options.GetFetchDynamicValue());
  return result;
}

bool SBValue::GetDescription(SBStream &description) {
  LLDB_INSTRUMENT_VA(this, description);

  Stream &strm = description.ref();

  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    DumpValueObjectOptions options;
    options.SetUseDynamicType(m_opaque_sp->GetUseDynamic());
    options.SetUseSyntheticValue(m_opaque_sp->GetUseSynthetic());
    if (llvm::Error error = value_sp->Dump(strm, options)) {
      strm << "error: " << toString(std::move(error));
      return false;
    }
  } else {
    strm.PutCString("No value");
  }

  return true;
}

lldb::Format SBValue::GetFormat() {
  LLDB_INSTRUMENT_VA(this);

  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp)
    return value_sp->GetFormat();
  return eFormatDefault;
}

void SBValue::SetFormat(lldb::Format format) {
  LLDB_INSTRUMENT_VA(this, format);

  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp)
    value_sp->SetFormat(format);
}

lldb::SBValue SBValue::AddressOf() {
  LLDB_INSTRUMENT_VA(this);

  SBValue sb_value;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    Status error;
    sb_value.SetSP(value_sp->AddressOf(error), GetPreferDynamicValue(),
                   GetPreferSyntheticValue());
  }

  return sb_value;
}

lldb::addr_t SBValue::GetLoadAddress() {
  LLDB_INSTRUMENT_VA(this);

  lldb::addr_t value = LLDB_INVALID_ADDRESS;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp)
    return value_sp->GetLoadAddress();

  return value;
}

lldb::SBAddress SBValue::GetAddress() {
  LLDB_INSTRUMENT_VA(this);

  Address addr;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    TargetSP target_sp(value_sp->GetTargetSP());
    if (target_sp) {
      lldb::addr_t value = LLDB_INVALID_ADDRESS;
      const bool scalar_is_load_address = true;
      AddressType addr_type;
      value = value_sp->GetAddressOf(scalar_is_load_address, &addr_type);
      if (addr_type == eAddressTypeFile) {
        ModuleSP module_sp(value_sp->GetModule());
        if (module_sp)
          module_sp->ResolveFileAddress(value, addr);
      } else if (addr_type == eAddressTypeLoad) {
        // no need to check the return value on this.. if it can actually do
        // the resolve addr will be in the form (section,offset), otherwise it
        // will simply be returned as (NULL, value)
        addr.SetLoadAddress(value, target_sp.get());
      }
    }
  }

  return SBAddress(addr);
}

lldb::SBData SBValue::GetPointeeData(uint32_t item_idx, uint32_t item_count) {
  LLDB_INSTRUMENT_VA(this, item_idx, item_count);

  lldb::SBData sb_data;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    TargetSP target_sp(value_sp->GetTargetSP());
    if (target_sp) {
      DataExtractorSP data_sp(new DataExtractor());
      value_sp->GetPointeeData(*data_sp, item_idx, item_count);
      if (data_sp->GetByteSize() > 0)
        *sb_data = data_sp;
    }
  }

  return sb_data;
}

lldb::SBData SBValue::GetData() {
  LLDB_INSTRUMENT_VA(this);

  lldb::SBData sb_data;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    DataExtractorSP data_sp(new DataExtractor());
    Status error;
    value_sp->GetData(*data_sp, error);
    if (error.Success())
      *sb_data = data_sp;
  }

  return sb_data;
}

bool SBValue::SetData(lldb::SBData &data, SBError &error) {
  LLDB_INSTRUMENT_VA(this, data, error);

  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  bool ret = true;

  if (value_sp) {
    DataExtractor *data_extractor = data.get();

    if (!data_extractor) {
      error.SetErrorString("No data to set");
      ret = false;
    } else {
      Status set_error;

      value_sp->SetData(*data_extractor, set_error);

      if (!set_error.Success()) {
        error.SetErrorStringWithFormat("Couldn't set data: %s",
                                       set_error.AsCString());
        ret = false;
      }
    }
  } else {
    error.SetErrorStringWithFormat(
        "Couldn't set data: could not get SBValue: %s",
        locker.GetError().AsCString());
    ret = false;
  }

  return ret;
}

lldb::SBValue SBValue::Clone(const char *new_name) {
  LLDB_INSTRUMENT_VA(this, new_name);

  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));

  if (value_sp)
    return lldb::SBValue(value_sp->Clone(ConstString(new_name)));
  else
    return lldb::SBValue();
}

lldb::SBDeclaration SBValue::GetDeclaration() {
  LLDB_INSTRUMENT_VA(this);

  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  SBDeclaration decl_sb;
  if (value_sp) {
    Declaration decl;
    if (value_sp->GetDeclaration(decl))
      decl_sb.SetDeclaration(decl);
  }
  return decl_sb;
}

lldb::SBWatchpoint SBValue::Watch(bool resolve_location, bool read, bool write,
                                  SBError &error) {
  LLDB_INSTRUMENT_VA(this, resolve_location, read, write, error);

  SBWatchpoint sb_watchpoint;

  // If the SBValue is not valid, there's no point in even trying to watch it.
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  TargetSP target_sp(GetTarget().GetSP());
  if (value_sp && target_sp) {
    // Read and Write cannot both be false.
    if (!read && !write)
      return sb_watchpoint;

    // If the value is not in scope, don't try and watch and invalid value
    if (!IsInScope())
      return sb_watchpoint;

    addr_t addr = GetLoadAddress();
    if (addr == LLDB_INVALID_ADDRESS)
      return sb_watchpoint;
    size_t byte_size = GetByteSize();
    if (byte_size == 0)
      return sb_watchpoint;

    uint32_t watch_type = 0;
    if (read) {
      watch_type |= LLDB_WATCH_TYPE_READ;
      // read + write, the most likely intention
      // is to catch all writes to this, not just
      // value modifications.
      if (write)
        watch_type |= LLDB_WATCH_TYPE_WRITE;
    } else {
      if (write)
        watch_type |= LLDB_WATCH_TYPE_MODIFY;
    }

    Status rc;
    CompilerType type(value_sp->GetCompilerType());
    WatchpointSP watchpoint_sp =
        target_sp->CreateWatchpoint(addr, byte_size, &type, watch_type, rc);
    error.SetError(rc);

    if (watchpoint_sp) {
      sb_watchpoint.SetSP(watchpoint_sp);
      Declaration decl;
      if (value_sp->GetDeclaration(decl)) {
        if (decl.GetFile()) {
          StreamString ss;
          // True to show fullpath for declaration file.
          decl.DumpStopContext(&ss, true);
          watchpoint_sp->SetDeclInfo(std::string(ss.GetString()));
        }
      }
    }
  } else if (target_sp) {
    error.SetErrorStringWithFormat("could not get SBValue: %s",
                                   locker.GetError().AsCString());
  } else {
    error.SetErrorString("could not set watchpoint, a target is required");
  }

  return sb_watchpoint;
}

// FIXME: Remove this method impl (as well as the decl in .h) once it is no
// longer needed.
// Backward compatibility fix in the interim.
lldb::SBWatchpoint SBValue::Watch(bool resolve_location, bool read,
                                  bool write) {
  LLDB_INSTRUMENT_VA(this, resolve_location, read, write);

  SBError error;
  return Watch(resolve_location, read, write, error);
}

lldb::SBWatchpoint SBValue::WatchPointee(bool resolve_location, bool read,
                                         bool write, SBError &error) {
  LLDB_INSTRUMENT_VA(this, resolve_location, read, write, error);

  SBWatchpoint sb_watchpoint;
  if (IsInScope() && GetType().IsPointerType())
    sb_watchpoint = Dereference().Watch(resolve_location, read, write, error);
  return sb_watchpoint;
}

lldb::SBValue SBValue::Persist() {
  LLDB_INSTRUMENT_VA(this);

  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  SBValue persisted_sb;
  if (value_sp) {
    persisted_sb.SetSP(value_sp->Persist());
  }
  return persisted_sb;
}

lldb::SBValue SBValue::GetVTable() {
  SBValue vtable_sb;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (!value_sp)
    return vtable_sb;

  vtable_sb.SetSP(value_sp->GetVTable());
  return vtable_sb;
}
