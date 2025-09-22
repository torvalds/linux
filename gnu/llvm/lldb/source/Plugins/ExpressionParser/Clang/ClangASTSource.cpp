//===-- ClangASTSource.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ClangASTSource.h"

#include "ClangDeclVendor.h"
#include "ClangModulesDeclVendor.h"

#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleList.h"
#include "lldb/Symbol/CompilerDeclContext.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Symbol/TaggedASTType.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "clang/AST/ASTContext.h"
#include "clang/Basic/SourceManager.h"

#include "Plugins/ExpressionParser/Clang/ClangUtil.h"
#include "Plugins/LanguageRuntime/ObjC/ObjCLanguageRuntime.h"
#include "Plugins/TypeSystem/Clang/TypeSystemClang.h"

#include <memory>
#include <vector>

using namespace clang;
using namespace lldb_private;

// Scoped class that will remove an active lexical decl from the set when it
// goes out of scope.
namespace {
class ScopedLexicalDeclEraser {
public:
  ScopedLexicalDeclEraser(std::set<const clang::Decl *> &decls,
                          const clang::Decl *decl)
      : m_active_lexical_decls(decls), m_decl(decl) {}

  ~ScopedLexicalDeclEraser() { m_active_lexical_decls.erase(m_decl); }

private:
  std::set<const clang::Decl *> &m_active_lexical_decls;
  const clang::Decl *m_decl;
};
}

ClangASTSource::ClangASTSource(
    const lldb::TargetSP &target,
    const std::shared_ptr<ClangASTImporter> &importer)
    : m_lookups_enabled(false), m_target(target), m_ast_context(nullptr),
      m_ast_importer_sp(importer), m_active_lexical_decls(),
      m_active_lookups() {
  assert(m_ast_importer_sp && "No ClangASTImporter passed to ClangASTSource?");
}

void ClangASTSource::InstallASTContext(TypeSystemClang &clang_ast_context) {
  m_ast_context = &clang_ast_context.getASTContext();
  m_clang_ast_context = &clang_ast_context;
  m_file_manager = &m_ast_context->getSourceManager().getFileManager();
  m_ast_importer_sp->InstallMapCompleter(m_ast_context, *this);
}

ClangASTSource::~ClangASTSource() {
  m_ast_importer_sp->ForgetDestination(m_ast_context);

  if (!m_target)
    return;

  // Unregister the current ASTContext as a source for all scratch
  // ASTContexts in the ClangASTImporter. Without this the scratch AST might
  // query the deleted ASTContext for additional type information.
  // We unregister from *all* scratch ASTContexts in case a type got exported
  // to a scratch AST that isn't the best fitting scratch ASTContext.
  lldb::TypeSystemClangSP scratch_ts_sp = ScratchTypeSystemClang::GetForTarget(
      *m_target, ScratchTypeSystemClang::DefaultAST, false);

  if (!scratch_ts_sp)
    return;

  ScratchTypeSystemClang *default_scratch_ast =
      llvm::cast<ScratchTypeSystemClang>(scratch_ts_sp.get());
  // Unregister from the default scratch AST (and all sub-ASTs).
  default_scratch_ast->ForgetSource(m_ast_context, *m_ast_importer_sp);
}

void ClangASTSource::StartTranslationUnit(ASTConsumer *Consumer) {
  if (!m_ast_context)
    return;

  m_ast_context->getTranslationUnitDecl()->setHasExternalVisibleStorage();
  m_ast_context->getTranslationUnitDecl()->setHasExternalLexicalStorage();
}

// The core lookup interface.
bool ClangASTSource::FindExternalVisibleDeclsByName(
    const DeclContext *decl_ctx, DeclarationName clang_decl_name) {
  if (!m_ast_context) {
    SetNoExternalVisibleDeclsForName(decl_ctx, clang_decl_name);
    return false;
  }

  std::string decl_name(clang_decl_name.getAsString());

  switch (clang_decl_name.getNameKind()) {
  // Normal identifiers.
  case DeclarationName::Identifier: {
    clang::IdentifierInfo *identifier_info =
        clang_decl_name.getAsIdentifierInfo();

    if (!identifier_info || identifier_info->getBuiltinID() != 0) {
      SetNoExternalVisibleDeclsForName(decl_ctx, clang_decl_name);
      return false;
    }
  } break;

  // Operator names.
  case DeclarationName::CXXOperatorName:
  case DeclarationName::CXXLiteralOperatorName:
    break;

  // Using directives found in this context.
  // Tell Sema we didn't find any or we'll end up getting asked a *lot*.
  case DeclarationName::CXXUsingDirective:
    SetNoExternalVisibleDeclsForName(decl_ctx, clang_decl_name);
    return false;

  case DeclarationName::ObjCZeroArgSelector:
  case DeclarationName::ObjCOneArgSelector:
  case DeclarationName::ObjCMultiArgSelector: {
    llvm::SmallVector<NamedDecl *, 1> method_decls;

    NameSearchContext method_search_context(*m_clang_ast_context, method_decls,
                                            clang_decl_name, decl_ctx);

    FindObjCMethodDecls(method_search_context);

    SetExternalVisibleDeclsForName(decl_ctx, clang_decl_name, method_decls);
    return (method_decls.size() > 0);
  }
  // These aren't possible in the global context.
  case DeclarationName::CXXConstructorName:
  case DeclarationName::CXXDestructorName:
  case DeclarationName::CXXConversionFunctionName:
  case DeclarationName::CXXDeductionGuideName:
    SetNoExternalVisibleDeclsForName(decl_ctx, clang_decl_name);
    return false;
  }

  if (!GetLookupsEnabled()) {
    // Wait until we see a '$' at the start of a name before we start doing any
    // lookups so we can avoid lookup up all of the builtin types.
    if (!decl_name.empty() && decl_name[0] == '$') {
      SetLookupsEnabled(true);
    } else {
      SetNoExternalVisibleDeclsForName(decl_ctx, clang_decl_name);
      return false;
    }
  }

  ConstString const_decl_name(decl_name.c_str());

  const char *uniqued_const_decl_name = const_decl_name.GetCString();
  if (m_active_lookups.find(uniqued_const_decl_name) !=
      m_active_lookups.end()) {
    // We are currently looking up this name...
    SetNoExternalVisibleDeclsForName(decl_ctx, clang_decl_name);
    return false;
  }
  m_active_lookups.insert(uniqued_const_decl_name);
  llvm::SmallVector<NamedDecl *, 4> name_decls;
  NameSearchContext name_search_context(*m_clang_ast_context, name_decls,
                                        clang_decl_name, decl_ctx);
  FindExternalVisibleDecls(name_search_context);
  SetExternalVisibleDeclsForName(decl_ctx, clang_decl_name, name_decls);
  m_active_lookups.erase(uniqued_const_decl_name);
  return (name_decls.size() != 0);
}

TagDecl *ClangASTSource::FindCompleteType(const TagDecl *decl) {
  Log *log = GetLog(LLDBLog::Expressions);

  if (const NamespaceDecl *namespace_context =
          dyn_cast<NamespaceDecl>(decl->getDeclContext())) {
    ClangASTImporter::NamespaceMapSP namespace_map =
        m_ast_importer_sp->GetNamespaceMap(namespace_context);

    if (!namespace_map)
      return nullptr;

    LLDB_LOGV(log, "      CTD Inspecting namespace map{0:x} ({1} entries)",
              namespace_map.get(), namespace_map->size());

    for (const ClangASTImporter::NamespaceMapItem &item : *namespace_map) {
      LLDB_LOG(log, "      CTD Searching namespace {0} in module {1}",
               item.second.GetName(), item.first->GetFileSpec().GetFilename());

      ConstString name(decl->getName());

      // Create a type matcher using the CompilerDeclContext for the namespace
      // as the context (item.second) and search for the name inside of this
      // context.
      TypeQuery query(item.second, name);
      TypeResults results;
      item.first->FindTypes(query, results);

      for (const lldb::TypeSP &type_sp : results.GetTypeMap().Types()) {
        CompilerType clang_type(type_sp->GetFullCompilerType());

        if (!ClangUtil::IsClangType(clang_type))
          continue;

        const TagType *tag_type =
            ClangUtil::GetQualType(clang_type)->getAs<TagType>();

        if (!tag_type)
          continue;

        TagDecl *candidate_tag_decl =
            const_cast<TagDecl *>(tag_type->getDecl());

        if (TypeSystemClang::GetCompleteDecl(
                &candidate_tag_decl->getASTContext(), candidate_tag_decl))
          return candidate_tag_decl;
      }
    }
  } else {
    const ModuleList &module_list = m_target->GetImages();
    // Create a type matcher using a CompilerDecl. Each TypeSystem class knows
    // how to fill out a CompilerContext array using a CompilerDecl.
    TypeQuery query(CompilerDecl(m_clang_ast_context, (void *)decl));
    TypeResults results;
    module_list.FindTypes(nullptr, query, results);
    for (const lldb::TypeSP &type_sp : results.GetTypeMap().Types()) {

      CompilerType clang_type(type_sp->GetFullCompilerType());

      if (!ClangUtil::IsClangType(clang_type))
        continue;

      const TagType *tag_type =
          ClangUtil::GetQualType(clang_type)->getAs<TagType>();

      if (!tag_type)
        continue;

      TagDecl *candidate_tag_decl = const_cast<TagDecl *>(tag_type->getDecl());

      if (TypeSystemClang::GetCompleteDecl(&candidate_tag_decl->getASTContext(),
                                           candidate_tag_decl))
        return candidate_tag_decl;
    }
  }
  return nullptr;
}

void ClangASTSource::CompleteType(TagDecl *tag_decl) {
  Log *log = GetLog(LLDBLog::Expressions);

  if (log) {
    LLDB_LOG(log,
             "    CompleteTagDecl on (ASTContext*){0} Completing "
             "(TagDecl*){1:x} named {2}",
             m_clang_ast_context->getDisplayName(), tag_decl,
             tag_decl->getName());

    LLDB_LOG(log, "      CTD Before:\n{0}", ClangUtil::DumpDecl(tag_decl));
  }

  auto iter = m_active_lexical_decls.find(tag_decl);
  if (iter != m_active_lexical_decls.end())
    return;
  m_active_lexical_decls.insert(tag_decl);
  ScopedLexicalDeclEraser eraser(m_active_lexical_decls, tag_decl);

  if (!m_ast_importer_sp->CompleteTagDecl(tag_decl)) {
    // We couldn't complete the type.  Maybe there's a definition somewhere
    // else that can be completed.
    if (TagDecl *alternate = FindCompleteType(tag_decl))
      m_ast_importer_sp->CompleteTagDeclWithOrigin(tag_decl, alternate);
  }

  LLDB_LOG(log, "      [CTD] After:\n{0}", ClangUtil::DumpDecl(tag_decl));
}

void ClangASTSource::CompleteType(clang::ObjCInterfaceDecl *interface_decl) {
  Log *log = GetLog(LLDBLog::Expressions);

  LLDB_LOG(log,
           "    [CompleteObjCInterfaceDecl] on (ASTContext*){0:x} '{1}' "
           "Completing an ObjCInterfaceDecl named {1}",
           m_ast_context, m_clang_ast_context->getDisplayName(),
           interface_decl->getName());
  LLDB_LOG(log, "      [COID] Before:\n{0}",
           ClangUtil::DumpDecl(interface_decl));

  ClangASTImporter::DeclOrigin original = m_ast_importer_sp->GetDeclOrigin(interface_decl);

  if (original.Valid()) {
    if (ObjCInterfaceDecl *original_iface_decl =
            dyn_cast<ObjCInterfaceDecl>(original.decl)) {
      ObjCInterfaceDecl *complete_iface_decl =
          GetCompleteObjCInterface(original_iface_decl);

      if (complete_iface_decl && (complete_iface_decl != original_iface_decl)) {
        m_ast_importer_sp->SetDeclOrigin(interface_decl, complete_iface_decl);
      }
    }
  }

  m_ast_importer_sp->CompleteObjCInterfaceDecl(interface_decl);

  if (interface_decl->getSuperClass() &&
      interface_decl->getSuperClass() != interface_decl)
    CompleteType(interface_decl->getSuperClass());

  LLDB_LOG(log, "      [COID] After:");
  LLDB_LOG(log, "      [COID] {0}", ClangUtil::DumpDecl(interface_decl));
}

clang::ObjCInterfaceDecl *ClangASTSource::GetCompleteObjCInterface(
    const clang::ObjCInterfaceDecl *interface_decl) {
  lldb::ProcessSP process(m_target->GetProcessSP());

  if (!process)
    return nullptr;

  ObjCLanguageRuntime *language_runtime(ObjCLanguageRuntime::Get(*process));

  if (!language_runtime)
    return nullptr;

  ConstString class_name(interface_decl->getNameAsString().c_str());

  lldb::TypeSP complete_type_sp(
      language_runtime->LookupInCompleteClassCache(class_name));

  if (!complete_type_sp)
    return nullptr;

  TypeFromUser complete_type =
      TypeFromUser(complete_type_sp->GetFullCompilerType());
  lldb::opaque_compiler_type_t complete_opaque_type =
      complete_type.GetOpaqueQualType();

  if (!complete_opaque_type)
    return nullptr;

  const clang::Type *complete_clang_type =
      QualType::getFromOpaquePtr(complete_opaque_type).getTypePtr();
  const ObjCInterfaceType *complete_interface_type =
      dyn_cast<ObjCInterfaceType>(complete_clang_type);

  if (!complete_interface_type)
    return nullptr;

  ObjCInterfaceDecl *complete_iface_decl(complete_interface_type->getDecl());

  return complete_iface_decl;
}

void ClangASTSource::FindExternalLexicalDecls(
    const DeclContext *decl_context,
    llvm::function_ref<bool(Decl::Kind)> predicate,
    llvm::SmallVectorImpl<Decl *> &decls) {

  Log *log = GetLog(LLDBLog::Expressions);

  const Decl *context_decl = dyn_cast<Decl>(decl_context);

  if (!context_decl)
    return;

  auto iter = m_active_lexical_decls.find(context_decl);
  if (iter != m_active_lexical_decls.end())
    return;
  m_active_lexical_decls.insert(context_decl);
  ScopedLexicalDeclEraser eraser(m_active_lexical_decls, context_decl);

  if (log) {
    if (const NamedDecl *context_named_decl = dyn_cast<NamedDecl>(context_decl))
      LLDB_LOG(log,
               "FindExternalLexicalDecls on (ASTContext*){0:x} '{1}' in "
               "'{2}' ({3}Decl*){4}",
               m_ast_context, m_clang_ast_context->getDisplayName(),
               context_named_decl->getNameAsString().c_str(),
               context_decl->getDeclKindName(),
               static_cast<const void *>(context_decl));
    else if (context_decl)
      LLDB_LOG(log,
               "FindExternalLexicalDecls on (ASTContext*){0:x} '{1}' in "
               "({2}Decl*){3}",
               m_ast_context, m_clang_ast_context->getDisplayName(),
               context_decl->getDeclKindName(),
               static_cast<const void *>(context_decl));
    else
      LLDB_LOG(log,
               "FindExternalLexicalDecls on (ASTContext*){0:x} '{1}' in a "
               "NULL context",
               m_ast_context, m_clang_ast_context->getDisplayName());
  }

  ClangASTImporter::DeclOrigin original = m_ast_importer_sp->GetDeclOrigin(context_decl);

  if (!original.Valid())
    return;

  LLDB_LOG(log, "  FELD Original decl (ASTContext*){0:x} (Decl*){1:x}:\n{2}",
           static_cast<void *>(original.ctx),
           static_cast<void *>(original.decl),
           ClangUtil::DumpDecl(original.decl));

  if (ObjCInterfaceDecl *original_iface_decl =
          dyn_cast<ObjCInterfaceDecl>(original.decl)) {
    ObjCInterfaceDecl *complete_iface_decl =
        GetCompleteObjCInterface(original_iface_decl);

    if (complete_iface_decl && (complete_iface_decl != original_iface_decl)) {
      original.decl = complete_iface_decl;
      original.ctx = &complete_iface_decl->getASTContext();

      m_ast_importer_sp->SetDeclOrigin(context_decl, complete_iface_decl);
    }
  }

  if (TagDecl *original_tag_decl = dyn_cast<TagDecl>(original.decl)) {
    ExternalASTSource *external_source = original.ctx->getExternalSource();

    if (external_source)
      external_source->CompleteType(original_tag_decl);
  }

  const DeclContext *original_decl_context =
      dyn_cast<DeclContext>(original.decl);

  if (!original_decl_context)
    return;

  // Indicates whether we skipped any Decls of the original DeclContext.
  bool SkippedDecls = false;
  for (Decl *decl : original_decl_context->decls()) {
    // The predicate function returns true if the passed declaration kind is
    // the one we are looking for.
    // See clang::ExternalASTSource::FindExternalLexicalDecls()
    if (predicate(decl->getKind())) {
      if (log) {
        std::string ast_dump = ClangUtil::DumpDecl(decl);
        if (const NamedDecl *context_named_decl =
                dyn_cast<NamedDecl>(context_decl))
          LLDB_LOG(log, "  FELD Adding [to {0}Decl {1}] lexical {2}Decl {3}",
                   context_named_decl->getDeclKindName(),
                   context_named_decl->getName(), decl->getDeclKindName(),
                   ast_dump);
        else
          LLDB_LOG(log, "  FELD Adding lexical {0}Decl {1}",
                   decl->getDeclKindName(), ast_dump);
      }

      Decl *copied_decl = CopyDecl(decl);

      if (!copied_decl)
        continue;

      // FIXME: We should add the copied decl to the 'decls' list. This would
      // add the copied Decl into the DeclContext and make sure that we
      // correctly propagate that we added some Decls back to Clang.
      // By leaving 'decls' empty we incorrectly return false from
      // DeclContext::LoadLexicalDeclsFromExternalStorage which might cause
      // lookup issues later on.
      // We can't just add them for now as the ASTImporter already added the
      // decl into the DeclContext and this would add it twice.

      if (FieldDecl *copied_field = dyn_cast<FieldDecl>(copied_decl)) {
        QualType copied_field_type = copied_field->getType();

        m_ast_importer_sp->RequireCompleteType(copied_field_type);
      }
    } else {
      SkippedDecls = true;
    }
  }

  // CopyDecl may build a lookup table which may set up ExternalLexicalStorage
  // to false.  However, since we skipped some of the external Decls we must
  // set it back!
  if (SkippedDecls) {
    decl_context->setHasExternalLexicalStorage(true);
    // This sets HasLazyExternalLexicalLookups to true.  By setting this bit we
    // ensure that the lookup table is rebuilt, which means the external source
    // is consulted again when a clang::DeclContext::lookup is called.
    const_cast<DeclContext *>(decl_context)->setMustBuildLookupTable();
  }
}

void ClangASTSource::FindExternalVisibleDecls(NameSearchContext &context) {
  assert(m_ast_context);

  const ConstString name(context.m_decl_name.getAsString().c_str());

  Log *log = GetLog(LLDBLog::Expressions);

  if (log) {
    if (!context.m_decl_context)
      LLDB_LOG(log,
               "ClangASTSource::FindExternalVisibleDecls on "
               "(ASTContext*){0:x} '{1}' for '{2}' in a NULL DeclContext",
               m_ast_context, m_clang_ast_context->getDisplayName(), name);
    else if (const NamedDecl *context_named_decl =
                 dyn_cast<NamedDecl>(context.m_decl_context))
      LLDB_LOG(log,
               "ClangASTSource::FindExternalVisibleDecls on "
               "(ASTContext*){0:x} '{1}' for '{2}' in '{3}'",
               m_ast_context, m_clang_ast_context->getDisplayName(), name,
               context_named_decl->getName());
    else
      LLDB_LOG(log,
               "ClangASTSource::FindExternalVisibleDecls on "
               "(ASTContext*){0:x} '{1}' for '{2}' in a '{3}'",
               m_ast_context, m_clang_ast_context->getDisplayName(), name,
               context.m_decl_context->getDeclKindName());
  }

  if (isa<NamespaceDecl>(context.m_decl_context)) {
    LookupInNamespace(context);
  } else if (isa<ObjCInterfaceDecl>(context.m_decl_context)) {
    FindObjCPropertyAndIvarDecls(context);
  } else if (!isa<TranslationUnitDecl>(context.m_decl_context)) {
    // we shouldn't be getting FindExternalVisibleDecls calls for these
    return;
  } else {
    CompilerDeclContext namespace_decl;

    LLDB_LOG(log, "  CAS::FEVD Searching the root namespace");

    FindExternalVisibleDecls(context, lldb::ModuleSP(), namespace_decl);
  }

  if (!context.m_namespace_map->empty()) {
    if (log && log->GetVerbose())
      LLDB_LOG(log, "  CAS::FEVD Registering namespace map {0:x} ({1} entries)",
               context.m_namespace_map.get(), context.m_namespace_map->size());

    NamespaceDecl *clang_namespace_decl =
        AddNamespace(context, context.m_namespace_map);

    if (clang_namespace_decl)
      clang_namespace_decl->setHasExternalVisibleStorage();
  }
}

clang::Sema *ClangASTSource::getSema() {
  return m_clang_ast_context->getSema();
}

bool ClangASTSource::IgnoreName(const ConstString name,
                                bool ignore_all_dollar_names) {
  static const ConstString id_name("id");
  static const ConstString Class_name("Class");

  if (m_ast_context->getLangOpts().ObjC)
    if (name == id_name || name == Class_name)
      return true;

  StringRef name_string_ref = name.GetStringRef();

  // The ClangASTSource is not responsible for finding $-names.
  return name_string_ref.empty() ||
         (ignore_all_dollar_names && name_string_ref.starts_with("$")) ||
         name_string_ref.starts_with("_$");
}

void ClangASTSource::FindExternalVisibleDecls(
    NameSearchContext &context, lldb::ModuleSP module_sp,
    CompilerDeclContext &namespace_decl) {
  assert(m_ast_context);

  Log *log = GetLog(LLDBLog::Expressions);

  SymbolContextList sc_list;

  const ConstString name(context.m_decl_name.getAsString().c_str());
  if (IgnoreName(name, true))
    return;

  if (!m_target)
    return;

  FillNamespaceMap(context, module_sp, namespace_decl);

  if (context.m_found_type)
    return;

  lldb::TypeSP type_sp;
  TypeResults results;
  if (module_sp && namespace_decl) {
    // Match the name in the specified decl context.
    TypeQuery query(namespace_decl, name, TypeQueryOptions::e_find_one);
    module_sp->FindTypes(query, results);
    type_sp = results.GetFirstType();
  } else {
    // Match the exact name of the type at the root level.
    TypeQuery query(name.GetStringRef(), TypeQueryOptions::e_exact_match |
                                             TypeQueryOptions::e_find_one);
    m_target->GetImages().FindTypes(nullptr, query, results);
    type_sp = results.GetFirstType();
  }

  if (type_sp) {
    if (log) {
      const char *name_string = type_sp->GetName().GetCString();

      LLDB_LOG(log, "  CAS::FEVD Matching type found for \"{0}\": {1}", name,
               (name_string ? name_string : "<anonymous>"));
    }

    CompilerType full_type = type_sp->GetFullCompilerType();

    CompilerType copied_clang_type(GuardedCopyType(full_type));

    if (!copied_clang_type) {
      LLDB_LOG(log, "  CAS::FEVD - Couldn't export a type");
    } else {

      context.AddTypeDecl(copied_clang_type);

      context.m_found_type = true;
    }
  }

  if (!context.m_found_type) {
    // Try the modules next.
    FindDeclInModules(context, name);
  }

  if (!context.m_found_type && m_ast_context->getLangOpts().ObjC) {
    FindDeclInObjCRuntime(context, name);
  }
}

void ClangASTSource::FillNamespaceMap(
    NameSearchContext &context, lldb::ModuleSP module_sp,
    const CompilerDeclContext &namespace_decl) {
  const ConstString name(context.m_decl_name.getAsString().c_str());
  if (IgnoreName(name, true))
    return;

  Log *log = GetLog(LLDBLog::Expressions);

  if (module_sp && namespace_decl) {
    CompilerDeclContext found_namespace_decl;

    if (SymbolFile *symbol_file = module_sp->GetSymbolFile()) {
      found_namespace_decl = symbol_file->FindNamespace(name, namespace_decl);

      if (found_namespace_decl) {
        context.m_namespace_map->push_back(
            std::pair<lldb::ModuleSP, CompilerDeclContext>(
                module_sp, found_namespace_decl));

        LLDB_LOG(log, "  CAS::FEVD Found namespace {0} in module {1}", name,
                 module_sp->GetFileSpec().GetFilename());
      }
    }
    return;
  }

  for (lldb::ModuleSP image : m_target->GetImages().Modules()) {
    if (!image)
      continue;

    CompilerDeclContext found_namespace_decl;

    SymbolFile *symbol_file = image->GetSymbolFile();

    if (!symbol_file)
      continue;

    // If namespace_decl is not valid, 'FindNamespace' would look for
    // any namespace called 'name' (ignoring parent contexts) and return
    // the first one it finds. Thus if we're doing a qualified lookup only
    // consider root namespaces. E.g., in an expression ::A::B::Foo, the
    // lookup of ::A will result in a qualified lookup. Note, namespace
    // disambiguation for function calls are handled separately in
    // SearchFunctionsInSymbolContexts.
    const bool find_root_namespaces =
        context.m_decl_context &&
        context.m_decl_context->shouldUseQualifiedLookup();
    found_namespace_decl = symbol_file->FindNamespace(
        name, namespace_decl, /* only root namespaces */ find_root_namespaces);

    if (found_namespace_decl) {
      context.m_namespace_map->push_back(
          std::pair<lldb::ModuleSP, CompilerDeclContext>(image,
                                                         found_namespace_decl));

      LLDB_LOG(log, "  CAS::FEVD Found namespace {0} in module {1}", name,
               image->GetFileSpec().GetFilename());
    }
  }
}

bool ClangASTSource::FindObjCMethodDeclsWithOrigin(
    NameSearchContext &context, ObjCInterfaceDecl *original_interface_decl,
    const char *log_info) {
  const DeclarationName &decl_name(context.m_decl_name);
  clang::ASTContext *original_ctx = &original_interface_decl->getASTContext();

  Selector original_selector;

  if (decl_name.isObjCZeroArgSelector()) {
    const IdentifierInfo *ident =
        &original_ctx->Idents.get(decl_name.getAsString());
    original_selector = original_ctx->Selectors.getSelector(0, &ident);
  } else if (decl_name.isObjCOneArgSelector()) {
    const std::string &decl_name_string = decl_name.getAsString();
    std::string decl_name_string_without_colon(decl_name_string.c_str(),
                                               decl_name_string.length() - 1);
    const IdentifierInfo *ident =
        &original_ctx->Idents.get(decl_name_string_without_colon);
    original_selector = original_ctx->Selectors.getSelector(1, &ident);
  } else {
    SmallVector<const IdentifierInfo *, 4> idents;

    clang::Selector sel = decl_name.getObjCSelector();

    unsigned num_args = sel.getNumArgs();

    for (unsigned i = 0; i != num_args; ++i) {
      idents.push_back(&original_ctx->Idents.get(sel.getNameForSlot(i)));
    }

    original_selector =
        original_ctx->Selectors.getSelector(num_args, idents.data());
  }

  DeclarationName original_decl_name(original_selector);

  llvm::SmallVector<NamedDecl *, 1> methods;

  TypeSystemClang::GetCompleteDecl(original_ctx, original_interface_decl);

  if (ObjCMethodDecl *instance_method_decl =
          original_interface_decl->lookupInstanceMethod(original_selector)) {
    methods.push_back(instance_method_decl);
  } else if (ObjCMethodDecl *class_method_decl =
                 original_interface_decl->lookupClassMethod(
                     original_selector)) {
    methods.push_back(class_method_decl);
  }

  if (methods.empty()) {
    return false;
  }

  for (NamedDecl *named_decl : methods) {
    if (!named_decl)
      continue;

    ObjCMethodDecl *result_method = dyn_cast<ObjCMethodDecl>(named_decl);

    if (!result_method)
      continue;

    Decl *copied_decl = CopyDecl(result_method);

    if (!copied_decl)
      continue;

    ObjCMethodDecl *copied_method_decl = dyn_cast<ObjCMethodDecl>(copied_decl);

    if (!copied_method_decl)
      continue;

    Log *log = GetLog(LLDBLog::Expressions);

    LLDB_LOG(log, "  CAS::FOMD found ({0}) {1}", log_info,
             ClangUtil::DumpDecl(copied_method_decl));

    context.AddNamedDecl(copied_method_decl);
  }

  return true;
}

void ClangASTSource::FindDeclInModules(NameSearchContext &context,
                                       ConstString name) {
  Log *log = GetLog(LLDBLog::Expressions);

  std::shared_ptr<ClangModulesDeclVendor> modules_decl_vendor =
      GetClangModulesDeclVendor();
  if (!modules_decl_vendor)
    return;

  bool append = false;
  uint32_t max_matches = 1;
  std::vector<clang::NamedDecl *> decls;

  if (!modules_decl_vendor->FindDecls(name, append, max_matches, decls))
    return;

  LLDB_LOG(log, "  CAS::FEVD Matching entity found for \"{0}\" in the modules",
           name);

  clang::NamedDecl *const decl_from_modules = decls[0];

  if (llvm::isa<clang::TypeDecl>(decl_from_modules) ||
      llvm::isa<clang::ObjCContainerDecl>(decl_from_modules) ||
      llvm::isa<clang::EnumConstantDecl>(decl_from_modules)) {
    clang::Decl *copied_decl = CopyDecl(decl_from_modules);
    clang::NamedDecl *copied_named_decl =
        copied_decl ? dyn_cast<clang::NamedDecl>(copied_decl) : nullptr;

    if (!copied_named_decl) {
      LLDB_LOG(log, "  CAS::FEVD - Couldn't export a type from the modules");

      return;
    }

    context.AddNamedDecl(copied_named_decl);

    context.m_found_type = true;
  }
}

void ClangASTSource::FindDeclInObjCRuntime(NameSearchContext &context,
                                           ConstString name) {
  Log *log = GetLog(LLDBLog::Expressions);

  lldb::ProcessSP process(m_target->GetProcessSP());

  if (!process)
    return;

  ObjCLanguageRuntime *language_runtime(ObjCLanguageRuntime::Get(*process));

  if (!language_runtime)
    return;

  DeclVendor *decl_vendor = language_runtime->GetDeclVendor();

  if (!decl_vendor)
    return;

  bool append = false;
  uint32_t max_matches = 1;
  std::vector<clang::NamedDecl *> decls;

  auto *clang_decl_vendor = llvm::cast<ClangDeclVendor>(decl_vendor);
  if (!clang_decl_vendor->FindDecls(name, append, max_matches, decls))
    return;

  LLDB_LOG(log, "  CAS::FEVD Matching type found for \"{0}\" in the runtime",
           name);

  clang::Decl *copied_decl = CopyDecl(decls[0]);
  clang::NamedDecl *copied_named_decl =
      copied_decl ? dyn_cast<clang::NamedDecl>(copied_decl) : nullptr;

  if (!copied_named_decl) {
    LLDB_LOG(log, "  CAS::FEVD - Couldn't export a type from the runtime");

    return;
  }

  context.AddNamedDecl(copied_named_decl);
}

void ClangASTSource::FindObjCMethodDecls(NameSearchContext &context) {
  Log *log = GetLog(LLDBLog::Expressions);

  const DeclarationName &decl_name(context.m_decl_name);
  const DeclContext *decl_ctx(context.m_decl_context);

  const ObjCInterfaceDecl *interface_decl =
      dyn_cast<ObjCInterfaceDecl>(decl_ctx);

  if (!interface_decl)
    return;

  do {
    ClangASTImporter::DeclOrigin original = m_ast_importer_sp->GetDeclOrigin(interface_decl);

    if (!original.Valid())
      break;

    ObjCInterfaceDecl *original_interface_decl =
        dyn_cast<ObjCInterfaceDecl>(original.decl);

    if (FindObjCMethodDeclsWithOrigin(context, original_interface_decl,
                                      "at origin"))
      return; // found it, no need to look any further
  } while (false);

  StreamString ss;

  if (decl_name.isObjCZeroArgSelector()) {
    ss.Printf("%s", decl_name.getAsString().c_str());
  } else if (decl_name.isObjCOneArgSelector()) {
    ss.Printf("%s", decl_name.getAsString().c_str());
  } else {
    clang::Selector sel = decl_name.getObjCSelector();

    for (unsigned i = 0, e = sel.getNumArgs(); i != e; ++i) {
      llvm::StringRef r = sel.getNameForSlot(i);
      ss.Printf("%s:", r.str().c_str());
    }
  }
  ss.Flush();

  if (ss.GetString().contains("$__lldb"))
    return; // we don't need any results

  ConstString selector_name(ss.GetString());

  LLDB_LOG(log,
           "ClangASTSource::FindObjCMethodDecls on (ASTContext*){0:x} '{1}' "
           "for selector [{2} {3}]",
           m_ast_context, m_clang_ast_context->getDisplayName(),
           interface_decl->getName(), selector_name);
  SymbolContextList sc_list;

  ModuleFunctionSearchOptions function_options;
  function_options.include_symbols = false;
  function_options.include_inlines = false;

  std::string interface_name = interface_decl->getNameAsString();

  do {
    StreamString ms;
    ms.Printf("-[%s %s]", interface_name.c_str(), selector_name.AsCString());
    ms.Flush();
    ConstString instance_method_name(ms.GetString());

    sc_list.Clear();
    m_target->GetImages().FindFunctions(instance_method_name,
                                        lldb::eFunctionNameTypeFull,
                                        function_options, sc_list);

    if (sc_list.GetSize())
      break;

    ms.Clear();
    ms.Printf("+[%s %s]", interface_name.c_str(), selector_name.AsCString());
    ms.Flush();
    ConstString class_method_name(ms.GetString());

    sc_list.Clear();
    m_target->GetImages().FindFunctions(class_method_name,
                                        lldb::eFunctionNameTypeFull,
                                        function_options, sc_list);

    if (sc_list.GetSize())
      break;

    // Fall back and check for methods in categories.  If we find methods this
    // way, we need to check that they're actually in categories on the desired
    // class.

    SymbolContextList candidate_sc_list;

    m_target->GetImages().FindFunctions(selector_name,
                                        lldb::eFunctionNameTypeSelector,
                                        function_options, candidate_sc_list);

    for (const SymbolContext &candidate_sc : candidate_sc_list) {
      if (!candidate_sc.function)
        continue;

      const char *candidate_name = candidate_sc.function->GetName().AsCString();

      const char *cursor = candidate_name;

      if (*cursor != '+' && *cursor != '-')
        continue;

      ++cursor;

      if (*cursor != '[')
        continue;

      ++cursor;

      size_t interface_len = interface_name.length();

      if (strncmp(cursor, interface_name.c_str(), interface_len))
        continue;

      cursor += interface_len;

      if (*cursor == ' ' || *cursor == '(')
        sc_list.Append(candidate_sc);
    }
  } while (false);

  if (sc_list.GetSize()) {
    // We found a good function symbol.  Use that.

    for (const SymbolContext &sc : sc_list) {
      if (!sc.function)
        continue;

      CompilerDeclContext function_decl_ctx = sc.function->GetDeclContext();
      if (!function_decl_ctx)
        continue;

      ObjCMethodDecl *method_decl =
          TypeSystemClang::DeclContextGetAsObjCMethodDecl(function_decl_ctx);

      if (!method_decl)
        continue;

      ObjCInterfaceDecl *found_interface_decl =
          method_decl->getClassInterface();

      if (!found_interface_decl)
        continue;

      if (found_interface_decl->getName() == interface_decl->getName()) {
        Decl *copied_decl = CopyDecl(method_decl);

        if (!copied_decl)
          continue;

        ObjCMethodDecl *copied_method_decl =
            dyn_cast<ObjCMethodDecl>(copied_decl);

        if (!copied_method_decl)
          continue;

        LLDB_LOG(log, "  CAS::FOMD found (in symbols)\n{0}",
                 ClangUtil::DumpDecl(copied_method_decl));

        context.AddNamedDecl(copied_method_decl);
      }
    }

    return;
  }

  // Try the debug information.

  do {
    ObjCInterfaceDecl *complete_interface_decl = GetCompleteObjCInterface(
        const_cast<ObjCInterfaceDecl *>(interface_decl));

    if (!complete_interface_decl)
      break;

    // We found the complete interface.  The runtime never needs to be queried
    // in this scenario.

    DeclFromUser<const ObjCInterfaceDecl> complete_iface_decl(
        complete_interface_decl);

    if (complete_interface_decl == interface_decl)
      break; // already checked this one

    LLDB_LOG(log,
             "CAS::FOPD trying origin "
             "(ObjCInterfaceDecl*){0:x}/(ASTContext*){1:x}...",
             complete_interface_decl, &complete_iface_decl->getASTContext());

    FindObjCMethodDeclsWithOrigin(context, complete_interface_decl,
                                  "in debug info");

    return;
  } while (false);

  do {
    // Check the modules only if the debug information didn't have a complete
    // interface.

    if (std::shared_ptr<ClangModulesDeclVendor> modules_decl_vendor =
            GetClangModulesDeclVendor()) {
      ConstString interface_name(interface_decl->getNameAsString().c_str());
      bool append = false;
      uint32_t max_matches = 1;
      std::vector<clang::NamedDecl *> decls;

      if (!modules_decl_vendor->FindDecls(interface_name, append, max_matches,
                                          decls))
        break;

      ObjCInterfaceDecl *interface_decl_from_modules =
          dyn_cast<ObjCInterfaceDecl>(decls[0]);

      if (!interface_decl_from_modules)
        break;

      if (FindObjCMethodDeclsWithOrigin(context, interface_decl_from_modules,
                                        "in modules"))
        return;
    }
  } while (false);

  do {
    // Check the runtime only if the debug information didn't have a complete
    // interface and the modules don't get us anywhere.

    lldb::ProcessSP process(m_target->GetProcessSP());

    if (!process)
      break;

    ObjCLanguageRuntime *language_runtime(ObjCLanguageRuntime::Get(*process));

    if (!language_runtime)
      break;

    DeclVendor *decl_vendor = language_runtime->GetDeclVendor();

    if (!decl_vendor)
      break;

    ConstString interface_name(interface_decl->getNameAsString().c_str());
    bool append = false;
    uint32_t max_matches = 1;
    std::vector<clang::NamedDecl *> decls;

    auto *clang_decl_vendor = llvm::cast<ClangDeclVendor>(decl_vendor);
    if (!clang_decl_vendor->FindDecls(interface_name, append, max_matches,
                                      decls))
      break;

    ObjCInterfaceDecl *runtime_interface_decl =
        dyn_cast<ObjCInterfaceDecl>(decls[0]);

    if (!runtime_interface_decl)
      break;

    FindObjCMethodDeclsWithOrigin(context, runtime_interface_decl,
                                  "in runtime");
  } while (false);
}

bool ClangASTSource::FindObjCPropertyAndIvarDeclsWithOrigin(
    NameSearchContext &context,
    DeclFromUser<const ObjCInterfaceDecl> &origin_iface_decl) {
  Log *log = GetLog(LLDBLog::Expressions);

  if (origin_iface_decl.IsInvalid())
    return false;

  std::string name_str = context.m_decl_name.getAsString();
  StringRef name(name_str);
  IdentifierInfo &name_identifier(
      origin_iface_decl->getASTContext().Idents.get(name));

  DeclFromUser<ObjCPropertyDecl> origin_property_decl(
      origin_iface_decl->FindPropertyDeclaration(
          &name_identifier, ObjCPropertyQueryKind::OBJC_PR_query_instance));

  bool found = false;

  if (origin_property_decl.IsValid()) {
    DeclFromParser<ObjCPropertyDecl> parser_property_decl(
        origin_property_decl.Import(m_ast_context, *m_ast_importer_sp));
    if (parser_property_decl.IsValid()) {
      LLDB_LOG(log, "  CAS::FOPD found\n{0}",
               ClangUtil::DumpDecl(parser_property_decl.decl));

      context.AddNamedDecl(parser_property_decl.decl);
      found = true;
    }
  }

  DeclFromUser<ObjCIvarDecl> origin_ivar_decl(
      origin_iface_decl->getIvarDecl(&name_identifier));

  if (origin_ivar_decl.IsValid()) {
    DeclFromParser<ObjCIvarDecl> parser_ivar_decl(
        origin_ivar_decl.Import(m_ast_context, *m_ast_importer_sp));
    if (parser_ivar_decl.IsValid()) {
      LLDB_LOG(log, "  CAS::FOPD found\n{0}",
               ClangUtil::DumpDecl(parser_ivar_decl.decl));

      context.AddNamedDecl(parser_ivar_decl.decl);
      found = true;
    }
  }

  return found;
}

void ClangASTSource::FindObjCPropertyAndIvarDecls(NameSearchContext &context) {
  Log *log = GetLog(LLDBLog::Expressions);

  DeclFromParser<const ObjCInterfaceDecl> parser_iface_decl(
      cast<ObjCInterfaceDecl>(context.m_decl_context));
  DeclFromUser<const ObjCInterfaceDecl> origin_iface_decl(
      parser_iface_decl.GetOrigin(*m_ast_importer_sp));

  ConstString class_name(parser_iface_decl->getNameAsString().c_str());

  LLDB_LOG(log,
           "ClangASTSource::FindObjCPropertyAndIvarDecls on "
           "(ASTContext*){0:x} '{1}' for '{2}.{3}'",
           m_ast_context, m_clang_ast_context->getDisplayName(),
           parser_iface_decl->getName(), context.m_decl_name.getAsString());

  if (FindObjCPropertyAndIvarDeclsWithOrigin(context, origin_iface_decl))
    return;

  LLDB_LOG(log,
           "CAS::FOPD couldn't find the property on origin "
           "(ObjCInterfaceDecl*){0:x}/(ASTContext*){1:x}, searching "
           "elsewhere...",
           origin_iface_decl.decl, &origin_iface_decl->getASTContext());

  SymbolContext null_sc;
  TypeList type_list;

  do {
    ObjCInterfaceDecl *complete_interface_decl = GetCompleteObjCInterface(
        const_cast<ObjCInterfaceDecl *>(parser_iface_decl.decl));

    if (!complete_interface_decl)
      break;

    // We found the complete interface.  The runtime never needs to be queried
    // in this scenario.

    DeclFromUser<const ObjCInterfaceDecl> complete_iface_decl(
        complete_interface_decl);

    if (complete_iface_decl.decl == origin_iface_decl.decl)
      break; // already checked this one

    LLDB_LOG(log,
             "CAS::FOPD trying origin "
             "(ObjCInterfaceDecl*){0:x}/(ASTContext*){1:x}...",
             complete_iface_decl.decl, &complete_iface_decl->getASTContext());

    FindObjCPropertyAndIvarDeclsWithOrigin(context, complete_iface_decl);

    return;
  } while (false);

  do {
    // Check the modules only if the debug information didn't have a complete
    // interface.

    std::shared_ptr<ClangModulesDeclVendor> modules_decl_vendor =
        GetClangModulesDeclVendor();

    if (!modules_decl_vendor)
      break;

    bool append = false;
    uint32_t max_matches = 1;
    std::vector<clang::NamedDecl *> decls;

    if (!modules_decl_vendor->FindDecls(class_name, append, max_matches, decls))
      break;

    DeclFromUser<const ObjCInterfaceDecl> interface_decl_from_modules(
        dyn_cast<ObjCInterfaceDecl>(decls[0]));

    if (!interface_decl_from_modules.IsValid())
      break;

    LLDB_LOG(log,
             "CAS::FOPD[{0:x}] trying module "
             "(ObjCInterfaceDecl*){0:x}/(ASTContext*){1:x}...",
             interface_decl_from_modules.decl,
             &interface_decl_from_modules->getASTContext());

    if (FindObjCPropertyAndIvarDeclsWithOrigin(context,
                                               interface_decl_from_modules))
      return;
  } while (false);

  do {
    // Check the runtime only if the debug information didn't have a complete
    // interface and nothing was in the modules.

    lldb::ProcessSP process(m_target->GetProcessSP());

    if (!process)
      return;

    ObjCLanguageRuntime *language_runtime(ObjCLanguageRuntime::Get(*process));

    if (!language_runtime)
      return;

    DeclVendor *decl_vendor = language_runtime->GetDeclVendor();

    if (!decl_vendor)
      break;

    bool append = false;
    uint32_t max_matches = 1;
    std::vector<clang::NamedDecl *> decls;

    auto *clang_decl_vendor = llvm::cast<ClangDeclVendor>(decl_vendor);
    if (!clang_decl_vendor->FindDecls(class_name, append, max_matches, decls))
      break;

    DeclFromUser<const ObjCInterfaceDecl> interface_decl_from_runtime(
        dyn_cast<ObjCInterfaceDecl>(decls[0]));

    if (!interface_decl_from_runtime.IsValid())
      break;

    LLDB_LOG(log,
             "CAS::FOPD[{0:x}] trying runtime "
             "(ObjCInterfaceDecl*){0:x}/(ASTContext*){1:x}...",
             interface_decl_from_runtime.decl,
             &interface_decl_from_runtime->getASTContext());

    if (FindObjCPropertyAndIvarDeclsWithOrigin(context,
                                               interface_decl_from_runtime))
      return;
  } while (false);
}

void ClangASTSource::LookupInNamespace(NameSearchContext &context) {
  const NamespaceDecl *namespace_context =
      dyn_cast<NamespaceDecl>(context.m_decl_context);

  Log *log = GetLog(LLDBLog::Expressions);

  ClangASTImporter::NamespaceMapSP namespace_map =
      m_ast_importer_sp->GetNamespaceMap(namespace_context);

  LLDB_LOGV(log, "  CAS::FEVD Inspecting namespace map {0:x} ({1} entries)",
            namespace_map.get(), namespace_map->size());

  if (!namespace_map)
    return;

  for (ClangASTImporter::NamespaceMap::iterator i = namespace_map->begin(),
                                                e = namespace_map->end();
       i != e; ++i) {
    LLDB_LOG(log, "  CAS::FEVD Searching namespace {0} in module {1}",
             i->second.GetName(), i->first->GetFileSpec().GetFilename());

    FindExternalVisibleDecls(context, i->first, i->second);
  }
}

bool ClangASTSource::layoutRecordType(
    const RecordDecl *record, uint64_t &size, uint64_t &alignment,
    llvm::DenseMap<const clang::FieldDecl *, uint64_t> &field_offsets,
    llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits>
        &base_offsets,
    llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits>
        &virtual_base_offsets) {
  return m_ast_importer_sp->importRecordLayoutFromOrigin(
      record, size, alignment, field_offsets, base_offsets,
      virtual_base_offsets);
}

void ClangASTSource::CompleteNamespaceMap(
    ClangASTImporter::NamespaceMapSP &namespace_map, ConstString name,
    ClangASTImporter::NamespaceMapSP &parent_map) const {

  Log *log = GetLog(LLDBLog::Expressions);

  if (log) {
    if (parent_map && parent_map->size())
      LLDB_LOG(log,
               "CompleteNamespaceMap on (ASTContext*){0:x} '{1}' Searching "
               "for namespace {2} in namespace {3}",
               m_ast_context, m_clang_ast_context->getDisplayName(), name,
               parent_map->begin()->second.GetName());
    else
      LLDB_LOG(log,
               "CompleteNamespaceMap on (ASTContext*){0} '{1}' Searching "
               "for namespace {2}",
               m_ast_context, m_clang_ast_context->getDisplayName(), name);
  }

  if (parent_map) {
    for (ClangASTImporter::NamespaceMap::iterator i = parent_map->begin(),
                                                  e = parent_map->end();
         i != e; ++i) {
      CompilerDeclContext found_namespace_decl;

      lldb::ModuleSP module_sp = i->first;
      CompilerDeclContext module_parent_namespace_decl = i->second;

      SymbolFile *symbol_file = module_sp->GetSymbolFile();

      if (!symbol_file)
        continue;

      found_namespace_decl =
          symbol_file->FindNamespace(name, module_parent_namespace_decl);

      if (!found_namespace_decl)
        continue;

      namespace_map->push_back(std::pair<lldb::ModuleSP, CompilerDeclContext>(
          module_sp, found_namespace_decl));

      LLDB_LOG(log, "  CMN Found namespace {0} in module {1}", name,
               module_sp->GetFileSpec().GetFilename());
    }
  } else {
    CompilerDeclContext null_namespace_decl;
    for (lldb::ModuleSP image : m_target->GetImages().Modules()) {
      if (!image)
        continue;

      CompilerDeclContext found_namespace_decl;

      SymbolFile *symbol_file = image->GetSymbolFile();

      if (!symbol_file)
        continue;

      found_namespace_decl =
          symbol_file->FindNamespace(name, null_namespace_decl);

      if (!found_namespace_decl)
        continue;

      namespace_map->push_back(std::pair<lldb::ModuleSP, CompilerDeclContext>(
          image, found_namespace_decl));

      LLDB_LOG(log, "  CMN[{0}] Found namespace {0} in module {1}", name,
               image->GetFileSpec().GetFilename());
    }
  }
}

NamespaceDecl *ClangASTSource::AddNamespace(
    NameSearchContext &context,
    ClangASTImporter::NamespaceMapSP &namespace_decls) {
  if (!namespace_decls)
    return nullptr;

  const CompilerDeclContext &namespace_decl = namespace_decls->begin()->second;

  clang::ASTContext *src_ast =
      TypeSystemClang::DeclContextGetTypeSystemClang(namespace_decl);
  if (!src_ast)
    return nullptr;
  clang::NamespaceDecl *src_namespace_decl =
      TypeSystemClang::DeclContextGetAsNamespaceDecl(namespace_decl);

  if (!src_namespace_decl)
    return nullptr;

  Decl *copied_decl = CopyDecl(src_namespace_decl);

  if (!copied_decl)
    return nullptr;

  NamespaceDecl *copied_namespace_decl = dyn_cast<NamespaceDecl>(copied_decl);

  if (!copied_namespace_decl)
    return nullptr;

  context.m_decls.push_back(copied_namespace_decl);

  m_ast_importer_sp->RegisterNamespaceMap(copied_namespace_decl,
                                          namespace_decls);

  return dyn_cast<NamespaceDecl>(copied_decl);
}

clang::Decl *ClangASTSource::CopyDecl(Decl *src_decl) {
  return m_ast_importer_sp->CopyDecl(m_ast_context, src_decl);
}

ClangASTImporter::DeclOrigin ClangASTSource::GetDeclOrigin(const clang::Decl *decl) {
  return m_ast_importer_sp->GetDeclOrigin(decl);
}

CompilerType ClangASTSource::GuardedCopyType(const CompilerType &src_type) {
  auto ts = src_type.GetTypeSystem();
  auto src_ast = ts.dyn_cast_or_null<TypeSystemClang>();
  if (!src_ast)
    return {};

  QualType copied_qual_type = ClangUtil::GetQualType(
      m_ast_importer_sp->CopyType(*m_clang_ast_context, src_type));

  if (copied_qual_type.getAsOpaquePtr() &&
      copied_qual_type->getCanonicalTypeInternal().isNull())
    // this shouldn't happen, but we're hardening because the AST importer
    // seems to be generating bad types on occasion.
    return {};

  return m_clang_ast_context->GetType(copied_qual_type);
}

std::shared_ptr<ClangModulesDeclVendor>
ClangASTSource::GetClangModulesDeclVendor() {
  auto persistent_vars = llvm::cast<ClangPersistentVariables>(
      m_target->GetPersistentExpressionStateForLanguage(lldb::eLanguageTypeC));
  return persistent_vars->GetClangModulesDeclVendor();
}
