/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2003-2011 Netlogic Microsystems (Netlogic). All rights
 * reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY Netlogic Microsystems ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL NETLOGIC OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * NETLOGIC_BSD
 * $FreeBSD$
 */

#ifndef __NLM_HAL_CPUCONTROL_H__
#define	__NLM_HAL_CPUCONTROL_H__

#define	CPU_BLOCKID_IFU		0
#define	CPU_BLOCKID_ICU		1
#define	CPU_BLOCKID_IEU		2
#define	CPU_BLOCKID_LSU		3
#define	CPU_BLOCKID_MMU		4
#define	CPU_BLOCKID_PRF		5
#define	CPU_BLOCKID_SCH		7
#define	CPU_BLOCKID_SCU		8
#define	CPU_BLOCKID_FPU		9
#define	CPU_BLOCKID_MAP		10

#define	LSU_DEFEATURE		0x304
#define	LSU_DEBUG_ADDR		0x305
#define	LSU_DEBUG_DATA0		0x306
#define	LSU_CERRLOG_REGID	0x09
#define	SCHED_DEFEATURE		0x700

/* Offsets of interest from the 'MAP' Block */
#define	MAP_THREADMODE			0x00
#define	MAP_EXT_EBASE_ENABLE		0x04
#define	MAP_CCDI_CONFIG			0x08
#define	MAP_THRD0_CCDI_STATUS		0x0c
#define	MAP_THRD1_CCDI_STATUS		0x10
#define	MAP_THRD2_CCDI_STATUS		0x14
#define	MAP_THRD3_CCDI_STATUS		0x18
#define	MAP_THRD0_DEBUG_MODE		0x1c
#define	MAP_THRD1_DEBUG_MODE		0x20
#define	MAP_THRD2_DEBUG_MODE		0x24
#define	MAP_THRD3_DEBUG_MODE		0x28
#define	MAP_MISC_STATE			0x60
#define	MAP_DEBUG_READ_CTL		0x64
#define	MAP_DEBUG_READ_REG0		0x68
#define	MAP_DEBUG_READ_REG1		0x6c

#define	MMU_SETUP		0x400
#define	MMU_LFSRSEED		0x401
#define	MMU_HPW_NUM_PAGE_LVL	0x410
#define	MMU_PGWKR_PGDBASE	0x411
#define	MMU_PGWKR_PGDSHFT	0x412
#define	MMU_PGWKR_PGDMASK	0x413
#define	MMU_PGWKR_PUDSHFT	0x414
#define	MMU_PGWKR_PUDMASK	0x415
#define	MMU_PGWKR_PMDSHFT	0x416
#define	MMU_PGWKR_PMDMASK	0x417
#define	MMU_PGWKR_PTESHFT	0x418
#define	MMU_PGWKR_PTEMASK	0x419


#if !defined(LOCORE) && !defined(__ASSEMBLY__)
#if defined(__mips_n64) || defined(__mips_n32)
static __inline uint64_t
nlm_mfcr(uint32_t reg)
{
	uint64_t res;

	__asm__ __volatile__(
	    ".set	push\n\t"
	    ".set	noreorder\n\t"
	    "move	$9, %1\n\t"
	    ".word	0x71280018\n\t"  /* mfcr $8, $9 */
	    "move	%0, $8\n\t"
	    ".set	pop\n"
	    : "=r" (res) : "r"(reg)
	    : "$8", "$9"
	);
	return (res);
}

static __inline void
nlm_mtcr(uint32_t reg, uint64_t value)
{
	__asm__ __volatile__(
	    ".set	push\n\t"
	    ".set	noreorder\n\t"
	    "move	$8, %0\n"
	    "move	$9, %1\n"
	    ".word	0x71280019\n"    /* mtcr $8, $9  */
	    ".set	pop\n"
	    :
	    : "r" (value), "r" (reg)
	    : "$8", "$9"
	);
}

#else /* !(defined(__mips_n64) || defined(__mips_n32)) */

static __inline__  uint64_t
nlm_mfcr(uint32_t reg)
{
	uint32_t hi, lo;

	__asm__ __volatile__ (
	    ".set push\n"
	    ".set mips64\n"
	    "move   $8, %2\n"
	    ".word  0x71090018\n"
	    "nop	\n"
	    "dsra32 %0, $9, 0\n"
	    "sll    %1, $9, 0\n"
	    ".set pop\n"
	    : "=r"(hi), "=r"(lo)
	    : "r"(reg) : "$8", "$9");

	return (((uint64_t)hi) << 32) | lo;
}

static __inline__  void
nlm_mtcr(uint32_t reg, uint64_t val)
{
	uint32_t hi, lo;

	hi = val >> 32;
	lo = val & 0xffffffff;

	__asm__ __volatile__ (
	    ".set push\n"
	    ".set mips64\n"
	    "move   $9, %0\n"
	    "dsll32 $9, %1, 0\n"
	    "dsll32 $8, %0, 0\n"
	    "dsrl32 $9, $9, 0\n"
	    "or     $9, $9, $8\n"
	    "move   $8, %2\n"
	    ".word  0x71090019\n"
	    "nop	\n"
	    ".set pop\n"
	    : :"r"(hi), "r"(lo), "r"(reg)
	    : "$8", "$9");
}
#endif /* (defined(__mips_n64) || defined(__mips_n32)) */

/* hashindex_en = 1 to enable hash mode, hashindex_en=0 to disable
 * global_mode = 1 to enable global mode, global_mode=0 to disable
 * clk_gating = 0 to enable clock gating, clk_gating=1 to disable
 */
static __inline__ void nlm_mmu_setup(int hashindex_en, int global_mode,
		int clk_gating)
{
	uint32_t mmusetup = 0;

	mmusetup |= (hashindex_en << 13);
	mmusetup |= (clk_gating << 3);
	mmusetup |= (global_mode << 0);
	nlm_mtcr(MMU_SETUP, mmusetup);
}

static __inline__ void nlm_mmu_lfsr_seed (int thr0_seed, int thr1_seed,
		int thr2_seed, int thr3_seed)
{
	uint32_t seed = nlm_mfcr(MMU_LFSRSEED);

	seed |= ((thr3_seed & 0x7f) << 23);
	seed |= ((thr2_seed & 0x7f) << 16);
	seed |= ((thr1_seed & 0x7f) << 7);
	seed |= ((thr0_seed & 0x7f) << 0);
	nlm_mtcr(MMU_LFSRSEED, seed);
}

#endif /* __ASSEMBLY__ */
#endif /* __NLM_CPUCONTROL_H__ */
