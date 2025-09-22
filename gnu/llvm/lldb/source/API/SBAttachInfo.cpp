//===-- SBAttachInfo.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBAttachInfo.h"
#include "Utils.h"
#include "lldb/API/SBFileSpec.h"
#include "lldb/API/SBListener.h"
#include "lldb/API/SBStructuredData.h"
#include "lldb/Target/Process.h"
#include "lldb/Utility/Instrumentation.h"
#include "lldb/Utility/ScriptedMetadata.h"

using namespace lldb;
using namespace lldb_private;

SBAttachInfo::SBAttachInfo() : m_opaque_sp(new ProcessAttachInfo()) {
  LLDB_INSTRUMENT_VA(this);
}

SBAttachInfo::SBAttachInfo(lldb::pid_t pid)
    : m_opaque_sp(new ProcessAttachInfo()) {
  LLDB_INSTRUMENT_VA(this, pid);

  m_opaque_sp->SetProcessID(pid);
}

SBAttachInfo::SBAttachInfo(const char *path, bool wait_for)
    : m_opaque_sp(new ProcessAttachInfo()) {
  LLDB_INSTRUMENT_VA(this, path, wait_for);

  if (path && path[0])
    m_opaque_sp->GetExecutableFile().SetFile(path, FileSpec::Style::native);
  m_opaque_sp->SetWaitForLaunch(wait_for);
}

SBAttachInfo::SBAttachInfo(const char *path, bool wait_for, bool async)
    : m_opaque_sp(new ProcessAttachInfo()) {
  LLDB_INSTRUMENT_VA(this, path, wait_for, async);

  if (path && path[0])
    m_opaque_sp->GetExecutableFile().SetFile(path, FileSpec::Style::native);
  m_opaque_sp->SetWaitForLaunch(wait_for);
  m_opaque_sp->SetAsync(async);
}

SBAttachInfo::SBAttachInfo(const SBAttachInfo &rhs)
    : m_opaque_sp(new ProcessAttachInfo()) {
  LLDB_INSTRUMENT_VA(this, rhs);

  m_opaque_sp = clone(rhs.m_opaque_sp);
}

SBAttachInfo::~SBAttachInfo() = default;

lldb_private::ProcessAttachInfo &SBAttachInfo::ref() { return *m_opaque_sp; }

SBAttachInfo &SBAttachInfo::operator=(const SBAttachInfo &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (this != &rhs)
    m_opaque_sp = clone(rhs.m_opaque_sp);
  return *this;
}

lldb::pid_t SBAttachInfo::GetProcessID() {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_sp->GetProcessID();
}

void SBAttachInfo::SetProcessID(lldb::pid_t pid) {
  LLDB_INSTRUMENT_VA(this, pid);

  m_opaque_sp->SetProcessID(pid);
}

uint32_t SBAttachInfo::GetResumeCount() {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_sp->GetResumeCount();
}

void SBAttachInfo::SetResumeCount(uint32_t c) {
  LLDB_INSTRUMENT_VA(this, c);

  m_opaque_sp->SetResumeCount(c);
}

const char *SBAttachInfo::GetProcessPluginName() {
  LLDB_INSTRUMENT_VA(this);

  return ConstString(m_opaque_sp->GetProcessPluginName()).GetCString();
}

void SBAttachInfo::SetProcessPluginName(const char *plugin_name) {
  LLDB_INSTRUMENT_VA(this, plugin_name);

  return m_opaque_sp->SetProcessPluginName(plugin_name);
}

void SBAttachInfo::SetExecutable(const char *path) {
  LLDB_INSTRUMENT_VA(this, path);

  if (path && path[0])
    m_opaque_sp->GetExecutableFile().SetFile(path, FileSpec::Style::native);
  else
    m_opaque_sp->GetExecutableFile().Clear();
}

void SBAttachInfo::SetExecutable(SBFileSpec exe_file) {
  LLDB_INSTRUMENT_VA(this, exe_file);

  if (exe_file.IsValid())
    m_opaque_sp->GetExecutableFile() = exe_file.ref();
  else
    m_opaque_sp->GetExecutableFile().Clear();
}

bool SBAttachInfo::GetWaitForLaunch() {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_sp->GetWaitForLaunch();
}

void SBAttachInfo::SetWaitForLaunch(bool b) {
  LLDB_INSTRUMENT_VA(this, b);

  m_opaque_sp->SetWaitForLaunch(b);
}

void SBAttachInfo::SetWaitForLaunch(bool b, bool async) {
  LLDB_INSTRUMENT_VA(this, b, async);

  m_opaque_sp->SetWaitForLaunch(b);
  m_opaque_sp->SetAsync(async);
}

bool SBAttachInfo::GetIgnoreExisting() {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_sp->GetIgnoreExisting();
}

void SBAttachInfo::SetIgnoreExisting(bool b) {
  LLDB_INSTRUMENT_VA(this, b);

  m_opaque_sp->SetIgnoreExisting(b);
}

uint32_t SBAttachInfo::GetUserID() {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_sp->GetUserID();
}

uint32_t SBAttachInfo::GetGroupID() {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_sp->GetGroupID();
}

bool SBAttachInfo::UserIDIsValid() {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_sp->UserIDIsValid();
}

bool SBAttachInfo::GroupIDIsValid() {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_sp->GroupIDIsValid();
}

void SBAttachInfo::SetUserID(uint32_t uid) {
  LLDB_INSTRUMENT_VA(this, uid);

  m_opaque_sp->SetUserID(uid);
}

void SBAttachInfo::SetGroupID(uint32_t gid) {
  LLDB_INSTRUMENT_VA(this, gid);

  m_opaque_sp->SetGroupID(gid);
}

uint32_t SBAttachInfo::GetEffectiveUserID() {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_sp->GetEffectiveUserID();
}

uint32_t SBAttachInfo::GetEffectiveGroupID() {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_sp->GetEffectiveGroupID();
}

bool SBAttachInfo::EffectiveUserIDIsValid() {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_sp->EffectiveUserIDIsValid();
}

bool SBAttachInfo::EffectiveGroupIDIsValid() {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_sp->EffectiveGroupIDIsValid();
}

void SBAttachInfo::SetEffectiveUserID(uint32_t uid) {
  LLDB_INSTRUMENT_VA(this, uid);

  m_opaque_sp->SetEffectiveUserID(uid);
}

void SBAttachInfo::SetEffectiveGroupID(uint32_t gid) {
  LLDB_INSTRUMENT_VA(this, gid);

  m_opaque_sp->SetEffectiveGroupID(gid);
}

lldb::pid_t SBAttachInfo::GetParentProcessID() {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_sp->GetParentProcessID();
}

void SBAttachInfo::SetParentProcessID(lldb::pid_t pid) {
  LLDB_INSTRUMENT_VA(this, pid);

  m_opaque_sp->SetParentProcessID(pid);
}

bool SBAttachInfo::ParentProcessIDIsValid() {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_sp->ParentProcessIDIsValid();
}

SBListener SBAttachInfo::GetListener() {
  LLDB_INSTRUMENT_VA(this);

  return SBListener(m_opaque_sp->GetListener());
}

void SBAttachInfo::SetListener(SBListener &listener) {
  LLDB_INSTRUMENT_VA(this, listener);

  m_opaque_sp->SetListener(listener.GetSP());
}

SBListener SBAttachInfo::GetShadowListener() {
  LLDB_INSTRUMENT_VA(this);

  lldb::ListenerSP shadow_sp = m_opaque_sp->GetShadowListener();
  if (!shadow_sp)
    return SBListener();
  return SBListener(shadow_sp);
}

void SBAttachInfo::SetShadowListener(SBListener &listener) {
  LLDB_INSTRUMENT_VA(this, listener);

  m_opaque_sp->SetShadowListener(listener.GetSP());
}

const char *SBAttachInfo::GetScriptedProcessClassName() const {
  LLDB_INSTRUMENT_VA(this);

  ScriptedMetadataSP metadata_sp = m_opaque_sp->GetScriptedMetadata();

  if (!metadata_sp || !*metadata_sp)
    return nullptr;

  // Constify this string so that it is saved in the string pool.  Otherwise it
  // would be freed when this function goes out of scope.
  ConstString class_name(metadata_sp->GetClassName().data());
  return class_name.AsCString();
}

void SBAttachInfo::SetScriptedProcessClassName(const char *class_name) {
  LLDB_INSTRUMENT_VA(this, class_name);

  ScriptedMetadataSP metadata_sp = m_opaque_sp->GetScriptedMetadata();

  if (!metadata_sp)
    metadata_sp = std::make_shared<ScriptedMetadata>(class_name, nullptr);
  else
    metadata_sp = std::make_shared<ScriptedMetadata>(class_name,
                                                     metadata_sp->GetArgsSP());

  m_opaque_sp->SetScriptedMetadata(metadata_sp);
}

lldb::SBStructuredData SBAttachInfo::GetScriptedProcessDictionary() const {
  LLDB_INSTRUMENT_VA(this);

  ScriptedMetadataSP metadata_sp = m_opaque_sp->GetScriptedMetadata();

  SBStructuredData data;
  if (!metadata_sp)
    return data;

  lldb_private::StructuredData::DictionarySP dict_sp = metadata_sp->GetArgsSP();
  data.m_impl_up->SetObjectSP(dict_sp);

  return data;
}

void SBAttachInfo::SetScriptedProcessDictionary(lldb::SBStructuredData dict) {
  LLDB_INSTRUMENT_VA(this, dict);

  if (!dict.IsValid() || !dict.m_impl_up)
    return;

  StructuredData::ObjectSP obj_sp = dict.m_impl_up->GetObjectSP();

  if (!obj_sp)
    return;

  StructuredData::DictionarySP dict_sp =
      std::make_shared<StructuredData::Dictionary>(obj_sp);
  if (!dict_sp || dict_sp->GetType() == lldb::eStructuredDataTypeInvalid)
    return;

  ScriptedMetadataSP metadata_sp = m_opaque_sp->GetScriptedMetadata();

  if (!metadata_sp)
    metadata_sp = std::make_shared<ScriptedMetadata>("", dict_sp);
  else
    metadata_sp = std::make_shared<ScriptedMetadata>(
        metadata_sp->GetClassName(), dict_sp);

  m_opaque_sp->SetScriptedMetadata(metadata_sp);
}
