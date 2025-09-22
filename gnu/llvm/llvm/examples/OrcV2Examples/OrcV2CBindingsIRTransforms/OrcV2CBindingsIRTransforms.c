//===- OrcV2CBindingsDumpObjects.c - Dump JIT'd objects to disk via C API -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// To run the demo build 'OrcV2CBindingsDumpObjects', then run the built
// program. It will execute as for OrcV2CBindingsBasicUsage, but will write
// a single JIT'd object out to the working directory.
//
// Try experimenting with the DumpDir and IdentifierOverride arguments to
// LLVMOrcCreateDumpObjects.
//
//===----------------------------------------------------------------------===//

#include "llvm-c/Core.h"
#include "llvm-c/Error.h"
#include "llvm-c/LLJIT.h"
#include "llvm-c/Support.h"
#include "llvm-c/Target.h"
#include "llvm-c/Transforms/PassBuilder.h"

#include <stdio.h>

int handleError(LLVMErrorRef Err) {
  char *ErrMsg = LLVMGetErrorMessage(Err);
  fprintf(stderr, "Error: %s\n", ErrMsg);
  LLVMDisposeErrorMessage(ErrMsg);
  return 1;
}

LLVMOrcThreadSafeModuleRef createDemoModule(void) {
  LLVMOrcThreadSafeContextRef TSCtx = LLVMOrcCreateNewThreadSafeContext();
  LLVMContextRef Ctx = LLVMOrcThreadSafeContextGetContext(TSCtx);
  LLVMModuleRef M = LLVMModuleCreateWithNameInContext("demo", Ctx);
  LLVMTypeRef ParamTypes[] = {LLVMInt32Type(), LLVMInt32Type()};
  LLVMTypeRef SumFunctionType =
      LLVMFunctionType(LLVMInt32Type(), ParamTypes, 2, 0);
  LLVMValueRef SumFunction = LLVMAddFunction(M, "sum", SumFunctionType);
  LLVMBasicBlockRef EntryBB = LLVMAppendBasicBlock(SumFunction, "entry");
  LLVMBuilderRef Builder = LLVMCreateBuilder();
  LLVMPositionBuilderAtEnd(Builder, EntryBB);
  LLVMValueRef SumArg0 = LLVMGetParam(SumFunction, 0);
  LLVMValueRef SumArg1 = LLVMGetParam(SumFunction, 1);
  LLVMValueRef Result = LLVMBuildAdd(Builder, SumArg0, SumArg1, "result");
  LLVMBuildRet(Builder, Result);
  LLVMDisposeBuilder(Builder);
  LLVMOrcThreadSafeModuleRef TSM = LLVMOrcCreateNewThreadSafeModule(M, TSCtx);
  LLVMOrcDisposeThreadSafeContext(TSCtx);
  return TSM;
}

LLVMErrorRef myModuleTransform(void *Ctx, LLVMModuleRef Mod) {
  LLVMPassBuilderOptionsRef Options = LLVMCreatePassBuilderOptions();
  LLVMErrorRef E = LLVMRunPasses(Mod, "instcombine", NULL, Options);
  LLVMDisposePassBuilderOptions(Options);
  return E;
}

LLVMErrorRef transform(void *Ctx, LLVMOrcThreadSafeModuleRef *ModInOut,
                       LLVMOrcMaterializationResponsibilityRef MR) {
  return LLVMOrcThreadSafeModuleWithModuleDo(*ModInOut, myModuleTransform, Ctx);
}

int main(int argc, const char *argv[]) {

  int MainResult = 0;

  LLVMParseCommandLineOptions(argc, argv, "");

  LLVMInitializeNativeTarget();
  LLVMInitializeNativeAsmPrinter();

  // Create a DumpObjects instance to use when dumping objects to disk.
  LLVMOrcDumpObjectsRef DumpObjects = LLVMOrcCreateDumpObjects("", "");

  // Create the JIT instance.
  LLVMOrcLLJITRef J;
  {
    LLVMErrorRef Err;
    if ((Err = LLVMOrcCreateLLJIT(&J, 0))) {
      MainResult = handleError(Err);
      goto llvm_shutdown;
    }
  }

  // Use TransformLayer to set IR transform.
  {
    LLVMOrcIRTransformLayerRef TL = LLVMOrcLLJITGetIRTransformLayer(J);
    LLVMOrcIRTransformLayerSetTransform(TL, *transform, NULL);
  }

  // Create our demo module.
  LLVMOrcThreadSafeModuleRef TSM = createDemoModule();

  // Add our demo module to the JIT.
  {
    LLVMOrcJITDylibRef MainJD = LLVMOrcLLJITGetMainJITDylib(J);
    LLVMErrorRef Err;
    if ((Err = LLVMOrcLLJITAddLLVMIRModule(J, MainJD, TSM))) {
      // If adding the ThreadSafeModule fails then we need to clean it up
      // ourselves. If adding it succeeds the JIT will manage the memory.
      LLVMOrcDisposeThreadSafeModule(TSM);
      MainResult = handleError(Err);
      goto jit_cleanup;
    }
  }

  // Look up the address of our demo entry point.
  LLVMOrcJITTargetAddress SumAddr;
  {
    LLVMErrorRef Err;
    if ((Err = LLVMOrcLLJITLookup(J, &SumAddr, "sum"))) {
      MainResult = handleError(Err);
      goto jit_cleanup;
    }
  }

  // If we made it here then everything succeeded. Execute our JIT'd code.
  int32_t (*Sum)(int32_t, int32_t) = (int32_t(*)(int32_t, int32_t))SumAddr;
  int32_t Result = Sum(1, 2);

  // Print the result.
  printf("1 + 2 = %i\n", Result);

jit_cleanup:

  // Destroy our JIT instance.
  {
    LLVMErrorRef Err;
    if ((Err = LLVMOrcDisposeLLJIT(J))) {
      int NewFailureResult = handleError(Err);
      if (MainResult == 0)
        MainResult = NewFailureResult;
    }
  }

llvm_shutdown:
  // Destroy our DumpObjects instance.
  LLVMOrcDisposeDumpObjects(DumpObjects);

  // Shut down LLVM.
  LLVMShutdown();

  return MainResult;
}
