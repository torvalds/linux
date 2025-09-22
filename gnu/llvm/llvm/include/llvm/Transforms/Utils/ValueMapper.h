//===- ValueMapper.h - Remapping for constants and metadata -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the MapValue interface which is used by various parts of
// the Transforms/Utils library to implement cloning and linking facilities.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_VALUEMAPPER_H
#define LLVM_TRANSFORMS_UTILS_VALUEMAPPER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/simple_ilist.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/IR/ValueMap.h"

namespace llvm {

class Constant;
class DIBuilder;
class DbgRecord;
class Function;
class GlobalVariable;
class Instruction;
class MDNode;
class Metadata;
class Module;
class Type;
class Value;

using ValueToValueMapTy = ValueMap<const Value *, WeakTrackingVH>;
using DbgRecordIterator = simple_ilist<DbgRecord>::iterator;

/// This is a class that can be implemented by clients to remap types when
/// cloning constants and instructions.
class ValueMapTypeRemapper {
  virtual void anchor(); // Out of line method.

public:
  virtual ~ValueMapTypeRemapper() = default;

  /// The client should implement this method if they want to remap types while
  /// mapping values.
  virtual Type *remapType(Type *SrcTy) = 0;
};

/// This is a class that can be implemented by clients to materialize Values on
/// demand.
class ValueMaterializer {
  virtual void anchor(); // Out of line method.

protected:
  ValueMaterializer() = default;
  ValueMaterializer(const ValueMaterializer &) = default;
  ValueMaterializer &operator=(const ValueMaterializer &) = default;
  ~ValueMaterializer() = default;

public:
  /// This method can be implemented to generate a mapped Value on demand. For
  /// example, if linking lazily. Returns null if the value is not materialized.
  virtual Value *materialize(Value *V) = 0;
};

/// These are flags that the value mapping APIs allow.
enum RemapFlags {
  RF_None = 0,

  /// If this flag is set, the remapper knows that only local values within a
  /// function (such as an instruction or argument) are mapped, not global
  /// values like functions and global metadata.
  RF_NoModuleLevelChanges = 1,

  /// If this flag is set, the remapper ignores missing function-local entries
  /// (Argument, Instruction, BasicBlock) that are not in the value map.  If it
  /// is unset, it aborts if an operand is asked to be remapped which doesn't
  /// exist in the mapping.
  ///
  /// There are no such assertions in MapValue(), whose results are almost
  /// unchanged by this flag.  This flag mainly changes the assertion behaviour
  /// in RemapInstruction().
  ///
  /// Since an Instruction's metadata operands (even that point to SSA values)
  /// aren't guaranteed to be dominated by their definitions, MapMetadata will
  /// return "!{}" instead of "null" for \a LocalAsMetadata instances whose SSA
  /// values are unmapped when this flag is set.  Otherwise, \a MapValue()
  /// completely ignores this flag.
  ///
  /// \a MapMetadata() always ignores this flag.
  RF_IgnoreMissingLocals = 2,

  /// Instruct the remapper to reuse and mutate distinct metadata (remapping
  /// them in place) instead of cloning remapped copies. This flag has no
  /// effect when RF_NoModuleLevelChanges, since that implies an identity
  /// mapping.
  RF_ReuseAndMutateDistinctMDs = 4,

  /// Any global values not in value map are mapped to null instead of mapping
  /// to self.  Illegal if RF_IgnoreMissingLocals is also set.
  RF_NullMapMissingGlobalValues = 8,
};

inline RemapFlags operator|(RemapFlags LHS, RemapFlags RHS) {
  return RemapFlags(unsigned(LHS) | unsigned(RHS));
}

/// Context for (re-)mapping values (and metadata).
///
/// A shared context used for mapping and remapping of Value and Metadata
/// instances using \a ValueToValueMapTy, \a RemapFlags, \a
/// ValueMapTypeRemapper, and \a ValueMaterializer.
///
/// There are a number of top-level entry points:
/// - \a mapValue() (and \a mapConstant());
/// - \a mapMetadata() (and \a mapMDNode());
/// - \a remapInstruction();
/// - \a remapFunction(); and
/// - \a remapGlobalObjectMetadata().
///
/// The \a ValueMaterializer can be used as a callback, but cannot invoke any
/// of these top-level functions recursively.  Instead, callbacks should use
/// one of the following to schedule work lazily in the \a ValueMapper
/// instance:
/// - \a scheduleMapGlobalInitializer()
/// - \a scheduleMapAppendingVariable()
/// - \a scheduleMapGlobalAlias()
/// - \a scheduleMapGlobalIFunc()
/// - \a scheduleRemapFunction()
///
/// Sometimes a callback needs a different mapping context.  Such a context can
/// be registered using \a registerAlternateMappingContext(), which takes an
/// alternate \a ValueToValueMapTy and \a ValueMaterializer and returns a ID to
/// pass into the schedule*() functions.
///
/// TODO: lib/Linker really doesn't need the \a ValueHandle in the \a
/// ValueToValueMapTy.  We should template \a ValueMapper (and its
/// implementation classes), and explicitly instantiate on two concrete
/// instances of \a ValueMap (one as \a ValueToValueMap, and one with raw \a
/// Value pointers).  It may be viable to do away with \a TrackingMDRef in the
/// \a Metadata side map for the lib/Linker case as well, in which case we'll
/// need a new template parameter on \a ValueMap.
///
/// TODO: Update callers of \a RemapInstruction() and \a MapValue() (etc.) to
/// use \a ValueMapper directly.
class ValueMapper {
  void *pImpl;

public:
  ValueMapper(ValueToValueMapTy &VM, RemapFlags Flags = RF_None,
              ValueMapTypeRemapper *TypeMapper = nullptr,
              ValueMaterializer *Materializer = nullptr);
  ValueMapper(ValueMapper &&) = delete;
  ValueMapper(const ValueMapper &) = delete;
  ValueMapper &operator=(ValueMapper &&) = delete;
  ValueMapper &operator=(const ValueMapper &) = delete;
  ~ValueMapper();

  /// Register an alternate mapping context.
  ///
  /// Returns a MappingContextID that can be used with the various schedule*()
  /// API to switch in a different value map on-the-fly.
  unsigned
  registerAlternateMappingContext(ValueToValueMapTy &VM,
                                  ValueMaterializer *Materializer = nullptr);

  /// Add to the current \a RemapFlags.
  ///
  /// \note Like the top-level mapping functions, \a addFlags() must be called
  /// at the top level, not during a callback in a \a ValueMaterializer.
  void addFlags(RemapFlags Flags);

  Metadata *mapMetadata(const Metadata &MD);
  MDNode *mapMDNode(const MDNode &N);

  Value *mapValue(const Value &V);
  Constant *mapConstant(const Constant &C);

  void remapInstruction(Instruction &I);
  void remapDbgRecord(Module *M, DbgRecord &V);
  void remapDbgRecordRange(Module *M, iterator_range<DbgRecordIterator> Range);
  void remapFunction(Function &F);
  void remapGlobalObjectMetadata(GlobalObject &GO);

  void scheduleMapGlobalInitializer(GlobalVariable &GV, Constant &Init,
                                    unsigned MappingContextID = 0);
  void scheduleMapAppendingVariable(GlobalVariable &GV, Constant *InitPrefix,
                                    bool IsOldCtorDtor,
                                    ArrayRef<Constant *> NewMembers,
                                    unsigned MappingContextID = 0);
  void scheduleMapGlobalAlias(GlobalAlias &GA, Constant &Aliasee,
                              unsigned MappingContextID = 0);
  void scheduleMapGlobalIFunc(GlobalIFunc &GI, Constant &Resolver,
                              unsigned MappingContextID = 0);
  void scheduleRemapFunction(Function &F, unsigned MappingContextID = 0);
};

/// Look up or compute a value in the value map.
///
/// Return a mapped value for a function-local value (Argument, Instruction,
/// BasicBlock), or compute and memoize a value for a Constant.
///
///  1. If \c V is in VM, return the result.
///  2. Else if \c V can be materialized with \c Materializer, do so, memoize
///     it in \c VM, and return it.
///  3. Else if \c V is a function-local value, return nullptr.
///  4. Else if \c V is a \a GlobalValue, return \c nullptr or \c V depending
///     on \a RF_NullMapMissingGlobalValues.
///  5. Else if \c V is a \a MetadataAsValue wrapping a LocalAsMetadata,
///     recurse on the local SSA value, and return nullptr or "metadata !{}" on
///     missing depending on RF_IgnoreMissingValues.
///  6. Else if \c V is a \a MetadataAsValue, rewrap the return of \a
///     MapMetadata().
///  7. Else, compute the equivalent constant, and return it.
inline Value *MapValue(const Value *V, ValueToValueMapTy &VM,
                       RemapFlags Flags = RF_None,
                       ValueMapTypeRemapper *TypeMapper = nullptr,
                       ValueMaterializer *Materializer = nullptr) {
  return ValueMapper(VM, Flags, TypeMapper, Materializer).mapValue(*V);
}

/// Lookup or compute a mapping for a piece of metadata.
///
/// Compute and memoize a mapping for \c MD.
///
///  1. If \c MD is mapped, return it.
///  2. Else if \a RF_NoModuleLevelChanges or \c MD is an \a MDString, return
///     \c MD.
///  3. Else if \c MD is a \a ConstantAsMetadata, call \a MapValue() and
///     re-wrap its return (returning nullptr on nullptr).
///  4. Else, \c MD is an \a MDNode.  These are remapped, along with their
///     transitive operands.  Distinct nodes are duplicated or moved depending
///     on \a RF_MoveDistinctNodes.  Uniqued nodes are remapped like constants.
///
/// \note \a LocalAsMetadata is completely unsupported by \a MapMetadata.
/// Instead, use \a MapValue() with its wrapping \a MetadataAsValue instance.
inline Metadata *MapMetadata(const Metadata *MD, ValueToValueMapTy &VM,
                             RemapFlags Flags = RF_None,
                             ValueMapTypeRemapper *TypeMapper = nullptr,
                             ValueMaterializer *Materializer = nullptr) {
  return ValueMapper(VM, Flags, TypeMapper, Materializer).mapMetadata(*MD);
}

/// Version of MapMetadata with type safety for MDNode.
inline MDNode *MapMetadata(const MDNode *MD, ValueToValueMapTy &VM,
                           RemapFlags Flags = RF_None,
                           ValueMapTypeRemapper *TypeMapper = nullptr,
                           ValueMaterializer *Materializer = nullptr) {
  return ValueMapper(VM, Flags, TypeMapper, Materializer).mapMDNode(*MD);
}

/// Convert the instruction operands from referencing the current values into
/// those specified by VM.
///
/// If \a RF_IgnoreMissingLocals is set and an operand can't be found via \a
/// MapValue(), use the old value.  Otherwise assert that this doesn't happen.
///
/// Note that \a MapValue() only returns \c nullptr for SSA values missing from
/// \c VM.
inline void RemapInstruction(Instruction *I, ValueToValueMapTy &VM,
                             RemapFlags Flags = RF_None,
                             ValueMapTypeRemapper *TypeMapper = nullptr,
                             ValueMaterializer *Materializer = nullptr) {
  ValueMapper(VM, Flags, TypeMapper, Materializer).remapInstruction(*I);
}

/// Remap the Values used in the DbgRecord \a DR using the value map \a
/// VM.
inline void RemapDbgRecord(Module *M, DbgRecord *DR, ValueToValueMapTy &VM,
                           RemapFlags Flags = RF_None,
                           ValueMapTypeRemapper *TypeMapper = nullptr,
                           ValueMaterializer *Materializer = nullptr) {
  ValueMapper(VM, Flags, TypeMapper, Materializer).remapDbgRecord(M, *DR);
}

/// Remap the Values used in the DbgRecords \a Range using the value map \a
/// VM.
inline void RemapDbgRecordRange(Module *M,
                                iterator_range<DbgRecordIterator> Range,
                                ValueToValueMapTy &VM,
                                RemapFlags Flags = RF_None,
                                ValueMapTypeRemapper *TypeMapper = nullptr,
                                ValueMaterializer *Materializer = nullptr) {
  ValueMapper(VM, Flags, TypeMapper, Materializer)
      .remapDbgRecordRange(M, Range);
}

/// Remap the operands, metadata, arguments, and instructions of a function.
///
/// Calls \a MapValue() on prefix data, prologue data, and personality
/// function; calls \a MapMetadata() on each attached MDNode; remaps the
/// argument types using the provided \c TypeMapper; and calls \a
/// RemapInstruction() on every instruction.
inline void RemapFunction(Function &F, ValueToValueMapTy &VM,
                          RemapFlags Flags = RF_None,
                          ValueMapTypeRemapper *TypeMapper = nullptr,
                          ValueMaterializer *Materializer = nullptr) {
  ValueMapper(VM, Flags, TypeMapper, Materializer).remapFunction(F);
}

/// Version of MapValue with type safety for Constant.
inline Constant *MapValue(const Constant *V, ValueToValueMapTy &VM,
                          RemapFlags Flags = RF_None,
                          ValueMapTypeRemapper *TypeMapper = nullptr,
                          ValueMaterializer *Materializer = nullptr) {
  return ValueMapper(VM, Flags, TypeMapper, Materializer).mapConstant(*V);
}

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_VALUEMAPPER_H
