/*===-- executionengine_ocaml.c - LLVM OCaml Glue ---------------*- C++ -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions.                                                                *|
|* See https://llvm.org/LICENSE.txt for license information.                  *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* This file glues LLVM's OCaml interface to its C interface. These functions *|
|* are by and large transparent wrappers to the corresponding C functions.    *|
|*                                                                            *|
|* Note that these functions intentionally take liberties with the CAMLparamX *|
|* macros, since most of the parameters are not GC heap objects.              *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#include "caml/alloc.h"
#include "caml/callback.h"
#include "caml/custom.h"
#include "caml/fail.h"
#include "caml/memory.h"
#include "llvm_ocaml.h"
#include "llvm-c/Core.h"
#include "llvm-c/ExecutionEngine.h"
#include "llvm-c/Target.h"
#include <assert.h>
#include <string.h>

#define ExecutionEngine_val(v) ((LLVMExecutionEngineRef)from_val(v))

void llvm_raise(value Prototype, char *Message);

/* unit -> bool */
value llvm_ee_initialize(value Unit) {
  LLVMLinkInMCJIT();

  return Val_bool(!LLVMInitializeNativeTarget() &&
                  !LLVMInitializeNativeAsmParser() &&
                  !LLVMInitializeNativeAsmPrinter());
}

/* llcompileroption -> llmodule -> ExecutionEngine.t */
value llvm_ee_create(value OptRecordOpt, value M) {
  LLVMExecutionEngineRef MCJIT;
  char *Error;
  struct LLVMMCJITCompilerOptions Options;

  LLVMInitializeMCJITCompilerOptions(&Options, sizeof(Options));
  if (OptRecordOpt != Val_int(0)) {
    value OptRecord = Field(OptRecordOpt, 0);
    Options.OptLevel = Int_val(Field(OptRecord, 0));
    Options.CodeModel = Int_val(Field(OptRecord, 1));
    Options.NoFramePointerElim = Int_val(Field(OptRecord, 2));
    Options.EnableFastISel = Int_val(Field(OptRecord, 3));
    Options.MCJMM = NULL;
  }

  if (LLVMCreateMCJITCompilerForModule(&MCJIT, Module_val(M), &Options,
                                       sizeof(Options), &Error))
    llvm_raise(*caml_named_value("Llvm_executionengine.Error"), Error);
  return to_val(MCJIT);
}

/* ExecutionEngine.t -> unit */
value llvm_ee_dispose(value EE) {
  LLVMDisposeExecutionEngine(ExecutionEngine_val(EE));
  return Val_unit;
}

/* llmodule -> ExecutionEngine.t -> unit */
value llvm_ee_add_module(value M, value EE) {
  LLVMAddModule(ExecutionEngine_val(EE), Module_val(M));
  return Val_unit;
}

/* llmodule -> ExecutionEngine.t -> llmodule */
value llvm_ee_remove_module(value M, value EE) {
  LLVMModuleRef RemovedModule;
  char *Error;
  if (LLVMRemoveModule(ExecutionEngine_val(EE), Module_val(M), &RemovedModule,
                       &Error))
    llvm_raise(*caml_named_value("Llvm_executionengine.Error"), Error);
  return Val_unit;
}

/* ExecutionEngine.t -> unit */
value llvm_ee_run_static_ctors(value EE) {
  LLVMRunStaticConstructors(ExecutionEngine_val(EE));
  return Val_unit;
}

/* ExecutionEngine.t -> unit */
value llvm_ee_run_static_dtors(value EE) {
  LLVMRunStaticDestructors(ExecutionEngine_val(EE));
  return Val_unit;
}

extern value llvm_alloc_data_layout(LLVMTargetDataRef TargetData);

/* ExecutionEngine.t -> Llvm_target.DataLayout.t */
value llvm_ee_get_data_layout(value EE) {
  value DataLayout;
  LLVMTargetDataRef OrigDataLayout;
  char *TargetDataCStr;

  OrigDataLayout = LLVMGetExecutionEngineTargetData(ExecutionEngine_val(EE));
  TargetDataCStr = LLVMCopyStringRepOfTargetData(OrigDataLayout);
  DataLayout = llvm_alloc_data_layout(LLVMCreateTargetData(TargetDataCStr));
  LLVMDisposeMessage(TargetDataCStr);

  return DataLayout;
}

/* Llvm.llvalue -> int64 -> llexecutionengine -> unit */
value llvm_ee_add_global_mapping(value Global, value Ptr, value EE) {
  LLVMAddGlobalMapping(ExecutionEngine_val(EE), Value_val(Global),
                       (void *)(Int64_val(Ptr)));
  return Val_unit;
}

value llvm_ee_get_global_value_address(value Name, value EE) {
  return caml_copy_int64((int64_t)LLVMGetGlobalValueAddress(
      ExecutionEngine_val(EE), String_val(Name)));
}

value llvm_ee_get_function_address(value Name, value EE) {
  return caml_copy_int64((int64_t)LLVMGetFunctionAddress(
      ExecutionEngine_val(EE), String_val(Name)));
}
