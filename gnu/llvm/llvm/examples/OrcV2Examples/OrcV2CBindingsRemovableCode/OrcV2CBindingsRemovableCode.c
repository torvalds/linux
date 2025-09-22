//===-------- BasicOrcV2CBindings.c - Basic OrcV2 C Bindings Demo ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm-c/Core.h"
#include "llvm-c/Error.h"
#include "llvm-c/LLJIT.h"
#include "llvm-c/Support.h"
#include "llvm-c/Target.h"

#include <stdio.h>

int handleError(LLVMErrorRef Err) {
  char *ErrMsg = LLVMGetErrorMessage(Err);
  fprintf(stderr, "Error: %s\n", ErrMsg);
  LLVMDisposeErrorMessage(ErrMsg);
  return 1;
}

LLVMOrcThreadSafeModuleRef createDemoModule(void) {
  // Create a new ThreadSafeContext and underlying LLVMContext.
  LLVMOrcThreadSafeContextRef TSCtx = LLVMOrcCreateNewThreadSafeContext();

  // Get a reference to the underlying LLVMContext.
  LLVMContextRef Ctx = LLVMOrcThreadSafeContextGetContext(TSCtx);

  // Create a new LLVM module.
  LLVMModuleRef M = LLVMModuleCreateWithNameInContext("demo", Ctx);

  // Add a "sum" function":
  //  - Create the function type and function instance.
  LLVMTypeRef ParamTypes[] = {LLVMInt32Type(), LLVMInt32Type()};
  LLVMTypeRef SumFunctionType =
      LLVMFunctionType(LLVMInt32Type(), ParamTypes, 2, 0);
  LLVMValueRef SumFunction = LLVMAddFunction(M, "sum", SumFunctionType);

  //  - Add a basic block to the function.
  LLVMBasicBlockRef EntryBB = LLVMAppendBasicBlock(SumFunction, "entry");

  //  - Add an IR builder and point it at the end of the basic block.
  LLVMBuilderRef Builder = LLVMCreateBuilder();
  LLVMPositionBuilderAtEnd(Builder, EntryBB);

  //  - Get the two function arguments and use them co construct an "add"
  //    instruction.
  LLVMValueRef SumArg0 = LLVMGetParam(SumFunction, 0);
  LLVMValueRef SumArg1 = LLVMGetParam(SumFunction, 1);
  LLVMValueRef Result = LLVMBuildAdd(Builder, SumArg0, SumArg1, "result");

  //  - Build the return instruction.
  LLVMBuildRet(Builder, Result);

  //  - Free the builder.
  LLVMDisposeBuilder(Builder);

  // Our demo module is now complete. Wrap it and our ThreadSafeContext in a
  // ThreadSafeModule.
  LLVMOrcThreadSafeModuleRef TSM = LLVMOrcCreateNewThreadSafeModule(M, TSCtx);

  // Dispose of our local ThreadSafeContext value. The underlying LLVMContext
  // will be kept alive by our ThreadSafeModule, TSM.
  LLVMOrcDisposeThreadSafeContext(TSCtx);

  // Return the result.
  return TSM;
}

int main(int argc, const char *argv[]) {

  int MainResult = 0;

  // Parse command line arguments and initialize LLVM Core.
  LLVMParseCommandLineOptions(argc, argv, "");

  // Initialize native target codegen and asm printer.
  LLVMInitializeNativeTarget();
  LLVMInitializeNativeAsmPrinter();

  // Create the JIT instance.
  LLVMOrcLLJITRef J;
  {
    LLVMErrorRef Err;
    if ((Err = LLVMOrcCreateLLJIT(&J, 0))) {
      MainResult = handleError(Err);
      goto llvm_shutdown;
    }
  }

  // Create our demo module.
  LLVMOrcThreadSafeModuleRef TSM = createDemoModule();
  LLVMOrcResourceTrackerRef RT;

  // Add our demo module to the JIT.
  {
    LLVMOrcJITDylibRef MainJD = LLVMOrcLLJITGetMainJITDylib(J);
    RT = LLVMOrcJITDylibCreateResourceTracker(MainJD);
    LLVMErrorRef Err;
    if ((Err = LLVMOrcLLJITAddLLVMIRModuleWithRT(J, RT, TSM))) {
      // If adding the ThreadSafeModule fails then we need to clean it up
      // ourselves. If adding it succeeds the JIT will manage the memory.
      LLVMOrcDisposeThreadSafeModule(TSM);
      MainResult = handleError(Err);
      goto jit_cleanup;
    }
  }

  // Look up the address of our demo entry point.
  printf("Looking up before removal...\n");
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

  // Remove the code.
  {
    LLVMErrorRef Err;
    if ((Err = LLVMOrcResourceTrackerRemove(RT))) {
      MainResult = handleError(Err);
      goto jit_cleanup;
    }
  }

  // Attempt a second lookup. Here we expect an error as the code and symbols
  // should have been removed.
  printf("Attempting to remove code / symbols...\n");
  {
    LLVMOrcJITTargetAddress ThrowAwayAddress;
    LLVMErrorRef Err = LLVMOrcLLJITLookup(J, &ThrowAwayAddress, "sum");
    if (Err) {
      printf("Received error as expected:\n");
      handleError(Err);
    } else {
      printf("Failure: Second lookup should have generated an error.\n");
      MainResult = 1;
    }
  }

jit_cleanup:
  // Destroy our JIT instance. This will clean up any memory that the JIT has
  // taken ownership of. This operation is non-trivial (e.g. it may need to
  // JIT static destructors) and may also fail. In that case we want to render
  // the error to stderr, but not overwrite any existing return value.

  printf("Releasing resource tracker...\n");
  LLVMOrcReleaseResourceTracker(RT);

  printf("Destroying LLJIT instance and exiting.\n");
  {
    LLVMErrorRef Err;
    if ((Err = LLVMOrcDisposeLLJIT(J))) {
      int NewFailureResult = handleError(Err);
      if (MainResult == 0)
        MainResult = NewFailureResult;
    }
  }

llvm_shutdown:
  // Shut down LLVM.
  LLVMShutdown();

  return MainResult;
}
