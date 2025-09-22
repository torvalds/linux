/*	$OpenBSD: tfork_thread.S,v 1.6 2024/03/27 20:03:29 miod Exp $	*/

/*
 * Copyright (c) 2007 Miodrag Vallat.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice, this permission notice, and the disclaimer below
 * appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "SYS.h"

/*
 * int __tfork_thread(const struct __tfork *param, size_t psize, void (*func)(void *), void *arg);
 *			r4			r5		   r6		r7
 */
ENTRY(__tfork_thread)
	systrap SYS___tfork
	bf	9f

	tst	r0, r0
	bt	1f

	/*
	 * In parent process: just return.
	 */
	rts
	 nop

	systrap_data SYS___tfork

1:
	/*
	 * In child process: invoke function, then exit.
	 */
	jsr	@r6
	 mov	r7, r4

	systrap SYS___threxit
	systrap_data SYS___threxit

9:
	/*
	 * System call failure.
	 */
	SET_ERRNO_AND_RETURN(__tfork)

	SET_ENTRY_SIZE(__tfork_thread)
