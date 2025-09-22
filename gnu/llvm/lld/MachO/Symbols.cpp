//===- Symbols.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Symbols.h"
#include "InputFiles.h"
#include "SyntheticSections.h"
#include "llvm/Demangle/Demangle.h"

using namespace llvm;
using namespace lld;
using namespace lld::macho;

static_assert(sizeof(void *) != 8 || sizeof(Symbol) == 56,
              "Try to minimize Symbol's size; we create many instances");

// The Microsoft ABI doesn't support using parent class tail padding for child
// members, hence the _MSC_VER check.
#if !defined(_MSC_VER)
static_assert(sizeof(void *) != 8 || sizeof(Defined) == 88,
              "Try to minimize Defined's size; we create many instances");
#endif

static_assert(sizeof(SymbolUnion) == sizeof(Defined),
              "Defined should be the largest Symbol kind");

// Returns a symbol name for an error message.
static std::string maybeDemangleSymbol(StringRef symName) {
  if (config->demangle) {
    symName.consume_front("_");
    return demangle(symName);
  }
  return symName.str();
}

std::string lld::toString(const Symbol &sym) {
  return maybeDemangleSymbol(sym.getName());
}

std::string lld::toMachOString(const object::Archive::Symbol &b) {
  return maybeDemangleSymbol(b.getName());
}

uint64_t Symbol::getStubVA() const { return in.stubs->getVA(stubsIndex); }
uint64_t Symbol::getLazyPtrVA() const {
  return in.lazyPointers->getVA(stubsIndex);
}
uint64_t Symbol::getGotVA() const { return in.got->getVA(gotIndex); }
uint64_t Symbol::getTlvVA() const { return in.tlvPointers->getVA(gotIndex); }

Defined::Defined(StringRefZ name, InputFile *file, InputSection *isec,
                 uint64_t value, uint64_t size, bool isWeakDef, bool isExternal,
                 bool isPrivateExtern, bool includeInSymtab,
                 bool isReferencedDynamically, bool noDeadStrip,
                 bool canOverrideWeakDef, bool isWeakDefCanBeHidden,
                 bool interposable)
    : Symbol(DefinedKind, name, file), overridesWeakDef(canOverrideWeakDef),
      privateExtern(isPrivateExtern), includeInSymtab(includeInSymtab),
      wasIdenticalCodeFolded(false),
      referencedDynamically(isReferencedDynamically), noDeadStrip(noDeadStrip),
      interposable(interposable), weakDefCanBeHidden(isWeakDefCanBeHidden),
      weakDef(isWeakDef), external(isExternal), originalIsec(isec),
      value(value), size(size) {
  if (isec) {
    isec->symbols.push_back(this);
    // Maintain sorted order.
    for (auto it = isec->symbols.rbegin(), rend = isec->symbols.rend();
         it != rend; ++it) {
      auto next = std::next(it);
      if (next == rend)
        break;
      if ((*it)->value < (*next)->value)
        std::swap(*next, *it);
      else
        break;
    }
  }
}

bool Defined::isTlv() const {
  return !isAbsolute() && isThreadLocalVariables(originalIsec->getFlags());
}

uint64_t Defined::getVA() const {
  assert(isLive() && "this should only be called for live symbols");

  if (isAbsolute())
    return value;

  if (!isec()->isFinal) {
    // A target arch that does not use thunks ought never ask for
    // the address of a function that has not yet been finalized.
    assert(target->usesThunks());

    // ConcatOutputSection::finalize() can seek the address of a
    // function before its address is assigned. The thunking algorithm
    // knows that unfinalized functions will be out of range, so it is
    // expedient to return a contrived out-of-range address.
    return TargetInfo::outOfRangeVA;
  }
  return isec()->getVA(value);
}

ObjFile *Defined::getObjectFile() const {
  return originalIsec ? dyn_cast_or_null<ObjFile>(originalIsec->getFile())
                      : nullptr;
}

std::string Defined::getSourceLocation() {
  if (!originalIsec)
    return {};
  return originalIsec->getSourceLocation(value);
}

// Get the canonical InputSection of the symbol.
InputSection *Defined::isec() const {
  return originalIsec ? originalIsec->canonical() : nullptr;
}

// Get the canonical unwind entry of the symbol.
ConcatInputSection *Defined::unwindEntry() const {
  return originalUnwindEntry ? originalUnwindEntry->canonical() : nullptr;
}

uint64_t DylibSymbol::getVA() const {
  return isInStubs() ? getStubVA() : Symbol::getVA();
}

void LazyArchive::fetchArchiveMember() { getFile()->fetch(sym); }
