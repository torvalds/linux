/* $OpenBSD: tfork_thread.S,v 1.8 2022/12/24 10:47:22 kettenis Exp $ */
/*
 * Copyright (c) 2005 Dale Rahn <drahn@openbsd.org>
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

#include "SYS.h"

/*
 *                r0     r1     r2         r3
 * __tfork_thread(param, psize, start_fnc, start_arg);
 */

ENTRY(__tfork_thread)
	.cfi_startproc
	RETGUARD_SETUP(__tfork_thread, x15)
	SYSTRAP(__tfork)
	.cfi_endproc
	bcs	CERROR

	/* check if we are parent or child */
	cbz	x0, 1f
	RETGUARD_CHECK(__tfork_thread, x15)
	ret

1:
	.cfi_startproc
	.cfi_undefined	lr
	/* child */
	mov	fp, #0
	mov	x0, x3
	blr	x2
	SYSTRAP(__threxit)
	udf	#0
	.cfi_endproc
END(__tfork_thread)
