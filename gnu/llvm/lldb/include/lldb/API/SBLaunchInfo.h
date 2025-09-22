//===-- SBLaunchInfo.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBLAUNCHINFO_H
#define LLDB_API_SBLAUNCHINFO_H

#include "lldb/API/SBDefines.h"

namespace lldb_private {
class SBLaunchInfoImpl;
class ScriptInterpreter;
}

namespace lldb {

class SBPlatform;
class SBTarget;

class LLDB_API SBLaunchInfo {
public:
  SBLaunchInfo(const char **argv);

  ~SBLaunchInfo();

#ifndef SWIG
  // The copy constructor for SBLaunchInfo presents some problems on some
  // supported versions of swig (e.g. 3.0.2). When trying to create an
  // SBLaunchInfo from python with the argument `None`, swig will try to call
  // the copy constructor instead of SBLaunchInfo(const char **). For that
  // reason, we avoid exposing the copy constructor to python.
  SBLaunchInfo(const SBLaunchInfo &rhs);
#endif

  SBLaunchInfo &operator=(const SBLaunchInfo &rhs);

  lldb::pid_t GetProcessID();

  uint32_t GetUserID();

  uint32_t GetGroupID();

  bool UserIDIsValid();

  bool GroupIDIsValid();

  void SetUserID(uint32_t uid);

  void SetGroupID(uint32_t gid);

  SBFileSpec GetExecutableFile();

  /// Set the executable file that will be used to launch the process and
  /// optionally set it as the first argument in the argument vector.
  ///
  /// This only needs to be specified if clients wish to carefully control
  /// the exact path will be used to launch a binary. If you create a
  /// target with a symlink, that symlink will get resolved in the target
  /// and the resolved path will get used to launch the process. Calling
  /// this function can help you still launch your process using the
  /// path of your choice.
  ///
  /// If this function is not called prior to launching with
  /// SBTarget::Launch(...), the target will use the resolved executable
  /// path that was used to create the target.
  ///
  /// \param[in] exe_file
  ///     The override path to use when launching the executable.
  ///
  /// \param[in] add_as_first_arg
  ///     If true, then the path will be inserted into the argument vector
  ///     prior to launching. Otherwise the argument vector will be left
  ///     alone.
  void SetExecutableFile(SBFileSpec exe_file, bool add_as_first_arg);

  /// Get the listener that will be used to receive process events.
  ///
  /// If no listener has been set via a call to
  /// SBLaunchInfo::SetListener(), then an invalid SBListener will be
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

  uint32_t GetNumArguments();

  const char *GetArgumentAtIndex(uint32_t idx);

  void SetArguments(const char **argv, bool append);

  uint32_t GetNumEnvironmentEntries();

  const char *GetEnvironmentEntryAtIndex(uint32_t idx);

  /// Update this object with the given environment variables.
  ///
  /// If append is false, the provided environment will replace the existing
  /// environment. Otherwise, existing values will be updated of left untouched
  /// accordingly.
  ///
  /// \param [in] envp
  ///     The new environment variables as a list of strings with the following
  ///     format
  ///         name=value
  ///
  /// \param [in] append
  ///     Flag that controls whether to replace the existing environment.
  void SetEnvironmentEntries(const char **envp, bool append);

  /// Update this object with the given environment variables.
  ///
  /// If append is false, the provided environment will replace the existing
  /// environment. Otherwise, existing values will be updated of left untouched
  /// accordingly.
  ///
  /// \param [in] env
  ///     The new environment variables.
  ///
  /// \param [in] append
  ///     Flag that controls whether to replace the existing environment.
  void SetEnvironment(const SBEnvironment &env, bool append);

  /// Return the environment variables of this object.
  ///
  /// \return
  ///     An lldb::SBEnvironment object which is a copy of the SBLaunchInfo's
  ///     environment.
  SBEnvironment GetEnvironment();

  void Clear();

  const char *GetWorkingDirectory() const;

  void SetWorkingDirectory(const char *working_dir);

  uint32_t GetLaunchFlags();

  void SetLaunchFlags(uint32_t flags);

  const char *GetProcessPluginName();

  void SetProcessPluginName(const char *plugin_name);

  const char *GetShell();

  void SetShell(const char *path);

  bool GetShellExpandArguments();

  void SetShellExpandArguments(bool expand);

  uint32_t GetResumeCount();

  void SetResumeCount(uint32_t c);

  bool AddCloseFileAction(int fd);

  bool AddDuplicateFileAction(int fd, int dup_fd);

  bool AddOpenFileAction(int fd, const char *path, bool read, bool write);

  bool AddSuppressFileAction(int fd, bool read, bool write);

  void SetLaunchEventData(const char *data);

  const char *GetLaunchEventData() const;

  bool GetDetachOnError() const;

  void SetDetachOnError(bool enable);

  const char *GetScriptedProcessClassName() const;

  void SetScriptedProcessClassName(const char *class_name);

  lldb::SBStructuredData GetScriptedProcessDictionary() const;

  void SetScriptedProcessDictionary(lldb::SBStructuredData dict);

protected:
  friend class SBPlatform;
  friend class SBTarget;

  friend class lldb_private::ScriptInterpreter;

  const lldb_private::ProcessLaunchInfo &ref() const;
  void set_ref(const lldb_private::ProcessLaunchInfo &info);

  std::shared_ptr<lldb_private::SBLaunchInfoImpl> m_opaque_sp;
};

} // namespace lldb

#endif // LLDB_API_SBLAUNCHINFO_H
