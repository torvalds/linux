//===-- PerfJITEventListener.cpp - Tell Linux's perf about JITted code ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines a JITEventListener object that tells perf about JITted
// functions, including source line information.
//
// Documentation for perf jit integration is available at:
// https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/tree/tools/perf/Documentation/jitdump-specification.txt
// https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/tree/tools/perf/Documentation/jit-interface.txt
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/Twine.h"
#include "llvm/Config/config.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/ExecutionEngine/JITEventListener.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Object/SymbolSize.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Errno.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Mutex.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/raw_ostream.h"
#include <mutex>

#include <sys/mman.h>  // mmap()
#include <time.h>      // clock_gettime(), time(), localtime_r() */
#include <unistd.h>    // for read(), close()

using namespace llvm;
using namespace llvm::object;
typedef DILineInfoSpecifier::FileLineInfoKind FileLineInfoKind;

namespace {

// language identifier (XXX: should we generate something better from debug
// info?)
#define JIT_LANG "llvm-IR"
#define LLVM_PERF_JIT_MAGIC                                                    \
  ((uint32_t)'J' << 24 | (uint32_t)'i' << 16 | (uint32_t)'T' << 8 |            \
   (uint32_t)'D')
#define LLVM_PERF_JIT_VERSION 1

// bit 0: set if the jitdump file is using an architecture-specific timestamp
// clock source
#define JITDUMP_FLAGS_ARCH_TIMESTAMP (1ULL << 0)

struct LLVMPerfJitHeader;

class PerfJITEventListener : public JITEventListener {
public:
  PerfJITEventListener();
  ~PerfJITEventListener() {
    if (MarkerAddr)
      CloseMarker();
  }

  void notifyObjectLoaded(ObjectKey K, const ObjectFile &Obj,
                          const RuntimeDyld::LoadedObjectInfo &L) override;
  void notifyFreeingObject(ObjectKey K) override;

private:
  bool InitDebuggingDir();
  bool OpenMarker();
  void CloseMarker();
  static bool FillMachine(LLVMPerfJitHeader &hdr);

  void NotifyCode(Expected<llvm::StringRef> &Symbol, uint64_t CodeAddr,
                  uint64_t CodeSize);
  void NotifyDebug(uint64_t CodeAddr, DILineInfoTable Lines);

  // cache lookups
  sys::Process::Pid Pid;

  // base directory for output data
  std::string JitPath;

  // output data stream, closed via Dumpstream
  int DumpFd = -1;

  // output data stream
  std::unique_ptr<raw_fd_ostream> Dumpstream;

  // prevent concurrent dumps from messing up the output file
  sys::Mutex Mutex;

  // perf mmap marker
  void *MarkerAddr = NULL;

  // perf support ready
  bool SuccessfullyInitialized = false;

  // identifier for functions, primarily to identify when moving them around
  uint64_t CodeGeneration = 1;
};

// The following are POD struct definitions from the perf jit specification

enum LLVMPerfJitRecordType {
  JIT_CODE_LOAD = 0,
  JIT_CODE_MOVE = 1, // not emitted, code isn't moved
  JIT_CODE_DEBUG_INFO = 2,
  JIT_CODE_CLOSE = 3,          // not emitted, unnecessary
  JIT_CODE_UNWINDING_INFO = 4, // not emitted

  JIT_CODE_MAX
};

struct LLVMPerfJitHeader {
  uint32_t Magic;     // characters "JiTD"
  uint32_t Version;   // header version
  uint32_t TotalSize; // total size of header
  uint32_t ElfMach;   // elf mach target
  uint32_t Pad1;      // reserved
  uint32_t Pid;
  uint64_t Timestamp; // timestamp
  uint64_t Flags;     // flags
};

// record prefix (mandatory in each record)
struct LLVMPerfJitRecordPrefix {
  uint32_t Id; // record type identifier
  uint32_t TotalSize;
  uint64_t Timestamp;
};

struct LLVMPerfJitRecordCodeLoad {
  LLVMPerfJitRecordPrefix Prefix;

  uint32_t Pid;
  uint32_t Tid;
  uint64_t Vma;
  uint64_t CodeAddr;
  uint64_t CodeSize;
  uint64_t CodeIndex;
};

struct LLVMPerfJitDebugEntry {
  uint64_t Addr;
  int Lineno;  // source line number starting at 1
  int Discrim; // column discriminator, 0 is default
  // followed by null terminated filename, \xff\0 if same as previous entry
};

struct LLVMPerfJitRecordDebugInfo {
  LLVMPerfJitRecordPrefix Prefix;

  uint64_t CodeAddr;
  uint64_t NrEntry;
  // followed by NrEntry LLVMPerfJitDebugEntry records
};

static inline uint64_t timespec_to_ns(const struct timespec *ts) {
  const uint64_t NanoSecPerSec = 1000000000;
  return ((uint64_t)ts->tv_sec * NanoSecPerSec) + ts->tv_nsec;
}

static inline uint64_t perf_get_timestamp(void) {
  struct timespec ts;
  int ret;

  ret = clock_gettime(CLOCK_MONOTONIC, &ts);
  if (ret)
    return 0;

  return timespec_to_ns(&ts);
}

PerfJITEventListener::PerfJITEventListener()
    : Pid(sys::Process::getProcessId()) {
  // check if clock-source is supported
  if (!perf_get_timestamp()) {
    errs() << "kernel does not support CLOCK_MONOTONIC\n";
    return;
  }

  if (!InitDebuggingDir()) {
    errs() << "could not initialize debugging directory\n";
    return;
  }

  std::string Filename;
  raw_string_ostream FilenameBuf(Filename);
  FilenameBuf << JitPath << "/jit-" << Pid << ".dump";

  // Need to open ourselves, because we need to hand the FD to OpenMarker() and
  // raw_fd_ostream doesn't expose the FD.
  using sys::fs::openFileForWrite;
  if (auto EC =
          openFileForReadWrite(FilenameBuf.str(), DumpFd,
			       sys::fs::CD_CreateNew, sys::fs::OF_None)) {
    errs() << "could not open JIT dump file " << FilenameBuf.str() << ": "
           << EC.message() << "\n";
    return;
  }

  Dumpstream = std::make_unique<raw_fd_ostream>(DumpFd, true);

  LLVMPerfJitHeader Header = {0, 0, 0, 0, 0, 0, 0, 0};
  if (!FillMachine(Header))
    return;

  // signal this process emits JIT information
  if (!OpenMarker())
    return;

  // emit dumpstream header
  Header.Magic = LLVM_PERF_JIT_MAGIC;
  Header.Version = LLVM_PERF_JIT_VERSION;
  Header.TotalSize = sizeof(Header);
  Header.Pid = Pid;
  Header.Timestamp = perf_get_timestamp();
  Dumpstream->write(reinterpret_cast<const char *>(&Header), sizeof(Header));

  // Everything initialized, can do profiling now.
  if (!Dumpstream->has_error())
    SuccessfullyInitialized = true;
}

void PerfJITEventListener::notifyObjectLoaded(
    ObjectKey K, const ObjectFile &Obj,
    const RuntimeDyld::LoadedObjectInfo &L) {

  if (!SuccessfullyInitialized)
    return;

  OwningBinary<ObjectFile> DebugObjOwner = L.getObjectForDebug(Obj);
  const ObjectFile &DebugObj = *DebugObjOwner.getBinary();

  // Get the address of the object image for use as a unique identifier
  std::unique_ptr<DIContext> Context = DWARFContext::create(DebugObj);

  // Use symbol info to iterate over functions in the object.
  for (const std::pair<SymbolRef, uint64_t> &P : computeSymbolSizes(DebugObj)) {
    SymbolRef Sym = P.first;
    std::string SourceFileName;

    Expected<SymbolRef::Type> SymTypeOrErr = Sym.getType();
    if (!SymTypeOrErr) {
      // There's not much we can with errors here
      consumeError(SymTypeOrErr.takeError());
      continue;
    }
    SymbolRef::Type SymType = *SymTypeOrErr;
    if (SymType != SymbolRef::ST_Function)
      continue;

    Expected<StringRef> Name = Sym.getName();
    if (!Name) {
      consumeError(Name.takeError());
      continue;
    }

    Expected<uint64_t> AddrOrErr = Sym.getAddress();
    if (!AddrOrErr) {
      consumeError(AddrOrErr.takeError());
      continue;
    }
    uint64_t Size = P.second;
    object::SectionedAddress Address;
    Address.Address = *AddrOrErr;

    uint64_t SectionIndex = object::SectionedAddress::UndefSection;
    if (auto SectOrErr = Sym.getSection())
        if (*SectOrErr != Obj.section_end())
            SectionIndex = SectOrErr.get()->getIndex();

    // According to spec debugging info has to come before loading the
    // corresponding code load.
    DILineInfoTable Lines = Context->getLineInfoForAddressRange(
        {*AddrOrErr, SectionIndex}, Size, FileLineInfoKind::AbsoluteFilePath);

    NotifyDebug(*AddrOrErr, Lines);
    NotifyCode(Name, *AddrOrErr, Size);
  }

  // avoid races with writes
  std::lock_guard<sys::Mutex> Guard(Mutex);

  Dumpstream->flush();
}

void PerfJITEventListener::notifyFreeingObject(ObjectKey K) {
  // perf currently doesn't have an interface for unloading. But munmap()ing the
  // code section does, so that's ok.
}

bool PerfJITEventListener::InitDebuggingDir() {
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
    errs() << "could not create jit cache directory " << Path << ": "
           << EC.message() << "\n";
    return false;
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
    errs() << "could not create unique jit cache directory " << UniqueDebugDir
           << ": " << EC.message() << "\n";
    return false;
  }

  JitPath = std::string(UniqueDebugDir.str());

  return true;
}

bool PerfJITEventListener::OpenMarker() {
  // We mmap the jitdump to create an MMAP RECORD in perf.data file.  The mmap
  // is captured either live (perf record running when we mmap) or in deferred
  // mode, via /proc/PID/maps. The MMAP record is used as a marker of a jitdump
  // file for more meta data info about the jitted code. Perf report/annotate
  // detect this special filename and process the jitdump file.
  //
  // Mapping must be PROT_EXEC to ensure it is captured by perf record
  // even when not using -d option.
  MarkerAddr = ::mmap(NULL, sys::Process::getPageSizeEstimate(),
                      PROT_READ | PROT_EXEC, MAP_PRIVATE, DumpFd, 0);

  if (MarkerAddr == MAP_FAILED) {
    errs() << "could not mmap JIT marker\n";
    return false;
  }
  return true;
}

void PerfJITEventListener::CloseMarker() {
  if (!MarkerAddr)
    return;

  munmap(MarkerAddr, sys::Process::getPageSizeEstimate());
  MarkerAddr = nullptr;
}

bool PerfJITEventListener::FillMachine(LLVMPerfJitHeader &hdr) {
  char id[16];
  struct {
    uint16_t e_type;
    uint16_t e_machine;
  } info;

  size_t RequiredMemory = sizeof(id) + sizeof(info);

  ErrorOr<std::unique_ptr<MemoryBuffer>> MB =
    MemoryBuffer::getFileSlice("/proc/self/exe",
			       RequiredMemory,
			       0);

  // This'll not guarantee that enough data was actually read from the
  // underlying file. Instead the trailing part of the buffer would be
  // zeroed. Given the ELF signature check below that seems ok though,
  // it's unlikely that the file ends just after that, and the
  // consequence would just be that perf wouldn't recognize the
  // signature.
  if (auto EC = MB.getError()) {
    errs() << "could not open /proc/self/exe: " << EC.message() << "\n";
    return false;
  }

  memcpy(&id, (*MB)->getBufferStart(), sizeof(id));
  memcpy(&info, (*MB)->getBufferStart() + sizeof(id), sizeof(info));

  // check ELF signature
  if (id[0] != 0x7f || id[1] != 'E' || id[2] != 'L' || id[3] != 'F') {
    errs() << "invalid elf signature\n";
    return false;
  }

  hdr.ElfMach = info.e_machine;

  return true;
}

void PerfJITEventListener::NotifyCode(Expected<llvm::StringRef> &Symbol,
                                      uint64_t CodeAddr, uint64_t CodeSize) {
  assert(SuccessfullyInitialized);

  // 0 length functions can't have samples.
  if (CodeSize == 0)
    return;

  LLVMPerfJitRecordCodeLoad rec;
  rec.Prefix.Id = JIT_CODE_LOAD;
  rec.Prefix.TotalSize = sizeof(rec) +        // debug record itself
                         Symbol->size() + 1 + // symbol name
                         CodeSize;            // and code
  rec.Prefix.Timestamp = perf_get_timestamp();

  rec.CodeSize = CodeSize;
  rec.Vma = CodeAddr;
  rec.CodeAddr = CodeAddr;
  rec.Pid = Pid;
  rec.Tid = get_threadid();

  // avoid interspersing output
  std::lock_guard<sys::Mutex> Guard(Mutex);

  rec.CodeIndex = CodeGeneration++; // under lock!

  Dumpstream->write(reinterpret_cast<const char *>(&rec), sizeof(rec));
  Dumpstream->write(Symbol->data(), Symbol->size() + 1);
  Dumpstream->write(reinterpret_cast<const char *>(CodeAddr), CodeSize);
}

void PerfJITEventListener::NotifyDebug(uint64_t CodeAddr,
                                       DILineInfoTable Lines) {
  assert(SuccessfullyInitialized);

  // Didn't get useful debug info.
  if (Lines.empty())
    return;

  LLVMPerfJitRecordDebugInfo rec;
  rec.Prefix.Id = JIT_CODE_DEBUG_INFO;
  rec.Prefix.TotalSize = sizeof(rec); // will be increased further
  rec.Prefix.Timestamp = perf_get_timestamp();
  rec.CodeAddr = CodeAddr;
  rec.NrEntry = Lines.size();

  // compute total size of record (variable due to filenames)
  DILineInfoTable::iterator Begin = Lines.begin();
  DILineInfoTable::iterator End = Lines.end();
  for (DILineInfoTable::iterator It = Begin; It != End; ++It) {
    DILineInfo &line = It->second;
    rec.Prefix.TotalSize += sizeof(LLVMPerfJitDebugEntry);
    rec.Prefix.TotalSize += line.FileName.size() + 1;
  }

  // The debug_entry describes the source line information. It is defined as
  // follows in order:
  // * uint64_t code_addr: address of function for which the debug information
  // is generated
  // * uint32_t line     : source file line number (starting at 1)
  // * uint32_t discrim  : column discriminator, 0 is default
  // * char name[n]      : source file name in ASCII, including null termination

  // avoid interspersing output
  std::lock_guard<sys::Mutex> Guard(Mutex);

  Dumpstream->write(reinterpret_cast<const char *>(&rec), sizeof(rec));

  for (DILineInfoTable::iterator It = Begin; It != End; ++It) {
    LLVMPerfJitDebugEntry LineInfo;
    DILineInfo &Line = It->second;

    LineInfo.Addr = It->first;
    // The function re-created by perf is preceded by a elf
    // header. Need to adjust for that, otherwise the results are
    // wrong.
    LineInfo.Addr += 0x40;
    LineInfo.Lineno = Line.Line;
    LineInfo.Discrim = Line.Discriminator;

    Dumpstream->write(reinterpret_cast<const char *>(&LineInfo),
                      sizeof(LineInfo));
    Dumpstream->write(Line.FileName.c_str(), Line.FileName.size() + 1);
  }
}

} // end anonymous namespace

namespace llvm {
JITEventListener *JITEventListener::createPerfJITEventListener() {
  // There should be only a single event listener per process, otherwise perf
  // gets confused.
  static PerfJITEventListener PerfListener;
  return &PerfListener;
}

} // namespace llvm

LLVMJITEventListenerRef LLVMCreatePerfJITEventListener(void)
{
  return wrap(JITEventListener::createPerfJITEventListener());
}
