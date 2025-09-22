//===----- AllocationActions.gpp -- JITLink allocation support calls  -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/Shared/AllocationActions.h"

namespace llvm {
namespace orc {
namespace shared {

Expected<std::vector<WrapperFunctionCall>>
runFinalizeActions(AllocActions &AAs) {
  std::vector<WrapperFunctionCall> DeallocActions;
  DeallocActions.reserve(numDeallocActions(AAs));

  for (auto &AA : AAs) {
    if (AA.Finalize)
      if (auto Err = AA.Finalize.runWithSPSRetErrorMerged())
        return joinErrors(std::move(Err), runDeallocActions(DeallocActions));

    if (AA.Dealloc)
      DeallocActions.push_back(std::move(AA.Dealloc));
  }

  AAs.clear();
  return DeallocActions;
}

Error runDeallocActions(ArrayRef<WrapperFunctionCall> DAs) {
  Error Err = Error::success();
  while (!DAs.empty()) {
    Err = joinErrors(std::move(Err), DAs.back().runWithSPSRetErrorMerged());
    DAs = DAs.drop_back();
  }
  return Err;
}

} // namespace shared
} // namespace orc
} // namespace llvm
