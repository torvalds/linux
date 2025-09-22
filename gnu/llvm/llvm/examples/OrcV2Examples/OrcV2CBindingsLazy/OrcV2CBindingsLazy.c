//===-------- BasicOrcV2CBindings.c - Basic OrcV2 C Bindings Demo ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm-c/Core.h"
#include "llvm-c/Error.h"
#include "llvm-c/IRReader.h"
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

// Example IR modules.
//
// Note that in the conditionally compiled modules, FooMod and BarMod, functions
// have been given an _body suffix. This is to ensure that their names do not
// clash with their lazy-reexports.
// For clients who do not wish to rename function bodies (e.g. because they want
// to re-use cached objects between static and JIT compiles) techniques exist to
// avoid renaming. See the lazy-reexports section of the ORCv2 design doc.

const char FooMod[] = "  define i32 @foo_body() { \n"
                      "  entry:                   \n"
                      "    ret i32 1              \n"
                      "  }                        \n";

const char BarMod[] = "  define i32 @bar_body() { \n"
                      "  entry:                   \n"
                      "    ret i32 2              \n"
                      "  }                        \n";

const char MainMod[] =
    "  define i32 @entry(i32 %argc) {                                 \n"
    "  entry:                                                         \n"
    "    %and = and i32 %argc, 1                                      \n"
    "    %tobool = icmp eq i32 %and, 0                                \n"
    "    br i1 %tobool, label %if.end, label %if.then                 \n"
    "                                                                 \n"
    "  if.then:                                                       \n"
    "    %call = tail call i32 @foo()                                 \n"
    "    br label %return                                             \n"
    "                                                                 \n"
    "  if.end:                                                        \n"
    "    %call1 = tail call i32 @bar()                                \n"
    "    br label %return                                             \n"
    "                                                                 \n"
    "  return:                                                        \n"
    "    %retval.0 = phi i32 [ %call, %if.then ], [ %call1, %if.end ] \n"
    "    ret i32 %retval.0                                            \n"
    "  }                                                              \n"
    "                                                                 \n"
    "  declare i32 @foo()                                             \n"
    "  declare i32 @bar()                                             \n";

LLVMErrorRef parseExampleModule(const char *Source, size_t Len,
                                const char *Name,
                                LLVMOrcThreadSafeModuleRef *TSM) {
  // Create a new ThreadSafeContext and underlying LLVMContext.
  LLVMOrcThreadSafeContextRef TSCtx = LLVMOrcCreateNewThreadSafeContext();

  // Get a reference to the underlying LLVMContext.
  LLVMContextRef Ctx = LLVMOrcThreadSafeContextGetContext(TSCtx);

  // Wrap Source in a MemoryBuffer
  LLVMMemoryBufferRef MB =
      LLVMCreateMemoryBufferWithMemoryRange(Source, Len, Name, 0);

  // Parse the LLVM module.
  LLVMModuleRef M;
  char *ErrMsg;
  if (LLVMParseIRInContext(Ctx, MB, &M, &ErrMsg)) {
    return LLVMCreateStringError(ErrMsg);
    // TODO: LLVMDisposeMessage(ErrMsg);
  }

  // Our module is now complete. Wrap it and our ThreadSafeContext in a
  // ThreadSafeModule.
  *TSM = LLVMOrcCreateNewThreadSafeModule(M, TSCtx);

  // Dispose of our local ThreadSafeContext value. The underlying LLVMContext
  // will be kept alive by our ThreadSafeModule, TSM.
  LLVMOrcDisposeThreadSafeContext(TSCtx);

  return LLVMErrorSuccess;
}

int main(int argc, const char *argv[]) {

  int MainResult = 0;

  // Parse command line arguments and initialize LLVM Core.
  LLVMParseCommandLineOptions(argc, argv, "");

  // Initialize native target codegen and asm printer.
  LLVMInitializeNativeTarget();
  LLVMInitializeNativeAsmPrinter();

  // Set up a JIT instance.
  LLVMOrcLLJITRef J;
  const char *TargetTriple;
  {
    LLVMErrorRef Err;
    if ((Err = LLVMOrcCreateLLJIT(&J, 0))) {
      MainResult = handleError(Err);
      goto llvm_shutdown;
    }
    TargetTriple = LLVMOrcLLJITGetTripleString(J);
  }

  // Add our demo modules to the JIT.
  {
    LLVMOrcJITDylibRef MainJD = LLVMOrcLLJITGetMainJITDylib(J);
    LLVMErrorRef Err;

    LLVMOrcThreadSafeModuleRef FooTSM;
    if ((Err = parseExampleModule(FooMod, sizeof(FooMod) - 1, "foo-mod",
                                  &FooTSM))) {
      MainResult = handleError(Err);
      goto jit_cleanup;
    }

    if ((Err = LLVMOrcLLJITAddLLVMIRModule(J, MainJD, FooTSM))) {
      // If adding the ThreadSafeModule fails then we need to clean it up
      // ourselves. If adding it succeeds the JIT will manage the memory.
      LLVMOrcDisposeThreadSafeModule(FooTSM);
      MainResult = handleError(Err);
      goto jit_cleanup;
    }

    LLVMOrcThreadSafeModuleRef BarTSM;
    if ((Err = parseExampleModule(BarMod, sizeof(BarMod) - 1, "bar-mod",
                                  &BarTSM))) {
      MainResult = handleError(Err);
      goto jit_cleanup;
    }

    if ((Err = LLVMOrcLLJITAddLLVMIRModule(J, MainJD, BarTSM))) {
      LLVMOrcDisposeThreadSafeModule(BarTSM);
      MainResult = handleError(Err);
      goto jit_cleanup;
    }

    LLVMOrcThreadSafeModuleRef MainTSM;
    if ((Err = parseExampleModule(MainMod, sizeof(MainMod) - 1, "main-mod",
                                  &MainTSM))) {
      MainResult = handleError(Err);
      goto jit_cleanup;
    }

    if ((Err = LLVMOrcLLJITAddLLVMIRModule(J, MainJD, MainTSM))) {
      LLVMOrcDisposeThreadSafeModule(MainTSM);
      MainResult = handleError(Err);
      goto jit_cleanup;
    }
  }

  // add lazy reexports
  LLVMOrcIndirectStubsManagerRef ISM =
      LLVMOrcCreateLocalIndirectStubsManager(TargetTriple);

  LLVMOrcLazyCallThroughManagerRef LCTM;
  {
    LLVMErrorRef Err;
    LLVMOrcExecutionSessionRef ES = LLVMOrcLLJITGetExecutionSession(J);
    if ((Err = LLVMOrcCreateLocalLazyCallThroughManager(TargetTriple, ES, 0,
                                                        &LCTM))) {
      LLVMOrcDisposeIndirectStubsManager(ISM);
      MainResult = handleError(Err);
      goto jit_cleanup;
    }
  }

  LLVMJITSymbolFlags flag = {
      LLVMJITSymbolGenericFlagsExported | LLVMJITSymbolGenericFlagsCallable, 0};
  LLVMOrcCSymbolAliasMapPair ReExports[2] = {
      {LLVMOrcLLJITMangleAndIntern(J, "foo"),
       {LLVMOrcLLJITMangleAndIntern(J, "foo_body"), flag}},
      {LLVMOrcLLJITMangleAndIntern(J, "bar"),
       {LLVMOrcLLJITMangleAndIntern(J, "bar_body"), flag}},
  };

  {
    LLVMOrcJITDylibRef MainJD = LLVMOrcLLJITGetMainJITDylib(J);
    LLVMOrcMaterializationUnitRef MU =
        LLVMOrcLazyReexports(LCTM, ISM, MainJD, ReExports, 2);
    LLVMOrcJITDylibDefine(MainJD, MU);
  }

  // Look up the address of our demo entry point.
  LLVMOrcJITTargetAddress EntryAddr;
  {
    LLVMErrorRef Err;
    if ((Err = LLVMOrcLLJITLookup(J, &EntryAddr, "entry"))) {
      MainResult = handleError(Err);
      goto cleanup;
    }
  }

  // If we made it here then everything succeeded. Execute our JIT'd code.
  int32_t (*Entry)(int32_t) = (int32_t(*)(int32_t))EntryAddr;
  int32_t Result = Entry(argc);

  printf("--- Result ---\n");
  printf("entry(%i) = %i\n", argc, Result);

cleanup : {
  LLVMOrcDisposeIndirectStubsManager(ISM);
  LLVMOrcDisposeLazyCallThroughManager(LCTM);
}

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
