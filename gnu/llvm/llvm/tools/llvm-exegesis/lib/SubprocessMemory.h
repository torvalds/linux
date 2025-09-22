//===-- SubprocessMemory.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Defines a class that automatically handles auxiliary memory and the
/// underlying shared memory backings for memory definitions
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_EXEGESIS_SUBPROCESSMEMORY_H
#define LLVM_TOOLS_LLVM_EXEGESIS_SUBPROCESSMEMORY_H

#include "BenchmarkResult.h"
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _MSC_VER
typedef int pid_t;
#else
#include <sys/types.h>
#endif // _MSC_VER


namespace llvm {
namespace exegesis {

class SubprocessMemory {
public:
  static constexpr const size_t AuxiliaryMemoryOffset = 1;
  static constexpr const size_t AuxiliaryMemorySize = 4096;

  // Gets the thread ID for the calling thread.
  static long getCurrentTID();

  Error initializeSubprocessMemory(pid_t ProcessID);

  // The following function sets up memory definitions. It creates shared
  // memory objects for the definitions and fills them with the specified
  // values. Arguments: MemoryDefinitions - A map from memory value names to
  // MemoryValues, ProcessID - The ID of the current process.
  Error addMemoryDefinition(
      std::unordered_map<std::string, MemoryValue> MemoryDefinitions,
      pid_t ProcessID);

  // The following function sets up the auxiliary memory by opening shared
  // memory objects backing memory definitions and putting file descriptors
  // into appropriate places. Arguments: MemoryDefinitions - A map from memory
  // values names to Memoryvalues, ParentPID - The ID of the process that
  // setup the memory definitions, CounterFileDescriptor - The file descriptor
  // for the performance counter that will be placed in the auxiliary memory
  // section.
  static Expected<int> setupAuxiliaryMemoryInSubprocess(
      std::unordered_map<std::string, MemoryValue> MemoryDefinitions,
      pid_t ParentPID, long ParentTID, int CounterFileDescriptor);

  ~SubprocessMemory();

private:
  std::vector<std::string> SharedMemoryNames;
};

} // namespace exegesis
} // namespace llvm

#endif
