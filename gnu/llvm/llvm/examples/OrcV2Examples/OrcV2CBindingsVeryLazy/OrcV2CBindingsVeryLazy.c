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

#include <assert.h>
#include <stdio.h>
#include <string.h>

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

LLVMErrorRef applyDataLayout(void *Ctx, LLVMModuleRef M) {
  LLVMSetDataLayout(M, LLVMOrcLLJITGetDataLayoutStr((LLVMOrcLLJITRef)Ctx));
  return LLVMErrorSuccess;
}

LLVMErrorRef parseExampleModule(const char *Source, size_t Len,
                                const char *Name,
                                LLVMOrcThreadSafeModuleRef *TSM) {
  // Create a new ThreadSafeContext and underlying LLVMContext.
  LLVMOrcThreadSafeContextRef TSCtx = LLVMOrcCreateNewThreadSafeContext();

  // Get a reference to the underlying LLVMContext.
  LLVMContextRef Ctx = LLVMOrcThreadSafeContextGetContext(TSCtx);

  // Wrap Source in a MemoryBuffer
  LLVMMemoryBufferRef MB =
      LLVMCreateMemoryBufferWithMemoryRange(Source, Len, Name, 1);

  // Parse the LLVM module.
  LLVMModuleRef M;
  char *ErrMsg;
  if (LLVMParseIRInContext(Ctx, MB, &M, &ErrMsg)) {
    LLVMErrorRef Err = LLVMCreateStringError(ErrMsg);
    LLVMDisposeMessage(ErrMsg);
    return Err;
  }

  // Our module is now complete. Wrap it and our ThreadSafeContext in a
  // ThreadSafeModule.
  *TSM = LLVMOrcCreateNewThreadSafeModule(M, TSCtx);

  // Dispose of our local ThreadSafeContext value. The underlying LLVMContext
  // will be kept alive by our ThreadSafeModule, TSM.
  LLVMOrcDisposeThreadSafeContext(TSCtx);

  return LLVMErrorSuccess;
}

void Destroy(void *Ctx) {}

void Materialize(void *Ctx, LLVMOrcMaterializationResponsibilityRef MR) {
  int MainResult = 0;

  size_t NumSymbols;
  LLVMOrcSymbolStringPoolEntryRef *Symbols =
      LLVMOrcMaterializationResponsibilityGetRequestedSymbols(MR, &NumSymbols);

  assert(NumSymbols == 1);

  LLVMOrcLLJITRef J = (LLVMOrcLLJITRef)Ctx;
  LLVMOrcSymbolStringPoolEntryRef Sym = Symbols[0];

  LLVMOrcThreadSafeModuleRef TSM = 0;
  LLVMErrorRef Err;

  LLVMOrcSymbolStringPoolEntryRef FooBody =
      LLVMOrcLLJITMangleAndIntern(J, "foo_body");
  LLVMOrcSymbolStringPoolEntryRef BarBody =
      LLVMOrcLLJITMangleAndIntern(J, "bar_body");

  if (Sym == FooBody) {
    if ((Err = parseExampleModule(FooMod, strlen(FooMod), "foo-mod", &TSM))) {
      MainResult = handleError(Err);
      goto cleanup;
    }
  } else if (Sym == BarBody) {
    if ((Err = parseExampleModule(BarMod, strlen(BarMod), "bar-mod", &TSM))) {
      MainResult = handleError(Err);
      goto cleanup;
    }
  } else {
    MainResult = 1;
    goto cleanup;
  }
  assert(TSM);

  if ((Err = LLVMOrcThreadSafeModuleWithModuleDo(TSM, &applyDataLayout, Ctx))) {
    MainResult = handleError(Err);
    goto cleanup;
  }

cleanup:
  LLVMOrcReleaseSymbolStringPoolEntry(BarBody);
  LLVMOrcReleaseSymbolStringPoolEntry(FooBody);
  LLVMOrcDisposeSymbols(Symbols);
  if (MainResult == 1) {
    LLVMOrcMaterializationResponsibilityFailMaterialization(MR);
    LLVMOrcDisposeMaterializationResponsibility(MR);
  } else {
    LLVMOrcIRTransformLayerRef IRLayer = LLVMOrcLLJITGetIRTransformLayer(J);
    LLVMOrcIRTransformLayerEmit(IRLayer, MR, TSM);
  }
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

  // Add our main module to the JIT.
  {
    LLVMOrcJITDylibRef MainJD = LLVMOrcLLJITGetMainJITDylib(J);
    LLVMErrorRef Err;

    LLVMOrcThreadSafeModuleRef MainTSM;
    if ((Err = parseExampleModule(MainMod, strlen(MainMod), "main-mod",
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

  LLVMJITSymbolFlags Flags = {
      LLVMJITSymbolGenericFlagsExported | LLVMJITSymbolGenericFlagsCallable, 0};
  LLVMOrcCSymbolFlagsMapPair FooSym = {
      LLVMOrcLLJITMangleAndIntern(J, "foo_body"), Flags};
  LLVMOrcCSymbolFlagsMapPair BarSym = {
      LLVMOrcLLJITMangleAndIntern(J, "bar_body"), Flags};

  // add custom MaterializationUnit
  {
    LLVMOrcMaterializationUnitRef FooMU =
        LLVMOrcCreateCustomMaterializationUnit("FooMU", J, &FooSym, 1, NULL,
                                               &Materialize, NULL, &Destroy);

    LLVMOrcMaterializationUnitRef BarMU =
        LLVMOrcCreateCustomMaterializationUnit("BarMU", J, &BarSym, 1, NULL,
                                               &Materialize, NULL, &Destroy);

    LLVMOrcJITDylibRef MainJD = LLVMOrcLLJITGetMainJITDylib(J);
    LLVMOrcJITDylibDefine(MainJD, FooMU);
    LLVMOrcJITDylibDefine(MainJD, BarMU);
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

  LLVMOrcCSymbolAliasMapPair ReExports[2] = {
      {LLVMOrcLLJITMangleAndIntern(J, "foo"),
       {LLVMOrcLLJITMangleAndIntern(J, "foo_body"), Flags}},
      {LLVMOrcLLJITMangleAndIntern(J, "bar"),
       {LLVMOrcLLJITMangleAndIntern(J, "bar_body"), Flags}},
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
