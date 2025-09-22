//===--- SPIRVMetadata.h ---- IR Metadata Parsing Funcs ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains functions needed for parsing LLVM IR metadata relevant
// to the SPIR-V target.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SPIRV_SPIRVMETADATA_H
#define LLVM_LIB_TARGET_SPIRV_SPIRVMETADATA_H

#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"

namespace llvm {

//===----------------------------------------------------------------------===//
// OpenCL Metadata
//

MDString *getOCLKernelArgAccessQual(const Function &F, unsigned ArgIdx);
MDString *getOCLKernelArgTypeQual(const Function &F, unsigned ArgIdx);

} // namespace llvm
#endif // LLVM_LIB_TARGET_SPIRV_METADATA_H
