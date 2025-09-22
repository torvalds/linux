//===- ForceFunctionAttrs.cpp - Force function attrs for debugging --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/IPO/ForceFunctionAttrs.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/LineIterator.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "forceattrs"

static cl::list<std::string> ForceAttributes(
    "force-attribute", cl::Hidden,
    cl::desc(
        "Add an attribute to a function. This can be a "
        "pair of 'function-name:attribute-name', to apply an attribute to a "
        "specific function. For "
        "example -force-attribute=foo:noinline. Specifying only an attribute "
        "will apply the attribute to every function in the module. This "
        "option can be specified multiple times."));

static cl::list<std::string> ForceRemoveAttributes(
    "force-remove-attribute", cl::Hidden,
    cl::desc("Remove an attribute from a function. This can be a "
             "pair of 'function-name:attribute-name' to remove an attribute "
             "from a specific function. For "
             "example -force-remove-attribute=foo:noinline. Specifying only an "
             "attribute will remove the attribute from all functions in the "
             "module. This "
             "option can be specified multiple times."));

static cl::opt<std::string> CSVFilePath(
    "forceattrs-csv-path", cl::Hidden,
    cl::desc(
        "Path to CSV file containing lines of function names and attributes to "
        "add to them in the form of `f1,attr1` or `f2,attr2=str`."));

/// If F has any forced attributes given on the command line, add them.
/// If F has any forced remove attributes given on the command line, remove
/// them. When both force and force-remove are given to a function, the latter
/// takes precedence.
static void forceAttributes(Function &F) {
  auto ParseFunctionAndAttr = [&](StringRef S) {
    StringRef AttributeText;
    if (S.contains(':')) {
      auto KV = StringRef(S).split(':');
      if (KV.first != F.getName())
        return Attribute::None;
      AttributeText = KV.second;
    } else {
      AttributeText = S;
    }
    auto Kind = Attribute::getAttrKindFromName(AttributeText);
    if (Kind == Attribute::None || !Attribute::canUseAsFnAttr(Kind)) {
      LLVM_DEBUG(dbgs() << "ForcedAttribute: " << AttributeText
                        << " unknown or not a function attribute!\n");
    }
    return Kind;
  };

  for (const auto &S : ForceAttributes) {
    auto Kind = ParseFunctionAndAttr(S);
    if (Kind == Attribute::None || F.hasFnAttribute(Kind))
      continue;
    F.addFnAttr(Kind);
  }

  for (const auto &S : ForceRemoveAttributes) {
    auto Kind = ParseFunctionAndAttr(S);
    if (Kind == Attribute::None || !F.hasFnAttribute(Kind))
      continue;
    F.removeFnAttr(Kind);
  }
}

static bool hasForceAttributes() {
  return !ForceAttributes.empty() || !ForceRemoveAttributes.empty();
}

PreservedAnalyses ForceFunctionAttrsPass::run(Module &M,
                                              ModuleAnalysisManager &) {
  bool Changed = false;
  if (!CSVFilePath.empty()) {
    auto BufferOrError = MemoryBuffer::getFileOrSTDIN(CSVFilePath);
    if (!BufferOrError)
      report_fatal_error("Cannot open CSV file.");
    StringRef Buffer = BufferOrError.get()->getBuffer();
    auto MemoryBuffer = MemoryBuffer::getMemBuffer(Buffer);
    line_iterator It(*MemoryBuffer);
    for (; !It.is_at_end(); ++It) {
      auto SplitPair = It->split(',');
      if (SplitPair.second.empty())
        continue;
      Function *Func = M.getFunction(SplitPair.first);
      if (Func) {
        if (Func->isDeclaration())
          continue;
        auto SecondSplitPair = SplitPair.second.split('=');
        if (!SecondSplitPair.second.empty()) {
          Func->addFnAttr(SecondSplitPair.first, SecondSplitPair.second);
          Changed = true;
        } else {
          auto AttrKind = Attribute::getAttrKindFromName(SplitPair.second);
          if (AttrKind != Attribute::None &&
              Attribute::canUseAsFnAttr(AttrKind)) {
            // TODO: There could be string attributes without a value, we should
            // support those, too.
            Func->addFnAttr(AttrKind);
            Changed = true;
          } else
            errs() << "Cannot add " << SplitPair.second
                   << " as an attribute name.\n";
        }
      } else {
        errs() << "Function in CSV file at line " << It.line_number()
               << " does not exist.\n";
        // TODO: `report_fatal_error at end of pass for missing functions.
        continue;
      }
    }
  }
  if (hasForceAttributes()) {
    for (Function &F : M.functions())
      forceAttributes(F);
    Changed = true;
  }
  // Just conservatively invalidate analyses if we've made any changes, this
  // isn't likely to be important.
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
