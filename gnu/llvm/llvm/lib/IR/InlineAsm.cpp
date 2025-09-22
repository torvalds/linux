//===- InlineAsm.cpp - Implement the InlineAsm class ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the InlineAsm class.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/InlineAsm.h"
#include "ConstantsContext.h"
#include "LLVMContextImpl.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Errc.h"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdlib>

using namespace llvm;

InlineAsm::InlineAsm(FunctionType *FTy, const std::string &asmString,
                     const std::string &constraints, bool hasSideEffects,
                     bool isAlignStack, AsmDialect asmDialect, bool canThrow)
    : Value(PointerType::getUnqual(FTy), Value::InlineAsmVal),
      AsmString(asmString), Constraints(constraints), FTy(FTy),
      HasSideEffects(hasSideEffects), IsAlignStack(isAlignStack),
      Dialect(asmDialect), CanThrow(canThrow) {
#ifndef NDEBUG
  // Do various checks on the constraint string and type.
  cantFail(verify(getFunctionType(), constraints));
#endif
}

InlineAsm *InlineAsm::get(FunctionType *FTy, StringRef AsmString,
                          StringRef Constraints, bool hasSideEffects,
                          bool isAlignStack, AsmDialect asmDialect,
                          bool canThrow) {
  InlineAsmKeyType Key(AsmString, Constraints, FTy, hasSideEffects,
                       isAlignStack, asmDialect, canThrow);
  LLVMContextImpl *pImpl = FTy->getContext().pImpl;
  return pImpl->InlineAsms.getOrCreate(PointerType::getUnqual(FTy), Key);
}

void InlineAsm::destroyConstant() {
  getType()->getContext().pImpl->InlineAsms.remove(this);
  delete this;
}

FunctionType *InlineAsm::getFunctionType() const {
  return FTy;
}

void InlineAsm::collectAsmStrs(SmallVectorImpl<StringRef> &AsmStrs) const {
  StringRef AsmStr(AsmString);
  AsmStrs.clear();

  // TODO: 1) Unify delimiter for inline asm, we also meet other delimiters
  // for example "\0A", ";".
  // 2) Enhance StringRef. Some of the special delimiter ("\0") can't be
  // split in StringRef. Also empty StringRef can not call split (will stuck).
  if (AsmStr.empty())
    return;
  AsmStr.split(AsmStrs, "\n\t", -1, false);
}

/// Parse - Analyze the specified string (e.g. "==&{eax}") and fill in the
/// fields in this structure.  If the constraint string is not understood,
/// return true, otherwise return false.
bool InlineAsm::ConstraintInfo::Parse(StringRef Str,
                     InlineAsm::ConstraintInfoVector &ConstraintsSoFar) {
  StringRef::iterator I = Str.begin(), E = Str.end();
  unsigned multipleAlternativeCount = Str.count('|') + 1;
  unsigned multipleAlternativeIndex = 0;
  ConstraintCodeVector *pCodes = &Codes;

  // Initialize
  isMultipleAlternative = multipleAlternativeCount > 1;
  if (isMultipleAlternative) {
    multipleAlternatives.resize(multipleAlternativeCount);
    pCodes = &multipleAlternatives[0].Codes;
  }
  Type = isInput;
  isEarlyClobber = false;
  MatchingInput = -1;
  isCommutative = false;
  isIndirect = false;
  currentAlternativeIndex = 0;

  // Parse prefixes.
  if (*I == '~') {
    Type = isClobber;
    ++I;

    // '{' must immediately follow '~'.
    if (I != E && *I != '{')
      return true;
  } else if (*I == '=') {
    ++I;
    Type = isOutput;
  } else if (*I == '!') {
    ++I;
    Type = isLabel;
  }

  if (*I == '*') {
    isIndirect = true;
    ++I;
  }

  if (I == E) return true;  // Just a prefix, like "==" or "~".

  // Parse the modifiers.
  bool DoneWithModifiers = false;
  while (!DoneWithModifiers) {
    switch (*I) {
    default:
      DoneWithModifiers = true;
      break;
    case '&':     // Early clobber.
      if (Type != isOutput ||      // Cannot early clobber anything but output.
          isEarlyClobber)          // Reject &&&&&&
        return true;
      isEarlyClobber = true;
      break;
    case '%':     // Commutative.
      if (Type == isClobber ||     // Cannot commute clobbers.
          isCommutative)           // Reject %%%%%
        return true;
      isCommutative = true;
      break;
    case '#':     // Comment.
    case '*':     // Register preferencing.
      return true;     // Not supported.
    }

    if (!DoneWithModifiers) {
      ++I;
      if (I == E) return true;   // Just prefixes and modifiers!
    }
  }

  // Parse the various constraints.
  while (I != E) {
    if (*I == '{') {   // Physical register reference.
      // Find the end of the register name.
      StringRef::iterator ConstraintEnd = std::find(I+1, E, '}');
      if (ConstraintEnd == E) return true;  // "{foo"
      pCodes->push_back(std::string(StringRef(I, ConstraintEnd + 1 - I)));
      I = ConstraintEnd+1;
    } else if (isdigit(static_cast<unsigned char>(*I))) { // Matching Constraint
      // Maximal munch numbers.
      StringRef::iterator NumStart = I;
      while (I != E && isdigit(static_cast<unsigned char>(*I)))
        ++I;
      pCodes->push_back(std::string(StringRef(NumStart, I - NumStart)));
      unsigned N = atoi(pCodes->back().c_str());
      // Check that this is a valid matching constraint!
      if (N >= ConstraintsSoFar.size() || ConstraintsSoFar[N].Type != isOutput||
          Type != isInput)
        return true;  // Invalid constraint number.

      // If Operand N already has a matching input, reject this.  An output
      // can't be constrained to the same value as multiple inputs.
      if (isMultipleAlternative) {
        if (multipleAlternativeIndex >=
            ConstraintsSoFar[N].multipleAlternatives.size())
          return true;
        InlineAsm::SubConstraintInfo &scInfo =
          ConstraintsSoFar[N].multipleAlternatives[multipleAlternativeIndex];
        if (scInfo.MatchingInput != -1)
          return true;
        // Note that operand #n has a matching input.
        scInfo.MatchingInput = ConstraintsSoFar.size();
        assert(scInfo.MatchingInput >= 0);
      } else {
        if (ConstraintsSoFar[N].hasMatchingInput() &&
            (size_t)ConstraintsSoFar[N].MatchingInput !=
                ConstraintsSoFar.size())
          return true;
        // Note that operand #n has a matching input.
        ConstraintsSoFar[N].MatchingInput = ConstraintsSoFar.size();
        assert(ConstraintsSoFar[N].MatchingInput >= 0);
        }
    } else if (*I == '|') {
      multipleAlternativeIndex++;
      pCodes = &multipleAlternatives[multipleAlternativeIndex].Codes;
      ++I;
    } else if (*I == '^') {
      // Multi-letter constraint
      // FIXME: For now assuming these are 2-character constraints.
      pCodes->push_back(std::string(StringRef(I + 1, 2)));
      I += 3;
    } else if (*I == '@') {
      // Multi-letter constraint
      ++I;
      unsigned char C = static_cast<unsigned char>(*I);
      assert(isdigit(C) && "Expected a digit!");
      int N = C - '0';
      assert(N > 0 && "Found a zero letter constraint!");
      ++I;
      pCodes->push_back(std::string(StringRef(I, N)));
      I += N;
    } else {
      // Single letter constraint.
      pCodes->push_back(std::string(StringRef(I, 1)));
      ++I;
    }
  }

  return false;
}

/// selectAlternative - Point this constraint to the alternative constraint
/// indicated by the index.
void InlineAsm::ConstraintInfo::selectAlternative(unsigned index) {
  if (index < multipleAlternatives.size()) {
    currentAlternativeIndex = index;
    InlineAsm::SubConstraintInfo &scInfo =
      multipleAlternatives[currentAlternativeIndex];
    MatchingInput = scInfo.MatchingInput;
    Codes = scInfo.Codes;
  }
}

InlineAsm::ConstraintInfoVector
InlineAsm::ParseConstraints(StringRef Constraints) {
  ConstraintInfoVector Result;

  // Scan the constraints string.
  for (StringRef::iterator I = Constraints.begin(),
         E = Constraints.end(); I != E; ) {
    ConstraintInfo Info;

    // Find the end of this constraint.
    StringRef::iterator ConstraintEnd = std::find(I, E, ',');

    if (ConstraintEnd == I ||  // Empty constraint like ",,"
        Info.Parse(StringRef(I, ConstraintEnd-I), Result)) {
      Result.clear();          // Erroneous constraint?
      break;
    }

    Result.push_back(Info);

    // ConstraintEnd may be either the next comma or the end of the string.  In
    // the former case, we skip the comma.
    I = ConstraintEnd;
    if (I != E) {
      ++I;
      if (I == E) {
        Result.clear();
        break;
      } // don't allow "xyz,"
    }
  }

  return Result;
}

static Error makeStringError(const char *Msg) {
  return createStringError(errc::invalid_argument, Msg);
}

Error InlineAsm::verify(FunctionType *Ty, StringRef ConstStr) {
  if (Ty->isVarArg())
    return makeStringError("inline asm cannot be variadic");

  ConstraintInfoVector Constraints = ParseConstraints(ConstStr);

  // Error parsing constraints.
  if (Constraints.empty() && !ConstStr.empty())
    return makeStringError("failed to parse constraints");

  unsigned NumOutputs = 0, NumInputs = 0, NumClobbers = 0;
  unsigned NumIndirect = 0, NumLabels = 0;

  for (const ConstraintInfo &Constraint : Constraints) {
    switch (Constraint.Type) {
    case InlineAsm::isOutput:
      if ((NumInputs-NumIndirect) != 0 || NumClobbers != 0 || NumLabels != 0)
        return makeStringError("output constraint occurs after input, "
                               "clobber or label constraint");

      if (!Constraint.isIndirect) {
        ++NumOutputs;
        break;
      }
      ++NumIndirect;
      [[fallthrough]]; // We fall through for Indirect Outputs.
    case InlineAsm::isInput:
      if (NumClobbers)
        return makeStringError("input constraint occurs after clobber "
                               "constraint");
      ++NumInputs;
      break;
    case InlineAsm::isClobber:
      ++NumClobbers;
      break;
    case InlineAsm::isLabel:
      if (NumClobbers)
        return makeStringError("label constraint occurs after clobber "
                               "constraint");

      ++NumLabels;
      break;
    }
  }

  switch (NumOutputs) {
  case 0:
    if (!Ty->getReturnType()->isVoidTy())
      return makeStringError("inline asm without outputs must return void");
    break;
  case 1:
    if (Ty->getReturnType()->isStructTy())
      return makeStringError("inline asm with one output cannot return struct");
    break;
  default:
    StructType *STy = dyn_cast<StructType>(Ty->getReturnType());
    if (!STy || STy->getNumElements() != NumOutputs)
      return makeStringError("number of output constraints does not match "
                             "number of return struct elements");
    break;
  }

  if (Ty->getNumParams() != NumInputs)
    return makeStringError("number of input constraints does not match number "
                           "of parameters");

  // We don't have access to labels here, NumLabels will be checked separately.
  return Error::success();
}
