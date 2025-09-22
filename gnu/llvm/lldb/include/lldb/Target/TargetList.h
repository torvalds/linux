//===-- TargetList.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TARGET_TARGETLIST_H
#define LLDB_TARGET_TARGETLIST_H

#include <mutex>
#include <vector>

#include "lldb/Target/Target.h"
#include "lldb/Utility/Broadcaster.h"
#include "lldb/Utility/Iterable.h"

namespace lldb_private {

class TargetList : public Broadcaster {
private:
  friend class Debugger;

  /// Constructor
  ///
  /// The constructor for the target list is private. Clients can
  /// get ahold of the one and only target list through the
  /// lldb_private::Debugger::GetSharedInstance().GetTargetList().
  ///
  /// \see static TargetList& lldb_private::Debugger::GetTargetList().
  TargetList(Debugger &debugger);

public:
  /// Broadcaster event bits definitions.
  enum { eBroadcastBitInterrupt = (1 << 0) };

  // These two functions fill out the Broadcaster interface:

  static llvm::StringRef GetStaticBroadcasterClass();

  llvm::StringRef GetBroadcasterClass() const override {
    return GetStaticBroadcasterClass();
  }

  typedef std::vector<lldb::TargetSP> collection;
  typedef LockingAdaptedIterable<collection, lldb::TargetSP, vector_adapter,
                                 std::recursive_mutex>
      TargetIterable;

  /// Create a new Target.
  ///
  /// Clients must use this function to create a Target. This allows
  /// a global list of targets to be maintained in a central location
  /// so signal handlers and other global functions can use it to
  /// locate an appropriate target to deliver asynchronous information
  /// to.
  ///
  /// \param[in] debugger
  ///     The debugger to associate this target with
  ///
  /// \param[in] user_exe_path
  ///     The main executable file for a debug target. This value
  ///     can be empty and the file can be set later using:
  ///     Target::SetExecutableModule (ModuleSP&)
  ///
  /// \param[in] triple_str
  ///     A target triple string to be used for the target. This can
  ///     be nullptr if the triple is not known or when attaching to a
  ///     process.
  ///
  /// \param[in] get_dependent_modules
  ///     Track down the dependent modules for an executable and
  ///     load those into the module list.
  ///
  /// \param[in] platform_options
  ///     A pointer to the platform options to use when creating this
  ///     target. If this value is nullptr, then the currently selected
  ///     platform will be used.
  ///
  /// \param[out] target_sp
  ///     A shared pointer to a target that will be filled in if
  ///     this call is successful.
  ///
  /// \return
  ///     An error object that indicates success or failure
  Status CreateTarget(Debugger &debugger, llvm::StringRef user_exe_path,
                      llvm::StringRef triple_str,
                      LoadDependentFiles get_dependent_modules,
                      const OptionGroupPlatform *platform_options,
                      lldb::TargetSP &target_sp);

  /// Create a new Target.
  ///
  /// Same as the function above, but used when you already know the
  /// platform you will be using
  Status CreateTarget(Debugger &debugger, llvm::StringRef user_exe_path,
                      const ArchSpec &arch,
                      LoadDependentFiles get_dependent_modules,
                      lldb::PlatformSP &platform_sp, lldb::TargetSP &target_sp);

  /// Delete a Target object from the list.
  ///
  /// When clients are done with the Target objects, this function
  /// should be called to release the memory associated with a target
  /// object.
  ///
  /// \param[in] target_sp
  ///     The shared pointer to a target.
  ///
  /// \return
  ///     Returns \b true if the target was successfully removed from
  ///     from this target list, \b false otherwise. The client will
  ///     be left with the last remaining shared pointer to the target
  ///     in \a target_sp which can then be properly released.
  bool DeleteTarget(lldb::TargetSP &target_sp);

  size_t GetNumTargets() const;

  lldb::TargetSP GetTargetAtIndex(uint32_t index) const;

  uint32_t GetIndexOfTarget(lldb::TargetSP target_sp) const;

  /// Find the target that contains has an executable whose path
  /// matches \a exe_file_spec, and whose architecture matches
  /// \a arch_ptr if arch_ptr is not nullptr.
  ///
  /// \param[in] exe_file_spec
  ///     A file spec containing a basename, or a full path (directory
  ///     and basename). If \a exe_file_spec contains only a filename
  ///     (empty GetDirectory() value) then matching will be done
  ///     solely based on the filenames and directories won't be
  ///     compared. If \a exe_file_spec contains a filename and a
  ///     directory, then both must match.
  ///
  /// \param[in] exe_arch_ptr
  ///     If not nullptr then the architecture also needs to match, else
  ///     the architectures will be compared.
  ///
  /// \return
  ///     A shared pointer to a target object. The returned shared
  ///     pointer will contain nullptr if no target objects have a
  ///     executable whose full or partial path matches
  ///     with a matching process ID.
  lldb::TargetSP FindTargetWithExecutableAndArchitecture(
      const FileSpec &exe_file_spec,
      const ArchSpec *exe_arch_ptr = nullptr) const;

  /// Find the target that contains a process with process ID \a
  /// pid.
  ///
  /// \param[in] pid
  ///     The process ID to search our target list for.
  ///
  /// \return
  ///     A shared pointer to a target object. The returned shared
  ///     pointer will contain nullptr if no target objects own a process
  ///     with a matching process ID.
  lldb::TargetSP FindTargetWithProcessID(lldb::pid_t pid) const;

  lldb::TargetSP FindTargetWithProcess(lldb_private::Process *process) const;

  lldb::TargetSP GetTargetSP(Target *target) const;

  /// Send an async interrupt to one or all processes.
  ///
  /// Find the target that contains the process with process ID \a
  /// pid and send a LLDB_EVENT_ASYNC_INTERRUPT event to the process's
  /// event queue.
  ///
  /// \param[in] pid
  ///     The process ID to search our target list for, if \a pid is
  ///     LLDB_INVALID_PROCESS_ID, then the interrupt will be sent to
  ///     all processes.
  ///
  /// \return
  ///     The number of async interrupts sent.
  uint32_t SendAsyncInterrupt(lldb::pid_t pid = LLDB_INVALID_PROCESS_ID);

  uint32_t SignalIfRunning(lldb::pid_t pid, int signo);

  void SetSelectedTarget(uint32_t index);

  void SetSelectedTarget(const lldb::TargetSP &target);

  lldb::TargetSP GetSelectedTarget();
  
  ///  Returns whether any module, including ones in the process of being
  ///  added, contains this module.  I don't want to give direct access to
  ///  these not yet added target, but for interruption purposes, we might
  ///  need to ask whether this target contains this module. 
  bool AnyTargetContainsModule(Module &module);

  TargetIterable Targets() {
    return TargetIterable(m_target_list, m_target_list_mutex);
  }

private:
  collection m_target_list;
  std::unordered_set<lldb::TargetSP> m_in_process_target_list;
  mutable std::recursive_mutex m_target_list_mutex;
  uint32_t m_selected_target_idx;

  static Status CreateTargetInternal(
      Debugger &debugger, llvm::StringRef user_exe_path,
      llvm::StringRef triple_str, LoadDependentFiles load_dependent_files,
      const OptionGroupPlatform *platform_options, lldb::TargetSP &target_sp);

  static Status CreateTargetInternal(Debugger &debugger,
                                     llvm::StringRef user_exe_path,
                                     const ArchSpec &arch,
                                     LoadDependentFiles get_dependent_modules,
                                     lldb::PlatformSP &platform_sp,
                                     lldb::TargetSP &target_sp);

  void RegisterInProcessTarget(lldb::TargetSP target_sp);
  
  void UnregisterInProcessTarget(lldb::TargetSP target_sp);
  
  bool IsTargetInProcess(lldb::TargetSP target_sp);
  
  void AddTargetInternal(lldb::TargetSP target_sp, bool do_select);

  void SetSelectedTargetInternal(uint32_t index);

  TargetList(const TargetList &) = delete;
  const TargetList &operator=(const TargetList &) = delete;
};

} // namespace lldb_private

#endif // LLDB_TARGET_TARGETLIST_H
