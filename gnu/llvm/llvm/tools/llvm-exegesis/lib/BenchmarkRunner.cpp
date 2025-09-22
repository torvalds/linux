//===-- BenchmarkRunner.cpp -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <cmath>
#include <memory>
#include <string>

#include "Assembler.h"
#include "BenchmarkRunner.h"
#include "Error.h"
#include "MCInstrDescView.h"
#include "MmapUtils.h"
#include "PerfHelper.h"
#include "SubprocessMemory.h"
#include "Target.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/CrashRecoveryContext.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/SystemZ/zOSSupport.h"

#ifdef __linux__
#ifdef HAVE_LIBPFM
#include <perfmon/perf_event.h>
#endif
#include <sys/mman.h>
#include <sys/ptrace.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__GLIBC__) && __has_include(<sys/rseq.h>) && defined(HAVE_BUILTIN_THREAD_POINTER)
#include <sys/rseq.h>
#if defined(RSEQ_SIG) && defined(SYS_rseq)
#define GLIBC_INITS_RSEQ
#endif
#endif
#endif // __linux__

namespace llvm {
namespace exegesis {

BenchmarkRunner::BenchmarkRunner(const LLVMState &State, Benchmark::ModeE Mode,
                                 BenchmarkPhaseSelectorE BenchmarkPhaseSelector,
                                 ExecutionModeE ExecutionMode,
                                 ArrayRef<ValidationEvent> ValCounters)
    : State(State), Mode(Mode), BenchmarkPhaseSelector(BenchmarkPhaseSelector),
      ExecutionMode(ExecutionMode), ValidationCounters(ValCounters),
      Scratch(std::make_unique<ScratchSpace>()) {}

BenchmarkRunner::~BenchmarkRunner() = default;

void BenchmarkRunner::FunctionExecutor::accumulateCounterValues(
    const SmallVectorImpl<int64_t> &NewValues,
    SmallVectorImpl<int64_t> *Result) {
  const size_t NumValues = std::max(NewValues.size(), Result->size());
  if (NumValues > Result->size())
    Result->resize(NumValues, 0);
  for (size_t I = 0, End = NewValues.size(); I < End; ++I)
    (*Result)[I] += NewValues[I];
}

Expected<SmallVector<int64_t, 4>>
BenchmarkRunner::FunctionExecutor::runAndSample(
    const char *Counters, ArrayRef<const char *> ValidationCounters,
    SmallVectorImpl<int64_t> &ValidationCounterValues) const {
  // We sum counts when there are several counters for a single ProcRes
  // (e.g. P23 on SandyBridge).
  SmallVector<int64_t, 4> CounterValues;
  SmallVector<StringRef, 2> CounterNames;
  StringRef(Counters).split(CounterNames, '+');
  for (auto &CounterName : CounterNames) {
    CounterName = CounterName.trim();
    Expected<SmallVector<int64_t, 4>> ValueOrError = runWithCounter(
        CounterName, ValidationCounters, ValidationCounterValues);
    if (!ValueOrError)
      return ValueOrError.takeError();
    accumulateCounterValues(ValueOrError.get(), &CounterValues);
  }
  return CounterValues;
}

namespace {
class InProcessFunctionExecutorImpl : public BenchmarkRunner::FunctionExecutor {
public:
  static Expected<std::unique_ptr<InProcessFunctionExecutorImpl>>
  create(const LLVMState &State, object::OwningBinary<object::ObjectFile> Obj,
         BenchmarkRunner::ScratchSpace *Scratch) {
    Expected<ExecutableFunction> EF =
        ExecutableFunction::create(State.createTargetMachine(), std::move(Obj));

    if (!EF)
      return EF.takeError();

    return std::unique_ptr<InProcessFunctionExecutorImpl>(
        new InProcessFunctionExecutorImpl(State, std::move(*EF), Scratch));
  }

private:
  InProcessFunctionExecutorImpl(const LLVMState &State,
                                ExecutableFunction Function,
                                BenchmarkRunner::ScratchSpace *Scratch)
      : State(State), Function(std::move(Function)), Scratch(Scratch) {}

  static void accumulateCounterValues(const SmallVector<int64_t, 4> &NewValues,
                                      SmallVector<int64_t, 4> *Result) {
    const size_t NumValues = std::max(NewValues.size(), Result->size());
    if (NumValues > Result->size())
      Result->resize(NumValues, 0);
    for (size_t I = 0, End = NewValues.size(); I < End; ++I)
      (*Result)[I] += NewValues[I];
  }

  Expected<SmallVector<int64_t, 4>> runWithCounter(
      StringRef CounterName, ArrayRef<const char *> ValidationCounters,
      SmallVectorImpl<int64_t> &ValidationCounterValues) const override {
    const ExegesisTarget &ET = State.getExegesisTarget();
    char *const ScratchPtr = Scratch->ptr();
    auto CounterOrError =
        ET.createCounter(CounterName, State, ValidationCounters);

    if (!CounterOrError)
      return CounterOrError.takeError();

    pfm::CounterGroup *Counter = CounterOrError.get().get();
    Scratch->clear();
    {
      auto PS = ET.withSavedState();
      CrashRecoveryContext CRC;
      CrashRecoveryContext::Enable();
      const bool Crashed = !CRC.RunSafely([this, Counter, ScratchPtr]() {
        Counter->start();
        this->Function(ScratchPtr);
        Counter->stop();
      });
      CrashRecoveryContext::Disable();
      PS.reset();
      if (Crashed) {
#ifdef LLVM_ON_UNIX
        // See "Exit Status for Commands":
        // https://pubs.opengroup.org/onlinepubs/9699919799/xrat/V4_xcu_chap02.html
        constexpr const int kSigOffset = 128;
        return make_error<SnippetSignal>(CRC.RetCode - kSigOffset);
#else
        // The exit code of the process on windows is not meaningful as a
        // signal, so simply pass in -1 as the signal into the error.
        return make_error<SnippetSignal>(-1);
#endif // LLVM_ON_UNIX
      }
    }

    auto ValidationValuesOrErr = Counter->readValidationCountersOrError();
    if (!ValidationValuesOrErr)
      return ValidationValuesOrErr.takeError();

    ArrayRef RealValidationValues = *ValidationValuesOrErr;
    for (size_t I = 0; I < RealValidationValues.size(); ++I)
      ValidationCounterValues[I] = RealValidationValues[I];

    return Counter->readOrError(Function.getFunctionBytes());
  }

  const LLVMState &State;
  const ExecutableFunction Function;
  BenchmarkRunner::ScratchSpace *const Scratch;
};

#ifdef __linux__
// The following class implements a function executor that executes the
// benchmark code within a subprocess rather than within the main llvm-exegesis
// process. This allows for much more control over the execution context of the
// snippet, particularly with regard to memory. This class performs all the
// necessary functions to create the subprocess, execute the snippet in the
// subprocess, and report results/handle errors.
class SubProcessFunctionExecutorImpl
    : public BenchmarkRunner::FunctionExecutor {
public:
  static Expected<std::unique_ptr<SubProcessFunctionExecutorImpl>>
  create(const LLVMState &State, object::OwningBinary<object::ObjectFile> Obj,
         const BenchmarkKey &Key) {
    Expected<ExecutableFunction> EF =
        ExecutableFunction::create(State.createTargetMachine(), std::move(Obj));
    if (!EF)
      return EF.takeError();

    return std::unique_ptr<SubProcessFunctionExecutorImpl>(
        new SubProcessFunctionExecutorImpl(State, std::move(*EF), Key));
  }

private:
  SubProcessFunctionExecutorImpl(const LLVMState &State,
                                 ExecutableFunction Function,
                                 const BenchmarkKey &Key)
      : State(State), Function(std::move(Function)), Key(Key) {}

  enum ChildProcessExitCodeE {
    CounterFDReadFailed = 1,
    RSeqDisableFailed,
    FunctionDataMappingFailed,
    AuxiliaryMemorySetupFailed
  };

  StringRef childProcessExitCodeToString(int ExitCode) const {
    switch (ExitCode) {
    case ChildProcessExitCodeE::CounterFDReadFailed:
      return "Counter file descriptor read failed";
    case ChildProcessExitCodeE::RSeqDisableFailed:
      return "Disabling restartable sequences failed";
    case ChildProcessExitCodeE::FunctionDataMappingFailed:
      return "Failed to map memory for assembled snippet";
    case ChildProcessExitCodeE::AuxiliaryMemorySetupFailed:
      return "Failed to setup auxiliary memory";
    default:
      return "Child process returned with unknown exit code";
    }
  }

  Error sendFileDescriptorThroughSocket(int SocketFD, int FD) const {
    struct msghdr Message = {};
    char Buffer[CMSG_SPACE(sizeof(FD))];
    memset(Buffer, 0, sizeof(Buffer));
    Message.msg_control = Buffer;
    Message.msg_controllen = sizeof(Buffer);

    struct cmsghdr *ControlMessage = CMSG_FIRSTHDR(&Message);
    ControlMessage->cmsg_level = SOL_SOCKET;
    ControlMessage->cmsg_type = SCM_RIGHTS;
    ControlMessage->cmsg_len = CMSG_LEN(sizeof(FD));

    memcpy(CMSG_DATA(ControlMessage), &FD, sizeof(FD));

    Message.msg_controllen = CMSG_SPACE(sizeof(FD));

    ssize_t BytesWritten = sendmsg(SocketFD, &Message, 0);

    if (BytesWritten < 0)
      return make_error<Failure>("Failed to write FD to socket: " +
                                 Twine(strerror(errno)));

    return Error::success();
  }

  Expected<int> getFileDescriptorFromSocket(int SocketFD) const {
    struct msghdr Message = {};

    char ControlBuffer[256];
    Message.msg_control = ControlBuffer;
    Message.msg_controllen = sizeof(ControlBuffer);

    ssize_t BytesRead = recvmsg(SocketFD, &Message, 0);

    if (BytesRead < 0)
      return make_error<Failure>("Failed to read FD from socket: " +
                                 Twine(strerror(errno)));

    struct cmsghdr *ControlMessage = CMSG_FIRSTHDR(&Message);

    int FD;

    if (ControlMessage->cmsg_len != CMSG_LEN(sizeof(FD)))
      return make_error<Failure>("Failed to get correct number of bytes for "
                                 "file descriptor from socket.");

    memcpy(&FD, CMSG_DATA(ControlMessage), sizeof(FD));

    return FD;
  }

  Error
  runParentProcess(pid_t ChildPID, int WriteFD, StringRef CounterName,
                   SmallVectorImpl<int64_t> &CounterValues,
                   ArrayRef<const char *> ValidationCounters,
                   SmallVectorImpl<int64_t> &ValidationCounterValues) const {
    auto WriteFDClose = make_scope_exit([WriteFD]() { close(WriteFD); });
    const ExegesisTarget &ET = State.getExegesisTarget();
    auto CounterOrError =
        ET.createCounter(CounterName, State, ValidationCounters, ChildPID);

    if (!CounterOrError)
      return CounterOrError.takeError();

    pfm::CounterGroup *Counter = CounterOrError.get().get();

    // Make sure to attach to the process (and wait for the sigstop to be
    // delivered and for the process to continue) before we write to the counter
    // file descriptor. Attaching to the process before writing to the socket
    // ensures that the subprocess at most has blocked on the read call. If we
    // attach afterwards, the subprocess might exit before we get to the attach
    // call due to effects like scheduler contention, introducing transient
    // failures.
    if (ptrace(PTRACE_ATTACH, ChildPID, NULL, NULL) != 0)
      return make_error<Failure>("Failed to attach to the child process: " +
                                 Twine(strerror(errno)));

    if (waitpid(ChildPID, NULL, 0) == -1) {
      return make_error<Failure>(
          "Failed to wait for child process to stop after attaching: " +
          Twine(strerror(errno)));
    }

    if (ptrace(PTRACE_CONT, ChildPID, NULL, NULL) != 0)
      return make_error<Failure>(
          "Failed to continue execution of the child process: " +
          Twine(strerror(errno)));

    int CounterFileDescriptor = Counter->getFileDescriptor();
    Error SendError =
        sendFileDescriptorThroughSocket(WriteFD, CounterFileDescriptor);

    if (SendError)
      return SendError;

    int ChildStatus;
    if (waitpid(ChildPID, &ChildStatus, 0) == -1) {
      return make_error<Failure>(
          "Waiting for the child process to complete failed: " +
          Twine(strerror(errno)));
    }

    if (WIFEXITED(ChildStatus)) {
      int ChildExitCode = WEXITSTATUS(ChildStatus);
      if (ChildExitCode == 0) {
        // The child exited succesfully, read counter values and return
        // success.
        auto CounterValueOrErr = Counter->readOrError();
        if (!CounterValueOrErr)
          return CounterValueOrErr.takeError();
        CounterValues = std::move(*CounterValueOrErr);

        auto ValidationValuesOrErr = Counter->readValidationCountersOrError();
        if (!ValidationValuesOrErr)
          return ValidationValuesOrErr.takeError();

        ArrayRef RealValidationValues = *ValidationValuesOrErr;
        for (size_t I = 0; I < RealValidationValues.size(); ++I)
          ValidationCounterValues[I] = RealValidationValues[I];

        return Error::success();
      }
      // The child exited, but not successfully.
      return make_error<Failure>(
          "Child benchmarking process exited with non-zero exit code: " +
          childProcessExitCodeToString(ChildExitCode));
    }

    // An error was encountered running the snippet, process it
    siginfo_t ChildSignalInfo;
    if (ptrace(PTRACE_GETSIGINFO, ChildPID, NULL, &ChildSignalInfo) == -1) {
      return make_error<Failure>("Getting signal info from the child failed: " +
                                 Twine(strerror(errno)));
    }

    // Send SIGKILL rather than SIGTERM as the child process has no SIGTERM
    // handlers to run, and calling SIGTERM would mean that ptrace will force
    // it to block in the signal-delivery-stop for the SIGSEGV/other signals,
    // and upon exit.
    if (kill(ChildPID, SIGKILL) == -1)
      return make_error<Failure>("Failed to kill child benchmarking proces: " +
                                 Twine(strerror(errno)));

    // Wait for the process to exit so that there are no zombie processes left
    // around.
    if (waitpid(ChildPID, NULL, 0) == -1)
      return make_error<Failure>("Failed to wait for process to die: " +
                                 Twine(strerror(errno)));

    if (ChildSignalInfo.si_signo == SIGSEGV)
      return make_error<SnippetSegmentationFault>(
          reinterpret_cast<intptr_t>(ChildSignalInfo.si_addr));

    return make_error<SnippetSignal>(ChildSignalInfo.si_signo);
  }

  Error createSubProcessAndRunBenchmark(
      StringRef CounterName, SmallVectorImpl<int64_t> &CounterValues,
      ArrayRef<const char *> ValidationCounters,
      SmallVectorImpl<int64_t> &ValidationCounterValues) const {
    int PipeFiles[2];
    int PipeSuccessOrErr = socketpair(AF_UNIX, SOCK_DGRAM, 0, PipeFiles);
    if (PipeSuccessOrErr != 0) {
      return make_error<Failure>(
          "Failed to create a pipe for interprocess communication between "
          "llvm-exegesis and the benchmarking subprocess: " +
          Twine(strerror(errno)));
    }

    SubprocessMemory SPMemory;
    Error MemoryInitError = SPMemory.initializeSubprocessMemory(getpid());
    if (MemoryInitError)
      return MemoryInitError;

    Error AddMemDefError =
        SPMemory.addMemoryDefinition(Key.MemoryValues, getpid());
    if (AddMemDefError)
      return AddMemDefError;

    long ParentTID = SubprocessMemory::getCurrentTID();
    pid_t ParentOrChildPID = fork();

    if (ParentOrChildPID == -1) {
      return make_error<Failure>("Failed to create child process: " +
                                 Twine(strerror(errno)));
    }

    if (ParentOrChildPID == 0) {
      // We are in the child process, close the write end of the pipe.
      close(PipeFiles[1]);
      // Unregister handlers, signal handling is now handled through ptrace in
      // the host process.
      sys::unregisterHandlers();
      runChildSubprocess(PipeFiles[0], Key, ParentTID);
      // The child process terminates in the above function, so we should never
      // get to this point.
      llvm_unreachable("Child process didn't exit when expected.");
    }

    // Close the read end of the pipe as we only need to write to the subprocess
    // from the parent process.
    close(PipeFiles[0]);
    return runParentProcess(ParentOrChildPID, PipeFiles[1], CounterName,
                            CounterValues, ValidationCounters,
                            ValidationCounterValues);
  }

  void disableCoreDumps() const {
    struct rlimit rlim;

    rlim.rlim_cur = 0;
    setrlimit(RLIMIT_CORE, &rlim);
  }

  [[noreturn]] void runChildSubprocess(int Pipe, const BenchmarkKey &Key,
                                       long ParentTID) const {
    // Disable core dumps in the child process as otherwise everytime we
    // encounter an execution failure like a segmentation fault, we will create
    // a core dump. We report the information directly rather than require the
    // user inspect a core dump.
    disableCoreDumps();

    // The following occurs within the benchmarking subprocess.
    pid_t ParentPID = getppid();

    Expected<int> CounterFileDescriptorOrError =
        getFileDescriptorFromSocket(Pipe);

    if (!CounterFileDescriptorOrError)
      exit(ChildProcessExitCodeE::CounterFDReadFailed);

    int CounterFileDescriptor = *CounterFileDescriptorOrError;

// Glibc versions greater than 2.35 automatically call rseq during
// initialization. Unmapping the region that glibc sets up for this causes
// segfaults in the program. Unregister the rseq region so that we can safely
// unmap it later
#ifdef GLIBC_INITS_RSEQ
    unsigned int RseqStructSize = __rseq_size;

    // Glibc v2.40 (the change is also expected to be backported to v2.35)
    // changes the definition of __rseq_size to be the usable area of the struct
    // rather than the actual size of the struct. v2.35 uses only 20 bytes of
    // the 32 byte struct. For now, it should be safe to assume that if the
    // usable size is less than 32, the actual size of the struct will be 32
    // bytes given alignment requirements.
    if (__rseq_size < 32)
      RseqStructSize = 32;

    long RseqDisableOutput =
        syscall(SYS_rseq, (intptr_t)__builtin_thread_pointer() + __rseq_offset,
                RseqStructSize, RSEQ_FLAG_UNREGISTER, RSEQ_SIG);
    if (RseqDisableOutput != 0)
      exit(ChildProcessExitCodeE::RSeqDisableFailed);
#endif // GLIBC_INITS_RSEQ

    // The frontend that generates the memory annotation structures should
    // validate that the address to map the snippet in at is a multiple of
    // the page size. Assert that this is true here.
    assert(Key.SnippetAddress % getpagesize() == 0 &&
           "The snippet address needs to be aligned to a page boundary.");

    size_t FunctionDataCopySize = this->Function.FunctionBytes.size();
    void *MapAddress = NULL;
    int MapFlags = MAP_PRIVATE | MAP_ANONYMOUS;

    if (Key.SnippetAddress != 0) {
      MapAddress = reinterpret_cast<void *>(Key.SnippetAddress);
      MapFlags |= MAP_FIXED_NOREPLACE;
    }

    char *FunctionDataCopy =
        (char *)mmap(MapAddress, FunctionDataCopySize, PROT_READ | PROT_WRITE,
                     MapFlags, 0, 0);
    if ((intptr_t)FunctionDataCopy == -1)
      exit(ChildProcessExitCodeE::FunctionDataMappingFailed);

    memcpy(FunctionDataCopy, this->Function.FunctionBytes.data(),
           this->Function.FunctionBytes.size());
    mprotect(FunctionDataCopy, FunctionDataCopySize, PROT_READ | PROT_EXEC);

    Expected<int> AuxMemFDOrError =
        SubprocessMemory::setupAuxiliaryMemoryInSubprocess(
            Key.MemoryValues, ParentPID, ParentTID, CounterFileDescriptor);
    if (!AuxMemFDOrError)
      exit(ChildProcessExitCodeE::AuxiliaryMemorySetupFailed);

    ((void (*)(size_t, int))(intptr_t)FunctionDataCopy)(FunctionDataCopySize,
                                                        *AuxMemFDOrError);

    exit(0);
  }

  Expected<SmallVector<int64_t, 4>> runWithCounter(
      StringRef CounterName, ArrayRef<const char *> ValidationCounters,
      SmallVectorImpl<int64_t> &ValidationCounterValues) const override {
    SmallVector<int64_t, 4> Value(1, 0);
    Error PossibleBenchmarkError = createSubProcessAndRunBenchmark(
        CounterName, Value, ValidationCounters, ValidationCounterValues);

    if (PossibleBenchmarkError)
      return std::move(PossibleBenchmarkError);

    return Value;
  }

  const LLVMState &State;
  const ExecutableFunction Function;
  const BenchmarkKey &Key;
};
#endif // __linux__
} // namespace

Expected<SmallString<0>> BenchmarkRunner::assembleSnippet(
    const BenchmarkCode &BC, const SnippetRepetitor &Repetitor,
    unsigned MinInstructions, unsigned LoopBodySize,
    bool GenerateMemoryInstructions) const {
  const std::vector<MCInst> &Instructions = BC.Key.Instructions;
  SmallString<0> Buffer;
  raw_svector_ostream OS(Buffer);
  if (Error E = assembleToStream(
          State.getExegesisTarget(), State.createTargetMachine(), BC.LiveIns,
          Repetitor.Repeat(Instructions, MinInstructions, LoopBodySize,
                           GenerateMemoryInstructions),
          OS, BC.Key, GenerateMemoryInstructions)) {
    return std::move(E);
  }
  return Buffer;
}

Expected<BenchmarkRunner::RunnableConfiguration>
BenchmarkRunner::getRunnableConfiguration(
    const BenchmarkCode &BC, unsigned MinInstructions, unsigned LoopBodySize,
    const SnippetRepetitor &Repetitor) const {
  RunnableConfiguration RC;

  Benchmark &BenchmarkResult = RC.BenchmarkResult;
  BenchmarkResult.Mode = Mode;
  BenchmarkResult.CpuName =
      std::string(State.getTargetMachine().getTargetCPU());
  BenchmarkResult.LLVMTriple =
      State.getTargetMachine().getTargetTriple().normalize();
  BenchmarkResult.MinInstructions = MinInstructions;
  BenchmarkResult.Info = BC.Info;

  const std::vector<MCInst> &Instructions = BC.Key.Instructions;

  bool GenerateMemoryInstructions = ExecutionMode == ExecutionModeE::SubProcess;

  BenchmarkResult.Key = BC.Key;

  // Assemble at least kMinInstructionsForSnippet instructions by repeating
  // the snippet for debug/analysis. This is so that the user clearly
  // understands that the inside instructions are repeated.
  if (BenchmarkPhaseSelector > BenchmarkPhaseSelectorE::PrepareSnippet) {
    const int MinInstructionsForSnippet = 4 * Instructions.size();
    const int LoopBodySizeForSnippet = 2 * Instructions.size();
    auto Snippet =
        assembleSnippet(BC, Repetitor, MinInstructionsForSnippet,
                        LoopBodySizeForSnippet, GenerateMemoryInstructions);
    if (Error E = Snippet.takeError())
      return std::move(E);

    if (auto Err = getBenchmarkFunctionBytes(*Snippet,
                                             BenchmarkResult.AssembledSnippet))
      return std::move(Err);
  }

  // Assemble enough repetitions of the snippet so we have at least
  // MinInstructions instructions.
  if (BenchmarkPhaseSelector >
      BenchmarkPhaseSelectorE::PrepareAndAssembleSnippet) {
    auto Snippet =
        assembleSnippet(BC, Repetitor, BenchmarkResult.MinInstructions,
                        LoopBodySize, GenerateMemoryInstructions);
    if (Error E = Snippet.takeError())
      return std::move(E);
    RC.ObjectFile = getObjectFromBuffer(*Snippet);
  }

  return std::move(RC);
}

Expected<std::unique_ptr<BenchmarkRunner::FunctionExecutor>>
BenchmarkRunner::createFunctionExecutor(
    object::OwningBinary<object::ObjectFile> ObjectFile,
    const BenchmarkKey &Key) const {
  switch (ExecutionMode) {
  case ExecutionModeE::InProcess: {
    auto InProcessExecutorOrErr = InProcessFunctionExecutorImpl::create(
        State, std::move(ObjectFile), Scratch.get());
    if (!InProcessExecutorOrErr)
      return InProcessExecutorOrErr.takeError();

    return std::move(*InProcessExecutorOrErr);
  }
  case ExecutionModeE::SubProcess: {
#ifdef __linux__
    auto SubProcessExecutorOrErr = SubProcessFunctionExecutorImpl::create(
        State, std::move(ObjectFile), Key);
    if (!SubProcessExecutorOrErr)
      return SubProcessExecutorOrErr.takeError();

    return std::move(*SubProcessExecutorOrErr);
#else
    return make_error<Failure>(
        "The subprocess execution mode is only supported on Linux");
#endif
  }
  }
  llvm_unreachable("ExecutionMode is outside expected range");
}

std::pair<Error, Benchmark> BenchmarkRunner::runConfiguration(
    RunnableConfiguration &&RC,
    const std::optional<StringRef> &DumpFile) const {
  Benchmark &BenchmarkResult = RC.BenchmarkResult;
  object::OwningBinary<object::ObjectFile> &ObjectFile = RC.ObjectFile;

  if (DumpFile && BenchmarkPhaseSelector >
                      BenchmarkPhaseSelectorE::PrepareAndAssembleSnippet) {
    auto ObjectFilePath =
        writeObjectFile(ObjectFile.getBinary()->getData(), *DumpFile);
    if (Error E = ObjectFilePath.takeError()) {
      return {std::move(E), std::move(BenchmarkResult)};
    }
    outs() << "Check generated assembly with: /usr/bin/objdump -d "
           << *ObjectFilePath << "\n";
  }

  if (BenchmarkPhaseSelector < BenchmarkPhaseSelectorE::Measure) {
    BenchmarkResult.Error = "actual measurements skipped.";
    return {Error::success(), std::move(BenchmarkResult)};
  }

  Expected<std::unique_ptr<BenchmarkRunner::FunctionExecutor>> Executor =
      createFunctionExecutor(std::move(ObjectFile), RC.BenchmarkResult.Key);
  if (!Executor)
    return {Executor.takeError(), std::move(BenchmarkResult)};
  auto NewMeasurements = runMeasurements(**Executor);

  if (Error E = NewMeasurements.takeError()) {
    return {std::move(E), std::move(BenchmarkResult)};
  }
  assert(BenchmarkResult.MinInstructions > 0 && "invalid MinInstructions");
  for (BenchmarkMeasure &BM : *NewMeasurements) {
    // Scale the measurements by the number of instructions.
    BM.PerInstructionValue /= BenchmarkResult.MinInstructions;
    // Scale the measurements by the number of times the entire snippet is
    // repeated.
    BM.PerSnippetValue /=
        std::ceil(BenchmarkResult.MinInstructions /
                  static_cast<double>(BenchmarkResult.Key.Instructions.size()));
  }
  BenchmarkResult.Measurements = std::move(*NewMeasurements);

  return {Error::success(), std::move(BenchmarkResult)};
}

Expected<std::string>
BenchmarkRunner::writeObjectFile(StringRef Buffer, StringRef FileName) const {
  int ResultFD = 0;
  SmallString<256> ResultPath = FileName;
  if (Error E = errorCodeToError(
          FileName.empty() ? sys::fs::createTemporaryFile("snippet", "o",
                                                          ResultFD, ResultPath)
                           : sys::fs::openFileForReadWrite(
                                 FileName, ResultFD, sys::fs::CD_CreateAlways,
                                 sys::fs::OF_None)))
    return std::move(E);
  raw_fd_ostream OFS(ResultFD, true /*ShouldClose*/);
  OFS.write(Buffer.data(), Buffer.size());
  OFS.flush();
  return std::string(ResultPath);
}

static bool EventLessThan(const std::pair<ValidationEvent, const char *> LHS,
                          const ValidationEvent RHS) {
  return static_cast<int>(LHS.first) < static_cast<int>(RHS);
}

Error BenchmarkRunner::getValidationCountersToRun(
    SmallVector<const char *> &ValCountersToRun) const {
  const PfmCountersInfo &PCI = State.getPfmCounters();
  ValCountersToRun.reserve(ValidationCounters.size());

  ValCountersToRun.reserve(ValidationCounters.size());
  ArrayRef TargetValidationEvents(PCI.ValidationEvents,
                                  PCI.NumValidationEvents);
  for (const ValidationEvent RequestedValEvent : ValidationCounters) {
    auto ValCounterIt =
        lower_bound(TargetValidationEvents, RequestedValEvent, EventLessThan);
    if (ValCounterIt == TargetValidationEvents.end() ||
        ValCounterIt->first != RequestedValEvent)
      return make_error<Failure>("Cannot create validation counter");

    assert(ValCounterIt->first == RequestedValEvent &&
           "The array of validation events from the target should be sorted");
    ValCountersToRun.push_back(ValCounterIt->second);
  }

  return Error::success();
}

BenchmarkRunner::FunctionExecutor::~FunctionExecutor() {}

} // namespace exegesis
} // namespace llvm
