//===-- ModuleUtils.cpp - Functions to manipulate Modules -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This family of functions perform manipulations on Modules.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/ModuleUtils.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/MD5.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/xxhash.h"

using namespace llvm;

#define DEBUG_TYPE "moduleutils"

static void appendToGlobalArray(StringRef ArrayName, Module &M, Function *F,
                                int Priority, Constant *Data) {
  IRBuilder<> IRB(M.getContext());
  FunctionType *FnTy = FunctionType::get(IRB.getVoidTy(), false);

  // Get the current set of static global constructors and add the new ctor
  // to the list.
  SmallVector<Constant *, 16> CurrentCtors;
  StructType *EltTy;
  if (GlobalVariable *GVCtor = M.getNamedGlobal(ArrayName)) {
    EltTy = cast<StructType>(GVCtor->getValueType()->getArrayElementType());
    if (Constant *Init = GVCtor->getInitializer()) {
      unsigned n = Init->getNumOperands();
      CurrentCtors.reserve(n + 1);
      for (unsigned i = 0; i != n; ++i)
        CurrentCtors.push_back(cast<Constant>(Init->getOperand(i)));
    }
    GVCtor->eraseFromParent();
  } else {
    EltTy = StructType::get(IRB.getInt32Ty(),
                            PointerType::get(FnTy, F->getAddressSpace()),
                            IRB.getPtrTy());
  }

  // Build a 3 field global_ctor entry.  We don't take a comdat key.
  Constant *CSVals[3];
  CSVals[0] = IRB.getInt32(Priority);
  CSVals[1] = F;
  CSVals[2] = Data ? ConstantExpr::getPointerCast(Data, IRB.getPtrTy())
                   : Constant::getNullValue(IRB.getPtrTy());
  Constant *RuntimeCtorInit =
      ConstantStruct::get(EltTy, ArrayRef(CSVals, EltTy->getNumElements()));

  CurrentCtors.push_back(RuntimeCtorInit);

  // Create a new initializer.
  ArrayType *AT = ArrayType::get(EltTy, CurrentCtors.size());
  Constant *NewInit = ConstantArray::get(AT, CurrentCtors);

  // Create the new global variable and replace all uses of
  // the old global variable with the new one.
  (void)new GlobalVariable(M, NewInit->getType(), false,
                           GlobalValue::AppendingLinkage, NewInit, ArrayName);
}

void llvm::appendToGlobalCtors(Module &M, Function *F, int Priority, Constant *Data) {
  appendToGlobalArray("llvm.global_ctors", M, F, Priority, Data);
}

void llvm::appendToGlobalDtors(Module &M, Function *F, int Priority, Constant *Data) {
  appendToGlobalArray("llvm.global_dtors", M, F, Priority, Data);
}

static void collectUsedGlobals(GlobalVariable *GV,
                               SmallSetVector<Constant *, 16> &Init) {
  if (!GV || !GV->hasInitializer())
    return;

  auto *CA = cast<ConstantArray>(GV->getInitializer());
  for (Use &Op : CA->operands())
    Init.insert(cast<Constant>(Op));
}

static void appendToUsedList(Module &M, StringRef Name, ArrayRef<GlobalValue *> Values) {
  GlobalVariable *GV = M.getGlobalVariable(Name);

  SmallSetVector<Constant *, 16> Init;
  collectUsedGlobals(GV, Init);
  if (GV)
    GV->eraseFromParent();

  Type *ArrayEltTy = llvm::PointerType::getUnqual(M.getContext());
  for (auto *V : Values)
    Init.insert(ConstantExpr::getPointerBitCastOrAddrSpaceCast(V, ArrayEltTy));

  if (Init.empty())
    return;

  ArrayType *ATy = ArrayType::get(ArrayEltTy, Init.size());
  GV = new llvm::GlobalVariable(M, ATy, false, GlobalValue::AppendingLinkage,
                                ConstantArray::get(ATy, Init.getArrayRef()),
                                Name);
  GV->setSection("llvm.metadata");
}

void llvm::appendToUsed(Module &M, ArrayRef<GlobalValue *> Values) {
  appendToUsedList(M, "llvm.used", Values);
}

void llvm::appendToCompilerUsed(Module &M, ArrayRef<GlobalValue *> Values) {
  appendToUsedList(M, "llvm.compiler.used", Values);
}

static void removeFromUsedList(Module &M, StringRef Name,
                               function_ref<bool(Constant *)> ShouldRemove) {
  GlobalVariable *GV = M.getNamedGlobal(Name);
  if (!GV)
    return;

  SmallSetVector<Constant *, 16> Init;
  collectUsedGlobals(GV, Init);

  Type *ArrayEltTy = cast<ArrayType>(GV->getValueType())->getElementType();

  SmallVector<Constant *, 16> NewInit;
  for (Constant *MaybeRemoved : Init) {
    if (!ShouldRemove(MaybeRemoved->stripPointerCasts()))
      NewInit.push_back(MaybeRemoved);
  }

  if (!NewInit.empty()) {
    ArrayType *ATy = ArrayType::get(ArrayEltTy, NewInit.size());
    GlobalVariable *NewGV =
        new GlobalVariable(M, ATy, false, GlobalValue::AppendingLinkage,
                           ConstantArray::get(ATy, NewInit), "", GV,
                           GV->getThreadLocalMode(), GV->getAddressSpace());
    NewGV->setSection(GV->getSection());
    NewGV->takeName(GV);
  }

  GV->eraseFromParent();
}

void llvm::removeFromUsedLists(Module &M,
                               function_ref<bool(Constant *)> ShouldRemove) {
  removeFromUsedList(M, "llvm.used", ShouldRemove);
  removeFromUsedList(M, "llvm.compiler.used", ShouldRemove);
}

void llvm::setKCFIType(Module &M, Function &F, StringRef MangledType) {
  if (!M.getModuleFlag("kcfi"))
    return;
  // Matches CodeGenModule::CreateKCFITypeId in Clang.
  LLVMContext &Ctx = M.getContext();
  MDBuilder MDB(Ctx);
  std::string Type = MangledType.str();
  if (M.getModuleFlag("cfi-normalize-integers"))
    Type += ".normalized";
  F.setMetadata(LLVMContext::MD_kcfi_type,
                MDNode::get(Ctx, MDB.createConstant(ConstantInt::get(
                                     Type::getInt32Ty(Ctx),
                                     static_cast<uint32_t>(xxHash64(Type))))));
  // If the module was compiled with -fpatchable-function-entry, ensure
  // we use the same patchable-function-prefix.
  if (auto *MD = mdconst::extract_or_null<ConstantInt>(
          M.getModuleFlag("kcfi-offset"))) {
    if (unsigned Offset = MD->getZExtValue())
      F.addFnAttr("patchable-function-prefix", std::to_string(Offset));
  }
}

FunctionCallee llvm::declareSanitizerInitFunction(Module &M, StringRef InitName,
                                                  ArrayRef<Type *> InitArgTypes,
                                                  bool Weak) {
  assert(!InitName.empty() && "Expected init function name");
  auto *VoidTy = Type::getVoidTy(M.getContext());
  auto *FnTy = FunctionType::get(VoidTy, InitArgTypes, false);
  auto FnCallee = M.getOrInsertFunction(InitName, FnTy);
  auto *Fn = cast<Function>(FnCallee.getCallee());
  if (Weak && Fn->isDeclaration())
    Fn->setLinkage(Function::ExternalWeakLinkage);
  return FnCallee;
}

Function *llvm::createSanitizerCtor(Module &M, StringRef CtorName) {
  Function *Ctor = Function::createWithDefaultAttr(
      FunctionType::get(Type::getVoidTy(M.getContext()), false),
      GlobalValue::InternalLinkage, M.getDataLayout().getProgramAddressSpace(),
      CtorName, &M);
  Ctor->addFnAttr(Attribute::NoUnwind);
  setKCFIType(M, *Ctor, "_ZTSFvvE"); // void (*)(void)
  BasicBlock *CtorBB = BasicBlock::Create(M.getContext(), "", Ctor);
  ReturnInst::Create(M.getContext(), CtorBB);
  // Ensure Ctor cannot be discarded, even if in a comdat.
  appendToUsed(M, {Ctor});
  return Ctor;
}

std::pair<Function *, FunctionCallee> llvm::createSanitizerCtorAndInitFunctions(
    Module &M, StringRef CtorName, StringRef InitName,
    ArrayRef<Type *> InitArgTypes, ArrayRef<Value *> InitArgs,
    StringRef VersionCheckName, bool Weak) {
  assert(!InitName.empty() && "Expected init function name");
  assert(InitArgs.size() == InitArgTypes.size() &&
         "Sanitizer's init function expects different number of arguments");
  FunctionCallee InitFunction =
      declareSanitizerInitFunction(M, InitName, InitArgTypes, Weak);
  Function *Ctor = createSanitizerCtor(M, CtorName);
  IRBuilder<> IRB(M.getContext());

  BasicBlock *RetBB = &Ctor->getEntryBlock();
  if (Weak) {
    RetBB->setName("ret");
    auto *EntryBB = BasicBlock::Create(M.getContext(), "entry", Ctor, RetBB);
    auto *CallInitBB =
        BasicBlock::Create(M.getContext(), "callfunc", Ctor, RetBB);
    auto *InitFn = cast<Function>(InitFunction.getCallee());
    auto *InitFnPtr =
        PointerType::get(InitFn->getType(), InitFn->getAddressSpace());
    IRB.SetInsertPoint(EntryBB);
    Value *InitNotNull =
        IRB.CreateICmpNE(InitFn, ConstantPointerNull::get(InitFnPtr));
    IRB.CreateCondBr(InitNotNull, CallInitBB, RetBB);
    IRB.SetInsertPoint(CallInitBB);
  } else {
    IRB.SetInsertPoint(RetBB->getTerminator());
  }

  IRB.CreateCall(InitFunction, InitArgs);
  if (!VersionCheckName.empty()) {
    FunctionCallee VersionCheckFunction = M.getOrInsertFunction(
        VersionCheckName, FunctionType::get(IRB.getVoidTy(), {}, false),
        AttributeList());
    IRB.CreateCall(VersionCheckFunction, {});
  }

  if (Weak)
    IRB.CreateBr(RetBB);

  return std::make_pair(Ctor, InitFunction);
}

std::pair<Function *, FunctionCallee>
llvm::getOrCreateSanitizerCtorAndInitFunctions(
    Module &M, StringRef CtorName, StringRef InitName,
    ArrayRef<Type *> InitArgTypes, ArrayRef<Value *> InitArgs,
    function_ref<void(Function *, FunctionCallee)> FunctionsCreatedCallback,
    StringRef VersionCheckName, bool Weak) {
  assert(!CtorName.empty() && "Expected ctor function name");

  if (Function *Ctor = M.getFunction(CtorName))
    // FIXME: Sink this logic into the module, similar to the handling of
    // globals. This will make moving to a concurrent model much easier.
    if (Ctor->arg_empty() ||
        Ctor->getReturnType() == Type::getVoidTy(M.getContext()))
      return {Ctor,
              declareSanitizerInitFunction(M, InitName, InitArgTypes, Weak)};

  Function *Ctor;
  FunctionCallee InitFunction;
  std::tie(Ctor, InitFunction) = llvm::createSanitizerCtorAndInitFunctions(
      M, CtorName, InitName, InitArgTypes, InitArgs, VersionCheckName, Weak);
  FunctionsCreatedCallback(Ctor, InitFunction);
  return std::make_pair(Ctor, InitFunction);
}

void llvm::filterDeadComdatFunctions(
    SmallVectorImpl<Function *> &DeadComdatFunctions) {
  SmallPtrSet<Function *, 32> MaybeDeadFunctions;
  SmallPtrSet<Comdat *, 32> MaybeDeadComdats;
  for (Function *F : DeadComdatFunctions) {
    MaybeDeadFunctions.insert(F);
    if (Comdat *C = F->getComdat())
      MaybeDeadComdats.insert(C);
  }

  // Find comdats for which all users are dead now.
  SmallPtrSet<Comdat *, 32> DeadComdats;
  for (Comdat *C : MaybeDeadComdats) {
    auto IsUserDead = [&](GlobalObject *GO) {
      auto *F = dyn_cast<Function>(GO);
      return F && MaybeDeadFunctions.contains(F);
    };
    if (all_of(C->getUsers(), IsUserDead))
      DeadComdats.insert(C);
  }

  // Only keep functions which have no comdat or a dead comdat.
  erase_if(DeadComdatFunctions, [&](Function *F) {
    Comdat *C = F->getComdat();
    return C && !DeadComdats.contains(C);
  });
}

std::string llvm::getUniqueModuleId(Module *M) {
  MD5 Md5;
  bool ExportsSymbols = false;
  auto AddGlobal = [&](GlobalValue &GV) {
    if (GV.isDeclaration() || GV.getName().starts_with("llvm.") ||
        !GV.hasExternalLinkage() || GV.hasComdat())
      return;
    ExportsSymbols = true;
    Md5.update(GV.getName());
    Md5.update(ArrayRef<uint8_t>{0});
  };

  for (auto &F : *M)
    AddGlobal(F);
  for (auto &GV : M->globals())
    AddGlobal(GV);
  for (auto &GA : M->aliases())
    AddGlobal(GA);
  for (auto &IF : M->ifuncs())
    AddGlobal(IF);

  if (!ExportsSymbols)
    return "";

  MD5::MD5Result R;
  Md5.final(R);

  SmallString<32> Str;
  MD5::stringifyResult(R, Str);
  return ("." + Str).str();
}

void llvm::embedBufferInModule(Module &M, MemoryBufferRef Buf,
                               StringRef SectionName, Align Alignment) {
  // Embed the memory buffer into the module.
  Constant *ModuleConstant = ConstantDataArray::get(
      M.getContext(), ArrayRef(Buf.getBufferStart(), Buf.getBufferSize()));
  GlobalVariable *GV = new GlobalVariable(
      M, ModuleConstant->getType(), true, GlobalValue::PrivateLinkage,
      ModuleConstant, "llvm.embedded.object");
  GV->setSection(SectionName);
  GV->setAlignment(Alignment);

  LLVMContext &Ctx = M.getContext();
  NamedMDNode *MD = M.getOrInsertNamedMetadata("llvm.embedded.objects");
  Metadata *MDVals[] = {ConstantAsMetadata::get(GV),
                        MDString::get(Ctx, SectionName)};

  MD->addOperand(llvm::MDNode::get(Ctx, MDVals));
  GV->setMetadata(LLVMContext::MD_exclude, llvm::MDNode::get(Ctx, {}));

  appendToCompilerUsed(M, GV);
}

bool llvm::lowerGlobalIFuncUsersAsGlobalCtor(
    Module &M, ArrayRef<GlobalIFunc *> FilteredIFuncsToLower) {
  SmallVector<GlobalIFunc *, 32> AllIFuncs;
  ArrayRef<GlobalIFunc *> IFuncsToLower = FilteredIFuncsToLower;
  if (FilteredIFuncsToLower.empty()) { // Default to lowering all ifuncs
    for (GlobalIFunc &GI : M.ifuncs())
      AllIFuncs.push_back(&GI);
    IFuncsToLower = AllIFuncs;
  }

  bool UnhandledUsers = false;
  LLVMContext &Ctx = M.getContext();
  const DataLayout &DL = M.getDataLayout();

  PointerType *TableEntryTy =
      PointerType::get(Ctx, DL.getProgramAddressSpace());

  ArrayType *FuncPtrTableTy =
      ArrayType::get(TableEntryTy, IFuncsToLower.size());

  Align PtrAlign = DL.getABITypeAlign(TableEntryTy);

  // Create a global table of function pointers we'll initialize in a global
  // constructor.
  auto *FuncPtrTable = new GlobalVariable(
      M, FuncPtrTableTy, false, GlobalValue::InternalLinkage,
      PoisonValue::get(FuncPtrTableTy), "", nullptr,
      GlobalVariable::NotThreadLocal, DL.getDefaultGlobalsAddressSpace());
  FuncPtrTable->setAlignment(PtrAlign);

  // Create a function to initialize the function pointer table.
  Function *NewCtor = Function::Create(
      FunctionType::get(Type::getVoidTy(Ctx), false), Function::InternalLinkage,
      DL.getProgramAddressSpace(), "", &M);

  BasicBlock *BB = BasicBlock::Create(Ctx, "", NewCtor);
  IRBuilder<> InitBuilder(BB);

  size_t TableIndex = 0;
  for (GlobalIFunc *GI : IFuncsToLower) {
    Function *ResolvedFunction = GI->getResolverFunction();

    // We don't know what to pass to a resolver function taking arguments
    //
    // FIXME: Is this even valid? clang and gcc don't complain but this
    // probably should be invalid IR. We could just pass through undef.
    if (!std::empty(ResolvedFunction->getFunctionType()->params())) {
      LLVM_DEBUG(dbgs() << "Not lowering ifunc resolver function "
                        << ResolvedFunction->getName() << " with parameters\n");
      UnhandledUsers = true;
      continue;
    }

    // Initialize the function pointer table.
    CallInst *ResolvedFunc = InitBuilder.CreateCall(ResolvedFunction);
    Value *Casted = InitBuilder.CreatePointerCast(ResolvedFunc, TableEntryTy);
    Constant *GEP = cast<Constant>(InitBuilder.CreateConstInBoundsGEP2_32(
        FuncPtrTableTy, FuncPtrTable, 0, TableIndex++));
    InitBuilder.CreateAlignedStore(Casted, GEP, PtrAlign);

    // Update all users to load a pointer from the global table.
    for (User *User : make_early_inc_range(GI->users())) {
      Instruction *UserInst = dyn_cast<Instruction>(User);
      if (!UserInst) {
        // TODO: Should handle constantexpr casts in user instructions. Probably
        // can't do much about constant initializers.
        UnhandledUsers = true;
        continue;
      }

      IRBuilder<> UseBuilder(UserInst);
      LoadInst *ResolvedTarget =
          UseBuilder.CreateAlignedLoad(TableEntryTy, GEP, PtrAlign);
      Value *ResolvedCast =
          UseBuilder.CreatePointerCast(ResolvedTarget, GI->getType());
      UserInst->replaceUsesOfWith(GI, ResolvedCast);
    }

    // If we handled all users, erase the ifunc.
    if (GI->use_empty())
      GI->eraseFromParent();
  }

  InitBuilder.CreateRetVoid();

  PointerType *ConstantDataTy = PointerType::get(Ctx, 0);

  // TODO: Is this the right priority? Probably should be before any other
  // constructors?
  const int Priority = 10;
  appendToGlobalCtors(M, NewCtor, Priority,
                      ConstantPointerNull::get(ConstantDataTy));
  return UnhandledUsers;
}
