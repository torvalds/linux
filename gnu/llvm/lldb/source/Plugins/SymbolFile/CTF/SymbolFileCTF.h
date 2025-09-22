//===-- SymbolFileCTF.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_SYMBOLFILE_CTF_SYMBOLFILECTF_H
#define LLDB_SOURCE_PLUGINS_SYMBOLFILE_CTF_SYMBOLFILECTF_H

#include <map>
#include <optional>
#include <vector>

#include "CTFTypes.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/SymbolFile.h"

namespace lldb_private {

class SymbolFileCTF : public lldb_private::SymbolFileCommon {
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

  SymbolFileCTF(lldb::ObjectFileSP objfile_sp);

  static void Initialize();

  static void Terminate();

  static llvm::StringRef GetPluginNameStatic() { return "CTF"; }

  static llvm::StringRef GetPluginDescriptionStatic();

  static lldb_private::SymbolFile *
  CreateInstance(lldb::ObjectFileSP objfile_sp);

  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

  uint32_t CalculateAbilities() override;

  void InitializeObject() override;

  lldb::LanguageType ParseLanguage(CompileUnit &comp_unit) override {
    return lldb::eLanguageTypeUnknown;
  }

  bool ParseHeader();

  size_t ParseFunctions(CompileUnit &comp_unit) override;

  size_t ParseObjects(CompileUnit &comp_unit);

  bool ParseLineTable(CompileUnit &comp_unit) override { return false; }

  bool ParseDebugMacros(CompileUnit &comp_unit) override { return false; }

  bool ParseSupportFiles(CompileUnit &comp_unit,
                         SupportFileList &support_files) override {
    return false;
  }

  size_t ParseTypes(CompileUnit &cu) override;

  bool ParseImportedModules(
      const SymbolContext &sc,
      std::vector<lldb_private::SourceModule> &imported_modules) override {
    return false;
  }

  size_t ParseBlocksRecursive(Function &func) override { return 0; }

  size_t ParseVariablesForContext(const SymbolContext &sc) override;

  uint32_t CalculateNumCompileUnits() override { return 0; }

  lldb::CompUnitSP ParseCompileUnitAtIndex(uint32_t index) override;

  Type *ResolveTypeUID(lldb::user_id_t type_uid) override;
  std::optional<ArrayInfo> GetDynamicArrayInfoForUID(
      lldb::user_id_t type_uid,
      const lldb_private::ExecutionContext *exe_ctx) override {
    return std::nullopt;
  }

  bool CompleteType(CompilerType &compiler_type) override;

  uint32_t ResolveSymbolContext(const lldb_private::Address &so_addr,
                                lldb::SymbolContextItem resolve_scope,
                                lldb_private::SymbolContext &sc) override;

  void AddSymbols(Symtab &symtab) override;

  void GetTypes(lldb_private::SymbolContextScope *sc_scope,
                lldb::TypeClass type_mask,
                lldb_private::TypeList &type_list) override {}

  void FindTypes(const lldb_private::TypeQuery &match,
                 lldb_private::TypeResults &results) override;

  void FindTypesByRegex(const lldb_private::RegularExpression &regex,
                        uint32_t max_matches, lldb_private::TypeMap &types);

  void FindFunctions(const lldb_private::Module::LookupInfo &lookup_info,
                     const lldb_private::CompilerDeclContext &parent_decl_ctx,
                     bool include_inlines,
                     lldb_private::SymbolContextList &sc_list) override;

  void FindFunctions(const lldb_private::RegularExpression &regex,
                     bool include_inlines,
                     lldb_private::SymbolContextList &sc_list) override;

  void
  FindGlobalVariables(lldb_private::ConstString name,
                      const lldb_private::CompilerDeclContext &parent_decl_ctx,
                      uint32_t max_matches,
                      lldb_private::VariableList &variables) override;

  void FindGlobalVariables(const lldb_private::RegularExpression &regex,
                           uint32_t max_matches,
                           lldb_private::VariableList &variables) override;

  enum TypeKind : uint32_t {
    eUnknown = 0,
    eInteger = 1,
    eFloat = 2,
    ePointer = 3,
    eArray = 4,
    eFunction = 5,
    eStruct = 6,
    eUnion = 7,
    eEnum = 8,
    eForward = 9,
    eTypedef = 10,
    eVolatile = 11,
    eConst = 12,
    eRestrict = 13,
    eSlice = 14,
  };

private:
  enum Flags : uint32_t {
    eFlagCompress = (1u << 0),
    eFlagNewFuncInfo = (1u << 1),
    eFlagIdxSorted = (1u << 2),
    eFlagDynStr = (1u << 3),
  };

  enum IntEncoding : uint32_t {
    eSigned = 0x1,
    eChar = 0x2,
    eBool = 0x4,
    eVarArgs = 0x8,
  };

  struct ctf_preamble_t {
    uint16_t magic;
    uint8_t version;
    uint8_t flags;
  };

  struct ctf_header_t {
    ctf_preamble_t preamble;
    uint32_t parlabel;
    uint32_t parname;
    uint32_t lbloff;
    uint32_t objtoff;
    uint32_t funcoff;
    uint32_t typeoff;
    uint32_t stroff;
    uint32_t strlen;
  };

  struct ctf_type_t {
    uint32_t name;
    uint32_t info;
    union {
      uint32_t size;
      uint32_t type;
    };
    uint32_t lsizehi;
    uint32_t lsizelo;
  };

  struct ctf_stype_t {
    uint32_t name;
    uint32_t info;
    union {
      uint32_t size;
      uint32_t type;
    };

    bool IsLargeType() const { return size == 0xffff; }
    uint32_t GetStructSize() const {
      if (IsLargeType())
        return sizeof(ctf_type_t);
      return sizeof(ctf_stype_t);
    }
    uint32_t GetType() const { return type; }
    uint32_t GetSize() const { return size; }
  };

  llvm::Expected<std::unique_ptr<CTFType>> ParseType(lldb::offset_t &offset,
                                                     lldb::user_id_t uid);

  llvm::Expected<lldb::TypeSP> CreateType(CTFType *ctf_type);
  llvm::Expected<lldb::TypeSP> CreateInteger(const CTFInteger &ctf_integer);
  llvm::Expected<lldb::TypeSP> CreateModifier(const CTFModifier &ctf_modifier);
  llvm::Expected<lldb::TypeSP> CreateTypedef(const CTFTypedef &ctf_typedef);
  llvm::Expected<lldb::TypeSP> CreateArray(const CTFArray &ctf_array);
  llvm::Expected<lldb::TypeSP> CreateEnum(const CTFEnum &ctf_enum);
  llvm::Expected<lldb::TypeSP> CreateFunction(const CTFFunction &ctf_function);
  llvm::Expected<lldb::TypeSP> CreateRecord(const CTFRecord &ctf_record);
  llvm::Expected<lldb::TypeSP> CreateForward(const CTFForward &ctf_forward);

  llvm::StringRef ReadString(lldb::offset_t offset) const;

  std::vector<uint16_t> GetFieldSizes(lldb::offset_t field_offset,
                                      uint32_t fields, uint32_t struct_size);

  DataExtractor m_data;

  /// The start offset of the CTF body into m_data. If the body is uncompressed,
  /// m_data contains the header and the body and the body starts after the
  /// header. If the body is compressed, m_data only contains the body and the
  /// offset is zero.
  lldb::offset_t m_body_offset = 0;

  TypeSystemClang *m_ast;
  lldb::CompUnitSP m_comp_unit_sp;

  std::optional<ctf_header_t> m_header;

  /// Parsed CTF types.
  llvm::DenseMap<lldb::user_id_t, std::unique_ptr<CTFType>> m_ctf_types;

  /// Parsed LLDB types.
  llvm::DenseMap<lldb::user_id_t, lldb::TypeSP> m_types;

  /// To complete types, we need a way to map (imcomplete) compiler types back
  /// to parsed CTF types.
  llvm::DenseMap<lldb::opaque_compiler_type_t, const CTFType *>
      m_compiler_types;

  std::vector<lldb::FunctionSP> m_functions;
  std::vector<lldb::VariableSP> m_variables;

  static constexpr uint16_t g_ctf_magic = 0xcff1;
  static constexpr uint8_t g_ctf_version = 4;
  static constexpr uint16_t g_ctf_field_threshold = 0x2000;
};
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_SYMBOLFILE_CTF_SYMBOLFILECTF_H
