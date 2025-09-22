//===-- DiffConsumer.cpp - Difference Consumer ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This files implements the LLVM difference Consumer
//
//===----------------------------------------------------------------------===//

#include "DiffConsumer.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

static void ComputeNumbering(const Function *F,
                             DenseMap<const Value *, unsigned> &Numbering) {
  unsigned IN = 0;

  // Arguments get the first numbers.
  for (const auto &Arg : F->args())
    if (!Arg.hasName())
      Numbering[&Arg] = IN++;

  // Walk the basic blocks in order.
  for (const auto &Func : *F) {
    if (!Func.hasName())
      Numbering[&Func] = IN++;

    // Walk the instructions in order.
    for (const auto &BB : Func)
      // void instructions don't get numbers.
      if (!BB.hasName() && !BB.getType()->isVoidTy())
        Numbering[&BB] = IN++;
  }

  assert(!Numbering.empty() && "asked for numbering but numbering was no-op");
}

void Consumer::anchor() { }

void DiffConsumer::printValue(const Value *V, bool isL) {
  if (V->hasName()) {
    out << (isa<GlobalValue>(V) ? '@' : '%') << V->getName();
    return;
  }
  if (V->getType()->isVoidTy()) {
    if (auto *SI = dyn_cast<StoreInst>(V)) {
      out << "store to ";
      printValue(SI->getPointerOperand(), isL);
    } else if (auto *CI = dyn_cast<CallInst>(V)) {
      out << "call to ";
      printValue(CI->getCalledOperand(), isL);
    } else if (auto *II = dyn_cast<InvokeInst>(V)) {
      out << "invoke to ";
      printValue(II->getCalledOperand(), isL);
    } else {
      out << *V;
    }
    return;
  }
  if (isa<Constant>(V)) {
    out << *V;
    return;
  }

  unsigned N = contexts.size();
  while (N > 0) {
    --N;
    DiffContext &ctxt = contexts[N];
    if (!ctxt.IsFunction) continue;
    if (isL) {
      if (ctxt.LNumbering.empty())
        ComputeNumbering(cast<Function>(ctxt.L), ctxt.LNumbering);
      out << '%' << ctxt.LNumbering[V];
      return;
    } else {
      if (ctxt.RNumbering.empty())
        ComputeNumbering(cast<Function>(ctxt.R), ctxt.RNumbering);
      out << '%' << ctxt.RNumbering[V];
      return;
    }
  }

  out << "<anonymous>";
}

void DiffConsumer::header() {
  if (contexts.empty()) return;
  for (SmallVectorImpl<DiffContext>::iterator
         I = contexts.begin(), E = contexts.end(); I != E; ++I) {
    if (I->Differences) continue;
    if (isa<Function>(I->L)) {
      // Extra newline between functions.
      if (Differences) out << "\n";

      const Function *L = cast<Function>(I->L);
      const Function *R = cast<Function>(I->R);
      if (L->getName() != R->getName())
        out << "in function " << L->getName()
            << " / " << R->getName() << ":\n";
      else
        out << "in function " << L->getName() << ":\n";
    } else if (isa<BasicBlock>(I->L)) {
      const BasicBlock *L = cast<BasicBlock>(I->L);
      const BasicBlock *R = cast<BasicBlock>(I->R);
      if (L->hasName() && R->hasName() && L->getName() == R->getName())
        out << "  in block %" << L->getName() << ":\n";
      else {
        out << "  in block ";
        printValue(L, true);
        out << " / ";
        printValue(R, false);
        out << ":\n";
      }
    } else if (isa<Instruction>(I->L)) {
      out << "    in instruction ";
      printValue(I->L, true);
      out << " / ";
      printValue(I->R, false);
      out << ":\n";
    }

    I->Differences = true;
  }
}

void DiffConsumer::indent() {
  unsigned N = Indent;
  while (N--) out << ' ';
}

void DiffConsumer::reset() {
  contexts.clear();
  Differences = false;
  Indent = 0;
}

bool DiffConsumer::hadDifferences() const {
  return Differences;
}

void DiffConsumer::enterContext(const Value *L, const Value *R) {
  contexts.push_back(DiffContext(L, R));
  Indent += 2;
}

void DiffConsumer::exitContext() {
  Differences |= contexts.back().Differences;
  contexts.pop_back();
  Indent -= 2;
}

void DiffConsumer::log(StringRef text) {
  header();
  indent();
  out << text << '\n';
}

void DiffConsumer::logf(const LogBuilder &Log) {
  header();
  indent();

  unsigned arg = 0;

  StringRef format = Log.getFormat();
  while (true) {
    size_t percent = format.find('%');
    if (percent == StringRef::npos) {
      out << format;
      break;
    }
    assert(format[percent] == '%');

    if (percent > 0) out << format.substr(0, percent);

    switch (format[percent+1]) {
    case '%': out << '%'; break;
    case 'l': printValue(Log.getArgument(arg++), true); break;
    case 'r': printValue(Log.getArgument(arg++), false); break;
    default: llvm_unreachable("unknown format character");
    }

    format = format.substr(percent+2);
  }

  out << '\n';
}

void DiffConsumer::logd(const DiffLogBuilder &Log) {
  header();

  for (unsigned I = 0, E = Log.getNumLines(); I != E; ++I) {
    indent();
    switch (Log.getLineKind(I)) {
    case DC_match:
      out << "  ";
      Log.getLeft(I)->print(dbgs()); dbgs() << '\n';
      //printValue(Log.getLeft(I), true);
      break;
    case DC_left:
      out << "< ";
      Log.getLeft(I)->print(dbgs()); dbgs() << '\n';
      //printValue(Log.getLeft(I), true);
      break;
    case DC_right:
      out << "> ";
      Log.getRight(I)->print(dbgs()); dbgs() << '\n';
      //printValue(Log.getRight(I), false);
      break;
    }
    //out << "\n";
  }
}
