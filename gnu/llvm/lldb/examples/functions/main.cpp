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
// create an executable target without adding dependent shared
// libraries. It will then set a regular expression breakpoint to get
// breakpoint locations for all functions in the module, and use the
// locations to extract the symbol context for each location. Then it
// dumps all // information about the function: its name, file address
// range, the return type (if any), and all argument types.
//
// To build the program, type (while in this directory):
//
//    $ make
//
// then to run this on MacOSX, specify the path to your LLDB.framework
// library using the DYLD_FRAMEWORK_PATH option and run the executable
//
//    $ DYLD_FRAMEWORK_PATH=/Volumes/data/lldb/tot/build/Debug ./a.out
//    executable_path1 [executable_path2 ...]
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
    {"arch", required_argument, NULL, 'a'},
    {"canonical", no_argument, NULL, 'c'},
    {"extern", no_argument, NULL, 'x'},
    {"help", no_argument, NULL, 'h'},
    {"platform", required_argument, NULL, 'p'},
    {"verbose", no_argument, NULL, 'v'},
    {NULL, 0, NULL, 0}};

#define PROGRAM_NAME "lldb-functions"
void usage() {
  puts("NAME\n"
       "    " PROGRAM_NAME
       " -- extract all function signatures from one or more binaries.\n"
       "\n"
       "SYNOPSIS\n"
       "    " PROGRAM_NAME " [[--arch=<ARCH>] [--platform=<PLATFORM>] "
                           "[--verbose] [--help] [--canonical] --] <PATH> "
                           "[<PATH>....]\n"
       "\n"
       "DESCRIPTION\n"
       "    Loads the executable pointed to by <PATH> and dumps complete "
       "signatures for all functions that have debug information.\n"
       "\n"
       "EXAMPLE\n"
       "   " PROGRAM_NAME " --arch=x86_64 /usr/lib/dyld\n");
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
  bool canonical = false;
  bool external_only = false;
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

    case 'c':
      canonical = true;
      break;

    case 'x':
      external_only = true;
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

  const bool add_dependent_libs = false;
  SBError error;
  for (int arg_idx = 0; arg_idx < argc; ++arg_idx) {
    // The first argument is the file path we want to look something up in
    const char *exe_file_path = argv[arg_idx];

    // Create a target using the executable.
    SBTarget target = debugger.CreateTarget(exe_file_path, arch, platform,
                                            add_dependent_libs, error);

    if (error.Success()) {
      if (target.IsValid()) {
        SBFileSpec exe_file_spec(exe_file_path, true);
        SBModule module(target.FindModule(exe_file_spec));
        SBFileSpecList comp_unit_list;

        if (module.IsValid()) {
          char command[1024];
          lldb::SBCommandReturnObject command_result;
          snprintf(command, sizeof(command), "add-dsym --uuid %s",
                   module.GetUUIDString());
          debugger.GetCommandInterpreter().HandleCommand(command,
                                                         command_result);
          if (!command_result.Succeeded()) {
            fprintf(stderr, "error: couldn't locate debug symbols for '%s'\n",
                    exe_file_path);
            exit(1);
          }

          SBFileSpecList module_list;
          module_list.Append(exe_file_spec);
          SBBreakpoint bp =
              target.BreakpointCreateByRegex(".", module_list, comp_unit_list);

          const size_t num_locations = bp.GetNumLocations();
          for (uint32_t bp_loc_idx = 0; bp_loc_idx < num_locations;
               ++bp_loc_idx) {
            SBBreakpointLocation bp_loc = bp.GetLocationAtIndex(bp_loc_idx);
            SBSymbolContext sc(
                bp_loc.GetAddress().GetSymbolContext(eSymbolContextEverything));
            if (sc.IsValid()) {
              if (sc.GetBlock().GetContainingInlinedBlock().IsValid()) {
                // Skip inlined functions
                continue;
              }
              SBFunction function(sc.GetFunction());
              if (function.IsValid()) {
                addr_t lo_pc = function.GetStartAddress().GetFileAddress();
                if (lo_pc == LLDB_INVALID_ADDRESS) {
                  // Skip functions that don't have concrete instances in the
                  // binary
                  continue;
                }
                addr_t hi_pc = function.GetEndAddress().GetFileAddress();
                const char *func_demangled_name = function.GetName();
                const char *func_mangled_name = function.GetMangledName();

                bool dump = true;
                const bool is_objc_method = ((func_demangled_name[0] == '-') ||
                                             (func_demangled_name[0] == '+')) &&
                                            (func_demangled_name[1] == '[');
                if (external_only) {
                  // Dump all objective C methods, or external symbols
                  dump = is_objc_method;
                  if (!dump)
                    dump = sc.GetSymbol().IsExternal();
                }

                if (dump) {
                  if (verbose) {
                    printf("\n   name: %s\n", func_demangled_name);
                    if (func_mangled_name)
                      printf("mangled: %s\n", func_mangled_name);
                    printf("  range: [0x%16.16llx - 0x%16.16llx)\n   type: ",
                           lo_pc, hi_pc);
                  } else {
                    printf("[0x%16.16llx - 0x%16.16llx) ", lo_pc, hi_pc);
                  }
                  SBType function_type = function.GetType();
                  SBType return_type = function_type.GetFunctionReturnType();

                  if (canonical)
                    return_type = return_type.GetCanonicalType();

                  if (func_mangled_name && func_mangled_name[0] == '_' &&
                      func_mangled_name[1] == 'Z') {
                    printf("%s %s\n", return_type.GetName(),
                           func_demangled_name);
                  } else {
                    SBTypeList function_args =
                        function_type.GetFunctionArgumentTypes();
                    const size_t num_function_args = function_args.GetSize();

                    if (is_objc_method) {
                      const char *class_name_start = func_demangled_name + 2;

                      if (num_function_args == 0) {
                        printf("%c(%s)[%s\n", func_demangled_name[0],
                               return_type.GetName(), class_name_start);
                      } else {
                        const char *class_name_end =
                            strchr(class_name_start, ' ');
                        const int class_name_len =
                            class_name_end - class_name_start;
                        printf("%c(%s)[%*.*s", func_demangled_name[0],
                               return_type.GetName(), class_name_len,
                               class_name_len, class_name_start);

                        const char *selector_pos = class_name_end + 1;
                        for (uint32_t function_arg_idx = 0;
                             function_arg_idx < num_function_args;
                             ++function_arg_idx) {
                          const char *selector_end =
                              strchr(selector_pos, ':') + 1;
                          const int selector_len = selector_end - selector_pos;
                          SBType function_arg_type =
                              function_args.GetTypeAtIndex(function_arg_idx);

                          if (canonical)
                            function_arg_type =
                                function_arg_type.GetCanonicalType();

                          printf(" %*.*s", selector_len, selector_len,
                                 selector_pos);
                          if (function_arg_type.IsValid()) {
                            printf("(%s)", function_arg_type.GetName());
                          } else {
                            printf("(?)");
                          }
                          selector_pos = selector_end;
                        }
                        printf("]\n");
                      }
                    } else {
                      printf("%s ", return_type.GetName());
                      if (strchr(func_demangled_name, '('))
                        printf("(*)(");
                      else
                        printf("%s(", func_demangled_name);

                      for (uint32_t function_arg_idx = 0;
                           function_arg_idx < num_function_args;
                           ++function_arg_idx) {
                        SBType function_arg_type =
                            function_args.GetTypeAtIndex(function_arg_idx);

                        if (canonical)
                          function_arg_type =
                              function_arg_type.GetCanonicalType();

                        if (function_arg_type.IsValid()) {
                          printf("%s%s", function_arg_idx > 0 ? ", " : "",
                                 function_arg_type.GetName());
                        } else {
                          printf("%s???", function_arg_idx > 0 ? ", " : "");
                        }
                      }
                      printf(")\n");
                    }
                  }
                }
              }
            }
          }
        }
      }
    } else {
      fprintf(stderr, "error: %s\n", error.GetCString());
      exit(1);
    }
  }

  return 0;
}
