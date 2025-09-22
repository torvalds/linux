/* $OpenBSD: sigpending.S,v 1.3 2023/12/10 16:45:52 deraadt Exp $ */
/*
 * Copyright (c) 2012 Mark Kettenis
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

/* sigpending(sigset_t *set); */

#include "SYS.h"

	.text
SYSENTRY(sigpending)
	RETGUARD_SETUP(sigpending, %r11)
	li	%r0, SYS_sigpending

	mr	%r5, %r3
99:	sc
	PINSYSCALL(SYS_sigpending, 99b)
	stw	%r3, 0(%r5)
	li	%r3, 0
	RETGUARD_CHECK(sigpending, %r11)
	blr
SYSCALL_END_HIDDEN(sigpending)
