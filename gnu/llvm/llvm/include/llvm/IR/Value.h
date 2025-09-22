//===- llvm/Value.h - Definition of the Value class -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the Value class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_VALUE_H
#define LLVM_IR_VALUE_H

#include "llvm-c/Types.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/IR/Use.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/CBindingWrapping.h"
#include "llvm/Support/Casting.h"
#include <cassert>
#include <iterator>
#include <memory>

namespace llvm {

class APInt;
class Argument;
class BasicBlock;
class Constant;
class ConstantData;
class ConstantAggregate;
class DataLayout;
class Function;
class GlobalAlias;
class GlobalIFunc;
class GlobalObject;
class GlobalValue;
class GlobalVariable;
class InlineAsm;
class Instruction;
class LLVMContext;
class MDNode;
class Module;
class ModuleSlotTracker;
class raw_ostream;
template<typename ValueTy> class StringMapEntry;
class Twine;
class Type;
class User;

using ValueName = StringMapEntry<Value *>;

//===----------------------------------------------------------------------===//
//                                 Value Class
//===----------------------------------------------------------------------===//

/// LLVM Value Representation
///
/// This is a very important LLVM class. It is the base class of all values
/// computed by a program that may be used as operands to other values. Value is
/// the super class of other important classes such as Instruction and Function.
/// All Values have a Type. Type is not a subclass of Value. Some values can
/// have a name and they belong to some Module.  Setting the name on the Value
/// automatically updates the module's symbol table.
///
/// Every value has a "use list" that keeps track of which other Values are
/// using this Value.  A Value can also have an arbitrary number of ValueHandle
/// objects that watch it and listen to RAUW and Destroy events.  See
/// llvm/IR/ValueHandle.h for details.
class Value {
  const unsigned char SubclassID;   // Subclass identifier (for isa/dyn_cast)
  unsigned char HasValueHandle : 1; // Has a ValueHandle pointing to this?

protected:
  /// Hold subclass data that can be dropped.
  ///
  /// This member is similar to SubclassData, however it is for holding
  /// information which may be used to aid optimization, but which may be
  /// cleared to zero without affecting conservative interpretation.
  unsigned char SubclassOptionalData : 7;

private:
  /// Hold arbitrary subclass data.
  ///
  /// This member is defined by this class, but is not used for anything.
  /// Subclasses can use it to hold whatever state they find useful.  This
  /// field is initialized to zero by the ctor.
  unsigned short SubclassData;

protected:
  /// The number of operands in the subclass.
  ///
  /// This member is defined by this class, but not used for anything.
  /// Subclasses can use it to store their number of operands, if they have
  /// any.
  ///
  /// This is stored here to save space in User on 64-bit hosts.  Since most
  /// instances of Value have operands, 32-bit hosts aren't significantly
  /// affected.
  ///
  /// Note, this should *NOT* be used directly by any class other than User.
  /// User uses this value to find the Use list.
  enum : unsigned { NumUserOperandsBits = 27 };
  unsigned NumUserOperands : NumUserOperandsBits;

  // Use the same type as the bitfield above so that MSVC will pack them.
  unsigned IsUsedByMD : 1;
  unsigned HasName : 1;
  unsigned HasMetadata : 1; // Has metadata attached to this?
  unsigned HasHungOffUses : 1;
  unsigned HasDescriptor : 1;

private:
  Type *VTy;
  Use *UseList;

  friend class ValueAsMetadata; // Allow access to IsUsedByMD.
  friend class ValueHandleBase; // Allow access to HasValueHandle.

  template <typename UseT> // UseT == 'Use' or 'const Use'
  class use_iterator_impl {
    friend class Value;

    UseT *U;

    explicit use_iterator_impl(UseT *u) : U(u) {}

  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = UseT *;
    using difference_type = std::ptrdiff_t;
    using pointer = value_type *;
    using reference = value_type &;

    use_iterator_impl() : U() {}

    bool operator==(const use_iterator_impl &x) const { return U == x.U; }
    bool operator!=(const use_iterator_impl &x) const { return !operator==(x); }

    use_iterator_impl &operator++() { // Preincrement
      assert(U && "Cannot increment end iterator!");
      U = U->getNext();
      return *this;
    }

    use_iterator_impl operator++(int) { // Postincrement
      auto tmp = *this;
      ++*this;
      return tmp;
    }

    UseT &operator*() const {
      assert(U && "Cannot dereference end iterator!");
      return *U;
    }

    UseT *operator->() const { return &operator*(); }

    operator use_iterator_impl<const UseT>() const {
      return use_iterator_impl<const UseT>(U);
    }
  };

  template <typename UserTy> // UserTy == 'User' or 'const User'
  class user_iterator_impl {
    use_iterator_impl<Use> UI;
    explicit user_iterator_impl(Use *U) : UI(U) {}
    friend class Value;

  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = UserTy *;
    using difference_type = std::ptrdiff_t;
    using pointer = value_type *;
    using reference = value_type &;

    user_iterator_impl() = default;

    bool operator==(const user_iterator_impl &x) const { return UI == x.UI; }
    bool operator!=(const user_iterator_impl &x) const { return !operator==(x); }

    /// Returns true if this iterator is equal to user_end() on the value.
    bool atEnd() const { return *this == user_iterator_impl(); }

    user_iterator_impl &operator++() { // Preincrement
      ++UI;
      return *this;
    }

    user_iterator_impl operator++(int) { // Postincrement
      auto tmp = *this;
      ++*this;
      return tmp;
    }

    // Retrieve a pointer to the current User.
    UserTy *operator*() const {
      return UI->getUser();
    }

    UserTy *operator->() const { return operator*(); }

    operator user_iterator_impl<const UserTy>() const {
      return user_iterator_impl<const UserTy>(*UI);
    }

    Use &getUse() const { return *UI; }
  };

protected:
  Value(Type *Ty, unsigned scid);

  /// Value's destructor should be virtual by design, but that would require
  /// that Value and all of its subclasses have a vtable that effectively
  /// duplicates the information in the value ID. As a size optimization, the
  /// destructor has been protected, and the caller should manually call
  /// deleteValue.
  ~Value(); // Use deleteValue() to delete a generic Value.

public:
  Value(const Value &) = delete;
  Value &operator=(const Value &) = delete;

  /// Delete a pointer to a generic Value.
  void deleteValue();

  /// Support for debugging, callable in GDB: V->dump()
  void dump() const;

  /// Implement operator<< on Value.
  /// @{
  void print(raw_ostream &O, bool IsForDebug = false) const;
  void print(raw_ostream &O, ModuleSlotTracker &MST,
             bool IsForDebug = false) const;
  /// @}

  /// Print the name of this Value out to the specified raw_ostream.
  ///
  /// This is useful when you just want to print 'int %reg126', not the
  /// instruction that generated it. If you specify a Module for context, then
  /// even constants get pretty-printed; for example, the type of a null
  /// pointer is printed symbolically.
  /// @{
  void printAsOperand(raw_ostream &O, bool PrintType = true,
                      const Module *M = nullptr) const;
  void printAsOperand(raw_ostream &O, bool PrintType,
                      ModuleSlotTracker &MST) const;
  /// @}

  /// All values are typed, get the type of this value.
  Type *getType() const { return VTy; }

  /// All values hold a context through their type.
  LLVMContext &getContext() const;

  // All values can potentially be named.
  bool hasName() const { return HasName; }
  ValueName *getValueName() const;
  void setValueName(ValueName *VN);

private:
  void destroyValueName();
  enum class ReplaceMetadataUses { No, Yes };
  void doRAUW(Value *New, ReplaceMetadataUses);
  void setNameImpl(const Twine &Name);

public:
  /// Return a constant reference to the value's name.
  ///
  /// This guaranteed to return the same reference as long as the value is not
  /// modified.  If the value has a name, this does a hashtable lookup, so it's
  /// not free.
  StringRef getName() const;

  /// Change the name of the value.
  ///
  /// Choose a new unique name if the provided name is taken.
  ///
  /// \param Name The new name; or "" if the value's name should be removed.
  void setName(const Twine &Name);

  /// Transfer the name from V to this value.
  ///
  /// After taking V's name, sets V's name to empty.
  ///
  /// \note It is an error to call V->takeName(V).
  void takeName(Value *V);

#ifndef NDEBUG
  std::string getNameOrAsOperand() const;
#endif

  /// Change all uses of this to point to a new Value.
  ///
  /// Go through the uses list for this definition and make each use point to
  /// "V" instead of "this".  After this completes, 'this's use list is
  /// guaranteed to be empty.
  void replaceAllUsesWith(Value *V);

  /// Change non-metadata uses of this to point to a new Value.
  ///
  /// Go through the uses list for this definition and make each use point to
  /// "V" instead of "this". This function skips metadata entries in the list.
  void replaceNonMetadataUsesWith(Value *V);

  /// Go through the uses list for this definition and make each use point
  /// to "V" if the callback ShouldReplace returns true for the given Use.
  /// Unlike replaceAllUsesWith() this function does not support basic block
  /// values.
  void replaceUsesWithIf(Value *New,
                         llvm::function_ref<bool(Use &U)> ShouldReplace);

  /// replaceUsesOutsideBlock - Go through the uses list for this definition and
  /// make each use point to "V" instead of "this" when the use is outside the
  /// block. 'This's use list is expected to have at least one element.
  /// Unlike replaceAllUsesWith() this function does not support basic block
  /// values.
  void replaceUsesOutsideBlock(Value *V, BasicBlock *BB);

  //----------------------------------------------------------------------
  // Methods for handling the chain of uses of this Value.
  //
  // Materializing a function can introduce new uses, so these methods come in
  // two variants:
  // The methods that start with materialized_ check the uses that are
  // currently known given which functions are materialized. Be very careful
  // when using them since you might not get all uses.
  // The methods that don't start with materialized_ assert that modules is
  // fully materialized.
  void assertModuleIsMaterializedImpl() const;
  // This indirection exists so we can keep assertModuleIsMaterializedImpl()
  // around in release builds of Value.cpp to be linked with other code built
  // in debug mode. But this avoids calling it in any of the release built code.
  void assertModuleIsMaterialized() const {
#ifndef NDEBUG
    assertModuleIsMaterializedImpl();
#endif
  }

  bool use_empty() const {
    assertModuleIsMaterialized();
    return UseList == nullptr;
  }

  bool materialized_use_empty() const {
    return UseList == nullptr;
  }

  using use_iterator = use_iterator_impl<Use>;
  using const_use_iterator = use_iterator_impl<const Use>;

  use_iterator materialized_use_begin() { return use_iterator(UseList); }
  const_use_iterator materialized_use_begin() const {
    return const_use_iterator(UseList);
  }
  use_iterator use_begin() {
    assertModuleIsMaterialized();
    return materialized_use_begin();
  }
  const_use_iterator use_begin() const {
    assertModuleIsMaterialized();
    return materialized_use_begin();
  }
  use_iterator use_end() { return use_iterator(); }
  const_use_iterator use_end() const { return const_use_iterator(); }
  iterator_range<use_iterator> materialized_uses() {
    return make_range(materialized_use_begin(), use_end());
  }
  iterator_range<const_use_iterator> materialized_uses() const {
    return make_range(materialized_use_begin(), use_end());
  }
  iterator_range<use_iterator> uses() {
    assertModuleIsMaterialized();
    return materialized_uses();
  }
  iterator_range<const_use_iterator> uses() const {
    assertModuleIsMaterialized();
    return materialized_uses();
  }

  bool user_empty() const {
    assertModuleIsMaterialized();
    return UseList == nullptr;
  }

  using user_iterator = user_iterator_impl<User>;
  using const_user_iterator = user_iterator_impl<const User>;

  user_iterator materialized_user_begin() { return user_iterator(UseList); }
  const_user_iterator materialized_user_begin() const {
    return const_user_iterator(UseList);
  }
  user_iterator user_begin() {
    assertModuleIsMaterialized();
    return materialized_user_begin();
  }
  const_user_iterator user_begin() const {
    assertModuleIsMaterialized();
    return materialized_user_begin();
  }
  user_iterator user_end() { return user_iterator(); }
  const_user_iterator user_end() const { return const_user_iterator(); }
  User *user_back() {
    assertModuleIsMaterialized();
    return *materialized_user_begin();
  }
  const User *user_back() const {
    assertModuleIsMaterialized();
    return *materialized_user_begin();
  }
  iterator_range<user_iterator> materialized_users() {
    return make_range(materialized_user_begin(), user_end());
  }
  iterator_range<const_user_iterator> materialized_users() const {
    return make_range(materialized_user_begin(), user_end());
  }
  iterator_range<user_iterator> users() {
    assertModuleIsMaterialized();
    return materialized_users();
  }
  iterator_range<const_user_iterator> users() const {
    assertModuleIsMaterialized();
    return materialized_users();
  }

  /// Return true if there is exactly one use of this value.
  ///
  /// This is specialized because it is a common request and does not require
  /// traversing the whole use list.
  bool hasOneUse() const { return hasSingleElement(uses()); }

  /// Return true if this Value has exactly N uses.
  bool hasNUses(unsigned N) const;

  /// Return true if this value has N uses or more.
  ///
  /// This is logically equivalent to getNumUses() >= N.
  bool hasNUsesOrMore(unsigned N) const;

  /// Return true if there is exactly one user of this value.
  ///
  /// Note that this is not the same as "has one use". If a value has one use,
  /// then there certainly is a single user. But if value has several uses,
  /// it is possible that all uses are in a single user, or not.
  ///
  /// This check is potentially costly, since it requires traversing,
  /// in the worst case, the whole use list of a value.
  bool hasOneUser() const;

  /// Return true if there is exactly one use of this value that cannot be
  /// dropped.
  Use *getSingleUndroppableUse();
  const Use *getSingleUndroppableUse() const {
    return const_cast<Value *>(this)->getSingleUndroppableUse();
  }

  /// Return true if there is exactly one unique user of this value that cannot be
  /// dropped (that user can have multiple uses of this value).
  User *getUniqueUndroppableUser();
  const User *getUniqueUndroppableUser() const {
    return const_cast<Value *>(this)->getUniqueUndroppableUser();
  }

  /// Return true if there this value.
  ///
  /// This is specialized because it is a common request and does not require
  /// traversing the whole use list.
  bool hasNUndroppableUses(unsigned N) const;

  /// Return true if this value has N uses or more.
  ///
  /// This is logically equivalent to getNumUses() >= N.
  bool hasNUndroppableUsesOrMore(unsigned N) const;

  /// Remove every uses that can safely be removed.
  ///
  /// This will remove for example uses in llvm.assume.
  /// This should be used when performing want to perform a tranformation but
  /// some Droppable uses pervent it.
  /// This function optionally takes a filter to only remove some droppable
  /// uses.
  void dropDroppableUses(llvm::function_ref<bool(const Use *)> ShouldDrop =
                             [](const Use *) { return true; });

  /// Remove every use of this value in \p User that can safely be removed.
  void dropDroppableUsesIn(User &Usr);

  /// Remove the droppable use \p U.
  static void dropDroppableUse(Use &U);

  /// Check if this value is used in the specified basic block.
  bool isUsedInBasicBlock(const BasicBlock *BB) const;

  /// This method computes the number of uses of this Value.
  ///
  /// This is a linear time operation.  Use hasOneUse, hasNUses, or
  /// hasNUsesOrMore to check for specific values.
  unsigned getNumUses() const;

  /// This method should only be used by the Use class.
  void addUse(Use &U) { U.addToList(&UseList); }

  /// Concrete subclass of this.
  ///
  /// An enumeration for keeping track of the concrete subclass of Value that
  /// is actually instantiated. Values of this enumeration are kept in the
  /// Value classes SubclassID field. They are used for concrete type
  /// identification.
  enum ValueTy {
#define HANDLE_VALUE(Name) Name##Val,
#include "llvm/IR/Value.def"

    // Markers:
#define HANDLE_CONSTANT_MARKER(Marker, Constant) Marker = Constant##Val,
#include "llvm/IR/Value.def"
  };

  /// Return an ID for the concrete type of this object.
  ///
  /// This is used to implement the classof checks.  This should not be used
  /// for any other purpose, as the values may change as LLVM evolves.  Also,
  /// note that for instructions, the Instruction's opcode is added to
  /// InstructionVal. So this means three things:
  /// # there is no value with code InstructionVal (no opcode==0).
  /// # there are more possible values for the value type than in ValueTy enum.
  /// # the InstructionVal enumerator must be the highest valued enumerator in
  ///   the ValueTy enum.
  unsigned getValueID() const {
    return SubclassID;
  }

  /// Return the raw optional flags value contained in this value.
  ///
  /// This should only be used when testing two Values for equivalence.
  unsigned getRawSubclassOptionalData() const {
    return SubclassOptionalData;
  }

  /// Clear the optional flags contained in this value.
  void clearSubclassOptionalData() {
    SubclassOptionalData = 0;
  }

  /// Check the optional flags for equality.
  bool hasSameSubclassOptionalData(const Value *V) const {
    return SubclassOptionalData == V->SubclassOptionalData;
  }

  /// Return true if there is a value handle associated with this value.
  bool hasValueHandle() const { return HasValueHandle; }

  /// Return true if there is metadata referencing this value.
  bool isUsedByMetadata() const { return IsUsedByMD; }

protected:
  /// Get the current metadata attachments for the given kind, if any.
  ///
  /// These functions require that the value have at most a single attachment
  /// of the given kind, and return \c nullptr if such an attachment is missing.
  /// @{
  MDNode *getMetadata(unsigned KindID) const {
    if (!HasMetadata)
      return nullptr;
    return getMetadataImpl(KindID);
  }
  MDNode *getMetadata(StringRef Kind) const;
  /// @}

  /// Appends all attachments with the given ID to \c MDs in insertion order.
  /// If the Value has no attachments with the given ID, or if ID is invalid,
  /// leaves MDs unchanged.
  /// @{
  void getMetadata(unsigned KindID, SmallVectorImpl<MDNode *> &MDs) const;
  void getMetadata(StringRef Kind, SmallVectorImpl<MDNode *> &MDs) const;
  /// @}

  /// Appends all metadata attached to this value to \c MDs, sorting by
  /// KindID. The first element of each pair returned is the KindID, the second
  /// element is the metadata value. Attachments with the same ID appear in
  /// insertion order.
  void
  getAllMetadata(SmallVectorImpl<std::pair<unsigned, MDNode *>> &MDs) const;

  /// Return true if this value has any metadata attached to it.
  bool hasMetadata() const { return (bool)HasMetadata; }

  /// Return true if this value has the given type of metadata attached.
  /// @{
  bool hasMetadata(unsigned KindID) const {
    return getMetadata(KindID) != nullptr;
  }
  bool hasMetadata(StringRef Kind) const {
    return getMetadata(Kind) != nullptr;
  }
  /// @}

  /// Set a particular kind of metadata attachment.
  ///
  /// Sets the given attachment to \c MD, erasing it if \c MD is \c nullptr or
  /// replacing it if it already exists.
  /// @{
  void setMetadata(unsigned KindID, MDNode *Node);
  void setMetadata(StringRef Kind, MDNode *Node);
  /// @}

  /// Add a metadata attachment.
  /// @{
  void addMetadata(unsigned KindID, MDNode &MD);
  void addMetadata(StringRef Kind, MDNode &MD);
  /// @}

  /// Erase all metadata attachments with the given kind.
  ///
  /// \returns true if any metadata was removed.
  bool eraseMetadata(unsigned KindID);

  /// Erase all metadata attachments matching the given predicate.
  void eraseMetadataIf(function_ref<bool(unsigned, MDNode *)> Pred);

  /// Erase all metadata attached to this Value.
  void clearMetadata();

  /// Get metadata for the given kind, if any.
  /// This is an internal function that must only be called after
  /// checking that `hasMetadata()` returns true.
  MDNode *getMetadataImpl(unsigned KindID) const;

public:
  /// Return true if this value is a swifterror value.
  ///
  /// swifterror values can be either a function argument or an alloca with a
  /// swifterror attribute.
  bool isSwiftError() const;

  /// Strip off pointer casts, all-zero GEPs and address space casts.
  ///
  /// Returns the original uncasted value.  If this is called on a non-pointer
  /// value, it returns 'this'.
  const Value *stripPointerCasts() const;
  Value *stripPointerCasts() {
    return const_cast<Value *>(
        static_cast<const Value *>(this)->stripPointerCasts());
  }

  /// Strip off pointer casts, all-zero GEPs, address space casts, and aliases.
  ///
  /// Returns the original uncasted value.  If this is called on a non-pointer
  /// value, it returns 'this'.
  const Value *stripPointerCastsAndAliases() const;
  Value *stripPointerCastsAndAliases() {
    return const_cast<Value *>(
        static_cast<const Value *>(this)->stripPointerCastsAndAliases());
  }

  /// Strip off pointer casts, all-zero GEPs and address space casts
  /// but ensures the representation of the result stays the same.
  ///
  /// Returns the original uncasted value with the same representation. If this
  /// is called on a non-pointer value, it returns 'this'.
  const Value *stripPointerCastsSameRepresentation() const;
  Value *stripPointerCastsSameRepresentation() {
    return const_cast<Value *>(static_cast<const Value *>(this)
                                   ->stripPointerCastsSameRepresentation());
  }

  /// Strip off pointer casts, all-zero GEPs, single-argument phi nodes and
  /// invariant group info.
  ///
  /// Returns the original uncasted value.  If this is called on a non-pointer
  /// value, it returns 'this'. This function should be used only in
  /// Alias analysis.
  const Value *stripPointerCastsForAliasAnalysis() const;
  Value *stripPointerCastsForAliasAnalysis() {
    return const_cast<Value *>(static_cast<const Value *>(this)
                                   ->stripPointerCastsForAliasAnalysis());
  }

  /// Strip off pointer casts and all-constant inbounds GEPs.
  ///
  /// Returns the original pointer value.  If this is called on a non-pointer
  /// value, it returns 'this'.
  const Value *stripInBoundsConstantOffsets() const;
  Value *stripInBoundsConstantOffsets() {
    return const_cast<Value *>(
              static_cast<const Value *>(this)->stripInBoundsConstantOffsets());
  }

  /// Accumulate the constant offset this value has compared to a base pointer.
  /// Only 'getelementptr' instructions (GEPs) are accumulated but other
  /// instructions, e.g., casts, are stripped away as well.
  /// The accumulated constant offset is added to \p Offset and the base
  /// pointer is returned.
  ///
  /// The APInt \p Offset has to have a bit-width equal to the IntPtr type for
  /// the address space of 'this' pointer value, e.g., use
  /// DataLayout::getIndexTypeSizeInBits(Ty).
  ///
  /// If \p AllowNonInbounds is true, offsets in GEPs are stripped and
  /// accumulated even if the GEP is not "inbounds".
  ///
  /// If \p AllowInvariantGroup is true then this method also looks through
  /// strip.invariant.group and launder.invariant.group intrinsics.
  ///
  /// If \p ExternalAnalysis is provided it will be used to calculate a offset
  /// when a operand of GEP is not constant.
  /// For example, for a value \p ExternalAnalysis might try to calculate a
  /// lower bound. If \p ExternalAnalysis is successful, it should return true.
  ///
  /// If this is called on a non-pointer value, it returns 'this' and the
  /// \p Offset is not modified.
  ///
  /// Note that this function will never return a nullptr. It will also never
  /// manipulate the \p Offset in a way that would not match the difference
  /// between the underlying value and the returned one. Thus, if no constant
  /// offset was found, the returned value is the underlying one and \p Offset
  /// is unchanged.
  const Value *stripAndAccumulateConstantOffsets(
      const DataLayout &DL, APInt &Offset, bool AllowNonInbounds,
      bool AllowInvariantGroup = false,
      function_ref<bool(Value &Value, APInt &Offset)> ExternalAnalysis =
          nullptr) const;
  Value *stripAndAccumulateConstantOffsets(const DataLayout &DL, APInt &Offset,
                                           bool AllowNonInbounds,
                                           bool AllowInvariantGroup = false) {
    return const_cast<Value *>(
        static_cast<const Value *>(this)->stripAndAccumulateConstantOffsets(
            DL, Offset, AllowNonInbounds, AllowInvariantGroup));
  }

  /// This is a wrapper around stripAndAccumulateConstantOffsets with the
  /// in-bounds requirement set to false.
  const Value *stripAndAccumulateInBoundsConstantOffsets(const DataLayout &DL,
                                                         APInt &Offset) const {
    return stripAndAccumulateConstantOffsets(DL, Offset,
                                             /* AllowNonInbounds */ false);
  }
  Value *stripAndAccumulateInBoundsConstantOffsets(const DataLayout &DL,
                                                   APInt &Offset) {
    return stripAndAccumulateConstantOffsets(DL, Offset,
                                             /* AllowNonInbounds */ false);
  }

  /// Strip off pointer casts and inbounds GEPs.
  ///
  /// Returns the original pointer value.  If this is called on a non-pointer
  /// value, it returns 'this'.
  const Value *stripInBoundsOffsets(function_ref<void(const Value *)> Func =
                                        [](const Value *) {}) const;
  inline Value *stripInBoundsOffsets(function_ref<void(const Value *)> Func =
                                  [](const Value *) {}) {
    return const_cast<Value *>(
        static_cast<const Value *>(this)->stripInBoundsOffsets(Func));
  }

  /// If this ptr is provably equal to \p Other plus a constant offset, return
  /// that offset in bytes. Essentially `ptr this` subtract `ptr Other`.
  std::optional<int64_t> getPointerOffsetFrom(const Value *Other,
                                              const DataLayout &DL) const;

  /// Return true if the memory object referred to by V can by freed in the
  /// scope for which the SSA value defining the allocation is statically
  /// defined.  E.g.  deallocation after the static scope of a value does not
  /// count, but a deallocation before that does.
  bool canBeFreed() const;

  /// Returns the number of bytes known to be dereferenceable for the
  /// pointer value.
  ///
  /// If CanBeNull is set by this function the pointer can either be null or be
  /// dereferenceable up to the returned number of bytes.
  ///
  /// IF CanBeFreed is true, the pointer is known to be dereferenceable at
  /// point of definition only.  Caller must prove that allocation is not
  /// deallocated between point of definition and use.
  uint64_t getPointerDereferenceableBytes(const DataLayout &DL,
                                          bool &CanBeNull,
                                          bool &CanBeFreed) const;

  /// Returns an alignment of the pointer value.
  ///
  /// Returns an alignment which is either specified explicitly, e.g. via
  /// align attribute of a function argument, or guaranteed by DataLayout.
  Align getPointerAlignment(const DataLayout &DL) const;

  /// Translate PHI node to its predecessor from the given basic block.
  ///
  /// If this value is a PHI node with CurBB as its parent, return the value in
  /// the PHI node corresponding to PredBB.  If not, return ourself.  This is
  /// useful if you want to know the value something has in a predecessor
  /// block.
  const Value *DoPHITranslation(const BasicBlock *CurBB,
                                const BasicBlock *PredBB) const;
  Value *DoPHITranslation(const BasicBlock *CurBB, const BasicBlock *PredBB) {
    return const_cast<Value *>(
             static_cast<const Value *>(this)->DoPHITranslation(CurBB, PredBB));
  }

  /// The maximum alignment for instructions.
  ///
  /// This is the greatest alignment value supported by load, store, and alloca
  /// instructions, and global values.
  static constexpr unsigned MaxAlignmentExponent = 32;
  static constexpr uint64_t MaximumAlignment = 1ULL << MaxAlignmentExponent;

  /// Mutate the type of this Value to be of the specified type.
  ///
  /// Note that this is an extremely dangerous operation which can create
  /// completely invalid IR very easily.  It is strongly recommended that you
  /// recreate IR objects with the right types instead of mutating them in
  /// place.
  void mutateType(Type *Ty) {
    VTy = Ty;
  }

  /// Sort the use-list.
  ///
  /// Sorts the Value's use-list by Cmp using a stable mergesort.  Cmp is
  /// expected to compare two \a Use references.
  template <class Compare> void sortUseList(Compare Cmp);

  /// Reverse the use-list.
  void reverseUseList();

private:
  /// Merge two lists together.
  ///
  /// Merges \c L and \c R using \c Cmp.  To enable stable sorts, always pushes
  /// "equal" items from L before items from R.
  ///
  /// \return the first element in the list.
  ///
  /// \note Completely ignores \a Use::Prev (doesn't read, doesn't update).
  template <class Compare>
  static Use *mergeUseLists(Use *L, Use *R, Compare Cmp) {
    Use *Merged;
    Use **Next = &Merged;

    while (true) {
      if (!L) {
        *Next = R;
        break;
      }
      if (!R) {
        *Next = L;
        break;
      }
      if (Cmp(*R, *L)) {
        *Next = R;
        Next = &R->Next;
        R = R->Next;
      } else {
        *Next = L;
        Next = &L->Next;
        L = L->Next;
      }
    }

    return Merged;
  }

protected:
  unsigned short getSubclassDataFromValue() const { return SubclassData; }
  void setValueSubclassData(unsigned short D) { SubclassData = D; }
};

struct ValueDeleter { void operator()(Value *V) { V->deleteValue(); } };

/// Use this instead of std::unique_ptr<Value> or std::unique_ptr<Instruction>.
/// Those don't work because Value and Instruction's destructors are protected,
/// aren't virtual, and won't destroy the complete object.
using unique_value = std::unique_ptr<Value, ValueDeleter>;

inline raw_ostream &operator<<(raw_ostream &OS, const Value &V) {
  V.print(OS);
  return OS;
}

void Use::set(Value *V) {
  if (Val) removeFromList();
  Val = V;
  if (V) V->addUse(*this);
}

Value *Use::operator=(Value *RHS) {
  set(RHS);
  return RHS;
}

const Use &Use::operator=(const Use &RHS) {
  set(RHS.Val);
  return *this;
}

template <class Compare> void Value::sortUseList(Compare Cmp) {
  if (!UseList || !UseList->Next)
    // No need to sort 0 or 1 uses.
    return;

  // Note: this function completely ignores Prev pointers until the end when
  // they're fixed en masse.

  // Create a binomial vector of sorted lists, visiting uses one at a time and
  // merging lists as necessary.
  const unsigned MaxSlots = 32;
  Use *Slots[MaxSlots];

  // Collect the first use, turning it into a single-item list.
  Use *Next = UseList->Next;
  UseList->Next = nullptr;
  unsigned NumSlots = 1;
  Slots[0] = UseList;

  // Collect all but the last use.
  while (Next->Next) {
    Use *Current = Next;
    Next = Current->Next;

    // Turn Current into a single-item list.
    Current->Next = nullptr;

    // Save Current in the first available slot, merging on collisions.
    unsigned I;
    for (I = 0; I < NumSlots; ++I) {
      if (!Slots[I])
        break;

      // Merge two lists, doubling the size of Current and emptying slot I.
      //
      // Since the uses in Slots[I] originally preceded those in Current, send
      // Slots[I] in as the left parameter to maintain a stable sort.
      Current = mergeUseLists(Slots[I], Current, Cmp);
      Slots[I] = nullptr;
    }
    // Check if this is a new slot.
    if (I == NumSlots) {
      ++NumSlots;
      assert(NumSlots <= MaxSlots && "Use list bigger than 2^32");
    }

    // Found an open slot.
    Slots[I] = Current;
  }

  // Merge all the lists together.
  assert(Next && "Expected one more Use");
  assert(!Next->Next && "Expected only one Use");
  UseList = Next;
  for (unsigned I = 0; I < NumSlots; ++I)
    if (Slots[I])
      // Since the uses in Slots[I] originally preceded those in UseList, send
      // Slots[I] in as the left parameter to maintain a stable sort.
      UseList = mergeUseLists(Slots[I], UseList, Cmp);

  // Fix the Prev pointers.
  for (Use *I = UseList, **Prev = &UseList; I; I = I->Next) {
    I->Prev = Prev;
    Prev = &I->Next;
  }
}

// isa - Provide some specializations of isa so that we don't have to include
// the subtype header files to test to see if the value is a subclass...
//
template <> struct isa_impl<Constant, Value> {
  static inline bool doit(const Value &Val) {
    static_assert(Value::ConstantFirstVal == 0, "Val.getValueID() >= Value::ConstantFirstVal");
    return Val.getValueID() <= Value::ConstantLastVal;
  }
};

template <> struct isa_impl<ConstantData, Value> {
  static inline bool doit(const Value &Val) {
    return Val.getValueID() >= Value::ConstantDataFirstVal &&
           Val.getValueID() <= Value::ConstantDataLastVal;
  }
};

template <> struct isa_impl<ConstantAggregate, Value> {
  static inline bool doit(const Value &Val) {
    return Val.getValueID() >= Value::ConstantAggregateFirstVal &&
           Val.getValueID() <= Value::ConstantAggregateLastVal;
  }
};

template <> struct isa_impl<Argument, Value> {
  static inline bool doit (const Value &Val) {
    return Val.getValueID() == Value::ArgumentVal;
  }
};

template <> struct isa_impl<InlineAsm, Value> {
  static inline bool doit(const Value &Val) {
    return Val.getValueID() == Value::InlineAsmVal;
  }
};

template <> struct isa_impl<Instruction, Value> {
  static inline bool doit(const Value &Val) {
    return Val.getValueID() >= Value::InstructionVal;
  }
};

template <> struct isa_impl<BasicBlock, Value> {
  static inline bool doit(const Value &Val) {
    return Val.getValueID() == Value::BasicBlockVal;
  }
};

template <> struct isa_impl<Function, Value> {
  static inline bool doit(const Value &Val) {
    return Val.getValueID() == Value::FunctionVal;
  }
};

template <> struct isa_impl<GlobalVariable, Value> {
  static inline bool doit(const Value &Val) {
    return Val.getValueID() == Value::GlobalVariableVal;
  }
};

template <> struct isa_impl<GlobalAlias, Value> {
  static inline bool doit(const Value &Val) {
    return Val.getValueID() == Value::GlobalAliasVal;
  }
};

template <> struct isa_impl<GlobalIFunc, Value> {
  static inline bool doit(const Value &Val) {
    return Val.getValueID() == Value::GlobalIFuncVal;
  }
};

template <> struct isa_impl<GlobalValue, Value> {
  static inline bool doit(const Value &Val) {
    return isa<GlobalObject>(Val) || isa<GlobalAlias>(Val);
  }
};

template <> struct isa_impl<GlobalObject, Value> {
  static inline bool doit(const Value &Val) {
    return isa<GlobalVariable>(Val) || isa<Function>(Val) ||
           isa<GlobalIFunc>(Val);
  }
};

// Create wrappers for C Binding types (see CBindingWrapping.h).
DEFINE_ISA_CONVERSION_FUNCTIONS(Value, LLVMValueRef)

// Specialized opaque value conversions.
inline Value **unwrap(LLVMValueRef *Vals) {
  return reinterpret_cast<Value**>(Vals);
}

template<typename T>
inline T **unwrap(LLVMValueRef *Vals, unsigned Length) {
#ifndef NDEBUG
  for (LLVMValueRef *I = Vals, *E = Vals + Length; I != E; ++I)
    unwrap<T>(*I); // For side effect of calling assert on invalid usage.
#endif
  (void)Length;
  return reinterpret_cast<T**>(Vals);
}

inline LLVMValueRef *wrap(const Value **Vals) {
  return reinterpret_cast<LLVMValueRef*>(const_cast<Value**>(Vals));
}

} // end namespace llvm

#endif // LLVM_IR_VALUE_H
