//===-- PdbAstBuilder.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_SYMBOLFILE_NATIVEPDB_PDBASTBUILDER_H
#define LLDB_SOURCE_PLUGINS_SYMBOLFILE_NATIVEPDB_PDBASTBUILDER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Threading.h"

#include "Plugins/ExpressionParser/Clang/ClangASTImporter.h"

#include "PdbIndex.h"
#include "PdbSymUid.h"
#include <optional>

namespace clang {
class TagDecl;
class DeclContext;
class Decl;
class QualType;
class FunctionDecl;
class NamespaceDecl;
} // namespace clang

namespace llvm {
namespace codeview {
class ProcSym;
}
} // namespace llvm

namespace lldb_private {
class ClangASTImporter;
class ObjectFile;

namespace npdb {
class PdbIndex;
struct VariableInfo;

struct DeclStatus {
  DeclStatus() = default;
  DeclStatus(lldb::user_id_t uid, bool resolved)
      : uid(uid), resolved(resolved) {}
  lldb::user_id_t uid = 0;
  bool resolved = false;
};

class PdbAstBuilder {
public:
  // Constructors and Destructors
  PdbAstBuilder(TypeSystemClang &clang);

  lldb_private::CompilerDeclContext GetTranslationUnitDecl();

  std::optional<lldb_private::CompilerDecl>
  GetOrCreateDeclForUid(PdbSymUid uid);
  clang::DeclContext *GetOrCreateDeclContextForUid(PdbSymUid uid);
  clang::DeclContext *GetParentDeclContext(PdbSymUid uid);

  clang::FunctionDecl *GetOrCreateFunctionDecl(PdbCompilandSymId func_id);
  clang::FunctionDecl *
  GetOrCreateInlinedFunctionDecl(PdbCompilandSymId inlinesite_id);
  clang::BlockDecl *GetOrCreateBlockDecl(PdbCompilandSymId block_id);
  clang::VarDecl *GetOrCreateVariableDecl(PdbCompilandSymId scope_id,
                                          PdbCompilandSymId var_id);
  clang::VarDecl *GetOrCreateVariableDecl(PdbGlobalSymId var_id);
  clang::TypedefNameDecl *GetOrCreateTypedefDecl(PdbGlobalSymId id);
  void ParseDeclsForContext(clang::DeclContext &context);

  clang::QualType GetBasicType(lldb::BasicType type);
  clang::QualType GetOrCreateType(PdbTypeSymId type);

  bool CompleteTagDecl(clang::TagDecl &tag);
  bool CompleteType(clang::QualType qt);

  CompilerDecl ToCompilerDecl(clang::Decl &decl);
  CompilerType ToCompilerType(clang::QualType qt);
  CompilerDeclContext ToCompilerDeclContext(clang::DeclContext &context);
  clang::Decl *FromCompilerDecl(CompilerDecl decl);
  clang::DeclContext *FromCompilerDeclContext(CompilerDeclContext context);

  TypeSystemClang &clang() { return m_clang; }
  ClangASTImporter &GetClangASTImporter() { return m_importer; }

  void Dump(Stream &stream);

private:
  clang::Decl *TryGetDecl(PdbSymUid uid) const;

  using TypeIndex = llvm::codeview::TypeIndex;

  clang::QualType
  CreatePointerType(const llvm::codeview::PointerRecord &pointer);
  clang::QualType
  CreateModifierType(const llvm::codeview::ModifierRecord &modifier);
  clang::QualType CreateArrayType(const llvm::codeview::ArrayRecord &array);
  clang::QualType CreateRecordType(PdbTypeSymId id,
                                   const llvm::codeview::TagRecord &record);
  clang::QualType CreateEnumType(PdbTypeSymId id,
                                 const llvm::codeview::EnumRecord &record);
  clang::QualType
  CreateFunctionType(TypeIndex args_type_idx, TypeIndex return_type_idx,
                     llvm::codeview::CallingConvention calling_convention);
  clang::QualType CreateType(PdbTypeSymId type);

  void CreateFunctionParameters(PdbCompilandSymId func_id,
                                clang::FunctionDecl &function_decl,
                                uint32_t param_count);
  clang::Decl *GetOrCreateSymbolForId(PdbCompilandSymId id);
  clang::VarDecl *CreateVariableDecl(PdbSymUid uid,
                                     llvm::codeview::CVSymbol sym,
                                     clang::DeclContext &scope);
  clang::NamespaceDecl *GetOrCreateNamespaceDecl(const char *name,
                                                 clang::DeclContext &context);
  clang::FunctionDecl *CreateFunctionDeclFromId(PdbTypeSymId func_tid,
                                                PdbCompilandSymId func_sid);
  clang::FunctionDecl *
  CreateFunctionDecl(PdbCompilandSymId func_id, llvm::StringRef func_name,
                     TypeIndex func_ti, CompilerType func_ct,
                     uint32_t param_count, clang::StorageClass func_storage,
                     bool is_inline, clang::DeclContext *parent);
  void ParseNamespace(clang::DeclContext &parent);
  void ParseAllTypes();
  void ParseAllFunctionsAndNonLocalVars();
  void ParseDeclsForSimpleContext(clang::DeclContext &context);
  void ParseBlockChildren(PdbCompilandSymId block_id);

  std::pair<clang::DeclContext *, std::string>
  CreateDeclInfoForType(const llvm::codeview::TagRecord &record, TypeIndex ti);
  std::pair<clang::DeclContext *, std::string>
  CreateDeclInfoForUndecoratedName(llvm::StringRef uname);
  clang::QualType CreateSimpleType(TypeIndex ti);

  TypeSystemClang &m_clang;

  ClangASTImporter m_importer;
  llvm::once_flag m_parse_functions_and_non_local_vars;
  llvm::once_flag m_parse_all_types;
  llvm::DenseMap<clang::Decl *, DeclStatus> m_decl_to_status;
  llvm::DenseMap<lldb::user_id_t, clang::Decl *> m_uid_to_decl;
  llvm::DenseMap<lldb::user_id_t, clang::QualType> m_uid_to_type;

  // From class/struct's opaque_compiler_type_t to a set containing the pairs of
  // method's name and CompilerType.
  llvm::DenseMap<lldb::opaque_compiler_type_t,
                 llvm::SmallSet<std::pair<llvm::StringRef, CompilerType>, 8>>
      m_cxx_record_map;
  llvm::DenseSet<clang::NamespaceDecl *> m_parsed_namespaces;
};

} // namespace npdb
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_SYMBOLFILE_NATIVEPDB_PDBASTBUILDER_H
