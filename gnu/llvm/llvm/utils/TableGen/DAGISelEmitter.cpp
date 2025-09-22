//===- DAGISelEmitter.cpp - Generate an instruction selector --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This tablegen backend emits a DAG instruction selector.
//
//===----------------------------------------------------------------------===//

#include "Common/CodeGenDAGPatterns.h"
#include "Common/CodeGenInstruction.h"
#include "Common/CodeGenTarget.h"
#include "Common/DAGISelMatcher.h"
#include "llvm/Support/Debug.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/TableGenBackend.h"
using namespace llvm;

#define DEBUG_TYPE "dag-isel-emitter"

namespace {
/// DAGISelEmitter - The top-level class which coordinates construction
/// and emission of the instruction selector.
class DAGISelEmitter {
  RecordKeeper &Records; // Just so we can get at the timing functions.
  CodeGenDAGPatterns CGP;

public:
  explicit DAGISelEmitter(RecordKeeper &R) : Records(R), CGP(R) {}
  void run(raw_ostream &OS);
};
} // End anonymous namespace

//===----------------------------------------------------------------------===//
// DAGISelEmitter Helper methods
//

/// Compute the number of instructions for this pattern.
/// This is a temporary hack.  We should really include the instruction
/// latencies in this calculation.
static unsigned getResultPatternCost(TreePatternNode &P,
                                     const CodeGenDAGPatterns &CGP) {
  if (P.isLeaf())
    return 0;

  unsigned Cost = 0;
  Record *Op = P.getOperator();
  if (Op->isSubClassOf("Instruction")) {
    Cost++;
    CodeGenInstruction &II = CGP.getTargetInfo().getInstruction(Op);
    if (II.usesCustomInserter)
      Cost += 10;
  }
  for (unsigned i = 0, e = P.getNumChildren(); i != e; ++i)
    Cost += getResultPatternCost(P.getChild(i), CGP);
  return Cost;
}

/// getResultPatternCodeSize - Compute the code size of instructions for this
/// pattern.
static unsigned getResultPatternSize(TreePatternNode &P,
                                     const CodeGenDAGPatterns &CGP) {
  if (P.isLeaf())
    return 0;

  unsigned Cost = 0;
  Record *Op = P.getOperator();
  if (Op->isSubClassOf("Instruction")) {
    Cost += Op->getValueAsInt("CodeSize");
  }
  for (unsigned i = 0, e = P.getNumChildren(); i != e; ++i)
    Cost += getResultPatternSize(P.getChild(i), CGP);
  return Cost;
}

namespace {
// PatternSortingPredicate - return true if we prefer to match LHS before RHS.
// In particular, we want to match maximal patterns first and lowest cost within
// a particular complexity first.
struct PatternSortingPredicate {
  PatternSortingPredicate(CodeGenDAGPatterns &cgp) : CGP(cgp) {}
  CodeGenDAGPatterns &CGP;

  bool operator()(const PatternToMatch *LHS, const PatternToMatch *RHS) {
    const TreePatternNode &LT = LHS->getSrcPattern();
    const TreePatternNode &RT = RHS->getSrcPattern();

    MVT LHSVT = LT.getNumTypes() != 0 ? LT.getSimpleType(0) : MVT::Other;
    MVT RHSVT = RT.getNumTypes() != 0 ? RT.getSimpleType(0) : MVT::Other;
    if (LHSVT.isVector() != RHSVT.isVector())
      return RHSVT.isVector();

    if (LHSVT.isFloatingPoint() != RHSVT.isFloatingPoint())
      return RHSVT.isFloatingPoint();

    // Otherwise, if the patterns might both match, sort based on complexity,
    // which means that we prefer to match patterns that cover more nodes in the
    // input over nodes that cover fewer.
    int LHSSize = LHS->getPatternComplexity(CGP);
    int RHSSize = RHS->getPatternComplexity(CGP);
    if (LHSSize > RHSSize)
      return true; // LHS -> bigger -> less cost
    if (LHSSize < RHSSize)
      return false;

    // If the patterns have equal complexity, compare generated instruction cost
    unsigned LHSCost = getResultPatternCost(LHS->getDstPattern(), CGP);
    unsigned RHSCost = getResultPatternCost(RHS->getDstPattern(), CGP);
    if (LHSCost < RHSCost)
      return true;
    if (LHSCost > RHSCost)
      return false;

    unsigned LHSPatSize = getResultPatternSize(LHS->getDstPattern(), CGP);
    unsigned RHSPatSize = getResultPatternSize(RHS->getDstPattern(), CGP);
    if (LHSPatSize < RHSPatSize)
      return true;
    if (LHSPatSize > RHSPatSize)
      return false;

    // Sort based on the UID of the pattern, to reflect source order.
    // Note that this is not guaranteed to be unique, since a single source
    // pattern may have been resolved into multiple match patterns due to
    // alternative fragments.  To ensure deterministic output, always use
    // std::stable_sort with this predicate.
    return LHS->getID() < RHS->getID();
  }
};
} // End anonymous namespace

void DAGISelEmitter::run(raw_ostream &OS) {
  Records.startTimer("Parse patterns");
  emitSourceFileHeader("DAG Instruction Selector for the " +
                           CGP.getTargetInfo().getName().str() + " target",
                       OS);

  OS << "// *** NOTE: This file is #included into the middle of the target\n"
     << "// *** instruction selector class.  These functions are really "
     << "methods.\n\n";

  OS << "// If GET_DAGISEL_DECL is #defined with any value, only function\n"
        "// declarations will be included when this file is included.\n"
        "// If GET_DAGISEL_BODY is #defined, its value should be the name of\n"
        "// the instruction selector class. Function bodies will be emitted\n"
        "// and each function's name will be qualified with the name of the\n"
        "// class.\n"
        "//\n"
        "// When neither of the GET_DAGISEL* macros is defined, the functions\n"
        "// are emitted inline.\n\n";

  LLVM_DEBUG(errs() << "\n\nALL PATTERNS TO MATCH:\n\n";
             for (CodeGenDAGPatterns::ptm_iterator I = CGP.ptm_begin(),
                  E = CGP.ptm_end();
                  I != E; ++I) {
               errs() << "PATTERN: ";
               I->getSrcPattern().dump();
               errs() << "\nRESULT:  ";
               I->getDstPattern().dump();
               errs() << "\n";
             });

  // Add all the patterns to a temporary list so we can sort them.
  Records.startTimer("Sort patterns");
  std::vector<const PatternToMatch *> Patterns;
  for (const PatternToMatch &PTM : CGP.ptms())
    Patterns.push_back(&PTM);

  // We want to process the matches in order of minimal cost.  Sort the patterns
  // so the least cost one is at the start.
  llvm::stable_sort(Patterns, PatternSortingPredicate(CGP));

  // Convert each variant of each pattern into a Matcher.
  Records.startTimer("Convert to matchers");
  SmallVector<Matcher *, 0> PatternMatchers;
  for (const PatternToMatch *PTM : Patterns) {
    for (unsigned Variant = 0;; ++Variant) {
      if (Matcher *M = ConvertPatternToMatcher(*PTM, Variant, CGP))
        PatternMatchers.push_back(M);
      else
        break;
    }
  }

  std::unique_ptr<Matcher> TheMatcher =
      std::make_unique<ScopeMatcher>(std::move(PatternMatchers));

  Records.startTimer("Optimize matchers");
  OptimizeMatcher(TheMatcher, CGP);

  // Matcher->dump();

  Records.startTimer("Emit matcher table");
  EmitMatcherTable(TheMatcher.get(), CGP, OS);
}

static TableGen::Emitter::OptClass<DAGISelEmitter>
    X("gen-dag-isel", "Generate a DAG instruction selector");
