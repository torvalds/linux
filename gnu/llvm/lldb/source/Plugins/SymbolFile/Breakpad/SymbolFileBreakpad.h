//===-- SymbolFileBreakpad.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_SYMBOLFILE_BREAKPAD_SYMBOLFILEBREAKPAD_H
#define LLDB_SOURCE_PLUGINS_SYMBOLFILE_BREAKPAD_SYMBOLFILEBREAKPAD_H

#include "Plugins/ObjectFile/Breakpad/BreakpadRecords.h"
#include "lldb/Symbol/LineTable.h"
#include "lldb/Symbol/PostfixExpression.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Utility/FileSpecList.h"
#include <optional>

namespace lldb_private {

namespace breakpad {

class SymbolFileBreakpad : public SymbolFileCommon {
  /// LLVM RTTI support.
  static char ID;

public:
  /// LLVM RTTI support.
  /// \{
  bool isA(const void *ClassID) const override {
    return ClassID == &ID || SymbolFileCommon::isA(ClassID);
  }
  static bool classof(const SymbolFile *obj) { return obj->isA(&ID); }
  /// \}

  // Static Functions
  static void Initialize();
  static void Terminate();
  static void DebuggerInitialize(Debugger &debugger) {}
  static llvm::StringRef GetPluginNameStatic() { return "breakpad"; }

  static llvm::StringRef GetPluginDescriptionStatic() {
    return "Breakpad debug symbol file reader.";
  }

  static SymbolFile *CreateInstance(lldb::ObjectFileSP objfile_sp) {
    return new SymbolFileBreakpad(std::move(objfile_sp));
  }

  // Constructors and Destructors
  SymbolFileBreakpad(lldb::ObjectFileSP objfile_sp)
      : SymbolFileCommon(std::move(objfile_sp)) {}

  ~SymbolFileBreakpad() override = default;

  uint32_t CalculateAbilities() override;

  void InitializeObject() override {}

  // Compile Unit function calls

  lldb::LanguageType ParseLanguage(CompileUnit &comp_unit) override {
    return lldb::eLanguageTypeUnknown;
  }

  lldb::FunctionSP GetOrCreateFunction(CompileUnit &comp_unit);

  size_t ParseFunctions(CompileUnit &comp_unit) override;

  bool ParseLineTable(CompileUnit &comp_unit) override;

  bool ParseDebugMacros(CompileUnit &comp_unit) override { return false; }

  bool ParseSupportFiles(CompileUnit &comp_unit,
                         SupportFileList &support_files) override;
  size_t ParseTypes(CompileUnit &cu) override { return 0; }

  bool ParseImportedModules(
      const SymbolContext &sc,
      std::vector<lldb_private::SourceModule> &imported_modules) override {
    return false;
  }

  size_t ParseBlocksRecursive(Function &func) override;

  void FindGlobalVariables(ConstString name,
                           const CompilerDeclContext &parent_decl_ctx,
                           uint32_t max_matches,
                           VariableList &variables) override {}

  size_t ParseVariablesForContext(const SymbolContext &sc) override {
    return 0;
  }
  Type *ResolveTypeUID(lldb::user_id_t type_uid) override { return nullptr; }
  std::optional<ArrayInfo> GetDynamicArrayInfoForUID(
      lldb::user_id_t type_uid,
      const lldb_private::ExecutionContext *exe_ctx) override {
    return std::nullopt;
  }

  bool CompleteType(CompilerType &compiler_type) override { return false; }
  uint32_t ResolveSymbolContext(const Address &so_addr,
                                lldb::SymbolContextItem resolve_scope,
                                SymbolContext &sc) override;

  uint32_t ResolveSymbolContext(const SourceLocationSpec &src_location_spec,
                                lldb::SymbolContextItem resolve_scope,
                                SymbolContextList &sc_list) override;

  void GetTypes(SymbolContextScope *sc_scope, lldb::TypeClass type_mask,
                TypeList &type_list) override {}

  void FindFunctions(const Module::LookupInfo &lookup_info,
                     const CompilerDeclContext &parent_decl_ctx,
                     bool include_inlines, SymbolContextList &sc_list) override;

  void FindFunctions(const RegularExpression &regex, bool include_inlines,
                     SymbolContextList &sc_list) override;

  llvm::Expected<lldb::TypeSystemSP>
  GetTypeSystemForLanguage(lldb::LanguageType language) override {
    return llvm::createStringError(
        "SymbolFileBreakpad does not support GetTypeSystemForLanguage");
  }

  CompilerDeclContext FindNamespace(ConstString name,
                                    const CompilerDeclContext &parent_decl_ctx,
                                    bool only_root_namespaces) override {
    return CompilerDeclContext();
  }

  void AddSymbols(Symtab &symtab) override;

  llvm::Expected<lldb::addr_t> GetParameterStackSize(Symbol &symbol) override;

  lldb::UnwindPlanSP
  GetUnwindPlan(const Address &address,
                const RegisterInfoResolver &resolver) override;

  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

  uint64_t GetDebugInfoSize(bool load_all_debug_info = false) override;

private:
  // A class representing a position in the breakpad file. Useful for
  // remembering the position so we can go back to it later and parse more data.
  // Can be converted to/from a LineIterator, but it has a much smaller memory
  // footprint.
  struct Bookmark {
    uint32_t section;
    size_t offset;

    friend bool operator<(const Bookmark &lhs, const Bookmark &rhs) {
      return std::tie(lhs.section, lhs.offset) <
             std::tie(rhs.section, rhs.offset);
    }
  };

  // At iterator class for simplifying algorithms reading data from the breakpad
  // file. It iterates over all records (lines) in the sections of a given type.
  // It also supports saving a specific position (via the GetBookmark() method)
  // and then resuming from it afterwards.
  class LineIterator;

  // Return an iterator range for all records in the given object file of the
  // given type.
  llvm::iterator_range<LineIterator> lines(Record::Kind section_type);

  // Breakpad files do not contain sufficient information to correctly
  // reconstruct compile units. The approach chosen here is to treat each
  // function as a compile unit. The compile unit name is the name if the first
  // line entry belonging to this function.
  // This class is our internal representation of a compile unit. It stores the
  // CompileUnit object and a bookmark pointing to the FUNC record of the
  // compile unit function. It also lazily construct the list of support files
  // and line table entries for the compile unit, when these are needed.
  class CompUnitData {
  public:
    CompUnitData(Bookmark bookmark) : bookmark(bookmark) {}

    CompUnitData() = default;
    CompUnitData(const CompUnitData &rhs) : bookmark(rhs.bookmark) {}
    CompUnitData &operator=(const CompUnitData &rhs) {
      bookmark = rhs.bookmark;
      support_files.reset();
      line_table_up.reset();
      return *this;
    }
    friend bool operator<(const CompUnitData &lhs, const CompUnitData &rhs) {
      return lhs.bookmark < rhs.bookmark;
    }

    Bookmark bookmark;
    std::optional<FileSpecList> support_files;
    std::unique_ptr<LineTable> line_table_up;
  };

  uint32_t CalculateNumCompileUnits() override;
  lldb::CompUnitSP ParseCompileUnitAtIndex(uint32_t index) override;

  lldb::addr_t GetBaseFileAddress();
  void ParseFileRecords();
  void ParseCUData();
  void ParseLineTableAndSupportFiles(CompileUnit &cu, CompUnitData &data);
  void ParseUnwindData();
  llvm::ArrayRef<uint8_t> SaveAsDWARF(postfix::Node &node);
  lldb::UnwindPlanSP ParseCFIUnwindPlan(const Bookmark &bookmark,
                                        const RegisterInfoResolver &resolver);
  bool ParseCFIUnwindRow(llvm::StringRef unwind_rules,
                         const RegisterInfoResolver &resolver,
                         UnwindPlan::Row &row);
  lldb::UnwindPlanSP ParseWinUnwindPlan(const Bookmark &bookmark,
                                        const RegisterInfoResolver &resolver);
  void ParseInlineOriginRecords();

  using CompUnitMap = RangeDataVector<lldb::addr_t, lldb::addr_t, CompUnitData>;

  std::optional<std::vector<FileSpec>> m_files;
  std::optional<CompUnitMap> m_cu_data;
  std::optional<std::vector<llvm::StringRef>> m_inline_origins;

  using UnwindMap = RangeDataVector<lldb::addr_t, lldb::addr_t, Bookmark>;
  struct UnwindData {
    UnwindMap cfi;
    UnwindMap win;
  };
  std::optional<UnwindData> m_unwind_data;
  llvm::BumpPtrAllocator m_allocator;
};

} // namespace breakpad
} // namespace lldb_private

#endif
