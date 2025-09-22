//===- WasmObject.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "WasmObject.h"

#include "llvm/Support/LEB128.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
namespace objcopy {
namespace wasm {

using namespace object;
using namespace llvm::wasm;

void Object::addSectionWithOwnedContents(
    Section NewSection, std::unique_ptr<MemoryBuffer> &&Content) {
  Sections.push_back(NewSection);
  OwnedContents.emplace_back(std::move(Content));
}

void Object::removeSections(function_ref<bool(const Section &)> ToRemove) {
  // TODO: remove reloc sections for the removed section, handle symbols, etc.
  llvm::erase_if(Sections, ToRemove);
}

} // end namespace wasm
} // end namespace objcopy
} // end namespace llvm
