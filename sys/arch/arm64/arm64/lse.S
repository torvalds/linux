/*	$OpenBSD: lse.S,v 1.1 2024/07/02 10:25:16 kettenis Exp $	*/
/*
 * Copyright (c) 2024 Mark Kettenis <kettenis@openbsd.org>
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

#include <machine/asm.h>

/*
 * Out-of-line LSE atomics helpers
 */
	
.arch	armv8-a+lse

ENTRY(__aarch64_cas4_acq_rel)
	RETGUARD_SETUP(__aarch64_cas4_acq_rel, x15)
	adrp	x9, arm64_has_lse 
	ldr	w9, [x9, :lo12:arm64_has_lse]
	cbz	w9, 1f
	casal	w0, w1, [x2]
	RETGUARD_CHECK(__aarch64_cas4_acq_rel, x15)
	ret
1:
	ldaxr	w9, [x2]
	cmp	w9, w0
	b.ne	2f
	stlxr	w10, w1, [x2]
	cbnz	w10, 1b
2:
	mov	w0, w9
	RETGUARD_CHECK(__aarch64_cas4_acq_rel, x15)
	ret
END(__aarch64_cas4_acq_rel)

ENTRY(__aarch64_cas8_acq_rel)
	RETGUARD_SETUP(__aarch64_cas8_acq_rel, x15)
	adrp	x9, arm64_has_lse 
	ldr	w9, [x9, :lo12:arm64_has_lse]
	cbz	w9, 1f
	casal	x0, x1, [x2]
	RETGUARD_CHECK(__aarch64_cas8_acq_rel, x15)
	ret
1:
	ldaxr	x9, [x2]
	cmp	x9, x0
	b.ne	2f
	stlxr	w10, x1, [x2]
	cbnz	w10, 1b
2:
	mov	x0, x9
	RETGUARD_CHECK(__aarch64_cas8_acq_rel, x15)
	ret
END(__aarch64_cas8_acq_rel)

ENTRY(__aarch64_ldadd4_acq_rel)
	RETGUARD_SETUP(__aarch64_ldadd4_acq_rel, x15)
	adrp	x9, arm64_has_lse 
	ldr	w9, [x9, :lo12:arm64_has_lse]
	cbz	w9, 1f
	ldaddal	w0, w0, [x1]
	RETGUARD_CHECK(__aarch64_ldadd4_acq_rel, x15)
	ret
1:
	ldaxr	w9, [x1]
	add	w11, w9, w0
	stlxr	w10, w11, [x1]
	cbnz	w10, 1b
	mov	w0, w9
	RETGUARD_CHECK(__aarch64_ldadd4_acq_rel, x15)
	ret
END(__aarch64_ldadd4_acq_rel)

ENTRY(__aarch64_ldadd8_acq_rel)
	RETGUARD_SETUP(__aarch64_ldadd8_acq_rel, x15)
	adrp	x9, arm64_has_lse 
	ldr	w9, [x9, :lo12:arm64_has_lse]
	cbz	w9, 1f
	ldaddal	x0, x0, [x1]
	RETGUARD_CHECK(__aarch64_ldadd8_acq_rel, x15)
	ret
1:
	ldaxr	x9, [x1]
	add	x11, x9, x0	
	stlxr	w10, x11, [x1]
	cbnz	w10, 1b
	mov	x0, x9
	RETGUARD_CHECK(__aarch64_ldadd8_acq_rel, x15)
	ret
END(__aarch64_ldadd8_acq_rel)

ENTRY(__aarch64_ldclr4_acq_rel)
	RETGUARD_SETUP(__aarch64_ldclr4_acq_rel, x15)
	adrp	x9, arm64_has_lse 
	ldr	w9, [x9, :lo12:arm64_has_lse]
	cbz	w9, 1f
	ldclral	w0, w0, [x1]
	RETGUARD_CHECK(__aarch64_ldclr4_acq_rel, x15)
	ret
1:
	ldaxr	w9, [x1]
	bic	w11, w9, w0
	stlxr	w10, w11, [x1]
	cbnz	w10, 1b
	mov	w0, w9
	RETGUARD_CHECK(__aarch64_ldclr4_acq_rel, x15)
	ret
END(__aarch64_ldclr4_acq_rel)

ENTRY(__aarch64_ldset4_acq_rel)
	RETGUARD_SETUP(__aarch64_ldset4_acq_rel, x15)
	adrp	x9, arm64_has_lse 
	ldr	w9, [x9, :lo12:arm64_has_lse]
	cbz	w9, 1f
	ldsetal	w0, w0, [x1]
	RETGUARD_CHECK(__aarch64_ldset4_acq_rel, x15)
	ret
1:
	ldaxr	w9, [x1]
	orr	w11, w9, w0
	stlxr	w10, w11, [x1]
	cbnz	w10, 1b
	mov	w0, w9
	RETGUARD_CHECK(__aarch64_ldset4_acq_rel, x15)
	ret
END(__aarch64_ldset4_acq_rel)

ENTRY(__aarch64_swp4_acq_rel)
	RETGUARD_SETUP(__aarch64_swp4_acq_rel, x15)
	adrp	x9, arm64_has_lse 
	ldr	w9, [x9, :lo12:arm64_has_lse]
	cbz	w9, 1f
	swpal	w0, w0, [x1]
	RETGUARD_CHECK(__aarch64_swp4_acq_rel, x15)
	ret
1:
	ldaxr	w9, [x1]
	stlxr	w10, w0, [x1]
	cbnz	w10, 1b
	mov	w0, w9
	RETGUARD_CHECK(__aarch64_swp4_acq_rel, x15)
	ret
END(__aarch64_swp4_acq_rel)

ENTRY(__aarch64_swp8_acq_rel)
	RETGUARD_SETUP(__aarch64_swp8_acq_rel, x15)
	adrp	x9, arm64_has_lse 
	ldr	w9, [x9, :lo12:arm64_has_lse]
	cbz	w9, 1f
	swpal	x0, x0, [x1]
	RETGUARD_CHECK(__aarch64_swp8_acq_rel, x15)
	ret
1:
	ldaxr	x9, [x1]
	stlxr	w10, x0, [x1]
	cbnz	w10, 1b
	mov	x0, x9
	RETGUARD_CHECK(__aarch64_swp8_acq_rel, x15)
	ret
END(__aarch64_swp8_acq_rel)
