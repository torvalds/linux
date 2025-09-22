//===- TGParser.cpp - Parser for TableGen Files ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implement the Parser for TableGen.
//
//===----------------------------------------------------------------------===//

#include "TGParser.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>

using namespace llvm;

//===----------------------------------------------------------------------===//
// Support Code for the Semantic Actions.
//===----------------------------------------------------------------------===//

namespace llvm {

struct SubClassReference {
  SMRange RefRange;
  Record *Rec = nullptr;
  SmallVector<ArgumentInit *, 4> TemplateArgs;

  SubClassReference() = default;

  bool isInvalid() const { return Rec == nullptr; }
};

struct SubMultiClassReference {
  SMRange RefRange;
  MultiClass *MC = nullptr;
  SmallVector<ArgumentInit *, 4> TemplateArgs;

  SubMultiClassReference() = default;

  bool isInvalid() const { return MC == nullptr; }
  void dump() const;
};

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void SubMultiClassReference::dump() const {
  errs() << "Multiclass:\n";

  MC->dump();

  errs() << "Template args:\n";
  for (Init *TA : TemplateArgs)
    TA->dump();
}
#endif

} // end namespace llvm

static bool checkBitsConcrete(Record &R, const RecordVal &RV) {
  BitsInit *BV = cast<BitsInit>(RV.getValue());
  for (unsigned i = 0, e = BV->getNumBits(); i != e; ++i) {
    Init *Bit = BV->getBit(i);
    bool IsReference = false;
    if (auto VBI = dyn_cast<VarBitInit>(Bit)) {
      if (auto VI = dyn_cast<VarInit>(VBI->getBitVar())) {
        if (R.getValue(VI->getName()))
          IsReference = true;
      }
    } else if (isa<VarInit>(Bit)) {
      IsReference = true;
    }
    if (!(IsReference || Bit->isConcrete()))
      return false;
  }
  return true;
}

static void checkConcrete(Record &R) {
  for (const RecordVal &RV : R.getValues()) {
    // HACK: Disable this check for variables declared with 'field'. This is
    // done merely because existing targets have legitimate cases of
    // non-concrete variables in helper defs. Ideally, we'd introduce a
    // 'maybe' or 'optional' modifier instead of this.
    if (RV.isNonconcreteOK())
      continue;

    if (Init *V = RV.getValue()) {
      bool Ok = isa<BitsInit>(V) ? checkBitsConcrete(R, RV) : V->isConcrete();
      if (!Ok) {
        PrintError(R.getLoc(),
                   Twine("Initializer of '") + RV.getNameInitAsString() +
                   "' in '" + R.getNameInitAsString() +
                   "' could not be fully resolved: " +
                   RV.getValue()->getAsString());
      }
    }
  }
}

/// Return an Init with a qualifier prefix referring
/// to CurRec's name.
static Init *QualifyName(Record &CurRec, Init *Name) {
  RecordKeeper &RK = CurRec.getRecords();
  Init *NewName = BinOpInit::getStrConcat(
      CurRec.getNameInit(),
      StringInit::get(RK, CurRec.isMultiClass() ? "::" : ":"));
  NewName = BinOpInit::getStrConcat(NewName, Name);

  if (BinOpInit *BinOp = dyn_cast<BinOpInit>(NewName))
    NewName = BinOp->Fold(&CurRec);
  return NewName;
}

static Init *QualifyName(MultiClass *MC, Init *Name) {
  return QualifyName(MC->Rec, Name);
}

/// Return the qualified version of the implicit 'NAME' template argument.
static Init *QualifiedNameOfImplicitName(Record &Rec) {
  return QualifyName(Rec, StringInit::get(Rec.getRecords(), "NAME"));
}

static Init *QualifiedNameOfImplicitName(MultiClass *MC) {
  return QualifiedNameOfImplicitName(MC->Rec);
}

Init *TGVarScope::getVar(RecordKeeper &Records, MultiClass *ParsingMultiClass,
                         StringInit *Name, SMRange NameLoc,
                         bool TrackReferenceLocs) const {
  // First, we search in local variables.
  auto It = Vars.find(Name->getValue());
  if (It != Vars.end())
    return It->second;

  auto FindValueInArgs = [&](Record *Rec, StringInit *Name) -> Init * {
    if (!Rec)
      return nullptr;
    Init *ArgName = QualifyName(*Rec, Name);
    if (Rec->isTemplateArg(ArgName)) {
      RecordVal *RV = Rec->getValue(ArgName);
      assert(RV && "Template arg doesn't exist??");
      RV->setUsed(true);
      if (TrackReferenceLocs)
        RV->addReferenceLoc(NameLoc);
      return VarInit::get(ArgName, RV->getType());
    }
    return Name->getValue() == "NAME"
               ? VarInit::get(ArgName, StringRecTy::get(Records))
               : nullptr;
  };

  // If not found, we try to find the variable in additional variables like
  // arguments, loop iterator, etc.
  switch (Kind) {
  case SK_Local:
    break; /* do nothing. */
  case SK_Record: {
    if (CurRec) {
      // The variable is a record field?
      if (RecordVal *RV = CurRec->getValue(Name)) {
        if (TrackReferenceLocs)
          RV->addReferenceLoc(NameLoc);
        return VarInit::get(Name, RV->getType());
      }

      // The variable is a class template argument?
      if (CurRec->isClass())
        if (auto *V = FindValueInArgs(CurRec, Name))
          return V;
    }
    break;
  }
  case SK_ForeachLoop: {
    // The variable is a loop iterator?
    if (CurLoop->IterVar) {
      VarInit *IterVar = dyn_cast<VarInit>(CurLoop->IterVar);
      if (IterVar && IterVar->getNameInit() == Name)
        return IterVar;
    }
    break;
  }
  case SK_MultiClass: {
    // The variable is a multiclass template argument?
    if (CurMultiClass)
      if (auto *V = FindValueInArgs(&CurMultiClass->Rec, Name))
        return V;
    break;
  }
  }

  // Then, we try to find the name in parent scope.
  if (Parent)
    return Parent->getVar(Records, ParsingMultiClass, Name, NameLoc,
                          TrackReferenceLocs);

  return nullptr;
}

bool TGParser::AddValue(Record *CurRec, SMLoc Loc, const RecordVal &RV) {
  if (!CurRec)
    CurRec = &CurMultiClass->Rec;

  if (RecordVal *ERV = CurRec->getValue(RV.getNameInit())) {
    // The value already exists in the class, treat this as a set.
    if (ERV->setValue(RV.getValue()))
      return Error(Loc, "New definition of '" + RV.getName() + "' of type '" +
                   RV.getType()->getAsString() + "' is incompatible with " +
                   "previous definition of type '" +
                   ERV->getType()->getAsString() + "'");
  } else {
    CurRec->addValue(RV);
  }
  return false;
}

/// SetValue -
/// Return true on error, false on success.
bool TGParser::SetValue(Record *CurRec, SMLoc Loc, Init *ValName,
                        ArrayRef<unsigned> BitList, Init *V,
                        bool AllowSelfAssignment, bool OverrideDefLoc) {
  if (!V) return false;

  if (!CurRec) CurRec = &CurMultiClass->Rec;

  RecordVal *RV = CurRec->getValue(ValName);
  if (!RV)
    return Error(Loc, "Value '" + ValName->getAsUnquotedString() +
                 "' unknown!");

  // Do not allow assignments like 'X = X'.  This will just cause infinite loops
  // in the resolution machinery.
  if (BitList.empty())
    if (VarInit *VI = dyn_cast<VarInit>(V))
      if (VI->getNameInit() == ValName && !AllowSelfAssignment)
        return Error(Loc, "Recursion / self-assignment forbidden");

  // If we are assigning to a subset of the bits in the value... then we must be
  // assigning to a field of BitsRecTy, which must have a BitsInit
  // initializer.
  //
  if (!BitList.empty()) {
    BitsInit *CurVal = dyn_cast<BitsInit>(RV->getValue());
    if (!CurVal)
      return Error(Loc, "Value '" + ValName->getAsUnquotedString() +
                   "' is not a bits type");

    // Convert the incoming value to a bits type of the appropriate size...
    Init *BI = V->getCastTo(BitsRecTy::get(Records, BitList.size()));
    if (!BI)
      return Error(Loc, "Initializer is not compatible with bit range");

    SmallVector<Init *, 16> NewBits(CurVal->getNumBits());

    // Loop over bits, assigning values as appropriate.
    for (unsigned i = 0, e = BitList.size(); i != e; ++i) {
      unsigned Bit = BitList[i];
      if (NewBits[Bit])
        return Error(Loc, "Cannot set bit #" + Twine(Bit) + " of value '" +
                     ValName->getAsUnquotedString() + "' more than once");
      NewBits[Bit] = BI->getBit(i);
    }

    for (unsigned i = 0, e = CurVal->getNumBits(); i != e; ++i)
      if (!NewBits[i])
        NewBits[i] = CurVal->getBit(i);

    V = BitsInit::get(Records, NewBits);
  }

  if (OverrideDefLoc ? RV->setValue(V, Loc) : RV->setValue(V)) {
    std::string InitType;
    if (BitsInit *BI = dyn_cast<BitsInit>(V))
      InitType = (Twine("' of type bit initializer with length ") +
                  Twine(BI->getNumBits())).str();
    else if (TypedInit *TI = dyn_cast<TypedInit>(V))
      InitType = (Twine("' of type '") + TI->getType()->getAsString()).str();
    return Error(Loc, "Field '" + ValName->getAsUnquotedString() +
                          "' of type '" + RV->getType()->getAsString() +
                          "' is incompatible with value '" +
                          V->getAsString() + InitType + "'");
  }
  return false;
}

/// AddSubClass - Add SubClass as a subclass to CurRec, resolving its template
/// args as SubClass's template arguments.
bool TGParser::AddSubClass(Record *CurRec, SubClassReference &SubClass) {
  Record *SC = SubClass.Rec;
  MapResolver R(CurRec);

  // Loop over all the subclass record's fields. Add regular fields to the new
  // record.
  for (const RecordVal &Field : SC->getValues())
    if (!Field.isTemplateArg())
      if (AddValue(CurRec, SubClass.RefRange.Start, Field))
        return true;

  if (resolveArgumentsOfClass(R, SC, SubClass.TemplateArgs,
                              SubClass.RefRange.Start))
    return true;

  // Copy the subclass record's assertions to the new record.
  CurRec->appendAssertions(SC);

  // Copy the subclass record's dumps to the new record.
  CurRec->appendDumps(SC);

  Init *Name;
  if (CurRec->isClass())
    Name = VarInit::get(QualifiedNameOfImplicitName(*CurRec),
                        StringRecTy::get(Records));
  else
    Name = CurRec->getNameInit();
  R.set(QualifiedNameOfImplicitName(*SC), Name);

  CurRec->resolveReferences(R);

  // Since everything went well, we can now set the "superclass" list for the
  // current record.
  ArrayRef<std::pair<Record *, SMRange>> SCs = SC->getSuperClasses();
  for (const auto &SCPair : SCs) {
    if (CurRec->isSubClassOf(SCPair.first))
      return Error(SubClass.RefRange.Start,
                   "Already subclass of '" + SCPair.first->getName() + "'!\n");
    CurRec->addSuperClass(SCPair.first, SCPair.second);
  }

  if (CurRec->isSubClassOf(SC))
    return Error(SubClass.RefRange.Start,
                 "Already subclass of '" + SC->getName() + "'!\n");
  CurRec->addSuperClass(SC, SubClass.RefRange);
  return false;
}

bool TGParser::AddSubClass(RecordsEntry &Entry, SubClassReference &SubClass) {
  if (Entry.Rec)
    return AddSubClass(Entry.Rec.get(), SubClass);

  if (Entry.Assertion)
    return false;

  for (auto &E : Entry.Loop->Entries) {
    if (AddSubClass(E, SubClass))
      return true;
  }

  return false;
}

/// AddSubMultiClass - Add SubMultiClass as a subclass to
/// CurMC, resolving its template args as SubMultiClass's
/// template arguments.
bool TGParser::AddSubMultiClass(MultiClass *CurMC,
                                SubMultiClassReference &SubMultiClass) {
  MultiClass *SMC = SubMultiClass.MC;

  SubstStack Substs;
  if (resolveArgumentsOfMultiClass(
          Substs, SMC, SubMultiClass.TemplateArgs,
          VarInit::get(QualifiedNameOfImplicitName(CurMC),
                       StringRecTy::get(Records)),
          SubMultiClass.RefRange.Start))
    return true;

  // Add all of the defs in the subclass into the current multiclass.
  return resolve(SMC->Entries, Substs, false, &CurMC->Entries);
}

/// Add a record, foreach loop, or assertion to the current context.
bool TGParser::addEntry(RecordsEntry E) {
  assert((!!E.Rec + !!E.Loop + !!E.Assertion + !!E.Dump) == 1 &&
         "RecordsEntry has invalid number of items");

  // If we are parsing a loop, add it to the loop's entries.
  if (!Loops.empty()) {
    Loops.back()->Entries.push_back(std::move(E));
    return false;
  }

  // If it is a loop, then resolve and perform the loop.
  if (E.Loop) {
    SubstStack Stack;
    return resolve(*E.Loop, Stack, CurMultiClass == nullptr,
                   CurMultiClass ? &CurMultiClass->Entries : nullptr);
  }

  // If we are parsing a multiclass, add it to the multiclass's entries.
  if (CurMultiClass) {
    CurMultiClass->Entries.push_back(std::move(E));
    return false;
  }

  // If it is an assertion, then it's a top-level one, so check it.
  if (E.Assertion) {
    CheckAssert(E.Assertion->Loc, E.Assertion->Condition, E.Assertion->Message);
    return false;
  }

  if (E.Dump) {
    dumpMessage(E.Dump->Loc, E.Dump->Message);
    return false;
  }

  // It must be a record, so finish it off.
  return addDefOne(std::move(E.Rec));
}

/// Resolve the entries in \p Loop, going over inner loops recursively
/// and making the given subsitutions of (name, value) pairs.
///
/// The resulting records are stored in \p Dest if non-null. Otherwise, they
/// are added to the global record keeper.
bool TGParser::resolve(const ForeachLoop &Loop, SubstStack &Substs,
                       bool Final, std::vector<RecordsEntry> *Dest,
                       SMLoc *Loc) {

  MapResolver R;
  for (const auto &S : Substs)
    R.set(S.first, S.second);
  Init *List = Loop.ListValue->resolveReferences(R);

  // For if-then-else blocks, we lower to a foreach loop whose list is a
  // ternary selection between lists of different length.  Since we don't
  // have a means to track variable length record lists, we *must* resolve
  // the condition here.  We want to defer final resolution of the arms
  // until the resulting records are finalized.
  // e.g. !if(!exists<SchedWrite>("__does_not_exist__"), [1], [])
  if (auto *TI = dyn_cast<TernOpInit>(List);
      TI && TI->getOpcode() == TernOpInit::IF && Final) {
    Init *OldLHS = TI->getLHS();
    R.setFinal(true);
    Init *LHS = OldLHS->resolveReferences(R);
    if (LHS == OldLHS) {
      PrintError(Loop.Loc,
                 Twine("unable to resolve if condition '") +
                 LHS->getAsString() + "' at end of containing scope");
      return true;
    }
    Init *MHS = TI->getMHS();
    Init *RHS = TI->getRHS();
    List = TernOpInit::get(TernOpInit::IF, LHS, MHS, RHS, TI->getType())
      ->Fold(nullptr);
  }

  auto LI = dyn_cast<ListInit>(List);
  if (!LI) {
    if (!Final) {
      Dest->emplace_back(std::make_unique<ForeachLoop>(Loop.Loc, Loop.IterVar,
                                                  List));
      return resolve(Loop.Entries, Substs, Final, &Dest->back().Loop->Entries,
                     Loc);
    }

    PrintError(Loop.Loc, Twine("attempting to loop over '") +
                              List->getAsString() + "', expected a list");
    return true;
  }

  bool Error = false;
  for (auto *Elt : *LI) {
    if (Loop.IterVar)
      Substs.emplace_back(Loop.IterVar->getNameInit(), Elt);
    Error = resolve(Loop.Entries, Substs, Final, Dest);
    if (Loop.IterVar)
      Substs.pop_back();
    if (Error)
      break;
  }
  return Error;
}

/// Resolve the entries in \p Source, going over loops recursively and
/// making the given substitutions of (name, value) pairs.
///
/// The resulting records are stored in \p Dest if non-null. Otherwise, they
/// are added to the global record keeper.
bool TGParser::resolve(const std::vector<RecordsEntry> &Source,
                       SubstStack &Substs, bool Final,
                       std::vector<RecordsEntry> *Dest, SMLoc *Loc) {
  bool Error = false;
  for (auto &E : Source) {
    if (E.Loop) {
      Error = resolve(*E.Loop, Substs, Final, Dest);

    } else if (E.Assertion) {
      MapResolver R;
      for (const auto &S : Substs)
        R.set(S.first, S.second);
      Init *Condition = E.Assertion->Condition->resolveReferences(R);
      Init *Message = E.Assertion->Message->resolveReferences(R);

      if (Dest)
        Dest->push_back(std::make_unique<Record::AssertionInfo>(
            E.Assertion->Loc, Condition, Message));
      else
        CheckAssert(E.Assertion->Loc, Condition, Message);

    } else if (E.Dump) {
      MapResolver R;
      for (const auto &S : Substs)
        R.set(S.first, S.second);
      Init *Message = E.Dump->Message->resolveReferences(R);

      if (Dest)
        Dest->push_back(
            std::make_unique<Record::DumpInfo>(E.Dump->Loc, Message));
      else
        dumpMessage(E.Dump->Loc, Message);

    } else {
      auto Rec = std::make_unique<Record>(*E.Rec);
      if (Loc)
        Rec->appendLoc(*Loc);

      MapResolver R(Rec.get());
      for (const auto &S : Substs)
        R.set(S.first, S.second);
      Rec->resolveReferences(R);

      if (Dest)
        Dest->push_back(std::move(Rec));
      else
        Error = addDefOne(std::move(Rec));
    }
    if (Error)
      break;
  }
  return Error;
}

/// Resolve the record fully and add it to the record keeper.
bool TGParser::addDefOne(std::unique_ptr<Record> Rec) {
  Init *NewName = nullptr;
  if (Record *Prev = Records.getDef(Rec->getNameInitAsString())) {
    if (!Rec->isAnonymous()) {
      PrintError(Rec->getLoc(),
                 "def already exists: " + Rec->getNameInitAsString());
      PrintNote(Prev->getLoc(), "location of previous definition");
      return true;
    }
    NewName = Records.getNewAnonymousName();
  }

  Rec->resolveReferences(NewName);
  checkConcrete(*Rec);

  if (!isa<StringInit>(Rec->getNameInit())) {
    PrintError(Rec->getLoc(), Twine("record name '") +
                                  Rec->getNameInit()->getAsString() +
                                  "' could not be fully resolved");
    return true;
  }

  // Check the assertions.
  Rec->checkRecordAssertions();

  // Run the dumps.
  Rec->emitRecordDumps();

  // If ObjectBody has template arguments, it's an error.
  assert(Rec->getTemplateArgs().empty() && "How'd this get template args?");

  for (DefsetRecord *Defset : Defsets) {
    DefInit *I = Rec->getDefInit();
    if (!I->getType()->typeIsA(Defset->EltTy)) {
      PrintError(Rec->getLoc(), Twine("adding record of incompatible type '") +
                                    I->getType()->getAsString() +
                                     "' to defset");
      PrintNote(Defset->Loc, "location of defset declaration");
      return true;
    }
    Defset->Elements.push_back(I);
  }

  Records.addDef(std::move(Rec));
  return false;
}

bool TGParser::resolveArguments(Record *Rec, ArrayRef<ArgumentInit *> ArgValues,
                                SMLoc Loc, ArgValueHandler ArgValueHandler) {
  ArrayRef<Init *> ArgNames = Rec->getTemplateArgs();
  assert(ArgValues.size() <= ArgNames.size() &&
         "Too many template arguments allowed");

  // Loop over the template arguments and handle the (name, value) pair.
  SmallVector<Init *, 2> UnsolvedArgNames(ArgNames);
  for (auto *Arg : ArgValues) {
    Init *ArgName = nullptr;
    Init *ArgValue = Arg->getValue();
    if (Arg->isPositional())
      ArgName = ArgNames[Arg->getIndex()];
    if (Arg->isNamed())
      ArgName = Arg->getName();

    // We can only specify the template argument once.
    if (!is_contained(UnsolvedArgNames, ArgName))
      return Error(Loc, "We can only specify the template argument '" +
                            ArgName->getAsUnquotedString() + "' once");

    ArgValueHandler(ArgName, ArgValue);
    llvm::erase(UnsolvedArgNames, ArgName);
  }

  // For unsolved arguments, if there is no default value, complain.
  for (auto *UnsolvedArgName : UnsolvedArgNames) {
    Init *Default = Rec->getValue(UnsolvedArgName)->getValue();
    if (!Default->isComplete()) {
      std::string Name = UnsolvedArgName->getAsUnquotedString();
      Error(Loc, "value not specified for template argument '" + Name + "'");
      PrintNote(Rec->getFieldLoc(Name),
                "declared in '" + Rec->getNameInitAsString() + "'");
      return true;
    }
    ArgValueHandler(UnsolvedArgName, Default);
  }

  return false;
}

/// Resolve the arguments of class and set them to MapResolver.
/// Returns true if failed.
bool TGParser::resolveArgumentsOfClass(MapResolver &R, Record *Rec,
                                       ArrayRef<ArgumentInit *> ArgValues,
                                       SMLoc Loc) {
  return resolveArguments(Rec, ArgValues, Loc,
                          [&](Init *Name, Init *Value) { R.set(Name, Value); });
}

/// Resolve the arguments of multiclass and store them into SubstStack.
/// Returns true if failed.
bool TGParser::resolveArgumentsOfMultiClass(SubstStack &Substs, MultiClass *MC,
                                            ArrayRef<ArgumentInit *> ArgValues,
                                            Init *DefmName, SMLoc Loc) {
  // Add an implicit argument NAME.
  Substs.emplace_back(QualifiedNameOfImplicitName(MC), DefmName);
  return resolveArguments(
      &MC->Rec, ArgValues, Loc,
      [&](Init *Name, Init *Value) { Substs.emplace_back(Name, Value); });
}

//===----------------------------------------------------------------------===//
// Parser Code
//===----------------------------------------------------------------------===//

bool TGParser::consume(tgtok::TokKind K) {
  if (Lex.getCode() == K) {
    Lex.Lex();
    return true;
  }
  return false;
}

/// ParseObjectName - If a valid object name is specified, return it. If no
/// name is specified, return the unset initializer. Return nullptr on parse
/// error.
///   ObjectName ::= Value [ '#' Value ]*
///   ObjectName ::= /*empty*/
///
Init *TGParser::ParseObjectName(MultiClass *CurMultiClass) {
  switch (Lex.getCode()) {
  case tgtok::colon:
  case tgtok::semi:
  case tgtok::l_brace:
    // These are all of the tokens that can begin an object body.
    // Some of these can also begin values but we disallow those cases
    // because they are unlikely to be useful.
    return UnsetInit::get(Records);
  default:
    break;
  }

  Record *CurRec = nullptr;
  if (CurMultiClass)
    CurRec = &CurMultiClass->Rec;

  Init *Name = ParseValue(CurRec, StringRecTy::get(Records), ParseNameMode);
  if (!Name)
    return nullptr;

  if (CurMultiClass) {
    Init *NameStr = QualifiedNameOfImplicitName(CurMultiClass);
    HasReferenceResolver R(NameStr);
    Name->resolveReferences(R);
    if (!R.found())
      Name = BinOpInit::getStrConcat(
          VarInit::get(NameStr, StringRecTy::get(Records)), Name);
  }

  return Name;
}

/// ParseClassID - Parse and resolve a reference to a class name.  This returns
/// null on error.
///
///    ClassID ::= ID
///
Record *TGParser::ParseClassID() {
  if (Lex.getCode() != tgtok::Id) {
    TokError("expected name for ClassID");
    return nullptr;
  }

  Record *Result = Records.getClass(Lex.getCurStrVal());
  if (!Result) {
    std::string Msg("Couldn't find class '" + Lex.getCurStrVal() + "'");
    if (MultiClasses[Lex.getCurStrVal()].get())
      TokError(Msg + ". Use 'defm' if you meant to use multiclass '" +
               Lex.getCurStrVal() + "'");
    else
      TokError(Msg);
  } else if (TrackReferenceLocs) {
    Result->appendReferenceLoc(Lex.getLocRange());
  }

  Lex.Lex();
  return Result;
}

/// ParseMultiClassID - Parse and resolve a reference to a multiclass name.
/// This returns null on error.
///
///    MultiClassID ::= ID
///
MultiClass *TGParser::ParseMultiClassID() {
  if (Lex.getCode() != tgtok::Id) {
    TokError("expected name for MultiClassID");
    return nullptr;
  }

  MultiClass *Result = MultiClasses[Lex.getCurStrVal()].get();
  if (!Result)
    TokError("Couldn't find multiclass '" + Lex.getCurStrVal() + "'");

  Lex.Lex();
  return Result;
}

/// ParseSubClassReference - Parse a reference to a subclass or a
/// multiclass. This returns a SubClassRefTy with a null Record* on error.
///
///  SubClassRef ::= ClassID
///  SubClassRef ::= ClassID '<' ArgValueList '>'
///
SubClassReference TGParser::
ParseSubClassReference(Record *CurRec, bool isDefm) {
  SubClassReference Result;
  Result.RefRange.Start = Lex.getLoc();

  if (isDefm) {
    if (MultiClass *MC = ParseMultiClassID())
      Result.Rec = &MC->Rec;
  } else {
    Result.Rec = ParseClassID();
  }
  if (!Result.Rec) return Result;

  // If there is no template arg list, we're done.
  if (!consume(tgtok::less)) {
    Result.RefRange.End = Lex.getLoc();
    return Result;
  }

  if (ParseTemplateArgValueList(Result.TemplateArgs, CurRec, Result.Rec)) {
    Result.Rec = nullptr; // Error parsing value list.
    return Result;
  }

  if (CheckTemplateArgValues(Result.TemplateArgs, Result.RefRange.Start,
                             Result.Rec)) {
    Result.Rec = nullptr; // Error checking value list.
    return Result;
  }

  Result.RefRange.End = Lex.getLoc();
  return Result;
}

/// ParseSubMultiClassReference - Parse a reference to a subclass or to a
/// templated submulticlass.  This returns a SubMultiClassRefTy with a null
/// Record* on error.
///
///  SubMultiClassRef ::= MultiClassID
///  SubMultiClassRef ::= MultiClassID '<' ArgValueList '>'
///
SubMultiClassReference TGParser::
ParseSubMultiClassReference(MultiClass *CurMC) {
  SubMultiClassReference Result;
  Result.RefRange.Start = Lex.getLoc();

  Result.MC = ParseMultiClassID();
  if (!Result.MC) return Result;

  // If there is no template arg list, we're done.
  if (!consume(tgtok::less)) {
    Result.RefRange.End = Lex.getLoc();
    return Result;
  }

  if (ParseTemplateArgValueList(Result.TemplateArgs, &CurMC->Rec,
                                &Result.MC->Rec)) {
    Result.MC = nullptr; // Error parsing value list.
    return Result;
  }

  Result.RefRange.End = Lex.getLoc();

  return Result;
}

/// ParseSliceElement - Parse subscript or range
///
///  SliceElement  ::= Value<list<int>>
///  SliceElement  ::= Value<int>
///  SliceElement  ::= Value<int> '...' Value<int>
///  SliceElement  ::= Value<int> '-' Value<int> (deprecated)
///  SliceElement  ::= Value<int> INTVAL(Negative; deprecated)
///
/// SliceElement is either IntRecTy, ListRecTy, or nullptr
///
TypedInit *TGParser::ParseSliceElement(Record *CurRec) {
  auto LHSLoc = Lex.getLoc();
  auto *CurVal = ParseValue(CurRec);
  if (!CurVal)
    return nullptr;
  auto *LHS = cast<TypedInit>(CurVal);

  TypedInit *RHS = nullptr;
  switch (Lex.getCode()) {
  case tgtok::dotdotdot:
  case tgtok::minus: { // Deprecated
    Lex.Lex();         // eat
    auto RHSLoc = Lex.getLoc();
    CurVal = ParseValue(CurRec);
    if (!CurVal)
      return nullptr;
    RHS = cast<TypedInit>(CurVal);
    if (!isa<IntRecTy>(RHS->getType())) {
      Error(RHSLoc,
            "expected int...int, got " + Twine(RHS->getType()->getAsString()));
      return nullptr;
    }
    break;
  }
  case tgtok::IntVal: { // Deprecated "-num"
    auto i = -Lex.getCurIntVal();
    if (i < 0) {
      TokError("invalid range, cannot be negative");
      return nullptr;
    }
    RHS = IntInit::get(Records, i);
    Lex.Lex(); // eat IntVal
    break;
  }
  default: // Single value (IntRecTy or ListRecTy)
    return LHS;
  }

  assert(RHS);
  assert(isa<IntRecTy>(RHS->getType()));

  // Closed-interval range <LHS:IntRecTy>...<RHS:IntRecTy>
  if (!isa<IntRecTy>(LHS->getType())) {
    Error(LHSLoc,
          "expected int...int, got " + Twine(LHS->getType()->getAsString()));
    return nullptr;
  }

  return cast<TypedInit>(BinOpInit::get(BinOpInit::RANGEC, LHS, RHS,
                                        IntRecTy::get(Records)->getListTy())
                             ->Fold(CurRec));
}

/// ParseSliceElements - Parse subscripts in square brackets.
///
///  SliceElements ::= ( SliceElement ',' )* SliceElement ','?
///
/// SliceElement is either IntRecTy, ListRecTy, or nullptr
///
/// Returns ListRecTy by defaut.
/// Returns IntRecTy if;
///  - Single=true
///  - SliceElements is Value<int> w/o trailing comma
///
TypedInit *TGParser::ParseSliceElements(Record *CurRec, bool Single) {
  TypedInit *CurVal;
  SmallVector<Init *, 2> Elems;       // int
  SmallVector<TypedInit *, 2> Slices; // list<int>

  auto FlushElems = [&] {
    if (!Elems.empty()) {
      Slices.push_back(ListInit::get(Elems, IntRecTy::get(Records)));
      Elems.clear();
    }
  };

  do {
    auto LHSLoc = Lex.getLoc();
    CurVal = ParseSliceElement(CurRec);
    if (!CurVal)
      return nullptr;
    auto *CurValTy = CurVal->getType();

    if (auto *ListValTy = dyn_cast<ListRecTy>(CurValTy)) {
      if (!isa<IntRecTy>(ListValTy->getElementType())) {
        Error(LHSLoc,
              "expected list<int>, got " + Twine(ListValTy->getAsString()));
        return nullptr;
      }

      FlushElems();
      Slices.push_back(CurVal);
      Single = false;
      CurVal = nullptr;
    } else if (!isa<IntRecTy>(CurValTy)) {
      Error(LHSLoc,
            "unhandled type " + Twine(CurValTy->getAsString()) + " in range");
      return nullptr;
    }

    if (Lex.getCode() != tgtok::comma)
      break;

    Lex.Lex(); // eat comma

    // `[i,]` is not LISTELEM but LISTSLICE
    Single = false;
    if (CurVal)
      Elems.push_back(CurVal);
    CurVal = nullptr;
  } while (Lex.getCode() != tgtok::r_square);

  if (CurVal) {
    // LISTELEM
    if (Single)
      return CurVal;

    Elems.push_back(CurVal);
  }

  FlushElems();

  // Concatenate lists in Slices
  TypedInit *Result = nullptr;
  for (auto *Slice : Slices) {
    Result = (Result ? cast<TypedInit>(BinOpInit::getListConcat(Result, Slice))
                     : Slice);
  }

  return Result;
}

/// ParseRangePiece - Parse a bit/value range.
///   RangePiece ::= INTVAL
///   RangePiece ::= INTVAL '...' INTVAL
///   RangePiece ::= INTVAL '-' INTVAL
///   RangePiece ::= INTVAL INTVAL 
// The last two forms are deprecated.
bool TGParser::ParseRangePiece(SmallVectorImpl<unsigned> &Ranges,
                               TypedInit *FirstItem) {
  Init *CurVal = FirstItem;
  if (!CurVal)
    CurVal = ParseValue(nullptr);

  IntInit *II = dyn_cast_or_null<IntInit>(CurVal);
  if (!II)
    return TokError("expected integer or bitrange");

  int64_t Start = II->getValue();
  int64_t End;

  if (Start < 0)
    return TokError("invalid range, cannot be negative");

  switch (Lex.getCode()) {
  default:
    Ranges.push_back(Start);
    return false;

  case tgtok::dotdotdot:
  case tgtok::minus: {
    Lex.Lex(); // eat

    Init *I_End = ParseValue(nullptr);
    IntInit *II_End = dyn_cast_or_null<IntInit>(I_End);
    if (!II_End) {
      TokError("expected integer value as end of range");
      return true;
    }

    End = II_End->getValue();
    break;
  }
  case tgtok::IntVal: {
    End = -Lex.getCurIntVal();
    Lex.Lex();
    break;
  }
  }
  if (End < 0)
    return TokError("invalid range, cannot be negative");

  // Add to the range.
  if (Start < End)
    for (; Start <= End; ++Start)
      Ranges.push_back(Start);
  else
    for (; Start >= End; --Start)
      Ranges.push_back(Start);
  return false;
}

/// ParseRangeList - Parse a list of scalars and ranges into scalar values.
///
///   RangeList ::= RangePiece (',' RangePiece)*
///
void TGParser::ParseRangeList(SmallVectorImpl<unsigned> &Result) {
  // Parse the first piece.
  if (ParseRangePiece(Result)) {
    Result.clear();
    return;
  }
  while (consume(tgtok::comma))
    // Parse the next range piece.
    if (ParseRangePiece(Result)) {
      Result.clear();
      return;
    }
}

/// ParseOptionalRangeList - Parse either a range list in <>'s or nothing.
///   OptionalRangeList ::= '<' RangeList '>'
///   OptionalRangeList ::= /*empty*/
bool TGParser::ParseOptionalRangeList(SmallVectorImpl<unsigned> &Ranges) {
  SMLoc StartLoc = Lex.getLoc();
  if (!consume(tgtok::less))
    return false;

  // Parse the range list.
  ParseRangeList(Ranges);
  if (Ranges.empty()) return true;

  if (!consume(tgtok::greater)) {
    TokError("expected '>' at end of range list");
    return Error(StartLoc, "to match this '<'");
  }
  return false;
}

/// ParseOptionalBitList - Parse either a bit list in {}'s or nothing.
///   OptionalBitList ::= '{' RangeList '}'
///   OptionalBitList ::= /*empty*/
bool TGParser::ParseOptionalBitList(SmallVectorImpl<unsigned> &Ranges) {
  SMLoc StartLoc = Lex.getLoc();
  if (!consume(tgtok::l_brace))
    return false;

  // Parse the range list.
  ParseRangeList(Ranges);
  if (Ranges.empty()) return true;

  if (!consume(tgtok::r_brace)) {
    TokError("expected '}' at end of bit list");
    return Error(StartLoc, "to match this '{'");
  }
  return false;
}

/// ParseType - Parse and return a tblgen type.  This returns null on error.
///
///   Type ::= STRING                       // string type
///   Type ::= CODE                         // code type
///   Type ::= BIT                          // bit type
///   Type ::= BITS '<' INTVAL '>'          // bits<x> type
///   Type ::= INT                          // int type
///   Type ::= LIST '<' Type '>'            // list<x> type
///   Type ::= DAG                          // dag type
///   Type ::= ClassID                      // Record Type
///
RecTy *TGParser::ParseType() {
  switch (Lex.getCode()) {
  default: TokError("Unknown token when expecting a type"); return nullptr;
  case tgtok::String:
  case tgtok::Code:
    Lex.Lex();
    return StringRecTy::get(Records);
  case tgtok::Bit:
    Lex.Lex();
    return BitRecTy::get(Records);
  case tgtok::Int:
    Lex.Lex();
    return IntRecTy::get(Records);
  case tgtok::Dag:
    Lex.Lex();
    return DagRecTy::get(Records);
  case tgtok::Id: {
    auto I = TypeAliases.find(Lex.getCurStrVal());
    if (I != TypeAliases.end()) {
      Lex.Lex();
      return I->second;
    }
    if (Record *R = ParseClassID())
      return RecordRecTy::get(R);
    TokError("unknown class name");
    return nullptr;
  }
  case tgtok::Bits: {
    if (Lex.Lex() != tgtok::less) { // Eat 'bits'
      TokError("expected '<' after bits type");
      return nullptr;
    }
    if (Lex.Lex() != tgtok::IntVal) { // Eat '<'
      TokError("expected integer in bits<n> type");
      return nullptr;
    }
    uint64_t Val = Lex.getCurIntVal();
    if (Lex.Lex() != tgtok::greater) { // Eat count.
      TokError("expected '>' at end of bits<n> type");
      return nullptr;
    }
    Lex.Lex();  // Eat '>'
    return BitsRecTy::get(Records, Val);
  }
  case tgtok::List: {
    if (Lex.Lex() != tgtok::less) { // Eat 'bits'
      TokError("expected '<' after list type");
      return nullptr;
    }
    Lex.Lex();  // Eat '<'
    RecTy *SubType = ParseType();
    if (!SubType) return nullptr;

    if (!consume(tgtok::greater)) {
      TokError("expected '>' at end of list<ty> type");
      return nullptr;
    }
    return ListRecTy::get(SubType);
  }
  }
}

/// ParseIDValue
Init *TGParser::ParseIDValue(Record *CurRec, StringInit *Name, SMRange NameLoc,
                             IDParseMode Mode) {
  if (Init *I = CurScope->getVar(Records, CurMultiClass, Name, NameLoc,
                                 TrackReferenceLocs))
    return I;

  if (Mode == ParseNameMode)
    return Name;

  if (Init *I = Records.getGlobal(Name->getValue())) {
    // Add a reference to the global if it's a record.
    if (TrackReferenceLocs) {
      if (auto *Def = dyn_cast<DefInit>(I))
        Def->getDef()->appendReferenceLoc(NameLoc);
    }
    return I;
  }

  // Allow self-references of concrete defs, but delay the lookup so that we
  // get the correct type.
  if (CurRec && !CurRec->isClass() && !CurMultiClass &&
      CurRec->getNameInit() == Name)
    return UnOpInit::get(UnOpInit::CAST, Name, CurRec->getType());

  Error(NameLoc.Start, "Variable not defined: '" + Name->getValue() + "'");
  return nullptr;
}

/// ParseOperation - Parse an operator.  This returns null on error.
///
/// Operation ::= XOperator ['<' Type '>'] '(' Args ')'
///
Init *TGParser::ParseOperation(Record *CurRec, RecTy *ItemType) {
  switch (Lex.getCode()) {
  default:
    TokError("unknown bang operator");
    return nullptr;
  case tgtok::XNOT:
  case tgtok::XToLower:
  case tgtok::XToUpper:
  case tgtok::XLOG2:
  case tgtok::XHead:
  case tgtok::XTail:
  case tgtok::XSize:
  case tgtok::XEmpty:
  case tgtok::XCast:
  case tgtok::XRepr:
  case tgtok::XGetDagOp: { // Value ::= !unop '(' Value ')'
    UnOpInit::UnaryOp Code;
    RecTy *Type = nullptr;

    switch (Lex.getCode()) {
    default: llvm_unreachable("Unhandled code!");
    case tgtok::XCast:
      Lex.Lex();  // eat the operation
      Code = UnOpInit::CAST;

      Type = ParseOperatorType();

      if (!Type) {
        TokError("did not get type for unary operator");
        return nullptr;
      }

      break;
    case tgtok::XRepr:
      Lex.Lex(); // eat the operation
      Code = UnOpInit::REPR;
      Type = StringRecTy::get(Records);
      break;
    case tgtok::XToLower:
      Lex.Lex(); // eat the operation
      Code = UnOpInit::TOLOWER;
      Type = StringRecTy::get(Records);
      break;
    case tgtok::XToUpper:
      Lex.Lex(); // eat the operation
      Code = UnOpInit::TOUPPER;
      Type = StringRecTy::get(Records);
      break;
    case tgtok::XNOT:
      Lex.Lex();  // eat the operation
      Code = UnOpInit::NOT;
      Type = IntRecTy::get(Records);
      break;
    case tgtok::XLOG2:
      Lex.Lex();  // eat the operation
      Code = UnOpInit::LOG2;
      Type = IntRecTy::get(Records);
      break;
    case tgtok::XHead:
      Lex.Lex();  // eat the operation
      Code = UnOpInit::HEAD;
      break;
    case tgtok::XTail:
      Lex.Lex();  // eat the operation
      Code = UnOpInit::TAIL;
      break;
    case tgtok::XSize:
      Lex.Lex();
      Code = UnOpInit::SIZE;
      Type = IntRecTy::get(Records);
      break;
    case tgtok::XEmpty:
      Lex.Lex();  // eat the operation
      Code = UnOpInit::EMPTY;
      Type = IntRecTy::get(Records);
      break;
    case tgtok::XGetDagOp:
      Lex.Lex();  // eat the operation
      if (Lex.getCode() == tgtok::less) {
        // Parse an optional type suffix, so that you can say
        // !getdagop<BaseClass>(someDag) as a shorthand for
        // !cast<BaseClass>(!getdagop(someDag)).
        Type = ParseOperatorType();

        if (!Type) {
          TokError("did not get type for unary operator");
          return nullptr;
        }

        if (!isa<RecordRecTy>(Type)) {
          TokError("type for !getdagop must be a record type");
          // but keep parsing, to consume the operand
        }
      } else {
        Type = RecordRecTy::get(Records, {});
      }
      Code = UnOpInit::GETDAGOP;
      break;
    }
    if (!consume(tgtok::l_paren)) {
      TokError("expected '(' after unary operator");
      return nullptr;
    }

    Init *LHS = ParseValue(CurRec);
    if (!LHS) return nullptr;

    if (Code == UnOpInit::EMPTY || Code == UnOpInit::SIZE) {
      ListInit *LHSl = dyn_cast<ListInit>(LHS);
      StringInit *LHSs = dyn_cast<StringInit>(LHS);
      DagInit *LHSd = dyn_cast<DagInit>(LHS);
      TypedInit *LHSt = dyn_cast<TypedInit>(LHS);
      if (!LHSl && !LHSs && !LHSd && !LHSt) {
        TokError("expected string, list, or dag type argument in unary operator");
        return nullptr;
      }
      if (LHSt) {
        ListRecTy *LType = dyn_cast<ListRecTy>(LHSt->getType());
        StringRecTy *SType = dyn_cast<StringRecTy>(LHSt->getType());
        DagRecTy *DType = dyn_cast<DagRecTy>(LHSt->getType());
        if (!LType && !SType && !DType) {
          TokError("expected string, list, or dag type argument in unary operator");
          return nullptr;
        }
      }
    }

    if (Code == UnOpInit::HEAD || Code == UnOpInit::TAIL) {
      ListInit *LHSl = dyn_cast<ListInit>(LHS);
      TypedInit *LHSt = dyn_cast<TypedInit>(LHS);
      if (!LHSl && !LHSt) {
        TokError("expected list type argument in unary operator");
        return nullptr;
      }
      if (LHSt) {
        ListRecTy *LType = dyn_cast<ListRecTy>(LHSt->getType());
        if (!LType) {
          TokError("expected list type argument in unary operator");
          return nullptr;
        }
      }

      if (LHSl && LHSl->empty()) {
        TokError("empty list argument in unary operator");
        return nullptr;
      }
      if (LHSl) {
        Init *Item = LHSl->getElement(0);
        TypedInit *Itemt = dyn_cast<TypedInit>(Item);
        if (!Itemt) {
          TokError("untyped list element in unary operator");
          return nullptr;
        }
        Type = (Code == UnOpInit::HEAD) ? Itemt->getType()
                                        : ListRecTy::get(Itemt->getType());
      } else {
        assert(LHSt && "expected list type argument in unary operator");
        ListRecTy *LType = dyn_cast<ListRecTy>(LHSt->getType());
        Type = (Code == UnOpInit::HEAD) ? LType->getElementType() : LType;
      }
    }

    if (!consume(tgtok::r_paren)) {
      TokError("expected ')' in unary operator");
      return nullptr;
    }
    return (UnOpInit::get(Code, LHS, Type))->Fold(CurRec);
  }

  case tgtok::XIsA: {
    // Value ::= !isa '<' Type '>' '(' Value ')'
    Lex.Lex(); // eat the operation

    RecTy *Type = ParseOperatorType();
    if (!Type)
      return nullptr;

    if (!consume(tgtok::l_paren)) {
      TokError("expected '(' after type of !isa");
      return nullptr;
    }

    Init *LHS = ParseValue(CurRec);
    if (!LHS)
      return nullptr;

    if (!consume(tgtok::r_paren)) {
      TokError("expected ')' in !isa");
      return nullptr;
    }

    return (IsAOpInit::get(Type, LHS))->Fold();
  }

  case tgtok::XExists: {
    // Value ::= !exists '<' Type '>' '(' Value ')'
    Lex.Lex(); // eat the operation

    RecTy *Type = ParseOperatorType();
    if (!Type)
      return nullptr;

    if (!consume(tgtok::l_paren)) {
      TokError("expected '(' after type of !exists");
      return nullptr;
    }

    SMLoc ExprLoc = Lex.getLoc();
    Init *Expr = ParseValue(CurRec);
    if (!Expr)
      return nullptr;

    TypedInit *ExprType = dyn_cast<TypedInit>(Expr);
    if (!ExprType) {
      Error(ExprLoc, "expected string type argument in !exists operator");
      return nullptr;
    }

    RecordRecTy *RecType = dyn_cast<RecordRecTy>(ExprType->getType());
    if (RecType) {
      Error(ExprLoc,
            "expected string type argument in !exists operator, please "
            "use !isa instead");
      return nullptr;
    }

    StringRecTy *SType = dyn_cast<StringRecTy>(ExprType->getType());
    if (!SType) {
      Error(ExprLoc, "expected string type argument in !exists operator");
      return nullptr;
    }

    if (!consume(tgtok::r_paren)) {
      TokError("expected ')' in !exists");
      return nullptr;
    }

    return (ExistsOpInit::get(Type, Expr))->Fold(CurRec);
  }

  case tgtok::XConcat:
  case tgtok::XADD:
  case tgtok::XSUB:
  case tgtok::XMUL:
  case tgtok::XDIV:
  case tgtok::XAND:
  case tgtok::XOR:
  case tgtok::XXOR:
  case tgtok::XSRA:
  case tgtok::XSRL:
  case tgtok::XSHL:
  case tgtok::XEq:
  case tgtok::XNe:
  case tgtok::XLe:
  case tgtok::XLt:
  case tgtok::XGe:
  case tgtok::XGt:
  case tgtok::XListConcat:
  case tgtok::XListSplat:
  case tgtok::XListRemove:
  case tgtok::XStrConcat:
  case tgtok::XInterleave:
  case tgtok::XGetDagArg:
  case tgtok::XGetDagName:
  case tgtok::XSetDagOp: { // Value ::= !binop '(' Value ',' Value ')'
    tgtok::TokKind OpTok = Lex.getCode();
    SMLoc OpLoc = Lex.getLoc();
    Lex.Lex();  // eat the operation

    BinOpInit::BinaryOp Code;
    switch (OpTok) {
    default: llvm_unreachable("Unhandled code!");
    case tgtok::XConcat: Code = BinOpInit::CONCAT; break;
    case tgtok::XADD:    Code = BinOpInit::ADD; break;
    case tgtok::XSUB:    Code = BinOpInit::SUB; break;
    case tgtok::XMUL:    Code = BinOpInit::MUL; break;
    case tgtok::XDIV:    Code = BinOpInit::DIV; break;
    case tgtok::XAND:    Code = BinOpInit::AND; break;
    case tgtok::XOR:     Code = BinOpInit::OR; break;
    case tgtok::XXOR:    Code = BinOpInit::XOR; break;
    case tgtok::XSRA:    Code = BinOpInit::SRA; break;
    case tgtok::XSRL:    Code = BinOpInit::SRL; break;
    case tgtok::XSHL:    Code = BinOpInit::SHL; break;
    case tgtok::XEq:     Code = BinOpInit::EQ; break;
    case tgtok::XNe:     Code = BinOpInit::NE; break;
    case tgtok::XLe:     Code = BinOpInit::LE; break;
    case tgtok::XLt:     Code = BinOpInit::LT; break;
    case tgtok::XGe:     Code = BinOpInit::GE; break;
    case tgtok::XGt:     Code = BinOpInit::GT; break;
    case tgtok::XListConcat: Code = BinOpInit::LISTCONCAT; break;
    case tgtok::XListSplat:  Code = BinOpInit::LISTSPLAT; break;
    case tgtok::XListRemove:
      Code = BinOpInit::LISTREMOVE;
      break;
    case tgtok::XStrConcat:  Code = BinOpInit::STRCONCAT; break;
    case tgtok::XInterleave: Code = BinOpInit::INTERLEAVE; break;
    case tgtok::XSetDagOp:   Code = BinOpInit::SETDAGOP; break;
    case tgtok::XGetDagArg:
      Code = BinOpInit::GETDAGARG;
      break;
    case tgtok::XGetDagName:
      Code = BinOpInit::GETDAGNAME;
      break;
    }

    RecTy *Type = nullptr;
    RecTy *ArgType = nullptr;
    switch (OpTok) {
    default:
      llvm_unreachable("Unhandled code!");
    case tgtok::XConcat:
    case tgtok::XSetDagOp:
      Type = DagRecTy::get(Records);
      ArgType = DagRecTy::get(Records);
      break;
    case tgtok::XGetDagArg:
      Type = ParseOperatorType();
      if (!Type) {
        TokError("did not get type for !getdagarg operator");
        return nullptr;
      }
      ArgType = DagRecTy::get(Records);
      break;
    case tgtok::XGetDagName:
      Type = StringRecTy::get(Records);
      ArgType = DagRecTy::get(Records);
      break;
    case tgtok::XAND:
    case tgtok::XOR:
    case tgtok::XXOR:
    case tgtok::XSRA:
    case tgtok::XSRL:
    case tgtok::XSHL:
    case tgtok::XADD:
    case tgtok::XSUB:
    case tgtok::XMUL:
    case tgtok::XDIV:
      Type = IntRecTy::get(Records);
      ArgType = IntRecTy::get(Records);
      break;
    case tgtok::XEq:
    case tgtok::XNe:
    case tgtok::XLe:
    case tgtok::XLt:
    case tgtok::XGe:
    case tgtok::XGt:
      Type = BitRecTy::get(Records);
      // ArgType for the comparison operators is not yet known.
      break;
    case tgtok::XListConcat:
      // We don't know the list type until we parse the first argument.
      ArgType = ItemType;
      break;
    case tgtok::XListSplat:
      // Can't do any typechecking until we parse the first argument.
      break;
    case tgtok::XListRemove:
      // We don't know the list type until we parse the first argument.
      ArgType = ItemType;
      break;
    case tgtok::XStrConcat:
      Type = StringRecTy::get(Records);
      ArgType = StringRecTy::get(Records);
      break;
    case tgtok::XInterleave:
      Type = StringRecTy::get(Records);
      // The first argument type is not yet known.
    }

    if (Type && ItemType && !Type->typeIsConvertibleTo(ItemType)) {
      Error(OpLoc, Twine("expected value of type '") +
                   ItemType->getAsString() + "', got '" +
                   Type->getAsString() + "'");
      return nullptr;
    }

    if (!consume(tgtok::l_paren)) {
      TokError("expected '(' after binary operator");
      return nullptr;
    }

    SmallVector<Init*, 2> InitList;

    // Note that this loop consumes an arbitrary number of arguments.
    // The actual count is checked later.
    for (;;) {
      SMLoc InitLoc = Lex.getLoc();
      InitList.push_back(ParseValue(CurRec, ArgType));
      if (!InitList.back()) return nullptr;

      TypedInit *InitListBack = dyn_cast<TypedInit>(InitList.back());
      if (!InitListBack) {
        Error(OpLoc, Twine("expected value to be a typed value, got '" +
                           InitList.back()->getAsString() + "'"));
        return nullptr;
      }
      RecTy *ListType = InitListBack->getType();

      if (!ArgType) {
        // Argument type must be determined from the argument itself.
        ArgType = ListType;

        switch (Code) {
        case BinOpInit::LISTCONCAT:
          if (!isa<ListRecTy>(ArgType)) {
            Error(InitLoc, Twine("expected a list, got value of type '") +
                           ArgType->getAsString() + "'");
            return nullptr;
          }
          break;
        case BinOpInit::LISTSPLAT:
          if (ItemType && InitList.size() == 1) {
            if (!isa<ListRecTy>(ItemType)) {
              Error(OpLoc,
                    Twine("expected output type to be a list, got type '") +
                        ItemType->getAsString() + "'");
              return nullptr;
            }
            if (!ArgType->getListTy()->typeIsConvertibleTo(ItemType)) {
              Error(OpLoc, Twine("expected first arg type to be '") +
                               ArgType->getAsString() +
                               "', got value of type '" +
                               cast<ListRecTy>(ItemType)
                                   ->getElementType()
                                   ->getAsString() +
                               "'");
              return nullptr;
            }
          }
          if (InitList.size() == 2 && !isa<IntRecTy>(ArgType)) {
            Error(InitLoc, Twine("expected second parameter to be an int, got "
                                 "value of type '") +
                               ArgType->getAsString() + "'");
            return nullptr;
          }
          ArgType = nullptr; // Broken invariant: types not identical.
          break;
        case BinOpInit::LISTREMOVE:
          if (!isa<ListRecTy>(ArgType)) {
            Error(InitLoc, Twine("expected a list, got value of type '") +
                               ArgType->getAsString() + "'");
            return nullptr;
          }
          break;
        case BinOpInit::EQ:
        case BinOpInit::NE:
          if (!ArgType->typeIsConvertibleTo(IntRecTy::get(Records)) &&
              !ArgType->typeIsConvertibleTo(StringRecTy::get(Records)) &&
              !ArgType->typeIsConvertibleTo(RecordRecTy::get(Records, {}))) {
            Error(InitLoc, Twine("expected bit, bits, int, string, or record; "
                                 "got value of type '") + ArgType->getAsString() + 
                                 "'");
            return nullptr;
          }
          break;
        case BinOpInit::GETDAGARG: // The 2nd argument of !getdagarg could be
                                   // index or name.
        case BinOpInit::LE:
        case BinOpInit::LT:
        case BinOpInit::GE:
        case BinOpInit::GT:
          if (!ArgType->typeIsConvertibleTo(IntRecTy::get(Records)) &&
              !ArgType->typeIsConvertibleTo(StringRecTy::get(Records))) {
            Error(InitLoc, Twine("expected bit, bits, int, or string; "
                                 "got value of type '") + ArgType->getAsString() + 
                                 "'");
            return nullptr;
          }
          break;
        case BinOpInit::INTERLEAVE:
          switch (InitList.size()) {
          case 1: // First argument must be a list of strings or integers.
            if (ArgType != StringRecTy::get(Records)->getListTy() &&
                !ArgType->typeIsConvertibleTo(
                    IntRecTy::get(Records)->getListTy())) {
              Error(InitLoc, Twine("expected list of string, int, bits, or bit; "
                                   "got value of type '") +
                                   ArgType->getAsString() + "'");
              return nullptr;
            }
            break;
          case 2: // Second argument must be a string.
            if (!isa<StringRecTy>(ArgType)) {
              Error(InitLoc, Twine("expected second argument to be a string, "
                                   "got value of type '") +
                                 ArgType->getAsString() + "'");
              return nullptr;
            }
            break;
          default: ;
          }
          ArgType = nullptr; // Broken invariant: types not identical.
          break;
        default: llvm_unreachable("other ops have fixed argument types");
        }

      } else {
        // Desired argument type is a known and in ArgType.
        RecTy *Resolved = resolveTypes(ArgType, ListType);
        if (!Resolved) {
          Error(InitLoc, Twine("expected value of type '") +
                             ArgType->getAsString() + "', got '" +
                             ListType->getAsString() + "'");
          return nullptr;
        }
        if (Code != BinOpInit::ADD && Code != BinOpInit::SUB &&
            Code != BinOpInit::AND && Code != BinOpInit::OR &&
            Code != BinOpInit::XOR && Code != BinOpInit::SRA &&
            Code != BinOpInit::SRL && Code != BinOpInit::SHL &&
            Code != BinOpInit::MUL && Code != BinOpInit::DIV)
          ArgType = Resolved;
      }

      // Deal with BinOps whose arguments have different types, by
      // rewriting ArgType in between them.
      switch (Code) {
        case BinOpInit::SETDAGOP:
          // After parsing the first dag argument, switch to expecting
          // a record, with no restriction on its superclasses.
          ArgType = RecordRecTy::get(Records, {});
          break;
        case BinOpInit::GETDAGARG:
          // After parsing the first dag argument, expect an index integer or a
          // name string.
          ArgType = nullptr;
          break;
        case BinOpInit::GETDAGNAME:
          // After parsing the first dag argument, expect an index integer.
          ArgType = IntRecTy::get(Records);
          break;
        default:
          break;
      }

      if (!consume(tgtok::comma))
        break;
    }

    if (!consume(tgtok::r_paren)) {
      TokError("expected ')' in operator");
      return nullptr;
    }

    // listconcat returns a list with type of the argument.
    if (Code == BinOpInit::LISTCONCAT)
      Type = ArgType;
    // listsplat returns a list of type of the *first* argument.
    if (Code == BinOpInit::LISTSPLAT)
      Type = cast<TypedInit>(InitList.front())->getType()->getListTy();
    // listremove returns a list with type of the argument.
    if (Code == BinOpInit::LISTREMOVE)
      Type = ArgType;

    // We allow multiple operands to associative operators like !strconcat as
    // shorthand for nesting them.
    if (Code == BinOpInit::STRCONCAT || Code == BinOpInit::LISTCONCAT ||
        Code == BinOpInit::CONCAT || Code == BinOpInit::ADD ||
        Code == BinOpInit::AND || Code == BinOpInit::OR ||
        Code == BinOpInit::XOR || Code == BinOpInit::MUL) {
      while (InitList.size() > 2) {
        Init *RHS = InitList.pop_back_val();
        RHS = (BinOpInit::get(Code, InitList.back(), RHS, Type))->Fold(CurRec);
        InitList.back() = RHS;
      }
    }

    if (InitList.size() == 2)
      return (BinOpInit::get(Code, InitList[0], InitList[1], Type))
          ->Fold(CurRec);

    Error(OpLoc, "expected two operands to operator");
    return nullptr;
  }

  case tgtok::XForEach:
  case tgtok::XFilter: {
    return ParseOperationForEachFilter(CurRec, ItemType);
  }

  case tgtok::XRange: {
    SMLoc OpLoc = Lex.getLoc();
    Lex.Lex(); // eat the operation

    if (!consume(tgtok::l_paren)) {
      TokError("expected '(' after !range operator");
      return nullptr;
    }

    SmallVector<Init *, 2> Args;
    bool FirstArgIsList = false;
    for (;;) {
      if (Args.size() >= 3) {
        TokError("expected at most three values of integer");
        return nullptr;
      }

      SMLoc InitLoc = Lex.getLoc();
      Args.push_back(ParseValue(CurRec));
      if (!Args.back())
        return nullptr;

      TypedInit *ArgBack = dyn_cast<TypedInit>(Args.back());
      if (!ArgBack) {
        Error(OpLoc, Twine("expected value to be a typed value, got '" +
                           Args.back()->getAsString() + "'"));
        return nullptr;
      }

      RecTy *ArgBackType = ArgBack->getType();
      if (!FirstArgIsList || Args.size() == 1) {
        if (Args.size() == 1 && isa<ListRecTy>(ArgBackType)) {
          FirstArgIsList = true; // Detect error if 2nd arg were present.
        } else if (isa<IntRecTy>(ArgBackType)) {
          // Assume 2nd arg should be IntRecTy
        } else {
          if (Args.size() != 1)
            Error(InitLoc, Twine("expected value of type 'int', got '" +
                                 ArgBackType->getAsString() + "'"));
          else
            Error(InitLoc, Twine("expected list or int, got value of type '") +
                               ArgBackType->getAsString() + "'");
          return nullptr;
        }
      } else {
        // Don't come here unless 1st arg is ListRecTy.
        assert(isa<ListRecTy>(cast<TypedInit>(Args[0])->getType()));
        Error(InitLoc, Twine("expected one list, got extra value of type '") +
                           ArgBackType->getAsString() + "'");
        return nullptr;
      }
      if (!consume(tgtok::comma))
        break;
    }

    if (!consume(tgtok::r_paren)) {
      TokError("expected ')' in operator");
      return nullptr;
    }

    Init *LHS, *MHS, *RHS;
    auto ArgCount = Args.size();
    assert(ArgCount >= 1);
    auto *Arg0 = cast<TypedInit>(Args[0]);
    auto *Arg0Ty = Arg0->getType();
    if (ArgCount == 1) {
      if (isa<ListRecTy>(Arg0Ty)) {
        // (0, !size(arg), 1)
        LHS = IntInit::get(Records, 0);
        MHS = UnOpInit::get(UnOpInit::SIZE, Arg0, IntRecTy::get(Records))
                  ->Fold(CurRec);
        RHS = IntInit::get(Records, 1);
      } else {
        assert(isa<IntRecTy>(Arg0Ty));
        // (0, arg, 1)
        LHS = IntInit::get(Records, 0);
        MHS = Arg0;
        RHS = IntInit::get(Records, 1);
      }
    } else {
      assert(isa<IntRecTy>(Arg0Ty));
      auto *Arg1 = cast<TypedInit>(Args[1]);
      assert(isa<IntRecTy>(Arg1->getType()));
      LHS = Arg0;
      MHS = Arg1;
      if (ArgCount == 3) {
        // (start, end, step)
        auto *Arg2 = cast<TypedInit>(Args[2]);
        assert(isa<IntRecTy>(Arg2->getType()));
        RHS = Arg2;
      } else
        // (start, end, 1)
        RHS = IntInit::get(Records, 1);
    }
    return TernOpInit::get(TernOpInit::RANGE, LHS, MHS, RHS,
                           IntRecTy::get(Records)->getListTy())
        ->Fold(CurRec);
  }

  case tgtok::XSetDagArg:
  case tgtok::XSetDagName:
  case tgtok::XDag:
  case tgtok::XIf:
  case tgtok::XSubst: { // Value ::= !ternop '(' Value ',' Value ',' Value ')'
    TernOpInit::TernaryOp Code;
    RecTy *Type = nullptr;

    tgtok::TokKind LexCode = Lex.getCode();
    Lex.Lex();  // eat the operation
    switch (LexCode) {
    default: llvm_unreachable("Unhandled code!");
    case tgtok::XDag:
      Code = TernOpInit::DAG;
      Type = DagRecTy::get(Records);
      ItemType = nullptr;
      break;
    case tgtok::XIf:
      Code = TernOpInit::IF;
      break;
    case tgtok::XSubst:
      Code = TernOpInit::SUBST;
      break;
    case tgtok::XSetDagArg:
      Code = TernOpInit::SETDAGARG;
      Type = DagRecTy::get(Records);
      ItemType = nullptr;
      break;
    case tgtok::XSetDagName:
      Code = TernOpInit::SETDAGNAME;
      Type = DagRecTy::get(Records);
      ItemType = nullptr;
      break;
    }
    if (!consume(tgtok::l_paren)) {
      TokError("expected '(' after ternary operator");
      return nullptr;
    }

    Init *LHS = ParseValue(CurRec);
    if (!LHS) return nullptr;

    if (!consume(tgtok::comma)) {
      TokError("expected ',' in ternary operator");
      return nullptr;
    }

    SMLoc MHSLoc = Lex.getLoc();
    Init *MHS = ParseValue(CurRec, ItemType);
    if (!MHS)
      return nullptr;

    if (!consume(tgtok::comma)) {
      TokError("expected ',' in ternary operator");
      return nullptr;
    }

    SMLoc RHSLoc = Lex.getLoc();
    Init *RHS = ParseValue(CurRec, ItemType);
    if (!RHS)
      return nullptr;

    if (!consume(tgtok::r_paren)) {
      TokError("expected ')' in binary operator");
      return nullptr;
    }

    switch (LexCode) {
    default: llvm_unreachable("Unhandled code!");
    case tgtok::XDag: {
      TypedInit *MHSt = dyn_cast<TypedInit>(MHS);
      if (!MHSt && !isa<UnsetInit>(MHS)) {
        Error(MHSLoc, "could not determine type of the child list in !dag");
        return nullptr;
      }
      if (MHSt && !isa<ListRecTy>(MHSt->getType())) {
        Error(MHSLoc, Twine("expected list of children, got type '") +
                          MHSt->getType()->getAsString() + "'");
        return nullptr;
      }

      TypedInit *RHSt = dyn_cast<TypedInit>(RHS);
      if (!RHSt && !isa<UnsetInit>(RHS)) {
        Error(RHSLoc, "could not determine type of the name list in !dag");
        return nullptr;
      }
      if (RHSt && StringRecTy::get(Records)->getListTy() != RHSt->getType()) {
        Error(RHSLoc, Twine("expected list<string>, got type '") +
                          RHSt->getType()->getAsString() + "'");
        return nullptr;
      }

      if (!MHSt && !RHSt) {
        Error(MHSLoc,
              "cannot have both unset children and unset names in !dag");
        return nullptr;
      }
      break;
    }
    case tgtok::XIf: {
      RecTy *MHSTy = nullptr;
      RecTy *RHSTy = nullptr;

      if (TypedInit *MHSt = dyn_cast<TypedInit>(MHS))
        MHSTy = MHSt->getType();
      if (BitsInit *MHSbits = dyn_cast<BitsInit>(MHS))
        MHSTy = BitsRecTy::get(Records, MHSbits->getNumBits());
      if (isa<BitInit>(MHS))
        MHSTy = BitRecTy::get(Records);

      if (TypedInit *RHSt = dyn_cast<TypedInit>(RHS))
        RHSTy = RHSt->getType();
      if (BitsInit *RHSbits = dyn_cast<BitsInit>(RHS))
        RHSTy = BitsRecTy::get(Records, RHSbits->getNumBits());
      if (isa<BitInit>(RHS))
        RHSTy = BitRecTy::get(Records);

      // For UnsetInit, it's typed from the other hand.
      if (isa<UnsetInit>(MHS))
        MHSTy = RHSTy;
      if (isa<UnsetInit>(RHS))
        RHSTy = MHSTy;

      if (!MHSTy || !RHSTy) {
        TokError("could not get type for !if");
        return nullptr;
      }

      Type = resolveTypes(MHSTy, RHSTy);
      if (!Type) {
        TokError(Twine("inconsistent types '") + MHSTy->getAsString() +
                 "' and '" + RHSTy->getAsString() + "' for !if");
        return nullptr;
      }
      break;
    }
    case tgtok::XSubst: {
      TypedInit *RHSt = dyn_cast<TypedInit>(RHS);
      if (!RHSt) {
        TokError("could not get type for !subst");
        return nullptr;
      }
      Type = RHSt->getType();
      break;
    }
    case tgtok::XSetDagArg: {
      TypedInit *MHSt = dyn_cast<TypedInit>(MHS);
      if (!MHSt || !isa<IntRecTy, StringRecTy>(MHSt->getType())) {
        Error(MHSLoc, Twine("expected integer index or string name, got ") +
                          (MHSt ? ("type '" + MHSt->getType()->getAsString())
                                : ("'" + MHS->getAsString())) +
                          "'");
        return nullptr;
      }
      break;
    }
    case tgtok::XSetDagName: {
      TypedInit *MHSt = dyn_cast<TypedInit>(MHS);
      if (!MHSt || !isa<IntRecTy, StringRecTy>(MHSt->getType())) {
        Error(MHSLoc, Twine("expected integer index or string name, got ") +
                          (MHSt ? ("type '" + MHSt->getType()->getAsString())
                                : ("'" + MHS->getAsString())) +
                          "'");
        return nullptr;
      }
      TypedInit *RHSt = dyn_cast<TypedInit>(RHS);
      // The name could be a string or unset.
      if (RHSt && !isa<StringRecTy>(RHSt->getType())) {
        Error(RHSLoc, Twine("expected string or unset name, got type '") +
                          RHSt->getType()->getAsString() + "'");
        return nullptr;
      }
      break;
    }
    }
    return (TernOpInit::get(Code, LHS, MHS, RHS, Type))->Fold(CurRec);
  }

  case tgtok::XSubstr:
    return ParseOperationSubstr(CurRec, ItemType);

  case tgtok::XFind:
    return ParseOperationFind(CurRec, ItemType);

  case tgtok::XCond:
    return ParseOperationCond(CurRec, ItemType);

  case tgtok::XFoldl: {
    // Value ::= !foldl '(' Value ',' Value ',' Id ',' Id ',' Expr ')'
    Lex.Lex(); // eat the operation
    if (!consume(tgtok::l_paren)) {
      TokError("expected '(' after !foldl");
      return nullptr;
    }

    Init *StartUntyped = ParseValue(CurRec);
    if (!StartUntyped)
      return nullptr;

    TypedInit *Start = dyn_cast<TypedInit>(StartUntyped);
    if (!Start) {
      TokError(Twine("could not get type of !foldl start: '") +
               StartUntyped->getAsString() + "'");
      return nullptr;
    }

    if (!consume(tgtok::comma)) {
      TokError("expected ',' in !foldl");
      return nullptr;
    }

    Init *ListUntyped = ParseValue(CurRec);
    if (!ListUntyped)
      return nullptr;

    TypedInit *List = dyn_cast<TypedInit>(ListUntyped);
    if (!List) {
      TokError(Twine("could not get type of !foldl list: '") +
               ListUntyped->getAsString() + "'");
      return nullptr;
    }

    ListRecTy *ListType = dyn_cast<ListRecTy>(List->getType());
    if (!ListType) {
      TokError(Twine("!foldl list must be a list, but is of type '") +
               List->getType()->getAsString());
      return nullptr;
    }

    if (Lex.getCode() != tgtok::comma) {
      TokError("expected ',' in !foldl");
      return nullptr;
    }

    if (Lex.Lex() != tgtok::Id) { // eat the ','
      TokError("third argument of !foldl must be an identifier");
      return nullptr;
    }

    Init *A = StringInit::get(Records, Lex.getCurStrVal());
    if (CurRec && CurRec->getValue(A)) {
      TokError((Twine("left !foldl variable '") + A->getAsString() +
                "' already defined")
                   .str());
      return nullptr;
    }

    if (Lex.Lex() != tgtok::comma) { // eat the id
      TokError("expected ',' in !foldl");
      return nullptr;
    }

    if (Lex.Lex() != tgtok::Id) { // eat the ','
      TokError("fourth argument of !foldl must be an identifier");
      return nullptr;
    }

    Init *B = StringInit::get(Records, Lex.getCurStrVal());
    if (CurRec && CurRec->getValue(B)) {
      TokError((Twine("right !foldl variable '") + B->getAsString() +
                "' already defined")
                   .str());
      return nullptr;
    }

    if (Lex.Lex() != tgtok::comma) { // eat the id
      TokError("expected ',' in !foldl");
      return nullptr;
    }
    Lex.Lex(); // eat the ','

    // We need to create a temporary record to provide a scope for the
    // two variables.
    std::unique_ptr<Record> ParseRecTmp;
    Record *ParseRec = CurRec;
    if (!ParseRec) {
      ParseRecTmp = std::make_unique<Record>(".parse", ArrayRef<SMLoc>{}, Records);
      ParseRec = ParseRecTmp.get();
    }

    TGVarScope *FoldScope = PushScope(ParseRec);
    ParseRec->addValue(RecordVal(A, Start->getType(), RecordVal::FK_Normal));
    ParseRec->addValue(
        RecordVal(B, ListType->getElementType(), RecordVal::FK_Normal));
    Init *ExprUntyped = ParseValue(ParseRec);
    ParseRec->removeValue(A);
    ParseRec->removeValue(B);
    PopScope(FoldScope);
    if (!ExprUntyped)
      return nullptr;

    TypedInit *Expr = dyn_cast<TypedInit>(ExprUntyped);
    if (!Expr) {
      TokError("could not get type of !foldl expression");
      return nullptr;
    }

    if (Expr->getType() != Start->getType()) {
      TokError(Twine("!foldl expression must be of same type as start (") +
               Start->getType()->getAsString() + "), but is of type " +
               Expr->getType()->getAsString());
      return nullptr;
    }

    if (!consume(tgtok::r_paren)) {
      TokError("expected ')' in fold operator");
      return nullptr;
    }

    return FoldOpInit::get(Start, List, A, B, Expr, Start->getType())
        ->Fold(CurRec);
  }
  }
}

/// ParseOperatorType - Parse a type for an operator.  This returns
/// null on error.
///
/// OperatorType ::= '<' Type '>'
///
RecTy *TGParser::ParseOperatorType() {
  RecTy *Type = nullptr;

  if (!consume(tgtok::less)) {
    TokError("expected type name for operator");
    return nullptr;
  }

  if (Lex.getCode() == tgtok::Code)
    TokError("the 'code' type is not allowed in bang operators; use 'string'");

  Type = ParseType();

  if (!Type) {
    TokError("expected type name for operator");
    return nullptr;
  }

  if (!consume(tgtok::greater)) {
    TokError("expected type name for operator");
    return nullptr;
  }

  return Type;
}

/// Parse the !substr operation. Return null on error.
///
/// Substr ::= !substr(string, start-int [, length-int]) => string
Init *TGParser::ParseOperationSubstr(Record *CurRec, RecTy *ItemType) {
  TernOpInit::TernaryOp Code = TernOpInit::SUBSTR;
  RecTy *Type = StringRecTy::get(Records);

  Lex.Lex(); // eat the operation

  if (!consume(tgtok::l_paren)) {
    TokError("expected '(' after !substr operator");
    return nullptr;
  }

  Init *LHS = ParseValue(CurRec);
  if (!LHS)
    return nullptr;

  if (!consume(tgtok::comma)) {
    TokError("expected ',' in !substr operator");
    return nullptr;
  }

  SMLoc MHSLoc = Lex.getLoc();
  Init *MHS = ParseValue(CurRec);
  if (!MHS)
    return nullptr;

  SMLoc RHSLoc = Lex.getLoc();
  Init *RHS;
  if (consume(tgtok::comma)) {
    RHSLoc = Lex.getLoc();
    RHS = ParseValue(CurRec);
    if (!RHS)
      return nullptr;
  } else {
    RHS = IntInit::get(Records, std::numeric_limits<int64_t>::max());
  }

  if (!consume(tgtok::r_paren)) {
    TokError("expected ')' in !substr operator");
    return nullptr;
  }

  if (ItemType && !Type->typeIsConvertibleTo(ItemType)) {
    Error(RHSLoc, Twine("expected value of type '") +
                  ItemType->getAsString() + "', got '" +
                  Type->getAsString() + "'");
  }

  TypedInit *LHSt = dyn_cast<TypedInit>(LHS);
  if (!LHSt && !isa<UnsetInit>(LHS)) {
    TokError("could not determine type of the string in !substr");
    return nullptr;
  }
  if (LHSt && !isa<StringRecTy>(LHSt->getType())) {
    TokError(Twine("expected string, got type '") +
             LHSt->getType()->getAsString() + "'");
    return nullptr;
  }

  TypedInit *MHSt = dyn_cast<TypedInit>(MHS);
  if (!MHSt && !isa<UnsetInit>(MHS)) {
    TokError("could not determine type of the start position in !substr");
    return nullptr;
  }
  if (MHSt && !isa<IntRecTy>(MHSt->getType())) {
    Error(MHSLoc, Twine("expected int, got type '") +
                      MHSt->getType()->getAsString() + "'");
    return nullptr;
  }

  if (RHS) {
    TypedInit *RHSt = dyn_cast<TypedInit>(RHS);
    if (!RHSt && !isa<UnsetInit>(RHS)) {
      TokError("could not determine type of the length in !substr");
      return nullptr;
    }
    if (RHSt && !isa<IntRecTy>(RHSt->getType())) {
      TokError(Twine("expected int, got type '") +
               RHSt->getType()->getAsString() + "'");
      return nullptr;
    }
  }

  return (TernOpInit::get(Code, LHS, MHS, RHS, Type))->Fold(CurRec);
}

/// Parse the !find operation. Return null on error.
///
/// Substr ::= !find(string, string [, start-int]) => int
Init *TGParser::ParseOperationFind(Record *CurRec, RecTy *ItemType) {
  TernOpInit::TernaryOp Code = TernOpInit::FIND;
  RecTy *Type = IntRecTy::get(Records);

  Lex.Lex(); // eat the operation

  if (!consume(tgtok::l_paren)) {
    TokError("expected '(' after !find operator");
    return nullptr;
  }

  Init *LHS = ParseValue(CurRec);
  if (!LHS)
    return nullptr;

  if (!consume(tgtok::comma)) {
    TokError("expected ',' in !find operator");
    return nullptr;
  }

  SMLoc MHSLoc = Lex.getLoc();
  Init *MHS = ParseValue(CurRec);
  if (!MHS)
    return nullptr;

  SMLoc RHSLoc = Lex.getLoc();
  Init *RHS;
  if (consume(tgtok::comma)) {
    RHSLoc = Lex.getLoc();
    RHS = ParseValue(CurRec);
    if (!RHS)
      return nullptr;
  } else {
    RHS = IntInit::get(Records, 0);
  }

  if (!consume(tgtok::r_paren)) {
    TokError("expected ')' in !find operator");
    return nullptr;
  }

  if (ItemType && !Type->typeIsConvertibleTo(ItemType)) {
    Error(RHSLoc, Twine("expected value of type '") +
                  ItemType->getAsString() + "', got '" +
                  Type->getAsString() + "'");
  }

  TypedInit *LHSt = dyn_cast<TypedInit>(LHS);
  if (!LHSt && !isa<UnsetInit>(LHS)) {
    TokError("could not determine type of the source string in !find");
    return nullptr;
  }
  if (LHSt && !isa<StringRecTy>(LHSt->getType())) {
    TokError(Twine("expected string, got type '") +
             LHSt->getType()->getAsString() + "'");
    return nullptr;
  }

  TypedInit *MHSt = dyn_cast<TypedInit>(MHS);
  if (!MHSt && !isa<UnsetInit>(MHS)) {
    TokError("could not determine type of the target string in !find");
    return nullptr;
  }
  if (MHSt && !isa<StringRecTy>(MHSt->getType())) {
    Error(MHSLoc, Twine("expected string, got type '") +
                      MHSt->getType()->getAsString() + "'");
    return nullptr;
  }

  if (RHS) {
    TypedInit *RHSt = dyn_cast<TypedInit>(RHS);
    if (!RHSt && !isa<UnsetInit>(RHS)) {
      TokError("could not determine type of the start position in !find");
      return nullptr;
    }
    if (RHSt && !isa<IntRecTy>(RHSt->getType())) {
      TokError(Twine("expected int, got type '") +
               RHSt->getType()->getAsString() + "'");
      return nullptr;
    }
  }

  return (TernOpInit::get(Code, LHS, MHS, RHS, Type))->Fold(CurRec);
}

/// Parse the !foreach and !filter operations. Return null on error.
///
/// ForEach ::= !foreach(ID, list-or-dag, expr) => list<expr type>
/// Filter  ::= !foreach(ID, list, predicate) ==> list<list type>
Init *TGParser::ParseOperationForEachFilter(Record *CurRec, RecTy *ItemType) { 
  SMLoc OpLoc = Lex.getLoc();
  tgtok::TokKind Operation = Lex.getCode();
  Lex.Lex(); // eat the operation
  if (Lex.getCode() != tgtok::l_paren) {
    TokError("expected '(' after !foreach/!filter");
    return nullptr;
  }

  if (Lex.Lex() != tgtok::Id) { // eat the '('
    TokError("first argument of !foreach/!filter must be an identifier");
    return nullptr;
  }

  Init *LHS = StringInit::get(Records, Lex.getCurStrVal());
  Lex.Lex(); // eat the ID.

  if (CurRec && CurRec->getValue(LHS)) {
    TokError((Twine("iteration variable '") + LHS->getAsString() +
              "' is already defined")
                 .str());
    return nullptr;
  }

  if (!consume(tgtok::comma)) {
    TokError("expected ',' in !foreach/!filter");
    return nullptr;
  }

  Init *MHS = ParseValue(CurRec);
  if (!MHS)
    return nullptr;

  if (!consume(tgtok::comma)) {
    TokError("expected ',' in !foreach/!filter");
    return nullptr;
  }

  TypedInit *MHSt = dyn_cast<TypedInit>(MHS);
  if (!MHSt) {
    TokError("could not get type of !foreach/!filter list or dag");
    return nullptr;
  }

  RecTy *InEltType = nullptr;
  RecTy *ExprEltType = nullptr;
  bool IsDAG = false;

  if (ListRecTy *InListTy = dyn_cast<ListRecTy>(MHSt->getType())) {
    InEltType = InListTy->getElementType();
    if (ItemType) {
      if (ListRecTy *OutListTy = dyn_cast<ListRecTy>(ItemType)) {
        ExprEltType = (Operation == tgtok::XForEach)
                          ? OutListTy->getElementType()
                          : IntRecTy::get(Records);
      } else {
        Error(OpLoc,
              "expected value of type '" +
                  Twine(ItemType->getAsString()) +
                  "', but got list type");
        return nullptr;
      }
    }
  } else if (DagRecTy *InDagTy = dyn_cast<DagRecTy>(MHSt->getType())) {
    if (Operation == tgtok::XFilter) {
      TokError("!filter must have a list argument");
      return nullptr;
    }
    InEltType = InDagTy;
    if (ItemType && !isa<DagRecTy>(ItemType)) {
      Error(OpLoc,
            "expected value of type '" + Twine(ItemType->getAsString()) +
                "', but got dag type");
      return nullptr;
    }
    IsDAG = true;
  } else {
    if (Operation == tgtok::XForEach)
      TokError("!foreach must have a list or dag argument");
    else
      TokError("!filter must have a list argument");
    return nullptr;
  }

  // We need to create a temporary record to provide a scope for the
  // iteration variable.
  std::unique_ptr<Record> ParseRecTmp;
  Record *ParseRec = CurRec;
  if (!ParseRec) {
    ParseRecTmp =
        std::make_unique<Record>(".parse", ArrayRef<SMLoc>{}, Records);
    ParseRec = ParseRecTmp.get();
  }
  TGVarScope *TempScope = PushScope(ParseRec);
  ParseRec->addValue(RecordVal(LHS, InEltType, RecordVal::FK_Normal));
  Init *RHS = ParseValue(ParseRec, ExprEltType);
  ParseRec->removeValue(LHS);
  PopScope(TempScope);
  if (!RHS)
    return nullptr;

  if (!consume(tgtok::r_paren)) {
    TokError("expected ')' in !foreach/!filter");
    return nullptr;
  }

  RecTy *OutType = InEltType;
  if (Operation == tgtok::XForEach && !IsDAG) {
    TypedInit *RHSt = dyn_cast<TypedInit>(RHS);
    if (!RHSt) {
      TokError("could not get type of !foreach result expression");
      return nullptr;
    }
    OutType = RHSt->getType()->getListTy();
  } else if (Operation == tgtok::XFilter) {
    OutType = InEltType->getListTy();
  }    

  return (TernOpInit::get((Operation == tgtok::XForEach) ? TernOpInit::FOREACH
                                                         : TernOpInit::FILTER,
                          LHS, MHS, RHS, OutType))
      ->Fold(CurRec);
}

Init *TGParser::ParseOperationCond(Record *CurRec, RecTy *ItemType) {
  Lex.Lex();  // eat the operation 'cond'

  if (!consume(tgtok::l_paren)) {
    TokError("expected '(' after !cond operator");
    return nullptr;
  }

  // Parse through '[Case: Val,]+'
  SmallVector<Init *, 4> Case;
  SmallVector<Init *, 4> Val;
  while (true) {
    if (consume(tgtok::r_paren))
      break;

    Init *V = ParseValue(CurRec);
    if (!V)
      return nullptr;
    Case.push_back(V);

    if (!consume(tgtok::colon)) {
      TokError("expected ':'  following a condition in !cond operator");
      return nullptr;
    }

    V = ParseValue(CurRec, ItemType);
    if (!V)
      return nullptr;
    Val.push_back(V);

    if (consume(tgtok::r_paren))
      break;

    if (!consume(tgtok::comma)) {
      TokError("expected ',' or ')' following a value in !cond operator");
      return nullptr;
    }
  }

  if (Case.size() < 1) {
    TokError("there should be at least 1 'condition : value' in the !cond operator");
    return nullptr;
  }

  // resolve type
  RecTy *Type = nullptr;
  for (Init *V : Val) {
    RecTy *VTy = nullptr;
    if (TypedInit *Vt = dyn_cast<TypedInit>(V))
      VTy = Vt->getType();
    if (BitsInit *Vbits = dyn_cast<BitsInit>(V))
      VTy = BitsRecTy::get(Records, Vbits->getNumBits());
    if (isa<BitInit>(V))
      VTy = BitRecTy::get(Records);

    if (Type == nullptr) {
      if (!isa<UnsetInit>(V))
        Type = VTy;
    } else {
      if (!isa<UnsetInit>(V)) {
        RecTy *RType = resolveTypes(Type, VTy);
        if (!RType) {
          TokError(Twine("inconsistent types '") + Type->getAsString() +
                         "' and '" + VTy->getAsString() + "' for !cond");
          return nullptr;
        }
        Type = RType;
      }
    }
  }

  if (!Type) {
    TokError("could not determine type for !cond from its arguments");
    return nullptr;
  }
  return CondOpInit::get(Case, Val, Type)->Fold(CurRec);
}

/// ParseSimpleValue - Parse a tblgen value.  This returns null on error.
///
///   SimpleValue ::= IDValue
///   SimpleValue ::= INTVAL
///   SimpleValue ::= STRVAL+
///   SimpleValue ::= CODEFRAGMENT
///   SimpleValue ::= '?'
///   SimpleValue ::= '{' ValueList '}'
///   SimpleValue ::= ID '<' ValueListNE '>'
///   SimpleValue ::= '[' ValueList ']'
///   SimpleValue ::= '(' IDValue DagArgList ')'
///   SimpleValue ::= CONCATTOK '(' Value ',' Value ')'
///   SimpleValue ::= ADDTOK '(' Value ',' Value ')'
///   SimpleValue ::= DIVTOK '(' Value ',' Value ')'
///   SimpleValue ::= SUBTOK '(' Value ',' Value ')'
///   SimpleValue ::= SHLTOK '(' Value ',' Value ')'
///   SimpleValue ::= SRATOK '(' Value ',' Value ')'
///   SimpleValue ::= SRLTOK '(' Value ',' Value ')'
///   SimpleValue ::= LISTCONCATTOK '(' Value ',' Value ')'
///   SimpleValue ::= LISTSPLATTOK '(' Value ',' Value ')'
///   SimpleValue ::= LISTREMOVETOK '(' Value ',' Value ')'
///   SimpleValue ::= RANGE '(' Value ')'
///   SimpleValue ::= RANGE '(' Value ',' Value ')'
///   SimpleValue ::= RANGE '(' Value ',' Value ',' Value ')'
///   SimpleValue ::= STRCONCATTOK '(' Value ',' Value ')'
///   SimpleValue ::= COND '(' [Value ':' Value,]+ ')'
///
Init *TGParser::ParseSimpleValue(Record *CurRec, RecTy *ItemType,
                                 IDParseMode Mode) {
  Init *R = nullptr;
  tgtok::TokKind Code = Lex.getCode();

  // Parse bang operators.
  if (tgtok::isBangOperator(Code))
    return ParseOperation(CurRec, ItemType);

  switch (Code) {
  default: TokError("Unknown or reserved token when parsing a value"); break;

  case tgtok::TrueVal:
    R = IntInit::get(Records, 1);
    Lex.Lex();
    break;
  case tgtok::FalseVal:
    R = IntInit::get(Records, 0);
    Lex.Lex();
    break;
  case tgtok::IntVal:
    R = IntInit::get(Records, Lex.getCurIntVal());
    Lex.Lex();
    break;
  case tgtok::BinaryIntVal: {
    auto BinaryVal = Lex.getCurBinaryIntVal();
    SmallVector<Init*, 16> Bits(BinaryVal.second);
    for (unsigned i = 0, e = BinaryVal.second; i != e; ++i)
      Bits[i] = BitInit::get(Records, BinaryVal.first & (1LL << i));
    R = BitsInit::get(Records, Bits);
    Lex.Lex();
    break;
  }
  case tgtok::StrVal: {
    std::string Val = Lex.getCurStrVal();
    Lex.Lex();

    // Handle multiple consecutive concatenated strings.
    while (Lex.getCode() == tgtok::StrVal) {
      Val += Lex.getCurStrVal();
      Lex.Lex();
    }

    R = StringInit::get(Records, Val);
    break;
  }
  case tgtok::CodeFragment:
    R = StringInit::get(Records, Lex.getCurStrVal(), StringInit::SF_Code);
    Lex.Lex();
    break;
  case tgtok::question:
    R = UnsetInit::get(Records);
    Lex.Lex();
    break;
  case tgtok::Id: {
    SMRange NameLoc = Lex.getLocRange();
    StringInit *Name = StringInit::get(Records, Lex.getCurStrVal());
    tgtok::TokKind Next = Lex.Lex();
    if (Next == tgtok::equal) // Named argument.
      return Name;
    if (Next != tgtok::less)                            // consume the Id.
      return ParseIDValue(CurRec, Name, NameLoc, Mode); // Value ::= IDValue

    // Value ::= CLASSID '<' ArgValueList '>' (CLASSID has been consumed)
    // This is supposed to synthesize a new anonymous definition, deriving
    // from the class with the template arguments, but no body.
    Record *Class = Records.getClass(Name->getValue());
    if (!Class) {
      Error(NameLoc.Start,
            "Expected a class name, got '" + Name->getValue() + "'");
      return nullptr;
    }

    SmallVector<ArgumentInit *, 8> Args;
    Lex.Lex(); // consume the <
    if (ParseTemplateArgValueList(Args, CurRec, Class))
      return nullptr; // Error parsing value list.

    if (CheckTemplateArgValues(Args, NameLoc.Start, Class))
      return nullptr; // Error checking template argument values.

    if (resolveArguments(Class, Args, NameLoc.Start))
      return nullptr;

    if (TrackReferenceLocs)
      Class->appendReferenceLoc(NameLoc);
    return VarDefInit::get(Class, Args)->Fold();
  }
  case tgtok::l_brace: {           // Value ::= '{' ValueList '}'
    SMLoc BraceLoc = Lex.getLoc();
    Lex.Lex(); // eat the '{'
    SmallVector<Init*, 16> Vals;

    if (Lex.getCode() != tgtok::r_brace) {
      ParseValueList(Vals, CurRec);
      if (Vals.empty()) return nullptr;
    }
    if (!consume(tgtok::r_brace)) {
      TokError("expected '}' at end of bit list value");
      return nullptr;
    }

    SmallVector<Init *, 16> NewBits;

    // As we parse { a, b, ... }, 'a' is the highest bit, but we parse it
    // first.  We'll first read everything in to a vector, then we can reverse
    // it to get the bits in the correct order for the BitsInit value.
    for (unsigned i = 0, e = Vals.size(); i != e; ++i) {
      // FIXME: The following two loops would not be duplicated
      //        if the API was a little more orthogonal.

      // bits<n> values are allowed to initialize n bits.
      if (BitsInit *BI = dyn_cast<BitsInit>(Vals[i])) {
        for (unsigned i = 0, e = BI->getNumBits(); i != e; ++i)
          NewBits.push_back(BI->getBit((e - i) - 1));
        continue;
      }
      // bits<n> can also come from variable initializers.
      if (VarInit *VI = dyn_cast<VarInit>(Vals[i])) {
        if (BitsRecTy *BitsRec = dyn_cast<BitsRecTy>(VI->getType())) {
          for (unsigned i = 0, e = BitsRec->getNumBits(); i != e; ++i)
            NewBits.push_back(VI->getBit((e - i) - 1));
          continue;
        }
        // Fallthrough to try convert this to a bit.
      }
      // All other values must be convertible to just a single bit.
      Init *Bit = Vals[i]->getCastTo(BitRecTy::get(Records));
      if (!Bit) {
        Error(BraceLoc, "Element #" + Twine(i) + " (" + Vals[i]->getAsString() +
              ") is not convertable to a bit");
        return nullptr;
      }
      NewBits.push_back(Bit);
    }
    std::reverse(NewBits.begin(), NewBits.end());
    return BitsInit::get(Records, NewBits);
  }
  case tgtok::l_square: {          // Value ::= '[' ValueList ']'
    Lex.Lex(); // eat the '['
    SmallVector<Init*, 16> Vals;

    RecTy *DeducedEltTy = nullptr;
    ListRecTy *GivenListTy = nullptr;

    if (ItemType) {
      ListRecTy *ListType = dyn_cast<ListRecTy>(ItemType);
      if (!ListType) {
        TokError(Twine("Encountered a list when expecting a ") +
                 ItemType->getAsString());
        return nullptr;
      }
      GivenListTy = ListType;
    }

    if (Lex.getCode() != tgtok::r_square) {
      ParseValueList(Vals, CurRec,
                     GivenListTy ? GivenListTy->getElementType() : nullptr);
      if (Vals.empty()) return nullptr;
    }
    if (!consume(tgtok::r_square)) {
      TokError("expected ']' at end of list value");
      return nullptr;
    }

    RecTy *GivenEltTy = nullptr;
    if (consume(tgtok::less)) {
      // Optional list element type
      GivenEltTy = ParseType();
      if (!GivenEltTy) {
        // Couldn't parse element type
        return nullptr;
      }

      if (!consume(tgtok::greater)) {
        TokError("expected '>' at end of list element type");
        return nullptr;
      }
    }

    // Check elements
    RecTy *EltTy = nullptr;
    for (Init *V : Vals) {
      TypedInit *TArg = dyn_cast<TypedInit>(V);
      if (TArg) {
        if (EltTy) {
          EltTy = resolveTypes(EltTy, TArg->getType());
          if (!EltTy) {
            TokError("Incompatible types in list elements");
            return nullptr;
          }
        } else {
          EltTy = TArg->getType();
        }
      }
    }

    if (GivenEltTy) {
      if (EltTy) {
        // Verify consistency
        if (!EltTy->typeIsConvertibleTo(GivenEltTy)) {
          TokError("Incompatible types in list elements");
          return nullptr;
        }
      }
      EltTy = GivenEltTy;
    }

    if (!EltTy) {
      if (!ItemType) {
        TokError("No type for list");
        return nullptr;
      }
      DeducedEltTy = GivenListTy->getElementType();
    } else {
      // Make sure the deduced type is compatible with the given type
      if (GivenListTy) {
        if (!EltTy->typeIsConvertibleTo(GivenListTy->getElementType())) {
          TokError(Twine("Element type mismatch for list: element type '") +
                   EltTy->getAsString() + "' not convertible to '" +
                   GivenListTy->getElementType()->getAsString());
          return nullptr;
        }
      }
      DeducedEltTy = EltTy;
    }

    return ListInit::get(Vals, DeducedEltTy);
  }
  case tgtok::l_paren: {         // Value ::= '(' IDValue DagArgList ')'
    Lex.Lex();   // eat the '('
    if (Lex.getCode() != tgtok::Id && Lex.getCode() != tgtok::XCast &&
        Lex.getCode() != tgtok::question && Lex.getCode() != tgtok::XGetDagOp) {
      TokError("expected identifier in dag init");
      return nullptr;
    }

    Init *Operator = ParseValue(CurRec);
    if (!Operator) return nullptr;

    // If the operator name is present, parse it.
    StringInit *OperatorName = nullptr;
    if (consume(tgtok::colon)) {
      if (Lex.getCode() != tgtok::VarName) { // eat the ':'
        TokError("expected variable name in dag operator");
        return nullptr;
      }
      OperatorName = StringInit::get(Records, Lex.getCurStrVal());
      Lex.Lex();  // eat the VarName.
    }

    SmallVector<std::pair<llvm::Init*, StringInit*>, 8> DagArgs;
    if (Lex.getCode() != tgtok::r_paren) {
      ParseDagArgList(DagArgs, CurRec);
      if (DagArgs.empty()) return nullptr;
    }

    if (!consume(tgtok::r_paren)) {
      TokError("expected ')' in dag init");
      return nullptr;
    }

    return DagInit::get(Operator, OperatorName, DagArgs);
  }
  }

  return R;
}

/// ParseValue - Parse a TableGen value. This returns null on error.
///
///   Value       ::= SimpleValue ValueSuffix*
///   ValueSuffix ::= '{' BitList '}'
///   ValueSuffix ::= '[' SliceElements ']'
///   ValueSuffix ::= '.' ID
///
Init *TGParser::ParseValue(Record *CurRec, RecTy *ItemType, IDParseMode Mode) {
  SMLoc LHSLoc = Lex.getLoc();
  Init *Result = ParseSimpleValue(CurRec, ItemType, Mode);
  if (!Result) return nullptr;

  // Parse the suffixes now if present.
  while (true) {
    switch (Lex.getCode()) {
    default: return Result;
    case tgtok::l_brace: {
      if (Mode == ParseNameMode)
        // This is the beginning of the object body.
        return Result;

      SMLoc CurlyLoc = Lex.getLoc();
      Lex.Lex(); // eat the '{'
      SmallVector<unsigned, 16> Ranges;
      ParseRangeList(Ranges);
      if (Ranges.empty()) return nullptr;

      // Reverse the bitlist.
      std::reverse(Ranges.begin(), Ranges.end());
      Result = Result->convertInitializerBitRange(Ranges);
      if (!Result) {
        Error(CurlyLoc, "Invalid bit range for value");
        return nullptr;
      }

      // Eat the '}'.
      if (!consume(tgtok::r_brace)) {
        TokError("expected '}' at end of bit range list");
        return nullptr;
      }
      break;
    }
    case tgtok::l_square: {
      auto *LHS = dyn_cast<TypedInit>(Result);
      if (!LHS) {
        Error(LHSLoc, "Invalid value, list expected");
        return nullptr;
      }

      auto *LHSTy = dyn_cast<ListRecTy>(LHS->getType());
      if (!LHSTy) {
        Error(LHSLoc, "Type '" + Twine(LHS->getType()->getAsString()) +
                          "' is invalid, list expected");
        return nullptr;
      }

      Lex.Lex(); // eat the '['
      TypedInit *RHS = ParseSliceElements(CurRec, /*Single=*/true);
      if (!RHS)
        return nullptr;

      if (isa<ListRecTy>(RHS->getType())) {
        Result =
            BinOpInit::get(BinOpInit::LISTSLICE, LHS, RHS, LHSTy)->Fold(CurRec);
      } else {
        Result = BinOpInit::get(BinOpInit::LISTELEM, LHS, RHS,
                                LHSTy->getElementType())
                     ->Fold(CurRec);
      }

      assert(Result);

      // Eat the ']'.
      if (!consume(tgtok::r_square)) {
        TokError("expected ']' at end of list slice");
        return nullptr;
      }
      break;
    }
    case tgtok::dot: {
      if (Lex.Lex() != tgtok::Id) { // eat the .
        TokError("expected field identifier after '.'");
        return nullptr;
      }
      SMRange FieldNameLoc = Lex.getLocRange();
      StringInit *FieldName = StringInit::get(Records, Lex.getCurStrVal());
      if (!Result->getFieldType(FieldName)) {
        TokError("Cannot access field '" + Lex.getCurStrVal() + "' of value '" +
                 Result->getAsString() + "'");
        return nullptr;
      }

      // Add a reference to this field if we know the record class.
      if (TrackReferenceLocs) {
        if (auto *DI = dyn_cast<DefInit>(Result)) {
          DI->getDef()->getValue(FieldName)->addReferenceLoc(FieldNameLoc);
        } else if (auto *TI = dyn_cast<TypedInit>(Result)) {
          if (auto *RecTy = dyn_cast<RecordRecTy>(TI->getType())) {
            for (Record *R : RecTy->getClasses())
              if (auto *RV = R->getValue(FieldName))
                RV->addReferenceLoc(FieldNameLoc);
          }
        }
      }

      Result = FieldInit::get(Result, FieldName)->Fold(CurRec);
      Lex.Lex();  // eat field name
      break;
    }

    case tgtok::paste:
      SMLoc PasteLoc = Lex.getLoc();
      TypedInit *LHS = dyn_cast<TypedInit>(Result);
      if (!LHS) {
        Error(PasteLoc, "LHS of paste is not typed!");
        return nullptr;
      }

      // Check if it's a 'listA # listB'
      if (isa<ListRecTy>(LHS->getType())) {
        Lex.Lex();  // Eat the '#'.

        assert(Mode == ParseValueMode && "encountered paste of lists in name");

        switch (Lex.getCode()) {
        case tgtok::colon:
        case tgtok::semi:
        case tgtok::l_brace:
          Result = LHS; // trailing paste, ignore.
          break;
        default:
          Init *RHSResult = ParseValue(CurRec, ItemType, ParseValueMode);
          if (!RHSResult)
            return nullptr;
          Result = BinOpInit::getListConcat(LHS, RHSResult);
          break;
        }
        break;
      }

      // Create a !strconcat() operation, first casting each operand to
      // a string if necessary.
      if (LHS->getType() != StringRecTy::get(Records)) {
        auto CastLHS = dyn_cast<TypedInit>(
            UnOpInit::get(UnOpInit::CAST, LHS, StringRecTy::get(Records))
                ->Fold(CurRec));
        if (!CastLHS) {
          Error(PasteLoc,
                Twine("can't cast '") + LHS->getAsString() + "' to string");
          return nullptr;
        }
        LHS = CastLHS;
      }

      TypedInit *RHS = nullptr;

      Lex.Lex();  // Eat the '#'.
      switch (Lex.getCode()) {
      case tgtok::colon:
      case tgtok::semi:
      case tgtok::l_brace:
        // These are all of the tokens that can begin an object body.
        // Some of these can also begin values but we disallow those cases
        // because they are unlikely to be useful.

        // Trailing paste, concat with an empty string.
        RHS = StringInit::get(Records, "");
        break;

      default:
        Init *RHSResult = ParseValue(CurRec, nullptr, ParseNameMode);
        if (!RHSResult)
          return nullptr;
        RHS = dyn_cast<TypedInit>(RHSResult);
        if (!RHS) {
          Error(PasteLoc, "RHS of paste is not typed!");
          return nullptr;
        }

        if (RHS->getType() != StringRecTy::get(Records)) {
          auto CastRHS = dyn_cast<TypedInit>(
              UnOpInit::get(UnOpInit::CAST, RHS, StringRecTy::get(Records))
                  ->Fold(CurRec));
          if (!CastRHS) {
            Error(PasteLoc,
                  Twine("can't cast '") + RHS->getAsString() + "' to string");
            return nullptr;
          }
          RHS = CastRHS;
        }

        break;
      }

      Result = BinOpInit::getStrConcat(LHS, RHS);
      break;
    }
  }
}

/// ParseDagArgList - Parse the argument list for a dag literal expression.
///
///    DagArg     ::= Value (':' VARNAME)?
///    DagArg     ::= VARNAME
///    DagArgList ::= DagArg
///    DagArgList ::= DagArgList ',' DagArg
void TGParser::ParseDagArgList(
    SmallVectorImpl<std::pair<llvm::Init*, StringInit*>> &Result,
    Record *CurRec) {

  while (true) {
    // DagArg ::= VARNAME
    if (Lex.getCode() == tgtok::VarName) {
      // A missing value is treated like '?'.
      StringInit *VarName = StringInit::get(Records, Lex.getCurStrVal());
      Result.emplace_back(UnsetInit::get(Records), VarName);
      Lex.Lex();
    } else {
      // DagArg ::= Value (':' VARNAME)?
      Init *Val = ParseValue(CurRec);
      if (!Val) {
        Result.clear();
        return;
      }

      // If the variable name is present, add it.
      StringInit *VarName = nullptr;
      if (Lex.getCode() == tgtok::colon) {
        if (Lex.Lex() != tgtok::VarName) { // eat the ':'
          TokError("expected variable name in dag literal");
          Result.clear();
          return;
        }
        VarName = StringInit::get(Records, Lex.getCurStrVal());
        Lex.Lex();  // eat the VarName.
      }

      Result.push_back(std::make_pair(Val, VarName));
    }
    if (!consume(tgtok::comma))
      break;
  }
}

/// ParseValueList - Parse a comma separated list of values, returning them
/// in a vector. Note that this always expects to be able to parse at least one
/// value. It returns an empty list if this is not possible.
///
///   ValueList ::= Value (',' Value)
///
void TGParser::ParseValueList(SmallVectorImpl<Init *> &Result, Record *CurRec,
                              RecTy *ItemType) {

  Result.push_back(ParseValue(CurRec, ItemType));
  if (!Result.back()) {
    Result.clear();
    return;
  }

  while (consume(tgtok::comma)) {
    // ignore trailing comma for lists
    if (Lex.getCode() == tgtok::r_square)
      return;
    Result.push_back(ParseValue(CurRec, ItemType));
    if (!Result.back()) {
      Result.clear();
      return;
    }
  }
}

// ParseTemplateArgValueList - Parse a template argument list with the syntax
// shown, filling in the Result vector. The open angle has been consumed.
// An empty argument list is allowed. Return false if okay, true if an
// error was detected.
//
//   ArgValueList ::= '<' PostionalArgValueList [','] NamedArgValueList '>'
//   PostionalArgValueList ::= [Value {',' Value}*]
//   NamedArgValueList ::= [NameValue '=' Value {',' NameValue '=' Value}*]
bool TGParser::ParseTemplateArgValueList(
    SmallVectorImpl<ArgumentInit *> &Result, Record *CurRec, Record *ArgsRec) {
  assert(Result.empty() && "Result vector is not empty");
  ArrayRef<Init *> TArgs = ArgsRec->getTemplateArgs();

  if (consume(tgtok::greater)) // empty value list
    return false;

  bool HasNamedArg = false;
  unsigned ArgIndex = 0;
  while (true) {
    if (ArgIndex >= TArgs.size()) {
      TokError("Too many template arguments: " + utostr(ArgIndex + 1));
      return true;
    }

    SMLoc ValueLoc = Lex.getLoc();
    // If we are parsing named argument, we don't need to know the argument name
    // and argument type will be resolved after we know the name.
    Init *Value = ParseValue(
        CurRec,
        HasNamedArg ? nullptr : ArgsRec->getValue(TArgs[ArgIndex])->getType());
    if (!Value)
      return true;

    // If we meet '=', then we are parsing named arguments.
    if (Lex.getCode() == tgtok::equal) {
      if (!isa<StringInit>(Value))
        return Error(ValueLoc,
                     "The name of named argument should be a valid identifier");

      auto *Name = cast<StringInit>(Value);
      Init *QualifiedName = QualifyName(*ArgsRec, Name);
      auto *NamedArg = ArgsRec->getValue(QualifiedName);
      if (!NamedArg)
        return Error(ValueLoc,
                     "Argument " + Name->getAsString() + " doesn't exist");

      Lex.Lex(); // eat the '='.
      ValueLoc = Lex.getLoc();
      Value = ParseValue(CurRec, NamedArg->getType());
      // Named value can't be uninitialized.
      if (isa<UnsetInit>(Value))
        return Error(ValueLoc,
                     "The value of named argument should be initialized, "
                     "but we got '" +
                         Value->getAsString() + "'");

      Result.push_back(ArgumentInit::get(Value, QualifiedName));
      HasNamedArg = true;
    } else {
      // Positional arguments should be put before named arguments.
      if (HasNamedArg)
        return Error(ValueLoc,
                     "Positional argument should be put before named argument");

      Result.push_back(ArgumentInit::get(Value, ArgIndex));
    }

    if (consume(tgtok::greater)) // end of argument list?
      return false;
    if (!consume(tgtok::comma))
      return TokError("Expected comma before next argument");
    ++ArgIndex;
  }
}

/// ParseDeclaration - Read a declaration, returning the name of field ID, or an
/// empty string on error.  This can happen in a number of different contexts,
/// including within a def or in the template args for a class (in which case
/// CurRec will be non-null) and within the template args for a multiclass (in
/// which case CurRec will be null, but CurMultiClass will be set).  This can
/// also happen within a def that is within a multiclass, which will set both
/// CurRec and CurMultiClass.
///
///  Declaration ::= FIELD? Type ID ('=' Value)?
///
Init *TGParser::ParseDeclaration(Record *CurRec,
                                       bool ParsingTemplateArgs) {
  // Read the field prefix if present.
  bool HasField = consume(tgtok::Field);

  RecTy *Type = ParseType();
  if (!Type) return nullptr;

  if (Lex.getCode() != tgtok::Id) {
    TokError("Expected identifier in declaration");
    return nullptr;
  }

  std::string Str = Lex.getCurStrVal();
  if (Str == "NAME") {
    TokError("'" + Str + "' is a reserved variable name");
    return nullptr;
  }

  if (!ParsingTemplateArgs && CurScope->varAlreadyDefined(Str)) {
    TokError("local variable of this name already exists");
    return nullptr;
  }

  SMLoc IdLoc = Lex.getLoc();
  Init *DeclName = StringInit::get(Records, Str);
  Lex.Lex();

  bool BadField;
  if (!ParsingTemplateArgs) { // def, possibly in a multiclass
    BadField = AddValue(CurRec, IdLoc,
                        RecordVal(DeclName, IdLoc, Type,
                                  HasField ? RecordVal::FK_NonconcreteOK
                                           : RecordVal::FK_Normal));
  } else if (CurRec) { // class template argument
    DeclName = QualifyName(*CurRec, DeclName);
    BadField =
        AddValue(CurRec, IdLoc,
                 RecordVal(DeclName, IdLoc, Type, RecordVal::FK_TemplateArg));
  } else { // multiclass template argument
    assert(CurMultiClass && "invalid context for template argument");
    DeclName = QualifyName(CurMultiClass, DeclName);
    BadField =
        AddValue(CurRec, IdLoc,
                 RecordVal(DeclName, IdLoc, Type, RecordVal::FK_TemplateArg));
  }
  if (BadField)
    return nullptr;

  // If a value is present, parse it and set new field's value.
  if (consume(tgtok::equal)) {
    SMLoc ValLoc = Lex.getLoc();
    Init *Val = ParseValue(CurRec, Type);
    if (!Val ||
        SetValue(CurRec, ValLoc, DeclName, std::nullopt, Val,
                 /*AllowSelfAssignment=*/false, /*OverrideDefLoc=*/false)) {
      // Return the name, even if an error is thrown.  This is so that we can
      // continue to make some progress, even without the value having been
      // initialized.
      return DeclName;
    }
  }

  return DeclName;
}

/// ParseForeachDeclaration - Read a foreach declaration, returning
/// the name of the declared object or a NULL Init on error.  Return
/// the name of the parsed initializer list through ForeachListName.
///
///  ForeachDeclaration ::= ID '=' '{' RangeList '}'
///  ForeachDeclaration ::= ID '=' RangePiece
///  ForeachDeclaration ::= ID '=' Value
///
VarInit *TGParser::ParseForeachDeclaration(Init *&ForeachListValue) {
  if (Lex.getCode() != tgtok::Id) {
    TokError("Expected identifier in foreach declaration");
    return nullptr;
  }

  Init *DeclName = StringInit::get(Records, Lex.getCurStrVal());
  Lex.Lex();

  // If a value is present, parse it.
  if (!consume(tgtok::equal)) {
    TokError("Expected '=' in foreach declaration");
    return nullptr;
  }

  RecTy *IterType = nullptr;
  SmallVector<unsigned, 16> Ranges;

  switch (Lex.getCode()) {
  case tgtok::l_brace: { // '{' RangeList '}'
    Lex.Lex(); // eat the '{'
    ParseRangeList(Ranges);
    if (!consume(tgtok::r_brace)) {
      TokError("expected '}' at end of bit range list");
      return nullptr;
    }
    break;
  }

  default: {
    SMLoc ValueLoc = Lex.getLoc();
    Init *I = ParseValue(nullptr);
    if (!I)
      return nullptr;

    TypedInit *TI = dyn_cast<TypedInit>(I);
    if (TI && isa<ListRecTy>(TI->getType())) {
      ForeachListValue = I;
      IterType = cast<ListRecTy>(TI->getType())->getElementType();
      break;
    }

    if (TI) {
      if (ParseRangePiece(Ranges, TI))
        return nullptr;
      break;
    }

    Error(ValueLoc, "expected a list, got '" + I->getAsString() + "'");
    if (CurMultiClass) {
      PrintNote({}, "references to multiclass template arguments cannot be "
                "resolved at this time");
    }
    return nullptr;
  }
  }


  if (!Ranges.empty()) {
    assert(!IterType && "Type already initialized?");
    IterType = IntRecTy::get(Records);
    std::vector<Init *> Values;
    for (unsigned R : Ranges)
      Values.push_back(IntInit::get(Records, R));
    ForeachListValue = ListInit::get(Values, IterType);
  }

  if (!IterType)
    return nullptr;

  return VarInit::get(DeclName, IterType);
}

/// ParseTemplateArgList - Read a template argument list, which is a non-empty
/// sequence of template-declarations in <>'s.  If CurRec is non-null, these are
/// template args for a class. If null, these are the template args for a
/// multiclass.
///
///    TemplateArgList ::= '<' Declaration (',' Declaration)* '>'
///
bool TGParser::ParseTemplateArgList(Record *CurRec) {
  assert(Lex.getCode() == tgtok::less && "Not a template arg list!");
  Lex.Lex(); // eat the '<'

  Record *TheRecToAddTo = CurRec ? CurRec : &CurMultiClass->Rec;

  // Read the first declaration.
  Init *TemplArg = ParseDeclaration(CurRec, true/*templateargs*/);
  if (!TemplArg)
    return true;

  TheRecToAddTo->addTemplateArg(TemplArg);

  while (consume(tgtok::comma)) {
    // Read the following declarations.
    SMLoc Loc = Lex.getLoc();
    TemplArg = ParseDeclaration(CurRec, true/*templateargs*/);
    if (!TemplArg)
      return true;

    if (TheRecToAddTo->isTemplateArg(TemplArg))
      return Error(Loc, "template argument with the same name has already been "
                        "defined");

    TheRecToAddTo->addTemplateArg(TemplArg);
  }

  if (!consume(tgtok::greater))
    return TokError("expected '>' at end of template argument list");
  return false;
}

/// ParseBodyItem - Parse a single item within the body of a def or class.
///
///   BodyItem ::= Declaration ';'
///   BodyItem ::= LET ID OptionalBitList '=' Value ';'
///   BodyItem ::= Defvar
///   BodyItem ::= Dump
///   BodyItem ::= Assert
///
bool TGParser::ParseBodyItem(Record *CurRec) {
  if (Lex.getCode() == tgtok::Assert)
    return ParseAssert(nullptr, CurRec);

  if (Lex.getCode() == tgtok::Defvar)
    return ParseDefvar(CurRec);

  if (Lex.getCode() == tgtok::Dump)
    return ParseDump(nullptr, CurRec);

  if (Lex.getCode() != tgtok::Let) {
    if (!ParseDeclaration(CurRec, false))
      return true;

    if (!consume(tgtok::semi))
      return TokError("expected ';' after declaration");
    return false;
  }

  // LET ID OptionalRangeList '=' Value ';'
  if (Lex.Lex() != tgtok::Id)
    return TokError("expected field identifier after let");

  SMLoc IdLoc = Lex.getLoc();
  StringInit *FieldName = StringInit::get(Records, Lex.getCurStrVal());
  Lex.Lex();  // eat the field name.

  SmallVector<unsigned, 16> BitList;
  if (ParseOptionalBitList(BitList))
    return true;
  std::reverse(BitList.begin(), BitList.end());

  if (!consume(tgtok::equal))
    return TokError("expected '=' in let expression");

  RecordVal *Field = CurRec->getValue(FieldName);
  if (!Field)
    return TokError("Value '" + FieldName->getValue() + "' unknown!");

  RecTy *Type = Field->getType();
  if (!BitList.empty() && isa<BitsRecTy>(Type)) {
    // When assigning to a subset of a 'bits' object, expect the RHS to have
    // the type of that subset instead of the type of the whole object.
    Type = BitsRecTy::get(Records, BitList.size());
  }

  Init *Val = ParseValue(CurRec, Type);
  if (!Val) return true;

  if (!consume(tgtok::semi))
    return TokError("expected ';' after let expression");

  return SetValue(CurRec, IdLoc, FieldName, BitList, Val);
}

/// ParseBody - Read the body of a class or def.  Return true on error, false on
/// success.
///
///   Body     ::= ';'
///   Body     ::= '{' BodyList '}'
///   BodyList BodyItem*
///
bool TGParser::ParseBody(Record *CurRec) {
  // If this is a null definition, just eat the semi and return.
  if (consume(tgtok::semi))
    return false;

  if (!consume(tgtok::l_brace))
    return TokError("Expected '{' to start body or ';' for declaration only");

  while (Lex.getCode() != tgtok::r_brace)
    if (ParseBodyItem(CurRec))
      return true;

  // Eat the '}'.
  Lex.Lex();

  // If we have a semicolon, print a gentle error.
  SMLoc SemiLoc = Lex.getLoc();
  if (consume(tgtok::semi)) {
    PrintError(SemiLoc, "A class or def body should not end with a semicolon");
    PrintNote("Semicolon ignored; remove to eliminate this error");    
  }

  return false;
}

/// Apply the current let bindings to \a CurRec.
/// \returns true on error, false otherwise.
bool TGParser::ApplyLetStack(Record *CurRec) {
  for (SmallVectorImpl<LetRecord> &LetInfo : LetStack)
    for (LetRecord &LR : LetInfo)
      if (SetValue(CurRec, LR.Loc, LR.Name, LR.Bits, LR.Value))
        return true;
  return false;
}

/// Apply the current let bindings to the RecordsEntry.
bool TGParser::ApplyLetStack(RecordsEntry &Entry) {
  if (Entry.Rec)
    return ApplyLetStack(Entry.Rec.get());

  // Let bindings are not applied to assertions.
  if (Entry.Assertion)
    return false;

  // Let bindings are not applied to dumps.
  if (Entry.Dump)
    return false;

  for (auto &E : Entry.Loop->Entries) {
    if (ApplyLetStack(E))
      return true;
  }

  return false;
}

/// ParseObjectBody - Parse the body of a def or class.  This consists of an
/// optional ClassList followed by a Body.  CurRec is the current def or class
/// that is being parsed.
///
///   ObjectBody      ::= BaseClassList Body
///   BaseClassList   ::= /*empty*/
///   BaseClassList   ::= ':' BaseClassListNE
///   BaseClassListNE ::= SubClassRef (',' SubClassRef)*
///
bool TGParser::ParseObjectBody(Record *CurRec) {
  // An object body introduces a new scope for local variables.
  TGVarScope *ObjectScope = PushScope(CurRec);
  // If there is a baseclass list, read it.
  if (consume(tgtok::colon)) {

    // Read all of the subclasses.
    SubClassReference SubClass = ParseSubClassReference(CurRec, false);
    while (true) {
      // Check for error.
      if (!SubClass.Rec) return true;

      // Add it.
      if (AddSubClass(CurRec, SubClass))
        return true;

      if (!consume(tgtok::comma))
        break;
      SubClass = ParseSubClassReference(CurRec, false);
    }
  }

  if (ApplyLetStack(CurRec))
    return true;

  bool Result = ParseBody(CurRec);
  PopScope(ObjectScope);
  return Result;
}

/// ParseDef - Parse and return a top level or multiclass record definition.
/// Return false if okay, true if error.
///
///   DefInst ::= DEF ObjectName ObjectBody
///
bool TGParser::ParseDef(MultiClass *CurMultiClass) {
  SMLoc DefLoc = Lex.getLoc();
  assert(Lex.getCode() == tgtok::Def && "Unknown tok");
  Lex.Lex();  // Eat the 'def' token.

  // If the name of the def is an Id token, use that for the location.
  // Otherwise, the name is more complex and we use the location of the 'def'
  // token.
  SMLoc NameLoc = Lex.getCode() == tgtok::Id ? Lex.getLoc() : DefLoc;

  // Parse ObjectName and make a record for it.
  std::unique_ptr<Record> CurRec;
  Init *Name = ParseObjectName(CurMultiClass);
  if (!Name)
    return true;

  if (isa<UnsetInit>(Name)) {
    CurRec = std::make_unique<Record>(Records.getNewAnonymousName(), DefLoc,
                                      Records, Record::RK_AnonymousDef);
  } else {
    CurRec = std::make_unique<Record>(Name, NameLoc, Records);
  }

  if (ParseObjectBody(CurRec.get()))
    return true;

  return addEntry(std::move(CurRec));
}

/// ParseDefset - Parse a defset statement.
///
///   Defset ::= DEFSET Type Id '=' '{' ObjectList '}'
///
bool TGParser::ParseDefset() {
  assert(Lex.getCode() == tgtok::Defset);
  Lex.Lex(); // Eat the 'defset' token

  DefsetRecord Defset;
  Defset.Loc = Lex.getLoc();
  RecTy *Type = ParseType();
  if (!Type)
    return true;
  if (!isa<ListRecTy>(Type))
    return Error(Defset.Loc, "expected list type");
  Defset.EltTy = cast<ListRecTy>(Type)->getElementType();

  if (Lex.getCode() != tgtok::Id)
    return TokError("expected identifier");
  StringInit *DeclName = StringInit::get(Records, Lex.getCurStrVal());
  if (Records.getGlobal(DeclName->getValue()))
    return TokError("def or global variable of this name already exists");

  if (Lex.Lex() != tgtok::equal) // Eat the identifier
    return TokError("expected '='");
  if (Lex.Lex() != tgtok::l_brace) // Eat the '='
    return TokError("expected '{'");
  SMLoc BraceLoc = Lex.getLoc();
  Lex.Lex(); // Eat the '{'

  Defsets.push_back(&Defset);
  bool Err = ParseObjectList(nullptr);
  Defsets.pop_back();
  if (Err)
    return true;

  if (!consume(tgtok::r_brace)) {
    TokError("expected '}' at end of defset");
    return Error(BraceLoc, "to match this '{'");
  }

  Records.addExtraGlobal(DeclName->getValue(),
                         ListInit::get(Defset.Elements, Defset.EltTy));
  return false;
}

/// ParseDeftype - Parse a defvar statement.
///
///   Deftype ::= DEFTYPE Id '=' Type ';'
///
bool TGParser::ParseDeftype() {
  assert(Lex.getCode() == tgtok::Deftype);
  Lex.Lex(); // Eat the 'deftype' token

  if (Lex.getCode() != tgtok::Id)
    return TokError("expected identifier");

  const std::string TypeName = Lex.getCurStrVal();
  if (TypeAliases.count(TypeName) || Records.getClass(TypeName))
    return TokError("type of this name '" + TypeName + "' already exists");

  Lex.Lex();
  if (!consume(tgtok::equal))
    return TokError("expected '='");

  SMLoc Loc = Lex.getLoc();
  RecTy *Type = ParseType();
  if (!Type)
    return true;

  if (Type->getRecTyKind() == RecTy::RecordRecTyKind)
    return Error(Loc, "cannot define type alias for class type '" +
                          Type->getAsString() + "'");

  TypeAliases[TypeName] = Type;

  if (!consume(tgtok::semi))
    return TokError("expected ';'");

  return false;
}

/// ParseDefvar - Parse a defvar statement.
///
///   Defvar ::= DEFVAR Id '=' Value ';'
///
bool TGParser::ParseDefvar(Record *CurRec) {
  assert(Lex.getCode() == tgtok::Defvar);
  Lex.Lex(); // Eat the 'defvar' token

  if (Lex.getCode() != tgtok::Id)
    return TokError("expected identifier");
  StringInit *DeclName = StringInit::get(Records, Lex.getCurStrVal());
  if (CurScope->varAlreadyDefined(DeclName->getValue()))
    return TokError("local variable of this name already exists");

  // The name should not be conflicted with existed field names.
  if (CurRec) {
    auto *V = CurRec->getValue(DeclName->getValue());
    if (V && !V->isTemplateArg())
      return TokError("field of this name already exists");
  }

  // If this defvar is in the top level, the name should not be conflicted
  // with existed global names.
  if (CurScope->isOutermost() && Records.getGlobal(DeclName->getValue()))
    return TokError("def or global variable of this name already exists");

  Lex.Lex();
  if (!consume(tgtok::equal))
    return TokError("expected '='");

  Init *Value = ParseValue(CurRec);
  if (!Value)
    return true;

  if (!consume(tgtok::semi))
    return TokError("expected ';'");

  if (!CurScope->isOutermost())
    CurScope->addVar(DeclName->getValue(), Value);
  else
    Records.addExtraGlobal(DeclName->getValue(), Value);

  return false;
}

/// ParseForeach - Parse a for statement.  Return the record corresponding
/// to it.  This returns true on error.
///
///   Foreach ::= FOREACH Declaration IN '{ ObjectList '}'
///   Foreach ::= FOREACH Declaration IN Object
///
bool TGParser::ParseForeach(MultiClass *CurMultiClass) {
  SMLoc Loc = Lex.getLoc();
  assert(Lex.getCode() == tgtok::Foreach && "Unknown tok");
  Lex.Lex();  // Eat the 'for' token.

  // Make a temporary object to record items associated with the for
  // loop.
  Init *ListValue = nullptr;
  VarInit *IterName = ParseForeachDeclaration(ListValue);
  if (!IterName)
    return TokError("expected declaration in for");

  if (!consume(tgtok::In))
    return TokError("Unknown tok");

  // Create a loop object and remember it.
  auto TheLoop = std::make_unique<ForeachLoop>(Loc, IterName, ListValue);
  // A foreach loop introduces a new scope for local variables.
  TGVarScope *ForeachScope = PushScope(TheLoop.get());
  Loops.push_back(std::move(TheLoop));

  if (Lex.getCode() != tgtok::l_brace) {
    // FOREACH Declaration IN Object
    if (ParseObject(CurMultiClass))
      return true;
  } else {
    SMLoc BraceLoc = Lex.getLoc();
    // Otherwise, this is a group foreach.
    Lex.Lex();  // eat the '{'.

    // Parse the object list.
    if (ParseObjectList(CurMultiClass))
      return true;

    if (!consume(tgtok::r_brace)) {
      TokError("expected '}' at end of foreach command");
      return Error(BraceLoc, "to match this '{'");
    }
  }

  PopScope(ForeachScope);

  // Resolve the loop or store it for later resolution.
  std::unique_ptr<ForeachLoop> Loop = std::move(Loops.back());
  Loops.pop_back();

  return addEntry(std::move(Loop));
}

/// ParseIf - Parse an if statement.
///
///   If ::= IF Value THEN IfBody
///   If ::= IF Value THEN IfBody ELSE IfBody
///
bool TGParser::ParseIf(MultiClass *CurMultiClass) {
  SMLoc Loc = Lex.getLoc();
  assert(Lex.getCode() == tgtok::If && "Unknown tok");
  Lex.Lex(); // Eat the 'if' token.

  // Make a temporary object to record items associated with the for
  // loop.
  Init *Condition = ParseValue(nullptr);
  if (!Condition)
    return true;

  if (!consume(tgtok::Then))
    return TokError("Unknown tok");

  // We have to be able to save if statements to execute later, and they have
  // to live on the same stack as foreach loops. The simplest implementation
  // technique is to convert each 'then' or 'else' clause *into* a foreach
  // loop, over a list of length 0 or 1 depending on the condition, and with no
  // iteration variable being assigned.

  ListInit *EmptyList = ListInit::get({}, BitRecTy::get(Records));
  ListInit *SingletonList =
      ListInit::get({BitInit::get(Records, true)}, BitRecTy::get(Records));
  RecTy *BitListTy = ListRecTy::get(BitRecTy::get(Records));

  // The foreach containing the then-clause selects SingletonList if
  // the condition is true.
  Init *ThenClauseList =
      TernOpInit::get(TernOpInit::IF, Condition, SingletonList, EmptyList,
                      BitListTy)
          ->Fold(nullptr);
  Loops.push_back(std::make_unique<ForeachLoop>(Loc, nullptr, ThenClauseList));

  if (ParseIfBody(CurMultiClass, "then"))
    return true;

  std::unique_ptr<ForeachLoop> Loop = std::move(Loops.back());
  Loops.pop_back();

  if (addEntry(std::move(Loop)))
    return true;

  // Now look for an optional else clause. The if-else syntax has the usual
  // dangling-else ambiguity, and by greedily matching an else here if we can,
  // we implement the usual resolution of pairing with the innermost unmatched
  // if.
  if (consume(tgtok::ElseKW)) {
    // The foreach containing the else-clause uses the same pair of lists as
    // above, but this time, selects SingletonList if the condition is *false*.
    Init *ElseClauseList =
        TernOpInit::get(TernOpInit::IF, Condition, EmptyList, SingletonList,
                        BitListTy)
            ->Fold(nullptr);
    Loops.push_back(
        std::make_unique<ForeachLoop>(Loc, nullptr, ElseClauseList));

    if (ParseIfBody(CurMultiClass, "else"))
      return true;

    Loop = std::move(Loops.back());
    Loops.pop_back();

    if (addEntry(std::move(Loop)))
      return true;
  }

  return false;
}

/// ParseIfBody - Parse the then-clause or else-clause of an if statement.
///
///   IfBody ::= Object
///   IfBody ::= '{' ObjectList '}'
///
bool TGParser::ParseIfBody(MultiClass *CurMultiClass, StringRef Kind) {
  // An if-statement introduces a new scope for local variables.
  TGVarScope *BodyScope = PushScope();

  if (Lex.getCode() != tgtok::l_brace) {
    // A single object.
    if (ParseObject(CurMultiClass))
      return true;
  } else {
    SMLoc BraceLoc = Lex.getLoc();
    // A braced block.
    Lex.Lex(); // eat the '{'.

    // Parse the object list.
    if (ParseObjectList(CurMultiClass))
      return true;

    if (!consume(tgtok::r_brace)) {
      TokError("expected '}' at end of '" + Kind + "' clause");
      return Error(BraceLoc, "to match this '{'");
    }
  }

  PopScope(BodyScope);
  return false;
}

/// ParseAssert - Parse an assert statement.
///
///   Assert ::= ASSERT condition , message ;
bool TGParser::ParseAssert(MultiClass *CurMultiClass, Record *CurRec) {
  assert(Lex.getCode() == tgtok::Assert && "Unknown tok");
  Lex.Lex(); // Eat the 'assert' token.

  SMLoc ConditionLoc = Lex.getLoc();
  Init *Condition = ParseValue(CurRec);
  if (!Condition)
    return true;

  if (!consume(tgtok::comma)) {
    TokError("expected ',' in assert statement");
    return true;
  }

  Init *Message = ParseValue(CurRec);
  if (!Message)
    return true;

  if (!consume(tgtok::semi))
    return TokError("expected ';'");

  if (CurRec)
    CurRec->addAssertion(ConditionLoc, Condition, Message);
  else
    addEntry(std::make_unique<Record::AssertionInfo>(ConditionLoc, Condition,
                                                     Message));
  return false;
}

/// ParseClass - Parse a tblgen class definition.
///
///   ClassInst ::= CLASS ID TemplateArgList? ObjectBody
///
bool TGParser::ParseClass() {
  assert(Lex.getCode() == tgtok::Class && "Unexpected token!");
  Lex.Lex();

  if (Lex.getCode() != tgtok::Id)
    return TokError("expected class name after 'class' keyword");

  const std::string &Name = Lex.getCurStrVal();
  Record *CurRec = Records.getClass(Name);
  if (CurRec) {
    // If the body was previously defined, this is an error.
    if (!CurRec->getValues().empty() ||
        !CurRec->getSuperClasses().empty() ||
        !CurRec->getTemplateArgs().empty())
      return TokError("Class '" + CurRec->getNameInitAsString() +
                      "' already defined");

    CurRec->updateClassLoc(Lex.getLoc());
  } else {
    // If this is the first reference to this class, create and add it.
    auto NewRec = std::make_unique<Record>(Lex.getCurStrVal(), Lex.getLoc(),
                                           Records, Record::RK_Class);
    CurRec = NewRec.get();
    Records.addClass(std::move(NewRec));
  }

  if (TypeAliases.count(Name))
    return TokError("there is already a defined type alias '" + Name + "'");

  Lex.Lex(); // eat the name.

  // A class definition introduces a new scope.
  TGVarScope *ClassScope = PushScope(CurRec);
  // If there are template args, parse them.
  if (Lex.getCode() == tgtok::less)
    if (ParseTemplateArgList(CurRec))
      return true;

  if (ParseObjectBody(CurRec))
    return true;

  if (!NoWarnOnUnusedTemplateArgs)
    CurRec->checkUnusedTemplateArgs();

  PopScope(ClassScope);
  return false;
}

/// ParseLetList - Parse a non-empty list of assignment expressions into a list
/// of LetRecords.
///
///   LetList ::= LetItem (',' LetItem)*
///   LetItem ::= ID OptionalRangeList '=' Value
///
void TGParser::ParseLetList(SmallVectorImpl<LetRecord> &Result) {
  do {
    if (Lex.getCode() != tgtok::Id) {
      TokError("expected identifier in let definition");
      Result.clear();
      return;
    }

    StringInit *Name = StringInit::get(Records, Lex.getCurStrVal());
    SMLoc NameLoc = Lex.getLoc();
    Lex.Lex();  // Eat the identifier.

    // Check for an optional RangeList.
    SmallVector<unsigned, 16> Bits;
    if (ParseOptionalRangeList(Bits)) {
      Result.clear();
      return;
    }
    std::reverse(Bits.begin(), Bits.end());

    if (!consume(tgtok::equal)) {
      TokError("expected '=' in let expression");
      Result.clear();
      return;
    }

    Init *Val = ParseValue(nullptr);
    if (!Val) {
      Result.clear();
      return;
    }

    // Now that we have everything, add the record.
    Result.emplace_back(Name, Bits, Val, NameLoc);
  } while (consume(tgtok::comma));
}

/// ParseTopLevelLet - Parse a 'let' at top level.  This can be a couple of
/// different related productions. This works inside multiclasses too.
///
///   Object ::= LET LetList IN '{' ObjectList '}'
///   Object ::= LET LetList IN Object
///
bool TGParser::ParseTopLevelLet(MultiClass *CurMultiClass) {
  assert(Lex.getCode() == tgtok::Let && "Unexpected token");
  Lex.Lex();

  // Add this entry to the let stack.
  SmallVector<LetRecord, 8> LetInfo;
  ParseLetList(LetInfo);
  if (LetInfo.empty()) return true;
  LetStack.push_back(std::move(LetInfo));

  if (!consume(tgtok::In))
    return TokError("expected 'in' at end of top-level 'let'");

  // If this is a scalar let, just handle it now
  if (Lex.getCode() != tgtok::l_brace) {
    // LET LetList IN Object
    if (ParseObject(CurMultiClass))
      return true;
  } else {   // Object ::= LETCommand '{' ObjectList '}'
    SMLoc BraceLoc = Lex.getLoc();
    // Otherwise, this is a group let.
    Lex.Lex();  // eat the '{'.

    // A group let introduces a new scope for local variables.
    TGVarScope *LetScope = PushScope();

    // Parse the object list.
    if (ParseObjectList(CurMultiClass))
      return true;

    if (!consume(tgtok::r_brace)) {
      TokError("expected '}' at end of top level let command");
      return Error(BraceLoc, "to match this '{'");
    }

    PopScope(LetScope);
  }

  // Outside this let scope, this let block is not active.
  LetStack.pop_back();
  return false;
}

/// ParseMultiClass - Parse a multiclass definition.
///
///  MultiClassInst ::= MULTICLASS ID TemplateArgList?
///                     ':' BaseMultiClassList '{' MultiClassObject+ '}'
///  MultiClassObject ::= Assert
///  MultiClassObject ::= DefInst
///  MultiClassObject ::= DefMInst
///  MultiClassObject ::= Defvar
///  MultiClassObject ::= Foreach
///  MultiClassObject ::= If
///  MultiClassObject ::= LETCommand '{' ObjectList '}'
///  MultiClassObject ::= LETCommand Object
///
bool TGParser::ParseMultiClass() {
  assert(Lex.getCode() == tgtok::MultiClass && "Unexpected token");
  Lex.Lex();  // Eat the multiclass token.

  if (Lex.getCode() != tgtok::Id)
    return TokError("expected identifier after multiclass for name");
  std::string Name = Lex.getCurStrVal();

  auto Result =
    MultiClasses.insert(std::make_pair(Name,
                    std::make_unique<MultiClass>(Name, Lex.getLoc(),Records)));

  if (!Result.second)
    return TokError("multiclass '" + Name + "' already defined");

  CurMultiClass = Result.first->second.get();
  Lex.Lex();  // Eat the identifier.

  // A multiclass body introduces a new scope for local variables.
  TGVarScope *MulticlassScope = PushScope(CurMultiClass);

  // If there are template args, parse them.
  if (Lex.getCode() == tgtok::less)
    if (ParseTemplateArgList(nullptr))
      return true;

  bool inherits = false;

  // If there are submulticlasses, parse them.
  if (consume(tgtok::colon)) {
    inherits = true;

    // Read all of the submulticlasses.
    SubMultiClassReference SubMultiClass =
      ParseSubMultiClassReference(CurMultiClass);
    while (true) {
      // Check for error.
      if (!SubMultiClass.MC) return true;

      // Add it.
      if (AddSubMultiClass(CurMultiClass, SubMultiClass))
        return true;

      if (!consume(tgtok::comma))
        break;
      SubMultiClass = ParseSubMultiClassReference(CurMultiClass);
    }
  }

  if (Lex.getCode() != tgtok::l_brace) {
    if (!inherits)
      return TokError("expected '{' in multiclass definition");
    if (!consume(tgtok::semi))
      return TokError("expected ';' in multiclass definition");
  } else {
    if (Lex.Lex() == tgtok::r_brace)  // eat the '{'.
      return TokError("multiclass must contain at least one def");

    while (Lex.getCode() != tgtok::r_brace) {
      switch (Lex.getCode()) {
      default:
        return TokError("expected 'assert', 'def', 'defm', 'defvar', 'dump', "
                        "'foreach', 'if', or 'let' in multiclass body");

      case tgtok::Assert:
      case tgtok::Def:
      case tgtok::Defm:
      case tgtok::Defvar:
      case tgtok::Dump:
      case tgtok::Foreach:
      case tgtok::If:
      case tgtok::Let:
        if (ParseObject(CurMultiClass))
          return true;
        break;
      }
    }
    Lex.Lex();  // eat the '}'.

    // If we have a semicolon, print a gentle error.
    SMLoc SemiLoc = Lex.getLoc();
    if (consume(tgtok::semi)) {
      PrintError(SemiLoc, "A multiclass body should not end with a semicolon");
      PrintNote("Semicolon ignored; remove to eliminate this error");    
    }
  }

  if (!NoWarnOnUnusedTemplateArgs)
    CurMultiClass->Rec.checkUnusedTemplateArgs();

  PopScope(MulticlassScope);
  CurMultiClass = nullptr;
  return false;
}

/// ParseDefm - Parse the instantiation of a multiclass.
///
///   DefMInst ::= DEFM ID ':' DefmSubClassRef ';'
///
bool TGParser::ParseDefm(MultiClass *CurMultiClass) {
  assert(Lex.getCode() == tgtok::Defm && "Unexpected token!");
  Lex.Lex(); // eat the defm

  Init *DefmName = ParseObjectName(CurMultiClass);
  if (!DefmName)
    return true;
  if (isa<UnsetInit>(DefmName)) {
    DefmName = Records.getNewAnonymousName();
    if (CurMultiClass)
      DefmName = BinOpInit::getStrConcat(
          VarInit::get(QualifiedNameOfImplicitName(CurMultiClass),
                       StringRecTy::get(Records)),
          DefmName);
  }

  if (Lex.getCode() != tgtok::colon)
    return TokError("expected ':' after defm identifier");

  // Keep track of the new generated record definitions.
  std::vector<RecordsEntry> NewEntries;

  // This record also inherits from a regular class (non-multiclass)?
  bool InheritFromClass = false;

  // eat the colon.
  Lex.Lex();

  SMLoc SubClassLoc = Lex.getLoc();
  SubClassReference Ref = ParseSubClassReference(nullptr, true);

  while (true) {
    if (!Ref.Rec) return true;

    // To instantiate a multiclass, we get the multiclass and then loop
    // through its template argument names. Substs contains a substitution
    // value for each argument, either the value specified or the default.
    // Then we can resolve the template arguments.
    MultiClass *MC = MultiClasses[std::string(Ref.Rec->getName())].get();
    assert(MC && "Didn't lookup multiclass correctly?");

    SubstStack Substs;
    if (resolveArgumentsOfMultiClass(Substs, MC, Ref.TemplateArgs, DefmName,
                                     SubClassLoc))
      return true;

    if (resolve(MC->Entries, Substs, !CurMultiClass && Loops.empty(),
                &NewEntries, &SubClassLoc))
      return true;

    if (!consume(tgtok::comma))
      break;

    if (Lex.getCode() != tgtok::Id)
      return TokError("expected identifier");

    SubClassLoc = Lex.getLoc();

    // A defm can inherit from regular classes (non-multiclasses) as
    // long as they come in the end of the inheritance list.
    InheritFromClass = (Records.getClass(Lex.getCurStrVal()) != nullptr);

    if (InheritFromClass)
      break;

    Ref = ParseSubClassReference(nullptr, true);
  }

  if (InheritFromClass) {
    // Process all the classes to inherit as if they were part of a
    // regular 'def' and inherit all record values.
    SubClassReference SubClass = ParseSubClassReference(nullptr, false);
    while (true) {
      // Check for error.
      if (!SubClass.Rec) return true;

      // Get the expanded definition prototypes and teach them about
      // the record values the current class to inherit has
      for (auto &E : NewEntries) {
        // Add it.
        if (AddSubClass(E, SubClass))
          return true;
      }

      if (!consume(tgtok::comma))
        break;
      SubClass = ParseSubClassReference(nullptr, false);
    }
  }

  for (auto &E : NewEntries) {
    if (ApplyLetStack(E))
      return true;

    addEntry(std::move(E));
  }

  if (!consume(tgtok::semi))
    return TokError("expected ';' at end of defm");

  return false;
}

/// ParseObject
///   Object ::= ClassInst
///   Object ::= DefInst
///   Object ::= MultiClassInst
///   Object ::= DefMInst
///   Object ::= LETCommand '{' ObjectList '}'
///   Object ::= LETCommand Object
///   Object ::= Defset
///   Object ::= Deftype
///   Object ::= Defvar
///   Object ::= Assert
///   Object ::= Dump
bool TGParser::ParseObject(MultiClass *MC) {
  switch (Lex.getCode()) {
  default:
    return TokError(
        "Expected assert, class, def, defm, defset, dump, foreach, if, or let");
  case tgtok::Assert:  return ParseAssert(MC);
  case tgtok::Def:     return ParseDef(MC);
  case tgtok::Defm:    return ParseDefm(MC);
  case tgtok::Deftype:
    return ParseDeftype();
  case tgtok::Defvar:  return ParseDefvar();
  case tgtok::Dump:
    return ParseDump(MC);
  case tgtok::Foreach: return ParseForeach(MC);
  case tgtok::If:      return ParseIf(MC);
  case tgtok::Let:     return ParseTopLevelLet(MC);
  case tgtok::Defset:
    if (MC)
      return TokError("defset is not allowed inside multiclass");
    return ParseDefset();
  case tgtok::Class:
    if (MC)
      return TokError("class is not allowed inside multiclass");
    if (!Loops.empty())
      return TokError("class is not allowed inside foreach loop");
    return ParseClass();
  case tgtok::MultiClass:
    if (!Loops.empty())
      return TokError("multiclass is not allowed inside foreach loop");
    return ParseMultiClass();
  }
}

/// ParseObjectList
///   ObjectList :== Object*
bool TGParser::ParseObjectList(MultiClass *MC) {
  while (tgtok::isObjectStart(Lex.getCode())) {
    if (ParseObject(MC))
      return true;
  }
  return false;
}

bool TGParser::ParseFile() {
  Lex.Lex(); // Prime the lexer.
  TGVarScope *GlobalScope = PushScope();
  if (ParseObjectList())
    return true;
  PopScope(GlobalScope);

  // If we have unread input at the end of the file, report it.
  if (Lex.getCode() == tgtok::Eof)
    return false;

  return TokError("Unexpected token at top level");
}

// Check the types of the template argument values for a class
// inheritance, multiclass invocation, or anonymous class invocation.
// If necessary, replace an argument with a cast to the required type.
// The argument count has already been checked.
bool TGParser::CheckTemplateArgValues(
    SmallVectorImpl<llvm::ArgumentInit *> &Values, SMLoc Loc, Record *ArgsRec) {
  ArrayRef<Init *> TArgs = ArgsRec->getTemplateArgs();

  for (llvm::ArgumentInit *&Value : Values) {
    Init *ArgName = nullptr;
    if (Value->isPositional())
      ArgName = TArgs[Value->getIndex()];
    if (Value->isNamed())
      ArgName = Value->getName();

    RecordVal *Arg = ArgsRec->getValue(ArgName);
    RecTy *ArgType = Arg->getType();

    if (TypedInit *ArgValue = dyn_cast<TypedInit>(Value->getValue())) {
      auto *CastValue = ArgValue->getCastTo(ArgType);
      if (CastValue) {
        assert((!isa<TypedInit>(CastValue) ||
                cast<TypedInit>(CastValue)->getType()->typeIsA(ArgType)) &&
               "result of template arg value cast has wrong type");
        Value = Value->cloneWithValue(CastValue);
      } else {
        PrintFatalError(Loc, "Value specified for template argument '" +
                                 Arg->getNameInitAsString() + "' is of type " +
                                 ArgValue->getType()->getAsString() +
                                 "; expected type " + ArgType->getAsString() +
                                 ": " + ArgValue->getAsString());
      }
    }
  }

  return false;
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void RecordsEntry::dump() const {
  if (Loop)
    Loop->dump();
  if (Rec)
    Rec->dump();
}

LLVM_DUMP_METHOD void ForeachLoop::dump() const {
  errs() << "foreach " << IterVar->getAsString() << " = "
         << ListValue->getAsString() << " in {\n";

  for (const auto &E : Entries)
    E.dump();

  errs() << "}\n";
}

LLVM_DUMP_METHOD void MultiClass::dump() const {
  errs() << "Record:\n";
  Rec.dump();

  errs() << "Defs:\n";
  for (const auto &E : Entries)
    E.dump();
}
#endif

bool TGParser::ParseDump(MultiClass *CurMultiClass, Record *CurRec) {
  // Location of the `dump` statement.
  SMLoc Loc = Lex.getLoc();
  assert(Lex.getCode() == tgtok::Dump && "Unknown tok");
  Lex.Lex(); // eat the operation

  Init *Message = ParseValue(CurRec);
  if (!Message)
    return true;

  // Allow to use dump directly on `defvar` and `def`, by wrapping
  // them with a `!repl`.
  if (isa<DefInit>(Message))
    Message = UnOpInit::get(UnOpInit::REPR, Message, StringRecTy::get(Records))
                  ->Fold(CurRec);

  if (!consume(tgtok::semi))
    return TokError("expected ';'");

  if (CurRec)
    CurRec->addDump(Loc, Message);
  else
    addEntry(std::make_unique<Record::DumpInfo>(Loc, Message));

  return false;
}
