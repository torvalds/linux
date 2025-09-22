//===-- lldb-server.cpp -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SystemInitializerLLGS.h"
#include "lldb/Host/Config.h"
#include "lldb/Initialization/SystemLifetimeManager.h"
#include "lldb/Version/Version.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Signals.h"

#include <cstdio>
#include <cstdlib>

static llvm::ManagedStatic<lldb_private::SystemLifetimeManager>
    g_debugger_lifetime;

static void display_usage(const char *progname) {
  fprintf(stderr, "Usage:\n"
                  "  %s v[ersion]\n"
                  "  %s g[dbserver] [options]\n"
                  "  %s p[latform] [options]\n"
                  "Invoke subcommand for additional help\n",
          progname, progname, progname);
  exit(0);
}

// Forward declarations of subcommand main methods.
int main_gdbserver(int argc, char *argv[]);
int main_platform(int argc, char *argv[]);

namespace llgs {
static void initialize() {
  if (auto e = g_debugger_lifetime->Initialize(
          std::make_unique<SystemInitializerLLGS>(), nullptr))
    llvm::consumeError(std::move(e));
}

static void terminate_debugger() { g_debugger_lifetime->Terminate(); }
} // namespace llgs

// main
int main(int argc, char *argv[]) {
  llvm::InitLLVM IL(argc, argv, /*InstallPipeSignalExitHandler=*/false);
  llvm::setBugReportMsg("PLEASE submit a bug report to " LLDB_BUG_REPORT_URL
                        " and include the crash backtrace.\n");

  int option_error = 0;
  const char *progname = argv[0];
  if (argc < 2) {
    display_usage(progname);
    exit(option_error);
  }

  switch (argv[1][0]) {
  case 'g':
    llgs::initialize();
    main_gdbserver(argc, argv);
    llgs::terminate_debugger();
    break;
  case 'p':
    llgs::initialize();
    main_platform(argc, argv);
    llgs::terminate_debugger();
    break;
  case 'v':
    fprintf(stderr, "%s\n", lldb_private::GetVersion());
    break;
  default:
    display_usage(progname);
    exit(option_error);
  }
}
