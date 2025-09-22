//===- WasmObject.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_OBJCOPY_WASM_WASMOBJECT_H
#define LLVM_LIB_OBJCOPY_WASM_WASMOBJECT_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Object/Wasm.h"
#include "llvm/Support/MemoryBuffer.h"
#include <vector>

namespace llvm {
namespace objcopy {
namespace wasm {

struct Section {
  // For now, each section is only an opaque binary blob with no distinction
  // between custom and known sections.
  uint8_t SectionType;
  std::optional<uint8_t> HeaderSecSizeEncodingLen;
  StringRef Name;
  ArrayRef<uint8_t> Contents;
};

struct Object {
  llvm::wasm::WasmObjectHeader Header;
  // For now don't discriminate between kinds of sections.
  std::vector<Section> Sections;

  void addSectionWithOwnedContents(Section NewSection,
                                   std::unique_ptr<MemoryBuffer> &&Content);
  void removeSections(function_ref<bool(const Section &)> ToRemove);

private:
  std::vector<std::unique_ptr<MemoryBuffer>> OwnedContents;
};

} // end namespace wasm
} // end namespace objcopy
} // end namespace llvm

#endif // LLVM_LIB_OBJCOPY_WASM_WASMOBJECT_H
