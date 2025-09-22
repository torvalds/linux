//===- COFFObject.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "COFFObject.h"
#include "llvm/ADT/DenseSet.h"
#include <algorithm>

namespace llvm {
namespace objcopy {
namespace coff {

using namespace object;

void Object::addSymbols(ArrayRef<Symbol> NewSymbols) {
  for (Symbol S : NewSymbols) {
    S.UniqueId = NextSymbolUniqueId++;
    Symbols.emplace_back(S);
  }
  updateSymbols();
}

void Object::updateSymbols() {
  SymbolMap = DenseMap<size_t, Symbol *>(Symbols.size());
  for (Symbol &Sym : Symbols)
    SymbolMap[Sym.UniqueId] = &Sym;
}

const Symbol *Object::findSymbol(size_t UniqueId) const {
  return SymbolMap.lookup(UniqueId);
}

Error Object::removeSymbols(
    function_ref<Expected<bool>(const Symbol &)> ToRemove) {
  Error Errs = Error::success();
  llvm::erase_if(Symbols, [ToRemove, &Errs](const Symbol &Sym) {
    Expected<bool> ShouldRemove = ToRemove(Sym);
    if (!ShouldRemove) {
      Errs = joinErrors(std::move(Errs), ShouldRemove.takeError());
      return false;
    }
    return *ShouldRemove;
  });

  updateSymbols();
  return Errs;
}

Error Object::markSymbols() {
  for (Symbol &Sym : Symbols)
    Sym.Referenced = false;
  for (const Section &Sec : Sections) {
    for (const Relocation &R : Sec.Relocs) {
      auto It = SymbolMap.find(R.Target);
      if (It == SymbolMap.end())
        return createStringError(object_error::invalid_symbol_index,
                                 "relocation target %zu not found", R.Target);
      It->second->Referenced = true;
    }
  }
  return Error::success();
}

void Object::addSections(ArrayRef<Section> NewSections) {
  for (Section S : NewSections) {
    S.UniqueId = NextSectionUniqueId++;
    Sections.emplace_back(S);
  }
  updateSections();
}

void Object::updateSections() {
  SectionMap = DenseMap<ssize_t, Section *>(Sections.size());
  size_t Index = 1;
  for (Section &S : Sections) {
    SectionMap[S.UniqueId] = &S;
    S.Index = Index++;
  }
}

const Section *Object::findSection(ssize_t UniqueId) const {
  return SectionMap.lookup(UniqueId);
}

void Object::removeSections(function_ref<bool(const Section &)> ToRemove) {
  DenseSet<ssize_t> AssociatedSections;
  auto RemoveAssociated = [&AssociatedSections](const Section &Sec) {
    return AssociatedSections.contains(Sec.UniqueId);
  };
  do {
    DenseSet<ssize_t> RemovedSections;
    llvm::erase_if(Sections, [ToRemove, &RemovedSections](const Section &Sec) {
      bool Remove = ToRemove(Sec);
      if (Remove)
        RemovedSections.insert(Sec.UniqueId);
      return Remove;
    });
    // Remove all symbols referring to the removed sections.
    AssociatedSections.clear();
    llvm::erase_if(
        Symbols, [&RemovedSections, &AssociatedSections](const Symbol &Sym) {
          // If there are sections that are associative to a removed
          // section,
          // remove those as well as nothing will include them (and we can't
          // leave them dangling).
          if (RemovedSections.contains(Sym.AssociativeComdatTargetSectionId))
            AssociatedSections.insert(Sym.TargetSectionId);
          return RemovedSections.contains(Sym.TargetSectionId);
        });
    ToRemove = RemoveAssociated;
  } while (!AssociatedSections.empty());
  updateSections();
  updateSymbols();
}

void Object::truncateSections(function_ref<bool(const Section &)> ToTruncate) {
  for (Section &Sec : Sections) {
    if (ToTruncate(Sec)) {
      Sec.clearContents();
      Sec.Relocs.clear();
      Sec.Header.SizeOfRawData = 0;
    }
  }
}

} // end namespace coff
} // end namespace objcopy
} // end namespace llvm
