//===- llvm/IR/Metadata.h - Metadata definitions ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// @file
/// This file contains the declarations for metadata subclasses.
/// They represent the different flavors of metadata that live in LLVM.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_METADATA_H
#define LLVM_IR_METADATA_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/ilist_node.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/CBindingWrapping.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace llvm {

class Module;
class ModuleSlotTracker;
class raw_ostream;
class DbgVariableRecord;
template <typename T> class StringMapEntry;
template <typename ValueTy> class StringMapEntryStorage;
class Type;

enum LLVMConstants : uint32_t {
  DEBUG_METADATA_VERSION = 3 // Current debug info version number.
};

/// Magic number in the value profile metadata showing a target has been
/// promoted for the instruction and shouldn't be promoted again.
const uint64_t NOMORE_ICP_MAGICNUM = -1;

/// Root of the metadata hierarchy.
///
/// This is a root class for typeless data in the IR.
class Metadata {
  friend class ReplaceableMetadataImpl;

  /// RTTI.
  const unsigned char SubclassID;

protected:
  /// Active type of storage.
  enum StorageType { Uniqued, Distinct, Temporary };

  /// Storage flag for non-uniqued, otherwise unowned, metadata.
  unsigned char Storage : 7;

  unsigned char SubclassData1 : 1;
  unsigned short SubclassData16 = 0;
  unsigned SubclassData32 = 0;

public:
  enum MetadataKind {
#define HANDLE_METADATA_LEAF(CLASS) CLASS##Kind,
#include "llvm/IR/Metadata.def"
  };

protected:
  Metadata(unsigned ID, StorageType Storage)
      : SubclassID(ID), Storage(Storage), SubclassData1(false) {
    static_assert(sizeof(*this) == 8, "Metadata fields poorly packed");
  }

  ~Metadata() = default;

  /// Default handling of a changed operand, which asserts.
  ///
  /// If subclasses pass themselves in as owners to a tracking node reference,
  /// they must provide an implementation of this method.
  void handleChangedOperand(void *, Metadata *) {
    llvm_unreachable("Unimplemented in Metadata subclass");
  }

public:
  unsigned getMetadataID() const { return SubclassID; }

  /// User-friendly dump.
  ///
  /// If \c M is provided, metadata nodes will be numbered canonically;
  /// otherwise, pointer addresses are substituted.
  ///
  /// Note: this uses an explicit overload instead of default arguments so that
  /// the nullptr version is easy to call from a debugger.
  ///
  /// @{
  void dump() const;
  void dump(const Module *M) const;
  /// @}

  /// Print.
  ///
  /// Prints definition of \c this.
  ///
  /// If \c M is provided, metadata nodes will be numbered canonically;
  /// otherwise, pointer addresses are substituted.
  /// @{
  void print(raw_ostream &OS, const Module *M = nullptr,
             bool IsForDebug = false) const;
  void print(raw_ostream &OS, ModuleSlotTracker &MST, const Module *M = nullptr,
             bool IsForDebug = false) const;
  /// @}

  /// Print as operand.
  ///
  /// Prints reference of \c this.
  ///
  /// If \c M is provided, metadata nodes will be numbered canonically;
  /// otherwise, pointer addresses are substituted.
  /// @{
  void printAsOperand(raw_ostream &OS, const Module *M = nullptr) const;
  void printAsOperand(raw_ostream &OS, ModuleSlotTracker &MST,
                      const Module *M = nullptr) const;
  /// @}
};

// Create wrappers for C Binding types (see CBindingWrapping.h).
DEFINE_ISA_CONVERSION_FUNCTIONS(Metadata, LLVMMetadataRef)

// Specialized opaque metadata conversions.
inline Metadata **unwrap(LLVMMetadataRef *MDs) {
  return reinterpret_cast<Metadata**>(MDs);
}

#define HANDLE_METADATA(CLASS) class CLASS;
#include "llvm/IR/Metadata.def"

// Provide specializations of isa so that we don't need definitions of
// subclasses to see if the metadata is a subclass.
#define HANDLE_METADATA_LEAF(CLASS)                                            \
  template <> struct isa_impl<CLASS, Metadata> {                               \
    static inline bool doit(const Metadata &MD) {                              \
      return MD.getMetadataID() == Metadata::CLASS##Kind;                      \
    }                                                                          \
  };
#include "llvm/IR/Metadata.def"

inline raw_ostream &operator<<(raw_ostream &OS, const Metadata &MD) {
  MD.print(OS);
  return OS;
}

/// Metadata wrapper in the Value hierarchy.
///
/// A member of the \a Value hierarchy to represent a reference to metadata.
/// This allows, e.g., intrinsics to have metadata as operands.
///
/// Notably, this is the only thing in either hierarchy that is allowed to
/// reference \a LocalAsMetadata.
class MetadataAsValue : public Value {
  friend class ReplaceableMetadataImpl;
  friend class LLVMContextImpl;

  Metadata *MD;

  MetadataAsValue(Type *Ty, Metadata *MD);

  /// Drop use of metadata (during teardown).
  void dropUse() { MD = nullptr; }

public:
  ~MetadataAsValue();

  static MetadataAsValue *get(LLVMContext &Context, Metadata *MD);
  static MetadataAsValue *getIfExists(LLVMContext &Context, Metadata *MD);

  Metadata *getMetadata() const { return MD; }

  static bool classof(const Value *V) {
    return V->getValueID() == MetadataAsValueVal;
  }

private:
  void handleChangedMetadata(Metadata *MD);
  void track();
  void untrack();
};

/// Base class for tracking ValueAsMetadata/DIArgLists with user lookups and
/// Owner callbacks outside of ValueAsMetadata.
///
/// Currently only inherited by DbgVariableRecord; if other classes need to use
/// it, then a SubclassID will need to be added (either as a new field or by
/// making DebugValue into a PointerIntUnion) to discriminate between the
/// subclasses in lookup and callback handling.
class DebugValueUser {
protected:
  // Capacity to store 3 debug values.
  // TODO: Not all DebugValueUser instances need all 3 elements, if we
  // restructure the DbgVariableRecord class then we can template parameterize
  // this array size.
  std::array<Metadata *, 3> DebugValues;

  ArrayRef<Metadata *> getDebugValues() const { return DebugValues; }

public:
  DbgVariableRecord *getUser();
  const DbgVariableRecord *getUser() const;
  /// To be called by ReplaceableMetadataImpl::replaceAllUsesWith, where `Old`
  /// is a pointer to one of the pointers in `DebugValues` (so should be type
  /// Metadata**), and `NewDebugValue` is the new Metadata* that is replacing
  /// *Old.
  /// For manually replacing elements of DebugValues,
  /// `resetDebugValue(Idx, NewDebugValue)` should be used instead.
  void handleChangedValue(void *Old, Metadata *NewDebugValue);
  DebugValueUser() = default;
  explicit DebugValueUser(std::array<Metadata *, 3> DebugValues)
      : DebugValues(DebugValues) {
    trackDebugValues();
  }
  DebugValueUser(DebugValueUser &&X) {
    DebugValues = X.DebugValues;
    retrackDebugValues(X);
  }
  DebugValueUser(const DebugValueUser &X) {
    DebugValues = X.DebugValues;
    trackDebugValues();
  }

  DebugValueUser &operator=(DebugValueUser &&X) {
    if (&X == this)
      return *this;

    untrackDebugValues();
    DebugValues = X.DebugValues;
    retrackDebugValues(X);
    return *this;
  }

  DebugValueUser &operator=(const DebugValueUser &X) {
    if (&X == this)
      return *this;

    untrackDebugValues();
    DebugValues = X.DebugValues;
    trackDebugValues();
    return *this;
  }

  ~DebugValueUser() { untrackDebugValues(); }

  void resetDebugValues() {
    untrackDebugValues();
    DebugValues.fill(nullptr);
  }

  void resetDebugValue(size_t Idx, Metadata *DebugValue) {
    assert(Idx < 3 && "Invalid debug value index.");
    untrackDebugValue(Idx);
    DebugValues[Idx] = DebugValue;
    trackDebugValue(Idx);
  }

  bool operator==(const DebugValueUser &X) const {
    return DebugValues == X.DebugValues;
  }
  bool operator!=(const DebugValueUser &X) const {
    return DebugValues != X.DebugValues;
  }

private:
  void trackDebugValue(size_t Idx);
  void trackDebugValues();

  void untrackDebugValue(size_t Idx);
  void untrackDebugValues();

  void retrackDebugValues(DebugValueUser &X);
};

/// API for tracking metadata references through RAUW and deletion.
///
/// Shared API for updating \a Metadata pointers in subclasses that support
/// RAUW.
///
/// This API is not meant to be used directly.  See \a TrackingMDRef for a
/// user-friendly tracking reference.
class MetadataTracking {
public:
  /// Track the reference to metadata.
  ///
  /// Register \c MD with \c *MD, if the subclass supports tracking.  If \c *MD
  /// gets RAUW'ed, \c MD will be updated to the new address.  If \c *MD gets
  /// deleted, \c MD will be set to \c nullptr.
  ///
  /// If tracking isn't supported, \c *MD will not change.
  ///
  /// \return true iff tracking is supported by \c MD.
  static bool track(Metadata *&MD) {
    return track(&MD, *MD, static_cast<Metadata *>(nullptr));
  }

  /// Track the reference to metadata for \a Metadata.
  ///
  /// As \a track(Metadata*&), but with support for calling back to \c Owner to
  /// tell it that its operand changed.  This could trigger \c Owner being
  /// re-uniqued.
  static bool track(void *Ref, Metadata &MD, Metadata &Owner) {
    return track(Ref, MD, &Owner);
  }

  /// Track the reference to metadata for \a MetadataAsValue.
  ///
  /// As \a track(Metadata*&), but with support for calling back to \c Owner to
  /// tell it that its operand changed.  This could trigger \c Owner being
  /// re-uniqued.
  static bool track(void *Ref, Metadata &MD, MetadataAsValue &Owner) {
    return track(Ref, MD, &Owner);
  }

  /// Track the reference to metadata for \a DebugValueUser.
  ///
  /// As \a track(Metadata*&), but with support for calling back to \c Owner to
  /// tell it that its operand changed.  This could trigger \c Owner being
  /// re-uniqued.
  static bool track(void *Ref, Metadata &MD, DebugValueUser &Owner) {
    return track(Ref, MD, &Owner);
  }

  /// Stop tracking a reference to metadata.
  ///
  /// Stops \c *MD from tracking \c MD.
  static void untrack(Metadata *&MD) { untrack(&MD, *MD); }
  static void untrack(void *Ref, Metadata &MD);

  /// Move tracking from one reference to another.
  ///
  /// Semantically equivalent to \c untrack(MD) followed by \c track(New),
  /// except that ownership callbacks are maintained.
  ///
  /// Note: it is an error if \c *MD does not equal \c New.
  ///
  /// \return true iff tracking is supported by \c MD.
  static bool retrack(Metadata *&MD, Metadata *&New) {
    return retrack(&MD, *MD, &New);
  }
  static bool retrack(void *Ref, Metadata &MD, void *New);

  /// Check whether metadata is replaceable.
  static bool isReplaceable(const Metadata &MD);

  using OwnerTy = PointerUnion<MetadataAsValue *, Metadata *, DebugValueUser *>;

private:
  /// Track a reference to metadata for an owner.
  ///
  /// Generalized version of tracking.
  static bool track(void *Ref, Metadata &MD, OwnerTy Owner);
};

/// Shared implementation of use-lists for replaceable metadata.
///
/// Most metadata cannot be RAUW'ed.  This is a shared implementation of
/// use-lists and associated API for the three that support it (
/// \a ValueAsMetadata, \a TempMDNode, and \a DIArgList).
class ReplaceableMetadataImpl {
  friend class MetadataTracking;

public:
  using OwnerTy = MetadataTracking::OwnerTy;

private:
  LLVMContext &Context;
  uint64_t NextIndex = 0;
  SmallDenseMap<void *, std::pair<OwnerTy, uint64_t>, 4> UseMap;

public:
  ReplaceableMetadataImpl(LLVMContext &Context) : Context(Context) {}

  ~ReplaceableMetadataImpl() {
    assert(UseMap.empty() && "Cannot destroy in-use replaceable metadata");
  }

  LLVMContext &getContext() const { return Context; }

  /// Replace all uses of this with MD.
  ///
  /// Replace all uses of this with \c MD, which is allowed to be null.
  void replaceAllUsesWith(Metadata *MD);
   /// Replace all uses of the constant with Undef in debug info metadata
  static void SalvageDebugInfo(const Constant &C); 
  /// Returns the list of all DIArgList users of this.
  SmallVector<Metadata *> getAllArgListUsers();
  /// Returns the list of all DbgVariableRecord users of this.
  SmallVector<DbgVariableRecord *> getAllDbgVariableRecordUsers();

  /// Resolve all uses of this.
  ///
  /// Resolve all uses of this, turning off RAUW permanently.  If \c
  /// ResolveUsers, call \a MDNode::resolve() on any users whose last operand
  /// is resolved.
  void resolveAllUses(bool ResolveUsers = true);

  unsigned getNumUses() const { return UseMap.size(); }

private:
  void addRef(void *Ref, OwnerTy Owner);
  void dropRef(void *Ref);
  void moveRef(void *Ref, void *New, const Metadata &MD);

  /// Lazily construct RAUW support on MD.
  ///
  /// If this is an unresolved MDNode, RAUW support will be created on-demand.
  /// ValueAsMetadata always has RAUW support.
  static ReplaceableMetadataImpl *getOrCreate(Metadata &MD);

  /// Get RAUW support on MD, if it exists.
  static ReplaceableMetadataImpl *getIfExists(Metadata &MD);

  /// Check whether this node will support RAUW.
  ///
  /// Returns \c true unless getOrCreate() would return null.
  static bool isReplaceable(const Metadata &MD);
};

/// Value wrapper in the Metadata hierarchy.
///
/// This is a custom value handle that allows other metadata to refer to
/// classes in the Value hierarchy.
///
/// Because of full uniquing support, each value is only wrapped by a single \a
/// ValueAsMetadata object, so the lookup maps are far more efficient than
/// those using ValueHandleBase.
class ValueAsMetadata : public Metadata, ReplaceableMetadataImpl {
  friend class ReplaceableMetadataImpl;
  friend class LLVMContextImpl;

  Value *V;

  /// Drop users without RAUW (during teardown).
  void dropUsers() {
    ReplaceableMetadataImpl::resolveAllUses(/* ResolveUsers */ false);
  }

protected:
  ValueAsMetadata(unsigned ID, Value *V)
      : Metadata(ID, Uniqued), ReplaceableMetadataImpl(V->getContext()), V(V) {
    assert(V && "Expected valid value");
  }

  ~ValueAsMetadata() = default;

public:
  static ValueAsMetadata *get(Value *V);

  static ConstantAsMetadata *getConstant(Value *C) {
    return cast<ConstantAsMetadata>(get(C));
  }

  static LocalAsMetadata *getLocal(Value *Local) {
    return cast<LocalAsMetadata>(get(Local));
  }

  static ValueAsMetadata *getIfExists(Value *V);

  static ConstantAsMetadata *getConstantIfExists(Value *C) {
    return cast_or_null<ConstantAsMetadata>(getIfExists(C));
  }

  static LocalAsMetadata *getLocalIfExists(Value *Local) {
    return cast_or_null<LocalAsMetadata>(getIfExists(Local));
  }

  Value *getValue() const { return V; }
  Type *getType() const { return V->getType(); }
  LLVMContext &getContext() const { return V->getContext(); }

  SmallVector<Metadata *> getAllArgListUsers() {
    return ReplaceableMetadataImpl::getAllArgListUsers();
  }
  SmallVector<DbgVariableRecord *> getAllDbgVariableRecordUsers() {
    return ReplaceableMetadataImpl::getAllDbgVariableRecordUsers();
  }

  static void handleDeletion(Value *V);
  static void handleRAUW(Value *From, Value *To);

protected:
  /// Handle collisions after \a Value::replaceAllUsesWith().
  ///
  /// RAUW isn't supported directly for \a ValueAsMetadata, but if the wrapped
  /// \a Value gets RAUW'ed and the target already exists, this is used to
  /// merge the two metadata nodes.
  void replaceAllUsesWith(Metadata *MD) {
    ReplaceableMetadataImpl::replaceAllUsesWith(MD);
  }

public:
  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == LocalAsMetadataKind ||
           MD->getMetadataID() == ConstantAsMetadataKind;
  }
};

class ConstantAsMetadata : public ValueAsMetadata {
  friend class ValueAsMetadata;

  ConstantAsMetadata(Constant *C)
      : ValueAsMetadata(ConstantAsMetadataKind, C) {}

public:
  static ConstantAsMetadata *get(Constant *C) {
    return ValueAsMetadata::getConstant(C);
  }

  static ConstantAsMetadata *getIfExists(Constant *C) {
    return ValueAsMetadata::getConstantIfExists(C);
  }

  Constant *getValue() const {
    return cast<Constant>(ValueAsMetadata::getValue());
  }

  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == ConstantAsMetadataKind;
  }
};

class LocalAsMetadata : public ValueAsMetadata {
  friend class ValueAsMetadata;

  LocalAsMetadata(Value *Local)
      : ValueAsMetadata(LocalAsMetadataKind, Local) {
    assert(!isa<Constant>(Local) && "Expected local value");
  }

public:
  static LocalAsMetadata *get(Value *Local) {
    return ValueAsMetadata::getLocal(Local);
  }

  static LocalAsMetadata *getIfExists(Value *Local) {
    return ValueAsMetadata::getLocalIfExists(Local);
  }

  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == LocalAsMetadataKind;
  }
};

/// Transitional API for extracting constants from Metadata.
///
/// This namespace contains transitional functions for metadata that points to
/// \a Constants.
///
/// In prehistory -- when metadata was a subclass of \a Value -- \a MDNode
/// operands could refer to any \a Value.  There's was a lot of code like this:
///
/// \code
///     MDNode *N = ...;
///     auto *CI = dyn_cast<ConstantInt>(N->getOperand(2));
/// \endcode
///
/// Now that \a Value and \a Metadata are in separate hierarchies, maintaining
/// the semantics for \a isa(), \a cast(), \a dyn_cast() (etc.) requires three
/// steps: cast in the \a Metadata hierarchy, extraction of the \a Value, and
/// cast in the \a Value hierarchy.  Besides creating boiler-plate, this
/// requires subtle control flow changes.
///
/// The end-goal is to create a new type of metadata, called (e.g.) \a MDInt,
/// so that metadata can refer to numbers without traversing a bridge to the \a
/// Value hierarchy.  In this final state, the code above would look like this:
///
/// \code
///     MDNode *N = ...;
///     auto *MI = dyn_cast<MDInt>(N->getOperand(2));
/// \endcode
///
/// The API in this namespace supports the transition.  \a MDInt doesn't exist
/// yet, and even once it does, changing each metadata schema to use it is its
/// own mini-project.  In the meantime this API prevents us from introducing
/// complex and bug-prone control flow that will disappear in the end.  In
/// particular, the above code looks like this:
///
/// \code
///     MDNode *N = ...;
///     auto *CI = mdconst::dyn_extract<ConstantInt>(N->getOperand(2));
/// \endcode
///
/// The full set of provided functions includes:
///
///   mdconst::hasa                <=> isa
///   mdconst::extract             <=> cast
///   mdconst::extract_or_null     <=> cast_or_null
///   mdconst::dyn_extract         <=> dyn_cast
///   mdconst::dyn_extract_or_null <=> dyn_cast_or_null
///
/// The target of the cast must be a subclass of \a Constant.
namespace mdconst {

namespace detail {

template <class T> T &make();
template <class T, class Result> struct HasDereference {
  using Yes = char[1];
  using No = char[2];
  template <size_t N> struct SFINAE {};

  template <class U, class V>
  static Yes &hasDereference(SFINAE<sizeof(static_cast<V>(*make<U>()))> * = 0);
  template <class U, class V> static No &hasDereference(...);

  static const bool value =
      sizeof(hasDereference<T, Result>(nullptr)) == sizeof(Yes);
};
template <class V, class M> struct IsValidPointer {
  static const bool value = std::is_base_of<Constant, V>::value &&
                            HasDereference<M, const Metadata &>::value;
};
template <class V, class M> struct IsValidReference {
  static const bool value = std::is_base_of<Constant, V>::value &&
                            std::is_convertible<M, const Metadata &>::value;
};

} // end namespace detail

/// Check whether Metadata has a Value.
///
/// As an analogue to \a isa(), check whether \c MD has an \a Value inside of
/// type \c X.
template <class X, class Y>
inline std::enable_if_t<detail::IsValidPointer<X, Y>::value, bool>
hasa(Y &&MD) {
  assert(MD && "Null pointer sent into hasa");
  if (auto *V = dyn_cast<ConstantAsMetadata>(MD))
    return isa<X>(V->getValue());
  return false;
}
template <class X, class Y>
inline std::enable_if_t<detail::IsValidReference<X, Y &>::value, bool>
hasa(Y &MD) {
  return hasa(&MD);
}

/// Extract a Value from Metadata.
///
/// As an analogue to \a cast(), extract the \a Value subclass \c X from \c MD.
template <class X, class Y>
inline std::enable_if_t<detail::IsValidPointer<X, Y>::value, X *>
extract(Y &&MD) {
  return cast<X>(cast<ConstantAsMetadata>(MD)->getValue());
}
template <class X, class Y>
inline std::enable_if_t<detail::IsValidReference<X, Y &>::value, X *>
extract(Y &MD) {
  return extract(&MD);
}

/// Extract a Value from Metadata, allowing null.
///
/// As an analogue to \a cast_or_null(), extract the \a Value subclass \c X
/// from \c MD, allowing \c MD to be null.
template <class X, class Y>
inline std::enable_if_t<detail::IsValidPointer<X, Y>::value, X *>
extract_or_null(Y &&MD) {
  if (auto *V = cast_or_null<ConstantAsMetadata>(MD))
    return cast<X>(V->getValue());
  return nullptr;
}

/// Extract a Value from Metadata, if any.
///
/// As an analogue to \a dyn_cast_or_null(), extract the \a Value subclass \c X
/// from \c MD, return null if \c MD doesn't contain a \a Value or if the \a
/// Value it does contain is of the wrong subclass.
template <class X, class Y>
inline std::enable_if_t<detail::IsValidPointer<X, Y>::value, X *>
dyn_extract(Y &&MD) {
  if (auto *V = dyn_cast<ConstantAsMetadata>(MD))
    return dyn_cast<X>(V->getValue());
  return nullptr;
}

/// Extract a Value from Metadata, if any, allowing null.
///
/// As an analogue to \a dyn_cast_or_null(), extract the \a Value subclass \c X
/// from \c MD, return null if \c MD doesn't contain a \a Value or if the \a
/// Value it does contain is of the wrong subclass, allowing \c MD to be null.
template <class X, class Y>
inline std::enable_if_t<detail::IsValidPointer<X, Y>::value, X *>
dyn_extract_or_null(Y &&MD) {
  if (auto *V = dyn_cast_or_null<ConstantAsMetadata>(MD))
    return dyn_cast<X>(V->getValue());
  return nullptr;
}

} // end namespace mdconst

//===----------------------------------------------------------------------===//
/// A single uniqued string.
///
/// These are used to efficiently contain a byte sequence for metadata.
/// MDString is always unnamed.
class MDString : public Metadata {
  friend class StringMapEntryStorage<MDString>;

  StringMapEntry<MDString> *Entry = nullptr;

  MDString() : Metadata(MDStringKind, Uniqued) {}

public:
  MDString(const MDString &) = delete;
  MDString &operator=(MDString &&) = delete;
  MDString &operator=(const MDString &) = delete;

  static MDString *get(LLVMContext &Context, StringRef Str);
  static MDString *get(LLVMContext &Context, const char *Str) {
    return get(Context, Str ? StringRef(Str) : StringRef());
  }

  StringRef getString() const;

  unsigned getLength() const { return (unsigned)getString().size(); }

  using iterator = StringRef::iterator;

  /// Pointer to the first byte of the string.
  iterator begin() const { return getString().begin(); }

  /// Pointer to one byte past the end of the string.
  iterator end() const { return getString().end(); }

  const unsigned char *bytes_begin() const { return getString().bytes_begin(); }
  const unsigned char *bytes_end() const { return getString().bytes_end(); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == MDStringKind;
  }
};

/// A collection of metadata nodes that might be associated with a
/// memory access used by the alias-analysis infrastructure.
struct AAMDNodes {
  explicit AAMDNodes() = default;
  explicit AAMDNodes(MDNode *T, MDNode *TS, MDNode *S, MDNode *N)
      : TBAA(T), TBAAStruct(TS), Scope(S), NoAlias(N) {}

  bool operator==(const AAMDNodes &A) const {
    return TBAA == A.TBAA && TBAAStruct == A.TBAAStruct && Scope == A.Scope &&
           NoAlias == A.NoAlias;
  }

  bool operator!=(const AAMDNodes &A) const { return !(*this == A); }

  explicit operator bool() const {
    return TBAA || TBAAStruct || Scope || NoAlias;
  }

  /// The tag for type-based alias analysis.
  MDNode *TBAA = nullptr;

  /// The tag for type-based alias analysis (tbaa struct).
  MDNode *TBAAStruct = nullptr;

  /// The tag for alias scope specification (used with noalias).
  MDNode *Scope = nullptr;

  /// The tag specifying the noalias scope.
  MDNode *NoAlias = nullptr;

  // Shift tbaa Metadata node to start off bytes later
  static MDNode *shiftTBAA(MDNode *M, size_t off);

  // Shift tbaa.struct Metadata node to start off bytes later
  static MDNode *shiftTBAAStruct(MDNode *M, size_t off);

  // Extend tbaa Metadata node to apply to a series of bytes of length len.
  // A size of -1 denotes an unknown size.
  static MDNode *extendToTBAA(MDNode *TBAA, ssize_t len);

  /// Given two sets of AAMDNodes that apply to the same pointer,
  /// give the best AAMDNodes that are compatible with both (i.e. a set of
  /// nodes whose allowable aliasing conclusions are a subset of those
  /// allowable by both of the inputs). However, for efficiency
  /// reasons, do not create any new MDNodes.
  AAMDNodes intersect(const AAMDNodes &Other) const {
    AAMDNodes Result;
    Result.TBAA = Other.TBAA == TBAA ? TBAA : nullptr;
    Result.TBAAStruct = Other.TBAAStruct == TBAAStruct ? TBAAStruct : nullptr;
    Result.Scope = Other.Scope == Scope ? Scope : nullptr;
    Result.NoAlias = Other.NoAlias == NoAlias ? NoAlias : nullptr;
    return Result;
  }

  /// Create a new AAMDNode that describes this AAMDNode after applying a
  /// constant offset to the start of the pointer.
  AAMDNodes shift(size_t Offset) const {
    AAMDNodes Result;
    Result.TBAA = TBAA ? shiftTBAA(TBAA, Offset) : nullptr;
    Result.TBAAStruct =
        TBAAStruct ? shiftTBAAStruct(TBAAStruct, Offset) : nullptr;
    Result.Scope = Scope;
    Result.NoAlias = NoAlias;
    return Result;
  }

  /// Create a new AAMDNode that describes this AAMDNode after extending it to
  /// apply to a series of bytes of length Len. A size of -1 denotes an unknown
  /// size.
  AAMDNodes extendTo(ssize_t Len) const {
    AAMDNodes Result;
    Result.TBAA = TBAA ? extendToTBAA(TBAA, Len) : nullptr;
    // tbaa.struct contains (offset, size, type) triples. Extending the length
    // of the tbaa.struct doesn't require changing this (though more information
    // could be provided by adding more triples at subsequent lengths).
    Result.TBAAStruct = TBAAStruct;
    Result.Scope = Scope;
    Result.NoAlias = NoAlias;
    return Result;
  }

  /// Given two sets of AAMDNodes applying to potentially different locations,
  /// determine the best AAMDNodes that apply to both.
  AAMDNodes merge(const AAMDNodes &Other) const;

  /// Determine the best AAMDNodes after concatenating two different locations
  /// together. Different from `merge`, where different locations should
  /// overlap each other, `concat` puts non-overlapping locations together.
  AAMDNodes concat(const AAMDNodes &Other) const;

  /// Create a new AAMDNode for accessing \p AccessSize bytes of this AAMDNode.
  /// If this AAMDNode has !tbaa.struct and \p AccessSize matches the size of
  /// the field at offset 0, get the TBAA tag describing the accessed field.
  /// If such an AAMDNode already embeds !tbaa, the existing one is retrieved.
  /// Finally, !tbaa.struct is zeroed out.
  AAMDNodes adjustForAccess(unsigned AccessSize);
  AAMDNodes adjustForAccess(size_t Offset, Type *AccessTy,
                            const DataLayout &DL);
  AAMDNodes adjustForAccess(size_t Offset, unsigned AccessSize);
};

// Specialize DenseMapInfo for AAMDNodes.
template<>
struct DenseMapInfo<AAMDNodes> {
  static inline AAMDNodes getEmptyKey() {
    return AAMDNodes(DenseMapInfo<MDNode *>::getEmptyKey(),
                     nullptr, nullptr, nullptr);
  }

  static inline AAMDNodes getTombstoneKey() {
    return AAMDNodes(DenseMapInfo<MDNode *>::getTombstoneKey(),
                     nullptr, nullptr, nullptr);
  }

  static unsigned getHashValue(const AAMDNodes &Val) {
    return DenseMapInfo<MDNode *>::getHashValue(Val.TBAA) ^
           DenseMapInfo<MDNode *>::getHashValue(Val.TBAAStruct) ^
           DenseMapInfo<MDNode *>::getHashValue(Val.Scope) ^
           DenseMapInfo<MDNode *>::getHashValue(Val.NoAlias);
  }

  static bool isEqual(const AAMDNodes &LHS, const AAMDNodes &RHS) {
    return LHS == RHS;
  }
};

/// Tracking metadata reference owned by Metadata.
///
/// Similar to \a TrackingMDRef, but it's expected to be owned by an instance
/// of \a Metadata, which has the option of registering itself for callbacks to
/// re-unique itself.
///
/// In particular, this is used by \a MDNode.
class MDOperand {
  Metadata *MD = nullptr;

public:
  MDOperand() = default;
  MDOperand(const MDOperand &) = delete;
  MDOperand(MDOperand &&Op) {
    MD = Op.MD;
    if (MD)
      (void)MetadataTracking::retrack(Op.MD, MD);
    Op.MD = nullptr;
  }
  MDOperand &operator=(const MDOperand &) = delete;
  MDOperand &operator=(MDOperand &&Op) {
    MD = Op.MD;
    if (MD)
      (void)MetadataTracking::retrack(Op.MD, MD);
    Op.MD = nullptr;
    return *this;
  }

  // Check if MDOperand is of type MDString and equals `Str`.
  bool equalsStr(StringRef Str) const {
    return isa<MDString>(this->get()) &&
           cast<MDString>(this->get())->getString() == Str;
  }

  ~MDOperand() { untrack(); }

  Metadata *get() const { return MD; }
  operator Metadata *() const { return get(); }
  Metadata *operator->() const { return get(); }
  Metadata &operator*() const { return *get(); }

  void reset() {
    untrack();
    MD = nullptr;
  }
  void reset(Metadata *MD, Metadata *Owner) {
    untrack();
    this->MD = MD;
    track(Owner);
  }

private:
  void track(Metadata *Owner) {
    if (MD) {
      if (Owner)
        MetadataTracking::track(this, *MD, *Owner);
      else
        MetadataTracking::track(MD);
    }
  }

  void untrack() {
    assert(static_cast<void *>(this) == &MD && "Expected same address");
    if (MD)
      MetadataTracking::untrack(MD);
  }
};

template <> struct simplify_type<MDOperand> {
  using SimpleType = Metadata *;

  static SimpleType getSimplifiedValue(MDOperand &MD) { return MD.get(); }
};

template <> struct simplify_type<const MDOperand> {
  using SimpleType = Metadata *;

  static SimpleType getSimplifiedValue(const MDOperand &MD) { return MD.get(); }
};

/// Pointer to the context, with optional RAUW support.
///
/// Either a raw (non-null) pointer to the \a LLVMContext, or an owned pointer
/// to \a ReplaceableMetadataImpl (which has a reference to \a LLVMContext).
class ContextAndReplaceableUses {
  PointerUnion<LLVMContext *, ReplaceableMetadataImpl *> Ptr;

public:
  ContextAndReplaceableUses(LLVMContext &Context) : Ptr(&Context) {}
  ContextAndReplaceableUses(
      std::unique_ptr<ReplaceableMetadataImpl> ReplaceableUses)
      : Ptr(ReplaceableUses.release()) {
    assert(getReplaceableUses() && "Expected non-null replaceable uses");
  }
  ContextAndReplaceableUses() = delete;
  ContextAndReplaceableUses(ContextAndReplaceableUses &&) = delete;
  ContextAndReplaceableUses(const ContextAndReplaceableUses &) = delete;
  ContextAndReplaceableUses &operator=(ContextAndReplaceableUses &&) = delete;
  ContextAndReplaceableUses &
  operator=(const ContextAndReplaceableUses &) = delete;
  ~ContextAndReplaceableUses() { delete getReplaceableUses(); }

  operator LLVMContext &() { return getContext(); }

  /// Whether this contains RAUW support.
  bool hasReplaceableUses() const {
    return isa<ReplaceableMetadataImpl *>(Ptr);
  }

  LLVMContext &getContext() const {
    if (hasReplaceableUses())
      return getReplaceableUses()->getContext();
    return *cast<LLVMContext *>(Ptr);
  }

  ReplaceableMetadataImpl *getReplaceableUses() const {
    if (hasReplaceableUses())
      return cast<ReplaceableMetadataImpl *>(Ptr);
    return nullptr;
  }

  /// Ensure that this has RAUW support, and then return it.
  ReplaceableMetadataImpl *getOrCreateReplaceableUses() {
    if (!hasReplaceableUses())
      makeReplaceable(std::make_unique<ReplaceableMetadataImpl>(getContext()));
    return getReplaceableUses();
  }

  /// Assign RAUW support to this.
  ///
  /// Make this replaceable, taking ownership of \c ReplaceableUses (which must
  /// not be null).
  void
  makeReplaceable(std::unique_ptr<ReplaceableMetadataImpl> ReplaceableUses) {
    assert(ReplaceableUses && "Expected non-null replaceable uses");
    assert(&ReplaceableUses->getContext() == &getContext() &&
           "Expected same context");
    delete getReplaceableUses();
    Ptr = ReplaceableUses.release();
  }

  /// Drop RAUW support.
  ///
  /// Cede ownership of RAUW support, returning it.
  std::unique_ptr<ReplaceableMetadataImpl> takeReplaceableUses() {
    assert(hasReplaceableUses() && "Expected to own replaceable uses");
    std::unique_ptr<ReplaceableMetadataImpl> ReplaceableUses(
        getReplaceableUses());
    Ptr = &ReplaceableUses->getContext();
    return ReplaceableUses;
  }
};

struct TempMDNodeDeleter {
  inline void operator()(MDNode *Node) const;
};

#define HANDLE_MDNODE_LEAF(CLASS)                                              \
  using Temp##CLASS = std::unique_ptr<CLASS, TempMDNodeDeleter>;
#define HANDLE_MDNODE_BRANCH(CLASS) HANDLE_MDNODE_LEAF(CLASS)
#include "llvm/IR/Metadata.def"

/// Metadata node.
///
/// Metadata nodes can be uniqued, like constants, or distinct.  Temporary
/// metadata nodes (with full support for RAUW) can be used to delay uniquing
/// until forward references are known.  The basic metadata node is an \a
/// MDTuple.
///
/// There is limited support for RAUW at construction time.  At construction
/// time, if any operand is a temporary node (or an unresolved uniqued node,
/// which indicates a transitive temporary operand), the node itself will be
/// unresolved.  As soon as all operands become resolved, it will drop RAUW
/// support permanently.
///
/// If an unresolved node is part of a cycle, \a resolveCycles() needs
/// to be called on some member of the cycle once all temporary nodes have been
/// replaced.
///
/// MDNodes can be large or small, as well as resizable or non-resizable.
/// Large MDNodes' operands are allocated in a separate storage vector,
/// whereas small MDNodes' operands are co-allocated. Distinct and temporary
/// MDnodes are resizable, but only MDTuples support this capability.
///
/// Clients can add operands to resizable MDNodes using push_back().
class MDNode : public Metadata {
  friend class ReplaceableMetadataImpl;
  friend class LLVMContextImpl;
  friend class DIAssignID;

  /// The header that is coallocated with an MDNode along with its "small"
  /// operands. It is located immediately before the main body of the node.
  /// The operands are in turn located immediately before the header.
  /// For resizable MDNodes, the space for the storage vector is also allocated
  /// immediately before the header, overlapping with the operands.
  /// Explicity set alignment because bitfields by default have an
  /// alignment of 1 on z/OS.
  struct alignas(alignof(size_t)) Header {
    bool IsResizable : 1;
    bool IsLarge : 1;
    size_t SmallSize : 4;
    size_t SmallNumOps : 4;
    size_t : sizeof(size_t) * CHAR_BIT - 10;

    unsigned NumUnresolved = 0;
    using LargeStorageVector = SmallVector<MDOperand, 0>;

    static constexpr size_t NumOpsFitInVector =
        sizeof(LargeStorageVector) / sizeof(MDOperand);
    static_assert(
        NumOpsFitInVector * sizeof(MDOperand) == sizeof(LargeStorageVector),
        "sizeof(LargeStorageVector) must be a multiple of sizeof(MDOperand)");

    static constexpr size_t MaxSmallSize = 15;

    static constexpr size_t getOpSize(unsigned NumOps) {
      return sizeof(MDOperand) * NumOps;
    }
    /// Returns the number of operands the node has space for based on its
    /// allocation characteristics.
    static size_t getSmallSize(size_t NumOps, bool IsResizable, bool IsLarge) {
      return IsLarge ? NumOpsFitInVector
                     : std::max(NumOps, NumOpsFitInVector * IsResizable);
    }
    /// Returns the number of bytes allocated for operands and header.
    static size_t getAllocSize(StorageType Storage, size_t NumOps) {
      return getOpSize(
                 getSmallSize(NumOps, isResizable(Storage), isLarge(NumOps))) +
             sizeof(Header);
    }

    /// Only temporary and distinct nodes are resizable.
    static bool isResizable(StorageType Storage) { return Storage != Uniqued; }
    static bool isLarge(size_t NumOps) { return NumOps > MaxSmallSize; }

    size_t getAllocSize() const {
      return getOpSize(SmallSize) + sizeof(Header);
    }
    void *getAllocation() {
      return reinterpret_cast<char *>(this + 1) -
             alignTo(getAllocSize(), alignof(uint64_t));
    }

    void *getLargePtr() const {
      static_assert(alignof(LargeStorageVector) <= alignof(Header),
                    "LargeStorageVector too strongly aligned");
      return reinterpret_cast<char *>(const_cast<Header *>(this)) -
             sizeof(LargeStorageVector);
    }

    void *getSmallPtr();

    LargeStorageVector &getLarge() {
      assert(IsLarge);
      return *reinterpret_cast<LargeStorageVector *>(getLargePtr());
    }

    const LargeStorageVector &getLarge() const {
      assert(IsLarge);
      return *reinterpret_cast<const LargeStorageVector *>(getLargePtr());
    }

    void resizeSmall(size_t NumOps);
    void resizeSmallToLarge(size_t NumOps);
    void resize(size_t NumOps);

    explicit Header(size_t NumOps, StorageType Storage);
    ~Header();

    MutableArrayRef<MDOperand> operands() {
      if (IsLarge)
        return getLarge();
      return MutableArrayRef(
          reinterpret_cast<MDOperand *>(this) - SmallSize, SmallNumOps);
    }

    ArrayRef<MDOperand> operands() const {
      if (IsLarge)
        return getLarge();
      return ArrayRef(reinterpret_cast<const MDOperand *>(this) - SmallSize,
                      SmallNumOps);
    }

    unsigned getNumOperands() const {
      if (!IsLarge)
        return SmallNumOps;
      return getLarge().size();
    }
  };

  Header &getHeader() { return *(reinterpret_cast<Header *>(this) - 1); }

  const Header &getHeader() const {
    return *(reinterpret_cast<const Header *>(this) - 1);
  }

  ContextAndReplaceableUses Context;

protected:
  MDNode(LLVMContext &Context, unsigned ID, StorageType Storage,
         ArrayRef<Metadata *> Ops1, ArrayRef<Metadata *> Ops2 = std::nullopt);
  ~MDNode() = default;

  void *operator new(size_t Size, size_t NumOps, StorageType Storage);
  void operator delete(void *Mem);

  /// Required by std, but never called.
  void operator delete(void *, unsigned) {
    llvm_unreachable("Constructor throws?");
  }

  /// Required by std, but never called.
  void operator delete(void *, unsigned, bool) {
    llvm_unreachable("Constructor throws?");
  }

  void dropAllReferences();

  MDOperand *mutable_begin() { return getHeader().operands().begin(); }
  MDOperand *mutable_end() { return getHeader().operands().end(); }

  using mutable_op_range = iterator_range<MDOperand *>;

  mutable_op_range mutable_operands() {
    return mutable_op_range(mutable_begin(), mutable_end());
  }

public:
  MDNode(const MDNode &) = delete;
  void operator=(const MDNode &) = delete;
  void *operator new(size_t) = delete;

  static inline MDTuple *get(LLVMContext &Context, ArrayRef<Metadata *> MDs);
  static inline MDTuple *getIfExists(LLVMContext &Context,
                                     ArrayRef<Metadata *> MDs);
  static inline MDTuple *getDistinct(LLVMContext &Context,
                                     ArrayRef<Metadata *> MDs);
  static inline TempMDTuple getTemporary(LLVMContext &Context,
                                         ArrayRef<Metadata *> MDs);

  /// Create a (temporary) clone of this.
  TempMDNode clone() const;

  /// Deallocate a node created by getTemporary.
  ///
  /// Calls \c replaceAllUsesWith(nullptr) before deleting, so any remaining
  /// references will be reset.
  static void deleteTemporary(MDNode *N);

  LLVMContext &getContext() const { return Context.getContext(); }

  /// Replace a specific operand.
  void replaceOperandWith(unsigned I, Metadata *New);

  /// Check if node is fully resolved.
  ///
  /// If \a isTemporary(), this always returns \c false; if \a isDistinct(),
  /// this always returns \c true.
  ///
  /// If \a isUniqued(), returns \c true if this has already dropped RAUW
  /// support (because all operands are resolved).
  ///
  /// As forward declarations are resolved, their containers should get
  /// resolved automatically.  However, if this (or one of its operands) is
  /// involved in a cycle, \a resolveCycles() needs to be called explicitly.
  bool isResolved() const { return !isTemporary() && !getNumUnresolved(); }

  bool isUniqued() const { return Storage == Uniqued; }
  bool isDistinct() const { return Storage == Distinct; }
  bool isTemporary() const { return Storage == Temporary; }

  bool isReplaceable() const { return isTemporary() || isAlwaysReplaceable(); }
  bool isAlwaysReplaceable() const { return getMetadataID() == DIAssignIDKind; }

  unsigned getNumTemporaryUses() const {
    assert(isTemporary() && "Only for temporaries");
    return Context.getReplaceableUses()->getNumUses();
  }

  /// RAUW a temporary.
  ///
  /// \pre \a isTemporary() must be \c true.
  void replaceAllUsesWith(Metadata *MD) {
    assert(isReplaceable() && "Expected temporary/replaceable node");
    if (Context.hasReplaceableUses())
      Context.getReplaceableUses()->replaceAllUsesWith(MD);
  }

  /// Resolve cycles.
  ///
  /// Once all forward declarations have been resolved, force cycles to be
  /// resolved.
  ///
  /// \pre No operands (or operands' operands, etc.) have \a isTemporary().
  void resolveCycles();

  /// Resolve a unique, unresolved node.
  void resolve();

  /// Replace a temporary node with a permanent one.
  ///
  /// Try to create a uniqued version of \c N -- in place, if possible -- and
  /// return it.  If \c N cannot be uniqued, return a distinct node instead.
  template <class T>
  static std::enable_if_t<std::is_base_of<MDNode, T>::value, T *>
  replaceWithPermanent(std::unique_ptr<T, TempMDNodeDeleter> N) {
    return cast<T>(N.release()->replaceWithPermanentImpl());
  }

  /// Replace a temporary node with a uniqued one.
  ///
  /// Create a uniqued version of \c N -- in place, if possible -- and return
  /// it.  Takes ownership of the temporary node.
  ///
  /// \pre N does not self-reference.
  template <class T>
  static std::enable_if_t<std::is_base_of<MDNode, T>::value, T *>
  replaceWithUniqued(std::unique_ptr<T, TempMDNodeDeleter> N) {
    return cast<T>(N.release()->replaceWithUniquedImpl());
  }

  /// Replace a temporary node with a distinct one.
  ///
  /// Create a distinct version of \c N -- in place, if possible -- and return
  /// it.  Takes ownership of the temporary node.
  template <class T>
  static std::enable_if_t<std::is_base_of<MDNode, T>::value, T *>
  replaceWithDistinct(std::unique_ptr<T, TempMDNodeDeleter> N) {
    return cast<T>(N.release()->replaceWithDistinctImpl());
  }

  /// Print in tree shape.
  ///
  /// Prints definition of \c this in tree shape.
  ///
  /// If \c M is provided, metadata nodes will be numbered canonically;
  /// otherwise, pointer addresses are substituted.
  /// @{
  void printTree(raw_ostream &OS, const Module *M = nullptr) const;
  void printTree(raw_ostream &OS, ModuleSlotTracker &MST,
                 const Module *M = nullptr) const;
  /// @}

  /// User-friendly dump in tree shape.
  ///
  /// If \c M is provided, metadata nodes will be numbered canonically;
  /// otherwise, pointer addresses are substituted.
  ///
  /// Note: this uses an explicit overload instead of default arguments so that
  /// the nullptr version is easy to call from a debugger.
  ///
  /// @{
  void dumpTree() const;
  void dumpTree(const Module *M) const;
  /// @}

private:
  MDNode *replaceWithPermanentImpl();
  MDNode *replaceWithUniquedImpl();
  MDNode *replaceWithDistinctImpl();

protected:
  /// Set an operand.
  ///
  /// Sets the operand directly, without worrying about uniquing.
  void setOperand(unsigned I, Metadata *New);

  unsigned getNumUnresolved() const { return getHeader().NumUnresolved; }

  void setNumUnresolved(unsigned N) { getHeader().NumUnresolved = N; }
  void storeDistinctInContext();
  template <class T, class StoreT>
  static T *storeImpl(T *N, StorageType Storage, StoreT &Store);
  template <class T> static T *storeImpl(T *N, StorageType Storage);

  /// Resize the node to hold \a NumOps operands.
  ///
  /// \pre \a isTemporary() or \a isDistinct()
  /// \pre MetadataID == MDTupleKind
  void resize(size_t NumOps) {
    assert(!isUniqued() && "Resizing is not supported for uniqued nodes");
    assert(getMetadataID() == MDTupleKind &&
           "Resizing is not supported for this node kind");
    getHeader().resize(NumOps);
  }

private:
  void handleChangedOperand(void *Ref, Metadata *New);

  /// Drop RAUW support, if any.
  void dropReplaceableUses();

  void resolveAfterOperandChange(Metadata *Old, Metadata *New);
  void decrementUnresolvedOperandCount();
  void countUnresolvedOperands();

  /// Mutate this to be "uniqued".
  ///
  /// Mutate this so that \a isUniqued().
  /// \pre \a isTemporary().
  /// \pre already added to uniquing set.
  void makeUniqued();

  /// Mutate this to be "distinct".
  ///
  /// Mutate this so that \a isDistinct().
  /// \pre \a isTemporary().
  void makeDistinct();

  void deleteAsSubclass();
  MDNode *uniquify();
  void eraseFromStore();

  template <class NodeTy> struct HasCachedHash;
  template <class NodeTy>
  static void dispatchRecalculateHash(NodeTy *N, std::true_type) {
    N->recalculateHash();
  }
  template <class NodeTy>
  static void dispatchRecalculateHash(NodeTy *, std::false_type) {}
  template <class NodeTy>
  static void dispatchResetHash(NodeTy *N, std::true_type) {
    N->setHash(0);
  }
  template <class NodeTy>
  static void dispatchResetHash(NodeTy *, std::false_type) {}

  /// Merge branch weights from two direct callsites.
  static MDNode *mergeDirectCallProfMetadata(MDNode *A, MDNode *B,
                                             const Instruction *AInstr,
                                             const Instruction *BInstr);

public:
  using op_iterator = const MDOperand *;
  using op_range = iterator_range<op_iterator>;

  op_iterator op_begin() const {
    return const_cast<MDNode *>(this)->mutable_begin();
  }

  op_iterator op_end() const {
    return const_cast<MDNode *>(this)->mutable_end();
  }

  ArrayRef<MDOperand> operands() const { return getHeader().operands(); }

  const MDOperand &getOperand(unsigned I) const {
    assert(I < getNumOperands() && "Out of range");
    return getHeader().operands()[I];
  }

  /// Return number of MDNode operands.
  unsigned getNumOperands() const { return getHeader().getNumOperands(); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Metadata *MD) {
    switch (MD->getMetadataID()) {
    default:
      return false;
#define HANDLE_MDNODE_LEAF(CLASS)                                              \
  case CLASS##Kind:                                                            \
    return true;
#include "llvm/IR/Metadata.def"
    }
  }

  /// Check whether MDNode is a vtable access.
  bool isTBAAVtableAccess() const;

  /// Methods for metadata merging.
  static MDNode *concatenate(MDNode *A, MDNode *B);
  static MDNode *intersect(MDNode *A, MDNode *B);
  static MDNode *getMostGenericTBAA(MDNode *A, MDNode *B);
  static MDNode *getMostGenericFPMath(MDNode *A, MDNode *B);
  static MDNode *getMostGenericRange(MDNode *A, MDNode *B);
  static MDNode *getMostGenericAliasScope(MDNode *A, MDNode *B);
  static MDNode *getMostGenericAlignmentOrDereferenceable(MDNode *A, MDNode *B);
  /// Merge !prof metadata from two instructions.
  /// Currently only implemented with direct callsites with branch weights.
  static MDNode *getMergedProfMetadata(MDNode *A, MDNode *B,
                                       const Instruction *AInstr,
                                       const Instruction *BInstr);
};

/// Tuple of metadata.
///
/// This is the simple \a MDNode arbitrary tuple.  Nodes are uniqued by
/// default based on their operands.
class MDTuple : public MDNode {
  friend class LLVMContextImpl;
  friend class MDNode;

  MDTuple(LLVMContext &C, StorageType Storage, unsigned Hash,
          ArrayRef<Metadata *> Vals)
      : MDNode(C, MDTupleKind, Storage, Vals) {
    setHash(Hash);
  }

  ~MDTuple() { dropAllReferences(); }

  void setHash(unsigned Hash) { SubclassData32 = Hash; }
  void recalculateHash();

  static MDTuple *getImpl(LLVMContext &Context, ArrayRef<Metadata *> MDs,
                          StorageType Storage, bool ShouldCreate = true);

  TempMDTuple cloneImpl() const {
    ArrayRef<MDOperand> Operands = operands();
    return getTemporary(getContext(), SmallVector<Metadata *, 4>(
                                          Operands.begin(), Operands.end()));
  }

public:
  /// Get the hash, if any.
  unsigned getHash() const { return SubclassData32; }

  static MDTuple *get(LLVMContext &Context, ArrayRef<Metadata *> MDs) {
    return getImpl(Context, MDs, Uniqued);
  }

  static MDTuple *getIfExists(LLVMContext &Context, ArrayRef<Metadata *> MDs) {
    return getImpl(Context, MDs, Uniqued, /* ShouldCreate */ false);
  }

  /// Return a distinct node.
  ///
  /// Return a distinct node -- i.e., a node that is not uniqued.
  static MDTuple *getDistinct(LLVMContext &Context, ArrayRef<Metadata *> MDs) {
    return getImpl(Context, MDs, Distinct);
  }

  /// Return a temporary node.
  ///
  /// For use in constructing cyclic MDNode structures. A temporary MDNode is
  /// not uniqued, may be RAUW'd, and must be manually deleted with
  /// deleteTemporary.
  static TempMDTuple getTemporary(LLVMContext &Context,
                                  ArrayRef<Metadata *> MDs) {
    return TempMDTuple(getImpl(Context, MDs, Temporary));
  }

  /// Return a (temporary) clone of this.
  TempMDTuple clone() const { return cloneImpl(); }

  /// Append an element to the tuple. This will resize the node.
  void push_back(Metadata *MD) {
    size_t NumOps = getNumOperands();
    resize(NumOps + 1);
    setOperand(NumOps, MD);
  }

  /// Shrink the operands by 1.
  void pop_back() { resize(getNumOperands() - 1); }

  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == MDTupleKind;
  }
};

MDTuple *MDNode::get(LLVMContext &Context, ArrayRef<Metadata *> MDs) {
  return MDTuple::get(Context, MDs);
}

MDTuple *MDNode::getIfExists(LLVMContext &Context, ArrayRef<Metadata *> MDs) {
  return MDTuple::getIfExists(Context, MDs);
}

MDTuple *MDNode::getDistinct(LLVMContext &Context, ArrayRef<Metadata *> MDs) {
  return MDTuple::getDistinct(Context, MDs);
}

TempMDTuple MDNode::getTemporary(LLVMContext &Context,
                                 ArrayRef<Metadata *> MDs) {
  return MDTuple::getTemporary(Context, MDs);
}

void TempMDNodeDeleter::operator()(MDNode *Node) const {
  MDNode::deleteTemporary(Node);
}

/// This is a simple wrapper around an MDNode which provides a higher-level
/// interface by hiding the details of how alias analysis information is encoded
/// in its operands.
class AliasScopeNode {
  const MDNode *Node = nullptr;

public:
  AliasScopeNode() = default;
  explicit AliasScopeNode(const MDNode *N) : Node(N) {}

  /// Get the MDNode for this AliasScopeNode.
  const MDNode *getNode() const { return Node; }

  /// Get the MDNode for this AliasScopeNode's domain.
  const MDNode *getDomain() const {
    if (Node->getNumOperands() < 2)
      return nullptr;
    return dyn_cast_or_null<MDNode>(Node->getOperand(1));
  }
  StringRef getName() const {
    if (Node->getNumOperands() > 2)
      if (MDString *N = dyn_cast_or_null<MDString>(Node->getOperand(2)))
        return N->getString();
    return StringRef();
  }
};

/// Typed iterator through MDNode operands.
///
/// An iterator that transforms an \a MDNode::iterator into an iterator over a
/// particular Metadata subclass.
template <class T> class TypedMDOperandIterator {
  MDNode::op_iterator I = nullptr;

public:
  using iterator_category = std::input_iterator_tag;
  using value_type = T *;
  using difference_type = std::ptrdiff_t;
  using pointer = void;
  using reference = T *;

  TypedMDOperandIterator() = default;
  explicit TypedMDOperandIterator(MDNode::op_iterator I) : I(I) {}

  T *operator*() const { return cast_or_null<T>(*I); }

  TypedMDOperandIterator &operator++() {
    ++I;
    return *this;
  }

  TypedMDOperandIterator operator++(int) {
    TypedMDOperandIterator Temp(*this);
    ++I;
    return Temp;
  }

  bool operator==(const TypedMDOperandIterator &X) const { return I == X.I; }
  bool operator!=(const TypedMDOperandIterator &X) const { return I != X.I; }
};

/// Typed, array-like tuple of metadata.
///
/// This is a wrapper for \a MDTuple that makes it act like an array holding a
/// particular type of metadata.
template <class T> class MDTupleTypedArrayWrapper {
  const MDTuple *N = nullptr;

public:
  MDTupleTypedArrayWrapper() = default;
  MDTupleTypedArrayWrapper(const MDTuple *N) : N(N) {}

  template <class U>
  MDTupleTypedArrayWrapper(
      const MDTupleTypedArrayWrapper<U> &Other,
      std::enable_if_t<std::is_convertible<U *, T *>::value> * = nullptr)
      : N(Other.get()) {}

  template <class U>
  explicit MDTupleTypedArrayWrapper(
      const MDTupleTypedArrayWrapper<U> &Other,
      std::enable_if_t<!std::is_convertible<U *, T *>::value> * = nullptr)
      : N(Other.get()) {}

  explicit operator bool() const { return get(); }
  explicit operator MDTuple *() const { return get(); }

  MDTuple *get() const { return const_cast<MDTuple *>(N); }
  MDTuple *operator->() const { return get(); }
  MDTuple &operator*() const { return *get(); }

  // FIXME: Fix callers and remove condition on N.
  unsigned size() const { return N ? N->getNumOperands() : 0u; }
  bool empty() const { return N ? N->getNumOperands() == 0 : true; }
  T *operator[](unsigned I) const { return cast_or_null<T>(N->getOperand(I)); }

  // FIXME: Fix callers and remove condition on N.
  using iterator = TypedMDOperandIterator<T>;

  iterator begin() const { return N ? iterator(N->op_begin()) : iterator(); }
  iterator end() const { return N ? iterator(N->op_end()) : iterator(); }
};

#define HANDLE_METADATA(CLASS)                                                 \
  using CLASS##Array = MDTupleTypedArrayWrapper<CLASS>;
#include "llvm/IR/Metadata.def"

/// Placeholder metadata for operands of distinct MDNodes.
///
/// This is a lightweight placeholder for an operand of a distinct node.  It's
/// purpose is to help track forward references when creating a distinct node.
/// This allows distinct nodes involved in a cycle to be constructed before
/// their operands without requiring a heavyweight temporary node with
/// full-blown RAUW support.
///
/// Each placeholder supports only a single MDNode user.  Clients should pass
/// an ID, retrieved via \a getID(), to indicate the "real" operand that this
/// should be replaced with.
///
/// While it would be possible to implement move operators, they would be
/// fairly expensive.  Leave them unimplemented to discourage their use
/// (clients can use std::deque, std::list, BumpPtrAllocator, etc.).
class DistinctMDOperandPlaceholder : public Metadata {
  friend class MetadataTracking;

  Metadata **Use = nullptr;

public:
  explicit DistinctMDOperandPlaceholder(unsigned ID)
      : Metadata(DistinctMDOperandPlaceholderKind, Distinct) {
    SubclassData32 = ID;
  }

  DistinctMDOperandPlaceholder() = delete;
  DistinctMDOperandPlaceholder(DistinctMDOperandPlaceholder &&) = delete;
  DistinctMDOperandPlaceholder(const DistinctMDOperandPlaceholder &) = delete;

  ~DistinctMDOperandPlaceholder() {
    if (Use)
      *Use = nullptr;
  }

  unsigned getID() const { return SubclassData32; }

  /// Replace the use of this with MD.
  void replaceUseWith(Metadata *MD) {
    if (!Use)
      return;
    *Use = MD;

    if (*Use)
      MetadataTracking::track(*Use);

    Metadata *T = cast<Metadata>(this);
    MetadataTracking::untrack(T);
    assert(!Use && "Use is still being tracked despite being untracked!");
  }
};

//===----------------------------------------------------------------------===//
/// A tuple of MDNodes.
///
/// Despite its name, a NamedMDNode isn't itself an MDNode.
///
/// NamedMDNodes are named module-level entities that contain lists of MDNodes.
///
/// It is illegal for a NamedMDNode to appear as an operand of an MDNode.
class NamedMDNode : public ilist_node<NamedMDNode> {
  friend class LLVMContextImpl;
  friend class Module;

  std::string Name;
  Module *Parent = nullptr;
  void *Operands; // SmallVector<TrackingMDRef, 4>

  void setParent(Module *M) { Parent = M; }

  explicit NamedMDNode(const Twine &N);

  template <class T1> class op_iterator_impl {
    friend class NamedMDNode;

    const NamedMDNode *Node = nullptr;
    unsigned Idx = 0;

    op_iterator_impl(const NamedMDNode *N, unsigned i) : Node(N), Idx(i) {}

  public:
    using iterator_category = std::bidirectional_iterator_tag;
    using value_type = T1;
    using difference_type = std::ptrdiff_t;
    using pointer = value_type *;
    using reference = value_type;

    op_iterator_impl() = default;

    bool operator==(const op_iterator_impl &o) const { return Idx == o.Idx; }
    bool operator!=(const op_iterator_impl &o) const { return Idx != o.Idx; }

    op_iterator_impl &operator++() {
      ++Idx;
      return *this;
    }

    op_iterator_impl operator++(int) {
      op_iterator_impl tmp(*this);
      operator++();
      return tmp;
    }

    op_iterator_impl &operator--() {
      --Idx;
      return *this;
    }

    op_iterator_impl operator--(int) {
      op_iterator_impl tmp(*this);
      operator--();
      return tmp;
    }

    T1 operator*() const { return Node->getOperand(Idx); }
  };

public:
  NamedMDNode(const NamedMDNode &) = delete;
  ~NamedMDNode();

  /// Drop all references and remove the node from parent module.
  void eraseFromParent();

  /// Remove all uses and clear node vector.
  void dropAllReferences() { clearOperands(); }
  /// Drop all references to this node's operands.
  void clearOperands();

  /// Get the module that holds this named metadata collection.
  inline Module *getParent() { return Parent; }
  inline const Module *getParent() const { return Parent; }

  MDNode *getOperand(unsigned i) const;
  unsigned getNumOperands() const;
  void addOperand(MDNode *M);
  void setOperand(unsigned I, MDNode *New);
  StringRef getName() const;
  void print(raw_ostream &ROS, bool IsForDebug = false) const;
  void print(raw_ostream &ROS, ModuleSlotTracker &MST,
             bool IsForDebug = false) const;
  void dump() const;

  // ---------------------------------------------------------------------------
  // Operand Iterator interface...
  //
  using op_iterator = op_iterator_impl<MDNode *>;

  op_iterator op_begin() { return op_iterator(this, 0); }
  op_iterator op_end()   { return op_iterator(this, getNumOperands()); }

  using const_op_iterator = op_iterator_impl<const MDNode *>;

  const_op_iterator op_begin() const { return const_op_iterator(this, 0); }
  const_op_iterator op_end()   const { return const_op_iterator(this, getNumOperands()); }

  inline iterator_range<op_iterator>  operands() {
    return make_range(op_begin(), op_end());
  }
  inline iterator_range<const_op_iterator> operands() const {
    return make_range(op_begin(), op_end());
  }
};

// Create wrappers for C Binding types (see CBindingWrapping.h).
DEFINE_ISA_CONVERSION_FUNCTIONS(NamedMDNode, LLVMNamedMDNodeRef)

} // end namespace llvm

#endif // LLVM_IR_METADATA_H
