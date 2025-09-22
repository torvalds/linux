//===-- main.cpp ------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <getopt.h>
#include <stdint.h>
#include <stdlib.h>

#if defined(__APPLE__)
#include <LLDB/LLDB.h>
#else
#include "LLDB/SBBlock.h"
#include "LLDB/SBCompileUnit.h"
#include "LLDB/SBDebugger.h"
#include "LLDB/SBFunction.h"
#include "LLDB/SBModule.h"
#include "LLDB/SBProcess.h"
#include "LLDB/SBStream.h"
#include "LLDB/SBSymbol.h"
#include "LLDB/SBTarget.h"
#include "LLDB/SBThread.h"
#endif

#include <string>

using namespace lldb;

// This quick sample code shows how to create a debugger instance and
// create an "i386" executable target. Then we can lookup the executable
// module and resolve a file address into a section offset address,
// and find all symbol context objects (if any) for that address:
// compile unit, function, deepest block, line table entry and the
// symbol.
//
// To build the program, type (while in this directory):
//
//    $ make
//
// then (for example):
//
//    $ DYLD_FRAMEWORK_PATH=/Volumes/data/lldb/svn/ToT/build/Debug ./a.out
//    executable_path file_address
class LLDBSentry {
public:
  LLDBSentry() {
    // Initialize LLDB
    SBDebugger::Initialize();
  }
  ~LLDBSentry() {
    // Terminate LLDB
    SBDebugger::Terminate();
  }
};

static struct option g_long_options[] = {
    {"help", no_argument, NULL, 'h'},
    {"verbose", no_argument, NULL, 'v'},
    {"arch", required_argument, NULL, 'a'},
    {"platform", required_argument, NULL, 'p'},
    {NULL, 0, NULL, 0}};

#define PROGRAM_NAME "lldb-lookup"
void usage() {
  puts("NAME\n"
       "    " PROGRAM_NAME " -- symbolicate addresses using lldb.\n"
       "\n"
       "SYNOPSIS\n"
       "    " PROGRAM_NAME " [[--arch=<ARCH>] [--platform=<PLATFORM>] "
                           "[--verbose] [--help] --] <PATH> <ADDRESS> "
                           "[<ADDRESS>....]\n"
       "\n"
       "DESCRIPTION\n"
       "    Loads the executable pointed to by <PATH> and looks up and "
       "<ADDRESS>\n"
       "    arguments\n"
       "\n"
       "EXAMPLE\n"
       "   " PROGRAM_NAME " --arch=x86_64 -- /usr/lib/dyld 0x100000000\n");
  exit(0);
}
int main(int argc, char const *argv[]) {
  // Use a sentry object to properly initialize/terminate LLDB.
  LLDBSentry sentry;

  SBDebugger debugger(SBDebugger::Create());

  // Create a debugger instance so we can create a target
  if (!debugger.IsValid())
    fprintf(stderr, "error: failed to create a debugger object\n");

  bool show_usage = false;
  bool verbose = false;
  const char *arch = NULL;
  const char *platform = NULL;
  std::string short_options("h?");
  for (const struct option *opt = g_long_options; opt->name; ++opt) {
    if (isprint(opt->val)) {
      short_options.append(1, (char)opt->val);
      switch (opt->has_arg) {
      case no_argument:
        break;
      case required_argument:
        short_options.append(1, ':');
        break;
      case optional_argument:
        short_options.append(2, ':');
        break;
      }
    }
  }
#ifdef __GLIBC__
  optind = 0;
#else
  optreset = 1;
  optind = 1;
#endif
  char ch;
  while ((ch = getopt_long_only(argc, (char *const *)argv,
                                short_options.c_str(), g_long_options, 0)) !=
         -1) {
    switch (ch) {
    case 0:
      break;

    case 'a':
      if (arch != NULL) {
        fprintf(stderr,
                "error: the --arch option can only be specified once\n");
        exit(1);
      }
      arch = optarg;
      break;

    case 'p':
      platform = optarg;
      break;

    case 'v':
      verbose = true;
      break;

    case 'h':
    case '?':
    default:
      show_usage = true;
      break;
    }
  }
  argc -= optind;
  argv += optind;

  if (show_usage || argc < 2)
    usage();

  int arg_idx = 0;
  // The first argument is the file path we want to look something up in
  const char *exe_file_path = argv[arg_idx];
  const char *addr_cstr;
  const bool add_dependent_libs = false;
  SBError error;
  SBStream strm;
  strm.RedirectToFileHandle(stdout, false);

  while ((addr_cstr = argv[++arg_idx]) != NULL) {
    // The second argument in the address that we want to lookup
    lldb::addr_t file_addr = strtoull(addr_cstr, NULL, 0);

    // Create a target using the executable.
    SBTarget target = debugger.CreateTarget(exe_file_path, arch, platform,
                                            add_dependent_libs, error);
    if (!error.Success()) {
      fprintf(stderr, "error: %s\n", error.GetCString());
      exit(1);
    }

    printf("%sLooking up 0x%llx in '%s':\n", (arg_idx > 1) ? "\n" : "",
           file_addr, exe_file_path);

    if (target.IsValid()) {
      // Find the executable module so we can do a lookup inside it
      SBFileSpec exe_file_spec(exe_file_path, true);
      SBModule module(target.FindModule(exe_file_spec));

      // Take a file virtual address and resolve it to a section offset
      // address that can be used to do a symbol lookup by address
      SBAddress addr = module.ResolveFileAddress(file_addr);
      bool success = addr.IsValid() && addr.GetSection().IsValid();
      if (success) {
        // We can resolve a section offset address in the module
        // and only ask for what we need. You can logical or together
        // bits from the SymbolContextItem enumeration found in
        // lldb-enumeration.h to request only what you want. Here we
        // are asking for everything.
        //
        // NOTE: the less you ask for, the less LLDB will parse as
        // LLDB does partial parsing on just about everything.
        SBSymbolContext sc(module.ResolveSymbolContextForAddress(
            addr, eSymbolContextEverything));

        strm.Printf("    Address: %s + 0x%llx\n    Summary: ",
                    addr.GetSection().GetName(), addr.GetOffset());
        addr.GetDescription(strm);
        strm.Printf("\n");
        if (verbose)
          sc.GetDescription(strm);
      } else {
        printf(
            "error: 0x%llx does not resolve to a valid file address in '%s'\n",
            file_addr, exe_file_path);
      }
    }
  }

  return 0;
}
