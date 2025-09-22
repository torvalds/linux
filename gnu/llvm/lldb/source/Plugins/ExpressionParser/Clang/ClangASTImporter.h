//===-- ClangASTImporter.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGASTIMPORTER_H
#define LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGASTIMPORTER_H

#include <map>
#include <memory>
#include <set>
#include <vector>

#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTImporter.h"
#include "clang/AST/CharUnits.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/FileSystemOptions.h"

#include "lldb/Host/FileSystem.h"
#include "lldb/Symbol/CompilerDeclContext.h"
#include "lldb/Utility/LLDBAssert.h"
#include "lldb/lldb-types.h"

#include "Plugins/ExpressionParser/Clang/CxxModuleHandler.h"

#include "llvm/ADT/DenseMap.h"

namespace lldb_private {

class ClangASTMetadata;
class TypeSystemClang;

/// Manages and observes all Clang AST node importing in LLDB.
///
/// The ClangASTImporter takes care of two things:
///
/// 1. Keeps track of all ASTImporter instances in LLDB.
///
/// Clang's ASTImporter takes care of importing types from one ASTContext to
/// another. This class expands this concept by allowing copying from several
/// ASTContext instances to several other ASTContext instances. Instead of
/// constructing a new ASTImporter manually to copy over a type/decl, this class
/// can be asked to do this. It will construct a ASTImporter for the caller (and
/// will cache the ASTImporter instance for later use) and then perform the
/// import.
///
/// This mainly prevents that a caller might construct several ASTImporter
/// instances for the same source/target ASTContext combination. As the
/// ASTImporter has an internal state that keeps track of already imported
/// declarations and so on, using only one ASTImporter instance is more
/// efficient and less error-prone than using multiple.
///
/// 2. Keeps track of from where declarations were imported (origin-tracking).
/// The ASTImporter instances in this class usually only performa a minimal
/// import, i.e., only a shallow copy is made that is filled out on demand
/// when more information is requested later on. This requires record-keeping
/// of where any shallow clone originally came from so that the right original
/// declaration can be found and used as the source of any missing information.
class ClangASTImporter {
public:
  struct LayoutInfo {
    LayoutInfo() = default;
    typedef llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits>
        OffsetMap;

    uint64_t bit_size = 0;
    uint64_t alignment = 0;
    llvm::DenseMap<const clang::FieldDecl *, uint64_t> field_offsets;
    OffsetMap base_offsets;
    OffsetMap vbase_offsets;
  };

  ClangASTImporter()
      : m_file_manager(clang::FileSystemOptions(),
                       FileSystem::Instance().GetVirtualFileSystem()) {}

  /// Copies the given type and the respective declarations to the destination
  /// type system.
  ///
  /// This function does a shallow copy and requires that the target AST
  /// has an ExternalASTSource which queries this ClangASTImporter instance
  /// for any additional information that is maybe lacking in the shallow copy.
  /// This also means that the type system of src_type can *not* be deleted
  /// after this function has been called. If you need to delete the source
  /// type system you either need to delete the destination type system first
  /// or use \ref ClangASTImporter::DeportType.
  ///
  /// \see ClangASTImporter::DeportType
  CompilerType CopyType(TypeSystemClang &dst, const CompilerType &src_type);

  /// \see ClangASTImporter::CopyType
  clang::Decl *CopyDecl(clang::ASTContext *dst_ctx, clang::Decl *decl);

  /// Copies the given type and the respective declarations to the destination
  /// type system.
  ///
  /// Unlike CopyType this function ensures that types/declarations which are
  /// originally from the AST of src_type are fully copied over. The type
  /// system of src_type can safely be deleted after calling this function.
  /// \see ClangASTImporter::CopyType
  CompilerType DeportType(TypeSystemClang &dst, const CompilerType &src_type);

  /// Copies the given decl to the destination type system.
  /// \see ClangASTImporter::DeportType
  clang::Decl *DeportDecl(clang::ASTContext *dst_ctx, clang::Decl *decl);

  /// Sets the layout for the given RecordDecl. The layout will later be
  /// used by Clang's during code generation. Not calling this function for
  /// a RecordDecl will cause that Clang's codegen tries to layout the
  /// record by itself.
  ///
  /// \param decl The RecordDecl to set the layout for.
  /// \param layout The layout for the record.
  void SetRecordLayout(clang::RecordDecl *decl, const LayoutInfo &layout);

  bool LayoutRecordType(
      const clang::RecordDecl *record_decl, uint64_t &bit_size,
      uint64_t &alignment,
      llvm::DenseMap<const clang::FieldDecl *, uint64_t> &field_offsets,
      llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits>
          &base_offsets,
      llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits>
          &vbase_offsets);

  /// If \ref record has a valid origin, this function copies that
  /// origin's layout into this ClangASTImporter instance.
  ///
  /// \param[in] record The decl whose layout we're calculating.
  /// \param[out] size Size of \ref record in bytes.
  /// \param[out] alignment Alignment of \ref record in bytes.
  /// \param[out] field_offsets Offsets of fields of \ref record.
  /// \param[out] base_offsets Offsets of base classes of \ref record.
  /// \param[out] vbase_offsets Offsets of virtual base classes of \ref record.
  ///
  /// \returns Returns 'false' if no valid origin was found for \ref record or
  /// this function failed to import the layout from the origin. Otherwise,
  /// returns 'true' and the offsets/size/alignment are valid for use.
  bool importRecordLayoutFromOrigin(
      const clang::RecordDecl *record, uint64_t &size, uint64_t &alignment,
      llvm::DenseMap<const clang::FieldDecl *, uint64_t> &field_offsets,
      llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits>
          &base_offsets,
      llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits>
          &vbase_offsets);

  /// Returns true iff the given type was copied from another TypeSystemClang
  /// and the original type in this other TypeSystemClang might contain
  /// additional information (e.g., the definition of a 'class' type) that could
  /// be imported.
  ///
  /// \see ClangASTImporter::Import
  bool CanImport(const CompilerType &type);

  /// If the given type was copied from another TypeSystemClang then copy over
  /// all missing information (e.g., the definition of a 'class' type).
  ///
  /// \return True iff an original type in another TypeSystemClang was found.
  ///         Note: Does *not* return false if an original type was found but
  ///               no information was imported over.
  ///
  /// \see ClangASTImporter::Import
  bool Import(const CompilerType &type);

  bool CompleteType(const CompilerType &compiler_type);

  bool CompleteTagDecl(clang::TagDecl *decl);

  bool CompleteTagDeclWithOrigin(clang::TagDecl *decl, clang::TagDecl *origin);

  bool CompleteObjCInterfaceDecl(clang::ObjCInterfaceDecl *interface_decl);

  bool CompleteAndFetchChildren(clang::QualType type);

  bool RequireCompleteType(clang::QualType type);

  /// Updates the internal origin-tracking information so that the given
  /// 'original' decl is from now on used to import additional information
  /// into the given decl.
  ///
  /// Usually the origin-tracking in the ClangASTImporter is automatically
  /// updated when a declaration is imported, so the only valid reason to ever
  /// call this is if there is a 'better' original decl and the target decl
  /// is only a shallow clone that lacks any contents.
  void SetDeclOrigin(const clang::Decl *decl, clang::Decl *original_decl);

  ClangASTMetadata *GetDeclMetadata(const clang::Decl *decl);

  //
  // Namespace maps
  //

  typedef std::pair<lldb::ModuleSP, CompilerDeclContext> NamespaceMapItem;
  typedef std::vector<NamespaceMapItem> NamespaceMap;
  typedef std::shared_ptr<NamespaceMap> NamespaceMapSP;

  void RegisterNamespaceMap(const clang::NamespaceDecl *decl,
                            NamespaceMapSP &namespace_map);

  NamespaceMapSP GetNamespaceMap(const clang::NamespaceDecl *decl);

  void BuildNamespaceMap(const clang::NamespaceDecl *decl);

  //
  // Completers for maps
  //

  class MapCompleter {
  public:
    virtual ~MapCompleter();

    virtual void CompleteNamespaceMap(NamespaceMapSP &namespace_map,
                                      ConstString name,
                                      NamespaceMapSP &parent_map) const = 0;
  };

  void InstallMapCompleter(clang::ASTContext *dst_ctx,
                           MapCompleter &completer) {
    ASTContextMetadataSP context_md;
    ContextMetadataMap::iterator context_md_iter = m_metadata_map.find(dst_ctx);

    if (context_md_iter == m_metadata_map.end()) {
      context_md = ASTContextMetadataSP(new ASTContextMetadata(dst_ctx));
      m_metadata_map[dst_ctx] = context_md;
    } else {
      context_md = context_md_iter->second;
    }

    context_md->m_map_completer = &completer;
  }

  void ForgetDestination(clang::ASTContext *dst_ctx);
  void ForgetSource(clang::ASTContext *dst_ctx, clang::ASTContext *src_ctx);

  struct DeclOrigin {
    DeclOrigin() = default;

    DeclOrigin(clang::ASTContext *_ctx, clang::Decl *_decl)
        : ctx(_ctx), decl(_decl) {
      // The decl has to be in its associated ASTContext.
      assert(_decl == nullptr || &_decl->getASTContext() == _ctx);
    }

    DeclOrigin(const DeclOrigin &rhs) {
      ctx = rhs.ctx;
      decl = rhs.decl;
    }

    void operator=(const DeclOrigin &rhs) {
      ctx = rhs.ctx;
      decl = rhs.decl;
    }

    bool Valid() const { return (ctx != nullptr || decl != nullptr); }

    clang::ASTContext *ctx = nullptr;
    clang::Decl *decl = nullptr;
  };

  /// Listener interface used by the ASTImporterDelegate to inform other code
  /// about decls that have been imported the first time.
  struct NewDeclListener {
    virtual ~NewDeclListener() = default;
    /// A decl has been imported for the first time.
    virtual void NewDeclImported(clang::Decl *from, clang::Decl *to) = 0;
  };

  /// ASTImporter that intercepts and records the import process of the
  /// underlying ASTImporter.
  ///
  /// This class updates the map from declarations to their original
  /// declarations and can record declarations that have been imported in a
  /// certain interval.
  ///
  /// When intercepting a declaration import, the ASTImporterDelegate uses the
  /// CxxModuleHandler to replace any missing or malformed declarations with
  /// their counterpart from a C++ module.
  struct ASTImporterDelegate : public clang::ASTImporter {
    ASTImporterDelegate(ClangASTImporter &main, clang::ASTContext *target_ctx,
                        clang::ASTContext *source_ctx)
        : clang::ASTImporter(*target_ctx, main.m_file_manager, *source_ctx,
                             main.m_file_manager, true /*minimal*/),
          m_main(main), m_source_ctx(source_ctx) {
      // Target and source ASTContext shouldn't be identical. Importing AST
      // nodes within the same AST doesn't make any sense as the whole idea
      // is to import them to a different AST.
      lldbassert(target_ctx != source_ctx && "Can't import into itself");
      // This is always doing a minimal import of any declarations. This means
      // that there has to be an ExternalASTSource in the target ASTContext
      // (that should implement the callbacks that complete any declarations
      // on demand). Without an ExternalASTSource, this ASTImporter will just
      // do a minimal import and the imported declarations won't be completed.
      assert(target_ctx->getExternalSource() && "Missing ExternalSource");
      setODRHandling(clang::ASTImporter::ODRHandlingType::Liberal);
    }

    /// Scope guard that attaches a CxxModuleHandler to an ASTImporterDelegate
    /// and deattaches it at the end of the scope. Supports being used multiple
    /// times on the same ASTImporterDelegate instance in nested scopes.
    class CxxModuleScope {
      /// The handler we attach to the ASTImporterDelegate.
      CxxModuleHandler m_handler;
      /// The ASTImporterDelegate we are supposed to attach the handler to.
      ASTImporterDelegate &m_delegate;
      /// True iff we attached the handler to the ASTImporterDelegate.
      bool m_valid = false;

    public:
      CxxModuleScope(ASTImporterDelegate &delegate, clang::ASTContext *dst_ctx)
          : m_delegate(delegate) {
        // If the delegate doesn't have a CxxModuleHandler yet, create one
        // and attach it.
        if (!delegate.m_std_handler) {
          m_handler = CxxModuleHandler(delegate, dst_ctx);
          m_valid = true;
          delegate.m_std_handler = &m_handler;
        }
      }
      ~CxxModuleScope() {
        if (m_valid) {
          // Make sure no one messed with the handler we placed.
          assert(m_delegate.m_std_handler == &m_handler);
          m_delegate.m_std_handler = nullptr;
        }
      }
    };

    void ImportDefinitionTo(clang::Decl *to, clang::Decl *from);

    void Imported(clang::Decl *from, clang::Decl *to) override;

    clang::Decl *GetOriginalDecl(clang::Decl *To) override;

    void SetImportListener(NewDeclListener *listener) {
      assert(m_new_decl_listener == nullptr && "Already attached a listener?");
      m_new_decl_listener = listener;
    }
    void RemoveImportListener() { m_new_decl_listener = nullptr; }

  protected:
    llvm::Expected<clang::Decl *> ImportImpl(clang::Decl *From) override;

  private:
    /// Decls we should ignore when mapping decls back to their original
    /// ASTContext. Used by the CxxModuleHandler to mark declarations that
    /// were created from the 'std' C++ module to prevent that the Importer
    /// tries to sync them with the broken equivalent in the debug info AST.
    llvm::SmallPtrSet<clang::Decl *, 16> m_decls_to_ignore;
    ClangASTImporter &m_main;
    clang::ASTContext *m_source_ctx;
    CxxModuleHandler *m_std_handler = nullptr;
    /// The currently attached listener.
    NewDeclListener *m_new_decl_listener = nullptr;
  };

  typedef std::shared_ptr<ASTImporterDelegate> ImporterDelegateSP;
  typedef llvm::DenseMap<clang::ASTContext *, ImporterDelegateSP> DelegateMap;
  typedef llvm::DenseMap<const clang::NamespaceDecl *, NamespaceMapSP>
      NamespaceMetaMap;

  class ASTContextMetadata {
    typedef llvm::DenseMap<const clang::Decl *, DeclOrigin> OriginMap;

  public:
    ASTContextMetadata(clang::ASTContext *dst_ctx) : m_dst_ctx(dst_ctx) {}

    clang::ASTContext *m_dst_ctx;
    DelegateMap m_delegates;

    NamespaceMetaMap m_namespace_maps;
    MapCompleter *m_map_completer = nullptr;

    /// Sets the DeclOrigin for the given Decl and overwrites any existing
    /// DeclOrigin.
    void setOrigin(const clang::Decl *decl, DeclOrigin origin) {
      // Setting the origin of any decl to itself (or to a different decl
      // in the same ASTContext) doesn't make any sense. It will also cause
      // ASTImporterDelegate::ImportImpl to infinite recurse when trying to find
      // the 'original' Decl when importing code.
      assert(&decl->getASTContext() != origin.ctx &&
             "Trying to set decl origin to its own ASTContext?");
      assert(decl != origin.decl && "Trying to set decl origin to itself?");
      m_origins[decl] = origin;
    }

    /// Removes any tracked DeclOrigin for the given decl.
    void removeOrigin(const clang::Decl *decl) { m_origins.erase(decl); }

    /// Remove all DeclOrigin entries that point to the given ASTContext.
    /// Useful when an ASTContext is about to be deleted and all the dangling
    /// pointers to it need to be removed.
    void removeOriginsWithContext(clang::ASTContext *ctx) {
      for (OriginMap::iterator iter = m_origins.begin();
           iter != m_origins.end();) {
        if (iter->second.ctx == ctx)
          m_origins.erase(iter++);
        else
          ++iter;
      }
    }

    /// Returns the DeclOrigin for the given Decl or an invalid DeclOrigin
    /// instance if there no known DeclOrigin for the given Decl.
    DeclOrigin getOrigin(const clang::Decl *decl) const {
      auto iter = m_origins.find(decl);
      if (iter == m_origins.end())
        return DeclOrigin();
      return iter->second;
    }

    /// Returns true there is a known DeclOrigin for the given Decl.
    bool hasOrigin(const clang::Decl *decl) const {
      return getOrigin(decl).Valid();
    }

  private:
    /// Maps declarations to the ASTContext/Decl from which they were imported
    /// from. If a declaration is from an ASTContext which has been deleted
    /// since the declaration was imported or the declaration wasn't created by
    /// the ASTImporter, then it doesn't have a DeclOrigin and will not be
    /// tracked here.
    OriginMap m_origins;
  };

  typedef std::shared_ptr<ASTContextMetadata> ASTContextMetadataSP;
  typedef llvm::DenseMap<const clang::ASTContext *, ASTContextMetadataSP>
      ContextMetadataMap;

  ContextMetadataMap m_metadata_map;

  ASTContextMetadataSP GetContextMetadata(clang::ASTContext *dst_ctx) {
    ContextMetadataMap::iterator context_md_iter = m_metadata_map.find(dst_ctx);

    if (context_md_iter == m_metadata_map.end()) {
      ASTContextMetadataSP context_md =
          ASTContextMetadataSP(new ASTContextMetadata(dst_ctx));
      m_metadata_map[dst_ctx] = context_md;
      return context_md;
    }
    return context_md_iter->second;
  }

  ASTContextMetadataSP MaybeGetContextMetadata(clang::ASTContext *dst_ctx) {
    ContextMetadataMap::iterator context_md_iter = m_metadata_map.find(dst_ctx);

    if (context_md_iter != m_metadata_map.end())
      return context_md_iter->second;
    return ASTContextMetadataSP();
  }

  ImporterDelegateSP GetDelegate(clang::ASTContext *dst_ctx,
                                 clang::ASTContext *src_ctx) {
    ASTContextMetadataSP context_md = GetContextMetadata(dst_ctx);

    DelegateMap &delegates = context_md->m_delegates;
    DelegateMap::iterator delegate_iter = delegates.find(src_ctx);

    if (delegate_iter == delegates.end()) {
      ImporterDelegateSP delegate =
          ImporterDelegateSP(new ASTImporterDelegate(*this, dst_ctx, src_ctx));
      delegates[src_ctx] = delegate;
      return delegate;
    }
    return delegate_iter->second;
  }

  DeclOrigin GetDeclOrigin(const clang::Decl *decl);

  clang::FileManager m_file_manager;
  typedef llvm::DenseMap<const clang::RecordDecl *, LayoutInfo>
      RecordDeclToLayoutMap;

  RecordDeclToLayoutMap m_record_decl_to_layout_map;
};

template <class D> class TaggedASTDecl {
public:
  TaggedASTDecl() : decl(nullptr) {}
  TaggedASTDecl(D *_decl) : decl(_decl) {}
  bool IsValid() const { return (decl != nullptr); }
  bool IsInvalid() const { return !IsValid(); }
  D *operator->() const { return decl; }
  D *decl;
};

template <class D2, template <class D> class TD, class D1>
TD<D2> DynCast(TD<D1> source) {
  return TD<D2>(llvm::dyn_cast<D2>(source.decl));
}

template <class D = clang::Decl> class DeclFromParser;
template <class D = clang::Decl> class DeclFromUser;

template <class D> class DeclFromParser : public TaggedASTDecl<D> {
public:
  DeclFromParser() : TaggedASTDecl<D>() {}
  DeclFromParser(D *_decl) : TaggedASTDecl<D>(_decl) {}

  DeclFromUser<D> GetOrigin(ClangASTImporter &importer);
};

template <class D> class DeclFromUser : public TaggedASTDecl<D> {
public:
  DeclFromUser() : TaggedASTDecl<D>() {}
  DeclFromUser(D *_decl) : TaggedASTDecl<D>(_decl) {}

  DeclFromParser<D> Import(clang::ASTContext *dest_ctx,
                           ClangASTImporter &importer);
};

template <class D>
DeclFromUser<D> DeclFromParser<D>::GetOrigin(ClangASTImporter &importer) {
  ClangASTImporter::DeclOrigin origin = importer.GetDeclOrigin(this->decl);
  if (!origin.Valid())
    return DeclFromUser<D>();
  return DeclFromUser<D>(llvm::dyn_cast<D>(origin.decl));
}

template <class D>
DeclFromParser<D> DeclFromUser<D>::Import(clang::ASTContext *dest_ctx,
                                          ClangASTImporter &importer) {
  DeclFromParser<> parser_generic_decl(importer.CopyDecl(dest_ctx, this->decl));
  if (parser_generic_decl.IsInvalid())
    return DeclFromParser<D>();
  return DeclFromParser<D>(llvm::dyn_cast<D>(parser_generic_decl.decl));
}

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGASTIMPORTER_H
