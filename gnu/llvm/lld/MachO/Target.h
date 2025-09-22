//===- Target.h -------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLD_MACHO_TARGET_H
#define LLD_MACHO_TARGET_H

#include "MachOStructs.h"
#include "Relocations.h"

#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/MemoryBuffer.h"

#include <cstddef>
#include <cstdint>

#include "mach-o/compact_unwind_encoding.h"

namespace lld::macho {
LLVM_ENABLE_BITMASK_ENUMS_IN_NAMESPACE();

class Symbol;
class Defined;
class DylibSymbol;
class InputSection;
class ObjFile;

static_assert(static_cast<uint32_t>(UNWIND_X86_64_MODE_MASK) ==
                  static_cast<uint32_t>(UNWIND_X86_MODE_MASK) &&
              static_cast<uint32_t>(UNWIND_ARM64_MODE_MASK) ==
                  static_cast<uint32_t>(UNWIND_X86_64_MODE_MASK));

// Since the mode masks have the same value on all targets, define
// a common one for convenience.
constexpr uint32_t UNWIND_MODE_MASK = UNWIND_X86_64_MODE_MASK;

class TargetInfo {
public:
  template <class LP> TargetInfo(LP) {
    // Having these values available in TargetInfo allows us to access them
    // without having to resort to templates.
    magic = LP::magic;
    pageZeroSize = LP::pageZeroSize;
    headerSize = sizeof(typename LP::mach_header);
    wordSize = LP::wordSize;
    p2WordSize = llvm::CTLog2<LP::wordSize>();
  }

  virtual ~TargetInfo() = default;

  // Validate the relocation structure and get its addend.
  virtual int64_t
  getEmbeddedAddend(llvm::MemoryBufferRef, uint64_t offset,
                    const llvm::MachO::relocation_info) const = 0;
  virtual void relocateOne(uint8_t *loc, const Reloc &, uint64_t va,
                           uint64_t relocVA) const = 0;

  // Write code for lazy binding. See the comments on StubsSection for more
  // details.
  virtual void writeStub(uint8_t *buf, const Symbol &,
                         uint64_t pointerVA) const = 0;
  virtual void writeStubHelperHeader(uint8_t *buf) const = 0;
  virtual void writeStubHelperEntry(uint8_t *buf, const Symbol &,
                                    uint64_t entryAddr) const = 0;

  virtual void writeObjCMsgSendStub(uint8_t *buf, Symbol *sym,
                                    uint64_t stubsAddr, uint64_t &stubOffset,
                                    uint64_t selrefVA,
                                    Symbol *objcMsgSend) const = 0;

  // Symbols may be referenced via either the GOT or the stubs section,
  // depending on the relocation type. prepareSymbolRelocation() will set up the
  // GOT/stubs entries, and resolveSymbolVA() will return the addresses of those
  // entries. resolveSymbolVA() may also relax the target instructions to save
  // on a level of address indirection.
  virtual void relaxGotLoad(uint8_t *loc, uint8_t type) const = 0;

  virtual uint64_t getPageSize() const = 0;

  virtual void populateThunk(InputSection *thunk, Symbol *funcSym) {
    llvm_unreachable("target does not use thunks");
  }

  const RelocAttrs &getRelocAttrs(uint8_t type) const {
    assert(type < relocAttrs.size() && "invalid relocation type");
    if (type >= relocAttrs.size())
      return invalidRelocAttrs;
    return relocAttrs[type];
  }

  bool hasAttr(uint8_t type, RelocAttrBits bit) const {
    return getRelocAttrs(type).hasAttr(bit);
  }

  bool usesThunks() const { return thunkSize > 0; }

  // For now, handleDtraceReloc only implements -no_dtrace_dof, and ensures
  // that the linking would not fail even when there are user-provided dtrace
  // symbols. However, unlike ld64, lld currently does not emit __dof sections.
  virtual void handleDtraceReloc(const Symbol *sym, const Reloc &r,
                                 uint8_t *loc) const {
    llvm_unreachable("Unsupported architecture for dtrace symbols");
  }

  virtual void applyOptimizationHints(uint8_t *, const ObjFile &) const {};

  uint32_t magic;
  llvm::MachO::CPUType cpuType;
  uint32_t cpuSubtype;

  uint64_t pageZeroSize;
  size_t headerSize;
  size_t stubSize;
  size_t stubHelperHeaderSize;
  size_t stubHelperEntrySize;
  size_t objcStubsFastSize;
  size_t objcStubsSmallSize;
  size_t objcStubsFastAlignment;
  size_t objcStubsSmallAlignment;
  uint8_t p2WordSize;
  size_t wordSize;

  size_t thunkSize = 0;
  uint64_t forwardBranchRange = 0;
  uint64_t backwardBranchRange = 0;

  uint32_t modeDwarfEncoding;
  uint8_t subtractorRelocType;
  uint8_t unsignedRelocType;

  llvm::ArrayRef<RelocAttrs> relocAttrs;

  // We contrive this value as sufficiently far from any valid address that it
  // will always be out-of-range for any architecture. UINT64_MAX is not a
  // good choice because it is (a) only 1 away from wrapping to 0, and (b) the
  // tombstone value for DenseMap<> and caused weird assertions for me.
  static constexpr uint64_t outOfRangeVA = 0xfull << 60;
};

TargetInfo *createX86_64TargetInfo();
TargetInfo *createARM64TargetInfo();
TargetInfo *createARM64_32TargetInfo();

struct LP64 {
  using mach_header = llvm::MachO::mach_header_64;
  using nlist = structs::nlist_64;
  using segment_command = llvm::MachO::segment_command_64;
  using section = llvm::MachO::section_64;
  using encryption_info_command = llvm::MachO::encryption_info_command_64;

  static constexpr uint32_t magic = llvm::MachO::MH_MAGIC_64;
  static constexpr uint32_t segmentLCType = llvm::MachO::LC_SEGMENT_64;
  static constexpr uint32_t encryptionInfoLCType =
      llvm::MachO::LC_ENCRYPTION_INFO_64;

  static constexpr uint64_t pageZeroSize = 1ull << 32;
  static constexpr size_t wordSize = 8;
};

struct ILP32 {
  using mach_header = llvm::MachO::mach_header;
  using nlist = structs::nlist;
  using segment_command = llvm::MachO::segment_command;
  using section = llvm::MachO::section;
  using encryption_info_command = llvm::MachO::encryption_info_command;

  static constexpr uint32_t magic = llvm::MachO::MH_MAGIC;
  static constexpr uint32_t segmentLCType = llvm::MachO::LC_SEGMENT;
  static constexpr uint32_t encryptionInfoLCType =
      llvm::MachO::LC_ENCRYPTION_INFO;

  static constexpr uint64_t pageZeroSize = 1ull << 12;
  static constexpr size_t wordSize = 4;
};

extern TargetInfo *target;

} // namespace lld::macho

#endif
