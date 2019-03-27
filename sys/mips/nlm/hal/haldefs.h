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

#ifndef __NLM_HAL_MMIO_H__
#define	__NLM_HAL_MMIO_H__

/*
 * This file contains platform specific memory mapped IO implementation
 * and will provide a way to read 32/64 bit memory mapped registers in
 * all ABIs
 */

/*
 * For o32 compilation, we have to disable interrupts and enable KX bit to
 * access 64 bit addresses or data.
 *
 * We need to disable interrupts because we save just the lower 32 bits of
 * registers in  interrupt handling. So if we get hit by an interrupt while
 * using the upper 32 bits of a register, we lose.
 */
static inline uint32_t nlm_save_flags_kx(void)
{
	uint32_t sr = mips_rd_status();

	mips_wr_status((sr & ~MIPS_SR_INT_IE) | MIPS_SR_KX);
	return (sr);
}

static inline uint32_t nlm_save_flags_cop2(void)
{
	uint32_t sr = mips_rd_status();

	mips_wr_status((sr & ~MIPS_SR_INT_IE) | MIPS_SR_COP_2_BIT);
	return (sr);
}

static inline void nlm_restore_flags(uint32_t sr)
{
	mips_wr_status(sr);
}

static inline uint32_t
nlm_load_word(uint64_t addr)
{
	volatile uint32_t *p = (volatile uint32_t *)(long)addr;

	return *p;
}

static inline void
nlm_store_word(uint64_t addr, uint32_t val)
{
	volatile uint32_t *p = (volatile uint32_t *)(long)addr;

	*p = val;
}

#if defined(__mips_n64) || defined(__mips_n32)
static inline uint64_t
nlm_load_dword(volatile uint64_t addr)
{
	volatile uint64_t *p = (volatile uint64_t *)(long)addr;

	return *p;
}

static inline void
nlm_store_dword(volatile uint64_t addr, uint64_t val)
{
	volatile uint64_t *p = (volatile uint64_t *)(long)addr;

	*p = val;
}

#else /* o32 */
static inline uint64_t
nlm_load_dword(uint64_t addr)
{
	volatile uint64_t *p = (volatile uint64_t *)(long)addr;
	uint32_t valhi, vallo, sr;

	sr = nlm_save_flags_kx();
	__asm__ __volatile__(
		".set	push\n\t"
		".set	mips64\n\t"
		"ld	$8, 0(%2)\n\t"
		"dsra32	%0, $8, 0\n\t"
		"sll	%1, $8, 0\n\t"
		".set	pop\n"
		: "=r"(valhi), "=r"(vallo)
		: "r"(p)
		: "$8");
	nlm_restore_flags(sr);

	return ((uint64_t)valhi << 32) | vallo;
}

static inline void
nlm_store_dword(uint64_t addr, uint64_t val)
{
	volatile uint64_t *p = (volatile uint64_t *)(long)addr;
	uint32_t valhi, vallo, sr;

	valhi = val >> 32;
	vallo = val & 0xffffffff;

	sr = nlm_save_flags_kx();
	__asm__ __volatile__(
		".set	push\n\t"
		".set	mips64\n\t"
		"dsll32	$8, %1, 0\n\t"
		"dsll32	$9, %2, 0\n\t"  /* get rid of the */
		"dsrl32	$9, $9, 0\n\t"  /* sign extend */
		"or	$9, $9, $8\n\t"
		"sd	$9, 0(%0)\n\t"
		".set	pop\n"
		: : "r"(p), "r"(valhi), "r"(vallo)
		: "$8", "$9", "memory");
	nlm_restore_flags(sr);
}
#endif

#if defined(__mips_n64)
static inline uint64_t
nlm_load_word_daddr(uint64_t addr)
{
	volatile uint32_t *p = (volatile uint32_t *)(long)addr;

	return *p;
}

static inline void
nlm_store_word_daddr(uint64_t addr, uint32_t val)
{
	volatile uint32_t *p = (volatile uint32_t *)(long)addr;

	*p = val;
}

static inline uint64_t
nlm_load_dword_daddr(uint64_t addr)
{
	volatile uint64_t *p = (volatile uint64_t *)(long)addr;

	return *p;
}

static inline void
nlm_store_dword_daddr(uint64_t addr, uint64_t val)
{
	volatile uint64_t *p = (volatile uint64_t *)(long)addr;

	*p = val;
}

#elif defined(__mips_n32)

static inline uint64_t
nlm_load_word_daddr(uint64_t addr)
{
	uint32_t val;

	__asm__ __volatile__(
		".set	push\n\t"
		".set	mips64\n\t"
		"lw		%0, 0(%1)\n\t"
		".set	pop\n"
		: "=r"(val)
		: "r"(addr));

	return val;
}

static inline void
nlm_store_word_daddr(uint64_t addr, uint32_t val)
{
	__asm__ __volatile__(
		".set	push\n\t"
		".set	mips64\n\t"
		"sw		%0, 0(%1)\n\t"
		".set	pop\n"
		: : "r"(val), "r"(addr)
		: "memory");
}

static inline uint64_t
nlm_load_dword_daddr(uint64_t addr)
{
	uint64_t val;

	__asm__ __volatile__(
		".set	push\n\t"
		".set	mips64\n\t"
		"ld		%0, 0(%1)\n\t"
		".set	pop\n"
		: "=r"(val)
		: "r"(addr));
	return val;
}

static inline void
nlm_store_dword_daddr(uint64_t addr, uint64_t val)
{
	__asm__ __volatile__(
		".set	push\n\t"
		".set	mips64\n\t"
		"sd		%0, 0(%1)\n\t"
		".set	pop\n"
		: : "r"(val), "r"(addr)
		: "memory");
}

#else /* o32 */
static inline uint64_t
nlm_load_word_daddr(uint64_t addr)
{
	uint32_t val, addrhi, addrlo, sr;

	addrhi = addr >> 32;
	addrlo = addr & 0xffffffff;

	sr = nlm_save_flags_kx();
	__asm__ __volatile__(
		".set	push\n\t"
		".set	mips64\n\t"
		"dsll32	$8, %1, 0\n\t"
		"dsll32	$9, %2, 0\n\t"
		"dsrl32	$9, $9, 0\n\t"
		"or	$9, $9, $8\n\t"
		"lw	%0, 0($9)\n\t"
		".set	pop\n"
		:	"=r"(val)
		:	"r"(addrhi), "r"(addrlo)
		:	"$8", "$9");
	nlm_restore_flags(sr);

	return val;

}

static inline void
nlm_store_word_daddr(uint64_t addr, uint32_t val)
{
	uint32_t addrhi, addrlo, sr;

	addrhi = addr >> 32;
	addrlo = addr & 0xffffffff;

	sr = nlm_save_flags_kx();
	__asm__ __volatile__(
		".set	push\n\t"
		".set	mips64\n\t"
		"dsll32	$8, %1, 0\n\t"
		"dsll32	$9, %2, 0\n\t"
		"dsrl32	$9, $9, 0\n\t"
		"or	$9, $9, $8\n\t"
		"sw	%0, 0($9)\n\t"
		".set	pop\n"
		: : "r"(val), "r"(addrhi), "r"(addrlo)
		:	"$8", "$9", "memory");
	nlm_restore_flags(sr);
}

static inline uint64_t
nlm_load_dword_daddr(uint64_t addr)
{
	uint32_t addrh, addrl, sr;
	uint32_t valh, vall;

	addrh = addr >> 32;
	addrl = addr & 0xffffffff;

	sr = nlm_save_flags_kx();
	__asm__ __volatile__(
		".set	push\n\t"
		".set	mips64\n\t"
		"dsll32	$8, %2, 0\n\t"
		"dsll32	$9, %3, 0\n\t"
		"dsrl32	$9, $9, 0\n\t"
		"or	$9, $9, $8\n\t"
		"ld	$8, 0($9)\n\t"
		"dsra32	%0, $8, 0\n\t"
		"sll	%1, $8, 0\n\t"
		".set	pop\n"
		: "=r"(valh), "=r"(vall)
		: "r"(addrh), "r"(addrl)
		: "$8", "$9");
	nlm_restore_flags(sr);

	return ((uint64_t)valh << 32) | vall;
}

static inline void
nlm_store_dword_daddr(uint64_t addr, uint64_t val)
{
	uint32_t addrh, addrl, sr;
	uint32_t valh, vall;

	addrh = addr >> 32;
	addrl = addr & 0xffffffff;
	valh = val >> 32;
	vall = val & 0xffffffff;

	sr = nlm_save_flags_kx();
	__asm__ __volatile__(
		".set	push\n\t"
		".set	mips64\n\t"
		"dsll32	$8, %2, 0\n\t"
		"dsll32	$9, %3, 0\n\t"
		"dsrl32	$9, $9, 0\n\t"
		"or	$9, $9, $8\n\t"
		"dsll32	$8, %0, 0\n\t"
		"dsll32	$10, %1, 0\n\t"
		"dsrl32	$10, $10, 0\n\t"
		"or	$8, $8, $10\n\t"
		"sd	$8, 0($9)\n\t"
		".set	pop\n"
		: :	"r"(valh), "r"(vall), "r"(addrh), "r"(addrl)
		:	"$8", "$9", "memory");
	nlm_restore_flags(sr);
}
#endif /* __mips_n64 */

static inline uint32_t
nlm_read_reg(uint64_t base, uint32_t reg)
{
	volatile uint32_t *addr = (volatile uint32_t *)(long)base + reg;

	return *addr;
}

static inline void
nlm_write_reg(uint64_t base, uint32_t reg, uint32_t val)
{
	volatile uint32_t *addr = (volatile uint32_t *)(long)base + reg;

	*addr = val;
}

static inline uint64_t
nlm_read_reg64(uint64_t base, uint32_t reg)
{
	uint64_t addr = base + (reg >> 1) * sizeof(uint64_t);

	return nlm_load_dword(addr);
}

static inline void
nlm_write_reg64(uint64_t base, uint32_t reg, uint64_t val)
{
	uint64_t addr = base + (reg >> 1) * sizeof(uint64_t);

	return nlm_store_dword(addr, val);
}

/*
 * Routines to store 32/64 bit values to 64 bit addresses,
 * used when going thru XKPHYS to access registers
 */
static inline uint32_t
nlm_read_reg_xkphys(uint64_t base, uint32_t reg)
{
	uint64_t addr = base + reg * sizeof(uint32_t);

	return nlm_load_word_daddr(addr);
}

static inline void
nlm_write_reg_xkphys(uint64_t base, uint32_t reg, uint32_t val)
{
	uint64_t addr = base + reg * sizeof(uint32_t);
	return nlm_store_word_daddr(addr, val);
}

static inline uint64_t
nlm_read_reg64_xkphys(uint64_t base, uint32_t reg)
{
	uint64_t addr = base + (reg >> 1) * sizeof(uint64_t);

	return nlm_load_dword_daddr(addr);
}

static inline void
nlm_write_reg64_xkphys(uint64_t base, uint32_t reg, uint64_t val)
{
	uint64_t addr = base + (reg >> 1) * sizeof(uint64_t);

	return nlm_store_dword_daddr(addr, val);
}

/* Location where IO base is mapped */
extern uint64_t xlp_io_base;

static inline uint64_t
nlm_pcicfg_base(uint32_t devoffset)
{
	return xlp_io_base + devoffset;
}

static inline uint64_t
nlm_xkphys_map_pcibar0(uint64_t pcibase)
{
	uint64_t paddr;

	paddr = nlm_read_reg(pcibase, 0x4) & ~0xfu;
	return (uint64_t)0x9000000000000000 | paddr;
}

#endif
