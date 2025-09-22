//===--- SemaType.cpp - Semantic Analysis for Types -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements type-related semantic analysis.
//
//===----------------------------------------------------------------------===//

#include "TypeLocBuilder.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTMutationListener.h"
#include "clang/AST/ASTStructuralEquivalence.h"
#include "clang/AST/CXXInheritance.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeLoc.h"
#include "clang/AST/TypeLocVisitor.h"
#include "clang/Basic/PartialDiagnostic.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/Specifiers.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Sema/DeclSpec.h"
#include "clang/Sema/DelayedDiagnostic.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/ParsedAttr.h"
#include "clang/Sema/ParsedTemplate.h"
#include "clang/Sema/ScopeInfo.h"
#include "clang/Sema/SemaCUDA.h"
#include "clang/Sema/SemaInternal.h"
#include "clang/Sema/SemaObjC.h"
#include "clang/Sema/SemaOpenMP.h"
#include "clang/Sema/Template.h"
#include "clang/Sema/TemplateInstCallback.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLForwardCompat.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include <bitset>
#include <optional>

using namespace clang;

enum TypeDiagSelector {
  TDS_Function,
  TDS_Pointer,
  TDS_ObjCObjOrBlock
};

/// isOmittedBlockReturnType - Return true if this declarator is missing a
/// return type because this is a omitted return type on a block literal.
static bool isOmittedBlockReturnType(const Declarator &D) {
  if (D.getContext() != DeclaratorContext::BlockLiteral ||
      D.getDeclSpec().hasTypeSpecifier())
    return false;

  if (D.getNumTypeObjects() == 0)
    return true;   // ^{ ... }

  if (D.getNumTypeObjects() == 1 &&
      D.getTypeObject(0).Kind == DeclaratorChunk::Function)
    return true;   // ^(int X, float Y) { ... }

  return false;
}

/// diagnoseBadTypeAttribute - Diagnoses a type attribute which
/// doesn't apply to the given type.
static void diagnoseBadTypeAttribute(Sema &S, const ParsedAttr &attr,
                                     QualType type) {
  TypeDiagSelector WhichType;
  bool useExpansionLoc = true;
  switch (attr.getKind()) {
  case ParsedAttr::AT_ObjCGC:
    WhichType = TDS_Pointer;
    break;
  case ParsedAttr::AT_ObjCOwnership:
    WhichType = TDS_ObjCObjOrBlock;
    break;
  default:
    // Assume everything else was a function attribute.
    WhichType = TDS_Function;
    useExpansionLoc = false;
    break;
  }

  SourceLocation loc = attr.getLoc();
  StringRef name = attr.getAttrName()->getName();

  // The GC attributes are usually written with macros;  special-case them.
  IdentifierInfo *II = attr.isArgIdent(0) ? attr.getArgAsIdent(0)->Ident
                                          : nullptr;
  if (useExpansionLoc && loc.isMacroID() && II) {
    if (II->isStr("strong")) {
      if (S.findMacroSpelling(loc, "__strong")) name = "__strong";
    } else if (II->isStr("weak")) {
      if (S.findMacroSpelling(loc, "__weak")) name = "__weak";
    }
  }

  S.Diag(loc, attr.isRegularKeywordAttribute()
                  ? diag::err_type_attribute_wrong_type
                  : diag::warn_type_attribute_wrong_type)
      << name << WhichType << type;
}

// objc_gc applies to Objective-C pointers or, otherwise, to the
// smallest available pointer type (i.e. 'void*' in 'void**').
#define OBJC_POINTER_TYPE_ATTRS_CASELIST                                       \
  case ParsedAttr::AT_ObjCGC:                                                  \
  case ParsedAttr::AT_ObjCOwnership

// Calling convention attributes.
#define CALLING_CONV_ATTRS_CASELIST                                            \
  case ParsedAttr::AT_CDecl:                                                   \
  case ParsedAttr::AT_FastCall:                                                \
  case ParsedAttr::AT_StdCall:                                                 \
  case ParsedAttr::AT_ThisCall:                                                \
  case ParsedAttr::AT_RegCall:                                                 \
  case ParsedAttr::AT_Pascal:                                                  \
  case ParsedAttr::AT_SwiftCall:                                               \
  case ParsedAttr::AT_SwiftAsyncCall:                                          \
  case ParsedAttr::AT_VectorCall:                                              \
  case ParsedAttr::AT_AArch64VectorPcs:                                        \
  case ParsedAttr::AT_AArch64SVEPcs:                                           \
  case ParsedAttr::AT_AMDGPUKernelCall:                                        \
  case ParsedAttr::AT_MSABI:                                                   \
  case ParsedAttr::AT_SysVABI:                                                 \
  case ParsedAttr::AT_Pcs:                                                     \
  case ParsedAttr::AT_IntelOclBicc:                                            \
  case ParsedAttr::AT_PreserveMost:                                            \
  case ParsedAttr::AT_PreserveAll:                                             \
  case ParsedAttr::AT_M68kRTD:                                                 \
  case ParsedAttr::AT_PreserveNone:                                            \
  case ParsedAttr::AT_RISCVVectorCC

// Function type attributes.
#define FUNCTION_TYPE_ATTRS_CASELIST                                           \
  case ParsedAttr::AT_NSReturnsRetained:                                       \
  case ParsedAttr::AT_NoReturn:                                                \
  case ParsedAttr::AT_NonBlocking:                                             \
  case ParsedAttr::AT_NonAllocating:                                           \
  case ParsedAttr::AT_Blocking:                                                \
  case ParsedAttr::AT_Allocating:                                              \
  case ParsedAttr::AT_Regparm:                                                 \
  case ParsedAttr::AT_CmseNSCall:                                              \
  case ParsedAttr::AT_ArmStreaming:                                            \
  case ParsedAttr::AT_ArmStreamingCompatible:                                  \
  case ParsedAttr::AT_ArmPreserves:                                            \
  case ParsedAttr::AT_ArmIn:                                                   \
  case ParsedAttr::AT_ArmOut:                                                  \
  case ParsedAttr::AT_ArmInOut:                                                \
  case ParsedAttr::AT_AnyX86NoCallerSavedRegisters:                            \
  case ParsedAttr::AT_AnyX86NoCfCheck:                                         \
    CALLING_CONV_ATTRS_CASELIST

// Microsoft-specific type qualifiers.
#define MS_TYPE_ATTRS_CASELIST                                                 \
  case ParsedAttr::AT_Ptr32:                                                   \
  case ParsedAttr::AT_Ptr64:                                                   \
  case ParsedAttr::AT_SPtr:                                                    \
  case ParsedAttr::AT_UPtr

// Nullability qualifiers.
#define NULLABILITY_TYPE_ATTRS_CASELIST                                        \
  case ParsedAttr::AT_TypeNonNull:                                             \
  case ParsedAttr::AT_TypeNullable:                                            \
  case ParsedAttr::AT_TypeNullableResult:                                      \
  case ParsedAttr::AT_TypeNullUnspecified

namespace {
  /// An object which stores processing state for the entire
  /// GetTypeForDeclarator process.
  class TypeProcessingState {
    Sema &sema;

    /// The declarator being processed.
    Declarator &declarator;

    /// The index of the declarator chunk we're currently processing.
    /// May be the total number of valid chunks, indicating the
    /// DeclSpec.
    unsigned chunkIndex;

    /// The original set of attributes on the DeclSpec.
    SmallVector<ParsedAttr *, 2> savedAttrs;

    /// A list of attributes to diagnose the uselessness of when the
    /// processing is complete.
    SmallVector<ParsedAttr *, 2> ignoredTypeAttrs;

    /// Attributes corresponding to AttributedTypeLocs that we have not yet
    /// populated.
    // FIXME: The two-phase mechanism by which we construct Types and fill
    // their TypeLocs makes it hard to correctly assign these. We keep the
    // attributes in creation order as an attempt to make them line up
    // properly.
    using TypeAttrPair = std::pair<const AttributedType*, const Attr*>;
    SmallVector<TypeAttrPair, 8> AttrsForTypes;
    bool AttrsForTypesSorted = true;

    /// MacroQualifiedTypes mapping to macro expansion locations that will be
    /// stored in a MacroQualifiedTypeLoc.
    llvm::DenseMap<const MacroQualifiedType *, SourceLocation> LocsForMacros;

    /// Flag to indicate we parsed a noderef attribute. This is used for
    /// validating that noderef was used on a pointer or array.
    bool parsedNoDeref;

  public:
    TypeProcessingState(Sema &sema, Declarator &declarator)
        : sema(sema), declarator(declarator),
          chunkIndex(declarator.getNumTypeObjects()), parsedNoDeref(false) {}

    Sema &getSema() const {
      return sema;
    }

    Declarator &getDeclarator() const {
      return declarator;
    }

    bool isProcessingDeclSpec() const {
      return chunkIndex == declarator.getNumTypeObjects();
    }

    unsigned getCurrentChunkIndex() const {
      return chunkIndex;
    }

    void setCurrentChunkIndex(unsigned idx) {
      assert(idx <= declarator.getNumTypeObjects());
      chunkIndex = idx;
    }

    ParsedAttributesView &getCurrentAttributes() const {
      if (isProcessingDeclSpec())
        return getMutableDeclSpec().getAttributes();
      return declarator.getTypeObject(chunkIndex).getAttrs();
    }

    /// Save the current set of attributes on the DeclSpec.
    void saveDeclSpecAttrs() {
      // Don't try to save them multiple times.
      if (!savedAttrs.empty())
        return;

      DeclSpec &spec = getMutableDeclSpec();
      llvm::append_range(savedAttrs,
                         llvm::make_pointer_range(spec.getAttributes()));
    }

    /// Record that we had nowhere to put the given type attribute.
    /// We will diagnose such attributes later.
    void addIgnoredTypeAttr(ParsedAttr &attr) {
      ignoredTypeAttrs.push_back(&attr);
    }

    /// Diagnose all the ignored type attributes, given that the
    /// declarator worked out to the given type.
    void diagnoseIgnoredTypeAttrs(QualType type) const {
      for (auto *Attr : ignoredTypeAttrs)
        diagnoseBadTypeAttribute(getSema(), *Attr, type);
    }

    /// Get an attributed type for the given attribute, and remember the Attr
    /// object so that we can attach it to the AttributedTypeLoc.
    QualType getAttributedType(Attr *A, QualType ModifiedType,
                               QualType EquivType) {
      QualType T =
          sema.Context.getAttributedType(A->getKind(), ModifiedType, EquivType);
      AttrsForTypes.push_back({cast<AttributedType>(T.getTypePtr()), A});
      AttrsForTypesSorted = false;
      return T;
    }

    /// Get a BTFTagAttributed type for the btf_type_tag attribute.
    QualType getBTFTagAttributedType(const BTFTypeTagAttr *BTFAttr,
                                     QualType WrappedType) {
      return sema.Context.getBTFTagAttributedType(BTFAttr, WrappedType);
    }

    /// Completely replace the \c auto in \p TypeWithAuto by
    /// \p Replacement. Also replace \p TypeWithAuto in \c TypeAttrPair if
    /// necessary.
    QualType ReplaceAutoType(QualType TypeWithAuto, QualType Replacement) {
      QualType T = sema.ReplaceAutoType(TypeWithAuto, Replacement);
      if (auto *AttrTy = TypeWithAuto->getAs<AttributedType>()) {
        // Attributed type still should be an attributed type after replacement.
        auto *NewAttrTy = cast<AttributedType>(T.getTypePtr());
        for (TypeAttrPair &A : AttrsForTypes) {
          if (A.first == AttrTy)
            A.first = NewAttrTy;
        }
        AttrsForTypesSorted = false;
      }
      return T;
    }

    /// Extract and remove the Attr* for a given attributed type.
    const Attr *takeAttrForAttributedType(const AttributedType *AT) {
      if (!AttrsForTypesSorted) {
        llvm::stable_sort(AttrsForTypes, llvm::less_first());
        AttrsForTypesSorted = true;
      }

      // FIXME: This is quadratic if we have lots of reuses of the same
      // attributed type.
      for (auto It = std::partition_point(
               AttrsForTypes.begin(), AttrsForTypes.end(),
               [=](const TypeAttrPair &A) { return A.first < AT; });
           It != AttrsForTypes.end() && It->first == AT; ++It) {
        if (It->second) {
          const Attr *Result = It->second;
          It->second = nullptr;
          return Result;
        }
      }

      llvm_unreachable("no Attr* for AttributedType*");
    }

    SourceLocation
    getExpansionLocForMacroQualifiedType(const MacroQualifiedType *MQT) const {
      auto FoundLoc = LocsForMacros.find(MQT);
      assert(FoundLoc != LocsForMacros.end() &&
             "Unable to find macro expansion location for MacroQualifedType");
      return FoundLoc->second;
    }

    void setExpansionLocForMacroQualifiedType(const MacroQualifiedType *MQT,
                                              SourceLocation Loc) {
      LocsForMacros[MQT] = Loc;
    }

    void setParsedNoDeref(bool parsed) { parsedNoDeref = parsed; }

    bool didParseNoDeref() const { return parsedNoDeref; }

    ~TypeProcessingState() {
      if (savedAttrs.empty())
        return;

      getMutableDeclSpec().getAttributes().clearListOnly();
      for (ParsedAttr *AL : savedAttrs)
        getMutableDeclSpec().getAttributes().addAtEnd(AL);
    }

  private:
    DeclSpec &getMutableDeclSpec() const {
      return const_cast<DeclSpec&>(declarator.getDeclSpec());
    }
  };
} // end anonymous namespace

static void moveAttrFromListToList(ParsedAttr &attr,
                                   ParsedAttributesView &fromList,
                                   ParsedAttributesView &toList) {
  fromList.remove(&attr);
  toList.addAtEnd(&attr);
}

/// The location of a type attribute.
enum TypeAttrLocation {
  /// The attribute is in the decl-specifier-seq.
  TAL_DeclSpec,
  /// The attribute is part of a DeclaratorChunk.
  TAL_DeclChunk,
  /// The attribute is immediately after the declaration's name.
  TAL_DeclName
};

static void
processTypeAttrs(TypeProcessingState &state, QualType &type,
                 TypeAttrLocation TAL, const ParsedAttributesView &attrs,
                 CUDAFunctionTarget CFT = CUDAFunctionTarget::HostDevice);

static bool handleFunctionTypeAttr(TypeProcessingState &state, ParsedAttr &attr,
                                   QualType &type, CUDAFunctionTarget CFT);

static bool handleMSPointerTypeQualifierAttr(TypeProcessingState &state,
                                             ParsedAttr &attr, QualType &type);

static bool handleObjCGCTypeAttr(TypeProcessingState &state, ParsedAttr &attr,
                                 QualType &type);

static bool handleObjCOwnershipTypeAttr(TypeProcessingState &state,
                                        ParsedAttr &attr, QualType &type);

static bool handleObjCPointerTypeAttr(TypeProcessingState &state,
                                      ParsedAttr &attr, QualType &type) {
  if (attr.getKind() == ParsedAttr::AT_ObjCGC)
    return handleObjCGCTypeAttr(state, attr, type);
  assert(attr.getKind() == ParsedAttr::AT_ObjCOwnership);
  return handleObjCOwnershipTypeAttr(state, attr, type);
}

/// Given the index of a declarator chunk, check whether that chunk
/// directly specifies the return type of a function and, if so, find
/// an appropriate place for it.
///
/// \param i - a notional index which the search will start
///   immediately inside
///
/// \param onlyBlockPointers Whether we should only look into block
/// pointer types (vs. all pointer types).
static DeclaratorChunk *maybeMovePastReturnType(Declarator &declarator,
                                                unsigned i,
                                                bool onlyBlockPointers) {
  assert(i <= declarator.getNumTypeObjects());

  DeclaratorChunk *result = nullptr;

  // First, look inwards past parens for a function declarator.
  for (; i != 0; --i) {
    DeclaratorChunk &fnChunk = declarator.getTypeObject(i-1);
    switch (fnChunk.Kind) {
    case DeclaratorChunk::Paren:
      continue;

    // If we find anything except a function, bail out.
    case DeclaratorChunk::Pointer:
    case DeclaratorChunk::BlockPointer:
    case DeclaratorChunk::Array:
    case DeclaratorChunk::Reference:
    case DeclaratorChunk::MemberPointer:
    case DeclaratorChunk::Pipe:
      return result;

    // If we do find a function declarator, scan inwards from that,
    // looking for a (block-)pointer declarator.
    case DeclaratorChunk::Function:
      for (--i; i != 0; --i) {
        DeclaratorChunk &ptrChunk = declarator.getTypeObject(i-1);
        switch (ptrChunk.Kind) {
        case DeclaratorChunk::Paren:
        case DeclaratorChunk::Array:
        case DeclaratorChunk::Function:
        case DeclaratorChunk::Reference:
        case DeclaratorChunk::Pipe:
          continue;

        case DeclaratorChunk::MemberPointer:
        case DeclaratorChunk::Pointer:
          if (onlyBlockPointers)
            continue;

          [[fallthrough]];

        case DeclaratorChunk::BlockPointer:
          result = &ptrChunk;
          goto continue_outer;
        }
        llvm_unreachable("bad declarator chunk kind");
      }

      // If we run out of declarators doing that, we're done.
      return result;
    }
    llvm_unreachable("bad declarator chunk kind");

    // Okay, reconsider from our new point.
  continue_outer: ;
  }

  // Ran out of chunks, bail out.
  return result;
}

/// Given that an objc_gc attribute was written somewhere on a
/// declaration *other* than on the declarator itself (for which, use
/// distributeObjCPointerTypeAttrFromDeclarator), and given that it
/// didn't apply in whatever position it was written in, try to move
/// it to a more appropriate position.
static void distributeObjCPointerTypeAttr(TypeProcessingState &state,
                                          ParsedAttr &attr, QualType type) {
  Declarator &declarator = state.getDeclarator();

  // Move it to the outermost normal or block pointer declarator.
  for (unsigned i = state.getCurrentChunkIndex(); i != 0; --i) {
    DeclaratorChunk &chunk = declarator.getTypeObject(i-1);
    switch (chunk.Kind) {
    case DeclaratorChunk::Pointer:
    case DeclaratorChunk::BlockPointer: {
      // But don't move an ARC ownership attribute to the return type
      // of a block.
      DeclaratorChunk *destChunk = nullptr;
      if (state.isProcessingDeclSpec() &&
          attr.getKind() == ParsedAttr::AT_ObjCOwnership)
        destChunk = maybeMovePastReturnType(declarator, i - 1,
                                            /*onlyBlockPointers=*/true);
      if (!destChunk) destChunk = &chunk;

      moveAttrFromListToList(attr, state.getCurrentAttributes(),
                             destChunk->getAttrs());
      return;
    }

    case DeclaratorChunk::Paren:
    case DeclaratorChunk::Array:
      continue;

    // We may be starting at the return type of a block.
    case DeclaratorChunk::Function:
      if (state.isProcessingDeclSpec() &&
          attr.getKind() == ParsedAttr::AT_ObjCOwnership) {
        if (DeclaratorChunk *dest = maybeMovePastReturnType(
                                      declarator, i,
                                      /*onlyBlockPointers=*/true)) {
          moveAttrFromListToList(attr, state.getCurrentAttributes(),
                                 dest->getAttrs());
          return;
        }
      }
      goto error;

    // Don't walk through these.
    case DeclaratorChunk::Reference:
    case DeclaratorChunk::MemberPointer:
    case DeclaratorChunk::Pipe:
      goto error;
    }
  }
 error:

  diagnoseBadTypeAttribute(state.getSema(), attr, type);
}

/// Distribute an objc_gc type attribute that was written on the
/// declarator.
static void distributeObjCPointerTypeAttrFromDeclarator(
    TypeProcessingState &state, ParsedAttr &attr, QualType &declSpecType) {
  Declarator &declarator = state.getDeclarator();

  // objc_gc goes on the innermost pointer to something that's not a
  // pointer.
  unsigned innermost = -1U;
  bool considerDeclSpec = true;
  for (unsigned i = 0, e = declarator.getNumTypeObjects(); i != e; ++i) {
    DeclaratorChunk &chunk = declarator.getTypeObject(i);
    switch (chunk.Kind) {
    case DeclaratorChunk::Pointer:
    case DeclaratorChunk::BlockPointer:
      innermost = i;
      continue;

    case DeclaratorChunk::Reference:
    case DeclaratorChunk::MemberPointer:
    case DeclaratorChunk::Paren:
    case DeclaratorChunk::Array:
    case DeclaratorChunk::Pipe:
      continue;

    case DeclaratorChunk::Function:
      considerDeclSpec = false;
      goto done;
    }
  }
 done:

  // That might actually be the decl spec if we weren't blocked by
  // anything in the declarator.
  if (considerDeclSpec) {
    if (handleObjCPointerTypeAttr(state, attr, declSpecType)) {
      // Splice the attribute into the decl spec.  Prevents the
      // attribute from being applied multiple times and gives
      // the source-location-filler something to work with.
      state.saveDeclSpecAttrs();
      declarator.getMutableDeclSpec().getAttributes().takeOneFrom(
          declarator.getAttributes(), &attr);
      return;
    }
  }

  // Otherwise, if we found an appropriate chunk, splice the attribute
  // into it.
  if (innermost != -1U) {
    moveAttrFromListToList(attr, declarator.getAttributes(),
                           declarator.getTypeObject(innermost).getAttrs());
    return;
  }

  // Otherwise, diagnose when we're done building the type.
  declarator.getAttributes().remove(&attr);
  state.addIgnoredTypeAttr(attr);
}

/// A function type attribute was written somewhere in a declaration
/// *other* than on the declarator itself or in the decl spec.  Given
/// that it didn't apply in whatever position it was written in, try
/// to move it to a more appropriate position.
static void distributeFunctionTypeAttr(TypeProcessingState &state,
                                       ParsedAttr &attr, QualType type) {
  Declarator &declarator = state.getDeclarator();

  // Try to push the attribute from the return type of a function to
  // the function itself.
  for (unsigned i = state.getCurrentChunkIndex(); i != 0; --i) {
    DeclaratorChunk &chunk = declarator.getTypeObject(i-1);
    switch (chunk.Kind) {
    case DeclaratorChunk::Function:
      moveAttrFromListToList(attr, state.getCurrentAttributes(),
                             chunk.getAttrs());
      return;

    case DeclaratorChunk::Paren:
    case DeclaratorChunk::Pointer:
    case DeclaratorChunk::BlockPointer:
    case DeclaratorChunk::Array:
    case DeclaratorChunk::Reference:
    case DeclaratorChunk::MemberPointer:
    case DeclaratorChunk::Pipe:
      continue;
    }
  }

  diagnoseBadTypeAttribute(state.getSema(), attr, type);
}

/// Try to distribute a function type attribute to the innermost
/// function chunk or type.  Returns true if the attribute was
/// distributed, false if no location was found.
static bool distributeFunctionTypeAttrToInnermost(
    TypeProcessingState &state, ParsedAttr &attr,
    ParsedAttributesView &attrList, QualType &declSpecType,
    CUDAFunctionTarget CFT) {
  Declarator &declarator = state.getDeclarator();

  // Put it on the innermost function chunk, if there is one.
  for (unsigned i = 0, e = declarator.getNumTypeObjects(); i != e; ++i) {
    DeclaratorChunk &chunk = declarator.getTypeObject(i);
    if (chunk.Kind != DeclaratorChunk::Function) continue;

    moveAttrFromListToList(attr, attrList, chunk.getAttrs());
    return true;
  }

  return handleFunctionTypeAttr(state, attr, declSpecType, CFT);
}

/// A function type attribute was written in the decl spec.  Try to
/// apply it somewhere.
static void distributeFunctionTypeAttrFromDeclSpec(TypeProcessingState &state,
                                                   ParsedAttr &attr,
                                                   QualType &declSpecType,
                                                   CUDAFunctionTarget CFT) {
  state.saveDeclSpecAttrs();

  // Try to distribute to the innermost.
  if (distributeFunctionTypeAttrToInnermost(
          state, attr, state.getCurrentAttributes(), declSpecType, CFT))
    return;

  // If that failed, diagnose the bad attribute when the declarator is
  // fully built.
  state.addIgnoredTypeAttr(attr);
}

/// A function type attribute was written on the declarator or declaration.
/// Try to apply it somewhere.
/// `Attrs` is the attribute list containing the declaration (either of the
/// declarator or the declaration).
static void distributeFunctionTypeAttrFromDeclarator(TypeProcessingState &state,
                                                     ParsedAttr &attr,
                                                     QualType &declSpecType,
                                                     CUDAFunctionTarget CFT) {
  Declarator &declarator = state.getDeclarator();

  // Try to distribute to the innermost.
  if (distributeFunctionTypeAttrToInnermost(
          state, attr, declarator.getAttributes(), declSpecType, CFT))
    return;

  // If that failed, diagnose the bad attribute when the declarator is
  // fully built.
  declarator.getAttributes().remove(&attr);
  state.addIgnoredTypeAttr(attr);
}

/// Given that there are attributes written on the declarator or declaration
/// itself, try to distribute any type attributes to the appropriate
/// declarator chunk.
///
/// These are attributes like the following:
///   int f ATTR;
///   int (f ATTR)();
/// but not necessarily this:
///   int f() ATTR;
///
/// `Attrs` is the attribute list containing the declaration (either of the
/// declarator or the declaration).
static void distributeTypeAttrsFromDeclarator(TypeProcessingState &state,
                                              QualType &declSpecType,
                                              CUDAFunctionTarget CFT) {
  // The called functions in this loop actually remove things from the current
  // list, so iterating over the existing list isn't possible.  Instead, make a
  // non-owning copy and iterate over that.
  ParsedAttributesView AttrsCopy{state.getDeclarator().getAttributes()};
  for (ParsedAttr &attr : AttrsCopy) {
    // Do not distribute [[]] attributes. They have strict rules for what
    // they appertain to.
    if (attr.isStandardAttributeSyntax() || attr.isRegularKeywordAttribute())
      continue;

    switch (attr.getKind()) {
    OBJC_POINTER_TYPE_ATTRS_CASELIST:
      distributeObjCPointerTypeAttrFromDeclarator(state, attr, declSpecType);
      break;

    FUNCTION_TYPE_ATTRS_CASELIST:
      distributeFunctionTypeAttrFromDeclarator(state, attr, declSpecType, CFT);
      break;

    MS_TYPE_ATTRS_CASELIST:
      // Microsoft type attributes cannot go after the declarator-id.
      continue;

    NULLABILITY_TYPE_ATTRS_CASELIST:
      // Nullability specifiers cannot go after the declarator-id.

    // Objective-C __kindof does not get distributed.
    case ParsedAttr::AT_ObjCKindOf:
      continue;

    default:
      break;
    }
  }
}

/// Add a synthetic '()' to a block-literal declarator if it is
/// required, given the return type.
static void maybeSynthesizeBlockSignature(TypeProcessingState &state,
                                          QualType declSpecType) {
  Declarator &declarator = state.getDeclarator();

  // First, check whether the declarator would produce a function,
  // i.e. whether the innermost semantic chunk is a function.
  if (declarator.isFunctionDeclarator()) {
    // If so, make that declarator a prototyped declarator.
    declarator.getFunctionTypeInfo().hasPrototype = true;
    return;
  }

  // If there are any type objects, the type as written won't name a
  // function, regardless of the decl spec type.  This is because a
  // block signature declarator is always an abstract-declarator, and
  // abstract-declarators can't just be parentheses chunks.  Therefore
  // we need to build a function chunk unless there are no type
  // objects and the decl spec type is a function.
  if (!declarator.getNumTypeObjects() && declSpecType->isFunctionType())
    return;

  // Note that there *are* cases with invalid declarators where
  // declarators consist solely of parentheses.  In general, these
  // occur only in failed efforts to make function declarators, so
  // faking up the function chunk is still the right thing to do.

  // Otherwise, we need to fake up a function declarator.
  SourceLocation loc = declarator.getBeginLoc();

  // ...and *prepend* it to the declarator.
  SourceLocation NoLoc;
  declarator.AddInnermostTypeInfo(DeclaratorChunk::getFunction(
      /*HasProto=*/true,
      /*IsAmbiguous=*/false,
      /*LParenLoc=*/NoLoc,
      /*ArgInfo=*/nullptr,
      /*NumParams=*/0,
      /*EllipsisLoc=*/NoLoc,
      /*RParenLoc=*/NoLoc,
      /*RefQualifierIsLvalueRef=*/true,
      /*RefQualifierLoc=*/NoLoc,
      /*MutableLoc=*/NoLoc, EST_None,
      /*ESpecRange=*/SourceRange(),
      /*Exceptions=*/nullptr,
      /*ExceptionRanges=*/nullptr,
      /*NumExceptions=*/0,
      /*NoexceptExpr=*/nullptr,
      /*ExceptionSpecTokens=*/nullptr,
      /*DeclsInPrototype=*/std::nullopt, loc, loc, declarator));

  // For consistency, make sure the state still has us as processing
  // the decl spec.
  assert(state.getCurrentChunkIndex() == declarator.getNumTypeObjects() - 1);
  state.setCurrentChunkIndex(declarator.getNumTypeObjects());
}

static void diagnoseAndRemoveTypeQualifiers(Sema &S, const DeclSpec &DS,
                                            unsigned &TypeQuals,
                                            QualType TypeSoFar,
                                            unsigned RemoveTQs,
                                            unsigned DiagID) {
  // If this occurs outside a template instantiation, warn the user about
  // it; they probably didn't mean to specify a redundant qualifier.
  typedef std::pair<DeclSpec::TQ, SourceLocation> QualLoc;
  for (QualLoc Qual : {QualLoc(DeclSpec::TQ_const, DS.getConstSpecLoc()),
                       QualLoc(DeclSpec::TQ_restrict, DS.getRestrictSpecLoc()),
                       QualLoc(DeclSpec::TQ_volatile, DS.getVolatileSpecLoc()),
                       QualLoc(DeclSpec::TQ_atomic, DS.getAtomicSpecLoc())}) {
    if (!(RemoveTQs & Qual.first))
      continue;

    if (!S.inTemplateInstantiation()) {
      if (TypeQuals & Qual.first)
        S.Diag(Qual.second, DiagID)
          << DeclSpec::getSpecifierName(Qual.first) << TypeSoFar
          << FixItHint::CreateRemoval(Qual.second);
    }

    TypeQuals &= ~Qual.first;
  }
}

/// Return true if this is omitted block return type. Also check type
/// attributes and type qualifiers when returning true.
static bool checkOmittedBlockReturnType(Sema &S, Declarator &declarator,
                                        QualType Result) {
  if (!isOmittedBlockReturnType(declarator))
    return false;

  // Warn if we see type attributes for omitted return type on a block literal.
  SmallVector<ParsedAttr *, 2> ToBeRemoved;
  for (ParsedAttr &AL : declarator.getMutableDeclSpec().getAttributes()) {
    if (AL.isInvalid() || !AL.isTypeAttr())
      continue;
    S.Diag(AL.getLoc(),
           diag::warn_block_literal_attributes_on_omitted_return_type)
        << AL;
    ToBeRemoved.push_back(&AL);
  }
  // Remove bad attributes from the list.
  for (ParsedAttr *AL : ToBeRemoved)
    declarator.getMutableDeclSpec().getAttributes().remove(AL);

  // Warn if we see type qualifiers for omitted return type on a block literal.
  const DeclSpec &DS = declarator.getDeclSpec();
  unsigned TypeQuals = DS.getTypeQualifiers();
  diagnoseAndRemoveTypeQualifiers(S, DS, TypeQuals, Result, (unsigned)-1,
      diag::warn_block_literal_qualifiers_on_omitted_return_type);
  declarator.getMutableDeclSpec().ClearTypeQualifiers();

  return true;
}

static OpenCLAccessAttr::Spelling
getImageAccess(const ParsedAttributesView &Attrs) {
  for (const ParsedAttr &AL : Attrs)
    if (AL.getKind() == ParsedAttr::AT_OpenCLAccess)
      return static_cast<OpenCLAccessAttr::Spelling>(AL.getSemanticSpelling());
  return OpenCLAccessAttr::Keyword_read_only;
}

static UnaryTransformType::UTTKind
TSTToUnaryTransformType(DeclSpec::TST SwitchTST) {
  switch (SwitchTST) {
#define TRANSFORM_TYPE_TRAIT_DEF(Enum, Trait)                                  \
  case TST_##Trait:                                                            \
    return UnaryTransformType::Enum;
#include "clang/Basic/TransformTypeTraits.def"
  default:
    llvm_unreachable("attempted to parse a non-unary transform builtin");
  }
}

/// Convert the specified declspec to the appropriate type
/// object.
/// \param state Specifies the declarator containing the declaration specifier
/// to be converted, along with other associated processing state.
/// \returns The type described by the declaration specifiers.  This function
/// never returns null.
static QualType ConvertDeclSpecToType(TypeProcessingState &state) {
  // FIXME: Should move the logic from DeclSpec::Finish to here for validity
  // checking.

  Sema &S = state.getSema();
  Declarator &declarator = state.getDeclarator();
  DeclSpec &DS = declarator.getMutableDeclSpec();
  SourceLocation DeclLoc = declarator.getIdentifierLoc();
  if (DeclLoc.isInvalid())
    DeclLoc = DS.getBeginLoc();

  ASTContext &Context = S.Context;

  QualType Result;
  switch (DS.getTypeSpecType()) {
  case DeclSpec::TST_void:
    Result = Context.VoidTy;
    break;
  case DeclSpec::TST_char:
    if (DS.getTypeSpecSign() == TypeSpecifierSign::Unspecified)
      Result = Context.CharTy;
    else if (DS.getTypeSpecSign() == TypeSpecifierSign::Signed)
      Result = Context.SignedCharTy;
    else {
      assert(DS.getTypeSpecSign() == TypeSpecifierSign::Unsigned &&
             "Unknown TSS value");
      Result = Context.UnsignedCharTy;
    }
    break;
  case DeclSpec::TST_wchar:
    if (DS.getTypeSpecSign() == TypeSpecifierSign::Unspecified)
      Result = Context.WCharTy;
    else if (DS.getTypeSpecSign() == TypeSpecifierSign::Signed) {
      S.Diag(DS.getTypeSpecSignLoc(), diag::ext_wchar_t_sign_spec)
        << DS.getSpecifierName(DS.getTypeSpecType(),
                               Context.getPrintingPolicy());
      Result = Context.getSignedWCharType();
    } else {
      assert(DS.getTypeSpecSign() == TypeSpecifierSign::Unsigned &&
             "Unknown TSS value");
      S.Diag(DS.getTypeSpecSignLoc(), diag::ext_wchar_t_sign_spec)
        << DS.getSpecifierName(DS.getTypeSpecType(),
                               Context.getPrintingPolicy());
      Result = Context.getUnsignedWCharType();
    }
    break;
  case DeclSpec::TST_char8:
    assert(DS.getTypeSpecSign() == TypeSpecifierSign::Unspecified &&
           "Unknown TSS value");
    Result = Context.Char8Ty;
    break;
  case DeclSpec::TST_char16:
    assert(DS.getTypeSpecSign() == TypeSpecifierSign::Unspecified &&
           "Unknown TSS value");
    Result = Context.Char16Ty;
    break;
  case DeclSpec::TST_char32:
    assert(DS.getTypeSpecSign() == TypeSpecifierSign::Unspecified &&
           "Unknown TSS value");
    Result = Context.Char32Ty;
    break;
  case DeclSpec::TST_unspecified:
    // If this is a missing declspec in a block literal return context, then it
    // is inferred from the return statements inside the block.
    // The declspec is always missing in a lambda expr context; it is either
    // specified with a trailing return type or inferred.
    if (S.getLangOpts().CPlusPlus14 &&
        declarator.getContext() == DeclaratorContext::LambdaExpr) {
      // In C++1y, a lambda's implicit return type is 'auto'.
      Result = Context.getAutoDeductType();
      break;
    } else if (declarator.getContext() == DeclaratorContext::LambdaExpr ||
               checkOmittedBlockReturnType(S, declarator,
                                           Context.DependentTy)) {
      Result = Context.DependentTy;
      break;
    }

    // Unspecified typespec defaults to int in C90.  However, the C90 grammar
    // [C90 6.5] only allows a decl-spec if there was *some* type-specifier,
    // type-qualifier, or storage-class-specifier.  If not, emit an extwarn.
    // Note that the one exception to this is function definitions, which are
    // allowed to be completely missing a declspec.  This is handled in the
    // parser already though by it pretending to have seen an 'int' in this
    // case.
    if (S.getLangOpts().isImplicitIntRequired()) {
      S.Diag(DeclLoc, diag::warn_missing_type_specifier)
          << DS.getSourceRange()
          << FixItHint::CreateInsertion(DS.getBeginLoc(), "int");
    } else if (!DS.hasTypeSpecifier()) {
      // C99 and C++ require a type specifier.  For example, C99 6.7.2p2 says:
      // "At least one type specifier shall be given in the declaration
      // specifiers in each declaration, and in the specifier-qualifier list in
      // each struct declaration and type name."
      if (!S.getLangOpts().isImplicitIntAllowed() && !DS.isTypeSpecPipe()) {
        S.Diag(DeclLoc, diag::err_missing_type_specifier)
            << DS.getSourceRange();

        // When this occurs, often something is very broken with the value
        // being declared, poison it as invalid so we don't get chains of
        // errors.
        declarator.setInvalidType(true);
      } else if (S.getLangOpts().getOpenCLCompatibleVersion() >= 200 &&
                 DS.isTypeSpecPipe()) {
        S.Diag(DeclLoc, diag::err_missing_actual_pipe_type)
            << DS.getSourceRange();
        declarator.setInvalidType(true);
      } else {
        assert(S.getLangOpts().isImplicitIntAllowed() &&
               "implicit int is disabled?");
        S.Diag(DeclLoc, diag::ext_missing_type_specifier)
            << DS.getSourceRange()
            << FixItHint::CreateInsertion(DS.getBeginLoc(), "int");
      }
    }

    [[fallthrough]];
  case DeclSpec::TST_int: {
    if (DS.getTypeSpecSign() != TypeSpecifierSign::Unsigned) {
      switch (DS.getTypeSpecWidth()) {
      case TypeSpecifierWidth::Unspecified:
        Result = Context.IntTy;
        break;
      case TypeSpecifierWidth::Short:
        Result = Context.ShortTy;
        break;
      case TypeSpecifierWidth::Long:
        Result = Context.LongTy;
        break;
      case TypeSpecifierWidth::LongLong:
        Result = Context.LongLongTy;

        // 'long long' is a C99 or C++11 feature.
        if (!S.getLangOpts().C99) {
          if (S.getLangOpts().CPlusPlus)
            S.Diag(DS.getTypeSpecWidthLoc(),
                   S.getLangOpts().CPlusPlus11 ?
                   diag::warn_cxx98_compat_longlong : diag::ext_cxx11_longlong);
          else
            S.Diag(DS.getTypeSpecWidthLoc(), diag::ext_c99_longlong);
        }
        break;
      }
    } else {
      switch (DS.getTypeSpecWidth()) {
      case TypeSpecifierWidth::Unspecified:
        Result = Context.UnsignedIntTy;
        break;
      case TypeSpecifierWidth::Short:
        Result = Context.UnsignedShortTy;
        break;
      case TypeSpecifierWidth::Long:
        Result = Context.UnsignedLongTy;
        break;
      case TypeSpecifierWidth::LongLong:
        Result = Context.UnsignedLongLongTy;

        // 'long long' is a C99 or C++11 feature.
        if (!S.getLangOpts().C99) {
          if (S.getLangOpts().CPlusPlus)
            S.Diag(DS.getTypeSpecWidthLoc(),
                   S.getLangOpts().CPlusPlus11 ?
                   diag::warn_cxx98_compat_longlong : diag::ext_cxx11_longlong);
          else
            S.Diag(DS.getTypeSpecWidthLoc(), diag::ext_c99_longlong);
        }
        break;
      }
    }
    break;
  }
  case DeclSpec::TST_bitint: {
    if (!S.Context.getTargetInfo().hasBitIntType())
      S.Diag(DS.getTypeSpecTypeLoc(), diag::err_type_unsupported) << "_BitInt";
    Result =
        S.BuildBitIntType(DS.getTypeSpecSign() == TypeSpecifierSign::Unsigned,
                          DS.getRepAsExpr(), DS.getBeginLoc());
    if (Result.isNull()) {
      Result = Context.IntTy;
      declarator.setInvalidType(true);
    }
    break;
  }
  case DeclSpec::TST_accum: {
    switch (DS.getTypeSpecWidth()) {
    case TypeSpecifierWidth::Short:
      Result = Context.ShortAccumTy;
      break;
    case TypeSpecifierWidth::Unspecified:
      Result = Context.AccumTy;
      break;
    case TypeSpecifierWidth::Long:
      Result = Context.LongAccumTy;
      break;
    case TypeSpecifierWidth::LongLong:
      llvm_unreachable("Unable to specify long long as _Accum width");
    }

    if (DS.getTypeSpecSign() == TypeSpecifierSign::Unsigned)
      Result = Context.getCorrespondingUnsignedType(Result);

    if (DS.isTypeSpecSat())
      Result = Context.getCorrespondingSaturatedType(Result);

    break;
  }
  case DeclSpec::TST_fract: {
    switch (DS.getTypeSpecWidth()) {
    case TypeSpecifierWidth::Short:
      Result = Context.ShortFractTy;
      break;
    case TypeSpecifierWidth::Unspecified:
      Result = Context.FractTy;
      break;
    case TypeSpecifierWidth::Long:
      Result = Context.LongFractTy;
      break;
    case TypeSpecifierWidth::LongLong:
      llvm_unreachable("Unable to specify long long as _Fract width");
    }

    if (DS.getTypeSpecSign() == TypeSpecifierSign::Unsigned)
      Result = Context.getCorrespondingUnsignedType(Result);

    if (DS.isTypeSpecSat())
      Result = Context.getCorrespondingSaturatedType(Result);

    break;
  }
  case DeclSpec::TST_int128:
    if (!S.Context.getTargetInfo().hasInt128Type() &&
        !(S.getLangOpts().SYCLIsDevice || S.getLangOpts().CUDAIsDevice ||
          (S.getLangOpts().OpenMP && S.getLangOpts().OpenMPIsTargetDevice)))
      S.Diag(DS.getTypeSpecTypeLoc(), diag::err_type_unsupported)
        << "__int128";
    if (DS.getTypeSpecSign() == TypeSpecifierSign::Unsigned)
      Result = Context.UnsignedInt128Ty;
    else
      Result = Context.Int128Ty;
    break;
  case DeclSpec::TST_float16:
    // CUDA host and device may have different _Float16 support, therefore
    // do not diagnose _Float16 usage to avoid false alarm.
    // ToDo: more precise diagnostics for CUDA.
    if (!S.Context.getTargetInfo().hasFloat16Type() && !S.getLangOpts().CUDA &&
        !(S.getLangOpts().OpenMP && S.getLangOpts().OpenMPIsTargetDevice))
      S.Diag(DS.getTypeSpecTypeLoc(), diag::err_type_unsupported)
        << "_Float16";
    Result = Context.Float16Ty;
    break;
  case DeclSpec::TST_half:    Result = Context.HalfTy; break;
  case DeclSpec::TST_BFloat16:
    if (!S.Context.getTargetInfo().hasBFloat16Type() &&
        !(S.getLangOpts().OpenMP && S.getLangOpts().OpenMPIsTargetDevice) &&
        !S.getLangOpts().SYCLIsDevice)
      S.Diag(DS.getTypeSpecTypeLoc(), diag::err_type_unsupported) << "__bf16";
    Result = Context.BFloat16Ty;
    break;
  case DeclSpec::TST_float:   Result = Context.FloatTy; break;
  case DeclSpec::TST_double:
    if (DS.getTypeSpecWidth() == TypeSpecifierWidth::Long)
      Result = Context.LongDoubleTy;
    else
      Result = Context.DoubleTy;
    if (S.getLangOpts().OpenCL) {
      if (!S.getOpenCLOptions().isSupported("cl_khr_fp64", S.getLangOpts()))
        S.Diag(DS.getTypeSpecTypeLoc(), diag::err_opencl_requires_extension)
            << 0 << Result
            << (S.getLangOpts().getOpenCLCompatibleVersion() == 300
                    ? "cl_khr_fp64 and __opencl_c_fp64"
                    : "cl_khr_fp64");
      else if (!S.getOpenCLOptions().isAvailableOption("cl_khr_fp64", S.getLangOpts()))
        S.Diag(DS.getTypeSpecTypeLoc(), diag::ext_opencl_double_without_pragma);
    }
    break;
  case DeclSpec::TST_float128:
    if (!S.Context.getTargetInfo().hasFloat128Type() &&
        !S.getLangOpts().SYCLIsDevice && !S.getLangOpts().CUDAIsDevice &&
        !(S.getLangOpts().OpenMP && S.getLangOpts().OpenMPIsTargetDevice))
      S.Diag(DS.getTypeSpecTypeLoc(), diag::err_type_unsupported)
        << "__float128";
    Result = Context.Float128Ty;
    break;
  case DeclSpec::TST_ibm128:
    if (!S.Context.getTargetInfo().hasIbm128Type() &&
        !S.getLangOpts().SYCLIsDevice &&
        !(S.getLangOpts().OpenMP && S.getLangOpts().OpenMPIsTargetDevice))
      S.Diag(DS.getTypeSpecTypeLoc(), diag::err_type_unsupported) << "__ibm128";
    Result = Context.Ibm128Ty;
    break;
  case DeclSpec::TST_bool:
    Result = Context.BoolTy; // _Bool or bool
    break;
  case DeclSpec::TST_decimal32:    // _Decimal32
  case DeclSpec::TST_decimal64:    // _Decimal64
  case DeclSpec::TST_decimal128:   // _Decimal128
    S.Diag(DS.getTypeSpecTypeLoc(), diag::err_decimal_unsupported);
    Result = Context.IntTy;
    declarator.setInvalidType(true);
    break;
  case DeclSpec::TST_class:
  case DeclSpec::TST_enum:
  case DeclSpec::TST_union:
  case DeclSpec::TST_struct:
  case DeclSpec::TST_interface: {
    TagDecl *D = dyn_cast_or_null<TagDecl>(DS.getRepAsDecl());
    if (!D) {
      // This can happen in C++ with ambiguous lookups.
      Result = Context.IntTy;
      declarator.setInvalidType(true);
      break;
    }

    // If the type is deprecated or unavailable, diagnose it.
    S.DiagnoseUseOfDecl(D, DS.getTypeSpecTypeNameLoc());

    assert(DS.getTypeSpecWidth() == TypeSpecifierWidth::Unspecified &&
           DS.getTypeSpecComplex() == 0 &&
           DS.getTypeSpecSign() == TypeSpecifierSign::Unspecified &&
           "No qualifiers on tag names!");

    // TypeQuals handled by caller.
    Result = Context.getTypeDeclType(D);

    // In both C and C++, make an ElaboratedType.
    ElaboratedTypeKeyword Keyword
      = ElaboratedType::getKeywordForTypeSpec(DS.getTypeSpecType());
    Result = S.getElaboratedType(Keyword, DS.getTypeSpecScope(), Result,
                                 DS.isTypeSpecOwned() ? D : nullptr);
    break;
  }
  case DeclSpec::TST_typename: {
    assert(DS.getTypeSpecWidth() == TypeSpecifierWidth::Unspecified &&
           DS.getTypeSpecComplex() == 0 &&
           DS.getTypeSpecSign() == TypeSpecifierSign::Unspecified &&
           "Can't handle qualifiers on typedef names yet!");
    Result = S.GetTypeFromParser(DS.getRepAsType());
    if (Result.isNull()) {
      declarator.setInvalidType(true);
    }

    // TypeQuals handled by caller.
    break;
  }
  case DeclSpec::TST_typeof_unqualType:
  case DeclSpec::TST_typeofType:
    // FIXME: Preserve type source info.
    Result = S.GetTypeFromParser(DS.getRepAsType());
    assert(!Result.isNull() && "Didn't get a type for typeof?");
    if (!Result->isDependentType())
      if (const TagType *TT = Result->getAs<TagType>())
        S.DiagnoseUseOfDecl(TT->getDecl(), DS.getTypeSpecTypeLoc());
    // TypeQuals handled by caller.
    Result = Context.getTypeOfType(
        Result, DS.getTypeSpecType() == DeclSpec::TST_typeof_unqualType
                    ? TypeOfKind::Unqualified
                    : TypeOfKind::Qualified);
    break;
  case DeclSpec::TST_typeof_unqualExpr:
  case DeclSpec::TST_typeofExpr: {
    Expr *E = DS.getRepAsExpr();
    assert(E && "Didn't get an expression for typeof?");
    // TypeQuals handled by caller.
    Result = S.BuildTypeofExprType(E, DS.getTypeSpecType() ==
                                              DeclSpec::TST_typeof_unqualExpr
                                          ? TypeOfKind::Unqualified
                                          : TypeOfKind::Qualified);
    if (Result.isNull()) {
      Result = Context.IntTy;
      declarator.setInvalidType(true);
    }
    break;
  }
  case DeclSpec::TST_decltype: {
    Expr *E = DS.getRepAsExpr();
    assert(E && "Didn't get an expression for decltype?");
    // TypeQuals handled by caller.
    Result = S.BuildDecltypeType(E);
    if (Result.isNull()) {
      Result = Context.IntTy;
      declarator.setInvalidType(true);
    }
    break;
  }
  case DeclSpec::TST_typename_pack_indexing: {
    Expr *E = DS.getPackIndexingExpr();
    assert(E && "Didn't get an expression for pack indexing");
    QualType Pattern = S.GetTypeFromParser(DS.getRepAsType());
    Result = S.BuildPackIndexingType(Pattern, E, DS.getBeginLoc(),
                                     DS.getEllipsisLoc());
    if (Result.isNull()) {
      declarator.setInvalidType(true);
      Result = Context.IntTy;
    }
    break;
  }

#define TRANSFORM_TYPE_TRAIT_DEF(_, Trait) case DeclSpec::TST_##Trait:
#include "clang/Basic/TransformTypeTraits.def"
    Result = S.GetTypeFromParser(DS.getRepAsType());
    assert(!Result.isNull() && "Didn't get a type for the transformation?");
    Result = S.BuildUnaryTransformType(
        Result, TSTToUnaryTransformType(DS.getTypeSpecType()),
        DS.getTypeSpecTypeLoc());
    if (Result.isNull()) {
      Result = Context.IntTy;
      declarator.setInvalidType(true);
    }
    break;

  case DeclSpec::TST_auto:
  case DeclSpec::TST_decltype_auto: {
    auto AutoKW = DS.getTypeSpecType() == DeclSpec::TST_decltype_auto
                      ? AutoTypeKeyword::DecltypeAuto
                      : AutoTypeKeyword::Auto;

    ConceptDecl *TypeConstraintConcept = nullptr;
    llvm::SmallVector<TemplateArgument, 8> TemplateArgs;
    if (DS.isConstrainedAuto()) {
      if (TemplateIdAnnotation *TemplateId = DS.getRepAsTemplateId()) {
        TypeConstraintConcept =
            cast<ConceptDecl>(TemplateId->Template.get().getAsTemplateDecl());
        TemplateArgumentListInfo TemplateArgsInfo;
        TemplateArgsInfo.setLAngleLoc(TemplateId->LAngleLoc);
        TemplateArgsInfo.setRAngleLoc(TemplateId->RAngleLoc);
        ASTTemplateArgsPtr TemplateArgsPtr(TemplateId->getTemplateArgs(),
                                           TemplateId->NumArgs);
        S.translateTemplateArguments(TemplateArgsPtr, TemplateArgsInfo);
        for (const auto &ArgLoc : TemplateArgsInfo.arguments())
          TemplateArgs.push_back(ArgLoc.getArgument());
      } else {
        declarator.setInvalidType(true);
      }
    }
    Result = S.Context.getAutoType(QualType(), AutoKW,
                                   /*IsDependent*/ false, /*IsPack=*/false,
                                   TypeConstraintConcept, TemplateArgs);
    break;
  }

  case DeclSpec::TST_auto_type:
    Result = Context.getAutoType(QualType(), AutoTypeKeyword::GNUAutoType, false);
    break;

  case DeclSpec::TST_unknown_anytype:
    Result = Context.UnknownAnyTy;
    break;

  case DeclSpec::TST_atomic:
    Result = S.GetTypeFromParser(DS.getRepAsType());
    assert(!Result.isNull() && "Didn't get a type for _Atomic?");
    Result = S.BuildAtomicType(Result, DS.getTypeSpecTypeLoc());
    if (Result.isNull()) {
      Result = Context.IntTy;
      declarator.setInvalidType(true);
    }
    break;

#define GENERIC_IMAGE_TYPE(ImgType, Id)                                        \
  case DeclSpec::TST_##ImgType##_t:                                            \
    switch (getImageAccess(DS.getAttributes())) {                              \
    case OpenCLAccessAttr::Keyword_write_only:                                 \
      Result = Context.Id##WOTy;                                               \
      break;                                                                   \
    case OpenCLAccessAttr::Keyword_read_write:                                 \
      Result = Context.Id##RWTy;                                               \
      break;                                                                   \
    case OpenCLAccessAttr::Keyword_read_only:                                  \
      Result = Context.Id##ROTy;                                               \
      break;                                                                   \
    case OpenCLAccessAttr::SpellingNotCalculated:                              \
      llvm_unreachable("Spelling not yet calculated");                         \
    }                                                                          \
    break;
#include "clang/Basic/OpenCLImageTypes.def"

  case DeclSpec::TST_error:
    Result = Context.IntTy;
    declarator.setInvalidType(true);
    break;
  }

  // FIXME: we want resulting declarations to be marked invalid, but claiming
  // the type is invalid is too strong - e.g. it causes ActOnTypeName to return
  // a null type.
  if (Result->containsErrors())
    declarator.setInvalidType();

  if (S.getLangOpts().OpenCL) {
    const auto &OpenCLOptions = S.getOpenCLOptions();
    bool IsOpenCLC30Compatible =
        S.getLangOpts().getOpenCLCompatibleVersion() == 300;
    // OpenCL C v3.0 s6.3.3 - OpenCL image types require __opencl_c_images
    // support.
    // OpenCL C v3.0 s6.2.1 - OpenCL 3d image write types requires support
    // for OpenCL C 2.0, or OpenCL C 3.0 or newer and the
    // __opencl_c_3d_image_writes feature. OpenCL C v3.0 API s4.2 - For devices
    // that support OpenCL 3.0, cl_khr_3d_image_writes must be returned when and
    // only when the optional feature is supported
    if ((Result->isImageType() || Result->isSamplerT()) &&
        (IsOpenCLC30Compatible &&
         !OpenCLOptions.isSupported("__opencl_c_images", S.getLangOpts()))) {
      S.Diag(DS.getTypeSpecTypeLoc(), diag::err_opencl_requires_extension)
          << 0 << Result << "__opencl_c_images";
      declarator.setInvalidType();
    } else if (Result->isOCLImage3dWOType() &&
               !OpenCLOptions.isSupported("cl_khr_3d_image_writes",
                                          S.getLangOpts())) {
      S.Diag(DS.getTypeSpecTypeLoc(), diag::err_opencl_requires_extension)
          << 0 << Result
          << (IsOpenCLC30Compatible
                  ? "cl_khr_3d_image_writes and __opencl_c_3d_image_writes"
                  : "cl_khr_3d_image_writes");
      declarator.setInvalidType();
    }
  }

  bool IsFixedPointType = DS.getTypeSpecType() == DeclSpec::TST_accum ||
                          DS.getTypeSpecType() == DeclSpec::TST_fract;

  // Only fixed point types can be saturated
  if (DS.isTypeSpecSat() && !IsFixedPointType)
    S.Diag(DS.getTypeSpecSatLoc(), diag::err_invalid_saturation_spec)
        << DS.getSpecifierName(DS.getTypeSpecType(),
                               Context.getPrintingPolicy());

  // Handle complex types.
  if (DS.getTypeSpecComplex() == DeclSpec::TSC_complex) {
    if (S.getLangOpts().Freestanding)
      S.Diag(DS.getTypeSpecComplexLoc(), diag::ext_freestanding_complex);
    Result = Context.getComplexType(Result);
  } else if (DS.isTypeAltiVecVector()) {
    unsigned typeSize = static_cast<unsigned>(Context.getTypeSize(Result));
    assert(typeSize > 0 && "type size for vector must be greater than 0 bits");
    VectorKind VecKind = VectorKind::AltiVecVector;
    if (DS.isTypeAltiVecPixel())
      VecKind = VectorKind::AltiVecPixel;
    else if (DS.isTypeAltiVecBool())
      VecKind = VectorKind::AltiVecBool;
    Result = Context.getVectorType(Result, 128/typeSize, VecKind);
  }

  // _Imaginary was a feature of C99 through C23 but was never supported in
  // Clang. The feature was removed in C2y, but we retain the unsupported
  // diagnostic for an improved user experience.
  if (DS.getTypeSpecComplex() == DeclSpec::TSC_imaginary)
    S.Diag(DS.getTypeSpecComplexLoc(), diag::err_imaginary_not_supported);

  // Before we process any type attributes, synthesize a block literal
  // function declarator if necessary.
  if (declarator.getContext() == DeclaratorContext::BlockLiteral)
    maybeSynthesizeBlockSignature(state, Result);

  // Apply any type attributes from the decl spec.  This may cause the
  // list of type attributes to be temporarily saved while the type
  // attributes are pushed around.
  // pipe attributes will be handled later ( at GetFullTypeForDeclarator )
  if (!DS.isTypeSpecPipe()) {
    // We also apply declaration attributes that "slide" to the decl spec.
    // Ordering can be important for attributes. The decalaration attributes
    // come syntactically before the decl spec attributes, so we process them
    // in that order.
    ParsedAttributesView SlidingAttrs;
    for (ParsedAttr &AL : declarator.getDeclarationAttributes()) {
      if (AL.slidesFromDeclToDeclSpecLegacyBehavior()) {
        SlidingAttrs.addAtEnd(&AL);

        // For standard syntax attributes, which would normally appertain to the
        // declaration here, suggest moving them to the type instead. But only
        // do this for our own vendor attributes; moving other vendors'
        // attributes might hurt portability.
        // There's one special case that we need to deal with here: The
        // `MatrixType` attribute may only be used in a typedef declaration. If
        // it's being used anywhere else, don't output the warning as
        // ProcessDeclAttributes() will output an error anyway.
        if (AL.isStandardAttributeSyntax() && AL.isClangScope() &&
            !(AL.getKind() == ParsedAttr::AT_MatrixType &&
              DS.getStorageClassSpec() != DeclSpec::SCS_typedef)) {
          S.Diag(AL.getLoc(), diag::warn_type_attribute_deprecated_on_decl)
              << AL;
        }
      }
    }
    // During this call to processTypeAttrs(),
    // TypeProcessingState::getCurrentAttributes() will erroneously return a
    // reference to the DeclSpec attributes, rather than the declaration
    // attributes. However, this doesn't matter, as getCurrentAttributes()
    // is only called when distributing attributes from one attribute list
    // to another. Declaration attributes are always C++11 attributes, and these
    // are never distributed.
    processTypeAttrs(state, Result, TAL_DeclSpec, SlidingAttrs);
    processTypeAttrs(state, Result, TAL_DeclSpec, DS.getAttributes());
  }

  // Apply const/volatile/restrict qualifiers to T.
  if (unsigned TypeQuals = DS.getTypeQualifiers()) {
    // Warn about CV qualifiers on function types.
    // C99 6.7.3p8:
    //   If the specification of a function type includes any type qualifiers,
    //   the behavior is undefined.
    // C++11 [dcl.fct]p7:
    //   The effect of a cv-qualifier-seq in a function declarator is not the
    //   same as adding cv-qualification on top of the function type. In the
    //   latter case, the cv-qualifiers are ignored.
    if (Result->isFunctionType()) {
      diagnoseAndRemoveTypeQualifiers(
          S, DS, TypeQuals, Result, DeclSpec::TQ_const | DeclSpec::TQ_volatile,
          S.getLangOpts().CPlusPlus
              ? diag::warn_typecheck_function_qualifiers_ignored
              : diag::warn_typecheck_function_qualifiers_unspecified);
      // No diagnostic for 'restrict' or '_Atomic' applied to a
      // function type; we'll diagnose those later, in BuildQualifiedType.
    }

    // C++11 [dcl.ref]p1:
    //   Cv-qualified references are ill-formed except when the
    //   cv-qualifiers are introduced through the use of a typedef-name
    //   or decltype-specifier, in which case the cv-qualifiers are ignored.
    //
    // There don't appear to be any other contexts in which a cv-qualified
    // reference type could be formed, so the 'ill-formed' clause here appears
    // to never happen.
    if (TypeQuals && Result->isReferenceType()) {
      diagnoseAndRemoveTypeQualifiers(
          S, DS, TypeQuals, Result,
          DeclSpec::TQ_const | DeclSpec::TQ_volatile | DeclSpec::TQ_atomic,
          diag::warn_typecheck_reference_qualifiers);
    }

    // C90 6.5.3 constraints: "The same type qualifier shall not appear more
    // than once in the same specifier-list or qualifier-list, either directly
    // or via one or more typedefs."
    if (!S.getLangOpts().C99 && !S.getLangOpts().CPlusPlus
        && TypeQuals & Result.getCVRQualifiers()) {
      if (TypeQuals & DeclSpec::TQ_const && Result.isConstQualified()) {
        S.Diag(DS.getConstSpecLoc(), diag::ext_duplicate_declspec)
          << "const";
      }

      if (TypeQuals & DeclSpec::TQ_volatile && Result.isVolatileQualified()) {
        S.Diag(DS.getVolatileSpecLoc(), diag::ext_duplicate_declspec)
          << "volatile";
      }

      // C90 doesn't have restrict nor _Atomic, so it doesn't force us to
      // produce a warning in this case.
    }

    QualType Qualified = S.BuildQualifiedType(Result, DeclLoc, TypeQuals, &DS);

    // If adding qualifiers fails, just use the unqualified type.
    if (Qualified.isNull())
      declarator.setInvalidType(true);
    else
      Result = Qualified;
  }

  assert(!Result.isNull() && "This function should not return a null type");
  return Result;
}

static std::string getPrintableNameForEntity(DeclarationName Entity) {
  if (Entity)
    return Entity.getAsString();

  return "type name";
}

static bool isDependentOrGNUAutoType(QualType T) {
  if (T->isDependentType())
    return true;

  const auto *AT = dyn_cast<AutoType>(T);
  return AT && AT->isGNUAutoType();
}

QualType Sema::BuildQualifiedType(QualType T, SourceLocation Loc,
                                  Qualifiers Qs, const DeclSpec *DS) {
  if (T.isNull())
    return QualType();

  // Ignore any attempt to form a cv-qualified reference.
  if (T->isReferenceType()) {
    Qs.removeConst();
    Qs.removeVolatile();
  }

  // Enforce C99 6.7.3p2: "Types other than pointer types derived from
  // object or incomplete types shall not be restrict-qualified."
  if (Qs.hasRestrict()) {
    unsigned DiagID = 0;
    QualType ProblemTy;

    if (T->isAnyPointerType() || T->isReferenceType() ||
        T->isMemberPointerType()) {
      QualType EltTy;
      if (T->isObjCObjectPointerType())
        EltTy = T;
      else if (const MemberPointerType *PTy = T->getAs<MemberPointerType>())
        EltTy = PTy->getPointeeType();
      else
        EltTy = T->getPointeeType();

      // If we have a pointer or reference, the pointee must have an object
      // incomplete type.
      if (!EltTy->isIncompleteOrObjectType()) {
        DiagID = diag::err_typecheck_invalid_restrict_invalid_pointee;
        ProblemTy = EltTy;
      }
    } else if (!isDependentOrGNUAutoType(T)) {
      // For an __auto_type variable, we may not have seen the initializer yet
      // and so have no idea whether the underlying type is a pointer type or
      // not.
      DiagID = diag::err_typecheck_invalid_restrict_not_pointer;
      ProblemTy = T;
    }

    if (DiagID) {
      Diag(DS ? DS->getRestrictSpecLoc() : Loc, DiagID) << ProblemTy;
      Qs.removeRestrict();
    }
  }

  return Context.getQualifiedType(T, Qs);
}

QualType Sema::BuildQualifiedType(QualType T, SourceLocation Loc,
                                  unsigned CVRAU, const DeclSpec *DS) {
  if (T.isNull())
    return QualType();

  // Ignore any attempt to form a cv-qualified reference.
  if (T->isReferenceType())
    CVRAU &=
        ~(DeclSpec::TQ_const | DeclSpec::TQ_volatile | DeclSpec::TQ_atomic);

  // Convert from DeclSpec::TQ to Qualifiers::TQ by just dropping TQ_atomic and
  // TQ_unaligned;
  unsigned CVR = CVRAU & ~(DeclSpec::TQ_atomic | DeclSpec::TQ_unaligned);

  // C11 6.7.3/5:
  //   If the same qualifier appears more than once in the same
  //   specifier-qualifier-list, either directly or via one or more typedefs,
  //   the behavior is the same as if it appeared only once.
  //
  // It's not specified what happens when the _Atomic qualifier is applied to
  // a type specified with the _Atomic specifier, but we assume that this
  // should be treated as if the _Atomic qualifier appeared multiple times.
  if (CVRAU & DeclSpec::TQ_atomic && !T->isAtomicType()) {
    // C11 6.7.3/5:
    //   If other qualifiers appear along with the _Atomic qualifier in a
    //   specifier-qualifier-list, the resulting type is the so-qualified
    //   atomic type.
    //
    // Don't need to worry about array types here, since _Atomic can't be
    // applied to such types.
    SplitQualType Split = T.getSplitUnqualifiedType();
    T = BuildAtomicType(QualType(Split.Ty, 0),
                        DS ? DS->getAtomicSpecLoc() : Loc);
    if (T.isNull())
      return T;
    Split.Quals.addCVRQualifiers(CVR);
    return BuildQualifiedType(T, Loc, Split.Quals);
  }

  Qualifiers Q = Qualifiers::fromCVRMask(CVR);
  Q.setUnaligned(CVRAU & DeclSpec::TQ_unaligned);
  return BuildQualifiedType(T, Loc, Q, DS);
}

QualType Sema::BuildParenType(QualType T) {
  return Context.getParenType(T);
}

/// Given that we're building a pointer or reference to the given
static QualType inferARCLifetimeForPointee(Sema &S, QualType type,
                                           SourceLocation loc,
                                           bool isReference) {
  // Bail out if retention is unrequired or already specified.
  if (!type->isObjCLifetimeType() ||
      type.getObjCLifetime() != Qualifiers::OCL_None)
    return type;

  Qualifiers::ObjCLifetime implicitLifetime = Qualifiers::OCL_None;

  // If the object type is const-qualified, we can safely use
  // __unsafe_unretained.  This is safe (because there are no read
  // barriers), and it'll be safe to coerce anything but __weak* to
  // the resulting type.
  if (type.isConstQualified()) {
    implicitLifetime = Qualifiers::OCL_ExplicitNone;

  // Otherwise, check whether the static type does not require
  // retaining.  This currently only triggers for Class (possibly
  // protocol-qualifed, and arrays thereof).
  } else if (type->isObjCARCImplicitlyUnretainedType()) {
    implicitLifetime = Qualifiers::OCL_ExplicitNone;

  // If we are in an unevaluated context, like sizeof, skip adding a
  // qualification.
  } else if (S.isUnevaluatedContext()) {
    return type;

  // If that failed, give an error and recover using __strong.  __strong
  // is the option most likely to prevent spurious second-order diagnostics,
  // like when binding a reference to a field.
  } else {
    // These types can show up in private ivars in system headers, so
    // we need this to not be an error in those cases.  Instead we
    // want to delay.
    if (S.DelayedDiagnostics.shouldDelayDiagnostics()) {
      S.DelayedDiagnostics.add(
          sema::DelayedDiagnostic::makeForbiddenType(loc,
              diag::err_arc_indirect_no_ownership, type, isReference));
    } else {
      S.Diag(loc, diag::err_arc_indirect_no_ownership) << type << isReference;
    }
    implicitLifetime = Qualifiers::OCL_Strong;
  }
  assert(implicitLifetime && "didn't infer any lifetime!");

  Qualifiers qs;
  qs.addObjCLifetime(implicitLifetime);
  return S.Context.getQualifiedType(type, qs);
}

static std::string getFunctionQualifiersAsString(const FunctionProtoType *FnTy){
  std::string Quals = FnTy->getMethodQuals().getAsString();

  switch (FnTy->getRefQualifier()) {
  case RQ_None:
    break;

  case RQ_LValue:
    if (!Quals.empty())
      Quals += ' ';
    Quals += '&';
    break;

  case RQ_RValue:
    if (!Quals.empty())
      Quals += ' ';
    Quals += "&&";
    break;
  }

  return Quals;
}

namespace {
/// Kinds of declarator that cannot contain a qualified function type.
///
/// C++98 [dcl.fct]p4 / C++11 [dcl.fct]p6:
///     a function type with a cv-qualifier or a ref-qualifier can only appear
///     at the topmost level of a type.
///
/// Parens and member pointers are permitted. We don't diagnose array and
/// function declarators, because they don't allow function types at all.
///
/// The values of this enum are used in diagnostics.
enum QualifiedFunctionKind { QFK_BlockPointer, QFK_Pointer, QFK_Reference };
} // end anonymous namespace

/// Check whether the type T is a qualified function type, and if it is,
/// diagnose that it cannot be contained within the given kind of declarator.
static bool checkQualifiedFunction(Sema &S, QualType T, SourceLocation Loc,
                                   QualifiedFunctionKind QFK) {
  // Does T refer to a function type with a cv-qualifier or a ref-qualifier?
  const FunctionProtoType *FPT = T->getAs<FunctionProtoType>();
  if (!FPT ||
      (FPT->getMethodQuals().empty() && FPT->getRefQualifier() == RQ_None))
    return false;

  S.Diag(Loc, diag::err_compound_qualified_function_type)
    << QFK << isa<FunctionType>(T.IgnoreParens()) << T
    << getFunctionQualifiersAsString(FPT);
  return true;
}

bool Sema::CheckQualifiedFunctionForTypeId(QualType T, SourceLocation Loc) {
  const FunctionProtoType *FPT = T->getAs<FunctionProtoType>();
  if (!FPT ||
      (FPT->getMethodQuals().empty() && FPT->getRefQualifier() == RQ_None))
    return false;

  Diag(Loc, diag::err_qualified_function_typeid)
      << T << getFunctionQualifiersAsString(FPT);
  return true;
}

// Helper to deduce addr space of a pointee type in OpenCL mode.
static QualType deduceOpenCLPointeeAddrSpace(Sema &S, QualType PointeeType) {
  if (!PointeeType->isUndeducedAutoType() && !PointeeType->isDependentType() &&
      !PointeeType->isSamplerT() &&
      !PointeeType.hasAddressSpace())
    PointeeType = S.getASTContext().getAddrSpaceQualType(
        PointeeType, S.getASTContext().getDefaultOpenCLPointeeAddrSpace());
  return PointeeType;
}

QualType Sema::BuildPointerType(QualType T,
                                SourceLocation Loc, DeclarationName Entity) {
  if (T->isReferenceType()) {
    // C++ 8.3.2p4: There shall be no ... pointers to references ...
    Diag(Loc, diag::err_illegal_decl_pointer_to_reference)
      << getPrintableNameForEntity(Entity) << T;
    return QualType();
  }

  if (T->isFunctionType() && getLangOpts().OpenCL &&
      !getOpenCLOptions().isAvailableOption("__cl_clang_function_pointers",
                                            getLangOpts())) {
    Diag(Loc, diag::err_opencl_function_pointer) << /*pointer*/ 0;
    return QualType();
  }

  if (getLangOpts().HLSL && Loc.isValid()) {
    Diag(Loc, diag::err_hlsl_pointers_unsupported) << 0;
    return QualType();
  }

  if (checkQualifiedFunction(*this, T, Loc, QFK_Pointer))
    return QualType();

  assert(!T->isObjCObjectType() && "Should build ObjCObjectPointerType");

  // In ARC, it is forbidden to build pointers to unqualified pointers.
  if (getLangOpts().ObjCAutoRefCount)
    T = inferARCLifetimeForPointee(*this, T, Loc, /*reference*/ false);

  if (getLangOpts().OpenCL)
    T = deduceOpenCLPointeeAddrSpace(*this, T);

  // In WebAssembly, pointers to reference types and pointers to tables are
  // illegal.
  if (getASTContext().getTargetInfo().getTriple().isWasm()) {
    if (T.isWebAssemblyReferenceType()) {
      Diag(Loc, diag::err_wasm_reference_pr) << 0;
      return QualType();
    }

    // We need to desugar the type here in case T is a ParenType.
    if (T->getUnqualifiedDesugaredType()->isWebAssemblyTableType()) {
      Diag(Loc, diag::err_wasm_table_pr) << 0;
      return QualType();
    }
  }

  // Build the pointer type.
  return Context.getPointerType(T);
}

QualType Sema::BuildReferenceType(QualType T, bool SpelledAsLValue,
                                  SourceLocation Loc,
                                  DeclarationName Entity) {
  assert(Context.getCanonicalType(T) != Context.OverloadTy &&
         "Unresolved overloaded function type");

  // C++0x [dcl.ref]p6:
  //   If a typedef (7.1.3), a type template-parameter (14.3.1), or a
  //   decltype-specifier (7.1.6.2) denotes a type TR that is a reference to a
  //   type T, an attempt to create the type "lvalue reference to cv TR" creates
  //   the type "lvalue reference to T", while an attempt to create the type
  //   "rvalue reference to cv TR" creates the type TR.
  bool LValueRef = SpelledAsLValue || T->getAs<LValueReferenceType>();

  // C++ [dcl.ref]p4: There shall be no references to references.
  //
  // According to C++ DR 106, references to references are only
  // diagnosed when they are written directly (e.g., "int & &"),
  // but not when they happen via a typedef:
  //
  //   typedef int& intref;
  //   typedef intref& intref2;
  //
  // Parser::ParseDeclaratorInternal diagnoses the case where
  // references are written directly; here, we handle the
  // collapsing of references-to-references as described in C++0x.
  // DR 106 and 540 introduce reference-collapsing into C++98/03.

  // C++ [dcl.ref]p1:
  //   A declarator that specifies the type "reference to cv void"
  //   is ill-formed.
  if (T->isVoidType()) {
    Diag(Loc, diag::err_reference_to_void);
    return QualType();
  }

  if (getLangOpts().HLSL && Loc.isValid()) {
    Diag(Loc, diag::err_hlsl_pointers_unsupported) << 1;
    return QualType();
  }

  if (checkQualifiedFunction(*this, T, Loc, QFK_Reference))
    return QualType();

  if (T->isFunctionType() && getLangOpts().OpenCL &&
      !getOpenCLOptions().isAvailableOption("__cl_clang_function_pointers",
                                            getLangOpts())) {
    Diag(Loc, diag::err_opencl_function_pointer) << /*reference*/ 1;
    return QualType();
  }

  // In ARC, it is forbidden to build references to unqualified pointers.
  if (getLangOpts().ObjCAutoRefCount)
    T = inferARCLifetimeForPointee(*this, T, Loc, /*reference*/ true);

  if (getLangOpts().OpenCL)
    T = deduceOpenCLPointeeAddrSpace(*this, T);

  // In WebAssembly, references to reference types and tables are illegal.
  if (getASTContext().getTargetInfo().getTriple().isWasm() &&
      T.isWebAssemblyReferenceType()) {
    Diag(Loc, diag::err_wasm_reference_pr) << 1;
    return QualType();
  }
  if (T->isWebAssemblyTableType()) {
    Diag(Loc, diag::err_wasm_table_pr) << 1;
    return QualType();
  }

  // Handle restrict on references.
  if (LValueRef)
    return Context.getLValueReferenceType(T, SpelledAsLValue);
  return Context.getRValueReferenceType(T);
}

QualType Sema::BuildReadPipeType(QualType T, SourceLocation Loc) {
  return Context.getReadPipeType(T);
}

QualType Sema::BuildWritePipeType(QualType T, SourceLocation Loc) {
  return Context.getWritePipeType(T);
}

QualType Sema::BuildBitIntType(bool IsUnsigned, Expr *BitWidth,
                               SourceLocation Loc) {
  if (BitWidth->isInstantiationDependent())
    return Context.getDependentBitIntType(IsUnsigned, BitWidth);

  llvm::APSInt Bits(32);
  ExprResult ICE =
      VerifyIntegerConstantExpression(BitWidth, &Bits, /*FIXME*/ AllowFold);

  if (ICE.isInvalid())
    return QualType();

  size_t NumBits = Bits.getZExtValue();
  if (!IsUnsigned && NumBits < 2) {
    Diag(Loc, diag::err_bit_int_bad_size) << 0;
    return QualType();
  }

  if (IsUnsigned && NumBits < 1) {
    Diag(Loc, diag::err_bit_int_bad_size) << 1;
    return QualType();
  }

  const TargetInfo &TI = getASTContext().getTargetInfo();
  if (NumBits > TI.getMaxBitIntWidth()) {
    Diag(Loc, diag::err_bit_int_max_size)
        << IsUnsigned << static_cast<uint64_t>(TI.getMaxBitIntWidth());
    return QualType();
  }

  return Context.getBitIntType(IsUnsigned, NumBits);
}

/// Check whether the specified array bound can be evaluated using the relevant
/// language rules. If so, returns the possibly-converted expression and sets
/// SizeVal to the size. If not, but the expression might be a VLA bound,
/// returns ExprResult(). Otherwise, produces a diagnostic and returns
/// ExprError().
static ExprResult checkArraySize(Sema &S, Expr *&ArraySize,
                                 llvm::APSInt &SizeVal, unsigned VLADiag,
                                 bool VLAIsError) {
  if (S.getLangOpts().CPlusPlus14 &&
      (VLAIsError ||
       !ArraySize->getType()->isIntegralOrUnscopedEnumerationType())) {
    // C++14 [dcl.array]p1:
    //   The constant-expression shall be a converted constant expression of
    //   type std::size_t.
    //
    // Don't apply this rule if we might be forming a VLA: in that case, we
    // allow non-constant expressions and constant-folding. We only need to use
    // the converted constant expression rules (to properly convert the source)
    // when the source expression is of class type.
    return S.CheckConvertedConstantExpression(
        ArraySize, S.Context.getSizeType(), SizeVal, Sema::CCEK_ArrayBound);
  }

  // If the size is an ICE, it certainly isn't a VLA. If we're in a GNU mode
  // (like gnu99, but not c99) accept any evaluatable value as an extension.
  class VLADiagnoser : public Sema::VerifyICEDiagnoser {
  public:
    unsigned VLADiag;
    bool VLAIsError;
    bool IsVLA = false;

    VLADiagnoser(unsigned VLADiag, bool VLAIsError)
        : VLADiag(VLADiag), VLAIsError(VLAIsError) {}

    Sema::SemaDiagnosticBuilder diagnoseNotICEType(Sema &S, SourceLocation Loc,
                                                   QualType T) override {
      return S.Diag(Loc, diag::err_array_size_non_int) << T;
    }

    Sema::SemaDiagnosticBuilder diagnoseNotICE(Sema &S,
                                               SourceLocation Loc) override {
      IsVLA = !VLAIsError;
      return S.Diag(Loc, VLADiag);
    }

    Sema::SemaDiagnosticBuilder diagnoseFold(Sema &S,
                                             SourceLocation Loc) override {
      return S.Diag(Loc, diag::ext_vla_folded_to_constant);
    }
  } Diagnoser(VLADiag, VLAIsError);

  ExprResult R =
      S.VerifyIntegerConstantExpression(ArraySize, &SizeVal, Diagnoser);
  if (Diagnoser.IsVLA)
    return ExprResult();
  return R;
}

bool Sema::checkArrayElementAlignment(QualType EltTy, SourceLocation Loc) {
  EltTy = Context.getBaseElementType(EltTy);
  if (EltTy->isIncompleteType() || EltTy->isDependentType() ||
      EltTy->isUndeducedType())
    return true;

  CharUnits Size = Context.getTypeSizeInChars(EltTy);
  CharUnits Alignment = Context.getTypeAlignInChars(EltTy);

  if (Size.isMultipleOf(Alignment))
    return true;

  Diag(Loc, diag::err_array_element_alignment)
      << EltTy << Size.getQuantity() << Alignment.getQuantity();
  return false;
}

QualType Sema::BuildArrayType(QualType T, ArraySizeModifier ASM,
                              Expr *ArraySize, unsigned Quals,
                              SourceRange Brackets, DeclarationName Entity) {

  SourceLocation Loc = Brackets.getBegin();
  if (getLangOpts().CPlusPlus) {
    // C++ [dcl.array]p1:
    //   T is called the array element type; this type shall not be a reference
    //   type, the (possibly cv-qualified) type void, a function type or an
    //   abstract class type.
    //
    // C++ [dcl.array]p3:
    //   When several "array of" specifications are adjacent, [...] only the
    //   first of the constant expressions that specify the bounds of the arrays
    //   may be omitted.
    //
    // Note: function types are handled in the common path with C.
    if (T->isReferenceType()) {
      Diag(Loc, diag::err_illegal_decl_array_of_references)
      << getPrintableNameForEntity(Entity) << T;
      return QualType();
    }

    if (T->isVoidType() || T->isIncompleteArrayType()) {
      Diag(Loc, diag::err_array_incomplete_or_sizeless_type) << 0 << T;
      return QualType();
    }

    if (RequireNonAbstractType(Brackets.getBegin(), T,
                               diag::err_array_of_abstract_type))
      return QualType();

    // Mentioning a member pointer type for an array type causes us to lock in
    // an inheritance model, even if it's inside an unused typedef.
    if (Context.getTargetInfo().getCXXABI().isMicrosoft())
      if (const MemberPointerType *MPTy = T->getAs<MemberPointerType>())
        if (!MPTy->getClass()->isDependentType())
          (void)isCompleteType(Loc, T);

  } else {
    // C99 6.7.5.2p1: If the element type is an incomplete or function type,
    // reject it (e.g. void ary[7], struct foo ary[7], void ary[7]())
    if (!T.isWebAssemblyReferenceType() &&
        RequireCompleteSizedType(Loc, T,
                                 diag::err_array_incomplete_or_sizeless_type))
      return QualType();
  }

  // Multi-dimensional arrays of WebAssembly references are not allowed.
  if (Context.getTargetInfo().getTriple().isWasm() && T->isArrayType()) {
    const auto *ATy = dyn_cast<ArrayType>(T);
    if (ATy && ATy->getElementType().isWebAssemblyReferenceType()) {
      Diag(Loc, diag::err_wasm_reftype_multidimensional_array);
      return QualType();
    }
  }

  if (T->isSizelessType() && !T.isWebAssemblyReferenceType()) {
    Diag(Loc, diag::err_array_incomplete_or_sizeless_type) << 1 << T;
    return QualType();
  }

  if (T->isFunctionType()) {
    Diag(Loc, diag::err_illegal_decl_array_of_functions)
      << getPrintableNameForEntity(Entity) << T;
    return QualType();
  }

  if (const RecordType *EltTy = T->getAs<RecordType>()) {
    // If the element type is a struct or union that contains a variadic
    // array, accept it as a GNU extension: C99 6.7.2.1p2.
    if (EltTy->getDecl()->hasFlexibleArrayMember())
      Diag(Loc, diag::ext_flexible_array_in_array) << T;
  } else if (T->isObjCObjectType()) {
    Diag(Loc, diag::err_objc_array_of_interfaces) << T;
    return QualType();
  }

  if (!checkArrayElementAlignment(T, Loc))
    return QualType();

  // Do placeholder conversions on the array size expression.
  if (ArraySize && ArraySize->hasPlaceholderType()) {
    ExprResult Result = CheckPlaceholderExpr(ArraySize);
    if (Result.isInvalid()) return QualType();
    ArraySize = Result.get();
  }

  // Do lvalue-to-rvalue conversions on the array size expression.
  if (ArraySize && !ArraySize->isPRValue()) {
    ExprResult Result = DefaultLvalueConversion(ArraySize);
    if (Result.isInvalid())
      return QualType();

    ArraySize = Result.get();
  }

  // C99 6.7.5.2p1: The size expression shall have integer type.
  // C++11 allows contextual conversions to such types.
  if (!getLangOpts().CPlusPlus11 &&
      ArraySize && !ArraySize->isTypeDependent() &&
      !ArraySize->getType()->isIntegralOrUnscopedEnumerationType()) {
    Diag(ArraySize->getBeginLoc(), diag::err_array_size_non_int)
        << ArraySize->getType() << ArraySize->getSourceRange();
    return QualType();
  }

  auto IsStaticAssertLike = [](const Expr *ArraySize, ASTContext &Context) {
    if (!ArraySize)
      return false;

    // If the array size expression is a conditional expression whose branches
    // are both integer constant expressions, one negative and one positive,
    // then it's assumed to be like an old-style static assertion. e.g.,
    //   int old_style_assert[expr ? 1 : -1];
    // We will accept any integer constant expressions instead of assuming the
    // values 1 and -1 are always used.
    if (const auto *CondExpr = dyn_cast_if_present<ConditionalOperator>(
            ArraySize->IgnoreParenImpCasts())) {
      std::optional<llvm::APSInt> LHS =
          CondExpr->getLHS()->getIntegerConstantExpr(Context);
      std::optional<llvm::APSInt> RHS =
          CondExpr->getRHS()->getIntegerConstantExpr(Context);
      return LHS && RHS && LHS->isNegative() != RHS->isNegative();
    }
    return false;
  };

  // VLAs always produce at least a -Wvla diagnostic, sometimes an error.
  unsigned VLADiag;
  bool VLAIsError;
  if (getLangOpts().OpenCL) {
    // OpenCL v1.2 s6.9.d: variable length arrays are not supported.
    VLADiag = diag::err_opencl_vla;
    VLAIsError = true;
  } else if (getLangOpts().C99) {
    VLADiag = diag::warn_vla_used;
    VLAIsError = false;
  } else if (isSFINAEContext()) {
    VLADiag = diag::err_vla_in_sfinae;
    VLAIsError = true;
  } else if (getLangOpts().OpenMP && OpenMP().isInOpenMPTaskUntiedContext()) {
    VLADiag = diag::err_openmp_vla_in_task_untied;
    VLAIsError = true;
  } else if (getLangOpts().CPlusPlus) {
    if (getLangOpts().CPlusPlus11 && IsStaticAssertLike(ArraySize, Context))
      VLADiag = getLangOpts().GNUMode
                    ? diag::ext_vla_cxx_in_gnu_mode_static_assert
                    : diag::ext_vla_cxx_static_assert;
    else
      VLADiag = getLangOpts().GNUMode ? diag::ext_vla_cxx_in_gnu_mode
                                      : diag::ext_vla_cxx;
    VLAIsError = false;
  } else {
    VLADiag = diag::ext_vla;
    VLAIsError = false;
  }

  llvm::APSInt ConstVal(Context.getTypeSize(Context.getSizeType()));
  if (!ArraySize) {
    if (ASM == ArraySizeModifier::Star) {
      Diag(Loc, VLADiag);
      if (VLAIsError)
        return QualType();

      T = Context.getVariableArrayType(T, nullptr, ASM, Quals, Brackets);
    } else {
      T = Context.getIncompleteArrayType(T, ASM, Quals);
    }
  } else if (ArraySize->isTypeDependent() || ArraySize->isValueDependent()) {
    T = Context.getDependentSizedArrayType(T, ArraySize, ASM, Quals, Brackets);
  } else {
    ExprResult R =
        checkArraySize(*this, ArraySize, ConstVal, VLADiag, VLAIsError);
    if (R.isInvalid())
      return QualType();

    if (!R.isUsable()) {
      // C99: an array with a non-ICE size is a VLA. We accept any expression
      // that we can fold to a non-zero positive value as a non-VLA as an
      // extension.
      T = Context.getVariableArrayType(T, ArraySize, ASM, Quals, Brackets);
    } else if (!T->isDependentType() && !T->isIncompleteType() &&
               !T->isConstantSizeType()) {
      // C99: an array with an element type that has a non-constant-size is a
      // VLA.
      // FIXME: Add a note to explain why this isn't a VLA.
      Diag(Loc, VLADiag);
      if (VLAIsError)
        return QualType();
      T = Context.getVariableArrayType(T, ArraySize, ASM, Quals, Brackets);
    } else {
      // C99 6.7.5.2p1: If the expression is a constant expression, it shall
      // have a value greater than zero.
      // In C++, this follows from narrowing conversions being disallowed.
      if (ConstVal.isSigned() && ConstVal.isNegative()) {
        if (Entity)
          Diag(ArraySize->getBeginLoc(), diag::err_decl_negative_array_size)
              << getPrintableNameForEntity(Entity)
              << ArraySize->getSourceRange();
        else
          Diag(ArraySize->getBeginLoc(),
               diag::err_typecheck_negative_array_size)
              << ArraySize->getSourceRange();
        return QualType();
      }
      if (ConstVal == 0 && !T.isWebAssemblyReferenceType()) {
        // GCC accepts zero sized static arrays. We allow them when
        // we're not in a SFINAE context.
        Diag(ArraySize->getBeginLoc(),
             isSFINAEContext() ? diag::err_typecheck_zero_array_size
                               : diag::ext_typecheck_zero_array_size)
            << 0 << ArraySize->getSourceRange();
      }

      // Is the array too large?
      unsigned ActiveSizeBits =
          (!T->isDependentType() && !T->isVariablyModifiedType() &&
           !T->isIncompleteType() && !T->isUndeducedType())
              ? ConstantArrayType::getNumAddressingBits(Context, T, ConstVal)
              : ConstVal.getActiveBits();
      if (ActiveSizeBits > ConstantArrayType::getMaxSizeBits(Context)) {
        Diag(ArraySize->getBeginLoc(), diag::err_array_too_large)
            << toString(ConstVal, 10) << ArraySize->getSourceRange();
        return QualType();
      }

      T = Context.getConstantArrayType(T, ConstVal, ArraySize, ASM, Quals);
    }
  }

  if (T->isVariableArrayType()) {
    if (!Context.getTargetInfo().isVLASupported()) {
      // CUDA device code and some other targets don't support VLAs.
      bool IsCUDADevice = (getLangOpts().CUDA && getLangOpts().CUDAIsDevice);
      targetDiag(Loc,
                 IsCUDADevice ? diag::err_cuda_vla : diag::err_vla_unsupported)
          << (IsCUDADevice ? llvm::to_underlying(CUDA().CurrentTarget()) : 0);
    } else if (sema::FunctionScopeInfo *FSI = getCurFunction()) {
      // VLAs are supported on this target, but we may need to do delayed
      // checking that the VLA is not being used within a coroutine.
      FSI->setHasVLA(Loc);
    }
  }

  // If this is not C99, diagnose array size modifiers on non-VLAs.
  if (!getLangOpts().C99 && !T->isVariableArrayType() &&
      (ASM != ArraySizeModifier::Normal || Quals != 0)) {
    Diag(Loc, getLangOpts().CPlusPlus ? diag::err_c99_array_usage_cxx
                                      : diag::ext_c99_array_usage)
        << llvm::to_underlying(ASM);
  }

  // OpenCL v2.0 s6.12.5 - Arrays of blocks are not supported.
  // OpenCL v2.0 s6.16.13.1 - Arrays of pipe type are not supported.
  // OpenCL v2.0 s6.9.b - Arrays of image/sampler type are not supported.
  if (getLangOpts().OpenCL) {
    const QualType ArrType = Context.getBaseElementType(T);
    if (ArrType->isBlockPointerType() || ArrType->isPipeType() ||
        ArrType->isSamplerT() || ArrType->isImageType()) {
      Diag(Loc, diag::err_opencl_invalid_type_array) << ArrType;
      return QualType();
    }
  }

  return T;
}

QualType Sema::BuildVectorType(QualType CurType, Expr *SizeExpr,
                               SourceLocation AttrLoc) {
  // The base type must be integer (not Boolean or enumeration) or float, and
  // can't already be a vector.
  if ((!CurType->isDependentType() &&
       (!CurType->isBuiltinType() || CurType->isBooleanType() ||
        (!CurType->isIntegerType() && !CurType->isRealFloatingType())) &&
       !CurType->isBitIntType()) ||
      CurType->isArrayType()) {
    Diag(AttrLoc, diag::err_attribute_invalid_vector_type) << CurType;
    return QualType();
  }
  // Only support _BitInt elements with byte-sized power of 2 NumBits.
  if (const auto *BIT = CurType->getAs<BitIntType>()) {
    unsigned NumBits = BIT->getNumBits();
    if (!llvm::isPowerOf2_32(NumBits) || NumBits < 8) {
      Diag(AttrLoc, diag::err_attribute_invalid_bitint_vector_type)
          << (NumBits < 8);
      return QualType();
    }
  }

  if (SizeExpr->isTypeDependent() || SizeExpr->isValueDependent())
    return Context.getDependentVectorType(CurType, SizeExpr, AttrLoc,
                                          VectorKind::Generic);

  std::optional<llvm::APSInt> VecSize =
      SizeExpr->getIntegerConstantExpr(Context);
  if (!VecSize) {
    Diag(AttrLoc, diag::err_attribute_argument_type)
        << "vector_size" << AANT_ArgumentIntegerConstant
        << SizeExpr->getSourceRange();
    return QualType();
  }

  if (CurType->isDependentType())
    return Context.getDependentVectorType(CurType, SizeExpr, AttrLoc,
                                          VectorKind::Generic);

  // vecSize is specified in bytes - convert to bits.
  if (!VecSize->isIntN(61)) {
    // Bit size will overflow uint64.
    Diag(AttrLoc, diag::err_attribute_size_too_large)
        << SizeExpr->getSourceRange() << "vector";
    return QualType();
  }
  uint64_t VectorSizeBits = VecSize->getZExtValue() * 8;
  unsigned TypeSize = static_cast<unsigned>(Context.getTypeSize(CurType));

  if (VectorSizeBits == 0) {
    Diag(AttrLoc, diag::err_attribute_zero_size)
        << SizeExpr->getSourceRange() << "vector";
    return QualType();
  }

  if (!TypeSize || VectorSizeBits % TypeSize) {
    Diag(AttrLoc, diag::err_attribute_invalid_size)
        << SizeExpr->getSourceRange();
    return QualType();
  }

  if (VectorSizeBits / TypeSize > std::numeric_limits<uint32_t>::max()) {
    Diag(AttrLoc, diag::err_attribute_size_too_large)
        << SizeExpr->getSourceRange() << "vector";
    return QualType();
  }

  return Context.getVectorType(CurType, VectorSizeBits / TypeSize,
                               VectorKind::Generic);
}

QualType Sema::BuildExtVectorType(QualType T, Expr *ArraySize,
                                  SourceLocation AttrLoc) {
  // Unlike gcc's vector_size attribute, we do not allow vectors to be defined
  // in conjunction with complex types (pointers, arrays, functions, etc.).
  //
  // Additionally, OpenCL prohibits vectors of booleans (they're considered a
  // reserved data type under OpenCL v2.0 s6.1.4), we don't support selects
  // on bitvectors, and we have no well-defined ABI for bitvectors, so vectors
  // of bool aren't allowed.
  //
  // We explicitly allow bool elements in ext_vector_type for C/C++.
  bool IsNoBoolVecLang = getLangOpts().OpenCL || getLangOpts().OpenCLCPlusPlus;
  if ((!T->isDependentType() && !T->isIntegerType() &&
       !T->isRealFloatingType()) ||
      (IsNoBoolVecLang && T->isBooleanType())) {
    Diag(AttrLoc, diag::err_attribute_invalid_vector_type) << T;
    return QualType();
  }

  // Only support _BitInt elements with byte-sized power of 2 NumBits.
  if (T->isBitIntType()) {
    unsigned NumBits = T->castAs<BitIntType>()->getNumBits();
    if (!llvm::isPowerOf2_32(NumBits) || NumBits < 8) {
      Diag(AttrLoc, diag::err_attribute_invalid_bitint_vector_type)
          << (NumBits < 8);
      return QualType();
    }
  }

  if (!ArraySize->isTypeDependent() && !ArraySize->isValueDependent()) {
    std::optional<llvm::APSInt> vecSize =
        ArraySize->getIntegerConstantExpr(Context);
    if (!vecSize) {
      Diag(AttrLoc, diag::err_attribute_argument_type)
        << "ext_vector_type" << AANT_ArgumentIntegerConstant
        << ArraySize->getSourceRange();
      return QualType();
    }

    if (!vecSize->isIntN(32)) {
      Diag(AttrLoc, diag::err_attribute_size_too_large)
          << ArraySize->getSourceRange() << "vector";
      return QualType();
    }
    // Unlike gcc's vector_size attribute, the size is specified as the
    // number of elements, not the number of bytes.
    unsigned vectorSize = static_cast<unsigned>(vecSize->getZExtValue());

    if (vectorSize == 0) {
      Diag(AttrLoc, diag::err_attribute_zero_size)
          << ArraySize->getSourceRange() << "vector";
      return QualType();
    }

    return Context.getExtVectorType(T, vectorSize);
  }

  return Context.getDependentSizedExtVectorType(T, ArraySize, AttrLoc);
}

QualType Sema::BuildMatrixType(QualType ElementTy, Expr *NumRows, Expr *NumCols,
                               SourceLocation AttrLoc) {
  assert(Context.getLangOpts().MatrixTypes &&
         "Should never build a matrix type when it is disabled");

  // Check element type, if it is not dependent.
  if (!ElementTy->isDependentType() &&
      !MatrixType::isValidElementType(ElementTy)) {
    Diag(AttrLoc, diag::err_attribute_invalid_matrix_type) << ElementTy;
    return QualType();
  }

  if (NumRows->isTypeDependent() || NumCols->isTypeDependent() ||
      NumRows->isValueDependent() || NumCols->isValueDependent())
    return Context.getDependentSizedMatrixType(ElementTy, NumRows, NumCols,
                                               AttrLoc);

  std::optional<llvm::APSInt> ValueRows =
      NumRows->getIntegerConstantExpr(Context);
  std::optional<llvm::APSInt> ValueColumns =
      NumCols->getIntegerConstantExpr(Context);

  auto const RowRange = NumRows->getSourceRange();
  auto const ColRange = NumCols->getSourceRange();

  // Both are row and column expressions are invalid.
  if (!ValueRows && !ValueColumns) {
    Diag(AttrLoc, diag::err_attribute_argument_type)
        << "matrix_type" << AANT_ArgumentIntegerConstant << RowRange
        << ColRange;
    return QualType();
  }

  // Only the row expression is invalid.
  if (!ValueRows) {
    Diag(AttrLoc, diag::err_attribute_argument_type)
        << "matrix_type" << AANT_ArgumentIntegerConstant << RowRange;
    return QualType();
  }

  // Only the column expression is invalid.
  if (!ValueColumns) {
    Diag(AttrLoc, diag::err_attribute_argument_type)
        << "matrix_type" << AANT_ArgumentIntegerConstant << ColRange;
    return QualType();
  }

  // Check the matrix dimensions.
  unsigned MatrixRows = static_cast<unsigned>(ValueRows->getZExtValue());
  unsigned MatrixColumns = static_cast<unsigned>(ValueColumns->getZExtValue());
  if (MatrixRows == 0 && MatrixColumns == 0) {
    Diag(AttrLoc, diag::err_attribute_zero_size)
        << "matrix" << RowRange << ColRange;
    return QualType();
  }
  if (MatrixRows == 0) {
    Diag(AttrLoc, diag::err_attribute_zero_size) << "matrix" << RowRange;
    return QualType();
  }
  if (MatrixColumns == 0) {
    Diag(AttrLoc, diag::err_attribute_zero_size) << "matrix" << ColRange;
    return QualType();
  }
  if (!ConstantMatrixType::isDimensionValid(MatrixRows)) {
    Diag(AttrLoc, diag::err_attribute_size_too_large)
        << RowRange << "matrix row";
    return QualType();
  }
  if (!ConstantMatrixType::isDimensionValid(MatrixColumns)) {
    Diag(AttrLoc, diag::err_attribute_size_too_large)
        << ColRange << "matrix column";
    return QualType();
  }
  return Context.getConstantMatrixType(ElementTy, MatrixRows, MatrixColumns);
}

bool Sema::CheckFunctionReturnType(QualType T, SourceLocation Loc) {
  if (T->isArrayType() || T->isFunctionType()) {
    Diag(Loc, diag::err_func_returning_array_function)
      << T->isFunctionType() << T;
    return true;
  }

  // Functions cannot return half FP.
  if (T->isHalfType() && !getLangOpts().NativeHalfArgsAndReturns &&
      !Context.getTargetInfo().allowHalfArgsAndReturns()) {
    Diag(Loc, diag::err_parameters_retval_cannot_have_fp16_type) << 1 <<
      FixItHint::CreateInsertion(Loc, "*");
    return true;
  }

  // Methods cannot return interface types. All ObjC objects are
  // passed by reference.
  if (T->isObjCObjectType()) {
    Diag(Loc, diag::err_object_cannot_be_passed_returned_by_value)
        << 0 << T << FixItHint::CreateInsertion(Loc, "*");
    return true;
  }

  if (T.hasNonTrivialToPrimitiveDestructCUnion() ||
      T.hasNonTrivialToPrimitiveCopyCUnion())
    checkNonTrivialCUnion(T, Loc, NTCUC_FunctionReturn,
                          NTCUK_Destruct|NTCUK_Copy);

  // C++2a [dcl.fct]p12:
  //   A volatile-qualified return type is deprecated
  if (T.isVolatileQualified() && getLangOpts().CPlusPlus20)
    Diag(Loc, diag::warn_deprecated_volatile_return) << T;

  if (T.getAddressSpace() != LangAS::Default && getLangOpts().HLSL)
    return true;
  return false;
}

/// Check the extended parameter information.  Most of the necessary
/// checking should occur when applying the parameter attribute; the
/// only other checks required are positional restrictions.
static void checkExtParameterInfos(Sema &S, ArrayRef<QualType> paramTypes,
                    const FunctionProtoType::ExtProtoInfo &EPI,
                    llvm::function_ref<SourceLocation(unsigned)> getParamLoc) {
  assert(EPI.ExtParameterInfos && "shouldn't get here without param infos");

  bool emittedError = false;
  auto actualCC = EPI.ExtInfo.getCC();
  enum class RequiredCC { OnlySwift, SwiftOrSwiftAsync };
  auto checkCompatible = [&](unsigned paramIndex, RequiredCC required) {
    bool isCompatible =
        (required == RequiredCC::OnlySwift)
            ? (actualCC == CC_Swift)
            : (actualCC == CC_Swift || actualCC == CC_SwiftAsync);
    if (isCompatible || emittedError)
      return;
    S.Diag(getParamLoc(paramIndex), diag::err_swift_param_attr_not_swiftcall)
        << getParameterABISpelling(EPI.ExtParameterInfos[paramIndex].getABI())
        << (required == RequiredCC::OnlySwift);
    emittedError = true;
  };
  for (size_t paramIndex = 0, numParams = paramTypes.size();
          paramIndex != numParams; ++paramIndex) {
    switch (EPI.ExtParameterInfos[paramIndex].getABI()) {
    // Nothing interesting to check for orindary-ABI parameters.
    case ParameterABI::Ordinary:
      continue;

    // swift_indirect_result parameters must be a prefix of the function
    // arguments.
    case ParameterABI::SwiftIndirectResult:
      checkCompatible(paramIndex, RequiredCC::SwiftOrSwiftAsync);
      if (paramIndex != 0 &&
          EPI.ExtParameterInfos[paramIndex - 1].getABI()
            != ParameterABI::SwiftIndirectResult) {
        S.Diag(getParamLoc(paramIndex),
               diag::err_swift_indirect_result_not_first);
      }
      continue;

    case ParameterABI::SwiftContext:
      checkCompatible(paramIndex, RequiredCC::SwiftOrSwiftAsync);
      continue;

    // SwiftAsyncContext is not limited to swiftasynccall functions.
    case ParameterABI::SwiftAsyncContext:
      continue;

    // swift_error parameters must be preceded by a swift_context parameter.
    case ParameterABI::SwiftErrorResult:
      checkCompatible(paramIndex, RequiredCC::OnlySwift);
      if (paramIndex == 0 ||
          EPI.ExtParameterInfos[paramIndex - 1].getABI() !=
              ParameterABI::SwiftContext) {
        S.Diag(getParamLoc(paramIndex),
               diag::err_swift_error_result_not_after_swift_context);
      }
      continue;
    }
    llvm_unreachable("bad ABI kind");
  }
}

QualType Sema::BuildFunctionType(QualType T,
                                 MutableArrayRef<QualType> ParamTypes,
                                 SourceLocation Loc, DeclarationName Entity,
                                 const FunctionProtoType::ExtProtoInfo &EPI) {
  bool Invalid = false;

  Invalid |= CheckFunctionReturnType(T, Loc);

  for (unsigned Idx = 0, Cnt = ParamTypes.size(); Idx < Cnt; ++Idx) {
    // FIXME: Loc is too inprecise here, should use proper locations for args.
    QualType ParamType = Context.getAdjustedParameterType(ParamTypes[Idx]);
    if (ParamType->isVoidType()) {
      Diag(Loc, diag::err_param_with_void_type);
      Invalid = true;
    } else if (ParamType->isHalfType() && !getLangOpts().NativeHalfArgsAndReturns &&
               !Context.getTargetInfo().allowHalfArgsAndReturns()) {
      // Disallow half FP arguments.
      Diag(Loc, diag::err_parameters_retval_cannot_have_fp16_type) << 0 <<
        FixItHint::CreateInsertion(Loc, "*");
      Invalid = true;
    } else if (ParamType->isWebAssemblyTableType()) {
      Diag(Loc, diag::err_wasm_table_as_function_parameter);
      Invalid = true;
    }

    // C++2a [dcl.fct]p4:
    //   A parameter with volatile-qualified type is deprecated
    if (ParamType.isVolatileQualified() && getLangOpts().CPlusPlus20)
      Diag(Loc, diag::warn_deprecated_volatile_param) << ParamType;

    ParamTypes[Idx] = ParamType;
  }

  if (EPI.ExtParameterInfos) {
    checkExtParameterInfos(*this, ParamTypes, EPI,
                           [=](unsigned i) { return Loc; });
  }

  if (EPI.ExtInfo.getProducesResult()) {
    // This is just a warning, so we can't fail to build if we see it.
    ObjC().checkNSReturnsRetainedReturnType(Loc, T);
  }

  if (Invalid)
    return QualType();

  return Context.getFunctionType(T, ParamTypes, EPI);
}

QualType Sema::BuildMemberPointerType(QualType T, QualType Class,
                                      SourceLocation Loc,
                                      DeclarationName Entity) {
  // Verify that we're not building a pointer to pointer to function with
  // exception specification.
  if (CheckDistantExceptionSpec(T)) {
    Diag(Loc, diag::err_distant_exception_spec);
    return QualType();
  }

  // C++ 8.3.3p3: A pointer to member shall not point to ... a member
  //   with reference type, or "cv void."
  if (T->isReferenceType()) {
    Diag(Loc, diag::err_illegal_decl_mempointer_to_reference)
      << getPrintableNameForEntity(Entity) << T;
    return QualType();
  }

  if (T->isVoidType()) {
    Diag(Loc, diag::err_illegal_decl_mempointer_to_void)
      << getPrintableNameForEntity(Entity);
    return QualType();
  }

  if (!Class->isDependentType() && !Class->isRecordType()) {
    Diag(Loc, diag::err_mempointer_in_nonclass_type) << Class;
    return QualType();
  }

  if (T->isFunctionType() && getLangOpts().OpenCL &&
      !getOpenCLOptions().isAvailableOption("__cl_clang_function_pointers",
                                            getLangOpts())) {
    Diag(Loc, diag::err_opencl_function_pointer) << /*pointer*/ 0;
    return QualType();
  }

  if (getLangOpts().HLSL && Loc.isValid()) {
    Diag(Loc, diag::err_hlsl_pointers_unsupported) << 0;
    return QualType();
  }

  // Adjust the default free function calling convention to the default method
  // calling convention.
  bool IsCtorOrDtor =
      (Entity.getNameKind() == DeclarationName::CXXConstructorName) ||
      (Entity.getNameKind() == DeclarationName::CXXDestructorName);
  if (T->isFunctionType())
    adjustMemberFunctionCC(T, /*HasThisPointer=*/true, IsCtorOrDtor, Loc);

  return Context.getMemberPointerType(T, Class.getTypePtr());
}

QualType Sema::BuildBlockPointerType(QualType T,
                                     SourceLocation Loc,
                                     DeclarationName Entity) {
  if (!T->isFunctionType()) {
    Diag(Loc, diag::err_nonfunction_block_type);
    return QualType();
  }

  if (checkQualifiedFunction(*this, T, Loc, QFK_BlockPointer))
    return QualType();

  if (getLangOpts().OpenCL)
    T = deduceOpenCLPointeeAddrSpace(*this, T);

  return Context.getBlockPointerType(T);
}

QualType Sema::GetTypeFromParser(ParsedType Ty, TypeSourceInfo **TInfo) {
  QualType QT = Ty.get();
  if (QT.isNull()) {
    if (TInfo) *TInfo = nullptr;
    return QualType();
  }

  TypeSourceInfo *DI = nullptr;
  if (const LocInfoType *LIT = dyn_cast<LocInfoType>(QT)) {
    QT = LIT->getType();
    DI = LIT->getTypeSourceInfo();
  }

  if (TInfo) *TInfo = DI;
  return QT;
}

static void transferARCOwnershipToDeclaratorChunk(TypeProcessingState &state,
                                            Qualifiers::ObjCLifetime ownership,
                                            unsigned chunkIndex);

/// Given that this is the declaration of a parameter under ARC,
/// attempt to infer attributes and such for pointer-to-whatever
/// types.
static void inferARCWriteback(TypeProcessingState &state,
                              QualType &declSpecType) {
  Sema &S = state.getSema();
  Declarator &declarator = state.getDeclarator();

  // TODO: should we care about decl qualifiers?

  // Check whether the declarator has the expected form.  We walk
  // from the inside out in order to make the block logic work.
  unsigned outermostPointerIndex = 0;
  bool isBlockPointer = false;
  unsigned numPointers = 0;
  for (unsigned i = 0, e = declarator.getNumTypeObjects(); i != e; ++i) {
    unsigned chunkIndex = i;
    DeclaratorChunk &chunk = declarator.getTypeObject(chunkIndex);
    switch (chunk.Kind) {
    case DeclaratorChunk::Paren:
      // Ignore parens.
      break;

    case DeclaratorChunk::Reference:
    case DeclaratorChunk::Pointer:
      // Count the number of pointers.  Treat references
      // interchangeably as pointers; if they're mis-ordered, normal
      // type building will discover that.
      outermostPointerIndex = chunkIndex;
      numPointers++;
      break;

    case DeclaratorChunk::BlockPointer:
      // If we have a pointer to block pointer, that's an acceptable
      // indirect reference; anything else is not an application of
      // the rules.
      if (numPointers != 1) return;
      numPointers++;
      outermostPointerIndex = chunkIndex;
      isBlockPointer = true;

      // We don't care about pointer structure in return values here.
      goto done;

    case DeclaratorChunk::Array: // suppress if written (id[])?
    case DeclaratorChunk::Function:
    case DeclaratorChunk::MemberPointer:
    case DeclaratorChunk::Pipe:
      return;
    }
  }
 done:

  // If we have *one* pointer, then we want to throw the qualifier on
  // the declaration-specifiers, which means that it needs to be a
  // retainable object type.
  if (numPointers == 1) {
    // If it's not a retainable object type, the rule doesn't apply.
    if (!declSpecType->isObjCRetainableType()) return;

    // If it already has lifetime, don't do anything.
    if (declSpecType.getObjCLifetime()) return;

    // Otherwise, modify the type in-place.
    Qualifiers qs;

    if (declSpecType->isObjCARCImplicitlyUnretainedType())
      qs.addObjCLifetime(Qualifiers::OCL_ExplicitNone);
    else
      qs.addObjCLifetime(Qualifiers::OCL_Autoreleasing);
    declSpecType = S.Context.getQualifiedType(declSpecType, qs);

  // If we have *two* pointers, then we want to throw the qualifier on
  // the outermost pointer.
  } else if (numPointers == 2) {
    // If we don't have a block pointer, we need to check whether the
    // declaration-specifiers gave us something that will turn into a
    // retainable object pointer after we slap the first pointer on it.
    if (!isBlockPointer && !declSpecType->isObjCObjectType())
      return;

    // Look for an explicit lifetime attribute there.
    DeclaratorChunk &chunk = declarator.getTypeObject(outermostPointerIndex);
    if (chunk.Kind != DeclaratorChunk::Pointer &&
        chunk.Kind != DeclaratorChunk::BlockPointer)
      return;
    for (const ParsedAttr &AL : chunk.getAttrs())
      if (AL.getKind() == ParsedAttr::AT_ObjCOwnership)
        return;

    transferARCOwnershipToDeclaratorChunk(state, Qualifiers::OCL_Autoreleasing,
                                          outermostPointerIndex);

  // Any other number of pointers/references does not trigger the rule.
  } else return;

  // TODO: mark whether we did this inference?
}

void Sema::diagnoseIgnoredQualifiers(unsigned DiagID, unsigned Quals,
                                     SourceLocation FallbackLoc,
                                     SourceLocation ConstQualLoc,
                                     SourceLocation VolatileQualLoc,
                                     SourceLocation RestrictQualLoc,
                                     SourceLocation AtomicQualLoc,
                                     SourceLocation UnalignedQualLoc) {
  if (!Quals)
    return;

  struct Qual {
    const char *Name;
    unsigned Mask;
    SourceLocation Loc;
  } const QualKinds[5] = {
    { "const", DeclSpec::TQ_const, ConstQualLoc },
    { "volatile", DeclSpec::TQ_volatile, VolatileQualLoc },
    { "restrict", DeclSpec::TQ_restrict, RestrictQualLoc },
    { "__unaligned", DeclSpec::TQ_unaligned, UnalignedQualLoc },
    { "_Atomic", DeclSpec::TQ_atomic, AtomicQualLoc }
  };

  SmallString<32> QualStr;
  unsigned NumQuals = 0;
  SourceLocation Loc;
  FixItHint FixIts[5];

  // Build a string naming the redundant qualifiers.
  for (auto &E : QualKinds) {
    if (Quals & E.Mask) {
      if (!QualStr.empty()) QualStr += ' ';
      QualStr += E.Name;

      // If we have a location for the qualifier, offer a fixit.
      SourceLocation QualLoc = E.Loc;
      if (QualLoc.isValid()) {
        FixIts[NumQuals] = FixItHint::CreateRemoval(QualLoc);
        if (Loc.isInvalid() ||
            getSourceManager().isBeforeInTranslationUnit(QualLoc, Loc))
          Loc = QualLoc;
      }

      ++NumQuals;
    }
  }

  Diag(Loc.isInvalid() ? FallbackLoc : Loc, DiagID)
    << QualStr << NumQuals << FixIts[0] << FixIts[1] << FixIts[2] << FixIts[3];
}

// Diagnose pointless type qualifiers on the return type of a function.
static void diagnoseRedundantReturnTypeQualifiers(Sema &S, QualType RetTy,
                                                  Declarator &D,
                                                  unsigned FunctionChunkIndex) {
  const DeclaratorChunk::FunctionTypeInfo &FTI =
      D.getTypeObject(FunctionChunkIndex).Fun;
  if (FTI.hasTrailingReturnType()) {
    S.diagnoseIgnoredQualifiers(diag::warn_qual_return_type,
                                RetTy.getLocalCVRQualifiers(),
                                FTI.getTrailingReturnTypeLoc());
    return;
  }

  for (unsigned OuterChunkIndex = FunctionChunkIndex + 1,
                End = D.getNumTypeObjects();
       OuterChunkIndex != End; ++OuterChunkIndex) {
    DeclaratorChunk &OuterChunk = D.getTypeObject(OuterChunkIndex);
    switch (OuterChunk.Kind) {
    case DeclaratorChunk::Paren:
      continue;

    case DeclaratorChunk::Pointer: {
      DeclaratorChunk::PointerTypeInfo &PTI = OuterChunk.Ptr;
      S.diagnoseIgnoredQualifiers(
          diag::warn_qual_return_type,
          PTI.TypeQuals,
          SourceLocation(),
          PTI.ConstQualLoc,
          PTI.VolatileQualLoc,
          PTI.RestrictQualLoc,
          PTI.AtomicQualLoc,
          PTI.UnalignedQualLoc);
      return;
    }

    case DeclaratorChunk::Function:
    case DeclaratorChunk::BlockPointer:
    case DeclaratorChunk::Reference:
    case DeclaratorChunk::Array:
    case DeclaratorChunk::MemberPointer:
    case DeclaratorChunk::Pipe:
      // FIXME: We can't currently provide an accurate source location and a
      // fix-it hint for these.
      unsigned AtomicQual = RetTy->isAtomicType() ? DeclSpec::TQ_atomic : 0;
      S.diagnoseIgnoredQualifiers(diag::warn_qual_return_type,
                                  RetTy.getCVRQualifiers() | AtomicQual,
                                  D.getIdentifierLoc());
      return;
    }

    llvm_unreachable("unknown declarator chunk kind");
  }

  // If the qualifiers come from a conversion function type, don't diagnose
  // them -- they're not necessarily redundant, since such a conversion
  // operator can be explicitly called as "x.operator const int()".
  if (D.getName().getKind() == UnqualifiedIdKind::IK_ConversionFunctionId)
    return;

  // Just parens all the way out to the decl specifiers. Diagnose any qualifiers
  // which are present there.
  S.diagnoseIgnoredQualifiers(diag::warn_qual_return_type,
                              D.getDeclSpec().getTypeQualifiers(),
                              D.getIdentifierLoc(),
                              D.getDeclSpec().getConstSpecLoc(),
                              D.getDeclSpec().getVolatileSpecLoc(),
                              D.getDeclSpec().getRestrictSpecLoc(),
                              D.getDeclSpec().getAtomicSpecLoc(),
                              D.getDeclSpec().getUnalignedSpecLoc());
}

static std::pair<QualType, TypeSourceInfo *>
InventTemplateParameter(TypeProcessingState &state, QualType T,
                        TypeSourceInfo *TrailingTSI, AutoType *Auto,
                        InventedTemplateParameterInfo &Info) {
  Sema &S = state.getSema();
  Declarator &D = state.getDeclarator();

  const unsigned TemplateParameterDepth = Info.AutoTemplateParameterDepth;
  const unsigned AutoParameterPosition = Info.TemplateParams.size();
  const bool IsParameterPack = D.hasEllipsis();

  // If auto is mentioned in a lambda parameter or abbreviated function
  // template context, convert it to a template parameter type.

  // Create the TemplateTypeParmDecl here to retrieve the corresponding
  // template parameter type. Template parameters are temporarily added
  // to the TU until the associated TemplateDecl is created.
  TemplateTypeParmDecl *InventedTemplateParam =
      TemplateTypeParmDecl::Create(
          S.Context, S.Context.getTranslationUnitDecl(),
          /*KeyLoc=*/D.getDeclSpec().getTypeSpecTypeLoc(),
          /*NameLoc=*/D.getIdentifierLoc(),
          TemplateParameterDepth, AutoParameterPosition,
          S.InventAbbreviatedTemplateParameterTypeName(
              D.getIdentifier(), AutoParameterPosition), false,
          IsParameterPack, /*HasTypeConstraint=*/Auto->isConstrained());
  InventedTemplateParam->setImplicit();
  Info.TemplateParams.push_back(InventedTemplateParam);

  // Attach type constraints to the new parameter.
  if (Auto->isConstrained()) {
    if (TrailingTSI) {
      // The 'auto' appears in a trailing return type we've already built;
      // extract its type constraints to attach to the template parameter.
      AutoTypeLoc AutoLoc = TrailingTSI->getTypeLoc().getContainedAutoTypeLoc();
      TemplateArgumentListInfo TAL(AutoLoc.getLAngleLoc(), AutoLoc.getRAngleLoc());
      bool Invalid = false;
      for (unsigned Idx = 0; Idx < AutoLoc.getNumArgs(); ++Idx) {
        if (D.getEllipsisLoc().isInvalid() && !Invalid &&
            S.DiagnoseUnexpandedParameterPack(AutoLoc.getArgLoc(Idx),
                                              Sema::UPPC_TypeConstraint))
          Invalid = true;
        TAL.addArgument(AutoLoc.getArgLoc(Idx));
      }

      if (!Invalid) {
        S.AttachTypeConstraint(
            AutoLoc.getNestedNameSpecifierLoc(), AutoLoc.getConceptNameInfo(),
            AutoLoc.getNamedConcept(), /*FoundDecl=*/AutoLoc.getFoundDecl(),
            AutoLoc.hasExplicitTemplateArgs() ? &TAL : nullptr,
            InventedTemplateParam, D.getEllipsisLoc());
      }
    } else {
      // The 'auto' appears in the decl-specifiers; we've not finished forming
      // TypeSourceInfo for it yet.
      TemplateIdAnnotation *TemplateId = D.getDeclSpec().getRepAsTemplateId();
      TemplateArgumentListInfo TemplateArgsInfo(TemplateId->LAngleLoc,
                                                TemplateId->RAngleLoc);
      bool Invalid = false;
      if (TemplateId->LAngleLoc.isValid()) {
        ASTTemplateArgsPtr TemplateArgsPtr(TemplateId->getTemplateArgs(),
                                           TemplateId->NumArgs);
        S.translateTemplateArguments(TemplateArgsPtr, TemplateArgsInfo);

        if (D.getEllipsisLoc().isInvalid()) {
          for (TemplateArgumentLoc Arg : TemplateArgsInfo.arguments()) {
            if (S.DiagnoseUnexpandedParameterPack(Arg,
                                                  Sema::UPPC_TypeConstraint)) {
              Invalid = true;
              break;
            }
          }
        }
      }
      if (!Invalid) {
        UsingShadowDecl *USD =
            TemplateId->Template.get().getAsUsingShadowDecl();
        auto *CD =
            cast<ConceptDecl>(TemplateId->Template.get().getAsTemplateDecl());
        S.AttachTypeConstraint(
            D.getDeclSpec().getTypeSpecScope().getWithLocInContext(S.Context),
            DeclarationNameInfo(DeclarationName(TemplateId->Name),
                                TemplateId->TemplateNameLoc),
            CD,
            /*FoundDecl=*/
            USD ? cast<NamedDecl>(USD) : CD,
            TemplateId->LAngleLoc.isValid() ? &TemplateArgsInfo : nullptr,
            InventedTemplateParam, D.getEllipsisLoc());
      }
    }
  }

  // Replace the 'auto' in the function parameter with this invented
  // template type parameter.
  // FIXME: Retain some type sugar to indicate that this was written
  //  as 'auto'?
  QualType Replacement(InventedTemplateParam->getTypeForDecl(), 0);
  QualType NewT = state.ReplaceAutoType(T, Replacement);
  TypeSourceInfo *NewTSI =
      TrailingTSI ? S.ReplaceAutoTypeSourceInfo(TrailingTSI, Replacement)
                  : nullptr;
  return {NewT, NewTSI};
}

static TypeSourceInfo *
GetTypeSourceInfoForDeclarator(TypeProcessingState &State,
                               QualType T, TypeSourceInfo *ReturnTypeInfo);

static QualType GetDeclSpecTypeForDeclarator(TypeProcessingState &state,
                                             TypeSourceInfo *&ReturnTypeInfo) {
  Sema &SemaRef = state.getSema();
  Declarator &D = state.getDeclarator();
  QualType T;
  ReturnTypeInfo = nullptr;

  // The TagDecl owned by the DeclSpec.
  TagDecl *OwnedTagDecl = nullptr;

  switch (D.getName().getKind()) {
  case UnqualifiedIdKind::IK_ImplicitSelfParam:
  case UnqualifiedIdKind::IK_OperatorFunctionId:
  case UnqualifiedIdKind::IK_Identifier:
  case UnqualifiedIdKind::IK_LiteralOperatorId:
  case UnqualifiedIdKind::IK_TemplateId:
    T = ConvertDeclSpecToType(state);

    if (!D.isInvalidType() && D.getDeclSpec().isTypeSpecOwned()) {
      OwnedTagDecl = cast<TagDecl>(D.getDeclSpec().getRepAsDecl());
      // Owned declaration is embedded in declarator.
      OwnedTagDecl->setEmbeddedInDeclarator(true);
    }
    break;

  case UnqualifiedIdKind::IK_ConstructorName:
  case UnqualifiedIdKind::IK_ConstructorTemplateId:
  case UnqualifiedIdKind::IK_DestructorName:
    // Constructors and destructors don't have return types. Use
    // "void" instead.
    T = SemaRef.Context.VoidTy;
    processTypeAttrs(state, T, TAL_DeclSpec,
                     D.getMutableDeclSpec().getAttributes());
    break;

  case UnqualifiedIdKind::IK_DeductionGuideName:
    // Deduction guides have a trailing return type and no type in their
    // decl-specifier sequence. Use a placeholder return type for now.
    T = SemaRef.Context.DependentTy;
    break;

  case UnqualifiedIdKind::IK_ConversionFunctionId:
    // The result type of a conversion function is the type that it
    // converts to.
    T = SemaRef.GetTypeFromParser(D.getName().ConversionFunctionId,
                                  &ReturnTypeInfo);
    break;
  }

  // Note: We don't need to distribute declaration attributes (i.e.
  // D.getDeclarationAttributes()) because those are always C++11 attributes,
  // and those don't get distributed.
  distributeTypeAttrsFromDeclarator(
      state, T, SemaRef.CUDA().IdentifyTarget(D.getAttributes()));

  // Find the deduced type in this type. Look in the trailing return type if we
  // have one, otherwise in the DeclSpec type.
  // FIXME: The standard wording doesn't currently describe this.
  DeducedType *Deduced = T->getContainedDeducedType();
  bool DeducedIsTrailingReturnType = false;
  if (Deduced && isa<AutoType>(Deduced) && D.hasTrailingReturnType()) {
    QualType T = SemaRef.GetTypeFromParser(D.getTrailingReturnType());
    Deduced = T.isNull() ? nullptr : T->getContainedDeducedType();
    DeducedIsTrailingReturnType = true;
  }

  // C++11 [dcl.spec.auto]p5: reject 'auto' if it is not in an allowed context.
  if (Deduced) {
    AutoType *Auto = dyn_cast<AutoType>(Deduced);
    int Error = -1;

    // Is this a 'auto' or 'decltype(auto)' type (as opposed to __auto_type or
    // class template argument deduction)?
    bool IsCXXAutoType =
        (Auto && Auto->getKeyword() != AutoTypeKeyword::GNUAutoType);
    bool IsDeducedReturnType = false;

    switch (D.getContext()) {
    case DeclaratorContext::LambdaExpr:
      // Declared return type of a lambda-declarator is implicit and is always
      // 'auto'.
      break;
    case DeclaratorContext::ObjCParameter:
    case DeclaratorContext::ObjCResult:
      Error = 0;
      break;
    case DeclaratorContext::RequiresExpr:
      Error = 22;
      break;
    case DeclaratorContext::Prototype:
    case DeclaratorContext::LambdaExprParameter: {
      InventedTemplateParameterInfo *Info = nullptr;
      if (D.getContext() == DeclaratorContext::Prototype) {
        // With concepts we allow 'auto' in function parameters.
        if (!SemaRef.getLangOpts().CPlusPlus20 || !Auto ||
            Auto->getKeyword() != AutoTypeKeyword::Auto) {
          Error = 0;
          break;
        } else if (!SemaRef.getCurScope()->isFunctionDeclarationScope()) {
          Error = 21;
          break;
        }

        Info = &SemaRef.InventedParameterInfos.back();
      } else {
        // In C++14, generic lambdas allow 'auto' in their parameters.
        if (!SemaRef.getLangOpts().CPlusPlus14 && Auto &&
            Auto->getKeyword() == AutoTypeKeyword::Auto) {
          Error = 25; // auto not allowed in lambda parameter (before C++14)
          break;
        } else if (!Auto || Auto->getKeyword() != AutoTypeKeyword::Auto) {
          Error = 16; // __auto_type or decltype(auto) not allowed in lambda
                      // parameter
          break;
        }
        Info = SemaRef.getCurLambda();
        assert(Info && "No LambdaScopeInfo on the stack!");
      }

      // We'll deal with inventing template parameters for 'auto' in trailing
      // return types when we pick up the trailing return type when processing
      // the function chunk.
      if (!DeducedIsTrailingReturnType)
        T = InventTemplateParameter(state, T, nullptr, Auto, *Info).first;
      break;
    }
    case DeclaratorContext::Member: {
      if (D.isStaticMember() || D.isFunctionDeclarator())
        break;
      bool Cxx = SemaRef.getLangOpts().CPlusPlus;
      if (isa<ObjCContainerDecl>(SemaRef.CurContext)) {
        Error = 6; // Interface member.
      } else {
        switch (cast<TagDecl>(SemaRef.CurContext)->getTagKind()) {
        case TagTypeKind::Enum:
          llvm_unreachable("unhandled tag kind");
        case TagTypeKind::Struct:
          Error = Cxx ? 1 : 2; /* Struct member */
          break;
        case TagTypeKind::Union:
          Error = Cxx ? 3 : 4; /* Union member */
          break;
        case TagTypeKind::Class:
          Error = 5; /* Class member */
          break;
        case TagTypeKind::Interface:
          Error = 6; /* Interface member */
          break;
        }
      }
      if (D.getDeclSpec().isFriendSpecified())
        Error = 20; // Friend type
      break;
    }
    case DeclaratorContext::CXXCatch:
    case DeclaratorContext::ObjCCatch:
      Error = 7; // Exception declaration
      break;
    case DeclaratorContext::TemplateParam:
      if (isa<DeducedTemplateSpecializationType>(Deduced) &&
          !SemaRef.getLangOpts().CPlusPlus20)
        Error = 19; // Template parameter (until C++20)
      else if (!SemaRef.getLangOpts().CPlusPlus17)
        Error = 8; // Template parameter (until C++17)
      break;
    case DeclaratorContext::BlockLiteral:
      Error = 9; // Block literal
      break;
    case DeclaratorContext::TemplateArg:
      // Within a template argument list, a deduced template specialization
      // type will be reinterpreted as a template template argument.
      if (isa<DeducedTemplateSpecializationType>(Deduced) &&
          !D.getNumTypeObjects() &&
          D.getDeclSpec().getParsedSpecifiers() == DeclSpec::PQ_TypeSpecifier)
        break;
      [[fallthrough]];
    case DeclaratorContext::TemplateTypeArg:
      Error = 10; // Template type argument
      break;
    case DeclaratorContext::AliasDecl:
    case DeclaratorContext::AliasTemplate:
      Error = 12; // Type alias
      break;
    case DeclaratorContext::TrailingReturn:
    case DeclaratorContext::TrailingReturnVar:
      if (!SemaRef.getLangOpts().CPlusPlus14 || !IsCXXAutoType)
        Error = 13; // Function return type
      IsDeducedReturnType = true;
      break;
    case DeclaratorContext::ConversionId:
      if (!SemaRef.getLangOpts().CPlusPlus14 || !IsCXXAutoType)
        Error = 14; // conversion-type-id
      IsDeducedReturnType = true;
      break;
    case DeclaratorContext::FunctionalCast:
      if (isa<DeducedTemplateSpecializationType>(Deduced))
        break;
      if (SemaRef.getLangOpts().CPlusPlus23 && IsCXXAutoType &&
          !Auto->isDecltypeAuto())
        break; // auto(x)
      [[fallthrough]];
    case DeclaratorContext::TypeName:
    case DeclaratorContext::Association:
      Error = 15; // Generic
      break;
    case DeclaratorContext::File:
    case DeclaratorContext::Block:
    case DeclaratorContext::ForInit:
    case DeclaratorContext::SelectionInit:
    case DeclaratorContext::Condition:
      // FIXME: P0091R3 (erroneously) does not permit class template argument
      // deduction in conditions, for-init-statements, and other declarations
      // that are not simple-declarations.
      break;
    case DeclaratorContext::CXXNew:
      // FIXME: P0091R3 does not permit class template argument deduction here,
      // but we follow GCC and allow it anyway.
      if (!IsCXXAutoType && !isa<DeducedTemplateSpecializationType>(Deduced))
        Error = 17; // 'new' type
      break;
    case DeclaratorContext::KNRTypeList:
      Error = 18; // K&R function parameter
      break;
    }

    if (D.getDeclSpec().getStorageClassSpec() == DeclSpec::SCS_typedef)
      Error = 11;

    // In Objective-C it is an error to use 'auto' on a function declarator
    // (and everywhere for '__auto_type').
    if (D.isFunctionDeclarator() &&
        (!SemaRef.getLangOpts().CPlusPlus11 || !IsCXXAutoType))
      Error = 13;

    SourceRange AutoRange = D.getDeclSpec().getTypeSpecTypeLoc();
    if (D.getName().getKind() == UnqualifiedIdKind::IK_ConversionFunctionId)
      AutoRange = D.getName().getSourceRange();

    if (Error != -1) {
      unsigned Kind;
      if (Auto) {
        switch (Auto->getKeyword()) {
        case AutoTypeKeyword::Auto: Kind = 0; break;
        case AutoTypeKeyword::DecltypeAuto: Kind = 1; break;
        case AutoTypeKeyword::GNUAutoType: Kind = 2; break;
        }
      } else {
        assert(isa<DeducedTemplateSpecializationType>(Deduced) &&
               "unknown auto type");
        Kind = 3;
      }

      auto *DTST = dyn_cast<DeducedTemplateSpecializationType>(Deduced);
      TemplateName TN = DTST ? DTST->getTemplateName() : TemplateName();

      SemaRef.Diag(AutoRange.getBegin(), diag::err_auto_not_allowed)
        << Kind << Error << (int)SemaRef.getTemplateNameKindForDiagnostics(TN)
        << QualType(Deduced, 0) << AutoRange;
      if (auto *TD = TN.getAsTemplateDecl())
        SemaRef.NoteTemplateLocation(*TD);

      T = SemaRef.Context.IntTy;
      D.setInvalidType(true);
    } else if (Auto && D.getContext() != DeclaratorContext::LambdaExpr) {
      // If there was a trailing return type, we already got
      // warn_cxx98_compat_trailing_return_type in the parser.
      SemaRef.Diag(AutoRange.getBegin(),
                   D.getContext() == DeclaratorContext::LambdaExprParameter
                       ? diag::warn_cxx11_compat_generic_lambda
                   : IsDeducedReturnType
                       ? diag::warn_cxx11_compat_deduced_return_type
                       : diag::warn_cxx98_compat_auto_type_specifier)
          << AutoRange;
    }
  }

  if (SemaRef.getLangOpts().CPlusPlus &&
      OwnedTagDecl && OwnedTagDecl->isCompleteDefinition()) {
    // Check the contexts where C++ forbids the declaration of a new class
    // or enumeration in a type-specifier-seq.
    unsigned DiagID = 0;
    switch (D.getContext()) {
    case DeclaratorContext::TrailingReturn:
    case DeclaratorContext::TrailingReturnVar:
      // Class and enumeration definitions are syntactically not allowed in
      // trailing return types.
      llvm_unreachable("parser should not have allowed this");
      break;
    case DeclaratorContext::File:
    case DeclaratorContext::Member:
    case DeclaratorContext::Block:
    case DeclaratorContext::ForInit:
    case DeclaratorContext::SelectionInit:
    case DeclaratorContext::BlockLiteral:
    case DeclaratorContext::LambdaExpr:
      // C++11 [dcl.type]p3:
      //   A type-specifier-seq shall not define a class or enumeration unless
      //   it appears in the type-id of an alias-declaration (7.1.3) that is not
      //   the declaration of a template-declaration.
    case DeclaratorContext::AliasDecl:
      break;
    case DeclaratorContext::AliasTemplate:
      DiagID = diag::err_type_defined_in_alias_template;
      break;
    case DeclaratorContext::TypeName:
    case DeclaratorContext::FunctionalCast:
    case DeclaratorContext::ConversionId:
    case DeclaratorContext::TemplateParam:
    case DeclaratorContext::CXXNew:
    case DeclaratorContext::CXXCatch:
    case DeclaratorContext::ObjCCatch:
    case DeclaratorContext::TemplateArg:
    case DeclaratorContext::TemplateTypeArg:
    case DeclaratorContext::Association:
      DiagID = diag::err_type_defined_in_type_specifier;
      break;
    case DeclaratorContext::Prototype:
    case DeclaratorContext::LambdaExprParameter:
    case DeclaratorContext::ObjCParameter:
    case DeclaratorContext::ObjCResult:
    case DeclaratorContext::KNRTypeList:
    case DeclaratorContext::RequiresExpr:
      // C++ [dcl.fct]p6:
      //   Types shall not be defined in return or parameter types.
      DiagID = diag::err_type_defined_in_param_type;
      break;
    case DeclaratorContext::Condition:
      // C++ 6.4p2:
      // The type-specifier-seq shall not contain typedef and shall not declare
      // a new class or enumeration.
      DiagID = diag::err_type_defined_in_condition;
      break;
    }

    if (DiagID != 0) {
      SemaRef.Diag(OwnedTagDecl->getLocation(), DiagID)
          << SemaRef.Context.getTypeDeclType(OwnedTagDecl);
      D.setInvalidType(true);
    }
  }

  assert(!T.isNull() && "This function should not return a null type");
  return T;
}

/// Produce an appropriate diagnostic for an ambiguity between a function
/// declarator and a C++ direct-initializer.
static void warnAboutAmbiguousFunction(Sema &S, Declarator &D,
                                       DeclaratorChunk &DeclType, QualType RT) {
  const DeclaratorChunk::FunctionTypeInfo &FTI = DeclType.Fun;
  assert(FTI.isAmbiguous && "no direct-initializer / function ambiguity");

  // If the return type is void there is no ambiguity.
  if (RT->isVoidType())
    return;

  // An initializer for a non-class type can have at most one argument.
  if (!RT->isRecordType() && FTI.NumParams > 1)
    return;

  // An initializer for a reference must have exactly one argument.
  if (RT->isReferenceType() && FTI.NumParams != 1)
    return;

  // Only warn if this declarator is declaring a function at block scope, and
  // doesn't have a storage class (such as 'extern') specified.
  if (!D.isFunctionDeclarator() ||
      D.getFunctionDefinitionKind() != FunctionDefinitionKind::Declaration ||
      !S.CurContext->isFunctionOrMethod() ||
      D.getDeclSpec().getStorageClassSpec() != DeclSpec::SCS_unspecified)
    return;

  // Inside a condition, a direct initializer is not permitted. We allow one to
  // be parsed in order to give better diagnostics in condition parsing.
  if (D.getContext() == DeclaratorContext::Condition)
    return;

  SourceRange ParenRange(DeclType.Loc, DeclType.EndLoc);

  S.Diag(DeclType.Loc,
         FTI.NumParams ? diag::warn_parens_disambiguated_as_function_declaration
                       : diag::warn_empty_parens_are_function_decl)
      << ParenRange;

  // If the declaration looks like:
  //   T var1,
  //   f();
  // and name lookup finds a function named 'f', then the ',' was
  // probably intended to be a ';'.
  if (!D.isFirstDeclarator() && D.getIdentifier()) {
    FullSourceLoc Comma(D.getCommaLoc(), S.SourceMgr);
    FullSourceLoc Name(D.getIdentifierLoc(), S.SourceMgr);
    if (Comma.getFileID() != Name.getFileID() ||
        Comma.getSpellingLineNumber() != Name.getSpellingLineNumber()) {
      LookupResult Result(S, D.getIdentifier(), SourceLocation(),
                          Sema::LookupOrdinaryName);
      if (S.LookupName(Result, S.getCurScope()))
        S.Diag(D.getCommaLoc(), diag::note_empty_parens_function_call)
          << FixItHint::CreateReplacement(D.getCommaLoc(), ";")
          << D.getIdentifier();
      Result.suppressDiagnostics();
    }
  }

  if (FTI.NumParams > 0) {
    // For a declaration with parameters, eg. "T var(T());", suggest adding
    // parens around the first parameter to turn the declaration into a
    // variable declaration.
    SourceRange Range = FTI.Params[0].Param->getSourceRange();
    SourceLocation B = Range.getBegin();
    SourceLocation E = S.getLocForEndOfToken(Range.getEnd());
    // FIXME: Maybe we should suggest adding braces instead of parens
    // in C++11 for classes that don't have an initializer_list constructor.
    S.Diag(B, diag::note_additional_parens_for_variable_declaration)
      << FixItHint::CreateInsertion(B, "(")
      << FixItHint::CreateInsertion(E, ")");
  } else {
    // For a declaration without parameters, eg. "T var();", suggest replacing
    // the parens with an initializer to turn the declaration into a variable
    // declaration.
    const CXXRecordDecl *RD = RT->getAsCXXRecordDecl();

    // Empty parens mean value-initialization, and no parens mean
    // default initialization. These are equivalent if the default
    // constructor is user-provided or if zero-initialization is a
    // no-op.
    if (RD && RD->hasDefinition() &&
        (RD->isEmpty() || RD->hasUserProvidedDefaultConstructor()))
      S.Diag(DeclType.Loc, diag::note_empty_parens_default_ctor)
        << FixItHint::CreateRemoval(ParenRange);
    else {
      std::string Init =
          S.getFixItZeroInitializerForType(RT, ParenRange.getBegin());
      if (Init.empty() && S.LangOpts.CPlusPlus11)
        Init = "{}";
      if (!Init.empty())
        S.Diag(DeclType.Loc, diag::note_empty_parens_zero_initialize)
          << FixItHint::CreateReplacement(ParenRange, Init);
    }
  }
}

/// Produce an appropriate diagnostic for a declarator with top-level
/// parentheses.
static void warnAboutRedundantParens(Sema &S, Declarator &D, QualType T) {
  DeclaratorChunk &Paren = D.getTypeObject(D.getNumTypeObjects() - 1);
  assert(Paren.Kind == DeclaratorChunk::Paren &&
         "do not have redundant top-level parentheses");

  // This is a syntactic check; we're not interested in cases that arise
  // during template instantiation.
  if (S.inTemplateInstantiation())
    return;

  // Check whether this could be intended to be a construction of a temporary
  // object in C++ via a function-style cast.
  bool CouldBeTemporaryObject =
      S.getLangOpts().CPlusPlus && D.isExpressionContext() &&
      !D.isInvalidType() && D.getIdentifier() &&
      D.getDeclSpec().getParsedSpecifiers() == DeclSpec::PQ_TypeSpecifier &&
      (T->isRecordType() || T->isDependentType()) &&
      D.getDeclSpec().getTypeQualifiers() == 0 && D.isFirstDeclarator();

  bool StartsWithDeclaratorId = true;
  for (auto &C : D.type_objects()) {
    switch (C.Kind) {
    case DeclaratorChunk::Paren:
      if (&C == &Paren)
        continue;
      [[fallthrough]];
    case DeclaratorChunk::Pointer:
      StartsWithDeclaratorId = false;
      continue;

    case DeclaratorChunk::Array:
      if (!C.Arr.NumElts)
        CouldBeTemporaryObject = false;
      continue;

    case DeclaratorChunk::Reference:
      // FIXME: Suppress the warning here if there is no initializer; we're
      // going to give an error anyway.
      // We assume that something like 'T (&x) = y;' is highly likely to not
      // be intended to be a temporary object.
      CouldBeTemporaryObject = false;
      StartsWithDeclaratorId = false;
      continue;

    case DeclaratorChunk::Function:
      // In a new-type-id, function chunks require parentheses.
      if (D.getContext() == DeclaratorContext::CXXNew)
        return;
      // FIXME: "A(f())" deserves a vexing-parse warning, not just a
      // redundant-parens warning, but we don't know whether the function
      // chunk was syntactically valid as an expression here.
      CouldBeTemporaryObject = false;
      continue;

    case DeclaratorChunk::BlockPointer:
    case DeclaratorChunk::MemberPointer:
    case DeclaratorChunk::Pipe:
      // These cannot appear in expressions.
      CouldBeTemporaryObject = false;
      StartsWithDeclaratorId = false;
      continue;
    }
  }

  // FIXME: If there is an initializer, assume that this is not intended to be
  // a construction of a temporary object.

  // Check whether the name has already been declared; if not, this is not a
  // function-style cast.
  if (CouldBeTemporaryObject) {
    LookupResult Result(S, D.getIdentifier(), SourceLocation(),
                        Sema::LookupOrdinaryName);
    if (!S.LookupName(Result, S.getCurScope()))
      CouldBeTemporaryObject = false;
    Result.suppressDiagnostics();
  }

  SourceRange ParenRange(Paren.Loc, Paren.EndLoc);

  if (!CouldBeTemporaryObject) {
    // If we have A (::B), the parentheses affect the meaning of the program.
    // Suppress the warning in that case. Don't bother looking at the DeclSpec
    // here: even (e.g.) "int ::x" is visually ambiguous even though it's
    // formally unambiguous.
    if (StartsWithDeclaratorId && D.getCXXScopeSpec().isValid()) {
      for (NestedNameSpecifier *NNS = D.getCXXScopeSpec().getScopeRep(); NNS;
           NNS = NNS->getPrefix()) {
        if (NNS->getKind() == NestedNameSpecifier::Global)
          return;
      }
    }

    S.Diag(Paren.Loc, diag::warn_redundant_parens_around_declarator)
        << ParenRange << FixItHint::CreateRemoval(Paren.Loc)
        << FixItHint::CreateRemoval(Paren.EndLoc);
    return;
  }

  S.Diag(Paren.Loc, diag::warn_parens_disambiguated_as_variable_declaration)
      << ParenRange << D.getIdentifier();
  auto *RD = T->getAsCXXRecordDecl();
  if (!RD || !RD->hasDefinition() || RD->hasNonTrivialDestructor())
    S.Diag(Paren.Loc, diag::note_raii_guard_add_name)
        << FixItHint::CreateInsertion(Paren.Loc, " varname") << T
        << D.getIdentifier();
  // FIXME: A cast to void is probably a better suggestion in cases where it's
  // valid (when there is no initializer and we're not in a condition).
  S.Diag(D.getBeginLoc(), diag::note_function_style_cast_add_parentheses)
      << FixItHint::CreateInsertion(D.getBeginLoc(), "(")
      << FixItHint::CreateInsertion(S.getLocForEndOfToken(D.getEndLoc()), ")");
  S.Diag(Paren.Loc, diag::note_remove_parens_for_variable_declaration)
      << FixItHint::CreateRemoval(Paren.Loc)
      << FixItHint::CreateRemoval(Paren.EndLoc);
}

/// Helper for figuring out the default CC for a function declarator type.  If
/// this is the outermost chunk, then we can determine the CC from the
/// declarator context.  If not, then this could be either a member function
/// type or normal function type.
static CallingConv getCCForDeclaratorChunk(
    Sema &S, Declarator &D, const ParsedAttributesView &AttrList,
    const DeclaratorChunk::FunctionTypeInfo &FTI, unsigned ChunkIndex) {
  assert(D.getTypeObject(ChunkIndex).Kind == DeclaratorChunk::Function);

  // Check for an explicit CC attribute.
  for (const ParsedAttr &AL : AttrList) {
    switch (AL.getKind()) {
    CALLING_CONV_ATTRS_CASELIST : {
      // Ignore attributes that don't validate or can't apply to the
      // function type.  We'll diagnose the failure to apply them in
      // handleFunctionTypeAttr.
      CallingConv CC;
      if (!S.CheckCallingConvAttr(AL, CC, /*FunctionDecl=*/nullptr,
                                  S.CUDA().IdentifyTarget(D.getAttributes())) &&
          (!FTI.isVariadic || supportsVariadicCall(CC))) {
        return CC;
      }
      break;
    }

    default:
      break;
    }
  }

  bool IsCXXInstanceMethod = false;

  if (S.getLangOpts().CPlusPlus) {
    // Look inwards through parentheses to see if this chunk will form a
    // member pointer type or if we're the declarator.  Any type attributes
    // between here and there will override the CC we choose here.
    unsigned I = ChunkIndex;
    bool FoundNonParen = false;
    while (I && !FoundNonParen) {
      --I;
      if (D.getTypeObject(I).Kind != DeclaratorChunk::Paren)
        FoundNonParen = true;
    }

    if (FoundNonParen) {
      // If we're not the declarator, we're a regular function type unless we're
      // in a member pointer.
      IsCXXInstanceMethod =
          D.getTypeObject(I).Kind == DeclaratorChunk::MemberPointer;
    } else if (D.getContext() == DeclaratorContext::LambdaExpr) {
      // This can only be a call operator for a lambda, which is an instance
      // method, unless explicitly specified as 'static'.
      IsCXXInstanceMethod =
          D.getDeclSpec().getStorageClassSpec() != DeclSpec::SCS_static;
    } else {
      // We're the innermost decl chunk, so must be a function declarator.
      assert(D.isFunctionDeclarator());

      // If we're inside a record, we're declaring a method, but it could be
      // explicitly or implicitly static.
      IsCXXInstanceMethod =
          D.isFirstDeclarationOfMember() &&
          D.getDeclSpec().getStorageClassSpec() != DeclSpec::SCS_typedef &&
          !D.isStaticMember();
    }
  }

  CallingConv CC = S.Context.getDefaultCallingConvention(FTI.isVariadic,
                                                         IsCXXInstanceMethod);

  // Attribute AT_OpenCLKernel affects the calling convention for SPIR
  // and AMDGPU targets, hence it cannot be treated as a calling
  // convention attribute. This is the simplest place to infer
  // calling convention for OpenCL kernels.
  if (S.getLangOpts().OpenCL) {
    for (const ParsedAttr &AL : D.getDeclSpec().getAttributes()) {
      if (AL.getKind() == ParsedAttr::AT_OpenCLKernel) {
        CC = CC_OpenCLKernel;
        break;
      }
    }
  } else if (S.getLangOpts().CUDA) {
    // If we're compiling CUDA/HIP code and targeting SPIR-V we need to make
    // sure the kernels will be marked with the right calling convention so that
    // they will be visible by the APIs that ingest SPIR-V.
    llvm::Triple Triple = S.Context.getTargetInfo().getTriple();
    if (Triple.getArch() == llvm::Triple::spirv32 ||
        Triple.getArch() == llvm::Triple::spirv64) {
      for (const ParsedAttr &AL : D.getDeclSpec().getAttributes()) {
        if (AL.getKind() == ParsedAttr::AT_CUDAGlobal) {
          CC = CC_OpenCLKernel;
          break;
        }
      }
    }
  }

  return CC;
}

namespace {
  /// A simple notion of pointer kinds, which matches up with the various
  /// pointer declarators.
  enum class SimplePointerKind {
    Pointer,
    BlockPointer,
    MemberPointer,
    Array,
  };
} // end anonymous namespace

IdentifierInfo *Sema::getNullabilityKeyword(NullabilityKind nullability) {
  switch (nullability) {
  case NullabilityKind::NonNull:
    if (!Ident__Nonnull)
      Ident__Nonnull = PP.getIdentifierInfo("_Nonnull");
    return Ident__Nonnull;

  case NullabilityKind::Nullable:
    if (!Ident__Nullable)
      Ident__Nullable = PP.getIdentifierInfo("_Nullable");
    return Ident__Nullable;

  case NullabilityKind::NullableResult:
    if (!Ident__Nullable_result)
      Ident__Nullable_result = PP.getIdentifierInfo("_Nullable_result");
    return Ident__Nullable_result;

  case NullabilityKind::Unspecified:
    if (!Ident__Null_unspecified)
      Ident__Null_unspecified = PP.getIdentifierInfo("_Null_unspecified");
    return Ident__Null_unspecified;
  }
  llvm_unreachable("Unknown nullability kind.");
}

/// Check whether there is a nullability attribute of any kind in the given
/// attribute list.
static bool hasNullabilityAttr(const ParsedAttributesView &attrs) {
  for (const ParsedAttr &AL : attrs) {
    if (AL.getKind() == ParsedAttr::AT_TypeNonNull ||
        AL.getKind() == ParsedAttr::AT_TypeNullable ||
        AL.getKind() == ParsedAttr::AT_TypeNullableResult ||
        AL.getKind() == ParsedAttr::AT_TypeNullUnspecified)
      return true;
  }

  return false;
}

namespace {
  /// Describes the kind of a pointer a declarator describes.
  enum class PointerDeclaratorKind {
    // Not a pointer.
    NonPointer,
    // Single-level pointer.
    SingleLevelPointer,
    // Multi-level pointer (of any pointer kind).
    MultiLevelPointer,
    // CFFooRef*
    MaybePointerToCFRef,
    // CFErrorRef*
    CFErrorRefPointer,
    // NSError**
    NSErrorPointerPointer,
  };

  /// Describes a declarator chunk wrapping a pointer that marks inference as
  /// unexpected.
  // These values must be kept in sync with diagnostics.
  enum class PointerWrappingDeclaratorKind {
    /// Pointer is top-level.
    None = -1,
    /// Pointer is an array element.
    Array = 0,
    /// Pointer is the referent type of a C++ reference.
    Reference = 1
  };
} // end anonymous namespace

/// Classify the given declarator, whose type-specified is \c type, based on
/// what kind of pointer it refers to.
///
/// This is used to determine the default nullability.
static PointerDeclaratorKind
classifyPointerDeclarator(Sema &S, QualType type, Declarator &declarator,
                          PointerWrappingDeclaratorKind &wrappingKind) {
  unsigned numNormalPointers = 0;

  // For any dependent type, we consider it a non-pointer.
  if (type->isDependentType())
    return PointerDeclaratorKind::NonPointer;

  // Look through the declarator chunks to identify pointers.
  for (unsigned i = 0, n = declarator.getNumTypeObjects(); i != n; ++i) {
    DeclaratorChunk &chunk = declarator.getTypeObject(i);
    switch (chunk.Kind) {
    case DeclaratorChunk::Array:
      if (numNormalPointers == 0)
        wrappingKind = PointerWrappingDeclaratorKind::Array;
      break;

    case DeclaratorChunk::Function:
    case DeclaratorChunk::Pipe:
      break;

    case DeclaratorChunk::BlockPointer:
    case DeclaratorChunk::MemberPointer:
      return numNormalPointers > 0 ? PointerDeclaratorKind::MultiLevelPointer
                                   : PointerDeclaratorKind::SingleLevelPointer;

    case DeclaratorChunk::Paren:
      break;

    case DeclaratorChunk::Reference:
      if (numNormalPointers == 0)
        wrappingKind = PointerWrappingDeclaratorKind::Reference;
      break;

    case DeclaratorChunk::Pointer:
      ++numNormalPointers;
      if (numNormalPointers > 2)
        return PointerDeclaratorKind::MultiLevelPointer;
      break;
    }
  }

  // Then, dig into the type specifier itself.
  unsigned numTypeSpecifierPointers = 0;
  do {
    // Decompose normal pointers.
    if (auto ptrType = type->getAs<PointerType>()) {
      ++numNormalPointers;

      if (numNormalPointers > 2)
        return PointerDeclaratorKind::MultiLevelPointer;

      type = ptrType->getPointeeType();
      ++numTypeSpecifierPointers;
      continue;
    }

    // Decompose block pointers.
    if (type->getAs<BlockPointerType>()) {
      return numNormalPointers > 0 ? PointerDeclaratorKind::MultiLevelPointer
                                   : PointerDeclaratorKind::SingleLevelPointer;
    }

    // Decompose member pointers.
    if (type->getAs<MemberPointerType>()) {
      return numNormalPointers > 0 ? PointerDeclaratorKind::MultiLevelPointer
                                   : PointerDeclaratorKind::SingleLevelPointer;
    }

    // Look at Objective-C object pointers.
    if (auto objcObjectPtr = type->getAs<ObjCObjectPointerType>()) {
      ++numNormalPointers;
      ++numTypeSpecifierPointers;

      // If this is NSError**, report that.
      if (auto objcClassDecl = objcObjectPtr->getInterfaceDecl()) {
        if (objcClassDecl->getIdentifier() == S.ObjC().getNSErrorIdent() &&
            numNormalPointers == 2 && numTypeSpecifierPointers < 2) {
          return PointerDeclaratorKind::NSErrorPointerPointer;
        }
      }

      break;
    }

    // Look at Objective-C class types.
    if (auto objcClass = type->getAs<ObjCInterfaceType>()) {
      if (objcClass->getInterface()->getIdentifier() ==
          S.ObjC().getNSErrorIdent()) {
        if (numNormalPointers == 2 && numTypeSpecifierPointers < 2)
          return PointerDeclaratorKind::NSErrorPointerPointer;
      }

      break;
    }

    // If at this point we haven't seen a pointer, we won't see one.
    if (numNormalPointers == 0)
      return PointerDeclaratorKind::NonPointer;

    if (auto recordType = type->getAs<RecordType>()) {
      RecordDecl *recordDecl = recordType->getDecl();

      // If this is CFErrorRef*, report it as such.
      if (numNormalPointers == 2 && numTypeSpecifierPointers < 2 &&
          S.ObjC().isCFError(recordDecl)) {
        return PointerDeclaratorKind::CFErrorRefPointer;
      }
      break;
    }

    break;
  } while (true);

  switch (numNormalPointers) {
  case 0:
    return PointerDeclaratorKind::NonPointer;

  case 1:
    return PointerDeclaratorKind::SingleLevelPointer;

  case 2:
    return PointerDeclaratorKind::MaybePointerToCFRef;

  default:
    return PointerDeclaratorKind::MultiLevelPointer;
  }
}

static FileID getNullabilityCompletenessCheckFileID(Sema &S,
                                                    SourceLocation loc) {
  // If we're anywhere in a function, method, or closure context, don't perform
  // completeness checks.
  for (DeclContext *ctx = S.CurContext; ctx; ctx = ctx->getParent()) {
    if (ctx->isFunctionOrMethod())
      return FileID();

    if (ctx->isFileContext())
      break;
  }

  // We only care about the expansion location.
  loc = S.SourceMgr.getExpansionLoc(loc);
  FileID file = S.SourceMgr.getFileID(loc);
  if (file.isInvalid())
    return FileID();

  // Retrieve file information.
  bool invalid = false;
  const SrcMgr::SLocEntry &sloc = S.SourceMgr.getSLocEntry(file, &invalid);
  if (invalid || !sloc.isFile())
    return FileID();

  // We don't want to perform completeness checks on the main file or in
  // system headers.
  const SrcMgr::FileInfo &fileInfo = sloc.getFile();
  if (fileInfo.getIncludeLoc().isInvalid())
    return FileID();
  if (fileInfo.getFileCharacteristic() != SrcMgr::C_User &&
      S.Diags.getSuppressSystemWarnings()) {
    return FileID();
  }

  return file;
}

/// Creates a fix-it to insert a C-style nullability keyword at \p pointerLoc,
/// taking into account whitespace before and after.
template <typename DiagBuilderT>
static void fixItNullability(Sema &S, DiagBuilderT &Diag,
                             SourceLocation PointerLoc,
                             NullabilityKind Nullability) {
  assert(PointerLoc.isValid());
  if (PointerLoc.isMacroID())
    return;

  SourceLocation FixItLoc = S.getLocForEndOfToken(PointerLoc);
  if (!FixItLoc.isValid() || FixItLoc == PointerLoc)
    return;

  const char *NextChar = S.SourceMgr.getCharacterData(FixItLoc);
  if (!NextChar)
    return;

  SmallString<32> InsertionTextBuf{" "};
  InsertionTextBuf += getNullabilitySpelling(Nullability);
  InsertionTextBuf += " ";
  StringRef InsertionText = InsertionTextBuf.str();

  if (isWhitespace(*NextChar)) {
    InsertionText = InsertionText.drop_back();
  } else if (NextChar[-1] == '[') {
    if (NextChar[0] == ']')
      InsertionText = InsertionText.drop_back().drop_front();
    else
      InsertionText = InsertionText.drop_front();
  } else if (!isAsciiIdentifierContinue(NextChar[0], /*allow dollar*/ true) &&
             !isAsciiIdentifierContinue(NextChar[-1], /*allow dollar*/ true)) {
    InsertionText = InsertionText.drop_back().drop_front();
  }

  Diag << FixItHint::CreateInsertion(FixItLoc, InsertionText);
}

static void emitNullabilityConsistencyWarning(Sema &S,
                                              SimplePointerKind PointerKind,
                                              SourceLocation PointerLoc,
                                              SourceLocation PointerEndLoc) {
  assert(PointerLoc.isValid());

  if (PointerKind == SimplePointerKind::Array) {
    S.Diag(PointerLoc, diag::warn_nullability_missing_array);
  } else {
    S.Diag(PointerLoc, diag::warn_nullability_missing)
      << static_cast<unsigned>(PointerKind);
  }

  auto FixItLoc = PointerEndLoc.isValid() ? PointerEndLoc : PointerLoc;
  if (FixItLoc.isMacroID())
    return;

  auto addFixIt = [&](NullabilityKind Nullability) {
    auto Diag = S.Diag(FixItLoc, diag::note_nullability_fix_it);
    Diag << static_cast<unsigned>(Nullability);
    Diag << static_cast<unsigned>(PointerKind);
    fixItNullability(S, Diag, FixItLoc, Nullability);
  };
  addFixIt(NullabilityKind::Nullable);
  addFixIt(NullabilityKind::NonNull);
}

/// Complains about missing nullability if the file containing \p pointerLoc
/// has other uses of nullability (either the keywords or the \c assume_nonnull
/// pragma).
///
/// If the file has \e not seen other uses of nullability, this particular
/// pointer is saved for possible later diagnosis. See recordNullabilitySeen().
static void
checkNullabilityConsistency(Sema &S, SimplePointerKind pointerKind,
                            SourceLocation pointerLoc,
                            SourceLocation pointerEndLoc = SourceLocation()) {
  // Determine which file we're performing consistency checking for.
  FileID file = getNullabilityCompletenessCheckFileID(S, pointerLoc);
  if (file.isInvalid())
    return;

  // If we haven't seen any type nullability in this file, we won't warn now
  // about anything.
  FileNullability &fileNullability = S.NullabilityMap[file];
  if (!fileNullability.SawTypeNullability) {
    // If this is the first pointer declarator in the file, and the appropriate
    // warning is on, record it in case we need to diagnose it retroactively.
    diag::kind diagKind;
    if (pointerKind == SimplePointerKind::Array)
      diagKind = diag::warn_nullability_missing_array;
    else
      diagKind = diag::warn_nullability_missing;

    if (fileNullability.PointerLoc.isInvalid() &&
        !S.Context.getDiagnostics().isIgnored(diagKind, pointerLoc)) {
      fileNullability.PointerLoc = pointerLoc;
      fileNullability.PointerEndLoc = pointerEndLoc;
      fileNullability.PointerKind = static_cast<unsigned>(pointerKind);
    }

    return;
  }

  // Complain about missing nullability.
  emitNullabilityConsistencyWarning(S, pointerKind, pointerLoc, pointerEndLoc);
}

/// Marks that a nullability feature has been used in the file containing
/// \p loc.
///
/// If this file already had pointer types in it that were missing nullability,
/// the first such instance is retroactively diagnosed.
///
/// \sa checkNullabilityConsistency
static void recordNullabilitySeen(Sema &S, SourceLocation loc) {
  FileID file = getNullabilityCompletenessCheckFileID(S, loc);
  if (file.isInvalid())
    return;

  FileNullability &fileNullability = S.NullabilityMap[file];
  if (fileNullability.SawTypeNullability)
    return;
  fileNullability.SawTypeNullability = true;

  // If we haven't seen any type nullability before, now we have. Retroactively
  // diagnose the first unannotated pointer, if there was one.
  if (fileNullability.PointerLoc.isInvalid())
    return;

  auto kind = static_cast<SimplePointerKind>(fileNullability.PointerKind);
  emitNullabilityConsistencyWarning(S, kind, fileNullability.PointerLoc,
                                    fileNullability.PointerEndLoc);
}

/// Returns true if any of the declarator chunks before \p endIndex include a
/// level of indirection: array, pointer, reference, or pointer-to-member.
///
/// Because declarator chunks are stored in outer-to-inner order, testing
/// every chunk before \p endIndex is testing all chunks that embed the current
/// chunk as part of their type.
///
/// It is legal to pass the result of Declarator::getNumTypeObjects() as the
/// end index, in which case all chunks are tested.
static bool hasOuterPointerLikeChunk(const Declarator &D, unsigned endIndex) {
  unsigned i = endIndex;
  while (i != 0) {
    // Walk outwards along the declarator chunks.
    --i;
    const DeclaratorChunk &DC = D.getTypeObject(i);
    switch (DC.Kind) {
    case DeclaratorChunk::Paren:
      break;
    case DeclaratorChunk::Array:
    case DeclaratorChunk::Pointer:
    case DeclaratorChunk::Reference:
    case DeclaratorChunk::MemberPointer:
      return true;
    case DeclaratorChunk::Function:
    case DeclaratorChunk::BlockPointer:
    case DeclaratorChunk::Pipe:
      // These are invalid anyway, so just ignore.
      break;
    }
  }
  return false;
}

static bool IsNoDerefableChunk(const DeclaratorChunk &Chunk) {
  return (Chunk.Kind == DeclaratorChunk::Pointer ||
          Chunk.Kind == DeclaratorChunk::Array);
}

template<typename AttrT>
static AttrT *createSimpleAttr(ASTContext &Ctx, ParsedAttr &AL) {
  AL.setUsedAsTypeAttr();
  return ::new (Ctx) AttrT(Ctx, AL);
}

static Attr *createNullabilityAttr(ASTContext &Ctx, ParsedAttr &Attr,
                                   NullabilityKind NK) {
  switch (NK) {
  case NullabilityKind::NonNull:
    return createSimpleAttr<TypeNonNullAttr>(Ctx, Attr);

  case NullabilityKind::Nullable:
    return createSimpleAttr<TypeNullableAttr>(Ctx, Attr);

  case NullabilityKind::NullableResult:
    return createSimpleAttr<TypeNullableResultAttr>(Ctx, Attr);

  case NullabilityKind::Unspecified:
    return createSimpleAttr<TypeNullUnspecifiedAttr>(Ctx, Attr);
  }
  llvm_unreachable("unknown NullabilityKind");
}

// Diagnose whether this is a case with the multiple addr spaces.
// Returns true if this is an invalid case.
// ISO/IEC TR 18037 S5.3 (amending C99 6.7.3): "No type shall be qualified
// by qualifiers for two or more different address spaces."
static bool DiagnoseMultipleAddrSpaceAttributes(Sema &S, LangAS ASOld,
                                                LangAS ASNew,
                                                SourceLocation AttrLoc) {
  if (ASOld != LangAS::Default) {
    if (ASOld != ASNew) {
      S.Diag(AttrLoc, diag::err_attribute_address_multiple_qualifiers);
      return true;
    }
    // Emit a warning if they are identical; it's likely unintended.
    S.Diag(AttrLoc,
           diag::warn_attribute_address_multiple_identical_qualifiers);
  }
  return false;
}

// Whether this is a type broadly expected to have nullability attached.
// These types are affected by `#pragma assume_nonnull`, and missing nullability
// will be diagnosed with -Wnullability-completeness.
static bool shouldHaveNullability(QualType T) {
  return T->canHaveNullability(/*ResultIfUnknown=*/false) &&
         // For now, do not infer/require nullability on C++ smart pointers.
         // It's unclear whether the pragma's behavior is useful for C++.
         // e.g. treating type-aliases and template-type-parameters differently
         // from types of declarations can be surprising.
         !isa<RecordType, TemplateSpecializationType>(
             T->getCanonicalTypeInternal());
}

static TypeSourceInfo *GetFullTypeForDeclarator(TypeProcessingState &state,
                                                QualType declSpecType,
                                                TypeSourceInfo *TInfo) {
  // The TypeSourceInfo that this function returns will not be a null type.
  // If there is an error, this function will fill in a dummy type as fallback.
  QualType T = declSpecType;
  Declarator &D = state.getDeclarator();
  Sema &S = state.getSema();
  ASTContext &Context = S.Context;
  const LangOptions &LangOpts = S.getLangOpts();

  // The name we're declaring, if any.
  DeclarationName Name;
  if (D.getIdentifier())
    Name = D.getIdentifier();

  // Does this declaration declare a typedef-name?
  bool IsTypedefName =
      D.getDeclSpec().getStorageClassSpec() == DeclSpec::SCS_typedef ||
      D.getContext() == DeclaratorContext::AliasDecl ||
      D.getContext() == DeclaratorContext::AliasTemplate;

  // Does T refer to a function type with a cv-qualifier or a ref-qualifier?
  bool IsQualifiedFunction = T->isFunctionProtoType() &&
      (!T->castAs<FunctionProtoType>()->getMethodQuals().empty() ||
       T->castAs<FunctionProtoType>()->getRefQualifier() != RQ_None);

  // If T is 'decltype(auto)', the only declarators we can have are parens
  // and at most one function declarator if this is a function declaration.
  // If T is a deduced class template specialization type, we can have no
  // declarator chunks at all.
  if (auto *DT = T->getAs<DeducedType>()) {
    const AutoType *AT = T->getAs<AutoType>();
    bool IsClassTemplateDeduction = isa<DeducedTemplateSpecializationType>(DT);
    if ((AT && AT->isDecltypeAuto()) || IsClassTemplateDeduction) {
      for (unsigned I = 0, E = D.getNumTypeObjects(); I != E; ++I) {
        unsigned Index = E - I - 1;
        DeclaratorChunk &DeclChunk = D.getTypeObject(Index);
        unsigned DiagId = IsClassTemplateDeduction
                              ? diag::err_deduced_class_template_compound_type
                              : diag::err_decltype_auto_compound_type;
        unsigned DiagKind = 0;
        switch (DeclChunk.Kind) {
        case DeclaratorChunk::Paren:
          // FIXME: Rejecting this is a little silly.
          if (IsClassTemplateDeduction) {
            DiagKind = 4;
            break;
          }
          continue;
        case DeclaratorChunk::Function: {
          if (IsClassTemplateDeduction) {
            DiagKind = 3;
            break;
          }
          unsigned FnIndex;
          if (D.isFunctionDeclarationContext() &&
              D.isFunctionDeclarator(FnIndex) && FnIndex == Index)
            continue;
          DiagId = diag::err_decltype_auto_function_declarator_not_declaration;
          break;
        }
        case DeclaratorChunk::Pointer:
        case DeclaratorChunk::BlockPointer:
        case DeclaratorChunk::MemberPointer:
          DiagKind = 0;
          break;
        case DeclaratorChunk::Reference:
          DiagKind = 1;
          break;
        case DeclaratorChunk::Array:
          DiagKind = 2;
          break;
        case DeclaratorChunk::Pipe:
          break;
        }

        S.Diag(DeclChunk.Loc, DiagId) << DiagKind;
        D.setInvalidType(true);
        break;
      }
    }
  }

  // Determine whether we should infer _Nonnull on pointer types.
  std::optional<NullabilityKind> inferNullability;
  bool inferNullabilityCS = false;
  bool inferNullabilityInnerOnly = false;
  bool inferNullabilityInnerOnlyComplete = false;

  // Are we in an assume-nonnull region?
  bool inAssumeNonNullRegion = false;
  SourceLocation assumeNonNullLoc = S.PP.getPragmaAssumeNonNullLoc();
  if (assumeNonNullLoc.isValid()) {
    inAssumeNonNullRegion = true;
    recordNullabilitySeen(S, assumeNonNullLoc);
  }

  // Whether to complain about missing nullability specifiers or not.
  enum {
    /// Never complain.
    CAMN_No,
    /// Complain on the inner pointers (but not the outermost
    /// pointer).
    CAMN_InnerPointers,
    /// Complain about any pointers that don't have nullability
    /// specified or inferred.
    CAMN_Yes
  } complainAboutMissingNullability = CAMN_No;
  unsigned NumPointersRemaining = 0;
  auto complainAboutInferringWithinChunk = PointerWrappingDeclaratorKind::None;

  if (IsTypedefName) {
    // For typedefs, we do not infer any nullability (the default),
    // and we only complain about missing nullability specifiers on
    // inner pointers.
    complainAboutMissingNullability = CAMN_InnerPointers;

    if (shouldHaveNullability(T) && !T->getNullability()) {
      // Note that we allow but don't require nullability on dependent types.
      ++NumPointersRemaining;
    }

    for (unsigned i = 0, n = D.getNumTypeObjects(); i != n; ++i) {
      DeclaratorChunk &chunk = D.getTypeObject(i);
      switch (chunk.Kind) {
      case DeclaratorChunk::Array:
      case DeclaratorChunk::Function:
      case DeclaratorChunk::Pipe:
        break;

      case DeclaratorChunk::BlockPointer:
      case DeclaratorChunk::MemberPointer:
        ++NumPointersRemaining;
        break;

      case DeclaratorChunk::Paren:
      case DeclaratorChunk::Reference:
        continue;

      case DeclaratorChunk::Pointer:
        ++NumPointersRemaining;
        continue;
      }
    }
  } else {
    bool isFunctionOrMethod = false;
    switch (auto context = state.getDeclarator().getContext()) {
    case DeclaratorContext::ObjCParameter:
    case DeclaratorContext::ObjCResult:
    case DeclaratorContext::Prototype:
    case DeclaratorContext::TrailingReturn:
    case DeclaratorContext::TrailingReturnVar:
      isFunctionOrMethod = true;
      [[fallthrough]];

    case DeclaratorContext::Member:
      if (state.getDeclarator().isObjCIvar() && !isFunctionOrMethod) {
        complainAboutMissingNullability = CAMN_No;
        break;
      }

      // Weak properties are inferred to be nullable.
      if (state.getDeclarator().isObjCWeakProperty()) {
        // Weak properties cannot be nonnull, and should not complain about
        // missing nullable attributes during completeness checks.
        complainAboutMissingNullability = CAMN_No;
        if (inAssumeNonNullRegion) {
          inferNullability = NullabilityKind::Nullable;
        }
        break;
      }

      [[fallthrough]];

    case DeclaratorContext::File:
    case DeclaratorContext::KNRTypeList: {
      complainAboutMissingNullability = CAMN_Yes;

      // Nullability inference depends on the type and declarator.
      auto wrappingKind = PointerWrappingDeclaratorKind::None;
      switch (classifyPointerDeclarator(S, T, D, wrappingKind)) {
      case PointerDeclaratorKind::NonPointer:
      case PointerDeclaratorKind::MultiLevelPointer:
        // Cannot infer nullability.
        break;

      case PointerDeclaratorKind::SingleLevelPointer:
        // Infer _Nonnull if we are in an assumes-nonnull region.
        if (inAssumeNonNullRegion) {
          complainAboutInferringWithinChunk = wrappingKind;
          inferNullability = NullabilityKind::NonNull;
          inferNullabilityCS = (context == DeclaratorContext::ObjCParameter ||
                                context == DeclaratorContext::ObjCResult);
        }
        break;

      case PointerDeclaratorKind::CFErrorRefPointer:
      case PointerDeclaratorKind::NSErrorPointerPointer:
        // Within a function or method signature, infer _Nullable at both
        // levels.
        if (isFunctionOrMethod && inAssumeNonNullRegion)
          inferNullability = NullabilityKind::Nullable;
        break;

      case PointerDeclaratorKind::MaybePointerToCFRef:
        if (isFunctionOrMethod) {
          // On pointer-to-pointer parameters marked cf_returns_retained or
          // cf_returns_not_retained, if the outer pointer is explicit then
          // infer the inner pointer as _Nullable.
          auto hasCFReturnsAttr =
              [](const ParsedAttributesView &AttrList) -> bool {
            return AttrList.hasAttribute(ParsedAttr::AT_CFReturnsRetained) ||
                   AttrList.hasAttribute(ParsedAttr::AT_CFReturnsNotRetained);
          };
          if (const auto *InnermostChunk = D.getInnermostNonParenChunk()) {
            if (hasCFReturnsAttr(D.getDeclarationAttributes()) ||
                hasCFReturnsAttr(D.getAttributes()) ||
                hasCFReturnsAttr(InnermostChunk->getAttrs()) ||
                hasCFReturnsAttr(D.getDeclSpec().getAttributes())) {
              inferNullability = NullabilityKind::Nullable;
              inferNullabilityInnerOnly = true;
            }
          }
        }
        break;
      }
      break;
    }

    case DeclaratorContext::ConversionId:
      complainAboutMissingNullability = CAMN_Yes;
      break;

    case DeclaratorContext::AliasDecl:
    case DeclaratorContext::AliasTemplate:
    case DeclaratorContext::Block:
    case DeclaratorContext::BlockLiteral:
    case DeclaratorContext::Condition:
    case DeclaratorContext::CXXCatch:
    case DeclaratorContext::CXXNew:
    case DeclaratorContext::ForInit:
    case DeclaratorContext::SelectionInit:
    case DeclaratorContext::LambdaExpr:
    case DeclaratorContext::LambdaExprParameter:
    case DeclaratorContext::ObjCCatch:
    case DeclaratorContext::TemplateParam:
    case DeclaratorContext::TemplateArg:
    case DeclaratorContext::TemplateTypeArg:
    case DeclaratorContext::TypeName:
    case DeclaratorContext::FunctionalCast:
    case DeclaratorContext::RequiresExpr:
    case DeclaratorContext::Association:
      // Don't infer in these contexts.
      break;
    }
  }

  // Local function that returns true if its argument looks like a va_list.
  auto isVaList = [&S](QualType T) -> bool {
    auto *typedefTy = T->getAs<TypedefType>();
    if (!typedefTy)
      return false;
    TypedefDecl *vaListTypedef = S.Context.getBuiltinVaListDecl();
    do {
      if (typedefTy->getDecl() == vaListTypedef)
        return true;
      if (auto *name = typedefTy->getDecl()->getIdentifier())
        if (name->isStr("va_list"))
          return true;
      typedefTy = typedefTy->desugar()->getAs<TypedefType>();
    } while (typedefTy);
    return false;
  };

  // Local function that checks the nullability for a given pointer declarator.
  // Returns true if _Nonnull was inferred.
  auto inferPointerNullability =
      [&](SimplePointerKind pointerKind, SourceLocation pointerLoc,
          SourceLocation pointerEndLoc,
          ParsedAttributesView &attrs, AttributePool &Pool) -> ParsedAttr * {
    // We've seen a pointer.
    if (NumPointersRemaining > 0)
      --NumPointersRemaining;

    // If a nullability attribute is present, there's nothing to do.
    if (hasNullabilityAttr(attrs))
      return nullptr;

    // If we're supposed to infer nullability, do so now.
    if (inferNullability && !inferNullabilityInnerOnlyComplete) {
      ParsedAttr::Form form =
          inferNullabilityCS
              ? ParsedAttr::Form::ContextSensitiveKeyword()
              : ParsedAttr::Form::Keyword(false /*IsAlignAs*/,
                                          false /*IsRegularKeywordAttribute*/);
      ParsedAttr *nullabilityAttr = Pool.create(
          S.getNullabilityKeyword(*inferNullability), SourceRange(pointerLoc),
          nullptr, SourceLocation(), nullptr, 0, form);

      attrs.addAtEnd(nullabilityAttr);

      if (inferNullabilityCS) {
        state.getDeclarator().getMutableDeclSpec().getObjCQualifiers()
          ->setObjCDeclQualifier(ObjCDeclSpec::DQ_CSNullability);
      }

      if (pointerLoc.isValid() &&
          complainAboutInferringWithinChunk !=
            PointerWrappingDeclaratorKind::None) {
        auto Diag =
            S.Diag(pointerLoc, diag::warn_nullability_inferred_on_nested_type);
        Diag << static_cast<int>(complainAboutInferringWithinChunk);
        fixItNullability(S, Diag, pointerLoc, NullabilityKind::NonNull);
      }

      if (inferNullabilityInnerOnly)
        inferNullabilityInnerOnlyComplete = true;
      return nullabilityAttr;
    }

    // If we're supposed to complain about missing nullability, do so
    // now if it's truly missing.
    switch (complainAboutMissingNullability) {
    case CAMN_No:
      break;

    case CAMN_InnerPointers:
      if (NumPointersRemaining == 0)
        break;
      [[fallthrough]];

    case CAMN_Yes:
      checkNullabilityConsistency(S, pointerKind, pointerLoc, pointerEndLoc);
    }
    return nullptr;
  };

  // If the type itself could have nullability but does not, infer pointer
  // nullability and perform consistency checking.
  if (S.CodeSynthesisContexts.empty()) {
    if (shouldHaveNullability(T) && !T->getNullability()) {
      if (isVaList(T)) {
        // Record that we've seen a pointer, but do nothing else.
        if (NumPointersRemaining > 0)
          --NumPointersRemaining;
      } else {
        SimplePointerKind pointerKind = SimplePointerKind::Pointer;
        if (T->isBlockPointerType())
          pointerKind = SimplePointerKind::BlockPointer;
        else if (T->isMemberPointerType())
          pointerKind = SimplePointerKind::MemberPointer;

        if (auto *attr = inferPointerNullability(
                pointerKind, D.getDeclSpec().getTypeSpecTypeLoc(),
                D.getDeclSpec().getEndLoc(),
                D.getMutableDeclSpec().getAttributes(),
                D.getMutableDeclSpec().getAttributePool())) {
          T = state.getAttributedType(
              createNullabilityAttr(Context, *attr, *inferNullability), T, T);
        }
      }
    }

    if (complainAboutMissingNullability == CAMN_Yes && T->isArrayType() &&
        !T->getNullability() && !isVaList(T) && D.isPrototypeContext() &&
        !hasOuterPointerLikeChunk(D, D.getNumTypeObjects())) {
      checkNullabilityConsistency(S, SimplePointerKind::Array,
                                  D.getDeclSpec().getTypeSpecTypeLoc());
    }
  }

  bool ExpectNoDerefChunk =
      state.getCurrentAttributes().hasAttribute(ParsedAttr::AT_NoDeref);

  // Walk the DeclTypeInfo, building the recursive type as we go.
  // DeclTypeInfos are ordered from the identifier out, which is
  // opposite of what we want :).

  // Track if the produced type matches the structure of the declarator.
  // This is used later to decide if we can fill `TypeLoc` from
  // `DeclaratorChunk`s. E.g. it must be false if Clang recovers from
  // an error by replacing the type with `int`.
  bool AreDeclaratorChunksValid = true;
  for (unsigned i = 0, e = D.getNumTypeObjects(); i != e; ++i) {
    unsigned chunkIndex = e - i - 1;
    state.setCurrentChunkIndex(chunkIndex);
    DeclaratorChunk &DeclType = D.getTypeObject(chunkIndex);
    IsQualifiedFunction &= DeclType.Kind == DeclaratorChunk::Paren;
    switch (DeclType.Kind) {
    case DeclaratorChunk::Paren:
      if (i == 0)
        warnAboutRedundantParens(S, D, T);
      T = S.BuildParenType(T);
      break;
    case DeclaratorChunk::BlockPointer:
      // If blocks are disabled, emit an error.
      if (!LangOpts.Blocks)
        S.Diag(DeclType.Loc, diag::err_blocks_disable) << LangOpts.OpenCL;

      // Handle pointer nullability.
      inferPointerNullability(SimplePointerKind::BlockPointer, DeclType.Loc,
                              DeclType.EndLoc, DeclType.getAttrs(),
                              state.getDeclarator().getAttributePool());

      T = S.BuildBlockPointerType(T, D.getIdentifierLoc(), Name);
      if (DeclType.Cls.TypeQuals || LangOpts.OpenCL) {
        // OpenCL v2.0, s6.12.5 - Block variable declarations are implicitly
        // qualified with const.
        if (LangOpts.OpenCL)
          DeclType.Cls.TypeQuals |= DeclSpec::TQ_const;
        T = S.BuildQualifiedType(T, DeclType.Loc, DeclType.Cls.TypeQuals);
      }
      break;
    case DeclaratorChunk::Pointer:
      // Verify that we're not building a pointer to pointer to function with
      // exception specification.
      if (LangOpts.CPlusPlus && S.CheckDistantExceptionSpec(T)) {
        S.Diag(D.getIdentifierLoc(), diag::err_distant_exception_spec);
        D.setInvalidType(true);
        // Build the type anyway.
      }

      // Handle pointer nullability
      inferPointerNullability(SimplePointerKind::Pointer, DeclType.Loc,
                              DeclType.EndLoc, DeclType.getAttrs(),
                              state.getDeclarator().getAttributePool());

      if (LangOpts.ObjC && T->getAs<ObjCObjectType>()) {
        T = Context.getObjCObjectPointerType(T);
        if (DeclType.Ptr.TypeQuals)
          T = S.BuildQualifiedType(T, DeclType.Loc, DeclType.Ptr.TypeQuals);
        break;
      }

      // OpenCL v2.0 s6.9b - Pointer to image/sampler cannot be used.
      // OpenCL v2.0 s6.13.16.1 - Pointer to pipe cannot be used.
      // OpenCL v2.0 s6.12.5 - Pointers to Blocks are not allowed.
      if (LangOpts.OpenCL) {
        if (T->isImageType() || T->isSamplerT() || T->isPipeType() ||
            T->isBlockPointerType()) {
          S.Diag(D.getIdentifierLoc(), diag::err_opencl_pointer_to_type) << T;
          D.setInvalidType(true);
        }
      }

      T = S.BuildPointerType(T, DeclType.Loc, Name);
      if (DeclType.Ptr.TypeQuals)
        T = S.BuildQualifiedType(T, DeclType.Loc, DeclType.Ptr.TypeQuals);
      break;
    case DeclaratorChunk::Reference: {
      // Verify that we're not building a reference to pointer to function with
      // exception specification.
      if (LangOpts.CPlusPlus && S.CheckDistantExceptionSpec(T)) {
        S.Diag(D.getIdentifierLoc(), diag::err_distant_exception_spec);
        D.setInvalidType(true);
        // Build the type anyway.
      }
      T = S.BuildReferenceType(T, DeclType.Ref.LValueRef, DeclType.Loc, Name);

      if (DeclType.Ref.HasRestrict)
        T = S.BuildQualifiedType(T, DeclType.Loc, Qualifiers::Restrict);
      break;
    }
    case DeclaratorChunk::Array: {
      // Verify that we're not building an array of pointers to function with
      // exception specification.
      if (LangOpts.CPlusPlus && S.CheckDistantExceptionSpec(T)) {
        S.Diag(D.getIdentifierLoc(), diag::err_distant_exception_spec);
        D.setInvalidType(true);
        // Build the type anyway.
      }
      DeclaratorChunk::ArrayTypeInfo &ATI = DeclType.Arr;
      Expr *ArraySize = static_cast<Expr*>(ATI.NumElts);
      ArraySizeModifier ASM;

      // Microsoft property fields can have multiple sizeless array chunks
      // (i.e. int x[][][]). Skip all of these except one to avoid creating
      // bad incomplete array types.
      if (chunkIndex != 0 && !ArraySize &&
          D.getDeclSpec().getAttributes().hasMSPropertyAttr()) {
        // This is a sizeless chunk. If the next is also, skip this one.
        DeclaratorChunk &NextDeclType = D.getTypeObject(chunkIndex - 1);
        if (NextDeclType.Kind == DeclaratorChunk::Array &&
            !NextDeclType.Arr.NumElts)
          break;
      }

      if (ATI.isStar)
        ASM = ArraySizeModifier::Star;
      else if (ATI.hasStatic)
        ASM = ArraySizeModifier::Static;
      else
        ASM = ArraySizeModifier::Normal;
      if (ASM == ArraySizeModifier::Star && !D.isPrototypeContext()) {
        // FIXME: This check isn't quite right: it allows star in prototypes
        // for function definitions, and disallows some edge cases detailed
        // in http://gcc.gnu.org/ml/gcc-patches/2009-02/msg00133.html
        S.Diag(DeclType.Loc, diag::err_array_star_outside_prototype);
        ASM = ArraySizeModifier::Normal;
        D.setInvalidType(true);
      }

      // C99 6.7.5.2p1: The optional type qualifiers and the keyword static
      // shall appear only in a declaration of a function parameter with an
      // array type, ...
      if (ASM == ArraySizeModifier::Static || ATI.TypeQuals) {
        if (!(D.isPrototypeContext() ||
              D.getContext() == DeclaratorContext::KNRTypeList)) {
          S.Diag(DeclType.Loc, diag::err_array_static_outside_prototype)
              << (ASM == ArraySizeModifier::Static ? "'static'"
                                                   : "type qualifier");
          // Remove the 'static' and the type qualifiers.
          if (ASM == ArraySizeModifier::Static)
            ASM = ArraySizeModifier::Normal;
          ATI.TypeQuals = 0;
          D.setInvalidType(true);
        }

        // C99 6.7.5.2p1: ... and then only in the outermost array type
        // derivation.
        if (hasOuterPointerLikeChunk(D, chunkIndex)) {
          S.Diag(DeclType.Loc, diag::err_array_static_not_outermost)
              << (ASM == ArraySizeModifier::Static ? "'static'"
                                                   : "type qualifier");
          if (ASM == ArraySizeModifier::Static)
            ASM = ArraySizeModifier::Normal;
          ATI.TypeQuals = 0;
          D.setInvalidType(true);
        }
      }

      // Array parameters can be marked nullable as well, although it's not
      // necessary if they're marked 'static'.
      if (complainAboutMissingNullability == CAMN_Yes &&
          !hasNullabilityAttr(DeclType.getAttrs()) &&
          ASM != ArraySizeModifier::Static && D.isPrototypeContext() &&
          !hasOuterPointerLikeChunk(D, chunkIndex)) {
        checkNullabilityConsistency(S, SimplePointerKind::Array, DeclType.Loc);
      }

      T = S.BuildArrayType(T, ASM, ArraySize, ATI.TypeQuals,
                           SourceRange(DeclType.Loc, DeclType.EndLoc), Name);
      break;
    }
    case DeclaratorChunk::Function: {
      // If the function declarator has a prototype (i.e. it is not () and
      // does not have a K&R-style identifier list), then the arguments are part
      // of the type, otherwise the argument list is ().
      DeclaratorChunk::FunctionTypeInfo &FTI = DeclType.Fun;
      IsQualifiedFunction =
          FTI.hasMethodTypeQualifiers() || FTI.hasRefQualifier();

      // Check for auto functions and trailing return type and adjust the
      // return type accordingly.
      if (!D.isInvalidType()) {
        auto IsClassType = [&](CXXScopeSpec &SS) {
          // If there already was an problem with the scope, don’t issue another
          // error about the explicit object parameter.
          return SS.isInvalid() ||
                 isa_and_present<CXXRecordDecl>(S.computeDeclContext(SS));
        };

        // C++23 [dcl.fct]p6:
        //
        // An explicit-object-parameter-declaration is a parameter-declaration
        // with a this specifier. An explicit-object-parameter-declaration shall
        // appear only as the first parameter-declaration of a
        // parameter-declaration-list of one of:
        //
        // - a declaration of a member function or member function template
        //   ([class.mem]), or
        //
        // - an explicit instantiation ([temp.explicit]) or explicit
        //   specialization ([temp.expl.spec]) of a templated member function,
        //   or
        //
        // - a lambda-declarator [expr.prim.lambda].
        DeclaratorContext C = D.getContext();
        ParmVarDecl *First =
            FTI.NumParams
                ? dyn_cast_if_present<ParmVarDecl>(FTI.Params[0].Param)
                : nullptr;

        bool IsFunctionDecl = D.getInnermostNonParenChunk() == &DeclType;
        if (First && First->isExplicitObjectParameter() &&
            C != DeclaratorContext::LambdaExpr &&

            // Either not a member or nested declarator in a member.
            //
            // Note that e.g. 'static' or 'friend' declarations are accepted
            // here; we diagnose them later when we build the member function
            // because it's easier that way.
            (C != DeclaratorContext::Member || !IsFunctionDecl) &&

            // Allow out-of-line definitions of member functions.
            !IsClassType(D.getCXXScopeSpec())) {
          if (IsFunctionDecl)
            S.Diag(First->getBeginLoc(),
                   diag::err_explicit_object_parameter_nonmember)
                << /*non-member*/ 2 << /*function*/ 0
                << First->getSourceRange();
          else
            S.Diag(First->getBeginLoc(),
                   diag::err_explicit_object_parameter_invalid)
                << First->getSourceRange();

          D.setInvalidType();
          AreDeclaratorChunksValid = false;
        }

        // trailing-return-type is only required if we're declaring a function,
        // and not, for instance, a pointer to a function.
        if (D.getDeclSpec().hasAutoTypeSpec() &&
            !FTI.hasTrailingReturnType() && chunkIndex == 0) {
          if (!S.getLangOpts().CPlusPlus14) {
            S.Diag(D.getDeclSpec().getTypeSpecTypeLoc(),
                   D.getDeclSpec().getTypeSpecType() == DeclSpec::TST_auto
                       ? diag::err_auto_missing_trailing_return
                       : diag::err_deduced_return_type);
            T = Context.IntTy;
            D.setInvalidType(true);
            AreDeclaratorChunksValid = false;
          } else {
            S.Diag(D.getDeclSpec().getTypeSpecTypeLoc(),
                   diag::warn_cxx11_compat_deduced_return_type);
          }
        } else if (FTI.hasTrailingReturnType()) {
          // T must be exactly 'auto' at this point. See CWG issue 681.
          if (isa<ParenType>(T)) {
            S.Diag(D.getBeginLoc(), diag::err_trailing_return_in_parens)
                << T << D.getSourceRange();
            D.setInvalidType(true);
            // FIXME: recover and fill decls in `TypeLoc`s.
            AreDeclaratorChunksValid = false;
          } else if (D.getName().getKind() ==
                     UnqualifiedIdKind::IK_DeductionGuideName) {
            if (T != Context.DependentTy) {
              S.Diag(D.getDeclSpec().getBeginLoc(),
                     diag::err_deduction_guide_with_complex_decl)
                  << D.getSourceRange();
              D.setInvalidType(true);
              // FIXME: recover and fill decls in `TypeLoc`s.
              AreDeclaratorChunksValid = false;
            }
          } else if (D.getContext() != DeclaratorContext::LambdaExpr &&
                     (T.hasQualifiers() || !isa<AutoType>(T) ||
                      cast<AutoType>(T)->getKeyword() !=
                          AutoTypeKeyword::Auto ||
                      cast<AutoType>(T)->isConstrained())) {
            S.Diag(D.getDeclSpec().getTypeSpecTypeLoc(),
                   diag::err_trailing_return_without_auto)
                << T << D.getDeclSpec().getSourceRange();
            D.setInvalidType(true);
            // FIXME: recover and fill decls in `TypeLoc`s.
            AreDeclaratorChunksValid = false;
          }
          T = S.GetTypeFromParser(FTI.getTrailingReturnType(), &TInfo);
          if (T.isNull()) {
            // An error occurred parsing the trailing return type.
            T = Context.IntTy;
            D.setInvalidType(true);
          } else if (AutoType *Auto = T->getContainedAutoType()) {
            // If the trailing return type contains an `auto`, we may need to
            // invent a template parameter for it, for cases like
            // `auto f() -> C auto` or `[](auto (*p) -> auto) {}`.
            InventedTemplateParameterInfo *InventedParamInfo = nullptr;
            if (D.getContext() == DeclaratorContext::Prototype)
              InventedParamInfo = &S.InventedParameterInfos.back();
            else if (D.getContext() == DeclaratorContext::LambdaExprParameter)
              InventedParamInfo = S.getCurLambda();
            if (InventedParamInfo) {
              std::tie(T, TInfo) = InventTemplateParameter(
                  state, T, TInfo, Auto, *InventedParamInfo);
            }
          }
        } else {
          // This function type is not the type of the entity being declared,
          // so checking the 'auto' is not the responsibility of this chunk.
        }
      }

      // C99 6.7.5.3p1: The return type may not be a function or array type.
      // For conversion functions, we'll diagnose this particular error later.
      if (!D.isInvalidType() && (T->isArrayType() || T->isFunctionType()) &&
          (D.getName().getKind() !=
           UnqualifiedIdKind::IK_ConversionFunctionId)) {
        unsigned diagID = diag::err_func_returning_array_function;
        // Last processing chunk in block context means this function chunk
        // represents the block.
        if (chunkIndex == 0 &&
            D.getContext() == DeclaratorContext::BlockLiteral)
          diagID = diag::err_block_returning_array_function;
        S.Diag(DeclType.Loc, diagID) << T->isFunctionType() << T;
        T = Context.IntTy;
        D.setInvalidType(true);
        AreDeclaratorChunksValid = false;
      }

      // Do not allow returning half FP value.
      // FIXME: This really should be in BuildFunctionType.
      if (T->isHalfType()) {
        if (S.getLangOpts().OpenCL) {
          if (!S.getOpenCLOptions().isAvailableOption("cl_khr_fp16",
                                                      S.getLangOpts())) {
            S.Diag(D.getIdentifierLoc(), diag::err_opencl_invalid_return)
                << T << 0 /*pointer hint*/;
            D.setInvalidType(true);
          }
        } else if (!S.getLangOpts().NativeHalfArgsAndReturns &&
                   !S.Context.getTargetInfo().allowHalfArgsAndReturns()) {
          S.Diag(D.getIdentifierLoc(),
            diag::err_parameters_retval_cannot_have_fp16_type) << 1;
          D.setInvalidType(true);
        }
      }

      if (LangOpts.OpenCL) {
        // OpenCL v2.0 s6.12.5 - A block cannot be the return value of a
        // function.
        if (T->isBlockPointerType() || T->isImageType() || T->isSamplerT() ||
            T->isPipeType()) {
          S.Diag(D.getIdentifierLoc(), diag::err_opencl_invalid_return)
              << T << 1 /*hint off*/;
          D.setInvalidType(true);
        }
        // OpenCL doesn't support variadic functions and blocks
        // (s6.9.e and s6.12.5 OpenCL v2.0) except for printf.
        // We also allow here any toolchain reserved identifiers.
        if (FTI.isVariadic &&
            !S.getOpenCLOptions().isAvailableOption(
                "__cl_clang_variadic_functions", S.getLangOpts()) &&
            !(D.getIdentifier() &&
              ((D.getIdentifier()->getName() == "printf" &&
                LangOpts.getOpenCLCompatibleVersion() >= 120) ||
               D.getIdentifier()->getName().starts_with("__")))) {
          S.Diag(D.getIdentifierLoc(), diag::err_opencl_variadic_function);
          D.setInvalidType(true);
        }
      }

      // Methods cannot return interface types. All ObjC objects are
      // passed by reference.
      if (T->isObjCObjectType()) {
        SourceLocation DiagLoc, FixitLoc;
        if (TInfo) {
          DiagLoc = TInfo->getTypeLoc().getBeginLoc();
          FixitLoc = S.getLocForEndOfToken(TInfo->getTypeLoc().getEndLoc());
        } else {
          DiagLoc = D.getDeclSpec().getTypeSpecTypeLoc();
          FixitLoc = S.getLocForEndOfToken(D.getDeclSpec().getEndLoc());
        }
        S.Diag(DiagLoc, diag::err_object_cannot_be_passed_returned_by_value)
          << 0 << T
          << FixItHint::CreateInsertion(FixitLoc, "*");

        T = Context.getObjCObjectPointerType(T);
        if (TInfo) {
          TypeLocBuilder TLB;
          TLB.pushFullCopy(TInfo->getTypeLoc());
          ObjCObjectPointerTypeLoc TLoc = TLB.push<ObjCObjectPointerTypeLoc>(T);
          TLoc.setStarLoc(FixitLoc);
          TInfo = TLB.getTypeSourceInfo(Context, T);
        } else {
          AreDeclaratorChunksValid = false;
        }

        D.setInvalidType(true);
      }

      // cv-qualifiers on return types are pointless except when the type is a
      // class type in C++.
      if ((T.getCVRQualifiers() || T->isAtomicType()) &&
          !(S.getLangOpts().CPlusPlus &&
            (T->isDependentType() || T->isRecordType()))) {
        if (T->isVoidType() && !S.getLangOpts().CPlusPlus &&
            D.getFunctionDefinitionKind() ==
                FunctionDefinitionKind::Definition) {
          // [6.9.1/3] qualified void return is invalid on a C
          // function definition.  Apparently ok on declarations and
          // in C++ though (!)
          S.Diag(DeclType.Loc, diag::err_func_returning_qualified_void) << T;
        } else
          diagnoseRedundantReturnTypeQualifiers(S, T, D, chunkIndex);

        // C++2a [dcl.fct]p12:
        //   A volatile-qualified return type is deprecated
        if (T.isVolatileQualified() && S.getLangOpts().CPlusPlus20)
          S.Diag(DeclType.Loc, diag::warn_deprecated_volatile_return) << T;
      }

      // Objective-C ARC ownership qualifiers are ignored on the function
      // return type (by type canonicalization). Complain if this attribute
      // was written here.
      if (T.getQualifiers().hasObjCLifetime()) {
        SourceLocation AttrLoc;
        if (chunkIndex + 1 < D.getNumTypeObjects()) {
          DeclaratorChunk ReturnTypeChunk = D.getTypeObject(chunkIndex + 1);
          for (const ParsedAttr &AL : ReturnTypeChunk.getAttrs()) {
            if (AL.getKind() == ParsedAttr::AT_ObjCOwnership) {
              AttrLoc = AL.getLoc();
              break;
            }
          }
        }
        if (AttrLoc.isInvalid()) {
          for (const ParsedAttr &AL : D.getDeclSpec().getAttributes()) {
            if (AL.getKind() == ParsedAttr::AT_ObjCOwnership) {
              AttrLoc = AL.getLoc();
              break;
            }
          }
        }

        if (AttrLoc.isValid()) {
          // The ownership attributes are almost always written via
          // the predefined
          // __strong/__weak/__autoreleasing/__unsafe_unretained.
          if (AttrLoc.isMacroID())
            AttrLoc =
                S.SourceMgr.getImmediateExpansionRange(AttrLoc).getBegin();

          S.Diag(AttrLoc, diag::warn_arc_lifetime_result_type)
            << T.getQualifiers().getObjCLifetime();
        }
      }

      if (LangOpts.CPlusPlus && D.getDeclSpec().hasTagDefinition()) {
        // C++ [dcl.fct]p6:
        //   Types shall not be defined in return or parameter types.
        TagDecl *Tag = cast<TagDecl>(D.getDeclSpec().getRepAsDecl());
        S.Diag(Tag->getLocation(), diag::err_type_defined_in_result_type)
          << Context.getTypeDeclType(Tag);
      }

      // Exception specs are not allowed in typedefs. Complain, but add it
      // anyway.
      if (IsTypedefName && FTI.getExceptionSpecType() && !LangOpts.CPlusPlus17)
        S.Diag(FTI.getExceptionSpecLocBeg(),
               diag::err_exception_spec_in_typedef)
            << (D.getContext() == DeclaratorContext::AliasDecl ||
                D.getContext() == DeclaratorContext::AliasTemplate);

      // If we see "T var();" or "T var(T());" at block scope, it is probably
      // an attempt to initialize a variable, not a function declaration.
      if (FTI.isAmbiguous)
        warnAboutAmbiguousFunction(S, D, DeclType, T);

      FunctionType::ExtInfo EI(
          getCCForDeclaratorChunk(S, D, DeclType.getAttrs(), FTI, chunkIndex));

      // OpenCL disallows functions without a prototype, but it doesn't enforce
      // strict prototypes as in C23 because it allows a function definition to
      // have an identifier list. See OpenCL 3.0 6.11/g for more details.
      if (!FTI.NumParams && !FTI.isVariadic &&
          !LangOpts.requiresStrictPrototypes() && !LangOpts.OpenCL) {
        // Simple void foo(), where the incoming T is the result type.
        T = Context.getFunctionNoProtoType(T, EI);
      } else {
        // We allow a zero-parameter variadic function in C if the
        // function is marked with the "overloadable" attribute. Scan
        // for this attribute now. We also allow it in C23 per WG14 N2975.
        if (!FTI.NumParams && FTI.isVariadic && !LangOpts.CPlusPlus) {
          if (LangOpts.C23)
            S.Diag(FTI.getEllipsisLoc(),
                   diag::warn_c17_compat_ellipsis_only_parameter);
          else if (!D.getDeclarationAttributes().hasAttribute(
                       ParsedAttr::AT_Overloadable) &&
                   !D.getAttributes().hasAttribute(
                       ParsedAttr::AT_Overloadable) &&
                   !D.getDeclSpec().getAttributes().hasAttribute(
                       ParsedAttr::AT_Overloadable))
            S.Diag(FTI.getEllipsisLoc(), diag::err_ellipsis_first_param);
        }

        if (FTI.NumParams && FTI.Params[0].Param == nullptr) {
          // C99 6.7.5.3p3: Reject int(x,y,z) when it's not a function
          // definition.
          S.Diag(FTI.Params[0].IdentLoc,
                 diag::err_ident_list_in_fn_declaration);
          D.setInvalidType(true);
          // Recover by creating a K&R-style function type, if possible.
          T = (!LangOpts.requiresStrictPrototypes() && !LangOpts.OpenCL)
                  ? Context.getFunctionNoProtoType(T, EI)
                  : Context.IntTy;
          AreDeclaratorChunksValid = false;
          break;
        }

        FunctionProtoType::ExtProtoInfo EPI;
        EPI.ExtInfo = EI;
        EPI.Variadic = FTI.isVariadic;
        EPI.EllipsisLoc = FTI.getEllipsisLoc();
        EPI.HasTrailingReturn = FTI.hasTrailingReturnType();
        EPI.TypeQuals.addCVRUQualifiers(
            FTI.MethodQualifiers ? FTI.MethodQualifiers->getTypeQualifiers()
                                 : 0);
        EPI.RefQualifier = !FTI.hasRefQualifier()? RQ_None
                    : FTI.RefQualifierIsLValueRef? RQ_LValue
                    : RQ_RValue;

        // Otherwise, we have a function with a parameter list that is
        // potentially variadic.
        SmallVector<QualType, 16> ParamTys;
        ParamTys.reserve(FTI.NumParams);

        SmallVector<FunctionProtoType::ExtParameterInfo, 16>
          ExtParameterInfos(FTI.NumParams);
        bool HasAnyInterestingExtParameterInfos = false;

        for (unsigned i = 0, e = FTI.NumParams; i != e; ++i) {
          ParmVarDecl *Param = cast<ParmVarDecl>(FTI.Params[i].Param);
          QualType ParamTy = Param->getType();
          assert(!ParamTy.isNull() && "Couldn't parse type?");

          // Look for 'void'.  void is allowed only as a single parameter to a
          // function with no other parameters (C99 6.7.5.3p10).  We record
          // int(void) as a FunctionProtoType with an empty parameter list.
          if (ParamTy->isVoidType()) {
            // If this is something like 'float(int, void)', reject it.  'void'
            // is an incomplete type (C99 6.2.5p19) and function decls cannot
            // have parameters of incomplete type.
            if (FTI.NumParams != 1 || FTI.isVariadic) {
              S.Diag(FTI.Params[i].IdentLoc, diag::err_void_only_param);
              ParamTy = Context.IntTy;
              Param->setType(ParamTy);
            } else if (FTI.Params[i].Ident) {
              // Reject, but continue to parse 'int(void abc)'.
              S.Diag(FTI.Params[i].IdentLoc, diag::err_param_with_void_type);
              ParamTy = Context.IntTy;
              Param->setType(ParamTy);
            } else {
              // Reject, but continue to parse 'float(const void)'.
              if (ParamTy.hasQualifiers())
                S.Diag(DeclType.Loc, diag::err_void_param_qualified);

              // Do not add 'void' to the list.
              break;
            }
          } else if (ParamTy->isHalfType()) {
            // Disallow half FP parameters.
            // FIXME: This really should be in BuildFunctionType.
            if (S.getLangOpts().OpenCL) {
              if (!S.getOpenCLOptions().isAvailableOption("cl_khr_fp16",
                                                          S.getLangOpts())) {
                S.Diag(Param->getLocation(), diag::err_opencl_invalid_param)
                    << ParamTy << 0;
                D.setInvalidType();
                Param->setInvalidDecl();
              }
            } else if (!S.getLangOpts().NativeHalfArgsAndReturns &&
                       !S.Context.getTargetInfo().allowHalfArgsAndReturns()) {
              S.Diag(Param->getLocation(),
                diag::err_parameters_retval_cannot_have_fp16_type) << 0;
              D.setInvalidType();
            }
          } else if (!FTI.hasPrototype) {
            if (Context.isPromotableIntegerType(ParamTy)) {
              ParamTy = Context.getPromotedIntegerType(ParamTy);
              Param->setKNRPromoted(true);
            } else if (const BuiltinType *BTy = ParamTy->getAs<BuiltinType>()) {
              if (BTy->getKind() == BuiltinType::Float) {
                ParamTy = Context.DoubleTy;
                Param->setKNRPromoted(true);
              }
            }
          } else if (S.getLangOpts().OpenCL && ParamTy->isBlockPointerType()) {
            // OpenCL 2.0 s6.12.5: A block cannot be a parameter of a function.
            S.Diag(Param->getLocation(), diag::err_opencl_invalid_param)
                << ParamTy << 1 /*hint off*/;
            D.setInvalidType();
          }

          if (LangOpts.ObjCAutoRefCount && Param->hasAttr<NSConsumedAttr>()) {
            ExtParameterInfos[i] = ExtParameterInfos[i].withIsConsumed(true);
            HasAnyInterestingExtParameterInfos = true;
          }

          if (auto attr = Param->getAttr<ParameterABIAttr>()) {
            ExtParameterInfos[i] =
              ExtParameterInfos[i].withABI(attr->getABI());
            HasAnyInterestingExtParameterInfos = true;
          }

          if (Param->hasAttr<PassObjectSizeAttr>()) {
            ExtParameterInfos[i] = ExtParameterInfos[i].withHasPassObjectSize();
            HasAnyInterestingExtParameterInfos = true;
          }

          if (Param->hasAttr<NoEscapeAttr>()) {
            ExtParameterInfos[i] = ExtParameterInfos[i].withIsNoEscape(true);
            HasAnyInterestingExtParameterInfos = true;
          }

          ParamTys.push_back(ParamTy);
        }

        if (HasAnyInterestingExtParameterInfos) {
          EPI.ExtParameterInfos = ExtParameterInfos.data();
          checkExtParameterInfos(S, ParamTys, EPI,
              [&](unsigned i) { return FTI.Params[i].Param->getLocation(); });
        }

        SmallVector<QualType, 4> Exceptions;
        SmallVector<ParsedType, 2> DynamicExceptions;
        SmallVector<SourceRange, 2> DynamicExceptionRanges;
        Expr *NoexceptExpr = nullptr;

        if (FTI.getExceptionSpecType() == EST_Dynamic) {
          // FIXME: It's rather inefficient to have to split into two vectors
          // here.
          unsigned N = FTI.getNumExceptions();
          DynamicExceptions.reserve(N);
          DynamicExceptionRanges.reserve(N);
          for (unsigned I = 0; I != N; ++I) {
            DynamicExceptions.push_back(FTI.Exceptions[I].Ty);
            DynamicExceptionRanges.push_back(FTI.Exceptions[I].Range);
          }
        } else if (isComputedNoexcept(FTI.getExceptionSpecType())) {
          NoexceptExpr = FTI.NoexceptExpr;
        }

        S.checkExceptionSpecification(D.isFunctionDeclarationContext(),
                                      FTI.getExceptionSpecType(),
                                      DynamicExceptions,
                                      DynamicExceptionRanges,
                                      NoexceptExpr,
                                      Exceptions,
                                      EPI.ExceptionSpec);

        // FIXME: Set address space from attrs for C++ mode here.
        // OpenCLCPlusPlus: A class member function has an address space.
        auto IsClassMember = [&]() {
          return (!state.getDeclarator().getCXXScopeSpec().isEmpty() &&
                  state.getDeclarator()
                          .getCXXScopeSpec()
                          .getScopeRep()
                          ->getKind() == NestedNameSpecifier::TypeSpec) ||
                 state.getDeclarator().getContext() ==
                     DeclaratorContext::Member ||
                 state.getDeclarator().getContext() ==
                     DeclaratorContext::LambdaExpr;
        };

        if (state.getSema().getLangOpts().OpenCLCPlusPlus && IsClassMember()) {
          LangAS ASIdx = LangAS::Default;
          // Take address space attr if any and mark as invalid to avoid adding
          // them later while creating QualType.
          if (FTI.MethodQualifiers)
            for (ParsedAttr &attr : FTI.MethodQualifiers->getAttributes()) {
              LangAS ASIdxNew = attr.asOpenCLLangAS();
              if (DiagnoseMultipleAddrSpaceAttributes(S, ASIdx, ASIdxNew,
                                                      attr.getLoc()))
                D.setInvalidType(true);
              else
                ASIdx = ASIdxNew;
            }
          // If a class member function's address space is not set, set it to
          // __generic.
          LangAS AS =
              (ASIdx == LangAS::Default ? S.getDefaultCXXMethodAddrSpace()
                                        : ASIdx);
          EPI.TypeQuals.addAddressSpace(AS);
        }
        T = Context.getFunctionType(T, ParamTys, EPI);
      }
      break;
    }
    case DeclaratorChunk::MemberPointer: {
      // The scope spec must refer to a class, or be dependent.
      CXXScopeSpec &SS = DeclType.Mem.Scope();
      QualType ClsType;

      // Handle pointer nullability.
      inferPointerNullability(SimplePointerKind::MemberPointer, DeclType.Loc,
                              DeclType.EndLoc, DeclType.getAttrs(),
                              state.getDeclarator().getAttributePool());

      if (SS.isInvalid()) {
        // Avoid emitting extra errors if we already errored on the scope.
        D.setInvalidType(true);
      } else if (S.isDependentScopeSpecifier(SS) ||
                 isa_and_nonnull<CXXRecordDecl>(S.computeDeclContext(SS))) {
        NestedNameSpecifier *NNS = SS.getScopeRep();
        NestedNameSpecifier *NNSPrefix = NNS->getPrefix();
        switch (NNS->getKind()) {
        case NestedNameSpecifier::Identifier:
          ClsType = Context.getDependentNameType(
              ElaboratedTypeKeyword::None, NNSPrefix, NNS->getAsIdentifier());
          break;

        case NestedNameSpecifier::Namespace:
        case NestedNameSpecifier::NamespaceAlias:
        case NestedNameSpecifier::Global:
        case NestedNameSpecifier::Super:
          llvm_unreachable("Nested-name-specifier must name a type");

        case NestedNameSpecifier::TypeSpec:
        case NestedNameSpecifier::TypeSpecWithTemplate:
          ClsType = QualType(NNS->getAsType(), 0);
          // Note: if the NNS has a prefix and ClsType is a nondependent
          // TemplateSpecializationType, then the NNS prefix is NOT included
          // in ClsType; hence we wrap ClsType into an ElaboratedType.
          // NOTE: in particular, no wrap occurs if ClsType already is an
          // Elaborated, DependentName, or DependentTemplateSpecialization.
          if (isa<TemplateSpecializationType>(NNS->getAsType()))
            ClsType = Context.getElaboratedType(ElaboratedTypeKeyword::None,
                                                NNSPrefix, ClsType);
          break;
        }
      } else {
        S.Diag(DeclType.Mem.Scope().getBeginLoc(),
             diag::err_illegal_decl_mempointer_in_nonclass)
          << (D.getIdentifier() ? D.getIdentifier()->getName() : "type name")
          << DeclType.Mem.Scope().getRange();
        D.setInvalidType(true);
      }

      if (!ClsType.isNull())
        T = S.BuildMemberPointerType(T, ClsType, DeclType.Loc,
                                     D.getIdentifier());
      else
        AreDeclaratorChunksValid = false;

      if (T.isNull()) {
        T = Context.IntTy;
        D.setInvalidType(true);
        AreDeclaratorChunksValid = false;
      } else if (DeclType.Mem.TypeQuals) {
        T = S.BuildQualifiedType(T, DeclType.Loc, DeclType.Mem.TypeQuals);
      }
      break;
    }

    case DeclaratorChunk::Pipe: {
      T = S.BuildReadPipeType(T, DeclType.Loc);
      processTypeAttrs(state, T, TAL_DeclSpec,
                       D.getMutableDeclSpec().getAttributes());
      break;
    }
    }

    if (T.isNull()) {
      D.setInvalidType(true);
      T = Context.IntTy;
      AreDeclaratorChunksValid = false;
    }

    // See if there are any attributes on this declarator chunk.
    processTypeAttrs(state, T, TAL_DeclChunk, DeclType.getAttrs(),
                     S.CUDA().IdentifyTarget(D.getAttributes()));

    if (DeclType.Kind != DeclaratorChunk::Paren) {
      if (ExpectNoDerefChunk && !IsNoDerefableChunk(DeclType))
        S.Diag(DeclType.Loc, diag::warn_noderef_on_non_pointer_or_array);

      ExpectNoDerefChunk = state.didParseNoDeref();
    }
  }

  if (ExpectNoDerefChunk)
    S.Diag(state.getDeclarator().getBeginLoc(),
           diag::warn_noderef_on_non_pointer_or_array);

  // GNU warning -Wstrict-prototypes
  //   Warn if a function declaration or definition is without a prototype.
  //   This warning is issued for all kinds of unprototyped function
  //   declarations (i.e. function type typedef, function pointer etc.)
  //   C99 6.7.5.3p14:
  //   The empty list in a function declarator that is not part of a definition
  //   of that function specifies that no information about the number or types
  //   of the parameters is supplied.
  // See ActOnFinishFunctionBody() and MergeFunctionDecl() for handling of
  // function declarations whose behavior changes in C23.
  if (!LangOpts.requiresStrictPrototypes()) {
    bool IsBlock = false;
    for (const DeclaratorChunk &DeclType : D.type_objects()) {
      switch (DeclType.Kind) {
      case DeclaratorChunk::BlockPointer:
        IsBlock = true;
        break;
      case DeclaratorChunk::Function: {
        const DeclaratorChunk::FunctionTypeInfo &FTI = DeclType.Fun;
        // We suppress the warning when there's no LParen location, as this
        // indicates the declaration was an implicit declaration, which gets
        // warned about separately via -Wimplicit-function-declaration. We also
        // suppress the warning when we know the function has a prototype.
        if (!FTI.hasPrototype && FTI.NumParams == 0 && !FTI.isVariadic &&
            FTI.getLParenLoc().isValid())
          S.Diag(DeclType.Loc, diag::warn_strict_prototypes)
              << IsBlock
              << FixItHint::CreateInsertion(FTI.getRParenLoc(), "void");
        IsBlock = false;
        break;
      }
      default:
        break;
      }
    }
  }

  assert(!T.isNull() && "T must not be null after this point");

  if (LangOpts.CPlusPlus && T->isFunctionType()) {
    const FunctionProtoType *FnTy = T->getAs<FunctionProtoType>();
    assert(FnTy && "Why oh why is there not a FunctionProtoType here?");

    // C++ 8.3.5p4:
    //   A cv-qualifier-seq shall only be part of the function type
    //   for a nonstatic member function, the function type to which a pointer
    //   to member refers, or the top-level function type of a function typedef
    //   declaration.
    //
    // Core issue 547 also allows cv-qualifiers on function types that are
    // top-level template type arguments.
    enum {
      NonMember,
      Member,
      ExplicitObjectMember,
      DeductionGuide
    } Kind = NonMember;
    if (D.getName().getKind() == UnqualifiedIdKind::IK_DeductionGuideName)
      Kind = DeductionGuide;
    else if (!D.getCXXScopeSpec().isSet()) {
      if ((D.getContext() == DeclaratorContext::Member ||
           D.getContext() == DeclaratorContext::LambdaExpr) &&
          !D.getDeclSpec().isFriendSpecified())
        Kind = Member;
    } else {
      DeclContext *DC = S.computeDeclContext(D.getCXXScopeSpec());
      if (!DC || DC->isRecord())
        Kind = Member;
    }

    if (Kind == Member) {
      unsigned I;
      if (D.isFunctionDeclarator(I)) {
        const DeclaratorChunk &Chunk = D.getTypeObject(I);
        if (Chunk.Fun.NumParams) {
          auto *P = dyn_cast_or_null<ParmVarDecl>(Chunk.Fun.Params->Param);
          if (P && P->isExplicitObjectParameter())
            Kind = ExplicitObjectMember;
        }
      }
    }

    // C++11 [dcl.fct]p6 (w/DR1417):
    // An attempt to specify a function type with a cv-qualifier-seq or a
    // ref-qualifier (including by typedef-name) is ill-formed unless it is:
    //  - the function type for a non-static member function,
    //  - the function type to which a pointer to member refers,
    //  - the top-level function type of a function typedef declaration or
    //    alias-declaration,
    //  - the type-id in the default argument of a type-parameter, or
    //  - the type-id of a template-argument for a type-parameter
    //
    // C++23 [dcl.fct]p6 (P0847R7)
    // ... A member-declarator with an explicit-object-parameter-declaration
    // shall not include a ref-qualifier or a cv-qualifier-seq and shall not be
    // declared static or virtual ...
    //
    // FIXME: Checking this here is insufficient. We accept-invalid on:
    //
    //   template<typename T> struct S { void f(T); };
    //   S<int() const> s;
    //
    // ... for instance.
    if (IsQualifiedFunction &&
        // Check for non-static member function and not and
        // explicit-object-parameter-declaration
        (Kind != Member || D.isExplicitObjectMemberFunction() ||
         D.getDeclSpec().getStorageClassSpec() == DeclSpec::SCS_static ||
         (D.getContext() == clang::DeclaratorContext::Member &&
          D.isStaticMember())) &&
        !IsTypedefName && D.getContext() != DeclaratorContext::TemplateArg &&
        D.getContext() != DeclaratorContext::TemplateTypeArg) {
      SourceLocation Loc = D.getBeginLoc();
      SourceRange RemovalRange;
      unsigned I;
      if (D.isFunctionDeclarator(I)) {
        SmallVector<SourceLocation, 4> RemovalLocs;
        const DeclaratorChunk &Chunk = D.getTypeObject(I);
        assert(Chunk.Kind == DeclaratorChunk::Function);

        if (Chunk.Fun.hasRefQualifier())
          RemovalLocs.push_back(Chunk.Fun.getRefQualifierLoc());

        if (Chunk.Fun.hasMethodTypeQualifiers())
          Chunk.Fun.MethodQualifiers->forEachQualifier(
              [&](DeclSpec::TQ TypeQual, StringRef QualName,
                  SourceLocation SL) { RemovalLocs.push_back(SL); });

        if (!RemovalLocs.empty()) {
          llvm::sort(RemovalLocs,
                     BeforeThanCompare<SourceLocation>(S.getSourceManager()));
          RemovalRange = SourceRange(RemovalLocs.front(), RemovalLocs.back());
          Loc = RemovalLocs.front();
        }
      }

      S.Diag(Loc, diag::err_invalid_qualified_function_type)
        << Kind << D.isFunctionDeclarator() << T
        << getFunctionQualifiersAsString(FnTy)
        << FixItHint::CreateRemoval(RemovalRange);

      // Strip the cv-qualifiers and ref-qualifiers from the type.
      FunctionProtoType::ExtProtoInfo EPI = FnTy->getExtProtoInfo();
      EPI.TypeQuals.removeCVRQualifiers();
      EPI.RefQualifier = RQ_None;

      T = Context.getFunctionType(FnTy->getReturnType(), FnTy->getParamTypes(),
                                  EPI);
      // Rebuild any parens around the identifier in the function type.
      for (unsigned i = 0, e = D.getNumTypeObjects(); i != e; ++i) {
        if (D.getTypeObject(i).Kind != DeclaratorChunk::Paren)
          break;
        T = S.BuildParenType(T);
      }
    }
  }

  // Apply any undistributed attributes from the declaration or declarator.
  ParsedAttributesView NonSlidingAttrs;
  for (ParsedAttr &AL : D.getDeclarationAttributes()) {
    if (!AL.slidesFromDeclToDeclSpecLegacyBehavior()) {
      NonSlidingAttrs.addAtEnd(&AL);
    }
  }
  processTypeAttrs(state, T, TAL_DeclName, NonSlidingAttrs);
  processTypeAttrs(state, T, TAL_DeclName, D.getAttributes());

  // Diagnose any ignored type attributes.
  state.diagnoseIgnoredTypeAttrs(T);

  // C++0x [dcl.constexpr]p9:
  //  A constexpr specifier used in an object declaration declares the object
  //  as const.
  if (D.getDeclSpec().getConstexprSpecifier() == ConstexprSpecKind::Constexpr &&
      T->isObjectType())
    T.addConst();

  // C++2a [dcl.fct]p4:
  //   A parameter with volatile-qualified type is deprecated
  if (T.isVolatileQualified() && S.getLangOpts().CPlusPlus20 &&
      (D.getContext() == DeclaratorContext::Prototype ||
       D.getContext() == DeclaratorContext::LambdaExprParameter))
    S.Diag(D.getIdentifierLoc(), diag::warn_deprecated_volatile_param) << T;

  // If there was an ellipsis in the declarator, the declaration declares a
  // parameter pack whose type may be a pack expansion type.
  if (D.hasEllipsis()) {
    // C++0x [dcl.fct]p13:
    //   A declarator-id or abstract-declarator containing an ellipsis shall
    //   only be used in a parameter-declaration. Such a parameter-declaration
    //   is a parameter pack (14.5.3). [...]
    switch (D.getContext()) {
    case DeclaratorContext::Prototype:
    case DeclaratorContext::LambdaExprParameter:
    case DeclaratorContext::RequiresExpr:
      // C++0x [dcl.fct]p13:
      //   [...] When it is part of a parameter-declaration-clause, the
      //   parameter pack is a function parameter pack (14.5.3). The type T
      //   of the declarator-id of the function parameter pack shall contain
      //   a template parameter pack; each template parameter pack in T is
      //   expanded by the function parameter pack.
      //
      // We represent function parameter packs as function parameters whose
      // type is a pack expansion.
      if (!T->containsUnexpandedParameterPack() &&
          (!LangOpts.CPlusPlus20 || !T->getContainedAutoType())) {
        S.Diag(D.getEllipsisLoc(),
             diag::err_function_parameter_pack_without_parameter_packs)
          << T <<  D.getSourceRange();
        D.setEllipsisLoc(SourceLocation());
      } else {
        T = Context.getPackExpansionType(T, std::nullopt,
                                         /*ExpectPackInType=*/false);
      }
      break;
    case DeclaratorContext::TemplateParam:
      // C++0x [temp.param]p15:
      //   If a template-parameter is a [...] is a parameter-declaration that
      //   declares a parameter pack (8.3.5), then the template-parameter is a
      //   template parameter pack (14.5.3).
      //
      // Note: core issue 778 clarifies that, if there are any unexpanded
      // parameter packs in the type of the non-type template parameter, then
      // it expands those parameter packs.
      if (T->containsUnexpandedParameterPack())
        T = Context.getPackExpansionType(T, std::nullopt);
      else
        S.Diag(D.getEllipsisLoc(),
               LangOpts.CPlusPlus11
                 ? diag::warn_cxx98_compat_variadic_templates
                 : diag::ext_variadic_templates);
      break;

    case DeclaratorContext::File:
    case DeclaratorContext::KNRTypeList:
    case DeclaratorContext::ObjCParameter: // FIXME: special diagnostic here?
    case DeclaratorContext::ObjCResult:    // FIXME: special diagnostic here?
    case DeclaratorContext::TypeName:
    case DeclaratorContext::FunctionalCast:
    case DeclaratorContext::CXXNew:
    case DeclaratorContext::AliasDecl:
    case DeclaratorContext::AliasTemplate:
    case DeclaratorContext::Member:
    case DeclaratorContext::Block:
    case DeclaratorContext::ForInit:
    case DeclaratorContext::SelectionInit:
    case DeclaratorContext::Condition:
    case DeclaratorContext::CXXCatch:
    case DeclaratorContext::ObjCCatch:
    case DeclaratorContext::BlockLiteral:
    case DeclaratorContext::LambdaExpr:
    case DeclaratorContext::ConversionId:
    case DeclaratorContext::TrailingReturn:
    case DeclaratorContext::TrailingReturnVar:
    case DeclaratorContext::TemplateArg:
    case DeclaratorContext::TemplateTypeArg:
    case DeclaratorContext::Association:
      // FIXME: We may want to allow parameter packs in block-literal contexts
      // in the future.
      S.Diag(D.getEllipsisLoc(),
             diag::err_ellipsis_in_declarator_not_parameter);
      D.setEllipsisLoc(SourceLocation());
      break;
    }
  }

  assert(!T.isNull() && "T must not be null at the end of this function");
  if (!AreDeclaratorChunksValid)
    return Context.getTrivialTypeSourceInfo(T);
  return GetTypeSourceInfoForDeclarator(state, T, TInfo);
}

TypeSourceInfo *Sema::GetTypeForDeclarator(Declarator &D) {
  // Determine the type of the declarator. Not all forms of declarator
  // have a type.

  TypeProcessingState state(*this, D);

  TypeSourceInfo *ReturnTypeInfo = nullptr;
  QualType T = GetDeclSpecTypeForDeclarator(state, ReturnTypeInfo);
  if (D.isPrototypeContext() && getLangOpts().ObjCAutoRefCount)
    inferARCWriteback(state, T);

  return GetFullTypeForDeclarator(state, T, ReturnTypeInfo);
}

static void transferARCOwnershipToDeclSpec(Sema &S,
                                           QualType &declSpecTy,
                                           Qualifiers::ObjCLifetime ownership) {
  if (declSpecTy->isObjCRetainableType() &&
      declSpecTy.getObjCLifetime() == Qualifiers::OCL_None) {
    Qualifiers qs;
    qs.addObjCLifetime(ownership);
    declSpecTy = S.Context.getQualifiedType(declSpecTy, qs);
  }
}

static void transferARCOwnershipToDeclaratorChunk(TypeProcessingState &state,
                                            Qualifiers::ObjCLifetime ownership,
                                            unsigned chunkIndex) {
  Sema &S = state.getSema();
  Declarator &D = state.getDeclarator();

  // Look for an explicit lifetime attribute.
  DeclaratorChunk &chunk = D.getTypeObject(chunkIndex);
  if (chunk.getAttrs().hasAttribute(ParsedAttr::AT_ObjCOwnership))
    return;

  const char *attrStr = nullptr;
  switch (ownership) {
  case Qualifiers::OCL_None: llvm_unreachable("no ownership!");
  case Qualifiers::OCL_ExplicitNone: attrStr = "none"; break;
  case Qualifiers::OCL_Strong: attrStr = "strong"; break;
  case Qualifiers::OCL_Weak: attrStr = "weak"; break;
  case Qualifiers::OCL_Autoreleasing: attrStr = "autoreleasing"; break;
  }

  IdentifierLoc *Arg = new (S.Context) IdentifierLoc;
  Arg->Ident = &S.Context.Idents.get(attrStr);
  Arg->Loc = SourceLocation();

  ArgsUnion Args(Arg);

  // If there wasn't one, add one (with an invalid source location
  // so that we don't make an AttributedType for it).
  ParsedAttr *attr = D.getAttributePool().create(
      &S.Context.Idents.get("objc_ownership"), SourceLocation(),
      /*scope*/ nullptr, SourceLocation(),
      /*args*/ &Args, 1, ParsedAttr::Form::GNU());
  chunk.getAttrs().addAtEnd(attr);
  // TODO: mark whether we did this inference?
}

/// Used for transferring ownership in casts resulting in l-values.
static void transferARCOwnership(TypeProcessingState &state,
                                 QualType &declSpecTy,
                                 Qualifiers::ObjCLifetime ownership) {
  Sema &S = state.getSema();
  Declarator &D = state.getDeclarator();

  int inner = -1;
  bool hasIndirection = false;
  for (unsigned i = 0, e = D.getNumTypeObjects(); i != e; ++i) {
    DeclaratorChunk &chunk = D.getTypeObject(i);
    switch (chunk.Kind) {
    case DeclaratorChunk::Paren:
      // Ignore parens.
      break;

    case DeclaratorChunk::Array:
    case DeclaratorChunk::Reference:
    case DeclaratorChunk::Pointer:
      if (inner != -1)
        hasIndirection = true;
      inner = i;
      break;

    case DeclaratorChunk::BlockPointer:
      if (inner != -1)
        transferARCOwnershipToDeclaratorChunk(state, ownership, i);
      return;

    case DeclaratorChunk::Function:
    case DeclaratorChunk::MemberPointer:
    case DeclaratorChunk::Pipe:
      return;
    }
  }

  if (inner == -1)
    return;

  DeclaratorChunk &chunk = D.getTypeObject(inner);
  if (chunk.Kind == DeclaratorChunk::Pointer) {
    if (declSpecTy->isObjCRetainableType())
      return transferARCOwnershipToDeclSpec(S, declSpecTy, ownership);
    if (declSpecTy->isObjCObjectType() && hasIndirection)
      return transferARCOwnershipToDeclaratorChunk(state, ownership, inner);
  } else {
    assert(chunk.Kind == DeclaratorChunk::Array ||
           chunk.Kind == DeclaratorChunk::Reference);
    return transferARCOwnershipToDeclSpec(S, declSpecTy, ownership);
  }
}

TypeSourceInfo *Sema::GetTypeForDeclaratorCast(Declarator &D, QualType FromTy) {
  TypeProcessingState state(*this, D);

  TypeSourceInfo *ReturnTypeInfo = nullptr;
  QualType declSpecTy = GetDeclSpecTypeForDeclarator(state, ReturnTypeInfo);

  if (getLangOpts().ObjC) {
    Qualifiers::ObjCLifetime ownership = Context.getInnerObjCOwnership(FromTy);
    if (ownership != Qualifiers::OCL_None)
      transferARCOwnership(state, declSpecTy, ownership);
  }

  return GetFullTypeForDeclarator(state, declSpecTy, ReturnTypeInfo);
}

static void fillAttributedTypeLoc(AttributedTypeLoc TL,
                                  TypeProcessingState &State) {
  TL.setAttr(State.takeAttrForAttributedType(TL.getTypePtr()));
}

static void fillMatrixTypeLoc(MatrixTypeLoc MTL,
                              const ParsedAttributesView &Attrs) {
  for (const ParsedAttr &AL : Attrs) {
    if (AL.getKind() == ParsedAttr::AT_MatrixType) {
      MTL.setAttrNameLoc(AL.getLoc());
      MTL.setAttrRowOperand(AL.getArgAsExpr(0));
      MTL.setAttrColumnOperand(AL.getArgAsExpr(1));
      MTL.setAttrOperandParensRange(SourceRange());
      return;
    }
  }

  llvm_unreachable("no matrix_type attribute found at the expected location!");
}

namespace {
  class TypeSpecLocFiller : public TypeLocVisitor<TypeSpecLocFiller> {
    Sema &SemaRef;
    ASTContext &Context;
    TypeProcessingState &State;
    const DeclSpec &DS;

  public:
    TypeSpecLocFiller(Sema &S, ASTContext &Context, TypeProcessingState &State,
                      const DeclSpec &DS)
        : SemaRef(S), Context(Context), State(State), DS(DS) {}

    void VisitAttributedTypeLoc(AttributedTypeLoc TL) {
      Visit(TL.getModifiedLoc());
      fillAttributedTypeLoc(TL, State);
    }
    void VisitBTFTagAttributedTypeLoc(BTFTagAttributedTypeLoc TL) {
      Visit(TL.getWrappedLoc());
    }
    void VisitMacroQualifiedTypeLoc(MacroQualifiedTypeLoc TL) {
      Visit(TL.getInnerLoc());
      TL.setExpansionLoc(
          State.getExpansionLocForMacroQualifiedType(TL.getTypePtr()));
    }
    void VisitQualifiedTypeLoc(QualifiedTypeLoc TL) {
      Visit(TL.getUnqualifiedLoc());
    }
    // Allow to fill pointee's type locations, e.g.,
    //   int __attr * __attr * __attr *p;
    void VisitPointerTypeLoc(PointerTypeLoc TL) { Visit(TL.getNextTypeLoc()); }
    void VisitTypedefTypeLoc(TypedefTypeLoc TL) {
      TL.setNameLoc(DS.getTypeSpecTypeLoc());
    }
    void VisitObjCInterfaceTypeLoc(ObjCInterfaceTypeLoc TL) {
      TL.setNameLoc(DS.getTypeSpecTypeLoc());
      // FIXME. We should have DS.getTypeSpecTypeEndLoc(). But, it requires
      // addition field. What we have is good enough for display of location
      // of 'fixit' on interface name.
      TL.setNameEndLoc(DS.getEndLoc());
    }
    void VisitObjCObjectTypeLoc(ObjCObjectTypeLoc TL) {
      TypeSourceInfo *RepTInfo = nullptr;
      Sema::GetTypeFromParser(DS.getRepAsType(), &RepTInfo);
      TL.copy(RepTInfo->getTypeLoc());
    }
    void VisitObjCObjectPointerTypeLoc(ObjCObjectPointerTypeLoc TL) {
      TypeSourceInfo *RepTInfo = nullptr;
      Sema::GetTypeFromParser(DS.getRepAsType(), &RepTInfo);
      TL.copy(RepTInfo->getTypeLoc());
    }
    void VisitTemplateSpecializationTypeLoc(TemplateSpecializationTypeLoc TL) {
      TypeSourceInfo *TInfo = nullptr;
      Sema::GetTypeFromParser(DS.getRepAsType(), &TInfo);

      // If we got no declarator info from previous Sema routines,
      // just fill with the typespec loc.
      if (!TInfo) {
        TL.initialize(Context, DS.getTypeSpecTypeNameLoc());
        return;
      }

      TypeLoc OldTL = TInfo->getTypeLoc();
      if (TInfo->getType()->getAs<ElaboratedType>()) {
        ElaboratedTypeLoc ElabTL = OldTL.castAs<ElaboratedTypeLoc>();
        TemplateSpecializationTypeLoc NamedTL = ElabTL.getNamedTypeLoc()
            .castAs<TemplateSpecializationTypeLoc>();
        TL.copy(NamedTL);
      } else {
        TL.copy(OldTL.castAs<TemplateSpecializationTypeLoc>());
        assert(TL.getRAngleLoc() == OldTL.castAs<TemplateSpecializationTypeLoc>().getRAngleLoc());
      }

    }
    void VisitTypeOfExprTypeLoc(TypeOfExprTypeLoc TL) {
      assert(DS.getTypeSpecType() == DeclSpec::TST_typeofExpr ||
             DS.getTypeSpecType() == DeclSpec::TST_typeof_unqualExpr);
      TL.setTypeofLoc(DS.getTypeSpecTypeLoc());
      TL.setParensRange(DS.getTypeofParensRange());
    }
    void VisitTypeOfTypeLoc(TypeOfTypeLoc TL) {
      assert(DS.getTypeSpecType() == DeclSpec::TST_typeofType ||
             DS.getTypeSpecType() == DeclSpec::TST_typeof_unqualType);
      TL.setTypeofLoc(DS.getTypeSpecTypeLoc());
      TL.setParensRange(DS.getTypeofParensRange());
      assert(DS.getRepAsType());
      TypeSourceInfo *TInfo = nullptr;
      Sema::GetTypeFromParser(DS.getRepAsType(), &TInfo);
      TL.setUnmodifiedTInfo(TInfo);
    }
    void VisitDecltypeTypeLoc(DecltypeTypeLoc TL) {
      assert(DS.getTypeSpecType() == DeclSpec::TST_decltype);
      TL.setDecltypeLoc(DS.getTypeSpecTypeLoc());
      TL.setRParenLoc(DS.getTypeofParensRange().getEnd());
    }
    void VisitPackIndexingTypeLoc(PackIndexingTypeLoc TL) {
      assert(DS.getTypeSpecType() == DeclSpec::TST_typename_pack_indexing);
      TL.setEllipsisLoc(DS.getEllipsisLoc());
    }
    void VisitUnaryTransformTypeLoc(UnaryTransformTypeLoc TL) {
      assert(DS.isTransformTypeTrait(DS.getTypeSpecType()));
      TL.setKWLoc(DS.getTypeSpecTypeLoc());
      TL.setParensRange(DS.getTypeofParensRange());
      assert(DS.getRepAsType());
      TypeSourceInfo *TInfo = nullptr;
      Sema::GetTypeFromParser(DS.getRepAsType(), &TInfo);
      TL.setUnderlyingTInfo(TInfo);
    }
    void VisitBuiltinTypeLoc(BuiltinTypeLoc TL) {
      // By default, use the source location of the type specifier.
      TL.setBuiltinLoc(DS.getTypeSpecTypeLoc());
      if (TL.needsExtraLocalData()) {
        // Set info for the written builtin specifiers.
        TL.getWrittenBuiltinSpecs() = DS.getWrittenBuiltinSpecs();
        // Try to have a meaningful source location.
        if (TL.getWrittenSignSpec() != TypeSpecifierSign::Unspecified)
          TL.expandBuiltinRange(DS.getTypeSpecSignLoc());
        if (TL.getWrittenWidthSpec() != TypeSpecifierWidth::Unspecified)
          TL.expandBuiltinRange(DS.getTypeSpecWidthRange());
      }
    }
    void VisitElaboratedTypeLoc(ElaboratedTypeLoc TL) {
      if (DS.getTypeSpecType() == TST_typename) {
        TypeSourceInfo *TInfo = nullptr;
        Sema::GetTypeFromParser(DS.getRepAsType(), &TInfo);
        if (TInfo)
          if (auto ETL = TInfo->getTypeLoc().getAs<ElaboratedTypeLoc>()) {
            TL.copy(ETL);
            return;
          }
      }
      const ElaboratedType *T = TL.getTypePtr();
      TL.setElaboratedKeywordLoc(T->getKeyword() != ElaboratedTypeKeyword::None
                                     ? DS.getTypeSpecTypeLoc()
                                     : SourceLocation());
      const CXXScopeSpec& SS = DS.getTypeSpecScope();
      TL.setQualifierLoc(SS.getWithLocInContext(Context));
      Visit(TL.getNextTypeLoc().getUnqualifiedLoc());
    }
    void VisitDependentNameTypeLoc(DependentNameTypeLoc TL) {
      assert(DS.getTypeSpecType() == TST_typename);
      TypeSourceInfo *TInfo = nullptr;
      Sema::GetTypeFromParser(DS.getRepAsType(), &TInfo);
      assert(TInfo);
      TL.copy(TInfo->getTypeLoc().castAs<DependentNameTypeLoc>());
    }
    void VisitDependentTemplateSpecializationTypeLoc(
                                 DependentTemplateSpecializationTypeLoc TL) {
      assert(DS.getTypeSpecType() == TST_typename);
      TypeSourceInfo *TInfo = nullptr;
      Sema::GetTypeFromParser(DS.getRepAsType(), &TInfo);
      assert(TInfo);
      TL.copy(
          TInfo->getTypeLoc().castAs<DependentTemplateSpecializationTypeLoc>());
    }
    void VisitAutoTypeLoc(AutoTypeLoc TL) {
      assert(DS.getTypeSpecType() == TST_auto ||
             DS.getTypeSpecType() == TST_decltype_auto ||
             DS.getTypeSpecType() == TST_auto_type ||
             DS.getTypeSpecType() == TST_unspecified);
      TL.setNameLoc(DS.getTypeSpecTypeLoc());
      if (DS.getTypeSpecType() == TST_decltype_auto)
        TL.setRParenLoc(DS.getTypeofParensRange().getEnd());
      if (!DS.isConstrainedAuto())
        return;
      TemplateIdAnnotation *TemplateId = DS.getRepAsTemplateId();
      if (!TemplateId)
        return;

      NestedNameSpecifierLoc NNS =
          (DS.getTypeSpecScope().isNotEmpty()
               ? DS.getTypeSpecScope().getWithLocInContext(Context)
               : NestedNameSpecifierLoc());
      TemplateArgumentListInfo TemplateArgsInfo(TemplateId->LAngleLoc,
                                                TemplateId->RAngleLoc);
      if (TemplateId->NumArgs > 0) {
        ASTTemplateArgsPtr TemplateArgsPtr(TemplateId->getTemplateArgs(),
                                           TemplateId->NumArgs);
        SemaRef.translateTemplateArguments(TemplateArgsPtr, TemplateArgsInfo);
      }
      DeclarationNameInfo DNI = DeclarationNameInfo(
          TL.getTypePtr()->getTypeConstraintConcept()->getDeclName(),
          TemplateId->TemplateNameLoc);

      NamedDecl *FoundDecl;
      if (auto TN = TemplateId->Template.get();
          UsingShadowDecl *USD = TN.getAsUsingShadowDecl())
        FoundDecl = cast<NamedDecl>(USD);
      else
        FoundDecl = cast_if_present<NamedDecl>(TN.getAsTemplateDecl());

      auto *CR = ConceptReference::Create(
          Context, NNS, TemplateId->TemplateKWLoc, DNI, FoundDecl,
          /*NamedDecl=*/TL.getTypePtr()->getTypeConstraintConcept(),
          ASTTemplateArgumentListInfo::Create(Context, TemplateArgsInfo));
      TL.setConceptReference(CR);
    }
    void VisitTagTypeLoc(TagTypeLoc TL) {
      TL.setNameLoc(DS.getTypeSpecTypeNameLoc());
    }
    void VisitAtomicTypeLoc(AtomicTypeLoc TL) {
      // An AtomicTypeLoc can come from either an _Atomic(...) type specifier
      // or an _Atomic qualifier.
      if (DS.getTypeSpecType() == DeclSpec::TST_atomic) {
        TL.setKWLoc(DS.getTypeSpecTypeLoc());
        TL.setParensRange(DS.getTypeofParensRange());

        TypeSourceInfo *TInfo = nullptr;
        Sema::GetTypeFromParser(DS.getRepAsType(), &TInfo);
        assert(TInfo);
        TL.getValueLoc().initializeFullCopy(TInfo->getTypeLoc());
      } else {
        TL.setKWLoc(DS.getAtomicSpecLoc());
        // No parens, to indicate this was spelled as an _Atomic qualifier.
        TL.setParensRange(SourceRange());
        Visit(TL.getValueLoc());
      }
    }

    void VisitPipeTypeLoc(PipeTypeLoc TL) {
      TL.setKWLoc(DS.getTypeSpecTypeLoc());

      TypeSourceInfo *TInfo = nullptr;
      Sema::GetTypeFromParser(DS.getRepAsType(), &TInfo);
      TL.getValueLoc().initializeFullCopy(TInfo->getTypeLoc());
    }

    void VisitExtIntTypeLoc(BitIntTypeLoc TL) {
      TL.setNameLoc(DS.getTypeSpecTypeLoc());
    }

    void VisitDependentExtIntTypeLoc(DependentBitIntTypeLoc TL) {
      TL.setNameLoc(DS.getTypeSpecTypeLoc());
    }

    void VisitTypeLoc(TypeLoc TL) {
      // FIXME: add other typespec types and change this to an assert.
      TL.initialize(Context, DS.getTypeSpecTypeLoc());
    }
  };

  class DeclaratorLocFiller : public TypeLocVisitor<DeclaratorLocFiller> {
    ASTContext &Context;
    TypeProcessingState &State;
    const DeclaratorChunk &Chunk;

  public:
    DeclaratorLocFiller(ASTContext &Context, TypeProcessingState &State,
                        const DeclaratorChunk &Chunk)
        : Context(Context), State(State), Chunk(Chunk) {}

    void VisitQualifiedTypeLoc(QualifiedTypeLoc TL) {
      llvm_unreachable("qualified type locs not expected here!");
    }
    void VisitDecayedTypeLoc(DecayedTypeLoc TL) {
      llvm_unreachable("decayed type locs not expected here!");
    }
    void VisitArrayParameterTypeLoc(ArrayParameterTypeLoc TL) {
      llvm_unreachable("array parameter type locs not expected here!");
    }

    void VisitAttributedTypeLoc(AttributedTypeLoc TL) {
      fillAttributedTypeLoc(TL, State);
    }
    void VisitCountAttributedTypeLoc(CountAttributedTypeLoc TL) {
      // nothing
    }
    void VisitBTFTagAttributedTypeLoc(BTFTagAttributedTypeLoc TL) {
      // nothing
    }
    void VisitAdjustedTypeLoc(AdjustedTypeLoc TL) {
      // nothing
    }
    void VisitBlockPointerTypeLoc(BlockPointerTypeLoc TL) {
      assert(Chunk.Kind == DeclaratorChunk::BlockPointer);
      TL.setCaretLoc(Chunk.Loc);
    }
    void VisitPointerTypeLoc(PointerTypeLoc TL) {
      assert(Chunk.Kind == DeclaratorChunk::Pointer);
      TL.setStarLoc(Chunk.Loc);
    }
    void VisitObjCObjectPointerTypeLoc(ObjCObjectPointerTypeLoc TL) {
      assert(Chunk.Kind == DeclaratorChunk::Pointer);
      TL.setStarLoc(Chunk.Loc);
    }
    void VisitMemberPointerTypeLoc(MemberPointerTypeLoc TL) {
      assert(Chunk.Kind == DeclaratorChunk::MemberPointer);
      const CXXScopeSpec& SS = Chunk.Mem.Scope();
      NestedNameSpecifierLoc NNSLoc = SS.getWithLocInContext(Context);

      const Type* ClsTy = TL.getClass();
      QualType ClsQT = QualType(ClsTy, 0);
      TypeSourceInfo *ClsTInfo = Context.CreateTypeSourceInfo(ClsQT, 0);
      // Now copy source location info into the type loc component.
      TypeLoc ClsTL = ClsTInfo->getTypeLoc();
      switch (NNSLoc.getNestedNameSpecifier()->getKind()) {
      case NestedNameSpecifier::Identifier:
        assert(isa<DependentNameType>(ClsTy) && "Unexpected TypeLoc");
        {
          DependentNameTypeLoc DNTLoc = ClsTL.castAs<DependentNameTypeLoc>();
          DNTLoc.setElaboratedKeywordLoc(SourceLocation());
          DNTLoc.setQualifierLoc(NNSLoc.getPrefix());
          DNTLoc.setNameLoc(NNSLoc.getLocalBeginLoc());
        }
        break;

      case NestedNameSpecifier::TypeSpec:
      case NestedNameSpecifier::TypeSpecWithTemplate:
        if (isa<ElaboratedType>(ClsTy)) {
          ElaboratedTypeLoc ETLoc = ClsTL.castAs<ElaboratedTypeLoc>();
          ETLoc.setElaboratedKeywordLoc(SourceLocation());
          ETLoc.setQualifierLoc(NNSLoc.getPrefix());
          TypeLoc NamedTL = ETLoc.getNamedTypeLoc();
          NamedTL.initializeFullCopy(NNSLoc.getTypeLoc());
        } else {
          ClsTL.initializeFullCopy(NNSLoc.getTypeLoc());
        }
        break;

      case NestedNameSpecifier::Namespace:
      case NestedNameSpecifier::NamespaceAlias:
      case NestedNameSpecifier::Global:
      case NestedNameSpecifier::Super:
        llvm_unreachable("Nested-name-specifier must name a type");
      }

      // Finally fill in MemberPointerLocInfo fields.
      TL.setStarLoc(Chunk.Mem.StarLoc);
      TL.setClassTInfo(ClsTInfo);
    }
    void VisitLValueReferenceTypeLoc(LValueReferenceTypeLoc TL) {
      assert(Chunk.Kind == DeclaratorChunk::Reference);
      // 'Amp' is misleading: this might have been originally
      /// spelled with AmpAmp.
      TL.setAmpLoc(Chunk.Loc);
    }
    void VisitRValueReferenceTypeLoc(RValueReferenceTypeLoc TL) {
      assert(Chunk.Kind == DeclaratorChunk::Reference);
      assert(!Chunk.Ref.LValueRef);
      TL.setAmpAmpLoc(Chunk.Loc);
    }
    void VisitArrayTypeLoc(ArrayTypeLoc TL) {
      assert(Chunk.Kind == DeclaratorChunk::Array);
      TL.setLBracketLoc(Chunk.Loc);
      TL.setRBracketLoc(Chunk.EndLoc);
      TL.setSizeExpr(static_cast<Expr*>(Chunk.Arr.NumElts));
    }
    void VisitFunctionTypeLoc(FunctionTypeLoc TL) {
      assert(Chunk.Kind == DeclaratorChunk::Function);
      TL.setLocalRangeBegin(Chunk.Loc);
      TL.setLocalRangeEnd(Chunk.EndLoc);

      const DeclaratorChunk::FunctionTypeInfo &FTI = Chunk.Fun;
      TL.setLParenLoc(FTI.getLParenLoc());
      TL.setRParenLoc(FTI.getRParenLoc());
      for (unsigned i = 0, e = TL.getNumParams(), tpi = 0; i != e; ++i) {
        ParmVarDecl *Param = cast<ParmVarDecl>(FTI.Params[i].Param);
        TL.setParam(tpi++, Param);
      }
      TL.setExceptionSpecRange(FTI.getExceptionSpecRange());
    }
    void VisitParenTypeLoc(ParenTypeLoc TL) {
      assert(Chunk.Kind == DeclaratorChunk::Paren);
      TL.setLParenLoc(Chunk.Loc);
      TL.setRParenLoc(Chunk.EndLoc);
    }
    void VisitPipeTypeLoc(PipeTypeLoc TL) {
      assert(Chunk.Kind == DeclaratorChunk::Pipe);
      TL.setKWLoc(Chunk.Loc);
    }
    void VisitBitIntTypeLoc(BitIntTypeLoc TL) {
      TL.setNameLoc(Chunk.Loc);
    }
    void VisitMacroQualifiedTypeLoc(MacroQualifiedTypeLoc TL) {
      TL.setExpansionLoc(Chunk.Loc);
    }
    void VisitVectorTypeLoc(VectorTypeLoc TL) { TL.setNameLoc(Chunk.Loc); }
    void VisitDependentVectorTypeLoc(DependentVectorTypeLoc TL) {
      TL.setNameLoc(Chunk.Loc);
    }
    void VisitExtVectorTypeLoc(ExtVectorTypeLoc TL) {
      TL.setNameLoc(Chunk.Loc);
    }
    void
    VisitDependentSizedExtVectorTypeLoc(DependentSizedExtVectorTypeLoc TL) {
      TL.setNameLoc(Chunk.Loc);
    }
    void VisitMatrixTypeLoc(MatrixTypeLoc TL) {
      fillMatrixTypeLoc(TL, Chunk.getAttrs());
    }

    void VisitTypeLoc(TypeLoc TL) {
      llvm_unreachable("unsupported TypeLoc kind in declarator!");
    }
  };
} // end anonymous namespace

static void fillAtomicQualLoc(AtomicTypeLoc ATL, const DeclaratorChunk &Chunk) {
  SourceLocation Loc;
  switch (Chunk.Kind) {
  case DeclaratorChunk::Function:
  case DeclaratorChunk::Array:
  case DeclaratorChunk::Paren:
  case DeclaratorChunk::Pipe:
    llvm_unreachable("cannot be _Atomic qualified");

  case DeclaratorChunk::Pointer:
    Loc = Chunk.Ptr.AtomicQualLoc;
    break;

  case DeclaratorChunk::BlockPointer:
  case DeclaratorChunk::Reference:
  case DeclaratorChunk::MemberPointer:
    // FIXME: Provide a source location for the _Atomic keyword.
    break;
  }

  ATL.setKWLoc(Loc);
  ATL.setParensRange(SourceRange());
}

static void
fillDependentAddressSpaceTypeLoc(DependentAddressSpaceTypeLoc DASTL,
                                 const ParsedAttributesView &Attrs) {
  for (const ParsedAttr &AL : Attrs) {
    if (AL.getKind() == ParsedAttr::AT_AddressSpace) {
      DASTL.setAttrNameLoc(AL.getLoc());
      DASTL.setAttrExprOperand(AL.getArgAsExpr(0));
      DASTL.setAttrOperandParensRange(SourceRange());
      return;
    }
  }

  llvm_unreachable(
      "no address_space attribute found at the expected location!");
}

/// Create and instantiate a TypeSourceInfo with type source information.
///
/// \param T QualType referring to the type as written in source code.
///
/// \param ReturnTypeInfo For declarators whose return type does not show
/// up in the normal place in the declaration specifiers (such as a C++
/// conversion function), this pointer will refer to a type source information
/// for that return type.
static TypeSourceInfo *
GetTypeSourceInfoForDeclarator(TypeProcessingState &State,
                               QualType T, TypeSourceInfo *ReturnTypeInfo) {
  Sema &S = State.getSema();
  Declarator &D = State.getDeclarator();

  TypeSourceInfo *TInfo = S.Context.CreateTypeSourceInfo(T);
  UnqualTypeLoc CurrTL = TInfo->getTypeLoc().getUnqualifiedLoc();

  // Handle parameter packs whose type is a pack expansion.
  if (isa<PackExpansionType>(T)) {
    CurrTL.castAs<PackExpansionTypeLoc>().setEllipsisLoc(D.getEllipsisLoc());
    CurrTL = CurrTL.getNextTypeLoc().getUnqualifiedLoc();
  }

  for (unsigned i = 0, e = D.getNumTypeObjects(); i != e; ++i) {
    // Microsoft property fields can have multiple sizeless array chunks
    // (i.e. int x[][][]). Don't create more than one level of incomplete array.
    if (CurrTL.getTypeLocClass() == TypeLoc::IncompleteArray && e != 1 &&
        D.getDeclSpec().getAttributes().hasMSPropertyAttr())
      continue;

    // An AtomicTypeLoc might be produced by an atomic qualifier in this
    // declarator chunk.
    if (AtomicTypeLoc ATL = CurrTL.getAs<AtomicTypeLoc>()) {
      fillAtomicQualLoc(ATL, D.getTypeObject(i));
      CurrTL = ATL.getValueLoc().getUnqualifiedLoc();
    }

    bool HasDesugaredTypeLoc = true;
    while (HasDesugaredTypeLoc) {
      switch (CurrTL.getTypeLocClass()) {
      case TypeLoc::MacroQualified: {
        auto TL = CurrTL.castAs<MacroQualifiedTypeLoc>();
        TL.setExpansionLoc(
            State.getExpansionLocForMacroQualifiedType(TL.getTypePtr()));
        CurrTL = TL.getNextTypeLoc().getUnqualifiedLoc();
        break;
      }

      case TypeLoc::Attributed: {
        auto TL = CurrTL.castAs<AttributedTypeLoc>();
        fillAttributedTypeLoc(TL, State);
        CurrTL = TL.getNextTypeLoc().getUnqualifiedLoc();
        break;
      }

      case TypeLoc::Adjusted:
      case TypeLoc::BTFTagAttributed: {
        CurrTL = CurrTL.getNextTypeLoc().getUnqualifiedLoc();
        break;
      }

      case TypeLoc::DependentAddressSpace: {
        auto TL = CurrTL.castAs<DependentAddressSpaceTypeLoc>();
        fillDependentAddressSpaceTypeLoc(TL, D.getTypeObject(i).getAttrs());
        CurrTL = TL.getPointeeTypeLoc().getUnqualifiedLoc();
        break;
      }

      default:
        HasDesugaredTypeLoc = false;
        break;
      }
    }

    DeclaratorLocFiller(S.Context, State, D.getTypeObject(i)).Visit(CurrTL);
    CurrTL = CurrTL.getNextTypeLoc().getUnqualifiedLoc();
  }

  // If we have different source information for the return type, use
  // that.  This really only applies to C++ conversion functions.
  if (ReturnTypeInfo) {
    TypeLoc TL = ReturnTypeInfo->getTypeLoc();
    assert(TL.getFullDataSize() == CurrTL.getFullDataSize());
    memcpy(CurrTL.getOpaqueData(), TL.getOpaqueData(), TL.getFullDataSize());
  } else {
    TypeSpecLocFiller(S, S.Context, State, D.getDeclSpec()).Visit(CurrTL);
  }

  return TInfo;
}

/// Create a LocInfoType to hold the given QualType and TypeSourceInfo.
ParsedType Sema::CreateParsedType(QualType T, TypeSourceInfo *TInfo) {
  // FIXME: LocInfoTypes are "transient", only needed for passing to/from Parser
  // and Sema during declaration parsing. Try deallocating/caching them when
  // it's appropriate, instead of allocating them and keeping them around.
  LocInfoType *LocT = (LocInfoType *)BumpAlloc.Allocate(sizeof(LocInfoType),
                                                        alignof(LocInfoType));
  new (LocT) LocInfoType(T, TInfo);
  assert(LocT->getTypeClass() != T->getTypeClass() &&
         "LocInfoType's TypeClass conflicts with an existing Type class");
  return ParsedType::make(QualType(LocT, 0));
}

void LocInfoType::getAsStringInternal(std::string &Str,
                                      const PrintingPolicy &Policy) const {
  llvm_unreachable("LocInfoType leaked into the type system; an opaque TypeTy*"
         " was used directly instead of getting the QualType through"
         " GetTypeFromParser");
}

TypeResult Sema::ActOnTypeName(Declarator &D) {
  // C99 6.7.6: Type names have no identifier.  This is already validated by
  // the parser.
  assert(D.getIdentifier() == nullptr &&
         "Type name should have no identifier!");

  TypeSourceInfo *TInfo = GetTypeForDeclarator(D);
  QualType T = TInfo->getType();
  if (D.isInvalidType())
    return true;

  // Make sure there are no unused decl attributes on the declarator.
  // We don't want to do this for ObjC parameters because we're going
  // to apply them to the actual parameter declaration.
  // Likewise, we don't want to do this for alias declarations, because
  // we are actually going to build a declaration from this eventually.
  if (D.getContext() != DeclaratorContext::ObjCParameter &&
      D.getContext() != DeclaratorContext::AliasDecl &&
      D.getContext() != DeclaratorContext::AliasTemplate)
    checkUnusedDeclAttributes(D);

  if (getLangOpts().CPlusPlus) {
    // Check that there are no default arguments (C++ only).
    CheckExtraCXXDefaultArguments(D);
  }

  if (AutoTypeLoc TL = TInfo->getTypeLoc().getContainedAutoTypeLoc()) {
    const AutoType *AT = TL.getTypePtr();
    CheckConstrainedAuto(AT, TL.getConceptNameLoc());
  }
  return CreateParsedType(T, TInfo);
}

//===----------------------------------------------------------------------===//
// Type Attribute Processing
//===----------------------------------------------------------------------===//

/// Build an AddressSpace index from a constant expression and diagnose any
/// errors related to invalid address_spaces. Returns true on successfully
/// building an AddressSpace index.
static bool BuildAddressSpaceIndex(Sema &S, LangAS &ASIdx,
                                   const Expr *AddrSpace,
                                   SourceLocation AttrLoc) {
  if (!AddrSpace->isValueDependent()) {
    std::optional<llvm::APSInt> OptAddrSpace =
        AddrSpace->getIntegerConstantExpr(S.Context);
    if (!OptAddrSpace) {
      S.Diag(AttrLoc, diag::err_attribute_argument_type)
          << "'address_space'" << AANT_ArgumentIntegerConstant
          << AddrSpace->getSourceRange();
      return false;
    }
    llvm::APSInt &addrSpace = *OptAddrSpace;

    // Bounds checking.
    if (addrSpace.isSigned()) {
      if (addrSpace.isNegative()) {
        S.Diag(AttrLoc, diag::err_attribute_address_space_negative)
            << AddrSpace->getSourceRange();
        return false;
      }
      addrSpace.setIsSigned(false);
    }

    llvm::APSInt max(addrSpace.getBitWidth());
    max =
        Qualifiers::MaxAddressSpace - (unsigned)LangAS::FirstTargetAddressSpace;

    if (addrSpace > max) {
      S.Diag(AttrLoc, diag::err_attribute_address_space_too_high)
          << (unsigned)max.getZExtValue() << AddrSpace->getSourceRange();
      return false;
    }

    ASIdx =
        getLangASFromTargetAS(static_cast<unsigned>(addrSpace.getZExtValue()));
    return true;
  }

  // Default value for DependentAddressSpaceTypes
  ASIdx = LangAS::Default;
  return true;
}

QualType Sema::BuildAddressSpaceAttr(QualType &T, LangAS ASIdx, Expr *AddrSpace,
                                     SourceLocation AttrLoc) {
  if (!AddrSpace->isValueDependent()) {
    if (DiagnoseMultipleAddrSpaceAttributes(*this, T.getAddressSpace(), ASIdx,
                                            AttrLoc))
      return QualType();

    return Context.getAddrSpaceQualType(T, ASIdx);
  }

  // A check with similar intentions as checking if a type already has an
  // address space except for on a dependent types, basically if the
  // current type is already a DependentAddressSpaceType then its already
  // lined up to have another address space on it and we can't have
  // multiple address spaces on the one pointer indirection
  if (T->getAs<DependentAddressSpaceType>()) {
    Diag(AttrLoc, diag::err_attribute_address_multiple_qualifiers);
    return QualType();
  }

  return Context.getDependentAddressSpaceType(T, AddrSpace, AttrLoc);
}

QualType Sema::BuildAddressSpaceAttr(QualType &T, Expr *AddrSpace,
                                     SourceLocation AttrLoc) {
  LangAS ASIdx;
  if (!BuildAddressSpaceIndex(*this, ASIdx, AddrSpace, AttrLoc))
    return QualType();
  return BuildAddressSpaceAttr(T, ASIdx, AddrSpace, AttrLoc);
}

static void HandleBTFTypeTagAttribute(QualType &Type, const ParsedAttr &Attr,
                                      TypeProcessingState &State) {
  Sema &S = State.getSema();

  // Check the number of attribute arguments.
  if (Attr.getNumArgs() != 1) {
    S.Diag(Attr.getLoc(), diag::err_attribute_wrong_number_arguments)
        << Attr << 1;
    Attr.setInvalid();
    return;
  }

  // Ensure the argument is a string.
  auto *StrLiteral = dyn_cast<StringLiteral>(Attr.getArgAsExpr(0));
  if (!StrLiteral) {
    S.Diag(Attr.getLoc(), diag::err_attribute_argument_type)
        << Attr << AANT_ArgumentString;
    Attr.setInvalid();
    return;
  }

  ASTContext &Ctx = S.Context;
  StringRef BTFTypeTag = StrLiteral->getString();
  Type = State.getBTFTagAttributedType(
      ::new (Ctx) BTFTypeTagAttr(Ctx, Attr, BTFTypeTag), Type);
}

/// HandleAddressSpaceTypeAttribute - Process an address_space attribute on the
/// specified type.  The attribute contains 1 argument, the id of the address
/// space for the type.
static void HandleAddressSpaceTypeAttribute(QualType &Type,
                                            const ParsedAttr &Attr,
                                            TypeProcessingState &State) {
  Sema &S = State.getSema();

  // ISO/IEC TR 18037 S5.3 (amending C99 6.7.3): "A function type shall not be
  // qualified by an address-space qualifier."
  if (Type->isFunctionType()) {
    S.Diag(Attr.getLoc(), diag::err_attribute_address_function_type);
    Attr.setInvalid();
    return;
  }

  LangAS ASIdx;
  if (Attr.getKind() == ParsedAttr::AT_AddressSpace) {

    // Check the attribute arguments.
    if (Attr.getNumArgs() != 1) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_number_arguments) << Attr
                                                                        << 1;
      Attr.setInvalid();
      return;
    }

    Expr *ASArgExpr = static_cast<Expr *>(Attr.getArgAsExpr(0));
    LangAS ASIdx;
    if (!BuildAddressSpaceIndex(S, ASIdx, ASArgExpr, Attr.getLoc())) {
      Attr.setInvalid();
      return;
    }

    ASTContext &Ctx = S.Context;
    auto *ASAttr =
        ::new (Ctx) AddressSpaceAttr(Ctx, Attr, static_cast<unsigned>(ASIdx));

    // If the expression is not value dependent (not templated), then we can
    // apply the address space qualifiers just to the equivalent type.
    // Otherwise, we make an AttributedType with the modified and equivalent
    // type the same, and wrap it in a DependentAddressSpaceType. When this
    // dependent type is resolved, the qualifier is added to the equivalent type
    // later.
    QualType T;
    if (!ASArgExpr->isValueDependent()) {
      QualType EquivType =
          S.BuildAddressSpaceAttr(Type, ASIdx, ASArgExpr, Attr.getLoc());
      if (EquivType.isNull()) {
        Attr.setInvalid();
        return;
      }
      T = State.getAttributedType(ASAttr, Type, EquivType);
    } else {
      T = State.getAttributedType(ASAttr, Type, Type);
      T = S.BuildAddressSpaceAttr(T, ASIdx, ASArgExpr, Attr.getLoc());
    }

    if (!T.isNull())
      Type = T;
    else
      Attr.setInvalid();
  } else {
    // The keyword-based type attributes imply which address space to use.
    ASIdx = S.getLangOpts().SYCLIsDevice ? Attr.asSYCLLangAS()
                                         : Attr.asOpenCLLangAS();
    if (S.getLangOpts().HLSL)
      ASIdx = Attr.asHLSLLangAS();

    if (ASIdx == LangAS::Default)
      llvm_unreachable("Invalid address space");

    if (DiagnoseMultipleAddrSpaceAttributes(S, Type.getAddressSpace(), ASIdx,
                                            Attr.getLoc())) {
      Attr.setInvalid();
      return;
    }

    Type = S.Context.getAddrSpaceQualType(Type, ASIdx);
  }
}

/// handleObjCOwnershipTypeAttr - Process an objc_ownership
/// attribute on the specified type.
///
/// Returns 'true' if the attribute was handled.
static bool handleObjCOwnershipTypeAttr(TypeProcessingState &state,
                                        ParsedAttr &attr, QualType &type) {
  bool NonObjCPointer = false;

  if (!type->isDependentType() && !type->isUndeducedType()) {
    if (const PointerType *ptr = type->getAs<PointerType>()) {
      QualType pointee = ptr->getPointeeType();
      if (pointee->isObjCRetainableType() || pointee->isPointerType())
        return false;
      // It is important not to lose the source info that there was an attribute
      // applied to non-objc pointer. We will create an attributed type but
      // its type will be the same as the original type.
      NonObjCPointer = true;
    } else if (!type->isObjCRetainableType()) {
      return false;
    }

    // Don't accept an ownership attribute in the declspec if it would
    // just be the return type of a block pointer.
    if (state.isProcessingDeclSpec()) {
      Declarator &D = state.getDeclarator();
      if (maybeMovePastReturnType(D, D.getNumTypeObjects(),
                                  /*onlyBlockPointers=*/true))
        return false;
    }
  }

  Sema &S = state.getSema();
  SourceLocation AttrLoc = attr.getLoc();
  if (AttrLoc.isMacroID())
    AttrLoc =
        S.getSourceManager().getImmediateExpansionRange(AttrLoc).getBegin();

  if (!attr.isArgIdent(0)) {
    S.Diag(AttrLoc, diag::err_attribute_argument_type) << attr
                                                       << AANT_ArgumentString;
    attr.setInvalid();
    return true;
  }

  IdentifierInfo *II = attr.getArgAsIdent(0)->Ident;
  Qualifiers::ObjCLifetime lifetime;
  if (II->isStr("none"))
    lifetime = Qualifiers::OCL_ExplicitNone;
  else if (II->isStr("strong"))
    lifetime = Qualifiers::OCL_Strong;
  else if (II->isStr("weak"))
    lifetime = Qualifiers::OCL_Weak;
  else if (II->isStr("autoreleasing"))
    lifetime = Qualifiers::OCL_Autoreleasing;
  else {
    S.Diag(AttrLoc, diag::warn_attribute_type_not_supported) << attr << II;
    attr.setInvalid();
    return true;
  }

  // Just ignore lifetime attributes other than __weak and __unsafe_unretained
  // outside of ARC mode.
  if (!S.getLangOpts().ObjCAutoRefCount &&
      lifetime != Qualifiers::OCL_Weak &&
      lifetime != Qualifiers::OCL_ExplicitNone) {
    return true;
  }

  SplitQualType underlyingType = type.split();

  // Check for redundant/conflicting ownership qualifiers.
  if (Qualifiers::ObjCLifetime previousLifetime
        = type.getQualifiers().getObjCLifetime()) {
    // If it's written directly, that's an error.
    if (S.Context.hasDirectOwnershipQualifier(type)) {
      S.Diag(AttrLoc, diag::err_attr_objc_ownership_redundant)
        << type;
      return true;
    }

    // Otherwise, if the qualifiers actually conflict, pull sugar off
    // and remove the ObjCLifetime qualifiers.
    if (previousLifetime != lifetime) {
      // It's possible to have multiple local ObjCLifetime qualifiers. We
      // can't stop after we reach a type that is directly qualified.
      const Type *prevTy = nullptr;
      while (!prevTy || prevTy != underlyingType.Ty) {
        prevTy = underlyingType.Ty;
        underlyingType = underlyingType.getSingleStepDesugaredType();
      }
      underlyingType.Quals.removeObjCLifetime();
    }
  }

  underlyingType.Quals.addObjCLifetime(lifetime);

  if (NonObjCPointer) {
    StringRef name = attr.getAttrName()->getName();
    switch (lifetime) {
    case Qualifiers::OCL_None:
    case Qualifiers::OCL_ExplicitNone:
      break;
    case Qualifiers::OCL_Strong: name = "__strong"; break;
    case Qualifiers::OCL_Weak: name = "__weak"; break;
    case Qualifiers::OCL_Autoreleasing: name = "__autoreleasing"; break;
    }
    S.Diag(AttrLoc, diag::warn_type_attribute_wrong_type) << name
      << TDS_ObjCObjOrBlock << type;
  }

  // Don't actually add the __unsafe_unretained qualifier in non-ARC files,
  // because having both 'T' and '__unsafe_unretained T' exist in the type
  // system causes unfortunate widespread consistency problems.  (For example,
  // they're not considered compatible types, and we mangle them identicially
  // as template arguments.)  These problems are all individually fixable,
  // but it's easier to just not add the qualifier and instead sniff it out
  // in specific places using isObjCInertUnsafeUnretainedType().
  //
  // Doing this does means we miss some trivial consistency checks that
  // would've triggered in ARC, but that's better than trying to solve all
  // the coexistence problems with __unsafe_unretained.
  if (!S.getLangOpts().ObjCAutoRefCount &&
      lifetime == Qualifiers::OCL_ExplicitNone) {
    type = state.getAttributedType(
        createSimpleAttr<ObjCInertUnsafeUnretainedAttr>(S.Context, attr),
        type, type);
    return true;
  }

  QualType origType = type;
  if (!NonObjCPointer)
    type = S.Context.getQualifiedType(underlyingType);

  // If we have a valid source location for the attribute, use an
  // AttributedType instead.
  if (AttrLoc.isValid()) {
    type = state.getAttributedType(::new (S.Context)
                                       ObjCOwnershipAttr(S.Context, attr, II),
                                   origType, type);
  }

  auto diagnoseOrDelay = [](Sema &S, SourceLocation loc,
                            unsigned diagnostic, QualType type) {
    if (S.DelayedDiagnostics.shouldDelayDiagnostics()) {
      S.DelayedDiagnostics.add(
          sema::DelayedDiagnostic::makeForbiddenType(
              S.getSourceManager().getExpansionLoc(loc),
              diagnostic, type, /*ignored*/ 0));
    } else {
      S.Diag(loc, diagnostic);
    }
  };

  // Sometimes, __weak isn't allowed.
  if (lifetime == Qualifiers::OCL_Weak &&
      !S.getLangOpts().ObjCWeak && !NonObjCPointer) {

    // Use a specialized diagnostic if the runtime just doesn't support them.
    unsigned diagnostic =
      (S.getLangOpts().ObjCWeakRuntime ? diag::err_arc_weak_disabled
                                       : diag::err_arc_weak_no_runtime);

    // In any case, delay the diagnostic until we know what we're parsing.
    diagnoseOrDelay(S, AttrLoc, diagnostic, type);

    attr.setInvalid();
    return true;
  }

  // Forbid __weak for class objects marked as
  // objc_arc_weak_reference_unavailable
  if (lifetime == Qualifiers::OCL_Weak) {
    if (const ObjCObjectPointerType *ObjT =
          type->getAs<ObjCObjectPointerType>()) {
      if (ObjCInterfaceDecl *Class = ObjT->getInterfaceDecl()) {
        if (Class->isArcWeakrefUnavailable()) {
          S.Diag(AttrLoc, diag::err_arc_unsupported_weak_class);
          S.Diag(ObjT->getInterfaceDecl()->getLocation(),
                 diag::note_class_declared);
        }
      }
    }
  }

  return true;
}

/// handleObjCGCTypeAttr - Process the __attribute__((objc_gc)) type
/// attribute on the specified type.  Returns true to indicate that
/// the attribute was handled, false to indicate that the type does
/// not permit the attribute.
static bool handleObjCGCTypeAttr(TypeProcessingState &state, ParsedAttr &attr,
                                 QualType &type) {
  Sema &S = state.getSema();

  // Delay if this isn't some kind of pointer.
  if (!type->isPointerType() &&
      !type->isObjCObjectPointerType() &&
      !type->isBlockPointerType())
    return false;

  if (type.getObjCGCAttr() != Qualifiers::GCNone) {
    S.Diag(attr.getLoc(), diag::err_attribute_multiple_objc_gc);
    attr.setInvalid();
    return true;
  }

  // Check the attribute arguments.
  if (!attr.isArgIdent(0)) {
    S.Diag(attr.getLoc(), diag::err_attribute_argument_type)
        << attr << AANT_ArgumentString;
    attr.setInvalid();
    return true;
  }
  Qualifiers::GC GCAttr;
  if (attr.getNumArgs() > 1) {
    S.Diag(attr.getLoc(), diag::err_attribute_wrong_number_arguments) << attr
                                                                      << 1;
    attr.setInvalid();
    return true;
  }

  IdentifierInfo *II = attr.getArgAsIdent(0)->Ident;
  if (II->isStr("weak"))
    GCAttr = Qualifiers::Weak;
  else if (II->isStr("strong"))
    GCAttr = Qualifiers::Strong;
  else {
    S.Diag(attr.getLoc(), diag::warn_attribute_type_not_supported)
        << attr << II;
    attr.setInvalid();
    return true;
  }

  QualType origType = type;
  type = S.Context.getObjCGCQualType(origType, GCAttr);

  // Make an attributed type to preserve the source information.
  if (attr.getLoc().isValid())
    type = state.getAttributedType(
        ::new (S.Context) ObjCGCAttr(S.Context, attr, II), origType, type);

  return true;
}

namespace {
  /// A helper class to unwrap a type down to a function for the
  /// purposes of applying attributes there.
  ///
  /// Use:
  ///   FunctionTypeUnwrapper unwrapped(SemaRef, T);
  ///   if (unwrapped.isFunctionType()) {
  ///     const FunctionType *fn = unwrapped.get();
  ///     // change fn somehow
  ///     T = unwrapped.wrap(fn);
  ///   }
  struct FunctionTypeUnwrapper {
    enum WrapKind {
      Desugar,
      Attributed,
      Parens,
      Array,
      Pointer,
      BlockPointer,
      Reference,
      MemberPointer,
      MacroQualified,
    };

    QualType Original;
    const FunctionType *Fn;
    SmallVector<unsigned char /*WrapKind*/, 8> Stack;

    FunctionTypeUnwrapper(Sema &S, QualType T) : Original(T) {
      while (true) {
        const Type *Ty = T.getTypePtr();
        if (isa<FunctionType>(Ty)) {
          Fn = cast<FunctionType>(Ty);
          return;
        } else if (isa<ParenType>(Ty)) {
          T = cast<ParenType>(Ty)->getInnerType();
          Stack.push_back(Parens);
        } else if (isa<ConstantArrayType>(Ty) || isa<VariableArrayType>(Ty) ||
                   isa<IncompleteArrayType>(Ty)) {
          T = cast<ArrayType>(Ty)->getElementType();
          Stack.push_back(Array);
        } else if (isa<PointerType>(Ty)) {
          T = cast<PointerType>(Ty)->getPointeeType();
          Stack.push_back(Pointer);
        } else if (isa<BlockPointerType>(Ty)) {
          T = cast<BlockPointerType>(Ty)->getPointeeType();
          Stack.push_back(BlockPointer);
        } else if (isa<MemberPointerType>(Ty)) {
          T = cast<MemberPointerType>(Ty)->getPointeeType();
          Stack.push_back(MemberPointer);
        } else if (isa<ReferenceType>(Ty)) {
          T = cast<ReferenceType>(Ty)->getPointeeType();
          Stack.push_back(Reference);
        } else if (isa<AttributedType>(Ty)) {
          T = cast<AttributedType>(Ty)->getEquivalentType();
          Stack.push_back(Attributed);
        } else if (isa<MacroQualifiedType>(Ty)) {
          T = cast<MacroQualifiedType>(Ty)->getUnderlyingType();
          Stack.push_back(MacroQualified);
        } else {
          const Type *DTy = Ty->getUnqualifiedDesugaredType();
          if (Ty == DTy) {
            Fn = nullptr;
            return;
          }

          T = QualType(DTy, 0);
          Stack.push_back(Desugar);
        }
      }
    }

    bool isFunctionType() const { return (Fn != nullptr); }
    const FunctionType *get() const { return Fn; }

    QualType wrap(Sema &S, const FunctionType *New) {
      // If T wasn't modified from the unwrapped type, do nothing.
      if (New == get()) return Original;

      Fn = New;
      return wrap(S.Context, Original, 0);
    }

  private:
    QualType wrap(ASTContext &C, QualType Old, unsigned I) {
      if (I == Stack.size())
        return C.getQualifiedType(Fn, Old.getQualifiers());

      // Build up the inner type, applying the qualifiers from the old
      // type to the new type.
      SplitQualType SplitOld = Old.split();

      // As a special case, tail-recurse if there are no qualifiers.
      if (SplitOld.Quals.empty())
        return wrap(C, SplitOld.Ty, I);
      return C.getQualifiedType(wrap(C, SplitOld.Ty, I), SplitOld.Quals);
    }

    QualType wrap(ASTContext &C, const Type *Old, unsigned I) {
      if (I == Stack.size()) return QualType(Fn, 0);

      switch (static_cast<WrapKind>(Stack[I++])) {
      case Desugar:
        // This is the point at which we potentially lose source
        // information.
        return wrap(C, Old->getUnqualifiedDesugaredType(), I);

      case Attributed:
        return wrap(C, cast<AttributedType>(Old)->getEquivalentType(), I);

      case Parens: {
        QualType New = wrap(C, cast<ParenType>(Old)->getInnerType(), I);
        return C.getParenType(New);
      }

      case MacroQualified:
        return wrap(C, cast<MacroQualifiedType>(Old)->getUnderlyingType(), I);

      case Array: {
        if (const auto *CAT = dyn_cast<ConstantArrayType>(Old)) {
          QualType New = wrap(C, CAT->getElementType(), I);
          return C.getConstantArrayType(New, CAT->getSize(), CAT->getSizeExpr(),
                                        CAT->getSizeModifier(),
                                        CAT->getIndexTypeCVRQualifiers());
        }

        if (const auto *VAT = dyn_cast<VariableArrayType>(Old)) {
          QualType New = wrap(C, VAT->getElementType(), I);
          return C.getVariableArrayType(
              New, VAT->getSizeExpr(), VAT->getSizeModifier(),
              VAT->getIndexTypeCVRQualifiers(), VAT->getBracketsRange());
        }

        const auto *IAT = cast<IncompleteArrayType>(Old);
        QualType New = wrap(C, IAT->getElementType(), I);
        return C.getIncompleteArrayType(New, IAT->getSizeModifier(),
                                        IAT->getIndexTypeCVRQualifiers());
      }

      case Pointer: {
        QualType New = wrap(C, cast<PointerType>(Old)->getPointeeType(), I);
        return C.getPointerType(New);
      }

      case BlockPointer: {
        QualType New = wrap(C, cast<BlockPointerType>(Old)->getPointeeType(),I);
        return C.getBlockPointerType(New);
      }

      case MemberPointer: {
        const MemberPointerType *OldMPT = cast<MemberPointerType>(Old);
        QualType New = wrap(C, OldMPT->getPointeeType(), I);
        return C.getMemberPointerType(New, OldMPT->getClass());
      }

      case Reference: {
        const ReferenceType *OldRef = cast<ReferenceType>(Old);
        QualType New = wrap(C, OldRef->getPointeeType(), I);
        if (isa<LValueReferenceType>(OldRef))
          return C.getLValueReferenceType(New, OldRef->isSpelledAsLValue());
        else
          return C.getRValueReferenceType(New);
      }
      }

      llvm_unreachable("unknown wrapping kind");
    }
  };
} // end anonymous namespace

static bool handleMSPointerTypeQualifierAttr(TypeProcessingState &State,
                                             ParsedAttr &PAttr, QualType &Type) {
  Sema &S = State.getSema();

  Attr *A;
  switch (PAttr.getKind()) {
  default: llvm_unreachable("Unknown attribute kind");
  case ParsedAttr::AT_Ptr32:
    A = createSimpleAttr<Ptr32Attr>(S.Context, PAttr);
    break;
  case ParsedAttr::AT_Ptr64:
    A = createSimpleAttr<Ptr64Attr>(S.Context, PAttr);
    break;
  case ParsedAttr::AT_SPtr:
    A = createSimpleAttr<SPtrAttr>(S.Context, PAttr);
    break;
  case ParsedAttr::AT_UPtr:
    A = createSimpleAttr<UPtrAttr>(S.Context, PAttr);
    break;
  }

  std::bitset<attr::LastAttr> Attrs;
  QualType Desugared = Type;
  for (;;) {
    if (const TypedefType *TT = dyn_cast<TypedefType>(Desugared)) {
      Desugared = TT->desugar();
      continue;
    } else if (const ElaboratedType *ET = dyn_cast<ElaboratedType>(Desugared)) {
      Desugared = ET->desugar();
      continue;
    }
    const AttributedType *AT = dyn_cast<AttributedType>(Desugared);
    if (!AT)
      break;
    Attrs[AT->getAttrKind()] = true;
    Desugared = AT->getModifiedType();
  }

  // You cannot specify duplicate type attributes, so if the attribute has
  // already been applied, flag it.
  attr::Kind NewAttrKind = A->getKind();
  if (Attrs[NewAttrKind]) {
    S.Diag(PAttr.getLoc(), diag::warn_duplicate_attribute_exact) << PAttr;
    return true;
  }
  Attrs[NewAttrKind] = true;

  // You cannot have both __sptr and __uptr on the same type, nor can you
  // have __ptr32 and __ptr64.
  if (Attrs[attr::Ptr32] && Attrs[attr::Ptr64]) {
    S.Diag(PAttr.getLoc(), diag::err_attributes_are_not_compatible)
        << "'__ptr32'"
        << "'__ptr64'" << /*isRegularKeyword=*/0;
    return true;
  } else if (Attrs[attr::SPtr] && Attrs[attr::UPtr]) {
    S.Diag(PAttr.getLoc(), diag::err_attributes_are_not_compatible)
        << "'__sptr'"
        << "'__uptr'" << /*isRegularKeyword=*/0;
    return true;
  }

  // Check the raw (i.e., desugared) Canonical type to see if it
  // is a pointer type.
  if (!isa<PointerType>(Desugared)) {
    // Pointer type qualifiers can only operate on pointer types, but not
    // pointer-to-member types.
    if (Type->isMemberPointerType())
      S.Diag(PAttr.getLoc(), diag::err_attribute_no_member_pointers) << PAttr;
    else
      S.Diag(PAttr.getLoc(), diag::err_attribute_pointers_only) << PAttr << 0;
    return true;
  }

  // Add address space to type based on its attributes.
  LangAS ASIdx = LangAS::Default;
  uint64_t PtrWidth =
      S.Context.getTargetInfo().getPointerWidth(LangAS::Default);
  if (PtrWidth == 32) {
    if (Attrs[attr::Ptr64])
      ASIdx = LangAS::ptr64;
    else if (Attrs[attr::UPtr])
      ASIdx = LangAS::ptr32_uptr;
  } else if (PtrWidth == 64 && Attrs[attr::Ptr32]) {
    if (Attrs[attr::UPtr])
      ASIdx = LangAS::ptr32_uptr;
    else
      ASIdx = LangAS::ptr32_sptr;
  }

  QualType Pointee = Type->getPointeeType();
  if (ASIdx != LangAS::Default)
    Pointee = S.Context.getAddrSpaceQualType(
        S.Context.removeAddrSpaceQualType(Pointee), ASIdx);
  Type = State.getAttributedType(A, Type, S.Context.getPointerType(Pointee));
  return false;
}

static bool HandleWebAssemblyFuncrefAttr(TypeProcessingState &State,
                                         QualType &QT, ParsedAttr &PAttr) {
  assert(PAttr.getKind() == ParsedAttr::AT_WebAssemblyFuncref);

  Sema &S = State.getSema();
  Attr *A = createSimpleAttr<WebAssemblyFuncrefAttr>(S.Context, PAttr);

  std::bitset<attr::LastAttr> Attrs;
  attr::Kind NewAttrKind = A->getKind();
  const auto *AT = dyn_cast<AttributedType>(QT);
  while (AT) {
    Attrs[AT->getAttrKind()] = true;
    AT = dyn_cast<AttributedType>(AT->getModifiedType());
  }

  // You cannot specify duplicate type attributes, so if the attribute has
  // already been applied, flag it.
  if (Attrs[NewAttrKind]) {
    S.Diag(PAttr.getLoc(), diag::warn_duplicate_attribute_exact) << PAttr;
    return true;
  }

  // Add address space to type based on its attributes.
  LangAS ASIdx = LangAS::wasm_funcref;
  QualType Pointee = QT->getPointeeType();
  Pointee = S.Context.getAddrSpaceQualType(
      S.Context.removeAddrSpaceQualType(Pointee), ASIdx);
  QT = State.getAttributedType(A, QT, S.Context.getPointerType(Pointee));
  return false;
}

/// Rebuild an attributed type without the nullability attribute on it.
static QualType rebuildAttributedTypeWithoutNullability(ASTContext &Ctx,
                                                        QualType Type) {
  auto Attributed = dyn_cast<AttributedType>(Type.getTypePtr());
  if (!Attributed)
    return Type;

  // Skip the nullability attribute; we're done.
  if (Attributed->getImmediateNullability())
    return Attributed->getModifiedType();

  // Build the modified type.
  QualType Modified = rebuildAttributedTypeWithoutNullability(
      Ctx, Attributed->getModifiedType());
  assert(Modified.getTypePtr() != Attributed->getModifiedType().getTypePtr());
  return Ctx.getAttributedType(Attributed->getAttrKind(), Modified,
                               Attributed->getEquivalentType());
}

/// Map a nullability attribute kind to a nullability kind.
static NullabilityKind mapNullabilityAttrKind(ParsedAttr::Kind kind) {
  switch (kind) {
  case ParsedAttr::AT_TypeNonNull:
    return NullabilityKind::NonNull;

  case ParsedAttr::AT_TypeNullable:
    return NullabilityKind::Nullable;

  case ParsedAttr::AT_TypeNullableResult:
    return NullabilityKind::NullableResult;

  case ParsedAttr::AT_TypeNullUnspecified:
    return NullabilityKind::Unspecified;

  default:
    llvm_unreachable("not a nullability attribute kind");
  }
}

static bool CheckNullabilityTypeSpecifier(
    Sema &S, TypeProcessingState *State, ParsedAttr *PAttr, QualType &QT,
    NullabilityKind Nullability, SourceLocation NullabilityLoc,
    bool IsContextSensitive, bool AllowOnArrayType, bool OverrideExisting) {
  bool Implicit = (State == nullptr);
  if (!Implicit)
    recordNullabilitySeen(S, NullabilityLoc);

  // Check for existing nullability attributes on the type.
  QualType Desugared = QT;
  while (auto *Attributed = dyn_cast<AttributedType>(Desugared.getTypePtr())) {
    // Check whether there is already a null
    if (auto ExistingNullability = Attributed->getImmediateNullability()) {
      // Duplicated nullability.
      if (Nullability == *ExistingNullability) {
        if (Implicit)
          break;

        S.Diag(NullabilityLoc, diag::warn_nullability_duplicate)
            << DiagNullabilityKind(Nullability, IsContextSensitive)
            << FixItHint::CreateRemoval(NullabilityLoc);

        break;
      }

      if (!OverrideExisting) {
        // Conflicting nullability.
        S.Diag(NullabilityLoc, diag::err_nullability_conflicting)
            << DiagNullabilityKind(Nullability, IsContextSensitive)
            << DiagNullabilityKind(*ExistingNullability, false);
        return true;
      }

      // Rebuild the attributed type, dropping the existing nullability.
      QT = rebuildAttributedTypeWithoutNullability(S.Context, QT);
    }

    Desugared = Attributed->getModifiedType();
  }

  // If there is already a different nullability specifier, complain.
  // This (unlike the code above) looks through typedefs that might
  // have nullability specifiers on them, which means we cannot
  // provide a useful Fix-It.
  if (auto ExistingNullability = Desugared->getNullability()) {
    if (Nullability != *ExistingNullability && !Implicit) {
      S.Diag(NullabilityLoc, diag::err_nullability_conflicting)
          << DiagNullabilityKind(Nullability, IsContextSensitive)
          << DiagNullabilityKind(*ExistingNullability, false);

      // Try to find the typedef with the existing nullability specifier.
      if (auto TT = Desugared->getAs<TypedefType>()) {
        TypedefNameDecl *typedefDecl = TT->getDecl();
        QualType underlyingType = typedefDecl->getUnderlyingType();
        if (auto typedefNullability =
                AttributedType::stripOuterNullability(underlyingType)) {
          if (*typedefNullability == *ExistingNullability) {
            S.Diag(typedefDecl->getLocation(), diag::note_nullability_here)
                << DiagNullabilityKind(*ExistingNullability, false);
          }
        }
      }

      return true;
    }
  }

  // If this definitely isn't a pointer type, reject the specifier.
  if (!Desugared->canHaveNullability() &&
      !(AllowOnArrayType && Desugared->isArrayType())) {
    if (!Implicit)
      S.Diag(NullabilityLoc, diag::err_nullability_nonpointer)
          << DiagNullabilityKind(Nullability, IsContextSensitive) << QT;

    return true;
  }

  // For the context-sensitive keywords/Objective-C property
  // attributes, require that the type be a single-level pointer.
  if (IsContextSensitive) {
    // Make sure that the pointee isn't itself a pointer type.
    const Type *pointeeType = nullptr;
    if (Desugared->isArrayType())
      pointeeType = Desugared->getArrayElementTypeNoTypeQual();
    else if (Desugared->isAnyPointerType())
      pointeeType = Desugared->getPointeeType().getTypePtr();

    if (pointeeType && (pointeeType->isAnyPointerType() ||
                        pointeeType->isObjCObjectPointerType() ||
                        pointeeType->isMemberPointerType())) {
      S.Diag(NullabilityLoc, diag::err_nullability_cs_multilevel)
          << DiagNullabilityKind(Nullability, true) << QT;
      S.Diag(NullabilityLoc, diag::note_nullability_type_specifier)
          << DiagNullabilityKind(Nullability, false) << QT
          << FixItHint::CreateReplacement(NullabilityLoc,
                                          getNullabilitySpelling(Nullability));
      return true;
    }
  }

  // Form the attributed type.
  if (State) {
    assert(PAttr);
    Attr *A = createNullabilityAttr(S.Context, *PAttr, Nullability);
    QT = State->getAttributedType(A, QT, QT);
  } else {
    attr::Kind attrKind = AttributedType::getNullabilityAttrKind(Nullability);
    QT = S.Context.getAttributedType(attrKind, QT, QT);
  }
  return false;
}

static bool CheckNullabilityTypeSpecifier(TypeProcessingState &State,
                                          QualType &Type, ParsedAttr &Attr,
                                          bool AllowOnArrayType) {
  NullabilityKind Nullability = mapNullabilityAttrKind(Attr.getKind());
  SourceLocation NullabilityLoc = Attr.getLoc();
  bool IsContextSensitive = Attr.isContextSensitiveKeywordAttribute();

  return CheckNullabilityTypeSpecifier(State.getSema(), &State, &Attr, Type,
                                       Nullability, NullabilityLoc,
                                       IsContextSensitive, AllowOnArrayType,
                                       /*overrideExisting*/ false);
}

bool Sema::CheckImplicitNullabilityTypeSpecifier(QualType &Type,
                                                 NullabilityKind Nullability,
                                                 SourceLocation DiagLoc,
                                                 bool AllowArrayTypes,
                                                 bool OverrideExisting) {
  return CheckNullabilityTypeSpecifier(
      *this, nullptr, nullptr, Type, Nullability, DiagLoc,
      /*isContextSensitive*/ false, AllowArrayTypes, OverrideExisting);
}

/// Check the application of the Objective-C '__kindof' qualifier to
/// the given type.
static bool checkObjCKindOfType(TypeProcessingState &state, QualType &type,
                                ParsedAttr &attr) {
  Sema &S = state.getSema();

  if (isa<ObjCTypeParamType>(type)) {
    // Build the attributed type to record where __kindof occurred.
    type = state.getAttributedType(
        createSimpleAttr<ObjCKindOfAttr>(S.Context, attr), type, type);
    return false;
  }

  // Find out if it's an Objective-C object or object pointer type;
  const ObjCObjectPointerType *ptrType = type->getAs<ObjCObjectPointerType>();
  const ObjCObjectType *objType = ptrType ? ptrType->getObjectType()
                                          : type->getAs<ObjCObjectType>();

  // If not, we can't apply __kindof.
  if (!objType) {
    // FIXME: Handle dependent types that aren't yet object types.
    S.Diag(attr.getLoc(), diag::err_objc_kindof_nonobject)
      << type;
    return true;
  }

  // Rebuild the "equivalent" type, which pushes __kindof down into
  // the object type.
  // There is no need to apply kindof on an unqualified id type.
  QualType equivType = S.Context.getObjCObjectType(
      objType->getBaseType(), objType->getTypeArgsAsWritten(),
      objType->getProtocols(),
      /*isKindOf=*/objType->isObjCUnqualifiedId() ? false : true);

  // If we started with an object pointer type, rebuild it.
  if (ptrType) {
    equivType = S.Context.getObjCObjectPointerType(equivType);
    if (auto nullability = type->getNullability()) {
      // We create a nullability attribute from the __kindof attribute.
      // Make sure that will make sense.
      assert(attr.getAttributeSpellingListIndex() == 0 &&
             "multiple spellings for __kindof?");
      Attr *A = createNullabilityAttr(S.Context, attr, *nullability);
      A->setImplicit(true);
      equivType = state.getAttributedType(A, equivType, equivType);
    }
  }

  // Build the attributed type to record where __kindof occurred.
  type = state.getAttributedType(
      createSimpleAttr<ObjCKindOfAttr>(S.Context, attr), type, equivType);
  return false;
}

/// Distribute a nullability type attribute that cannot be applied to
/// the type specifier to a pointer, block pointer, or member pointer
/// declarator, complaining if necessary.
///
/// \returns true if the nullability annotation was distributed, false
/// otherwise.
static bool distributeNullabilityTypeAttr(TypeProcessingState &state,
                                          QualType type, ParsedAttr &attr) {
  Declarator &declarator = state.getDeclarator();

  /// Attempt to move the attribute to the specified chunk.
  auto moveToChunk = [&](DeclaratorChunk &chunk, bool inFunction) -> bool {
    // If there is already a nullability attribute there, don't add
    // one.
    if (hasNullabilityAttr(chunk.getAttrs()))
      return false;

    // Complain about the nullability qualifier being in the wrong
    // place.
    enum {
      PK_Pointer,
      PK_BlockPointer,
      PK_MemberPointer,
      PK_FunctionPointer,
      PK_MemberFunctionPointer,
    } pointerKind
      = chunk.Kind == DeclaratorChunk::Pointer ? (inFunction ? PK_FunctionPointer
                                                             : PK_Pointer)
        : chunk.Kind == DeclaratorChunk::BlockPointer ? PK_BlockPointer
        : inFunction? PK_MemberFunctionPointer : PK_MemberPointer;

    auto diag = state.getSema().Diag(attr.getLoc(),
                                     diag::warn_nullability_declspec)
      << DiagNullabilityKind(mapNullabilityAttrKind(attr.getKind()),
                             attr.isContextSensitiveKeywordAttribute())
      << type
      << static_cast<unsigned>(pointerKind);

    // FIXME: MemberPointer chunks don't carry the location of the *.
    if (chunk.Kind != DeclaratorChunk::MemberPointer) {
      diag << FixItHint::CreateRemoval(attr.getLoc())
           << FixItHint::CreateInsertion(
                  state.getSema().getPreprocessor().getLocForEndOfToken(
                      chunk.Loc),
                  " " + attr.getAttrName()->getName().str() + " ");
    }

    moveAttrFromListToList(attr, state.getCurrentAttributes(),
                           chunk.getAttrs());
    return true;
  };

  // Move it to the outermost pointer, member pointer, or block
  // pointer declarator.
  for (unsigned i = state.getCurrentChunkIndex(); i != 0; --i) {
    DeclaratorChunk &chunk = declarator.getTypeObject(i-1);
    switch (chunk.Kind) {
    case DeclaratorChunk::Pointer:
    case DeclaratorChunk::BlockPointer:
    case DeclaratorChunk::MemberPointer:
      return moveToChunk(chunk, false);

    case DeclaratorChunk::Paren:
    case DeclaratorChunk::Array:
      continue;

    case DeclaratorChunk::Function:
      // Try to move past the return type to a function/block/member
      // function pointer.
      if (DeclaratorChunk *dest = maybeMovePastReturnType(
                                    declarator, i,
                                    /*onlyBlockPointers=*/false)) {
        return moveToChunk(*dest, true);
      }

      return false;

    // Don't walk through these.
    case DeclaratorChunk::Reference:
    case DeclaratorChunk::Pipe:
      return false;
    }
  }

  return false;
}

static Attr *getCCTypeAttr(ASTContext &Ctx, ParsedAttr &Attr) {
  assert(!Attr.isInvalid());
  switch (Attr.getKind()) {
  default:
    llvm_unreachable("not a calling convention attribute");
  case ParsedAttr::AT_CDecl:
    return createSimpleAttr<CDeclAttr>(Ctx, Attr);
  case ParsedAttr::AT_FastCall:
    return createSimpleAttr<FastCallAttr>(Ctx, Attr);
  case ParsedAttr::AT_StdCall:
    return createSimpleAttr<StdCallAttr>(Ctx, Attr);
  case ParsedAttr::AT_ThisCall:
    return createSimpleAttr<ThisCallAttr>(Ctx, Attr);
  case ParsedAttr::AT_RegCall:
    return createSimpleAttr<RegCallAttr>(Ctx, Attr);
  case ParsedAttr::AT_Pascal:
    return createSimpleAttr<PascalAttr>(Ctx, Attr);
  case ParsedAttr::AT_SwiftCall:
    return createSimpleAttr<SwiftCallAttr>(Ctx, Attr);
  case ParsedAttr::AT_SwiftAsyncCall:
    return createSimpleAttr<SwiftAsyncCallAttr>(Ctx, Attr);
  case ParsedAttr::AT_VectorCall:
    return createSimpleAttr<VectorCallAttr>(Ctx, Attr);
  case ParsedAttr::AT_AArch64VectorPcs:
    return createSimpleAttr<AArch64VectorPcsAttr>(Ctx, Attr);
  case ParsedAttr::AT_AArch64SVEPcs:
    return createSimpleAttr<AArch64SVEPcsAttr>(Ctx, Attr);
  case ParsedAttr::AT_ArmStreaming:
    return createSimpleAttr<ArmStreamingAttr>(Ctx, Attr);
  case ParsedAttr::AT_AMDGPUKernelCall:
    return createSimpleAttr<AMDGPUKernelCallAttr>(Ctx, Attr);
  case ParsedAttr::AT_Pcs: {
    // The attribute may have had a fixit applied where we treated an
    // identifier as a string literal.  The contents of the string are valid,
    // but the form may not be.
    StringRef Str;
    if (Attr.isArgExpr(0))
      Str = cast<StringLiteral>(Attr.getArgAsExpr(0))->getString();
    else
      Str = Attr.getArgAsIdent(0)->Ident->getName();
    PcsAttr::PCSType Type;
    if (!PcsAttr::ConvertStrToPCSType(Str, Type))
      llvm_unreachable("already validated the attribute");
    return ::new (Ctx) PcsAttr(Ctx, Attr, Type);
  }
  case ParsedAttr::AT_IntelOclBicc:
    return createSimpleAttr<IntelOclBiccAttr>(Ctx, Attr);
  case ParsedAttr::AT_MSABI:
    return createSimpleAttr<MSABIAttr>(Ctx, Attr);
  case ParsedAttr::AT_SysVABI:
    return createSimpleAttr<SysVABIAttr>(Ctx, Attr);
  case ParsedAttr::AT_PreserveMost:
    return createSimpleAttr<PreserveMostAttr>(Ctx, Attr);
  case ParsedAttr::AT_PreserveAll:
    return createSimpleAttr<PreserveAllAttr>(Ctx, Attr);
  case ParsedAttr::AT_M68kRTD:
    return createSimpleAttr<M68kRTDAttr>(Ctx, Attr);
  case ParsedAttr::AT_PreserveNone:
    return createSimpleAttr<PreserveNoneAttr>(Ctx, Attr);
  case ParsedAttr::AT_RISCVVectorCC:
    return createSimpleAttr<RISCVVectorCCAttr>(Ctx, Attr);
  }
  llvm_unreachable("unexpected attribute kind!");
}

std::optional<FunctionEffectMode>
Sema::ActOnEffectExpression(Expr *CondExpr, StringRef AttributeName) {
  if (CondExpr->isTypeDependent() || CondExpr->isValueDependent())
    return FunctionEffectMode::Dependent;

  std::optional<llvm::APSInt> ConditionValue =
      CondExpr->getIntegerConstantExpr(Context);
  if (!ConditionValue) {
    // FIXME: err_attribute_argument_type doesn't quote the attribute
    // name but needs to; users are inconsistent.
    Diag(CondExpr->getExprLoc(), diag::err_attribute_argument_type)
        << AttributeName << AANT_ArgumentIntegerConstant
        << CondExpr->getSourceRange();
    return std::nullopt;
  }
  return !ConditionValue->isZero() ? FunctionEffectMode::True
                                   : FunctionEffectMode::False;
}

static bool
handleNonBlockingNonAllocatingTypeAttr(TypeProcessingState &TPState,
                                       ParsedAttr &PAttr, QualType &QT,
                                       FunctionTypeUnwrapper &Unwrapped) {
  // Delay if this is not a function type.
  if (!Unwrapped.isFunctionType())
    return false;

  Sema &S = TPState.getSema();

  // Require FunctionProtoType.
  auto *FPT = Unwrapped.get()->getAs<FunctionProtoType>();
  if (FPT == nullptr) {
    S.Diag(PAttr.getLoc(), diag::err_func_with_effects_no_prototype)
        << PAttr.getAttrName()->getName();
    return true;
  }

  // Parse the new  attribute.
  // non/blocking or non/allocating? Or conditional (computed)?
  bool IsNonBlocking = PAttr.getKind() == ParsedAttr::AT_NonBlocking ||
                       PAttr.getKind() == ParsedAttr::AT_Blocking;

  FunctionEffectMode NewMode = FunctionEffectMode::None;
  Expr *CondExpr = nullptr; // only valid if dependent

  if (PAttr.getKind() == ParsedAttr::AT_NonBlocking ||
      PAttr.getKind() == ParsedAttr::AT_NonAllocating) {
    if (!PAttr.checkAtMostNumArgs(S, 1)) {
      PAttr.setInvalid();
      return true;
    }

    // Parse the condition, if any.
    if (PAttr.getNumArgs() == 1) {
      CondExpr = PAttr.getArgAsExpr(0);
      std::optional<FunctionEffectMode> MaybeMode =
          S.ActOnEffectExpression(CondExpr, PAttr.getAttrName()->getName());
      if (!MaybeMode) {
        PAttr.setInvalid();
        return true;
      }
      NewMode = *MaybeMode;
      if (NewMode != FunctionEffectMode::Dependent)
        CondExpr = nullptr;
    } else {
      NewMode = FunctionEffectMode::True;
    }
  } else {
    // This is the `blocking` or `allocating` attribute.
    if (S.CheckAttrNoArgs(PAttr)) {
      // The attribute has been marked invalid.
      return true;
    }
    NewMode = FunctionEffectMode::False;
  }

  const FunctionEffect::Kind FEKind =
      (NewMode == FunctionEffectMode::False)
          ? (IsNonBlocking ? FunctionEffect::Kind::Blocking
                           : FunctionEffect::Kind::Allocating)
          : (IsNonBlocking ? FunctionEffect::Kind::NonBlocking
                           : FunctionEffect::Kind::NonAllocating);
  const FunctionEffectWithCondition NewEC{FunctionEffect(FEKind),
                                          EffectConditionExpr(CondExpr)};

  if (S.diagnoseConflictingFunctionEffect(FPT->getFunctionEffects(), NewEC,
                                          PAttr.getLoc())) {
    PAttr.setInvalid();
    return true;
  }

  // Add the effect to the FunctionProtoType.
  FunctionProtoType::ExtProtoInfo EPI = FPT->getExtProtoInfo();
  FunctionEffectSet FX(EPI.FunctionEffects);
  FunctionEffectSet::Conflicts Errs;
  [[maybe_unused]] bool Success = FX.insert(NewEC, Errs);
  assert(Success && "effect conflicts should have been diagnosed above");
  EPI.FunctionEffects = FunctionEffectsRef(FX);

  QualType NewType = S.Context.getFunctionType(FPT->getReturnType(),
                                               FPT->getParamTypes(), EPI);
  QT = Unwrapped.wrap(S, NewType->getAs<FunctionType>());
  return true;
}

static bool checkMutualExclusion(TypeProcessingState &state,
                                 const FunctionProtoType::ExtProtoInfo &EPI,
                                 ParsedAttr &Attr,
                                 AttributeCommonInfo::Kind OtherKind) {
  auto OtherAttr = std::find_if(
      state.getCurrentAttributes().begin(), state.getCurrentAttributes().end(),
      [OtherKind](const ParsedAttr &A) { return A.getKind() == OtherKind; });
  if (OtherAttr == state.getCurrentAttributes().end() || OtherAttr->isInvalid())
    return false;

  Sema &S = state.getSema();
  S.Diag(Attr.getLoc(), diag::err_attributes_are_not_compatible)
      << *OtherAttr << Attr
      << (OtherAttr->isRegularKeywordAttribute() ||
          Attr.isRegularKeywordAttribute());
  S.Diag(OtherAttr->getLoc(), diag::note_conflicting_attribute);
  Attr.setInvalid();
  return true;
}

static bool handleArmStateAttribute(Sema &S,
                                    FunctionProtoType::ExtProtoInfo &EPI,
                                    ParsedAttr &Attr,
                                    FunctionType::ArmStateValue State) {
  if (!Attr.getNumArgs()) {
    S.Diag(Attr.getLoc(), diag::err_missing_arm_state) << Attr;
    Attr.setInvalid();
    return true;
  }

  for (unsigned I = 0; I < Attr.getNumArgs(); ++I) {
    StringRef StateName;
    SourceLocation LiteralLoc;
    if (!S.checkStringLiteralArgumentAttr(Attr, I, StateName, &LiteralLoc))
      return true;

    unsigned Shift;
    FunctionType::ArmStateValue ExistingState;
    if (StateName == "za") {
      Shift = FunctionType::SME_ZAShift;
      ExistingState = FunctionType::getArmZAState(EPI.AArch64SMEAttributes);
    } else if (StateName == "zt0") {
      Shift = FunctionType::SME_ZT0Shift;
      ExistingState = FunctionType::getArmZT0State(EPI.AArch64SMEAttributes);
    } else {
      S.Diag(LiteralLoc, diag::err_unknown_arm_state) << StateName;
      Attr.setInvalid();
      return true;
    }

    // __arm_in(S), __arm_out(S), __arm_inout(S) and __arm_preserves(S)
    // are all mutually exclusive for the same S, so check if there are
    // conflicting attributes.
    if (ExistingState != FunctionType::ARM_None && ExistingState != State) {
      S.Diag(LiteralLoc, diag::err_conflicting_attributes_arm_state)
          << StateName;
      Attr.setInvalid();
      return true;
    }

    EPI.setArmSMEAttribute(
        (FunctionType::AArch64SMETypeAttributes)((State << Shift)));
  }
  return false;
}

/// Process an individual function attribute.  Returns true to
/// indicate that the attribute was handled, false if it wasn't.
static bool handleFunctionTypeAttr(TypeProcessingState &state, ParsedAttr &attr,
                                   QualType &type, CUDAFunctionTarget CFT) {
  Sema &S = state.getSema();

  FunctionTypeUnwrapper unwrapped(S, type);

  if (attr.getKind() == ParsedAttr::AT_NoReturn) {
    if (S.CheckAttrNoArgs(attr))
      return true;

    // Delay if this is not a function type.
    if (!unwrapped.isFunctionType())
      return false;

    // Otherwise we can process right away.
    FunctionType::ExtInfo EI = unwrapped.get()->getExtInfo().withNoReturn(true);
    type = unwrapped.wrap(S, S.Context.adjustFunctionType(unwrapped.get(), EI));
    return true;
  }

  if (attr.getKind() == ParsedAttr::AT_CmseNSCall) {
    // Delay if this is not a function type.
    if (!unwrapped.isFunctionType())
      return false;

    // Ignore if we don't have CMSE enabled.
    if (!S.getLangOpts().Cmse) {
      S.Diag(attr.getLoc(), diag::warn_attribute_ignored) << attr;
      attr.setInvalid();
      return true;
    }

    // Otherwise we can process right away.
    FunctionType::ExtInfo EI =
        unwrapped.get()->getExtInfo().withCmseNSCall(true);
    type = unwrapped.wrap(S, S.Context.adjustFunctionType(unwrapped.get(), EI));
    return true;
  }

  // ns_returns_retained is not always a type attribute, but if we got
  // here, we're treating it as one right now.
  if (attr.getKind() == ParsedAttr::AT_NSReturnsRetained) {
    if (attr.getNumArgs()) return true;

    // Delay if this is not a function type.
    if (!unwrapped.isFunctionType())
      return false;

    // Check whether the return type is reasonable.
    if (S.ObjC().checkNSReturnsRetainedReturnType(
            attr.getLoc(), unwrapped.get()->getReturnType()))
      return true;

    // Only actually change the underlying type in ARC builds.
    QualType origType = type;
    if (state.getSema().getLangOpts().ObjCAutoRefCount) {
      FunctionType::ExtInfo EI
        = unwrapped.get()->getExtInfo().withProducesResult(true);
      type = unwrapped.wrap(S, S.Context.adjustFunctionType(unwrapped.get(), EI));
    }
    type = state.getAttributedType(
        createSimpleAttr<NSReturnsRetainedAttr>(S.Context, attr),
        origType, type);
    return true;
  }

  if (attr.getKind() == ParsedAttr::AT_AnyX86NoCallerSavedRegisters) {
    if (S.CheckAttrTarget(attr) || S.CheckAttrNoArgs(attr))
      return true;

    // Delay if this is not a function type.
    if (!unwrapped.isFunctionType())
      return false;

    FunctionType::ExtInfo EI =
        unwrapped.get()->getExtInfo().withNoCallerSavedRegs(true);
    type = unwrapped.wrap(S, S.Context.adjustFunctionType(unwrapped.get(), EI));
    return true;
  }

  if (attr.getKind() == ParsedAttr::AT_AnyX86NoCfCheck) {
    if (!S.getLangOpts().CFProtectionBranch) {
      S.Diag(attr.getLoc(), diag::warn_nocf_check_attribute_ignored);
      attr.setInvalid();
      return true;
    }

    if (S.CheckAttrTarget(attr) || S.CheckAttrNoArgs(attr))
      return true;

    // If this is not a function type, warning will be asserted by subject
    // check.
    if (!unwrapped.isFunctionType())
      return true;

    FunctionType::ExtInfo EI =
      unwrapped.get()->getExtInfo().withNoCfCheck(true);
    type = unwrapped.wrap(S, S.Context.adjustFunctionType(unwrapped.get(), EI));
    return true;
  }

  if (attr.getKind() == ParsedAttr::AT_Regparm) {
    unsigned value;
    if (S.CheckRegparmAttr(attr, value))
      return true;

    // Delay if this is not a function type.
    if (!unwrapped.isFunctionType())
      return false;

    // Diagnose regparm with fastcall.
    const FunctionType *fn = unwrapped.get();
    CallingConv CC = fn->getCallConv();
    if (CC == CC_X86FastCall) {
      S.Diag(attr.getLoc(), diag::err_attributes_are_not_compatible)
          << FunctionType::getNameForCallConv(CC) << "regparm"
          << attr.isRegularKeywordAttribute();
      attr.setInvalid();
      return true;
    }

    FunctionType::ExtInfo EI =
      unwrapped.get()->getExtInfo().withRegParm(value);
    type = unwrapped.wrap(S, S.Context.adjustFunctionType(unwrapped.get(), EI));
    return true;
  }

  if (attr.getKind() == ParsedAttr::AT_ArmStreaming ||
      attr.getKind() == ParsedAttr::AT_ArmStreamingCompatible ||
      attr.getKind() == ParsedAttr::AT_ArmPreserves ||
      attr.getKind() == ParsedAttr::AT_ArmIn ||
      attr.getKind() == ParsedAttr::AT_ArmOut ||
      attr.getKind() == ParsedAttr::AT_ArmInOut) {
    if (S.CheckAttrTarget(attr))
      return true;

    if (attr.getKind() == ParsedAttr::AT_ArmStreaming ||
        attr.getKind() == ParsedAttr::AT_ArmStreamingCompatible)
      if (S.CheckAttrNoArgs(attr))
        return true;

    if (!unwrapped.isFunctionType())
      return false;

    const auto *FnTy = unwrapped.get()->getAs<FunctionProtoType>();
    if (!FnTy) {
      // SME ACLE attributes are not supported on K&R-style unprototyped C
      // functions.
      S.Diag(attr.getLoc(), diag::warn_attribute_wrong_decl_type) <<
        attr << attr.isRegularKeywordAttribute() << ExpectedFunctionWithProtoType;
      attr.setInvalid();
      return false;
    }

    FunctionProtoType::ExtProtoInfo EPI = FnTy->getExtProtoInfo();
    switch (attr.getKind()) {
    case ParsedAttr::AT_ArmStreaming:
      if (checkMutualExclusion(state, EPI, attr,
                               ParsedAttr::AT_ArmStreamingCompatible))
        return true;
      EPI.setArmSMEAttribute(FunctionType::SME_PStateSMEnabledMask);
      break;
    case ParsedAttr::AT_ArmStreamingCompatible:
      if (checkMutualExclusion(state, EPI, attr, ParsedAttr::AT_ArmStreaming))
        return true;
      EPI.setArmSMEAttribute(FunctionType::SME_PStateSMCompatibleMask);
      break;
    case ParsedAttr::AT_ArmPreserves:
      if (handleArmStateAttribute(S, EPI, attr, FunctionType::ARM_Preserves))
        return true;
      break;
    case ParsedAttr::AT_ArmIn:
      if (handleArmStateAttribute(S, EPI, attr, FunctionType::ARM_In))
        return true;
      break;
    case ParsedAttr::AT_ArmOut:
      if (handleArmStateAttribute(S, EPI, attr, FunctionType::ARM_Out))
        return true;
      break;
    case ParsedAttr::AT_ArmInOut:
      if (handleArmStateAttribute(S, EPI, attr, FunctionType::ARM_InOut))
        return true;
      break;
    default:
      llvm_unreachable("Unsupported attribute");
    }

    QualType newtype = S.Context.getFunctionType(FnTy->getReturnType(),
                                                 FnTy->getParamTypes(), EPI);
    type = unwrapped.wrap(S, newtype->getAs<FunctionType>());
    return true;
  }

  if (attr.getKind() == ParsedAttr::AT_NoThrow) {
    // Delay if this is not a function type.
    if (!unwrapped.isFunctionType())
      return false;

    if (S.CheckAttrNoArgs(attr)) {
      attr.setInvalid();
      return true;
    }

    // Otherwise we can process right away.
    auto *Proto = unwrapped.get()->castAs<FunctionProtoType>();

    // MSVC ignores nothrow if it is in conflict with an explicit exception
    // specification.
    if (Proto->hasExceptionSpec()) {
      switch (Proto->getExceptionSpecType()) {
      case EST_None:
        llvm_unreachable("This doesn't have an exception spec!");

      case EST_DynamicNone:
      case EST_BasicNoexcept:
      case EST_NoexceptTrue:
      case EST_NoThrow:
        // Exception spec doesn't conflict with nothrow, so don't warn.
        [[fallthrough]];
      case EST_Unparsed:
      case EST_Uninstantiated:
      case EST_DependentNoexcept:
      case EST_Unevaluated:
        // We don't have enough information to properly determine if there is a
        // conflict, so suppress the warning.
        break;
      case EST_Dynamic:
      case EST_MSAny:
      case EST_NoexceptFalse:
        S.Diag(attr.getLoc(), diag::warn_nothrow_attribute_ignored);
        break;
      }
      return true;
    }

    type = unwrapped.wrap(
        S, S.Context
               .getFunctionTypeWithExceptionSpec(
                   QualType{Proto, 0},
                   FunctionProtoType::ExceptionSpecInfo{EST_NoThrow})
               ->getAs<FunctionType>());
    return true;
  }

  if (attr.getKind() == ParsedAttr::AT_NonBlocking ||
      attr.getKind() == ParsedAttr::AT_NonAllocating ||
      attr.getKind() == ParsedAttr::AT_Blocking ||
      attr.getKind() == ParsedAttr::AT_Allocating) {
    return handleNonBlockingNonAllocatingTypeAttr(state, attr, type, unwrapped);
  }

  // Delay if the type didn't work out to a function.
  if (!unwrapped.isFunctionType()) return false;

  // Otherwise, a calling convention.
  CallingConv CC;
  if (S.CheckCallingConvAttr(attr, CC, /*FunctionDecl=*/nullptr, CFT))
    return true;

  const FunctionType *fn = unwrapped.get();
  CallingConv CCOld = fn->getCallConv();
  Attr *CCAttr = getCCTypeAttr(S.Context, attr);

  if (CCOld != CC) {
    // Error out on when there's already an attribute on the type
    // and the CCs don't match.
    if (S.getCallingConvAttributedType(type)) {
      S.Diag(attr.getLoc(), diag::err_attributes_are_not_compatible)
          << FunctionType::getNameForCallConv(CC)
          << FunctionType::getNameForCallConv(CCOld)
          << attr.isRegularKeywordAttribute();
      attr.setInvalid();
      return true;
    }
  }

  // Diagnose use of variadic functions with calling conventions that
  // don't support them (e.g. because they're callee-cleanup).
  // We delay warning about this on unprototyped function declarations
  // until after redeclaration checking, just in case we pick up a
  // prototype that way.  And apparently we also "delay" warning about
  // unprototyped function types in general, despite not necessarily having
  // much ability to diagnose it later.
  if (!supportsVariadicCall(CC)) {
    const FunctionProtoType *FnP = dyn_cast<FunctionProtoType>(fn);
    if (FnP && FnP->isVariadic()) {
      // stdcall and fastcall are ignored with a warning for GCC and MS
      // compatibility.
      if (CC == CC_X86StdCall || CC == CC_X86FastCall)
        return S.Diag(attr.getLoc(), diag::warn_cconv_unsupported)
               << FunctionType::getNameForCallConv(CC)
               << (int)Sema::CallingConventionIgnoredReason::VariadicFunction;

      attr.setInvalid();
      return S.Diag(attr.getLoc(), diag::err_cconv_varargs)
             << FunctionType::getNameForCallConv(CC);
    }
  }

  // Also diagnose fastcall with regparm.
  if (CC == CC_X86FastCall && fn->getHasRegParm()) {
    S.Diag(attr.getLoc(), diag::err_attributes_are_not_compatible)
        << "regparm" << FunctionType::getNameForCallConv(CC_X86FastCall)
        << attr.isRegularKeywordAttribute();
    attr.setInvalid();
    return true;
  }

  // Modify the CC from the wrapped function type, wrap it all back, and then
  // wrap the whole thing in an AttributedType as written.  The modified type
  // might have a different CC if we ignored the attribute.
  QualType Equivalent;
  if (CCOld == CC) {
    Equivalent = type;
  } else {
    auto EI = unwrapped.get()->getExtInfo().withCallingConv(CC);
    Equivalent =
      unwrapped.wrap(S, S.Context.adjustFunctionType(unwrapped.get(), EI));
  }
  type = state.getAttributedType(CCAttr, type, Equivalent);
  return true;
}

bool Sema::hasExplicitCallingConv(QualType T) {
  const AttributedType *AT;

  // Stop if we'd be stripping off a typedef sugar node to reach the
  // AttributedType.
  while ((AT = T->getAs<AttributedType>()) &&
         AT->getAs<TypedefType>() == T->getAs<TypedefType>()) {
    if (AT->isCallingConv())
      return true;
    T = AT->getModifiedType();
  }
  return false;
}

void Sema::adjustMemberFunctionCC(QualType &T, bool HasThisPointer,
                                  bool IsCtorOrDtor, SourceLocation Loc) {
  FunctionTypeUnwrapper Unwrapped(*this, T);
  const FunctionType *FT = Unwrapped.get();
  bool IsVariadic = (isa<FunctionProtoType>(FT) &&
                     cast<FunctionProtoType>(FT)->isVariadic());
  CallingConv CurCC = FT->getCallConv();
  CallingConv ToCC =
      Context.getDefaultCallingConvention(IsVariadic, HasThisPointer);

  if (CurCC == ToCC)
    return;

  // MS compiler ignores explicit calling convention attributes on structors. We
  // should do the same.
  if (Context.getTargetInfo().getCXXABI().isMicrosoft() && IsCtorOrDtor) {
    // Issue a warning on ignored calling convention -- except of __stdcall.
    // Again, this is what MS compiler does.
    if (CurCC != CC_X86StdCall)
      Diag(Loc, diag::warn_cconv_unsupported)
          << FunctionType::getNameForCallConv(CurCC)
          << (int)Sema::CallingConventionIgnoredReason::ConstructorDestructor;
  // Default adjustment.
  } else {
    // Only adjust types with the default convention.  For example, on Windows
    // we should adjust a __cdecl type to __thiscall for instance methods, and a
    // __thiscall type to __cdecl for static methods.
    CallingConv DefaultCC =
        Context.getDefaultCallingConvention(IsVariadic, !HasThisPointer);

    if (CurCC != DefaultCC)
      return;

    if (hasExplicitCallingConv(T))
      return;
  }

  FT = Context.adjustFunctionType(FT, FT->getExtInfo().withCallingConv(ToCC));
  QualType Wrapped = Unwrapped.wrap(*this, FT);
  T = Context.getAdjustedType(T, Wrapped);
}

/// HandleVectorSizeAttribute - this attribute is only applicable to integral
/// and float scalars, although arrays, pointers, and function return values are
/// allowed in conjunction with this construct. Aggregates with this attribute
/// are invalid, even if they are of the same size as a corresponding scalar.
/// The raw attribute should contain precisely 1 argument, the vector size for
/// the variable, measured in bytes. If curType and rawAttr are well formed,
/// this routine will return a new vector type.
static void HandleVectorSizeAttr(QualType &CurType, const ParsedAttr &Attr,
                                 Sema &S) {
  // Check the attribute arguments.
  if (Attr.getNumArgs() != 1) {
    S.Diag(Attr.getLoc(), diag::err_attribute_wrong_number_arguments) << Attr
                                                                      << 1;
    Attr.setInvalid();
    return;
  }

  Expr *SizeExpr = Attr.getArgAsExpr(0);
  QualType T = S.BuildVectorType(CurType, SizeExpr, Attr.getLoc());
  if (!T.isNull())
    CurType = T;
  else
    Attr.setInvalid();
}

/// Process the OpenCL-like ext_vector_type attribute when it occurs on
/// a type.
static void HandleExtVectorTypeAttr(QualType &CurType, const ParsedAttr &Attr,
                                    Sema &S) {
  // check the attribute arguments.
  if (Attr.getNumArgs() != 1) {
    S.Diag(Attr.getLoc(), diag::err_attribute_wrong_number_arguments) << Attr
                                                                      << 1;
    return;
  }

  Expr *SizeExpr = Attr.getArgAsExpr(0);
  QualType T = S.BuildExtVectorType(CurType, SizeExpr, Attr.getLoc());
  if (!T.isNull())
    CurType = T;
}

static bool isPermittedNeonBaseType(QualType &Ty, VectorKind VecKind, Sema &S) {
  const BuiltinType *BTy = Ty->getAs<BuiltinType>();
  if (!BTy)
    return false;

  llvm::Triple Triple = S.Context.getTargetInfo().getTriple();

  // Signed poly is mathematically wrong, but has been baked into some ABIs by
  // now.
  bool IsPolyUnsigned = Triple.getArch() == llvm::Triple::aarch64 ||
                        Triple.getArch() == llvm::Triple::aarch64_32 ||
                        Triple.getArch() == llvm::Triple::aarch64_be;
  if (VecKind == VectorKind::NeonPoly) {
    if (IsPolyUnsigned) {
      // AArch64 polynomial vectors are unsigned.
      return BTy->getKind() == BuiltinType::UChar ||
             BTy->getKind() == BuiltinType::UShort ||
             BTy->getKind() == BuiltinType::ULong ||
             BTy->getKind() == BuiltinType::ULongLong;
    } else {
      // AArch32 polynomial vectors are signed.
      return BTy->getKind() == BuiltinType::SChar ||
             BTy->getKind() == BuiltinType::Short ||
             BTy->getKind() == BuiltinType::LongLong;
    }
  }

  // Non-polynomial vector types: the usual suspects are allowed, as well as
  // float64_t on AArch64.
  if ((Triple.isArch64Bit() || Triple.getArch() == llvm::Triple::aarch64_32) &&
      BTy->getKind() == BuiltinType::Double)
    return true;

  return BTy->getKind() == BuiltinType::SChar ||
         BTy->getKind() == BuiltinType::UChar ||
         BTy->getKind() == BuiltinType::Short ||
         BTy->getKind() == BuiltinType::UShort ||
         BTy->getKind() == BuiltinType::Int ||
         BTy->getKind() == BuiltinType::UInt ||
         BTy->getKind() == BuiltinType::Long ||
         BTy->getKind() == BuiltinType::ULong ||
         BTy->getKind() == BuiltinType::LongLong ||
         BTy->getKind() == BuiltinType::ULongLong ||
         BTy->getKind() == BuiltinType::Float ||
         BTy->getKind() == BuiltinType::Half ||
         BTy->getKind() == BuiltinType::BFloat16;
}

static bool verifyValidIntegerConstantExpr(Sema &S, const ParsedAttr &Attr,
                                           llvm::APSInt &Result) {
  const auto *AttrExpr = Attr.getArgAsExpr(0);
  if (!AttrExpr->isTypeDependent()) {
    if (std::optional<llvm::APSInt> Res =
            AttrExpr->getIntegerConstantExpr(S.Context)) {
      Result = *Res;
      return true;
    }
  }
  S.Diag(Attr.getLoc(), diag::err_attribute_argument_type)
      << Attr << AANT_ArgumentIntegerConstant << AttrExpr->getSourceRange();
  Attr.setInvalid();
  return false;
}

/// HandleNeonVectorTypeAttr - The "neon_vector_type" and
/// "neon_polyvector_type" attributes are used to create vector types that
/// are mangled according to ARM's ABI.  Otherwise, these types are identical
/// to those created with the "vector_size" attribute.  Unlike "vector_size"
/// the argument to these Neon attributes is the number of vector elements,
/// not the vector size in bytes.  The vector width and element type must
/// match one of the standard Neon vector types.
static void HandleNeonVectorTypeAttr(QualType &CurType, const ParsedAttr &Attr,
                                     Sema &S, VectorKind VecKind) {
  bool IsTargetCUDAAndHostARM = false;
  if (S.getLangOpts().CUDAIsDevice) {
    const TargetInfo *AuxTI = S.getASTContext().getAuxTargetInfo();
    IsTargetCUDAAndHostARM =
        AuxTI && (AuxTI->getTriple().isAArch64() || AuxTI->getTriple().isARM());
  }

  // Target must have NEON (or MVE, whose vectors are similar enough
  // not to need a separate attribute)
  if (!S.Context.getTargetInfo().hasFeature("mve") &&
      VecKind == VectorKind::Neon &&
      S.Context.getTargetInfo().getTriple().isArmMClass()) {
    S.Diag(Attr.getLoc(), diag::err_attribute_unsupported_m_profile)
        << Attr << "'mve'";
    Attr.setInvalid();
    return;
  }
  if (!S.Context.getTargetInfo().hasFeature("mve") &&
      VecKind == VectorKind::NeonPoly &&
      S.Context.getTargetInfo().getTriple().isArmMClass()) {
    S.Diag(Attr.getLoc(), diag::err_attribute_unsupported_m_profile)
        << Attr << "'mve'";
    Attr.setInvalid();
    return;
  }

  // Check the attribute arguments.
  if (Attr.getNumArgs() != 1) {
    S.Diag(Attr.getLoc(), diag::err_attribute_wrong_number_arguments)
        << Attr << 1;
    Attr.setInvalid();
    return;
  }
  // The number of elements must be an ICE.
  llvm::APSInt numEltsInt(32);
  if (!verifyValidIntegerConstantExpr(S, Attr, numEltsInt))
    return;

  // Only certain element types are supported for Neon vectors.
  if (!isPermittedNeonBaseType(CurType, VecKind, S) &&
      !IsTargetCUDAAndHostARM) {
    S.Diag(Attr.getLoc(), diag::err_attribute_invalid_vector_type) << CurType;
    Attr.setInvalid();
    return;
  }

  // The total size of the vector must be 64 or 128 bits.
  unsigned typeSize = static_cast<unsigned>(S.Context.getTypeSize(CurType));
  unsigned numElts = static_cast<unsigned>(numEltsInt.getZExtValue());
  unsigned vecSize = typeSize * numElts;
  if (vecSize != 64 && vecSize != 128) {
    S.Diag(Attr.getLoc(), diag::err_attribute_bad_neon_vector_size) << CurType;
    Attr.setInvalid();
    return;
  }

  CurType = S.Context.getVectorType(CurType, numElts, VecKind);
}

/// HandleArmSveVectorBitsTypeAttr - The "arm_sve_vector_bits" attribute is
/// used to create fixed-length versions of sizeless SVE types defined by
/// the ACLE, such as svint32_t and svbool_t.
static void HandleArmSveVectorBitsTypeAttr(QualType &CurType, ParsedAttr &Attr,
                                           Sema &S) {
  // Target must have SVE.
  if (!S.Context.getTargetInfo().hasFeature("sve")) {
    S.Diag(Attr.getLoc(), diag::err_attribute_unsupported) << Attr << "'sve'";
    Attr.setInvalid();
    return;
  }

  // Attribute is unsupported if '-msve-vector-bits=<bits>' isn't specified, or
  // if <bits>+ syntax is used.
  if (!S.getLangOpts().VScaleMin ||
      S.getLangOpts().VScaleMin != S.getLangOpts().VScaleMax) {
    S.Diag(Attr.getLoc(), diag::err_attribute_arm_feature_sve_bits_unsupported)
        << Attr;
    Attr.setInvalid();
    return;
  }

  // Check the attribute arguments.
  if (Attr.getNumArgs() != 1) {
    S.Diag(Attr.getLoc(), diag::err_attribute_wrong_number_arguments)
        << Attr << 1;
    Attr.setInvalid();
    return;
  }

  // The vector size must be an integer constant expression.
  llvm::APSInt SveVectorSizeInBits(32);
  if (!verifyValidIntegerConstantExpr(S, Attr, SveVectorSizeInBits))
    return;

  unsigned VecSize = static_cast<unsigned>(SveVectorSizeInBits.getZExtValue());

  // The attribute vector size must match -msve-vector-bits.
  if (VecSize != S.getLangOpts().VScaleMin * 128) {
    S.Diag(Attr.getLoc(), diag::err_attribute_bad_sve_vector_size)
        << VecSize << S.getLangOpts().VScaleMin * 128;
    Attr.setInvalid();
    return;
  }

  // Attribute can only be attached to a single SVE vector or predicate type.
  if (!CurType->isSveVLSBuiltinType()) {
    S.Diag(Attr.getLoc(), diag::err_attribute_invalid_sve_type)
        << Attr << CurType;
    Attr.setInvalid();
    return;
  }

  const auto *BT = CurType->castAs<BuiltinType>();

  QualType EltType = CurType->getSveEltType(S.Context);
  unsigned TypeSize = S.Context.getTypeSize(EltType);
  VectorKind VecKind = VectorKind::SveFixedLengthData;
  if (BT->getKind() == BuiltinType::SveBool) {
    // Predicates are represented as i8.
    VecSize /= S.Context.getCharWidth() * S.Context.getCharWidth();
    VecKind = VectorKind::SveFixedLengthPredicate;
  } else
    VecSize /= TypeSize;
  CurType = S.Context.getVectorType(EltType, VecSize, VecKind);
}

static void HandleArmMveStrictPolymorphismAttr(TypeProcessingState &State,
                                               QualType &CurType,
                                               ParsedAttr &Attr) {
  const VectorType *VT = dyn_cast<VectorType>(CurType);
  if (!VT || VT->getVectorKind() != VectorKind::Neon) {
    State.getSema().Diag(Attr.getLoc(),
                         diag::err_attribute_arm_mve_polymorphism);
    Attr.setInvalid();
    return;
  }

  CurType =
      State.getAttributedType(createSimpleAttr<ArmMveStrictPolymorphismAttr>(
                                  State.getSema().Context, Attr),
                              CurType, CurType);
}

/// HandleRISCVRVVVectorBitsTypeAttr - The "riscv_rvv_vector_bits" attribute is
/// used to create fixed-length versions of sizeless RVV types such as
/// vint8m1_t_t.
static void HandleRISCVRVVVectorBitsTypeAttr(QualType &CurType,
                                             ParsedAttr &Attr, Sema &S) {
  // Target must have vector extension.
  if (!S.Context.getTargetInfo().hasFeature("zve32x")) {
    S.Diag(Attr.getLoc(), diag::err_attribute_unsupported)
        << Attr << "'zve32x'";
    Attr.setInvalid();
    return;
  }

  auto VScale = S.Context.getTargetInfo().getVScaleRange(S.getLangOpts());
  if (!VScale || !VScale->first || VScale->first != VScale->second) {
    S.Diag(Attr.getLoc(), diag::err_attribute_riscv_rvv_bits_unsupported)
        << Attr;
    Attr.setInvalid();
    return;
  }

  // Check the attribute arguments.
  if (Attr.getNumArgs() != 1) {
    S.Diag(Attr.getLoc(), diag::err_attribute_wrong_number_arguments)
        << Attr << 1;
    Attr.setInvalid();
    return;
  }

  // The vector size must be an integer constant expression.
  llvm::APSInt RVVVectorSizeInBits(32);
  if (!verifyValidIntegerConstantExpr(S, Attr, RVVVectorSizeInBits))
    return;

  // Attribute can only be attached to a single RVV vector type.
  if (!CurType->isRVVVLSBuiltinType()) {
    S.Diag(Attr.getLoc(), diag::err_attribute_invalid_rvv_type)
        << Attr << CurType;
    Attr.setInvalid();
    return;
  }

  unsigned VecSize = static_cast<unsigned>(RVVVectorSizeInBits.getZExtValue());

  ASTContext::BuiltinVectorTypeInfo Info =
      S.Context.getBuiltinVectorTypeInfo(CurType->castAs<BuiltinType>());
  unsigned MinElts = Info.EC.getKnownMinValue();

  VectorKind VecKind = VectorKind::RVVFixedLengthData;
  unsigned ExpectedSize = VScale->first * MinElts;
  QualType EltType = CurType->getRVVEltType(S.Context);
  unsigned EltSize = S.Context.getTypeSize(EltType);
  unsigned NumElts;
  if (Info.ElementType == S.Context.BoolTy) {
    NumElts = VecSize / S.Context.getCharWidth();
    VecKind = VectorKind::RVVFixedLengthMask;
  } else {
    ExpectedSize *= EltSize;
    NumElts = VecSize / EltSize;
  }

  // The attribute vector size must match -mrvv-vector-bits.
  if (ExpectedSize % 8 != 0 || VecSize != ExpectedSize) {
    S.Diag(Attr.getLoc(), diag::err_attribute_bad_rvv_vector_size)
        << VecSize << ExpectedSize;
    Attr.setInvalid();
    return;
  }

  CurType = S.Context.getVectorType(EltType, NumElts, VecKind);
}

/// Handle OpenCL Access Qualifier Attribute.
static void HandleOpenCLAccessAttr(QualType &CurType, const ParsedAttr &Attr,
                                   Sema &S) {
  // OpenCL v2.0 s6.6 - Access qualifier can be used only for image and pipe type.
  if (!(CurType->isImageType() || CurType->isPipeType())) {
    S.Diag(Attr.getLoc(), diag::err_opencl_invalid_access_qualifier);
    Attr.setInvalid();
    return;
  }

  if (const TypedefType* TypedefTy = CurType->getAs<TypedefType>()) {
    QualType BaseTy = TypedefTy->desugar();

    std::string PrevAccessQual;
    if (BaseTy->isPipeType()) {
      if (TypedefTy->getDecl()->hasAttr<OpenCLAccessAttr>()) {
        OpenCLAccessAttr *Attr =
            TypedefTy->getDecl()->getAttr<OpenCLAccessAttr>();
        PrevAccessQual = Attr->getSpelling();
      } else {
        PrevAccessQual = "read_only";
      }
    } else if (const BuiltinType* ImgType = BaseTy->getAs<BuiltinType>()) {

      switch (ImgType->getKind()) {
        #define IMAGE_TYPE(ImgType, Id, SingletonId, Access, Suffix) \
      case BuiltinType::Id:                                          \
        PrevAccessQual = #Access;                                    \
        break;
        #include "clang/Basic/OpenCLImageTypes.def"
      default:
        llvm_unreachable("Unable to find corresponding image type.");
      }
    } else {
      llvm_unreachable("unexpected type");
    }
    StringRef AttrName = Attr.getAttrName()->getName();
    if (PrevAccessQual == AttrName.ltrim("_")) {
      // Duplicated qualifiers
      S.Diag(Attr.getLoc(), diag::warn_duplicate_declspec)
         << AttrName << Attr.getRange();
    } else {
      // Contradicting qualifiers
      S.Diag(Attr.getLoc(), diag::err_opencl_multiple_access_qualifiers);
    }

    S.Diag(TypedefTy->getDecl()->getBeginLoc(),
           diag::note_opencl_typedef_access_qualifier) << PrevAccessQual;
  } else if (CurType->isPipeType()) {
    if (Attr.getSemanticSpelling() == OpenCLAccessAttr::Keyword_write_only) {
      QualType ElemType = CurType->castAs<PipeType>()->getElementType();
      CurType = S.Context.getWritePipeType(ElemType);
    }
  }
}

/// HandleMatrixTypeAttr - "matrix_type" attribute, like ext_vector_type
static void HandleMatrixTypeAttr(QualType &CurType, const ParsedAttr &Attr,
                                 Sema &S) {
  if (!S.getLangOpts().MatrixTypes) {
    S.Diag(Attr.getLoc(), diag::err_builtin_matrix_disabled);
    return;
  }

  if (Attr.getNumArgs() != 2) {
    S.Diag(Attr.getLoc(), diag::err_attribute_wrong_number_arguments)
        << Attr << 2;
    return;
  }

  Expr *RowsExpr = Attr.getArgAsExpr(0);
  Expr *ColsExpr = Attr.getArgAsExpr(1);
  QualType T = S.BuildMatrixType(CurType, RowsExpr, ColsExpr, Attr.getLoc());
  if (!T.isNull())
    CurType = T;
}

static void HandleAnnotateTypeAttr(TypeProcessingState &State,
                                   QualType &CurType, const ParsedAttr &PA) {
  Sema &S = State.getSema();

  if (PA.getNumArgs() < 1) {
    S.Diag(PA.getLoc(), diag::err_attribute_too_few_arguments) << PA << 1;
    return;
  }

  // Make sure that there is a string literal as the annotation's first
  // argument.
  StringRef Str;
  if (!S.checkStringLiteralArgumentAttr(PA, 0, Str))
    return;

  llvm::SmallVector<Expr *, 4> Args;
  Args.reserve(PA.getNumArgs() - 1);
  for (unsigned Idx = 1; Idx < PA.getNumArgs(); Idx++) {
    assert(!PA.isArgIdent(Idx));
    Args.push_back(PA.getArgAsExpr(Idx));
  }
  if (!S.ConstantFoldAttrArgs(PA, Args))
    return;
  auto *AnnotateTypeAttr =
      AnnotateTypeAttr::Create(S.Context, Str, Args.data(), Args.size(), PA);
  CurType = State.getAttributedType(AnnotateTypeAttr, CurType, CurType);
}

static void HandleLifetimeBoundAttr(TypeProcessingState &State,
                                    QualType &CurType,
                                    ParsedAttr &Attr) {
  if (State.getDeclarator().isDeclarationOfFunction()) {
    CurType = State.getAttributedType(
        createSimpleAttr<LifetimeBoundAttr>(State.getSema().Context, Attr),
        CurType, CurType);
  }
}

static void HandleHLSLParamModifierAttr(QualType &CurType,
                                        const ParsedAttr &Attr, Sema &S) {
  // Don't apply this attribute to template dependent types. It is applied on
  // substitution during template instantiation.
  if (CurType->isDependentType())
    return;
  if (Attr.getSemanticSpelling() == HLSLParamModifierAttr::Keyword_inout ||
      Attr.getSemanticSpelling() == HLSLParamModifierAttr::Keyword_out)
    CurType = S.getASTContext().getLValueReferenceType(CurType);
}

static void processTypeAttrs(TypeProcessingState &state, QualType &type,
                             TypeAttrLocation TAL,
                             const ParsedAttributesView &attrs,
                             CUDAFunctionTarget CFT) {

  state.setParsedNoDeref(false);
  if (attrs.empty())
    return;

  // Scan through and apply attributes to this type where it makes sense.  Some
  // attributes (such as __address_space__, __vector_size__, etc) apply to the
  // type, but others can be present in the type specifiers even though they
  // apply to the decl.  Here we apply type attributes and ignore the rest.

  // This loop modifies the list pretty frequently, but we still need to make
  // sure we visit every element once. Copy the attributes list, and iterate
  // over that.
  ParsedAttributesView AttrsCopy{attrs};
  for (ParsedAttr &attr : AttrsCopy) {

    // Skip attributes that were marked to be invalid.
    if (attr.isInvalid())
      continue;

    if (attr.isStandardAttributeSyntax() || attr.isRegularKeywordAttribute()) {
      // [[gnu::...]] attributes are treated as declaration attributes, so may
      // not appertain to a DeclaratorChunk. If we handle them as type
      // attributes, accept them in that position and diagnose the GCC
      // incompatibility.
      if (attr.isGNUScope()) {
        assert(attr.isStandardAttributeSyntax());
        bool IsTypeAttr = attr.isTypeAttr();
        if (TAL == TAL_DeclChunk) {
          state.getSema().Diag(attr.getLoc(),
                               IsTypeAttr
                                   ? diag::warn_gcc_ignores_type_attr
                                   : diag::warn_cxx11_gnu_attribute_on_type)
              << attr;
          if (!IsTypeAttr)
            continue;
        }
      } else if (TAL != TAL_DeclSpec && TAL != TAL_DeclChunk &&
                 !attr.isTypeAttr()) {
        // Otherwise, only consider type processing for a C++11 attribute if
        // - it has actually been applied to a type (decl-specifier-seq or
        //   declarator chunk), or
        // - it is a type attribute, irrespective of where it was applied (so
        //   that we can support the legacy behavior of some type attributes
        //   that can be applied to the declaration name).
        continue;
      }
    }

    // If this is an attribute we can handle, do so now,
    // otherwise, add it to the FnAttrs list for rechaining.
    switch (attr.getKind()) {
    default:
      // A [[]] attribute on a declarator chunk must appertain to a type.
      if ((attr.isStandardAttributeSyntax() ||
           attr.isRegularKeywordAttribute()) &&
          TAL == TAL_DeclChunk) {
        state.getSema().Diag(attr.getLoc(), diag::err_attribute_not_type_attr)
            << attr << attr.isRegularKeywordAttribute();
        attr.setUsedAsTypeAttr();
      }
      break;

    case ParsedAttr::UnknownAttribute:
      if (attr.isStandardAttributeSyntax()) {
        state.getSema().Diag(attr.getLoc(),
                             diag::warn_unknown_attribute_ignored)
            << attr << attr.getRange();
        // Mark the attribute as invalid so we don't emit the same diagnostic
        // multiple times.
        attr.setInvalid();
      }
      break;

    case ParsedAttr::IgnoredAttribute:
      break;

    case ParsedAttr::AT_BTFTypeTag:
      HandleBTFTypeTagAttribute(type, attr, state);
      attr.setUsedAsTypeAttr();
      break;

    case ParsedAttr::AT_MayAlias:
      // FIXME: This attribute needs to actually be handled, but if we ignore
      // it it breaks large amounts of Linux software.
      attr.setUsedAsTypeAttr();
      break;
    case ParsedAttr::AT_OpenCLPrivateAddressSpace:
    case ParsedAttr::AT_OpenCLGlobalAddressSpace:
    case ParsedAttr::AT_OpenCLGlobalDeviceAddressSpace:
    case ParsedAttr::AT_OpenCLGlobalHostAddressSpace:
    case ParsedAttr::AT_OpenCLLocalAddressSpace:
    case ParsedAttr::AT_OpenCLConstantAddressSpace:
    case ParsedAttr::AT_OpenCLGenericAddressSpace:
    case ParsedAttr::AT_HLSLGroupSharedAddressSpace:
    case ParsedAttr::AT_AddressSpace:
      HandleAddressSpaceTypeAttribute(type, attr, state);
      attr.setUsedAsTypeAttr();
      break;
    OBJC_POINTER_TYPE_ATTRS_CASELIST:
      if (!handleObjCPointerTypeAttr(state, attr, type))
        distributeObjCPointerTypeAttr(state, attr, type);
      attr.setUsedAsTypeAttr();
      break;
    case ParsedAttr::AT_VectorSize:
      HandleVectorSizeAttr(type, attr, state.getSema());
      attr.setUsedAsTypeAttr();
      break;
    case ParsedAttr::AT_ExtVectorType:
      HandleExtVectorTypeAttr(type, attr, state.getSema());
      attr.setUsedAsTypeAttr();
      break;
    case ParsedAttr::AT_NeonVectorType:
      HandleNeonVectorTypeAttr(type, attr, state.getSema(), VectorKind::Neon);
      attr.setUsedAsTypeAttr();
      break;
    case ParsedAttr::AT_NeonPolyVectorType:
      HandleNeonVectorTypeAttr(type, attr, state.getSema(),
                               VectorKind::NeonPoly);
      attr.setUsedAsTypeAttr();
      break;
    case ParsedAttr::AT_ArmSveVectorBits:
      HandleArmSveVectorBitsTypeAttr(type, attr, state.getSema());
      attr.setUsedAsTypeAttr();
      break;
    case ParsedAttr::AT_ArmMveStrictPolymorphism: {
      HandleArmMveStrictPolymorphismAttr(state, type, attr);
      attr.setUsedAsTypeAttr();
      break;
    }
    case ParsedAttr::AT_RISCVRVVVectorBits:
      HandleRISCVRVVVectorBitsTypeAttr(type, attr, state.getSema());
      attr.setUsedAsTypeAttr();
      break;
    case ParsedAttr::AT_OpenCLAccess:
      HandleOpenCLAccessAttr(type, attr, state.getSema());
      attr.setUsedAsTypeAttr();
      break;
    case ParsedAttr::AT_LifetimeBound:
      if (TAL == TAL_DeclChunk)
        HandleLifetimeBoundAttr(state, type, attr);
      break;

    case ParsedAttr::AT_NoDeref: {
      // FIXME: `noderef` currently doesn't work correctly in [[]] syntax.
      // See https://github.com/llvm/llvm-project/issues/55790 for details.
      // For the time being, we simply emit a warning that the attribute is
      // ignored.
      if (attr.isStandardAttributeSyntax()) {
        state.getSema().Diag(attr.getLoc(), diag::warn_attribute_ignored)
            << attr;
        break;
      }
      ASTContext &Ctx = state.getSema().Context;
      type = state.getAttributedType(createSimpleAttr<NoDerefAttr>(Ctx, attr),
                                     type, type);
      attr.setUsedAsTypeAttr();
      state.setParsedNoDeref(true);
      break;
    }

    case ParsedAttr::AT_MatrixType:
      HandleMatrixTypeAttr(type, attr, state.getSema());
      attr.setUsedAsTypeAttr();
      break;

    case ParsedAttr::AT_WebAssemblyFuncref: {
      if (!HandleWebAssemblyFuncrefAttr(state, type, attr))
        attr.setUsedAsTypeAttr();
      break;
    }

    case ParsedAttr::AT_HLSLParamModifier: {
      HandleHLSLParamModifierAttr(type, attr, state.getSema());
      attr.setUsedAsTypeAttr();
      break;
    }

    MS_TYPE_ATTRS_CASELIST:
      if (!handleMSPointerTypeQualifierAttr(state, attr, type))
        attr.setUsedAsTypeAttr();
      break;


    NULLABILITY_TYPE_ATTRS_CASELIST:
      // Either add nullability here or try to distribute it.  We
      // don't want to distribute the nullability specifier past any
      // dependent type, because that complicates the user model.
      if (type->canHaveNullability() || type->isDependentType() ||
          type->isArrayType() ||
          !distributeNullabilityTypeAttr(state, type, attr)) {
        unsigned endIndex;
        if (TAL == TAL_DeclChunk)
          endIndex = state.getCurrentChunkIndex();
        else
          endIndex = state.getDeclarator().getNumTypeObjects();
        bool allowOnArrayType =
            state.getDeclarator().isPrototypeContext() &&
            !hasOuterPointerLikeChunk(state.getDeclarator(), endIndex);
        if (CheckNullabilityTypeSpecifier(state, type, attr,
                                          allowOnArrayType)) {
          attr.setInvalid();
        }

        attr.setUsedAsTypeAttr();
      }
      break;

    case ParsedAttr::AT_ObjCKindOf:
      // '__kindof' must be part of the decl-specifiers.
      switch (TAL) {
      case TAL_DeclSpec:
        break;

      case TAL_DeclChunk:
      case TAL_DeclName:
        state.getSema().Diag(attr.getLoc(),
                             diag::err_objc_kindof_wrong_position)
            << FixItHint::CreateRemoval(attr.getLoc())
            << FixItHint::CreateInsertion(
                   state.getDeclarator().getDeclSpec().getBeginLoc(),
                   "__kindof ");
        break;
      }

      // Apply it regardless.
      if (checkObjCKindOfType(state, type, attr))
        attr.setInvalid();
      break;

    case ParsedAttr::AT_NoThrow:
    // Exception Specifications aren't generally supported in C mode throughout
    // clang, so revert to attribute-based handling for C.
      if (!state.getSema().getLangOpts().CPlusPlus)
        break;
      [[fallthrough]];
    FUNCTION_TYPE_ATTRS_CASELIST:
      attr.setUsedAsTypeAttr();

      // Attributes with standard syntax have strict rules for what they
      // appertain to and hence should not use the "distribution" logic below.
      if (attr.isStandardAttributeSyntax() ||
          attr.isRegularKeywordAttribute()) {
        if (!handleFunctionTypeAttr(state, attr, type, CFT)) {
          diagnoseBadTypeAttribute(state.getSema(), attr, type);
          attr.setInvalid();
        }
        break;
      }

      // Never process function type attributes as part of the
      // declaration-specifiers.
      if (TAL == TAL_DeclSpec)
        distributeFunctionTypeAttrFromDeclSpec(state, attr, type, CFT);

      // Otherwise, handle the possible delays.
      else if (!handleFunctionTypeAttr(state, attr, type, CFT))
        distributeFunctionTypeAttr(state, attr, type);
      break;
    case ParsedAttr::AT_AcquireHandle: {
      if (!type->isFunctionType())
        return;

      if (attr.getNumArgs() != 1) {
        state.getSema().Diag(attr.getLoc(),
                             diag::err_attribute_wrong_number_arguments)
            << attr << 1;
        attr.setInvalid();
        return;
      }

      StringRef HandleType;
      if (!state.getSema().checkStringLiteralArgumentAttr(attr, 0, HandleType))
        return;
      type = state.getAttributedType(
          AcquireHandleAttr::Create(state.getSema().Context, HandleType, attr),
          type, type);
      attr.setUsedAsTypeAttr();
      break;
    }
    case ParsedAttr::AT_AnnotateType: {
      HandleAnnotateTypeAttr(state, type, attr);
      attr.setUsedAsTypeAttr();
      break;
    }
    }

    // Handle attributes that are defined in a macro. We do not want this to be
    // applied to ObjC builtin attributes.
    if (isa<AttributedType>(type) && attr.hasMacroIdentifier() &&
        !type.getQualifiers().hasObjCLifetime() &&
        !type.getQualifiers().hasObjCGCAttr() &&
        attr.getKind() != ParsedAttr::AT_ObjCGC &&
        attr.getKind() != ParsedAttr::AT_ObjCOwnership) {
      const IdentifierInfo *MacroII = attr.getMacroIdentifier();
      type = state.getSema().Context.getMacroQualifiedType(type, MacroII);
      state.setExpansionLocForMacroQualifiedType(
          cast<MacroQualifiedType>(type.getTypePtr()),
          attr.getMacroExpansionLoc());
    }
  }
}

void Sema::completeExprArrayBound(Expr *E) {
  if (DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E->IgnoreParens())) {
    if (VarDecl *Var = dyn_cast<VarDecl>(DRE->getDecl())) {
      if (isTemplateInstantiation(Var->getTemplateSpecializationKind())) {
        auto *Def = Var->getDefinition();
        if (!Def) {
          SourceLocation PointOfInstantiation = E->getExprLoc();
          runWithSufficientStackSpace(PointOfInstantiation, [&] {
            InstantiateVariableDefinition(PointOfInstantiation, Var);
          });
          Def = Var->getDefinition();

          // If we don't already have a point of instantiation, and we managed
          // to instantiate a definition, this is the point of instantiation.
          // Otherwise, we don't request an end-of-TU instantiation, so this is
          // not a point of instantiation.
          // FIXME: Is this really the right behavior?
          if (Var->getPointOfInstantiation().isInvalid() && Def) {
            assert(Var->getTemplateSpecializationKind() ==
                       TSK_ImplicitInstantiation &&
                   "explicit instantiation with no point of instantiation");
            Var->setTemplateSpecializationKind(
                Var->getTemplateSpecializationKind(), PointOfInstantiation);
          }
        }

        // Update the type to the definition's type both here and within the
        // expression.
        if (Def) {
          DRE->setDecl(Def);
          QualType T = Def->getType();
          DRE->setType(T);
          // FIXME: Update the type on all intervening expressions.
          E->setType(T);
        }

        // We still go on to try to complete the type independently, as it
        // may also require instantiations or diagnostics if it remains
        // incomplete.
      }
    }
  }
  if (const auto CastE = dyn_cast<ExplicitCastExpr>(E)) {
    QualType DestType = CastE->getTypeAsWritten();
    if (const auto *IAT = Context.getAsIncompleteArrayType(DestType)) {
      // C++20 [expr.static.cast]p.4: ... If T is array of unknown bound,
      // this direct-initialization defines the type of the expression
      // as U[1]
      QualType ResultType = Context.getConstantArrayType(
          IAT->getElementType(),
          llvm::APInt(Context.getTypeSize(Context.getSizeType()), 1),
          /*SizeExpr=*/nullptr, ArraySizeModifier::Normal,
          /*IndexTypeQuals=*/0);
      E->setType(ResultType);
    }
  }
}

QualType Sema::getCompletedType(Expr *E) {
  // Incomplete array types may be completed by the initializer attached to
  // their definitions. For static data members of class templates and for
  // variable templates, we need to instantiate the definition to get this
  // initializer and complete the type.
  if (E->getType()->isIncompleteArrayType())
    completeExprArrayBound(E);

  // FIXME: Are there other cases which require instantiating something other
  // than the type to complete the type of an expression?

  return E->getType();
}

bool Sema::RequireCompleteExprType(Expr *E, CompleteTypeKind Kind,
                                   TypeDiagnoser &Diagnoser) {
  return RequireCompleteType(E->getExprLoc(), getCompletedType(E), Kind,
                             Diagnoser);
}

bool Sema::RequireCompleteExprType(Expr *E, unsigned DiagID) {
  BoundTypeDiagnoser<> Diagnoser(DiagID);
  return RequireCompleteExprType(E, CompleteTypeKind::Default, Diagnoser);
}

bool Sema::RequireCompleteType(SourceLocation Loc, QualType T,
                               CompleteTypeKind Kind,
                               TypeDiagnoser &Diagnoser) {
  if (RequireCompleteTypeImpl(Loc, T, Kind, &Diagnoser))
    return true;
  if (const TagType *Tag = T->getAs<TagType>()) {
    if (!Tag->getDecl()->isCompleteDefinitionRequired()) {
      Tag->getDecl()->setCompleteDefinitionRequired();
      Consumer.HandleTagDeclRequiredDefinition(Tag->getDecl());
    }
  }
  return false;
}

bool Sema::hasStructuralCompatLayout(Decl *D, Decl *Suggested) {
  llvm::DenseSet<std::pair<Decl *, Decl *>> NonEquivalentDecls;
  if (!Suggested)
    return false;

  // FIXME: Add a specific mode for C11 6.2.7/1 in StructuralEquivalenceContext
  // and isolate from other C++ specific checks.
  StructuralEquivalenceContext Ctx(
      D->getASTContext(), Suggested->getASTContext(), NonEquivalentDecls,
      StructuralEquivalenceKind::Default,
      false /*StrictTypeSpelling*/, true /*Complain*/,
      true /*ErrorOnTagTypeMismatch*/);
  return Ctx.IsEquivalent(D, Suggested);
}

bool Sema::hasAcceptableDefinition(NamedDecl *D, NamedDecl **Suggested,
                                   AcceptableKind Kind, bool OnlyNeedComplete) {
  // Easy case: if we don't have modules, all declarations are visible.
  if (!getLangOpts().Modules && !getLangOpts().ModulesLocalVisibility)
    return true;

  // If this definition was instantiated from a template, map back to the
  // pattern from which it was instantiated.
  if (isa<TagDecl>(D) && cast<TagDecl>(D)->isBeingDefined()) {
    // We're in the middle of defining it; this definition should be treated
    // as visible.
    return true;
  } else if (auto *RD = dyn_cast<CXXRecordDecl>(D)) {
    if (auto *Pattern = RD->getTemplateInstantiationPattern())
      RD = Pattern;
    D = RD->getDefinition();
  } else if (auto *ED = dyn_cast<EnumDecl>(D)) {
    if (auto *Pattern = ED->getTemplateInstantiationPattern())
      ED = Pattern;
    if (OnlyNeedComplete && (ED->isFixed() || getLangOpts().MSVCCompat)) {
      // If the enum has a fixed underlying type, it may have been forward
      // declared. In -fms-compatibility, `enum Foo;` will also forward declare
      // the enum and assign it the underlying type of `int`. Since we're only
      // looking for a complete type (not a definition), any visible declaration
      // of it will do.
      *Suggested = nullptr;
      for (auto *Redecl : ED->redecls()) {
        if (isAcceptable(Redecl, Kind))
          return true;
        if (Redecl->isThisDeclarationADefinition() ||
            (Redecl->isCanonicalDecl() && !*Suggested))
          *Suggested = Redecl;
      }

      return false;
    }
    D = ED->getDefinition();
  } else if (auto *FD = dyn_cast<FunctionDecl>(D)) {
    if (auto *Pattern = FD->getTemplateInstantiationPattern())
      FD = Pattern;
    D = FD->getDefinition();
  } else if (auto *VD = dyn_cast<VarDecl>(D)) {
    if (auto *Pattern = VD->getTemplateInstantiationPattern())
      VD = Pattern;
    D = VD->getDefinition();
  }

  assert(D && "missing definition for pattern of instantiated definition");

  *Suggested = D;

  auto DefinitionIsAcceptable = [&] {
    // The (primary) definition might be in a visible module.
    if (isAcceptable(D, Kind))
      return true;

    // A visible module might have a merged definition instead.
    if (D->isModulePrivate() ? hasMergedDefinitionInCurrentModule(D)
                             : hasVisibleMergedDefinition(D)) {
      if (CodeSynthesisContexts.empty() &&
          !getLangOpts().ModulesLocalVisibility) {
        // Cache the fact that this definition is implicitly visible because
        // there is a visible merged definition.
        D->setVisibleDespiteOwningModule();
      }
      return true;
    }

    return false;
  };

  if (DefinitionIsAcceptable())
    return true;

  // The external source may have additional definitions of this entity that are
  // visible, so complete the redeclaration chain now and ask again.
  if (auto *Source = Context.getExternalSource()) {
    Source->CompleteRedeclChain(D);
    return DefinitionIsAcceptable();
  }

  return false;
}

/// Determine whether there is any declaration of \p D that was ever a
///        definition (perhaps before module merging) and is currently visible.
/// \param D The definition of the entity.
/// \param Suggested Filled in with the declaration that should be made visible
///        in order to provide a definition of this entity.
/// \param OnlyNeedComplete If \c true, we only need the type to be complete,
///        not defined. This only matters for enums with a fixed underlying
///        type, since in all other cases, a type is complete if and only if it
///        is defined.
bool Sema::hasVisibleDefinition(NamedDecl *D, NamedDecl **Suggested,
                                bool OnlyNeedComplete) {
  return hasAcceptableDefinition(D, Suggested, Sema::AcceptableKind::Visible,
                                 OnlyNeedComplete);
}

/// Determine whether there is any declaration of \p D that was ever a
///        definition (perhaps before module merging) and is currently
///        reachable.
/// \param D The definition of the entity.
/// \param Suggested Filled in with the declaration that should be made
/// reachable
///        in order to provide a definition of this entity.
/// \param OnlyNeedComplete If \c true, we only need the type to be complete,
///        not defined. This only matters for enums with a fixed underlying
///        type, since in all other cases, a type is complete if and only if it
///        is defined.
bool Sema::hasReachableDefinition(NamedDecl *D, NamedDecl **Suggested,
                                  bool OnlyNeedComplete) {
  return hasAcceptableDefinition(D, Suggested, Sema::AcceptableKind::Reachable,
                                 OnlyNeedComplete);
}

/// Locks in the inheritance model for the given class and all of its bases.
static void assignInheritanceModel(Sema &S, CXXRecordDecl *RD) {
  RD = RD->getMostRecentNonInjectedDecl();
  if (!RD->hasAttr<MSInheritanceAttr>()) {
    MSInheritanceModel IM;
    bool BestCase = false;
    switch (S.MSPointerToMemberRepresentationMethod) {
    case LangOptions::PPTMK_BestCase:
      BestCase = true;
      IM = RD->calculateInheritanceModel();
      break;
    case LangOptions::PPTMK_FullGeneralitySingleInheritance:
      IM = MSInheritanceModel::Single;
      break;
    case LangOptions::PPTMK_FullGeneralityMultipleInheritance:
      IM = MSInheritanceModel::Multiple;
      break;
    case LangOptions::PPTMK_FullGeneralityVirtualInheritance:
      IM = MSInheritanceModel::Unspecified;
      break;
    }

    SourceRange Loc = S.ImplicitMSInheritanceAttrLoc.isValid()
                          ? S.ImplicitMSInheritanceAttrLoc
                          : RD->getSourceRange();
    RD->addAttr(MSInheritanceAttr::CreateImplicit(
        S.getASTContext(), BestCase, Loc, MSInheritanceAttr::Spelling(IM)));
    S.Consumer.AssignInheritanceModel(RD);
  }
}

bool Sema::RequireCompleteTypeImpl(SourceLocation Loc, QualType T,
                                   CompleteTypeKind Kind,
                                   TypeDiagnoser *Diagnoser) {
  // FIXME: Add this assertion to make sure we always get instantiation points.
  //  assert(!Loc.isInvalid() && "Invalid location in RequireCompleteType");
  // FIXME: Add this assertion to help us flush out problems with
  // checking for dependent types and type-dependent expressions.
  //
  //  assert(!T->isDependentType() &&
  //         "Can't ask whether a dependent type is complete");

  if (const MemberPointerType *MPTy = T->getAs<MemberPointerType>()) {
    if (!MPTy->getClass()->isDependentType()) {
      if (getLangOpts().CompleteMemberPointers &&
          !MPTy->getClass()->getAsCXXRecordDecl()->isBeingDefined() &&
          RequireCompleteType(Loc, QualType(MPTy->getClass(), 0), Kind,
                              diag::err_memptr_incomplete))
        return true;

      // We lock in the inheritance model once somebody has asked us to ensure
      // that a pointer-to-member type is complete.
      if (Context.getTargetInfo().getCXXABI().isMicrosoft()) {
        (void)isCompleteType(Loc, QualType(MPTy->getClass(), 0));
        assignInheritanceModel(*this, MPTy->getMostRecentCXXRecordDecl());
      }
    }
  }

  NamedDecl *Def = nullptr;
  bool AcceptSizeless = (Kind == CompleteTypeKind::AcceptSizeless);
  bool Incomplete = (T->isIncompleteType(&Def) ||
                     (!AcceptSizeless && T->isSizelessBuiltinType()));

  // Check that any necessary explicit specializations are visible. For an
  // enum, we just need the declaration, so don't check this.
  if (Def && !isa<EnumDecl>(Def))
    checkSpecializationReachability(Loc, Def);

  // If we have a complete type, we're done.
  if (!Incomplete) {
    NamedDecl *Suggested = nullptr;
    if (Def &&
        !hasReachableDefinition(Def, &Suggested, /*OnlyNeedComplete=*/true)) {
      // If the user is going to see an error here, recover by making the
      // definition visible.
      bool TreatAsComplete = Diagnoser && !isSFINAEContext();
      if (Diagnoser && Suggested)
        diagnoseMissingImport(Loc, Suggested, MissingImportKind::Definition,
                              /*Recover*/ TreatAsComplete);
      return !TreatAsComplete;
    } else if (Def && !TemplateInstCallbacks.empty()) {
      CodeSynthesisContext TempInst;
      TempInst.Kind = CodeSynthesisContext::Memoization;
      TempInst.Template = Def;
      TempInst.Entity = Def;
      TempInst.PointOfInstantiation = Loc;
      atTemplateBegin(TemplateInstCallbacks, *this, TempInst);
      atTemplateEnd(TemplateInstCallbacks, *this, TempInst);
    }

    return false;
  }

  TagDecl *Tag = dyn_cast_or_null<TagDecl>(Def);
  ObjCInterfaceDecl *IFace = dyn_cast_or_null<ObjCInterfaceDecl>(Def);

  // Give the external source a chance to provide a definition of the type.
  // This is kept separate from completing the redeclaration chain so that
  // external sources such as LLDB can avoid synthesizing a type definition
  // unless it's actually needed.
  if (Tag || IFace) {
    // Avoid diagnosing invalid decls as incomplete.
    if (Def->isInvalidDecl())
      return true;

    // Give the external AST source a chance to complete the type.
    if (auto *Source = Context.getExternalSource()) {
      if (Tag && Tag->hasExternalLexicalStorage())
          Source->CompleteType(Tag);
      if (IFace && IFace->hasExternalLexicalStorage())
          Source->CompleteType(IFace);
      // If the external source completed the type, go through the motions
      // again to ensure we're allowed to use the completed type.
      if (!T->isIncompleteType())
        return RequireCompleteTypeImpl(Loc, T, Kind, Diagnoser);
    }
  }

  // If we have a class template specialization or a class member of a
  // class template specialization, or an array with known size of such,
  // try to instantiate it.
  if (auto *RD = dyn_cast_or_null<CXXRecordDecl>(Tag)) {
    bool Instantiated = false;
    bool Diagnosed = false;
    if (RD->isDependentContext()) {
      // Don't try to instantiate a dependent class (eg, a member template of
      // an instantiated class template specialization).
      // FIXME: Can this ever happen?
    } else if (auto *ClassTemplateSpec =
            dyn_cast<ClassTemplateSpecializationDecl>(RD)) {
      if (ClassTemplateSpec->getSpecializationKind() == TSK_Undeclared) {
        runWithSufficientStackSpace(Loc, [&] {
          Diagnosed = InstantiateClassTemplateSpecialization(
              Loc, ClassTemplateSpec, TSK_ImplicitInstantiation,
              /*Complain=*/Diagnoser);
        });
        Instantiated = true;
      }
    } else {
      CXXRecordDecl *Pattern = RD->getInstantiatedFromMemberClass();
      if (!RD->isBeingDefined() && Pattern) {
        MemberSpecializationInfo *MSI = RD->getMemberSpecializationInfo();
        assert(MSI && "Missing member specialization information?");
        // This record was instantiated from a class within a template.
        if (MSI->getTemplateSpecializationKind() !=
            TSK_ExplicitSpecialization) {
          runWithSufficientStackSpace(Loc, [&] {
            Diagnosed = InstantiateClass(Loc, RD, Pattern,
                                         getTemplateInstantiationArgs(RD),
                                         TSK_ImplicitInstantiation,
                                         /*Complain=*/Diagnoser);
          });
          Instantiated = true;
        }
      }
    }

    if (Instantiated) {
      // Instantiate* might have already complained that the template is not
      // defined, if we asked it to.
      if (Diagnoser && Diagnosed)
        return true;
      // If we instantiated a definition, check that it's usable, even if
      // instantiation produced an error, so that repeated calls to this
      // function give consistent answers.
      if (!T->isIncompleteType())
        return RequireCompleteTypeImpl(Loc, T, Kind, Diagnoser);
    }
  }

  // FIXME: If we didn't instantiate a definition because of an explicit
  // specialization declaration, check that it's visible.

  if (!Diagnoser)
    return true;

  Diagnoser->diagnose(*this, Loc, T);

  // If the type was a forward declaration of a class/struct/union
  // type, produce a note.
  if (Tag && !Tag->isInvalidDecl() && !Tag->getLocation().isInvalid())
    Diag(Tag->getLocation(),
         Tag->isBeingDefined() ? diag::note_type_being_defined
                               : diag::note_forward_declaration)
      << Context.getTagDeclType(Tag);

  // If the Objective-C class was a forward declaration, produce a note.
  if (IFace && !IFace->isInvalidDecl() && !IFace->getLocation().isInvalid())
    Diag(IFace->getLocation(), diag::note_forward_class);

  // If we have external information that we can use to suggest a fix,
  // produce a note.
  if (ExternalSource)
    ExternalSource->MaybeDiagnoseMissingCompleteType(Loc, T);

  return true;
}

bool Sema::RequireCompleteType(SourceLocation Loc, QualType T,
                               CompleteTypeKind Kind, unsigned DiagID) {
  BoundTypeDiagnoser<> Diagnoser(DiagID);
  return RequireCompleteType(Loc, T, Kind, Diagnoser);
}

/// Get diagnostic %select index for tag kind for
/// literal type diagnostic message.
/// WARNING: Indexes apply to particular diagnostics only!
///
/// \returns diagnostic %select index.
static unsigned getLiteralDiagFromTagKind(TagTypeKind Tag) {
  switch (Tag) {
  case TagTypeKind::Struct:
    return 0;
  case TagTypeKind::Interface:
    return 1;
  case TagTypeKind::Class:
    return 2;
  default: llvm_unreachable("Invalid tag kind for literal type diagnostic!");
  }
}

bool Sema::RequireLiteralType(SourceLocation Loc, QualType T,
                              TypeDiagnoser &Diagnoser) {
  assert(!T->isDependentType() && "type should not be dependent");

  QualType ElemType = Context.getBaseElementType(T);
  if ((isCompleteType(Loc, ElemType) || ElemType->isVoidType()) &&
      T->isLiteralType(Context))
    return false;

  Diagnoser.diagnose(*this, Loc, T);

  if (T->isVariableArrayType())
    return true;

  const RecordType *RT = ElemType->getAs<RecordType>();
  if (!RT)
    return true;

  const CXXRecordDecl *RD = cast<CXXRecordDecl>(RT->getDecl());

  // A partially-defined class type can't be a literal type, because a literal
  // class type must have a trivial destructor (which can't be checked until
  // the class definition is complete).
  if (RequireCompleteType(Loc, ElemType, diag::note_non_literal_incomplete, T))
    return true;

  // [expr.prim.lambda]p3:
  //   This class type is [not] a literal type.
  if (RD->isLambda() && !getLangOpts().CPlusPlus17) {
    Diag(RD->getLocation(), diag::note_non_literal_lambda);
    return true;
  }

  // If the class has virtual base classes, then it's not an aggregate, and
  // cannot have any constexpr constructors or a trivial default constructor,
  // so is non-literal. This is better to diagnose than the resulting absence
  // of constexpr constructors.
  if (RD->getNumVBases()) {
    Diag(RD->getLocation(), diag::note_non_literal_virtual_base)
      << getLiteralDiagFromTagKind(RD->getTagKind()) << RD->getNumVBases();
    for (const auto &I : RD->vbases())
      Diag(I.getBeginLoc(), diag::note_constexpr_virtual_base_here)
          << I.getSourceRange();
  } else if (!RD->isAggregate() && !RD->hasConstexprNonCopyMoveConstructor() &&
             !RD->hasTrivialDefaultConstructor()) {
    Diag(RD->getLocation(), diag::note_non_literal_no_constexpr_ctors) << RD;
  } else if (RD->hasNonLiteralTypeFieldsOrBases()) {
    for (const auto &I : RD->bases()) {
      if (!I.getType()->isLiteralType(Context)) {
        Diag(I.getBeginLoc(), diag::note_non_literal_base_class)
            << RD << I.getType() << I.getSourceRange();
        return true;
      }
    }
    for (const auto *I : RD->fields()) {
      if (!I->getType()->isLiteralType(Context) ||
          I->getType().isVolatileQualified()) {
        Diag(I->getLocation(), diag::note_non_literal_field)
          << RD << I << I->getType()
          << I->getType().isVolatileQualified();
        return true;
      }
    }
  } else if (getLangOpts().CPlusPlus20 ? !RD->hasConstexprDestructor()
                                       : !RD->hasTrivialDestructor()) {
    // All fields and bases are of literal types, so have trivial or constexpr
    // destructors. If this class's destructor is non-trivial / non-constexpr,
    // it must be user-declared.
    CXXDestructorDecl *Dtor = RD->getDestructor();
    assert(Dtor && "class has literal fields and bases but no dtor?");
    if (!Dtor)
      return true;

    if (getLangOpts().CPlusPlus20) {
      Diag(Dtor->getLocation(), diag::note_non_literal_non_constexpr_dtor)
          << RD;
    } else {
      Diag(Dtor->getLocation(), Dtor->isUserProvided()
                                    ? diag::note_non_literal_user_provided_dtor
                                    : diag::note_non_literal_nontrivial_dtor)
          << RD;
      if (!Dtor->isUserProvided())
        SpecialMemberIsTrivial(Dtor, CXXSpecialMemberKind::Destructor,
                               TAH_IgnoreTrivialABI,
                               /*Diagnose*/ true);
    }
  }

  return true;
}

bool Sema::RequireLiteralType(SourceLocation Loc, QualType T, unsigned DiagID) {
  BoundTypeDiagnoser<> Diagnoser(DiagID);
  return RequireLiteralType(Loc, T, Diagnoser);
}

QualType Sema::getElaboratedType(ElaboratedTypeKeyword Keyword,
                                 const CXXScopeSpec &SS, QualType T,
                                 TagDecl *OwnedTagDecl) {
  if (T.isNull())
    return T;
  return Context.getElaboratedType(
      Keyword, SS.isValid() ? SS.getScopeRep() : nullptr, T, OwnedTagDecl);
}

QualType Sema::BuildTypeofExprType(Expr *E, TypeOfKind Kind) {
  assert(!E->hasPlaceholderType() && "unexpected placeholder");

  if (!getLangOpts().CPlusPlus && E->refersToBitField())
    Diag(E->getExprLoc(), diag::err_sizeof_alignof_typeof_bitfield)
        << (Kind == TypeOfKind::Unqualified ? 3 : 2);

  if (!E->isTypeDependent()) {
    QualType T = E->getType();
    if (const TagType *TT = T->getAs<TagType>())
      DiagnoseUseOfDecl(TT->getDecl(), E->getExprLoc());
  }
  return Context.getTypeOfExprType(E, Kind);
}

static void
BuildTypeCoupledDecls(Expr *E,
                      llvm::SmallVectorImpl<TypeCoupledDeclRefInfo> &Decls) {
  // Currently, 'counted_by' only allows direct DeclRefExpr to FieldDecl.
  auto *CountDecl = cast<DeclRefExpr>(E)->getDecl();
  Decls.push_back(TypeCoupledDeclRefInfo(CountDecl, /*IsDref*/ false));
}

QualType Sema::BuildCountAttributedArrayOrPointerType(QualType WrappedTy,
                                                      Expr *CountExpr,
                                                      bool CountInBytes,
                                                      bool OrNull) {
  assert(WrappedTy->isIncompleteArrayType() || WrappedTy->isPointerType());

  llvm::SmallVector<TypeCoupledDeclRefInfo, 1> Decls;
  BuildTypeCoupledDecls(CountExpr, Decls);
  /// When the resulting expression is invalid, we still create the AST using
  /// the original count expression for the sake of AST dump.
  return Context.getCountAttributedType(WrappedTy, CountExpr, CountInBytes,
                                        OrNull, Decls);
}

/// getDecltypeForExpr - Given an expr, will return the decltype for
/// that expression, according to the rules in C++11
/// [dcl.type.simple]p4 and C++11 [expr.lambda.prim]p18.
QualType Sema::getDecltypeForExpr(Expr *E) {

  Expr *IDExpr = E;
  if (auto *ImplCastExpr = dyn_cast<ImplicitCastExpr>(E))
    IDExpr = ImplCastExpr->getSubExpr();

  if (auto *PackExpr = dyn_cast<PackIndexingExpr>(E)) {
    if (E->isInstantiationDependent())
      IDExpr = PackExpr->getPackIdExpression();
    else
      IDExpr = PackExpr->getSelectedExpr();
  }

  if (E->isTypeDependent())
    return Context.DependentTy;

  // C++11 [dcl.type.simple]p4:
  //   The type denoted by decltype(e) is defined as follows:

  // C++20:
  //     - if E is an unparenthesized id-expression naming a non-type
  //       template-parameter (13.2), decltype(E) is the type of the
  //       template-parameter after performing any necessary type deduction
  // Note that this does not pick up the implicit 'const' for a template
  // parameter object. This rule makes no difference before C++20 so we apply
  // it unconditionally.
  if (const auto *SNTTPE = dyn_cast<SubstNonTypeTemplateParmExpr>(IDExpr))
    return SNTTPE->getParameterType(Context);

  //     - if e is an unparenthesized id-expression or an unparenthesized class
  //       member access (5.2.5), decltype(e) is the type of the entity named
  //       by e. If there is no such entity, or if e names a set of overloaded
  //       functions, the program is ill-formed;
  //
  // We apply the same rules for Objective-C ivar and property references.
  if (const auto *DRE = dyn_cast<DeclRefExpr>(IDExpr)) {
    const ValueDecl *VD = DRE->getDecl();
    QualType T = VD->getType();
    return isa<TemplateParamObjectDecl>(VD) ? T.getUnqualifiedType() : T;
  }
  if (const auto *ME = dyn_cast<MemberExpr>(IDExpr)) {
    if (const auto *VD = ME->getMemberDecl())
      if (isa<FieldDecl>(VD) || isa<VarDecl>(VD))
        return VD->getType();
  } else if (const auto *IR = dyn_cast<ObjCIvarRefExpr>(IDExpr)) {
    return IR->getDecl()->getType();
  } else if (const auto *PR = dyn_cast<ObjCPropertyRefExpr>(IDExpr)) {
    if (PR->isExplicitProperty())
      return PR->getExplicitProperty()->getType();
  } else if (const auto *PE = dyn_cast<PredefinedExpr>(IDExpr)) {
    return PE->getType();
  }

  // C++11 [expr.lambda.prim]p18:
  //   Every occurrence of decltype((x)) where x is a possibly
  //   parenthesized id-expression that names an entity of automatic
  //   storage duration is treated as if x were transformed into an
  //   access to a corresponding data member of the closure type that
  //   would have been declared if x were an odr-use of the denoted
  //   entity.
  if (getCurLambda() && isa<ParenExpr>(IDExpr)) {
    if (auto *DRE = dyn_cast<DeclRefExpr>(IDExpr->IgnoreParens())) {
      if (auto *Var = dyn_cast<VarDecl>(DRE->getDecl())) {
        QualType T = getCapturedDeclRefType(Var, DRE->getLocation());
        if (!T.isNull())
          return Context.getLValueReferenceType(T);
      }
    }
  }

  return Context.getReferenceQualifiedType(E);
}

QualType Sema::BuildDecltypeType(Expr *E, bool AsUnevaluated) {
  assert(!E->hasPlaceholderType() && "unexpected placeholder");

  if (AsUnevaluated && CodeSynthesisContexts.empty() &&
      !E->isInstantiationDependent() && E->HasSideEffects(Context, false)) {
    // The expression operand for decltype is in an unevaluated expression
    // context, so side effects could result in unintended consequences.
    // Exclude instantiation-dependent expressions, because 'decltype' is often
    // used to build SFINAE gadgets.
    Diag(E->getExprLoc(), diag::warn_side_effects_unevaluated_context);
  }
  return Context.getDecltypeType(E, getDecltypeForExpr(E));
}

QualType Sema::ActOnPackIndexingType(QualType Pattern, Expr *IndexExpr,
                                     SourceLocation Loc,
                                     SourceLocation EllipsisLoc) {
  if (!IndexExpr)
    return QualType();

  // Diagnose unexpanded packs but continue to improve recovery.
  if (!Pattern->containsUnexpandedParameterPack())
    Diag(Loc, diag::err_expected_name_of_pack) << Pattern;

  QualType Type = BuildPackIndexingType(Pattern, IndexExpr, Loc, EllipsisLoc);

  if (!Type.isNull())
    Diag(Loc, getLangOpts().CPlusPlus26 ? diag::warn_cxx23_pack_indexing
                                        : diag::ext_pack_indexing);
  return Type;
}

QualType Sema::BuildPackIndexingType(QualType Pattern, Expr *IndexExpr,
                                     SourceLocation Loc,
                                     SourceLocation EllipsisLoc,
                                     bool FullySubstituted,
                                     ArrayRef<QualType> Expansions) {

  std::optional<int64_t> Index;
  if (FullySubstituted && !IndexExpr->isValueDependent() &&
      !IndexExpr->isTypeDependent()) {
    llvm::APSInt Value(Context.getIntWidth(Context.getSizeType()));
    ExprResult Res = CheckConvertedConstantExpression(
        IndexExpr, Context.getSizeType(), Value, CCEK_ArrayBound);
    if (!Res.isUsable())
      return QualType();
    Index = Value.getExtValue();
    IndexExpr = Res.get();
  }

  if (FullySubstituted && Index) {
    if (*Index < 0 || *Index >= int64_t(Expansions.size())) {
      Diag(IndexExpr->getBeginLoc(), diag::err_pack_index_out_of_bound)
          << *Index << Pattern << Expansions.size();
      return QualType();
    }
  }

  return Context.getPackIndexingType(Pattern, IndexExpr, FullySubstituted,
                                     Expansions, Index.value_or(-1));
}

static QualType GetEnumUnderlyingType(Sema &S, QualType BaseType,
                                      SourceLocation Loc) {
  assert(BaseType->isEnumeralType());
  EnumDecl *ED = BaseType->castAs<EnumType>()->getDecl();
  assert(ED && "EnumType has no EnumDecl");

  S.DiagnoseUseOfDecl(ED, Loc);

  QualType Underlying = ED->getIntegerType();
  assert(!Underlying.isNull());

  return Underlying;
}

QualType Sema::BuiltinEnumUnderlyingType(QualType BaseType,
                                         SourceLocation Loc) {
  if (!BaseType->isEnumeralType()) {
    Diag(Loc, diag::err_only_enums_have_underlying_types);
    return QualType();
  }

  // The enum could be incomplete if we're parsing its definition or
  // recovering from an error.
  NamedDecl *FwdDecl = nullptr;
  if (BaseType->isIncompleteType(&FwdDecl)) {
    Diag(Loc, diag::err_underlying_type_of_incomplete_enum) << BaseType;
    Diag(FwdDecl->getLocation(), diag::note_forward_declaration) << FwdDecl;
    return QualType();
  }

  return GetEnumUnderlyingType(*this, BaseType, Loc);
}

QualType Sema::BuiltinAddPointer(QualType BaseType, SourceLocation Loc) {
  QualType Pointer = BaseType.isReferenceable() || BaseType->isVoidType()
                         ? BuildPointerType(BaseType.getNonReferenceType(), Loc,
                                            DeclarationName())
                         : BaseType;

  return Pointer.isNull() ? QualType() : Pointer;
}

QualType Sema::BuiltinRemovePointer(QualType BaseType, SourceLocation Loc) {
  // We don't want block pointers or ObjectiveC's id type.
  if (!BaseType->isAnyPointerType() || BaseType->isObjCIdType())
    return BaseType;

  return BaseType->getPointeeType();
}

QualType Sema::BuiltinDecay(QualType BaseType, SourceLocation Loc) {
  QualType Underlying = BaseType.getNonReferenceType();
  if (Underlying->isArrayType())
    return Context.getDecayedType(Underlying);

  if (Underlying->isFunctionType())
    return BuiltinAddPointer(BaseType, Loc);

  SplitQualType Split = Underlying.getSplitUnqualifiedType();
  // std::decay is supposed to produce 'std::remove_cv', but since 'restrict' is
  // in the same group of qualifiers as 'const' and 'volatile', we're extending
  // '__decay(T)' so that it removes all qualifiers.
  Split.Quals.removeCVRQualifiers();
  return Context.getQualifiedType(Split);
}

QualType Sema::BuiltinAddReference(QualType BaseType, UTTKind UKind,
                                   SourceLocation Loc) {
  assert(LangOpts.CPlusPlus);
  QualType Reference =
      BaseType.isReferenceable()
          ? BuildReferenceType(BaseType,
                               UKind == UnaryTransformType::AddLvalueReference,
                               Loc, DeclarationName())
          : BaseType;
  return Reference.isNull() ? QualType() : Reference;
}

QualType Sema::BuiltinRemoveExtent(QualType BaseType, UTTKind UKind,
                                   SourceLocation Loc) {
  if (UKind == UnaryTransformType::RemoveAllExtents)
    return Context.getBaseElementType(BaseType);

  if (const auto *AT = Context.getAsArrayType(BaseType))
    return AT->getElementType();

  return BaseType;
}

QualType Sema::BuiltinRemoveReference(QualType BaseType, UTTKind UKind,
                                      SourceLocation Loc) {
  assert(LangOpts.CPlusPlus);
  QualType T = BaseType.getNonReferenceType();
  if (UKind == UTTKind::RemoveCVRef &&
      (T.isConstQualified() || T.isVolatileQualified())) {
    Qualifiers Quals;
    QualType Unqual = Context.getUnqualifiedArrayType(T, Quals);
    Quals.removeConst();
    Quals.removeVolatile();
    T = Context.getQualifiedType(Unqual, Quals);
  }
  return T;
}

QualType Sema::BuiltinChangeCVRQualifiers(QualType BaseType, UTTKind UKind,
                                          SourceLocation Loc) {
  if ((BaseType->isReferenceType() && UKind != UTTKind::RemoveRestrict) ||
      BaseType->isFunctionType())
    return BaseType;

  Qualifiers Quals;
  QualType Unqual = Context.getUnqualifiedArrayType(BaseType, Quals);

  if (UKind == UTTKind::RemoveConst || UKind == UTTKind::RemoveCV)
    Quals.removeConst();
  if (UKind == UTTKind::RemoveVolatile || UKind == UTTKind::RemoveCV)
    Quals.removeVolatile();
  if (UKind == UTTKind::RemoveRestrict)
    Quals.removeRestrict();

  return Context.getQualifiedType(Unqual, Quals);
}

static QualType ChangeIntegralSignedness(Sema &S, QualType BaseType,
                                         bool IsMakeSigned,
                                         SourceLocation Loc) {
  if (BaseType->isEnumeralType()) {
    QualType Underlying = GetEnumUnderlyingType(S, BaseType, Loc);
    if (auto *BitInt = dyn_cast<BitIntType>(Underlying)) {
      unsigned int Bits = BitInt->getNumBits();
      if (Bits > 1)
        return S.Context.getBitIntType(!IsMakeSigned, Bits);

      S.Diag(Loc, diag::err_make_signed_integral_only)
          << IsMakeSigned << /*_BitInt(1)*/ true << BaseType << 1 << Underlying;
      return QualType();
    }
    if (Underlying->isBooleanType()) {
      S.Diag(Loc, diag::err_make_signed_integral_only)
          << IsMakeSigned << /*_BitInt(1)*/ false << BaseType << 1
          << Underlying;
      return QualType();
    }
  }

  bool Int128Unsupported = !S.Context.getTargetInfo().hasInt128Type();
  std::array<CanQualType *, 6> AllSignedIntegers = {
      &S.Context.SignedCharTy, &S.Context.ShortTy,    &S.Context.IntTy,
      &S.Context.LongTy,       &S.Context.LongLongTy, &S.Context.Int128Ty};
  ArrayRef<CanQualType *> AvailableSignedIntegers(
      AllSignedIntegers.data(), AllSignedIntegers.size() - Int128Unsupported);
  std::array<CanQualType *, 6> AllUnsignedIntegers = {
      &S.Context.UnsignedCharTy,     &S.Context.UnsignedShortTy,
      &S.Context.UnsignedIntTy,      &S.Context.UnsignedLongTy,
      &S.Context.UnsignedLongLongTy, &S.Context.UnsignedInt128Ty};
  ArrayRef<CanQualType *> AvailableUnsignedIntegers(AllUnsignedIntegers.data(),
                                                    AllUnsignedIntegers.size() -
                                                        Int128Unsupported);
  ArrayRef<CanQualType *> *Consider =
      IsMakeSigned ? &AvailableSignedIntegers : &AvailableUnsignedIntegers;

  uint64_t BaseSize = S.Context.getTypeSize(BaseType);
  auto *Result =
      llvm::find_if(*Consider, [&S, BaseSize](const CanQual<Type> *T) {
        return BaseSize == S.Context.getTypeSize(T->getTypePtr());
      });

  assert(Result != Consider->end());
  return QualType((*Result)->getTypePtr(), 0);
}

QualType Sema::BuiltinChangeSignedness(QualType BaseType, UTTKind UKind,
                                       SourceLocation Loc) {
  bool IsMakeSigned = UKind == UnaryTransformType::MakeSigned;
  if ((!BaseType->isIntegerType() && !BaseType->isEnumeralType()) ||
      BaseType->isBooleanType() ||
      (BaseType->isBitIntType() &&
       BaseType->getAs<BitIntType>()->getNumBits() < 2)) {
    Diag(Loc, diag::err_make_signed_integral_only)
        << IsMakeSigned << BaseType->isBitIntType() << BaseType << 0;
    return QualType();
  }

  bool IsNonIntIntegral =
      BaseType->isChar16Type() || BaseType->isChar32Type() ||
      BaseType->isWideCharType() || BaseType->isEnumeralType();

  QualType Underlying =
      IsNonIntIntegral
          ? ChangeIntegralSignedness(*this, BaseType, IsMakeSigned, Loc)
      : IsMakeSigned ? Context.getCorrespondingSignedType(BaseType)
                     : Context.getCorrespondingUnsignedType(BaseType);
  if (Underlying.isNull())
    return Underlying;
  return Context.getQualifiedType(Underlying, BaseType.getQualifiers());
}

QualType Sema::BuildUnaryTransformType(QualType BaseType, UTTKind UKind,
                                       SourceLocation Loc) {
  if (BaseType->isDependentType())
    return Context.getUnaryTransformType(BaseType, BaseType, UKind);
  QualType Result;
  switch (UKind) {
  case UnaryTransformType::EnumUnderlyingType: {
    Result = BuiltinEnumUnderlyingType(BaseType, Loc);
    break;
  }
  case UnaryTransformType::AddPointer: {
    Result = BuiltinAddPointer(BaseType, Loc);
    break;
  }
  case UnaryTransformType::RemovePointer: {
    Result = BuiltinRemovePointer(BaseType, Loc);
    break;
  }
  case UnaryTransformType::Decay: {
    Result = BuiltinDecay(BaseType, Loc);
    break;
  }
  case UnaryTransformType::AddLvalueReference:
  case UnaryTransformType::AddRvalueReference: {
    Result = BuiltinAddReference(BaseType, UKind, Loc);
    break;
  }
  case UnaryTransformType::RemoveAllExtents:
  case UnaryTransformType::RemoveExtent: {
    Result = BuiltinRemoveExtent(BaseType, UKind, Loc);
    break;
  }
  case UnaryTransformType::RemoveCVRef:
  case UnaryTransformType::RemoveReference: {
    Result = BuiltinRemoveReference(BaseType, UKind, Loc);
    break;
  }
  case UnaryTransformType::RemoveConst:
  case UnaryTransformType::RemoveCV:
  case UnaryTransformType::RemoveRestrict:
  case UnaryTransformType::RemoveVolatile: {
    Result = BuiltinChangeCVRQualifiers(BaseType, UKind, Loc);
    break;
  }
  case UnaryTransformType::MakeSigned:
  case UnaryTransformType::MakeUnsigned: {
    Result = BuiltinChangeSignedness(BaseType, UKind, Loc);
    break;
  }
  }

  return !Result.isNull()
             ? Context.getUnaryTransformType(BaseType, Result, UKind)
             : Result;
}

QualType Sema::BuildAtomicType(QualType T, SourceLocation Loc) {
  if (!isDependentOrGNUAutoType(T)) {
    // FIXME: It isn't entirely clear whether incomplete atomic types
    // are allowed or not; for simplicity, ban them for the moment.
    if (RequireCompleteType(Loc, T, diag::err_atomic_specifier_bad_type, 0))
      return QualType();

    int DisallowedKind = -1;
    if (T->isArrayType())
      DisallowedKind = 1;
    else if (T->isFunctionType())
      DisallowedKind = 2;
    else if (T->isReferenceType())
      DisallowedKind = 3;
    else if (T->isAtomicType())
      DisallowedKind = 4;
    else if (T.hasQualifiers())
      DisallowedKind = 5;
    else if (T->isSizelessType())
      DisallowedKind = 6;
    else if (!T.isTriviallyCopyableType(Context) && getLangOpts().CPlusPlus)
      // Some other non-trivially-copyable type (probably a C++ class)
      DisallowedKind = 7;
    else if (T->isBitIntType())
      DisallowedKind = 8;
    else if (getLangOpts().C23 && T->isUndeducedAutoType())
      // _Atomic auto is prohibited in C23
      DisallowedKind = 9;

    if (DisallowedKind != -1) {
      Diag(Loc, diag::err_atomic_specifier_bad_type) << DisallowedKind << T;
      return QualType();
    }

    // FIXME: Do we need any handling for ARC here?
  }

  // Build the pointer type.
  return Context.getAtomicType(T);
}
