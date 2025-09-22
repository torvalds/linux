//===- ExternalASTSource.h - Abstract External AST Interface ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the ExternalASTSource interface, which enables
//  construction of AST nodes from some external source.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_EXTERNALASTSOURCE_H
#define LLVM_CLANG_AST_EXTERNALASTSOURCE_H

#include "clang/AST/CharUnits.h"
#include "clang/AST/DeclBase.h"
#include "clang/Basic/LLVM.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/iterator.h"
#include "llvm/Support/PointerLikeTypeTraits.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <new>
#include <optional>
#include <utility>

namespace clang {

class ASTConsumer;
class ASTContext;
class ASTSourceDescriptor;
class CXXBaseSpecifier;
class CXXCtorInitializer;
class CXXRecordDecl;
class DeclarationName;
class FieldDecl;
class IdentifierInfo;
class NamedDecl;
class ObjCInterfaceDecl;
class RecordDecl;
class Selector;
class Stmt;
class TagDecl;

/// Abstract interface for external sources of AST nodes.
///
/// External AST sources provide AST nodes constructed from some
/// external source, such as a precompiled header. External AST
/// sources can resolve types and declarations from abstract IDs into
/// actual type and declaration nodes, and read parts of declaration
/// contexts.
class ExternalASTSource : public RefCountedBase<ExternalASTSource> {
  friend class ExternalSemaSource;

  /// Generation number for this external AST source. Must be increased
  /// whenever we might have added new redeclarations for existing decls.
  uint32_t CurrentGeneration = 0;

  /// LLVM-style RTTI.
  static char ID;

public:
  ExternalASTSource() = default;
  virtual ~ExternalASTSource();

  /// RAII class for safely pairing a StartedDeserializing call
  /// with FinishedDeserializing.
  class Deserializing {
    ExternalASTSource *Source;

  public:
    explicit Deserializing(ExternalASTSource *source) : Source(source) {
      assert(Source);
      Source->StartedDeserializing();
    }

    ~Deserializing() {
      Source->FinishedDeserializing();
    }
  };

  /// Get the current generation of this AST source. This number
  /// is incremented each time the AST source lazily extends an existing
  /// entity.
  uint32_t getGeneration() const { return CurrentGeneration; }

  /// Resolve a declaration ID into a declaration, potentially
  /// building a new declaration.
  ///
  /// This method only needs to be implemented if the AST source ever
  /// passes back decl sets as VisibleDeclaration objects.
  ///
  /// The default implementation of this method is a no-op.
  virtual Decl *GetExternalDecl(GlobalDeclID ID);

  /// Resolve a selector ID into a selector.
  ///
  /// This operation only needs to be implemented if the AST source
  /// returns non-zero for GetNumKnownSelectors().
  ///
  /// The default implementation of this method is a no-op.
  virtual Selector GetExternalSelector(uint32_t ID);

  /// Returns the number of selectors known to the external AST
  /// source.
  ///
  /// The default implementation of this method is a no-op.
  virtual uint32_t GetNumExternalSelectors();

  /// Resolve the offset of a statement in the decl stream into
  /// a statement.
  ///
  /// This operation is meant to be used via a LazyOffsetPtr.  It only
  /// needs to be implemented if the AST source uses methods like
  /// FunctionDecl::setLazyBody when building decls.
  ///
  /// The default implementation of this method is a no-op.
  virtual Stmt *GetExternalDeclStmt(uint64_t Offset);

  /// Resolve the offset of a set of C++ constructor initializers in
  /// the decl stream into an array of initializers.
  ///
  /// The default implementation of this method is a no-op.
  virtual CXXCtorInitializer **GetExternalCXXCtorInitializers(uint64_t Offset);

  /// Resolve the offset of a set of C++ base specifiers in the decl
  /// stream into an array of specifiers.
  ///
  /// The default implementation of this method is a no-op.
  virtual CXXBaseSpecifier *GetExternalCXXBaseSpecifiers(uint64_t Offset);

  /// Update an out-of-date identifier.
  virtual void updateOutOfDateIdentifier(const IdentifierInfo &II) {}

  /// Find all declarations with the given name in the given context,
  /// and add them to the context by calling SetExternalVisibleDeclsForName
  /// or SetNoExternalVisibleDeclsForName.
  /// \return \c true if any declarations might have been found, \c false if
  /// we definitely have no declarations with tbis name.
  ///
  /// The default implementation of this method is a no-op returning \c false.
  virtual bool
  FindExternalVisibleDeclsByName(const DeclContext *DC, DeclarationName Name);

  /// Ensures that the table of all visible declarations inside this
  /// context is up to date.
  ///
  /// The default implementation of this function is a no-op.
  virtual void completeVisibleDeclsMap(const DeclContext *DC);

  /// Retrieve the module that corresponds to the given module ID.
  virtual Module *getModule(unsigned ID) { return nullptr; }

  /// Return a descriptor for the corresponding module, if one exists.
  virtual std::optional<ASTSourceDescriptor> getSourceDescriptor(unsigned ID);

  enum ExtKind { EK_Always, EK_Never, EK_ReplyHazy };

  virtual ExtKind hasExternalDefinitions(const Decl *D);

  /// Finds all declarations lexically contained within the given
  /// DeclContext, after applying an optional filter predicate.
  ///
  /// \param IsKindWeWant a predicate function that returns true if the passed
  /// declaration kind is one we are looking for.
  ///
  /// The default implementation of this method is a no-op.
  virtual void
  FindExternalLexicalDecls(const DeclContext *DC,
                           llvm::function_ref<bool(Decl::Kind)> IsKindWeWant,
                           SmallVectorImpl<Decl *> &Result);

  /// Finds all declarations lexically contained within the given
  /// DeclContext.
  void FindExternalLexicalDecls(const DeclContext *DC,
                                SmallVectorImpl<Decl *> &Result) {
    FindExternalLexicalDecls(DC, [](Decl::Kind) { return true; }, Result);
  }

  /// Get the decls that are contained in a file in the Offset/Length
  /// range. \p Length can be 0 to indicate a point at \p Offset instead of
  /// a range.
  virtual void FindFileRegionDecls(FileID File, unsigned Offset,
                                   unsigned Length,
                                   SmallVectorImpl<Decl *> &Decls);

  /// Gives the external AST source an opportunity to complete
  /// the redeclaration chain for a declaration. Called each time we
  /// need the most recent declaration of a declaration after the
  /// generation count is incremented.
  virtual void CompleteRedeclChain(const Decl *D);

  /// Gives the external AST source an opportunity to complete
  /// an incomplete type.
  virtual void CompleteType(TagDecl *Tag);

  /// Gives the external AST source an opportunity to complete an
  /// incomplete Objective-C class.
  ///
  /// This routine will only be invoked if the "externally completed" bit is
  /// set on the ObjCInterfaceDecl via the function
  /// \c ObjCInterfaceDecl::setExternallyCompleted().
  virtual void CompleteType(ObjCInterfaceDecl *Class);

  /// Loads comment ranges.
  virtual void ReadComments();

  /// Notify ExternalASTSource that we started deserialization of
  /// a decl or type so until FinishedDeserializing is called there may be
  /// decls that are initializing. Must be paired with FinishedDeserializing.
  ///
  /// The default implementation of this method is a no-op.
  virtual void StartedDeserializing();

  /// Notify ExternalASTSource that we finished the deserialization of
  /// a decl or type. Must be paired with StartedDeserializing.
  ///
  /// The default implementation of this method is a no-op.
  virtual void FinishedDeserializing();

  /// Function that will be invoked when we begin parsing a new
  /// translation unit involving this external AST source.
  ///
  /// The default implementation of this method is a no-op.
  virtual void StartTranslationUnit(ASTConsumer *Consumer);

  /// Print any statistics that have been gathered regarding
  /// the external AST source.
  ///
  /// The default implementation of this method is a no-op.
  virtual void PrintStats();

  /// Perform layout on the given record.
  ///
  /// This routine allows the external AST source to provide an specific
  /// layout for a record, overriding the layout that would normally be
  /// constructed. It is intended for clients who receive specific layout
  /// details rather than source code (such as LLDB). The client is expected
  /// to fill in the field offsets, base offsets, virtual base offsets, and
  /// complete object size.
  ///
  /// \param Record The record whose layout is being requested.
  ///
  /// \param Size The final size of the record, in bits.
  ///
  /// \param Alignment The final alignment of the record, in bits.
  ///
  /// \param FieldOffsets The offset of each of the fields within the record,
  /// expressed in bits. All of the fields must be provided with offsets.
  ///
  /// \param BaseOffsets The offset of each of the direct, non-virtual base
  /// classes. If any bases are not given offsets, the bases will be laid
  /// out according to the ABI.
  ///
  /// \param VirtualBaseOffsets The offset of each of the virtual base classes
  /// (either direct or not). If any bases are not given offsets, the bases will be laid
  /// out according to the ABI.
  ///
  /// \returns true if the record layout was provided, false otherwise.
  virtual bool layoutRecordType(
      const RecordDecl *Record, uint64_t &Size, uint64_t &Alignment,
      llvm::DenseMap<const FieldDecl *, uint64_t> &FieldOffsets,
      llvm::DenseMap<const CXXRecordDecl *, CharUnits> &BaseOffsets,
      llvm::DenseMap<const CXXRecordDecl *, CharUnits> &VirtualBaseOffsets);

  //===--------------------------------------------------------------------===//
  // Queries for performance analysis.
  //===--------------------------------------------------------------------===//

  struct MemoryBufferSizes {
    size_t malloc_bytes;
    size_t mmap_bytes;

    MemoryBufferSizes(size_t malloc_bytes, size_t mmap_bytes)
        : malloc_bytes(malloc_bytes), mmap_bytes(mmap_bytes) {}
  };

  /// Return the amount of memory used by memory buffers, breaking down
  /// by heap-backed versus mmap'ed memory.
  MemoryBufferSizes getMemoryBufferSizes() const {
    MemoryBufferSizes sizes(0, 0);
    getMemoryBufferSizes(sizes);
    return sizes;
  }

  virtual void getMemoryBufferSizes(MemoryBufferSizes &sizes) const;

  /// LLVM-style RTTI.
  /// \{
  virtual bool isA(const void *ClassID) const { return ClassID == &ID; }
  static bool classof(const ExternalASTSource *S) { return S->isA(&ID); }
  /// \}

protected:
  static DeclContextLookupResult
  SetExternalVisibleDeclsForName(const DeclContext *DC,
                                 DeclarationName Name,
                                 ArrayRef<NamedDecl*> Decls);

  static DeclContextLookupResult
  SetNoExternalVisibleDeclsForName(const DeclContext *DC,
                                   DeclarationName Name);

  /// Increment the current generation.
  uint32_t incrementGeneration(ASTContext &C);
};

/// A lazy pointer to an AST node (of base type T) that resides
/// within an external AST source.
///
/// The AST node is identified within the external AST source by a
/// 63-bit offset, and can be retrieved via an operation on the
/// external AST source itself.
template<typename T, typename OffsT, T* (ExternalASTSource::*Get)(OffsT Offset)>
struct LazyOffsetPtr {
  /// Either a pointer to an AST node or the offset within the
  /// external AST source where the AST node can be found.
  ///
  /// If the low bit is clear, a pointer to the AST node. If the low
  /// bit is set, the upper 63 bits are the offset.
  static constexpr size_t DataSize = std::max(sizeof(uint64_t), sizeof(T *));
  alignas(uint64_t) alignas(T *) mutable unsigned char Data[DataSize] = {};

  unsigned char GetLSB() const {
    return Data[llvm::sys::IsBigEndianHost ? DataSize - 1 : 0];
  }

  template <typename U> U &As(bool New) const {
    unsigned char *Obj =
        Data + (llvm::sys::IsBigEndianHost ? DataSize - sizeof(U) : 0);
    if (New)
      return *new (Obj) U;
    return *std::launder(reinterpret_cast<U *>(Obj));
  }

  T *&GetPtr() const { return As<T *>(false); }
  uint64_t &GetU64() const { return As<uint64_t>(false); }
  void SetPtr(T *Ptr) const { As<T *>(true) = Ptr; }
  void SetU64(uint64_t U64) const { As<uint64_t>(true) = U64; }

public:
  LazyOffsetPtr() = default;
  explicit LazyOffsetPtr(T *Ptr) : Data() { SetPtr(Ptr); }

  explicit LazyOffsetPtr(uint64_t Offset) : Data() {
    assert((Offset << 1 >> 1) == Offset && "Offsets must require < 63 bits");
    if (Offset == 0)
      SetPtr(nullptr);
    else
      SetU64((Offset << 1) | 0x01);
  }

  LazyOffsetPtr &operator=(T *Ptr) {
    SetPtr(Ptr);
    return *this;
  }

  LazyOffsetPtr &operator=(uint64_t Offset) {
    assert((Offset << 1 >> 1) == Offset && "Offsets must require < 63 bits");
    if (Offset == 0)
      SetPtr(nullptr);
    else
      SetU64((Offset << 1) | 0x01);

    return *this;
  }

  /// Whether this pointer is non-NULL.
  ///
  /// This operation does not require the AST node to be deserialized.
  explicit operator bool() const { return isOffset() || GetPtr() != nullptr; }

  /// Whether this pointer is non-NULL.
  ///
  /// This operation does not require the AST node to be deserialized.
  bool isValid() const { return isOffset() || GetPtr() != nullptr; }

  /// Whether this pointer is currently stored as an offset.
  bool isOffset() const { return GetLSB() & 0x01; }

  /// Retrieve the pointer to the AST node that this lazy pointer points to.
  ///
  /// \param Source the external AST source.
  ///
  /// \returns a pointer to the AST node.
  T *get(ExternalASTSource *Source) const {
    if (isOffset()) {
      assert(Source &&
             "Cannot deserialize a lazy pointer without an AST source");
      SetPtr((Source->*Get)(OffsT(GetU64() >> 1)));
    }
    return GetPtr();
  }

  /// Retrieve the address of the AST node pointer. Deserializes the pointee if
  /// necessary.
  T **getAddressOfPointer(ExternalASTSource *Source) const {
    // Ensure the integer is in pointer form.
    (void)get(Source);
    return &GetPtr();
  }
};

/// A lazy value (of type T) that is within an AST node of type Owner,
/// where the value might change in later generations of the external AST
/// source.
template<typename Owner, typename T, void (ExternalASTSource::*Update)(Owner)>
struct LazyGenerationalUpdatePtr {
  /// A cache of the value of this pointer, in the most recent generation in
  /// which we queried it.
  struct LazyData {
    ExternalASTSource *ExternalSource;
    uint32_t LastGeneration = 0;
    T LastValue;

    LazyData(ExternalASTSource *Source, T Value)
        : ExternalSource(Source), LastValue(Value) {}
  };

  // Our value is represented as simply T if there is no external AST source.
  using ValueType = llvm::PointerUnion<T, LazyData*>;
  ValueType Value;

  LazyGenerationalUpdatePtr(ValueType V) : Value(V) {}

  // Defined in ASTContext.h
  static ValueType makeValue(const ASTContext &Ctx, T Value);

public:
  explicit LazyGenerationalUpdatePtr(const ASTContext &Ctx, T Value = T())
      : Value(makeValue(Ctx, Value)) {}

  /// Create a pointer that is not potentially updated by later generations of
  /// the external AST source.
  enum NotUpdatedTag { NotUpdated };
  LazyGenerationalUpdatePtr(NotUpdatedTag, T Value = T())
      : Value(Value) {}

  /// Forcibly set this pointer (which must be lazy) as needing updates.
  void markIncomplete() {
    Value.template get<LazyData *>()->LastGeneration = 0;
  }

  /// Set the value of this pointer, in the current generation.
  void set(T NewValue) {
    if (auto *LazyVal = Value.template dyn_cast<LazyData *>()) {
      LazyVal->LastValue = NewValue;
      return;
    }
    Value = NewValue;
  }

  /// Set the value of this pointer, for this and all future generations.
  void setNotUpdated(T NewValue) { Value = NewValue; }

  /// Get the value of this pointer, updating its owner if necessary.
  T get(Owner O) {
    if (auto *LazyVal = Value.template dyn_cast<LazyData *>()) {
      if (LazyVal->LastGeneration != LazyVal->ExternalSource->getGeneration()) {
        LazyVal->LastGeneration = LazyVal->ExternalSource->getGeneration();
        (LazyVal->ExternalSource->*Update)(O);
      }
      return LazyVal->LastValue;
    }
    return Value.template get<T>();
  }

  /// Get the most recently computed value of this pointer without updating it.
  T getNotUpdated() const {
    if (auto *LazyVal = Value.template dyn_cast<LazyData *>())
      return LazyVal->LastValue;
    return Value.template get<T>();
  }

  void *getOpaqueValue() { return Value.getOpaqueValue(); }
  static LazyGenerationalUpdatePtr getFromOpaqueValue(void *Ptr) {
    return LazyGenerationalUpdatePtr(ValueType::getFromOpaqueValue(Ptr));
  }
};

} // namespace clang

namespace llvm {

/// Specialize PointerLikeTypeTraits to allow LazyGenerationalUpdatePtr to be
/// placed into a PointerUnion.
template<typename Owner, typename T,
         void (clang::ExternalASTSource::*Update)(Owner)>
struct PointerLikeTypeTraits<
    clang::LazyGenerationalUpdatePtr<Owner, T, Update>> {
  using Ptr = clang::LazyGenerationalUpdatePtr<Owner, T, Update>;

  static void *getAsVoidPointer(Ptr P) { return P.getOpaqueValue(); }
  static Ptr getFromVoidPointer(void *P) { return Ptr::getFromOpaqueValue(P); }

  static constexpr int NumLowBitsAvailable =
      PointerLikeTypeTraits<T>::NumLowBitsAvailable - 1;
};

} // namespace llvm

namespace clang {

/// Represents a lazily-loaded vector of data.
///
/// The lazily-loaded vector of data contains data that is partially loaded
/// from an external source and partially added by local translation. The
/// items loaded from the external source are loaded lazily, when needed for
/// iteration over the complete vector.
template<typename T, typename Source,
         void (Source::*Loader)(SmallVectorImpl<T>&),
         unsigned LoadedStorage = 2, unsigned LocalStorage = 4>
class LazyVector {
  SmallVector<T, LoadedStorage> Loaded;
  SmallVector<T, LocalStorage> Local;

public:
  /// Iteration over the elements in the vector.
  ///
  /// In a complete iteration, the iterator walks the range [-M, N),
  /// where negative values are used to indicate elements
  /// loaded from the external source while non-negative values are used to
  /// indicate elements added via \c push_back().
  /// However, to provide iteration in source order (for, e.g., chained
  /// precompiled headers), dereferencing the iterator flips the negative
  /// values (corresponding to loaded entities), so that position -M
  /// corresponds to element 0 in the loaded entities vector, position -M+1
  /// corresponds to element 1 in the loaded entities vector, etc. This
  /// gives us a reasonably efficient, source-order walk.
  ///
  /// We define this as a wrapping iterator around an int. The
  /// iterator_adaptor_base class forwards the iterator methods to basic integer
  /// arithmetic.
  class iterator
      : public llvm::iterator_adaptor_base<
            iterator, int, std::random_access_iterator_tag, T, int, T *, T &> {
    friend class LazyVector;

    LazyVector *Self;

    iterator(LazyVector *Self, int Position)
        : iterator::iterator_adaptor_base(Position), Self(Self) {}

    bool isLoaded() const { return this->I < 0; }

  public:
    iterator() : iterator(nullptr, 0) {}

    typename iterator::reference operator*() const {
      if (isLoaded())
        return Self->Loaded.end()[this->I];
      return Self->Local.begin()[this->I];
    }
  };

  iterator begin(Source *source, bool LocalOnly = false) {
    if (LocalOnly)
      return iterator(this, 0);

    if (source)
      (source->*Loader)(Loaded);
    return iterator(this, -(int)Loaded.size());
  }

  iterator end() {
    return iterator(this, Local.size());
  }

  void push_back(const T& LocalValue) {
    Local.push_back(LocalValue);
  }

  void erase(iterator From, iterator To) {
    if (From.isLoaded() && To.isLoaded()) {
      Loaded.erase(&*From, &*To);
      return;
    }

    if (From.isLoaded()) {
      Loaded.erase(&*From, Loaded.end());
      From = begin(nullptr, true);
    }

    Local.erase(&*From, &*To);
  }
};

/// A lazy pointer to a statement.
using LazyDeclStmtPtr =
    LazyOffsetPtr<Stmt, uint64_t, &ExternalASTSource::GetExternalDeclStmt>;

/// A lazy pointer to a declaration.
using LazyDeclPtr =
    LazyOffsetPtr<Decl, GlobalDeclID, &ExternalASTSource::GetExternalDecl>;

/// A lazy pointer to a set of CXXCtorInitializers.
using LazyCXXCtorInitializersPtr =
    LazyOffsetPtr<CXXCtorInitializer *, uint64_t,
                  &ExternalASTSource::GetExternalCXXCtorInitializers>;

/// A lazy pointer to a set of CXXBaseSpecifiers.
using LazyCXXBaseSpecifiersPtr =
    LazyOffsetPtr<CXXBaseSpecifier, uint64_t,
                  &ExternalASTSource::GetExternalCXXBaseSpecifiers>;

} // namespace clang

#endif // LLVM_CLANG_AST_EXTERNALASTSOURCE_H
