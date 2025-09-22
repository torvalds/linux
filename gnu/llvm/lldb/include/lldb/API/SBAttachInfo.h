//===-- SBAttachInfo.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBATTACHINFO_H
#define LLDB_API_SBATTACHINFO_H

#include "lldb/API/SBDefines.h"

namespace lldb_private {
class ScriptInterpreter;
}

namespace lldb {

class SBTarget;

class LLDB_API SBAttachInfo {
public:
  SBAttachInfo();

  SBAttachInfo(lldb::pid_t pid);

  /// Attach to a process by name.
  ///
  /// This function implies that a future call to SBTarget::Attach(...)
  /// will be synchronous.
  ///
  /// \param[in] path
  ///     A full or partial name for the process to attach to.
  ///
  /// \param[in] wait_for
  ///     If \b false, attach to an existing process whose name matches.
  ///     If \b true, then wait for the next process whose name matches.
  SBAttachInfo(const char *path, bool wait_for);

  /// Attach to a process by name.
  ///
  /// Future calls to SBTarget::Attach(...) will be synchronous or
  /// asynchronous depending on the \a async argument.
  ///
  /// \param[in] path
  ///     A full or partial name for the process to attach to.
  ///
  /// \param[in] wait_for
  ///     If \b false, attach to an existing process whose name matches.
  ///     If \b true, then wait for the next process whose name matches.
  ///
  /// \param[in] async
  ///     If \b false, then the SBTarget::Attach(...) call will be a
  ///     synchronous call with no way to cancel the attach in
  ///     progress.
  ///     If \b true, then the SBTarget::Attach(...) function will
  ///     return immediately and clients are expected to wait for a
  ///     process eStateStopped event if a suitable process is
  ///     eventually found. If the client wants to cancel the event,
  ///     SBProcess::Stop() can be called and an eStateExited process
  ///     event will be delivered.
  SBAttachInfo(const char *path, bool wait_for, bool async);

  SBAttachInfo(const SBAttachInfo &rhs);

  ~SBAttachInfo();

  SBAttachInfo &operator=(const SBAttachInfo &rhs);

  lldb::pid_t GetProcessID();

  void SetProcessID(lldb::pid_t pid);

  void SetExecutable(const char *path);

  void SetExecutable(lldb::SBFileSpec exe_file);

  bool GetWaitForLaunch();

  /// Set attach by process name settings.
  ///
  /// Designed to be used after a call to SBAttachInfo::SetExecutable().
  /// This function implies that a call to SBTarget::Attach(...) will
  /// be synchronous.
  ///
  /// \param[in] b
  ///     If \b false, attach to an existing process whose name matches.
  ///     If \b true, then wait for the next process whose name matches.
  void SetWaitForLaunch(bool b);

  /// Set attach by process name settings.
  ///
  /// Designed to be used after a call to SBAttachInfo::SetExecutable().
  /// Future calls to SBTarget::Attach(...) will be synchronous or
  /// asynchronous depending on the \a async argument.
  ///
  /// \param[in] b
  ///     If \b false, attach to an existing process whose name matches.
  ///     If \b true, then wait for the next process whose name matches.
  ///
  /// \param[in] async
  ///     If \b false, then the SBTarget::Attach(...) call will be a
  ///     synchronous call with no way to cancel the attach in
  ///     progress.
  ///     If \b true, then the SBTarget::Attach(...) function will
  ///     return immediately and clients are expected to wait for a
  ///     process eStateStopped event if a suitable process is
  ///     eventually found. If the client wants to cancel the event,
  ///     SBProcess::Stop() can be called and an eStateExited process
  ///     event will be delivered.
  void SetWaitForLaunch(bool b, bool async);

  bool GetIgnoreExisting();

  void SetIgnoreExisting(bool b);

  uint32_t GetResumeCount();

  void SetResumeCount(uint32_t c);

  const char *GetProcessPluginName();

  void SetProcessPluginName(const char *plugin_name);

  uint32_t GetUserID();

  uint32_t GetGroupID();

  bool UserIDIsValid();

  bool GroupIDIsValid();

  void SetUserID(uint32_t uid);

  void SetGroupID(uint32_t gid);

  uint32_t GetEffectiveUserID();

  uint32_t GetEffectiveGroupID();

  bool EffectiveUserIDIsValid();

  bool EffectiveGroupIDIsValid();

  void SetEffectiveUserID(uint32_t uid);

  void SetEffectiveGroupID(uint32_t gid);

  lldb::pid_t GetParentProcessID();

  void SetParentProcessID(lldb::pid_t pid);

  bool ParentProcessIDIsValid();

  /// Get the listener that will be used to receive process events.
  ///
  /// If no listener has been set via a call to
  /// SBAttachInfo::SetListener(), then an invalid SBListener will be
  /// returned (SBListener::IsValid() will return false). If a listener
  /// has been set, then the valid listener object will be returned.
  SBListener GetListener();

  /// Set the listener that will be used to receive process events.
  ///
  /// By default the SBDebugger, which has a listener, that the SBTarget
  /// belongs to will listen for the process events. Calling this function
  /// allows a different listener to be used to listen for process events.
  void SetListener(SBListener &listener);

  /// Get the shadow listener that receive public process events,
  /// additionally to the default process event listener.
  ///
  /// If no listener has been set via a call to
  /// SBLaunchInfo::SetShadowListener(), then an invalid SBListener will
  /// be returned (SBListener::IsValid() will return false). If a listener
  /// has been set, then the valid listener object will be returned.
  SBListener GetShadowListener();

  /// Set the shadow listener that will receive public process events,
  /// additionally to the default process event listener.
  ///
  /// By default a process have no shadow event listener.
  /// Calling this function allows public process events to be broadcasted to an
  /// additional listener on top of the default process event listener.
  /// If the `listener` argument is invalid (SBListener::IsValid() will
  /// return false), this will clear the shadow listener.
  void SetShadowListener(SBListener &listener);

  const char *GetScriptedProcessClassName() const;

  void SetScriptedProcessClassName(const char *class_name);

  lldb::SBStructuredData GetScriptedProcessDictionary() const;

  void SetScriptedProcessDictionary(lldb::SBStructuredData dict);

protected:
  friend class SBTarget;
  friend class SBPlatform;

  friend class lldb_private::ScriptInterpreter;

  lldb_private::ProcessAttachInfo &ref();

  ProcessAttachInfoSP m_opaque_sp;
};

} // namespace lldb

#endif // LLDB_API_SBATTACHINFO_H
