//===-- ClangASTImporter.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/Module.h"
#include "lldb/Utility/LLDBAssert.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/RecordLayout.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/Sema.h"
#include "llvm/Support/raw_ostream.h"

#include "Plugins/ExpressionParser/Clang/ClangASTImporter.h"
#include "Plugins/ExpressionParser/Clang/ClangASTMetadata.h"
#include "Plugins/ExpressionParser/Clang/ClangASTSource.h"
#include "Plugins/ExpressionParser/Clang/ClangExternalASTSourceCallbacks.h"
#include "Plugins/ExpressionParser/Clang/ClangUtil.h"
#include "Plugins/TypeSystem/Clang/TypeSystemClang.h"

#include <memory>
#include <optional>
#include <type_traits>

using namespace lldb_private;
using namespace clang;

CompilerType ClangASTImporter::CopyType(TypeSystemClang &dst_ast,
                                        const CompilerType &src_type) {
  clang::ASTContext &dst_clang_ast = dst_ast.getASTContext();

  auto src_ast = src_type.GetTypeSystem().dyn_cast_or_null<TypeSystemClang>();
  if (!src_ast)
    return CompilerType();

  clang::ASTContext &src_clang_ast = src_ast->getASTContext();

  clang::QualType src_qual_type = ClangUtil::GetQualType(src_type);

  ImporterDelegateSP delegate_sp(GetDelegate(&dst_clang_ast, &src_clang_ast));
  if (!delegate_sp)
    return CompilerType();

  ASTImporterDelegate::CxxModuleScope std_scope(*delegate_sp, &dst_clang_ast);

  llvm::Expected<QualType> ret_or_error = delegate_sp->Import(src_qual_type);
  if (!ret_or_error) {
    Log *log = GetLog(LLDBLog::Expressions);
    LLDB_LOG_ERROR(log, ret_or_error.takeError(),
        "Couldn't import type: {0}");
    return CompilerType();
  }

  lldb::opaque_compiler_type_t dst_clang_type = ret_or_error->getAsOpaquePtr();

  if (dst_clang_type)
    return CompilerType(dst_ast.weak_from_this(), dst_clang_type);
  return CompilerType();
}

clang::Decl *ClangASTImporter::CopyDecl(clang::ASTContext *dst_ast,
                                        clang::Decl *decl) {
  ImporterDelegateSP delegate_sp;

  clang::ASTContext *src_ast = &decl->getASTContext();
  delegate_sp = GetDelegate(dst_ast, src_ast);

  ASTImporterDelegate::CxxModuleScope std_scope(*delegate_sp, dst_ast);

  if (!delegate_sp)
    return nullptr;

  llvm::Expected<clang::Decl *> result = delegate_sp->Import(decl);
  if (!result) {
    Log *log = GetLog(LLDBLog::Expressions);
    LLDB_LOG_ERROR(log, result.takeError(), "Couldn't import decl: {0}");
    if (log) {
      lldb::user_id_t user_id = LLDB_INVALID_UID;
      ClangASTMetadata *metadata = GetDeclMetadata(decl);
      if (metadata)
        user_id = metadata->GetUserID();

      if (NamedDecl *named_decl = dyn_cast<NamedDecl>(decl))
        LLDB_LOG(log,
                 "  [ClangASTImporter] WARNING: Failed to import a {0} "
                 "'{1}', metadata {2}",
                 decl->getDeclKindName(), named_decl->getNameAsString(),
                 user_id);
      else
        LLDB_LOG(log,
                 "  [ClangASTImporter] WARNING: Failed to import a {0}, "
                 "metadata {1}",
                 decl->getDeclKindName(), user_id);
    }
    return nullptr;
  }

  return *result;
}

class DeclContextOverride {
private:
  struct Backup {
    clang::DeclContext *decl_context;
    clang::DeclContext *lexical_decl_context;
  };

  llvm::DenseMap<clang::Decl *, Backup> m_backups;

  void OverrideOne(clang::Decl *decl) {
    if (m_backups.contains(decl)) {
      return;
    }

    m_backups[decl] = {decl->getDeclContext(), decl->getLexicalDeclContext()};

    decl->setDeclContext(decl->getASTContext().getTranslationUnitDecl());
    decl->setLexicalDeclContext(decl->getASTContext().getTranslationUnitDecl());
  }

  bool ChainPassesThrough(
      clang::Decl *decl, clang::DeclContext *base,
      clang::DeclContext *(clang::Decl::*contextFromDecl)(),
      clang::DeclContext *(clang::DeclContext::*contextFromContext)()) {
    for (DeclContext *decl_ctx = (decl->*contextFromDecl)(); decl_ctx;
         decl_ctx = (decl_ctx->*contextFromContext)()) {
      if (decl_ctx == base) {
        return true;
      }
    }

    return false;
  }

  clang::Decl *GetEscapedChild(clang::Decl *decl,
                               clang::DeclContext *base = nullptr) {
    if (base) {
      // decl's DeclContext chains must pass through base.

      if (!ChainPassesThrough(decl, base, &clang::Decl::getDeclContext,
                              &clang::DeclContext::getParent) ||
          !ChainPassesThrough(decl, base, &clang::Decl::getLexicalDeclContext,
                              &clang::DeclContext::getLexicalParent)) {
        return decl;
      }
    } else {
      base = clang::dyn_cast<clang::DeclContext>(decl);

      if (!base) {
        return nullptr;
      }
    }

    if (clang::DeclContext *context =
            clang::dyn_cast<clang::DeclContext>(decl)) {
      for (clang::Decl *decl : context->decls()) {
        if (clang::Decl *escaped_child = GetEscapedChild(decl)) {
          return escaped_child;
        }
      }
    }

    return nullptr;
  }

  void Override(clang::Decl *decl) {
    if (clang::Decl *escaped_child = GetEscapedChild(decl)) {
      Log *log = GetLog(LLDBLog::Expressions);

      LLDB_LOG(log,
               "    [ClangASTImporter] DeclContextOverride couldn't "
               "override ({0}Decl*){1} - its child ({2}Decl*){3} escapes",
               decl->getDeclKindName(), decl, escaped_child->getDeclKindName(),
               escaped_child);
      lldbassert(0 && "Couldn't override!");
    }

    OverrideOne(decl);
  }

public:
  DeclContextOverride() = default;

  void OverrideAllDeclsFromContainingFunction(clang::Decl *decl) {
    for (DeclContext *decl_context = decl->getLexicalDeclContext();
         decl_context; decl_context = decl_context->getLexicalParent()) {
      DeclContext *redecl_context = decl_context->getRedeclContext();

      if (llvm::isa<FunctionDecl>(redecl_context) &&
          llvm::isa<TranslationUnitDecl>(redecl_context->getLexicalParent())) {
        for (clang::Decl *child_decl : decl_context->decls()) {
          Override(child_decl);
        }
      }
    }
  }

  ~DeclContextOverride() {
    for (const std::pair<clang::Decl *, Backup> &backup : m_backups) {
      backup.first->setDeclContext(backup.second.decl_context);
      backup.first->setLexicalDeclContext(backup.second.lexical_decl_context);
    }
  }
};

namespace {
/// Completes all imported TagDecls at the end of the scope.
///
/// While in a CompleteTagDeclsScope, every decl that could be completed will
/// be completed at the end of the scope (including all Decls that are
/// imported while completing the original Decls).
class CompleteTagDeclsScope : public ClangASTImporter::NewDeclListener {
  ClangASTImporter::ImporterDelegateSP m_delegate;
  /// List of declarations in the target context that need to be completed.
  /// Every declaration should only be completed once and therefore should only
  /// be once in this list.
  llvm::SetVector<NamedDecl *> m_decls_to_complete;
  /// Set of declarations that already were successfully completed (not just
  /// added to m_decls_to_complete).
  llvm::SmallPtrSet<NamedDecl *, 32> m_decls_already_completed;
  clang::ASTContext *m_dst_ctx;
  clang::ASTContext *m_src_ctx;
  ClangASTImporter &importer;

public:
  /// Constructs a CompleteTagDeclsScope.
  /// \param importer The ClangASTImporter that we should observe.
  /// \param dst_ctx The ASTContext to which Decls are imported.
  /// \param src_ctx The ASTContext from which Decls are imported.
  explicit CompleteTagDeclsScope(ClangASTImporter &importer,
                            clang::ASTContext *dst_ctx,
                            clang::ASTContext *src_ctx)
      : m_delegate(importer.GetDelegate(dst_ctx, src_ctx)), m_dst_ctx(dst_ctx),
        m_src_ctx(src_ctx), importer(importer) {
    m_delegate->SetImportListener(this);
  }

  ~CompleteTagDeclsScope() override {
    ClangASTImporter::ASTContextMetadataSP to_context_md =
        importer.GetContextMetadata(m_dst_ctx);

    // Complete all decls we collected until now.
    while (!m_decls_to_complete.empty()) {
      NamedDecl *decl = m_decls_to_complete.pop_back_val();
      m_decls_already_completed.insert(decl);

      // The decl that should be completed has to be imported into the target
      // context from some other context.
      assert(to_context_md->hasOrigin(decl));
      // We should only complete decls coming from the source context.
      assert(to_context_md->getOrigin(decl).ctx == m_src_ctx);

      Decl *original_decl = to_context_md->getOrigin(decl).decl;

      // Complete the decl now.
      TypeSystemClang::GetCompleteDecl(m_src_ctx, original_decl);
      if (auto *tag_decl = dyn_cast<TagDecl>(decl)) {
        if (auto *original_tag_decl = dyn_cast<TagDecl>(original_decl)) {
          if (original_tag_decl->isCompleteDefinition()) {
            m_delegate->ImportDefinitionTo(tag_decl, original_tag_decl);
            tag_decl->setCompleteDefinition(true);
          }
        }

        tag_decl->setHasExternalLexicalStorage(false);
        tag_decl->setHasExternalVisibleStorage(false);
      } else if (auto *container_decl = dyn_cast<ObjCContainerDecl>(decl)) {
        container_decl->setHasExternalLexicalStorage(false);
        container_decl->setHasExternalVisibleStorage(false);
      }

      to_context_md->removeOrigin(decl);
    }

    // Stop listening to imported decls. We do this after clearing the
    // Decls we needed to import to catch all Decls they might have pulled in.
    m_delegate->RemoveImportListener();
  }

  void NewDeclImported(clang::Decl *from, clang::Decl *to) override {
    // Filter out decls that we can't complete later.
    if (!isa<TagDecl>(to) && !isa<ObjCInterfaceDecl>(to))
      return;
    RecordDecl *from_record_decl = dyn_cast<RecordDecl>(from);
    // We don't need to complete injected class name decls.
    if (from_record_decl && from_record_decl->isInjectedClassName())
      return;

    NamedDecl *to_named_decl = dyn_cast<NamedDecl>(to);
    // Check if we already completed this type.
    if (m_decls_already_completed.contains(to_named_decl))
      return;
    // Queue this type to be completed.
    m_decls_to_complete.insert(to_named_decl);
  }
};
} // namespace

CompilerType ClangASTImporter::DeportType(TypeSystemClang &dst,
                                          const CompilerType &src_type) {
  Log *log = GetLog(LLDBLog::Expressions);

  auto src_ctxt = src_type.GetTypeSystem().dyn_cast_or_null<TypeSystemClang>();
  if (!src_ctxt)
    return {};

  LLDB_LOG(log,
           "    [ClangASTImporter] DeportType called on ({0}Type*){1:x} "
           "from (ASTContext*){2:x} to (ASTContext*){3:x}",
           src_type.GetTypeName(), src_type.GetOpaqueQualType(),
           &src_ctxt->getASTContext(), &dst.getASTContext());

  DeclContextOverride decl_context_override;

  if (auto *t = ClangUtil::GetQualType(src_type)->getAs<TagType>())
    decl_context_override.OverrideAllDeclsFromContainingFunction(t->getDecl());

  CompleteTagDeclsScope complete_scope(*this, &dst.getASTContext(),
                                       &src_ctxt->getASTContext());
  return CopyType(dst, src_type);
}

clang::Decl *ClangASTImporter::DeportDecl(clang::ASTContext *dst_ctx,
                                          clang::Decl *decl) {
  Log *log = GetLog(LLDBLog::Expressions);

  clang::ASTContext *src_ctx = &decl->getASTContext();
  LLDB_LOG(log,
           "    [ClangASTImporter] DeportDecl called on ({0}Decl*){1:x} from "
           "(ASTContext*){2:x} to (ASTContext*){3:x}",
           decl->getDeclKindName(), decl, src_ctx, dst_ctx);

  DeclContextOverride decl_context_override;

  decl_context_override.OverrideAllDeclsFromContainingFunction(decl);

  clang::Decl *result;
  {
    CompleteTagDeclsScope complete_scope(*this, dst_ctx, src_ctx);
    result = CopyDecl(dst_ctx, decl);
  }

  if (!result)
    return nullptr;

  LLDB_LOG(log,
           "    [ClangASTImporter] DeportDecl deported ({0}Decl*){1:x} to "
           "({2}Decl*){3:x}",
           decl->getDeclKindName(), decl, result->getDeclKindName(), result);

  return result;
}

bool ClangASTImporter::CanImport(const CompilerType &type) {
  if (!ClangUtil::IsClangType(type))
    return false;

  clang::QualType qual_type(
      ClangUtil::GetCanonicalQualType(ClangUtil::RemoveFastQualifiers(type)));

  const clang::Type::TypeClass type_class = qual_type->getTypeClass();
  switch (type_class) {
  case clang::Type::Record: {
    const clang::CXXRecordDecl *cxx_record_decl =
        qual_type->getAsCXXRecordDecl();
    if (cxx_record_decl) {
      if (GetDeclOrigin(cxx_record_decl).Valid())
        return true;
    }
  } break;

  case clang::Type::Enum: {
    clang::EnumDecl *enum_decl =
        llvm::cast<clang::EnumType>(qual_type)->getDecl();
    if (enum_decl) {
      if (GetDeclOrigin(enum_decl).Valid())
        return true;
    }
  } break;

  case clang::Type::ObjCObject:
  case clang::Type::ObjCInterface: {
    const clang::ObjCObjectType *objc_class_type =
        llvm::dyn_cast<clang::ObjCObjectType>(qual_type);
    if (objc_class_type) {
      clang::ObjCInterfaceDecl *class_interface_decl =
          objc_class_type->getInterface();
      // We currently can't complete objective C types through the newly added
      // ASTContext because it only supports TagDecl objects right now...
      if (class_interface_decl) {
        if (GetDeclOrigin(class_interface_decl).Valid())
          return true;
      }
    }
  } break;

  case clang::Type::Typedef:
    return CanImport(CompilerType(type.GetTypeSystem(),
                                  llvm::cast<clang::TypedefType>(qual_type)
                                      ->getDecl()
                                      ->getUnderlyingType()
                                      .getAsOpaquePtr()));

  case clang::Type::Auto:
    return CanImport(CompilerType(type.GetTypeSystem(),
                                  llvm::cast<clang::AutoType>(qual_type)
                                      ->getDeducedType()
                                      .getAsOpaquePtr()));

  case clang::Type::Elaborated:
    return CanImport(CompilerType(type.GetTypeSystem(),
                                  llvm::cast<clang::ElaboratedType>(qual_type)
                                      ->getNamedType()
                                      .getAsOpaquePtr()));

  case clang::Type::Paren:
    return CanImport(CompilerType(
        type.GetTypeSystem(),
        llvm::cast<clang::ParenType>(qual_type)->desugar().getAsOpaquePtr()));

  default:
    break;
  }

  return false;
}

bool ClangASTImporter::Import(const CompilerType &type) {
  if (!ClangUtil::IsClangType(type))
    return false;

  clang::QualType qual_type(
      ClangUtil::GetCanonicalQualType(ClangUtil::RemoveFastQualifiers(type)));

  const clang::Type::TypeClass type_class = qual_type->getTypeClass();
  switch (type_class) {
  case clang::Type::Record: {
    const clang::CXXRecordDecl *cxx_record_decl =
        qual_type->getAsCXXRecordDecl();
    if (cxx_record_decl) {
      if (GetDeclOrigin(cxx_record_decl).Valid())
        return CompleteAndFetchChildren(qual_type);
    }
  } break;

  case clang::Type::Enum: {
    clang::EnumDecl *enum_decl =
        llvm::cast<clang::EnumType>(qual_type)->getDecl();
    if (enum_decl) {
      if (GetDeclOrigin(enum_decl).Valid())
        return CompleteAndFetchChildren(qual_type);
    }
  } break;

  case clang::Type::ObjCObject:
  case clang::Type::ObjCInterface: {
    const clang::ObjCObjectType *objc_class_type =
        llvm::dyn_cast<clang::ObjCObjectType>(qual_type);
    if (objc_class_type) {
      clang::ObjCInterfaceDecl *class_interface_decl =
          objc_class_type->getInterface();
      // We currently can't complete objective C types through the newly added
      // ASTContext because it only supports TagDecl objects right now...
      if (class_interface_decl) {
        if (GetDeclOrigin(class_interface_decl).Valid())
          return CompleteAndFetchChildren(qual_type);
      }
    }
  } break;

  case clang::Type::Typedef:
    return Import(CompilerType(type.GetTypeSystem(),
                               llvm::cast<clang::TypedefType>(qual_type)
                                   ->getDecl()
                                   ->getUnderlyingType()
                                   .getAsOpaquePtr()));

  case clang::Type::Auto:
    return Import(CompilerType(type.GetTypeSystem(),
                               llvm::cast<clang::AutoType>(qual_type)
                                   ->getDeducedType()
                                   .getAsOpaquePtr()));

  case clang::Type::Elaborated:
    return Import(CompilerType(type.GetTypeSystem(),
                               llvm::cast<clang::ElaboratedType>(qual_type)
                                   ->getNamedType()
                                   .getAsOpaquePtr()));

  case clang::Type::Paren:
    return Import(CompilerType(
        type.GetTypeSystem(),
        llvm::cast<clang::ParenType>(qual_type)->desugar().getAsOpaquePtr()));

  default:
    break;
  }
  return false;
}

bool ClangASTImporter::CompleteType(const CompilerType &compiler_type) {
  if (!CanImport(compiler_type))
    return false;

  if (Import(compiler_type)) {
    TypeSystemClang::CompleteTagDeclarationDefinition(compiler_type);
    return true;
  }

  TypeSystemClang::SetHasExternalStorage(compiler_type.GetOpaqueQualType(),
                                         false);
  return false;
}

/// Copy layout information from \ref source_map to the \ref destination_map.
///
/// In the process of copying over layout info, we may need to import
/// decls from the \ref source_map. This function will use the supplied
/// \ref importer to import the necessary decls into \ref dest_ctx.
///
/// \param[in,out] dest_ctx Destination ASTContext into which we import
///                         decls from the \ref source_map.
/// \param[out]    destination_map A map from decls in \ref dest_ctx to an
///                                integral offest, which will be copies
///                                of the decl/offest pairs in \ref source_map
///                                if successful.
/// \param[in]     source_map A map from decls to integral offests. These will
///                           be copied into \ref destination_map.
/// \param[in,out] importer Used to import decls into \ref dest_ctx.
///
/// \returns On success, will return 'true' and the offsets in \ref
/// destination_map
///          are usable copies of \ref source_map.
template <class D, class O>
static bool ImportOffsetMap(clang::ASTContext *dest_ctx,
                            llvm::DenseMap<const D *, O> &destination_map,
                            llvm::DenseMap<const D *, O> &source_map,
                            ClangASTImporter &importer) {
  // When importing fields into a new record, clang has a hard requirement that
  // fields be imported in field offset order.  Since they are stored in a
  // DenseMap with a pointer as the key type, this means we cannot simply
  // iterate over the map, as the order will be non-deterministic.  Instead we
  // have to sort by the offset and then insert in sorted order.
  typedef llvm::DenseMap<const D *, O> MapType;
  typedef typename MapType::value_type PairType;
  std::vector<PairType> sorted_items;
  sorted_items.reserve(source_map.size());
  sorted_items.assign(source_map.begin(), source_map.end());
  llvm::sort(sorted_items, llvm::less_second());

  for (const auto &item : sorted_items) {
    DeclFromUser<D> user_decl(const_cast<D *>(item.first));
    DeclFromParser<D> parser_decl(user_decl.Import(dest_ctx, importer));
    if (parser_decl.IsInvalid())
      return false;
    destination_map.insert(
        std::pair<const D *, O>(parser_decl.decl, item.second));
  }

  return true;
}

/// Given a CXXRecordDecl, will calculate and populate \ref base_offsets
/// with the integral offsets of any of its (possibly virtual) base classes.
///
/// \param[in] record_layout ASTRecordLayout of \ref record.
/// \param[in] record The record that we're calculating the base layouts of.
/// \param[out] base_offsets Map of base-class decl to integral offset which
///                          this function will fill in.
///
/// \returns On success, will return 'true' and the offsets in \ref base_offsets
///          are usable.
template <bool IsVirtual>
bool ExtractBaseOffsets(const ASTRecordLayout &record_layout,
                        DeclFromUser<const CXXRecordDecl> &record,
                        llvm::DenseMap<const clang::CXXRecordDecl *,
                                       clang::CharUnits> &base_offsets) {
  for (CXXRecordDecl::base_class_const_iterator
           bi = (IsVirtual ? record->vbases_begin() : record->bases_begin()),
           be = (IsVirtual ? record->vbases_end() : record->bases_end());
       bi != be; ++bi) {
    if (!IsVirtual && bi->isVirtual())
      continue;

    const clang::Type *origin_base_type = bi->getType().getTypePtr();
    const clang::RecordType *origin_base_record_type =
        origin_base_type->getAs<RecordType>();

    if (!origin_base_record_type)
      return false;

    DeclFromUser<RecordDecl> origin_base_record(
        origin_base_record_type->getDecl());

    if (origin_base_record.IsInvalid())
      return false;

    DeclFromUser<CXXRecordDecl> origin_base_cxx_record(
        DynCast<CXXRecordDecl>(origin_base_record));

    if (origin_base_cxx_record.IsInvalid())
      return false;

    CharUnits base_offset;

    if (IsVirtual)
      base_offset =
          record_layout.getVBaseClassOffset(origin_base_cxx_record.decl);
    else
      base_offset =
          record_layout.getBaseClassOffset(origin_base_cxx_record.decl);

    base_offsets.insert(std::pair<const CXXRecordDecl *, CharUnits>(
        origin_base_cxx_record.decl, base_offset));
  }

  return true;
}

bool ClangASTImporter::importRecordLayoutFromOrigin(
    const RecordDecl *record, uint64_t &size, uint64_t &alignment,
    llvm::DenseMap<const clang::FieldDecl *, uint64_t> &field_offsets,
    llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits>
        &base_offsets,
    llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits>
        &vbase_offsets) {

  Log *log = GetLog(LLDBLog::Expressions);

  clang::ASTContext &dest_ctx = record->getASTContext();
  LLDB_LOG(log,
           "LayoutRecordType on (ASTContext*){0:x} '{1}' for (RecordDecl*)"
           "{2:x} [name = '{3}']",
           &dest_ctx,
           TypeSystemClang::GetASTContext(&dest_ctx)->getDisplayName(), record,
           record->getName());

  DeclFromParser<const RecordDecl> parser_record(record);
  DeclFromUser<const RecordDecl> origin_record(parser_record.GetOrigin(*this));

  if (origin_record.IsInvalid())
    return false;

  std::remove_reference_t<decltype(field_offsets)> origin_field_offsets;
  std::remove_reference_t<decltype(base_offsets)> origin_base_offsets;
  std::remove_reference_t<decltype(vbase_offsets)> origin_virtual_base_offsets;

  TypeSystemClang::GetCompleteDecl(
      &origin_record->getASTContext(),
      const_cast<RecordDecl *>(origin_record.decl));

  clang::RecordDecl *definition = origin_record.decl->getDefinition();
  if (!definition || !definition->isCompleteDefinition())
    return false;

  const ASTRecordLayout &record_layout(
      origin_record->getASTContext().getASTRecordLayout(origin_record.decl));

  int field_idx = 0, field_count = record_layout.getFieldCount();

  for (RecordDecl::field_iterator fi = origin_record->field_begin(),
                                  fe = origin_record->field_end();
       fi != fe; ++fi) {
    if (field_idx >= field_count)
      return false; // Layout didn't go well.  Bail out.

    uint64_t field_offset = record_layout.getFieldOffset(field_idx);

    origin_field_offsets.insert(
        std::pair<const FieldDecl *, uint64_t>(*fi, field_offset));

    field_idx++;
  }

  DeclFromUser<const CXXRecordDecl> origin_cxx_record(
      DynCast<const CXXRecordDecl>(origin_record));

  if (origin_cxx_record.IsValid()) {
    if (!ExtractBaseOffsets<false>(record_layout, origin_cxx_record,
                                   origin_base_offsets) ||
        !ExtractBaseOffsets<true>(record_layout, origin_cxx_record,
                                  origin_virtual_base_offsets))
      return false;
  }

  if (!ImportOffsetMap(&dest_ctx, field_offsets, origin_field_offsets, *this) ||
      !ImportOffsetMap(&dest_ctx, base_offsets, origin_base_offsets, *this) ||
      !ImportOffsetMap(&dest_ctx, vbase_offsets, origin_virtual_base_offsets,
                       *this))
    return false;

  size = record_layout.getSize().getQuantity() * dest_ctx.getCharWidth();
  alignment =
      record_layout.getAlignment().getQuantity() * dest_ctx.getCharWidth();

  if (log) {
    LLDB_LOG(log, "LRT returned:");
    LLDB_LOG(log, "LRT   Original = (RecordDecl*){0:x}",
             static_cast<const void *>(origin_record.decl));
    LLDB_LOG(log, "LRT   Size = {0}", size);
    LLDB_LOG(log, "LRT   Alignment = {0}", alignment);
    LLDB_LOG(log, "LRT   Fields:");
    for (RecordDecl::field_iterator fi = record->field_begin(),
                                    fe = record->field_end();
         fi != fe; ++fi) {
      LLDB_LOG(
          log,
          "LRT     (FieldDecl*){0:x}, Name = '{1}', Type = '{2}', Offset = "
          "{3} bits",
          *fi, fi->getName(), fi->getType().getAsString(), field_offsets[*fi]);
    }
    DeclFromParser<const CXXRecordDecl> parser_cxx_record =
        DynCast<const CXXRecordDecl>(parser_record);
    if (parser_cxx_record.IsValid()) {
      LLDB_LOG(log, "LRT   Bases:");
      for (CXXRecordDecl::base_class_const_iterator
               bi = parser_cxx_record->bases_begin(),
               be = parser_cxx_record->bases_end();
           bi != be; ++bi) {
        bool is_virtual = bi->isVirtual();

        QualType base_type = bi->getType();
        const RecordType *base_record_type = base_type->getAs<RecordType>();
        DeclFromParser<RecordDecl> base_record(base_record_type->getDecl());
        DeclFromParser<CXXRecordDecl> base_cxx_record =
            DynCast<CXXRecordDecl>(base_record);

        LLDB_LOG(log,
                 "LRT     {0}(CXXRecordDecl*){1:x}, Name = '{2}', Offset = "
                 "{3} chars",
                 (is_virtual ? "Virtual " : ""), base_cxx_record.decl,
                 base_cxx_record.decl->getName(),
                 (is_virtual
                      ? vbase_offsets[base_cxx_record.decl].getQuantity()
                      : base_offsets[base_cxx_record.decl].getQuantity()));
      }
    } else {
      LLDB_LOG(log, "LRD   Not a CXXRecord, so no bases");
    }
  }

  return true;
}

bool ClangASTImporter::LayoutRecordType(
    const clang::RecordDecl *record_decl, uint64_t &bit_size,
    uint64_t &alignment,
    llvm::DenseMap<const clang::FieldDecl *, uint64_t> &field_offsets,
    llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits>
        &base_offsets,
    llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits>
        &vbase_offsets) {
  RecordDeclToLayoutMap::iterator pos =
      m_record_decl_to_layout_map.find(record_decl);
  base_offsets.clear();
  vbase_offsets.clear();
  if (pos != m_record_decl_to_layout_map.end()) {
    bit_size = pos->second.bit_size;
    alignment = pos->second.alignment;
    field_offsets.swap(pos->second.field_offsets);
    base_offsets.swap(pos->second.base_offsets);
    vbase_offsets.swap(pos->second.vbase_offsets);
    m_record_decl_to_layout_map.erase(pos);
    return true;
  }

  // It's possible that we calculated the layout in a different
  // ClangASTImporter instance. Try to import such layout if
  // our decl has an origin.
  if (auto origin = GetDeclOrigin(record_decl); origin.Valid())
    if (importRecordLayoutFromOrigin(record_decl, bit_size, alignment,
                                     field_offsets, base_offsets,
                                     vbase_offsets))
      return true;

  bit_size = 0;
  alignment = 0;
  field_offsets.clear();

  return false;
}

void ClangASTImporter::SetRecordLayout(clang::RecordDecl *decl,
                                        const LayoutInfo &layout) {
  m_record_decl_to_layout_map.insert(std::make_pair(decl, layout));
}

bool ClangASTImporter::CompleteTagDecl(clang::TagDecl *decl) {
  DeclOrigin decl_origin = GetDeclOrigin(decl);

  if (!decl_origin.Valid())
    return false;

  if (!TypeSystemClang::GetCompleteDecl(decl_origin.ctx, decl_origin.decl))
    return false;

  ImporterDelegateSP delegate_sp(
      GetDelegate(&decl->getASTContext(), decl_origin.ctx));

  ASTImporterDelegate::CxxModuleScope std_scope(*delegate_sp,
                                                &decl->getASTContext());
  if (delegate_sp)
    delegate_sp->ImportDefinitionTo(decl, decl_origin.decl);

  return true;
}

bool ClangASTImporter::CompleteTagDeclWithOrigin(clang::TagDecl *decl,
                                                 clang::TagDecl *origin_decl) {
  clang::ASTContext *origin_ast_ctx = &origin_decl->getASTContext();

  if (!TypeSystemClang::GetCompleteDecl(origin_ast_ctx, origin_decl))
    return false;

  ImporterDelegateSP delegate_sp(
      GetDelegate(&decl->getASTContext(), origin_ast_ctx));

  if (delegate_sp)
    delegate_sp->ImportDefinitionTo(decl, origin_decl);

  ASTContextMetadataSP context_md = GetContextMetadata(&decl->getASTContext());

  context_md->setOrigin(decl, DeclOrigin(origin_ast_ctx, origin_decl));
  return true;
}

bool ClangASTImporter::CompleteObjCInterfaceDecl(
    clang::ObjCInterfaceDecl *interface_decl) {
  DeclOrigin decl_origin = GetDeclOrigin(interface_decl);

  if (!decl_origin.Valid())
    return false;

  if (!TypeSystemClang::GetCompleteDecl(decl_origin.ctx, decl_origin.decl))
    return false;

  ImporterDelegateSP delegate_sp(
      GetDelegate(&interface_decl->getASTContext(), decl_origin.ctx));

  if (delegate_sp)
    delegate_sp->ImportDefinitionTo(interface_decl, decl_origin.decl);

  if (ObjCInterfaceDecl *super_class = interface_decl->getSuperClass())
    RequireCompleteType(clang::QualType(super_class->getTypeForDecl(), 0));

  return true;
}

bool ClangASTImporter::CompleteAndFetchChildren(clang::QualType type) {
  if (!RequireCompleteType(type))
    return false;

  Log *log = GetLog(LLDBLog::Expressions);

  if (const TagType *tag_type = type->getAs<TagType>()) {
    TagDecl *tag_decl = tag_type->getDecl();

    DeclOrigin decl_origin = GetDeclOrigin(tag_decl);

    if (!decl_origin.Valid())
      return false;

    ImporterDelegateSP delegate_sp(
        GetDelegate(&tag_decl->getASTContext(), decl_origin.ctx));

    ASTImporterDelegate::CxxModuleScope std_scope(*delegate_sp,
                                                  &tag_decl->getASTContext());

    TagDecl *origin_tag_decl = llvm::dyn_cast<TagDecl>(decl_origin.decl);

    for (Decl *origin_child_decl : origin_tag_decl->decls()) {
      llvm::Expected<Decl *> imported_or_err =
          delegate_sp->Import(origin_child_decl);
      if (!imported_or_err) {
        LLDB_LOG_ERROR(log, imported_or_err.takeError(),
                       "Couldn't import decl: {0}");
        return false;
      }
    }

    if (RecordDecl *record_decl = dyn_cast<RecordDecl>(origin_tag_decl))
      record_decl->setHasLoadedFieldsFromExternalStorage(true);

    return true;
  }

  if (const ObjCObjectType *objc_object_type = type->getAs<ObjCObjectType>()) {
    if (ObjCInterfaceDecl *objc_interface_decl =
            objc_object_type->getInterface()) {
      DeclOrigin decl_origin = GetDeclOrigin(objc_interface_decl);

      if (!decl_origin.Valid())
        return false;

      ImporterDelegateSP delegate_sp(
          GetDelegate(&objc_interface_decl->getASTContext(), decl_origin.ctx));

      ObjCInterfaceDecl *origin_interface_decl =
          llvm::dyn_cast<ObjCInterfaceDecl>(decl_origin.decl);

      for (Decl *origin_child_decl : origin_interface_decl->decls()) {
        llvm::Expected<Decl *> imported_or_err =
            delegate_sp->Import(origin_child_decl);
        if (!imported_or_err) {
          LLDB_LOG_ERROR(log, imported_or_err.takeError(),
                         "Couldn't import decl: {0}");
          return false;
        }
      }

      return true;
    }
    return false;
  }

  return true;
}

bool ClangASTImporter::RequireCompleteType(clang::QualType type) {
  if (type.isNull())
    return false;

  if (const TagType *tag_type = type->getAs<TagType>()) {
    TagDecl *tag_decl = tag_type->getDecl();

    if (tag_decl->getDefinition() || tag_decl->isBeingDefined())
      return true;

    return CompleteTagDecl(tag_decl);
  }
  if (const ObjCObjectType *objc_object_type = type->getAs<ObjCObjectType>()) {
    if (ObjCInterfaceDecl *objc_interface_decl =
            objc_object_type->getInterface())
      return CompleteObjCInterfaceDecl(objc_interface_decl);
    return false;
  }
  if (const ArrayType *array_type = type->getAsArrayTypeUnsafe())
    return RequireCompleteType(array_type->getElementType());
  if (const AtomicType *atomic_type = type->getAs<AtomicType>())
    return RequireCompleteType(atomic_type->getPointeeType());

  return true;
}

ClangASTMetadata *ClangASTImporter::GetDeclMetadata(const clang::Decl *decl) {
  DeclOrigin decl_origin = GetDeclOrigin(decl);

  if (decl_origin.Valid()) {
    TypeSystemClang *ast = TypeSystemClang::GetASTContext(decl_origin.ctx);
    return ast->GetMetadata(decl_origin.decl);
  }
  TypeSystemClang *ast = TypeSystemClang::GetASTContext(&decl->getASTContext());
  return ast->GetMetadata(decl);
}

ClangASTImporter::DeclOrigin
ClangASTImporter::GetDeclOrigin(const clang::Decl *decl) {
  ASTContextMetadataSP context_md = GetContextMetadata(&decl->getASTContext());

  return context_md->getOrigin(decl);
}

void ClangASTImporter::SetDeclOrigin(const clang::Decl *decl,
                                     clang::Decl *original_decl) {
  ASTContextMetadataSP context_md = GetContextMetadata(&decl->getASTContext());
  context_md->setOrigin(
      decl, DeclOrigin(&original_decl->getASTContext(), original_decl));
}

void ClangASTImporter::RegisterNamespaceMap(const clang::NamespaceDecl *decl,
                                            NamespaceMapSP &namespace_map) {
  ASTContextMetadataSP context_md = GetContextMetadata(&decl->getASTContext());

  context_md->m_namespace_maps[decl] = namespace_map;
}

ClangASTImporter::NamespaceMapSP
ClangASTImporter::GetNamespaceMap(const clang::NamespaceDecl *decl) {
  ASTContextMetadataSP context_md = GetContextMetadata(&decl->getASTContext());

  NamespaceMetaMap &namespace_maps = context_md->m_namespace_maps;

  NamespaceMetaMap::iterator iter = namespace_maps.find(decl);

  if (iter != namespace_maps.end())
    return iter->second;
  return NamespaceMapSP();
}

void ClangASTImporter::BuildNamespaceMap(const clang::NamespaceDecl *decl) {
  assert(decl);
  ASTContextMetadataSP context_md = GetContextMetadata(&decl->getASTContext());

  const DeclContext *parent_context = decl->getDeclContext();
  const NamespaceDecl *parent_namespace =
      dyn_cast<NamespaceDecl>(parent_context);
  NamespaceMapSP parent_map;

  if (parent_namespace)
    parent_map = GetNamespaceMap(parent_namespace);

  NamespaceMapSP new_map;

  new_map = std::make_shared<NamespaceMap>();

  if (context_md->m_map_completer) {
    std::string namespace_string = decl->getDeclName().getAsString();

    context_md->m_map_completer->CompleteNamespaceMap(
        new_map, ConstString(namespace_string.c_str()), parent_map);
  }

  context_md->m_namespace_maps[decl] = new_map;
}

void ClangASTImporter::ForgetDestination(clang::ASTContext *dst_ast) {
  Log *log = GetLog(LLDBLog::Expressions);

  LLDB_LOG(log,
           "    [ClangASTImporter] Forgetting destination (ASTContext*){0:x}",
           dst_ast);

  m_metadata_map.erase(dst_ast);
}

void ClangASTImporter::ForgetSource(clang::ASTContext *dst_ast,
                                    clang::ASTContext *src_ast) {
  ASTContextMetadataSP md = MaybeGetContextMetadata(dst_ast);

  Log *log = GetLog(LLDBLog::Expressions);

  LLDB_LOG(log,
           "    [ClangASTImporter] Forgetting source->dest "
           "(ASTContext*){0:x}->(ASTContext*){1:x}",
           src_ast, dst_ast);

  if (!md)
    return;

  md->m_delegates.erase(src_ast);
  md->removeOriginsWithContext(src_ast);
}

ClangASTImporter::MapCompleter::~MapCompleter() = default;

llvm::Expected<Decl *>
ClangASTImporter::ASTImporterDelegate::ImportImpl(Decl *From) {
  if (m_std_handler) {
    std::optional<Decl *> D = m_std_handler->Import(From);
    if (D) {
      // Make sure we don't use this decl later to map it back to it's original
      // decl. The decl the CxxModuleHandler created has nothing to do with
      // the one from debug info, and linking those two would just cause the
      // ASTImporter to try 'updating' the module decl with the minimal one from
      // the debug info.
      m_decls_to_ignore.insert(*D);
      return *D;
    }
  }

  // Check which ASTContext this declaration originally came from.
  DeclOrigin origin = m_main.GetDeclOrigin(From);

  // Prevent infinite recursion when the origin tracking contains a cycle.
  assert(origin.decl != From && "Origin points to itself?");

  // If it originally came from the target ASTContext then we can just
  // pretend that the original is the one we imported. This can happen for
  // example when inspecting a persistent declaration from the scratch
  // ASTContext (which will provide the declaration when parsing the
  // expression and then we later try to copy the declaration back to the
  // scratch ASTContext to store the result).
  // Without this check we would ask the ASTImporter to import a declaration
  // into the same ASTContext where it came from (which doesn't make a lot of
  // sense).
  if (origin.Valid() && origin.ctx == &getToContext()) {
    RegisterImportedDecl(From, origin.decl);
    return origin.decl;
  }

  // This declaration came originally from another ASTContext. Instead of
  // copying our potentially incomplete 'From' Decl we instead go to the
  // original ASTContext and copy the original to the target. This is not
  // only faster than first completing our current decl and then copying it
  // to the target, but it also prevents that indirectly copying the same
  // declaration to the same target requires the ASTImporter to merge all
  // the different decls that appear to come from different ASTContexts (even
  // though all these different source ASTContexts just got a copy from
  // one source AST).
  if (origin.Valid()) {
    auto R = m_main.CopyDecl(&getToContext(), origin.decl);
    if (R) {
      RegisterImportedDecl(From, R);
      return R;
    }
  }

  // If we have a forcefully completed type, try to find an actual definition
  // for it in other modules.
  const ClangASTMetadata *md = m_main.GetDeclMetadata(From);
  auto *td = dyn_cast<TagDecl>(From);
  if (td && md && md->IsForcefullyCompleted()) {
    Log *log = GetLog(LLDBLog::Expressions);
    LLDB_LOG(log,
             "[ClangASTImporter] Searching for a complete definition of {0} in "
             "other modules",
             td->getName());
    Expected<DeclContext *> dc_or_err = ImportContext(td->getDeclContext());
    if (!dc_or_err)
      return dc_or_err.takeError();
    Expected<DeclarationName> dn_or_err = Import(td->getDeclName());
    if (!dn_or_err)
      return dn_or_err.takeError();
    DeclContext *dc = *dc_or_err;
    DeclContext::lookup_result lr = dc->lookup(*dn_or_err);
    for (clang::Decl *candidate : lr) {
      if (candidate->getKind() == From->getKind()) {
        RegisterImportedDecl(From, candidate);
        m_decls_to_ignore.insert(candidate);
        return candidate;
      }
    }
    LLDB_LOG(log, "[ClangASTImporter] Complete definition not found");
  }

  return ASTImporter::ImportImpl(From);
}

void ClangASTImporter::ASTImporterDelegate::ImportDefinitionTo(
    clang::Decl *to, clang::Decl *from) {
  // We might have a forward declaration from a shared library that we
  // gave external lexical storage so that Clang asks us about the full
  // definition when it needs it. In this case the ASTImporter isn't aware
  // that the forward decl from the shared library is the actual import
  // target but would create a second declaration that would then be defined.
  // We want that 'to' is actually complete after this function so let's
  // tell the ASTImporter that 'to' was imported from 'from'.
  MapImported(from, to);

  Log *log = GetLog(LLDBLog::Expressions);

  if (llvm::Error err = ImportDefinition(from)) {
    LLDB_LOG_ERROR(log, std::move(err),
                   "[ClangASTImporter] Error during importing definition: {0}");
    return;
  }

  if (clang::TagDecl *to_tag = dyn_cast<clang::TagDecl>(to)) {
    if (clang::TagDecl *from_tag = dyn_cast<clang::TagDecl>(from)) {
      to_tag->setCompleteDefinition(from_tag->isCompleteDefinition());

      if (Log *log_ast = GetLog(LLDBLog::AST)) {
        std::string name_string;
        if (NamedDecl *from_named_decl = dyn_cast<clang::NamedDecl>(from)) {
          llvm::raw_string_ostream name_stream(name_string);
          from_named_decl->printName(name_stream);
          name_stream.flush();
        }
        LLDB_LOG(log_ast,
                 "==== [ClangASTImporter][TUDecl: {0:x}] Imported "
                 "({1}Decl*){2:x}, named {3} (from "
                 "(Decl*){4:x})",
                 static_cast<void *>(to->getTranslationUnitDecl()),
                 from->getDeclKindName(), static_cast<void *>(to), name_string,
                 static_cast<void *>(from));

        // Log the AST of the TU.
        std::string ast_string;
        llvm::raw_string_ostream ast_stream(ast_string);
        to->getTranslationUnitDecl()->dump(ast_stream);
        LLDB_LOG(log_ast, "{0}", ast_string);
      }
    }
  }

  // If we're dealing with an Objective-C class, ensure that the inheritance
  // has been set up correctly.  The ASTImporter may not do this correctly if
  // the class was originally sourced from symbols.

  if (ObjCInterfaceDecl *to_objc_interface = dyn_cast<ObjCInterfaceDecl>(to)) {
    ObjCInterfaceDecl *to_superclass = to_objc_interface->getSuperClass();

    if (to_superclass)
      return; // we're not going to override it if it's set

    ObjCInterfaceDecl *from_objc_interface = dyn_cast<ObjCInterfaceDecl>(from);

    if (!from_objc_interface)
      return;

    ObjCInterfaceDecl *from_superclass = from_objc_interface->getSuperClass();

    if (!from_superclass)
      return;

    llvm::Expected<Decl *> imported_from_superclass_decl =
        Import(from_superclass);

    if (!imported_from_superclass_decl) {
      LLDB_LOG_ERROR(log, imported_from_superclass_decl.takeError(),
                     "Couldn't import decl: {0}");
      return;
    }

    ObjCInterfaceDecl *imported_from_superclass =
        dyn_cast<ObjCInterfaceDecl>(*imported_from_superclass_decl);

    if (!imported_from_superclass)
      return;

    if (!to_objc_interface->hasDefinition())
      to_objc_interface->startDefinition();

    to_objc_interface->setSuperClass(m_source_ctx->getTrivialTypeSourceInfo(
        m_source_ctx->getObjCInterfaceType(imported_from_superclass)));
  }
}

/// Takes a CXXMethodDecl and completes the return type if necessary. This
/// is currently only necessary for virtual functions with covariant return
/// types where Clang's CodeGen expects that the underlying records are already
/// completed.
static void MaybeCompleteReturnType(ClangASTImporter &importer,
                                        CXXMethodDecl *to_method) {
  if (!to_method->isVirtual())
    return;
  QualType return_type = to_method->getReturnType();
  if (!return_type->isPointerType() && !return_type->isReferenceType())
    return;

  clang::RecordDecl *rd = return_type->getPointeeType()->getAsRecordDecl();
  if (!rd)
    return;
  if (rd->getDefinition())
    return;

  importer.CompleteTagDecl(rd);
}

/// Recreate a module with its parents in \p to_source and return its id.
static OptionalClangModuleID
RemapModule(OptionalClangModuleID from_id,
            ClangExternalASTSourceCallbacks &from_source,
            ClangExternalASTSourceCallbacks &to_source) {
  if (!from_id.HasValue())
    return {};
  clang::Module *module = from_source.getModule(from_id.GetValue());
  OptionalClangModuleID parent = RemapModule(
      from_source.GetIDForModule(module->Parent), from_source, to_source);
  TypeSystemClang &to_ts = to_source.GetTypeSystem();
  return to_ts.GetOrCreateClangModule(module->Name, parent, module->IsFramework,
                                      module->IsExplicit);
}

void ClangASTImporter::ASTImporterDelegate::Imported(clang::Decl *from,
                                                     clang::Decl *to) {
  Log *log = GetLog(LLDBLog::Expressions);

  // Some decls shouldn't be tracked here because they were not created by
  // copying 'from' to 'to'. Just exit early for those.
  if (m_decls_to_ignore.count(to))
    return;

  // Transfer module ownership information.
  auto *from_source = llvm::dyn_cast_or_null<ClangExternalASTSourceCallbacks>(
      getFromContext().getExternalSource());
  // Can also be a ClangASTSourceProxy.
  auto *to_source = llvm::dyn_cast_or_null<ClangExternalASTSourceCallbacks>(
      getToContext().getExternalSource());
  if (from_source && to_source) {
    OptionalClangModuleID from_id(from->getOwningModuleID());
    OptionalClangModuleID to_id =
        RemapModule(from_id, *from_source, *to_source);
    TypeSystemClang &to_ts = to_source->GetTypeSystem();
    to_ts.SetOwningModule(to, to_id);
  }

  lldb::user_id_t user_id = LLDB_INVALID_UID;
  ClangASTMetadata *metadata = m_main.GetDeclMetadata(from);
  if (metadata)
    user_id = metadata->GetUserID();

  if (log) {
    if (NamedDecl *from_named_decl = dyn_cast<clang::NamedDecl>(from)) {
      std::string name_string;
      llvm::raw_string_ostream name_stream(name_string);
      from_named_decl->printName(name_stream);
      name_stream.flush();

      LLDB_LOG(
          log,
          "    [ClangASTImporter] Imported ({0}Decl*){1:x}, named {2} (from "
          "(Decl*){3:x}), metadata {4}",
          from->getDeclKindName(), to, name_string, from, user_id);
    } else {
      LLDB_LOG(log,
               "    [ClangASTImporter] Imported ({0}Decl*){1:x} (from "
               "(Decl*){2:x}), metadata {3}",
               from->getDeclKindName(), to, from, user_id);
    }
  }

  ASTContextMetadataSP to_context_md =
      m_main.GetContextMetadata(&to->getASTContext());
  ASTContextMetadataSP from_context_md =
      m_main.MaybeGetContextMetadata(m_source_ctx);

  if (from_context_md) {
    DeclOrigin origin = from_context_md->getOrigin(from);

    if (origin.Valid()) {
      if (origin.ctx != &to->getASTContext()) {
        if (!to_context_md->hasOrigin(to) || user_id != LLDB_INVALID_UID)
          to_context_md->setOrigin(to, origin);

        LLDB_LOG(log,
                 "    [ClangASTImporter] Propagated origin "
                 "(Decl*){0:x}/(ASTContext*){1:x} from (ASTContext*){2:x} to "
                 "(ASTContext*){3:x}",
                 origin.decl, origin.ctx, &from->getASTContext(),
                 &to->getASTContext());
      }
    } else {
      if (m_new_decl_listener)
        m_new_decl_listener->NewDeclImported(from, to);

      if (!to_context_md->hasOrigin(to) || user_id != LLDB_INVALID_UID)
        to_context_md->setOrigin(to, DeclOrigin(m_source_ctx, from));

      LLDB_LOG(log,
               "    [ClangASTImporter] Decl has no origin information in "
               "(ASTContext*){0:x}",
               &from->getASTContext());
    }

    if (auto *to_namespace = dyn_cast<clang::NamespaceDecl>(to)) {
      auto *from_namespace = cast<clang::NamespaceDecl>(from);

      NamespaceMetaMap &namespace_maps = from_context_md->m_namespace_maps;

      NamespaceMetaMap::iterator namespace_map_iter =
          namespace_maps.find(from_namespace);

      if (namespace_map_iter != namespace_maps.end())
        to_context_md->m_namespace_maps[to_namespace] =
            namespace_map_iter->second;
    }
  } else {
    to_context_md->setOrigin(to, DeclOrigin(m_source_ctx, from));

    LLDB_LOG(log,
             "    [ClangASTImporter] Sourced origin "
             "(Decl*){0:x}/(ASTContext*){1:x} into (ASTContext*){2:x}",
             from, m_source_ctx, &to->getASTContext());
  }

  if (auto *to_tag_decl = dyn_cast<TagDecl>(to)) {
    to_tag_decl->setHasExternalLexicalStorage();
    to_tag_decl->getPrimaryContext()->setMustBuildLookupTable();
    auto from_tag_decl = cast<TagDecl>(from);

    LLDB_LOG(
        log,
        "    [ClangASTImporter] To is a TagDecl - attributes {0}{1} [{2}->{3}]",
        (to_tag_decl->hasExternalLexicalStorage() ? " Lexical" : ""),
        (to_tag_decl->hasExternalVisibleStorage() ? " Visible" : ""),
        (from_tag_decl->isCompleteDefinition() ? "complete" : "incomplete"),
        (to_tag_decl->isCompleteDefinition() ? "complete" : "incomplete"));
  }

  if (auto *to_namespace_decl = dyn_cast<NamespaceDecl>(to)) {
    m_main.BuildNamespaceMap(to_namespace_decl);
    to_namespace_decl->setHasExternalVisibleStorage();
  }

  if (auto *to_container_decl = dyn_cast<ObjCContainerDecl>(to)) {
    to_container_decl->setHasExternalLexicalStorage();
    to_container_decl->setHasExternalVisibleStorage();

    if (log) {
      if (ObjCInterfaceDecl *to_interface_decl =
              llvm::dyn_cast<ObjCInterfaceDecl>(to_container_decl)) {
        LLDB_LOG(
            log,
            "    [ClangASTImporter] To is an ObjCInterfaceDecl - attributes "
            "{0}{1}{2}",
            (to_interface_decl->hasExternalLexicalStorage() ? " Lexical" : ""),
            (to_interface_decl->hasExternalVisibleStorage() ? " Visible" : ""),
            (to_interface_decl->hasDefinition() ? " HasDefinition" : ""));
      } else {
        LLDB_LOG(
            log, "    [ClangASTImporter] To is an {0}Decl - attributes {1}{2}",
            ((Decl *)to_container_decl)->getDeclKindName(),
            (to_container_decl->hasExternalLexicalStorage() ? " Lexical" : ""),
            (to_container_decl->hasExternalVisibleStorage() ? " Visible" : ""));
      }
    }
  }

  if (clang::CXXMethodDecl *to_method = dyn_cast<CXXMethodDecl>(to))
    MaybeCompleteReturnType(m_main, to_method);
}

clang::Decl *
ClangASTImporter::ASTImporterDelegate::GetOriginalDecl(clang::Decl *To) {
  return m_main.GetDeclOrigin(To).decl;
}
