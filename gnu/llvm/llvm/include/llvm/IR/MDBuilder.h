//===---- llvm/MDBuilder.h - Builder for LLVM metadata ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the MDBuilder class, which is used as a convenient way to
// create LLVM metadata with a consistent and simplified interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_MDBUILDER_H
#define LLVM_IR_MDBUILDER_H

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/Support/DataTypes.h"
#include <utility>

namespace llvm {

class APInt;
template <typename T> class ArrayRef;
class LLVMContext;
class Constant;
class ConstantAsMetadata;
class Function;
class MDNode;
class MDString;
class Metadata;

class MDBuilder {
  LLVMContext &Context;

public:
  MDBuilder(LLVMContext &context) : Context(context) {}

  /// Return the given string as metadata.
  MDString *createString(StringRef Str);

  /// Return the given constant as metadata.
  ConstantAsMetadata *createConstant(Constant *C);

  //===------------------------------------------------------------------===//
  // FPMath metadata.
  //===------------------------------------------------------------------===//

  /// Return metadata with the given settings.  The special value 0.0
  /// for the Accuracy parameter indicates the default (maximal precision)
  /// setting.
  MDNode *createFPMath(float Accuracy);

  //===------------------------------------------------------------------===//
  // Prof metadata.
  //===------------------------------------------------------------------===//

  /// Return metadata containing two branch weights.
  /// @param TrueWeight the weight of the true branch
  /// @param FalseWeight the weight of the false branch
  /// @param Do these weights come from __builtin_expect*
  MDNode *createBranchWeights(uint32_t TrueWeight, uint32_t FalseWeight,
                              bool IsExpected = false);

  /// Return metadata containing two branch weights, with significant bias
  /// towards `true` destination.
  MDNode *createLikelyBranchWeights();

  /// Return metadata containing two branch weights, with significant bias
  /// towards `false` destination.
  MDNode *createUnlikelyBranchWeights();

  /// Return metadata containing a number of branch weights.
  /// @param Weights the weights of all the branches
  /// @param Do these weights come from __builtin_expect*
  MDNode *createBranchWeights(ArrayRef<uint32_t> Weights,
                              bool IsExpected = false);

  /// Return metadata specifying that a branch or switch is unpredictable.
  MDNode *createUnpredictable();

  /// Return metadata containing the entry \p Count for a function, a boolean
  /// \Synthetic indicating whether the counts were synthetized, and the
  /// GUIDs stored in \p Imports that need to be imported for sample PGO, to
  /// enable the same inlines as the profiled optimized binary
  MDNode *createFunctionEntryCount(uint64_t Count, bool Synthetic,
                                   const DenseSet<GlobalValue::GUID> *Imports);

  /// Return metadata containing the section prefix for a function.
  MDNode *createFunctionSectionPrefix(StringRef Prefix);

  /// Return metadata containing the pseudo probe descriptor for a function.
  MDNode *createPseudoProbeDesc(uint64_t GUID, uint64_t Hash, StringRef FName);

  /// Return metadata containing llvm statistics.
  MDNode *
  createLLVMStats(ArrayRef<std::pair<StringRef, uint64_t>> LLVMStatsVec);

  //===------------------------------------------------------------------===//
  // Range metadata.
  //===------------------------------------------------------------------===//

  /// Return metadata describing the range [Lo, Hi).
  MDNode *createRange(const APInt &Lo, const APInt &Hi);

  /// Return metadata describing the range [Lo, Hi).
  MDNode *createRange(Constant *Lo, Constant *Hi);

  //===------------------------------------------------------------------===//
  // Callees metadata.
  //===------------------------------------------------------------------===//

  /// Return metadata indicating the possible callees of indirect
  /// calls.
  MDNode *createCallees(ArrayRef<Function *> Callees);

  //===------------------------------------------------------------------===//
  // Callback metadata.
  //===------------------------------------------------------------------===//

  /// Return metadata describing a callback (see llvm::AbstractCallSite).
  MDNode *createCallbackEncoding(unsigned CalleeArgNo, ArrayRef<int> Arguments,
                                 bool VarArgsArePassed);

  /// Merge the new callback encoding \p NewCB into \p ExistingCallbacks.
  MDNode *mergeCallbackEncodings(MDNode *ExistingCallbacks, MDNode *NewCB);

  /// Return metadata feeding to the CodeGen about how to generate a function
  /// prologue for the "function" santizier.
  MDNode *createRTTIPointerPrologue(Constant *PrologueSig, Constant *RTTI);

  //===------------------------------------------------------------------===//
  // PC sections metadata.
  //===------------------------------------------------------------------===//

  /// A pair of PC section name with auxilliary constant data.
  using PCSection = std::pair<StringRef, SmallVector<Constant *>>;

  /// Return metadata for PC sections.
  MDNode *createPCSections(ArrayRef<PCSection> Sections);

  //===------------------------------------------------------------------===//
  // AA metadata.
  //===------------------------------------------------------------------===//

protected:
  /// Return metadata appropriate for a AA root node (scope or TBAA).
  /// Each returned node is distinct from all other metadata and will never
  /// be identified (uniqued) with anything else.
  MDNode *createAnonymousAARoot(StringRef Name = StringRef(),
                                MDNode *Extra = nullptr);

public:
  /// Return metadata appropriate for a TBAA root node. Each returned
  /// node is distinct from all other metadata and will never be identified
  /// (uniqued) with anything else.
  MDNode *createAnonymousTBAARoot() {
    return createAnonymousAARoot();
  }

  /// Return metadata appropriate for an alias scope domain node.
  /// Each returned node is distinct from all other metadata and will never
  /// be identified (uniqued) with anything else.
  MDNode *createAnonymousAliasScopeDomain(StringRef Name = StringRef()) {
    return createAnonymousAARoot(Name);
  }

  /// Return metadata appropriate for an alias scope root node.
  /// Each returned node is distinct from all other metadata and will never
  /// be identified (uniqued) with anything else.
  MDNode *createAnonymousAliasScope(MDNode *Domain,
                                    StringRef Name = StringRef()) {
    return createAnonymousAARoot(Name, Domain);
  }

  /// Return metadata appropriate for a TBAA root node with the given
  /// name.  This may be identified (uniqued) with other roots with the same
  /// name.
  MDNode *createTBAARoot(StringRef Name);

  /// Return metadata appropriate for an alias scope domain node with
  /// the given name. This may be identified (uniqued) with other roots with
  /// the same name.
  MDNode *createAliasScopeDomain(StringRef Name);

  /// Return metadata appropriate for an alias scope node with
  /// the given name. This may be identified (uniqued) with other scopes with
  /// the same name and domain.
  MDNode *createAliasScope(StringRef Name, MDNode *Domain);

  /// Return metadata for a non-root TBAA node with the given name,
  /// parent in the TBAA tree, and value for 'pointsToConstantMemory'.
  MDNode *createTBAANode(StringRef Name, MDNode *Parent,
                         bool isConstant = false);

  struct TBAAStructField {
    uint64_t Offset;
    uint64_t Size;
    MDNode *Type;
    TBAAStructField(uint64_t Offset, uint64_t Size, MDNode *Type) :
      Offset(Offset), Size(Size), Type(Type) {}
  };

  /// Return metadata for a tbaa.struct node with the given
  /// struct field descriptions.
  MDNode *createTBAAStructNode(ArrayRef<TBAAStructField> Fields);

  /// Return metadata for a TBAA struct node in the type DAG
  /// with the given name, a list of pairs (offset, field type in the type DAG).
  MDNode *
  createTBAAStructTypeNode(StringRef Name,
                           ArrayRef<std::pair<MDNode *, uint64_t>> Fields);

  /// Return metadata for a TBAA scalar type node with the
  /// given name, an offset and a parent in the TBAA type DAG.
  MDNode *createTBAAScalarTypeNode(StringRef Name, MDNode *Parent,
                                   uint64_t Offset = 0);

  /// Return metadata for a TBAA tag node with the given
  /// base type, access type and offset relative to the base type.
  MDNode *createTBAAStructTagNode(MDNode *BaseType, MDNode *AccessType,
                                  uint64_t Offset, bool IsConstant = false);

  /// Return metadata for a TBAA type node in the TBAA type DAG with the
  /// given parent type, size in bytes, type identifier and a list of fields.
  MDNode *createTBAATypeNode(MDNode *Parent, uint64_t Size, Metadata *Id,
                             ArrayRef<TBAAStructField> Fields =
                                 ArrayRef<TBAAStructField>());

  /// Return metadata for a TBAA access tag with the given base type,
  /// final access type, offset of the access relative to the base type, size of
  /// the access and flag indicating whether the accessed object can be
  /// considered immutable for the purposes of the TBAA analysis.
  MDNode *createTBAAAccessTag(MDNode *BaseType, MDNode *AccessType,
                              uint64_t Offset, uint64_t Size,
                              bool IsImmutable = false);

  /// Return mutable version of the given mutable or immutable TBAA
  /// access tag.
  MDNode *createMutableTBAAAccessTag(MDNode *Tag);

  /// Return metadata containing an irreducible loop header weight.
  MDNode *createIrrLoopHeaderWeight(uint64_t Weight);
};

} // end namespace llvm

#endif
