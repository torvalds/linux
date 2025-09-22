//===- llvm/Testing/Support/Error.cpp -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Testing/Support/Error.h"

using namespace llvm;

llvm::detail::ErrorHolder llvm::detail::TakeError(llvm::Error Err) {
  std::vector<std::shared_ptr<ErrorInfoBase>> Infos;
  handleAllErrors(std::move(Err),
                  [&Infos](std::unique_ptr<ErrorInfoBase> Info) {
                    Infos.emplace_back(std::move(Info));
                  });
  return {std::move(Infos)};
}
