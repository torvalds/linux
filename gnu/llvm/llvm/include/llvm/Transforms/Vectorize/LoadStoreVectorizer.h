//===- LoadStoreVectorizer.cpp - GPU Load & Store Vectorizer --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_VECTORIZE_LOADSTOREVECTORIZER_H
#define LLVM_TRANSFORMS_VECTORIZE_LOADSTOREVECTORIZER_H

#include "llvm/IR/PassManager.h"

namespace llvm {
class Pass;
class Function;

class LoadStoreVectorizerPass : public PassInfoMixin<LoadStoreVectorizerPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// Create a legacy pass manager instance of the LoadStoreVectorizer pass
Pass *createLoadStoreVectorizerPass();

}

#endif /* LLVM_TRANSFORMS_VECTORIZE_LOADSTOREVECTORIZER_H */
