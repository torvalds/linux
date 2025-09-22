//===-- sanitizer_symbolizer_mac.cpp --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is shared between various sanitizers' runtime libraries.
//
// Implementation of Mac-specific "atos" symbolizer.
//===----------------------------------------------------------------------===//

#include "sanitizer_platform.h"
#if SANITIZER_APPLE

#  include <dlfcn.h>
#  include <errno.h>
#  include <stdlib.h>
#  include <sys/wait.h>
#  include <unistd.h>
#  include <util.h>

#  include "sanitizer_allocator_internal.h"
#  include "sanitizer_mac.h"
#  include "sanitizer_symbolizer_mac.h"

namespace __sanitizer {

bool DlAddrSymbolizer::SymbolizePC(uptr addr, SymbolizedStack *stack) {
  Dl_info info;
  int result = dladdr((const void *)addr, &info);
  if (!result) return false;

  // Compute offset if possible. `dladdr()` doesn't always ensure that `addr >=
  // sym_addr` so only compute the offset when this holds. Failure to find the
  // function offset is not treated as a failure because it might still be
  // possible to get the symbol name.
  uptr sym_addr = reinterpret_cast<uptr>(info.dli_saddr);
  if (addr >= sym_addr) {
    stack->info.function_offset = addr - sym_addr;
  }

  const char *demangled = DemangleSwiftAndCXX(info.dli_sname);
  if (!demangled)
    demangled = info.dli_sname;
  stack->info.function = internal_strdup(demangled);
  return true;
}

bool DlAddrSymbolizer::SymbolizeData(uptr addr, DataInfo *datainfo) {
  Dl_info info;
  int result = dladdr((const void *)addr, &info);
  if (!result) return false;
  const char *demangled = DemangleSwiftAndCXX(info.dli_sname);
  if (!demangled)
    demangled = info.dli_sname;
  datainfo->name = internal_strdup(demangled);
  datainfo->start = (uptr)info.dli_saddr;
  return true;
}

class AtosSymbolizerProcess final : public SymbolizerProcess {
 public:
  explicit AtosSymbolizerProcess(const char *path)
      : SymbolizerProcess(path, /*use_posix_spawn*/ true) {
    pid_str_[0] = '\0';
  }

 private:
  bool StartSymbolizerSubprocess() override {
    // Put the string command line argument in the object so that it outlives
    // the call to GetArgV.
    internal_snprintf(pid_str_, sizeof(pid_str_), "%d", (int)internal_getpid());

    // Configure sandbox before starting atos process.
    return SymbolizerProcess::StartSymbolizerSubprocess();
  }

  bool ReachedEndOfOutput(const char *buffer, uptr length) const override {
    return (length >= 1 && buffer[length - 1] == '\n');
  }

  void GetArgV(const char *path_to_binary,
               const char *(&argv)[kArgVMax]) const override {
    int i = 0;
    argv[i++] = path_to_binary;
    argv[i++] = "-p";
    argv[i++] = &pid_str_[0];
    if (GetMacosAlignedVersion() == MacosVersion(10, 9)) {
      // On Mavericks atos prints a deprecation warning which we suppress by
      // passing -d. The warning isn't present on other OSX versions, even the
      // newer ones.
      argv[i++] = "-d";
    }
    argv[i++] = nullptr;
    CHECK_LE(i, kArgVMax);
  }

  char pid_str_[16];
};

#undef K_ATOS_ENV_VAR

static bool ParseCommandOutput(const char *str, uptr addr, char **out_name,
                               char **out_module, char **out_file, uptr *line,
                               uptr *start_address) {
  // Trim ending newlines.
  char *trim;
  ExtractTokenUpToDelimiter(str, "\n", &trim);

  // The line from `atos` is in one of these formats:
  //   myfunction (in library.dylib) (sourcefile.c:17)
  //   myfunction (in library.dylib) + 0x1fe
  //   myfunction (in library.dylib) + 15
  //   0xdeadbeef (in library.dylib) + 0x1fe
  //   0xdeadbeef (in library.dylib) + 15
  //   0xdeadbeef (in library.dylib)
  //   0xdeadbeef

  const char *rest = trim;
  char *symbol_name;
  rest = ExtractTokenUpToDelimiter(rest, " (in ", &symbol_name);
  if (rest[0] == '\0') {
    InternalFree(symbol_name);
    InternalFree(trim);
    return false;
  }

  if (internal_strncmp(symbol_name, "0x", 2) != 0)
    *out_name = symbol_name;
  else
    InternalFree(symbol_name);
  rest = ExtractTokenUpToDelimiter(rest, ") ", out_module);

  if (rest[0] == '(') {
    if (out_file) {
      rest++;
      rest = ExtractTokenUpToDelimiter(rest, ":", out_file);
      char *extracted_line_number;
      rest = ExtractTokenUpToDelimiter(rest, ")", &extracted_line_number);
      if (line) *line = (uptr)internal_atoll(extracted_line_number);
      InternalFree(extracted_line_number);
    }
  } else if (rest[0] == '+') {
    rest += 2;
    uptr offset = internal_atoll(rest);
    if (start_address) *start_address = addr - offset;
  }

  InternalFree(trim);
  return true;
}

AtosSymbolizer::AtosSymbolizer(const char *path, LowLevelAllocator *allocator)
    : process_(new (*allocator) AtosSymbolizerProcess(path)) {}

bool AtosSymbolizer::SymbolizePC(uptr addr, SymbolizedStack *stack) {
  if (!process_) return false;
  if (addr == 0) return false;
  char command[32];
  internal_snprintf(command, sizeof(command), "0x%zx\n", addr);
  const char *buf = process_->SendCommand(command);
  if (!buf) return false;
  uptr line;
  uptr start_address = AddressInfo::kUnknown;
  if (!ParseCommandOutput(buf, addr, &stack->info.function, &stack->info.module,
                          &stack->info.file, &line, &start_address)) {
    Report("WARNING: atos failed to symbolize address \"0x%zx\"\n", addr);
    return false;
  }
  stack->info.line = (int)line;

  if (start_address == AddressInfo::kUnknown) {
    // Fallback to dladdr() to get function start address if atos doesn't report
    // it.
    Dl_info info;
    int result = dladdr((const void *)addr, &info);
    if (result)
      start_address = reinterpret_cast<uptr>(info.dli_saddr);
  }

  // Only assign to `function_offset` if we were able to get the function's
  // start address and we got a sensible `start_address` (dladdr doesn't always
  // ensure that `addr >= sym_addr`).
  if (start_address != AddressInfo::kUnknown && addr >= start_address) {
    stack->info.function_offset = addr - start_address;
  }
  return true;
}

bool AtosSymbolizer::SymbolizeData(uptr addr, DataInfo *info) {
  if (!process_) return false;
  char command[32];
  internal_snprintf(command, sizeof(command), "0x%zx\n", addr);
  const char *buf = process_->SendCommand(command);
  if (!buf) return false;
  if (!ParseCommandOutput(buf, addr, &info->name, &info->module, nullptr,
                          nullptr, &info->start)) {
    process_ = nullptr;
    return false;
  }
  return true;
}

}  // namespace __sanitizer

#endif  // SANITIZER_APPLE
