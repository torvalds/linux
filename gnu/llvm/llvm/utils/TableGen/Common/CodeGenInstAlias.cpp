//===- CodeGenInstAlias.cpp - CodeGen InstAlias Class Wrapper -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the CodeGenInstAlias class.
//
//===----------------------------------------------------------------------===//

#include "CodeGenInstAlias.h"
#include "CodeGenInstruction.h"
#include "CodeGenRegisters.h"
#include "CodeGenTarget.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"

using namespace llvm;

/// tryAliasOpMatch - This is a helper function for the CodeGenInstAlias
/// constructor.  It checks if an argument in an InstAlias pattern matches
/// the corresponding operand of the instruction.  It returns true on a
/// successful match, with ResOp set to the result operand to be used.
bool CodeGenInstAlias::tryAliasOpMatch(DagInit *Result, unsigned AliasOpNo,
                                       Record *InstOpRec, bool hasSubOps,
                                       ArrayRef<SMLoc> Loc, CodeGenTarget &T,
                                       ResultOperand &ResOp) {
  Init *Arg = Result->getArg(AliasOpNo);
  DefInit *ADI = dyn_cast<DefInit>(Arg);
  Record *ResultRecord = ADI ? ADI->getDef() : nullptr;

  if (ADI && ADI->getDef() == InstOpRec) {
    // If the operand is a record, it must have a name, and the record type
    // must match up with the instruction's argument type.
    if (!Result->getArgName(AliasOpNo))
      PrintFatalError(Loc, "result argument #" + Twine(AliasOpNo) +
                               " must have a name!");
    ResOp = ResultOperand(std::string(Result->getArgNameStr(AliasOpNo)),
                          ResultRecord);
    return true;
  }

  // For register operands, the source register class can be a subclass
  // of the instruction register class, not just an exact match.
  if (InstOpRec->isSubClassOf("RegisterOperand"))
    InstOpRec = InstOpRec->getValueAsDef("RegClass");

  if (ADI && ADI->getDef()->isSubClassOf("RegisterOperand"))
    ADI = ADI->getDef()->getValueAsDef("RegClass")->getDefInit();

  if (ADI && ADI->getDef()->isSubClassOf("RegisterClass")) {
    if (!InstOpRec->isSubClassOf("RegisterClass"))
      return false;
    if (!T.getRegisterClass(InstOpRec).hasSubClass(
            &T.getRegisterClass(ADI->getDef())))
      return false;
    ResOp = ResultOperand(std::string(Result->getArgNameStr(AliasOpNo)),
                          ResultRecord);
    return true;
  }

  // Handle explicit registers.
  if (ADI && ADI->getDef()->isSubClassOf("Register")) {
    if (InstOpRec->isSubClassOf("OptionalDefOperand")) {
      DagInit *DI = InstOpRec->getValueAsDag("MIOperandInfo");
      // The operand info should only have a single (register) entry. We
      // want the register class of it.
      InstOpRec = cast<DefInit>(DI->getArg(0))->getDef();
    }

    if (!InstOpRec->isSubClassOf("RegisterClass"))
      return false;

    if (!T.getRegisterClass(InstOpRec).contains(
            T.getRegBank().getReg(ADI->getDef())))
      PrintFatalError(Loc, "fixed register " + ADI->getDef()->getName() +
                               " is not a member of the " +
                               InstOpRec->getName() + " register class!");

    if (Result->getArgName(AliasOpNo))
      PrintFatalError(Loc, "result fixed register argument must "
                           "not have a name!");

    ResOp = ResultOperand(ResultRecord);
    return true;
  }

  // Handle "zero_reg" for optional def operands.
  if (ADI && ADI->getDef()->getName() == "zero_reg") {

    // Check if this is an optional def.
    // Tied operands where the source is a sub-operand of a complex operand
    // need to represent both operands in the alias destination instruction.
    // Allow zero_reg for the tied portion. This can and should go away once
    // the MC representation of things doesn't use tied operands at all.
    // if (!InstOpRec->isSubClassOf("OptionalDefOperand"))
    //  throw TGError(Loc, "reg0 used for result that is not an "
    //                "OptionalDefOperand!");

    ResOp = ResultOperand(static_cast<Record *>(nullptr));
    return true;
  }

  // Literal integers.
  if (IntInit *II = dyn_cast<IntInit>(Arg)) {
    if (hasSubOps || !InstOpRec->isSubClassOf("Operand"))
      return false;
    // Integer arguments can't have names.
    if (Result->getArgName(AliasOpNo))
      PrintFatalError(Loc, "result argument #" + Twine(AliasOpNo) +
                               " must not have a name!");
    ResOp = ResultOperand(II->getValue());
    return true;
  }

  // Bits<n> (also used for 0bxx literals)
  if (BitsInit *BI = dyn_cast<BitsInit>(Arg)) {
    if (hasSubOps || !InstOpRec->isSubClassOf("Operand"))
      return false;
    if (!BI->isComplete())
      return false;
    // Convert the bits init to an integer and use that for the result.
    IntInit *II = dyn_cast_or_null<IntInit>(
        BI->convertInitializerTo(IntRecTy::get(BI->getRecordKeeper())));
    if (!II)
      return false;
    ResOp = ResultOperand(II->getValue());
    return true;
  }

  // If both are Operands with the same MVT, allow the conversion. It's
  // up to the user to make sure the values are appropriate, just like
  // for isel Pat's.
  if (InstOpRec->isSubClassOf("Operand") && ADI &&
      ADI->getDef()->isSubClassOf("Operand")) {
    // FIXME: What other attributes should we check here? Identical
    // MIOperandInfo perhaps?
    if (InstOpRec->getValueInit("Type") != ADI->getDef()->getValueInit("Type"))
      return false;
    ResOp = ResultOperand(std::string(Result->getArgNameStr(AliasOpNo)),
                          ADI->getDef());
    return true;
  }

  return false;
}

unsigned CodeGenInstAlias::ResultOperand::getMINumOperands() const {
  if (!isRecord())
    return 1;

  Record *Rec = getRecord();
  if (!Rec->isSubClassOf("Operand"))
    return 1;

  DagInit *MIOpInfo = Rec->getValueAsDag("MIOperandInfo");
  if (MIOpInfo->getNumArgs() == 0) {
    // Unspecified, so it defaults to 1
    return 1;
  }

  return MIOpInfo->getNumArgs();
}

CodeGenInstAlias::CodeGenInstAlias(Record *R, CodeGenTarget &T) : TheDef(R) {
  Result = R->getValueAsDag("ResultInst");
  AsmString = std::string(R->getValueAsString("AsmString"));

  // Verify that the root of the result is an instruction.
  DefInit *DI = dyn_cast<DefInit>(Result->getOperator());
  if (!DI || !DI->getDef()->isSubClassOf("Instruction"))
    PrintFatalError(R->getLoc(),
                    "result of inst alias should be an instruction");

  ResultInst = &T.getInstruction(DI->getDef());

  // NameClass - If argument names are repeated, we need to verify they have
  // the same class.
  StringMap<Record *> NameClass;
  for (unsigned i = 0, e = Result->getNumArgs(); i != e; ++i) {
    DefInit *ADI = dyn_cast<DefInit>(Result->getArg(i));
    if (!ADI || !Result->getArgName(i))
      continue;
    // Verify we don't have something like: (someinst GR16:$foo, GR32:$foo)
    // $foo can exist multiple times in the result list, but it must have the
    // same type.
    Record *&Entry = NameClass[Result->getArgNameStr(i)];
    if (Entry && Entry != ADI->getDef())
      PrintFatalError(R->getLoc(), "result value $" + Result->getArgNameStr(i) +
                                       " is both " + Entry->getName() +
                                       " and " + ADI->getDef()->getName() +
                                       "!");
    Entry = ADI->getDef();
  }

  // Decode and validate the arguments of the result.
  unsigned AliasOpNo = 0;
  for (unsigned i = 0, e = ResultInst->Operands.size(); i != e; ++i) {

    // Tied registers don't have an entry in the result dag unless they're part
    // of a complex operand, in which case we include them anyways, as we
    // don't have any other way to specify the whole operand.
    if (ResultInst->Operands[i].MINumOperands == 1 &&
        ResultInst->Operands[i].getTiedRegister() != -1) {
      // Tied operands of different RegisterClass should be explicit within an
      // instruction's syntax and so cannot be skipped.
      int TiedOpNum = ResultInst->Operands[i].getTiedRegister();
      if (ResultInst->Operands[i].Rec->getName() ==
          ResultInst->Operands[TiedOpNum].Rec->getName())
        continue;
    }

    if (AliasOpNo >= Result->getNumArgs())
      PrintFatalError(R->getLoc(), "not enough arguments for instruction!");

    Record *InstOpRec = ResultInst->Operands[i].Rec;
    unsigned NumSubOps = ResultInst->Operands[i].MINumOperands;
    ResultOperand ResOp(static_cast<int64_t>(0));
    if (tryAliasOpMatch(Result, AliasOpNo, InstOpRec, (NumSubOps > 1),
                        R->getLoc(), T, ResOp)) {
      // If this is a simple operand, or a complex operand with a custom match
      // class, then we can match is verbatim.
      if (NumSubOps == 1 || (InstOpRec->getValue("ParserMatchClass") &&
                             InstOpRec->getValueAsDef("ParserMatchClass")
                                     ->getValueAsString("Name") != "Imm")) {
        ResultOperands.push_back(ResOp);
        ResultInstOperandIndex.push_back(std::pair(i, -1));
        ++AliasOpNo;

        // Otherwise, we need to match each of the suboperands individually.
      } else {
        DagInit *MIOI = ResultInst->Operands[i].MIOperandInfo;
        for (unsigned SubOp = 0; SubOp != NumSubOps; ++SubOp) {
          Record *SubRec = cast<DefInit>(MIOI->getArg(SubOp))->getDef();

          // Take care to instantiate each of the suboperands with the correct
          // nomenclature: $foo.bar
          ResultOperands.emplace_back(
              Result->getArgName(AliasOpNo)->getAsUnquotedString() + "." +
                  MIOI->getArgName(SubOp)->getAsUnquotedString(),
              SubRec);
          ResultInstOperandIndex.push_back(std::pair(i, SubOp));
        }
        ++AliasOpNo;
      }
      continue;
    }

    // If the argument did not match the instruction operand, and the operand
    // is composed of multiple suboperands, try matching the suboperands.
    if (NumSubOps > 1) {
      DagInit *MIOI = ResultInst->Operands[i].MIOperandInfo;
      for (unsigned SubOp = 0; SubOp != NumSubOps; ++SubOp) {
        if (AliasOpNo >= Result->getNumArgs())
          PrintFatalError(R->getLoc(), "not enough arguments for instruction!");
        Record *SubRec = cast<DefInit>(MIOI->getArg(SubOp))->getDef();
        if (tryAliasOpMatch(Result, AliasOpNo, SubRec, false, R->getLoc(), T,
                            ResOp)) {
          ResultOperands.push_back(ResOp);
          ResultInstOperandIndex.push_back(std::pair(i, SubOp));
          ++AliasOpNo;
        } else {
          PrintFatalError(
              R->getLoc(),
              "result argument #" + Twine(AliasOpNo) +
                  " does not match instruction operand class " +
                  (SubOp == 0 ? InstOpRec->getName() : SubRec->getName()));
        }
      }
      continue;
    }
    PrintFatalError(R->getLoc(),
                    "result argument #" + Twine(AliasOpNo) +
                        " does not match instruction operand class " +
                        InstOpRec->getName());
  }

  if (AliasOpNo != Result->getNumArgs())
    PrintFatalError(R->getLoc(), "too many operands for instruction!");
}
