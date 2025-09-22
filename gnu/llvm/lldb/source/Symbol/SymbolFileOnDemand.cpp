//===-- SymbolFileOnDemand.cpp ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Symbol/SymbolFileOnDemand.h"

#include "lldb/Core/Module.h"
#include "lldb/Symbol/SymbolFile.h"

#include <memory>
#include <optional>

using namespace lldb;
using namespace lldb_private;

char SymbolFileOnDemand::ID;

SymbolFileOnDemand::SymbolFileOnDemand(
    std::unique_ptr<SymbolFile> &&symbol_file)
    : m_sym_file_impl(std::move(symbol_file)) {}

SymbolFileOnDemand::~SymbolFileOnDemand() = default;

uint32_t SymbolFileOnDemand::CalculateAbilities() {
  // Explicitly allow ability checking to pass though.
  // This should be a cheap operation.
  return m_sym_file_impl->CalculateAbilities();
}

std::recursive_mutex &SymbolFileOnDemand::GetModuleMutex() const {
  return m_sym_file_impl->GetModuleMutex();
}

void SymbolFileOnDemand::InitializeObject() {
  if (!m_debug_info_enabled) {
    LLDB_LOG(GetLog(), "[{0}] {1} is skipped", GetSymbolFileName(),
             __FUNCTION__);
    return;
  }
  return m_sym_file_impl->InitializeObject();
}

lldb::LanguageType SymbolFileOnDemand::ParseLanguage(CompileUnit &comp_unit) {
  if (!m_debug_info_enabled) {
    Log *log = GetLog();
    LLDB_LOG(log, "[{0}] {1} is skipped", GetSymbolFileName(), __FUNCTION__);
    if (log) {
      lldb::LanguageType langType = m_sym_file_impl->ParseLanguage(comp_unit);
      if (langType != eLanguageTypeUnknown)
        LLDB_LOG(log, "Language {0} would return if hydrated.", langType);
    }
    return eLanguageTypeUnknown;
  }
  return m_sym_file_impl->ParseLanguage(comp_unit);
}

XcodeSDK SymbolFileOnDemand::ParseXcodeSDK(CompileUnit &comp_unit) {
  if (!m_debug_info_enabled) {
    Log *log = GetLog();
    LLDB_LOG(log, "[{0}] {1} is skipped", GetSymbolFileName(), __FUNCTION__);
    XcodeSDK defaultValue{};
    if (log) {
      XcodeSDK sdk = m_sym_file_impl->ParseXcodeSDK(comp_unit);
      if (!(sdk == defaultValue))
        LLDB_LOG(log, "SDK {0} would return if hydrated.", sdk.GetString());
    }
    return defaultValue;
  }
  return m_sym_file_impl->ParseXcodeSDK(comp_unit);
}

size_t SymbolFileOnDemand::ParseFunctions(CompileUnit &comp_unit) {
  if (!m_debug_info_enabled) {
    LLDB_LOG(GetLog(), "[{0}] {1} is skipped", GetSymbolFileName(),
             __FUNCTION__);
    return 0;
  }
  return m_sym_file_impl->ParseFunctions(comp_unit);
}

bool SymbolFileOnDemand::ParseLineTable(CompileUnit &comp_unit) {
  if (!m_debug_info_enabled) {
    LLDB_LOG(GetLog(), "[{0}] {1} is skipped", GetSymbolFileName(),
             __FUNCTION__);
    return false;
  }
  return m_sym_file_impl->ParseLineTable(comp_unit);
}

bool SymbolFileOnDemand::ParseDebugMacros(CompileUnit &comp_unit) {
  if (!m_debug_info_enabled) {
    LLDB_LOG(GetLog(), "[{0}] {1} is skipped", GetSymbolFileName(),
             __FUNCTION__);
    return false;
  }
  return m_sym_file_impl->ParseDebugMacros(comp_unit);
}

bool SymbolFileOnDemand::ForEachExternalModule(
    CompileUnit &comp_unit,
    llvm::DenseSet<lldb_private::SymbolFile *> &visited_symbol_files,
    llvm::function_ref<bool(Module &)> lambda) {
  if (!m_debug_info_enabled) {
    LLDB_LOG(GetLog(), "[{0}] {1} is skipped", GetSymbolFileName(),
             __FUNCTION__);
    // Return false to not early exit.
    return false;
  }
  return m_sym_file_impl->ForEachExternalModule(comp_unit, visited_symbol_files,
                                                lambda);
}

bool SymbolFileOnDemand::ParseSupportFiles(CompileUnit &comp_unit,
                                           SupportFileList &support_files) {
  LLDB_LOG(GetLog(),
           "[{0}] {1} is not skipped: explicitly allowed to support breakpoint",
           GetSymbolFileName(), __FUNCTION__);
  // Explicitly allow this API through to support source line breakpoint.
  return m_sym_file_impl->ParseSupportFiles(comp_unit, support_files);
}

bool SymbolFileOnDemand::ParseIsOptimized(CompileUnit &comp_unit) {
  if (!m_debug_info_enabled) {
    Log *log = GetLog();
    LLDB_LOG(log, "[{0}] {1} is skipped", GetSymbolFileName(), __FUNCTION__);
    if (log) {
      bool optimized = m_sym_file_impl->ParseIsOptimized(comp_unit);
      if (optimized) {
        LLDB_LOG(log, "Would return optimized if hydrated.");
      }
    }
    return false;
  }
  return m_sym_file_impl->ParseIsOptimized(comp_unit);
}

size_t SymbolFileOnDemand::ParseTypes(CompileUnit &comp_unit) {
  if (!m_debug_info_enabled) {
    LLDB_LOG(GetLog(), "[{0}] {1} is skipped", GetSymbolFileName(),
             __FUNCTION__);
    return 0;
  }
  return m_sym_file_impl->ParseTypes(comp_unit);
}

bool SymbolFileOnDemand::ParseImportedModules(
    const lldb_private::SymbolContext &sc,
    std::vector<SourceModule> &imported_modules) {
  if (!m_debug_info_enabled) {
    Log *log = GetLog();
    LLDB_LOG(log, "[{0}] {1} is skipped", GetSymbolFileName(), __FUNCTION__);
    if (log) {
      std::vector<SourceModule> tmp_imported_modules;
      bool succeed =
          m_sym_file_impl->ParseImportedModules(sc, tmp_imported_modules);
      if (succeed)
        LLDB_LOG(log, "{0} imported modules would be parsed if hydrated.",
                 tmp_imported_modules.size());
    }
    return false;
  }
  return m_sym_file_impl->ParseImportedModules(sc, imported_modules);
}

size_t SymbolFileOnDemand::ParseBlocksRecursive(Function &func) {
  if (!m_debug_info_enabled) {
    LLDB_LOG(GetLog(), "[{0}] {1} is skipped", GetSymbolFileName(),
             __FUNCTION__);
    return 0;
  }
  return m_sym_file_impl->ParseBlocksRecursive(func);
}

size_t SymbolFileOnDemand::ParseVariablesForContext(const SymbolContext &sc) {
  if (!m_debug_info_enabled) {
    LLDB_LOG(GetLog(), "[{0}] {1} is skipped", GetSymbolFileName(),
             __FUNCTION__);
    return 0;
  }
  return m_sym_file_impl->ParseVariablesForContext(sc);
}

Type *SymbolFileOnDemand::ResolveTypeUID(lldb::user_id_t type_uid) {
  if (!m_debug_info_enabled) {
    Log *log = GetLog();
    LLDB_LOG(log, "[{0}] {1} is skipped", GetSymbolFileName(), __FUNCTION__);
    if (log) {
      Type *resolved_type = m_sym_file_impl->ResolveTypeUID(type_uid);
      if (resolved_type)
        LLDB_LOG(log, "Type would be parsed for {0} if hydrated.", type_uid);
    }
    return nullptr;
  }
  return m_sym_file_impl->ResolveTypeUID(type_uid);
}

std::optional<SymbolFile::ArrayInfo>
SymbolFileOnDemand::GetDynamicArrayInfoForUID(
    lldb::user_id_t type_uid, const lldb_private::ExecutionContext *exe_ctx) {
  if (!m_debug_info_enabled) {
    LLDB_LOG(GetLog(), "[{0}] {1} is skipped", GetSymbolFileName(),
             __FUNCTION__);
    return std::nullopt;
  }
  return m_sym_file_impl->GetDynamicArrayInfoForUID(type_uid, exe_ctx);
}

bool SymbolFileOnDemand::CompleteType(CompilerType &compiler_type) {
  if (!m_debug_info_enabled) {
    LLDB_LOG(GetLog(), "[{0}] {1} is skipped", GetSymbolFileName(),
             __FUNCTION__);
    return false;
  }
  return m_sym_file_impl->CompleteType(compiler_type);
}

CompilerDecl SymbolFileOnDemand::GetDeclForUID(lldb::user_id_t type_uid) {
  if (!m_debug_info_enabled) {
    Log *log = GetLog();
    LLDB_LOG(log, "[{0}] {1} is skipped", GetSymbolFileName(), __FUNCTION__);
    if (log) {
      CompilerDecl parsed_decl = m_sym_file_impl->GetDeclForUID(type_uid);
      if (parsed_decl != CompilerDecl()) {
        LLDB_LOG(log, "CompilerDecl {0} would be parsed for {1} if hydrated.",
                 parsed_decl.GetName(), type_uid);
      }
    }
    return CompilerDecl();
  }
  return m_sym_file_impl->GetDeclForUID(type_uid);
}

CompilerDeclContext
SymbolFileOnDemand::GetDeclContextForUID(lldb::user_id_t type_uid) {
  if (!m_debug_info_enabled) {
    LLDB_LOG(GetLog(), "[{0}] {1} is skipped", GetSymbolFileName(),
             __FUNCTION__);
    return CompilerDeclContext();
  }
  return m_sym_file_impl->GetDeclContextForUID(type_uid);
}

CompilerDeclContext
SymbolFileOnDemand::GetDeclContextContainingUID(lldb::user_id_t type_uid) {
  if (!m_debug_info_enabled) {
    LLDB_LOG(GetLog(), "[{0}] {1} is skipped", GetSymbolFileName(),
             __FUNCTION__);
    return CompilerDeclContext();
  }
  return m_sym_file_impl->GetDeclContextContainingUID(type_uid);
}

void SymbolFileOnDemand::ParseDeclsForContext(CompilerDeclContext decl_ctx) {
  if (!m_debug_info_enabled) {
    LLDB_LOG(GetLog(), "[{0}] {1} is skipped", GetSymbolFileName(),
             __FUNCTION__);
    return;
  }
  return m_sym_file_impl->ParseDeclsForContext(decl_ctx);
}

uint32_t
SymbolFileOnDemand::ResolveSymbolContext(const Address &so_addr,
                                         SymbolContextItem resolve_scope,
                                         SymbolContext &sc) {
  if (!m_debug_info_enabled) {
    LLDB_LOG(GetLog(), "[{0}] {1} is skipped", GetSymbolFileName(),
             __FUNCTION__);
    return 0;
  }
  return m_sym_file_impl->ResolveSymbolContext(so_addr, resolve_scope, sc);
}

Status SymbolFileOnDemand::CalculateFrameVariableError(StackFrame &frame) {
  if (!m_debug_info_enabled) {
    LLDB_LOG(GetLog(), "[{0}] {1} is skipped", GetSymbolFileName(),
             __FUNCTION__);
    return Status();
  }
  return m_sym_file_impl->CalculateFrameVariableError(frame);
}

uint32_t SymbolFileOnDemand::ResolveSymbolContext(
    const SourceLocationSpec &src_location_spec,
    SymbolContextItem resolve_scope, SymbolContextList &sc_list) {
  if (!m_debug_info_enabled) {
    LLDB_LOG(GetLog(), "[{0}] {1} is skipped", GetSymbolFileName(),
             __FUNCTION__);
    return 0;
  }
  return m_sym_file_impl->ResolveSymbolContext(src_location_spec, resolve_scope,
                                               sc_list);
}

void SymbolFileOnDemand::Dump(lldb_private::Stream &s) {
  if (!m_debug_info_enabled) {
    LLDB_LOG(GetLog(), "[{0}] {1} is skipped", GetSymbolFileName(),
             __FUNCTION__);
    return;
  }
  return m_sym_file_impl->Dump(s);
}

void SymbolFileOnDemand::DumpClangAST(lldb_private::Stream &s) {
  if (!m_debug_info_enabled) {
    LLDB_LOG(GetLog(), "[{0}] {1} is skipped", GetSymbolFileName(),
             __FUNCTION__);
    return;
  }
  return m_sym_file_impl->DumpClangAST(s);
}

void SymbolFileOnDemand::FindGlobalVariables(const RegularExpression &regex,
                                             uint32_t max_matches,
                                             VariableList &variables) {
  if (!m_debug_info_enabled) {
    LLDB_LOG(GetLog(), "[{0}] {1} is skipped", GetSymbolFileName(),
             __FUNCTION__);
    return;
  }
  return m_sym_file_impl->FindGlobalVariables(regex, max_matches, variables);
}

void SymbolFileOnDemand::FindGlobalVariables(
    ConstString name, const CompilerDeclContext &parent_decl_ctx,
    uint32_t max_matches, VariableList &variables) {
  if (!m_debug_info_enabled) {
    Log *log = GetLog();
    Symtab *symtab = GetSymtab();
    if (!symtab) {
      LLDB_LOG(log, "[{0}] {1} is skipped - fail to get symtab",
               GetSymbolFileName(), __FUNCTION__);
      return;
    }
    Symbol *sym = symtab->FindFirstSymbolWithNameAndType(
        name, eSymbolTypeData, Symtab::eDebugAny, Symtab::eVisibilityAny);
    if (!sym) {
      LLDB_LOG(log, "[{0}] {1} is skipped - fail to find match in symtab",
               GetSymbolFileName(), __FUNCTION__);
      return;
    }
    LLDB_LOG(log, "[{0}] {1} is NOT skipped - found match in symtab",
             GetSymbolFileName(), __FUNCTION__);

    // Found match in symbol table hydrate debug info and
    // allow the FindGlobalVariables to go through.
    SetLoadDebugInfoEnabled();
  }
  return m_sym_file_impl->FindGlobalVariables(name, parent_decl_ctx,
                                              max_matches, variables);
}

void SymbolFileOnDemand::FindFunctions(const RegularExpression &regex,
                                       bool include_inlines,
                                       SymbolContextList &sc_list) {
  if (!m_debug_info_enabled) {
    Log *log = GetLog();
    Symtab *symtab = GetSymtab();
    if (!symtab) {
      LLDB_LOG(log, "[{0}] {1} is skipped - fail to get symtab",
               GetSymbolFileName(), __FUNCTION__);
      return;
    }
    std::vector<uint32_t> symbol_indexes;
    symtab->AppendSymbolIndexesMatchingRegExAndType(
        regex, eSymbolTypeAny, Symtab::eDebugAny, Symtab::eVisibilityAny,
        symbol_indexes);
    if (symbol_indexes.empty()) {
      LLDB_LOG(log, "[{0}] {1} is skipped - fail to find match in symtab",
               GetSymbolFileName(), __FUNCTION__);
      return;
    }
    LLDB_LOG(log, "[{0}] {1} is NOT skipped - found match in symtab",
             GetSymbolFileName(), __FUNCTION__);

    // Found match in symbol table hydrate debug info and
    // allow the FindFucntions to go through.
    SetLoadDebugInfoEnabled();
  }
  return m_sym_file_impl->FindFunctions(regex, include_inlines, sc_list);
}

void SymbolFileOnDemand::FindFunctions(
    const Module::LookupInfo &lookup_info,
    const CompilerDeclContext &parent_decl_ctx, bool include_inlines,
    SymbolContextList &sc_list) {
  ConstString name = lookup_info.GetLookupName();
  FunctionNameType name_type_mask = lookup_info.GetNameTypeMask();
  if (!m_debug_info_enabled) {
    Log *log = GetLog();

    Symtab *symtab = GetSymtab();
    if (!symtab) {
      LLDB_LOG(log, "[{0}] {1}({2}) is skipped  - fail to get symtab",
               GetSymbolFileName(), __FUNCTION__, name);
      return;
    }

    SymbolContextList sc_list_helper;
    symtab->FindFunctionSymbols(name, name_type_mask, sc_list_helper);
    if (sc_list_helper.GetSize() == 0) {
      LLDB_LOG(log, "[{0}] {1}({2}) is skipped - fail to find match in symtab",
               GetSymbolFileName(), __FUNCTION__, name);
      return;
    }
    LLDB_LOG(log, "[{0}] {1}({2}) is NOT skipped - found match in symtab",
             GetSymbolFileName(), __FUNCTION__, name);

    // Found match in symbol table hydrate debug info and
    // allow the FindFucntions to go through.
    SetLoadDebugInfoEnabled();
  }
  return m_sym_file_impl->FindFunctions(lookup_info, parent_decl_ctx,
                                        include_inlines, sc_list);
}

void SymbolFileOnDemand::GetMangledNamesForFunction(
    const std::string &scope_qualified_name,
    std::vector<ConstString> &mangled_names) {
  if (!m_debug_info_enabled) {
    Log *log = GetLog();
    LLDB_LOG(log, "[{0}] {1}({2}) is skipped", GetSymbolFileName(),
             __FUNCTION__, scope_qualified_name);
    return;
  }
  return m_sym_file_impl->GetMangledNamesForFunction(scope_qualified_name,
                                                     mangled_names);
}

void SymbolFileOnDemand::FindTypes(const TypeQuery &match,
                                   TypeResults &results) {
  if (!m_debug_info_enabled) {
    LLDB_LOG(GetLog(), "[{0}] {1} is skipped", GetSymbolFileName(),
             __FUNCTION__);
    return;
  }
  return m_sym_file_impl->FindTypes(match, results);
}

void SymbolFileOnDemand::GetTypes(SymbolContextScope *sc_scope,
                                  TypeClass type_mask, TypeList &type_list) {
  if (!m_debug_info_enabled) {
    LLDB_LOG(GetLog(), "[{0}] {1} is skipped", GetSymbolFileName(),
             __FUNCTION__);
    return;
  }
  return m_sym_file_impl->GetTypes(sc_scope, type_mask, type_list);
}

llvm::Expected<lldb::TypeSystemSP>
SymbolFileOnDemand::GetTypeSystemForLanguage(LanguageType language) {
  if (!m_debug_info_enabled) {
    Log *log = GetLog();
    LLDB_LOG(log, "[{0}] {1} is skipped for language type {2}",
             GetSymbolFileName(), __FUNCTION__, language);
    return llvm::createStringError(
        "GetTypeSystemForLanguage is skipped by SymbolFileOnDemand");
  }
  return m_sym_file_impl->GetTypeSystemForLanguage(language);
}

CompilerDeclContext
SymbolFileOnDemand::FindNamespace(ConstString name,
                                  const CompilerDeclContext &parent_decl_ctx,
                                  bool only_root_namespaces) {
  if (!m_debug_info_enabled) {
    LLDB_LOG(GetLog(), "[{0}] {1}({2}) is skipped", GetSymbolFileName(),
             __FUNCTION__, name);
    return SymbolFile::FindNamespace(name, parent_decl_ctx,
                                     only_root_namespaces);
  }
  return m_sym_file_impl->FindNamespace(name, parent_decl_ctx,
                                        only_root_namespaces);
}

std::vector<std::unique_ptr<lldb_private::CallEdge>>
SymbolFileOnDemand::ParseCallEdgesInFunction(UserID func_id) {
  if (!m_debug_info_enabled) {
    Log *log = GetLog();
    LLDB_LOG(log, "[{0}] {1} is skipped", GetSymbolFileName(), __FUNCTION__);
    if (log) {
      std::vector<std::unique_ptr<lldb_private::CallEdge>> call_edges =
          m_sym_file_impl->ParseCallEdgesInFunction(func_id);
      if (call_edges.size() > 0) {
        LLDB_LOG(log, "{0} call edges would be parsed for {1} if hydrated.",
                 call_edges.size(), func_id.GetID());
      }
    }
    return {};
  }
  return m_sym_file_impl->ParseCallEdgesInFunction(func_id);
}

lldb::UnwindPlanSP
SymbolFileOnDemand::GetUnwindPlan(const Address &address,
                                  const RegisterInfoResolver &resolver) {
  if (!m_debug_info_enabled) {
    LLDB_LOG(GetLog(), "[{0}] {1} is skipped", GetSymbolFileName(),
             __FUNCTION__);
    return nullptr;
  }
  return m_sym_file_impl->GetUnwindPlan(address, resolver);
}

llvm::Expected<lldb::addr_t>
SymbolFileOnDemand::GetParameterStackSize(Symbol &symbol) {
  if (!m_debug_info_enabled) {
    Log *log = GetLog();
    LLDB_LOG(log, "[{0}] {1} is skipped", GetSymbolFileName(), __FUNCTION__);
    if (log) {
      llvm::Expected<lldb::addr_t> stack_size =
          m_sym_file_impl->GetParameterStackSize(symbol);
      if (stack_size) {
        LLDB_LOG(log, "{0} stack size would return for symbol {1} if hydrated.",
                 *stack_size, symbol.GetName());
      }
    }
    return SymbolFile::GetParameterStackSize(symbol);
  }
  return m_sym_file_impl->GetParameterStackSize(symbol);
}

void SymbolFileOnDemand::PreloadSymbols() {
  m_preload_symbols = true;
  if (!m_debug_info_enabled) {
    LLDB_LOG(GetLog(), "[{0}] {1} is skipped", GetSymbolFileName(),
             __FUNCTION__);
    return;
  }
  return m_sym_file_impl->PreloadSymbols();
}

uint64_t SymbolFileOnDemand::GetDebugInfoSize(bool load_all_debug_info) {
  // Always return the real debug info size.
  LLDB_LOG(GetLog(), "[{0}] {1} is not skipped", GetSymbolFileName(),
           __FUNCTION__);
  return m_sym_file_impl->GetDebugInfoSize(load_all_debug_info);
}

StatsDuration::Duration SymbolFileOnDemand::GetDebugInfoParseTime() {
  // Always return the real parse time.
  LLDB_LOG(GetLog(), "[{0}] {1} is not skipped", GetSymbolFileName(),
           __FUNCTION__);
  return m_sym_file_impl->GetDebugInfoParseTime();
}

StatsDuration::Duration SymbolFileOnDemand::GetDebugInfoIndexTime() {
  // Always return the real index time.
  LLDB_LOG(GetLog(), "[{0}] {1} is not skipped", GetSymbolFileName(),
           __FUNCTION__);
  return m_sym_file_impl->GetDebugInfoIndexTime();
}

void SymbolFileOnDemand::SetLoadDebugInfoEnabled() {
  if (m_debug_info_enabled)
    return;
  LLDB_LOG(GetLog(), "[{0}] Hydrate debug info", GetSymbolFileName());
  m_debug_info_enabled = true;
  InitializeObject();
  if (m_preload_symbols)
    PreloadSymbols();
}

uint32_t SymbolFileOnDemand::GetNumCompileUnits() {
  LLDB_LOG(GetLog(), "[{0}] {1} is not skipped to support breakpoint hydration",
           GetSymbolFileName(), __FUNCTION__);
  return m_sym_file_impl->GetNumCompileUnits();
}

CompUnitSP SymbolFileOnDemand::GetCompileUnitAtIndex(uint32_t idx) {
  LLDB_LOG(GetLog(), "[{0}] {1} is not skipped to support breakpoint hydration",
           GetSymbolFileName(), __FUNCTION__);
  return m_sym_file_impl->GetCompileUnitAtIndex(idx);
}

uint32_t SymbolFileOnDemand::GetAbilities() {
  if (!m_debug_info_enabled) {
    LLDB_LOG(GetLog(), "[{0}] {1} is skipped", GetSymbolFileName(),
             __FUNCTION__);
    return 0;
  }
  return m_sym_file_impl->GetAbilities();
}
