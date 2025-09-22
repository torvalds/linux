/*===-- llvm-c-test.h - tool for testing libLLVM and llvm-c API -----------===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions.                                                                *|
|* See https://llvm.org/LICENSE.txt for license information.                  *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* Header file for llvm-c-test                                                *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/
#ifndef LLVM_C_TEST_H
#define LLVM_C_TEST_H

#include <stdbool.h>
#include "llvm-c/Core.h"

#ifdef __cplusplus
extern "C" {
#endif

// helpers.c
void llvm_tokenize_stdin(void (*cb)(char **tokens, int ntokens));

// module.c
LLVMModuleRef llvm_load_module(bool Lazy, bool New);
int llvm_module_dump(bool Lazy, bool New);
int llvm_module_list_functions(void);
int llvm_module_list_globals(void);

// calc.c
int llvm_calc(void);

// disassemble.c
int llvm_disassemble(void);

// debuginfo.c
int llvm_test_dibuilder(void);
int llvm_get_di_tag(void);
int llvm_di_type_get_name(void);

// metadata.c
int llvm_add_named_metadata_operand(void);
int llvm_set_metadata(void);
int llvm_replace_md_operand(void);
int llvm_is_a_value_as_metadata(void);

// object.c
int llvm_object_list_sections(void);
int llvm_object_list_symbols(void);

// targets.c
int llvm_targets_list(void);

// echo.c
int llvm_echo(void);

// diagnostic.c
int llvm_test_diagnostic_handler(void);

// attributes.c
int llvm_test_function_attributes(void);
int llvm_test_callsite_attributes(void);

#ifdef __cplusplus
}
#endif /* !defined(__cplusplus) */

#endif
