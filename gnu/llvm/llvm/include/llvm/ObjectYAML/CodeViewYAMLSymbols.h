//===- CodeViewYAMLSymbols.h - CodeView YAMLIO Symbol implementation ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines classes for handling the YAML representation of CodeView
// Debug Info.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECTYAML_CODEVIEWYAMLSYMBOLS_H
#define LLVM_OBJECTYAML_CODEVIEWYAMLSYMBOLS_H

#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/SymbolRecord.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/YAMLTraits.h"
#include <memory>

namespace llvm {
namespace CodeViewYAML {

namespace detail {

struct SymbolRecordBase;

} // end namespace detail

struct SymbolRecord {
  std::shared_ptr<detail::SymbolRecordBase> Symbol;

  codeview::CVSymbol
  toCodeViewSymbol(BumpPtrAllocator &Allocator,
                   codeview::CodeViewContainer Container) const;

  static Expected<SymbolRecord> fromCodeViewSymbol(codeview::CVSymbol Symbol);
};

} // end namespace CodeViewYAML
} // end namespace llvm

LLVM_YAML_DECLARE_MAPPING_TRAITS(CodeViewYAML::SymbolRecord)
LLVM_YAML_IS_SEQUENCE_VECTOR(CodeViewYAML::SymbolRecord)

#endif // LLVM_OBJECTYAML_CODEVIEWYAMLSYMBOLS_H
