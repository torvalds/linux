//===- InstCombineAddSub.cpp ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the visit functions for add, fadd, sub, and fsub.
//
//===----------------------------------------------------------------------===//

#include "InstCombineInternal.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/AlignOf.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Transforms/InstCombine/InstCombiner.h"
#include <cassert>
#include <utility>

using namespace llvm;
using namespace PatternMatch;

#define DEBUG_TYPE "instcombine"

namespace {

  /// Class representing coefficient of floating-point addend.
  /// This class needs to be highly efficient, which is especially true for
  /// the constructor. As of I write this comment, the cost of the default
  /// constructor is merely 4-byte-store-zero (Assuming compiler is able to
  /// perform write-merging).
  ///
  class FAddendCoef {
  public:
    // The constructor has to initialize a APFloat, which is unnecessary for
    // most addends which have coefficient either 1 or -1. So, the constructor
    // is expensive. In order to avoid the cost of the constructor, we should
    // reuse some instances whenever possible. The pre-created instances
    // FAddCombine::Add[0-5] embodies this idea.
    FAddendCoef() = default;
    ~FAddendCoef();

    // If possible, don't define operator+/operator- etc because these
    // operators inevitably call FAddendCoef's constructor which is not cheap.
    void operator=(const FAddendCoef &A);
    void operator+=(const FAddendCoef &A);
    void operator*=(const FAddendCoef &S);

    void set(short C) {
      assert(!insaneIntVal(C) && "Insane coefficient");
      IsFp = false; IntVal = C;
    }

    void set(const APFloat& C);

    void negate();

    bool isZero() const { return isInt() ? !IntVal : getFpVal().isZero(); }
    Value *getValue(Type *) const;

    bool isOne() const { return isInt() && IntVal == 1; }
    bool isTwo() const { return isInt() && IntVal == 2; }
    bool isMinusOne() const { return isInt() && IntVal == -1; }
    bool isMinusTwo() const { return isInt() && IntVal == -2; }

  private:
    bool insaneIntVal(int V) { return V > 4 || V < -4; }

    APFloat *getFpValPtr() { return reinterpret_cast<APFloat *>(&FpValBuf); }

    const APFloat *getFpValPtr() const {
      return reinterpret_cast<const APFloat *>(&FpValBuf);
    }

    const APFloat &getFpVal() const {
      assert(IsFp && BufHasFpVal && "Incorret state");
      return *getFpValPtr();
    }

    APFloat &getFpVal() {
      assert(IsFp && BufHasFpVal && "Incorret state");
      return *getFpValPtr();
    }

    bool isInt() const { return !IsFp; }

    // If the coefficient is represented by an integer, promote it to a
    // floating point.
    void convertToFpType(const fltSemantics &Sem);

    // Construct an APFloat from a signed integer.
    // TODO: We should get rid of this function when APFloat can be constructed
    //       from an *SIGNED* integer.
    APFloat createAPFloatFromInt(const fltSemantics &Sem, int Val);

    bool IsFp = false;

    // True iff FpValBuf contains an instance of APFloat.
    bool BufHasFpVal = false;

    // The integer coefficient of an individual addend is either 1 or -1,
    // and we try to simplify at most 4 addends from neighboring at most
    // two instructions. So the range of <IntVal> falls in [-4, 4]. APInt
    // is overkill of this end.
    short IntVal = 0;

    AlignedCharArrayUnion<APFloat> FpValBuf;
  };

  /// FAddend is used to represent floating-point addend. An addend is
  /// represented as <C, V>, where the V is a symbolic value, and C is a
  /// constant coefficient. A constant addend is represented as <C, 0>.
  class FAddend {
  public:
    FAddend() = default;

    void operator+=(const FAddend &T) {
      assert((Val == T.Val) && "Symbolic-values disagree");
      Coeff += T.Coeff;
    }

    Value *getSymVal() const { return Val; }
    const FAddendCoef &getCoef() const { return Coeff; }

    bool isConstant() const { return Val == nullptr; }
    bool isZero() const { return Coeff.isZero(); }

    void set(short Coefficient, Value *V) {
      Coeff.set(Coefficient);
      Val = V;
    }
    void set(const APFloat &Coefficient, Value *V) {
      Coeff.set(Coefficient);
      Val = V;
    }
    void set(const ConstantFP *Coefficient, Value *V) {
      Coeff.set(Coefficient->getValueAPF());
      Val = V;
    }

    void negate() { Coeff.negate(); }

    /// Drill down the U-D chain one step to find the definition of V, and
    /// try to break the definition into one or two addends.
    static unsigned drillValueDownOneStep(Value* V, FAddend &A0, FAddend &A1);

    /// Similar to FAddend::drillDownOneStep() except that the value being
    /// splitted is the addend itself.
    unsigned drillAddendDownOneStep(FAddend &Addend0, FAddend &Addend1) const;

  private:
    void Scale(const FAddendCoef& ScaleAmt) { Coeff *= ScaleAmt; }

    // This addend has the value of "Coeff * Val".
    Value *Val = nullptr;
    FAddendCoef Coeff;
  };

  /// FAddCombine is the class for optimizing an unsafe fadd/fsub along
  /// with its neighboring at most two instructions.
  ///
  class FAddCombine {
  public:
    FAddCombine(InstCombiner::BuilderTy &B) : Builder(B) {}

    Value *simplify(Instruction *FAdd);

  private:
    using AddendVect = SmallVector<const FAddend *, 4>;

    Value *simplifyFAdd(AddendVect& V, unsigned InstrQuota);

    /// Convert given addend to a Value
    Value *createAddendVal(const FAddend &A, bool& NeedNeg);

    /// Return the number of instructions needed to emit the N-ary addition.
    unsigned calcInstrNumber(const AddendVect& Vect);

    Value *createFSub(Value *Opnd0, Value *Opnd1);
    Value *createFAdd(Value *Opnd0, Value *Opnd1);
    Value *createFMul(Value *Opnd0, Value *Opnd1);
    Value *createFNeg(Value *V);
    Value *createNaryFAdd(const AddendVect& Opnds, unsigned InstrQuota);
    void createInstPostProc(Instruction *NewInst, bool NoNumber = false);

     // Debugging stuff are clustered here.
    #ifndef NDEBUG
      unsigned CreateInstrNum;
      void initCreateInstNum() { CreateInstrNum = 0; }
      void incCreateInstNum() { CreateInstrNum++; }
    #else
      void initCreateInstNum() {}
      void incCreateInstNum() {}
    #endif

    InstCombiner::BuilderTy &Builder;
    Instruction *Instr = nullptr;
  };

} // end anonymous namespace

//===----------------------------------------------------------------------===//
//
// Implementation of
//    {FAddendCoef, FAddend, FAddition, FAddCombine}.
//
//===----------------------------------------------------------------------===//
FAddendCoef::~FAddendCoef() {
  if (BufHasFpVal)
    getFpValPtr()->~APFloat();
}

void FAddendCoef::set(const APFloat& C) {
  APFloat *P = getFpValPtr();

  if (isInt()) {
    // As the buffer is meanless byte stream, we cannot call
    // APFloat::operator=().
    new(P) APFloat(C);
  } else
    *P = C;

  IsFp = BufHasFpVal = true;
}

void FAddendCoef::convertToFpType(const fltSemantics &Sem) {
  if (!isInt())
    return;

  APFloat *P = getFpValPtr();
  if (IntVal > 0)
    new(P) APFloat(Sem, IntVal);
  else {
    new(P) APFloat(Sem, 0 - IntVal);
    P->changeSign();
  }
  IsFp = BufHasFpVal = true;
}

APFloat FAddendCoef::createAPFloatFromInt(const fltSemantics &Sem, int Val) {
  if (Val >= 0)
    return APFloat(Sem, Val);

  APFloat T(Sem, 0 - Val);
  T.changeSign();

  return T;
}

void FAddendCoef::operator=(const FAddendCoef &That) {
  if (That.isInt())
    set(That.IntVal);
  else
    set(That.getFpVal());
}

void FAddendCoef::operator+=(const FAddendCoef &That) {
  RoundingMode RndMode = RoundingMode::NearestTiesToEven;
  if (isInt() == That.isInt()) {
    if (isInt())
      IntVal += That.IntVal;
    else
      getFpVal().add(That.getFpVal(), RndMode);
    return;
  }

  if (isInt()) {
    const APFloat &T = That.getFpVal();
    convertToFpType(T.getSemantics());
    getFpVal().add(T, RndMode);
    return;
  }

  APFloat &T = getFpVal();
  T.add(createAPFloatFromInt(T.getSemantics(), That.IntVal), RndMode);
}

void FAddendCoef::operator*=(const FAddendCoef &That) {
  if (That.isOne())
    return;

  if (That.isMinusOne()) {
    negate();
    return;
  }

  if (isInt() && That.isInt()) {
    int Res = IntVal * (int)That.IntVal;
    assert(!insaneIntVal(Res) && "Insane int value");
    IntVal = Res;
    return;
  }

  const fltSemantics &Semantic =
    isInt() ? That.getFpVal().getSemantics() : getFpVal().getSemantics();

  if (isInt())
    convertToFpType(Semantic);
  APFloat &F0 = getFpVal();

  if (That.isInt())
    F0.multiply(createAPFloatFromInt(Semantic, That.IntVal),
                APFloat::rmNearestTiesToEven);
  else
    F0.multiply(That.getFpVal(), APFloat::rmNearestTiesToEven);
}

void FAddendCoef::negate() {
  if (isInt())
    IntVal = 0 - IntVal;
  else
    getFpVal().changeSign();
}

Value *FAddendCoef::getValue(Type *Ty) const {
  return isInt() ?
    ConstantFP::get(Ty, float(IntVal)) :
    ConstantFP::get(Ty->getContext(), getFpVal());
}

// The definition of <Val>     Addends
// =========================================
//  A + B                     <1, A>, <1,B>
//  A - B                     <1, A>, <1,B>
//  0 - B                     <-1, B>
//  C * A,                    <C, A>
//  A + C                     <1, A> <C, NULL>
//  0 +/- 0                   <0, NULL> (corner case)
//
// Legend: A and B are not constant, C is constant
unsigned FAddend::drillValueDownOneStep
  (Value *Val, FAddend &Addend0, FAddend &Addend1) {
  Instruction *I = nullptr;
  if (!Val || !(I = dyn_cast<Instruction>(Val)))
    return 0;

  unsigned Opcode = I->getOpcode();

  if (Opcode == Instruction::FAdd || Opcode == Instruction::FSub) {
    ConstantFP *C0, *C1;
    Value *Opnd0 = I->getOperand(0);
    Value *Opnd1 = I->getOperand(1);
    if ((C0 = dyn_cast<ConstantFP>(Opnd0)) && C0->isZero())
      Opnd0 = nullptr;

    if ((C1 = dyn_cast<ConstantFP>(Opnd1)) && C1->isZero())
      Opnd1 = nullptr;

    if (Opnd0) {
      if (!C0)
        Addend0.set(1, Opnd0);
      else
        Addend0.set(C0, nullptr);
    }

    if (Opnd1) {
      FAddend &Addend = Opnd0 ? Addend1 : Addend0;
      if (!C1)
        Addend.set(1, Opnd1);
      else
        Addend.set(C1, nullptr);
      if (Opcode == Instruction::FSub)
        Addend.negate();
    }

    if (Opnd0 || Opnd1)
      return Opnd0 && Opnd1 ? 2 : 1;

    // Both operands are zero. Weird!
    Addend0.set(APFloat(C0->getValueAPF().getSemantics()), nullptr);
    return 1;
  }

  if (I->getOpcode() == Instruction::FMul) {
    Value *V0 = I->getOperand(0);
    Value *V1 = I->getOperand(1);
    if (ConstantFP *C = dyn_cast<ConstantFP>(V0)) {
      Addend0.set(C, V1);
      return 1;
    }

    if (ConstantFP *C = dyn_cast<ConstantFP>(V1)) {
      Addend0.set(C, V0);
      return 1;
    }
  }

  return 0;
}

// Try to break *this* addend into two addends. e.g. Suppose this addend is
// <2.3, V>, and V = X + Y, by calling this function, we obtain two addends,
// i.e. <2.3, X> and <2.3, Y>.
unsigned FAddend::drillAddendDownOneStep
  (FAddend &Addend0, FAddend &Addend1) const {
  if (isConstant())
    return 0;

  unsigned BreakNum = FAddend::drillValueDownOneStep(Val, Addend0, Addend1);
  if (!BreakNum || Coeff.isOne())
    return BreakNum;

  Addend0.Scale(Coeff);

  if (BreakNum == 2)
    Addend1.Scale(Coeff);

  return BreakNum;
}

Value *FAddCombine::simplify(Instruction *I) {
  assert(I->hasAllowReassoc() && I->hasNoSignedZeros() &&
         "Expected 'reassoc'+'nsz' instruction");

  // Currently we are not able to handle vector type.
  if (I->getType()->isVectorTy())
    return nullptr;

  assert((I->getOpcode() == Instruction::FAdd ||
          I->getOpcode() == Instruction::FSub) && "Expect add/sub");

  // Save the instruction before calling other member-functions.
  Instr = I;

  FAddend Opnd0, Opnd1, Opnd0_0, Opnd0_1, Opnd1_0, Opnd1_1;

  unsigned OpndNum = FAddend::drillValueDownOneStep(I, Opnd0, Opnd1);

  // Step 1: Expand the 1st addend into Opnd0_0 and Opnd0_1.
  unsigned Opnd0_ExpNum = 0;
  unsigned Opnd1_ExpNum = 0;

  if (!Opnd0.isConstant())
    Opnd0_ExpNum = Opnd0.drillAddendDownOneStep(Opnd0_0, Opnd0_1);

  // Step 2: Expand the 2nd addend into Opnd1_0 and Opnd1_1.
  if (OpndNum == 2 && !Opnd1.isConstant())
    Opnd1_ExpNum = Opnd1.drillAddendDownOneStep(Opnd1_0, Opnd1_1);

  // Step 3: Try to optimize Opnd0_0 + Opnd0_1 + Opnd1_0 + Opnd1_1
  if (Opnd0_ExpNum && Opnd1_ExpNum) {
    AddendVect AllOpnds;
    AllOpnds.push_back(&Opnd0_0);
    AllOpnds.push_back(&Opnd1_0);
    if (Opnd0_ExpNum == 2)
      AllOpnds.push_back(&Opnd0_1);
    if (Opnd1_ExpNum == 2)
      AllOpnds.push_back(&Opnd1_1);

    // Compute instruction quota. We should save at least one instruction.
    unsigned InstQuota = 0;

    Value *V0 = I->getOperand(0);
    Value *V1 = I->getOperand(1);
    InstQuota = ((!isa<Constant>(V0) && V0->hasOneUse()) &&
                 (!isa<Constant>(V1) && V1->hasOneUse())) ? 2 : 1;

    if (Value *R = simplifyFAdd(AllOpnds, InstQuota))
      return R;
  }

  if (OpndNum != 2) {
    // The input instruction is : "I=0.0 +/- V". If the "V" were able to be
    // splitted into two addends, say "V = X - Y", the instruction would have
    // been optimized into "I = Y - X" in the previous steps.
    //
    const FAddendCoef &CE = Opnd0.getCoef();
    return CE.isOne() ? Opnd0.getSymVal() : nullptr;
  }

  // step 4: Try to optimize Opnd0 + Opnd1_0 [+ Opnd1_1]
  if (Opnd1_ExpNum) {
    AddendVect AllOpnds;
    AllOpnds.push_back(&Opnd0);
    AllOpnds.push_back(&Opnd1_0);
    if (Opnd1_ExpNum == 2)
      AllOpnds.push_back(&Opnd1_1);

    if (Value *R = simplifyFAdd(AllOpnds, 1))
      return R;
  }

  // step 5: Try to optimize Opnd1 + Opnd0_0 [+ Opnd0_1]
  if (Opnd0_ExpNum) {
    AddendVect AllOpnds;
    AllOpnds.push_back(&Opnd1);
    AllOpnds.push_back(&Opnd0_0);
    if (Opnd0_ExpNum == 2)
      AllOpnds.push_back(&Opnd0_1);

    if (Value *R = simplifyFAdd(AllOpnds, 1))
      return R;
  }

  return nullptr;
}

Value *FAddCombine::simplifyFAdd(AddendVect& Addends, unsigned InstrQuota) {
  unsigned AddendNum = Addends.size();
  assert(AddendNum <= 4 && "Too many addends");

  // For saving intermediate results;
  unsigned NextTmpIdx = 0;
  FAddend TmpResult[3];

  // Simplified addends are placed <SimpVect>.
  AddendVect SimpVect;

  // The outer loop works on one symbolic-value at a time. Suppose the input
  // addends are : <a1, x>, <b1, y>, <a2, x>, <c1, z>, <b2, y>, ...
  // The symbolic-values will be processed in this order: x, y, z.
  for (unsigned SymIdx = 0; SymIdx < AddendNum; SymIdx++) {

    const FAddend *ThisAddend = Addends[SymIdx];
    if (!ThisAddend) {
      // This addend was processed before.
      continue;
    }

    Value *Val = ThisAddend->getSymVal();

    // If the resulting expr has constant-addend, this constant-addend is
    // desirable to reside at the top of the resulting expression tree. Placing
    // constant close to super-expr(s) will potentially reveal some
    // optimization opportunities in super-expr(s). Here we do not implement
    // this logic intentionally and rely on SimplifyAssociativeOrCommutative
    // call later.

    unsigned StartIdx = SimpVect.size();
    SimpVect.push_back(ThisAddend);

    // The inner loop collects addends sharing same symbolic-value, and these
    // addends will be later on folded into a single addend. Following above
    // example, if the symbolic value "y" is being processed, the inner loop
    // will collect two addends "<b1,y>" and "<b2,Y>". These two addends will
    // be later on folded into "<b1+b2, y>".
    for (unsigned SameSymIdx = SymIdx + 1;
         SameSymIdx < AddendNum; SameSymIdx++) {
      const FAddend *T = Addends[SameSymIdx];
      if (T && T->getSymVal() == Val) {
        // Set null such that next iteration of the outer loop will not process
        // this addend again.
        Addends[SameSymIdx] = nullptr;
        SimpVect.push_back(T);
      }
    }

    // If multiple addends share same symbolic value, fold them together.
    if (StartIdx + 1 != SimpVect.size()) {
      FAddend &R = TmpResult[NextTmpIdx ++];
      R = *SimpVect[StartIdx];
      for (unsigned Idx = StartIdx + 1; Idx < SimpVect.size(); Idx++)
        R += *SimpVect[Idx];

      // Pop all addends being folded and push the resulting folded addend.
      SimpVect.resize(StartIdx);
      if (!R.isZero()) {
        SimpVect.push_back(&R);
      }
    }
  }

  assert((NextTmpIdx <= std::size(TmpResult) + 1) && "out-of-bound access");

  Value *Result;
  if (!SimpVect.empty())
    Result = createNaryFAdd(SimpVect, InstrQuota);
  else {
    // The addition is folded to 0.0.
    Result = ConstantFP::get(Instr->getType(), 0.0);
  }

  return Result;
}

Value *FAddCombine::createNaryFAdd
  (const AddendVect &Opnds, unsigned InstrQuota) {
  assert(!Opnds.empty() && "Expect at least one addend");

  // Step 1: Check if the # of instructions needed exceeds the quota.

  unsigned InstrNeeded = calcInstrNumber(Opnds);
  if (InstrNeeded > InstrQuota)
    return nullptr;

  initCreateInstNum();

  // step 2: Emit the N-ary addition.
  // Note that at most three instructions are involved in Fadd-InstCombine: the
  // addition in question, and at most two neighboring instructions.
  // The resulting optimized addition should have at least one less instruction
  // than the original addition expression tree. This implies that the resulting
  // N-ary addition has at most two instructions, and we don't need to worry
  // about tree-height when constructing the N-ary addition.

  Value *LastVal = nullptr;
  bool LastValNeedNeg = false;

  // Iterate the addends, creating fadd/fsub using adjacent two addends.
  for (const FAddend *Opnd : Opnds) {
    bool NeedNeg;
    Value *V = createAddendVal(*Opnd, NeedNeg);
    if (!LastVal) {
      LastVal = V;
      LastValNeedNeg = NeedNeg;
      continue;
    }

    if (LastValNeedNeg == NeedNeg) {
      LastVal = createFAdd(LastVal, V);
      continue;
    }

    if (LastValNeedNeg)
      LastVal = createFSub(V, LastVal);
    else
      LastVal = createFSub(LastVal, V);

    LastValNeedNeg = false;
  }

  if (LastValNeedNeg) {
    LastVal = createFNeg(LastVal);
  }

#ifndef NDEBUG
  assert(CreateInstrNum == InstrNeeded &&
         "Inconsistent in instruction numbers");
#endif

  return LastVal;
}

Value *FAddCombine::createFSub(Value *Opnd0, Value *Opnd1) {
  Value *V = Builder.CreateFSub(Opnd0, Opnd1);
  if (Instruction *I = dyn_cast<Instruction>(V))
    createInstPostProc(I);
  return V;
}

Value *FAddCombine::createFNeg(Value *V) {
  Value *NewV = Builder.CreateFNeg(V);
  if (Instruction *I = dyn_cast<Instruction>(NewV))
    createInstPostProc(I, true); // fneg's don't receive instruction numbers.
  return NewV;
}

Value *FAddCombine::createFAdd(Value *Opnd0, Value *Opnd1) {
  Value *V = Builder.CreateFAdd(Opnd0, Opnd1);
  if (Instruction *I = dyn_cast<Instruction>(V))
    createInstPostProc(I);
  return V;
}

Value *FAddCombine::createFMul(Value *Opnd0, Value *Opnd1) {
  Value *V = Builder.CreateFMul(Opnd0, Opnd1);
  if (Instruction *I = dyn_cast<Instruction>(V))
    createInstPostProc(I);
  return V;
}

void FAddCombine::createInstPostProc(Instruction *NewInstr, bool NoNumber) {
  NewInstr->setDebugLoc(Instr->getDebugLoc());

  // Keep track of the number of instruction created.
  if (!NoNumber)
    incCreateInstNum();

  // Propagate fast-math flags
  NewInstr->setFastMathFlags(Instr->getFastMathFlags());
}

// Return the number of instruction needed to emit the N-ary addition.
// NOTE: Keep this function in sync with createAddendVal().
unsigned FAddCombine::calcInstrNumber(const AddendVect &Opnds) {
  unsigned OpndNum = Opnds.size();
  unsigned InstrNeeded = OpndNum - 1;

  // Adjust the number of instructions needed to emit the N-ary add.
  for (const FAddend *Opnd : Opnds) {
    if (Opnd->isConstant())
      continue;

    // The constant check above is really for a few special constant
    // coefficients.
    if (isa<UndefValue>(Opnd->getSymVal()))
      continue;

    const FAddendCoef &CE = Opnd->getCoef();
    // Let the addend be "c * x". If "c == +/-1", the value of the addend
    // is immediately available; otherwise, it needs exactly one instruction
    // to evaluate the value.
    if (!CE.isMinusOne() && !CE.isOne())
      InstrNeeded++;
  }
  return InstrNeeded;
}

// Input Addend        Value           NeedNeg(output)
// ================================================================
// Constant C          C               false
// <+/-1, V>           V               coefficient is -1
// <2/-2, V>          "fadd V, V"      coefficient is -2
// <C, V>             "fmul V, C"      false
//
// NOTE: Keep this function in sync with FAddCombine::calcInstrNumber.
Value *FAddCombine::createAddendVal(const FAddend &Opnd, bool &NeedNeg) {
  const FAddendCoef &Coeff = Opnd.getCoef();

  if (Opnd.isConstant()) {
    NeedNeg = false;
    return Coeff.getValue(Instr->getType());
  }

  Value *OpndVal = Opnd.getSymVal();

  if (Coeff.isMinusOne() || Coeff.isOne()) {
    NeedNeg = Coeff.isMinusOne();
    return OpndVal;
  }

  if (Coeff.isTwo() || Coeff.isMinusTwo()) {
    NeedNeg = Coeff.isMinusTwo();
    return createFAdd(OpndVal, OpndVal);
  }

  NeedNeg = false;
  return createFMul(OpndVal, Coeff.getValue(Instr->getType()));
}

// Checks if any operand is negative and we can convert add to sub.
// This function checks for following negative patterns
//   ADD(XOR(OR(Z, NOT(C)), C)), 1) == NEG(AND(Z, C))
//   ADD(XOR(AND(Z, C), C), 1) == NEG(OR(Z, ~C))
//   XOR(AND(Z, C), (C + 1)) == NEG(OR(Z, ~C)) if C is even
static Value *checkForNegativeOperand(BinaryOperator &I,
                                      InstCombiner::BuilderTy &Builder) {
  Value *LHS = I.getOperand(0), *RHS = I.getOperand(1);

  // This function creates 2 instructions to replace ADD, we need at least one
  // of LHS or RHS to have one use to ensure benefit in transform.
  if (!LHS->hasOneUse() && !RHS->hasOneUse())
    return nullptr;

  Value *X = nullptr, *Y = nullptr, *Z = nullptr;
  const APInt *C1 = nullptr, *C2 = nullptr;

  // if ONE is on other side, swap
  if (match(RHS, m_Add(m_Value(X), m_One())))
    std::swap(LHS, RHS);

  if (match(LHS, m_Add(m_Value(X), m_One()))) {
    // if XOR on other side, swap
    if (match(RHS, m_Xor(m_Value(Y), m_APInt(C1))))
      std::swap(X, RHS);

    if (match(X, m_Xor(m_Value(Y), m_APInt(C1)))) {
      // X = XOR(Y, C1), Y = OR(Z, C2), C2 = NOT(C1) ==> X == NOT(AND(Z, C1))
      // ADD(ADD(X, 1), RHS) == ADD(X, ADD(RHS, 1)) == SUB(RHS, AND(Z, C1))
      if (match(Y, m_Or(m_Value(Z), m_APInt(C2))) && (*C2 == ~(*C1))) {
        Value *NewAnd = Builder.CreateAnd(Z, *C1);
        return Builder.CreateSub(RHS, NewAnd, "sub");
      } else if (match(Y, m_And(m_Value(Z), m_APInt(C2))) && (*C1 == *C2)) {
        // X = XOR(Y, C1), Y = AND(Z, C2), C2 == C1 ==> X == NOT(OR(Z, ~C1))
        // ADD(ADD(X, 1), RHS) == ADD(X, ADD(RHS, 1)) == SUB(RHS, OR(Z, ~C1))
        Value *NewOr = Builder.CreateOr(Z, ~(*C1));
        return Builder.CreateSub(RHS, NewOr, "sub");
      }
    }
  }

  // Restore LHS and RHS
  LHS = I.getOperand(0);
  RHS = I.getOperand(1);

  // if XOR is on other side, swap
  if (match(RHS, m_Xor(m_Value(Y), m_APInt(C1))))
    std::swap(LHS, RHS);

  // C2 is ODD
  // LHS = XOR(Y, C1), Y = AND(Z, C2), C1 == (C2 + 1) => LHS == NEG(OR(Z, ~C2))
  // ADD(LHS, RHS) == SUB(RHS, OR(Z, ~C2))
  if (match(LHS, m_Xor(m_Value(Y), m_APInt(C1))))
    if (C1->countr_zero() == 0)
      if (match(Y, m_And(m_Value(Z), m_APInt(C2))) && *C1 == (*C2 + 1)) {
        Value *NewOr = Builder.CreateOr(Z, ~(*C2));
        return Builder.CreateSub(RHS, NewOr, "sub");
      }
  return nullptr;
}

/// Wrapping flags may allow combining constants separated by an extend.
static Instruction *foldNoWrapAdd(BinaryOperator &Add,
                                  InstCombiner::BuilderTy &Builder) {
  Value *Op0 = Add.getOperand(0), *Op1 = Add.getOperand(1);
  Type *Ty = Add.getType();
  Constant *Op1C;
  if (!match(Op1, m_Constant(Op1C)))
    return nullptr;

  // Try this match first because it results in an add in the narrow type.
  // (zext (X +nuw C2)) + C1 --> zext (X + (C2 + trunc(C1)))
  Value *X;
  const APInt *C1, *C2;
  if (match(Op1, m_APInt(C1)) &&
      match(Op0, m_ZExt(m_NUWAddLike(m_Value(X), m_APInt(C2)))) &&
      C1->isNegative() && C1->sge(-C2->sext(C1->getBitWidth()))) {
    APInt NewC = *C2 + C1->trunc(C2->getBitWidth());
    // If the smaller add will fold to zero, we don't need to check one use.
    if (NewC.isZero())
      return new ZExtInst(X, Ty);
    // Otherwise only do this if the existing zero extend will be removed.
    if (Op0->hasOneUse())
      return new ZExtInst(
          Builder.CreateNUWAdd(X, ConstantInt::get(X->getType(), NewC)), Ty);
  }

  // More general combining of constants in the wide type.
  // (sext (X +nsw NarrowC)) + C --> (sext X) + (sext(NarrowC) + C)
  // or (zext nneg (X +nsw NarrowC)) + C --> (sext X) + (sext(NarrowC) + C)
  Constant *NarrowC;
  if (match(Op0, m_OneUse(m_SExtLike(
                     m_NSWAddLike(m_Value(X), m_Constant(NarrowC)))))) {
    Value *WideC = Builder.CreateSExt(NarrowC, Ty);
    Value *NewC = Builder.CreateAdd(WideC, Op1C);
    Value *WideX = Builder.CreateSExt(X, Ty);
    return BinaryOperator::CreateAdd(WideX, NewC);
  }
  // (zext (X +nuw NarrowC)) + C --> (zext X) + (zext(NarrowC) + C)
  if (match(Op0,
            m_OneUse(m_ZExt(m_NUWAddLike(m_Value(X), m_Constant(NarrowC)))))) {
    Value *WideC = Builder.CreateZExt(NarrowC, Ty);
    Value *NewC = Builder.CreateAdd(WideC, Op1C);
    Value *WideX = Builder.CreateZExt(X, Ty);
    return BinaryOperator::CreateAdd(WideX, NewC);
  }
  return nullptr;
}

Instruction *InstCombinerImpl::foldAddWithConstant(BinaryOperator &Add) {
  Value *Op0 = Add.getOperand(0), *Op1 = Add.getOperand(1);
  Type *Ty = Add.getType();
  Constant *Op1C;
  if (!match(Op1, m_ImmConstant(Op1C)))
    return nullptr;

  if (Instruction *NV = foldBinOpIntoSelectOrPhi(Add))
    return NV;

  Value *X;
  Constant *Op00C;

  // add (sub C1, X), C2 --> sub (add C1, C2), X
  if (match(Op0, m_Sub(m_Constant(Op00C), m_Value(X))))
    return BinaryOperator::CreateSub(ConstantExpr::getAdd(Op00C, Op1C), X);

  Value *Y;

  // add (sub X, Y), -1 --> add (not Y), X
  if (match(Op0, m_OneUse(m_Sub(m_Value(X), m_Value(Y)))) &&
      match(Op1, m_AllOnes()))
    return BinaryOperator::CreateAdd(Builder.CreateNot(Y), X);

  // zext(bool) + C -> bool ? C + 1 : C
  if (match(Op0, m_ZExt(m_Value(X))) &&
      X->getType()->getScalarSizeInBits() == 1)
    return SelectInst::Create(X, InstCombiner::AddOne(Op1C), Op1);
  // sext(bool) + C -> bool ? C - 1 : C
  if (match(Op0, m_SExt(m_Value(X))) &&
      X->getType()->getScalarSizeInBits() == 1)
    return SelectInst::Create(X, InstCombiner::SubOne(Op1C), Op1);

  // ~X + C --> (C-1) - X
  if (match(Op0, m_Not(m_Value(X)))) {
    // ~X + C has NSW and (C-1) won't oveflow => (C-1)-X can have NSW
    auto *COne = ConstantInt::get(Op1C->getType(), 1);
    bool WillNotSOV = willNotOverflowSignedSub(Op1C, COne, Add);
    BinaryOperator *Res =
        BinaryOperator::CreateSub(ConstantExpr::getSub(Op1C, COne), X);
    Res->setHasNoSignedWrap(Add.hasNoSignedWrap() && WillNotSOV);
    return Res;
  }

  // (iN X s>> (N - 1)) + 1 --> zext (X > -1)
  const APInt *C;
  unsigned BitWidth = Ty->getScalarSizeInBits();
  if (match(Op0, m_OneUse(m_AShr(m_Value(X),
                                 m_SpecificIntAllowPoison(BitWidth - 1)))) &&
      match(Op1, m_One()))
    return new ZExtInst(Builder.CreateIsNotNeg(X, "isnotneg"), Ty);

  if (!match(Op1, m_APInt(C)))
    return nullptr;

  // (X | Op01C) + Op1C --> X + (Op01C + Op1C) iff the `or` is actually an `add`
  Constant *Op01C;
  if (match(Op0, m_DisjointOr(m_Value(X), m_ImmConstant(Op01C)))) {
    BinaryOperator *NewAdd =
        BinaryOperator::CreateAdd(X, ConstantExpr::getAdd(Op01C, Op1C));
    NewAdd->setHasNoSignedWrap(Add.hasNoSignedWrap() &&
                               willNotOverflowSignedAdd(Op01C, Op1C, Add));
    NewAdd->setHasNoUnsignedWrap(Add.hasNoUnsignedWrap());
    return NewAdd;
  }

  // (X | C2) + C --> (X | C2) ^ C2 iff (C2 == -C)
  const APInt *C2;
  if (match(Op0, m_Or(m_Value(), m_APInt(C2))) && *C2 == -*C)
    return BinaryOperator::CreateXor(Op0, ConstantInt::get(Add.getType(), *C2));

  if (C->isSignMask()) {
    // If wrapping is not allowed, then the addition must set the sign bit:
    // X + (signmask) --> X | signmask
    if (Add.hasNoSignedWrap() || Add.hasNoUnsignedWrap())
      return BinaryOperator::CreateOr(Op0, Op1);

    // If wrapping is allowed, then the addition flips the sign bit of LHS:
    // X + (signmask) --> X ^ signmask
    return BinaryOperator::CreateXor(Op0, Op1);
  }

  // Is this add the last step in a convoluted sext?
  // add(zext(xor i16 X, -32768), -32768) --> sext X
  if (match(Op0, m_ZExt(m_Xor(m_Value(X), m_APInt(C2)))) &&
      C2->isMinSignedValue() && C2->sext(Ty->getScalarSizeInBits()) == *C)
    return CastInst::Create(Instruction::SExt, X, Ty);

  if (match(Op0, m_Xor(m_Value(X), m_APInt(C2)))) {
    // (X ^ signmask) + C --> (X + (signmask ^ C))
    if (C2->isSignMask())
      return BinaryOperator::CreateAdd(X, ConstantInt::get(Ty, *C2 ^ *C));

    // If X has no high-bits set above an xor mask:
    // add (xor X, LowMaskC), C --> sub (LowMaskC + C), X
    if (C2->isMask()) {
      KnownBits LHSKnown = computeKnownBits(X, 0, &Add);
      if ((*C2 | LHSKnown.Zero).isAllOnes())
        return BinaryOperator::CreateSub(ConstantInt::get(Ty, *C2 + *C), X);
    }

    // Look for a math+logic pattern that corresponds to sext-in-register of a
    // value with cleared high bits. Convert that into a pair of shifts:
    // add (xor X, 0x80), 0xF..F80 --> (X << ShAmtC) >>s ShAmtC
    // add (xor X, 0xF..F80), 0x80 --> (X << ShAmtC) >>s ShAmtC
    if (Op0->hasOneUse() && *C2 == -(*C)) {
      unsigned BitWidth = Ty->getScalarSizeInBits();
      unsigned ShAmt = 0;
      if (C->isPowerOf2())
        ShAmt = BitWidth - C->logBase2() - 1;
      else if (C2->isPowerOf2())
        ShAmt = BitWidth - C2->logBase2() - 1;
      if (ShAmt && MaskedValueIsZero(X, APInt::getHighBitsSet(BitWidth, ShAmt),
                                     0, &Add)) {
        Constant *ShAmtC = ConstantInt::get(Ty, ShAmt);
        Value *NewShl = Builder.CreateShl(X, ShAmtC, "sext");
        return BinaryOperator::CreateAShr(NewShl, ShAmtC);
      }
    }
  }

  if (C->isOne() && Op0->hasOneUse()) {
    // add (sext i1 X), 1 --> zext (not X)
    // TODO: The smallest IR representation is (select X, 0, 1), and that would
    // not require the one-use check. But we need to remove a transform in
    // visitSelect and make sure that IR value tracking for select is equal or
    // better than for these ops.
    if (match(Op0, m_SExt(m_Value(X))) &&
        X->getType()->getScalarSizeInBits() == 1)
      return new ZExtInst(Builder.CreateNot(X), Ty);

    // Shifts and add used to flip and mask off the low bit:
    // add (ashr (shl i32 X, 31), 31), 1 --> and (not X), 1
    const APInt *C3;
    if (match(Op0, m_AShr(m_Shl(m_Value(X), m_APInt(C2)), m_APInt(C3))) &&
        C2 == C3 && *C2 == Ty->getScalarSizeInBits() - 1) {
      Value *NotX = Builder.CreateNot(X);
      return BinaryOperator::CreateAnd(NotX, ConstantInt::get(Ty, 1));
    }
  }

  // Fold (add (zext (add X, -1)), 1) -> (zext X) if X is non-zero.
  // TODO: There's a general form for any constant on the outer add.
  if (C->isOne()) {
    if (match(Op0, m_ZExt(m_Add(m_Value(X), m_AllOnes())))) {
      const SimplifyQuery Q = SQ.getWithInstruction(&Add);
      if (llvm::isKnownNonZero(X, Q))
        return new ZExtInst(X, Ty);
    }
  }

  return nullptr;
}

// match variations of a^2 + 2*a*b + b^2
//
// to reuse the code between the FP and Int versions, the instruction OpCodes
//  and constant types have been turned into template parameters.
//
// Mul2Rhs: The constant to perform the multiplicative equivalent of X*2 with;
//  should be `m_SpecificFP(2.0)` for FP and `m_SpecificInt(1)` for Int
//  (we're matching `X<<1` instead of `X*2` for Int)
template <bool FP, typename Mul2Rhs>
static bool matchesSquareSum(BinaryOperator &I, Mul2Rhs M2Rhs, Value *&A,
                             Value *&B) {
  constexpr unsigned MulOp = FP ? Instruction::FMul : Instruction::Mul;
  constexpr unsigned AddOp = FP ? Instruction::FAdd : Instruction::Add;
  constexpr unsigned Mul2Op = FP ? Instruction::FMul : Instruction::Shl;

  // (a * a) + (((a * 2) + b) * b)
  if (match(&I, m_c_BinOp(
                    AddOp, m_OneUse(m_BinOp(MulOp, m_Value(A), m_Deferred(A))),
                    m_OneUse(m_c_BinOp(
                        MulOp,
                        m_c_BinOp(AddOp, m_BinOp(Mul2Op, m_Deferred(A), M2Rhs),
                                  m_Value(B)),
                        m_Deferred(B))))))
    return true;

  // ((a * b) * 2)  or ((a * 2) * b)
  // +
  // (a * a + b * b) or (b * b + a * a)
  return match(
      &I, m_c_BinOp(
              AddOp,
              m_CombineOr(
                  m_OneUse(m_BinOp(
                      Mul2Op, m_BinOp(MulOp, m_Value(A), m_Value(B)), M2Rhs)),
                  m_OneUse(m_c_BinOp(MulOp, m_BinOp(Mul2Op, m_Value(A), M2Rhs),
                                     m_Value(B)))),
              m_OneUse(
                  m_c_BinOp(AddOp, m_BinOp(MulOp, m_Deferred(A), m_Deferred(A)),
                            m_BinOp(MulOp, m_Deferred(B), m_Deferred(B))))));
}

// Fold integer variations of a^2 + 2*a*b + b^2 -> (a + b)^2
Instruction *InstCombinerImpl::foldSquareSumInt(BinaryOperator &I) {
  Value *A, *B;
  if (matchesSquareSum</*FP*/ false>(I, m_SpecificInt(1), A, B)) {
    Value *AB = Builder.CreateAdd(A, B);
    return BinaryOperator::CreateMul(AB, AB);
  }
  return nullptr;
}

// Fold floating point variations of a^2 + 2*a*b + b^2 -> (a + b)^2
// Requires `nsz` and `reassoc`.
Instruction *InstCombinerImpl::foldSquareSumFP(BinaryOperator &I) {
  assert(I.hasAllowReassoc() && I.hasNoSignedZeros() && "Assumption mismatch");
  Value *A, *B;
  if (matchesSquareSum</*FP*/ true>(I, m_SpecificFP(2.0), A, B)) {
    Value *AB = Builder.CreateFAddFMF(A, B, &I);
    return BinaryOperator::CreateFMulFMF(AB, AB, &I);
  }
  return nullptr;
}

// Matches multiplication expression Op * C where C is a constant. Returns the
// constant value in C and the other operand in Op. Returns true if such a
// match is found.
static bool MatchMul(Value *E, Value *&Op, APInt &C) {
  const APInt *AI;
  if (match(E, m_Mul(m_Value(Op), m_APInt(AI)))) {
    C = *AI;
    return true;
  }
  if (match(E, m_Shl(m_Value(Op), m_APInt(AI)))) {
    C = APInt(AI->getBitWidth(), 1);
    C <<= *AI;
    return true;
  }
  return false;
}

// Matches remainder expression Op % C where C is a constant. Returns the
// constant value in C and the other operand in Op. Returns the signedness of
// the remainder operation in IsSigned. Returns true if such a match is
// found.
static bool MatchRem(Value *E, Value *&Op, APInt &C, bool &IsSigned) {
  const APInt *AI;
  IsSigned = false;
  if (match(E, m_SRem(m_Value(Op), m_APInt(AI)))) {
    IsSigned = true;
    C = *AI;
    return true;
  }
  if (match(E, m_URem(m_Value(Op), m_APInt(AI)))) {
    C = *AI;
    return true;
  }
  if (match(E, m_And(m_Value(Op), m_APInt(AI))) && (*AI + 1).isPowerOf2()) {
    C = *AI + 1;
    return true;
  }
  return false;
}

// Matches division expression Op / C with the given signedness as indicated
// by IsSigned, where C is a constant. Returns the constant value in C and the
// other operand in Op. Returns true if such a match is found.
static bool MatchDiv(Value *E, Value *&Op, APInt &C, bool IsSigned) {
  const APInt *AI;
  if (IsSigned && match(E, m_SDiv(m_Value(Op), m_APInt(AI)))) {
    C = *AI;
    return true;
  }
  if (!IsSigned) {
    if (match(E, m_UDiv(m_Value(Op), m_APInt(AI)))) {
      C = *AI;
      return true;
    }
    if (match(E, m_LShr(m_Value(Op), m_APInt(AI)))) {
      C = APInt(AI->getBitWidth(), 1);
      C <<= *AI;
      return true;
    }
  }
  return false;
}

// Returns whether C0 * C1 with the given signedness overflows.
static bool MulWillOverflow(APInt &C0, APInt &C1, bool IsSigned) {
  bool overflow;
  if (IsSigned)
    (void)C0.smul_ov(C1, overflow);
  else
    (void)C0.umul_ov(C1, overflow);
  return overflow;
}

// Simplifies X % C0 + (( X / C0 ) % C1) * C0 to X % (C0 * C1), where (C0 * C1)
// does not overflow.
// Simplifies (X / C0) * C1 + (X % C0) * C2 to
// (X / C0) * (C1 - C2 * C0) + X * C2
Value *InstCombinerImpl::SimplifyAddWithRemainder(BinaryOperator &I) {
  Value *LHS = I.getOperand(0), *RHS = I.getOperand(1);
  Value *X, *MulOpV;
  APInt C0, MulOpC;
  bool IsSigned;
  // Match I = X % C0 + MulOpV * C0
  if (((MatchRem(LHS, X, C0, IsSigned) && MatchMul(RHS, MulOpV, MulOpC)) ||
       (MatchRem(RHS, X, C0, IsSigned) && MatchMul(LHS, MulOpV, MulOpC))) &&
      C0 == MulOpC) {
    Value *RemOpV;
    APInt C1;
    bool Rem2IsSigned;
    // Match MulOpC = RemOpV % C1
    if (MatchRem(MulOpV, RemOpV, C1, Rem2IsSigned) &&
        IsSigned == Rem2IsSigned) {
      Value *DivOpV;
      APInt DivOpC;
      // Match RemOpV = X / C0
      if (MatchDiv(RemOpV, DivOpV, DivOpC, IsSigned) && X == DivOpV &&
          C0 == DivOpC && !MulWillOverflow(C0, C1, IsSigned)) {
        Value *NewDivisor = ConstantInt::get(X->getType(), C0 * C1);
        return IsSigned ? Builder.CreateSRem(X, NewDivisor, "srem")
                        : Builder.CreateURem(X, NewDivisor, "urem");
      }
    }
  }

  // Match I = (X / C0) * C1 + (X % C0) * C2
  Value *Div, *Rem;
  APInt C1, C2;
  if (!LHS->hasOneUse() || !MatchMul(LHS, Div, C1))
    Div = LHS, C1 = APInt(I.getType()->getScalarSizeInBits(), 1);
  if (!RHS->hasOneUse() || !MatchMul(RHS, Rem, C2))
    Rem = RHS, C2 = APInt(I.getType()->getScalarSizeInBits(), 1);
  if (match(Div, m_IRem(m_Value(), m_Value()))) {
    std::swap(Div, Rem);
    std::swap(C1, C2);
  }
  Value *DivOpV;
  APInt DivOpC;
  if (MatchRem(Rem, X, C0, IsSigned) &&
      MatchDiv(Div, DivOpV, DivOpC, IsSigned) && X == DivOpV && C0 == DivOpC) {
    APInt NewC = C1 - C2 * C0;
    if (!NewC.isZero() && !Rem->hasOneUse())
      return nullptr;
    if (!isGuaranteedNotToBeUndef(X, &AC, &I, &DT))
      return nullptr;
    Value *MulXC2 = Builder.CreateMul(X, ConstantInt::get(X->getType(), C2));
    if (NewC.isZero())
      return MulXC2;
    return Builder.CreateAdd(
        Builder.CreateMul(Div, ConstantInt::get(X->getType(), NewC)), MulXC2);
  }

  return nullptr;
}

/// Fold
///   (1 << NBits) - 1
/// Into:
///   ~(-(1 << NBits))
/// Because a 'not' is better for bit-tracking analysis and other transforms
/// than an 'add'. The new shl is always nsw, and is nuw if old `and` was.
static Instruction *canonicalizeLowbitMask(BinaryOperator &I,
                                           InstCombiner::BuilderTy &Builder) {
  Value *NBits;
  if (!match(&I, m_Add(m_OneUse(m_Shl(m_One(), m_Value(NBits))), m_AllOnes())))
    return nullptr;

  Constant *MinusOne = Constant::getAllOnesValue(NBits->getType());
  Value *NotMask = Builder.CreateShl(MinusOne, NBits, "notmask");
  // Be wary of constant folding.
  if (auto *BOp = dyn_cast<BinaryOperator>(NotMask)) {
    // Always NSW. But NUW propagates from `add`.
    BOp->setHasNoSignedWrap();
    BOp->setHasNoUnsignedWrap(I.hasNoUnsignedWrap());
  }

  return BinaryOperator::CreateNot(NotMask, I.getName());
}

static Instruction *foldToUnsignedSaturatedAdd(BinaryOperator &I) {
  assert(I.getOpcode() == Instruction::Add && "Expecting add instruction");
  Type *Ty = I.getType();
  auto getUAddSat = [&]() {
    return Intrinsic::getDeclaration(I.getModule(), Intrinsic::uadd_sat, Ty);
  };

  // add (umin X, ~Y), Y --> uaddsat X, Y
  Value *X, *Y;
  if (match(&I, m_c_Add(m_c_UMin(m_Value(X), m_Not(m_Value(Y))),
                        m_Deferred(Y))))
    return CallInst::Create(getUAddSat(), { X, Y });

  // add (umin X, ~C), C --> uaddsat X, C
  const APInt *C, *NotC;
  if (match(&I, m_Add(m_UMin(m_Value(X), m_APInt(NotC)), m_APInt(C))) &&
      *C == ~*NotC)
    return CallInst::Create(getUAddSat(), { X, ConstantInt::get(Ty, *C) });

  return nullptr;
}

// Transform:
//  (add A, (shl (neg B), Y))
//      -> (sub A, (shl B, Y))
static Instruction *combineAddSubWithShlAddSub(InstCombiner::BuilderTy &Builder,
                                               const BinaryOperator &I) {
  Value *A, *B, *Cnt;
  if (match(&I,
            m_c_Add(m_OneUse(m_Shl(m_OneUse(m_Neg(m_Value(B))), m_Value(Cnt))),
                    m_Value(A)))) {
    Value *NewShl = Builder.CreateShl(B, Cnt);
    return BinaryOperator::CreateSub(A, NewShl);
  }
  return nullptr;
}

/// Try to reduce signed division by power-of-2 to an arithmetic shift right.
static Instruction *foldAddToAshr(BinaryOperator &Add) {
  // Division must be by power-of-2, but not the minimum signed value.
  Value *X;
  const APInt *DivC;
  if (!match(Add.getOperand(0), m_SDiv(m_Value(X), m_Power2(DivC))) ||
      DivC->isNegative())
    return nullptr;

  // Rounding is done by adding -1 if the dividend (X) is negative and has any
  // low bits set. It recognizes two canonical patterns:
  // 1. For an 'ugt' cmp with the signed minimum value (SMIN), the
  //    pattern is: sext (icmp ugt (X & (DivC - 1)), SMIN).
  // 2. For an 'eq' cmp, the pattern's: sext (icmp eq X & (SMIN + 1), SMIN + 1).
  // Note that, by the time we end up here, if possible, ugt has been
  // canonicalized into eq.
  const APInt *MaskC, *MaskCCmp;
  ICmpInst::Predicate Pred;
  if (!match(Add.getOperand(1),
             m_SExt(m_ICmp(Pred, m_And(m_Specific(X), m_APInt(MaskC)),
                           m_APInt(MaskCCmp)))))
    return nullptr;

  if ((Pred != ICmpInst::ICMP_UGT || !MaskCCmp->isSignMask()) &&
      (Pred != ICmpInst::ICMP_EQ || *MaskCCmp != *MaskC))
    return nullptr;

  APInt SMin = APInt::getSignedMinValue(Add.getType()->getScalarSizeInBits());
  bool IsMaskValid = Pred == ICmpInst::ICMP_UGT
                         ? (*MaskC == (SMin | (*DivC - 1)))
                         : (*DivC == 2 && *MaskC == SMin + 1);
  if (!IsMaskValid)
    return nullptr;

  // (X / DivC) + sext ((X & (SMin | (DivC - 1)) >u SMin) --> X >>s log2(DivC)
  return BinaryOperator::CreateAShr(
      X, ConstantInt::get(Add.getType(), DivC->exactLogBase2()));
}

Instruction *InstCombinerImpl::
    canonicalizeCondSignextOfHighBitExtractToSignextHighBitExtract(
        BinaryOperator &I) {
  assert((I.getOpcode() == Instruction::Add ||
          I.getOpcode() == Instruction::Or ||
          I.getOpcode() == Instruction::Sub) &&
         "Expecting add/or/sub instruction");

  // We have a subtraction/addition between a (potentially truncated) *logical*
  // right-shift of X and a "select".
  Value *X, *Select;
  Instruction *LowBitsToSkip, *Extract;
  if (!match(&I, m_c_BinOp(m_TruncOrSelf(m_CombineAnd(
                               m_LShr(m_Value(X), m_Instruction(LowBitsToSkip)),
                               m_Instruction(Extract))),
                           m_Value(Select))))
    return nullptr;

  // `add`/`or` is commutative; but for `sub`, "select" *must* be on RHS.
  if (I.getOpcode() == Instruction::Sub && I.getOperand(1) != Select)
    return nullptr;

  Type *XTy = X->getType();
  bool HadTrunc = I.getType() != XTy;

  // If there was a truncation of extracted value, then we'll need to produce
  // one extra instruction, so we need to ensure one instruction will go away.
  if (HadTrunc && !match(&I, m_c_BinOp(m_OneUse(m_Value()), m_Value())))
    return nullptr;

  // Extraction should extract high NBits bits, with shift amount calculated as:
  //   low bits to skip = shift bitwidth - high bits to extract
  // The shift amount itself may be extended, and we need to look past zero-ext
  // when matching NBits, that will matter for matching later.
  Constant *C;
  Value *NBits;
  if (!match(
          LowBitsToSkip,
          m_ZExtOrSelf(m_Sub(m_Constant(C), m_ZExtOrSelf(m_Value(NBits))))) ||
      !match(C, m_SpecificInt_ICMP(ICmpInst::Predicate::ICMP_EQ,
                                   APInt(C->getType()->getScalarSizeInBits(),
                                         X->getType()->getScalarSizeInBits()))))
    return nullptr;

  // Sign-extending value can be zero-extended if we `sub`tract it,
  // or sign-extended otherwise.
  auto SkipExtInMagic = [&I](Value *&V) {
    if (I.getOpcode() == Instruction::Sub)
      match(V, m_ZExtOrSelf(m_Value(V)));
    else
      match(V, m_SExtOrSelf(m_Value(V)));
  };

  // Now, finally validate the sign-extending magic.
  // `select` itself may be appropriately extended, look past that.
  SkipExtInMagic(Select);

  ICmpInst::Predicate Pred;
  const APInt *Thr;
  Value *SignExtendingValue, *Zero;
  bool ShouldSignext;
  // It must be a select between two values we will later establish to be a
  // sign-extending value and a zero constant. The condition guarding the
  // sign-extension must be based on a sign bit of the same X we had in `lshr`.
  if (!match(Select, m_Select(m_ICmp(Pred, m_Specific(X), m_APInt(Thr)),
                              m_Value(SignExtendingValue), m_Value(Zero))) ||
      !isSignBitCheck(Pred, *Thr, ShouldSignext))
    return nullptr;

  // icmp-select pair is commutative.
  if (!ShouldSignext)
    std::swap(SignExtendingValue, Zero);

  // If we should not perform sign-extension then we must add/or/subtract zero.
  if (!match(Zero, m_Zero()))
    return nullptr;
  // Otherwise, it should be some constant, left-shifted by the same NBits we
  // had in `lshr`. Said left-shift can also be appropriately extended.
  // Again, we must look past zero-ext when looking for NBits.
  SkipExtInMagic(SignExtendingValue);
  Constant *SignExtendingValueBaseConstant;
  if (!match(SignExtendingValue,
             m_Shl(m_Constant(SignExtendingValueBaseConstant),
                   m_ZExtOrSelf(m_Specific(NBits)))))
    return nullptr;
  // If we `sub`, then the constant should be one, else it should be all-ones.
  if (I.getOpcode() == Instruction::Sub
          ? !match(SignExtendingValueBaseConstant, m_One())
          : !match(SignExtendingValueBaseConstant, m_AllOnes()))
    return nullptr;

  auto *NewAShr = BinaryOperator::CreateAShr(X, LowBitsToSkip,
                                             Extract->getName() + ".sext");
  NewAShr->copyIRFlags(Extract); // Preserve `exact`-ness.
  if (!HadTrunc)
    return NewAShr;

  Builder.Insert(NewAShr);
  return TruncInst::CreateTruncOrBitCast(NewAShr, I.getType());
}

/// This is a specialization of a more general transform from
/// foldUsingDistributiveLaws. If that code can be made to work optimally
/// for multi-use cases or propagating nsw/nuw, then we would not need this.
static Instruction *factorizeMathWithShlOps(BinaryOperator &I,
                                            InstCombiner::BuilderTy &Builder) {
  // TODO: Also handle mul by doubling the shift amount?
  assert((I.getOpcode() == Instruction::Add ||
          I.getOpcode() == Instruction::Sub) &&
         "Expected add/sub");
  auto *Op0 = dyn_cast<BinaryOperator>(I.getOperand(0));
  auto *Op1 = dyn_cast<BinaryOperator>(I.getOperand(1));
  if (!Op0 || !Op1 || !(Op0->hasOneUse() || Op1->hasOneUse()))
    return nullptr;

  Value *X, *Y, *ShAmt;
  if (!match(Op0, m_Shl(m_Value(X), m_Value(ShAmt))) ||
      !match(Op1, m_Shl(m_Value(Y), m_Specific(ShAmt))))
    return nullptr;

  // No-wrap propagates only when all ops have no-wrap.
  bool HasNSW = I.hasNoSignedWrap() && Op0->hasNoSignedWrap() &&
                Op1->hasNoSignedWrap();
  bool HasNUW = I.hasNoUnsignedWrap() && Op0->hasNoUnsignedWrap() &&
                Op1->hasNoUnsignedWrap();

  // add/sub (X << ShAmt), (Y << ShAmt) --> (add/sub X, Y) << ShAmt
  Value *NewMath = Builder.CreateBinOp(I.getOpcode(), X, Y);
  if (auto *NewI = dyn_cast<BinaryOperator>(NewMath)) {
    NewI->setHasNoSignedWrap(HasNSW);
    NewI->setHasNoUnsignedWrap(HasNUW);
  }
  auto *NewShl = BinaryOperator::CreateShl(NewMath, ShAmt);
  NewShl->setHasNoSignedWrap(HasNSW);
  NewShl->setHasNoUnsignedWrap(HasNUW);
  return NewShl;
}

/// Reduce a sequence of masked half-width multiplies to a single multiply.
/// ((XLow * YHigh) + (YLow * XHigh)) << HalfBits) + (XLow * YLow) --> X * Y
static Instruction *foldBoxMultiply(BinaryOperator &I) {
  unsigned BitWidth = I.getType()->getScalarSizeInBits();
  // Skip the odd bitwidth types.
  if ((BitWidth & 0x1))
    return nullptr;

  unsigned HalfBits = BitWidth >> 1;
  APInt HalfMask = APInt::getMaxValue(HalfBits);

  // ResLo = (CrossSum << HalfBits) + (YLo * XLo)
  Value *XLo, *YLo;
  Value *CrossSum;
  // Require one-use on the multiply to avoid increasing the number of
  // multiplications.
  if (!match(&I, m_c_Add(m_Shl(m_Value(CrossSum), m_SpecificInt(HalfBits)),
                         m_OneUse(m_Mul(m_Value(YLo), m_Value(XLo))))))
    return nullptr;

  // XLo = X & HalfMask
  // YLo = Y & HalfMask
  // TODO: Refactor with SimplifyDemandedBits or KnownBits known leading zeros
  // to enhance robustness
  Value *X, *Y;
  if (!match(XLo, m_And(m_Value(X), m_SpecificInt(HalfMask))) ||
      !match(YLo, m_And(m_Value(Y), m_SpecificInt(HalfMask))))
    return nullptr;

  // CrossSum = (X' * (Y >> Halfbits)) + (Y' * (X >> HalfBits))
  // X' can be either X or XLo in the pattern (and the same for Y')
  if (match(CrossSum,
            m_c_Add(m_c_Mul(m_LShr(m_Specific(Y), m_SpecificInt(HalfBits)),
                            m_CombineOr(m_Specific(X), m_Specific(XLo))),
                    m_c_Mul(m_LShr(m_Specific(X), m_SpecificInt(HalfBits)),
                            m_CombineOr(m_Specific(Y), m_Specific(YLo))))))
    return BinaryOperator::CreateMul(X, Y);

  return nullptr;
}

Instruction *InstCombinerImpl::visitAdd(BinaryOperator &I) {
  if (Value *V = simplifyAddInst(I.getOperand(0), I.getOperand(1),
                                 I.hasNoSignedWrap(), I.hasNoUnsignedWrap(),
                                 SQ.getWithInstruction(&I)))
    return replaceInstUsesWith(I, V);

  if (SimplifyAssociativeOrCommutative(I))
    return &I;

  if (Instruction *X = foldVectorBinop(I))
    return X;

  if (Instruction *Phi = foldBinopWithPhiOperands(I))
    return Phi;

  // (A*B)+(A*C) -> A*(B+C) etc
  if (Value *V = foldUsingDistributiveLaws(I))
    return replaceInstUsesWith(I, V);

  if (Instruction *R = foldBoxMultiply(I))
    return R;

  if (Instruction *R = factorizeMathWithShlOps(I, Builder))
    return R;

  if (Instruction *X = foldAddWithConstant(I))
    return X;

  if (Instruction *X = foldNoWrapAdd(I, Builder))
    return X;

  if (Instruction *R = foldBinOpShiftWithShift(I))
    return R;

  if (Instruction *R = combineAddSubWithShlAddSub(Builder, I))
    return R;

  Value *LHS = I.getOperand(0), *RHS = I.getOperand(1);
  Type *Ty = I.getType();
  if (Ty->isIntOrIntVectorTy(1))
    return BinaryOperator::CreateXor(LHS, RHS);

  // X + X --> X << 1
  if (LHS == RHS) {
    auto *Shl = BinaryOperator::CreateShl(LHS, ConstantInt::get(Ty, 1));
    Shl->setHasNoSignedWrap(I.hasNoSignedWrap());
    Shl->setHasNoUnsignedWrap(I.hasNoUnsignedWrap());
    return Shl;
  }

  Value *A, *B;
  if (match(LHS, m_Neg(m_Value(A)))) {
    // -A + -B --> -(A + B)
    if (match(RHS, m_Neg(m_Value(B))))
      return BinaryOperator::CreateNeg(Builder.CreateAdd(A, B));

    // -A + B --> B - A
    auto *Sub = BinaryOperator::CreateSub(RHS, A);
    auto *OB0 = cast<OverflowingBinaryOperator>(LHS);
    Sub->setHasNoSignedWrap(I.hasNoSignedWrap() && OB0->hasNoSignedWrap());

    return Sub;
  }

  // A + -B  -->  A - B
  if (match(RHS, m_Neg(m_Value(B))))
    return BinaryOperator::CreateSub(LHS, B);

  if (Value *V = checkForNegativeOperand(I, Builder))
    return replaceInstUsesWith(I, V);

  // (A + 1) + ~B --> A - B
  // ~B + (A + 1) --> A - B
  // (~B + A) + 1 --> A - B
  // (A + ~B) + 1 --> A - B
  if (match(&I, m_c_BinOp(m_Add(m_Value(A), m_One()), m_Not(m_Value(B)))) ||
      match(&I, m_BinOp(m_c_Add(m_Not(m_Value(B)), m_Value(A)), m_One())))
    return BinaryOperator::CreateSub(A, B);

  // (A + RHS) + RHS --> A + (RHS << 1)
  if (match(LHS, m_OneUse(m_c_Add(m_Value(A), m_Specific(RHS)))))
    return BinaryOperator::CreateAdd(A, Builder.CreateShl(RHS, 1, "reass.add"));

  // LHS + (A + LHS) --> A + (LHS << 1)
  if (match(RHS, m_OneUse(m_c_Add(m_Value(A), m_Specific(LHS)))))
    return BinaryOperator::CreateAdd(A, Builder.CreateShl(LHS, 1, "reass.add"));

  {
    // (A + C1) + (C2 - B) --> (A - B) + (C1 + C2)
    Constant *C1, *C2;
    if (match(&I, m_c_Add(m_Add(m_Value(A), m_ImmConstant(C1)),
                          m_Sub(m_ImmConstant(C2), m_Value(B)))) &&
        (LHS->hasOneUse() || RHS->hasOneUse())) {
      Value *Sub = Builder.CreateSub(A, B);
      return BinaryOperator::CreateAdd(Sub, ConstantExpr::getAdd(C1, C2));
    }

    // Canonicalize a constant sub operand as an add operand for better folding:
    // (C1 - A) + B --> (B - A) + C1
    if (match(&I, m_c_Add(m_OneUse(m_Sub(m_ImmConstant(C1), m_Value(A))),
                          m_Value(B)))) {
      Value *Sub = Builder.CreateSub(B, A, "reass.sub");
      return BinaryOperator::CreateAdd(Sub, C1);
    }
  }

  // X % C0 + (( X / C0 ) % C1) * C0 => X % (C0 * C1)
  if (Value *V = SimplifyAddWithRemainder(I)) return replaceInstUsesWith(I, V);

  // ((X s/ C1) << C2) + X => X s% -C1 where -C1 is 1 << C2
  const APInt *C1, *C2;
  if (match(LHS, m_Shl(m_SDiv(m_Specific(RHS), m_APInt(C1)), m_APInt(C2)))) {
    APInt one(C2->getBitWidth(), 1);
    APInt minusC1 = -(*C1);
    if (minusC1 == (one << *C2)) {
      Constant *NewRHS = ConstantInt::get(RHS->getType(), minusC1);
      return BinaryOperator::CreateSRem(RHS, NewRHS);
    }
  }

  // (A & 2^C1) + A => A & (2^C1 - 1) iff bit C1 in A is a sign bit
  if (match(&I, m_c_Add(m_And(m_Value(A), m_APInt(C1)), m_Deferred(A))) &&
      C1->isPowerOf2() && (ComputeNumSignBits(A) > C1->countl_zero())) {
    Constant *NewMask = ConstantInt::get(RHS->getType(), *C1 - 1);
    return BinaryOperator::CreateAnd(A, NewMask);
  }

  // ZExt (B - A) + ZExt(A) --> ZExt(B)
  if ((match(RHS, m_ZExt(m_Value(A))) &&
       match(LHS, m_ZExt(m_NUWSub(m_Value(B), m_Specific(A))))) ||
      (match(LHS, m_ZExt(m_Value(A))) &&
       match(RHS, m_ZExt(m_NUWSub(m_Value(B), m_Specific(A))))))
    return new ZExtInst(B, LHS->getType());

  // zext(A) + sext(A) --> 0 if A is i1
  if (match(&I, m_c_BinOp(m_ZExt(m_Value(A)), m_SExt(m_Deferred(A)))) &&
      A->getType()->isIntOrIntVectorTy(1))
    return replaceInstUsesWith(I, Constant::getNullValue(I.getType()));

  // A+B --> A|B iff A and B have no bits set in common.
  WithCache<const Value *> LHSCache(LHS), RHSCache(RHS);
  if (haveNoCommonBitsSet(LHSCache, RHSCache, SQ.getWithInstruction(&I)))
    return BinaryOperator::CreateDisjointOr(LHS, RHS);

  if (Instruction *Ext = narrowMathIfNoOverflow(I))
    return Ext;

  // (add (xor A, B) (and A, B)) --> (or A, B)
  // (add (and A, B) (xor A, B)) --> (or A, B)
  if (match(&I, m_c_BinOp(m_Xor(m_Value(A), m_Value(B)),
                          m_c_And(m_Deferred(A), m_Deferred(B)))))
    return BinaryOperator::CreateOr(A, B);

  // (add (or A, B) (and A, B)) --> (add A, B)
  // (add (and A, B) (or A, B)) --> (add A, B)
  if (match(&I, m_c_BinOp(m_Or(m_Value(A), m_Value(B)),
                          m_c_And(m_Deferred(A), m_Deferred(B))))) {
    // Replacing operands in-place to preserve nuw/nsw flags.
    replaceOperand(I, 0, A);
    replaceOperand(I, 1, B);
    return &I;
  }

  // (add A (or A, -A)) --> (and (add A, -1) A)
  // (add A (or -A, A)) --> (and (add A, -1) A)
  // (add (or A, -A) A) --> (and (add A, -1) A)
  // (add (or -A, A) A) --> (and (add A, -1) A)
  if (match(&I, m_c_BinOp(m_Value(A), m_OneUse(m_c_Or(m_Neg(m_Deferred(A)),
                                                      m_Deferred(A)))))) {
    Value *Add =
        Builder.CreateAdd(A, Constant::getAllOnesValue(A->getType()), "",
                          I.hasNoUnsignedWrap(), I.hasNoSignedWrap());
    return BinaryOperator::CreateAnd(Add, A);
  }

  // Canonicalize ((A & -A) - 1) --> ((A - 1) & ~A)
  // Forms all commutable operations, and simplifies ctpop -> cttz folds.
  if (match(&I,
            m_Add(m_OneUse(m_c_And(m_Value(A), m_OneUse(m_Neg(m_Deferred(A))))),
                  m_AllOnes()))) {
    Constant *AllOnes = ConstantInt::getAllOnesValue(RHS->getType());
    Value *Dec = Builder.CreateAdd(A, AllOnes);
    Value *Not = Builder.CreateXor(A, AllOnes);
    return BinaryOperator::CreateAnd(Dec, Not);
  }

  // Disguised reassociation/factorization:
  // ~(A * C1) + A
  // ((A * -C1) - 1) + A
  // ((A * -C1) + A) - 1
  // (A * (1 - C1)) - 1
  if (match(&I,
            m_c_Add(m_OneUse(m_Not(m_OneUse(m_Mul(m_Value(A), m_APInt(C1))))),
                    m_Deferred(A)))) {
    Type *Ty = I.getType();
    Constant *NewMulC = ConstantInt::get(Ty, 1 - *C1);
    Value *NewMul = Builder.CreateMul(A, NewMulC);
    return BinaryOperator::CreateAdd(NewMul, ConstantInt::getAllOnesValue(Ty));
  }

  // (A * -2**C) + B --> B - (A << C)
  const APInt *NegPow2C;
  if (match(&I, m_c_Add(m_OneUse(m_Mul(m_Value(A), m_NegatedPower2(NegPow2C))),
                        m_Value(B)))) {
    Constant *ShiftAmtC = ConstantInt::get(Ty, NegPow2C->countr_zero());
    Value *Shl = Builder.CreateShl(A, ShiftAmtC);
    return BinaryOperator::CreateSub(B, Shl);
  }

  // Canonicalize signum variant that ends in add:
  // (A s>> (BW - 1)) + (zext (A s> 0)) --> (A s>> (BW - 1)) | (zext (A != 0))
  ICmpInst::Predicate Pred;
  uint64_t BitWidth = Ty->getScalarSizeInBits();
  if (match(LHS, m_AShr(m_Value(A), m_SpecificIntAllowPoison(BitWidth - 1))) &&
      match(RHS, m_OneUse(m_ZExt(
                     m_OneUse(m_ICmp(Pred, m_Specific(A), m_ZeroInt()))))) &&
      Pred == CmpInst::ICMP_SGT) {
    Value *NotZero = Builder.CreateIsNotNull(A, "isnotnull");
    Value *Zext = Builder.CreateZExt(NotZero, Ty, "isnotnull.zext");
    return BinaryOperator::CreateOr(LHS, Zext);
  }

  {
    Value *Cond, *Ext;
    Constant *C;
    // (add X, (sext/zext (icmp eq X, C)))
    //    -> (select (icmp eq X, C), (add C, (sext/zext 1)), X)
    auto CondMatcher = m_CombineAnd(
        m_Value(Cond), m_ICmp(Pred, m_Deferred(A), m_ImmConstant(C)));

    if (match(&I,
              m_c_Add(m_Value(A),
                      m_CombineAnd(m_Value(Ext), m_ZExtOrSExt(CondMatcher)))) &&
        Pred == ICmpInst::ICMP_EQ && Ext->hasOneUse()) {
      Value *Add = isa<ZExtInst>(Ext) ? InstCombiner::AddOne(C)
                                      : InstCombiner::SubOne(C);
      return replaceInstUsesWith(I, Builder.CreateSelect(Cond, Add, A));
    }
  }

  if (Instruction *Ashr = foldAddToAshr(I))
    return Ashr;

  // (~X) + (~Y) --> -2 - (X + Y)
  {
    // To ensure we can save instructions we need to ensure that we consume both
    // LHS/RHS (i.e they have a `not`).
    bool ConsumesLHS, ConsumesRHS;
    if (isFreeToInvert(LHS, LHS->hasOneUse(), ConsumesLHS) && ConsumesLHS &&
        isFreeToInvert(RHS, RHS->hasOneUse(), ConsumesRHS) && ConsumesRHS) {
      Value *NotLHS = getFreelyInverted(LHS, LHS->hasOneUse(), &Builder);
      Value *NotRHS = getFreelyInverted(RHS, RHS->hasOneUse(), &Builder);
      assert(NotLHS != nullptr && NotRHS != nullptr &&
             "isFreeToInvert desynced with getFreelyInverted");
      Value *LHSPlusRHS = Builder.CreateAdd(NotLHS, NotRHS);
      return BinaryOperator::CreateSub(
          ConstantInt::getSigned(RHS->getType(), -2), LHSPlusRHS);
    }
  }

  if (Instruction *R = tryFoldInstWithCtpopWithNot(&I))
    return R;

  // TODO(jingyue): Consider willNotOverflowSignedAdd and
  // willNotOverflowUnsignedAdd to reduce the number of invocations of
  // computeKnownBits.
  bool Changed = false;
  if (!I.hasNoSignedWrap() && willNotOverflowSignedAdd(LHSCache, RHSCache, I)) {
    Changed = true;
    I.setHasNoSignedWrap(true);
  }
  if (!I.hasNoUnsignedWrap() &&
      willNotOverflowUnsignedAdd(LHSCache, RHSCache, I)) {
    Changed = true;
    I.setHasNoUnsignedWrap(true);
  }

  if (Instruction *V = canonicalizeLowbitMask(I, Builder))
    return V;

  if (Instruction *V =
          canonicalizeCondSignextOfHighBitExtractToSignextHighBitExtract(I))
    return V;

  if (Instruction *SatAdd = foldToUnsignedSaturatedAdd(I))
    return SatAdd;

  // usub.sat(A, B) + B => umax(A, B)
  if (match(&I, m_c_BinOp(
          m_OneUse(m_Intrinsic<Intrinsic::usub_sat>(m_Value(A), m_Value(B))),
          m_Deferred(B)))) {
    return replaceInstUsesWith(I,
        Builder.CreateIntrinsic(Intrinsic::umax, {I.getType()}, {A, B}));
  }

  // ctpop(A) + ctpop(B) => ctpop(A | B) if A and B have no bits set in common.
  if (match(LHS, m_OneUse(m_Intrinsic<Intrinsic::ctpop>(m_Value(A)))) &&
      match(RHS, m_OneUse(m_Intrinsic<Intrinsic::ctpop>(m_Value(B)))) &&
      haveNoCommonBitsSet(A, B, SQ.getWithInstruction(&I)))
    return replaceInstUsesWith(
        I, Builder.CreateIntrinsic(Intrinsic::ctpop, {I.getType()},
                                   {Builder.CreateOr(A, B)}));

  // Fold the log2_ceil idiom:
  // zext(ctpop(A) >u/!= 1) + (ctlz(A, true) ^ (BW - 1))
  // -->
  // BW - ctlz(A - 1, false)
  const APInt *XorC;
  if (match(&I,
            m_c_Add(
                m_ZExt(m_ICmp(Pred, m_Intrinsic<Intrinsic::ctpop>(m_Value(A)),
                              m_One())),
                m_OneUse(m_ZExtOrSelf(m_OneUse(m_Xor(
                    m_OneUse(m_TruncOrSelf(m_OneUse(
                        m_Intrinsic<Intrinsic::ctlz>(m_Deferred(A), m_One())))),
                    m_APInt(XorC))))))) &&
      (Pred == ICmpInst::ICMP_UGT || Pred == ICmpInst::ICMP_NE) &&
      *XorC == A->getType()->getScalarSizeInBits() - 1) {
    Value *Sub = Builder.CreateAdd(A, Constant::getAllOnesValue(A->getType()));
    Value *Ctlz = Builder.CreateIntrinsic(Intrinsic::ctlz, {A->getType()},
                                          {Sub, Builder.getFalse()});
    Value *Ret = Builder.CreateSub(
        ConstantInt::get(A->getType(), A->getType()->getScalarSizeInBits()),
        Ctlz, "", /*HasNUW*/ true, /*HasNSW*/ true);
    return replaceInstUsesWith(I, Builder.CreateZExtOrTrunc(Ret, I.getType()));
  }

  if (Instruction *Res = foldSquareSumInt(I))
    return Res;

  if (Instruction *Res = foldBinOpOfDisplacedShifts(I))
    return Res;

  if (Instruction *Res = foldBinOpOfSelectAndCastOfSelectCondition(I))
    return Res;

  return Changed ? &I : nullptr;
}

/// Eliminate an op from a linear interpolation (lerp) pattern.
static Instruction *factorizeLerp(BinaryOperator &I,
                                  InstCombiner::BuilderTy &Builder) {
  Value *X, *Y, *Z;
  if (!match(&I, m_c_FAdd(m_OneUse(m_c_FMul(m_Value(Y),
                                            m_OneUse(m_FSub(m_FPOne(),
                                                            m_Value(Z))))),
                          m_OneUse(m_c_FMul(m_Value(X), m_Deferred(Z))))))
    return nullptr;

  // (Y * (1.0 - Z)) + (X * Z) --> Y + Z * (X - Y) [8 commuted variants]
  Value *XY = Builder.CreateFSubFMF(X, Y, &I);
  Value *MulZ = Builder.CreateFMulFMF(Z, XY, &I);
  return BinaryOperator::CreateFAddFMF(Y, MulZ, &I);
}

/// Factor a common operand out of fadd/fsub of fmul/fdiv.
static Instruction *factorizeFAddFSub(BinaryOperator &I,
                                      InstCombiner::BuilderTy &Builder) {
  assert((I.getOpcode() == Instruction::FAdd ||
          I.getOpcode() == Instruction::FSub) && "Expecting fadd/fsub");
  assert(I.hasAllowReassoc() && I.hasNoSignedZeros() &&
         "FP factorization requires FMF");

  if (Instruction *Lerp = factorizeLerp(I, Builder))
    return Lerp;

  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1);
  if (!Op0->hasOneUse() || !Op1->hasOneUse())
    return nullptr;

  Value *X, *Y, *Z;
  bool IsFMul;
  if ((match(Op0, m_FMul(m_Value(X), m_Value(Z))) &&
       match(Op1, m_c_FMul(m_Value(Y), m_Specific(Z)))) ||
      (match(Op0, m_FMul(m_Value(Z), m_Value(X))) &&
       match(Op1, m_c_FMul(m_Value(Y), m_Specific(Z)))))
    IsFMul = true;
  else if (match(Op0, m_FDiv(m_Value(X), m_Value(Z))) &&
           match(Op1, m_FDiv(m_Value(Y), m_Specific(Z))))
    IsFMul = false;
  else
    return nullptr;

  // (X * Z) + (Y * Z) --> (X + Y) * Z
  // (X * Z) - (Y * Z) --> (X - Y) * Z
  // (X / Z) + (Y / Z) --> (X + Y) / Z
  // (X / Z) - (Y / Z) --> (X - Y) / Z
  bool IsFAdd = I.getOpcode() == Instruction::FAdd;
  Value *XY = IsFAdd ? Builder.CreateFAddFMF(X, Y, &I)
                     : Builder.CreateFSubFMF(X, Y, &I);

  // Bail out if we just created a denormal constant.
  // TODO: This is copied from a previous implementation. Is it necessary?
  const APFloat *C;
  if (match(XY, m_APFloat(C)) && !C->isNormal())
    return nullptr;

  return IsFMul ? BinaryOperator::CreateFMulFMF(XY, Z, &I)
                : BinaryOperator::CreateFDivFMF(XY, Z, &I);
}

Instruction *InstCombinerImpl::visitFAdd(BinaryOperator &I) {
  if (Value *V = simplifyFAddInst(I.getOperand(0), I.getOperand(1),
                                  I.getFastMathFlags(),
                                  SQ.getWithInstruction(&I)))
    return replaceInstUsesWith(I, V);

  if (SimplifyAssociativeOrCommutative(I))
    return &I;

  if (Instruction *X = foldVectorBinop(I))
    return X;

  if (Instruction *Phi = foldBinopWithPhiOperands(I))
    return Phi;

  if (Instruction *FoldedFAdd = foldBinOpIntoSelectOrPhi(I))
    return FoldedFAdd;

  // (-X) + Y --> Y - X
  Value *X, *Y;
  if (match(&I, m_c_FAdd(m_FNeg(m_Value(X)), m_Value(Y))))
    return BinaryOperator::CreateFSubFMF(Y, X, &I);

  // Similar to above, but look through fmul/fdiv for the negated term.
  // (-X * Y) + Z --> Z - (X * Y) [4 commuted variants]
  Value *Z;
  if (match(&I, m_c_FAdd(m_OneUse(m_c_FMul(m_FNeg(m_Value(X)), m_Value(Y))),
                         m_Value(Z)))) {
    Value *XY = Builder.CreateFMulFMF(X, Y, &I);
    return BinaryOperator::CreateFSubFMF(Z, XY, &I);
  }
  // (-X / Y) + Z --> Z - (X / Y) [2 commuted variants]
  // (X / -Y) + Z --> Z - (X / Y) [2 commuted variants]
  if (match(&I, m_c_FAdd(m_OneUse(m_FDiv(m_FNeg(m_Value(X)), m_Value(Y))),
                         m_Value(Z))) ||
      match(&I, m_c_FAdd(m_OneUse(m_FDiv(m_Value(X), m_FNeg(m_Value(Y)))),
                         m_Value(Z)))) {
    Value *XY = Builder.CreateFDivFMF(X, Y, &I);
    return BinaryOperator::CreateFSubFMF(Z, XY, &I);
  }

  // Check for (fadd double (sitofp x), y), see if we can merge this into an
  // integer add followed by a promotion.
  if (Instruction *R = foldFBinOpOfIntCasts(I))
    return R;

  Value *LHS = I.getOperand(0), *RHS = I.getOperand(1);
  // Handle specials cases for FAdd with selects feeding the operation
  if (Value *V = SimplifySelectsFeedingBinaryOp(I, LHS, RHS))
    return replaceInstUsesWith(I, V);

  if (I.hasAllowReassoc() && I.hasNoSignedZeros()) {
    if (Instruction *F = factorizeFAddFSub(I, Builder))
      return F;

    if (Instruction *F = foldSquareSumFP(I))
      return F;

    // Try to fold fadd into start value of reduction intrinsic.
    if (match(&I, m_c_FAdd(m_OneUse(m_Intrinsic<Intrinsic::vector_reduce_fadd>(
                               m_AnyZeroFP(), m_Value(X))),
                           m_Value(Y)))) {
      // fadd (rdx 0.0, X), Y --> rdx Y, X
      return replaceInstUsesWith(
          I, Builder.CreateIntrinsic(Intrinsic::vector_reduce_fadd,
                                     {X->getType()}, {Y, X}, &I));
    }
    const APFloat *StartC, *C;
    if (match(LHS, m_OneUse(m_Intrinsic<Intrinsic::vector_reduce_fadd>(
                       m_APFloat(StartC), m_Value(X)))) &&
        match(RHS, m_APFloat(C))) {
      // fadd (rdx StartC, X), C --> rdx (C + StartC), X
      Constant *NewStartC = ConstantFP::get(I.getType(), *C + *StartC);
      return replaceInstUsesWith(
          I, Builder.CreateIntrinsic(Intrinsic::vector_reduce_fadd,
                                     {X->getType()}, {NewStartC, X}, &I));
    }

    // (X * MulC) + X --> X * (MulC + 1.0)
    Constant *MulC;
    if (match(&I, m_c_FAdd(m_FMul(m_Value(X), m_ImmConstant(MulC)),
                           m_Deferred(X)))) {
      if (Constant *NewMulC = ConstantFoldBinaryOpOperands(
              Instruction::FAdd, MulC, ConstantFP::get(I.getType(), 1.0), DL))
        return BinaryOperator::CreateFMulFMF(X, NewMulC, &I);
    }

    // (-X - Y) + (X + Z) --> Z - Y
    if (match(&I, m_c_FAdd(m_FSub(m_FNeg(m_Value(X)), m_Value(Y)),
                           m_c_FAdd(m_Deferred(X), m_Value(Z)))))
      return BinaryOperator::CreateFSubFMF(Z, Y, &I);

    if (Value *V = FAddCombine(Builder).simplify(&I))
      return replaceInstUsesWith(I, V);
  }

  // minumum(X, Y) + maximum(X, Y) => X + Y.
  if (match(&I,
            m_c_FAdd(m_Intrinsic<Intrinsic::maximum>(m_Value(X), m_Value(Y)),
                     m_c_Intrinsic<Intrinsic::minimum>(m_Deferred(X),
                                                       m_Deferred(Y))))) {
    BinaryOperator *Result = BinaryOperator::CreateFAddFMF(X, Y, &I);
    // We cannot preserve ninf if nnan flag is not set.
    // If X is NaN and Y is Inf then in original program we had NaN + NaN,
    // while in optimized version NaN + Inf and this is a poison with ninf flag.
    if (!Result->hasNoNaNs())
      Result->setHasNoInfs(false);
    return Result;
  }

  return nullptr;
}

/// Optimize pointer differences into the same array into a size.  Consider:
///  &A[10] - &A[0]: we should compile this to "10".  LHS/RHS are the pointer
/// operands to the ptrtoint instructions for the LHS/RHS of the subtract.
Value *InstCombinerImpl::OptimizePointerDifference(Value *LHS, Value *RHS,
                                                   Type *Ty, bool IsNUW) {
  // If LHS is a gep based on RHS or RHS is a gep based on LHS, we can optimize
  // this.
  bool Swapped = false;
  GEPOperator *GEP1 = nullptr, *GEP2 = nullptr;
  if (!isa<GEPOperator>(LHS) && isa<GEPOperator>(RHS)) {
    std::swap(LHS, RHS);
    Swapped = true;
  }

  // Require at least one GEP with a common base pointer on both sides.
  if (auto *LHSGEP = dyn_cast<GEPOperator>(LHS)) {
    // (gep X, ...) - X
    if (LHSGEP->getOperand(0)->stripPointerCasts() ==
        RHS->stripPointerCasts()) {
      GEP1 = LHSGEP;
    } else if (auto *RHSGEP = dyn_cast<GEPOperator>(RHS)) {
      // (gep X, ...) - (gep X, ...)
      if (LHSGEP->getOperand(0)->stripPointerCasts() ==
          RHSGEP->getOperand(0)->stripPointerCasts()) {
        GEP1 = LHSGEP;
        GEP2 = RHSGEP;
      }
    }
  }

  if (!GEP1)
    return nullptr;

  // To avoid duplicating the offset arithmetic, rewrite the GEP to use the
  // computed offset. This may erase the original GEP, so be sure to cache the
  // inbounds flag before emitting the offset.
  // TODO: We should probably do this even if there is only one GEP.
  bool RewriteGEPs = GEP2 != nullptr;

  // Emit the offset of the GEP and an intptr_t.
  bool GEP1IsInBounds = GEP1->isInBounds();
  Value *Result = EmitGEPOffset(GEP1, RewriteGEPs);

  // If this is a single inbounds GEP and the original sub was nuw,
  // then the final multiplication is also nuw.
  if (auto *I = dyn_cast<Instruction>(Result))
    if (IsNUW && !GEP2 && !Swapped && GEP1IsInBounds &&
        I->getOpcode() == Instruction::Mul)
      I->setHasNoUnsignedWrap();

  // If we have a 2nd GEP of the same base pointer, subtract the offsets.
  // If both GEPs are inbounds, then the subtract does not have signed overflow.
  if (GEP2) {
    bool GEP2IsInBounds = GEP2->isInBounds();
    Value *Offset = EmitGEPOffset(GEP2, RewriteGEPs);
    Result = Builder.CreateSub(Result, Offset, "gepdiff", /* NUW */ false,
                               GEP1IsInBounds && GEP2IsInBounds);
  }

  // If we have p - gep(p, ...)  then we have to negate the result.
  if (Swapped)
    Result = Builder.CreateNeg(Result, "diff.neg");

  return Builder.CreateIntCast(Result, Ty, true);
}

static Instruction *foldSubOfMinMax(BinaryOperator &I,
                                    InstCombiner::BuilderTy &Builder) {
  Value *Op0 = I.getOperand(0);
  Value *Op1 = I.getOperand(1);
  Type *Ty = I.getType();
  auto *MinMax = dyn_cast<MinMaxIntrinsic>(Op1);
  if (!MinMax)
    return nullptr;

  // sub(add(X,Y), s/umin(X,Y)) --> s/umax(X,Y)
  // sub(add(X,Y), s/umax(X,Y)) --> s/umin(X,Y)
  Value *X = MinMax->getLHS();
  Value *Y = MinMax->getRHS();
  if (match(Op0, m_c_Add(m_Specific(X), m_Specific(Y))) &&
      (Op0->hasOneUse() || Op1->hasOneUse())) {
    Intrinsic::ID InvID = getInverseMinMaxIntrinsic(MinMax->getIntrinsicID());
    Function *F = Intrinsic::getDeclaration(I.getModule(), InvID, Ty);
    return CallInst::Create(F, {X, Y});
  }

  // sub(add(X,Y),umin(Y,Z)) --> add(X,usub.sat(Y,Z))
  // sub(add(X,Z),umin(Y,Z)) --> add(X,usub.sat(Z,Y))
  Value *Z;
  if (match(Op1, m_OneUse(m_UMin(m_Value(Y), m_Value(Z))))) {
    if (match(Op0, m_OneUse(m_c_Add(m_Specific(Y), m_Value(X))))) {
      Value *USub = Builder.CreateIntrinsic(Intrinsic::usub_sat, Ty, {Y, Z});
      return BinaryOperator::CreateAdd(X, USub);
    }
    if (match(Op0, m_OneUse(m_c_Add(m_Specific(Z), m_Value(X))))) {
      Value *USub = Builder.CreateIntrinsic(Intrinsic::usub_sat, Ty, {Z, Y});
      return BinaryOperator::CreateAdd(X, USub);
    }
  }

  // sub Op0, smin((sub nsw Op0, Z), 0) --> smax Op0, Z
  // sub Op0, smax((sub nsw Op0, Z), 0) --> smin Op0, Z
  if (MinMax->isSigned() && match(Y, m_ZeroInt()) &&
      match(X, m_NSWSub(m_Specific(Op0), m_Value(Z)))) {
    Intrinsic::ID InvID = getInverseMinMaxIntrinsic(MinMax->getIntrinsicID());
    Function *F = Intrinsic::getDeclaration(I.getModule(), InvID, Ty);
    return CallInst::Create(F, {Op0, Z});
  }

  return nullptr;
}

Instruction *InstCombinerImpl::visitSub(BinaryOperator &I) {
  if (Value *V = simplifySubInst(I.getOperand(0), I.getOperand(1),
                                 I.hasNoSignedWrap(), I.hasNoUnsignedWrap(),
                                 SQ.getWithInstruction(&I)))
    return replaceInstUsesWith(I, V);

  if (Instruction *X = foldVectorBinop(I))
    return X;

  if (Instruction *Phi = foldBinopWithPhiOperands(I))
    return Phi;

  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1);

  // If this is a 'B = x-(-A)', change to B = x+A.
  // We deal with this without involving Negator to preserve NSW flag.
  if (Value *V = dyn_castNegVal(Op1)) {
    BinaryOperator *Res = BinaryOperator::CreateAdd(Op0, V);

    if (const auto *BO = dyn_cast<BinaryOperator>(Op1)) {
      assert(BO->getOpcode() == Instruction::Sub &&
             "Expected a subtraction operator!");
      if (BO->hasNoSignedWrap() && I.hasNoSignedWrap())
        Res->setHasNoSignedWrap(true);
    } else {
      if (cast<Constant>(Op1)->isNotMinSignedValue() && I.hasNoSignedWrap())
        Res->setHasNoSignedWrap(true);
    }

    return Res;
  }

  // Try this before Negator to preserve NSW flag.
  if (Instruction *R = factorizeMathWithShlOps(I, Builder))
    return R;

  Constant *C;
  if (match(Op0, m_ImmConstant(C))) {
    Value *X;
    Constant *C2;

    // C-(X+C2) --> (C-C2)-X
    if (match(Op1, m_Add(m_Value(X), m_ImmConstant(C2)))) {
      // C-C2 never overflow, and C-(X+C2), (X+C2) has NSW/NUW
      // => (C-C2)-X can have NSW/NUW
      bool WillNotSOV = willNotOverflowSignedSub(C, C2, I);
      BinaryOperator *Res =
          BinaryOperator::CreateSub(ConstantExpr::getSub(C, C2), X);
      auto *OBO1 = cast<OverflowingBinaryOperator>(Op1);
      Res->setHasNoSignedWrap(I.hasNoSignedWrap() && OBO1->hasNoSignedWrap() &&
                              WillNotSOV);
      Res->setHasNoUnsignedWrap(I.hasNoUnsignedWrap() &&
                                OBO1->hasNoUnsignedWrap());
      return Res;
    }
  }

  auto TryToNarrowDeduceFlags = [this, &I, &Op0, &Op1]() -> Instruction * {
    if (Instruction *Ext = narrowMathIfNoOverflow(I))
      return Ext;

    bool Changed = false;
    if (!I.hasNoSignedWrap() && willNotOverflowSignedSub(Op0, Op1, I)) {
      Changed = true;
      I.setHasNoSignedWrap(true);
    }
    if (!I.hasNoUnsignedWrap() && willNotOverflowUnsignedSub(Op0, Op1, I)) {
      Changed = true;
      I.setHasNoUnsignedWrap(true);
    }

    return Changed ? &I : nullptr;
  };

  // First, let's try to interpret `sub a, b` as `add a, (sub 0, b)`,
  // and let's try to sink `(sub 0, b)` into `b` itself. But only if this isn't
  // a pure negation used by a select that looks like abs/nabs.
  bool IsNegation = match(Op0, m_ZeroInt());
  if (!IsNegation || none_of(I.users(), [&I, Op1](const User *U) {
        const Instruction *UI = dyn_cast<Instruction>(U);
        if (!UI)
          return false;
        return match(UI,
                     m_Select(m_Value(), m_Specific(Op1), m_Specific(&I))) ||
               match(UI, m_Select(m_Value(), m_Specific(&I), m_Specific(Op1)));
      })) {
    if (Value *NegOp1 = Negator::Negate(IsNegation, /* IsNSW */ IsNegation &&
                                                        I.hasNoSignedWrap(),
                                        Op1, *this))
      return BinaryOperator::CreateAdd(NegOp1, Op0);
  }
  if (IsNegation)
    return TryToNarrowDeduceFlags(); // Should have been handled in Negator!

  // (A*B)-(A*C) -> A*(B-C) etc
  if (Value *V = foldUsingDistributiveLaws(I))
    return replaceInstUsesWith(I, V);

  if (I.getType()->isIntOrIntVectorTy(1))
    return BinaryOperator::CreateXor(Op0, Op1);

  // Replace (-1 - A) with (~A).
  if (match(Op0, m_AllOnes()))
    return BinaryOperator::CreateNot(Op1);

  // (X + -1) - Y --> ~Y + X
  Value *X, *Y;
  if (match(Op0, m_OneUse(m_Add(m_Value(X), m_AllOnes()))))
    return BinaryOperator::CreateAdd(Builder.CreateNot(Op1), X);

  // Reassociate sub/add sequences to create more add instructions and
  // reduce dependency chains:
  // ((X - Y) + Z) - Op1 --> (X + Z) - (Y + Op1)
  Value *Z;
  if (match(Op0, m_OneUse(m_c_Add(m_OneUse(m_Sub(m_Value(X), m_Value(Y))),
                                  m_Value(Z))))) {
    Value *XZ = Builder.CreateAdd(X, Z);
    Value *YW = Builder.CreateAdd(Y, Op1);
    return BinaryOperator::CreateSub(XZ, YW);
  }

  // ((X - Y) - Op1)  -->  X - (Y + Op1)
  if (match(Op0, m_OneUse(m_Sub(m_Value(X), m_Value(Y))))) {
    OverflowingBinaryOperator *LHSSub = cast<OverflowingBinaryOperator>(Op0);
    bool HasNUW = I.hasNoUnsignedWrap() && LHSSub->hasNoUnsignedWrap();
    bool HasNSW = HasNUW && I.hasNoSignedWrap() && LHSSub->hasNoSignedWrap();
    Value *Add = Builder.CreateAdd(Y, Op1, "", /* HasNUW */ HasNUW,
                                   /* HasNSW */ HasNSW);
    BinaryOperator *Sub = BinaryOperator::CreateSub(X, Add);
    Sub->setHasNoUnsignedWrap(HasNUW);
    Sub->setHasNoSignedWrap(HasNSW);
    return Sub;
  }

  {
    // (X + Z) - (Y + Z) --> (X - Y)
    // This is done in other passes, but we want to be able to consume this
    // pattern in InstCombine so we can generate it without creating infinite
    // loops.
    if (match(Op0, m_Add(m_Value(X), m_Value(Z))) &&
        match(Op1, m_c_Add(m_Value(Y), m_Specific(Z))))
      return BinaryOperator::CreateSub(X, Y);

    // (X + C0) - (Y + C1) --> (X - Y) + (C0 - C1)
    Constant *CX, *CY;
    if (match(Op0, m_OneUse(m_Add(m_Value(X), m_ImmConstant(CX)))) &&
        match(Op1, m_OneUse(m_Add(m_Value(Y), m_ImmConstant(CY))))) {
      Value *OpsSub = Builder.CreateSub(X, Y);
      Constant *ConstsSub = ConstantExpr::getSub(CX, CY);
      return BinaryOperator::CreateAdd(OpsSub, ConstsSub);
    }
  }

  // (~X) - (~Y) --> Y - X
  {
    // Need to ensure we can consume at least one of the `not` instructions,
    // otherwise this can inf loop.
    bool ConsumesOp0, ConsumesOp1;
    if (isFreeToInvert(Op0, Op0->hasOneUse(), ConsumesOp0) &&
        isFreeToInvert(Op1, Op1->hasOneUse(), ConsumesOp1) &&
        (ConsumesOp0 || ConsumesOp1)) {
      Value *NotOp0 = getFreelyInverted(Op0, Op0->hasOneUse(), &Builder);
      Value *NotOp1 = getFreelyInverted(Op1, Op1->hasOneUse(), &Builder);
      assert(NotOp0 != nullptr && NotOp1 != nullptr &&
             "isFreeToInvert desynced with getFreelyInverted");
      return BinaryOperator::CreateSub(NotOp1, NotOp0);
    }
  }

  auto m_AddRdx = [](Value *&Vec) {
    return m_OneUse(m_Intrinsic<Intrinsic::vector_reduce_add>(m_Value(Vec)));
  };
  Value *V0, *V1;
  if (match(Op0, m_AddRdx(V0)) && match(Op1, m_AddRdx(V1)) &&
      V0->getType() == V1->getType()) {
    // Difference of sums is sum of differences:
    // add_rdx(V0) - add_rdx(V1) --> add_rdx(V0 - V1)
    Value *Sub = Builder.CreateSub(V0, V1);
    Value *Rdx = Builder.CreateIntrinsic(Intrinsic::vector_reduce_add,
                                         {Sub->getType()}, {Sub});
    return replaceInstUsesWith(I, Rdx);
  }

  if (Constant *C = dyn_cast<Constant>(Op0)) {
    Value *X;
    if (match(Op1, m_ZExt(m_Value(X))) && X->getType()->isIntOrIntVectorTy(1))
      // C - (zext bool) --> bool ? C - 1 : C
      return SelectInst::Create(X, InstCombiner::SubOne(C), C);
    if (match(Op1, m_SExt(m_Value(X))) && X->getType()->isIntOrIntVectorTy(1))
      // C - (sext bool) --> bool ? C + 1 : C
      return SelectInst::Create(X, InstCombiner::AddOne(C), C);

    // C - ~X == X + (1+C)
    if (match(Op1, m_Not(m_Value(X))))
      return BinaryOperator::CreateAdd(X, InstCombiner::AddOne(C));

    // Try to fold constant sub into select arguments.
    if (SelectInst *SI = dyn_cast<SelectInst>(Op1))
      if (Instruction *R = FoldOpIntoSelect(I, SI))
        return R;

    // Try to fold constant sub into PHI values.
    if (PHINode *PN = dyn_cast<PHINode>(Op1))
      if (Instruction *R = foldOpIntoPhi(I, PN))
        return R;

    Constant *C2;

    // C-(C2-X) --> X+(C-C2)
    if (match(Op1, m_Sub(m_ImmConstant(C2), m_Value(X))))
      return BinaryOperator::CreateAdd(X, ConstantExpr::getSub(C, C2));
  }

  const APInt *Op0C;
  if (match(Op0, m_APInt(Op0C))) {
    if (Op0C->isMask()) {
      // Turn this into a xor if LHS is 2^n-1 and the remaining bits are known
      // zero. We don't use information from dominating conditions so this
      // transform is easier to reverse if necessary.
      KnownBits RHSKnown = llvm::computeKnownBits(
          Op1, 0, SQ.getWithInstruction(&I).getWithoutDomCondCache());
      if ((*Op0C | RHSKnown.Zero).isAllOnes())
        return BinaryOperator::CreateXor(Op1, Op0);
    }

    // C - ((C3 -nuw X) & C2) --> (C - (C2 & C3)) + (X & C2) when:
    // (C3 - ((C2 & C3) - 1)) is pow2
    // ((C2 + C3) & ((C2 & C3) - 1)) == ((C2 & C3) - 1)
    // C2 is negative pow2 || sub nuw
    const APInt *C2, *C3;
    BinaryOperator *InnerSub;
    if (match(Op1, m_OneUse(m_And(m_BinOp(InnerSub), m_APInt(C2)))) &&
        match(InnerSub, m_Sub(m_APInt(C3), m_Value(X))) &&
        (InnerSub->hasNoUnsignedWrap() || C2->isNegatedPowerOf2())) {
      APInt C2AndC3 = *C2 & *C3;
      APInt C2AndC3Minus1 = C2AndC3 - 1;
      APInt C2AddC3 = *C2 + *C3;
      if ((*C3 - C2AndC3Minus1).isPowerOf2() &&
          C2AndC3Minus1.isSubsetOf(C2AddC3)) {
        Value *And = Builder.CreateAnd(X, ConstantInt::get(I.getType(), *C2));
        return BinaryOperator::CreateAdd(
            And, ConstantInt::get(I.getType(), *Op0C - C2AndC3));
      }
    }
  }

  {
    Value *Y;
    // X-(X+Y) == -Y    X-(Y+X) == -Y
    if (match(Op1, m_c_Add(m_Specific(Op0), m_Value(Y))))
      return BinaryOperator::CreateNeg(Y);

    // (X-Y)-X == -Y
    if (match(Op0, m_Sub(m_Specific(Op1), m_Value(Y))))
      return BinaryOperator::CreateNeg(Y);
  }

  // (sub (or A, B) (and A, B)) --> (xor A, B)
  {
    Value *A, *B;
    if (match(Op1, m_And(m_Value(A), m_Value(B))) &&
        match(Op0, m_c_Or(m_Specific(A), m_Specific(B))))
      return BinaryOperator::CreateXor(A, B);
  }

  // (sub (add A, B) (or A, B)) --> (and A, B)
  {
    Value *A, *B;
    if (match(Op0, m_Add(m_Value(A), m_Value(B))) &&
        match(Op1, m_c_Or(m_Specific(A), m_Specific(B))))
      return BinaryOperator::CreateAnd(A, B);
  }

  // (sub (add A, B) (and A, B)) --> (or A, B)
  {
    Value *A, *B;
    if (match(Op0, m_Add(m_Value(A), m_Value(B))) &&
        match(Op1, m_c_And(m_Specific(A), m_Specific(B))))
      return BinaryOperator::CreateOr(A, B);
  }

  // (sub (and A, B) (or A, B)) --> neg (xor A, B)
  {
    Value *A, *B;
    if (match(Op0, m_And(m_Value(A), m_Value(B))) &&
        match(Op1, m_c_Or(m_Specific(A), m_Specific(B))) &&
        (Op0->hasOneUse() || Op1->hasOneUse()))
      return BinaryOperator::CreateNeg(Builder.CreateXor(A, B));
  }

  // (sub (or A, B), (xor A, B)) --> (and A, B)
  {
    Value *A, *B;
    if (match(Op1, m_Xor(m_Value(A), m_Value(B))) &&
        match(Op0, m_c_Or(m_Specific(A), m_Specific(B))))
      return BinaryOperator::CreateAnd(A, B);
  }

  // (sub (xor A, B) (or A, B)) --> neg (and A, B)
  {
    Value *A, *B;
    if (match(Op0, m_Xor(m_Value(A), m_Value(B))) &&
        match(Op1, m_c_Or(m_Specific(A), m_Specific(B))) &&
        (Op0->hasOneUse() || Op1->hasOneUse()))
      return BinaryOperator::CreateNeg(Builder.CreateAnd(A, B));
  }

  {
    Value *Y;
    // ((X | Y) - X) --> (~X & Y)
    if (match(Op0, m_OneUse(m_c_Or(m_Value(Y), m_Specific(Op1)))))
      return BinaryOperator::CreateAnd(
          Y, Builder.CreateNot(Op1, Op1->getName() + ".not"));
  }

  {
    // (sub (and Op1, (neg X)), Op1) --> neg (and Op1, (add X, -1))
    Value *X;
    if (match(Op0, m_OneUse(m_c_And(m_Specific(Op1),
                                    m_OneUse(m_Neg(m_Value(X))))))) {
      return BinaryOperator::CreateNeg(Builder.CreateAnd(
          Op1, Builder.CreateAdd(X, Constant::getAllOnesValue(I.getType()))));
    }
  }

  {
    // (sub (and Op1, C), Op1) --> neg (and Op1, ~C)
    Constant *C;
    if (match(Op0, m_OneUse(m_And(m_Specific(Op1), m_Constant(C))))) {
      return BinaryOperator::CreateNeg(
          Builder.CreateAnd(Op1, Builder.CreateNot(C)));
    }
  }

  {
    // (sub (xor X, (sext C)), (sext C)) => (select C, (neg X), X)
    // (sub (sext C), (xor X, (sext C))) => (select C, X, (neg X))
    Value *C, *X;
    auto m_SubXorCmp = [&C, &X](Value *LHS, Value *RHS) {
      return match(LHS, m_OneUse(m_c_Xor(m_Value(X), m_Specific(RHS)))) &&
             match(RHS, m_SExt(m_Value(C))) &&
             (C->getType()->getScalarSizeInBits() == 1);
    };
    if (m_SubXorCmp(Op0, Op1))
      return SelectInst::Create(C, Builder.CreateNeg(X), X);
    if (m_SubXorCmp(Op1, Op0))
      return SelectInst::Create(C, X, Builder.CreateNeg(X));
  }

  if (Instruction *R = tryFoldInstWithCtpopWithNot(&I))
    return R;

  if (Instruction *R = foldSubOfMinMax(I, Builder))
    return R;

  {
    // If we have a subtraction between some value and a select between
    // said value and something else, sink subtraction into select hands, i.e.:
    //   sub (select %Cond, %TrueVal, %FalseVal), %Op1
    //     ->
    //   select %Cond, (sub %TrueVal, %Op1), (sub %FalseVal, %Op1)
    //  or
    //   sub %Op0, (select %Cond, %TrueVal, %FalseVal)
    //     ->
    //   select %Cond, (sub %Op0, %TrueVal), (sub %Op0, %FalseVal)
    // This will result in select between new subtraction and 0.
    auto SinkSubIntoSelect =
        [Ty = I.getType()](Value *Select, Value *OtherHandOfSub,
                           auto SubBuilder) -> Instruction * {
      Value *Cond, *TrueVal, *FalseVal;
      if (!match(Select, m_OneUse(m_Select(m_Value(Cond), m_Value(TrueVal),
                                           m_Value(FalseVal)))))
        return nullptr;
      if (OtherHandOfSub != TrueVal && OtherHandOfSub != FalseVal)
        return nullptr;
      // While it is really tempting to just create two subtractions and let
      // InstCombine fold one of those to 0, it isn't possible to do so
      // because of worklist visitation order. So ugly it is.
      bool OtherHandOfSubIsTrueVal = OtherHandOfSub == TrueVal;
      Value *NewSub = SubBuilder(OtherHandOfSubIsTrueVal ? FalseVal : TrueVal);
      Constant *Zero = Constant::getNullValue(Ty);
      SelectInst *NewSel =
          SelectInst::Create(Cond, OtherHandOfSubIsTrueVal ? Zero : NewSub,
                             OtherHandOfSubIsTrueVal ? NewSub : Zero);
      // Preserve prof metadata if any.
      NewSel->copyMetadata(cast<Instruction>(*Select));
      return NewSel;
    };
    if (Instruction *NewSel = SinkSubIntoSelect(
            /*Select=*/Op0, /*OtherHandOfSub=*/Op1,
            [Builder = &Builder, Op1](Value *OtherHandOfSelect) {
              return Builder->CreateSub(OtherHandOfSelect,
                                        /*OtherHandOfSub=*/Op1);
            }))
      return NewSel;
    if (Instruction *NewSel = SinkSubIntoSelect(
            /*Select=*/Op1, /*OtherHandOfSub=*/Op0,
            [Builder = &Builder, Op0](Value *OtherHandOfSelect) {
              return Builder->CreateSub(/*OtherHandOfSub=*/Op0,
                                        OtherHandOfSelect);
            }))
      return NewSel;
  }

  // (X - (X & Y))   -->   (X & ~Y)
  if (match(Op1, m_c_And(m_Specific(Op0), m_Value(Y))) &&
      (Op1->hasOneUse() || isa<Constant>(Y)))
    return BinaryOperator::CreateAnd(
        Op0, Builder.CreateNot(Y, Y->getName() + ".not"));

  // ~X - Min/Max(~X, Y) -> ~Min/Max(X, ~Y) - X
  // ~X - Min/Max(Y, ~X) -> ~Min/Max(X, ~Y) - X
  // Min/Max(~X, Y) - ~X -> X - ~Min/Max(X, ~Y)
  // Min/Max(Y, ~X) - ~X -> X - ~Min/Max(X, ~Y)
  // As long as Y is freely invertible, this will be neutral or a win.
  // Note: We don't generate the inverse max/min, just create the 'not' of
  // it and let other folds do the rest.
  if (match(Op0, m_Not(m_Value(X))) &&
      match(Op1, m_c_MaxOrMin(m_Specific(Op0), m_Value(Y))) &&
      !Op0->hasNUsesOrMore(3) && isFreeToInvert(Y, Y->hasOneUse())) {
    Value *Not = Builder.CreateNot(Op1);
    return BinaryOperator::CreateSub(Not, X);
  }
  if (match(Op1, m_Not(m_Value(X))) &&
      match(Op0, m_c_MaxOrMin(m_Specific(Op1), m_Value(Y))) &&
      !Op1->hasNUsesOrMore(3) && isFreeToInvert(Y, Y->hasOneUse())) {
    Value *Not = Builder.CreateNot(Op0);
    return BinaryOperator::CreateSub(X, Not);
  }

  // Optimize pointer differences into the same array into a size.  Consider:
  //  &A[10] - &A[0]: we should compile this to "10".
  Value *LHSOp, *RHSOp;
  if (match(Op0, m_PtrToInt(m_Value(LHSOp))) &&
      match(Op1, m_PtrToInt(m_Value(RHSOp))))
    if (Value *Res = OptimizePointerDifference(LHSOp, RHSOp, I.getType(),
                                               I.hasNoUnsignedWrap()))
      return replaceInstUsesWith(I, Res);

  // trunc(p)-trunc(q) -> trunc(p-q)
  if (match(Op0, m_Trunc(m_PtrToInt(m_Value(LHSOp)))) &&
      match(Op1, m_Trunc(m_PtrToInt(m_Value(RHSOp)))))
    if (Value *Res = OptimizePointerDifference(LHSOp, RHSOp, I.getType(),
                                               /* IsNUW */ false))
      return replaceInstUsesWith(I, Res);

  // Canonicalize a shifty way to code absolute value to the common pattern.
  // There are 2 potential commuted variants.
  // We're relying on the fact that we only do this transform when the shift has
  // exactly 2 uses and the xor has exactly 1 use (otherwise, we might increase
  // instructions).
  Value *A;
  const APInt *ShAmt;
  Type *Ty = I.getType();
  unsigned BitWidth = Ty->getScalarSizeInBits();
  if (match(Op1, m_AShr(m_Value(A), m_APInt(ShAmt))) &&
      Op1->hasNUses(2) && *ShAmt == BitWidth - 1 &&
      match(Op0, m_OneUse(m_c_Xor(m_Specific(A), m_Specific(Op1))))) {
    // B = ashr i32 A, 31 ; smear the sign bit
    // sub (xor A, B), B  ; flip bits if negative and subtract -1 (add 1)
    // --> (A < 0) ? -A : A
    Value *IsNeg = Builder.CreateIsNeg(A);
    // Copy the nsw flags from the sub to the negate.
    Value *NegA = I.hasNoUnsignedWrap()
                      ? Constant::getNullValue(A->getType())
                      : Builder.CreateNeg(A, "", I.hasNoSignedWrap());
    return SelectInst::Create(IsNeg, NegA, A);
  }

  // If we are subtracting a low-bit masked subset of some value from an add
  // of that same value with no low bits changed, that is clearing some low bits
  // of the sum:
  // sub (X + AddC), (X & AndC) --> and (X + AddC), ~AndC
  const APInt *AddC, *AndC;
  if (match(Op0, m_Add(m_Value(X), m_APInt(AddC))) &&
      match(Op1, m_And(m_Specific(X), m_APInt(AndC)))) {
    unsigned Cttz = AddC->countr_zero();
    APInt HighMask(APInt::getHighBitsSet(BitWidth, BitWidth - Cttz));
    if ((HighMask & *AndC).isZero())
      return BinaryOperator::CreateAnd(Op0, ConstantInt::get(Ty, ~(*AndC)));
  }

  if (Instruction *V =
          canonicalizeCondSignextOfHighBitExtractToSignextHighBitExtract(I))
    return V;

  // X - usub.sat(X, Y) => umin(X, Y)
  if (match(Op1, m_OneUse(m_Intrinsic<Intrinsic::usub_sat>(m_Specific(Op0),
                                                           m_Value(Y)))))
    return replaceInstUsesWith(
        I, Builder.CreateIntrinsic(Intrinsic::umin, {I.getType()}, {Op0, Y}));

  // umax(X, Op1) - Op1 --> usub.sat(X, Op1)
  // TODO: The one-use restriction is not strictly necessary, but it may
  //       require improving other pattern matching and/or codegen.
  if (match(Op0, m_OneUse(m_c_UMax(m_Value(X), m_Specific(Op1)))))
    return replaceInstUsesWith(
        I, Builder.CreateIntrinsic(Intrinsic::usub_sat, {Ty}, {X, Op1}));

  // Op0 - umin(X, Op0) --> usub.sat(Op0, X)
  if (match(Op1, m_OneUse(m_c_UMin(m_Value(X), m_Specific(Op0)))))
    return replaceInstUsesWith(
        I, Builder.CreateIntrinsic(Intrinsic::usub_sat, {Ty}, {Op0, X}));

  // Op0 - umax(X, Op0) --> 0 - usub.sat(X, Op0)
  if (match(Op1, m_OneUse(m_c_UMax(m_Value(X), m_Specific(Op0))))) {
    Value *USub = Builder.CreateIntrinsic(Intrinsic::usub_sat, {Ty}, {X, Op0});
    return BinaryOperator::CreateNeg(USub);
  }

  // umin(X, Op1) - Op1 --> 0 - usub.sat(Op1, X)
  if (match(Op0, m_OneUse(m_c_UMin(m_Value(X), m_Specific(Op1))))) {
    Value *USub = Builder.CreateIntrinsic(Intrinsic::usub_sat, {Ty}, {Op1, X});
    return BinaryOperator::CreateNeg(USub);
  }

  // C - ctpop(X) => ctpop(~X) if C is bitwidth
  if (match(Op0, m_SpecificInt(BitWidth)) &&
      match(Op1, m_OneUse(m_Intrinsic<Intrinsic::ctpop>(m_Value(X)))))
    return replaceInstUsesWith(
        I, Builder.CreateIntrinsic(Intrinsic::ctpop, {I.getType()},
                                   {Builder.CreateNot(X)}));

  // Reduce multiplies for difference-of-squares by factoring:
  // (X * X) - (Y * Y) --> (X + Y) * (X - Y)
  if (match(Op0, m_OneUse(m_Mul(m_Value(X), m_Deferred(X)))) &&
      match(Op1, m_OneUse(m_Mul(m_Value(Y), m_Deferred(Y))))) {
    auto *OBO0 = cast<OverflowingBinaryOperator>(Op0);
    auto *OBO1 = cast<OverflowingBinaryOperator>(Op1);
    bool PropagateNSW = I.hasNoSignedWrap() && OBO0->hasNoSignedWrap() &&
                        OBO1->hasNoSignedWrap() && BitWidth > 2;
    bool PropagateNUW = I.hasNoUnsignedWrap() && OBO0->hasNoUnsignedWrap() &&
                        OBO1->hasNoUnsignedWrap() && BitWidth > 1;
    Value *Add = Builder.CreateAdd(X, Y, "add", PropagateNUW, PropagateNSW);
    Value *Sub = Builder.CreateSub(X, Y, "sub", PropagateNUW, PropagateNSW);
    Value *Mul = Builder.CreateMul(Add, Sub, "", PropagateNUW, PropagateNSW);
    return replaceInstUsesWith(I, Mul);
  }

  // max(X,Y) nsw/nuw - min(X,Y) --> abs(X nsw - Y)
  if (match(Op0, m_OneUse(m_c_SMax(m_Value(X), m_Value(Y)))) &&
      match(Op1, m_OneUse(m_c_SMin(m_Specific(X), m_Specific(Y))))) {
    if (I.hasNoUnsignedWrap() || I.hasNoSignedWrap()) {
      Value *Sub =
          Builder.CreateSub(X, Y, "sub", /*HasNUW=*/false, /*HasNSW=*/true);
      Value *Call =
          Builder.CreateBinaryIntrinsic(Intrinsic::abs, Sub, Builder.getTrue());
      return replaceInstUsesWith(I, Call);
    }
  }

  if (Instruction *Res = foldBinOpOfSelectAndCastOfSelectCondition(I))
    return Res;

  return TryToNarrowDeduceFlags();
}

/// This eliminates floating-point negation in either 'fneg(X)' or
/// 'fsub(-0.0, X)' form by combining into a constant operand.
static Instruction *foldFNegIntoConstant(Instruction &I, const DataLayout &DL) {
  // This is limited with one-use because fneg is assumed better for
  // reassociation and cheaper in codegen than fmul/fdiv.
  // TODO: Should the m_OneUse restriction be removed?
  Instruction *FNegOp;
  if (!match(&I, m_FNeg(m_OneUse(m_Instruction(FNegOp)))))
    return nullptr;

  Value *X;
  Constant *C;

  // Fold negation into constant operand.
  // -(X * C) --> X * (-C)
  if (match(FNegOp, m_FMul(m_Value(X), m_Constant(C))))
    if (Constant *NegC = ConstantFoldUnaryOpOperand(Instruction::FNeg, C, DL))
      return BinaryOperator::CreateFMulFMF(X, NegC, &I);
  // -(X / C) --> X / (-C)
  if (match(FNegOp, m_FDiv(m_Value(X), m_Constant(C))))
    if (Constant *NegC = ConstantFoldUnaryOpOperand(Instruction::FNeg, C, DL))
      return BinaryOperator::CreateFDivFMF(X, NegC, &I);
  // -(C / X) --> (-C) / X
  if (match(FNegOp, m_FDiv(m_Constant(C), m_Value(X))))
    if (Constant *NegC = ConstantFoldUnaryOpOperand(Instruction::FNeg, C, DL)) {
      Instruction *FDiv = BinaryOperator::CreateFDivFMF(NegC, X, &I);

      // Intersect 'nsz' and 'ninf' because those special value exceptions may
      // not apply to the fdiv. Everything else propagates from the fneg.
      // TODO: We could propagate nsz/ninf from fdiv alone?
      FastMathFlags FMF = I.getFastMathFlags();
      FastMathFlags OpFMF = FNegOp->getFastMathFlags();
      FDiv->setHasNoSignedZeros(FMF.noSignedZeros() && OpFMF.noSignedZeros());
      FDiv->setHasNoInfs(FMF.noInfs() && OpFMF.noInfs());
      return FDiv;
    }
  // With NSZ [ counter-example with -0.0: -(-0.0 + 0.0) != 0.0 + -0.0 ]:
  // -(X + C) --> -X + -C --> -C - X
  if (I.hasNoSignedZeros() && match(FNegOp, m_FAdd(m_Value(X), m_Constant(C))))
    if (Constant *NegC = ConstantFoldUnaryOpOperand(Instruction::FNeg, C, DL))
      return BinaryOperator::CreateFSubFMF(NegC, X, &I);

  return nullptr;
}

Instruction *InstCombinerImpl::hoistFNegAboveFMulFDiv(Value *FNegOp,
                                                      Instruction &FMFSource) {
  Value *X, *Y;
  if (match(FNegOp, m_FMul(m_Value(X), m_Value(Y)))) {
    return cast<Instruction>(Builder.CreateFMulFMF(
        Builder.CreateFNegFMF(X, &FMFSource), Y, &FMFSource));
  }

  if (match(FNegOp, m_FDiv(m_Value(X), m_Value(Y)))) {
    return cast<Instruction>(Builder.CreateFDivFMF(
        Builder.CreateFNegFMF(X, &FMFSource), Y, &FMFSource));
  }

  if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(FNegOp)) {
    // Make sure to preserve flags and metadata on the call.
    if (II->getIntrinsicID() == Intrinsic::ldexp) {
      FastMathFlags FMF = FMFSource.getFastMathFlags() | II->getFastMathFlags();
      IRBuilder<>::FastMathFlagGuard FMFGuard(Builder);
      Builder.setFastMathFlags(FMF);

      CallInst *New = Builder.CreateCall(
          II->getCalledFunction(),
          {Builder.CreateFNeg(II->getArgOperand(0)), II->getArgOperand(1)});
      New->copyMetadata(*II);
      return New;
    }
  }

  return nullptr;
}

Instruction *InstCombinerImpl::visitFNeg(UnaryOperator &I) {
  Value *Op = I.getOperand(0);

  if (Value *V = simplifyFNegInst(Op, I.getFastMathFlags(),
                                  getSimplifyQuery().getWithInstruction(&I)))
    return replaceInstUsesWith(I, V);

  if (Instruction *X = foldFNegIntoConstant(I, DL))
    return X;

  Value *X, *Y;

  // If we can ignore the sign of zeros: -(X - Y) --> (Y - X)
  if (I.hasNoSignedZeros() &&
      match(Op, m_OneUse(m_FSub(m_Value(X), m_Value(Y)))))
    return BinaryOperator::CreateFSubFMF(Y, X, &I);

  Value *OneUse;
  if (!match(Op, m_OneUse(m_Value(OneUse))))
    return nullptr;

  if (Instruction *R = hoistFNegAboveFMulFDiv(OneUse, I))
    return replaceInstUsesWith(I, R);

  // Try to eliminate fneg if at least 1 arm of the select is negated.
  Value *Cond;
  if (match(OneUse, m_Select(m_Value(Cond), m_Value(X), m_Value(Y)))) {
    // Unlike most transforms, this one is not safe to propagate nsz unless
    // it is present on the original select. We union the flags from the select
    // and fneg and then remove nsz if needed.
    auto propagateSelectFMF = [&](SelectInst *S, bool CommonOperand) {
      S->copyFastMathFlags(&I);
      if (auto *OldSel = dyn_cast<SelectInst>(Op)) {
        FastMathFlags FMF = I.getFastMathFlags() | OldSel->getFastMathFlags();
        S->setFastMathFlags(FMF);
        if (!OldSel->hasNoSignedZeros() && !CommonOperand &&
            !isGuaranteedNotToBeUndefOrPoison(OldSel->getCondition()))
          S->setHasNoSignedZeros(false);
      }
    };
    // -(Cond ? -P : Y) --> Cond ? P : -Y
    Value *P;
    if (match(X, m_FNeg(m_Value(P)))) {
      Value *NegY = Builder.CreateFNegFMF(Y, &I, Y->getName() + ".neg");
      SelectInst *NewSel = SelectInst::Create(Cond, P, NegY);
      propagateSelectFMF(NewSel, P == Y);
      return NewSel;
    }
    // -(Cond ? X : -P) --> Cond ? -X : P
    if (match(Y, m_FNeg(m_Value(P)))) {
      Value *NegX = Builder.CreateFNegFMF(X, &I, X->getName() + ".neg");
      SelectInst *NewSel = SelectInst::Create(Cond, NegX, P);
      propagateSelectFMF(NewSel, P == X);
      return NewSel;
    }

    // -(Cond ? X : C) --> Cond ? -X : -C
    // -(Cond ? C : Y) --> Cond ? -C : -Y
    if (match(X, m_ImmConstant()) || match(Y, m_ImmConstant())) {
      Value *NegX = Builder.CreateFNegFMF(X, &I, X->getName() + ".neg");
      Value *NegY = Builder.CreateFNegFMF(Y, &I, Y->getName() + ".neg");
      SelectInst *NewSel = SelectInst::Create(Cond, NegX, NegY);
      propagateSelectFMF(NewSel, /*CommonOperand=*/true);
      return NewSel;
    }
  }

  // fneg (copysign x, y) -> copysign x, (fneg y)
  if (match(OneUse, m_CopySign(m_Value(X), m_Value(Y)))) {
    // The source copysign has an additional value input, so we can't propagate
    // flags the copysign doesn't also have.
    FastMathFlags FMF = I.getFastMathFlags();
    FMF &= cast<FPMathOperator>(OneUse)->getFastMathFlags();

    IRBuilder<>::FastMathFlagGuard FMFGuard(Builder);
    Builder.setFastMathFlags(FMF);

    Value *NegY = Builder.CreateFNeg(Y);
    Value *NewCopySign = Builder.CreateCopySign(X, NegY);
    return replaceInstUsesWith(I, NewCopySign);
  }

  return nullptr;
}

Instruction *InstCombinerImpl::visitFSub(BinaryOperator &I) {
  if (Value *V = simplifyFSubInst(I.getOperand(0), I.getOperand(1),
                                  I.getFastMathFlags(),
                                  getSimplifyQuery().getWithInstruction(&I)))
    return replaceInstUsesWith(I, V);

  if (Instruction *X = foldVectorBinop(I))
    return X;

  if (Instruction *Phi = foldBinopWithPhiOperands(I))
    return Phi;

  // Subtraction from -0.0 is the canonical form of fneg.
  // fsub -0.0, X ==> fneg X
  // fsub nsz 0.0, X ==> fneg nsz X
  //
  // FIXME This matcher does not respect FTZ or DAZ yet:
  // fsub -0.0, Denorm ==> +-0
  // fneg Denorm ==> -Denorm
  Value *Op;
  if (match(&I, m_FNeg(m_Value(Op))))
    return UnaryOperator::CreateFNegFMF(Op, &I);

  if (Instruction *X = foldFNegIntoConstant(I, DL))
    return X;

  if (Instruction *R = foldFBinOpOfIntCasts(I))
    return R;

  Value *X, *Y;
  Constant *C;

  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1);
  // If Op0 is not -0.0 or we can ignore -0.0: Z - (X - Y) --> Z + (Y - X)
  // Canonicalize to fadd to make analysis easier.
  // This can also help codegen because fadd is commutative.
  // Note that if this fsub was really an fneg, the fadd with -0.0 will get
  // killed later. We still limit that particular transform with 'hasOneUse'
  // because an fneg is assumed better/cheaper than a generic fsub.
  if (I.hasNoSignedZeros() ||
      cannotBeNegativeZero(Op0, 0, getSimplifyQuery().getWithInstruction(&I))) {
    if (match(Op1, m_OneUse(m_FSub(m_Value(X), m_Value(Y))))) {
      Value *NewSub = Builder.CreateFSubFMF(Y, X, &I);
      return BinaryOperator::CreateFAddFMF(Op0, NewSub, &I);
    }
  }

  // (-X) - Op1 --> -(X + Op1)
  if (I.hasNoSignedZeros() && !isa<ConstantExpr>(Op0) &&
      match(Op0, m_OneUse(m_FNeg(m_Value(X))))) {
    Value *FAdd = Builder.CreateFAddFMF(X, Op1, &I);
    return UnaryOperator::CreateFNegFMF(FAdd, &I);
  }

  if (isa<Constant>(Op0))
    if (SelectInst *SI = dyn_cast<SelectInst>(Op1))
      if (Instruction *NV = FoldOpIntoSelect(I, SI))
        return NV;

  // X - C --> X + (-C)
  // But don't transform constant expressions because there's an inverse fold
  // for X + (-Y) --> X - Y.
  if (match(Op1, m_ImmConstant(C)))
    if (Constant *NegC = ConstantFoldUnaryOpOperand(Instruction::FNeg, C, DL))
      return BinaryOperator::CreateFAddFMF(Op0, NegC, &I);

  // X - (-Y) --> X + Y
  if (match(Op1, m_FNeg(m_Value(Y))))
    return BinaryOperator::CreateFAddFMF(Op0, Y, &I);

  // Similar to above, but look through a cast of the negated value:
  // X - (fptrunc(-Y)) --> X + fptrunc(Y)
  Type *Ty = I.getType();
  if (match(Op1, m_OneUse(m_FPTrunc(m_FNeg(m_Value(Y))))))
    return BinaryOperator::CreateFAddFMF(Op0, Builder.CreateFPTrunc(Y, Ty), &I);

  // X - (fpext(-Y)) --> X + fpext(Y)
  if (match(Op1, m_OneUse(m_FPExt(m_FNeg(m_Value(Y))))))
    return BinaryOperator::CreateFAddFMF(Op0, Builder.CreateFPExt(Y, Ty), &I);

  // Similar to above, but look through fmul/fdiv of the negated value:
  // Op0 - (-X * Y) --> Op0 + (X * Y)
  // Op0 - (Y * -X) --> Op0 + (X * Y)
  if (match(Op1, m_OneUse(m_c_FMul(m_FNeg(m_Value(X)), m_Value(Y))))) {
    Value *FMul = Builder.CreateFMulFMF(X, Y, &I);
    return BinaryOperator::CreateFAddFMF(Op0, FMul, &I);
  }
  // Op0 - (-X / Y) --> Op0 + (X / Y)
  // Op0 - (X / -Y) --> Op0 + (X / Y)
  if (match(Op1, m_OneUse(m_FDiv(m_FNeg(m_Value(X)), m_Value(Y)))) ||
      match(Op1, m_OneUse(m_FDiv(m_Value(X), m_FNeg(m_Value(Y)))))) {
    Value *FDiv = Builder.CreateFDivFMF(X, Y, &I);
    return BinaryOperator::CreateFAddFMF(Op0, FDiv, &I);
  }

  // Handle special cases for FSub with selects feeding the operation
  if (Value *V = SimplifySelectsFeedingBinaryOp(I, Op0, Op1))
    return replaceInstUsesWith(I, V);

  if (I.hasAllowReassoc() && I.hasNoSignedZeros()) {
    // (Y - X) - Y --> -X
    if (match(Op0, m_FSub(m_Specific(Op1), m_Value(X))))
      return UnaryOperator::CreateFNegFMF(X, &I);

    // Y - (X + Y) --> -X
    // Y - (Y + X) --> -X
    if (match(Op1, m_c_FAdd(m_Specific(Op0), m_Value(X))))
      return UnaryOperator::CreateFNegFMF(X, &I);

    // (X * C) - X --> X * (C - 1.0)
    if (match(Op0, m_FMul(m_Specific(Op1), m_Constant(C)))) {
      if (Constant *CSubOne = ConstantFoldBinaryOpOperands(
              Instruction::FSub, C, ConstantFP::get(Ty, 1.0), DL))
        return BinaryOperator::CreateFMulFMF(Op1, CSubOne, &I);
    }
    // X - (X * C) --> X * (1.0 - C)
    if (match(Op1, m_FMul(m_Specific(Op0), m_Constant(C)))) {
      if (Constant *OneSubC = ConstantFoldBinaryOpOperands(
              Instruction::FSub, ConstantFP::get(Ty, 1.0), C, DL))
        return BinaryOperator::CreateFMulFMF(Op0, OneSubC, &I);
    }

    // Reassociate fsub/fadd sequences to create more fadd instructions and
    // reduce dependency chains:
    // ((X - Y) + Z) - Op1 --> (X + Z) - (Y + Op1)
    Value *Z;
    if (match(Op0, m_OneUse(m_c_FAdd(m_OneUse(m_FSub(m_Value(X), m_Value(Y))),
                                     m_Value(Z))))) {
      Value *XZ = Builder.CreateFAddFMF(X, Z, &I);
      Value *YW = Builder.CreateFAddFMF(Y, Op1, &I);
      return BinaryOperator::CreateFSubFMF(XZ, YW, &I);
    }

    auto m_FaddRdx = [](Value *&Sum, Value *&Vec) {
      return m_OneUse(m_Intrinsic<Intrinsic::vector_reduce_fadd>(m_Value(Sum),
                                                                 m_Value(Vec)));
    };
    Value *A0, *A1, *V0, *V1;
    if (match(Op0, m_FaddRdx(A0, V0)) && match(Op1, m_FaddRdx(A1, V1)) &&
        V0->getType() == V1->getType()) {
      // Difference of sums is sum of differences:
      // add_rdx(A0, V0) - add_rdx(A1, V1) --> add_rdx(A0, V0 - V1) - A1
      Value *Sub = Builder.CreateFSubFMF(V0, V1, &I);
      Value *Rdx = Builder.CreateIntrinsic(Intrinsic::vector_reduce_fadd,
                                           {Sub->getType()}, {A0, Sub}, &I);
      return BinaryOperator::CreateFSubFMF(Rdx, A1, &I);
    }

    if (Instruction *F = factorizeFAddFSub(I, Builder))
      return F;

    // TODO: This performs reassociative folds for FP ops. Some fraction of the
    // functionality has been subsumed by simple pattern matching here and in
    // InstSimplify. We should let a dedicated reassociation pass handle more
    // complex pattern matching and remove this from InstCombine.
    if (Value *V = FAddCombine(Builder).simplify(&I))
      return replaceInstUsesWith(I, V);

    // (X - Y) - Op1 --> X - (Y + Op1)
    if (match(Op0, m_OneUse(m_FSub(m_Value(X), m_Value(Y))))) {
      Value *FAdd = Builder.CreateFAddFMF(Y, Op1, &I);
      return BinaryOperator::CreateFSubFMF(X, FAdd, &I);
    }
  }

  return nullptr;
}
