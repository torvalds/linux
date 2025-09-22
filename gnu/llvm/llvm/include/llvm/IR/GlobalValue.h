//===-- llvm/GlobalValue.h - Class to represent a global value --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a common base class of all globally definable objects.  As such,
// it is subclassed by GlobalVariable, GlobalAlias and by Function.  This is
// used because you can do certain things with these global objects that you
// can't do to anything else.  For example, use the address of one as a
// constant.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_GLOBALVALUE_H
#define LLVM_IR_GLOBALVALUE_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <cstdint>
#include <string>

namespace llvm {

class Comdat;
class ConstantRange;
class DataLayout;
class Error;
class GlobalObject;
class Module;

namespace Intrinsic {
typedef unsigned ID;
} // end namespace Intrinsic

// Choose ';' as the delimiter. ':' was used once but it doesn't work well for
// Objective-C functions which commonly have :'s in their names.
inline constexpr char GlobalIdentifierDelimiter = ';';

class GlobalValue : public Constant {
public:
  /// An enumeration for the kinds of linkage for global values.
  enum LinkageTypes {
    ExternalLinkage = 0,///< Externally visible function
    AvailableExternallyLinkage, ///< Available for inspection, not emission.
    LinkOnceAnyLinkage, ///< Keep one copy of function when linking (inline)
    LinkOnceODRLinkage, ///< Same, but only replaced by something equivalent.
    WeakAnyLinkage,     ///< Keep one copy of named function when linking (weak)
    WeakODRLinkage,     ///< Same, but only replaced by something equivalent.
    AppendingLinkage,   ///< Special purpose, only applies to global arrays
    InternalLinkage,    ///< Rename collisions when linking (static functions).
    PrivateLinkage,     ///< Like Internal, but omit from symbol table.
    ExternalWeakLinkage,///< ExternalWeak linkage description.
    CommonLinkage       ///< Tentative definitions.
  };

  /// An enumeration for the kinds of visibility of global values.
  enum VisibilityTypes {
    DefaultVisibility = 0,  ///< The GV is visible
    HiddenVisibility,       ///< The GV is hidden
    ProtectedVisibility     ///< The GV is protected
  };

  /// Storage classes of global values for PE targets.
  enum DLLStorageClassTypes {
    DefaultStorageClass   = 0,
    DLLImportStorageClass = 1, ///< Function to be imported from DLL
    DLLExportStorageClass = 2  ///< Function to be accessible from DLL.
  };

protected:
  GlobalValue(Type *Ty, ValueTy VTy, Use *Ops, unsigned NumOps,
              LinkageTypes Linkage, const Twine &Name, unsigned AddressSpace)
      : Constant(PointerType::get(Ty, AddressSpace), VTy, Ops, NumOps),
        ValueType(Ty), Visibility(DefaultVisibility),
        UnnamedAddrVal(unsigned(UnnamedAddr::None)),
        DllStorageClass(DefaultStorageClass), ThreadLocal(NotThreadLocal),
        HasLLVMReservedName(false), IsDSOLocal(false), HasPartition(false),
        HasSanitizerMetadata(false) {
    setLinkage(Linkage);
    setName(Name);
  }

  Type *ValueType;

  static const unsigned GlobalValueSubClassDataBits = 15;

  // All bitfields use unsigned as the underlying type so that MSVC will pack
  // them.
  unsigned Linkage : 4;       // The linkage of this global
  unsigned Visibility : 2;    // The visibility style of this global
  unsigned UnnamedAddrVal : 2; // This value's address is not significant
  unsigned DllStorageClass : 2; // DLL storage class

  unsigned ThreadLocal : 3; // Is this symbol "Thread Local", if so, what is
                            // the desired model?

  /// True if the function's name starts with "llvm.".  This corresponds to the
  /// value of Function::isIntrinsic(), which may be true even if
  /// Function::intrinsicID() returns Intrinsic::not_intrinsic.
  unsigned HasLLVMReservedName : 1;

  /// If true then there is a definition within the same linkage unit and that
  /// definition cannot be runtime preempted.
  unsigned IsDSOLocal : 1;

  /// True if this symbol has a partition name assigned (see
  /// https://lld.llvm.org/Partitions.html).
  unsigned HasPartition : 1;

  /// True if this symbol has sanitizer metadata available. Should only happen
  /// if sanitizers were enabled when building the translation unit which
  /// contains this GV.
  unsigned HasSanitizerMetadata : 1;

private:
  // Give subclasses access to what otherwise would be wasted padding.
  // (15 + 4 + 2 + 2 + 2 + 3 + 1 + 1 + 1 + 1) == 32.
  unsigned SubClassData : GlobalValueSubClassDataBits;

  friend class Constant;

  void destroyConstantImpl();
  Value *handleOperandChangeImpl(Value *From, Value *To);

  /// Returns true if the definition of this global may be replaced by a
  /// differently optimized variant of the same source level function at link
  /// time.
  bool mayBeDerefined() const {
    switch (getLinkage()) {
    case WeakODRLinkage:
    case LinkOnceODRLinkage:
    case AvailableExternallyLinkage:
      return true;

    case WeakAnyLinkage:
    case LinkOnceAnyLinkage:
    case CommonLinkage:
    case ExternalWeakLinkage:
    case ExternalLinkage:
    case AppendingLinkage:
    case InternalLinkage:
    case PrivateLinkage:
      // Optimizations may assume builtin semantics for functions defined as
      // nobuiltin due to attributes at call-sites. To avoid applying IPO based
      // on nobuiltin semantics, treat such function definitions as maybe
      // derefined.
      return isInterposable() || isNobuiltinFnDef();
    }

    llvm_unreachable("Fully covered switch above!");
  }

  /// Returns true if the global is a function definition with the nobuiltin
  /// attribute.
  bool isNobuiltinFnDef() const;

protected:
  /// The intrinsic ID for this subclass (which must be a Function).
  ///
  /// This member is defined by this class, but not used for anything.
  /// Subclasses can use it to store their intrinsic ID, if they have one.
  ///
  /// This is stored here to save space in Function on 64-bit hosts.
  Intrinsic::ID IntID = (Intrinsic::ID)0U;

  unsigned getGlobalValueSubClassData() const {
    return SubClassData;
  }
  void setGlobalValueSubClassData(unsigned V) {
    assert(V < (1 << GlobalValueSubClassDataBits) && "It will not fit");
    SubClassData = V;
  }

  Module *Parent = nullptr; // The containing module.

  // Used by SymbolTableListTraits.
  void setParent(Module *parent) {
    Parent = parent;
  }

  ~GlobalValue() {
    removeDeadConstantUsers();   // remove any dead constants using this.
  }

public:
  enum ThreadLocalMode {
    NotThreadLocal = 0,
    GeneralDynamicTLSModel,
    LocalDynamicTLSModel,
    InitialExecTLSModel,
    LocalExecTLSModel
  };

  GlobalValue(const GlobalValue &) = delete;

  unsigned getAddressSpace() const {
    return getType()->getAddressSpace();
  }

  enum class UnnamedAddr {
    None,
    Local,
    Global,
  };

  bool hasGlobalUnnamedAddr() const {
    return getUnnamedAddr() == UnnamedAddr::Global;
  }

  /// Returns true if this value's address is not significant in this module.
  /// This attribute is intended to be used only by the code generator and LTO
  /// to allow the linker to decide whether the global needs to be in the symbol
  /// table. It should probably not be used in optimizations, as the value may
  /// have uses outside the module; use hasGlobalUnnamedAddr() instead.
  bool hasAtLeastLocalUnnamedAddr() const {
    return getUnnamedAddr() != UnnamedAddr::None;
  }

  UnnamedAddr getUnnamedAddr() const {
    return UnnamedAddr(UnnamedAddrVal);
  }
  void setUnnamedAddr(UnnamedAddr Val) { UnnamedAddrVal = unsigned(Val); }

  static UnnamedAddr getMinUnnamedAddr(UnnamedAddr A, UnnamedAddr B) {
    if (A == UnnamedAddr::None || B == UnnamedAddr::None)
      return UnnamedAddr::None;
    if (A == UnnamedAddr::Local || B == UnnamedAddr::Local)
      return UnnamedAddr::Local;
    return UnnamedAddr::Global;
  }

  bool hasComdat() const { return getComdat() != nullptr; }
  const Comdat *getComdat() const;
  Comdat *getComdat() {
    return const_cast<Comdat *>(
                           static_cast<const GlobalValue *>(this)->getComdat());
  }

  VisibilityTypes getVisibility() const { return VisibilityTypes(Visibility); }
  bool hasDefaultVisibility() const { return Visibility == DefaultVisibility; }
  bool hasHiddenVisibility() const { return Visibility == HiddenVisibility; }
  bool hasProtectedVisibility() const {
    return Visibility == ProtectedVisibility;
  }
  void setVisibility(VisibilityTypes V) {
    assert((!hasLocalLinkage() || V == DefaultVisibility) &&
           "local linkage requires default visibility");
    Visibility = V;
    if (isImplicitDSOLocal())
      setDSOLocal(true);
  }

  /// If the value is "Thread Local", its value isn't shared by the threads.
  bool isThreadLocal() const { return getThreadLocalMode() != NotThreadLocal; }
  void setThreadLocal(bool Val) {
    setThreadLocalMode(Val ? GeneralDynamicTLSModel : NotThreadLocal);
  }
  void setThreadLocalMode(ThreadLocalMode Val) {
    assert(Val == NotThreadLocal || getValueID() != Value::FunctionVal);
    ThreadLocal = Val;
  }
  ThreadLocalMode getThreadLocalMode() const {
    return static_cast<ThreadLocalMode>(ThreadLocal);
  }

  DLLStorageClassTypes getDLLStorageClass() const {
    return DLLStorageClassTypes(DllStorageClass);
  }
  bool hasDLLImportStorageClass() const {
    return DllStorageClass == DLLImportStorageClass;
  }
  bool hasDLLExportStorageClass() const {
    return DllStorageClass == DLLExportStorageClass;
  }
  void setDLLStorageClass(DLLStorageClassTypes C) {
    assert((!hasLocalLinkage() || C == DefaultStorageClass) &&
           "local linkage requires DefaultStorageClass");
    DllStorageClass = C;
  }

  bool hasSection() const { return !getSection().empty(); }
  StringRef getSection() const;

  /// Global values are always pointers.
  PointerType *getType() const { return cast<PointerType>(User::getType()); }

  Type *getValueType() const { return ValueType; }

  bool isImplicitDSOLocal() const {
    return hasLocalLinkage() ||
           (!hasDefaultVisibility() && !hasExternalWeakLinkage());
  }

  void setDSOLocal(bool Local) { IsDSOLocal = Local; }

  bool isDSOLocal() const {
    return IsDSOLocal;
  }

  bool hasPartition() const {
    return HasPartition;
  }
  StringRef getPartition() const;
  void setPartition(StringRef Part);

  // ASan, HWASan and Memtag sanitizers have some instrumentation that applies
  // specifically to global variables.
  struct SanitizerMetadata {
    SanitizerMetadata()
        : NoAddress(false), NoHWAddress(false),
          Memtag(false), IsDynInit(false) {}
    // For ASan and HWASan, this instrumentation is implicitly applied to all
    // global variables when built with -fsanitize=*. What we need is a way to
    // persist the information that a certain global variable should *not* have
    // sanitizers applied, which occurs if:
    //   1. The global variable is in the sanitizer ignore list, or
    //   2. The global variable is created by the sanitizers itself for internal
    //      usage, or
    //   3. The global variable has __attribute__((no_sanitize("..."))) or
    //      __attribute__((disable_sanitizer_instrumentation)).
    //
    // This is important, a some IR passes like GlobalMerge can delete global
    // variables and replace them with new ones. If the old variables were
    // marked to be unsanitized, then the new ones should also be.
    unsigned NoAddress : 1;
    unsigned NoHWAddress : 1;

    // Memtag sanitization works differently: sanitization is requested by clang
    // when `-fsanitize=memtag-globals` is provided, and the request can be
    // denied (and the attribute removed) by the AArch64 global tagging pass if
    // it can't be fulfilled (e.g. the global variable is a TLS variable).
    // Memtag sanitization has to interact with other parts of LLVM (like
    // supressing certain optimisations, emitting assembly directives, or
    // creating special relocation sections).
    //
    // Use `GlobalValue::isTagged()` to check whether tagging should be enabled
    // for a global variable.
    unsigned Memtag : 1;

    // ASan-specific metadata. Is this global variable dynamically initialized
    // (from a C++ language perspective), and should therefore be checked for
    // ODR violations.
    unsigned IsDynInit : 1;
  };

  bool hasSanitizerMetadata() const { return HasSanitizerMetadata; }
  const SanitizerMetadata &getSanitizerMetadata() const;
  // Note: Not byref as it's a POD and otherwise it's too easy to call
  // G.setSanitizerMetadata(G2.getSanitizerMetadata()), and the argument becomes
  // dangling when the backing storage allocates the metadata for `G`, as the
  // storage is shared between `G1` and `G2`.
  void setSanitizerMetadata(SanitizerMetadata Meta);
  void removeSanitizerMetadata();
  void setNoSanitizeMetadata();

  bool isTagged() const {
    return hasSanitizerMetadata() && getSanitizerMetadata().Memtag;
  }

  static LinkageTypes getLinkOnceLinkage(bool ODR) {
    return ODR ? LinkOnceODRLinkage : LinkOnceAnyLinkage;
  }
  static LinkageTypes getWeakLinkage(bool ODR) {
    return ODR ? WeakODRLinkage : WeakAnyLinkage;
  }

  static bool isExternalLinkage(LinkageTypes Linkage) {
    return Linkage == ExternalLinkage;
  }
  static bool isAvailableExternallyLinkage(LinkageTypes Linkage) {
    return Linkage == AvailableExternallyLinkage;
  }
  static bool isLinkOnceAnyLinkage(LinkageTypes Linkage) {
    return Linkage == LinkOnceAnyLinkage;
  }
  static bool isLinkOnceODRLinkage(LinkageTypes Linkage) {
    return Linkage == LinkOnceODRLinkage;
  }
  static bool isLinkOnceLinkage(LinkageTypes Linkage) {
    return isLinkOnceAnyLinkage(Linkage) || isLinkOnceODRLinkage(Linkage);
  }
  static bool isWeakAnyLinkage(LinkageTypes Linkage) {
    return Linkage == WeakAnyLinkage;
  }
  static bool isWeakODRLinkage(LinkageTypes Linkage) {
    return Linkage == WeakODRLinkage;
  }
  static bool isWeakLinkage(LinkageTypes Linkage) {
    return isWeakAnyLinkage(Linkage) || isWeakODRLinkage(Linkage);
  }
  static bool isAppendingLinkage(LinkageTypes Linkage) {
    return Linkage == AppendingLinkage;
  }
  static bool isInternalLinkage(LinkageTypes Linkage) {
    return Linkage == InternalLinkage;
  }
  static bool isPrivateLinkage(LinkageTypes Linkage) {
    return Linkage == PrivateLinkage;
  }
  static bool isLocalLinkage(LinkageTypes Linkage) {
    return isInternalLinkage(Linkage) || isPrivateLinkage(Linkage);
  }
  static bool isExternalWeakLinkage(LinkageTypes Linkage) {
    return Linkage == ExternalWeakLinkage;
  }
  static bool isCommonLinkage(LinkageTypes Linkage) {
    return Linkage == CommonLinkage;
  }
  static bool isValidDeclarationLinkage(LinkageTypes Linkage) {
    return isExternalWeakLinkage(Linkage) || isExternalLinkage(Linkage);
  }

  /// Whether the definition of this global may be replaced by something
  /// non-equivalent at link time. For example, if a function has weak linkage
  /// then the code defining it may be replaced by different code.
  static bool isInterposableLinkage(LinkageTypes Linkage) {
    switch (Linkage) {
    case WeakAnyLinkage:
    case LinkOnceAnyLinkage:
    case CommonLinkage:
    case ExternalWeakLinkage:
      return true;

    case AvailableExternallyLinkage:
    case LinkOnceODRLinkage:
    case WeakODRLinkage:
    // The above three cannot be overridden but can be de-refined.

    case ExternalLinkage:
    case AppendingLinkage:
    case InternalLinkage:
    case PrivateLinkage:
      return false;
    }
    llvm_unreachable("Fully covered switch above!");
  }

  /// Whether the definition of this global may be discarded if it is not used
  /// in its compilation unit.
  static bool isDiscardableIfUnused(LinkageTypes Linkage) {
    return isLinkOnceLinkage(Linkage) || isLocalLinkage(Linkage) ||
           isAvailableExternallyLinkage(Linkage);
  }

  /// Whether the definition of this global may be replaced at link time.  NB:
  /// Using this method outside of the code generators is almost always a
  /// mistake: when working at the IR level use isInterposable instead as it
  /// knows about ODR semantics.
  static bool isWeakForLinker(LinkageTypes Linkage)  {
    return Linkage == WeakAnyLinkage || Linkage == WeakODRLinkage ||
           Linkage == LinkOnceAnyLinkage || Linkage == LinkOnceODRLinkage ||
           Linkage == CommonLinkage || Linkage == ExternalWeakLinkage;
  }

  /// Return true if the currently visible definition of this global (if any) is
  /// exactly the definition we will see at runtime.
  ///
  /// Non-exact linkage types inhibits most non-inlining IPO, since a
  /// differently optimized variant of the same function can have different
  /// observable or undefined behavior than in the variant currently visible.
  /// For instance, we could have started with
  ///
  ///   void foo(int *v) {
  ///     int t = 5 / v[0];
  ///     (void) t;
  ///   }
  ///
  /// and "refined" it to
  ///
  ///   void foo(int *v) { }
  ///
  /// However, we cannot infer readnone for `foo`, since that would justify
  /// DSE'ing a store to `v[0]` across a call to `foo`, which can cause
  /// undefined behavior if the linker replaces the actual call destination with
  /// the unoptimized `foo`.
  ///
  /// Inlining is okay across non-exact linkage types as long as they're not
  /// interposable (see \c isInterposable), since in such cases the currently
  /// visible variant is *a* correct implementation of the original source
  /// function; it just isn't the *only* correct implementation.
  bool isDefinitionExact() const {
    return !mayBeDerefined();
  }

  /// Return true if this global has an exact defintion.
  bool hasExactDefinition() const {
    // While this computes exactly the same thing as
    // isStrongDefinitionForLinker, the intended uses are different.  This
    // function is intended to help decide if specific inter-procedural
    // transforms are correct, while isStrongDefinitionForLinker's intended use
    // is in low level code generation.
    return !isDeclaration() && isDefinitionExact();
  }

  /// Return true if this global's definition can be substituted with an
  /// *arbitrary* definition at link time or load time. We cannot do any IPO or
  /// inlining across interposable call edges, since the callee can be
  /// replaced with something arbitrary.
  bool isInterposable() const;
  bool canBenefitFromLocalAlias() const;

  bool hasExternalLinkage() const { return isExternalLinkage(getLinkage()); }
  bool hasAvailableExternallyLinkage() const {
    return isAvailableExternallyLinkage(getLinkage());
  }
  bool hasLinkOnceLinkage() const { return isLinkOnceLinkage(getLinkage()); }
  bool hasLinkOnceAnyLinkage() const {
    return isLinkOnceAnyLinkage(getLinkage());
  }
  bool hasLinkOnceODRLinkage() const {
    return isLinkOnceODRLinkage(getLinkage());
  }
  bool hasWeakLinkage() const { return isWeakLinkage(getLinkage()); }
  bool hasWeakAnyLinkage() const { return isWeakAnyLinkage(getLinkage()); }
  bool hasWeakODRLinkage() const { return isWeakODRLinkage(getLinkage()); }
  bool hasAppendingLinkage() const { return isAppendingLinkage(getLinkage()); }
  bool hasInternalLinkage() const { return isInternalLinkage(getLinkage()); }
  bool hasPrivateLinkage() const { return isPrivateLinkage(getLinkage()); }
  bool hasLocalLinkage() const { return isLocalLinkage(getLinkage()); }
  bool hasExternalWeakLinkage() const {
    return isExternalWeakLinkage(getLinkage());
  }
  bool hasCommonLinkage() const { return isCommonLinkage(getLinkage()); }
  bool hasValidDeclarationLinkage() const {
    return isValidDeclarationLinkage(getLinkage());
  }

  void setLinkage(LinkageTypes LT) {
    if (isLocalLinkage(LT)) {
      Visibility = DefaultVisibility;
      DllStorageClass = DefaultStorageClass;
    }
    Linkage = LT;
    if (isImplicitDSOLocal())
      setDSOLocal(true);
  }
  LinkageTypes getLinkage() const { return LinkageTypes(Linkage); }

  bool isDiscardableIfUnused() const {
    return isDiscardableIfUnused(getLinkage());
  }

  bool isWeakForLinker() const { return isWeakForLinker(getLinkage()); }

protected:
  /// Copy all additional attributes (those not needed to create a GlobalValue)
  /// from the GlobalValue Src to this one.
  void copyAttributesFrom(const GlobalValue *Src);

public:
  /// If the given string begins with the GlobalValue name mangling escape
  /// character '\1', drop it.
  ///
  /// This function applies a specific mangling that is used in PGO profiles,
  /// among other things. If you're trying to get a symbol name for an
  /// arbitrary GlobalValue, this is not the function you're looking for; see
  /// Mangler.h.
  static StringRef dropLLVMManglingEscape(StringRef Name) {
    Name.consume_front("\1");
    return Name;
  }

  /// Return the modified name for a global value suitable to be
  /// used as the key for a global lookup (e.g. profile or ThinLTO).
  /// The value's original name is \c Name and has linkage of type
  /// \c Linkage. The value is defined in module \c FileName.
  static std::string getGlobalIdentifier(StringRef Name,
                                         GlobalValue::LinkageTypes Linkage,
                                         StringRef FileName);

  /// Return the modified name for this global value suitable to be
  /// used as the key for a global lookup (e.g. profile or ThinLTO).
  std::string getGlobalIdentifier() const;

  /// Declare a type to represent a global unique identifier for a global value.
  /// This is a 64 bits hash that is used by PGO and ThinLTO to have a compact
  /// unique way to identify a symbol.
  using GUID = uint64_t;

  /// Return a 64-bit global unique ID constructed from global value name
  /// (i.e. returned by getGlobalIdentifier()).
  static GUID getGUID(StringRef GlobalName);

  /// Return a 64-bit global unique ID constructed from global value name
  /// (i.e. returned by getGlobalIdentifier()).
  GUID getGUID() const { return getGUID(getGlobalIdentifier()); }

  /// @name Materialization
  /// Materialization is used to construct functions only as they're needed.
  /// This
  /// is useful to reduce memory usage in LLVM or parsing work done by the
  /// BitcodeReader to load the Module.
  /// @{

  /// If this function's Module is being lazily streamed in functions from disk
  /// or some other source, this method can be used to check to see if the
  /// function has been read in yet or not.
  bool isMaterializable() const;

  /// Make sure this GlobalValue is fully read.
  Error materialize();

/// @}

  /// Return true if the primary definition of this global value is outside of
  /// the current translation unit.
  bool isDeclaration() const;

  bool isDeclarationForLinker() const {
    if (hasAvailableExternallyLinkage())
      return true;

    return isDeclaration();
  }

  /// Returns true if this global's definition will be the one chosen by the
  /// linker.
  ///
  /// NB! Ideally this should not be used at the IR level at all.  If you're
  /// interested in optimization constraints implied by the linker's ability to
  /// choose an implementation, prefer using \c hasExactDefinition.
  bool isStrongDefinitionForLinker() const {
    return !(isDeclarationForLinker() || isWeakForLinker());
  }

  const GlobalObject *getAliaseeObject() const;
  GlobalObject *getAliaseeObject() {
    return const_cast<GlobalObject *>(
        static_cast<const GlobalValue *>(this)->getAliaseeObject());
  }

  /// Returns whether this is a reference to an absolute symbol.
  bool isAbsoluteSymbolRef() const;

  /// If this is an absolute symbol reference, returns the range of the symbol,
  /// otherwise returns std::nullopt.
  std::optional<ConstantRange> getAbsoluteSymbolRange() const;

  /// This method unlinks 'this' from the containing module, but does not delete
  /// it.
  void removeFromParent();

  /// This method unlinks 'this' from the containing module and deletes it.
  void eraseFromParent();

  /// Get the module that this global value is contained inside of...
  Module *getParent() { return Parent; }
  const Module *getParent() const { return Parent; }

  /// Get the data layout of the module this global belongs to.
  ///
  /// Requires the global to have a parent module.
  const DataLayout &getDataLayout() const;

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Value *V) {
    return V->getValueID() == Value::FunctionVal ||
           V->getValueID() == Value::GlobalVariableVal ||
           V->getValueID() == Value::GlobalAliasVal ||
           V->getValueID() == Value::GlobalIFuncVal;
  }

  /// True if GV can be left out of the object symbol table. This is the case
  /// for linkonce_odr values whose address is not significant. While legal, it
  /// is not normally profitable to omit them from the .o symbol table. Using
  /// this analysis makes sense when the information can be passed down to the
  /// linker or we are in LTO.
  bool canBeOmittedFromSymbolTable() const;
};

} // end namespace llvm

#endif // LLVM_IR_GLOBALVALUE_H
