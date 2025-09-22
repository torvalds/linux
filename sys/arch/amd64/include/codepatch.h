/*      $OpenBSD: codepatch.h,v 1.19 2024/02/12 01:18:17 guenther Exp $    */
/*
 * Copyright (c) 2014-2015 Stefan Fritsch <sf@sfritsch.de>
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

#ifndef _MACHINE_CODEPATCH_H_
#define _MACHINE_CODEPATCH_H_

#include <machine/param.h>

#ifndef _LOCORE

/* code in this section will be unmapped after boot */
#define __cptext __attribute__((section(".cptext")))

__cptext void *codepatch_maprw(vaddr_t *nva, vaddr_t dest);
__cptext void codepatch_unmaprw(vaddr_t nva);
__cptext void codepatch_fill_nop(void *caddr, uint16_t len);
__cptext void codepatch_nop(uint16_t tag);
__cptext void codepatch_replace(uint16_t tag, const void *code, size_t len);
__cptext void codepatch_call(uint16_t _tag, void *_func);
__cptext void codepatch_jmp(uint16_t _tag, void *_func);
void codepatch_disable(void);

#endif /* !_LOCORE */

/*
 * Mark the start of some code snippet to be patched.
 */
#define	CODEPATCH_START	998:
/*
 * Mark the end of some code to be patched, and assign the given tag.
 */
#define	CODEPATCH_END2(startnum,tag)		 \
	999:					 \
	.section .codepatch, "a"		;\
	.quad startnum##b			;\
	.short (999b - startnum##b)		;\
	.short tag				;\
	.int 0					;\
	.previous
#define	CODEPATCH_END(tag)	CODEPATCH_END2(998,tag)

#define CPTAG_STAC		1
#define CPTAG_CLAC		2
#define CPTAG_EOI		3
#define CPTAG_XRSTOR		4
#define CPTAG_XSAVE		5
#define CPTAG_MELTDOWN_NOP	6
#define CPTAG_MELTDOWN_ALLTRAPS	7
#define CPTAG_PCID_SET_REUSE	8
#define CPTAG_MDS		9
#define CPTAG_MDS_VMM		10
#define CPTAG_FENCE_SWAPGS_MIS_TAKEN	11
#define CPTAG_FENCE_NO_SAFE_SMAP	12
#define CPTAG_XRSTORS			13
#define CPTAG_RETPOLINE_RAX		14
#define CPTAG_RETPOLINE_R11		15
#define CPTAG_RETPOLINE_R13		16
#define CPTAG_IBPB_NOP			17

/*
 * stac/clac SMAP instructions have lfence like semantics.  Let's
 * guarantee those semantics on older cpus.
 */
#define SMAP_NOP	lfence
#define SMAP_STAC	CODEPATCH_START			;\
			SMAP_NOP			;\
			CODEPATCH_END(CPTAG_STAC)
#define SMAP_CLAC	CODEPATCH_START			;\
			SMAP_NOP			;\
			CODEPATCH_END(CPTAG_CLAC)

/* CVE-2019-1125: block speculation after swapgs */
#define	FENCE_SWAPGS_MIS_TAKEN \
	CODEPATCH_START				; \
	lfence					; \
	CODEPATCH_END(CPTAG_FENCE_SWAPGS_MIS_TAKEN)
/* block speculation when a correct SMAP impl would have been enough */
#define	FENCE_NO_SAFE_SMAP \
	CODEPATCH_START				; \
	lfence					; \
	CODEPATCH_END(CPTAG_FENCE_NO_SAFE_SMAP)

#define	PCID_SET_REUSE_SIZE	12
#define	PCID_SET_REUSE_NOP					\
	997:							;\
	.byte	0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00	;\
	.byte	0x0f, 0x1f, 0x40, 0x00				;\
	CODEPATCH_END2(997, CPTAG_PCID_SET_REUSE)

/* Would be neat if these could be in something like .cptext */
#define CODEPATCH_CODE(symbol, instructions...)		\
	.section .rodata;				\
	.globl	symbol;					\
symbol:	instructions;					\
	.size	symbol, . - symbol

/* provide a (short) variable with the length of the patch */
#define CODEPATCH_CODE_LEN(symbol, instructions...)	\
	CODEPATCH_CODE(symbol, instructions);		\
996:	.globl	symbol##_len;				\
	.align	2;					\
symbol##_len:						\
	.short	996b - symbol;				\
	.size	symbol##_len, 2

#endif /* _MACHINE_CODEPATCH_H_ */
