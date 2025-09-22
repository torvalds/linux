//===-- sanitizer_symbolizer_internal.h -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Header for internal classes and functions to be used by implementations of
// symbolizers.
//
//===----------------------------------------------------------------------===//
#ifndef SANITIZER_SYMBOLIZER_INTERNAL_H
#define SANITIZER_SYMBOLIZER_INTERNAL_H

#include "sanitizer_file.h"
#include "sanitizer_symbolizer.h"
#include "sanitizer_vector.h"

namespace __sanitizer {

// Parsing helpers, 'str' is searched for delimiter(s) and a string or uptr
// is extracted. When extracting a string, a newly allocated (using
// InternalAlloc) and null-terminated buffer is returned. They return a pointer
// to the next characted after the found delimiter.
const char *ExtractToken(const char *str, const char *delims, char **result);
const char *ExtractInt(const char *str, const char *delims, int *result);
const char *ExtractUptr(const char *str, const char *delims, uptr *result);
const char *ExtractTokenUpToDelimiter(const char *str, const char *delimiter,
                                      char **result);

const char *DemangleSwiftAndCXX(const char *name);

// SymbolizerTool is an interface that is implemented by individual "tools"
// that can perform symbolication (external llvm-symbolizer, libbacktrace,
// Windows DbgHelp symbolizer, etc.).
class SymbolizerTool {
 public:
  // The main |Symbolizer| class implements a "fallback chain" of symbolizer
  // tools. In a request to symbolize an address, if one tool returns false,
  // the next tool in the chain will be tried.
  SymbolizerTool *next;

  SymbolizerTool() : next(nullptr) { }

  // Can't declare pure virtual functions in sanitizer runtimes:
  // __cxa_pure_virtual might be unavailable.

  // The |stack| parameter is inout. It is pre-filled with the address,
  // module base and module offset values and is to be used to construct
  // other stack frames.
  virtual bool SymbolizePC(uptr addr, SymbolizedStack *stack) {
    UNIMPLEMENTED();
  }

  // The |info| parameter is inout. It is pre-filled with the module base
  // and module offset values.
  virtual bool SymbolizeData(uptr addr, DataInfo *info) {
    UNIMPLEMENTED();
  }

  virtual bool SymbolizeFrame(uptr addr, FrameInfo *info) {
    return false;
  }

  virtual void Flush() {}

  // Return nullptr to fallback to the default platform-specific demangler.
  virtual const char *Demangle(const char *name) {
    return nullptr;
  }

 protected:
  ~SymbolizerTool() {}
};

// SymbolizerProcess encapsulates communication between the tool and
// external symbolizer program, running in a different subprocess.
// SymbolizerProcess may not be used from two threads simultaneously.
class SymbolizerProcess {
 public:
  explicit SymbolizerProcess(const char *path, bool use_posix_spawn = false);
  const char *SendCommand(const char *command);

 protected:
  ~SymbolizerProcess() {}

  /// The maximum number of arguments required to invoke a tool process.
  static const unsigned kArgVMax = 16;

  // Customizable by subclasses.
  virtual bool StartSymbolizerSubprocess();
  virtual bool ReadFromSymbolizer();
  // Return the environment to run the symbolizer in.
  virtual char **GetEnvP() { return GetEnviron(); }
  InternalMmapVector<char> &GetBuff() { return buffer_; }

 private:
  virtual bool ReachedEndOfOutput(const char *buffer, uptr length) const {
    UNIMPLEMENTED();
  }

  /// Fill in an argv array to invoke the child process.
  virtual void GetArgV(const char *path_to_binary,
                       const char *(&argv)[kArgVMax]) const {
    UNIMPLEMENTED();
  }

  bool Restart();
  const char *SendCommandImpl(const char *command);
  bool WriteToSymbolizer(const char *buffer, uptr length);

  const char *path_;
  fd_t input_fd_;
  fd_t output_fd_;

  InternalMmapVector<char> buffer_;

  static const uptr kMaxTimesRestarted = 5;
  static const int kSymbolizerStartupTimeMillis = 10;
  uptr times_restarted_;
  bool failed_to_start_;
  bool reported_invalid_path_;
  bool use_posix_spawn_;
};

class LLVMSymbolizerProcess;

// This tool invokes llvm-symbolizer in a subprocess. It should be as portable
// as the llvm-symbolizer tool is.
class LLVMSymbolizer final : public SymbolizerTool {
 public:
  explicit LLVMSymbolizer(const char *path, LowLevelAllocator *allocator);

  bool SymbolizePC(uptr addr, SymbolizedStack *stack) override;
  bool SymbolizeData(uptr addr, DataInfo *info) override;
  bool SymbolizeFrame(uptr addr, FrameInfo *info) override;

 private:
  const char *FormatAndSendCommand(const char *command_prefix,
                                   const char *module_name, uptr module_offset,
                                   ModuleArch arch);

  LLVMSymbolizerProcess *symbolizer_process_;
  static const uptr kBufferSize = 16 * 1024;
  char buffer_[kBufferSize];
};

// Parses one or more two-line strings in the following format:
//   <function_name>
//   <file_name>:<line_number>[:<column_number>]
// Used by LLVMSymbolizer, Addr2LinePool and InternalSymbolizer, since all of
// them use the same output format.  Returns true if any useful debug
// information was found.
void ParseSymbolizePCOutput(const char *str, SymbolizedStack *res);

// Parses a two-line string in the following format:
//   <symbol_name>
//   <start_address> <size>
// Used by LLVMSymbolizer and InternalSymbolizer.
void ParseSymbolizeDataOutput(const char *str, DataInfo *info);

// Parses repeated strings in the following format:
//   <function_name>
//   <var_name>
//   <file_name>:<line_number>[:<column_number>]
//   [<frame_offset>|??] [<size>|??] [<tag_offset>|??]
// Used by LLVMSymbolizer and InternalSymbolizer.
void ParseSymbolizeFrameOutput(const char *str,
                               InternalMmapVector<LocalInfo> *locals);

}  // namespace __sanitizer

#endif  // SANITIZER_SYMBOLIZER_INTERNAL_H
