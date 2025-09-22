//===-- DWARFASTParser.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DWARFASTPARSER_H
#define LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DWARFASTPARSER_H

#include "DWARFDefines.h"
#include "lldb/Core/PluginInterface.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Symbol/CompilerDecl.h"
#include "lldb/Symbol/CompilerDeclContext.h"
#include "lldb/lldb-enumerations.h"
#include <optional>

namespace lldb_private {
class CompileUnit;
class ExecutionContext;
}

namespace lldb_private::plugin {
namespace dwarf {
class DWARFDIE;
class SymbolFileDWARF;

class DWARFASTParser {
public:
  enum class Kind { DWARFASTParserClang };
  DWARFASTParser(Kind kind) : m_kind(kind) {}

  virtual ~DWARFASTParser() = default;

  virtual lldb::TypeSP ParseTypeFromDWARF(const SymbolContext &sc,
                                          const DWARFDIE &die,
                                          bool *type_is_new_ptr) = 0;

  virtual ConstString ConstructDemangledNameFromDWARF(const DWARFDIE &die) = 0;

  virtual Function *ParseFunctionFromDWARF(CompileUnit &comp_unit,
                                           const DWARFDIE &die,
                                           const AddressRange &range) = 0;

  virtual bool CompleteTypeFromDWARF(const DWARFDIE &die, Type *type,
                                     CompilerType &compiler_type) = 0;

  virtual CompilerDecl GetDeclForUIDFromDWARF(const DWARFDIE &die) = 0;

  virtual CompilerDeclContext
  GetDeclContextForUIDFromDWARF(const DWARFDIE &die) = 0;

  virtual CompilerDeclContext
  GetDeclContextContainingUIDFromDWARF(const DWARFDIE &die) = 0;

  virtual void EnsureAllDIEsInDeclContextHaveBeenParsed(
      CompilerDeclContext decl_context) = 0;

  virtual std::string GetDIEClassTemplateParams(const DWARFDIE &die) = 0;

  static std::optional<SymbolFile::ArrayInfo>
  ParseChildArrayInfo(const DWARFDIE &parent_die,
                      const ExecutionContext *exe_ctx = nullptr);

  lldb_private::Type *GetTypeForDIE(const DWARFDIE &die);

  static lldb::AccessType GetAccessTypeFromDWARF(uint32_t dwarf_accessibility);

  Kind GetKind() const { return m_kind; }

private:
  const Kind m_kind;
};
} // namespace dwarf
} // namespace lldb_private::plugin

#endif // LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DWARFASTPARSER_H
