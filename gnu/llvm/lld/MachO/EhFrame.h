//===- EhFrame.h ------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLD_MACHO_EH_FRAME_H
#define LLD_MACHO_EH_FRAME_H

#include "InputSection.h"
#include "Relocations.h"

#include "lld/Common/LLVM.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/SmallVector.h"

/*
 * NOTE: The main bulk of the EH frame parsing logic is in InputFiles.cpp as it
 * is closely coupled with other file parsing logic; EhFrame.h just contains a
 * few helpers.
 */

/*
 * === The EH frame format ===
 *
 * EH frames can either be Common Information Entries (CIEs) or Frame
 * Description Entries (FDEs). CIEs contain information that is common amongst
 * several FDEs. Each FDE contains a pointer to its CIE. Thus all the EH frame
 * entries together form a forest of two-level trees, with CIEs as the roots
 * and FDEs as the leaves. Note that a CIE must precede the FDEs which point
 * to it.
 *
 * A CIE comprises the following fields in order:
 * 1.   Length of the entry (4 or 12 bytes)
 * 2.   CIE offset (4 bytes; always 0 for CIEs)
 * 3.   CIE version (byte)
 * 4.   Null-terminated augmentation string
 * 5-8. LEB128 values that we don't care about
 * 9.   Augmentation data, to be interpreted using the aug string
 * 10.  DWARF instructions (ignored by LLD)
 *
 * An FDE comprises of the following:
 * 1. Length of the entry (4 or 12 bytes)
 * 2. CIE offset (4 bytes pcrel offset that points backwards to this FDE's CIE)
 * 3. Function address (pointer-sized pcrel offset)
 * 4. (std::optional) Augmentation data length
 * 5. (std::optional) LSDA address (pointer-sized pcrel offset)
 * 6. DWARF instructions (ignored by LLD)
 */
namespace lld::macho {

class EhReader {
public:
  EhReader(const ObjFile *file, ArrayRef<uint8_t> data, size_t dataOff)
      : file(file), data(data), dataOff(dataOff) {}
  size_t size() const { return data.size(); }
  // Read and validate the length field.
  uint64_t readLength(size_t *off) const;
  // Skip the length field without doing validation.
  void skipValidLength(size_t *off) const;
  uint8_t readByte(size_t *off) const;
  uint32_t readU32(size_t *off) const;
  uint64_t readPointer(size_t *off, uint8_t size) const;
  StringRef readString(size_t *off) const;
  void skipLeb128(size_t *off) const;
  void failOn(size_t errOff, const Twine &msg) const;

private:
  const ObjFile *file;
  ArrayRef<uint8_t> data;
  // The offset of the data array within its section. Used only for error
  // reporting.
  const size_t dataOff;
};

// The EH frame format, when emitted by llvm-mc, consists of a number of
// "abs-ified" relocations, i.e. relocations that are implicitly encoded as
// pcrel offsets in the section data. The offsets refer to the locations of
// symbols in the input object file. When we ingest these EH frames, we convert
// these implicit relocations into explicit Relocs.
//
// These pcrel relocations are semantically similar to X86_64_RELOC_SIGNED_4.
// However, we need this operation to be cross-platform, and ARM does not have a
// similar relocation that is applicable. We therefore use the more verbose (but
// more generic) subtractor relocation to encode these pcrel values. ld64
// appears to do something similar -- its `-r` output contains these explicit
// subtractor relocations.
class EhRelocator {
public:
  EhRelocator(InputSection *isec) : isec(isec) {}

  // For the next two methods, let `PC` denote `isec address + off`.
  // Create relocs writing the value of target - PC to PC.
  void makePcRel(uint64_t off,
                 llvm::PointerUnion<Symbol *, InputSection *> target,
                 uint8_t length);
  // Create relocs writing the value of PC - target to PC.
  void makeNegativePcRel(uint64_t off,
                         llvm::PointerUnion<Symbol *, InputSection *> target,
                         uint8_t length);
  // Insert the new relocations into isec->relocs.
  void commit();

private:
  InputSection *isec;
  // Insert new relocs here so that we don't invalidate iterators into the
  // existing relocs vector.
  SmallVector<Reloc, 6> newRelocs;
};

} // namespace lld::macho

#endif
