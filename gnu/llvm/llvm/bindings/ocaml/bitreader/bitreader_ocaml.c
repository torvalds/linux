/*===-- bitwriter_ocaml.c - LLVM OCaml Glue ---------------------*- C++ -*-===*\
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
\*===----------------------------------------------------------------------===*/

#include "caml/alloc.h"
#include "caml/callback.h"
#include "caml/fail.h"
#include "caml/memory.h"
#include "llvm_ocaml.h"
#include "llvm-c/BitReader.h"
#include "llvm-c/Core.h"

void llvm_raise(value Prototype, char *Message);

/* Llvm.llcontext -> Llvm.llmemorybuffer -> Llvm.llmodule */
value llvm_get_module(value C, value MemBuf) {
  LLVMModuleRef M;

  if (LLVMGetBitcodeModuleInContext2(Context_val(C), MemoryBuffer_val(MemBuf),
                                     &M))
    llvm_raise(*caml_named_value("Llvm_bitreader.Error"),
               LLVMCreateMessage(""));

  return to_val(M);
}

/* Llvm.llcontext -> Llvm.llmemorybuffer -> Llvm.llmodule */
value llvm_parse_bitcode(value C, value MemBuf) {
  LLVMModuleRef M;

  if (LLVMParseBitcodeInContext2(Context_val(C), MemoryBuffer_val(MemBuf), &M))
    llvm_raise(*caml_named_value("Llvm_bitreader.Error"),
               LLVMCreateMessage(""));

  return to_val(M);
}
