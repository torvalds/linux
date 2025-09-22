//===-- SymbolFileJSON.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SymbolFileJSON.h"

#include "Plugins/ObjectFile/JSON/ObjectFileJSON.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Symbol/Symtab.h"
#include "lldb/Symbol/TypeList.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/RegularExpression.h"
#include "lldb/Utility/Timer.h"
#include "llvm/Support/MemoryBuffer.h"

#include <memory>
#include <optional>

using namespace llvm;
using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE(SymbolFileJSON)

char SymbolFileJSON::ID;

SymbolFileJSON::SymbolFileJSON(lldb::ObjectFileSP objfile_sp)
    : SymbolFileCommon(std::move(objfile_sp)) {}

void SymbolFileJSON::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                GetPluginDescriptionStatic(), CreateInstance);
}

void SymbolFileJSON::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

llvm::StringRef SymbolFileJSON::GetPluginDescriptionStatic() {
  return "Reads debug symbols from a JSON symbol table.";
}

SymbolFile *SymbolFileJSON::CreateInstance(ObjectFileSP objfile_sp) {
  return new SymbolFileJSON(std::move(objfile_sp));
}

uint32_t SymbolFileJSON::CalculateAbilities() {
  if (!m_objfile_sp || !llvm::isa<ObjectFileJSON>(*m_objfile_sp))
    return 0;

  return GlobalVariables | Functions;
}

uint32_t SymbolFileJSON::ResolveSymbolContext(const Address &so_addr,
                                              SymbolContextItem resolve_scope,
                                              SymbolContext &sc) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  if (m_objfile_sp->GetSymtab() == nullptr)
    return 0;

  uint32_t resolved_flags = 0;
  if (resolve_scope & eSymbolContextSymbol) {
    sc.symbol = m_objfile_sp->GetSymtab()->FindSymbolContainingFileAddress(
        so_addr.GetFileAddress());
    if (sc.symbol)
      resolved_flags |= eSymbolContextSymbol;
  }
  return resolved_flags;
}

CompUnitSP SymbolFileJSON::ParseCompileUnitAtIndex(uint32_t idx) { return {}; }

void SymbolFileJSON::GetTypes(SymbolContextScope *sc_scope, TypeClass type_mask,
                              lldb_private::TypeList &type_list) {}

void SymbolFileJSON::AddSymbols(Symtab &symtab) {
  if (!m_objfile_sp)
    return;

  Symtab *json_symtab = m_objfile_sp->GetSymtab();
  if (!json_symtab)
    return;

  if (&symtab == json_symtab)
    return;

  // Merge the two symbol tables.
  const size_t num_new_symbols = json_symtab->GetNumSymbols();
  for (size_t i = 0; i < num_new_symbols; ++i) {
    Symbol *s = json_symtab->SymbolAtIndex(i);
    symtab.AddSymbol(*s);
  }
  symtab.Finalize();
}
