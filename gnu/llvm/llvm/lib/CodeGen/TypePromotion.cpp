//===----- TypePromotion.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This is an opcode based type promotion pass for small types that would
/// otherwise be promoted during legalisation. This works around the limitations
/// of selection dag for cyclic regions. The search begins from icmp
/// instructions operands where a tree, consisting of non-wrapping or safe
/// wrapping instructions, is built, checked and promoted if possible.
///
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/TypePromotion.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Target/TargetMachine.h"

#define DEBUG_TYPE "type-promotion"
#define PASS_NAME "Type Promotion"

using namespace llvm;

static cl::opt<bool> DisablePromotion("disable-type-promotion", cl::Hidden,
                                      cl::init(false),
                                      cl::desc("Disable type promotion pass"));

// The goal of this pass is to enable more efficient code generation for
// operations on narrow types (i.e. types with < 32-bits) and this is a
// motivating IR code example:
//
//   define hidden i32 @cmp(i8 zeroext) {
//     %2 = add i8 %0, -49
//     %3 = icmp ult i8 %2, 3
//     ..
//   }
//
// The issue here is that i8 is type-legalized to i32 because i8 is not a
// legal type. Thus, arithmetic is done in integer-precision, but then the
// byte value is masked out as follows:
//
//   t19: i32 = add t4, Constant:i32<-49>
//     t24: i32 = and t19, Constant:i32<255>
//
// Consequently, we generate code like this:
//
//   subs  r0, #49
//   uxtb  r1, r0
//   cmp r1, #3
//
// This shows that masking out the byte value results in generation of
// the UXTB instruction. This is not optimal as r0 already contains the byte
// value we need, and so instead we can just generate:
//
//   sub.w r1, r0, #49
//   cmp r1, #3
//
// We achieve this by type promoting the IR to i32 like so for this example:
//
//   define i32 @cmp(i8 zeroext %c) {
//     %0 = zext i8 %c to i32
//     %c.off = add i32 %0, -49
//     %1 = icmp ult i32 %c.off, 3
//     ..
//   }
//
// For this to be valid and legal, we need to prove that the i32 add is
// producing the same value as the i8 addition, and that e.g. no overflow
// happens.
//
// A brief sketch of the algorithm and some terminology.
// We pattern match interesting IR patterns:
// - which have "sources": instructions producing narrow values (i8, i16), and
// - they have "sinks": instructions consuming these narrow values.
//
// We collect all instruction connecting sources and sinks in a worklist, so
// that we can mutate these instruction and perform type promotion when it is
// legal to do so.

namespace {
class IRPromoter {
  LLVMContext &Ctx;
  unsigned PromotedWidth = 0;
  SetVector<Value *> &Visited;
  SetVector<Value *> &Sources;
  SetVector<Instruction *> &Sinks;
  SmallPtrSetImpl<Instruction *> &SafeWrap;
  SmallPtrSetImpl<Instruction *> &InstsToRemove;
  IntegerType *ExtTy = nullptr;
  SmallPtrSet<Value *, 8> NewInsts;
  DenseMap<Value *, SmallVector<Type *, 4>> TruncTysMap;
  SmallPtrSet<Value *, 8> Promoted;

  void ReplaceAllUsersOfWith(Value *From, Value *To);
  void ExtendSources();
  void ConvertTruncs();
  void PromoteTree();
  void TruncateSinks();
  void Cleanup();

public:
  IRPromoter(LLVMContext &C, unsigned Width, SetVector<Value *> &visited,
             SetVector<Value *> &sources, SetVector<Instruction *> &sinks,
             SmallPtrSetImpl<Instruction *> &wrap,
             SmallPtrSetImpl<Instruction *> &instsToRemove)
      : Ctx(C), PromotedWidth(Width), Visited(visited), Sources(sources),
        Sinks(sinks), SafeWrap(wrap), InstsToRemove(instsToRemove) {
    ExtTy = IntegerType::get(Ctx, PromotedWidth);
  }

  void Mutate();
};

class TypePromotionImpl {
  unsigned TypeSize = 0;
  const TargetLowering *TLI = nullptr;
  LLVMContext *Ctx = nullptr;
  unsigned RegisterBitWidth = 0;
  SmallPtrSet<Value *, 16> AllVisited;
  SmallPtrSet<Instruction *, 8> SafeToPromote;
  SmallPtrSet<Instruction *, 4> SafeWrap;
  SmallPtrSet<Instruction *, 4> InstsToRemove;

  // Does V have the same size result type as TypeSize.
  bool EqualTypeSize(Value *V);
  // Does V have the same size, or narrower, result type as TypeSize.
  bool LessOrEqualTypeSize(Value *V);
  // Does V have a result type that is wider than TypeSize.
  bool GreaterThanTypeSize(Value *V);
  // Does V have a result type that is narrower than TypeSize.
  bool LessThanTypeSize(Value *V);
  // Should V be a leaf in the promote tree?
  bool isSource(Value *V);
  // Should V be a root in the promotion tree?
  bool isSink(Value *V);
  // Should we change the result type of V? It will result in the users of V
  // being visited.
  bool shouldPromote(Value *V);
  // Is I an add or a sub, which isn't marked as nuw, but where a wrapping
  // result won't affect the computation?
  bool isSafeWrap(Instruction *I);
  // Can V have its integer type promoted, or can the type be ignored.
  bool isSupportedType(Value *V);
  // Is V an instruction with a supported opcode or another value that we can
  // handle, such as constants and basic blocks.
  bool isSupportedValue(Value *V);
  // Is V an instruction thats result can trivially promoted, or has safe
  // wrapping.
  bool isLegalToPromote(Value *V);
  bool TryToPromote(Value *V, unsigned PromotedWidth, const LoopInfo &LI);

public:
  bool run(Function &F, const TargetMachine *TM,
           const TargetTransformInfo &TTI, const LoopInfo &LI);
};

class TypePromotionLegacy : public FunctionPass {
public:
  static char ID;

  TypePromotionLegacy() : FunctionPass(ID) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<LoopInfoWrapperPass>();
    AU.addRequired<TargetTransformInfoWrapperPass>();
    AU.addRequired<TargetPassConfig>();
    AU.setPreservesCFG();
    AU.addPreserved<LoopInfoWrapperPass>();
  }

  StringRef getPassName() const override { return PASS_NAME; }

  bool runOnFunction(Function &F) override;
};

} // namespace

static bool GenerateSignBits(Instruction *I) {
  unsigned Opc = I->getOpcode();
  return Opc == Instruction::AShr || Opc == Instruction::SDiv ||
         Opc == Instruction::SRem || Opc == Instruction::SExt;
}

bool TypePromotionImpl::EqualTypeSize(Value *V) {
  return V->getType()->getScalarSizeInBits() == TypeSize;
}

bool TypePromotionImpl::LessOrEqualTypeSize(Value *V) {
  return V->getType()->getScalarSizeInBits() <= TypeSize;
}

bool TypePromotionImpl::GreaterThanTypeSize(Value *V) {
  return V->getType()->getScalarSizeInBits() > TypeSize;
}

bool TypePromotionImpl::LessThanTypeSize(Value *V) {
  return V->getType()->getScalarSizeInBits() < TypeSize;
}

/// Return true if the given value is a source in the use-def chain, producing
/// a narrow 'TypeSize' value. These values will be zext to start the promotion
/// of the tree to i32. We guarantee that these won't populate the upper bits
/// of the register. ZExt on the loads will be free, and the same for call
/// return values because we only accept ones that guarantee a zeroext ret val.
/// Many arguments will have the zeroext attribute too, so those would be free
/// too.
bool TypePromotionImpl::isSource(Value *V) {
  if (!isa<IntegerType>(V->getType()))
    return false;

  // TODO Allow zext to be sources.
  if (isa<Argument>(V))
    return true;
  else if (isa<LoadInst>(V))
    return true;
  else if (auto *Call = dyn_cast<CallInst>(V))
    return Call->hasRetAttr(Attribute::AttrKind::ZExt);
  else if (auto *Trunc = dyn_cast<TruncInst>(V))
    return EqualTypeSize(Trunc);
  return false;
}

/// Return true if V will require any promoted values to be truncated for the
/// the IR to remain valid. We can't mutate the value type of these
/// instructions.
bool TypePromotionImpl::isSink(Value *V) {
  // TODO The truncate also isn't actually necessary because we would already
  // proved that the data value is kept within the range of the original data
  // type. We currently remove any truncs inserted for handling zext sinks.

  // Sinks are:
  // - points where the value in the register is being observed, such as an
  //   icmp, switch or store.
  // - points where value types have to match, such as calls and returns.
  // - zext are included to ease the transformation and are generally removed
  //   later on.
  if (auto *Store = dyn_cast<StoreInst>(V))
    return LessOrEqualTypeSize(Store->getValueOperand());
  if (auto *Return = dyn_cast<ReturnInst>(V))
    return LessOrEqualTypeSize(Return->getReturnValue());
  if (auto *ZExt = dyn_cast<ZExtInst>(V))
    return GreaterThanTypeSize(ZExt);
  if (auto *Switch = dyn_cast<SwitchInst>(V))
    return LessThanTypeSize(Switch->getCondition());
  if (auto *ICmp = dyn_cast<ICmpInst>(V))
    return ICmp->isSigned() || LessThanTypeSize(ICmp->getOperand(0));

  return isa<CallInst>(V);
}

/// Return whether this instruction can safely wrap.
bool TypePromotionImpl::isSafeWrap(Instruction *I) {
  // We can support a potentially wrapping Add/Sub instruction (I) if:
  // - It is only used by an unsigned icmp.
  // - The icmp uses a constant.
  // - The wrapping instruction (I) also uses a constant.
  //
  // This a common pattern emitted to check if a value is within a range.
  //
  // For example:
  //
  // %sub = sub i8 %a, C1
  // %cmp = icmp ule i8 %sub, C2
  //
  // or
  //
  // %add = add i8 %a, C1
  // %cmp = icmp ule i8 %add, C2.
  //
  // We will treat an add as though it were a subtract by -C1. To promote
  // the Add/Sub we will zero extend the LHS and the subtracted amount. For Add,
  // this means we need to negate the constant, zero extend to RegisterBitWidth,
  // and negate in the larger type.
  //
  // This will produce a value in the range [-zext(C1), zext(X)-zext(C1)] where
  // C1 is the subtracted amount. This is either a small unsigned number or a
  // large unsigned number in the promoted type.
  //
  // Now we need to correct the compare constant C2. Values >= C1 in the
  // original add result range have been remapped to large values in the
  // promoted range. If the compare constant fell into this range we need to
  // remap it as well. We can do this as -(zext(-C2)).
  //
  // For example:
  //
  // %sub = sub i8 %a, 2
  // %cmp = icmp ule i8 %sub, 254
  //
  // becomes
  //
  // %zext = zext %a to i32
  // %sub = sub i32 %zext, 2
  // %cmp = icmp ule i32 %sub, 4294967294
  //
  // Another example:
  //
  // %sub = sub i8 %a, 1
  // %cmp = icmp ule i8 %sub, 254
  //
  // becomes
  //
  // %zext = zext %a to i32
  // %sub = sub i32 %zext, 1
  // %cmp = icmp ule i32 %sub, 254

  unsigned Opc = I->getOpcode();
  if (Opc != Instruction::Add && Opc != Instruction::Sub)
    return false;

  if (!I->hasOneUse() || !isa<ICmpInst>(*I->user_begin()) ||
      !isa<ConstantInt>(I->getOperand(1)))
    return false;

  // Don't support an icmp that deals with sign bits.
  auto *CI = cast<ICmpInst>(*I->user_begin());
  if (CI->isSigned() || CI->isEquality())
    return false;

  ConstantInt *ICmpConstant = nullptr;
  if (auto *Const = dyn_cast<ConstantInt>(CI->getOperand(0)))
    ICmpConstant = Const;
  else if (auto *Const = dyn_cast<ConstantInt>(CI->getOperand(1)))
    ICmpConstant = Const;
  else
    return false;

  const APInt &ICmpConst = ICmpConstant->getValue();
  APInt OverflowConst = cast<ConstantInt>(I->getOperand(1))->getValue();
  if (Opc == Instruction::Sub)
    OverflowConst = -OverflowConst;

  // If the constant is positive, we will end up filling the promoted bits with
  // all 1s. Make sure that results in a cheap add constant.
  if (!OverflowConst.isNonPositive()) {
    // We don't have the true promoted width, just use 64 so we can create an
    // int64_t for the isLegalAddImmediate call.
    if (OverflowConst.getBitWidth() >= 64)
      return false;

    APInt NewConst = -((-OverflowConst).zext(64));
    if (!TLI->isLegalAddImmediate(NewConst.getSExtValue()))
      return false;
  }

  SafeWrap.insert(I);

  if (OverflowConst == 0 || OverflowConst.ugt(ICmpConst)) {
    LLVM_DEBUG(dbgs() << "IR Promotion: Allowing safe overflow for "
                      << "const of " << *I << "\n");
    return true;
  }

  LLVM_DEBUG(dbgs() << "IR Promotion: Allowing safe overflow for "
                    << "const of " << *I << " and " << *CI << "\n");
  SafeWrap.insert(CI);
  return true;
}

bool TypePromotionImpl::shouldPromote(Value *V) {
  if (!isa<IntegerType>(V->getType()) || isSink(V))
    return false;

  if (isSource(V))
    return true;

  auto *I = dyn_cast<Instruction>(V);
  if (!I)
    return false;

  if (isa<ICmpInst>(I))
    return false;

  return true;
}

/// Return whether we can safely mutate V's type to ExtTy without having to be
/// concerned with zero extending or truncation.
static bool isPromotedResultSafe(Instruction *I) {
  if (GenerateSignBits(I))
    return false;

  if (!isa<OverflowingBinaryOperator>(I))
    return true;

  return I->hasNoUnsignedWrap();
}

void IRPromoter::ReplaceAllUsersOfWith(Value *From, Value *To) {
  SmallVector<Instruction *, 4> Users;
  Instruction *InstTo = dyn_cast<Instruction>(To);
  bool ReplacedAll = true;

  LLVM_DEBUG(dbgs() << "IR Promotion: Replacing " << *From << " with " << *To
                    << "\n");

  for (Use &U : From->uses()) {
    auto *User = cast<Instruction>(U.getUser());
    if (InstTo && User->isIdenticalTo(InstTo)) {
      ReplacedAll = false;
      continue;
    }
    Users.push_back(User);
  }

  for (auto *U : Users)
    U->replaceUsesOfWith(From, To);

  if (ReplacedAll)
    if (auto *I = dyn_cast<Instruction>(From))
      InstsToRemove.insert(I);
}

void IRPromoter::ExtendSources() {
  IRBuilder<> Builder{Ctx};

  auto InsertZExt = [&](Value *V, Instruction *InsertPt) {
    assert(V->getType() != ExtTy && "zext already extends to i32");
    LLVM_DEBUG(dbgs() << "IR Promotion: Inserting ZExt for " << *V << "\n");
    Builder.SetInsertPoint(InsertPt);
    if (auto *I = dyn_cast<Instruction>(V))
      Builder.SetCurrentDebugLocation(I->getDebugLoc());

    Value *ZExt = Builder.CreateZExt(V, ExtTy);
    if (auto *I = dyn_cast<Instruction>(ZExt)) {
      if (isa<Argument>(V))
        I->moveBefore(InsertPt);
      else
        I->moveAfter(InsertPt);
      NewInsts.insert(I);
    }

    ReplaceAllUsersOfWith(V, ZExt);
  };

  // Now, insert extending instructions between the sources and their users.
  LLVM_DEBUG(dbgs() << "IR Promotion: Promoting sources:\n");
  for (auto *V : Sources) {
    LLVM_DEBUG(dbgs() << " - " << *V << "\n");
    if (auto *I = dyn_cast<Instruction>(V))
      InsertZExt(I, I);
    else if (auto *Arg = dyn_cast<Argument>(V)) {
      BasicBlock &BB = Arg->getParent()->front();
      InsertZExt(Arg, &*BB.getFirstInsertionPt());
    } else {
      llvm_unreachable("unhandled source that needs extending");
    }
    Promoted.insert(V);
  }
}

void IRPromoter::PromoteTree() {
  LLVM_DEBUG(dbgs() << "IR Promotion: Mutating the tree..\n");

  // Mutate the types of the instructions within the tree. Here we handle
  // constant operands.
  for (auto *V : Visited) {
    if (Sources.count(V))
      continue;

    auto *I = cast<Instruction>(V);
    if (Sinks.count(I))
      continue;

    for (unsigned i = 0, e = I->getNumOperands(); i < e; ++i) {
      Value *Op = I->getOperand(i);
      if ((Op->getType() == ExtTy) || !isa<IntegerType>(Op->getType()))
        continue;

      if (auto *Const = dyn_cast<ConstantInt>(Op)) {
        // For subtract, we only need to zext the constant. We only put it in
        // SafeWrap because SafeWrap.size() is used elsewhere.
        // For Add and ICmp we need to find how far the constant is from the
        // top of its original unsigned range and place it the same distance
        // from the top of its new unsigned range. We can do this by negating
        // the constant, zero extending it, then negating in the new type.
        APInt NewConst;
        if (SafeWrap.contains(I)) {
          if (I->getOpcode() == Instruction::ICmp)
            NewConst = -((-Const->getValue()).zext(PromotedWidth));
          else if (I->getOpcode() == Instruction::Add && i == 1)
            NewConst = -((-Const->getValue()).zext(PromotedWidth));
          else
            NewConst = Const->getValue().zext(PromotedWidth);
        } else
          NewConst = Const->getValue().zext(PromotedWidth);

        I->setOperand(i, ConstantInt::get(Const->getContext(), NewConst));
      } else if (isa<UndefValue>(Op))
        I->setOperand(i, ConstantInt::get(ExtTy, 0));
    }

    // Mutate the result type, unless this is an icmp or switch.
    if (!isa<ICmpInst>(I) && !isa<SwitchInst>(I)) {
      I->mutateType(ExtTy);
      Promoted.insert(I);
    }
  }
}

void IRPromoter::TruncateSinks() {
  LLVM_DEBUG(dbgs() << "IR Promotion: Fixing up the sinks:\n");

  IRBuilder<> Builder{Ctx};

  auto InsertTrunc = [&](Value *V, Type *TruncTy) -> Instruction * {
    if (!isa<Instruction>(V) || !isa<IntegerType>(V->getType()))
      return nullptr;

    if ((!Promoted.count(V) && !NewInsts.count(V)) || Sources.count(V))
      return nullptr;

    LLVM_DEBUG(dbgs() << "IR Promotion: Creating " << *TruncTy << " Trunc for "
                      << *V << "\n");
    Builder.SetInsertPoint(cast<Instruction>(V));
    auto *Trunc = dyn_cast<Instruction>(Builder.CreateTrunc(V, TruncTy));
    if (Trunc)
      NewInsts.insert(Trunc);
    return Trunc;
  };

  // Fix up any stores or returns that use the results of the promoted
  // chain.
  for (auto *I : Sinks) {
    LLVM_DEBUG(dbgs() << "IR Promotion: For Sink: " << *I << "\n");

    // Handle calls separately as we need to iterate over arg operands.
    if (auto *Call = dyn_cast<CallInst>(I)) {
      for (unsigned i = 0; i < Call->arg_size(); ++i) {
        Value *Arg = Call->getArgOperand(i);
        Type *Ty = TruncTysMap[Call][i];
        if (Instruction *Trunc = InsertTrunc(Arg, Ty)) {
          Trunc->moveBefore(Call);
          Call->setArgOperand(i, Trunc);
        }
      }
      continue;
    }

    // Special case switches because we need to truncate the condition.
    if (auto *Switch = dyn_cast<SwitchInst>(I)) {
      Type *Ty = TruncTysMap[Switch][0];
      if (Instruction *Trunc = InsertTrunc(Switch->getCondition(), Ty)) {
        Trunc->moveBefore(Switch);
        Switch->setCondition(Trunc);
      }
      continue;
    }

    // Don't insert a trunc for a zext which can still legally promote.
    // Nor insert a trunc when the input value to that trunc has the same width
    // as the zext we are inserting it for.  When this happens the input operand
    // for the zext will be promoted to the same width as the zext's return type
    // rendering that zext unnecessary.  This zext gets removed before the end
    // of the pass.
    if (auto ZExt = dyn_cast<ZExtInst>(I))
      if (ZExt->getType()->getScalarSizeInBits() >= PromotedWidth)
        continue;

    // Now handle the others.
    for (unsigned i = 0; i < I->getNumOperands(); ++i) {
      Type *Ty = TruncTysMap[I][i];
      if (Instruction *Trunc = InsertTrunc(I->getOperand(i), Ty)) {
        Trunc->moveBefore(I);
        I->setOperand(i, Trunc);
      }
    }
  }
}

void IRPromoter::Cleanup() {
  LLVM_DEBUG(dbgs() << "IR Promotion: Cleanup..\n");
  // Some zexts will now have become redundant, along with their trunc
  // operands, so remove them.
  for (auto *V : Visited) {
    if (!isa<ZExtInst>(V))
      continue;

    auto ZExt = cast<ZExtInst>(V);
    if (ZExt->getDestTy() != ExtTy)
      continue;

    Value *Src = ZExt->getOperand(0);
    if (ZExt->getSrcTy() == ZExt->getDestTy()) {
      LLVM_DEBUG(dbgs() << "IR Promotion: Removing unnecessary cast: " << *ZExt
                        << "\n");
      ReplaceAllUsersOfWith(ZExt, Src);
      continue;
    }

    // We've inserted a trunc for a zext sink, but we already know that the
    // input is in range, negating the need for the trunc.
    if (NewInsts.count(Src) && isa<TruncInst>(Src)) {
      auto *Trunc = cast<TruncInst>(Src);
      assert(Trunc->getOperand(0)->getType() == ExtTy &&
             "expected inserted trunc to be operating on i32");
      ReplaceAllUsersOfWith(ZExt, Trunc->getOperand(0));
    }
  }

  for (auto *I : InstsToRemove) {
    LLVM_DEBUG(dbgs() << "IR Promotion: Removing " << *I << "\n");
    I->dropAllReferences();
  }
}

void IRPromoter::ConvertTruncs() {
  LLVM_DEBUG(dbgs() << "IR Promotion: Converting truncs..\n");
  IRBuilder<> Builder{Ctx};

  for (auto *V : Visited) {
    if (!isa<TruncInst>(V) || Sources.count(V))
      continue;

    auto *Trunc = cast<TruncInst>(V);
    Builder.SetInsertPoint(Trunc);
    IntegerType *SrcTy = cast<IntegerType>(Trunc->getOperand(0)->getType());
    IntegerType *DestTy = cast<IntegerType>(TruncTysMap[Trunc][0]);

    unsigned NumBits = DestTy->getScalarSizeInBits();
    ConstantInt *Mask =
        ConstantInt::get(SrcTy, APInt::getMaxValue(NumBits).getZExtValue());
    Value *Masked = Builder.CreateAnd(Trunc->getOperand(0), Mask);
    if (SrcTy->getBitWidth() > ExtTy->getBitWidth())
      Masked = Builder.CreateTrunc(Masked, ExtTy);

    if (auto *I = dyn_cast<Instruction>(Masked))
      NewInsts.insert(I);

    ReplaceAllUsersOfWith(Trunc, Masked);
  }
}

void IRPromoter::Mutate() {
  LLVM_DEBUG(dbgs() << "IR Promotion: Promoting use-def chains to "
                    << PromotedWidth << "-bits\n");

  // Cache original types of the values that will likely need truncating
  for (auto *I : Sinks) {
    if (auto *Call = dyn_cast<CallInst>(I)) {
      for (Value *Arg : Call->args())
        TruncTysMap[Call].push_back(Arg->getType());
    } else if (auto *Switch = dyn_cast<SwitchInst>(I))
      TruncTysMap[I].push_back(Switch->getCondition()->getType());
    else {
      for (unsigned i = 0; i < I->getNumOperands(); ++i)
        TruncTysMap[I].push_back(I->getOperand(i)->getType());
    }
  }
  for (auto *V : Visited) {
    if (!isa<TruncInst>(V) || Sources.count(V))
      continue;
    auto *Trunc = cast<TruncInst>(V);
    TruncTysMap[Trunc].push_back(Trunc->getDestTy());
  }

  // Insert zext instructions between sources and their users.
  ExtendSources();

  // Promote visited instructions, mutating their types in place.
  PromoteTree();

  // Convert any truncs, that aren't sources, into AND masks.
  ConvertTruncs();

  // Insert trunc instructions for use by calls, stores etc...
  TruncateSinks();

  // Finally, remove unecessary zexts and truncs, delete old instructions and
  // clear the data structures.
  Cleanup();

  LLVM_DEBUG(dbgs() << "IR Promotion: Mutation complete\n");
}

/// We disallow booleans to make life easier when dealing with icmps but allow
/// any other integer that fits in a scalar register. Void types are accepted
/// so we can handle switches.
bool TypePromotionImpl::isSupportedType(Value *V) {
  Type *Ty = V->getType();

  // Allow voids and pointers, these won't be promoted.
  if (Ty->isVoidTy() || Ty->isPointerTy())
    return true;

  if (!isa<IntegerType>(Ty) || cast<IntegerType>(Ty)->getBitWidth() == 1 ||
      cast<IntegerType>(Ty)->getBitWidth() > RegisterBitWidth)
    return false;

  return LessOrEqualTypeSize(V);
}

/// We accept most instructions, as well as Arguments and ConstantInsts. We
/// Disallow casts other than zext and truncs and only allow calls if their
/// return value is zeroext. We don't allow opcodes that can introduce sign
/// bits.
bool TypePromotionImpl::isSupportedValue(Value *V) {
  if (auto *I = dyn_cast<Instruction>(V)) {
    switch (I->getOpcode()) {
    default:
      return isa<BinaryOperator>(I) && isSupportedType(I) &&
             !GenerateSignBits(I);
    case Instruction::GetElementPtr:
    case Instruction::Store:
    case Instruction::Br:
    case Instruction::Switch:
      return true;
    case Instruction::PHI:
    case Instruction::Select:
    case Instruction::Ret:
    case Instruction::Load:
    case Instruction::Trunc:
      return isSupportedType(I);
    case Instruction::BitCast:
      return I->getOperand(0)->getType() == I->getType();
    case Instruction::ZExt:
      return isSupportedType(I->getOperand(0));
    case Instruction::ICmp:
      // Now that we allow small types than TypeSize, only allow icmp of
      // TypeSize because they will require a trunc to be legalised.
      // TODO: Allow icmp of smaller types, and calculate at the end
      // whether the transform would be beneficial.
      if (isa<PointerType>(I->getOperand(0)->getType()))
        return true;
      return EqualTypeSize(I->getOperand(0));
    case Instruction::Call: {
      // Special cases for calls as we need to check for zeroext
      // TODO We should accept calls even if they don't have zeroext, as they
      // can still be sinks.
      auto *Call = cast<CallInst>(I);
      return isSupportedType(Call) &&
             Call->hasRetAttr(Attribute::AttrKind::ZExt);
    }
    }
  } else if (isa<Constant>(V) && !isa<ConstantExpr>(V)) {
    return isSupportedType(V);
  } else if (isa<Argument>(V))
    return isSupportedType(V);

  return isa<BasicBlock>(V);
}

/// Check that the type of V would be promoted and that the original type is
/// smaller than the targeted promoted type. Check that we're not trying to
/// promote something larger than our base 'TypeSize' type.
bool TypePromotionImpl::isLegalToPromote(Value *V) {
  auto *I = dyn_cast<Instruction>(V);
  if (!I)
    return true;

  if (SafeToPromote.count(I))
    return true;

  if (isPromotedResultSafe(I) || isSafeWrap(I)) {
    SafeToPromote.insert(I);
    return true;
  }
  return false;
}

bool TypePromotionImpl::TryToPromote(Value *V, unsigned PromotedWidth,
                                 const LoopInfo &LI) {
  Type *OrigTy = V->getType();
  TypeSize = OrigTy->getPrimitiveSizeInBits().getFixedValue();
  SafeToPromote.clear();
  SafeWrap.clear();

  if (!isSupportedValue(V) || !shouldPromote(V) || !isLegalToPromote(V))
    return false;

  LLVM_DEBUG(dbgs() << "IR Promotion: TryToPromote: " << *V << ", from "
                    << TypeSize << " bits to " << PromotedWidth << "\n");

  SetVector<Value *> WorkList;
  SetVector<Value *> Sources;
  SetVector<Instruction *> Sinks;
  SetVector<Value *> CurrentVisited;
  WorkList.insert(V);

  // Return true if V was added to the worklist as a supported instruction,
  // if it was already visited, or if we don't need to explore it (e.g.
  // pointer values and GEPs), and false otherwise.
  auto AddLegalInst = [&](Value *V) {
    if (CurrentVisited.count(V))
      return true;

    // Ignore GEPs because they don't need promoting and the constant indices
    // will prevent the transformation.
    if (isa<GetElementPtrInst>(V))
      return true;

    if (!isSupportedValue(V) || (shouldPromote(V) && !isLegalToPromote(V))) {
      LLVM_DEBUG(dbgs() << "IR Promotion: Can't handle: " << *V << "\n");
      return false;
    }

    WorkList.insert(V);
    return true;
  };

  // Iterate through, and add to, a tree of operands and users in the use-def.
  while (!WorkList.empty()) {
    Value *V = WorkList.pop_back_val();
    if (CurrentVisited.count(V))
      continue;

    // Ignore non-instructions, other than arguments.
    if (!isa<Instruction>(V) && !isSource(V))
      continue;

    // If we've already visited this value from somewhere, bail now because
    // the tree has already been explored.
    // TODO: This could limit the transform, ie if we try to promote something
    // from an i8 and fail first, before trying an i16.
    if (AllVisited.count(V))
      return false;

    CurrentVisited.insert(V);
    AllVisited.insert(V);

    // Calls can be both sources and sinks.
    if (isSink(V))
      Sinks.insert(cast<Instruction>(V));

    if (isSource(V))
      Sources.insert(V);

    if (!isSink(V) && !isSource(V)) {
      if (auto *I = dyn_cast<Instruction>(V)) {
        // Visit operands of any instruction visited.
        for (auto &U : I->operands()) {
          if (!AddLegalInst(U))
            return false;
        }
      }
    }

    // Don't visit users of a node which isn't going to be mutated unless its a
    // source.
    if (isSource(V) || shouldPromote(V)) {
      for (Use &U : V->uses()) {
        if (!AddLegalInst(U.getUser()))
          return false;
      }
    }
  }

  LLVM_DEBUG({
    dbgs() << "IR Promotion: Visited nodes:\n";
    for (auto *I : CurrentVisited)
      I->dump();
  });

  unsigned ToPromote = 0;
  unsigned NonFreeArgs = 0;
  unsigned NonLoopSources = 0, LoopSinks = 0;
  SmallPtrSet<BasicBlock *, 4> Blocks;
  for (auto *CV : CurrentVisited) {
    if (auto *I = dyn_cast<Instruction>(CV))
      Blocks.insert(I->getParent());

    if (Sources.count(CV)) {
      if (auto *Arg = dyn_cast<Argument>(CV))
        if (!Arg->hasZExtAttr() && !Arg->hasSExtAttr())
          ++NonFreeArgs;
      if (!isa<Instruction>(CV) ||
          !LI.getLoopFor(cast<Instruction>(CV)->getParent()))
        ++NonLoopSources;
      continue;
    }

    if (isa<PHINode>(CV))
      continue;
    if (LI.getLoopFor(cast<Instruction>(CV)->getParent()))
      ++LoopSinks;
    if (Sinks.count(cast<Instruction>(CV)))
      continue;
    ++ToPromote;
  }

  // DAG optimizations should be able to handle these cases better, especially
  // for function arguments.
  if (!isa<PHINode>(V) && !(LoopSinks && NonLoopSources) &&
      (ToPromote < 2 || (Blocks.size() == 1 && NonFreeArgs > SafeWrap.size())))
    return false;

  IRPromoter Promoter(*Ctx, PromotedWidth, CurrentVisited, Sources, Sinks,
                      SafeWrap, InstsToRemove);
  Promoter.Mutate();
  return true;
}

bool TypePromotionImpl::run(Function &F, const TargetMachine *TM,
                            const TargetTransformInfo &TTI,
                            const LoopInfo &LI) {
  if (DisablePromotion)
    return false;

  LLVM_DEBUG(dbgs() << "IR Promotion: Running on " << F.getName() << "\n");

  AllVisited.clear();
  SafeToPromote.clear();
  SafeWrap.clear();
  bool MadeChange = false;
  const DataLayout &DL = F.getDataLayout();
  const TargetSubtargetInfo *SubtargetInfo = TM->getSubtargetImpl(F);
  TLI = SubtargetInfo->getTargetLowering();
  RegisterBitWidth =
      TTI.getRegisterBitWidth(TargetTransformInfo::RGK_Scalar).getFixedValue();
  Ctx = &F.getContext();

  // Return the preferred integer width of the instruction, or zero if we
  // shouldn't try.
  auto GetPromoteWidth = [&](Instruction *I) -> uint32_t {
    if (!isa<IntegerType>(I->getType()))
      return 0;

    EVT SrcVT = TLI->getValueType(DL, I->getType());
    if (SrcVT.isSimple() && TLI->isTypeLegal(SrcVT.getSimpleVT()))
      return 0;

    if (TLI->getTypeAction(*Ctx, SrcVT) != TargetLowering::TypePromoteInteger)
      return 0;

    EVT PromotedVT = TLI->getTypeToTransformTo(*Ctx, SrcVT);
    if (TLI->isSExtCheaperThanZExt(SrcVT, PromotedVT))
      return 0;
    if (RegisterBitWidth < PromotedVT.getFixedSizeInBits()) {
      LLVM_DEBUG(dbgs() << "IR Promotion: Couldn't find target register "
                        << "for promoted type\n");
      return 0;
    }

    // TODO: Should we prefer to use RegisterBitWidth instead?
    return PromotedVT.getFixedSizeInBits();
  };

  auto BBIsInLoop = [&](BasicBlock *BB) -> bool {
    for (auto *L : LI)
      if (L->contains(BB))
        return true;
    return false;
  };

  for (BasicBlock &BB : F) {
    for (Instruction &I : BB) {
      if (AllVisited.count(&I))
        continue;

      if (isa<ZExtInst>(&I) && isa<PHINode>(I.getOperand(0)) &&
          isa<IntegerType>(I.getType()) && BBIsInLoop(&BB)) {
        LLVM_DEBUG(dbgs() << "IR Promotion: Searching from: "
                          << *I.getOperand(0) << "\n");
        EVT ZExtVT = TLI->getValueType(DL, I.getType());
        Instruction *Phi = static_cast<Instruction *>(I.getOperand(0));
        auto PromoteWidth = ZExtVT.getFixedSizeInBits();
        if (RegisterBitWidth < PromoteWidth) {
          LLVM_DEBUG(dbgs() << "IR Promotion: Couldn't find target "
                            << "register for ZExt type\n");
          continue;
        }
        MadeChange |= TryToPromote(Phi, PromoteWidth, LI);
      } else if (auto *ICmp = dyn_cast<ICmpInst>(&I)) {
        // Search up from icmps to try to promote their operands.
        // Skip signed or pointer compares
        if (ICmp->isSigned())
          continue;

        LLVM_DEBUG(dbgs() << "IR Promotion: Searching from: " << *ICmp << "\n");

        for (auto &Op : ICmp->operands()) {
          if (auto *OpI = dyn_cast<Instruction>(Op)) {
            if (auto PromotedWidth = GetPromoteWidth(OpI)) {
              MadeChange |= TryToPromote(OpI, PromotedWidth, LI);
              break;
            }
          }
        }
      }
    }
    if (!InstsToRemove.empty()) {
      for (auto *I : InstsToRemove)
        I->eraseFromParent();
      InstsToRemove.clear();
    }
  }

  AllVisited.clear();
  SafeToPromote.clear();
  SafeWrap.clear();

  return MadeChange;
}

INITIALIZE_PASS_BEGIN(TypePromotionLegacy, DEBUG_TYPE, PASS_NAME, false, false)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(TargetPassConfig)
INITIALIZE_PASS_DEPENDENCY(TargetTransformInfoWrapperPass)
INITIALIZE_PASS_END(TypePromotionLegacy, DEBUG_TYPE, PASS_NAME, false, false)

char TypePromotionLegacy::ID = 0;

bool TypePromotionLegacy::runOnFunction(Function &F) {
  if (skipFunction(F))
    return false;

  auto &TPC = getAnalysis<TargetPassConfig>();
  auto *TM = &TPC.getTM<TargetMachine>();
  auto &TTI = getAnalysis<TargetTransformInfoWrapperPass>().getTTI(F);
  auto &LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();

  TypePromotionImpl TP;
  return TP.run(F, TM, TTI, LI);
}

FunctionPass *llvm::createTypePromotionLegacyPass() {
  return new TypePromotionLegacy();
}

PreservedAnalyses TypePromotionPass::run(Function &F,
                                         FunctionAnalysisManager &AM) {
  auto &TTI = AM.getResult<TargetIRAnalysis>(F);
  auto &LI = AM.getResult<LoopAnalysis>(F);
  TypePromotionImpl TP;

  bool Changed = TP.run(F, TM, TTI, LI);
  if (!Changed)
    return PreservedAnalyses::all();

  PreservedAnalyses PA;
  PA.preserveSet<CFGAnalyses>();
  PA.preserve<LoopAnalysis>();
  return PA;
}
