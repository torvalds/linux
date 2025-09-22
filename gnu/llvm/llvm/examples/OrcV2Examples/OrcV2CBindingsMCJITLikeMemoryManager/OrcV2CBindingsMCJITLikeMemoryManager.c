//=== OrcV2CBindingsMemoryManager.c - OrcV2 Memory Manager C Bindings Demo ===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This demo illustrates the C-API bindings for custom memory managers in
// ORCv2. They are used here to place generated code into manually allocated
// buffers that are subsequently marked as executable.
//
//===----------------------------------------------------------------------===//

#include "llvm-c/Core.h"
#include "llvm-c/Error.h"
#include "llvm-c/LLJIT.h"
#include "llvm-c/OrcEE.h"
#include "llvm-c/Support.h"
#include "llvm-c/Target.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#include <sys/mman.h>
#endif

struct Section {
  void *Ptr;
  size_t Size;
  LLVMBool IsCode;
};

char CtxCtxPlaceholder;
char CtxPlaceholder;

#define MaxSections 16
static size_t SectionCount = 0;
static struct Section Sections[MaxSections];

void *addSection(size_t Size, LLVMBool IsCode) {
  if (SectionCount >= MaxSections) {
    fprintf(stderr, "addSection(): Too many sections!\n");
    abort();
  }

#if defined(_WIN32)
  void *Ptr =
      VirtualAlloc(NULL, Size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  if (!Ptr) {
    fprintf(stderr, "addSection(): Memory allocation failed!\n");
    abort();
  }
#else
  void *Ptr = mmap(NULL, Size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (Ptr == MAP_FAILED) {
    fprintf(stderr, "addSection(): Memory allocation failed!\n");
    abort();
  }
#endif

  Sections[SectionCount].Ptr = Ptr;
  Sections[SectionCount].Size = Size;
  Sections[SectionCount].IsCode = IsCode;
  SectionCount++;
  return Ptr;
}

// Callbacks to create the context for the subsequent functions (not used in
// this example)
void *memCreateContext(void *CtxCtx) {
  assert(CtxCtx == &CtxCtxPlaceholder && "Unexpected CtxCtx value");
  return &CtxPlaceholder;
}

void memNotifyTerminating(void *CtxCtx) {
  assert(CtxCtx == &CtxCtxPlaceholder && "Unexpected CtxCtx value");
}

uint8_t *memAllocate(void *Opaque, uintptr_t Size, unsigned Align, unsigned Id,
                     const char *Name) {
  printf("Allocated code section \"%s\"\n", Name);
  return addSection(Size, 1);
}

uint8_t *memAllocateData(void *Opaque, uintptr_t Size, unsigned Align,
                         unsigned Id, const char *Name, LLVMBool ReadOnly) {
  printf("Allocated data section \"%s\"\n", Name);
  return addSection(Size, 0);
}

LLVMBool memFinalize(void *Opaque, char **Err) {
  printf("Marking code sections as executable ..\n");
  for (size_t i = 0; i < SectionCount; ++i) {
    if (Sections[i].IsCode) {
      LLVMBool fail;
#if defined(_WIN32)
      DWORD unused;
      fail = VirtualProtect(Sections[i].Ptr, Sections[i].Size,
                            PAGE_EXECUTE_READ, &unused) == 0;
#else
      fail = mprotect(Sections[i].Ptr, Sections[i].Size,
                      PROT_READ | PROT_EXEC) == -1;
#endif
      if (fail) {
        fprintf(stderr, "Could not mark code section as executable!\n");
        abort();
      }
    }
  }
  return 0;
}

void memDestroy(void *Opaque) {
  assert(Opaque == &CtxPlaceholder && "Unexpected Ctx value");
  printf("Releasing section memory ..\n");
  for (size_t i = 0; i < SectionCount; ++i) {
    LLVMBool fail;
#if defined(_WIN32)
    fail = VirtualFree(Sections[i].Ptr, 0, MEM_RELEASE) == 0;
#else
    fail = munmap(Sections[i].Ptr, Sections[i].Size) == -1;
#endif
    if (fail) {
      fprintf(stderr, "Could not release memory for section!");
      abort();
    }
  }
}

LLVMOrcObjectLayerRef objectLinkingLayerCreator(void *Opaque,
                                                LLVMOrcExecutionSessionRef ES,
                                                const char *Triple) {
  return LLVMOrcCreateRTDyldObjectLinkingLayerWithMCJITMemoryManagerLikeCallbacks(
      ES, &CtxCtxPlaceholder, memCreateContext, memNotifyTerminating,
      memAllocate, memAllocateData, memFinalize, memDestroy);
}

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

    LLVMOrcLLJITBuilderRef Builder = LLVMOrcCreateLLJITBuilder();
    LLVMOrcLLJITBuilderSetObjectLinkingLayerCreator(
        Builder, objectLinkingLayerCreator, NULL);

    if ((Err = LLVMOrcCreateLLJIT(&J, Builder))) {
      MainResult = handleError(Err);
      goto llvm_shutdown;
    }
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
