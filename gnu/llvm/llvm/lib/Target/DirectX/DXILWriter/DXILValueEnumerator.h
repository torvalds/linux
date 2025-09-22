//===- DirectX/DXILWriter/ValueEnumerator.h - Number values -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class gives values and types Unique ID's.
// Forked from lib/Bitcode/Writer
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DXILWRITER_VALUEENUMERATOR_H
#define LLVM_DXILWRITER_VALUEENUMERATOR_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/UniqueVector.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/UseListOrder.h"
#include <cassert>
#include <cstdint>
#include <utility>
#include <vector>

namespace llvm {

class BasicBlock;
class Comdat;
class DIArgList;
class Function;
class Instruction;
class LocalAsMetadata;
class MDNode;
class Metadata;
class Module;
class NamedMDNode;
class raw_ostream;
class Type;
class Value;
class ValueSymbolTable;

namespace dxil {

class ValueEnumerator {
public:
  using TypeList = std::vector<Type *>;

  // For each value, we remember its Value* and occurrence frequency.
  using ValueList = std::vector<std::pair<const Value *, unsigned>>;

  /// Attribute groups as encoded in bitcode are almost AttributeSets, but they
  /// include the AttributeList index, so we have to track that in our map.
  using IndexAndAttrSet = std::pair<unsigned, AttributeSet>;

  UseListOrderStack UseListOrders;

private:
  using TypeMapType = DenseMap<Type *, unsigned>;
  TypeMapType TypeMap;
  TypeList Types;

  using ValueMapType = DenseMap<const Value *, unsigned>;
  ValueMapType ValueMap;
  ValueList Values;

  using ComdatSetType = UniqueVector<const Comdat *>;
  ComdatSetType Comdats;

  std::vector<const Metadata *> MDs;
  std::vector<const Metadata *> FunctionMDs;

  /// Index of information about a piece of metadata.
  struct MDIndex {
    unsigned F = 0;  ///< The ID of the function for this metadata, if any.
    unsigned ID = 0; ///< The implicit ID of this metadata in bitcode.

    MDIndex() = default;
    explicit MDIndex(unsigned F) : F(F) {}

    /// Check if this has a function tag, and it's different from NewF.
    bool hasDifferentFunction(unsigned NewF) const { return F && F != NewF; }

    /// Fetch the MD this references out of the given metadata array.
    const Metadata *get(ArrayRef<const Metadata *> MDs) const {
      assert(ID && "Expected non-zero ID");
      assert(ID <= MDs.size() && "Expected valid ID");
      return MDs[ID - 1];
    }
  };

  using MetadataMapType = DenseMap<const Metadata *, MDIndex>;
  MetadataMapType MetadataMap;

  /// Range of metadata IDs, as a half-open range.
  struct MDRange {
    unsigned First = 0;
    unsigned Last = 0;

    /// Number of strings in the prefix of the metadata range.
    unsigned NumStrings = 0;

    MDRange() = default;
    explicit MDRange(unsigned First) : First(First) {}
  };
  SmallDenseMap<unsigned, MDRange, 1> FunctionMDInfo;

  using AttributeGroupMapType = DenseMap<IndexAndAttrSet, unsigned>;
  AttributeGroupMapType AttributeGroupMap;
  std::vector<IndexAndAttrSet> AttributeGroups;

  using AttributeListMapType = DenseMap<AttributeList, unsigned>;
  AttributeListMapType AttributeListMap;
  std::vector<AttributeList> AttributeLists;

  /// GlobalBasicBlockIDs - This map memoizes the basic block ID's referenced by
  /// the "getGlobalBasicBlockID" method.
  mutable DenseMap<const BasicBlock *, unsigned> GlobalBasicBlockIDs;

  using InstructionMapType = DenseMap<const Instruction *, unsigned>;
  InstructionMapType InstructionMap;
  unsigned InstructionCount;

  /// BasicBlocks - This contains all the basic blocks for the currently
  /// incorporated function.  Their reverse mapping is stored in ValueMap.
  std::vector<const BasicBlock *> BasicBlocks;

  /// When a function is incorporated, this is the size of the Values list
  /// before incorporation.
  unsigned NumModuleValues;

  /// When a function is incorporated, this is the size of the Metadatas list
  /// before incorporation.
  unsigned NumModuleMDs = 0;
  unsigned NumMDStrings = 0;

  unsigned FirstFuncConstantID;
  unsigned FirstInstID;

public:
  ValueEnumerator(const Module &M, Type *PrefixType);
  ValueEnumerator(const ValueEnumerator &) = delete;
  ValueEnumerator &operator=(const ValueEnumerator &) = delete;

  void dump() const;
  void print(raw_ostream &OS, const ValueMapType &Map, const char *Name) const;
  void print(raw_ostream &OS, const MetadataMapType &Map,
             const char *Name) const;

  unsigned getValueID(const Value *V) const;

  unsigned getMetadataID(const Metadata *MD) const {
    auto ID = getMetadataOrNullID(MD);
    assert(ID != 0 && "Metadata not in slotcalculator!");
    return ID - 1;
  }

  unsigned getMetadataOrNullID(const Metadata *MD) const {
    return MetadataMap.lookup(MD).ID;
  }

  unsigned numMDs() const { return MDs.size(); }

  unsigned getTypeID(Type *T) const {
    TypeMapType::const_iterator I = TypeMap.find(T);
    assert(I != TypeMap.end() && "Type not in ValueEnumerator!");
    return I->second - 1;
  }

  unsigned getInstructionID(const Instruction *I) const;
  void setInstructionID(const Instruction *I);

  unsigned getAttributeListID(AttributeList PAL) const {
    if (PAL.isEmpty())
      return 0; // Null maps to zero.
    AttributeListMapType::const_iterator I = AttributeListMap.find(PAL);
    assert(I != AttributeListMap.end() && "Attribute not in ValueEnumerator!");
    return I->second;
  }

  unsigned getAttributeGroupID(IndexAndAttrSet Group) const {
    if (!Group.second.hasAttributes())
      return 0; // Null maps to zero.
    AttributeGroupMapType::const_iterator I = AttributeGroupMap.find(Group);
    assert(I != AttributeGroupMap.end() && "Attribute not in ValueEnumerator!");
    return I->second;
  }

  /// getFunctionConstantRange - Return the range of values that corresponds to
  /// function-local constants.
  void getFunctionConstantRange(unsigned &Start, unsigned &End) const {
    Start = FirstFuncConstantID;
    End = FirstInstID;
  }

  const ValueList &getValues() const { return Values; }

  /// Check whether the current block has any metadata to emit.
  bool hasMDs() const { return NumModuleMDs < MDs.size(); }

  /// Get the MDString metadata for this block.
  ArrayRef<const Metadata *> getMDStrings() const {
    return ArrayRef(MDs).slice(NumModuleMDs, NumMDStrings);
  }

  /// Get the non-MDString metadata for this block.
  ArrayRef<const Metadata *> getNonMDStrings() const {
    return ArrayRef(MDs).slice(NumModuleMDs).slice(NumMDStrings);
  }

  const TypeList &getTypes() const { return Types; }

  const std::vector<const BasicBlock *> &getBasicBlocks() const {
    return BasicBlocks;
  }

  const std::vector<AttributeList> &getAttributeLists() const {
    return AttributeLists;
  }

  const std::vector<IndexAndAttrSet> &getAttributeGroups() const {
    return AttributeGroups;
  }

  const ComdatSetType &getComdats() const { return Comdats; }
  unsigned getComdatID(const Comdat *C) const;

  /// getGlobalBasicBlockID - This returns the function-specific ID for the
  /// specified basic block.  This is relatively expensive information, so it
  /// should only be used by rare constructs such as address-of-label.
  unsigned getGlobalBasicBlockID(const BasicBlock *BB) const;

  /// incorporateFunction/purgeFunction - If you'd like to deal with a function,
  /// use these two methods to get its data into the ValueEnumerator!
  void incorporateFunction(const Function &F);

  void purgeFunction();
  uint64_t computeBitsRequiredForTypeIndices() const;

  void EnumerateType(Type *T);

private:

  /// Reorder the reachable metadata.
  ///
  /// This is not just an optimization, but is mandatory for emitting MDString
  /// correctly.
  void organizeMetadata();

  /// Drop the function tag from the transitive operands of the given node.
  void dropFunctionFromMetadata(MetadataMapType::value_type &FirstMD);

  /// Incorporate the function metadata.
  ///
  /// This should be called before enumerating LocalAsMetadata for the
  /// function.
  void incorporateFunctionMetadata(const Function &F);

  /// Enumerate a single instance of metadata with the given function tag.
  ///
  /// If \c MD has already been enumerated, check that \c F matches its
  /// function tag.  If not, call \a dropFunctionFromMetadata().
  ///
  /// Otherwise, mark \c MD as visited.  Assign it an ID, or just return it if
  /// it's an \a MDNode.
  const MDNode *enumerateMetadataImpl(unsigned F, const Metadata *MD);

  unsigned getMetadataFunctionID(const Function *F) const;

  /// Enumerate reachable metadata in (almost) post-order.
  ///
  /// Enumerate all the metadata reachable from MD.  We want to minimize the
  /// cost of reading bitcode records, and so the primary consideration is that
  /// operands of uniqued nodes are resolved before the nodes are read.  This
  /// avoids re-uniquing them on the context and factors away RAUW support.
  ///
  /// This algorithm guarantees that subgraphs of uniqued nodes are in
  /// post-order.  Distinct subgraphs reachable only from a single uniqued node
  /// will be in post-order.
  ///
  /// \note The relative order of a distinct and uniqued node is irrelevant.
  /// \a organizeMetadata() will later partition distinct nodes ahead of
  /// uniqued ones.
  ///{
  void EnumerateMetadata(const Function *F, const Metadata *MD);
  void EnumerateMetadata(unsigned F, const Metadata *MD);
  ///}

  void EnumerateFunctionLocalMetadata(const Function &F,
                                      const LocalAsMetadata *Local);
  void EnumerateFunctionLocalMetadata(unsigned F, const LocalAsMetadata *Local);
  void EnumerateFunctionLocalListMetadata(const Function &F,
                                          const DIArgList *ArgList);
  void EnumerateFunctionLocalListMetadata(unsigned F, const DIArgList *Arglist);
  void EnumerateNamedMDNode(const NamedMDNode *NMD);
  void EnumerateValue(const Value *V);
  void EnumerateOperandType(const Value *V);
  void EnumerateAttributes(AttributeList PAL);

  void EnumerateValueSymbolTable(const ValueSymbolTable &ST);
  void EnumerateNamedMetadata(const Module &M);
};

} // end namespace dxil
} // end namespace llvm

#endif // LLVM_DXILWRITER_VALUEENUMERATOR_H
