//===------- JITLoaderPerf.cpp - Register profiler objects ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Register objects for access by profilers via the perf JIT interface.
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/TargetProcess/JITLoaderPerf.h"

#include "llvm/ExecutionEngine/Orc/Shared/PerfSharedStructs.h"

#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Threading.h"

#include <mutex>
#include <optional>

#ifdef __linux__

#include <sys/mman.h> // mmap()
#include <time.h>     // clock_gettime(), time(), localtime_r() */
#include <unistd.h>   // for read(), close()

#define DEBUG_TYPE "orc"

// language identifier (XXX: should we generate something better from debug
// info?)
#define JIT_LANG "llvm-IR"
#define LLVM_PERF_JIT_MAGIC                                                    \
  ((uint32_t)'J' << 24 | (uint32_t)'i' << 16 | (uint32_t)'T' << 8 |            \
   (uint32_t)'D')
#define LLVM_PERF_JIT_VERSION 1

using namespace llvm;
using namespace llvm::orc;

struct PerfState {
  // cache lookups
  uint32_t Pid;

  // base directory for output data
  std::string JitPath;

  // output data stream, closed via Dumpstream
  int DumpFd = -1;

  // output data stream
  std::unique_ptr<raw_fd_ostream> Dumpstream;

  // perf mmap marker
  void *MarkerAddr = NULL;
};

// prevent concurrent dumps from messing up the output file
static std::mutex Mutex;
static std::optional<PerfState> State;

struct RecHeader {
  uint32_t Id;
  uint32_t TotalSize;
  uint64_t Timestamp;
};

struct DIR {
  RecHeader Prefix;
  uint64_t CodeAddr;
  uint64_t NrEntry;
};

struct DIE {
  uint64_t CodeAddr;
  uint32_t Line;
  uint32_t Discrim;
};

struct CLR {
  RecHeader Prefix;
  uint32_t Pid;
  uint32_t Tid;
  uint64_t Vma;
  uint64_t CodeAddr;
  uint64_t CodeSize;
  uint64_t CodeIndex;
};

struct UWR {
  RecHeader Prefix;
  uint64_t UnwindDataSize;
  uint64_t EhFrameHeaderSize;
  uint64_t MappedSize;
};

static inline uint64_t timespec_to_ns(const struct timespec *TS) {
  const uint64_t NanoSecPerSec = 1000000000;
  return ((uint64_t)TS->tv_sec * NanoSecPerSec) + TS->tv_nsec;
}

static inline uint64_t perf_get_timestamp() {
  timespec TS;
  if (clock_gettime(CLOCK_MONOTONIC, &TS))
    return 0;

  return timespec_to_ns(&TS);
}

static void writeDebugRecord(const PerfJITDebugInfoRecord &DebugRecord) {
  assert(State && "PerfState not initialized");
  LLVM_DEBUG(dbgs() << "Writing debug record with "
                    << DebugRecord.Entries.size() << " entries\n");
  [[maybe_unused]] size_t Written = 0;
  DIR Dir{RecHeader{static_cast<uint32_t>(DebugRecord.Prefix.Id),
                    DebugRecord.Prefix.TotalSize, perf_get_timestamp()},
          DebugRecord.CodeAddr, DebugRecord.Entries.size()};
  State->Dumpstream->write(reinterpret_cast<const char *>(&Dir), sizeof(Dir));
  Written += sizeof(Dir);
  for (auto &Die : DebugRecord.Entries) {
    DIE d{Die.Addr, Die.Lineno, Die.Discrim};
    State->Dumpstream->write(reinterpret_cast<const char *>(&d), sizeof(d));
    State->Dumpstream->write(Die.Name.data(), Die.Name.size() + 1);
    Written += sizeof(d) + Die.Name.size() + 1;
  }
  LLVM_DEBUG(dbgs() << "wrote " << Written << " bytes of debug info\n");
}

static void writeCodeRecord(const PerfJITCodeLoadRecord &CodeRecord) {
  assert(State && "PerfState not initialized");
  uint32_t Tid = get_threadid();
  LLVM_DEBUG(dbgs() << "Writing code record with code size "
                    << CodeRecord.CodeSize << " and code index "
                    << CodeRecord.CodeIndex << "\n");
  CLR Clr{RecHeader{static_cast<uint32_t>(CodeRecord.Prefix.Id),
                    CodeRecord.Prefix.TotalSize, perf_get_timestamp()},
          State->Pid,
          Tid,
          CodeRecord.Vma,
          CodeRecord.CodeAddr,
          CodeRecord.CodeSize,
          CodeRecord.CodeIndex};
  LLVM_DEBUG(dbgs() << "wrote " << sizeof(Clr) << " bytes of CLR, "
                    << CodeRecord.Name.size() + 1 << " bytes of name, "
                    << CodeRecord.CodeSize << " bytes of code\n");
  State->Dumpstream->write(reinterpret_cast<const char *>(&Clr), sizeof(Clr));
  State->Dumpstream->write(CodeRecord.Name.data(), CodeRecord.Name.size() + 1);
  State->Dumpstream->write((const char *)CodeRecord.CodeAddr,
                           CodeRecord.CodeSize);
}

static void
writeUnwindRecord(const PerfJITCodeUnwindingInfoRecord &UnwindRecord) {
  assert(State && "PerfState not initialized");
  dbgs() << "Writing unwind record with unwind data size "
         << UnwindRecord.UnwindDataSize << " and EH frame header size "
         << UnwindRecord.EHFrameHdrSize << " and mapped size "
         << UnwindRecord.MappedSize << "\n";
  UWR Uwr{RecHeader{static_cast<uint32_t>(UnwindRecord.Prefix.Id),
                    UnwindRecord.Prefix.TotalSize, perf_get_timestamp()},
          UnwindRecord.UnwindDataSize, UnwindRecord.EHFrameHdrSize,
          UnwindRecord.MappedSize};
  LLVM_DEBUG(dbgs() << "wrote " << sizeof(Uwr) << " bytes of UWR, "
                    << UnwindRecord.EHFrameHdrSize
                    << " bytes of EH frame header, "
                    << UnwindRecord.UnwindDataSize - UnwindRecord.EHFrameHdrSize
                    << " bytes of EH frame\n");
  State->Dumpstream->write(reinterpret_cast<const char *>(&Uwr), sizeof(Uwr));
  if (UnwindRecord.EHFrameHdrAddr)
    State->Dumpstream->write((const char *)UnwindRecord.EHFrameHdrAddr,
                             UnwindRecord.EHFrameHdrSize);
  else
    State->Dumpstream->write(UnwindRecord.EHFrameHdr.data(),
                             UnwindRecord.EHFrameHdrSize);
  State->Dumpstream->write((const char *)UnwindRecord.EHFrameAddr,
                           UnwindRecord.UnwindDataSize -
                               UnwindRecord.EHFrameHdrSize);
}

static Error registerJITLoaderPerfImpl(const PerfJITRecordBatch &Batch) {
  if (!State)
    return make_error<StringError>("PerfState not initialized",
                                   inconvertibleErrorCode());

  // Serialize the batch
  std::lock_guard<std::mutex> Lock(Mutex);
  if (Batch.UnwindingRecord.Prefix.TotalSize > 0)
    writeUnwindRecord(Batch.UnwindingRecord);

  for (const auto &DebugInfo : Batch.DebugInfoRecords)
    writeDebugRecord(DebugInfo);

  for (const auto &CodeLoad : Batch.CodeLoadRecords)
    writeCodeRecord(CodeLoad);

  State->Dumpstream->flush();

  return Error::success();
}

struct Header {
  uint32_t Magic;     // characters "JiTD"
  uint32_t Version;   // header version
  uint32_t TotalSize; // total size of header
  uint32_t ElfMach;   // elf mach target
  uint32_t Pad1;      // reserved
  uint32_t Pid;
  uint64_t Timestamp; // timestamp
  uint64_t Flags;     // flags
};

static Error OpenMarker(PerfState &State) {
  // We mmap the jitdump to create an MMAP RECORD in perf.data file.  The mmap
  // is captured either live (perf record running when we mmap) or in deferred
  // mode, via /proc/PID/maps. The MMAP record is used as a marker of a jitdump
  // file for more meta data info about the jitted code. Perf report/annotate
  // detect this special filename and process the jitdump file.
  //
  // Mapping must be PROT_EXEC to ensure it is captured by perf record
  // even when not using -d option.
  State.MarkerAddr =
      ::mmap(NULL, sys::Process::getPageSizeEstimate(), PROT_READ | PROT_EXEC,
             MAP_PRIVATE, State.DumpFd, 0);

  if (State.MarkerAddr == MAP_FAILED)
    return make_error<llvm::StringError>("could not mmap JIT marker",
                                         inconvertibleErrorCode());

  return Error::success();
}

void CloseMarker(PerfState &State) {
  if (!State.MarkerAddr)
    return;

  munmap(State.MarkerAddr, sys::Process::getPageSizeEstimate());
  State.MarkerAddr = nullptr;
}

static Expected<Header> FillMachine(PerfState &State) {
  Header Hdr;
  Hdr.Magic = LLVM_PERF_JIT_MAGIC;
  Hdr.Version = LLVM_PERF_JIT_VERSION;
  Hdr.TotalSize = sizeof(Hdr);
  Hdr.Pid = State.Pid;
  Hdr.Timestamp = perf_get_timestamp();

  char Id[16];
  struct {
    uint16_t e_type;
    uint16_t e_machine;
  } Info;

  size_t RequiredMemory = sizeof(Id) + sizeof(Info);

  ErrorOr<std::unique_ptr<MemoryBuffer>> MB =
      MemoryBuffer::getFileSlice("/proc/self/exe", RequiredMemory, 0);

  // This'll not guarantee that enough data was actually read from the
  // underlying file. Instead the trailing part of the buffer would be
  // zeroed. Given the ELF signature check below that seems ok though,
  // it's unlikely that the file ends just after that, and the
  // consequence would just be that perf wouldn't recognize the
  // signature.
  if (!MB)
    return make_error<llvm::StringError>("could not open /proc/self/exe",
                                         MB.getError());

  memcpy(&Id, (*MB)->getBufferStart(), sizeof(Id));
  memcpy(&Info, (*MB)->getBufferStart() + sizeof(Id), sizeof(Info));

  // check ELF signature
  if (Id[0] != 0x7f || Id[1] != 'E' || Id[2] != 'L' || Id[3] != 'F')
    return make_error<llvm::StringError>("invalid ELF signature",
                                         inconvertibleErrorCode());

  Hdr.ElfMach = Info.e_machine;

  return Hdr;
}

static Error InitDebuggingDir(PerfState &State) {
  time_t Time;
  struct tm LocalTime;
  char TimeBuffer[sizeof("YYYYMMDD")];
  SmallString<64> Path;

  // search for location to dump data to
  if (const char *BaseDir = getenv("JITDUMPDIR"))
    Path.append(BaseDir);
  else if (!sys::path::home_directory(Path))
    Path = ".";

  // create debug directory
  Path += "/.debug/jit/";
  if (auto EC = sys::fs::create_directories(Path)) {
    std::string ErrStr;
    raw_string_ostream ErrStream(ErrStr);
    ErrStream << "could not create jit cache directory " << Path << ": "
              << EC.message() << "\n";
    return make_error<StringError>(std::move(ErrStr), inconvertibleErrorCode());
  }

  // create unique directory for dump data related to this process
  time(&Time);
  localtime_r(&Time, &LocalTime);
  strftime(TimeBuffer, sizeof(TimeBuffer), "%Y%m%d", &LocalTime);
  Path += JIT_LANG "-jit-";
  Path += TimeBuffer;

  SmallString<128> UniqueDebugDir;

  using sys::fs::createUniqueDirectory;
  if (auto EC = createUniqueDirectory(Path, UniqueDebugDir)) {
    std::string ErrStr;
    raw_string_ostream ErrStream(ErrStr);
    ErrStream << "could not create unique jit cache directory "
              << UniqueDebugDir << ": " << EC.message() << "\n";
    return make_error<StringError>(std::move(ErrStr), inconvertibleErrorCode());
  }

  State.JitPath = std::string(UniqueDebugDir);

  return Error::success();
}

static Error registerJITLoaderPerfStartImpl() {
  PerfState Tentative;
  Tentative.Pid = sys::Process::getProcessId();
  // check if clock-source is supported
  if (!perf_get_timestamp())
    return make_error<StringError>("kernel does not support CLOCK_MONOTONIC",
                                   inconvertibleErrorCode());

  if (auto Err = InitDebuggingDir(Tentative))
    return Err;

  std::string Filename;
  raw_string_ostream FilenameBuf(Filename);
  FilenameBuf << Tentative.JitPath << "/jit-" << Tentative.Pid << ".dump";

  // Need to open ourselves, because we need to hand the FD to OpenMarker() and
  // raw_fd_ostream doesn't expose the FD.
  using sys::fs::openFileForWrite;
  if (auto EC = openFileForReadWrite(FilenameBuf.str(), Tentative.DumpFd,
                                     sys::fs::CD_CreateNew, sys::fs::OF_None)) {
    std::string ErrStr;
    raw_string_ostream ErrStream(ErrStr);
    ErrStream << "could not open JIT dump file " << FilenameBuf.str() << ": "
              << EC.message() << "\n";
    return make_error<StringError>(std::move(ErrStr), inconvertibleErrorCode());
  }

  Tentative.Dumpstream =
      std::make_unique<raw_fd_ostream>(Tentative.DumpFd, true);

  auto Header = FillMachine(Tentative);
  if (!Header)
    return Header.takeError();

  // signal this process emits JIT information
  if (auto Err = OpenMarker(Tentative))
    return Err;

  Tentative.Dumpstream->write(reinterpret_cast<const char *>(&Header.get()),
                              sizeof(*Header));

  // Everything initialized, can do profiling now.
  if (Tentative.Dumpstream->has_error())
    return make_error<StringError>("could not write JIT dump header",
                                   inconvertibleErrorCode());

  State = std::move(Tentative);
  return Error::success();
}

static Error registerJITLoaderPerfEndImpl() {
  if (!State)
    return make_error<StringError>("PerfState not initialized",
                                   inconvertibleErrorCode());

  RecHeader Close;
  Close.Id = static_cast<uint32_t>(PerfJITRecordType::JIT_CODE_CLOSE);
  Close.TotalSize = sizeof(Close);
  Close.Timestamp = perf_get_timestamp();
  State->Dumpstream->write(reinterpret_cast<const char *>(&Close),
                           sizeof(Close));
  if (State->MarkerAddr)
    CloseMarker(*State);

  State.reset();
  return Error::success();
}

extern "C" llvm::orc::shared::CWrapperFunctionResult
llvm_orc_registerJITLoaderPerfImpl(const char *Data, uint64_t Size) {
  using namespace orc::shared;
  return WrapperFunction<SPSError(SPSPerfJITRecordBatch)>::handle(
             Data, Size, registerJITLoaderPerfImpl)
      .release();
}

extern "C" llvm::orc::shared::CWrapperFunctionResult
llvm_orc_registerJITLoaderPerfStart(const char *Data, uint64_t Size) {
  using namespace orc::shared;
  return WrapperFunction<SPSError()>::handle(Data, Size,
                                             registerJITLoaderPerfStartImpl)
      .release();
}

extern "C" llvm::orc::shared::CWrapperFunctionResult
llvm_orc_registerJITLoaderPerfEnd(const char *Data, uint64_t Size) {
  using namespace orc::shared;
  return WrapperFunction<SPSError()>::handle(Data, Size,
                                             registerJITLoaderPerfEndImpl)
      .release();
}

#else

using namespace llvm;
using namespace llvm::orc;

static Error badOS() {
  using namespace llvm;
  return llvm::make_error<StringError>(
      "unsupported OS (perf support is only available on linux!)",
      inconvertibleErrorCode());
}

static Error badOSBatch(PerfJITRecordBatch &Batch) { return badOS(); }

extern "C" llvm::orc::shared::CWrapperFunctionResult
llvm_orc_registerJITLoaderPerfImpl(const char *Data, uint64_t Size) {
  using namespace shared;
  return WrapperFunction<SPSError(SPSPerfJITRecordBatch)>::handle(Data, Size,
                                                                  badOSBatch)
      .release();
}

extern "C" llvm::orc::shared::CWrapperFunctionResult
llvm_orc_registerJITLoaderPerfStart(const char *Data, uint64_t Size) {
  using namespace shared;
  return WrapperFunction<SPSError()>::handle(Data, Size, badOS).release();
}

extern "C" llvm::orc::shared::CWrapperFunctionResult
llvm_orc_registerJITLoaderPerfEnd(const char *Data, uint64_t Size) {
  using namespace shared;
  return WrapperFunction<SPSError()>::handle(Data, Size, badOS).release();
}

#endif
