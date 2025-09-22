/*===-- irreader_ocaml.c - LLVM OCaml Glue ----------------------*- C++ -*-===*\
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
#include "llvm-c/IRReader.h"

void llvm_raise(value Prototype, char *Message);

/* Llvm.llcontext -> Llvm.llmemorybuffer -> Llvm.llmodule */
value llvm_parse_ir(value C, value MemBuf) {
  CAMLparam0();
  CAMLlocal2(Variant, MessageVal);
  LLVMModuleRef M;
  char *Message;

  if (LLVMParseIRInContext(Context_val(C), MemoryBuffer_val(MemBuf), &M,
                           &Message))
    llvm_raise(*caml_named_value("Llvm_irreader.Error"), Message);

  CAMLreturn(to_val(M));
}
