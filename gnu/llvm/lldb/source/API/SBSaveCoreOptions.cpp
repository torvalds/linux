//===-- SBSaveCoreOptions.cpp -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBSaveCoreOptions.h"
#include "lldb/API/SBError.h"
#include "lldb/API/SBFileSpec.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Symbol/SaveCoreOptions.h"
#include "lldb/Utility/Instrumentation.h"

#include "Utils.h"

using namespace lldb;

SBSaveCoreOptions::SBSaveCoreOptions() {
  LLDB_INSTRUMENT_VA(this)

  m_opaque_up = std::make_unique<lldb_private::SaveCoreOptions>();
}

SBSaveCoreOptions::SBSaveCoreOptions(const SBSaveCoreOptions &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  m_opaque_up = clone(rhs.m_opaque_up);
}

SBSaveCoreOptions::~SBSaveCoreOptions() = default;

const SBSaveCoreOptions &
SBSaveCoreOptions::operator=(const SBSaveCoreOptions &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (this != &rhs)
    m_opaque_up = clone(rhs.m_opaque_up);
  return *this;
}

SBError SBSaveCoreOptions::SetPluginName(const char *name) {
  LLDB_INSTRUMENT_VA(this, name);
  lldb_private::Status error = m_opaque_up->SetPluginName(name);
  return SBError(error);
}

void SBSaveCoreOptions::SetStyle(lldb::SaveCoreStyle style) {
  LLDB_INSTRUMENT_VA(this, style);
  m_opaque_up->SetStyle(style);
}

void SBSaveCoreOptions::SetOutputFile(lldb::SBFileSpec file_spec) {
  LLDB_INSTRUMENT_VA(this, file_spec);
  m_opaque_up->SetOutputFile(file_spec.ref());
}

const char *SBSaveCoreOptions::GetPluginName() const {
  LLDB_INSTRUMENT_VA(this);
  const auto name = m_opaque_up->GetPluginName();
  if (!name)
    return nullptr;
  return lldb_private::ConstString(name.value()).GetCString();
}

SBFileSpec SBSaveCoreOptions::GetOutputFile() const {
  LLDB_INSTRUMENT_VA(this);
  const auto file_spec = m_opaque_up->GetOutputFile();
  if (file_spec)
    return SBFileSpec(file_spec.value());
  return SBFileSpec();
}

lldb::SaveCoreStyle SBSaveCoreOptions::GetStyle() const {
  LLDB_INSTRUMENT_VA(this);
  return m_opaque_up->GetStyle();
}

void SBSaveCoreOptions::Clear() {
  LLDB_INSTRUMENT_VA(this);
  m_opaque_up->Clear();
}

lldb_private::SaveCoreOptions &SBSaveCoreOptions::ref() const {
  return *m_opaque_up.get();
}
