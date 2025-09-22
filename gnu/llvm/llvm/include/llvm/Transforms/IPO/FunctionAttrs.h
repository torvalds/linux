//===- FunctionAttrs.h - Compute function attributes ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Provides passes for computing function attributes based on interprocedural
/// analyses.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_FUNCTIONATTRS_H
#define LLVM_TRANSFORMS_IPO_FUNCTIONATTRS_H

#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/LazyCallGraph.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

class GlobalValueSummary;
class ModuleSummaryIndex;
class Function;
class Module;

/// Returns the memory access properties of this copy of the function.
MemoryEffects computeFunctionBodyMemoryAccess(Function &F, AAResults &AAR);

/// Propagate function attributes for function summaries along the index's
/// callgraph during thinlink
bool thinLTOPropagateFunctionAttrs(
    ModuleSummaryIndex &Index,
    function_ref<bool(GlobalValue::GUID, const GlobalValueSummary *)>
        isPrevailing);

/// Computes function attributes in post-order over the call graph.
///
/// By operating in post-order, this pass computes precise attributes for
/// called functions prior to processsing their callers. This "bottom-up"
/// approach allows powerful interprocedural inference of function attributes
/// like memory access patterns, etc. It can discover functions that do not
/// access memory, or only read memory, and give them the readnone/readonly
/// attribute. It also discovers function arguments that are not captured by
/// the function and marks them with the nocapture attribute.
struct PostOrderFunctionAttrsPass : PassInfoMixin<PostOrderFunctionAttrsPass> {
  PostOrderFunctionAttrsPass(bool SkipNonRecursive = false)
      : SkipNonRecursive(SkipNonRecursive) {}
  PreservedAnalyses run(LazyCallGraph::SCC &C, CGSCCAnalysisManager &AM,
                        LazyCallGraph &CG, CGSCCUpdateResult &UR);

  void printPipeline(raw_ostream &OS,
                     function_ref<StringRef(StringRef)> MapClassName2PassName);

private:
  bool SkipNonRecursive;
};

/// A pass to do RPO deduction and propagation of function attributes.
///
/// This pass provides a general RPO or "top down" propagation of
/// function attributes. For a few (rare) cases, we can deduce significantly
/// more about function attributes by working in RPO, so this pass
/// provides the complement to the post-order pass above where the majority of
/// deduction is performed.
// FIXME: Currently there is no RPO CGSCC pass structure to slide into and so
// this is a boring module pass, but eventually it should be an RPO CGSCC pass
// when such infrastructure is available.
class ReversePostOrderFunctionAttrsPass
    : public PassInfoMixin<ReversePostOrderFunctionAttrsPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_IPO_FUNCTIONATTRS_H
