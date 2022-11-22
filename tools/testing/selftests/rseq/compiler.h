/* SPDX-License-Identifier: LGPL-2.1-only OR MIT */
/*
 * rseq/compiler.h
 *
 * Work-around asm goto compiler bugs.
 *
 * (C) Copyright 2021 - Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 */

#ifndef RSEQ_COMPILER_H
#define RSEQ_COMPILER_H

/*
 * gcc prior to 4.8.2 miscompiles asm goto.
 * https://gcc.gnu.org/bugzilla/show_bug.cgi?id=58670
 *
 * gcc prior to 8.1.0 miscompiles asm goto at O1.
 * https://gcc.gnu.org/bugzilla/show_bug.cgi?id=103908
 *
 * clang prior to version 13.0.1 miscompiles asm goto at O2.
 * https://github.com/llvm/llvm-project/issues/52735
 *
 * Work around these issues by adding a volatile inline asm with
 * memory clobber in the fallthrough after the asm goto and at each
 * label target.  Emit this for all compilers in case other similar
 * issues are found in the future.
 */
#define rseq_after_asm_goto()	asm volatile ("" : : : "memory")

/* Combine two tokens. */
#define RSEQ__COMBINE_TOKENS(_tokena, _tokenb)	\
	_tokena##_tokenb
#define RSEQ_COMBINE_TOKENS(_tokena, _tokenb)	\
	RSEQ__COMBINE_TOKENS(_tokena, _tokenb)

#endif  /* RSEQ_COMPILER_H_ */
