//=== DWARFLinker.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DWARFLinkerImpl.h"
#include "DependencyTracker.h"

using namespace llvm;
using namespace dwarf_linker;
using namespace dwarf_linker::parallel;

std::unique_ptr<DWARFLinker>
DWARFLinker::createLinker(MessageHandlerTy ErrorHandler,
                          MessageHandlerTy WarningHandler) {
  return std::make_unique<DWARFLinkerImpl>(ErrorHandler, WarningHandler);
}
