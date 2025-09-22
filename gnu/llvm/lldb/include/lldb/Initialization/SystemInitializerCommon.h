//===-- SystemInitializerCommon.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_INITIALIZATION_SYSTEMINITIALIZERCOMMON_H
#define LLDB_INITIALIZATION_SYSTEMINITIALIZERCOMMON_H

#include "SystemInitializer.h"
#include "lldb/Host/HostInfo.h"

namespace lldb_private {
/// Initializes common lldb functionality.
///
/// This class is responsible for initializing a subset of lldb
/// useful to both debug servers and debug clients.  Debug servers
/// do not use all of LLDB and desire small binary sizes, so this
/// functionality is separate.  This class is used by constructing
/// an instance of SystemLifetimeManager with this class passed to
/// the constructor.
class SystemInitializerCommon : public SystemInitializer {
public:
  SystemInitializerCommon(HostInfo::SharedLibraryDirectoryHelper *helper);
  ~SystemInitializerCommon() override;

  llvm::Error Initialize() override;
  void Terminate() override;

private:
  HostInfo::SharedLibraryDirectoryHelper *m_shlib_dir_helper;
};

} // namespace lldb_private

#endif // LLDB_INITIALIZATION_SYSTEMINITIALIZERCOMMON_H
