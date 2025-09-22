//===- Minidump.h - Minidump constants and structures -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header constants and data structures pertaining to the Windows Minidump
// core file format.
//
// Reference:
// https://msdn.microsoft.com/en-us/library/windows/desktop/ms679293(v=vs.85).aspx
// https://chromium.googlesource.com/breakpad/breakpad/
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_BINARYFORMAT_MINIDUMP_H
#define LLVM_BINARYFORMAT_MINIDUMP_H

#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/Support/Endian.h"

namespace llvm {
namespace minidump {

LLVM_ENABLE_BITMASK_ENUMS_IN_NAMESPACE();

/// The minidump header is the first part of a minidump file. It identifies the
/// file as a minidump file, and gives the location of the stream directory.
struct Header {
  static constexpr uint32_t MagicSignature = 0x504d444d; // PMDM
  static constexpr uint16_t MagicVersion = 0xa793;

  support::ulittle32_t Signature;
  // The high 16 bits of version field are implementation specific. The low 16
  // bits should be MagicVersion.
  support::ulittle32_t Version;
  support::ulittle32_t NumberOfStreams;
  support::ulittle32_t StreamDirectoryRVA;
  support::ulittle32_t Checksum;
  support::ulittle32_t TimeDateStamp;
  support::ulittle64_t Flags;
};
static_assert(sizeof(Header) == 32);

/// The type of a minidump stream identifies its contents. Streams numbers after
/// LastReserved are for application-defined data streams.
enum class StreamType : uint32_t {
#define HANDLE_MDMP_STREAM_TYPE(CODE, NAME) NAME = CODE,
#include "llvm/BinaryFormat/MinidumpConstants.def"
  Unused = 0,
  LastReserved = 0x0000ffff,
};

/// Specifies the location (and size) of various objects in the minidump file.
/// The location is relative to the start of the file.
struct LocationDescriptor {
  support::ulittle32_t DataSize;
  support::ulittle32_t RVA;
};
static_assert(sizeof(LocationDescriptor) == 8);

/// Describes a single memory range (both its VM address and where to find it in
/// the file) of the process from which this minidump file was generated.
struct MemoryDescriptor {
  support::ulittle64_t StartOfMemoryRange;
  LocationDescriptor Memory;
};
static_assert(sizeof(MemoryDescriptor) == 16);

struct MemoryDescriptor_64 {
  support::ulittle64_t StartOfMemoryRange;
  support::ulittle64_t DataSize;
};

struct MemoryInfoListHeader {
  support::ulittle32_t SizeOfHeader;
  support::ulittle32_t SizeOfEntry;
  support::ulittle64_t NumberOfEntries;

  MemoryInfoListHeader() = default;
  MemoryInfoListHeader(uint32_t SizeOfHeader, uint32_t SizeOfEntry,
                       uint64_t NumberOfEntries)
      : SizeOfHeader(SizeOfHeader), SizeOfEntry(SizeOfEntry),
        NumberOfEntries(NumberOfEntries) {}
};
static_assert(sizeof(MemoryInfoListHeader) == 16);

enum class MemoryProtection : uint32_t {
#define HANDLE_MDMP_PROTECT(CODE, NAME, NATIVENAME) NAME = CODE,
#include "llvm/BinaryFormat/MinidumpConstants.def"
  LLVM_MARK_AS_BITMASK_ENUM(/*LargestValue=*/0xffffffffu),
};

enum class MemoryState : uint32_t {
#define HANDLE_MDMP_MEMSTATE(CODE, NAME, NATIVENAME) NAME = CODE,
#include "llvm/BinaryFormat/MinidumpConstants.def"
  LLVM_MARK_AS_BITMASK_ENUM(/*LargestValue=*/0xffffffffu),
};

enum class MemoryType : uint32_t {
#define HANDLE_MDMP_MEMTYPE(CODE, NAME, NATIVENAME) NAME = CODE,
#include "llvm/BinaryFormat/MinidumpConstants.def"
  LLVM_MARK_AS_BITMASK_ENUM(/*LargestValue=*/0xffffffffu),
};

struct MemoryInfo {
  support::ulittle64_t BaseAddress;
  support::ulittle64_t AllocationBase;
  support::little_t<MemoryProtection> AllocationProtect;
  support::ulittle32_t Reserved0;
  support::ulittle64_t RegionSize;
  support::little_t<MemoryState> State;
  support::little_t<MemoryProtection> Protect;
  support::little_t<MemoryType> Type;
  support::ulittle32_t Reserved1;
};
static_assert(sizeof(MemoryInfo) == 48);

/// Specifies the location and type of a single stream in the minidump file. The
/// minidump stream directory is an array of entries of this type, with its size
/// given by Header.NumberOfStreams.
struct Directory {
  support::little_t<StreamType> Type;
  LocationDescriptor Location;
};
static_assert(sizeof(Directory) == 12);

/// The processor architecture of the system that generated this minidump. Used
/// in the ProcessorArch field of the SystemInfo stream.
enum class ProcessorArchitecture : uint16_t {
#define HANDLE_MDMP_ARCH(CODE, NAME) NAME = CODE,
#include "llvm/BinaryFormat/MinidumpConstants.def"
};

/// The OS Platform of the system that generated this minidump. Used in the
/// PlatformId field of the SystemInfo stream.
enum class OSPlatform : uint32_t {
#define HANDLE_MDMP_PLATFORM(CODE, NAME) NAME = CODE,
#include "llvm/BinaryFormat/MinidumpConstants.def"
};

/// Detailed information about the processor of the system that generated this
/// minidump. Its interpretation depends on the ProcessorArchitecture enum.
union CPUInfo {
  struct X86Info {
    char VendorID[12];                        // cpuid 0: ebx, edx, ecx
    support::ulittle32_t VersionInfo;         // cpuid 1: eax
    support::ulittle32_t FeatureInfo;         // cpuid 1: edx
    support::ulittle32_t AMDExtendedFeatures; // cpuid 0x80000001, ebx
  } X86;
  struct ArmInfo {
    support::ulittle32_t CPUID;
    support::ulittle32_t ElfHWCaps; // linux specific, 0 otherwise
  } Arm;
  struct OtherInfo {
    uint8_t ProcessorFeatures[16];
  } Other;
};
static_assert(sizeof(CPUInfo) == 24);

/// The SystemInfo stream, containing various information about the system where
/// this minidump was generated.
struct SystemInfo {
  support::little_t<ProcessorArchitecture> ProcessorArch;
  support::ulittle16_t ProcessorLevel;
  support::ulittle16_t ProcessorRevision;

  uint8_t NumberOfProcessors;
  uint8_t ProductType;

  support::ulittle32_t MajorVersion;
  support::ulittle32_t MinorVersion;
  support::ulittle32_t BuildNumber;
  support::little_t<OSPlatform> PlatformId;
  support::ulittle32_t CSDVersionRVA;

  support::ulittle16_t SuiteMask;
  support::ulittle16_t Reserved;

  CPUInfo CPU;
};
static_assert(sizeof(SystemInfo) == 56);

struct VSFixedFileInfo {
  support::ulittle32_t Signature;
  support::ulittle32_t StructVersion;
  support::ulittle32_t FileVersionHigh;
  support::ulittle32_t FileVersionLow;
  support::ulittle32_t ProductVersionHigh;
  support::ulittle32_t ProductVersionLow;
  support::ulittle32_t FileFlagsMask;
  support::ulittle32_t FileFlags;
  support::ulittle32_t FileOS;
  support::ulittle32_t FileType;
  support::ulittle32_t FileSubtype;
  support::ulittle32_t FileDateHigh;
  support::ulittle32_t FileDateLow;
};
static_assert(sizeof(VSFixedFileInfo) == 52);

inline bool operator==(const VSFixedFileInfo &LHS, const VSFixedFileInfo &RHS) {
  return memcmp(&LHS, &RHS, sizeof(VSFixedFileInfo)) == 0;
}

struct Module {
  support::ulittle64_t BaseOfImage;
  support::ulittle32_t SizeOfImage;
  support::ulittle32_t Checksum;
  support::ulittle32_t TimeDateStamp;
  support::ulittle32_t ModuleNameRVA;
  VSFixedFileInfo VersionInfo;
  LocationDescriptor CvRecord;
  LocationDescriptor MiscRecord;
  support::ulittle64_t Reserved0;
  support::ulittle64_t Reserved1;
};
static_assert(sizeof(Module) == 108);

/// Describes a single thread in the minidump file. Part of the ThreadList
/// stream.
struct Thread {
  support::ulittle32_t ThreadId;
  support::ulittle32_t SuspendCount;
  support::ulittle32_t PriorityClass;
  support::ulittle32_t Priority;
  support::ulittle64_t EnvironmentBlock;
  MemoryDescriptor Stack;
  LocationDescriptor Context;
};
static_assert(sizeof(Thread) == 48);

struct Exception {
  static constexpr size_t MaxParameters = 15;

  support::ulittle32_t ExceptionCode;
  support::ulittle32_t ExceptionFlags;
  support::ulittle64_t ExceptionRecord;
  support::ulittle64_t ExceptionAddress;
  support::ulittle32_t NumberParameters;
  support::ulittle32_t UnusedAlignment;
  support::ulittle64_t ExceptionInformation[MaxParameters];
};
static_assert(sizeof(Exception) == 152);

struct ExceptionStream {
  support::ulittle32_t ThreadId;
  support::ulittle32_t UnusedAlignment;
  Exception ExceptionRecord;
  LocationDescriptor ThreadContext;
};
static_assert(sizeof(ExceptionStream) == 168);

} // namespace minidump

template <> struct DenseMapInfo<minidump::StreamType> {
  static minidump::StreamType getEmptyKey() { return minidump::StreamType(-1); }

  static minidump::StreamType getTombstoneKey() {
    return minidump::StreamType(-2);
  }

  static unsigned getHashValue(minidump::StreamType Val) {
    return DenseMapInfo<uint32_t>::getHashValue(static_cast<uint32_t>(Val));
  }

  static bool isEqual(minidump::StreamType LHS, minidump::StreamType RHS) {
    return LHS == RHS;
  }
};

} // namespace llvm

#endif // LLVM_BINARYFORMAT_MINIDUMP_H
