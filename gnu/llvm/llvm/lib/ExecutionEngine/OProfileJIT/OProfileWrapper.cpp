//===-- OProfileWrapper.cpp - OProfile JIT API Wrapper implementation -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the interface in OProfileWrapper.h. It is responsible
// for loading the opagent dynamic library when the first call to an op_
// function occurs.
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/OProfileWrapper.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/Mutex.h"
#include "llvm/Support/raw_ostream.h"
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <mutex>
#include <stddef.h>
#include <sys/stat.h>
#include <unistd.h>

#define DEBUG_TYPE "oprofile-wrapper"

namespace {

// Global mutex to ensure a single thread initializes oprofile agent.
llvm::sys::Mutex OProfileInitializationMutex;

} // anonymous namespace

namespace llvm {

OProfileWrapper::OProfileWrapper()
: Agent(0),
  OpenAgentFunc(0),
  CloseAgentFunc(0),
  WriteNativeCodeFunc(0),
  WriteDebugLineInfoFunc(0),
  UnloadNativeCodeFunc(0),
  MajorVersionFunc(0),
  MinorVersionFunc(0),
  IsOProfileRunningFunc(0),
  Initialized(false) {
}

bool OProfileWrapper::initialize() {
  using namespace llvm;
  using namespace llvm::sys;

  std::lock_guard<sys::Mutex> Guard(OProfileInitializationMutex);

  if (Initialized)
    return OpenAgentFunc != 0;

  Initialized = true;

  // If the oprofile daemon is not running, don't load the opagent library
  if (!isOProfileRunning()) {
    LLVM_DEBUG(dbgs() << "OProfile daemon is not detected.\n");
    return false;
  }

  std::string error;
  if(!DynamicLibrary::LoadLibraryPermanently("libopagent.so", &error)) {
    LLVM_DEBUG(
        dbgs()
        << "OProfile connector library libopagent.so could not be loaded: "
        << error << "\n");
  }

  // Get the addresses of the opagent functions
  OpenAgentFunc = (op_open_agent_ptr_t)(intptr_t)
          DynamicLibrary::SearchForAddressOfSymbol("op_open_agent");
  CloseAgentFunc = (op_close_agent_ptr_t)(intptr_t)
          DynamicLibrary::SearchForAddressOfSymbol("op_close_agent");
  WriteNativeCodeFunc = (op_write_native_code_ptr_t)(intptr_t)
          DynamicLibrary::SearchForAddressOfSymbol("op_write_native_code");
  WriteDebugLineInfoFunc = (op_write_debug_line_info_ptr_t)(intptr_t)
          DynamicLibrary::SearchForAddressOfSymbol("op_write_debug_line_info");
  UnloadNativeCodeFunc = (op_unload_native_code_ptr_t)(intptr_t)
          DynamicLibrary::SearchForAddressOfSymbol("op_unload_native_code");
  MajorVersionFunc = (op_major_version_ptr_t)(intptr_t)
          DynamicLibrary::SearchForAddressOfSymbol("op_major_version");
  MinorVersionFunc = (op_major_version_ptr_t)(intptr_t)
          DynamicLibrary::SearchForAddressOfSymbol("op_minor_version");

  // With missing functions, we can do nothing
  if (!OpenAgentFunc
      || !CloseAgentFunc
      || !WriteNativeCodeFunc
      || !WriteDebugLineInfoFunc
      || !UnloadNativeCodeFunc) {
    OpenAgentFunc = 0;
    CloseAgentFunc = 0;
    WriteNativeCodeFunc = 0;
    WriteDebugLineInfoFunc = 0;
    UnloadNativeCodeFunc = 0;
    return false;
  }

  return true;
}

bool OProfileWrapper::isOProfileRunning() {
  if (IsOProfileRunningFunc != 0)
    return IsOProfileRunningFunc();
  return checkForOProfileProcEntry();
}

bool OProfileWrapper::checkForOProfileProcEntry() {
  DIR* ProcDir;

  ProcDir = opendir("/proc");
  if (!ProcDir)
    return false;

  // Walk the /proc tree looking for the oprofile daemon
  struct dirent* Entry;
  while (0 != (Entry = readdir(ProcDir))) {
    if (Entry->d_type == DT_DIR) {
      // Build a path from the current entry name
      SmallString<256> CmdLineFName;
      raw_svector_ostream(CmdLineFName) << "/proc/" << Entry->d_name
                                        << "/cmdline";

      // Open the cmdline file
      int CmdLineFD = open(CmdLineFName.c_str(), S_IRUSR);
      if (CmdLineFD != -1) {
        char    ExeName[PATH_MAX+1];
        char*   BaseName = 0;

        // Read the cmdline file
        ssize_t NumRead = read(CmdLineFD, ExeName, PATH_MAX+1);
        close(CmdLineFD);
        ssize_t Idx = 0;

        if (ExeName[0] != '/') {
          BaseName = ExeName;
        }

        // Find the terminator for the first string
        while (Idx < NumRead-1 && ExeName[Idx] != 0) {
          Idx++;
        }

        // Go back to the last non-null character
        Idx--;

        // Find the last path separator in the first string
        while (Idx > 0) {
          if (ExeName[Idx] == '/') {
            BaseName = ExeName + Idx + 1;
            break;
          }
          Idx--;
        }

        // Test this to see if it is the oprofile daemon
        if (BaseName != 0 && (!strcmp("oprofiled", BaseName) ||
                              !strcmp("operf", BaseName))) {
          // If it is, we're done
          closedir(ProcDir);
          return true;
        }
      }
    }
  }

  // We've looked through all the files and didn't find the daemon
  closedir(ProcDir);
  return false;
}

bool OProfileWrapper::op_open_agent() {
  if (!Initialized)
    initialize();

  if (OpenAgentFunc != 0) {
    Agent = OpenAgentFunc();
    return Agent != 0;
  }

  return false;
}

int OProfileWrapper::op_close_agent() {
  if (!Initialized)
    initialize();

  int ret = -1;
  if (Agent && CloseAgentFunc) {
    ret = CloseAgentFunc(Agent);
    if (ret == 0) {
      Agent = 0;
    }
  }
  return ret;
}

bool OProfileWrapper::isAgentAvailable() {
  return Agent != 0;
}

int OProfileWrapper::op_write_native_code(const char* Name,
                                          uint64_t Addr,
                                          void const* Code,
                                          const unsigned int Size) {
  if (!Initialized)
    initialize();

  if (Agent && WriteNativeCodeFunc)
    return WriteNativeCodeFunc(Agent, Name, Addr, Code, Size);

  return -1;
}

int OProfileWrapper::op_write_debug_line_info(
  void const* Code,
  size_t NumEntries,
  struct debug_line_info const* Info) {
  if (!Initialized)
    initialize();

  if (Agent && WriteDebugLineInfoFunc)
    return WriteDebugLineInfoFunc(Agent, Code, NumEntries, Info);

  return -1;
}

int OProfileWrapper::op_major_version() {
  if (!Initialized)
    initialize();

  if (Agent && MajorVersionFunc)
    return MajorVersionFunc();

  return -1;
}

int OProfileWrapper::op_minor_version() {
  if (!Initialized)
    initialize();

  if (Agent && MinorVersionFunc)
    return MinorVersionFunc();

  return -1;
}

int  OProfileWrapper::op_unload_native_code(uint64_t Addr) {
  if (!Initialized)
    initialize();

  if (Agent && UnloadNativeCodeFunc)
    return UnloadNativeCodeFunc(Agent, Addr);

  return -1;
}

} // namespace llvm
