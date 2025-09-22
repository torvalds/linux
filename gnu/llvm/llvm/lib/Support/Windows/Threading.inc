//===- Windows/Threading.inc - Win32 Threading Implementation - -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides the Win32 specific implementation of Threading functions.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/Twine.h"

#include "llvm/Support/Windows/WindowsSupport.h"
#include <process.h>

#include <bitset>

// Windows will at times define MemoryFence.
#ifdef MemoryFence
#undef MemoryFence
#endif

namespace llvm {
HANDLE
llvm_execute_on_thread_impl(unsigned(__stdcall *ThreadFunc)(void *), void *Arg,
                            std::optional<unsigned> StackSizeInBytes) {
  HANDLE hThread = (HANDLE)::_beginthreadex(NULL, StackSizeInBytes.value_or(0),
                                            ThreadFunc, Arg, 0, NULL);

  if (!hThread) {
    ReportLastErrorFatal("_beginthreadex failed");
  }

  return hThread;
}

void llvm_thread_join_impl(HANDLE hThread) {
  if (::WaitForSingleObject(hThread, INFINITE) == WAIT_FAILED) {
    ReportLastErrorFatal("WaitForSingleObject failed");
  }
}

void llvm_thread_detach_impl(HANDLE hThread) {
  if (::CloseHandle(hThread) == FALSE) {
    ReportLastErrorFatal("CloseHandle failed");
  }
}

DWORD llvm_thread_get_id_impl(HANDLE hThread) { return ::GetThreadId(hThread); }

DWORD llvm_thread_get_current_id_impl() { return ::GetCurrentThreadId(); }

} // namespace llvm

uint64_t llvm::get_threadid() { return uint64_t(::GetCurrentThreadId()); }

uint32_t llvm::get_max_thread_name_length() { return 0; }

#if defined(_MSC_VER)
static void SetThreadName(DWORD Id, LPCSTR Name) {
  constexpr DWORD MS_VC_EXCEPTION = 0x406D1388;

#pragma pack(push, 8)
  struct THREADNAME_INFO {
    DWORD dwType;     // Must be 0x1000.
    LPCSTR szName;    // Pointer to thread name
    DWORD dwThreadId; // Thread ID (-1 == current thread)
    DWORD dwFlags;    // Reserved.  Do not use.
  };
#pragma pack(pop)

  THREADNAME_INFO info;
  info.dwType = 0x1000;
  info.szName = Name;
  info.dwThreadId = Id;
  info.dwFlags = 0;

  __try {
    ::RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR),
                     (ULONG_PTR *)&info);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
}
#endif

void llvm::set_thread_name(const Twine &Name) {
#if defined(_MSC_VER)
  // Make sure the input is null terminated.
  SmallString<64> Storage;
  StringRef NameStr = Name.toNullTerminatedStringRef(Storage);
  SetThreadName(::GetCurrentThreadId(), NameStr.data());
#endif
}

void llvm::get_thread_name(SmallVectorImpl<char> &Name) {
  // "Name" is not an inherent property of a thread on Windows.  In fact, when
  // you "set" the name, you are only firing a one-time message to a debugger
  // which it interprets as a program setting its threads' name.  We may be
  // able to get fancy by creating a TLS entry when someone calls
  // set_thread_name so that subsequent calls to get_thread_name return this
  // value.
  Name.clear();
}

SetThreadPriorityResult llvm::set_thread_priority(ThreadPriority Priority) {
  // https://docs.microsoft.com/en-us/windows/desktop/api/processthreadsapi/nf-processthreadsapi-setthreadpriority
  // Begin background processing mode. The system lowers the resource scheduling
  // priorities of the thread so that it can perform background work without
  // significantly affecting activity in the foreground.
  // End background processing mode. The system restores the resource scheduling
  // priorities of the thread as they were before the thread entered background
  // processing mode.
  //
  // FIXME: consider THREAD_PRIORITY_BELOW_NORMAL for Low
  return SetThreadPriority(GetCurrentThread(),
                           Priority != ThreadPriority::Default
                               ? THREAD_MODE_BACKGROUND_BEGIN
                               : THREAD_MODE_BACKGROUND_END)
             ? SetThreadPriorityResult::SUCCESS
             : SetThreadPriorityResult::FAILURE;
}

struct ProcessorGroup {
  unsigned ID;
  unsigned AllThreads;
  unsigned UsableThreads;
  unsigned ThreadsPerCore;
  uint64_t Affinity;

  unsigned useableCores() const {
    return std::max(1U, UsableThreads / ThreadsPerCore);
  }
};

template <typename F>
static bool IterateProcInfo(LOGICAL_PROCESSOR_RELATIONSHIP Relationship, F Fn) {
  DWORD Len = 0;
  BOOL R = ::GetLogicalProcessorInformationEx(Relationship, NULL, &Len);
  if (R || GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
    return false;
  }
  auto *Info = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *)calloc(1, Len);
  R = ::GetLogicalProcessorInformationEx(Relationship, Info, &Len);
  if (R) {
    auto *End =
        (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *)((uint8_t *)Info + Len);
    for (auto *Curr = Info; Curr < End;
         Curr = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *)((uint8_t *)Curr +
                                                            Curr->Size)) {
      if (Curr->Relationship != Relationship)
        continue;
      Fn(Curr);
    }
  }
  free(Info);
  return true;
}

static std::optional<std::vector<USHORT>> getActiveGroups() {
  USHORT Count = 0;
  if (::GetProcessGroupAffinity(GetCurrentProcess(), &Count, nullptr))
    return std::nullopt;

  if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
    return std::nullopt;

  std::vector<USHORT> Groups;
  Groups.resize(Count);
  if (!::GetProcessGroupAffinity(GetCurrentProcess(), &Count, Groups.data()))
    return std::nullopt;

  return Groups;
}

static ArrayRef<ProcessorGroup> getProcessorGroups() {
  auto computeGroups = []() {
    SmallVector<ProcessorGroup, 4> Groups;

    auto HandleGroup = [&](SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *ProcInfo) {
      GROUP_RELATIONSHIP &El = ProcInfo->Group;
      for (unsigned J = 0; J < El.ActiveGroupCount; ++J) {
        ProcessorGroup G;
        G.ID = Groups.size();
        G.AllThreads = El.GroupInfo[J].MaximumProcessorCount;
        G.UsableThreads = El.GroupInfo[J].ActiveProcessorCount;
        assert(G.UsableThreads <= 64);
        G.Affinity = El.GroupInfo[J].ActiveProcessorMask;
        Groups.push_back(G);
      }
    };

    if (!IterateProcInfo(RelationGroup, HandleGroup))
      return std::vector<ProcessorGroup>();

    auto HandleProc = [&](SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *ProcInfo) {
      PROCESSOR_RELATIONSHIP &El = ProcInfo->Processor;
      assert(El.GroupCount == 1);
      unsigned NumHyperThreads = 1;
      // If the flag is set, each core supports more than one hyper-thread.
      if (El.Flags & LTP_PC_SMT)
        NumHyperThreads = std::bitset<64>(El.GroupMask[0].Mask).count();
      unsigned I = El.GroupMask[0].Group;
      Groups[I].ThreadsPerCore = NumHyperThreads;
    };

    if (!IterateProcInfo(RelationProcessorCore, HandleProc))
      return std::vector<ProcessorGroup>();

    auto ActiveGroups = getActiveGroups();
    if (!ActiveGroups)
      return std::vector<ProcessorGroup>();

    // If there's an affinity mask set, assume the user wants to constrain the
    // current process to only a single CPU group. On Windows, it is not
    // possible for affinity masks to cross CPU group boundaries.
    DWORD_PTR ProcessAffinityMask = 0, SystemAffinityMask = 0;
    if (::GetProcessAffinityMask(GetCurrentProcess(), &ProcessAffinityMask,
                                 &SystemAffinityMask)) {

      if (ProcessAffinityMask != SystemAffinityMask) {
        if (llvm::RunningWindows11OrGreater() && ActiveGroups->size() > 1) {
          // The process affinity mask is spurious, due to an OS bug, ignore it.
          return std::vector<ProcessorGroup>(Groups.begin(), Groups.end());
        }

        assert(ActiveGroups->size() == 1 &&
               "When an affinity mask is set, the process is expected to be "
               "assigned to a single processor group!");

        unsigned CurrentGroupID = (*ActiveGroups)[0];
        ProcessorGroup NewG{Groups[CurrentGroupID]};
        NewG.Affinity = ProcessAffinityMask;
        NewG.UsableThreads = llvm::popcount(ProcessAffinityMask);
        Groups.clear();
        Groups.push_back(NewG);
      }
    }
    return std::vector<ProcessorGroup>(Groups.begin(), Groups.end());
  };
  static auto Groups = computeGroups();
  return ArrayRef<ProcessorGroup>(Groups);
}

template <typename R, typename UnaryPredicate>
static unsigned aggregate(R &&Range, UnaryPredicate P) {
  unsigned I{};
  for (const auto &It : Range)
    I += P(It);
  return I;
}

int llvm::get_physical_cores() {
  static unsigned Cores =
      aggregate(getProcessorGroups(), [](const ProcessorGroup &G) {
        return G.UsableThreads / G.ThreadsPerCore;
      });
  return Cores;
}

static int computeHostNumHardwareThreads() {
  static unsigned Threads =
      aggregate(getProcessorGroups(),
                [](const ProcessorGroup &G) { return G.UsableThreads; });
  return Threads;
}

// Finds the proper CPU socket where a thread number should go. Returns
// 'std::nullopt' if the thread shall remain on the actual CPU socket.
std::optional<unsigned>
llvm::ThreadPoolStrategy::compute_cpu_socket(unsigned ThreadPoolNum) const {
  ArrayRef<ProcessorGroup> Groups = getProcessorGroups();
  // Only one CPU socket in the system or process affinity was set, no need to
  // move the thread(s) to another CPU socket.
  if (Groups.size() <= 1)
    return std::nullopt;

  // We ask for less threads than there are hardware threads per CPU socket, no
  // need to dispatch threads to other CPU sockets.
  unsigned MaxThreadsPerSocket =
      UseHyperThreads ? Groups[0].UsableThreads : Groups[0].useableCores();
  if (compute_thread_count() <= MaxThreadsPerSocket)
    return std::nullopt;

  assert(ThreadPoolNum < compute_thread_count() &&
         "The thread index is not within thread strategy's range!");

  // Assumes the same number of hardware threads per CPU socket.
  return (ThreadPoolNum * Groups.size()) / compute_thread_count();
}

// Assign the current thread to a more appropriate CPU socket or CPU group
void llvm::ThreadPoolStrategy::apply_thread_strategy(
    unsigned ThreadPoolNum) const {

  // After Windows 11 and Windows Server 2022, let the OS do the scheduling,
  // since a process automatically gains access to all processor groups.
  if (llvm::RunningWindows11OrGreater())
    return;

  std::optional<unsigned> Socket = compute_cpu_socket(ThreadPoolNum);
  if (!Socket)
    return;
  ArrayRef<ProcessorGroup> Groups = getProcessorGroups();
  GROUP_AFFINITY Affinity{};
  Affinity.Group = Groups[*Socket].ID;
  Affinity.Mask = Groups[*Socket].Affinity;
  SetThreadGroupAffinity(GetCurrentThread(), &Affinity, nullptr);
}

llvm::BitVector llvm::get_thread_affinity_mask() {
  GROUP_AFFINITY Affinity{};
  GetThreadGroupAffinity(GetCurrentThread(), &Affinity);

  static unsigned All =
      aggregate(getProcessorGroups(),
                [](const ProcessorGroup &G) { return G.AllThreads; });

  unsigned StartOffset =
      aggregate(getProcessorGroups(), [&](const ProcessorGroup &G) {
        return G.ID < Affinity.Group ? G.AllThreads : 0;
      });

  llvm::BitVector V;
  V.resize(All);
  for (unsigned I = 0; I < sizeof(KAFFINITY) * 8; ++I) {
    if ((Affinity.Mask >> I) & 1)
      V.set(StartOffset + I);
  }
  return V;
}

unsigned llvm::get_cpus() { return getProcessorGroups().size(); }
