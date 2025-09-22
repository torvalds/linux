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
#include "llvm-c/TargetMachine.h"

#include <stdio.h>

int handleError(LLVMErrorRef Err) {
  char *ErrMsg = LLVMGetErrorMessage(Err);
  fprintf(stderr, "Error: %s\n", ErrMsg);
  LLVMDisposeErrorMessage(ErrMsg);
  return 1;
}

LLVMModuleRef createDemoModule(LLVMContextRef Ctx) {
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

  return M;
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

  // Create our demo object file.
  LLVMMemoryBufferRef ObjectFileBuffer;
  {
    // Create a module.
    LLVMContextRef Ctx = LLVMContextCreate();
    LLVMModuleRef M = createDemoModule(Ctx);

    // Get the Target.
    const char *Triple = LLVMOrcLLJITGetTripleString(J);
    LLVMTargetRef Target = 0;
    char *ErrorMsg = 0;
    if (LLVMGetTargetFromTriple(Triple, &Target, &ErrorMsg)) {
      fprintf(stderr, "Error getting target for %s: %s\n", Triple, ErrorMsg);
      LLVMDisposeModule(M);
      LLVMContextDispose(Ctx);
      goto jit_cleanup;
    }

    // Construct a TargetMachine.
    LLVMTargetMachineRef TM =
        LLVMCreateTargetMachine(Target, Triple, "", "", LLVMCodeGenLevelNone,
                                LLVMRelocDefault, LLVMCodeModelDefault);

    // Run CodeGen to produce the buffer.
    if (LLVMTargetMachineEmitToMemoryBuffer(TM, M, LLVMObjectFile, &ErrorMsg,
                                            &ObjectFileBuffer)) {
      fprintf(stderr, "Error emitting object: %s\n", ErrorMsg);
      LLVMDisposeTargetMachine(TM);
      LLVMDisposeModule(M);
      LLVMContextDispose(Ctx);
      goto jit_cleanup;
    }

    // CodeGen succeeded -- We have our module, so free the Module, LLVMContext,
    // and TargetMachine.
    LLVMDisposeModule(M);
    LLVMContextDispose(Ctx);
    LLVMDisposeTargetMachine(TM);
  }

  // Add our object file buffer to the JIT.
  {
    LLVMOrcJITDylibRef MainJD = LLVMOrcLLJITGetMainJITDylib(J);
    LLVMErrorRef Err;
    if ((Err = LLVMOrcLLJITAddObjectFile(J, MainJD, ObjectFileBuffer))) {
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
  // Destroy our JIT instance. This will clean up any memory that the JIT has
  // taken ownership of. This operation is non-trivial (e.g. it may need to
  // JIT static destructors) and may also fail. In that case we want to render
  // the error to stderr, but not overwrite any existing return value.
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
