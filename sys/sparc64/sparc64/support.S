/*-
 * Copyright (c) 2001 Jake Burkholder.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <machine/asm.h>
__FBSDID("$FreeBSD$");

#include "opt_kstack_pages.h"

#include <sys/errno.h>

#include <machine/asi.h>
#include <machine/asmacros.h>
#include <machine/fsr.h>
#include <machine/intr_machdep.h>
#include <machine/pcb.h>
#include <machine/pstate.h>
#include <machine/wstate.h>

#include "assym.inc"

	.register %g2, #ignore
	.register %g3, #ignore
	.register %g6, #ignore

/*
 * Common code for copy routines.
 *
 * We use large macros to generate functions for each of the copy routines.
 * This allows the load and store instructions to be generated for the right
 * operation, asi or not.  It is possible to write an asi independent function
 * but this would require 2 expensive wrs in the main loop to switch %asi.
 * It would also screw up profiling (if we ever get it), but may save some I$.
 * We assume that either one of dasi and sasi is empty, or that they are both
 * the same (empty or non-empty).  It is up to the caller to set %asi.
 */

/*
 * ASI independent implementation of copystr(9).
 * Used to implement copyinstr() and copystr().
 *
 * Return value is in %g1.
 */
#define	_COPYSTR(src, dst, len, done, sa, sasi, da, dasi) \
	brz	len, 4f ; \
	 mov	src, %g2 ; \
1:	deccc	1, len ; \
	bl,a,pn	%xcc, 3f ; \
	 nop ; \
	LD(ub, sa) [src] sasi, %g1 ; \
	ST(b, da) %g1, [dst] dasi ; \
	brz,pn	%g1, 3f ; \
	 inc	src ; \
	ba	%xcc, 1b ; \
	 inc	dst ; \
2:	mov	ENAMETOOLONG, %g1 ; \
3:	sub	src, %g2, %g2 ; \
	brnz,a	done, 4f ; \
	 stx	%g2, [done] ; \
4:

/*
 * ASI independent implementation of memset(3).
 * Used to implement bzero(), memset() and aszero().
 *
 * If the pattern is non-zero, duplicate it to fill 64 bits.
 * Store bytes until dst is 8-byte aligned, then store 8 bytes.
 * It has yet to be determined how much unrolling is beneficial.
 * Could also read and compare before writing to minimize snoop traffic.
 *
 * XXX bzero() should be implemented as
 * #define bzero(dst, len) (void)memset((dst), 0, (len))
 * if at all.
 */
#define	_MEMSET(dst, pat, len, da, dasi) \
	brlez,pn len, 5f ; \
	 and	pat, 0xff, pat ; \
	brz,pt	pat, 1f ; \
	 sllx	pat, 8, %g1 ; \
	or	pat, %g1, pat ; \
	sllx	pat, 16, %g1 ; \
	or	pat, %g1, pat ; \
	sllx	pat, 32, %g1 ; \
	or	pat, %g1, pat ; \
	.align 16 ; \
1:	deccc	1, len ; \
	bl,pn	%xcc, 5f ; \
	 btst	7, dst ; \
	bz,a,pt	%xcc, 2f ; \
	 inc	1, len ; \
	ST(b, da) pat, [dst] dasi ; \
	ba	%xcc, 1b ; \
	 inc	dst ; \
	.align 16 ; \
2:	deccc	32, len ; \
	bl,a,pn	%xcc, 3f ; \
	 inc	32, len ; \
	ST(x, da) pat, [dst] dasi ; \
	ST(x, da) pat, [dst + 8] dasi ; \
	ST(x, da) pat, [dst + 16] dasi ; \
	ST(x, da) pat, [dst + 24] dasi ; \
	ba	%xcc, 2b ; \
	 inc	32, dst ; \
	.align 16 ; \
3:	deccc	8, len ; \
	bl,a,pn	%xcc, 4f ; \
	 inc	8, len ; \
	ST(x, da) pat, [dst] dasi ; \
	ba	%xcc, 3b ; \
	 inc	8, dst ; \
	.align 16 ; \
4:	deccc	1, len ; \
	bl,a,pn	%xcc, 5f ; \
	 nop ; \
	ST(b, da) pat, [dst] dasi ; \
	ba	%xcc, 4b ; \
	 inc	1, dst ; \
5:

/*
 * ASI independent implementation of memcpy(3).
 * Used to implement bcopy(), copyin(), copyout(), memcpy(), ascopy(),
 * ascopyfrom() and ascopyto().
 *
 * Transfer bytes until dst is 8-byte aligned.  If src is then also 8 byte
 * aligned, transfer 8 bytes, otherwise finish with bytes.  The unaligned
 * case could be optimized, but it is expected that this is the uncommon
 * case and of questionable value.  The code to do so is also rather large
 * and ugly.  It has yet to be determined how much unrolling is beneficial.
 *
 * XXX bcopy() must also check for overlap.  This is stupid.
 * XXX bcopy() should be implemented as
 * #define bcopy(src, dst, len) (void)memcpy((dst), (src), (len))
 * if at all.
 */
#define	_MEMCPY(dst, src, len, da, dasi, sa, sasi) \
1:	deccc	1, len ; \
	bl,pn	%xcc, 6f ; \
	 btst	7, dst ; \
	bz,a,pt	%xcc, 2f ; \
	 inc	1, len ; \
	LD(ub, sa) [src] sasi, %g1 ; \
	ST(b, da) %g1, [dst] dasi ; \
	inc	1, src ; \
	ba	%xcc, 1b ; \
	 inc	1, dst ; \
	.align 16 ; \
2:	btst	7, src ; \
	bz,a,pt	%xcc, 3f ; \
	 nop ; \
	ba,a	%xcc, 5f ; \
	.align 16 ; \
3:	deccc	32, len ; \
	bl,a,pn	%xcc, 4f ; \
	 inc	32, len ; \
	LD(x, sa) [src] sasi, %g1 ; \
	LD(x, sa) [src + 8] sasi, %g2 ; \
	LD(x, sa) [src + 16] sasi, %g3 ; \
	LD(x, sa) [src + 24] sasi, %g4 ; \
	ST(x, da) %g1, [dst] dasi ; \
	ST(x, da) %g2, [dst + 8] dasi ; \
	ST(x, da) %g3, [dst + 16] dasi ; \
	ST(x, da) %g4, [dst + 24] dasi ; \
	inc	32, src ; \
	ba	%xcc, 3b ; \
	 inc	32, dst ; \
	.align 16 ; \
4:	deccc	8, len ; \
	bl,a,pn	%xcc, 5f ; \
	 inc	8, len ; \
	LD(x, sa) [src] sasi, %g1 ; \
	ST(x, da) %g1, [dst] dasi ; \
	inc	8, src ; \
	ba	%xcc, 4b ; \
	 inc	8, dst ; \
	.align 16 ; \
5:	deccc	1, len ; \
	bl,a,pn	%xcc, 6f ; \
	 nop ; \
	LD(ub, sa) [src] sasi, %g1 ; \
	ST(b, da) %g1, [dst] dasi ; \
	inc	src ; \
	ba	%xcc, 5b ; \
	 inc	dst ; \
6:

/*
 * Extension of _MEMCPY dealing with overlap, but unaware of ASIs.
 * Used for bcopy() and memmove().
 */
#define	_MEMMOVE(dst, src, len) \
	/* Check for overlap, and copy backwards if so. */ \
	sub	dst, src, %g1 ; \
	cmp	%g1, len ; \
	bgeu,a,pt %xcc, 2f ; \
	 nop ; \
	/* Copy backwards. */ \
	add	src, len, src ; \
	add	dst, len, dst ; \
1:	deccc	1, len ; \
	bl,pn	%xcc, 3f ; \
	 dec	1, src ; \
	ldub	[src], %g1 ; \
	dec	1, dst ; \
	ba	%xcc, 1b ; \
	 stb	%g1, [dst] ; \
2:	/* Do the fast version. */ \
	_MEMCPY(dst, src, len, EMPTY, EMPTY, EMPTY, EMPTY) ; \
3:

/*
 * void ascopy(u_long asi, vm_offset_t src, vm_offset_t dst, size_t len)
 */
ENTRY(ascopy)
	wr	%o0, 0, %asi
	_MEMCPY(%o2, %o1, %o3, a, %asi, a, %asi)
	retl
	 nop
END(ascopy)

/*
 * void ascopyfrom(u_long sasi, vm_offset_t src, caddr_t dst, size_t len)
 */
ENTRY(ascopyfrom)
	wr	%o0, 0, %asi
	_MEMCPY(%o2, %o1, %o3, EMPTY, EMPTY, a, %asi)
	retl
	 nop
END(ascopyfrom)

/*
 * void ascopyto(caddr_t src, u_long dasi, vm_offset_t dst, size_t len)
 */
ENTRY(ascopyto)
	wr	%o1, 0, %asi
	_MEMCPY(%o2, %o0, %o3, a, %asi, EMPTY, EMPTY)
	retl
	 nop
END(ascopyto)

/*
 * void aszero(u_long asi, vm_offset_t pa, size_t len)
 */
ENTRY(aszero)
	wr	%o0, 0, %asi
	_MEMSET(%o1, %g0, %o2, a, %asi)
	retl
	 nop
END(aszero)

/*
 * int bcmp(const void *b1, const void *b2, size_t len)
 */
ENTRY(bcmp)
	brz,pn	%o2, 2f
	 clr	%o3
1:	ldub	[%o0 + %o3], %o4
	ldub	[%o1 + %o3], %o5
	cmp	%o4, %o5
	bne,pn	%xcc, 2f
	 inc	%o3
	deccc	%o2
	bne,pt	%xcc, 1b
	 nop
2:	retl
	 mov	%o2, %o0
END(bcmp)

/*
 * void bcopy(const void *src, void *dst, size_t len)
 */
ENTRY(bcopy)
	_MEMMOVE(%o1, %o0, %o2)
	retl
	 nop
END(bcopy)

/*
 * void bzero(void *b, size_t len)
 */
ENTRY(bzero)
	_MEMSET(%o0, %g0, %o1, EMPTY, EMPTY)
	retl
	 nop
END(bzero)

/*
 * int copystr(const void *src, void *dst, size_t len, size_t *done)
 */
ENTRY(copystr)
	_COPYSTR(%o0, %o1, %o2, %o3, EMPTY, EMPTY, EMPTY, EMPTY)
	retl
	 mov	%g1, %o0
END(copystr)

/*
 * void *memcpy(void *dst, const void *src, size_t len)
 */
ENTRY(memcpy)
	mov	%o0, %o3
	_MEMCPY(%o3, %o1, %o2, EMPTY, EMPTY, EMPTY, EMPTY)
	retl
	 nop
END(memcpy)

/*
 * void *memmove(void *dst, const void *src, size_t len)
 */
ENTRY(memmove)
	mov	%o0, %o3
	_MEMMOVE(%o3, %o1, %o2)
	retl
	 nop
END(memmove)

/*
 * void *memset(void *b, int c, size_t len)
 */
ENTRY(memset)
	mov	%o0, %o3
	_MEMSET(%o3, %o1, %o2, EMPTY, EMPTY)
	retl
	 nop
END(memset)

	.globl	copy_nofault_begin
copy_nofault_begin:
	nop

/*
 * int copyin(const void *uaddr, void *kaddr, size_t len)
 */
ENTRY(copyin)
	wr	%g0, ASI_AIUP, %asi
	_MEMCPY(%o1, %o0, %o2, EMPTY, EMPTY, a, %asi)
	retl
	 clr	%o0
END(copyin)

/*
 * int copyinstr(const void *uaddr, void *kaddr, size_t len, size_t *done)
 */
ENTRY(copyinstr)
	wr	%g0, ASI_AIUP, %asi
	_COPYSTR(%o0, %o1, %o2, %o3, a, %asi, EMPTY, EMPTY)
	retl
	 mov	%g1, %o0
END(copyinstr)

/*
 * int copyout(const void *kaddr, void *uaddr, size_t len)
 */
ENTRY(copyout)
	wr	%g0, ASI_AIUP, %asi
	_MEMCPY(%o1, %o0, %o2, a, %asi, EMPTY, EMPTY)
	retl
	 clr	%o0
END(copyout)

	.globl	copy_nofault_end
copy_nofault_end:
	nop

ENTRY(copy_fault)
	retl
	 mov	EFAULT, %o0
END(copy_fault)

	.globl	fs_nofault_begin
fs_nofault_begin:
	nop

/*
 * Chatty aliases for fetch, store functions.
 */
	.globl	fubyte, fusword, fuword, subyte, susword, suword
	.set	fubyte, fuword8
	.set	fusword, fuword16
	.set	fuword, fuword64
	.set	subyte, suword8
	.set	susword, suword16
	.set	suword, suword64

	.globl	casuword32, casuword, fuptr, suptr
	.set	casuword, casuword64
	.set	fuptr, fuword64
	.set	suptr, suword64

/*
 * int32_t casuword32(volatile int32_t *p, int32_t e, int32_t s)
 */
ENTRY(casuword32)
	casa	[%o0] ASI_AIUP, %o1, %o2
	retl
	 mov	%o2, %o0
END(casuword32)

/*
 * int64_t casuword64(volatile int64_t *p, int64_t e, int64_t s)
 */
ENTRY(casuword64)
	casxa	[%o0] ASI_AIUP, %o1, %o2
	retl
	 mov	%o2, %o0
END(casuword64)

/*
 * int fuword8(const void *base)
 */
ENTRY(fuword8)
	retl
	 lduba	[%o0] ASI_AIUP, %o0
END(fuword8)

/*
 * int fuword16(const void *base)
 */
ENTRY(fuword16)
	retl
	 lduha	[%o0] ASI_AIUP, %o0
END(fuword16)

/*
 * int32_t fuword32(const void *base)
 */
ENTRY(fuword32)
	retl
	 lduwa	[%o0] ASI_AIUP, %o0
END(fuword32)

/*
 * int64_t fuword64(const void *base)
 */
ENTRY(fuword64)
	retl
	 ldxa	[%o0] ASI_AIUP, %o0
END(fuword64)

/*
 * int suword8(const void *base, int word)
 */
ENTRY(suword8)
	stba	%o1, [%o0] ASI_AIUP
	retl
	 clr	%o0
END(suword8)

/*
 * int suword16(const void *base, int word)
 */
ENTRY(suword16)
	stha	%o1, [%o0] ASI_AIUP
	retl
	 clr	%o0
END(suword16)

/*
 * int suword32(const void *base, int32_t word)
 */
ENTRY(suword32)
	stwa	%o1, [%o0] ASI_AIUP
	retl
	 clr	%o0
END(suword32)

/*
 * int suword64(const void *base, int64_t word)
 */
ENTRY(suword64)
	stxa	%o1, [%o0] ASI_AIUP
	retl
	 clr	%o0
END(suword64)

	.globl	fs_nofault_end
fs_nofault_end:
	nop

ENTRY(fs_fault)
	retl
	 mov	-1, %o0
END(fs_fault)

	.globl	fas_nofault_begin
fas_nofault_begin:

/*
 * int fasword8(u_long asi, uint64_t addr, uint8_t *val)
 */
ENTRY(fasword8)
	wr	%o0, 0, %asi
	membar	#Sync
	lduba	[%o1] %asi, %o3
	membar	#Sync
	stb	%o3, [%o2]
	retl
	 clr	%o0
END(fasword8)

/*
 * int fasword16(u_long asi, uint64_t addr, uint16_t *val)
 */
ENTRY(fasword16)
	wr	%o0, 0, %asi
	membar	#Sync
	lduha	[%o1] %asi, %o3
	membar	#Sync
	sth	%o3, [%o2]
	retl
	 clr	%o0
END(fasword16)

/*
 * int fasword32(u_long asi, uint64_t addr, uint32_t *val)
 */
ENTRY(fasword32)
	wr	%o0, 0, %asi
	membar	#Sync
	lduwa	[%o1] %asi, %o3
	membar	#Sync
	stw	%o3, [%o2]
	retl
	 clr	%o0
END(fasword32)

	.globl	fas_nofault_end
fas_nofault_end:
	nop

	.globl	fas_fault
ENTRY(fas_fault)
	retl
	 mov	-1, %o0
END(fas_fault)

	.globl	fpu_fault_begin
fpu_fault_begin:
	nop

/*
 * void spitfire_block_copy(void *src, void *dst, size_t len)
 */
ENTRY(spitfire_block_copy)
	rdpr	%pstate, %o3
	wrpr	%g0, PSTATE_NORMAL, %pstate

	wr	%g0, ASI_BLK_S, %asi
	wr	%g0, FPRS_FEF, %fprs

	sub	PCB_REG, TF_SIZEOF, %o4
	ldx	[%o4 + TF_FPRS], %o5
	andcc	%o5, FPRS_FEF, %g0
	bz,a,pt	%xcc, 1f
	 nop
	stda	%f0, [PCB_REG + PCB_UFP + (0 * VIS_BLOCKSIZE)] %asi
	stda	%f16, [PCB_REG + PCB_UFP + (1 * VIS_BLOCKSIZE)] %asi
	stda	%f32, [PCB_REG + PCB_UFP + (2 * VIS_BLOCKSIZE)] %asi
	stda	%f48, [PCB_REG + PCB_UFP + (3 * VIS_BLOCKSIZE)] %asi
	membar	#Sync

	andn	%o5, FPRS_FEF, %o5
	stx	%o5, [%o4 + TF_FPRS]
	ldx	[PCB_REG + PCB_FLAGS], %o4
	or	%o4, PCB_FEF, %o4
	stx	%o4, [PCB_REG + PCB_FLAGS]

1:	wrpr	%o3, 0, %pstate

	ldda	[%o0] %asi, %f0
	add	%o0, VIS_BLOCKSIZE, %o0
	sub	%o2, VIS_BLOCKSIZE, %o2

2:	ldda	[%o0] %asi, %f16
	fsrc1	%f0, %f32
	fsrc1	%f2, %f34
	fsrc1	%f4, %f36
	fsrc1	%f6, %f38
	fsrc1	%f8, %f40
	fsrc1	%f10, %f42
	fsrc1	%f12, %f44
	fsrc1	%f14, %f46
	stda	%f32, [%o1] %asi
	add	%o0, VIS_BLOCKSIZE, %o0
	subcc	%o2, VIS_BLOCKSIZE, %o2
	bz,pn	%xcc, 3f
	 add	%o1, VIS_BLOCKSIZE, %o1
	ldda	[%o0] %asi, %f0
	fsrc1	%f16, %f32
	fsrc1	%f18, %f34
	fsrc1	%f20, %f36
	fsrc1	%f22, %f38
	fsrc1	%f24, %f40
	fsrc1	%f26, %f42
	fsrc1	%f28, %f44
	fsrc1	%f30, %f46
	stda	%f32, [%o1] %asi
	add	%o0, VIS_BLOCKSIZE, %o0
	sub	%o2, VIS_BLOCKSIZE, %o2
	ba,pt	%xcc, 2b
	 add	%o1, VIS_BLOCKSIZE, %o1

3:	membar	#Sync

	stda	%f16, [%o1] %asi
	membar	#Sync

	retl
	 wr	%g0, 0, %fprs
END(spitfire_block_copy)

/*
 * void zeus_block_copy(void *src, void *dst, size_t len)
 */
ENTRY(zeus_block_copy)
	prefetch [%o0 + (0 * VIS_BLOCKSIZE)], 0

	rdpr	%pstate, %o3
	wrpr	%g0, PSTATE_NORMAL, %pstate

	wr	%g0, ASI_BLK_S, %asi
	wr	%g0, FPRS_FEF, %fprs

	sub	PCB_REG, TF_SIZEOF, %o4
	ldx	[%o4 + TF_FPRS], %o5
	andcc	%o5, FPRS_FEF, %g0
	bz,a,pt	%xcc, 1f
	 nop
	stda	%f0, [PCB_REG + PCB_UFP + (0 * VIS_BLOCKSIZE)] %asi
	stda	%f16, [PCB_REG + PCB_UFP + (1 * VIS_BLOCKSIZE)] %asi
	stda	%f32, [PCB_REG + PCB_UFP + (2 * VIS_BLOCKSIZE)] %asi
	stda	%f48, [PCB_REG + PCB_UFP + (3 * VIS_BLOCKSIZE)] %asi
	membar	#Sync

	andn	%o5, FPRS_FEF, %o5
	stx	%o5, [%o4 + TF_FPRS]
	ldx	[PCB_REG + PCB_FLAGS], %o4
	or	%o4, PCB_FEF, %o4
	stx	%o4, [PCB_REG + PCB_FLAGS]

1:	wrpr	%o3, 0, %pstate

	ldd	[%o0 + (0 * 8)], %f0
	prefetch [%o0 + (1 * VIS_BLOCKSIZE)], 0
	ldd	[%o0 + (1 * 8)], %f2
	prefetch [%o0 + (2 * VIS_BLOCKSIZE)], 0
	fmovd	%f0, %f32
	ldd	[%o0 + (2 * 8)], %f4
	prefetch [%o0 + (3 * VIS_BLOCKSIZE)], 0
	fmovd	%f2, %f34
	ldd	[%o0 + (3 * 8)], %f6
	prefetch [%o0 + (4 * VIS_BLOCKSIZE)], 1
	fmovd	%f4, %f36
	ldd	[%o0 + (4 * 8)], %f8
	prefetch [%o0 + (8 * VIS_BLOCKSIZE)], 1
	fmovd	%f6, %f38
	ldd	[%o0 + (5 * 8)], %f10
	prefetch [%o0 + (12 * VIS_BLOCKSIZE)], 1
	fmovd	%f8, %f40
	ldd	[%o0 + (6 * 8)], %f12
	prefetch [%o0 + (16 * VIS_BLOCKSIZE)], 1
	fmovd	%f10, %f42
	ldd	[%o0 + (7 * 8)], %f14
	ldd	[%o0 + (8 * 8)], %f0
	sub	%o2, VIS_BLOCKSIZE, %o2
	add	%o0, VIS_BLOCKSIZE, %o0
	prefetch [%o0 + (19 * VIS_BLOCKSIZE)], 1
	ba,pt	%xcc, 2f
	 prefetch [%o0 + (23 * VIS_BLOCKSIZE)], 1
	.align	32

2:	ldd	[%o0 + (1 * 8)], %f2
	fmovd	%f12, %f44
	ldd	[%o0 + (2 * 8)], %f4
	fmovd	%f14, %f46
	stda	%f32, [%o1] %asi
	ldd	[%o0 + (3 * 8)], %f6
	fmovd	%f0, %f32
	ldd	[%o0 + (4 * 8)], %f8
	fmovd	%f2, %f34
	ldd	[%o0 + (5 * 8)], %f10
	fmovd	%f4, %f36
	ldd	[%o0 + (6 * 8)], %f12
	fmovd	%f6, %f38
	ldd	[%o0 + (7 * 8)], %f14
	fmovd	%f8, %f40
	ldd	[%o0 + (8 * 8)], %f0
	fmovd	%f10, %f42
	sub	%o2, VIS_BLOCKSIZE, %o2
	prefetch [%o0 + (3 * VIS_BLOCKSIZE)], 0
	add	%o1, VIS_BLOCKSIZE, %o1
	prefetch [%o0 + (24 * VIS_BLOCKSIZE)], 1
	add	%o0, VIS_BLOCKSIZE, %o0
	cmp	%o2, VIS_BLOCKSIZE + 8
	bgu,pt	%xcc, 2b
	 prefetch [%o0 + (12 * VIS_BLOCKSIZE)], 1
	ldd	[%o0 + (1 * 8)], %f2
	fsrc1	%f12, %f44
	ldd	[%o0 + (2 * 8)], %f4
	fsrc1	%f14, %f46
	stda	%f32, [%o1] %asi
	ldd	[%o0 + (3 * 8)], %f6
	fsrc1	%f0, %f32
	ldd	[%o0 + (4 * 8)], %f8
	fsrc1	%f2, %f34
	ldd	[%o0 + (5 * 8)], %f10
	fsrc1	%f4, %f36
	ldd	[%o0 + (6 * 8)], %f12
	fsrc1	%f6, %f38
	ldd	[%o0 + (7 * 8)], %f14
	fsrc1	%f8, %f40
	add	%o1, VIS_BLOCKSIZE, %o1
	fsrc1	%f10, %f42
	fsrc1	%f12, %f44
	fsrc1	%f14, %f46
	stda	%f32, [%o1] %asi
	membar	#Sync

	retl
	 wr	%g0, 0, %fprs
END(zeus_block_copy)

/*
 * void spitfire_block_zero(void *dst, size_t len)
 * void zeus_block_zero(void *dst, size_t len)
 */
ALTENTRY(zeus_block_zero)
ENTRY(spitfire_block_zero)
	rdpr	%pstate, %o3
	wrpr	%g0, PSTATE_NORMAL, %pstate

	wr	%g0, ASI_BLK_S, %asi
	wr	%g0, FPRS_FEF, %fprs

	sub	PCB_REG, TF_SIZEOF, %o4
	ldx	[%o4 + TF_FPRS], %o5
	andcc	%o5, FPRS_FEF, %g0
	bz,a,pt	%xcc, 1f
	 nop
	stda	%f0, [PCB_REG + PCB_UFP + (0 * VIS_BLOCKSIZE)] %asi
	stda	%f16, [PCB_REG + PCB_UFP + (1 * VIS_BLOCKSIZE)] %asi
	stda	%f32, [PCB_REG + PCB_UFP + (2 * VIS_BLOCKSIZE)] %asi
	stda	%f48, [PCB_REG + PCB_UFP + (3 * VIS_BLOCKSIZE)] %asi
	membar	#Sync

	andn	%o5, FPRS_FEF, %o5
	stx	%o5, [%o4 + TF_FPRS]
	ldx	[PCB_REG + PCB_FLAGS], %o4
	or	%o4, PCB_FEF, %o4
	stx	%o4, [PCB_REG + PCB_FLAGS]

1:	wrpr	%o3, 0, %pstate

	fzero	%f0
	fzero	%f2
	fzero	%f4
	fzero	%f6
	fzero	%f8
	fzero	%f10
	fzero	%f12
	fzero	%f14

1:	stda	%f0, [%o0 + (0 * VIS_BLOCKSIZE)] %asi
	stda	%f0, [%o0 + (1 * VIS_BLOCKSIZE)] %asi
	stda	%f0, [%o0 + (2 * VIS_BLOCKSIZE)] %asi
	stda	%f0, [%o0 + (3 * VIS_BLOCKSIZE)] %asi
	sub	%o1, (4 * VIS_BLOCKSIZE), %o1
	brnz,pt	%o1, 1b
	 add	%o0, (4 * VIS_BLOCKSIZE), %o0
	membar	#Sync

	retl
	 wr	%g0, 0, %fprs
END(spitfire_block_zero)

	.globl	fpu_fault_end
fpu_fault_end:
	nop

	.globl	fpu_fault_size
	.set	fpu_fault_size, fpu_fault_end - fpu_fault_begin

ENTRY(longjmp)
	set	1, %g3
	movrz	%o1, %o1, %g3
	mov	%o0, %g1
	ldx	[%g1 + _JB_FP], %g2
1:	cmp	%fp, %g2
	bl,a,pt	%xcc, 1b
	 restore
	bne,pn	%xcc, 2f
	 ldx	[%g1 + _JB_SP], %o2
	cmp	%o2, %sp
	blt,pn	%xcc, 2f
	 movge	%xcc, %o2, %sp
	ldx	[%g1 + _JB_PC], %o7
	retl
	 mov	%g3, %o0
2:	PANIC("longjmp botch", %l1)
END(longjmp)

ENTRY(setjmp)
	stx	%sp, [%o0 + _JB_SP]
	stx	%o7, [%o0 + _JB_PC]
	stx	%fp, [%o0 + _JB_FP]
	retl
	 clr	%o0
END(setjmp)

/*
 * void ofw_entry(cell_t args[])
 */
ENTRY(ofw_entry)
	save	%sp, -CCFSZ, %sp
	SET(ofw_vec, %l7, %l6)
	ldx	[%l6], %l6
	rdpr	%pstate, %l7
	andn	%l7, PSTATE_AM | PSTATE_IE, %l5
	wrpr	%l5, 0, %pstate
	SET(tba_taken_over, %l5, %l4)
	brz,pn	%l4, 1f
	 rdpr	%wstate, %l5
	andn	%l5, WSTATE_PROM_MASK, %l3
	wrpr	%l3, WSTATE_PROM_KMIX, %wstate
1:	call	%l6
	 mov	%i0, %o0
	brz,pn	%l4, 1f
	 nop
	wrpr	%g0, %l5, %wstate
1:	wrpr	%l7, 0, %pstate
	ret
	 restore %o0, %g0, %o0
END(ofw_entry)

/*
 * void ofw_exit(cell_t args[])
 */
ENTRY(ofw_exit)
	save	%sp, -CCFSZ, %sp
	flushw
	SET(ofw_tba, %l7, %l5)
	ldx	[%l5], %l5
	rdpr	%pstate, %l7
	andn	%l7, PSTATE_AM | PSTATE_IE, %l7
	wrpr	%l7, 0, %pstate
	rdpr	%wstate, %l7
	andn	%l7, WSTATE_PROM_MASK, %l7
	wrpr	%l7, WSTATE_PROM_KMIX, %wstate
	wrpr	%l5, 0, %tba			! restore the OFW trap table
	SET(ofw_vec, %l7, %l6)
	ldx	[%l6], %l6
	SET(kstack0 + KSTACK_PAGES * PAGE_SIZE - PCB_SIZEOF, %l7, %l0)
	sub	%l0, SPOFF, %fp			! setup a stack in a locked page
	sub	%l0, SPOFF + CCFSZ, %sp
	mov	AA_DMMU_PCXR, %l3		! force primary DMMU context 0
	sethi	%hi(KERNBASE), %l5
	stxa	%g0, [%l3] ASI_DMMU
	flush	%l5
	wrpr	%g0, 0, %tl			! force trap level 0
	call	%l6
	 mov	%i0, %o0
	! never to return
END(ofw_exit)

#ifdef GPROF

ENTRY(user)
	nop

ENTRY(btrap)
	nop

ENTRY(etrap)
	nop

ENTRY(bintr)
	nop

ENTRY(eintr)
	nop

/*
 * XXX including sys/gmon.h in genassym.c is not possible due to uintfptr_t
 * badness.
 */
#define	GM_STATE	0x0
#define	GMON_PROF_OFF	3
#define	GMON_PROF_HIRES	4

	.globl	_mcount
	.set	_mcount, __cyg_profile_func_enter

ENTRY(__cyg_profile_func_enter)
	SET(_gmonparam, %o3, %o2)
	lduw	[%o2 + GM_STATE], %o3
	cmp	%o3, GMON_PROF_OFF
	be,a,pn %icc, 1f
	 nop
	SET(mcount, %o3, %o2)
	jmpl	%o2, %g0
	 nop
1:	retl
	 nop
END(__cyg_profile_func_enter)

#ifdef GUPROF

ENTRY(__cyg_profile_func_exit)
	SET(_gmonparam, %o3, %o2)
	lduw	[%o2 + GM_STATE], %o3
	cmp	%o3, GMON_PROF_HIRES
	be,a,pn %icc, 1f
	 nop
	SET(mexitcount, %o3, %o2)
	jmpl	%o2, %g0
	 nop
1:	retl
	 nop
END(__cyg_profile_func_exit)

#endif /* GUPROF */

#endif /* GPROF */
