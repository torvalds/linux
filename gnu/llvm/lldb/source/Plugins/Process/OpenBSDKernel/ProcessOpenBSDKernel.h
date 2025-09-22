//===-- ProcessOpenBSDKernel.h ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_OPENBSDKERNEL_PROCESSOPENBSDKERNEL_H
#define LLDB_SOURCE_PLUGINS_PROCESS_OPENBSDKERNEL_PROCESSOPENBSDKERNEL_H

#include "lldb/Target/PostMortemProcess.h"

class ProcessOpenBSDKernel : public lldb_private::PostMortemProcess {
public:
  ProcessOpenBSDKernel(lldb::TargetSP target_sp, lldb::ListenerSP listener);

  static lldb::ProcessSP
  CreateInstance(lldb::TargetSP target_sp, lldb::ListenerSP listener,
		 const lldb_private::FileSpec *crash_file_path,
		 bool can_connect);

  static void Initialize();

  static void Terminate();

  static llvm::StringRef GetPluginNameStatic() { return "openbsd-kernel"; }

  static llvm::StringRef GetPluginDescriptionStatic() {
	return "OpenBSD kernel vmcore debugging plug-in.";
  }

  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

  lldb_private::Status DoDestroy() override;

  bool CanDebug(lldb::TargetSP target_sp,
		bool plugin_specified_by_name) override;

  void RefreshStateAfterStop() override;

  lldb_private::Status DoLoadCore() override;

  lldb_private::DynamicLoader *GetDynamicLoader() override;

protected:
  bool DoUpdateThreadList(lldb_private::ThreadList &old_thread_list,
			  lldb_private::ThreadList &new_thread_list) override;

  lldb::addr_t FindSymbol(const char* name);
};

#endif // LLDB_SOURCE_PLUGINS_PROCESS_OPENBSDKERNEL_PROCESSOPENBSDKERNEL_H
