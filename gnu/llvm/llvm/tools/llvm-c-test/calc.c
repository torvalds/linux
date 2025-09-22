/*===-- calc.c - tool for testing libLLVM and llvm-c API ------------------===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions.                                                                *|
|* See https://llvm.org/LICENSE.txt for license information.                  *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* This file implements the --calc command in llvm-c-test. --calc reads lines *|
|* from stdin, parses them as a name and an expression in reverse polish      *|
|* notation and prints a module with a function with the expression.          *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#include "llvm-c-test.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

typedef LLVMValueRef (*binop_func_t)(LLVMBuilderRef, LLVMValueRef LHS,
                                     LLVMValueRef RHS, const char *Name);

static LLVMOpcode op_to_opcode(char op) {
  switch (op) {
  case '+': return LLVMAdd;
  case '-': return LLVMSub;
  case '*': return LLVMMul;
  case '/': return LLVMSDiv;
  case '&': return LLVMAnd;
  case '|': return LLVMOr;
  case '^': return LLVMXor;
  }
  assert(0 && "unknown operation");
  return 0;
}

#define MAX_DEPTH 32

static LLVMValueRef build_from_tokens(char **tokens, int ntokens,
                                      LLVMBuilderRef builder,
                                      LLVMValueRef param) {
  LLVMValueRef stack[MAX_DEPTH];
  int depth = 0;
  int i;

  for (i = 0; i < ntokens; i++) {
    char tok = tokens[i][0];
    switch (tok) {
    case '+':
    case '-':
    case '*':
    case '/':
    case '&':
    case '|':
    case '^':
      if (depth < 2) {
        printf("stack underflow\n");
        return NULL;
      }

      stack[depth - 2] = LLVMBuildBinOp(builder, op_to_opcode(tok),
                                        stack[depth - 1], stack[depth - 2], "");
      depth--;

      break;

    case '@': {
      LLVMValueRef off;

      if (depth < 1) {
        printf("stack underflow\n");
        return NULL;
      }

      LLVMTypeRef ty = LLVMInt64Type();
      off = LLVMBuildGEP2(builder, ty, param, &stack[depth - 1], 1, "");
      stack[depth - 1] = LLVMBuildLoad2(builder, ty, off, "");

      break;
    }

    default: {
      char *end;
      long val = strtol(tokens[i], &end, 0);
      if (end[0] != '\0') {
        printf("error parsing number\n");
        return NULL;
      }

      if (depth >= MAX_DEPTH) {
        printf("stack overflow\n");
        return NULL;
      }

      stack[depth++] = LLVMConstInt(LLVMInt64Type(), val, 1);
      break;
    }
    }
  }

  if (depth < 1) {
    printf("stack underflow at return\n");
    return NULL;
  }

  LLVMBuildRet(builder, stack[depth - 1]);

  return stack[depth - 1];
}

static void handle_line(char **tokens, int ntokens) {
  char *name = tokens[0];
  LLVMValueRef param;
  LLVMValueRef res;

  LLVMModuleRef M = LLVMModuleCreateWithName(name);

  LLVMTypeRef I64ty = LLVMInt64Type();
  LLVMTypeRef I64Ptrty = LLVMPointerType(I64ty, 0);
  LLVMTypeRef Fty = LLVMFunctionType(I64ty, &I64Ptrty, 1, 0);

  LLVMValueRef F = LLVMAddFunction(M, name, Fty);
  LLVMBuilderRef builder = LLVMCreateBuilder();
  LLVMPositionBuilderAtEnd(builder, LLVMAppendBasicBlock(F, "entry"));

  LLVMGetParams(F, &param);
  LLVMSetValueName(param, "in");

  res = build_from_tokens(tokens + 1, ntokens - 1, builder, param);
  if (res) {
    char *irstr = LLVMPrintModuleToString(M);
    puts(irstr);
    LLVMDisposeMessage(irstr);
  }

  LLVMDisposeBuilder(builder);

  LLVMDisposeModule(M);
}

int llvm_calc(void) {

  llvm_tokenize_stdin(handle_line);

  return 0;
}
