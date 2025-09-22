/*	$OpenBSD: fenv.S,v 1.2 2024/04/01 18:52:34 anton Exp $	*/
/*
 * Copyright (c) 2018 Alexander Bluhm <bluhm@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/syscall.h>

#define PINSYSCALL(sysno, label)					\
	.pushsection .openbsd.syscalls,"",%progbits;			\
	.p2align 2;							\
	.long label;							\
	.long sysno;							\
	.popsection;

	.section	.note.openbsd.ident, "a"
	.p2align	2
	.long		8
	.long		4
	.long		1
	.ascii		"OpenBSD\0"
	.long		0
	.previous

	.data
env:
	.long	1	/* __control */
	.long	1	/* __status */
	.long	1	/* __tag */
	.long	1	/* __others 0 */
	.long	1	/* __others 1 */
	.long	1	/* __others 2 */
	.long	1	/* __others 3 */
mxcsr:
	.long	1	/* __mxcsr */
env_end:

	.text
	.align	8
	.global	_start
_start:
	fnstenv	env
	stmxcsr	mxcsr
	mov	$SYS_write,	%rax
	mov	$1,		%rdi
	mov	$env,		%rsi
	mov	$(env_end-env),	%rdx
1:
	syscall
	PINSYSCALL(SYS_write, 1b)
	mov	$SYS_exit,	%rax
	mov	$0,		%rdi
2:
	syscall
	PINSYSCALL(SYS_exit, 2b)
