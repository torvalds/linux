//===--- TypeLocBuilder.h - Type Source Info collector ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines TypeLocBuilder, a class for building TypeLocs
//  bottom-up.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_SEMA_TYPELOCBUILDER_H
#define LLVM_CLANG_LIB_SEMA_TYPELOCBUILDER_H

#include "clang/AST/ASTContext.h"
#include "clang/AST/TypeLoc.h"

namespace clang {

class TypeLocBuilder {
  enum { InlineCapacity = 8 * sizeof(SourceLocation) };

  /// The underlying location-data buffer.  Data grows from the end
  /// of the buffer backwards.
  char *Buffer;

  /// The capacity of the current buffer.
  size_t Capacity;

  /// The index of the first occupied byte in the buffer.
  size_t Index;

#ifndef NDEBUG
  /// The last type pushed on this builder.
  QualType LastTy;
#endif

  /// The inline buffer.
  enum { BufferMaxAlignment = alignof(void *) };
  alignas(BufferMaxAlignment) char InlineBuffer[InlineCapacity];
  unsigned NumBytesAtAlign4;
  bool AtAlign8;

public:
  TypeLocBuilder()
      : Buffer(InlineBuffer), Capacity(InlineCapacity), Index(InlineCapacity),
        NumBytesAtAlign4(0), AtAlign8(false) {}

  ~TypeLocBuilder() {
    if (Buffer != InlineBuffer)
      delete[] Buffer;
  }

  TypeLocBuilder(const TypeLocBuilder &) = delete;
  TypeLocBuilder &operator=(const TypeLocBuilder &) = delete;

  /// Ensures that this buffer has at least as much capacity as described.
  void reserve(size_t Requested) {
    if (Requested > Capacity)
      // For now, match the request exactly.
      grow(Requested);
  }

  /// Pushes a copy of the given TypeLoc onto this builder.  The builder
  /// must be empty for this to work.
  void pushFullCopy(TypeLoc L);

  /// Pushes 'T' with all locations pointing to 'Loc'.
  /// The builder must be empty for this to work.
  void pushTrivial(ASTContext &Context, QualType T, SourceLocation Loc);

  /// Pushes space for a typespec TypeLoc.  Invalidates any TypeLocs
  /// previously retrieved from this builder.
  TypeSpecTypeLoc pushTypeSpec(QualType T) {
    size_t LocalSize = TypeSpecTypeLoc::LocalDataSize;
    unsigned LocalAlign = TypeSpecTypeLoc::LocalDataAlignment;
    return pushImpl(T, LocalSize, LocalAlign).castAs<TypeSpecTypeLoc>();
  }

  /// Resets this builder to the newly-initialized state.
  void clear() {
#ifndef NDEBUG
    LastTy = QualType();
#endif
    Index = Capacity;
    NumBytesAtAlign4 = 0;
    AtAlign8 = false;
  }

  /// Tell the TypeLocBuilder that the type it is storing has been
  /// modified in some safe way that doesn't affect type-location information.
  void TypeWasModifiedSafely(QualType T) {
#ifndef NDEBUG
    LastTy = T;
#endif
  }

  /// Pushes space for a new TypeLoc of the given type.  Invalidates
  /// any TypeLocs previously retrieved from this builder.
  template <class TyLocType> TyLocType push(QualType T) {
    TyLocType Loc = TypeLoc(T, nullptr).castAs<TyLocType>();
    size_t LocalSize = Loc.getLocalDataSize();
    unsigned LocalAlign = Loc.getLocalDataAlignment();
    return pushImpl(T, LocalSize, LocalAlign).castAs<TyLocType>();
  }

  /// Creates a TypeSourceInfo for the given type.
  TypeSourceInfo *getTypeSourceInfo(ASTContext& Context, QualType T) {
#ifndef NDEBUG
    assert(T == LastTy && "type doesn't match last type pushed!");
#endif

    size_t FullDataSize = Capacity - Index;
    TypeSourceInfo *DI = Context.CreateTypeSourceInfo(T, FullDataSize);
    memcpy(DI->getTypeLoc().getOpaqueData(), &Buffer[Index], FullDataSize);
    return DI;
  }

  /// Copies the type-location information to the given AST context and
  /// returns a \c TypeLoc referring into the AST context.
  TypeLoc getTypeLocInContext(ASTContext &Context, QualType T) {
#ifndef NDEBUG
    assert(T == LastTy && "type doesn't match last type pushed!");
#endif

    size_t FullDataSize = Capacity - Index;
    void *Mem = Context.Allocate(FullDataSize);
    memcpy(Mem, &Buffer[Index], FullDataSize);
    return TypeLoc(T, Mem);
  }

private:

  TypeLoc pushImpl(QualType T, size_t LocalSize, unsigned LocalAlignment);

  /// Grow to the given capacity.
  void grow(size_t NewCapacity);

  /// Retrieve a temporary TypeLoc that refers into this \c TypeLocBuilder
  /// object.
  ///
  /// The resulting \c TypeLoc should only be used so long as the
  /// \c TypeLocBuilder is active and has not had more type information
  /// pushed into it.
  TypeLoc getTemporaryTypeLoc(QualType T) {
#ifndef NDEBUG
    assert(LastTy == T && "type doesn't match last type pushed!");
#endif
    return TypeLoc(T, &Buffer[Index]);
  }
};

}

#endif
