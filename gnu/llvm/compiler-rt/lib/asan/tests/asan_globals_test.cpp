//===-- asan_globals_test.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// Some globals in a separate file.
//===----------------------------------------------------------------------===//
#include "asan_test_utils.h"

char glob1[1];
char glob2[2];
char glob3[3];
char glob4[4];
char glob5[5];
char glob6[6];
char glob7[7];
char glob8[8];
char glob9[9];
char glob10[10];
char glob11[11];
char glob12[12];
char glob13[13];
char glob14[14];
char glob15[15];
char glob16[16];
char glob17[17];
char glob1000[1000];
char glob10000[10000];
char glob100000[100000];

static char static10[10];

int GlobalsTest(int zero) {
  static char func_static15[15];
  glob5[zero] = 0;
  static10[zero] = 0;
  func_static15[zero] = 0;
  return glob5[1] + func_static15[2];
}
