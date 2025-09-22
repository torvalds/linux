//===---------- ExecutorSharedMemoryMapperService.cpp -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/TargetProcess/ExecutorSharedMemoryMapperService.h"

#include "llvm/ExecutionEngine/Orc/Shared/OrcRTBridge.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/WindowsError.h"

#include <sstream>

#if defined(LLVM_ON_UNIX)
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#if defined(__MVS__)
#include "llvm/Support/BLAKE3.h"
#include <sys/shm.h>
#endif
#include <unistd.h>
#endif

namespace llvm {
namespace orc {
namespace rt_bootstrap {

#if defined(_WIN32)
static DWORD getWindowsProtectionFlags(MemProt MP) {
  if (MP == MemProt::Read)
    return PAGE_READONLY;
  if (MP == MemProt::Write ||
      MP == (MemProt::Write | MemProt::Read)) {
    // Note: PAGE_WRITE is not supported by VirtualProtect
    return PAGE_READWRITE;
  }
  if (MP == (MemProt::Read | MemProt::Exec))
    return PAGE_EXECUTE_READ;
  if (MP == (MemProt::Read | MemProt::Write | MemProt::Exec))
    return PAGE_EXECUTE_READWRITE;
  if (MP == MemProt::Exec)
    return PAGE_EXECUTE;

  return PAGE_NOACCESS;
}
#endif

Expected<std::pair<ExecutorAddr, std::string>>
ExecutorSharedMemoryMapperService::reserve(uint64_t Size) {
#if (defined(LLVM_ON_UNIX) && !defined(__ANDROID__)) || defined(_WIN32)

#if defined(LLVM_ON_UNIX)

  std::string SharedMemoryName;
  {
    std::stringstream SharedMemoryNameStream;
    SharedMemoryNameStream << "/jitlink_" << sys::Process::getProcessId() << '_'
                           << (++SharedMemoryCount);
    SharedMemoryName = SharedMemoryNameStream.str();
  }

#if defined(__MVS__)
  ArrayRef<uint8_t> Data(
      reinterpret_cast<const uint8_t *>(SharedMemoryName.c_str()),
      SharedMemoryName.size());
  auto HashedName = BLAKE3::hash<sizeof(key_t)>(Data);
  key_t Key = *reinterpret_cast<key_t *>(HashedName.data());
  int SharedMemoryId =
      shmget(Key, Size, IPC_CREAT | IPC_EXCL | __IPC_SHAREAS | 0700);
  if (SharedMemoryId < 0)
    return errorCodeToError(errnoAsErrorCode());

  void *Addr = shmat(SharedMemoryId, nullptr, 0);
  if (Addr == reinterpret_cast<void *>(-1))
    return errorCodeToError(errnoAsErrorCode());
#else
  int SharedMemoryFile =
      shm_open(SharedMemoryName.c_str(), O_RDWR | O_CREAT | O_EXCL, 0700);
  if (SharedMemoryFile < 0)
    return errorCodeToError(errnoAsErrorCode());

  // by default size is 0
  if (ftruncate(SharedMemoryFile, Size) < 0)
    return errorCodeToError(errnoAsErrorCode());

  void *Addr = mmap(nullptr, Size, PROT_NONE, MAP_SHARED, SharedMemoryFile, 0);
  if (Addr == MAP_FAILED)
    return errorCodeToError(errnoAsErrorCode());

  close(SharedMemoryFile);
#endif

#elif defined(_WIN32)

  std::string SharedMemoryName;
  {
    std::stringstream SharedMemoryNameStream;
    SharedMemoryNameStream << "jitlink_" << sys::Process::getProcessId() << '_'
                           << (++SharedMemoryCount);
    SharedMemoryName = SharedMemoryNameStream.str();
  }

  std::wstring WideSharedMemoryName(SharedMemoryName.begin(),
                                    SharedMemoryName.end());
  HANDLE SharedMemoryFile = CreateFileMappingW(
      INVALID_HANDLE_VALUE, NULL, PAGE_EXECUTE_READWRITE, Size >> 32,
      Size & 0xffffffff, WideSharedMemoryName.c_str());
  if (!SharedMemoryFile)
    return errorCodeToError(mapWindowsError(GetLastError()));

  void *Addr = MapViewOfFile(SharedMemoryFile,
                             FILE_MAP_ALL_ACCESS | FILE_MAP_EXECUTE, 0, 0, 0);
  if (!Addr) {
    CloseHandle(SharedMemoryFile);
    return errorCodeToError(mapWindowsError(GetLastError()));
  }

#endif

  {
    std::lock_guard<std::mutex> Lock(Mutex);
    Reservations[Addr].Size = Size;
#if defined(_WIN32)
    Reservations[Addr].SharedMemoryFile = SharedMemoryFile;
#endif
  }

  return std::make_pair(ExecutorAddr::fromPtr(Addr),
                        std::move(SharedMemoryName));
#else
  return make_error<StringError>(
      "SharedMemoryMapper is not supported on this platform yet",
      inconvertibleErrorCode());
#endif
}

Expected<ExecutorAddr> ExecutorSharedMemoryMapperService::initialize(
    ExecutorAddr Reservation, tpctypes::SharedMemoryFinalizeRequest &FR) {
#if (defined(LLVM_ON_UNIX) && !defined(__ANDROID__)) || defined(_WIN32)

  ExecutorAddr MinAddr(~0ULL);

  // Contents are already in place
  for (auto &Segment : FR.Segments) {
    if (Segment.Addr < MinAddr)
      MinAddr = Segment.Addr;

#if defined(LLVM_ON_UNIX)

#if defined(__MVS__)
      // TODO Is it possible to change the protection level?
#else
    int NativeProt = 0;
    if ((Segment.RAG.Prot & MemProt::Read) == MemProt::Read)
      NativeProt |= PROT_READ;
    if ((Segment.RAG.Prot & MemProt::Write) == MemProt::Write)
      NativeProt |= PROT_WRITE;
    if ((Segment.RAG.Prot & MemProt::Exec) == MemProt::Exec)
      NativeProt |= PROT_EXEC;

    if (mprotect(Segment.Addr.toPtr<void *>(), Segment.Size, NativeProt))
      return errorCodeToError(errnoAsErrorCode());
#endif

#elif defined(_WIN32)

    DWORD NativeProt = getWindowsProtectionFlags(Segment.RAG.Prot);

    if (!VirtualProtect(Segment.Addr.toPtr<void *>(), Segment.Size, NativeProt,
                        &NativeProt))
      return errorCodeToError(mapWindowsError(GetLastError()));

#endif

    if ((Segment.RAG.Prot & MemProt::Exec) == MemProt::Exec)
      sys::Memory::InvalidateInstructionCache(Segment.Addr.toPtr<void *>(),
                                              Segment.Size);
  }

  // Run finalization actions and get deinitlization action list.
  auto DeinitializeActions = shared::runFinalizeActions(FR.Actions);
  if (!DeinitializeActions) {
    return DeinitializeActions.takeError();
  }

  {
    std::lock_guard<std::mutex> Lock(Mutex);
    Allocations[MinAddr].DeinitializationActions =
        std::move(*DeinitializeActions);
    Reservations[Reservation.toPtr<void *>()].Allocations.push_back(MinAddr);
  }

  return MinAddr;

#else
  return make_error<StringError>(
      "SharedMemoryMapper is not supported on this platform yet",
      inconvertibleErrorCode());
#endif
}

Error ExecutorSharedMemoryMapperService::deinitialize(
    const std::vector<ExecutorAddr> &Bases) {
  Error AllErr = Error::success();

  {
    std::lock_guard<std::mutex> Lock(Mutex);

    for (auto Base : llvm::reverse(Bases)) {
      if (Error Err = shared::runDeallocActions(
              Allocations[Base].DeinitializationActions)) {
        AllErr = joinErrors(std::move(AllErr), std::move(Err));
      }

      // Remove the allocation from the allocation list of its reservation
      for (auto &Reservation : Reservations) {
        auto AllocationIt = llvm::find(Reservation.second.Allocations, Base);
        if (AllocationIt != Reservation.second.Allocations.end()) {
          Reservation.second.Allocations.erase(AllocationIt);
          break;
        }
      }

      Allocations.erase(Base);
    }
  }

  return AllErr;
}

Error ExecutorSharedMemoryMapperService::release(
    const std::vector<ExecutorAddr> &Bases) {
#if (defined(LLVM_ON_UNIX) && !defined(__ANDROID__)) || defined(_WIN32)
  Error Err = Error::success();

  for (auto Base : Bases) {
    std::vector<ExecutorAddr> AllocAddrs;
    size_t Size;

#if defined(_WIN32)
    HANDLE SharedMemoryFile;
#endif

    {
      std::lock_guard<std::mutex> Lock(Mutex);
      auto &R = Reservations[Base.toPtr<void *>()];
      Size = R.Size;

#if defined(_WIN32)
      SharedMemoryFile = R.SharedMemoryFile;
#endif

      AllocAddrs.swap(R.Allocations);
    }

    // deinitialize sub allocations
    if (Error E = deinitialize(AllocAddrs))
      Err = joinErrors(std::move(Err), std::move(E));

#if defined(LLVM_ON_UNIX)

#if defined(__MVS__)
    (void)Size;

    if (shmdt(Base.toPtr<void *>()) < 0)
      Err = joinErrors(std::move(Err), errorCodeToError(errnoAsErrorCode()));
#else
    if (munmap(Base.toPtr<void *>(), Size) != 0)
      Err = joinErrors(std::move(Err), errorCodeToError(errnoAsErrorCode()));
#endif

#elif defined(_WIN32)
    (void)Size;

    if (!UnmapViewOfFile(Base.toPtr<void *>()))
      Err = joinErrors(std::move(Err),
                       errorCodeToError(mapWindowsError(GetLastError())));

    CloseHandle(SharedMemoryFile);

#endif

    std::lock_guard<std::mutex> Lock(Mutex);
    Reservations.erase(Base.toPtr<void *>());
  }

  return Err;
#else
  return make_error<StringError>(
      "SharedMemoryMapper is not supported on this platform yet",
      inconvertibleErrorCode());
#endif
}

Error ExecutorSharedMemoryMapperService::shutdown() {
  if (Reservations.empty())
    return Error::success();

  std::vector<ExecutorAddr> ReservationAddrs;
  ReservationAddrs.reserve(Reservations.size());
  for (const auto &R : Reservations)
    ReservationAddrs.push_back(ExecutorAddr::fromPtr(R.getFirst()));

  return release(std::move(ReservationAddrs));
}

void ExecutorSharedMemoryMapperService::addBootstrapSymbols(
    StringMap<ExecutorAddr> &M) {
  M[rt::ExecutorSharedMemoryMapperServiceInstanceName] =
      ExecutorAddr::fromPtr(this);
  M[rt::ExecutorSharedMemoryMapperServiceReserveWrapperName] =
      ExecutorAddr::fromPtr(&reserveWrapper);
  M[rt::ExecutorSharedMemoryMapperServiceInitializeWrapperName] =
      ExecutorAddr::fromPtr(&initializeWrapper);
  M[rt::ExecutorSharedMemoryMapperServiceDeinitializeWrapperName] =
      ExecutorAddr::fromPtr(&deinitializeWrapper);
  M[rt::ExecutorSharedMemoryMapperServiceReleaseWrapperName] =
      ExecutorAddr::fromPtr(&releaseWrapper);
}

llvm::orc::shared::CWrapperFunctionResult
ExecutorSharedMemoryMapperService::reserveWrapper(const char *ArgData,
                                                  size_t ArgSize) {
  return shared::WrapperFunction<
             rt::SPSExecutorSharedMemoryMapperServiceReserveSignature>::
      handle(ArgData, ArgSize,
             shared::makeMethodWrapperHandler(
                 &ExecutorSharedMemoryMapperService::reserve))
          .release();
}

llvm::orc::shared::CWrapperFunctionResult
ExecutorSharedMemoryMapperService::initializeWrapper(const char *ArgData,
                                                     size_t ArgSize) {
  return shared::WrapperFunction<
             rt::SPSExecutorSharedMemoryMapperServiceInitializeSignature>::
      handle(ArgData, ArgSize,
             shared::makeMethodWrapperHandler(
                 &ExecutorSharedMemoryMapperService::initialize))
          .release();
}

llvm::orc::shared::CWrapperFunctionResult
ExecutorSharedMemoryMapperService::deinitializeWrapper(const char *ArgData,
                                                       size_t ArgSize) {
  return shared::WrapperFunction<
             rt::SPSExecutorSharedMemoryMapperServiceDeinitializeSignature>::
      handle(ArgData, ArgSize,
             shared::makeMethodWrapperHandler(
                 &ExecutorSharedMemoryMapperService::deinitialize))
          .release();
}

llvm::orc::shared::CWrapperFunctionResult
ExecutorSharedMemoryMapperService::releaseWrapper(const char *ArgData,
                                                  size_t ArgSize) {
  return shared::WrapperFunction<
             rt::SPSExecutorSharedMemoryMapperServiceReleaseSignature>::
      handle(ArgData, ArgSize,
             shared::makeMethodWrapperHandler(
                 &ExecutorSharedMemoryMapperService::release))
          .release();
}

} // namespace rt_bootstrap
} // end namespace orc
} // end namespace llvm
