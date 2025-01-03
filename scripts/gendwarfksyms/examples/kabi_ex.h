/* SPDX-License-Identifier: GPL-2.0 */
/*
 * kabi_ex.h
 *
 * Copyright (C) 2024 Google LLC
 *
 * Examples for kABI stability features with --stable.
 */

/*
 * The comments below each example contain the expected gendwarfksyms
 * output, which can be verified using LLVM's FileCheck tool:
 *
 * https://llvm.org/docs/CommandGuide/FileCheck.html
 *
 * Usage:
 *
 * $ gcc -g -c examples/kabi_ex.c -o examples/kabi_ex.o
 *
 * $ nm examples/kabi_ex.o | awk '{ print $NF }' | \
 * 	./gendwarfksyms --stable --dump-dies \
 * 		examples/kabi_ex.o 2>&1 >/dev/null | \
 * 	FileCheck examples/kabi_ex.h --check-prefix=STABLE
 */

#ifndef __KABI_EX_H__
#define __KABI_EX_H__

#include "kabi.h"

/*
 * Example: kABI rules
 */

struct s {
	int a;
};

KABI_DECLONLY(s);

/*
 * STABLE:      variable structure_type s {
 * STABLE-NEXT: }
 */

enum e {
	A,
	B,
	C,
	D,
};

KABI_ENUMERATOR_IGNORE(e, B);
KABI_ENUMERATOR_IGNORE(e, C);
KABI_ENUMERATOR_VALUE(e, D, 123456789);

/*
 * STABLE:      variable enumeration_type e {
 * STABLE-NEXT:   enumerator A = 0 ,
 * STABLE-NEXT:   enumerator D = 123456789
 * STABLE-NEXT: } byte_size(4)
 */

#endif /* __KABI_EX_H__ */
