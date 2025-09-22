//===-- ClangASTSource.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGASTSOURCE_H
#define LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGASTSOURCE_H

#include <set>

#include "Plugins/ExpressionParser/Clang/ClangASTImporter.h"
#include "Plugins/ExpressionParser/Clang/NameSearchContext.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Target/Target.h"
#include "clang/AST/ExternalASTSource.h"
#include "clang/Basic/IdentifierTable.h"

#include "llvm/ADT/SmallSet.h"

namespace lldb_private {

/// \class ClangASTSource ClangASTSource.h "lldb/Expression/ClangASTSource.h"
/// Provider for named objects defined in the debug info for Clang
///
/// As Clang parses an expression, it may encounter names that are not defined
/// inside the expression, including variables, functions, and types.  Clang
/// knows the name it is looking for, but nothing else. The ExternalSemaSource
/// class provides Decls (VarDecl, FunDecl, TypeDecl) to Clang for these
/// names, consulting the ClangExpressionDeclMap to do the actual lookups.
class ClangASTSource : public clang::ExternalASTSource,
                       public ClangASTImporter::MapCompleter {
public:
  /// Constructor
  ///
  /// Initializes class variables.
  ///
  /// \param[in] target
  ///     A reference to the target containing debug information to use.
  ///
  /// \param[in] importer
  ///     The ClangASTImporter to use.
  ClangASTSource(const lldb::TargetSP &target,
                 const std::shared_ptr<ClangASTImporter> &importer);

  /// Destructor
  ~ClangASTSource() override;

  /// Interface stubs.
  clang::Decl *GetExternalDecl(clang::GlobalDeclID) override { return nullptr; }
  clang::Stmt *GetExternalDeclStmt(uint64_t) override { return nullptr; }
  clang::Selector GetExternalSelector(uint32_t) override {
    return clang::Selector();
  }
  uint32_t GetNumExternalSelectors() override { return 0; }
  clang::CXXBaseSpecifier *
  GetExternalCXXBaseSpecifiers(uint64_t Offset) override {
    return nullptr;
  }
  void MaterializeVisibleDecls(const clang::DeclContext *DC) {}

  void InstallASTContext(TypeSystemClang &ast_context);

  //
  // APIs for ExternalASTSource
  //

  /// Look up all Decls that match a particular name.  Only handles
  /// Identifiers and DeclContexts that are either NamespaceDecls or
  /// TranslationUnitDecls.  Calls SetExternalVisibleDeclsForName with the
  /// result.
  ///
  /// The work for this function is done by
  /// void FindExternalVisibleDecls (NameSearchContext &);
  ///
  /// \param[in] DC
  ///     The DeclContext to register the found Decls in.
  ///
  /// \param[in] Name
  ///     The name to find entries for.
  ///
  /// \return
  ///     Whatever SetExternalVisibleDeclsForName returns.
  bool FindExternalVisibleDeclsByName(const clang::DeclContext *DC,
                                      clang::DeclarationName Name) override;

  /// Enumerate all Decls in a given lexical context.
  ///
  /// \param[in] DC
  ///     The DeclContext being searched.
  ///
  /// \param[in] IsKindWeWant
  ///     A callback function that returns true given the
  ///     DeclKinds of desired Decls, and false otherwise.
  ///
  /// \param[in] Decls
  ///     A vector that is filled in with matching Decls.
  void FindExternalLexicalDecls(
      const clang::DeclContext *DC,
      llvm::function_ref<bool(clang::Decl::Kind)> IsKindWeWant,
      llvm::SmallVectorImpl<clang::Decl *> &Decls) override;

  /// Specify the layout of the contents of a RecordDecl.
  ///
  /// \param[in] Record
  ///     The record (in the parser's AST context) that needs to be
  ///     laid out.
  ///
  /// \param[out] Size
  ///     The total size of the record in bits.
  ///
  /// \param[out] Alignment
  ///     The alignment of the record in bits.
  ///
  /// \param[in] FieldOffsets
  ///     A map that must be populated with pairs of the record's
  ///     fields (in the parser's AST context) and their offsets
  ///     (measured in bits).
  ///
  /// \param[in] BaseOffsets
  ///     A map that must be populated with pairs of the record's
  ///     C++ concrete base classes (in the parser's AST context,
  ///     and only if the record is a CXXRecordDecl and has base
  ///     classes) and their offsets (measured in bytes).
  ///
  /// \param[in] VirtualBaseOffsets
  ///     A map that must be populated with pairs of the record's
  ///     C++ virtual base classes (in the parser's AST context,
  ///     and only if the record is a CXXRecordDecl and has base
  ///     classes) and their offsets (measured in bytes).
  ///
  /// \return
  ///     True <=> the layout is valid.
  bool layoutRecordType(
      const clang::RecordDecl *Record, uint64_t &Size, uint64_t &Alignment,
      llvm::DenseMap<const clang::FieldDecl *, uint64_t> &FieldOffsets,
      llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits>
          &BaseOffsets,
      llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits>
          &VirtualBaseOffsets) override;

  /// Complete a TagDecl.
  ///
  /// \param[in] Tag
  ///     The Decl to be completed in place.
  void CompleteType(clang::TagDecl *Tag) override;

  /// Complete an ObjCInterfaceDecl.
  ///
  /// \param[in] Class
  ///     The Decl to be completed in place.
  void CompleteType(clang::ObjCInterfaceDecl *Class) override;

  /// Called on entering a translation unit.  Tells Clang by calling
  /// setHasExternalVisibleStorage() and setHasExternalLexicalStorage() that
  /// this object has something to say about undefined names.
  ///
  /// \param[in] Consumer
  ///     Unused.
  void StartTranslationUnit(clang::ASTConsumer *Consumer) override;

  //
  // APIs for NamespaceMapCompleter
  //

  /// Look up the modules containing a given namespace and put the appropriate
  /// entries in the namespace map.
  ///
  /// \param[in] namespace_map
  ///     The map to be completed.
  ///
  /// \param[in] name
  ///     The name of the namespace to be found.
  ///
  /// \param[in] parent_map
  ///     The map for the namespace's parent namespace, if there is
  ///     one.
  void CompleteNamespaceMap(
      ClangASTImporter::NamespaceMapSP &namespace_map, ConstString name,
      ClangASTImporter::NamespaceMapSP &parent_map) const override;

  //
  // Helper APIs
  //

  clang::NamespaceDecl *
  AddNamespace(NameSearchContext &context,
               ClangASTImporter::NamespaceMapSP &namespace_decls);

  /// The worker function for FindExternalVisibleDeclsByName.
  ///
  /// \param[in] context
  ///     The NameSearchContext to use when filing results.
  virtual void FindExternalVisibleDecls(NameSearchContext &context);

  clang::Sema *getSema();

  void SetLookupsEnabled(bool lookups_enabled) {
    m_lookups_enabled = lookups_enabled;
  }
  bool GetLookupsEnabled() { return m_lookups_enabled; }

  /// \class ClangASTSourceProxy ClangASTSource.h
  /// "lldb/Expression/ClangASTSource.h" Proxy for ClangASTSource
  ///
  /// Clang AST contexts like to own their AST sources, so this is a state-
  /// free proxy object.
  class ClangASTSourceProxy : public clang::ExternalASTSource {
  public:
    ClangASTSourceProxy(ClangASTSource &original) : m_original(original) {}

    bool FindExternalVisibleDeclsByName(const clang::DeclContext *DC,
                                        clang::DeclarationName Name) override {
      return m_original.FindExternalVisibleDeclsByName(DC, Name);
    }

    void FindExternalLexicalDecls(
        const clang::DeclContext *DC,
        llvm::function_ref<bool(clang::Decl::Kind)> IsKindWeWant,
        llvm::SmallVectorImpl<clang::Decl *> &Decls) override {
      return m_original.FindExternalLexicalDecls(DC, IsKindWeWant, Decls);
    }

    void CompleteType(clang::TagDecl *Tag) override {
      return m_original.CompleteType(Tag);
    }

    void CompleteType(clang::ObjCInterfaceDecl *Class) override {
      return m_original.CompleteType(Class);
    }

    bool layoutRecordType(
        const clang::RecordDecl *Record, uint64_t &Size, uint64_t &Alignment,
        llvm::DenseMap<const clang::FieldDecl *, uint64_t> &FieldOffsets,
        llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits>
            &BaseOffsets,
        llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits>
            &VirtualBaseOffsets) override {
      return m_original.layoutRecordType(Record, Size, Alignment, FieldOffsets,
                                         BaseOffsets, VirtualBaseOffsets);
    }

    void StartTranslationUnit(clang::ASTConsumer *Consumer) override {
      return m_original.StartTranslationUnit(Consumer);
    }

  private:
    ClangASTSource &m_original;
  };

  clang::ExternalASTSource *CreateProxy() {
    return new ClangASTSourceProxy(*this);
  }

protected:
  /// Look for the complete version of an Objective-C interface, and return it
  /// if found.
  ///
  /// \param[in] interface_decl
  ///     An ObjCInterfaceDecl that may not be the complete one.
  ///
  /// \return
  ///     NULL if the complete interface couldn't be found;
  ///     the complete interface otherwise.
  clang::ObjCInterfaceDecl *
  GetCompleteObjCInterface(const clang::ObjCInterfaceDecl *interface_decl);

  /// Find all entities matching a given name in a given module, using a
  /// NameSearchContext to make Decls for them.
  ///
  /// \param[in] context
  ///     The NameSearchContext that can construct Decls for this name.
  ///
  /// \param[in] module
  ///     If non-NULL, the module to query.
  ///
  /// \param[in] namespace_decl
  ///     If valid and module is non-NULL, the parent namespace.
  void FindExternalVisibleDecls(NameSearchContext &context,
                                lldb::ModuleSP module,
                                CompilerDeclContext &namespace_decl);

  /// Find all Objective-C methods matching a given selector.
  ///
  /// \param[in] context
  ///     The NameSearchContext that can construct Decls for this name.
  ///     Its m_decl_name contains the selector and its m_decl_context
  ///     is the containing object.
  void FindObjCMethodDecls(NameSearchContext &context);

  /// Find all Objective-C properties and ivars with a given name.
  ///
  /// \param[in] context
  ///     The NameSearchContext that can construct Decls for this name.
  ///     Its m_decl_name contains the name and its m_decl_context
  ///     is the containing object.
  void FindObjCPropertyAndIvarDecls(NameSearchContext &context);

  /// Performs lookup into a namespace.
  ///
  /// \param context
  ///     The NameSearchContext for a lookup inside a namespace.
  void LookupInNamespace(NameSearchContext &context);

  /// A wrapper for TypeSystemClang::CopyType that sets a flag that
  /// indicates that we should not respond to queries during import.
  ///
  /// \param[in] src_type
  ///     The source type.
  ///
  /// \return
  ///     The imported type.
  CompilerType GuardedCopyType(const CompilerType &src_type);

  std::shared_ptr<ClangModulesDeclVendor> GetClangModulesDeclVendor();

public:
  /// Returns true if a name should be ignored by name lookup.
  ///
  /// \param[in] name
  ///     The name to be considered.
  ///
  /// \param[in] ignore_all_dollar_names
  ///     True if $-names of all sorts should be ignored.
  ///
  /// \return
  ///     True if the name is one of a class of names that are ignored by
  ///     global lookup for performance reasons.
  bool IgnoreName(const ConstString name, bool ignore_all_dollar_names);

  /// Copies a single Decl into the parser's AST context.
  ///
  /// \param[in] src_decl
  ///     The Decl to copy.
  ///
  /// \return
  ///     A copy of the Decl in m_ast_context, or NULL if the copy failed.
  clang::Decl *CopyDecl(clang::Decl *src_decl);

  /// Determined the origin of a single Decl, if it can be found.
  ///
  /// \param[in] decl
  ///     The Decl whose origin is to be found.
  ///
  /// \return
  ///     True if lookup succeeded; false otherwise.
  ClangASTImporter::DeclOrigin GetDeclOrigin(const clang::Decl *decl);

  /// Returns the TypeSystem that uses this ClangASTSource instance as it's
  /// ExternalASTSource.
  TypeSystemClang *GetTypeSystem() const { return m_clang_ast_context; }

private:
  bool FindObjCPropertyAndIvarDeclsWithOrigin(
      NameSearchContext &context,
      DeclFromUser<const clang::ObjCInterfaceDecl> &origin_iface_decl);

protected:
  bool FindObjCMethodDeclsWithOrigin(
      NameSearchContext &context,
      clang::ObjCInterfaceDecl *original_interface_decl, const char *log_info);

  void FindDeclInModules(NameSearchContext &context, ConstString name);
  void FindDeclInObjCRuntime(NameSearchContext &context, ConstString name);

  /// Fills the namespace map of the given NameSearchContext.
  ///
  /// \param context The NameSearchContext with the namespace map to fill.
  /// \param module_sp The module to search for namespaces or a nullptr if
  ///                  the current target should be searched.
  /// \param namespace_decl The DeclContext in which to search for namespaces.
  void FillNamespaceMap(NameSearchContext &context, lldb::ModuleSP module_sp,
                        const CompilerDeclContext &namespace_decl);

  clang::TagDecl *FindCompleteType(const clang::TagDecl *decl);

  friend struct NameSearchContext;

  bool m_lookups_enabled;

  /// The target to use in finding variables and types.
  const lldb::TargetSP m_target;
  /// The AST context requests are coming in for.
  clang::ASTContext *m_ast_context;
  /// The TypeSystemClang for m_ast_context.
  TypeSystemClang *m_clang_ast_context;
  /// The file manager paired with the AST context.
  clang::FileManager *m_file_manager;
  /// The target's AST importer.
  std::shared_ptr<ClangASTImporter> m_ast_importer_sp;
  std::set<const clang::Decl *> m_active_lexical_decls;
  std::set<const char *> m_active_lookups;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGASTSOURCE_H
