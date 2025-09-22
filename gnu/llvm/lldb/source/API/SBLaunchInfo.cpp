//===-- SBLaunchInfo.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBLaunchInfo.h"
#include "lldb/Utility/Instrumentation.h"

#include "lldb/API/SBEnvironment.h"
#include "lldb/API/SBError.h"
#include "lldb/API/SBFileSpec.h"
#include "lldb/API/SBListener.h"
#include "lldb/API/SBStream.h"
#include "lldb/API/SBStructuredData.h"
#include "lldb/Core/StructuredDataImpl.h"
#include "lldb/Host/ProcessLaunchInfo.h"
#include "lldb/Utility/Listener.h"
#include "lldb/Utility/ScriptedMetadata.h"

using namespace lldb;
using namespace lldb_private;

class lldb_private::SBLaunchInfoImpl : public ProcessLaunchInfo {
public:
  SBLaunchInfoImpl() : m_envp(GetEnvironment().getEnvp()) {}

  const char *const *GetEnvp() const { return m_envp; }
  void RegenerateEnvp() { m_envp = GetEnvironment().getEnvp(); }

  SBLaunchInfoImpl &operator=(const ProcessLaunchInfo &rhs) {
    ProcessLaunchInfo::operator=(rhs);
    RegenerateEnvp();
    return *this;
  }

private:
  Environment::Envp m_envp;
};

SBLaunchInfo::SBLaunchInfo(const char **argv)
    : m_opaque_sp(new SBLaunchInfoImpl()) {
  LLDB_INSTRUMENT_VA(this, argv);

  m_opaque_sp->GetFlags().Reset(eLaunchFlagDebug | eLaunchFlagDisableASLR);
  if (argv && argv[0])
    m_opaque_sp->GetArguments().SetArguments(argv);
}

SBLaunchInfo::SBLaunchInfo(const SBLaunchInfo &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  m_opaque_sp = rhs.m_opaque_sp;
}

SBLaunchInfo &SBLaunchInfo::operator=(const SBLaunchInfo &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  m_opaque_sp = rhs.m_opaque_sp;
  return *this;
}

SBLaunchInfo::~SBLaunchInfo() = default;

const lldb_private::ProcessLaunchInfo &SBLaunchInfo::ref() const {
  return *m_opaque_sp;
}

void SBLaunchInfo::set_ref(const ProcessLaunchInfo &info) {
  *m_opaque_sp = info;
}

lldb::pid_t SBLaunchInfo::GetProcessID() {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_sp->GetProcessID();
}

uint32_t SBLaunchInfo::GetUserID() {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_sp->GetUserID();
}

uint32_t SBLaunchInfo::GetGroupID() {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_sp->GetGroupID();
}

bool SBLaunchInfo::UserIDIsValid() {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_sp->UserIDIsValid();
}

bool SBLaunchInfo::GroupIDIsValid() {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_sp->GroupIDIsValid();
}

void SBLaunchInfo::SetUserID(uint32_t uid) {
  LLDB_INSTRUMENT_VA(this, uid);

  m_opaque_sp->SetUserID(uid);
}

void SBLaunchInfo::SetGroupID(uint32_t gid) {
  LLDB_INSTRUMENT_VA(this, gid);

  m_opaque_sp->SetGroupID(gid);
}

SBFileSpec SBLaunchInfo::GetExecutableFile() {
  LLDB_INSTRUMENT_VA(this);

  return SBFileSpec(m_opaque_sp->GetExecutableFile());
}

void SBLaunchInfo::SetExecutableFile(SBFileSpec exe_file,
                                     bool add_as_first_arg) {
  LLDB_INSTRUMENT_VA(this, exe_file, add_as_first_arg);

  m_opaque_sp->SetExecutableFile(exe_file.ref(), add_as_first_arg);
}

SBListener SBLaunchInfo::GetListener() {
  LLDB_INSTRUMENT_VA(this);

  return SBListener(m_opaque_sp->GetListener());
}

void SBLaunchInfo::SetListener(SBListener &listener) {
  LLDB_INSTRUMENT_VA(this, listener);

  m_opaque_sp->SetListener(listener.GetSP());
}

uint32_t SBLaunchInfo::GetNumArguments() {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_sp->GetArguments().GetArgumentCount();
}

const char *SBLaunchInfo::GetArgumentAtIndex(uint32_t idx) {
  LLDB_INSTRUMENT_VA(this, idx);

  return ConstString(m_opaque_sp->GetArguments().GetArgumentAtIndex(idx))
      .GetCString();
}

void SBLaunchInfo::SetArguments(const char **argv, bool append) {
  LLDB_INSTRUMENT_VA(this, argv, append);

  if (append) {
    if (argv)
      m_opaque_sp->GetArguments().AppendArguments(argv);
  } else {
    if (argv)
      m_opaque_sp->GetArguments().SetArguments(argv);
    else
      m_opaque_sp->GetArguments().Clear();
  }
}

uint32_t SBLaunchInfo::GetNumEnvironmentEntries() {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_sp->GetEnvironment().size();
}

const char *SBLaunchInfo::GetEnvironmentEntryAtIndex(uint32_t idx) {
  LLDB_INSTRUMENT_VA(this, idx);

  if (idx > GetNumEnvironmentEntries())
    return nullptr;
  return ConstString(m_opaque_sp->GetEnvp()[idx]).GetCString();
}

void SBLaunchInfo::SetEnvironmentEntries(const char **envp, bool append) {
  LLDB_INSTRUMENT_VA(this, envp, append);
  SetEnvironment(SBEnvironment(Environment(envp)), append);
}

void SBLaunchInfo::SetEnvironment(const SBEnvironment &env, bool append) {
  LLDB_INSTRUMENT_VA(this, env, append);
  Environment &refEnv = env.ref();
  if (append) {
    for (auto &KV : refEnv)
      m_opaque_sp->GetEnvironment().insert_or_assign(KV.first(), KV.second);
  } else
    m_opaque_sp->GetEnvironment() = refEnv;
  m_opaque_sp->RegenerateEnvp();
}

SBEnvironment SBLaunchInfo::GetEnvironment() {
  LLDB_INSTRUMENT_VA(this);
  return SBEnvironment(Environment(m_opaque_sp->GetEnvironment()));
}

void SBLaunchInfo::Clear() {
  LLDB_INSTRUMENT_VA(this);

  m_opaque_sp->Clear();
}

const char *SBLaunchInfo::GetWorkingDirectory() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_sp->GetWorkingDirectory().GetPathAsConstString().AsCString();
}

void SBLaunchInfo::SetWorkingDirectory(const char *working_dir) {
  LLDB_INSTRUMENT_VA(this, working_dir);

  m_opaque_sp->SetWorkingDirectory(FileSpec(working_dir));
}

uint32_t SBLaunchInfo::GetLaunchFlags() {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_sp->GetFlags().Get();
}

void SBLaunchInfo::SetLaunchFlags(uint32_t flags) {
  LLDB_INSTRUMENT_VA(this, flags);

  m_opaque_sp->GetFlags().Reset(flags);
}

const char *SBLaunchInfo::GetProcessPluginName() {
  LLDB_INSTRUMENT_VA(this);

  return ConstString(m_opaque_sp->GetProcessPluginName()).GetCString();
}

void SBLaunchInfo::SetProcessPluginName(const char *plugin_name) {
  LLDB_INSTRUMENT_VA(this, plugin_name);

  return m_opaque_sp->SetProcessPluginName(plugin_name);
}

const char *SBLaunchInfo::GetShell() {
  LLDB_INSTRUMENT_VA(this);

  // Constify this string so that it is saved in the string pool.  Otherwise it
  // would be freed when this function goes out of scope.
  ConstString shell(m_opaque_sp->GetShell().GetPath().c_str());
  return shell.AsCString();
}

void SBLaunchInfo::SetShell(const char *path) {
  LLDB_INSTRUMENT_VA(this, path);

  m_opaque_sp->SetShell(FileSpec(path));
}

bool SBLaunchInfo::GetShellExpandArguments() {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_sp->GetShellExpandArguments();
}

void SBLaunchInfo::SetShellExpandArguments(bool expand) {
  LLDB_INSTRUMENT_VA(this, expand);

  m_opaque_sp->SetShellExpandArguments(expand);
}

uint32_t SBLaunchInfo::GetResumeCount() {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_sp->GetResumeCount();
}

void SBLaunchInfo::SetResumeCount(uint32_t c) {
  LLDB_INSTRUMENT_VA(this, c);

  m_opaque_sp->SetResumeCount(c);
}

bool SBLaunchInfo::AddCloseFileAction(int fd) {
  LLDB_INSTRUMENT_VA(this, fd);

  return m_opaque_sp->AppendCloseFileAction(fd);
}

bool SBLaunchInfo::AddDuplicateFileAction(int fd, int dup_fd) {
  LLDB_INSTRUMENT_VA(this, fd, dup_fd);

  return m_opaque_sp->AppendDuplicateFileAction(fd, dup_fd);
}

bool SBLaunchInfo::AddOpenFileAction(int fd, const char *path, bool read,
                                     bool write) {
  LLDB_INSTRUMENT_VA(this, fd, path, read, write);

  return m_opaque_sp->AppendOpenFileAction(fd, FileSpec(path), read, write);
}

bool SBLaunchInfo::AddSuppressFileAction(int fd, bool read, bool write) {
  LLDB_INSTRUMENT_VA(this, fd, read, write);

  return m_opaque_sp->AppendSuppressFileAction(fd, read, write);
}

void SBLaunchInfo::SetLaunchEventData(const char *data) {
  LLDB_INSTRUMENT_VA(this, data);

  m_opaque_sp->SetLaunchEventData(data);
}

const char *SBLaunchInfo::GetLaunchEventData() const {
  LLDB_INSTRUMENT_VA(this);

  return ConstString(m_opaque_sp->GetLaunchEventData()).GetCString();
}

void SBLaunchInfo::SetDetachOnError(bool enable) {
  LLDB_INSTRUMENT_VA(this, enable);

  m_opaque_sp->SetDetachOnError(enable);
}

bool SBLaunchInfo::GetDetachOnError() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_sp->GetDetachOnError();
}

const char *SBLaunchInfo::GetScriptedProcessClassName() const {
  LLDB_INSTRUMENT_VA(this);

  ScriptedMetadataSP metadata_sp = m_opaque_sp->GetScriptedMetadata();

  if (!metadata_sp || !*metadata_sp)
    return nullptr;

  // Constify this string so that it is saved in the string pool.  Otherwise it
  // would be freed when this function goes out of scope.
  ConstString class_name(metadata_sp->GetClassName().data());
  return class_name.AsCString();
}

void SBLaunchInfo::SetScriptedProcessClassName(const char *class_name) {
  LLDB_INSTRUMENT_VA(this, class_name);
  ScriptedMetadataSP metadata_sp = m_opaque_sp->GetScriptedMetadata();
  StructuredData::DictionarySP dict_sp =
      metadata_sp ? metadata_sp->GetArgsSP() : nullptr;
  metadata_sp = std::make_shared<ScriptedMetadata>(class_name, dict_sp);
  m_opaque_sp->SetScriptedMetadata(metadata_sp);
}

lldb::SBStructuredData SBLaunchInfo::GetScriptedProcessDictionary() const {
  LLDB_INSTRUMENT_VA(this);

  ScriptedMetadataSP metadata_sp = m_opaque_sp->GetScriptedMetadata();

  SBStructuredData data;
  if (!metadata_sp)
    return data;

  lldb_private::StructuredData::DictionarySP dict_sp = metadata_sp->GetArgsSP();
  data.m_impl_up->SetObjectSP(dict_sp);

  return data;
}

void SBLaunchInfo::SetScriptedProcessDictionary(lldb::SBStructuredData dict) {
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
  llvm::StringRef class_name = metadata_sp ? metadata_sp->GetClassName() : "";
  metadata_sp = std::make_shared<ScriptedMetadata>(class_name, dict_sp);
  m_opaque_sp->SetScriptedMetadata(metadata_sp);
}

SBListener SBLaunchInfo::GetShadowListener() {
  LLDB_INSTRUMENT_VA(this);

  lldb::ListenerSP shadow_sp = m_opaque_sp->GetShadowListener();
  if (!shadow_sp)
    return SBListener();
  return SBListener(shadow_sp);
}

void SBLaunchInfo::SetShadowListener(SBListener &listener) {
  LLDB_INSTRUMENT_VA(this, listener);

  m_opaque_sp->SetShadowListener(listener.GetSP());
}
