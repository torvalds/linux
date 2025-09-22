//===- Module.cpp - Implement the Module class ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Module class for the IR library.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/Module.h"
#include "SymbolTableListTraitsImpl.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Comdat.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GVMaterializer.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalIFunc.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/IR/SymbolTableListTraits.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/TypeFinder.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/ValueSymbolTable.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/RandomNumberGenerator.h"
#include "llvm/Support/VersionTuple.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

using namespace llvm;

extern cl::opt<bool> UseNewDbgInfoFormat;

//===----------------------------------------------------------------------===//
// Methods to implement the globals and functions lists.
//

// Explicit instantiations of SymbolTableListTraits since some of the methods
// are not in the public header file.
template class llvm::SymbolTableListTraits<Function>;
template class llvm::SymbolTableListTraits<GlobalVariable>;
template class llvm::SymbolTableListTraits<GlobalAlias>;
template class llvm::SymbolTableListTraits<GlobalIFunc>;

//===----------------------------------------------------------------------===//
// Primitive Module methods.
//

Module::Module(StringRef MID, LLVMContext &C)
    : Context(C), ValSymTab(std::make_unique<ValueSymbolTable>(-1)),
      ModuleID(std::string(MID)), SourceFileName(std::string(MID)), DL(""),
      IsNewDbgInfoFormat(UseNewDbgInfoFormat) {
  Context.addModule(this);
}

Module::~Module() {
  Context.removeModule(this);
  dropAllReferences();
  GlobalList.clear();
  FunctionList.clear();
  AliasList.clear();
  IFuncList.clear();
}

void Module::removeDebugIntrinsicDeclarations() {
  auto *DeclareIntrinsicFn =
      Intrinsic::getDeclaration(this, Intrinsic::dbg_declare);
  assert((!isMaterialized() || DeclareIntrinsicFn->hasZeroLiveUses()) &&
         "Debug declare intrinsic should have had uses removed.");
  DeclareIntrinsicFn->eraseFromParent();
  auto *ValueIntrinsicFn =
      Intrinsic::getDeclaration(this, Intrinsic::dbg_value);
  assert((!isMaterialized() || ValueIntrinsicFn->hasZeroLiveUses()) &&
         "Debug value intrinsic should have had uses removed.");
  ValueIntrinsicFn->eraseFromParent();
  auto *AssignIntrinsicFn =
      Intrinsic::getDeclaration(this, Intrinsic::dbg_assign);
  assert((!isMaterialized() || AssignIntrinsicFn->hasZeroLiveUses()) &&
         "Debug assign intrinsic should have had uses removed.");
  AssignIntrinsicFn->eraseFromParent();
  auto *LabelntrinsicFn = Intrinsic::getDeclaration(this, Intrinsic::dbg_label);
  assert((!isMaterialized() || LabelntrinsicFn->hasZeroLiveUses()) &&
         "Debug label intrinsic should have had uses removed.");
  LabelntrinsicFn->eraseFromParent();
}

std::unique_ptr<RandomNumberGenerator>
Module::createRNG(const StringRef Name) const {
  SmallString<32> Salt(Name);

  // This RNG is guaranteed to produce the same random stream only
  // when the Module ID and thus the input filename is the same. This
  // might be problematic if the input filename extension changes
  // (e.g. from .c to .bc or .ll).
  //
  // We could store this salt in NamedMetadata, but this would make
  // the parameter non-const. This would unfortunately make this
  // interface unusable by any Machine passes, since they only have a
  // const reference to their IR Module. Alternatively we can always
  // store salt metadata from the Module constructor.
  Salt += sys::path::filename(getModuleIdentifier());

  return std::unique_ptr<RandomNumberGenerator>(
      new RandomNumberGenerator(Salt));
}

/// getNamedValue - Return the first global value in the module with
/// the specified name, of arbitrary type.  This method returns null
/// if a global with the specified name is not found.
GlobalValue *Module::getNamedValue(StringRef Name) const {
  return cast_or_null<GlobalValue>(getValueSymbolTable().lookup(Name));
}

unsigned Module::getNumNamedValues() const {
  return getValueSymbolTable().size();
}

/// getMDKindID - Return a unique non-zero ID for the specified metadata kind.
/// This ID is uniqued across modules in the current LLVMContext.
unsigned Module::getMDKindID(StringRef Name) const {
  return Context.getMDKindID(Name);
}

/// getMDKindNames - Populate client supplied SmallVector with the name for
/// custom metadata IDs registered in this LLVMContext.   ID #0 is not used,
/// so it is filled in as an empty string.
void Module::getMDKindNames(SmallVectorImpl<StringRef> &Result) const {
  return Context.getMDKindNames(Result);
}

void Module::getOperandBundleTags(SmallVectorImpl<StringRef> &Result) const {
  return Context.getOperandBundleTags(Result);
}

//===----------------------------------------------------------------------===//
// Methods for easy access to the functions in the module.
//

// getOrInsertFunction - Look up the specified function in the module symbol
// table.  If it does not exist, add a prototype for the function and return
// it.  This is nice because it allows most passes to get away with not handling
// the symbol table directly for this common task.
//
FunctionCallee Module::getOrInsertFunction(StringRef Name, FunctionType *Ty,
                                           AttributeList AttributeList) {
  // See if we have a definition for the specified function already.
  GlobalValue *F = getNamedValue(Name);
  if (!F) {
    // Nope, add it
    Function *New = Function::Create(Ty, GlobalVariable::ExternalLinkage,
                                     DL.getProgramAddressSpace(), Name, this);
    if (!New->isIntrinsic())       // Intrinsics get attrs set on construction
      New->setAttributes(AttributeList);
    return {Ty, New}; // Return the new prototype.
  }

  // Otherwise, we just found the existing function or a prototype.
  return {Ty, F};
}

FunctionCallee Module::getOrInsertFunction(StringRef Name, FunctionType *Ty) {
  return getOrInsertFunction(Name, Ty, AttributeList());
}

// getFunction - Look up the specified function in the module symbol table.
// If it does not exist, return null.
//
Function *Module::getFunction(StringRef Name) const {
  return dyn_cast_or_null<Function>(getNamedValue(Name));
}

//===----------------------------------------------------------------------===//
// Methods for easy access to the global variables in the module.
//

/// getGlobalVariable - Look up the specified global variable in the module
/// symbol table.  If it does not exist, return null.  The type argument
/// should be the underlying type of the global, i.e., it should not have
/// the top-level PointerType, which represents the address of the global.
/// If AllowLocal is set to true, this function will return types that
/// have an local. By default, these types are not returned.
///
GlobalVariable *Module::getGlobalVariable(StringRef Name,
                                          bool AllowLocal) const {
  if (GlobalVariable *Result =
      dyn_cast_or_null<GlobalVariable>(getNamedValue(Name)))
    if (AllowLocal || !Result->hasLocalLinkage())
      return Result;
  return nullptr;
}

/// getOrInsertGlobal - Look up the specified global in the module symbol table.
///   1. If it does not exist, add a declaration of the global and return it.
///   2. Else, the global exists but has the wrong type: return the function
///      with a constantexpr cast to the right type.
///   3. Finally, if the existing global is the correct declaration, return the
///      existing global.
Constant *Module::getOrInsertGlobal(
    StringRef Name, Type *Ty,
    function_ref<GlobalVariable *()> CreateGlobalCallback) {
  // See if we have a definition for the specified global already.
  GlobalVariable *GV = dyn_cast_or_null<GlobalVariable>(getNamedValue(Name));
  if (!GV)
    GV = CreateGlobalCallback();
  assert(GV && "The CreateGlobalCallback is expected to create a global");

  // Otherwise, we just found the existing function or a prototype.
  return GV;
}

// Overload to construct a global variable using its constructor's defaults.
Constant *Module::getOrInsertGlobal(StringRef Name, Type *Ty) {
  return getOrInsertGlobal(Name, Ty, [&] {
    return new GlobalVariable(*this, Ty, false, GlobalVariable::ExternalLinkage,
                              nullptr, Name);
  });
}

//===----------------------------------------------------------------------===//
// Methods for easy access to the global variables in the module.
//

// getNamedAlias - Look up the specified global in the module symbol table.
// If it does not exist, return null.
//
GlobalAlias *Module::getNamedAlias(StringRef Name) const {
  return dyn_cast_or_null<GlobalAlias>(getNamedValue(Name));
}

GlobalIFunc *Module::getNamedIFunc(StringRef Name) const {
  return dyn_cast_or_null<GlobalIFunc>(getNamedValue(Name));
}

/// getNamedMetadata - Return the first NamedMDNode in the module with the
/// specified name. This method returns null if a NamedMDNode with the
/// specified name is not found.
NamedMDNode *Module::getNamedMetadata(const Twine &Name) const {
  SmallString<256> NameData;
  StringRef NameRef = Name.toStringRef(NameData);
  return NamedMDSymTab.lookup(NameRef);
}

/// getOrInsertNamedMetadata - Return the first named MDNode in the module
/// with the specified name. This method returns a new NamedMDNode if a
/// NamedMDNode with the specified name is not found.
NamedMDNode *Module::getOrInsertNamedMetadata(StringRef Name) {
  NamedMDNode *&NMD = NamedMDSymTab[Name];
  if (!NMD) {
    NMD = new NamedMDNode(Name);
    NMD->setParent(this);
    insertNamedMDNode(NMD);
  }
  return NMD;
}

/// eraseNamedMetadata - Remove the given NamedMDNode from this module and
/// delete it.
void Module::eraseNamedMetadata(NamedMDNode *NMD) {
  NamedMDSymTab.erase(NMD->getName());
  eraseNamedMDNode(NMD);
}

bool Module::isValidModFlagBehavior(Metadata *MD, ModFlagBehavior &MFB) {
  if (ConstantInt *Behavior = mdconst::dyn_extract_or_null<ConstantInt>(MD)) {
    uint64_t Val = Behavior->getLimitedValue();
    if (Val >= ModFlagBehaviorFirstVal && Val <= ModFlagBehaviorLastVal) {
      MFB = static_cast<ModFlagBehavior>(Val);
      return true;
    }
  }
  return false;
}

bool Module::isValidModuleFlag(const MDNode &ModFlag, ModFlagBehavior &MFB,
                               MDString *&Key, Metadata *&Val) {
  if (ModFlag.getNumOperands() < 3)
    return false;
  if (!isValidModFlagBehavior(ModFlag.getOperand(0), MFB))
    return false;
  MDString *K = dyn_cast_or_null<MDString>(ModFlag.getOperand(1));
  if (!K)
    return false;
  Key = K;
  Val = ModFlag.getOperand(2);
  return true;
}

/// getModuleFlagsMetadata - Returns the module flags in the provided vector.
void Module::
getModuleFlagsMetadata(SmallVectorImpl<ModuleFlagEntry> &Flags) const {
  const NamedMDNode *ModFlags = getModuleFlagsMetadata();
  if (!ModFlags) return;

  for (const MDNode *Flag : ModFlags->operands()) {
    ModFlagBehavior MFB;
    MDString *Key = nullptr;
    Metadata *Val = nullptr;
    if (isValidModuleFlag(*Flag, MFB, Key, Val)) {
      // Check the operands of the MDNode before accessing the operands.
      // The verifier will actually catch these failures.
      Flags.push_back(ModuleFlagEntry(MFB, Key, Val));
    }
  }
}

/// Return the corresponding value if Key appears in module flags, otherwise
/// return null.
Metadata *Module::getModuleFlag(StringRef Key) const {
  SmallVector<Module::ModuleFlagEntry, 8> ModuleFlags;
  getModuleFlagsMetadata(ModuleFlags);
  for (const ModuleFlagEntry &MFE : ModuleFlags) {
    if (Key == MFE.Key->getString())
      return MFE.Val;
  }
  return nullptr;
}

/// getModuleFlagsMetadata - Returns the NamedMDNode in the module that
/// represents module-level flags. This method returns null if there are no
/// module-level flags.
NamedMDNode *Module::getModuleFlagsMetadata() const {
  return getNamedMetadata("llvm.module.flags");
}

/// getOrInsertModuleFlagsMetadata - Returns the NamedMDNode in the module that
/// represents module-level flags. If module-level flags aren't found, it
/// creates the named metadata that contains them.
NamedMDNode *Module::getOrInsertModuleFlagsMetadata() {
  return getOrInsertNamedMetadata("llvm.module.flags");
}

/// addModuleFlag - Add a module-level flag to the module-level flags
/// metadata. It will create the module-level flags named metadata if it doesn't
/// already exist.
void Module::addModuleFlag(ModFlagBehavior Behavior, StringRef Key,
                           Metadata *Val) {
  Type *Int32Ty = Type::getInt32Ty(Context);
  Metadata *Ops[3] = {
      ConstantAsMetadata::get(ConstantInt::get(Int32Ty, Behavior)),
      MDString::get(Context, Key), Val};
  getOrInsertModuleFlagsMetadata()->addOperand(MDNode::get(Context, Ops));
}
void Module::addModuleFlag(ModFlagBehavior Behavior, StringRef Key,
                           Constant *Val) {
  addModuleFlag(Behavior, Key, ConstantAsMetadata::get(Val));
}
void Module::addModuleFlag(ModFlagBehavior Behavior, StringRef Key,
                           uint32_t Val) {
  Type *Int32Ty = Type::getInt32Ty(Context);
  addModuleFlag(Behavior, Key, ConstantInt::get(Int32Ty, Val));
}
void Module::addModuleFlag(MDNode *Node) {
  assert(Node->getNumOperands() == 3 &&
         "Invalid number of operands for module flag!");
  assert(mdconst::hasa<ConstantInt>(Node->getOperand(0)) &&
         isa<MDString>(Node->getOperand(1)) &&
         "Invalid operand types for module flag!");
  getOrInsertModuleFlagsMetadata()->addOperand(Node);
}

void Module::setModuleFlag(ModFlagBehavior Behavior, StringRef Key,
                           Metadata *Val) {
  NamedMDNode *ModFlags = getOrInsertModuleFlagsMetadata();
  // Replace the flag if it already exists.
  for (MDNode *Flag : ModFlags->operands()) {
    ModFlagBehavior MFB;
    MDString *K = nullptr;
    Metadata *V = nullptr;
    if (isValidModuleFlag(*Flag, MFB, K, V) && K->getString() == Key) {
      Flag->replaceOperandWith(2, Val);
      return;
    }
  }
  addModuleFlag(Behavior, Key, Val);
}
void Module::setModuleFlag(ModFlagBehavior Behavior, StringRef Key,
                           Constant *Val) {
  setModuleFlag(Behavior, Key, ConstantAsMetadata::get(Val));
}
void Module::setModuleFlag(ModFlagBehavior Behavior, StringRef Key,
                           uint32_t Val) {
  Type *Int32Ty = Type::getInt32Ty(Context);
  setModuleFlag(Behavior, Key, ConstantInt::get(Int32Ty, Val));
}

void Module::setDataLayout(StringRef Desc) {
  DL.reset(Desc);
}

void Module::setDataLayout(const DataLayout &Other) { DL = Other; }

DICompileUnit *Module::debug_compile_units_iterator::operator*() const {
  return cast<DICompileUnit>(CUs->getOperand(Idx));
}
DICompileUnit *Module::debug_compile_units_iterator::operator->() const {
  return cast<DICompileUnit>(CUs->getOperand(Idx));
}

void Module::debug_compile_units_iterator::SkipNoDebugCUs() {
  while (CUs && (Idx < CUs->getNumOperands()) &&
         ((*this)->getEmissionKind() == DICompileUnit::NoDebug))
    ++Idx;
}

iterator_range<Module::global_object_iterator> Module::global_objects() {
  return concat<GlobalObject>(functions(), globals());
}
iterator_range<Module::const_global_object_iterator>
Module::global_objects() const {
  return concat<const GlobalObject>(functions(), globals());
}

iterator_range<Module::global_value_iterator> Module::global_values() {
  return concat<GlobalValue>(functions(), globals(), aliases(), ifuncs());
}
iterator_range<Module::const_global_value_iterator>
Module::global_values() const {
  return concat<const GlobalValue>(functions(), globals(), aliases(), ifuncs());
}

//===----------------------------------------------------------------------===//
// Methods to control the materialization of GlobalValues in the Module.
//
void Module::setMaterializer(GVMaterializer *GVM) {
  assert(!Materializer &&
         "Module already has a GVMaterializer.  Call materializeAll"
         " to clear it out before setting another one.");
  Materializer.reset(GVM);
}

Error Module::materialize(GlobalValue *GV) {
  if (!Materializer)
    return Error::success();

  return Materializer->materialize(GV);
}

Error Module::materializeAll() {
  if (!Materializer)
    return Error::success();
  std::unique_ptr<GVMaterializer> M = std::move(Materializer);
  return M->materializeModule();
}

Error Module::materializeMetadata() {
  if (!Materializer)
    return Error::success();
  return Materializer->materializeMetadata();
}

//===----------------------------------------------------------------------===//
// Other module related stuff.
//

std::vector<StructType *> Module::getIdentifiedStructTypes() const {
  // If we have a materializer, it is possible that some unread function
  // uses a type that is currently not visible to a TypeFinder, so ask
  // the materializer which types it created.
  if (Materializer)
    return Materializer->getIdentifiedStructTypes();

  std::vector<StructType *> Ret;
  TypeFinder SrcStructTypes;
  SrcStructTypes.run(*this, true);
  Ret.assign(SrcStructTypes.begin(), SrcStructTypes.end());
  return Ret;
}

std::string Module::getUniqueIntrinsicName(StringRef BaseName, Intrinsic::ID Id,
                                           const FunctionType *Proto) {
  auto Encode = [&BaseName](unsigned Suffix) {
    return (Twine(BaseName) + "." + Twine(Suffix)).str();
  };

  {
    // fast path - the prototype is already known
    auto UinItInserted = UniquedIntrinsicNames.insert({{Id, Proto}, 0});
    if (!UinItInserted.second)
      return Encode(UinItInserted.first->second);
  }

  // Not known yet. A new entry was created with index 0. Check if there already
  // exists a matching declaration, or select a new entry.

  // Start looking for names with the current known maximum count (or 0).
  auto NiidItInserted = CurrentIntrinsicIds.insert({BaseName, 0});
  unsigned Count = NiidItInserted.first->second;

  // This might be slow if a whole population of intrinsics already existed, but
  // we cache the values for later usage.
  std::string NewName;
  while (true) {
    NewName = Encode(Count);
    GlobalValue *F = getNamedValue(NewName);
    if (!F) {
      // Reserve this entry for the new proto
      UniquedIntrinsicNames[{Id, Proto}] = Count;
      break;
    }

    // A declaration with this name already exists. Remember it.
    FunctionType *FT = dyn_cast<FunctionType>(F->getValueType());
    auto UinItInserted = UniquedIntrinsicNames.insert({{Id, FT}, Count});
    if (FT == Proto) {
      // It was a declaration for our prototype. This entry was allocated in the
      // beginning. Update the count to match the existing declaration.
      UinItInserted.first->second = Count;
      break;
    }

    ++Count;
  }

  NiidItInserted.first->second = Count + 1;

  return NewName;
}

// dropAllReferences() - This function causes all the subelements to "let go"
// of all references that they are maintaining.  This allows one to 'delete' a
// whole module at a time, even though there may be circular references... first
// all references are dropped, and all use counts go to zero.  Then everything
// is deleted for real.  Note that no operations are valid on an object that
// has "dropped all references", except operator delete.
//
void Module::dropAllReferences() {
  for (Function &F : *this)
    F.dropAllReferences();

  for (GlobalVariable &GV : globals())
    GV.dropAllReferences();

  for (GlobalAlias &GA : aliases())
    GA.dropAllReferences();

  for (GlobalIFunc &GIF : ifuncs())
    GIF.dropAllReferences();
}

unsigned Module::getNumberRegisterParameters() const {
  auto *Val =
      cast_or_null<ConstantAsMetadata>(getModuleFlag("NumRegisterParameters"));
  if (!Val)
    return 0;
  return cast<ConstantInt>(Val->getValue())->getZExtValue();
}

unsigned Module::getDwarfVersion() const {
  auto *Val = cast_or_null<ConstantAsMetadata>(getModuleFlag("Dwarf Version"));
  if (!Val)
    return 0;
  return cast<ConstantInt>(Val->getValue())->getZExtValue();
}

bool Module::isDwarf64() const {
  auto *Val = cast_or_null<ConstantAsMetadata>(getModuleFlag("DWARF64"));
  return Val && cast<ConstantInt>(Val->getValue())->isOne();
}

unsigned Module::getCodeViewFlag() const {
  auto *Val = cast_or_null<ConstantAsMetadata>(getModuleFlag("CodeView"));
  if (!Val)
    return 0;
  return cast<ConstantInt>(Val->getValue())->getZExtValue();
}

unsigned Module::getInstructionCount() const {
  unsigned NumInstrs = 0;
  for (const Function &F : FunctionList)
    NumInstrs += F.getInstructionCount();
  return NumInstrs;
}

Comdat *Module::getOrInsertComdat(StringRef Name) {
  auto &Entry = *ComdatSymTab.insert(std::make_pair(Name, Comdat())).first;
  Entry.second.Name = &Entry;
  return &Entry.second;
}

PICLevel::Level Module::getPICLevel() const {
  auto *Val = cast_or_null<ConstantAsMetadata>(getModuleFlag("PIC Level"));

  if (!Val)
    return PICLevel::NotPIC;

  return static_cast<PICLevel::Level>(
      cast<ConstantInt>(Val->getValue())->getZExtValue());
}

void Module::setPICLevel(PICLevel::Level PL) {
  // The merge result of a non-PIC object and a PIC object can only be reliably
  // used as a non-PIC object, so use the Min merge behavior.
  addModuleFlag(ModFlagBehavior::Min, "PIC Level", PL);
}

PIELevel::Level Module::getPIELevel() const {
  auto *Val = cast_or_null<ConstantAsMetadata>(getModuleFlag("PIE Level"));

  if (!Val)
    return PIELevel::Default;

  return static_cast<PIELevel::Level>(
      cast<ConstantInt>(Val->getValue())->getZExtValue());
}

void Module::setPIELevel(PIELevel::Level PL) {
  addModuleFlag(ModFlagBehavior::Max, "PIE Level", PL);
}

std::optional<CodeModel::Model> Module::getCodeModel() const {
  auto *Val = cast_or_null<ConstantAsMetadata>(getModuleFlag("Code Model"));

  if (!Val)
    return std::nullopt;

  return static_cast<CodeModel::Model>(
      cast<ConstantInt>(Val->getValue())->getZExtValue());
}

void Module::setCodeModel(CodeModel::Model CL) {
  // Linking object files with different code models is undefined behavior
  // because the compiler would have to generate additional code (to span
  // longer jumps) if a larger code model is used with a smaller one.
  // Therefore we will treat attempts to mix code models as an error.
  addModuleFlag(ModFlagBehavior::Error, "Code Model", CL);
}

std::optional<uint64_t> Module::getLargeDataThreshold() const {
  auto *Val =
      cast_or_null<ConstantAsMetadata>(getModuleFlag("Large Data Threshold"));

  if (!Val)
    return std::nullopt;

  return cast<ConstantInt>(Val->getValue())->getZExtValue();
}

void Module::setLargeDataThreshold(uint64_t Threshold) {
  // Since the large data threshold goes along with the code model, the merge
  // behavior is the same.
  addModuleFlag(ModFlagBehavior::Error, "Large Data Threshold",
                ConstantInt::get(Type::getInt64Ty(Context), Threshold));
}

void Module::setProfileSummary(Metadata *M, ProfileSummary::Kind Kind) {
  if (Kind == ProfileSummary::PSK_CSInstr)
    setModuleFlag(ModFlagBehavior::Error, "CSProfileSummary", M);
  else
    setModuleFlag(ModFlagBehavior::Error, "ProfileSummary", M);
}

Metadata *Module::getProfileSummary(bool IsCS) const {
  return (IsCS ? getModuleFlag("CSProfileSummary")
               : getModuleFlag("ProfileSummary"));
}

bool Module::getSemanticInterposition() const {
  Metadata *MF = getModuleFlag("SemanticInterposition");

  auto *Val = cast_or_null<ConstantAsMetadata>(MF);
  if (!Val)
    return false;

  return cast<ConstantInt>(Val->getValue())->getZExtValue();
}

void Module::setSemanticInterposition(bool SI) {
  addModuleFlag(ModFlagBehavior::Error, "SemanticInterposition", SI);
}

void Module::setOwnedMemoryBuffer(std::unique_ptr<MemoryBuffer> MB) {
  OwnedMemoryBuffer = std::move(MB);
}

bool Module::getRtLibUseGOT() const {
  auto *Val = cast_or_null<ConstantAsMetadata>(getModuleFlag("RtLibUseGOT"));
  return Val && (cast<ConstantInt>(Val->getValue())->getZExtValue() > 0);
}

void Module::setRtLibUseGOT() {
  addModuleFlag(ModFlagBehavior::Max, "RtLibUseGOT", 1);
}

bool Module::getDirectAccessExternalData() const {
  auto *Val = cast_or_null<ConstantAsMetadata>(
      getModuleFlag("direct-access-external-data"));
  if (Val)
    return cast<ConstantInt>(Val->getValue())->getZExtValue() > 0;
  return getPICLevel() == PICLevel::NotPIC;
}

void Module::setDirectAccessExternalData(bool Value) {
  addModuleFlag(ModFlagBehavior::Max, "direct-access-external-data", Value);
}

UWTableKind Module::getUwtable() const {
  if (auto *Val = cast_or_null<ConstantAsMetadata>(getModuleFlag("uwtable")))
    return UWTableKind(cast<ConstantInt>(Val->getValue())->getZExtValue());
  return UWTableKind::None;
}

void Module::setUwtable(UWTableKind Kind) {
  addModuleFlag(ModFlagBehavior::Max, "uwtable", uint32_t(Kind));
}

FramePointerKind Module::getFramePointer() const {
  auto *Val = cast_or_null<ConstantAsMetadata>(getModuleFlag("frame-pointer"));
  return static_cast<FramePointerKind>(
      Val ? cast<ConstantInt>(Val->getValue())->getZExtValue() : 0);
}

void Module::setFramePointer(FramePointerKind Kind) {
  addModuleFlag(ModFlagBehavior::Max, "frame-pointer", static_cast<int>(Kind));
}

StringRef Module::getStackProtectorGuard() const {
  Metadata *MD = getModuleFlag("stack-protector-guard");
  if (auto *MDS = dyn_cast_or_null<MDString>(MD))
    return MDS->getString();
  return {};
}

void Module::setStackProtectorGuard(StringRef Kind) {
  MDString *ID = MDString::get(getContext(), Kind);
  addModuleFlag(ModFlagBehavior::Error, "stack-protector-guard", ID);
}

StringRef Module::getStackProtectorGuardReg() const {
  Metadata *MD = getModuleFlag("stack-protector-guard-reg");
  if (auto *MDS = dyn_cast_or_null<MDString>(MD))
    return MDS->getString();
  return {};
}

void Module::setStackProtectorGuardReg(StringRef Reg) {
  MDString *ID = MDString::get(getContext(), Reg);
  addModuleFlag(ModFlagBehavior::Error, "stack-protector-guard-reg", ID);
}

StringRef Module::getStackProtectorGuardSymbol() const {
  Metadata *MD = getModuleFlag("stack-protector-guard-symbol");
  if (auto *MDS = dyn_cast_or_null<MDString>(MD))
    return MDS->getString();
  return {};
}

void Module::setStackProtectorGuardSymbol(StringRef Symbol) {
  MDString *ID = MDString::get(getContext(), Symbol);
  addModuleFlag(ModFlagBehavior::Error, "stack-protector-guard-symbol", ID);
}

int Module::getStackProtectorGuardOffset() const {
  Metadata *MD = getModuleFlag("stack-protector-guard-offset");
  if (auto *CI = mdconst::dyn_extract_or_null<ConstantInt>(MD))
    return CI->getSExtValue();
  return INT_MAX;
}

void Module::setStackProtectorGuardOffset(int Offset) {
  addModuleFlag(ModFlagBehavior::Error, "stack-protector-guard-offset", Offset);
}

unsigned Module::getOverrideStackAlignment() const {
  Metadata *MD = getModuleFlag("override-stack-alignment");
  if (auto *CI = mdconst::dyn_extract_or_null<ConstantInt>(MD))
    return CI->getZExtValue();
  return 0;
}

unsigned Module::getMaxTLSAlignment() const {
  Metadata *MD = getModuleFlag("MaxTLSAlign");
  if (auto *CI = mdconst::dyn_extract_or_null<ConstantInt>(MD))
    return CI->getZExtValue();
  return 0;
}

void Module::setOverrideStackAlignment(unsigned Align) {
  addModuleFlag(ModFlagBehavior::Error, "override-stack-alignment", Align);
}

static void addSDKVersionMD(const VersionTuple &V, Module &M, StringRef Name) {
  SmallVector<unsigned, 3> Entries;
  Entries.push_back(V.getMajor());
  if (auto Minor = V.getMinor()) {
    Entries.push_back(*Minor);
    if (auto Subminor = V.getSubminor())
      Entries.push_back(*Subminor);
    // Ignore the 'build' component as it can't be represented in the object
    // file.
  }
  M.addModuleFlag(Module::ModFlagBehavior::Warning, Name,
                  ConstantDataArray::get(M.getContext(), Entries));
}

void Module::setSDKVersion(const VersionTuple &V) {
  addSDKVersionMD(V, *this, "SDK Version");
}

static VersionTuple getSDKVersionMD(Metadata *MD) {
  auto *CM = dyn_cast_or_null<ConstantAsMetadata>(MD);
  if (!CM)
    return {};
  auto *Arr = dyn_cast_or_null<ConstantDataArray>(CM->getValue());
  if (!Arr)
    return {};
  auto getVersionComponent = [&](unsigned Index) -> std::optional<unsigned> {
    if (Index >= Arr->getNumElements())
      return std::nullopt;
    return (unsigned)Arr->getElementAsInteger(Index);
  };
  auto Major = getVersionComponent(0);
  if (!Major)
    return {};
  VersionTuple Result = VersionTuple(*Major);
  if (auto Minor = getVersionComponent(1)) {
    Result = VersionTuple(*Major, *Minor);
    if (auto Subminor = getVersionComponent(2)) {
      Result = VersionTuple(*Major, *Minor, *Subminor);
    }
  }
  return Result;
}

VersionTuple Module::getSDKVersion() const {
  return getSDKVersionMD(getModuleFlag("SDK Version"));
}

GlobalVariable *llvm::collectUsedGlobalVariables(
    const Module &M, SmallVectorImpl<GlobalValue *> &Vec, bool CompilerUsed) {
  const char *Name = CompilerUsed ? "llvm.compiler.used" : "llvm.used";
  GlobalVariable *GV = M.getGlobalVariable(Name);
  if (!GV || !GV->hasInitializer())
    return GV;

  const ConstantArray *Init = cast<ConstantArray>(GV->getInitializer());
  for (Value *Op : Init->operands()) {
    GlobalValue *G = cast<GlobalValue>(Op->stripPointerCasts());
    Vec.push_back(G);
  }
  return GV;
}

void Module::setPartialSampleProfileRatio(const ModuleSummaryIndex &Index) {
  if (auto *SummaryMD = getProfileSummary(/*IsCS*/ false)) {
    std::unique_ptr<ProfileSummary> ProfileSummary(
        ProfileSummary::getFromMD(SummaryMD));
    if (ProfileSummary) {
      if (ProfileSummary->getKind() != ProfileSummary::PSK_Sample ||
          !ProfileSummary->isPartialProfile())
        return;
      uint64_t BlockCount = Index.getBlockCount();
      uint32_t NumCounts = ProfileSummary->getNumCounts();
      if (!NumCounts)
        return;
      double Ratio = (double)BlockCount / NumCounts;
      ProfileSummary->setPartialProfileRatio(Ratio);
      setProfileSummary(ProfileSummary->getMD(getContext()),
                        ProfileSummary::PSK_Sample);
    }
  }
}

StringRef Module::getDarwinTargetVariantTriple() const {
  if (const auto *MD = getModuleFlag("darwin.target_variant.triple"))
    return cast<MDString>(MD)->getString();
  return "";
}

void Module::setDarwinTargetVariantTriple(StringRef T) {
  addModuleFlag(ModFlagBehavior::Warning, "darwin.target_variant.triple",
                MDString::get(getContext(), T));
}

VersionTuple Module::getDarwinTargetVariantSDKVersion() const {
  return getSDKVersionMD(getModuleFlag("darwin.target_variant.SDK Version"));
}

void Module::setDarwinTargetVariantSDKVersion(VersionTuple Version) {
  addSDKVersionMD(Version, *this, "darwin.target_variant.SDK Version");
}
