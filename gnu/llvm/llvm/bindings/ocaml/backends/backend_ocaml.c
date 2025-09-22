/*===-- backend_ocaml.c - LLVM OCaml Glue -----------------------*- C++ -*-===*\
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

#include "llvm-c/Target.h"
#include "caml/alloc.h"
#include "caml/memory.h"

/* TODO: Figure out how to call these only for targets which support them.
 * LLVMInitialize ## target ## AsmPrinter();
 * LLVMInitialize ## target ## AsmParser();
 * LLVMInitialize ## target ## Disassembler();
 */

#define INITIALIZER1(target) \
  value llvm_initialize_ ## target(value Unit) {  \
    LLVMInitialize ## target ## TargetInfo();              \
    LLVMInitialize ## target ## Target();                  \
    LLVMInitialize ## target ## TargetMC();                \
    return Val_unit;                                       \
  }

#define INITIALIZER(target) INITIALIZER1(target)

INITIALIZER(TARGET)
