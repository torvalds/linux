//===-- SubprocessMemory.cpp ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SubprocessMemory.h"
#include "Error.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FormatVariadic.h"
#include <cerrno>

#ifdef __linux__
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace llvm {
namespace exegesis {

#if defined(__linux__)

// The SYS_* macros for system calls are provided by the libc whereas the
// __NR_* macros are from the linux headers. This means that sometimes
// SYS_* macros might not be available for certain system calls depending
// upon the libc. This happens with the gettid syscall and bionic for
// example, so we use __NR_gettid when no SYS_gettid is available.
#ifndef SYS_gettid
#define SYS_gettid __NR_gettid
#endif

long SubprocessMemory::getCurrentTID() {
  // We're using the raw syscall here rather than the gettid() function provided
  // by most libcs for compatibility as gettid() was only added to glibc in
  // version 2.30.
  return syscall(SYS_gettid);
}

#if !defined(__ANDROID__)

Error SubprocessMemory::initializeSubprocessMemory(pid_t ProcessID) {
  // Add the PID to the shared memory name so that if we're running multiple
  // processes at the same time, they won't interfere with each other.
  // This comes up particularly often when running the exegesis tests with
  // llvm-lit. Additionally add the TID so that downstream consumers
  // using multiple threads don't run into conflicts.
  std::string AuxiliaryMemoryName =
      formatv("/{0}auxmem{1}", getCurrentTID(), ProcessID);
  int AuxiliaryMemoryFD = shm_open(AuxiliaryMemoryName.c_str(),
                                   O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
  if (AuxiliaryMemoryFD == -1)
    return make_error<Failure>(
        "Failed to create shared memory object for auxiliary memory: " +
        Twine(strerror(errno)));
  auto AuxiliaryMemoryFDClose =
      make_scope_exit([AuxiliaryMemoryFD]() { close(AuxiliaryMemoryFD); });
  if (ftruncate(AuxiliaryMemoryFD, AuxiliaryMemorySize) != 0) {
    return make_error<Failure>("Truncating the auxiliary memory failed: " +
                               Twine(strerror(errno)));
  }
  SharedMemoryNames.push_back(AuxiliaryMemoryName);
  return Error::success();
}

Error SubprocessMemory::addMemoryDefinition(
    std::unordered_map<std::string, MemoryValue> MemoryDefinitions,
    pid_t ProcessPID) {
  SharedMemoryNames.reserve(MemoryDefinitions.size());
  for (auto &[Name, MemVal] : MemoryDefinitions) {
    std::string SharedMemoryName =
        formatv("/{0}t{1}memdef{2}", ProcessPID, getCurrentTID(), MemVal.Index);
    SharedMemoryNames.push_back(SharedMemoryName);
    int SharedMemoryFD =
        shm_open(SharedMemoryName.c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
    if (SharedMemoryFD == -1)
      return make_error<Failure>(
          "Failed to create shared memory object for memory definition: " +
          Twine(strerror(errno)));
    auto SharedMemoryFDClose =
        make_scope_exit([SharedMemoryFD]() { close(SharedMemoryFD); });
    if (ftruncate(SharedMemoryFD, MemVal.SizeBytes) != 0) {
      return make_error<Failure>("Truncating a memory definiton failed: " +
                                 Twine(strerror(errno)));
    }

    char *SharedMemoryMapping =
        (char *)mmap(NULL, MemVal.SizeBytes, PROT_READ | PROT_WRITE, MAP_SHARED,
                     SharedMemoryFD, 0);
    // fill the buffer with the specified value
    size_t CurrentByte = 0;
    const size_t ValueWidthBytes = MemVal.Value.getBitWidth() / 8;
    while (CurrentByte < MemVal.SizeBytes - ValueWidthBytes) {
      memcpy(SharedMemoryMapping + CurrentByte, MemVal.Value.getRawData(),
             ValueWidthBytes);
      CurrentByte += ValueWidthBytes;
    }
    // fill the last section
    memcpy(SharedMemoryMapping + CurrentByte, MemVal.Value.getRawData(),
           MemVal.SizeBytes - CurrentByte);
    if (munmap(SharedMemoryMapping, MemVal.SizeBytes) != 0) {
      return make_error<Failure>(
          "Unmapping a memory definition in the parent failed: " +
          Twine(strerror(errno)));
    }
  }
  return Error::success();
}

Expected<int> SubprocessMemory::setupAuxiliaryMemoryInSubprocess(
    std::unordered_map<std::string, MemoryValue> MemoryDefinitions,
    pid_t ParentPID, long ParentTID, int CounterFileDescriptor) {
  std::string AuxiliaryMemoryName =
      formatv("/{0}auxmem{1}", ParentTID, ParentPID);
  int AuxiliaryMemoryFileDescriptor =
      shm_open(AuxiliaryMemoryName.c_str(), O_RDWR, S_IRUSR | S_IWUSR);
  if (AuxiliaryMemoryFileDescriptor == -1)
    return make_error<Failure>(
        "Getting file descriptor for auxiliary memory failed: " +
        Twine(strerror(errno)));
  // set up memory value file descriptors
  int *AuxiliaryMemoryMapping =
      (int *)mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED,
                  AuxiliaryMemoryFileDescriptor, 0);
  if ((intptr_t)AuxiliaryMemoryMapping == -1)
    return make_error<Failure>("Mapping auxiliary memory failed");
  AuxiliaryMemoryMapping[0] = CounterFileDescriptor;
  for (auto &[Name, MemVal] : MemoryDefinitions) {
    std::string MemoryValueName =
        formatv("/{0}t{1}memdef{2}", ParentPID, ParentTID, MemVal.Index);
    AuxiliaryMemoryMapping[AuxiliaryMemoryOffset + MemVal.Index] =
        shm_open(MemoryValueName.c_str(), O_RDWR, S_IRUSR | S_IWUSR);
    if (AuxiliaryMemoryMapping[AuxiliaryMemoryOffset + MemVal.Index] == -1)
      return make_error<Failure>("Mapping shared memory failed");
  }
  if (munmap(AuxiliaryMemoryMapping, 4096) == -1)
    return make_error<Failure>("Unmapping auxiliary memory failed");
  return AuxiliaryMemoryFileDescriptor;
}

SubprocessMemory::~SubprocessMemory() {
  for (const std::string &SharedMemoryName : SharedMemoryNames) {
    if (shm_unlink(SharedMemoryName.c_str()) != 0) {
      errs() << "Failed to unlink shared memory section: " << strerror(errno)
             << "\n";
    }
  }
}

#else

Error SubprocessMemory::initializeSubprocessMemory(pid_t ProcessPID) {
  return make_error<Failure>(
      "initializeSubprocessMemory is only supported on Linux");
}

Error SubprocessMemory::addMemoryDefinition(
    std::unordered_map<std::string, MemoryValue> MemoryDefinitions,
    pid_t ProcessPID) {
  return make_error<Failure>("addMemoryDefinitions is only supported on Linux");
}

Expected<int> SubprocessMemory::setupAuxiliaryMemoryInSubprocess(
    std::unordered_map<std::string, MemoryValue> MemoryDefinitions,
    pid_t ParentPID, long ParentTID, int CounterFileDescriptor) {
  return make_error<Failure>(
      "setupAuxiliaryMemoryInSubprocess is only supported on Linux");
}

SubprocessMemory::~SubprocessMemory() {}

#endif // !defined(__ANDROID__)
#endif // defined(__linux__)

} // namespace exegesis
} // namespace llvm
