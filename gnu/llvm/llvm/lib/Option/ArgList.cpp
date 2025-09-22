//===- ArgList.cpp - Argument List Management -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/Option.h"
#include "llvm/Option/OptSpecifier.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;
using namespace llvm::opt;

void ArgList::append(Arg *A) {
  Args.push_back(A);

  // Update ranges for the option and all of its groups.
  for (Option O = A->getOption().getUnaliasedOption(); O.isValid();
       O = O.getGroup()) {
    auto &R =
        OptRanges.insert(std::make_pair(O.getID(), emptyRange())).first->second;
    R.first = std::min<unsigned>(R.first, Args.size() - 1);
    R.second = Args.size();
  }
}

void ArgList::eraseArg(OptSpecifier Id) {
  // Zero out the removed entries but keep them around so that we don't
  // need to invalidate OptRanges.
  for (Arg *const &A : filtered(Id)) {
    // Avoid the need for a non-const filtered iterator variant.
    Arg **ArgsBegin = Args.data();
    ArgsBegin[&A - ArgsBegin] = nullptr;
  }
  OptRanges.erase(Id.getID());
}

ArgList::OptRange
ArgList::getRange(std::initializer_list<OptSpecifier> Ids) const {
  OptRange R = emptyRange();
  for (auto Id : Ids) {
    auto I = OptRanges.find(Id.getID());
    if (I != OptRanges.end()) {
      R.first = std::min(R.first, I->second.first);
      R.second = std::max(R.second, I->second.second);
    }
  }
  // Map an empty {-1, 0} range to {0, 0} so it can be used to form iterators.
  if (R.first == -1u)
    R.first = 0;
  return R;
}

bool ArgList::hasFlag(OptSpecifier Pos, OptSpecifier Neg, bool Default) const {
  if (Arg *A = getLastArg(Pos, Neg))
    return A->getOption().matches(Pos);
  return Default;
}

bool ArgList::hasFlagNoClaim(OptSpecifier Pos, OptSpecifier Neg,
                             bool Default) const {
  if (Arg *A = getLastArgNoClaim(Pos, Neg))
    return A->getOption().matches(Pos);
  return Default;
}

bool ArgList::hasFlag(OptSpecifier Pos, OptSpecifier PosAlias, OptSpecifier Neg,
                      bool Default) const {
  if (Arg *A = getLastArg(Pos, PosAlias, Neg))
    return A->getOption().matches(Pos) || A->getOption().matches(PosAlias);
  return Default;
}

StringRef ArgList::getLastArgValue(OptSpecifier Id, StringRef Default) const {
  if (Arg *A = getLastArg(Id))
    return A->getValue();
  return Default;
}

std::vector<std::string> ArgList::getAllArgValues(OptSpecifier Id) const {
  SmallVector<const char *, 16> Values;
  AddAllArgValues(Values, Id);
  return std::vector<std::string>(Values.begin(), Values.end());
}

void ArgList::addOptInFlag(ArgStringList &Output, OptSpecifier Pos,
                           OptSpecifier Neg) const {
  if (Arg *A = getLastArg(Pos, Neg))
    if (A->getOption().matches(Pos))
      A->render(*this, Output);
}

void ArgList::AddAllArgsExcept(ArgStringList &Output,
                               ArrayRef<OptSpecifier> Ids,
                               ArrayRef<OptSpecifier> ExcludeIds) const {
  for (const Arg *Arg : *this) {
    bool Excluded = false;
    for (OptSpecifier Id : ExcludeIds) {
      if (Arg->getOption().matches(Id)) {
        Excluded = true;
        break;
      }
    }
    if (!Excluded) {
      for (OptSpecifier Id : Ids) {
        if (Arg->getOption().matches(Id)) {
          Arg->claim();
          Arg->render(*this, Output);
          break;
        }
      }
    }
  }
}

/// This is a nicer interface when you don't have a list of Ids to exclude.
void ArgList::addAllArgs(ArgStringList &Output,
                         ArrayRef<OptSpecifier> Ids) const {
  ArrayRef<OptSpecifier> Exclude = std::nullopt;
  AddAllArgsExcept(Output, Ids, Exclude);
}

void ArgList::AddAllArgs(ArgStringList &Output, OptSpecifier Id0) const {
  for (auto *Arg : filtered(Id0)) {
    Arg->claim();
    Arg->render(*this, Output);
  }
}

void ArgList::AddAllArgValues(ArgStringList &Output, OptSpecifier Id0,
                              OptSpecifier Id1, OptSpecifier Id2) const {
  for (auto *Arg : filtered(Id0, Id1, Id2)) {
    Arg->claim();
    const auto &Values = Arg->getValues();
    Output.append(Values.begin(), Values.end());
  }
}

void ArgList::AddAllArgsTranslated(ArgStringList &Output, OptSpecifier Id0,
                                   const char *Translation,
                                   bool Joined) const {
  for (auto *Arg : filtered(Id0)) {
    Arg->claim();

    if (Joined) {
      Output.push_back(MakeArgString(StringRef(Translation) +
                                     Arg->getValue(0)));
    } else {
      Output.push_back(Translation);
      Output.push_back(Arg->getValue(0));
    }
  }
}

void ArgList::ClaimAllArgs(OptSpecifier Id0) const {
  for (auto *Arg : filtered(Id0))
    Arg->claim();
}

void ArgList::ClaimAllArgs() const {
  for (auto *Arg : *this)
    if (!Arg->isClaimed())
      Arg->claim();
}

const char *ArgList::GetOrMakeJoinedArgString(unsigned Index,
                                              StringRef LHS,
                                              StringRef RHS) const {
  StringRef Cur = getArgString(Index);
  if (Cur.size() == LHS.size() + RHS.size() && Cur.starts_with(LHS) &&
      Cur.ends_with(RHS))
    return Cur.data();

  return MakeArgString(LHS + RHS);
}

void ArgList::print(raw_ostream &O) const {
  for (Arg *A : *this) {
    O << "* ";
    A->print(O);
  }
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void ArgList::dump() const { print(dbgs()); }
#endif

void InputArgList::releaseMemory() {
  // An InputArgList always owns its arguments.
  for (Arg *A : *this)
    delete A;
}

InputArgList::InputArgList(const char* const *ArgBegin,
                           const char* const *ArgEnd)
  : NumInputArgStrings(ArgEnd - ArgBegin) {
  ArgStrings.append(ArgBegin, ArgEnd);
}

unsigned InputArgList::MakeIndex(StringRef String0) const {
  unsigned Index = ArgStrings.size();

  // Tuck away so we have a reliable const char *.
  SynthesizedStrings.push_back(std::string(String0));
  ArgStrings.push_back(SynthesizedStrings.back().c_str());

  return Index;
}

unsigned InputArgList::MakeIndex(StringRef String0,
                                 StringRef String1) const {
  unsigned Index0 = MakeIndex(String0);
  unsigned Index1 = MakeIndex(String1);
  assert(Index0 + 1 == Index1 && "Unexpected non-consecutive indices!");
  (void) Index1;
  return Index0;
}

const char *InputArgList::MakeArgStringRef(StringRef Str) const {
  return getArgString(MakeIndex(Str));
}

DerivedArgList::DerivedArgList(const InputArgList &BaseArgs)
    : BaseArgs(BaseArgs) {}

const char *DerivedArgList::MakeArgStringRef(StringRef Str) const {
  return BaseArgs.MakeArgString(Str);
}

void DerivedArgList::AddSynthesizedArg(Arg *A) {
  SynthesizedArgs.push_back(std::unique_ptr<Arg>(A));
}

Arg *DerivedArgList::MakeFlagArg(const Arg *BaseArg, const Option Opt) const {
  SynthesizedArgs.push_back(
      std::make_unique<Arg>(Opt, MakeArgString(Opt.getPrefix() + Opt.getName()),
                       BaseArgs.MakeIndex(Opt.getName()), BaseArg));
  return SynthesizedArgs.back().get();
}

Arg *DerivedArgList::MakePositionalArg(const Arg *BaseArg, const Option Opt,
                                       StringRef Value) const {
  unsigned Index = BaseArgs.MakeIndex(Value);
  SynthesizedArgs.push_back(
      std::make_unique<Arg>(Opt, MakeArgString(Opt.getPrefix() + Opt.getName()),
                       Index, BaseArgs.getArgString(Index), BaseArg));
  return SynthesizedArgs.back().get();
}

Arg *DerivedArgList::MakeSeparateArg(const Arg *BaseArg, const Option Opt,
                                     StringRef Value) const {
  unsigned Index = BaseArgs.MakeIndex(Opt.getName(), Value);
  SynthesizedArgs.push_back(
      std::make_unique<Arg>(Opt, MakeArgString(Opt.getPrefix() + Opt.getName()),
                       Index, BaseArgs.getArgString(Index + 1), BaseArg));
  return SynthesizedArgs.back().get();
}

Arg *DerivedArgList::MakeJoinedArg(const Arg *BaseArg, const Option Opt,
                                   StringRef Value) const {
  unsigned Index = BaseArgs.MakeIndex((Opt.getName() + Value).str());
  SynthesizedArgs.push_back(std::make_unique<Arg>(
      Opt, MakeArgString(Opt.getPrefix() + Opt.getName()), Index,
      BaseArgs.getArgString(Index) + Opt.getName().size(), BaseArg));
  return SynthesizedArgs.back().get();
}
