//===-- dictionary.c - Generate fuzzing dictionary for clang --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This binary emits a fuzzing dictionary describing strings that are
// significant to the clang parser: keywords and other tokens.
//
// The dictionary can be used by a fuzzer to reach interesting parser states
// much more quickly.
//
// The output is a single-file dictionary supported by libFuzzer and AFL:
// https://llvm.org/docs/LibFuzzer.html#dictionaries
//
//===----------------------------------------------------------------------===//

#include <stdio.h>

static void emit(const char *Name, const char *Spelling) {
  static char Hex[] = "0123456789abcdef";
  // Skip EmptySpellingName for IsDeducible.
  if (!Name[0]) return;

  printf("%s=\"", Name);
  unsigned char C;
  while ((C = *Spelling++)) {
    if (C < 32 || C == '"' || C == '\\')
      printf("\\x%c%c", Hex[C>>4], Hex[C%16]);
    else
      printf("%c", C);
  }
  printf("\"\n");
}

int main(int argc, char **argv) {
#define PUNCTUATOR(Name, Spelling) emit(#Name, Spelling);
#define KEYWORD(Name, Criteria) emit(#Name, #Name);
#define PPKEYWORD(Name) emit(#Name, #Name);
#define CXX_KEYWORD_OPERATOR(Name, Equivalent) emit(#Name, #Name);
#define OBJC_AT_KEYWORD(Name) emit(#Name, #Name);
#define ALIAS(Spelling, Equivalent, Criteria) emit(Spelling, Spelling);
#include "clang/Basic/TokenKinds.def"
  // Some other sub-token chunks significant to the lexer.
  emit("ucn16", "\\u0000");
  emit("ucn32", "\\U00000000");
  emit("rawstart", "R\"(");
  emit("rawend", ")\"");
  emit("quote", "\"");
  emit("squote", "'");
  emit("u8quote", "u8\"");
  emit("u16quote", "u\"");
  emit("u32quote", "U\"");
  emit("esc_nl", "\\\n");
  emit("hex", "0x");
}

