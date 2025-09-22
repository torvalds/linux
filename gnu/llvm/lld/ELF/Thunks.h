//===- Thunks.h --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLD_ELF_THUNKS_H
#define LLD_ELF_THUNKS_H

#include "llvm/ADT/SmallVector.h"
#include "Relocations.h"

namespace lld::elf {
class Defined;
class InputFile;
class Symbol;
class ThunkSection;
// Class to describe an instance of a Thunk.
// A Thunk is a code-sequence inserted by the linker in between a caller and
// the callee. The relocation to the callee is redirected to the Thunk, which
// after executing transfers control to the callee. Typical uses of Thunks
// include transferring control from non-pi to pi and changing state on
// targets like ARM.
//
// Thunks can be created for Defined, Shared and Undefined Symbols.
// Thunks are assigned to synthetic ThunkSections
class Thunk {
public:
  Thunk(Symbol &destination, int64_t addend);
  virtual ~Thunk();

  virtual uint32_t size() = 0;
  virtual void writeTo(uint8_t *buf) = 0;

  // All Thunks must define at least one symbol, known as the thunk target
  // symbol, so that we can redirect relocations to it. The thunk may define
  // additional symbols, but these are never targets for relocations.
  virtual void addSymbols(ThunkSection &isec) = 0;

  void setOffset(uint64_t offset);
  Defined *addSymbol(StringRef name, uint8_t type, uint64_t value,
                     InputSectionBase &section);

  // Some Thunks must be placed immediately before their Target as they elide
  // a branch and fall through to the first Symbol in the Target.
  virtual InputSection *getTargetInputSection() const { return nullptr; }

  // To reuse a Thunk the InputSection and the relocation must be compatible
  // with it.
  virtual bool isCompatibleWith(const InputSection &,
                                const Relocation &) const {
    return true;
  }

  Defined *getThunkTargetSym() const { return syms[0]; }

  Symbol &destination;
  int64_t addend;
  llvm::SmallVector<Defined *, 3> syms;
  uint64_t offset = 0;
  // The alignment requirement for this Thunk, defaults to the size of the
  // typical code section alignment.
  uint32_t alignment = 4;
};

// For a Relocation to symbol S create a Thunk to be added to a synthetic
// ThunkSection.
Thunk *addThunk(const InputSection &isec, Relocation &rel);

void writePPC32PltCallStub(uint8_t *buf, uint64_t gotPltVA,
                           const InputFile *file, int64_t addend);
void writePPC64LoadAndBranch(uint8_t *buf, int64_t offset);

} // namespace lld::elf

#endif
