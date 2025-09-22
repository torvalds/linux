//===-- SymbolFilePDB.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SymbolFilePDB.h"

#include "PDBASTParser.h"
#include "PDBLocationToDWARFExpression.h"

#include "clang/Lex/Lexer.h"

#include "Plugins/TypeSystem/Clang/TypeSystemClang.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/LineTable.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Symbol/SymbolVendor.h"
#include "lldb/Symbol/TypeList.h"
#include "lldb/Symbol/TypeMap.h"
#include "lldb/Symbol/Variable.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/RegularExpression.h"

#include "llvm/DebugInfo/PDB/ConcreteSymbolEnumerator.h"
#include "llvm/DebugInfo/PDB/GenericError.h"
#include "llvm/DebugInfo/PDB/IPDBDataStream.h"
#include "llvm/DebugInfo/PDB/IPDBEnumChildren.h"
#include "llvm/DebugInfo/PDB/IPDBLineNumber.h"
#include "llvm/DebugInfo/PDB/IPDBSectionContrib.h"
#include "llvm/DebugInfo/PDB/IPDBSourceFile.h"
#include "llvm/DebugInfo/PDB/IPDBTable.h"
#include "llvm/DebugInfo/PDB/PDBSymbol.h"
#include "llvm/DebugInfo/PDB/PDBSymbolBlock.h"
#include "llvm/DebugInfo/PDB/PDBSymbolCompiland.h"
#include "llvm/DebugInfo/PDB/PDBSymbolCompilandDetails.h"
#include "llvm/DebugInfo/PDB/PDBSymbolData.h"
#include "llvm/DebugInfo/PDB/PDBSymbolExe.h"
#include "llvm/DebugInfo/PDB/PDBSymbolFunc.h"
#include "llvm/DebugInfo/PDB/PDBSymbolFuncDebugEnd.h"
#include "llvm/DebugInfo/PDB/PDBSymbolFuncDebugStart.h"
#include "llvm/DebugInfo/PDB/PDBSymbolPublicSymbol.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeBuiltin.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeEnum.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeFunctionSig.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeTypedef.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeUDT.h"

#include "Plugins/Language/CPlusPlus/CPlusPlusLanguage.h"
#include "Plugins/Language/CPlusPlus/MSVCUndecoratedNameParser.h"
#include "Plugins/SymbolFile/NativePDB/SymbolFileNativePDB.h"

#if defined(_WIN32)
#include "llvm/Config/llvm-config.h"
#include <optional>
#endif

using namespace lldb;
using namespace lldb_private;
using namespace llvm::pdb;

LLDB_PLUGIN_DEFINE(SymbolFilePDB)

char SymbolFilePDB::ID;

namespace {
lldb::LanguageType TranslateLanguage(PDB_Lang lang) {
  switch (lang) {
  case PDB_Lang::Cpp:
    return lldb::LanguageType::eLanguageTypeC_plus_plus;
  case PDB_Lang::C:
    return lldb::LanguageType::eLanguageTypeC;
  case PDB_Lang::Swift:
    return lldb::LanguageType::eLanguageTypeSwift;
  case PDB_Lang::Rust:
    return lldb::LanguageType::eLanguageTypeRust;
  case PDB_Lang::ObjC:
    return lldb::LanguageType::eLanguageTypeObjC;
  case PDB_Lang::ObjCpp:
    return lldb::LanguageType::eLanguageTypeObjC_plus_plus;
  default:
    return lldb::LanguageType::eLanguageTypeUnknown;
  }
}

bool ShouldAddLine(uint32_t requested_line, uint32_t actual_line,
                   uint32_t addr_length) {
  return ((requested_line == 0 || actual_line == requested_line) &&
          addr_length > 0);
}
} // namespace

static bool ShouldUseNativeReader() {
#if defined(_WIN32)
#if LLVM_ENABLE_DIA_SDK
  llvm::StringRef use_native = ::getenv("LLDB_USE_NATIVE_PDB_READER");
  if (!use_native.equals_insensitive("on") &&
      !use_native.equals_insensitive("yes") &&
      !use_native.equals_insensitive("1") &&
      !use_native.equals_insensitive("true"))
    return false;
#endif
#endif
  return true;
}

void SymbolFilePDB::Initialize() {
  if (ShouldUseNativeReader()) {
    npdb::SymbolFileNativePDB::Initialize();
  } else {
    PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                  GetPluginDescriptionStatic(), CreateInstance,
                                  DebuggerInitialize);
  }
}

void SymbolFilePDB::Terminate() {
  if (ShouldUseNativeReader()) {
    npdb::SymbolFileNativePDB::Terminate();
  } else {
    PluginManager::UnregisterPlugin(CreateInstance);
  }
}

void SymbolFilePDB::DebuggerInitialize(lldb_private::Debugger &debugger) {}

llvm::StringRef SymbolFilePDB::GetPluginDescriptionStatic() {
  return "Microsoft PDB debug symbol file reader.";
}

lldb_private::SymbolFile *
SymbolFilePDB::CreateInstance(ObjectFileSP objfile_sp) {
  return new SymbolFilePDB(std::move(objfile_sp));
}

SymbolFilePDB::SymbolFilePDB(lldb::ObjectFileSP objfile_sp)
    : SymbolFileCommon(std::move(objfile_sp)), m_session_up(), m_global_scope_up() {}

SymbolFilePDB::~SymbolFilePDB() = default;

uint32_t SymbolFilePDB::CalculateAbilities() {
  uint32_t abilities = 0;
  if (!m_objfile_sp)
    return 0;

  if (!m_session_up) {
    // Lazily load and match the PDB file, but only do this once.
    std::string exePath = m_objfile_sp->GetFileSpec().GetPath();
    auto error = loadDataForEXE(PDB_ReaderType::DIA, llvm::StringRef(exePath),
                                m_session_up);
    if (error) {
      llvm::consumeError(std::move(error));
      auto module_sp = m_objfile_sp->GetModule();
      if (!module_sp)
        return 0;
      // See if any symbol file is specified through `--symfile` option.
      FileSpec symfile = module_sp->GetSymbolFileFileSpec();
      if (!symfile)
        return 0;
      error = loadDataForPDB(PDB_ReaderType::DIA,
                             llvm::StringRef(symfile.GetPath()), m_session_up);
      if (error) {
        llvm::consumeError(std::move(error));
        return 0;
      }
    }
  }
  if (!m_session_up)
    return 0;

  auto enum_tables_up = m_session_up->getEnumTables();
  if (!enum_tables_up)
    return 0;
  while (auto table_up = enum_tables_up->getNext()) {
    if (table_up->getItemCount() == 0)
      continue;
    auto type = table_up->getTableType();
    switch (type) {
    case PDB_TableType::Symbols:
      // This table represents a store of symbols with types listed in
      // PDBSym_Type
      abilities |= (CompileUnits | Functions | Blocks | GlobalVariables |
                    LocalVariables | VariableTypes);
      break;
    case PDB_TableType::LineNumbers:
      abilities |= LineTables;
      break;
    default:
      break;
    }
  }
  return abilities;
}

void SymbolFilePDB::InitializeObject() {
  lldb::addr_t obj_load_address =
      m_objfile_sp->GetBaseAddress().GetFileAddress();
  lldbassert(obj_load_address && obj_load_address != LLDB_INVALID_ADDRESS);
  m_session_up->setLoadAddress(obj_load_address);
  if (!m_global_scope_up)
    m_global_scope_up = m_session_up->getGlobalScope();
  lldbassert(m_global_scope_up.get());
}

uint32_t SymbolFilePDB::CalculateNumCompileUnits() {
  auto compilands = m_global_scope_up->findAllChildren<PDBSymbolCompiland>();
  if (!compilands)
    return 0;

  // The linker could link *.dll (compiland language = LINK), or import
  // *.dll. For example, a compiland with name `Import:KERNEL32.dll` could be
  // found as a child of the global scope (PDB executable). Usually, such
  // compilands contain `thunk` symbols in which we are not interested for
  // now. However we still count them in the compiland list. If we perform
  // any compiland related activity, like finding symbols through
  // llvm::pdb::IPDBSession methods, such compilands will all be searched
  // automatically no matter whether we include them or not.
  uint32_t compile_unit_count = compilands->getChildCount();

  // The linker can inject an additional "dummy" compilation unit into the
  // PDB. Ignore this special compile unit for our purposes, if it is there.
  // It is always the last one.
  auto last_compiland_up = compilands->getChildAtIndex(compile_unit_count - 1);
  lldbassert(last_compiland_up.get());
  std::string name = last_compiland_up->getName();
  if (name == "* Linker *")
    --compile_unit_count;
  return compile_unit_count;
}

void SymbolFilePDB::GetCompileUnitIndex(
    const llvm::pdb::PDBSymbolCompiland &pdb_compiland, uint32_t &index) {
  auto results_up = m_global_scope_up->findAllChildren<PDBSymbolCompiland>();
  if (!results_up)
    return;
  auto uid = pdb_compiland.getSymIndexId();
  for (uint32_t cu_idx = 0; cu_idx < GetNumCompileUnits(); ++cu_idx) {
    auto compiland_up = results_up->getChildAtIndex(cu_idx);
    if (!compiland_up)
      continue;
    if (compiland_up->getSymIndexId() == uid) {
      index = cu_idx;
      return;
    }
  }
  index = UINT32_MAX;
}

std::unique_ptr<llvm::pdb::PDBSymbolCompiland>
SymbolFilePDB::GetPDBCompilandByUID(uint32_t uid) {
  return m_session_up->getConcreteSymbolById<PDBSymbolCompiland>(uid);
}

lldb::CompUnitSP SymbolFilePDB::ParseCompileUnitAtIndex(uint32_t index) {
  if (index >= GetNumCompileUnits())
    return CompUnitSP();

  // Assuming we always retrieve same compilands listed in same order through
  // `PDBSymbolExe::findAllChildren` method, otherwise using `index` to get a
  // compile unit makes no sense.
  auto results = m_global_scope_up->findAllChildren<PDBSymbolCompiland>();
  if (!results)
    return CompUnitSP();
  auto compiland_up = results->getChildAtIndex(index);
  if (!compiland_up)
    return CompUnitSP();
  return ParseCompileUnitForUID(compiland_up->getSymIndexId(), index);
}

lldb::LanguageType SymbolFilePDB::ParseLanguage(CompileUnit &comp_unit) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  auto compiland_up = GetPDBCompilandByUID(comp_unit.GetID());
  if (!compiland_up)
    return lldb::eLanguageTypeUnknown;
  auto details = compiland_up->findOneChild<PDBSymbolCompilandDetails>();
  if (!details)
    return lldb::eLanguageTypeUnknown;
  return TranslateLanguage(details->getLanguage());
}

lldb_private::Function *
SymbolFilePDB::ParseCompileUnitFunctionForPDBFunc(const PDBSymbolFunc &pdb_func,
                                                  CompileUnit &comp_unit) {
  if (FunctionSP result = comp_unit.FindFunctionByUID(pdb_func.getSymIndexId()))
    return result.get();

  auto file_vm_addr = pdb_func.getVirtualAddress();
  if (file_vm_addr == LLDB_INVALID_ADDRESS || file_vm_addr == 0)
    return nullptr;

  auto func_length = pdb_func.getLength();
  AddressRange func_range =
      AddressRange(file_vm_addr, func_length,
                   GetObjectFile()->GetModule()->GetSectionList());
  if (!func_range.GetBaseAddress().IsValid())
    return nullptr;

  lldb_private::Type *func_type = ResolveTypeUID(pdb_func.getSymIndexId());
  if (!func_type)
    return nullptr;

  user_id_t func_type_uid = pdb_func.getSignatureId();

  Mangled mangled = GetMangledForPDBFunc(pdb_func);

  FunctionSP func_sp =
      std::make_shared<Function>(&comp_unit, pdb_func.getSymIndexId(),
                                 func_type_uid, mangled, func_type, func_range);

  comp_unit.AddFunction(func_sp);

  LanguageType lang = ParseLanguage(comp_unit);
  auto type_system_or_err = GetTypeSystemForLanguage(lang);
  if (auto err = type_system_or_err.takeError()) {
    LLDB_LOG_ERROR(GetLog(LLDBLog::Symbols), std::move(err),
                   "Unable to parse PDBFunc: {0}");
    return nullptr;
  }

  auto ts = *type_system_or_err;
  TypeSystemClang *clang_type_system =
    llvm::dyn_cast_or_null<TypeSystemClang>(ts.get());
  if (!clang_type_system)
    return nullptr;
  clang_type_system->GetPDBParser()->GetDeclForSymbol(pdb_func);

  return func_sp.get();
}

size_t SymbolFilePDB::ParseFunctions(CompileUnit &comp_unit) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  size_t func_added = 0;
  auto compiland_up = GetPDBCompilandByUID(comp_unit.GetID());
  if (!compiland_up)
    return 0;
  auto results_up = compiland_up->findAllChildren<PDBSymbolFunc>();
  if (!results_up)
    return 0;
  while (auto pdb_func_up = results_up->getNext()) {
    auto func_sp = comp_unit.FindFunctionByUID(pdb_func_up->getSymIndexId());
    if (!func_sp) {
      if (ParseCompileUnitFunctionForPDBFunc(*pdb_func_up, comp_unit))
        ++func_added;
    }
  }
  return func_added;
}

bool SymbolFilePDB::ParseLineTable(CompileUnit &comp_unit) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  if (comp_unit.GetLineTable())
    return true;
  return ParseCompileUnitLineTable(comp_unit, 0);
}

bool SymbolFilePDB::ParseDebugMacros(CompileUnit &comp_unit) {
  // PDB doesn't contain information about macros
  return false;
}

bool SymbolFilePDB::ParseSupportFiles(
    CompileUnit &comp_unit, lldb_private::SupportFileList &support_files) {

  // In theory this is unnecessary work for us, because all of this information
  // is easily (and quickly) accessible from DebugInfoPDB, so caching it a
  // second time seems like a waste.  Unfortunately, there's no good way around
  // this short of a moderate refactor since SymbolVendor depends on being able
  // to cache this list.
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  auto compiland_up = GetPDBCompilandByUID(comp_unit.GetID());
  if (!compiland_up)
    return false;
  auto files = m_session_up->getSourceFilesForCompiland(*compiland_up);
  if (!files || files->getChildCount() == 0)
    return false;

  while (auto file = files->getNext()) {
    FileSpec spec(file->getFileName(), FileSpec::Style::windows);
    support_files.AppendIfUnique(spec);
  }

  return true;
}

bool SymbolFilePDB::ParseImportedModules(
    const lldb_private::SymbolContext &sc,
    std::vector<SourceModule> &imported_modules) {
  // PDB does not yet support module debug info
  return false;
}

static size_t ParseFunctionBlocksForPDBSymbol(
    uint64_t func_file_vm_addr, const llvm::pdb::PDBSymbol *pdb_symbol,
    lldb_private::Block *parent_block, bool is_top_parent) {
  assert(pdb_symbol && parent_block);

  size_t num_added = 0;
  switch (pdb_symbol->getSymTag()) {
  case PDB_SymType::Block:
  case PDB_SymType::Function: {
    Block *block = nullptr;
    auto &raw_sym = pdb_symbol->getRawSymbol();
    if (auto *pdb_func = llvm::dyn_cast<PDBSymbolFunc>(pdb_symbol)) {
      if (pdb_func->hasNoInlineAttribute())
        break;
      if (is_top_parent)
        block = parent_block;
      else
        break;
    } else if (llvm::isa<PDBSymbolBlock>(pdb_symbol)) {
      auto uid = pdb_symbol->getSymIndexId();
      if (parent_block->FindBlockByID(uid))
        break;
      if (raw_sym.getVirtualAddress() < func_file_vm_addr)
        break;

      auto block_sp = std::make_shared<Block>(pdb_symbol->getSymIndexId());
      parent_block->AddChild(block_sp);
      block = block_sp.get();
    } else
      llvm_unreachable("Unexpected PDB symbol!");

    block->AddRange(Block::Range(
        raw_sym.getVirtualAddress() - func_file_vm_addr, raw_sym.getLength()));
    block->FinalizeRanges();
    ++num_added;

    auto results_up = pdb_symbol->findAllChildren();
    if (!results_up)
      break;
    while (auto symbol_up = results_up->getNext()) {
      num_added += ParseFunctionBlocksForPDBSymbol(
          func_file_vm_addr, symbol_up.get(), block, false);
    }
  } break;
  default:
    break;
  }
  return num_added;
}

size_t SymbolFilePDB::ParseBlocksRecursive(Function &func) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  size_t num_added = 0;
  auto uid = func.GetID();
  auto pdb_func_up = m_session_up->getConcreteSymbolById<PDBSymbolFunc>(uid);
  if (!pdb_func_up)
    return 0;
  Block &parent_block = func.GetBlock(false);
  num_added = ParseFunctionBlocksForPDBSymbol(
      pdb_func_up->getVirtualAddress(), pdb_func_up.get(), &parent_block, true);
  return num_added;
}

size_t SymbolFilePDB::ParseTypes(CompileUnit &comp_unit) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());

  size_t num_added = 0;
  auto compiland = GetPDBCompilandByUID(comp_unit.GetID());
  if (!compiland)
    return 0;

  auto ParseTypesByTagFn = [&num_added, this](const PDBSymbol &raw_sym) {
    std::unique_ptr<IPDBEnumSymbols> results;
    PDB_SymType tags_to_search[] = {PDB_SymType::Enum, PDB_SymType::Typedef,
                                    PDB_SymType::UDT};
    for (auto tag : tags_to_search) {
      results = raw_sym.findAllChildren(tag);
      if (!results || results->getChildCount() == 0)
        continue;
      while (auto symbol = results->getNext()) {
        switch (symbol->getSymTag()) {
        case PDB_SymType::Enum:
        case PDB_SymType::UDT:
        case PDB_SymType::Typedef:
          break;
        default:
          continue;
        }

        // This should cause the type to get cached and stored in the `m_types`
        // lookup.
        if (auto type = ResolveTypeUID(symbol->getSymIndexId())) {
          // Resolve the type completely to avoid a completion
          // (and so a list change, which causes an iterators invalidation)
          // during a TypeList dumping
          type->GetFullCompilerType();
          ++num_added;
        }
      }
    }
  };

  ParseTypesByTagFn(*compiland);

  // Also parse global types particularly coming from this compiland.
  // Unfortunately, PDB has no compiland information for each global type. We
  // have to parse them all. But ensure we only do this once.
  static bool parse_all_global_types = false;
  if (!parse_all_global_types) {
    ParseTypesByTagFn(*m_global_scope_up);
    parse_all_global_types = true;
  }
  return num_added;
}

size_t
SymbolFilePDB::ParseVariablesForContext(const lldb_private::SymbolContext &sc) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  if (!sc.comp_unit)
    return 0;

  size_t num_added = 0;
  if (sc.function) {
    auto pdb_func = m_session_up->getConcreteSymbolById<PDBSymbolFunc>(
        sc.function->GetID());
    if (!pdb_func)
      return 0;

    num_added += ParseVariables(sc, *pdb_func);
    sc.function->GetBlock(false).SetDidParseVariables(true, true);
  } else if (sc.comp_unit) {
    auto compiland = GetPDBCompilandByUID(sc.comp_unit->GetID());
    if (!compiland)
      return 0;

    if (sc.comp_unit->GetVariableList(false))
      return 0;

    auto results = m_global_scope_up->findAllChildren<PDBSymbolData>();
    if (results && results->getChildCount()) {
      while (auto result = results->getNext()) {
        auto cu_id = GetCompilandId(*result);
        // FIXME: We are not able to determine variable's compile unit.
        if (cu_id == 0)
          continue;

        if (cu_id == sc.comp_unit->GetID())
          num_added += ParseVariables(sc, *result);
      }
    }

    // FIXME: A `file static` or `global constant` variable appears both in
    // compiland's children and global scope's children with unexpectedly
    // different symbol's Id making it ambiguous.

    // FIXME: 'local constant', for example, const char var[] = "abc", declared
    // in a function scope, can't be found in PDB.

    // Parse variables in this compiland.
    num_added += ParseVariables(sc, *compiland);
  }

  return num_added;
}

lldb_private::Type *SymbolFilePDB::ResolveTypeUID(lldb::user_id_t type_uid) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  auto find_result = m_types.find(type_uid);
  if (find_result != m_types.end())
    return find_result->second.get();

  auto type_system_or_err =
      GetTypeSystemForLanguage(lldb::eLanguageTypeC_plus_plus);
  if (auto err = type_system_or_err.takeError()) {
    LLDB_LOG_ERROR(GetLog(LLDBLog::Symbols), std::move(err),
                   "Unable to ResolveTypeUID: {0}");
    return nullptr;
  }

  auto ts = *type_system_or_err;
  TypeSystemClang *clang_type_system =
      llvm::dyn_cast_or_null<TypeSystemClang>(ts.get());
  if (!clang_type_system)
    return nullptr;
  PDBASTParser *pdb = clang_type_system->GetPDBParser();
  if (!pdb)
    return nullptr;

  auto pdb_type = m_session_up->getSymbolById(type_uid);
  if (pdb_type == nullptr)
    return nullptr;

  lldb::TypeSP result = pdb->CreateLLDBTypeFromPDBType(*pdb_type);
  if (result) {
    m_types.insert(std::make_pair(type_uid, result));
  }
  return result.get();
}

std::optional<SymbolFile::ArrayInfo> SymbolFilePDB::GetDynamicArrayInfoForUID(
    lldb::user_id_t type_uid, const lldb_private::ExecutionContext *exe_ctx) {
  return std::nullopt;
}

bool SymbolFilePDB::CompleteType(lldb_private::CompilerType &compiler_type) {
  std::lock_guard<std::recursive_mutex> guard(
      GetObjectFile()->GetModule()->GetMutex());

  auto type_system_or_err =
      GetTypeSystemForLanguage(lldb::eLanguageTypeC_plus_plus);
  if (auto err = type_system_or_err.takeError()) {
    LLDB_LOG_ERROR(GetLog(LLDBLog::Symbols), std::move(err),
                   "Unable to get dynamic array info for UID: {0}");
    return false;
  }
  auto ts = *type_system_or_err;
  TypeSystemClang *clang_ast_ctx =
      llvm::dyn_cast_or_null<TypeSystemClang>(ts.get());

  if (!clang_ast_ctx)
    return false;

  PDBASTParser *pdb = clang_ast_ctx->GetPDBParser();
  if (!pdb)
    return false;

  return pdb->CompleteTypeFromPDB(compiler_type);
}

lldb_private::CompilerDecl SymbolFilePDB::GetDeclForUID(lldb::user_id_t uid) {
  auto type_system_or_err =
      GetTypeSystemForLanguage(lldb::eLanguageTypeC_plus_plus);
  if (auto err = type_system_or_err.takeError()) {
    LLDB_LOG_ERROR(GetLog(LLDBLog::Symbols), std::move(err),
                   "Unable to get decl for UID: {0}");
    return CompilerDecl();
  }
  auto ts = *type_system_or_err;
  TypeSystemClang *clang_ast_ctx =
      llvm::dyn_cast_or_null<TypeSystemClang>(ts.get());
  if (!clang_ast_ctx)
    return CompilerDecl();

  PDBASTParser *pdb = clang_ast_ctx->GetPDBParser();
  if (!pdb)
    return CompilerDecl();

  auto symbol = m_session_up->getSymbolById(uid);
  if (!symbol)
    return CompilerDecl();

  auto decl = pdb->GetDeclForSymbol(*symbol);
  if (!decl)
    return CompilerDecl();

  return clang_ast_ctx->GetCompilerDecl(decl);
}

lldb_private::CompilerDeclContext
SymbolFilePDB::GetDeclContextForUID(lldb::user_id_t uid) {
  auto type_system_or_err =
      GetTypeSystemForLanguage(lldb::eLanguageTypeC_plus_plus);
  if (auto err = type_system_or_err.takeError()) {
    LLDB_LOG_ERROR(GetLog(LLDBLog::Symbols), std::move(err),
                   "Unable to get DeclContext for UID: {0}");
    return CompilerDeclContext();
  }

  auto ts = *type_system_or_err;
  TypeSystemClang *clang_ast_ctx =
      llvm::dyn_cast_or_null<TypeSystemClang>(ts.get());
  if (!clang_ast_ctx)
    return CompilerDeclContext();

  PDBASTParser *pdb = clang_ast_ctx->GetPDBParser();
  if (!pdb)
    return CompilerDeclContext();

  auto symbol = m_session_up->getSymbolById(uid);
  if (!symbol)
    return CompilerDeclContext();

  auto decl_context = pdb->GetDeclContextForSymbol(*symbol);
  if (!decl_context)
    return GetDeclContextContainingUID(uid);

  return clang_ast_ctx->CreateDeclContext(decl_context);
}

lldb_private::CompilerDeclContext
SymbolFilePDB::GetDeclContextContainingUID(lldb::user_id_t uid) {
  auto type_system_or_err =
      GetTypeSystemForLanguage(lldb::eLanguageTypeC_plus_plus);
  if (auto err = type_system_or_err.takeError()) {
    LLDB_LOG_ERROR(GetLog(LLDBLog::Symbols), std::move(err),
                   "Unable to get DeclContext containing UID: {0}");
    return CompilerDeclContext();
  }

  auto ts = *type_system_or_err;
  TypeSystemClang *clang_ast_ctx =
      llvm::dyn_cast_or_null<TypeSystemClang>(ts.get());
  if (!clang_ast_ctx)
    return CompilerDeclContext();

  PDBASTParser *pdb = clang_ast_ctx->GetPDBParser();
  if (!pdb)
    return CompilerDeclContext();

  auto symbol = m_session_up->getSymbolById(uid);
  if (!symbol)
    return CompilerDeclContext();

  auto decl_context = pdb->GetDeclContextContainingSymbol(*symbol);
  assert(decl_context);

  return clang_ast_ctx->CreateDeclContext(decl_context);
}

void SymbolFilePDB::ParseDeclsForContext(
    lldb_private::CompilerDeclContext decl_ctx) {
  auto type_system_or_err =
      GetTypeSystemForLanguage(lldb::eLanguageTypeC_plus_plus);
  if (auto err = type_system_or_err.takeError()) {
    LLDB_LOG_ERROR(GetLog(LLDBLog::Symbols), std::move(err),
                   "Unable to parse decls for context: {0}");
    return;
  }

  auto ts = *type_system_or_err;
  TypeSystemClang *clang_ast_ctx =
      llvm::dyn_cast_or_null<TypeSystemClang>(ts.get());
  if (!clang_ast_ctx)
    return;

  PDBASTParser *pdb = clang_ast_ctx->GetPDBParser();
  if (!pdb)
    return;

  pdb->ParseDeclsForDeclContext(
      static_cast<clang::DeclContext *>(decl_ctx.GetOpaqueDeclContext()));
}

uint32_t
SymbolFilePDB::ResolveSymbolContext(const lldb_private::Address &so_addr,
                                    SymbolContextItem resolve_scope,
                                    lldb_private::SymbolContext &sc) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  uint32_t resolved_flags = 0;
  if (resolve_scope & eSymbolContextCompUnit ||
      resolve_scope & eSymbolContextVariable ||
      resolve_scope & eSymbolContextFunction ||
      resolve_scope & eSymbolContextBlock ||
      resolve_scope & eSymbolContextLineEntry) {
    auto cu_sp = GetCompileUnitContainsAddress(so_addr);
    if (!cu_sp) {
      if (resolved_flags & eSymbolContextVariable) {
        // TODO: Resolve variables
      }
      return 0;
    }
    sc.comp_unit = cu_sp.get();
    resolved_flags |= eSymbolContextCompUnit;
    lldbassert(sc.module_sp == cu_sp->GetModule());
  }

  if (resolve_scope & eSymbolContextFunction ||
      resolve_scope & eSymbolContextBlock) {
    addr_t file_vm_addr = so_addr.GetFileAddress();
    auto symbol_up =
        m_session_up->findSymbolByAddress(file_vm_addr, PDB_SymType::Function);
    if (symbol_up) {
      auto *pdb_func = llvm::dyn_cast<PDBSymbolFunc>(symbol_up.get());
      assert(pdb_func);
      auto func_uid = pdb_func->getSymIndexId();
      sc.function = sc.comp_unit->FindFunctionByUID(func_uid).get();
      if (sc.function == nullptr)
        sc.function =
            ParseCompileUnitFunctionForPDBFunc(*pdb_func, *sc.comp_unit);
      if (sc.function) {
        resolved_flags |= eSymbolContextFunction;
        if (resolve_scope & eSymbolContextBlock) {
          auto block_symbol = m_session_up->findSymbolByAddress(
              file_vm_addr, PDB_SymType::Block);
          auto block_id = block_symbol ? block_symbol->getSymIndexId()
                                       : sc.function->GetID();
          sc.block = sc.function->GetBlock(true).FindBlockByID(block_id);
          if (sc.block)
            resolved_flags |= eSymbolContextBlock;
        }
      }
    }
  }

  if (resolve_scope & eSymbolContextLineEntry) {
    if (auto *line_table = sc.comp_unit->GetLineTable()) {
      Address addr(so_addr);
      if (line_table->FindLineEntryByAddress(addr, sc.line_entry))
        resolved_flags |= eSymbolContextLineEntry;
    }
  }

  return resolved_flags;
}

uint32_t SymbolFilePDB::ResolveSymbolContext(
    const lldb_private::SourceLocationSpec &src_location_spec,
    SymbolContextItem resolve_scope, lldb_private::SymbolContextList &sc_list) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  const size_t old_size = sc_list.GetSize();
  const FileSpec &file_spec = src_location_spec.GetFileSpec();
  const uint32_t line = src_location_spec.GetLine().value_or(0);
  if (resolve_scope & lldb::eSymbolContextCompUnit) {
    // Locate all compilation units with line numbers referencing the specified
    // file.  For example, if `file_spec` is <vector>, then this should return
    // all source files and header files that reference <vector>, either
    // directly or indirectly.
    auto compilands = m_session_up->findCompilandsForSourceFile(
        file_spec.GetPath(), PDB_NameSearchFlags::NS_CaseInsensitive);

    if (!compilands)
      return 0;

    // For each one, either find its previously parsed data or parse it afresh
    // and add it to the symbol context list.
    while (auto compiland = compilands->getNext()) {
      // If we're not checking inlines, then don't add line information for
      // this file unless the FileSpec matches. For inline functions, we don't
      // have to match the FileSpec since they could be defined in headers
      // other than file specified in FileSpec.
      if (!src_location_spec.GetCheckInlines()) {
        std::string source_file = compiland->getSourceFileFullPath();
        if (source_file.empty())
          continue;
        FileSpec this_spec(source_file, FileSpec::Style::windows);
        bool need_full_match = !file_spec.GetDirectory().IsEmpty();
        if (FileSpec::Compare(file_spec, this_spec, need_full_match) != 0)
          continue;
      }

      SymbolContext sc;
      auto cu = ParseCompileUnitForUID(compiland->getSymIndexId());
      if (!cu)
        continue;
      sc.comp_unit = cu.get();
      sc.module_sp = cu->GetModule();

      // If we were asked to resolve line entries, add all entries to the line
      // table that match the requested line (or all lines if `line` == 0).
      if (resolve_scope & (eSymbolContextFunction | eSymbolContextBlock |
                           eSymbolContextLineEntry)) {
        bool has_line_table = ParseCompileUnitLineTable(*sc.comp_unit, line);

        if ((resolve_scope & eSymbolContextLineEntry) && !has_line_table) {
          // The query asks for line entries, but we can't get them for the
          // compile unit. This is not normal for `line` = 0. So just assert
          // it.
          assert(line && "Couldn't get all line entries!\n");

          // Current compiland does not have the requested line. Search next.
          continue;
        }

        if (resolve_scope & (eSymbolContextFunction | eSymbolContextBlock)) {
          if (!has_line_table)
            continue;

          auto *line_table = sc.comp_unit->GetLineTable();
          lldbassert(line_table);

          uint32_t num_line_entries = line_table->GetSize();
          // Skip the terminal line entry.
          --num_line_entries;

          // If `line `!= 0, see if we can resolve function for each line entry
          // in the line table.
          for (uint32_t line_idx = 0; line && line_idx < num_line_entries;
               ++line_idx) {
            if (!line_table->GetLineEntryAtIndex(line_idx, sc.line_entry))
              continue;

            auto file_vm_addr =
                sc.line_entry.range.GetBaseAddress().GetFileAddress();
            if (file_vm_addr == LLDB_INVALID_ADDRESS || file_vm_addr == 0)
              continue;

            auto symbol_up = m_session_up->findSymbolByAddress(
                file_vm_addr, PDB_SymType::Function);
            if (symbol_up) {
              auto func_uid = symbol_up->getSymIndexId();
              sc.function = sc.comp_unit->FindFunctionByUID(func_uid).get();
              if (sc.function == nullptr) {
                auto pdb_func = llvm::dyn_cast<PDBSymbolFunc>(symbol_up.get());
                assert(pdb_func);
                sc.function = ParseCompileUnitFunctionForPDBFunc(*pdb_func,
                                                                 *sc.comp_unit);
              }
              if (sc.function && (resolve_scope & eSymbolContextBlock)) {
                Block &block = sc.function->GetBlock(true);
                sc.block = block.FindBlockByID(sc.function->GetID());
              }
            }
            sc_list.Append(sc);
          }
        } else if (has_line_table) {
          // We can parse line table for the compile unit. But no query to
          // resolve function or block. We append `sc` to the list anyway.
          sc_list.Append(sc);
        }
      } else {
        // No query for line entry, function or block. But we have a valid
        // compile unit, append `sc` to the list.
        sc_list.Append(sc);
      }
    }
  }
  return sc_list.GetSize() - old_size;
}

std::string SymbolFilePDB::GetMangledForPDBData(const PDBSymbolData &pdb_data) {
  // Cache public names at first
  if (m_public_names.empty())
    if (auto result_up =
            m_global_scope_up->findAllChildren(PDB_SymType::PublicSymbol))
      while (auto symbol_up = result_up->getNext())
        if (auto addr = symbol_up->getRawSymbol().getVirtualAddress())
          m_public_names[addr] = symbol_up->getRawSymbol().getName();

  // Look up the name in the cache
  return m_public_names.lookup(pdb_data.getVirtualAddress());
}

VariableSP SymbolFilePDB::ParseVariableForPDBData(
    const lldb_private::SymbolContext &sc,
    const llvm::pdb::PDBSymbolData &pdb_data) {
  VariableSP var_sp;
  uint32_t var_uid = pdb_data.getSymIndexId();
  auto result = m_variables.find(var_uid);
  if (result != m_variables.end())
    return result->second;

  ValueType scope = eValueTypeInvalid;
  bool is_static_member = false;
  bool is_external = false;
  bool is_artificial = false;

  switch (pdb_data.getDataKind()) {
  case PDB_DataKind::Global:
    scope = eValueTypeVariableGlobal;
    is_external = true;
    break;
  case PDB_DataKind::Local:
    scope = eValueTypeVariableLocal;
    break;
  case PDB_DataKind::FileStatic:
    scope = eValueTypeVariableStatic;
    break;
  case PDB_DataKind::StaticMember:
    is_static_member = true;
    scope = eValueTypeVariableStatic;
    break;
  case PDB_DataKind::Member:
    scope = eValueTypeVariableStatic;
    break;
  case PDB_DataKind::Param:
    scope = eValueTypeVariableArgument;
    break;
  case PDB_DataKind::Constant:
    scope = eValueTypeConstResult;
    break;
  default:
    break;
  }

  switch (pdb_data.getLocationType()) {
  case PDB_LocType::TLS:
    scope = eValueTypeVariableThreadLocal;
    break;
  case PDB_LocType::RegRel: {
    // It is a `this` pointer.
    if (pdb_data.getDataKind() == PDB_DataKind::ObjectPtr) {
      scope = eValueTypeVariableArgument;
      is_artificial = true;
    }
  } break;
  default:
    break;
  }

  Declaration decl;
  if (!is_artificial && !pdb_data.isCompilerGenerated()) {
    if (auto lines = pdb_data.getLineNumbers()) {
      if (auto first_line = lines->getNext()) {
        uint32_t src_file_id = first_line->getSourceFileId();
        auto src_file = m_session_up->getSourceFileById(src_file_id);
        if (src_file) {
          FileSpec spec(src_file->getFileName());
          decl.SetFile(spec);
          decl.SetColumn(first_line->getColumnNumber());
          decl.SetLine(first_line->getLineNumber());
        }
      }
    }
  }

  Variable::RangeList ranges;
  SymbolContextScope *context_scope = sc.comp_unit;
  if (scope == eValueTypeVariableLocal || scope == eValueTypeVariableArgument) {
    if (sc.function) {
      Block &function_block = sc.function->GetBlock(true);
      Block *block =
          function_block.FindBlockByID(pdb_data.getLexicalParentId());
      if (!block)
        block = &function_block;

      context_scope = block;

      for (size_t i = 0, num_ranges = block->GetNumRanges(); i < num_ranges;
           ++i) {
        AddressRange range;
        if (!block->GetRangeAtIndex(i, range))
          continue;

        ranges.Append(range.GetBaseAddress().GetFileAddress(),
                      range.GetByteSize());
      }
    }
  }

  SymbolFileTypeSP type_sp =
      std::make_shared<SymbolFileType>(*this, pdb_data.getTypeId());

  auto var_name = pdb_data.getName();
  auto mangled = GetMangledForPDBData(pdb_data);
  auto mangled_cstr = mangled.empty() ? nullptr : mangled.c_str();

  bool is_constant;
  ModuleSP module_sp = GetObjectFile()->GetModule();
  DWARFExpressionList location(module_sp,
                               ConvertPDBLocationToDWARFExpression(
                                   module_sp, pdb_data, ranges, is_constant),
                               nullptr);

  var_sp = std::make_shared<Variable>(
      var_uid, var_name.c_str(), mangled_cstr, type_sp, scope, context_scope,
      ranges, &decl, location, is_external, is_artificial, is_constant,
      is_static_member);

  m_variables.insert(std::make_pair(var_uid, var_sp));
  return var_sp;
}

size_t
SymbolFilePDB::ParseVariables(const lldb_private::SymbolContext &sc,
                              const llvm::pdb::PDBSymbol &pdb_symbol,
                              lldb_private::VariableList *variable_list) {
  size_t num_added = 0;

  if (auto pdb_data = llvm::dyn_cast<PDBSymbolData>(&pdb_symbol)) {
    VariableListSP local_variable_list_sp;

    auto result = m_variables.find(pdb_data->getSymIndexId());
    if (result != m_variables.end()) {
      if (variable_list)
        variable_list->AddVariableIfUnique(result->second);
    } else {
      // Prepare right VariableList for this variable.
      if (auto lexical_parent = pdb_data->getLexicalParent()) {
        switch (lexical_parent->getSymTag()) {
        case PDB_SymType::Exe:
          assert(sc.comp_unit);
          [[fallthrough]];
        case PDB_SymType::Compiland: {
          if (sc.comp_unit) {
            local_variable_list_sp = sc.comp_unit->GetVariableList(false);
            if (!local_variable_list_sp) {
              local_variable_list_sp = std::make_shared<VariableList>();
              sc.comp_unit->SetVariableList(local_variable_list_sp);
            }
          }
        } break;
        case PDB_SymType::Block:
        case PDB_SymType::Function: {
          if (sc.function) {
            Block *block = sc.function->GetBlock(true).FindBlockByID(
                lexical_parent->getSymIndexId());
            if (block) {
              local_variable_list_sp = block->GetBlockVariableList(false);
              if (!local_variable_list_sp) {
                local_variable_list_sp = std::make_shared<VariableList>();
                block->SetVariableList(local_variable_list_sp);
              }
            }
          }
        } break;
        default:
          break;
        }
      }

      if (local_variable_list_sp) {
        if (auto var_sp = ParseVariableForPDBData(sc, *pdb_data)) {
          local_variable_list_sp->AddVariableIfUnique(var_sp);
          if (variable_list)
            variable_list->AddVariableIfUnique(var_sp);
          ++num_added;
          PDBASTParser *ast = GetPDBAstParser();
          if (ast)
            ast->GetDeclForSymbol(*pdb_data);
        }
      }
    }
  }

  if (auto results = pdb_symbol.findAllChildren()) {
    while (auto result = results->getNext())
      num_added += ParseVariables(sc, *result, variable_list);
  }

  return num_added;
}

void SymbolFilePDB::FindGlobalVariables(
    lldb_private::ConstString name, const CompilerDeclContext &parent_decl_ctx,
    uint32_t max_matches, lldb_private::VariableList &variables) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  if (!DeclContextMatchesThisSymbolFile(parent_decl_ctx))
    return;
  if (name.IsEmpty())
    return;

  auto results = m_global_scope_up->findAllChildren<PDBSymbolData>();
  if (!results)
    return;

  uint32_t matches = 0;
  size_t old_size = variables.GetSize();
  while (auto result = results->getNext()) {
    auto pdb_data = llvm::dyn_cast<PDBSymbolData>(result.get());
    if (max_matches > 0 && matches >= max_matches)
      break;

    SymbolContext sc;
    sc.module_sp = m_objfile_sp->GetModule();
    lldbassert(sc.module_sp.get());

    if (name.GetStringRef() !=
        MSVCUndecoratedNameParser::DropScope(pdb_data->getName()))
      continue;

    sc.comp_unit = ParseCompileUnitForUID(GetCompilandId(*pdb_data)).get();
    // FIXME: We are not able to determine the compile unit.
    if (sc.comp_unit == nullptr)
      continue;

    if (parent_decl_ctx.IsValid() &&
        GetDeclContextContainingUID(result->getSymIndexId()) != parent_decl_ctx)
      continue;

    ParseVariables(sc, *pdb_data, &variables);
    matches = variables.GetSize() - old_size;
  }
}

void SymbolFilePDB::FindGlobalVariables(
    const lldb_private::RegularExpression &regex, uint32_t max_matches,
    lldb_private::VariableList &variables) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  if (!regex.IsValid())
    return;
  auto results = m_global_scope_up->findAllChildren<PDBSymbolData>();
  if (!results)
    return;

  uint32_t matches = 0;
  size_t old_size = variables.GetSize();
  while (auto pdb_data = results->getNext()) {
    if (max_matches > 0 && matches >= max_matches)
      break;

    auto var_name = pdb_data->getName();
    if (var_name.empty())
      continue;
    if (!regex.Execute(var_name))
      continue;
    SymbolContext sc;
    sc.module_sp = m_objfile_sp->GetModule();
    lldbassert(sc.module_sp.get());

    sc.comp_unit = ParseCompileUnitForUID(GetCompilandId(*pdb_data)).get();
    // FIXME: We are not able to determine the compile unit.
    if (sc.comp_unit == nullptr)
      continue;

    ParseVariables(sc, *pdb_data, &variables);
    matches = variables.GetSize() - old_size;
  }
}

bool SymbolFilePDB::ResolveFunction(const llvm::pdb::PDBSymbolFunc &pdb_func,
                                    bool include_inlines,
                                    lldb_private::SymbolContextList &sc_list) {
  lldb_private::SymbolContext sc;
  sc.comp_unit = ParseCompileUnitForUID(pdb_func.getCompilandId()).get();
  if (!sc.comp_unit)
    return false;
  sc.module_sp = sc.comp_unit->GetModule();
  sc.function = ParseCompileUnitFunctionForPDBFunc(pdb_func, *sc.comp_unit);
  if (!sc.function)
    return false;

  sc_list.Append(sc);
  return true;
}

bool SymbolFilePDB::ResolveFunction(uint32_t uid, bool include_inlines,
                                    lldb_private::SymbolContextList &sc_list) {
  auto pdb_func_up = m_session_up->getConcreteSymbolById<PDBSymbolFunc>(uid);
  if (!pdb_func_up && !(include_inlines && pdb_func_up->hasInlineAttribute()))
    return false;
  return ResolveFunction(*pdb_func_up, include_inlines, sc_list);
}

void SymbolFilePDB::CacheFunctionNames() {
  if (!m_func_full_names.IsEmpty())
    return;

  std::map<uint64_t, uint32_t> addr_ids;

  if (auto results_up = m_global_scope_up->findAllChildren<PDBSymbolFunc>()) {
    while (auto pdb_func_up = results_up->getNext()) {
      if (pdb_func_up->isCompilerGenerated())
        continue;

      auto name = pdb_func_up->getName();
      auto demangled_name = pdb_func_up->getUndecoratedName();
      if (name.empty() && demangled_name.empty())
        continue;

      auto uid = pdb_func_up->getSymIndexId();
      if (!demangled_name.empty() && pdb_func_up->getVirtualAddress())
        addr_ids.insert(std::make_pair(pdb_func_up->getVirtualAddress(), uid));

      if (auto parent = pdb_func_up->getClassParent()) {

        // PDB have symbols for class/struct methods or static methods in Enum
        // Class. We won't bother to check if the parent is UDT or Enum here.
        m_func_method_names.Append(ConstString(name), uid);

        // To search a method name, like NS::Class:MemberFunc, LLDB searches
        // its base name, i.e. MemberFunc by default. Since PDBSymbolFunc does
        // not have information of this, we extract base names and cache them
        // by our own effort.
        llvm::StringRef basename = MSVCUndecoratedNameParser::DropScope(name);
        if (!basename.empty())
          m_func_base_names.Append(ConstString(basename), uid);
        else {
          m_func_base_names.Append(ConstString(name), uid);
        }

        if (!demangled_name.empty())
          m_func_full_names.Append(ConstString(demangled_name), uid);

      } else {
        // Handle not-method symbols.

        // The function name might contain namespace, or its lexical scope.
        llvm::StringRef basename = MSVCUndecoratedNameParser::DropScope(name);
        if (!basename.empty())
          m_func_base_names.Append(ConstString(basename), uid);
        else
          m_func_base_names.Append(ConstString(name), uid);

        if (name == "main") {
          m_func_full_names.Append(ConstString(name), uid);

          if (!demangled_name.empty() && name != demangled_name) {
            m_func_full_names.Append(ConstString(demangled_name), uid);
            m_func_base_names.Append(ConstString(demangled_name), uid);
          }
        } else if (!demangled_name.empty()) {
          m_func_full_names.Append(ConstString(demangled_name), uid);
        } else {
          m_func_full_names.Append(ConstString(name), uid);
        }
      }
    }
  }

  if (auto results_up =
          m_global_scope_up->findAllChildren<PDBSymbolPublicSymbol>()) {
    while (auto pub_sym_up = results_up->getNext()) {
      if (!pub_sym_up->isFunction())
        continue;
      auto name = pub_sym_up->getName();
      if (name.empty())
        continue;

      if (CPlusPlusLanguage::IsCPPMangledName(name.c_str())) {
        auto vm_addr = pub_sym_up->getVirtualAddress();

        // PDB public symbol has mangled name for its associated function.
        if (vm_addr && addr_ids.find(vm_addr) != addr_ids.end()) {
          // Cache mangled name.
          m_func_full_names.Append(ConstString(name), addr_ids[vm_addr]);
        }
      }
    }
  }
  // Sort them before value searching is working properly
  m_func_full_names.Sort();
  m_func_full_names.SizeToFit();
  m_func_method_names.Sort();
  m_func_method_names.SizeToFit();
  m_func_base_names.Sort();
  m_func_base_names.SizeToFit();
}

void SymbolFilePDB::FindFunctions(
    const lldb_private::Module::LookupInfo &lookup_info,
    const lldb_private::CompilerDeclContext &parent_decl_ctx,
    bool include_inlines,
    lldb_private::SymbolContextList &sc_list) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  ConstString name = lookup_info.GetLookupName();
  FunctionNameType name_type_mask = lookup_info.GetNameTypeMask();
  lldbassert((name_type_mask & eFunctionNameTypeAuto) == 0);

  if (name_type_mask & eFunctionNameTypeFull)
    name = lookup_info.GetName();

  if (name_type_mask == eFunctionNameTypeNone)
    return;
  if (!DeclContextMatchesThisSymbolFile(parent_decl_ctx))
    return;
  if (name.IsEmpty())
    return;

  if (name_type_mask & eFunctionNameTypeFull ||
      name_type_mask & eFunctionNameTypeBase ||
      name_type_mask & eFunctionNameTypeMethod) {
    CacheFunctionNames();

    std::set<uint32_t> resolved_ids;
    auto ResolveFn = [this, &name, parent_decl_ctx, include_inlines, &sc_list,
                      &resolved_ids](UniqueCStringMap<uint32_t> &Names) {
      std::vector<uint32_t> ids;
      if (!Names.GetValues(name, ids))
        return;

      for (uint32_t id : ids) {
        if (resolved_ids.find(id) != resolved_ids.end())
          continue;

        if (parent_decl_ctx.IsValid() &&
            GetDeclContextContainingUID(id) != parent_decl_ctx)
          continue;

        if (ResolveFunction(id, include_inlines, sc_list))
          resolved_ids.insert(id);
      }
    };
    if (name_type_mask & eFunctionNameTypeFull) {
      ResolveFn(m_func_full_names);
      ResolveFn(m_func_base_names);
      ResolveFn(m_func_method_names);
    }
    if (name_type_mask & eFunctionNameTypeBase)
      ResolveFn(m_func_base_names);
    if (name_type_mask & eFunctionNameTypeMethod)
      ResolveFn(m_func_method_names);
  }
}

void SymbolFilePDB::FindFunctions(const lldb_private::RegularExpression &regex,
                                  bool include_inlines,
                                  lldb_private::SymbolContextList &sc_list) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  if (!regex.IsValid())
    return;

  CacheFunctionNames();

  std::set<uint32_t> resolved_ids;
  auto ResolveFn = [&regex, include_inlines, &sc_list, &resolved_ids,
                    this](UniqueCStringMap<uint32_t> &Names) {
    std::vector<uint32_t> ids;
    if (Names.GetValues(regex, ids)) {
      for (auto id : ids) {
        if (resolved_ids.find(id) == resolved_ids.end())
          if (ResolveFunction(id, include_inlines, sc_list))
            resolved_ids.insert(id);
      }
    }
  };
  ResolveFn(m_func_full_names);
  ResolveFn(m_func_base_names);
}

void SymbolFilePDB::GetMangledNamesForFunction(
    const std::string &scope_qualified_name,
    std::vector<lldb_private::ConstString> &mangled_names) {}

void SymbolFilePDB::AddSymbols(lldb_private::Symtab &symtab) {
  std::set<lldb::addr_t> sym_addresses;
  for (size_t i = 0; i < symtab.GetNumSymbols(); i++)
    sym_addresses.insert(symtab.SymbolAtIndex(i)->GetFileAddress());

  auto results = m_global_scope_up->findAllChildren<PDBSymbolPublicSymbol>();
  if (!results)
    return;

  auto section_list = m_objfile_sp->GetSectionList();
  if (!section_list)
    return;

  while (auto pub_symbol = results->getNext()) {
    auto section_id = pub_symbol->getAddressSection();

    auto section = section_list->FindSectionByID(section_id);
    if (!section)
      continue;

    auto offset = pub_symbol->getAddressOffset();

    auto file_addr = section->GetFileAddress() + offset;
    if (sym_addresses.find(file_addr) != sym_addresses.end())
      continue;
    sym_addresses.insert(file_addr);

    auto size = pub_symbol->getLength();
    symtab.AddSymbol(
        Symbol(pub_symbol->getSymIndexId(),   // symID
               pub_symbol->getName().c_str(), // name
               pub_symbol->isCode() ? eSymbolTypeCode : eSymbolTypeData, // type
               true,      // external
               false,     // is_debug
               false,     // is_trampoline
               false,     // is_artificial
               section,   // section_sp
               offset,    // value
               size,      // size
               size != 0, // size_is_valid
               false,     // contains_linker_annotations
               0          // flags
               ));
  }

  symtab.Finalize();
}

void SymbolFilePDB::DumpClangAST(Stream &s) {
  auto type_system_or_err =
      GetTypeSystemForLanguage(lldb::eLanguageTypeC_plus_plus);
  if (auto err = type_system_or_err.takeError()) {
    LLDB_LOG_ERROR(GetLog(LLDBLog::Symbols), std::move(err),
                   "Unable to dump ClangAST: {0}");
    return;
  }

  auto ts = *type_system_or_err;
  TypeSystemClang *clang_type_system =
      llvm::dyn_cast_or_null<TypeSystemClang>(ts.get());
  if (!clang_type_system)
    return;
  clang_type_system->Dump(s.AsRawOstream());
}

void SymbolFilePDB::FindTypesByRegex(
    const lldb_private::RegularExpression &regex, uint32_t max_matches,
    lldb_private::TypeMap &types) {
  // When searching by regex, we need to go out of our way to limit the search
  // space as much as possible since this searches EVERYTHING in the PDB,
  // manually doing regex comparisons.  PDB library isn't optimized for regex
  // searches or searches across multiple symbol types at the same time, so the
  // best we can do is to search enums, then typedefs, then classes one by one,
  // and do a regex comparison against each of them.
  PDB_SymType tags_to_search[] = {PDB_SymType::Enum, PDB_SymType::Typedef,
                                  PDB_SymType::UDT};
  std::unique_ptr<IPDBEnumSymbols> results;

  uint32_t matches = 0;

  for (auto tag : tags_to_search) {
    results = m_global_scope_up->findAllChildren(tag);
    if (!results)
      continue;

    while (auto result = results->getNext()) {
      if (max_matches > 0 && matches >= max_matches)
        break;

      std::string type_name;
      if (auto enum_type = llvm::dyn_cast<PDBSymbolTypeEnum>(result.get()))
        type_name = enum_type->getName();
      else if (auto typedef_type =
                   llvm::dyn_cast<PDBSymbolTypeTypedef>(result.get()))
        type_name = typedef_type->getName();
      else if (auto class_type = llvm::dyn_cast<PDBSymbolTypeUDT>(result.get()))
        type_name = class_type->getName();
      else {
        // We're looking only for types that have names.  Skip symbols, as well
        // as unnamed types such as arrays, pointers, etc.
        continue;
      }

      if (!regex.Execute(type_name))
        continue;

      // This should cause the type to get cached and stored in the `m_types`
      // lookup.
      if (!ResolveTypeUID(result->getSymIndexId()))
        continue;

      auto iter = m_types.find(result->getSymIndexId());
      if (iter == m_types.end())
        continue;
      types.Insert(iter->second);
      ++matches;
    }
  }
}

void SymbolFilePDB::FindTypes(const lldb_private::TypeQuery &query,
                              lldb_private::TypeResults &type_results) {

  // Make sure we haven't already searched this SymbolFile before.
  if (type_results.AlreadySearched(this))
    return;

  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());

  std::unique_ptr<IPDBEnumSymbols> results;
  llvm::StringRef basename = query.GetTypeBasename().GetStringRef();
  if (basename.empty())
    return;
  results = m_global_scope_up->findAllChildren(PDB_SymType::None);
  if (!results)
    return;

  while (auto result = results->getNext()) {

    switch (result->getSymTag()) {
    case PDB_SymType::Enum:
    case PDB_SymType::UDT:
    case PDB_SymType::Typedef:
      break;
    default:
      // We're looking only for types that have names.  Skip symbols, as well
      // as unnamed types such as arrays, pointers, etc.
      continue;
    }

    if (MSVCUndecoratedNameParser::DropScope(
            result->getRawSymbol().getName()) != basename)
      continue;

    // This should cause the type to get cached and stored in the `m_types`
    // lookup.
    if (!ResolveTypeUID(result->getSymIndexId()))
      continue;

    auto iter = m_types.find(result->getSymIndexId());
    if (iter == m_types.end())
      continue;
    // We resolved a type. Get the fully qualified name to ensure it matches.
    ConstString name = iter->second->GetQualifiedName();
    TypeQuery type_match(name.GetStringRef(), TypeQueryOptions::e_exact_match);
    if (query.ContextMatches(type_match.GetContextRef())) {
      type_results.InsertUnique(iter->second);
      if (type_results.Done(query))
        return;
    }
  }
}

void SymbolFilePDB::GetTypesForPDBSymbol(const llvm::pdb::PDBSymbol &pdb_symbol,
                                         uint32_t type_mask,
                                         TypeCollection &type_collection) {
  bool can_parse = false;
  switch (pdb_symbol.getSymTag()) {
  case PDB_SymType::ArrayType:
    can_parse = ((type_mask & eTypeClassArray) != 0);
    break;
  case PDB_SymType::BuiltinType:
    can_parse = ((type_mask & eTypeClassBuiltin) != 0);
    break;
  case PDB_SymType::Enum:
    can_parse = ((type_mask & eTypeClassEnumeration) != 0);
    break;
  case PDB_SymType::Function:
  case PDB_SymType::FunctionSig:
    can_parse = ((type_mask & eTypeClassFunction) != 0);
    break;
  case PDB_SymType::PointerType:
    can_parse = ((type_mask & (eTypeClassPointer | eTypeClassBlockPointer |
                               eTypeClassMemberPointer)) != 0);
    break;
  case PDB_SymType::Typedef:
    can_parse = ((type_mask & eTypeClassTypedef) != 0);
    break;
  case PDB_SymType::UDT: {
    auto *udt = llvm::dyn_cast<PDBSymbolTypeUDT>(&pdb_symbol);
    assert(udt);
    can_parse = (udt->getUdtKind() != PDB_UdtType::Interface &&
                 ((type_mask & (eTypeClassClass | eTypeClassStruct |
                                eTypeClassUnion)) != 0));
  } break;
  default:
    break;
  }

  if (can_parse) {
    if (auto *type = ResolveTypeUID(pdb_symbol.getSymIndexId())) {
      if (!llvm::is_contained(type_collection, type))
        type_collection.push_back(type);
    }
  }

  auto results_up = pdb_symbol.findAllChildren();
  while (auto symbol_up = results_up->getNext())
    GetTypesForPDBSymbol(*symbol_up, type_mask, type_collection);
}

void SymbolFilePDB::GetTypes(lldb_private::SymbolContextScope *sc_scope,
                             TypeClass type_mask,
                             lldb_private::TypeList &type_list) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  TypeCollection type_collection;
  CompileUnit *cu =
      sc_scope ? sc_scope->CalculateSymbolContextCompileUnit() : nullptr;
  if (cu) {
    auto compiland_up = GetPDBCompilandByUID(cu->GetID());
    if (!compiland_up)
      return;
    GetTypesForPDBSymbol(*compiland_up, type_mask, type_collection);
  } else {
    for (uint32_t cu_idx = 0; cu_idx < GetNumCompileUnits(); ++cu_idx) {
      auto cu_sp = ParseCompileUnitAtIndex(cu_idx);
      if (cu_sp) {
        if (auto compiland_up = GetPDBCompilandByUID(cu_sp->GetID()))
          GetTypesForPDBSymbol(*compiland_up, type_mask, type_collection);
      }
    }
  }

  for (auto type : type_collection) {
    type->GetForwardCompilerType();
    type_list.Insert(type->shared_from_this());
  }
}

llvm::Expected<lldb::TypeSystemSP>
SymbolFilePDB::GetTypeSystemForLanguage(lldb::LanguageType language) {
  auto type_system_or_err =
      m_objfile_sp->GetModule()->GetTypeSystemForLanguage(language);
  if (type_system_or_err) {
    if (auto ts = *type_system_or_err)
      ts->SetSymbolFile(this);
  }
  return type_system_or_err;
}

PDBASTParser *SymbolFilePDB::GetPDBAstParser() {
  auto type_system_or_err =
      GetTypeSystemForLanguage(lldb::eLanguageTypeC_plus_plus);
  if (auto err = type_system_or_err.takeError()) {
    LLDB_LOG_ERROR(GetLog(LLDBLog::Symbols), std::move(err),
                   "Unable to get PDB AST parser: {0}");
    return nullptr;
  }

  auto ts = *type_system_or_err;
  auto *clang_type_system =
      llvm::dyn_cast_or_null<TypeSystemClang>(ts.get());
  if (!clang_type_system)
    return nullptr;

  return clang_type_system->GetPDBParser();
}

lldb_private::CompilerDeclContext
SymbolFilePDB::FindNamespace(lldb_private::ConstString name,
                             const CompilerDeclContext &parent_decl_ctx, bool) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  auto type_system_or_err =
      GetTypeSystemForLanguage(lldb::eLanguageTypeC_plus_plus);
  if (auto err = type_system_or_err.takeError()) {
    LLDB_LOG_ERROR(GetLog(LLDBLog::Symbols), std::move(err),
                   "Unable to find namespace {1}: {0}", name.AsCString());
    return CompilerDeclContext();
  }
  auto ts = *type_system_or_err;
  auto *clang_type_system =
      llvm::dyn_cast_or_null<TypeSystemClang>(ts.get());
  if (!clang_type_system)
    return CompilerDeclContext();

  PDBASTParser *pdb = clang_type_system->GetPDBParser();
  if (!pdb)
    return CompilerDeclContext();

  clang::DeclContext *decl_context = nullptr;
  if (parent_decl_ctx)
    decl_context = static_cast<clang::DeclContext *>(
        parent_decl_ctx.GetOpaqueDeclContext());

  auto namespace_decl =
      pdb->FindNamespaceDecl(decl_context, name.GetStringRef());
  if (!namespace_decl)
    return CompilerDeclContext();

  return clang_type_system->CreateDeclContext(namespace_decl);
}

IPDBSession &SymbolFilePDB::GetPDBSession() { return *m_session_up; }

const IPDBSession &SymbolFilePDB::GetPDBSession() const {
  return *m_session_up;
}

lldb::CompUnitSP SymbolFilePDB::ParseCompileUnitForUID(uint32_t id,
                                                       uint32_t index) {
  auto found_cu = m_comp_units.find(id);
  if (found_cu != m_comp_units.end())
    return found_cu->second;

  auto compiland_up = GetPDBCompilandByUID(id);
  if (!compiland_up)
    return CompUnitSP();

  lldb::LanguageType lang;
  auto details = compiland_up->findOneChild<PDBSymbolCompilandDetails>();
  if (!details)
    lang = lldb::eLanguageTypeC_plus_plus;
  else
    lang = TranslateLanguage(details->getLanguage());

  if (lang == lldb::LanguageType::eLanguageTypeUnknown)
    return CompUnitSP();

  std::string path = compiland_up->getSourceFileFullPath();
  if (path.empty())
    return CompUnitSP();

  // Don't support optimized code for now, DebugInfoPDB does not return this
  // information.
  LazyBool optimized = eLazyBoolNo;
  auto cu_sp = std::make_shared<CompileUnit>(m_objfile_sp->GetModule(), nullptr,
                                             path.c_str(), id, lang, optimized);

  if (!cu_sp)
    return CompUnitSP();

  m_comp_units.insert(std::make_pair(id, cu_sp));
  if (index == UINT32_MAX)
    GetCompileUnitIndex(*compiland_up, index);
  lldbassert(index != UINT32_MAX);
  SetCompileUnitAtIndex(index, cu_sp);
  return cu_sp;
}

bool SymbolFilePDB::ParseCompileUnitLineTable(CompileUnit &comp_unit,
                                              uint32_t match_line) {
  auto compiland_up = GetPDBCompilandByUID(comp_unit.GetID());
  if (!compiland_up)
    return false;

  // LineEntry needs the *index* of the file into the list of support files
  // returned by ParseCompileUnitSupportFiles.  But the underlying SDK gives us
  // a globally unique idenfitifier in the namespace of the PDB.  So, we have
  // to do a mapping so that we can hand out indices.
  llvm::DenseMap<uint32_t, uint32_t> index_map;
  BuildSupportFileIdToSupportFileIndexMap(*compiland_up, index_map);
  auto line_table = std::make_unique<LineTable>(&comp_unit);

  // Find contributions to `compiland` from all source and header files.
  auto files = m_session_up->getSourceFilesForCompiland(*compiland_up);
  if (!files)
    return false;

  // For each source and header file, create a LineSequence for contributions
  // to the compiland from that file, and add the sequence.
  while (auto file = files->getNext()) {
    std::unique_ptr<LineSequence> sequence(
        line_table->CreateLineSequenceContainer());
    auto lines = m_session_up->findLineNumbers(*compiland_up, *file);
    if (!lines)
      continue;
    int entry_count = lines->getChildCount();

    uint64_t prev_addr;
    uint32_t prev_length;
    uint32_t prev_line;
    uint32_t prev_source_idx;

    for (int i = 0; i < entry_count; ++i) {
      auto line = lines->getChildAtIndex(i);

      uint64_t lno = line->getLineNumber();
      uint64_t addr = line->getVirtualAddress();
      uint32_t length = line->getLength();
      uint32_t source_id = line->getSourceFileId();
      uint32_t col = line->getColumnNumber();
      uint32_t source_idx = index_map[source_id];

      // There was a gap between the current entry and the previous entry if
      // the addresses don't perfectly line up.
      bool is_gap = (i > 0) && (prev_addr + prev_length < addr);

      // Before inserting the current entry, insert a terminal entry at the end
      // of the previous entry's address range if the current entry resulted in
      // a gap from the previous entry.
      if (is_gap && ShouldAddLine(match_line, prev_line, prev_length)) {
        line_table->AppendLineEntryToSequence(
            sequence.get(), prev_addr + prev_length, prev_line, 0,
            prev_source_idx, false, false, false, false, true);

        line_table->InsertSequence(sequence.get());
        sequence = line_table->CreateLineSequenceContainer();
      }

      if (ShouldAddLine(match_line, lno, length)) {
        bool is_statement = line->isStatement();
        bool is_prologue = false;
        bool is_epilogue = false;
        auto func =
            m_session_up->findSymbolByAddress(addr, PDB_SymType::Function);
        if (func) {
          auto prologue = func->findOneChild<PDBSymbolFuncDebugStart>();
          if (prologue)
            is_prologue = (addr == prologue->getVirtualAddress());

          auto epilogue = func->findOneChild<PDBSymbolFuncDebugEnd>();
          if (epilogue)
            is_epilogue = (addr == epilogue->getVirtualAddress());
        }

        line_table->AppendLineEntryToSequence(sequence.get(), addr, lno, col,
                                              source_idx, is_statement, false,
                                              is_prologue, is_epilogue, false);
      }

      prev_addr = addr;
      prev_length = length;
      prev_line = lno;
      prev_source_idx = source_idx;
    }

    if (entry_count > 0 && ShouldAddLine(match_line, prev_line, prev_length)) {
      // The end is always a terminal entry, so insert it regardless.
      line_table->AppendLineEntryToSequence(
          sequence.get(), prev_addr + prev_length, prev_line, 0,
          prev_source_idx, false, false, false, false, true);
    }

    line_table->InsertSequence(sequence.get());
  }

  if (line_table->GetSize()) {
    comp_unit.SetLineTable(line_table.release());
    return true;
  }
  return false;
}

void SymbolFilePDB::BuildSupportFileIdToSupportFileIndexMap(
    const PDBSymbolCompiland &compiland,
    llvm::DenseMap<uint32_t, uint32_t> &index_map) const {
  // This is a hack, but we need to convert the source id into an index into
  // the support files array.  We don't want to do path comparisons to avoid
  // basename / full path issues that may or may not even be a problem, so we
  // use the globally unique source file identifiers.  Ideally we could use the
  // global identifiers everywhere, but LineEntry currently assumes indices.
  auto source_files = m_session_up->getSourceFilesForCompiland(compiland);
  if (!source_files)
    return;

  int index = 0;
  while (auto file = source_files->getNext()) {
    uint32_t source_id = file->getUniqueId();
    index_map[source_id] = index++;
  }
}

lldb::CompUnitSP SymbolFilePDB::GetCompileUnitContainsAddress(
    const lldb_private::Address &so_addr) {
  lldb::addr_t file_vm_addr = so_addr.GetFileAddress();
  if (file_vm_addr == LLDB_INVALID_ADDRESS || file_vm_addr == 0)
    return nullptr;

  // If it is a PDB function's vm addr, this is the first sure bet.
  if (auto lines =
          m_session_up->findLineNumbersByAddress(file_vm_addr, /*Length=*/1)) {
    if (auto first_line = lines->getNext())
      return ParseCompileUnitForUID(first_line->getCompilandId());
  }

  // Otherwise we resort to section contributions.
  if (auto sec_contribs = m_session_up->getSectionContribs()) {
    while (auto section = sec_contribs->getNext()) {
      auto va = section->getVirtualAddress();
      if (file_vm_addr >= va && file_vm_addr < va + section->getLength())
        return ParseCompileUnitForUID(section->getCompilandId());
    }
  }
  return nullptr;
}

Mangled
SymbolFilePDB::GetMangledForPDBFunc(const llvm::pdb::PDBSymbolFunc &pdb_func) {
  Mangled mangled;
  auto func_name = pdb_func.getName();
  auto func_undecorated_name = pdb_func.getUndecoratedName();
  std::string func_decorated_name;

  // Seek from public symbols for non-static function's decorated name if any.
  // For static functions, they don't have undecorated names and aren't exposed
  // in Public Symbols either.
  if (!func_undecorated_name.empty()) {
    auto result_up = m_global_scope_up->findChildren(
        PDB_SymType::PublicSymbol, func_undecorated_name,
        PDB_NameSearchFlags::NS_UndecoratedName);
    if (result_up) {
      while (auto symbol_up = result_up->getNext()) {
        // For a public symbol, it is unique.
        lldbassert(result_up->getChildCount() == 1);
        if (auto *pdb_public_sym =
                llvm::dyn_cast_or_null<PDBSymbolPublicSymbol>(
                    symbol_up.get())) {
          if (pdb_public_sym->isFunction()) {
            func_decorated_name = pdb_public_sym->getName();
            break;
          }
        }
      }
    }
  }
  if (!func_decorated_name.empty()) {
    mangled.SetMangledName(ConstString(func_decorated_name));

    // For MSVC, format of C function's decorated name depends on calling
    // convention. Unfortunately none of the format is recognized by current
    // LLDB. For example, `_purecall` is a __cdecl C function. From PDB,
    // `__purecall` is retrieved as both its decorated and undecorated name
    // (using PDBSymbolFunc::getUndecoratedName method). However `__purecall`
    // string is not treated as mangled in LLDB (neither `?` nor `_Z` prefix).
    // Mangled::GetDemangledName method will fail internally and caches an
    // empty string as its undecorated name. So we will face a contradiction
    // here for the same symbol:
    //   non-empty undecorated name from PDB
    //   empty undecorated name from LLDB
    if (!func_undecorated_name.empty() && mangled.GetDemangledName().IsEmpty())
      mangled.SetDemangledName(ConstString(func_undecorated_name));

    // LLDB uses several flags to control how a C++ decorated name is
    // undecorated for MSVC. See `safeUndecorateName` in Class Mangled. So the
    // yielded name could be different from what we retrieve from
    // PDB source unless we also apply same flags in getting undecorated
    // name through PDBSymbolFunc::getUndecoratedNameEx method.
    if (!func_undecorated_name.empty() &&
        mangled.GetDemangledName() != ConstString(func_undecorated_name))
      mangled.SetDemangledName(ConstString(func_undecorated_name));
  } else if (!func_undecorated_name.empty()) {
    mangled.SetDemangledName(ConstString(func_undecorated_name));
  } else if (!func_name.empty())
    mangled.SetValue(ConstString(func_name));

  return mangled;
}

bool SymbolFilePDB::DeclContextMatchesThisSymbolFile(
    const lldb_private::CompilerDeclContext &decl_ctx) {
  if (!decl_ctx.IsValid())
    return true;

  TypeSystem *decl_ctx_type_system = decl_ctx.GetTypeSystem();
  if (!decl_ctx_type_system)
    return false;
  auto type_system_or_err = GetTypeSystemForLanguage(
      decl_ctx_type_system->GetMinimumLanguage(nullptr));
  if (auto err = type_system_or_err.takeError()) {
    LLDB_LOG_ERROR(
        GetLog(LLDBLog::Symbols), std::move(err),
        "Unable to determine if DeclContext matches this symbol file: {0}");
    return false;
  }

  if (decl_ctx_type_system == type_system_or_err->get())
    return true; // The type systems match, return true

  return false;
}

uint32_t SymbolFilePDB::GetCompilandId(const llvm::pdb::PDBSymbolData &data) {
  static const auto pred_upper = [](uint32_t lhs, SecContribInfo rhs) {
    return lhs < rhs.Offset;
  };

  // Cache section contributions
  if (m_sec_contribs.empty()) {
    if (auto SecContribs = m_session_up->getSectionContribs()) {
      while (auto SectionContrib = SecContribs->getNext()) {
        auto comp_id = SectionContrib->getCompilandId();
        if (!comp_id)
          continue;

        auto sec = SectionContrib->getAddressSection();
        auto &sec_cs = m_sec_contribs[sec];

        auto offset = SectionContrib->getAddressOffset();
        auto it = llvm::upper_bound(sec_cs, offset, pred_upper);

        auto size = SectionContrib->getLength();
        sec_cs.insert(it, {offset, size, comp_id});
      }
    }
  }

  // Check by line number
  if (auto Lines = data.getLineNumbers()) {
    if (auto FirstLine = Lines->getNext())
      return FirstLine->getCompilandId();
  }

  // Retrieve section + offset
  uint32_t DataSection = data.getAddressSection();
  uint32_t DataOffset = data.getAddressOffset();
  if (DataSection == 0) {
    if (auto RVA = data.getRelativeVirtualAddress())
      m_session_up->addressForRVA(RVA, DataSection, DataOffset);
  }

  if (DataSection) {
    // Search by section contributions
    auto &sec_cs = m_sec_contribs[DataSection];
    auto it = llvm::upper_bound(sec_cs, DataOffset, pred_upper);
    if (it != sec_cs.begin()) {
      --it;
      if (DataOffset < it->Offset + it->Size)
        return it->CompilandId;
    }
  } else {
    // Search in lexical tree
    auto LexParentId = data.getLexicalParentId();
    while (auto LexParent = m_session_up->getSymbolById(LexParentId)) {
      if (LexParent->getSymTag() == PDB_SymType::Exe)
        break;
      if (LexParent->getSymTag() == PDB_SymType::Compiland)
        return LexParentId;
      LexParentId = LexParent->getRawSymbol().getLexicalParentId();
    }
  }

  return 0;
}
