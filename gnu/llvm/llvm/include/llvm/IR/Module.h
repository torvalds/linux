//===- llvm/Module.h - C++ class to represent a VM module -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// @file
/// Module.h This file contains the declarations for the Module class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_MODULE_H
#define LLVM_IR_MODULE_H

#include "llvm-c/Types.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Comdat.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalIFunc.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/ProfileSummary.h"
#include "llvm/IR/SymbolTableListTraits.h"
#include "llvm/Support/CBindingWrapping.h"
#include "llvm/Support/CodeGen.h"
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace llvm {

class Error;
class FunctionType;
class GVMaterializer;
class LLVMContext;
class MemoryBuffer;
class ModuleSummaryIndex;
class RandomNumberGenerator;
class StructType;
class VersionTuple;

/// A Module instance is used to store all the information related to an
/// LLVM module. Modules are the top level container of all other LLVM
/// Intermediate Representation (IR) objects. Each module directly contains a
/// list of globals variables, a list of functions, a list of libraries (or
/// other modules) this module depends on, a symbol table, and various data
/// about the target's characteristics.
///
/// A module maintains a GlobalList object that is used to hold all
/// constant references to global variables in the module.  When a global
/// variable is destroyed, it should have no entries in the GlobalList.
/// The main container class for the LLVM Intermediate Representation.
class LLVM_EXTERNAL_VISIBILITY Module {
  /// @name Types And Enumerations
  /// @{
public:
  /// The type for the list of global variables.
  using GlobalListType = SymbolTableList<GlobalVariable>;
  /// The type for the list of functions.
  using FunctionListType = SymbolTableList<Function>;
  /// The type for the list of aliases.
  using AliasListType = SymbolTableList<GlobalAlias>;
  /// The type for the list of ifuncs.
  using IFuncListType = SymbolTableList<GlobalIFunc>;
  /// The type for the list of named metadata.
  using NamedMDListType = ilist<NamedMDNode>;
  /// The type of the comdat "symbol" table.
  using ComdatSymTabType = StringMap<Comdat>;
  /// The type for mapping names to named metadata.
  using NamedMDSymTabType = StringMap<NamedMDNode *>;

  /// The Global Variable iterator.
  using global_iterator = GlobalListType::iterator;
  /// The Global Variable constant iterator.
  using const_global_iterator = GlobalListType::const_iterator;

  /// The Function iterators.
  using iterator = FunctionListType::iterator;
  /// The Function constant iterator
  using const_iterator = FunctionListType::const_iterator;

  /// The Function reverse iterator.
  using reverse_iterator = FunctionListType::reverse_iterator;
  /// The Function constant reverse iterator.
  using const_reverse_iterator = FunctionListType::const_reverse_iterator;

  /// The Global Alias iterators.
  using alias_iterator = AliasListType::iterator;
  /// The Global Alias constant iterator
  using const_alias_iterator = AliasListType::const_iterator;

  /// The Global IFunc iterators.
  using ifunc_iterator = IFuncListType::iterator;
  /// The Global IFunc constant iterator
  using const_ifunc_iterator = IFuncListType::const_iterator;

  /// The named metadata iterators.
  using named_metadata_iterator = NamedMDListType::iterator;
  /// The named metadata constant iterators.
  using const_named_metadata_iterator = NamedMDListType::const_iterator;

  /// This enumeration defines the supported behaviors of module flags.
  enum ModFlagBehavior {
    /// Emits an error if two values disagree, otherwise the resulting value is
    /// that of the operands.
    Error = 1,

    /// Emits a warning if two values disagree. The result value will be the
    /// operand for the flag from the first module being linked.
    Warning = 2,

    /// Adds a requirement that another module flag be present and have a
    /// specified value after linking is performed. The value must be a metadata
    /// pair, where the first element of the pair is the ID of the module flag
    /// to be restricted, and the second element of the pair is the value the
    /// module flag should be restricted to. This behavior can be used to
    /// restrict the allowable results (via triggering of an error) of linking
    /// IDs with the **Override** behavior.
    Require = 3,

    /// Uses the specified value, regardless of the behavior or value of the
    /// other module. If both modules specify **Override**, but the values
    /// differ, an error will be emitted.
    Override = 4,

    /// Appends the two values, which are required to be metadata nodes.
    Append = 5,

    /// Appends the two values, which are required to be metadata
    /// nodes. However, duplicate entries in the second list are dropped
    /// during the append operation.
    AppendUnique = 6,

    /// Takes the max of the two values, which are required to be integers.
    Max = 7,

    /// Takes the min of the two values, which are required to be integers.
    Min = 8,

    // Markers:
    ModFlagBehaviorFirstVal = Error,
    ModFlagBehaviorLastVal = Min
  };

  /// Checks if Metadata represents a valid ModFlagBehavior, and stores the
  /// converted result in MFB.
  static bool isValidModFlagBehavior(Metadata *MD, ModFlagBehavior &MFB);

  /// Check if the given module flag metadata represents a valid module flag,
  /// and store the flag behavior, the key string and the value metadata.
  static bool isValidModuleFlag(const MDNode &ModFlag, ModFlagBehavior &MFB,
                                MDString *&Key, Metadata *&Val);

  struct ModuleFlagEntry {
    ModFlagBehavior Behavior;
    MDString *Key;
    Metadata *Val;

    ModuleFlagEntry(ModFlagBehavior B, MDString *K, Metadata *V)
        : Behavior(B), Key(K), Val(V) {}
  };

/// @}
/// @name Member Variables
/// @{
private:
  LLVMContext &Context;           ///< The LLVMContext from which types and
                                  ///< constants are allocated.
  GlobalListType GlobalList;      ///< The Global Variables in the module
  FunctionListType FunctionList;  ///< The Functions in the module
  AliasListType AliasList;        ///< The Aliases in the module
  IFuncListType IFuncList;        ///< The IFuncs in the module
  NamedMDListType NamedMDList;    ///< The named metadata in the module
  std::string GlobalScopeAsm;     ///< Inline Asm at global scope.
  std::unique_ptr<ValueSymbolTable> ValSymTab; ///< Symbol table for values
  ComdatSymTabType ComdatSymTab;  ///< Symbol table for COMDATs
  std::unique_ptr<MemoryBuffer>
  OwnedMemoryBuffer;              ///< Memory buffer directly owned by this
                                  ///< module, for legacy clients only.
  std::unique_ptr<GVMaterializer>
  Materializer;                   ///< Used to materialize GlobalValues
  std::string ModuleID;           ///< Human readable identifier for the module
  std::string SourceFileName;     ///< Original source file name for module,
                                  ///< recorded in bitcode.
  std::string TargetTriple;       ///< Platform target triple Module compiled on
                                  ///< Format: (arch)(sub)-(vendor)-(sys0-(abi)
  NamedMDSymTabType NamedMDSymTab;  ///< NamedMDNode names.
  DataLayout DL;                  ///< DataLayout associated with the module
  StringMap<unsigned>
      CurrentIntrinsicIds; ///< Keep track of the current unique id count for
                           ///< the specified intrinsic basename.
  DenseMap<std::pair<Intrinsic::ID, const FunctionType *>, unsigned>
      UniquedIntrinsicNames; ///< Keep track of uniqued names of intrinsics
                             ///< based on unnamed types. The combination of
                             ///< ID and FunctionType maps to the extension that
                             ///< is used to make the intrinsic name unique.

  friend class Constant;

/// @}
/// @name Constructors
/// @{
public:
  /// Is this Module using intrinsics to record the position of debugging
  /// information, or non-intrinsic records? See IsNewDbgInfoFormat in
  /// \ref BasicBlock.
  bool IsNewDbgInfoFormat;

  /// Used when printing this module in the new debug info format; removes all
  /// declarations of debug intrinsics that are replaced by non-intrinsic
  /// records in the new format.
  void removeDebugIntrinsicDeclarations();

  /// \see BasicBlock::convertToNewDbgValues.
  void convertToNewDbgValues() {
    for (auto &F : *this) {
      F.convertToNewDbgValues();
    }
    IsNewDbgInfoFormat = true;
  }

  /// \see BasicBlock::convertFromNewDbgValues.
  void convertFromNewDbgValues() {
    for (auto &F : *this) {
      F.convertFromNewDbgValues();
    }
    IsNewDbgInfoFormat = false;
  }

  void setIsNewDbgInfoFormat(bool UseNewFormat) {
    if (UseNewFormat && !IsNewDbgInfoFormat)
      convertToNewDbgValues();
    else if (!UseNewFormat && IsNewDbgInfoFormat)
      convertFromNewDbgValues();
  }
  void setNewDbgInfoFormatFlag(bool NewFlag) {
    for (auto &F : *this) {
      F.setNewDbgInfoFormatFlag(NewFlag);
    }
    IsNewDbgInfoFormat = NewFlag;
  }

  /// The Module constructor. Note that there is no default constructor. You
  /// must provide a name for the module upon construction.
  explicit Module(StringRef ModuleID, LLVMContext& C);
  /// The module destructor. This will dropAllReferences.
  ~Module();

/// @}
/// @name Module Level Accessors
/// @{

  /// Get the module identifier which is, essentially, the name of the module.
  /// @returns the module identifier as a string
  const std::string &getModuleIdentifier() const { return ModuleID; }

  /// Returns the number of non-debug IR instructions in the module.
  /// This is equivalent to the sum of the IR instruction counts of each
  /// function contained in the module.
  unsigned getInstructionCount() const;

  /// Get the module's original source file name. When compiling from
  /// bitcode, this is taken from a bitcode record where it was recorded.
  /// For other compiles it is the same as the ModuleID, which would
  /// contain the source file name.
  const std::string &getSourceFileName() const { return SourceFileName; }

  /// Get a short "name" for the module.
  ///
  /// This is useful for debugging or logging. It is essentially a convenience
  /// wrapper around getModuleIdentifier().
  StringRef getName() const { return ModuleID; }

  /// Get the data layout string for the module's target platform. This is
  /// equivalent to getDataLayout()->getStringRepresentation().
  const std::string &getDataLayoutStr() const {
    return DL.getStringRepresentation();
  }

  /// Get the data layout for the module's target platform.
  const DataLayout &getDataLayout() const { return DL; }

  /// Get the target triple which is a string describing the target host.
  /// @returns a string containing the target triple.
  const std::string &getTargetTriple() const { return TargetTriple; }

  /// Get the global data context.
  /// @returns LLVMContext - a container for LLVM's global information
  LLVMContext &getContext() const { return Context; }

  /// Get any module-scope inline assembly blocks.
  /// @returns a string containing the module-scope inline assembly blocks.
  const std::string &getModuleInlineAsm() const { return GlobalScopeAsm; }

  /// Get a RandomNumberGenerator salted for use with this module. The
  /// RNG can be seeded via -rng-seed=<uint64> and is salted with the
  /// ModuleID and the provided pass salt. The returned RNG should not
  /// be shared across threads or passes.
  ///
  /// A unique RNG per pass ensures a reproducible random stream even
  /// when other randomness consuming passes are added or removed. In
  /// addition, the random stream will be reproducible across LLVM
  /// versions when the pass does not change.
  std::unique_ptr<RandomNumberGenerator> createRNG(const StringRef Name) const;

  /// Return true if size-info optimization remark is enabled, false
  /// otherwise.
  bool shouldEmitInstrCountChangedRemark() {
    return getContext().getDiagHandlerPtr()->isAnalysisRemarkEnabled(
        "size-info");
  }

  /// @}
  /// @name Module Level Mutators
  /// @{

  /// Set the module identifier.
  void setModuleIdentifier(StringRef ID) { ModuleID = std::string(ID); }

  /// Set the module's original source file name.
  void setSourceFileName(StringRef Name) { SourceFileName = std::string(Name); }

  /// Set the data layout
  void setDataLayout(StringRef Desc);
  void setDataLayout(const DataLayout &Other);

  /// Set the target triple.
  void setTargetTriple(StringRef T) { TargetTriple = std::string(T); }

  /// Set the module-scope inline assembly blocks.
  /// A trailing newline is added if the input doesn't have one.
  void setModuleInlineAsm(StringRef Asm) {
    GlobalScopeAsm = std::string(Asm);
    if (!GlobalScopeAsm.empty() && GlobalScopeAsm.back() != '\n')
      GlobalScopeAsm += '\n';
  }

  /// Append to the module-scope inline assembly blocks.
  /// A trailing newline is added if the input doesn't have one.
  void appendModuleInlineAsm(StringRef Asm) {
    GlobalScopeAsm += Asm;
    if (!GlobalScopeAsm.empty() && GlobalScopeAsm.back() != '\n')
      GlobalScopeAsm += '\n';
  }

/// @}
/// @name Generic Value Accessors
/// @{

  /// Return the global value in the module with the specified name, of
  /// arbitrary type. This method returns null if a global with the specified
  /// name is not found.
  GlobalValue *getNamedValue(StringRef Name) const;

  /// Return the number of global values in the module.
  unsigned getNumNamedValues() const;

  /// Return a unique non-zero ID for the specified metadata kind. This ID is
  /// uniqued across modules in the current LLVMContext.
  unsigned getMDKindID(StringRef Name) const;

  /// Populate client supplied SmallVector with the name for custom metadata IDs
  /// registered in this LLVMContext.
  void getMDKindNames(SmallVectorImpl<StringRef> &Result) const;

  /// Populate client supplied SmallVector with the bundle tags registered in
  /// this LLVMContext.  The bundle tags are ordered by increasing bundle IDs.
  /// \see LLVMContext::getOperandBundleTagID
  void getOperandBundleTags(SmallVectorImpl<StringRef> &Result) const;

  std::vector<StructType *> getIdentifiedStructTypes() const;

  /// Return a unique name for an intrinsic whose mangling is based on an
  /// unnamed type. The Proto represents the function prototype.
  std::string getUniqueIntrinsicName(StringRef BaseName, Intrinsic::ID Id,
                                     const FunctionType *Proto);

/// @}
/// @name Function Accessors
/// @{

  /// Look up the specified function in the module symbol table. If it does not
  /// exist, add a prototype for the function and return it. Otherwise, return
  /// the existing function.
  ///
  /// In all cases, the returned value is a FunctionCallee wrapper around the
  /// 'FunctionType *T' passed in, as well as the 'Value*' of the Function. The
  /// function type of the function may differ from the function type stored in
  /// FunctionCallee if it was previously created with a different type.
  ///
  /// Note: For library calls getOrInsertLibFunc() should be used instead.
  FunctionCallee getOrInsertFunction(StringRef Name, FunctionType *T,
                                     AttributeList AttributeList);

  FunctionCallee getOrInsertFunction(StringRef Name, FunctionType *T);

  /// Same as above, but takes a list of function arguments, which makes it
  /// easier for clients to use.
  template <typename... ArgsTy>
  FunctionCallee getOrInsertFunction(StringRef Name,
                                     AttributeList AttributeList, Type *RetTy,
                                     ArgsTy... Args) {
    SmallVector<Type*, sizeof...(ArgsTy)> ArgTys{Args...};
    return getOrInsertFunction(Name,
                               FunctionType::get(RetTy, ArgTys, false),
                               AttributeList);
  }

  /// Same as above, but without the attributes.
  template <typename... ArgsTy>
  FunctionCallee getOrInsertFunction(StringRef Name, Type *RetTy,
                                     ArgsTy... Args) {
    return getOrInsertFunction(Name, AttributeList{}, RetTy, Args...);
  }

  // Avoid an incorrect ordering that'd otherwise compile incorrectly.
  template <typename... ArgsTy>
  FunctionCallee
  getOrInsertFunction(StringRef Name, AttributeList AttributeList,
                      FunctionType *Invalid, ArgsTy... Args) = delete;

  /// Look up the specified function in the module symbol table. If it does not
  /// exist, return null.
  Function *getFunction(StringRef Name) const;

/// @}
/// @name Global Variable Accessors
/// @{

  /// Look up the specified global variable in the module symbol table. If it
  /// does not exist, return null. If AllowInternal is set to true, this
  /// function will return types that have InternalLinkage. By default, these
  /// types are not returned.
  GlobalVariable *getGlobalVariable(StringRef Name) const {
    return getGlobalVariable(Name, false);
  }

  GlobalVariable *getGlobalVariable(StringRef Name, bool AllowInternal) const;

  GlobalVariable *getGlobalVariable(StringRef Name,
                                    bool AllowInternal = false) {
    return static_cast<const Module *>(this)->getGlobalVariable(Name,
                                                                AllowInternal);
  }

  /// Return the global variable in the module with the specified name, of
  /// arbitrary type. This method returns null if a global with the specified
  /// name is not found.
  const GlobalVariable *getNamedGlobal(StringRef Name) const {
    return getGlobalVariable(Name, true);
  }
  GlobalVariable *getNamedGlobal(StringRef Name) {
    return const_cast<GlobalVariable *>(
                       static_cast<const Module *>(this)->getNamedGlobal(Name));
  }

  /// Look up the specified global in the module symbol table.
  /// If it does not exist, invoke a callback to create a declaration of the
  /// global and return it. The global is constantexpr casted to the expected
  /// type if necessary.
  Constant *
  getOrInsertGlobal(StringRef Name, Type *Ty,
                    function_ref<GlobalVariable *()> CreateGlobalCallback);

  /// Look up the specified global in the module symbol table. If required, this
  /// overload constructs the global variable using its constructor's defaults.
  Constant *getOrInsertGlobal(StringRef Name, Type *Ty);

/// @}
/// @name Global Alias Accessors
/// @{

  /// Return the global alias in the module with the specified name, of
  /// arbitrary type. This method returns null if a global with the specified
  /// name is not found.
  GlobalAlias *getNamedAlias(StringRef Name) const;

/// @}
/// @name Global IFunc Accessors
/// @{

  /// Return the global ifunc in the module with the specified name, of
  /// arbitrary type. This method returns null if a global with the specified
  /// name is not found.
  GlobalIFunc *getNamedIFunc(StringRef Name) const;

/// @}
/// @name Named Metadata Accessors
/// @{

  /// Return the first NamedMDNode in the module with the specified name. This
  /// method returns null if a NamedMDNode with the specified name is not found.
  NamedMDNode *getNamedMetadata(const Twine &Name) const;

  /// Return the named MDNode in the module with the specified name. This method
  /// returns a new NamedMDNode if a NamedMDNode with the specified name is not
  /// found.
  NamedMDNode *getOrInsertNamedMetadata(StringRef Name);

  /// Remove the given NamedMDNode from this module and delete it.
  void eraseNamedMetadata(NamedMDNode *NMD);

/// @}
/// @name Comdat Accessors
/// @{

  /// Return the Comdat in the module with the specified name. It is created
  /// if it didn't already exist.
  Comdat *getOrInsertComdat(StringRef Name);

/// @}
/// @name Module Flags Accessors
/// @{

  /// Returns the module flags in the provided vector.
  void getModuleFlagsMetadata(SmallVectorImpl<ModuleFlagEntry> &Flags) const;

  /// Return the corresponding value if Key appears in module flags, otherwise
  /// return null.
  Metadata *getModuleFlag(StringRef Key) const;

  /// Returns the NamedMDNode in the module that represents module-level flags.
  /// This method returns null if there are no module-level flags.
  NamedMDNode *getModuleFlagsMetadata() const;

  /// Returns the NamedMDNode in the module that represents module-level flags.
  /// If module-level flags aren't found, it creates the named metadata that
  /// contains them.
  NamedMDNode *getOrInsertModuleFlagsMetadata();

  /// Add a module-level flag to the module-level flags metadata. It will create
  /// the module-level flags named metadata if it doesn't already exist.
  void addModuleFlag(ModFlagBehavior Behavior, StringRef Key, Metadata *Val);
  void addModuleFlag(ModFlagBehavior Behavior, StringRef Key, Constant *Val);
  void addModuleFlag(ModFlagBehavior Behavior, StringRef Key, uint32_t Val);
  void addModuleFlag(MDNode *Node);
  /// Like addModuleFlag but replaces the old module flag if it already exists.
  void setModuleFlag(ModFlagBehavior Behavior, StringRef Key, Metadata *Val);
  void setModuleFlag(ModFlagBehavior Behavior, StringRef Key, Constant *Val);
  void setModuleFlag(ModFlagBehavior Behavior, StringRef Key, uint32_t Val);

  /// @}
  /// @name Materialization
  /// @{

  /// Sets the GVMaterializer to GVM. This module must not yet have a
  /// Materializer. To reset the materializer for a module that already has one,
  /// call materializeAll first. Destroying this module will destroy
  /// its materializer without materializing any more GlobalValues. Without
  /// destroying the Module, there is no way to detach or destroy a materializer
  /// without materializing all the GVs it controls, to avoid leaving orphan
  /// unmaterialized GVs.
  void setMaterializer(GVMaterializer *GVM);
  /// Retrieves the GVMaterializer, if any, for this Module.
  GVMaterializer *getMaterializer() const { return Materializer.get(); }
  bool isMaterialized() const { return !getMaterializer(); }

  /// Make sure the GlobalValue is fully read.
  llvm::Error materialize(GlobalValue *GV);

  /// Make sure all GlobalValues in this Module are fully read and clear the
  /// Materializer.
  llvm::Error materializeAll();

  llvm::Error materializeMetadata();

  /// Detach global variable \p GV from the list but don't delete it.
  void removeGlobalVariable(GlobalVariable *GV) { GlobalList.remove(GV); }
  /// Remove global variable \p GV from the list and delete it.
  void eraseGlobalVariable(GlobalVariable *GV) { GlobalList.erase(GV); }
  /// Insert global variable \p GV at the end of the global variable list and
  /// take ownership.
  void insertGlobalVariable(GlobalVariable *GV) {
    insertGlobalVariable(GlobalList.end(), GV);
  }
  /// Insert global variable \p GV into the global variable list before \p
  /// Where and take ownership.
  void insertGlobalVariable(GlobalListType::iterator Where, GlobalVariable *GV) {
    GlobalList.insert(Where, GV);
  }
  // Use global_size() to get the total number of global variables.
  // Use globals() to get the range of all global variables.

private:
/// @}
/// @name Direct access to the globals list, functions list, and symbol table
/// @{

  /// Get the Module's list of global variables (constant).
  const GlobalListType   &getGlobalList() const       { return GlobalList; }
  /// Get the Module's list of global variables.
  GlobalListType         &getGlobalList()             { return GlobalList; }

  static GlobalListType Module::*getSublistAccess(GlobalVariable*) {
    return &Module::GlobalList;
  }
  friend class llvm::SymbolTableListTraits<llvm::GlobalVariable>;

public:
  /// Get the Module's list of functions (constant).
  const FunctionListType &getFunctionList() const     { return FunctionList; }
  /// Get the Module's list of functions.
  FunctionListType       &getFunctionList()           { return FunctionList; }
  static FunctionListType Module::*getSublistAccess(Function*) {
    return &Module::FunctionList;
  }

  /// Detach \p Alias from the list but don't delete it.
  void removeAlias(GlobalAlias *Alias) { AliasList.remove(Alias); }
  /// Remove \p Alias from the list and delete it.
  void eraseAlias(GlobalAlias *Alias) { AliasList.erase(Alias); }
  /// Insert \p Alias at the end of the alias list and take ownership.
  void insertAlias(GlobalAlias *Alias) { AliasList.insert(AliasList.end(), Alias); }
  // Use alias_size() to get the size of AliasList.
  // Use aliases() to get a range of all Alias objects in AliasList.

  /// Detach \p IFunc from the list but don't delete it.
  void removeIFunc(GlobalIFunc *IFunc) { IFuncList.remove(IFunc); }
  /// Remove \p IFunc from the list and delete it.
  void eraseIFunc(GlobalIFunc *IFunc) { IFuncList.erase(IFunc); }
  /// Insert \p IFunc at the end of the alias list and take ownership.
  void insertIFunc(GlobalIFunc *IFunc) { IFuncList.push_back(IFunc); }
  // Use ifunc_size() to get the number of functions in IFuncList.
  // Use ifuncs() to get the range of all IFuncs.

  /// Detach \p MDNode from the list but don't delete it.
  void removeNamedMDNode(NamedMDNode *MDNode) { NamedMDList.remove(MDNode); }
  /// Remove \p MDNode from the list and delete it.
  void eraseNamedMDNode(NamedMDNode *MDNode) { NamedMDList.erase(MDNode); }
  /// Insert \p MDNode at the end of the alias list and take ownership.
  void insertNamedMDNode(NamedMDNode *MDNode) {
    NamedMDList.push_back(MDNode);
  }
  // Use named_metadata_size() to get the size of the named meatadata list.
  // Use named_metadata() to get the range of all named metadata.

private: // Please use functions like insertAlias(), removeAlias() etc.
  /// Get the Module's list of aliases (constant).
  const AliasListType    &getAliasList() const        { return AliasList; }
  /// Get the Module's list of aliases.
  AliasListType          &getAliasList()              { return AliasList; }

  static AliasListType Module::*getSublistAccess(GlobalAlias*) {
    return &Module::AliasList;
  }
  friend class llvm::SymbolTableListTraits<llvm::GlobalAlias>;

  /// Get the Module's list of ifuncs (constant).
  const IFuncListType    &getIFuncList() const        { return IFuncList; }
  /// Get the Module's list of ifuncs.
  IFuncListType          &getIFuncList()              { return IFuncList; }

  static IFuncListType Module::*getSublistAccess(GlobalIFunc*) {
    return &Module::IFuncList;
  }
  friend class llvm::SymbolTableListTraits<llvm::GlobalIFunc>;

  /// Get the Module's list of named metadata (constant).
  const NamedMDListType  &getNamedMDList() const      { return NamedMDList; }
  /// Get the Module's list of named metadata.
  NamedMDListType        &getNamedMDList()            { return NamedMDList; }

  static NamedMDListType Module::*getSublistAccess(NamedMDNode*) {
    return &Module::NamedMDList;
  }

public:
  /// Get the symbol table of global variable and function identifiers
  const ValueSymbolTable &getValueSymbolTable() const { return *ValSymTab; }
  /// Get the Module's symbol table of global variable and function identifiers.
  ValueSymbolTable       &getValueSymbolTable()       { return *ValSymTab; }

  /// Get the Module's symbol table for COMDATs (constant).
  const ComdatSymTabType &getComdatSymbolTable() const { return ComdatSymTab; }
  /// Get the Module's symbol table for COMDATs.
  ComdatSymTabType &getComdatSymbolTable() { return ComdatSymTab; }

/// @}
/// @name Global Variable Iteration
/// @{

  global_iterator       global_begin()       { return GlobalList.begin(); }
  const_global_iterator global_begin() const { return GlobalList.begin(); }
  global_iterator       global_end  ()       { return GlobalList.end(); }
  const_global_iterator global_end  () const { return GlobalList.end(); }
  size_t                global_size () const { return GlobalList.size(); }
  bool                  global_empty() const { return GlobalList.empty(); }

  iterator_range<global_iterator> globals() {
    return make_range(global_begin(), global_end());
  }
  iterator_range<const_global_iterator> globals() const {
    return make_range(global_begin(), global_end());
  }

/// @}
/// @name Function Iteration
/// @{

  iterator                begin()       { return FunctionList.begin(); }
  const_iterator          begin() const { return FunctionList.begin(); }
  iterator                end  ()       { return FunctionList.end();   }
  const_iterator          end  () const { return FunctionList.end();   }
  reverse_iterator        rbegin()      { return FunctionList.rbegin(); }
  const_reverse_iterator  rbegin() const{ return FunctionList.rbegin(); }
  reverse_iterator        rend()        { return FunctionList.rend(); }
  const_reverse_iterator  rend() const  { return FunctionList.rend(); }
  size_t                  size() const  { return FunctionList.size(); }
  bool                    empty() const { return FunctionList.empty(); }

  iterator_range<iterator> functions() {
    return make_range(begin(), end());
  }
  iterator_range<const_iterator> functions() const {
    return make_range(begin(), end());
  }

/// @}
/// @name Alias Iteration
/// @{

  alias_iterator       alias_begin()            { return AliasList.begin(); }
  const_alias_iterator alias_begin() const      { return AliasList.begin(); }
  alias_iterator       alias_end  ()            { return AliasList.end();   }
  const_alias_iterator alias_end  () const      { return AliasList.end();   }
  size_t               alias_size () const      { return AliasList.size();  }
  bool                 alias_empty() const      { return AliasList.empty(); }

  iterator_range<alias_iterator> aliases() {
    return make_range(alias_begin(), alias_end());
  }
  iterator_range<const_alias_iterator> aliases() const {
    return make_range(alias_begin(), alias_end());
  }

/// @}
/// @name IFunc Iteration
/// @{

  ifunc_iterator       ifunc_begin()            { return IFuncList.begin(); }
  const_ifunc_iterator ifunc_begin() const      { return IFuncList.begin(); }
  ifunc_iterator       ifunc_end  ()            { return IFuncList.end();   }
  const_ifunc_iterator ifunc_end  () const      { return IFuncList.end();   }
  size_t               ifunc_size () const      { return IFuncList.size();  }
  bool                 ifunc_empty() const      { return IFuncList.empty(); }

  iterator_range<ifunc_iterator> ifuncs() {
    return make_range(ifunc_begin(), ifunc_end());
  }
  iterator_range<const_ifunc_iterator> ifuncs() const {
    return make_range(ifunc_begin(), ifunc_end());
  }

  /// @}
  /// @name Convenience iterators
  /// @{

  using global_object_iterator =
      concat_iterator<GlobalObject, iterator, global_iterator>;
  using const_global_object_iterator =
      concat_iterator<const GlobalObject, const_iterator,
                      const_global_iterator>;

  iterator_range<global_object_iterator> global_objects();
  iterator_range<const_global_object_iterator> global_objects() const;

  using global_value_iterator =
      concat_iterator<GlobalValue, iterator, global_iterator, alias_iterator,
                      ifunc_iterator>;
  using const_global_value_iterator =
      concat_iterator<const GlobalValue, const_iterator, const_global_iterator,
                      const_alias_iterator, const_ifunc_iterator>;

  iterator_range<global_value_iterator> global_values();
  iterator_range<const_global_value_iterator> global_values() const;

  /// @}
  /// @name Named Metadata Iteration
  /// @{

  named_metadata_iterator named_metadata_begin() { return NamedMDList.begin(); }
  const_named_metadata_iterator named_metadata_begin() const {
    return NamedMDList.begin();
  }

  named_metadata_iterator named_metadata_end() { return NamedMDList.end(); }
  const_named_metadata_iterator named_metadata_end() const {
    return NamedMDList.end();
  }

  size_t named_metadata_size() const { return NamedMDList.size();  }
  bool named_metadata_empty() const { return NamedMDList.empty(); }

  iterator_range<named_metadata_iterator> named_metadata() {
    return make_range(named_metadata_begin(), named_metadata_end());
  }
  iterator_range<const_named_metadata_iterator> named_metadata() const {
    return make_range(named_metadata_begin(), named_metadata_end());
  }

  /// An iterator for DICompileUnits that skips those marked NoDebug.
  class debug_compile_units_iterator {
    NamedMDNode *CUs;
    unsigned Idx;

    void SkipNoDebugCUs();

  public:
    using iterator_category = std::input_iterator_tag;
    using value_type = DICompileUnit *;
    using difference_type = std::ptrdiff_t;
    using pointer = value_type *;
    using reference = value_type &;

    explicit debug_compile_units_iterator(NamedMDNode *CUs, unsigned Idx)
        : CUs(CUs), Idx(Idx) {
      SkipNoDebugCUs();
    }

    debug_compile_units_iterator &operator++() {
      ++Idx;
      SkipNoDebugCUs();
      return *this;
    }

    debug_compile_units_iterator operator++(int) {
      debug_compile_units_iterator T(*this);
      ++Idx;
      return T;
    }

    bool operator==(const debug_compile_units_iterator &I) const {
      return Idx == I.Idx;
    }

    bool operator!=(const debug_compile_units_iterator &I) const {
      return Idx != I.Idx;
    }

    DICompileUnit *operator*() const;
    DICompileUnit *operator->() const;
  };

  debug_compile_units_iterator debug_compile_units_begin() const {
    auto *CUs = getNamedMetadata("llvm.dbg.cu");
    return debug_compile_units_iterator(CUs, 0);
  }

  debug_compile_units_iterator debug_compile_units_end() const {
    auto *CUs = getNamedMetadata("llvm.dbg.cu");
    return debug_compile_units_iterator(CUs, CUs ? CUs->getNumOperands() : 0);
  }

  /// Return an iterator for all DICompileUnits listed in this Module's
  /// llvm.dbg.cu named metadata node and aren't explicitly marked as
  /// NoDebug.
  iterator_range<debug_compile_units_iterator> debug_compile_units() const {
    auto *CUs = getNamedMetadata("llvm.dbg.cu");
    return make_range(
        debug_compile_units_iterator(CUs, 0),
        debug_compile_units_iterator(CUs, CUs ? CUs->getNumOperands() : 0));
  }
/// @}

  /// Destroy ConstantArrays in LLVMContext if they are not used.
  /// ConstantArrays constructed during linking can cause quadratic memory
  /// explosion. Releasing all unused constants can cause a 20% LTO compile-time
  /// slowdown for a large application.
  ///
  /// NOTE: Constants are currently owned by LLVMContext. This can then only
  /// be called where all uses of the LLVMContext are understood.
  void dropTriviallyDeadConstantArrays();

/// @name Utility functions for printing and dumping Module objects
/// @{

  /// Print the module to an output stream with an optional
  /// AssemblyAnnotationWriter.  If \c ShouldPreserveUseListOrder, then include
  /// uselistorder directives so that use-lists can be recreated when reading
  /// the assembly.
  void print(raw_ostream &OS, AssemblyAnnotationWriter *AAW,
             bool ShouldPreserveUseListOrder = false,
             bool IsForDebug = false) const;

  /// Dump the module to stderr (for debugging).
  void dump() const;

  /// This function causes all the subinstructions to "let go" of all references
  /// that they are maintaining.  This allows one to 'delete' a whole class at
  /// a time, even though there may be circular references... first all
  /// references are dropped, and all use counts go to zero.  Then everything
  /// is delete'd for real.  Note that no operations are valid on an object
  /// that has "dropped all references", except operator delete.
  void dropAllReferences();

/// @}
/// @name Utility functions for querying Debug information.
/// @{

  /// Returns the Number of Register ParametersDwarf Version by checking
  /// module flags.
  unsigned getNumberRegisterParameters() const;

  /// Returns the Dwarf Version by checking module flags.
  unsigned getDwarfVersion() const;

  /// Returns the DWARF format by checking module flags.
  bool isDwarf64() const;

  /// Returns the CodeView Version by checking module flags.
  /// Returns zero if not present in module.
  unsigned getCodeViewFlag() const;

/// @}
/// @name Utility functions for querying and setting PIC level
/// @{

  /// Returns the PIC level (small or large model)
  PICLevel::Level getPICLevel() const;

  /// Set the PIC level (small or large model)
  void setPICLevel(PICLevel::Level PL);
/// @}

/// @}
/// @name Utility functions for querying and setting PIE level
/// @{

  /// Returns the PIE level (small or large model)
  PIELevel::Level getPIELevel() const;

  /// Set the PIE level (small or large model)
  void setPIELevel(PIELevel::Level PL);
/// @}

  /// @}
  /// @name Utility function for querying and setting code model
  /// @{

  /// Returns the code model (tiny, small, kernel, medium or large model)
  std::optional<CodeModel::Model> getCodeModel() const;

  /// Set the code model (tiny, small, kernel, medium or large)
  void setCodeModel(CodeModel::Model CL);
  /// @}

  /// @}
  /// @name Utility function for querying and setting the large data threshold
  /// @{

  /// Returns the code model (tiny, small, kernel, medium or large model)
  std::optional<uint64_t> getLargeDataThreshold() const;

  /// Set the code model (tiny, small, kernel, medium or large)
  void setLargeDataThreshold(uint64_t Threshold);
  /// @}

  /// @name Utility functions for querying and setting PGO summary
  /// @{

  /// Attach profile summary metadata to this module.
  void setProfileSummary(Metadata *M, ProfileSummary::Kind Kind);

  /// Returns profile summary metadata. When IsCS is true, use the context
  /// sensitive profile summary.
  Metadata *getProfileSummary(bool IsCS) const;
  /// @}

  /// Returns whether semantic interposition is to be respected.
  bool getSemanticInterposition() const;

  /// Set whether semantic interposition is to be respected.
  void setSemanticInterposition(bool);

  /// Returns true if PLT should be avoided for RTLib calls.
  bool getRtLibUseGOT() const;

  /// Set that PLT should be avoid for RTLib calls.
  void setRtLibUseGOT();

  /// Get/set whether referencing global variables can use direct access
  /// relocations on ELF targets.
  bool getDirectAccessExternalData() const;
  void setDirectAccessExternalData(bool Value);

  /// Get/set whether synthesized functions should get the uwtable attribute.
  UWTableKind getUwtable() const;
  void setUwtable(UWTableKind Kind);

  /// Get/set whether synthesized functions should get the "frame-pointer"
  /// attribute.
  FramePointerKind getFramePointer() const;
  void setFramePointer(FramePointerKind Kind);

  /// Get/set what kind of stack protector guard to use.
  StringRef getStackProtectorGuard() const;
  void setStackProtectorGuard(StringRef Kind);

  /// Get/set which register to use as the stack protector guard register. The
  /// empty string is equivalent to "global". Other values may be "tls" or
  /// "sysreg".
  StringRef getStackProtectorGuardReg() const;
  void setStackProtectorGuardReg(StringRef Reg);

  /// Get/set a symbol to use as the stack protector guard.
  StringRef getStackProtectorGuardSymbol() const;
  void setStackProtectorGuardSymbol(StringRef Symbol);

  /// Get/set what offset from the stack protector to use.
  int getStackProtectorGuardOffset() const;
  void setStackProtectorGuardOffset(int Offset);

  /// Get/set the stack alignment overridden from the default.
  unsigned getOverrideStackAlignment() const;
  void setOverrideStackAlignment(unsigned Align);

  unsigned getMaxTLSAlignment() const;

  /// @name Utility functions for querying and setting the build SDK version
  /// @{

  /// Attach a build SDK version metadata to this module.
  void setSDKVersion(const VersionTuple &V);

  /// Get the build SDK version metadata.
  ///
  /// An empty version is returned if no such metadata is attached.
  VersionTuple getSDKVersion() const;
  /// @}

  /// Take ownership of the given memory buffer.
  void setOwnedMemoryBuffer(std::unique_ptr<MemoryBuffer> MB);

  /// Set the partial sample profile ratio in the profile summary module flag,
  /// if applicable.
  void setPartialSampleProfileRatio(const ModuleSummaryIndex &Index);

  /// Get the target variant triple which is a string describing a variant of
  /// the target host platform. For example, Mac Catalyst can be a variant
  /// target triple for a macOS target.
  /// @returns a string containing the target variant triple.
  StringRef getDarwinTargetVariantTriple() const;

  /// Set the target variant triple which is a string describing a variant of
  /// the target host platform.
  void setDarwinTargetVariantTriple(StringRef T);

  /// Get the target variant version build SDK version metadata.
  ///
  /// An empty version is returned if no such metadata is attached.
  VersionTuple getDarwinTargetVariantSDKVersion() const;

  /// Set the target variant version build SDK version metadata.
  void setDarwinTargetVariantSDKVersion(VersionTuple Version);
};

/// Given "llvm.used" or "llvm.compiler.used" as a global name, collect the
/// initializer elements of that global in a SmallVector and return the global
/// itself.
GlobalVariable *collectUsedGlobalVariables(const Module &M,
                                           SmallVectorImpl<GlobalValue *> &Vec,
                                           bool CompilerUsed);

/// An raw_ostream inserter for modules.
inline raw_ostream &operator<<(raw_ostream &O, const Module &M) {
  M.print(O, nullptr);
  return O;
}

// Create wrappers for C Binding types (see CBindingWrapping.h).
DEFINE_SIMPLE_CONVERSION_FUNCTIONS(Module, LLVMModuleRef)

/* LLVMModuleProviderRef exists for historical reasons, but now just holds a
 * Module.
 */
inline Module *unwrap(LLVMModuleProviderRef MP) {
  return reinterpret_cast<Module*>(MP);
}

} // end namespace llvm

#endif // LLVM_IR_MODULE_H
