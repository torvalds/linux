//===- Signals.cpp - Generic Unix Signals Implementation -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines some helpful functions for dealing with the possibility of
// Unix signals occurring while your program is running.
//
//===----------------------------------------------------------------------===//
//
// This file is extremely careful to only do signal-safe things while in a
// signal handler. In particular, memory allocation and acquiring a mutex
// while in a signal handler should never occur. ManagedStatic isn't usable from
// a signal handler for 2 reasons:
//
//  1. Creating a new one allocates.
//  2. The signal handler could fire while llvm_shutdown is being processed, in
//     which case the ManagedStatic is in an unknown state because it could
//     already have been destroyed, or be in the process of being destroyed.
//
// Modifying the behavior of the signal handlers (such as registering new ones)
// can acquire a mutex, but all this guarantees is that the signal handler
// behavior is only modified by one thread at a time. A signal handler can still
// fire while this occurs!
//
// Adding work to a signal handler requires lock-freedom (and assume atomics are
// always lock-free) because the signal handler could fire while new work is
// being added.
//
//===----------------------------------------------------------------------===//

#include "Unix.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Config/config.h"
#include "llvm/Demangle/Demangle.h"
#include "llvm/Support/ExitCodes.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Mutex.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/SaveAndRestore.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <string>
#ifdef HAVE_BACKTRACE
#include BACKTRACE_HEADER // For backtrace().
#endif
#if HAVE_SIGNAL_H
#include <signal.h>
#endif
#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#if HAVE_DLFCN_H
#include <dlfcn.h>
#endif
#if HAVE_MACH_MACH_H
#include <mach/mach.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#if HAVE_LINK_H
#include <link.h>
#endif
#ifdef HAVE__UNWIND_BACKTRACE
// FIXME: We should be able to use <unwind.h> for any target that has an
// _Unwind_Backtrace function, but on FreeBSD the configure test passes
// despite the function not existing, and on Android, <unwind.h> conflicts
// with <link.h>.
#ifdef __GLIBC__
#include <unwind.h>
#else
#undef HAVE__UNWIND_BACKTRACE
#endif
#endif

using namespace llvm;

static void SignalHandler(int Sig);     // defined below.
static void InfoSignalHandler(int Sig); // defined below.

using SignalHandlerFunctionType = void (*)();
/// The function to call if ctrl-c is pressed.
static std::atomic<SignalHandlerFunctionType> InterruptFunction = nullptr;
static std::atomic<SignalHandlerFunctionType> InfoSignalFunction = nullptr;
/// The function to call on SIGPIPE (one-time use only).
static std::atomic<SignalHandlerFunctionType> OneShotPipeSignalFunction =
    nullptr;

namespace {
/// Signal-safe removal of files.
/// Inserting and erasing from the list isn't signal-safe, but removal of files
/// themselves is signal-safe. Memory is freed when the head is freed, deletion
/// is therefore not signal-safe either.
class FileToRemoveList {
  std::atomic<char *> Filename = nullptr;
  std::atomic<FileToRemoveList *> Next = nullptr;

  FileToRemoveList() = default;
  // Not signal-safe.
  FileToRemoveList(const std::string &str) : Filename(strdup(str.c_str())) {}

public:
  // Not signal-safe.
  ~FileToRemoveList() {
    if (FileToRemoveList *N = Next.exchange(nullptr))
      delete N;
    if (char *F = Filename.exchange(nullptr))
      free(F);
  }

  // Not signal-safe.
  static void insert(std::atomic<FileToRemoveList *> &Head,
                     const std::string &Filename) {
    // Insert the new file at the end of the list.
    FileToRemoveList *NewHead = new FileToRemoveList(Filename);
    std::atomic<FileToRemoveList *> *InsertionPoint = &Head;
    FileToRemoveList *OldHead = nullptr;
    while (!InsertionPoint->compare_exchange_strong(OldHead, NewHead)) {
      InsertionPoint = &OldHead->Next;
      OldHead = nullptr;
    }
  }

  // Not signal-safe.
  static void erase(std::atomic<FileToRemoveList *> &Head,
                    const std::string &Filename) {
    // Use a lock to avoid concurrent erase: the comparison would access
    // free'd memory.
    static ManagedStatic<sys::SmartMutex<true>> Lock;
    sys::SmartScopedLock<true> Writer(*Lock);

    for (FileToRemoveList *Current = Head.load(); Current;
         Current = Current->Next.load()) {
      if (char *OldFilename = Current->Filename.load()) {
        if (OldFilename != Filename)
          continue;
        // Leave an empty filename.
        OldFilename = Current->Filename.exchange(nullptr);
        // The filename might have become null between the time we
        // compared it and we exchanged it.
        if (OldFilename)
          free(OldFilename);
      }
    }
  }

  // Signal-safe.
  static void removeAllFiles(std::atomic<FileToRemoveList *> &Head) {
    // If cleanup were to occur while we're removing files we'd have a bad time.
    // Make sure we're OK by preventing cleanup from doing anything while we're
    // removing files. If cleanup races with us and we win we'll have a leak,
    // but we won't crash.
    FileToRemoveList *OldHead = Head.exchange(nullptr);

    for (FileToRemoveList *currentFile = OldHead; currentFile;
         currentFile = currentFile->Next.load()) {
      // If erasing was occuring while we're trying to remove files we'd look
      // at free'd data. Take away the path and put it back when done.
      if (char *path = currentFile->Filename.exchange(nullptr)) {
        // Get the status so we can determine if it's a file or directory. If we
        // can't stat the file, ignore it.
        struct stat buf;
        if (stat(path, &buf) != 0)
          continue;

        // If this is not a regular file, ignore it. We want to prevent removal
        // of special files like /dev/null, even if the compiler is being run
        // with the super-user permissions.
        if (!S_ISREG(buf.st_mode))
          continue;

        // Otherwise, remove the file. We ignore any errors here as there is
        // nothing else we can do.
        unlink(path);

        // We're done removing the file, erasing can safely proceed.
        currentFile->Filename.exchange(path);
      }
    }

    // We're done removing files, cleanup can safely proceed.
    Head.exchange(OldHead);
  }
};
static std::atomic<FileToRemoveList *> FilesToRemove = nullptr;

/// Clean up the list in a signal-friendly manner.
/// Recall that signals can fire during llvm_shutdown. If this occurs we should
/// either clean something up or nothing at all, but we shouldn't crash!
struct FilesToRemoveCleanup {
  // Not signal-safe.
  ~FilesToRemoveCleanup() {
    FileToRemoveList *Head = FilesToRemove.exchange(nullptr);
    if (Head)
      delete Head;
  }
};
} // namespace

static StringRef Argv0;

/// Signals that represent requested termination. There's no bug or failure, or
/// if there is, it's not our direct responsibility. For whatever reason, our
/// continued execution is no longer desirable.
static const int IntSigs[] = {SIGHUP, SIGINT, SIGTERM, SIGUSR2};

/// Signals that represent that we have a bug, and our prompt termination has
/// been ordered.
static const int KillSigs[] = {SIGILL,
                               SIGTRAP,
                               SIGABRT,
                               SIGFPE,
                               SIGBUS,
                               SIGSEGV,
                               SIGQUIT
#ifdef SIGSYS
                               ,
                               SIGSYS
#endif
#ifdef SIGXCPU
                               ,
                               SIGXCPU
#endif
#ifdef SIGXFSZ
                               ,
                               SIGXFSZ
#endif
#ifdef SIGEMT
                               ,
                               SIGEMT
#endif
};

/// Signals that represent requests for status.
static const int InfoSigs[] = {SIGUSR1
#ifdef SIGINFO
                               ,
                               SIGINFO
#endif
};

static const size_t NumSigs = std::size(IntSigs) + std::size(KillSigs) +
                              std::size(InfoSigs) + 1 /* SIGPIPE */;

static std::atomic<unsigned> NumRegisteredSignals = 0;
static struct {
  struct sigaction SA;
  int SigNo;
} RegisteredSignalInfo[NumSigs];

#if defined(HAVE_SIGALTSTACK)
// Hold onto both the old and new alternate signal stack so that it's not
// reported as a leak. We don't make any attempt to remove our alt signal
// stack if we remove our signal handlers; that can't be done reliably if
// someone else is also trying to do the same thing.
static stack_t OldAltStack;
LLVM_ATTRIBUTE_USED static void *NewAltStackPointer;

static void CreateSigAltStack() {
  const size_t AltStackSize = MINSIGSTKSZ + 64 * 1024;

  // If we're executing on the alternate stack, or we already have an alternate
  // signal stack that we're happy with, there's nothing for us to do. Don't
  // reduce the size, some other part of the process might need a larger stack
  // than we do.
  if (sigaltstack(nullptr, &OldAltStack) != 0 ||
      OldAltStack.ss_flags & SS_ONSTACK ||
      (OldAltStack.ss_sp && OldAltStack.ss_size >= AltStackSize))
    return;

  stack_t AltStack = {};
  AltStack.ss_sp = static_cast<char *>(safe_malloc(AltStackSize));
  NewAltStackPointer = AltStack.ss_sp; // Save to avoid reporting a leak.
  AltStack.ss_size = AltStackSize;
  if (sigaltstack(&AltStack, &OldAltStack) != 0)
    free(AltStack.ss_sp);
}
#else
static void CreateSigAltStack() {}
#endif

static void RegisterHandlers() { // Not signal-safe.
  // The mutex prevents other threads from registering handlers while we're
  // doing it. We also have to protect the handlers and their count because
  // a signal handler could fire while we're registering handlers.
  static ManagedStatic<sys::SmartMutex<true>> SignalHandlerRegistrationMutex;
  sys::SmartScopedLock<true> Guard(*SignalHandlerRegistrationMutex);

  // If the handlers are already registered, we're done.
  if (NumRegisteredSignals.load() != 0)
    return;

  // Create an alternate stack for signal handling. This is necessary for us to
  // be able to reliably handle signals due to stack overflow.
  CreateSigAltStack();

  enum class SignalKind { IsKill, IsInfo };
  auto registerHandler = [&](int Signal, SignalKind Kind) {
    unsigned Index = NumRegisteredSignals.load();
    assert(Index < std::size(RegisteredSignalInfo) &&
           "Out of space for signal handlers!");

    struct sigaction NewHandler;

    switch (Kind) {
    case SignalKind::IsKill:
      NewHandler.sa_handler = SignalHandler;
      NewHandler.sa_flags = SA_NODEFER | SA_RESETHAND | SA_ONSTACK;
      break;
    case SignalKind::IsInfo:
      NewHandler.sa_handler = InfoSignalHandler;
      NewHandler.sa_flags = SA_ONSTACK;
      break;
    }
    sigemptyset(&NewHandler.sa_mask);

    // Install the new handler, save the old one in RegisteredSignalInfo.
    sigaction(Signal, &NewHandler, &RegisteredSignalInfo[Index].SA);
    RegisteredSignalInfo[Index].SigNo = Signal;
    ++NumRegisteredSignals;
  };

  for (auto S : IntSigs)
    registerHandler(S, SignalKind::IsKill);
  for (auto S : KillSigs)
    registerHandler(S, SignalKind::IsKill);
  if (OneShotPipeSignalFunction)
    registerHandler(SIGPIPE, SignalKind::IsKill);
  for (auto S : InfoSigs)
    registerHandler(S, SignalKind::IsInfo);
}

void sys::unregisterHandlers() {
  // Restore all of the signal handlers to how they were before we showed up.
  for (unsigned i = 0, e = NumRegisteredSignals.load(); i != e; ++i) {
    sigaction(RegisteredSignalInfo[i].SigNo, &RegisteredSignalInfo[i].SA,
              nullptr);
    --NumRegisteredSignals;
  }
}

/// Process the FilesToRemove list.
static void RemoveFilesToRemove() {
  FileToRemoveList::removeAllFiles(FilesToRemove);
}

void sys::CleanupOnSignal(uintptr_t Context) {
  int Sig = (int)Context;

  if (llvm::is_contained(InfoSigs, Sig)) {
    InfoSignalHandler(Sig);
    return;
  }

  RemoveFilesToRemove();

  if (llvm::is_contained(IntSigs, Sig) || Sig == SIGPIPE)
    return;

  llvm::sys::RunSignalHandlers();
}

// The signal handler that runs.
static void SignalHandler(int Sig) {
  // Restore the signal behavior to default, so that the program actually
  // crashes when we return and the signal reissues.  This also ensures that if
  // we crash in our signal handler that the program will terminate immediately
  // instead of recursing in the signal handler.
  sys::unregisterHandlers();

  // Unmask all potentially blocked kill signals.
  sigset_t SigMask;
  sigfillset(&SigMask);
  sigprocmask(SIG_UNBLOCK, &SigMask, nullptr);

  {
    RemoveFilesToRemove();

    if (Sig == SIGPIPE)
      if (auto OldOneShotPipeFunction =
              OneShotPipeSignalFunction.exchange(nullptr))
        return OldOneShotPipeFunction();

    bool IsIntSig = llvm::is_contained(IntSigs, Sig);
    if (IsIntSig)
      if (auto OldInterruptFunction = InterruptFunction.exchange(nullptr))
        return OldInterruptFunction();

    if (Sig == SIGPIPE || IsIntSig) {
      raise(Sig); // Execute the default handler.
      return;
    }
  }

  // Otherwise if it is a fault (like SEGV) run any handler.
  llvm::sys::RunSignalHandlers();

#ifdef __s390__
  // On S/390, certain signals are delivered with PSW Address pointing to
  // *after* the faulting instruction.  Simply returning from the signal
  // handler would continue execution after that point, instead of
  // re-raising the signal.  Raise the signal manually in those cases.
  if (Sig == SIGILL || Sig == SIGFPE || Sig == SIGTRAP)
    raise(Sig);
#endif
}

static void InfoSignalHandler(int Sig) {
  SaveAndRestore SaveErrnoDuringASignalHandler(errno);
  if (SignalHandlerFunctionType CurrentInfoFunction = InfoSignalFunction)
    CurrentInfoFunction();
}

void llvm::sys::RunInterruptHandlers() { RemoveFilesToRemove(); }

void llvm::sys::SetInterruptFunction(void (*IF)()) {
  InterruptFunction.exchange(IF);
  RegisterHandlers();
}

void llvm::sys::SetInfoSignalFunction(void (*Handler)()) {
  InfoSignalFunction.exchange(Handler);
  RegisterHandlers();
}

void llvm::sys::SetOneShotPipeSignalFunction(void (*Handler)()) {
  OneShotPipeSignalFunction.exchange(Handler);
  RegisterHandlers();
}

void llvm::sys::DefaultOneShotPipeSignalHandler() {
  // Send a special return code that drivers can check for, from sysexits.h.
  exit(EX_IOERR);
}

// The public API
bool llvm::sys::RemoveFileOnSignal(StringRef Filename, std::string *ErrMsg) {
  // Ensure that cleanup will occur as soon as one file is added.
  static ManagedStatic<FilesToRemoveCleanup> FilesToRemoveCleanup;
  *FilesToRemoveCleanup;
  FileToRemoveList::insert(FilesToRemove, Filename.str());
  RegisterHandlers();
  return false;
}

// The public API
void llvm::sys::DontRemoveFileOnSignal(StringRef Filename) {
  FileToRemoveList::erase(FilesToRemove, Filename.str());
}

/// Add a function to be called when a signal is delivered to the process. The
/// handler can have a cookie passed to it to identify what instance of the
/// handler it is.
void llvm::sys::AddSignalHandler(sys::SignalHandlerCallback FnPtr,
                                 void *Cookie) { // Signal-safe.
  insertSignalHandler(FnPtr, Cookie);
  RegisterHandlers();
}

#if ENABLE_BACKTRACES && defined(HAVE_BACKTRACE) && HAVE_LINK_H &&             \
    (defined(__linux__) || defined(__FreeBSD__) ||                             \
     defined(__FreeBSD_kernel__) || defined(__NetBSD__))
struct DlIteratePhdrData {
  void **StackTrace;
  int depth;
  bool first;
  const char **modules;
  intptr_t *offsets;
  const char *main_exec_name;
};

static int dl_iterate_phdr_cb(dl_phdr_info *info, size_t size, void *arg) {
  DlIteratePhdrData *data = (DlIteratePhdrData *)arg;
  const char *name = data->first ? data->main_exec_name : info->dlpi_name;
  data->first = false;
  for (int i = 0; i < info->dlpi_phnum; i++) {
    const auto *phdr = &info->dlpi_phdr[i];
    if (phdr->p_type != PT_LOAD)
      continue;
    intptr_t beg = info->dlpi_addr + phdr->p_vaddr;
    intptr_t end = beg + phdr->p_memsz;
    for (int j = 0; j < data->depth; j++) {
      if (data->modules[j])
        continue;
      intptr_t addr = (intptr_t)data->StackTrace[j];
      if (beg <= addr && addr < end) {
        data->modules[j] = name;
        data->offsets[j] = addr - info->dlpi_addr;
      }
    }
  }
  return 0;
}

/// If this is an ELF platform, we can find all loaded modules and their virtual
/// addresses with dl_iterate_phdr.
static bool findModulesAndOffsets(void **StackTrace, int Depth,
                                  const char **Modules, intptr_t *Offsets,
                                  const char *MainExecutableName,
                                  StringSaver &StrPool) {
  DlIteratePhdrData data = {StackTrace, Depth,   true,
                            Modules,    Offsets, MainExecutableName};
  dl_iterate_phdr(dl_iterate_phdr_cb, &data);
  return true;
}

class DSOMarkupPrinter {
  llvm::raw_ostream &OS;
  const char *MainExecutableName;
  size_t ModuleCount = 0;
  bool IsFirst = true;

public:
  DSOMarkupPrinter(llvm::raw_ostream &OS, const char *MainExecutableName)
      : OS(OS), MainExecutableName(MainExecutableName) {}

  /// Print llvm-symbolizer markup describing the layout of the given DSO.
  void printDSOMarkup(dl_phdr_info *Info) {
    ArrayRef<uint8_t> BuildID = findBuildID(Info);
    if (BuildID.empty())
      return;
    OS << format("{{{module:%d:%s:elf:", ModuleCount,
                 IsFirst ? MainExecutableName : Info->dlpi_name);
    for (uint8_t X : BuildID)
      OS << format("%02x", X);
    OS << "}}}\n";

    for (int I = 0; I < Info->dlpi_phnum; I++) {
      const auto *Phdr = &Info->dlpi_phdr[I];
      if (Phdr->p_type != PT_LOAD)
        continue;
      uintptr_t StartAddress = Info->dlpi_addr + Phdr->p_vaddr;
      uintptr_t ModuleRelativeAddress = Phdr->p_vaddr;
      std::array<char, 4> ModeStr = modeStrFromFlags(Phdr->p_flags);
      OS << format("{{{mmap:%#016x:%#x:load:%d:%s:%#016x}}}\n", StartAddress,
                   Phdr->p_memsz, ModuleCount, &ModeStr[0],
                   ModuleRelativeAddress);
    }
    IsFirst = false;
    ModuleCount++;
  }

  /// Callback for use with dl_iterate_phdr. The last dl_iterate_phdr argument
  /// must be a pointer to an instance of this class.
  static int printDSOMarkup(dl_phdr_info *Info, size_t Size, void *Arg) {
    static_cast<DSOMarkupPrinter *>(Arg)->printDSOMarkup(Info);
    return 0;
  }

  // Returns the build ID for the given DSO as an array of bytes. Returns an
  // empty array if none could be found.
  ArrayRef<uint8_t> findBuildID(dl_phdr_info *Info) {
    for (int I = 0; I < Info->dlpi_phnum; I++) {
      const auto *Phdr = &Info->dlpi_phdr[I];
      if (Phdr->p_type != PT_NOTE)
        continue;

      ArrayRef<uint8_t> Notes(
          reinterpret_cast<const uint8_t *>(Info->dlpi_addr + Phdr->p_vaddr),
          Phdr->p_memsz);
      while (Notes.size() > 12) {
        uint32_t NameSize = *reinterpret_cast<const uint32_t *>(Notes.data());
        Notes = Notes.drop_front(4);
        uint32_t DescSize = *reinterpret_cast<const uint32_t *>(Notes.data());
        Notes = Notes.drop_front(4);
        uint32_t Type = *reinterpret_cast<const uint32_t *>(Notes.data());
        Notes = Notes.drop_front(4);

        ArrayRef<uint8_t> Name = Notes.take_front(NameSize);
        auto CurPos = reinterpret_cast<uintptr_t>(Notes.data());
        uint32_t BytesUntilDesc =
            alignToPowerOf2(CurPos + NameSize, 4) - CurPos;
        if (BytesUntilDesc >= Notes.size())
          break;
        Notes = Notes.drop_front(BytesUntilDesc);

        ArrayRef<uint8_t> Desc = Notes.take_front(DescSize);
        CurPos = reinterpret_cast<uintptr_t>(Notes.data());
        uint32_t BytesUntilNextNote =
            alignToPowerOf2(CurPos + DescSize, 4) - CurPos;
        if (BytesUntilNextNote > Notes.size())
          break;
        Notes = Notes.drop_front(BytesUntilNextNote);

        if (Type == 3 /*NT_GNU_BUILD_ID*/ && Name.size() >= 3 &&
            Name[0] == 'G' && Name[1] == 'N' && Name[2] == 'U')
          return Desc;
      }
    }
    return {};
  }

  // Returns a symbolizer markup string describing the permissions on a DSO
  // with the given p_flags.
  std::array<char, 4> modeStrFromFlags(uint32_t Flags) {
    std::array<char, 4> Mode;
    char *Cur = &Mode[0];
    if (Flags & PF_R)
      *Cur++ = 'r';
    if (Flags & PF_W)
      *Cur++ = 'w';
    if (Flags & PF_X)
      *Cur++ = 'x';
    *Cur = '\0';
    return Mode;
  }
};

static bool printMarkupContext(llvm::raw_ostream &OS,
                               const char *MainExecutableName) {
  OS << "{{{reset}}}\n";
  DSOMarkupPrinter MP(OS, MainExecutableName);
  dl_iterate_phdr(DSOMarkupPrinter::printDSOMarkup, &MP);
  return true;
}

#elif ENABLE_BACKTRACES && defined(__APPLE__) && defined(__LP64__)
static bool findModulesAndOffsets(void **StackTrace, int Depth,
                                  const char **Modules, intptr_t *Offsets,
                                  const char *MainExecutableName,
                                  StringSaver &StrPool) {
  uint32_t NumImgs = _dyld_image_count();
  for (uint32_t ImageIndex = 0; ImageIndex < NumImgs; ImageIndex++) {
    const char *Name = _dyld_get_image_name(ImageIndex);
    intptr_t Slide = _dyld_get_image_vmaddr_slide(ImageIndex);
    auto *Header =
        (const struct mach_header_64 *)_dyld_get_image_header(ImageIndex);
    if (Header == NULL)
      continue;
    auto Cmd = (const struct load_command *)(&Header[1]);
    for (uint32_t CmdNum = 0; CmdNum < Header->ncmds; ++CmdNum) {
      uint32_t BaseCmd = Cmd->cmd & ~LC_REQ_DYLD;
      if (BaseCmd == LC_SEGMENT_64) {
        auto CmdSeg64 = (const struct segment_command_64 *)Cmd;
        for (int j = 0; j < Depth; j++) {
          if (Modules[j])
            continue;
          intptr_t Addr = (intptr_t)StackTrace[j];
          if ((intptr_t)CmdSeg64->vmaddr + Slide <= Addr &&
              Addr < intptr_t(CmdSeg64->vmaddr + CmdSeg64->vmsize + Slide)) {
            Modules[j] = Name;
            Offsets[j] = Addr - Slide;
          }
        }
      }
      Cmd = (const load_command *)(((const char *)Cmd) + (Cmd->cmdsize));
    }
  }
  return true;
}

static bool printMarkupContext(llvm::raw_ostream &OS,
                               const char *MainExecutableName) {
  return false;
}
#else
/// Backtraces are not enabled or we don't yet know how to find all loaded DSOs
/// on this platform.
static bool findModulesAndOffsets(void **StackTrace, int Depth,
                                  const char **Modules, intptr_t *Offsets,
                                  const char *MainExecutableName,
                                  StringSaver &StrPool) {
  return false;
}

static bool printMarkupContext(llvm::raw_ostream &OS,
                               const char *MainExecutableName) {
  return false;
}
#endif // ENABLE_BACKTRACES && ... (findModulesAndOffsets variants)

#if ENABLE_BACKTRACES && defined(HAVE__UNWIND_BACKTRACE)
static int unwindBacktrace(void **StackTrace, int MaxEntries) {
  if (MaxEntries < 0)
    return 0;

  // Skip the first frame ('unwindBacktrace' itself).
  int Entries = -1;

  auto HandleFrame = [&](_Unwind_Context *Context) -> _Unwind_Reason_Code {
    // Apparently we need to detect reaching the end of the stack ourselves.
    void *IP = (void *)_Unwind_GetIP(Context);
    if (!IP)
      return _URC_END_OF_STACK;

    assert(Entries < MaxEntries && "recursively called after END_OF_STACK?");
    if (Entries >= 0)
      StackTrace[Entries] = IP;

    if (++Entries == MaxEntries)
      return _URC_END_OF_STACK;
    return _URC_NO_REASON;
  };

  _Unwind_Backtrace(
      [](_Unwind_Context *Context, void *Handler) {
        return (*static_cast<decltype(HandleFrame) *>(Handler))(Context);
      },
      static_cast<void *>(&HandleFrame));
  return std::max(Entries, 0);
}
#endif

// In the case of a program crash or fault, print out a stack trace so that the
// user has an indication of why and where we died.
//
// On glibc systems we have the 'backtrace' function, which works nicely, but
// doesn't demangle symbols.
void llvm::sys::PrintStackTrace(raw_ostream &OS, int Depth) {
#if ENABLE_BACKTRACES
  static void *StackTrace[256];
  int depth = 0;
#if defined(HAVE_BACKTRACE)
  // Use backtrace() to output a backtrace on Linux systems with glibc.
  if (!depth)
    depth = backtrace(StackTrace, static_cast<int>(std::size(StackTrace)));
#endif
#if defined(HAVE__UNWIND_BACKTRACE)
  // Try _Unwind_Backtrace() if backtrace() failed.
  if (!depth)
    depth =
        unwindBacktrace(StackTrace, static_cast<int>(std::size(StackTrace)));
#endif
  if (!depth)
    return;
  // If "Depth" is not provided by the caller, use the return value of
  // backtrace() for printing a symbolized stack trace.
  if (!Depth)
    Depth = depth;
  if (printMarkupStackTrace(Argv0, StackTrace, Depth, OS))
    return;
  if (printSymbolizedStackTrace(Argv0, StackTrace, Depth, OS))
    return;
  OS << "Stack dump without symbol names (ensure you have llvm-symbolizer in "
        "your PATH or set the environment var `LLVM_SYMBOLIZER_PATH` to point "
        "to it):\n";
#if HAVE_DLFCN_H && HAVE_DLADDR
  int width = 0;
  for (int i = 0; i < depth; ++i) {
    Dl_info dlinfo;
    dladdr(StackTrace[i], &dlinfo);
    const char *name = strrchr(dlinfo.dli_fname, '/');

    int nwidth;
    if (!name)
      nwidth = strlen(dlinfo.dli_fname);
    else
      nwidth = strlen(name) - 1;

    if (nwidth > width)
      width = nwidth;
  }

  for (int i = 0; i < depth; ++i) {
    Dl_info dlinfo;
    dladdr(StackTrace[i], &dlinfo);

    OS << format("%-2d", i);

    const char *name = strrchr(dlinfo.dli_fname, '/');
    if (!name)
      OS << format(" %-*s", width, dlinfo.dli_fname);
    else
      OS << format(" %-*s", width, name + 1);

    OS << format(" %#0*lx", (int)(sizeof(void *) * 2) + 2,
                 (unsigned long)StackTrace[i]);

    if (dlinfo.dli_sname != nullptr) {
      OS << ' ';
      if (char *d = itaniumDemangle(dlinfo.dli_sname)) {
        OS << d;
        free(d);
      } else {
        OS << dlinfo.dli_sname;
      }

      OS << format(" + %tu", (static_cast<const char *>(StackTrace[i]) -
                              static_cast<const char *>(dlinfo.dli_saddr)));
    }
    OS << '\n';
  }
#elif defined(HAVE_BACKTRACE)
  backtrace_symbols_fd(StackTrace, Depth, STDERR_FILENO);
#endif
#endif
}

static void PrintStackTraceSignalHandler(void *) {
  sys::PrintStackTrace(llvm::errs());
}

void llvm::sys::DisableSystemDialogsOnCrash() {}

/// When an error signal (such as SIGABRT or SIGSEGV) is delivered to the
/// process, print a stack trace and then exit.
void llvm::sys::PrintStackTraceOnErrorSignal(StringRef Argv0,
                                             bool DisableCrashReporting) {
  ::Argv0 = Argv0;

  AddSignalHandler(PrintStackTraceSignalHandler, nullptr);

#if defined(__APPLE__) && ENABLE_CRASH_OVERRIDES
  // Environment variable to disable any kind of crash dialog.
  if (DisableCrashReporting || getenv("LLVM_DISABLE_CRASH_REPORT")) {
    mach_port_t self = mach_task_self();

    exception_mask_t mask = EXC_MASK_CRASH;

    kern_return_t ret = task_set_exception_ports(
        self, mask, MACH_PORT_NULL,
        EXCEPTION_STATE_IDENTITY | MACH_EXCEPTION_CODES, THREAD_STATE_NONE);
    (void)ret;
  }
#endif
}
