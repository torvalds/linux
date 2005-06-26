#ifndef XTENSA_CACHEATTRASM_H
#define XTENSA_CACHEATTRASM_H

/*
 * THIS FILE IS GENERATED -- DO NOT MODIFY BY HAND
 *
 * include/asm-xtensa/xtensa/cacheattrasm.h -- assembler-specific
 * CACHEATTR register related definitions that depend on CORE
 * configuration.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2002 Tensilica Inc.
 */


#include <xtensa/coreasm.h>


/*
 *  This header file defines assembler macros of the form:
 *	<x>cacheattr_<func>
 *  where:
 *	<x> is 'i', 'd' or absent for instruction, data
 *		or both caches; and
 *	<func> indicates the function of the macro.
 *
 *  The following functions are defined:
 *
 *  icacheattr_get
 *	Reads I-cache CACHEATTR into a2 (clobbers a3-a5).
 *
 *  dcacheattr_get
 *	Reads D-cache CACHEATTR into a2 (clobbers a3-a5).
 *	(Note:  for configs with a real CACHEATTR register, the
 *	 above two macros are identical.)
 *
 *  cacheattr_set
 *	Writes both I-cache and D-cache CACHEATTRs from a2 (a3-a8 clobbered).
 *	Works even when changing one's own code's attributes.
 *
 *  icacheattr_is_enabled  label
 *	Branches to \label if I-cache appears to have been enabled
 *	(eg. if CACHEATTR contains a cache-enabled attribute).
 *	(clobbers a2-a5,SAR)
 *
 *  dcacheattr_is_enabled  label
 *	Branches to \label if D-cache appears to have been enabled
 *	(eg. if CACHEATTR contains a cache-enabled attribute).
 *	(clobbers a2-a5,SAR)
 *
 *  cacheattr_is_enabled  label
 *	Branches to \label if either I-cache or D-cache appears to have been enabled
 *	(eg. if CACHEATTR contains a cache-enabled attribute).
 *	(clobbers a2-a5,SAR)
 *
 *  The following macros are only defined under certain conditions:
 *
 *  icacheattr_set	(if XCHAL_HAVE_MIMIC_CACHEATTR || XCHAL_HAVE_XLT_CACHEATTR)
 *	Writes I-cache CACHEATTR from a2 (a3-a8 clobbered).
 *
 *  dcacheattr_set	(if XCHAL_HAVE_MIMIC_CACHEATTR || XCHAL_HAVE_XLT_CACHEATTR)
 *	Writes D-cache CACHEATTR from a2 (a3-a8 clobbered).
 */



/***************************   GENERIC -- ALL CACHES   ***************************/

/*
 *  _cacheattr_get
 *
 *  (Internal macro.)
 *  Returns value of CACHEATTR register (or closest equivalent) in a2.
 *
 *  Entry:
 *	(none)
 *  Exit:
 *	a2	value read from CACHEATTR
 *	a3-a5	clobbered (temporaries)
 */
	.macro	_cacheattr_get	tlb
#if XCHAL_HAVE_CACHEATTR
	rsr	a2, CACHEATTR
#elif XCHAL_HAVE_MIMIC_CACHEATTR || XCHAL_HAVE_XLT_CACHEATTR
	//  We have a config that "mimics" CACHEATTR using a simplified
	//  "MMU" composed of a single statically-mapped way.
	//  DTLB and ITLB are independent, so there's no single
	//  cache attribute that can describe both.  So for now
	//  just return the DTLB state.
	movi	a5, 0xE0000000
	movi	a2, 0
	movi	a3, 0
1:	add	a3, a3, a5	// next segment
	r&tlb&1	a4, a3		// get PPN+CA of segment at 0xE0000000, 0xC0000000, ..., 0
	dsync	// interlock???
	slli	a2, a2, 4
	extui	a4, a4, 0, 4	// extract CA
	or	a2, a2, a4
	bnez	a3, 1b
#else
	//  This macro isn't applicable to arbitrary MMU configurations.
	//  Just return zero.
	movi	a2, 0
#endif
	.endm

	.macro	icacheattr_get
	_cacheattr_get	itlb
	.endm

	.macro	dcacheattr_get
	_cacheattr_get	dtlb
	.endm


#define XCHAL_CACHEATTR_ALL_BYPASS	0x22222222	/* default (powerup/reset) value of CACHEATTR, all BYPASS
							   mode (ie. disabled/bypassed caches) */

#if XCHAL_HAVE_CACHEATTR || XCHAL_HAVE_MIMIC_CACHEATTR || XCHAL_HAVE_XLT_CACHEATTR

#define XCHAL_FCA_ENAMASK	0x001A	/* bitmap of fetch attributes that require enabled icache */
#define XCHAL_LCA_ENAMASK	0x0003	/* bitmap of load  attributes that require enabled dcache */
#define XCHAL_SCA_ENAMASK	0x0003	/* bitmap of store attributes that require enabled dcache */
#define XCHAL_LSCA_ENAMASK	(XCHAL_LCA_ENAMASK|XCHAL_SCA_ENAMASK)	/* l/s attrs requiring enabled dcache */
#define XCHAL_ALLCA_ENAMASK	(XCHAL_FCA_ENAMASK|XCHAL_LSCA_ENAMASK)	/* all attrs requiring enabled caches */

/*
 *  _cacheattr_is_enabled
 *
 *  (Internal macro.)
 *  Branches to \label if CACHEATTR in a2 indicates an enabled
 *  cache, using mask in a3.
 *
 *  Parameters:
 *	label	where to branch to if cache is enabled
 *  Entry:
 *	a2	contains CACHEATTR value used to determine whether
 *		caches are enabled
 *	a3	16-bit constant where each bit correspond to
 *		one of the 16 possible CA values (in a CACHEATTR mask);
 *		CA values that indicate the cache is enabled
 *		have their corresponding bit set in this mask
 *		(eg. use XCHAL_xCA_ENAMASK , above)
 *  Exit:
 *	a2,a4,a5	clobbered
 *	SAR		clobbered
 */
	.macro	_cacheattr_is_enabled	label
	movi	a4, 8		// loop 8 times
.Lcaife\@:
	extui	a5, a2, 0, 4	// get CA nibble
	ssr	a5		// index into mask according to CA...
	srl	a5, a3		// ...and get CA's mask bit in a5 bit 0
	bbsi.l	a5, 0, \label	// if CA indicates cache enabled, jump to label
	srli	a2, a2, 4	// next nibble
	addi	a4, a4, -1
	bnez	a4, .Lcaife\@	// loop for each nibble
	.endm

#else /* XCHAL_HAVE_CACHEATTR || XCHAL_HAVE_MIMIC_CACHEATTR || XCHAL_HAVE_XLT_CACHEATTR */
	.macro	_cacheattr_is_enabled	label
	j	\label		// macro not applicable, assume caches always enabled
	.endm
#endif /* XCHAL_HAVE_CACHEATTR || XCHAL_HAVE_MIMIC_CACHEATTR || XCHAL_HAVE_XLT_CACHEATTR */



/*
 *  icacheattr_is_enabled
 *
 *  Branches to \label if I-cache is enabled.
 *
 *  Parameters:
 *	label	where to branch to if icache is enabled
 *  Entry:
 *	(none)
 *  Exit:
 *	a2-a5, SAR	clobbered (temporaries)
 */
	.macro	icacheattr_is_enabled	label
#if XCHAL_HAVE_CACHEATTR || XCHAL_HAVE_MIMIC_CACHEATTR || XCHAL_HAVE_XLT_CACHEATTR
	icacheattr_get
	movi	a3, XCHAL_FCA_ENAMASK
#endif
	_cacheattr_is_enabled	\label
	.endm

/*
 *  dcacheattr_is_enabled
 *
 *  Branches to \label if D-cache is enabled.
 *
 *  Parameters:
 *	label	where to branch to if dcache is enabled
 *  Entry:
 *	(none)
 *  Exit:
 *	a2-a5, SAR	clobbered (temporaries)
 */
	.macro	dcacheattr_is_enabled	label
#if XCHAL_HAVE_CACHEATTR || XCHAL_HAVE_MIMIC_CACHEATTR || XCHAL_HAVE_XLT_CACHEATTR
	dcacheattr_get
	movi	a3, XCHAL_LSCA_ENAMASK
#endif
	_cacheattr_is_enabled	\label
	.endm

/*
 *  cacheattr_is_enabled
 *
 *  Branches to \label if either I-cache or D-cache is enabled.
 *
 *  Parameters:
 *	label	where to branch to if a cache is enabled
 *  Entry:
 *	(none)
 *  Exit:
 *	a2-a5, SAR	clobbered (temporaries)
 */
	.macro	cacheattr_is_enabled	label
#if XCHAL_HAVE_CACHEATTR
	rsr	a2, CACHEATTR
	movi	a3, XCHAL_ALLCA_ENAMASK
#elif XCHAL_HAVE_MIMIC_CACHEATTR || XCHAL_HAVE_XLT_CACHEATTR
	icacheattr_get
	movi	a3, XCHAL_FCA_ENAMASK
	_cacheattr_is_enabled	\label
	dcacheattr_get
	movi	a3, XCHAL_LSCA_ENAMASK
#endif
	_cacheattr_is_enabled	\label
	.endm



/*
 *  The ISA does not have a defined way to change the
 *  instruction cache attributes of the running code,
 *  ie. of the memory area that encloses the current PC.
 *  However, each micro-architecture (or class of
 *  configurations within a micro-architecture)
 *  provides a way to deal with this issue.
 *
 *  Here are a few macros used to implement the relevant
 *  approach taken.
 */

#if XCHAL_HAVE_MIMIC_CACHEATTR || XCHAL_HAVE_XLT_CACHEATTR
	//  We have a config that "mimics" CACHEATTR using a simplified
	//  "MMU" composed of a single statically-mapped way.

/*
 *  icacheattr_set
 *
 *  Entry:
 *	a2		cacheattr value to set
 *  Exit:
 *	a2		unchanged
 *	a3-a8		clobbered (temporaries)
 */
	.macro	icacheattr_set

	movi	a5, 0xE0000000	// mask of upper 3 bits
	movi	a6, 3f		// PC where ITLB is set
	movi	a3, 0		// start at region 0 (0 .. 7)
	and	a6, a6, a5	// upper 3 bits of local PC area
	mov	a7, a2		// copy a2 so it doesn't get clobbered
	j	3f

# if XCHAL_HAVE_XLT_CACHEATTR
	//  Can do translations, use generic method:
1:	sub	a6, a3, a5	// address of some other segment
	ritlb1	a8, a6		// save its PPN+CA
	dsync	// interlock??
	witlb	a4, a6		// make it translate to this code area
	movi	a6, 5f		// where to jump into it
	isync
	sub	a6, a6, a5	// adjust jump address within that other segment
	jx	a6

	//  Note that in the following code snippet, which runs at a different virtual
	//  address than it is assembled for, we avoid using literals (eg. via movi/l32r)
	//  just in case literals end up in a different 512 MB segment, and we avoid
	//  instructions that rely on the current PC being what is expected.
	//
	.align	4
	_j	6f		// this is at label '5' minus 4 bytes
	.align	4
5:	witlb	a4, a3		// we're in other segment, now can write previous segment's CA
	isync
	add	a6, a6, a5	// back to previous segment
	addi	a6, a6, -4	// next jump label
	jx	a6

6:	sub	a6, a3, a5	// address of some other segment
	witlb	a8, a6		// restore PPN+CA of other segment
	mov	a6, a3		// restore a6
	isync
# else /* XCHAL_HAVE_XLT_CACHEATTR */
	//  Use micro-architecture specific method.
	//  The following 4-instruction sequence is aligned such that
	//  it all fits within a single I-cache line.  Sixteen byte
	//  alignment is sufficient for this (using XCHAL_ICACHE_LINESIZE
	//  actually causes problems because that can be greater than
	//  the alignment of the reset vector, where this macro is often
	//  invoked, which would cause the linker to align the reset
	//  vector code away from the reset vector!!).
	.align	16 /*XCHAL_ICACHE_LINESIZE*/
1:	_witlb	a4, a3		// write wired PTE (CA, no PPN) of 512MB segment to ITLB
	_isync
	nop
	nop
# endif /* XCHAL_HAVE_XLT_CACHEATTR */
	beq	a3, a5, 4f	// done?

	//  Note that in the WITLB loop, we don't do any load/stores
	//  (may not be an issue here, but it is important in the DTLB case).
2:	srli	a7, a7, 4	// next CA
	sub	a3, a3, a5	// next segment (add 0x20000000)
3:
# if XCHAL_HAVE_XLT_CACHEATTR	/* if have translation, preserve it */
	ritlb1	a8, a3		// get current PPN+CA of segment
	dsync	// interlock???
	extui	a4, a7, 0, 4	// extract CA to set
	srli	a8, a8, 4	// clear CA but keep PPN ...
	slli	a8, a8, 4	// ...
	add	a4, a4, a8	// combine new CA with PPN to preserve
# else
	extui	a4, a7, 0, 4	// extract CA
# endif
	beq	a3, a6, 1b	// current PC's region? if so, do it in a safe way
	witlb	a4, a3		// write wired PTE (CA [+PPN]) of 512MB segment to ITLB
	bne	a3, a5, 2b
	isync			// make sure all ifetch changes take effect
4:
	.endm	// icacheattr_set


/*
 *  dcacheattr_set
 *
 *  Entry:
 *	a2		cacheattr value to set
 *  Exit:
 *	a2		unchanged
 *	a3-a8		clobbered (temporaries)
 */

	.macro	dcacheattr_set

	movi	a5, 0xE0000000	// mask of upper 3 bits
	movi	a3, 0		// start at region 0 (0 .. 7)
	mov	a7, a2		// copy a2 so it doesn't get clobbered
	j	3f
	//  Note that in the WDTLB loop, we don't do any load/stores
	//  (including implicit l32r via movi) because it isn't safe.
2:	srli	a7, a7, 4	// next CA
	sub	a3, a3, a5	// next segment (add 0x20000000)
3:
# if XCHAL_HAVE_XLT_CACHEATTR	/* if have translation, preserve it */
	rdtlb1	a8, a3		// get current PPN+CA of segment
	dsync	// interlock???
	extui	a4, a7, 0, 4	// extract CA to set
	srli	a8, a8, 4	// clear CA but keep PPN ...
	slli	a8, a8, 4	// ...
	add	a4, a4, a8	// combine new CA with PPN to preserve
# else
	extui	a4, a7, 0, 4	// extract CA to set
# endif
	wdtlb	a4, a3		// write wired PTE (CA [+PPN]) of 512MB segment to DTLB
	bne	a3, a5, 2b
	dsync			// make sure all data path changes take effect
	.endm	// dcacheattr_set

#endif /* XCHAL_HAVE_MIMIC_CACHEATTR || XCHAL_HAVE_XLT_CACHEATTR */



/*
 *  cacheattr_set
 *
 *  Macro that sets the current CACHEATTR safely
 *  (both i and d) according to the current contents of a2.
 *  It works even when changing the cache attributes of
 *  the currently running code.
 *
 *  Entry:
 *	a2		cacheattr value to set
 *  Exit:
 *	a2		unchanged
 *	a3-a8		clobbered (temporaries)
 */
	.macro	cacheattr_set

#if XCHAL_HAVE_CACHEATTR
# if XCHAL_ICACHE_LINESIZE < 4
	//  No i-cache, so can always safely write to CACHEATTR:
	wsr	a2, CACHEATTR
# else
	//  The Athens micro-architecture, when using the old
	//  exception architecture option (ie. with the CACHEATTR register)
	//  allows changing the cache attributes of the running code
	//  using the following exact sequence aligned to be within
	//  an instruction cache line.  (NOTE: using XCHAL_ICACHE_LINESIZE
	//  alignment actually causes problems because that can be greater
	//  than the alignment of the reset vector, where this macro is often
	//  invoked, which would cause the linker to align the reset
	//  vector code away from the reset vector!!).
	j	1f
	.align	16 /*XCHAL_ICACHE_LINESIZE*/	// align to within an I-cache line
1:	_wsr	a2, CACHEATTR
	_isync
	nop
	nop
# endif
#elif XCHAL_HAVE_MIMIC_CACHEATTR || XCHAL_HAVE_XLT_CACHEATTR
	//  DTLB and ITLB are independent, but to keep semantics
	//  of this macro we simply write to both.
	icacheattr_set
	dcacheattr_set
#else
	//  This macro isn't applicable to arbitrary MMU configurations.
	//  Do nothing in this case.
#endif
	.endm


#endif /*XTENSA_CACHEATTRASM_H*/

