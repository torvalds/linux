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

#ifndef __NLM_MIPS_EXTNS_H__
#define	__NLM_MIPS_EXTNS_H__

#if !defined(LOCORE) && !defined(__ASSEMBLY__)
static __inline__ int32_t nlm_swapw(int32_t *loc, int32_t val)
{
	int32_t oldval = 0;

	__asm__ __volatile__ (
		".set push\n"
		".set noreorder\n"
		"move $9, %2\n"
		"move $8, %3\n"
		".word 0x71280014\n"   /* "swapw $8, $9\n" */
		"move %1, $8\n"
		".set pop\n"
		: "+m" (*loc), "=r" (oldval)
		: "r" (loc), "r" (val)
		: "$8", "$9" );

	return oldval;
}

static __inline__ uint32_t nlm_swapwu(int32_t *loc, uint32_t val)
{
	uint32_t oldval;

	__asm__ __volatile__ (
		".set push\n"
		".set noreorder\n"
		"move $9, %2\n"
		"move $8, %3\n"
		".word 0x71280015\n"   /* "swapwu $8, $9\n" */
		"move %1, $8\n"
		".set pop\n"
		: "+m" (*loc), "=r" (oldval)
		: "r" (loc), "r" (val)
		: "$8", "$9" );

	return oldval;
}

#if (__mips == 64)
static __inline__ uint64_t nlm_swapd(int32_t *loc, uint64_t val)
{
	uint64_t oldval;

	__asm__ __volatile__ (
		".set push\n"
		".set noreorder\n"
		"move $9, %2\n"
		"move $8, %3\n"
		".word 0x71280014\n"   /* "swapw $8, $9\n" */
		"move %1, $8\n"
		".set pop\n"
		: "+m" (*loc), "=r" (oldval)
		: "r" (loc), "r" (val)
		: "$8", "$9" );

	return oldval;
}
#endif

/*
 * Atomic increment a unsigned  int
 */
static __inline unsigned int
nlm_ldaddwu(unsigned int value, unsigned int *addr)
{
	__asm__	 __volatile__(
	    ".set	push\n"
	    ".set	noreorder\n"
	    "move	$8, %2\n"
	    "move	$9, %3\n"
	    ".word	0x71280011\n"  /* ldaddwu $8, $9 */
	    "move	%0, $8\n"
	    ".set	pop\n"
	    : "=&r"(value), "+m"(*addr)
	    : "0"(value), "r" ((unsigned long)addr)
	    :  "$8", "$9");

	return (value);
}
/*
 * 32 bit read write for c0
 */
#define	read_c0_register32(reg, sel)				\
({								\
	 uint32_t __rv;						\
	__asm__ __volatile__(					\
	    ".set	push\n\t"				\
	    ".set	mips32\n\t"				\
	    "mfc0	%0, $%1, %2\n\t"			\
	    ".set	pop\n"					\
	    : "=r" (__rv) : "i" (reg), "i" (sel) );		\
	__rv;							\
 })

#define	write_c0_register32(reg,  sel, value)			\
	__asm__ __volatile__(					\
	    ".set	push\n\t"				\
	    ".set	mips32\n\t"				\
	    "mtc0	%0, $%1, %2\n\t"			\
	    ".set	pop\n"					\
	: : "r" (value), "i" (reg), "i" (sel) );

#if defined(__mips_n64) || defined(__mips_n32)
/*
 * On 64 bit compilation, the operations are simple
 */
#define	read_c0_register64(reg, sel)				\
({								\
	uint64_t __rv;						\
	__asm__ __volatile__(					\
	    ".set	push\n\t"				\
	    ".set	mips64\n\t"				\
	    "dmfc0	%0, $%1, %2\n\t"			\
	    ".set	pop\n"					\
	    : "=r" (__rv) : "i" (reg), "i" (sel) );		\
	__rv;							\
 })

#define	write_c0_register64(reg,  sel, value)			\
	__asm__ __volatile__(					\
	    ".set	push\n\t"				\
	    ".set	mips64\n\t"				\
	    "dmtc0	%0, $%1, %2\n\t"			\
	    ".set	pop\n"					\
	: : "r" (value), "i" (reg), "i" (sel) );
#else /* ! (defined(__mips_n64) || defined(__mips_n32)) */

/*
 * 32 bit compilation, 64 bit values has to split
 */
#define	read_c0_register64(reg, sel)				\
({								\
	uint32_t __high, __low;					\
	__asm__ __volatile__(					\
	    ".set	push\n\t"				\
	    ".set	noreorder\n\t"				\
	    ".set	mips64\n\t"				\
	    "dmfc0	$8, $%2, %3\n\t"			\
	    "dsra32	%0, $8, 0\n\t"				\
	    "sll	%1, $8, 0\n\t"				\
	    ".set	pop\n"					\
	    : "=r"(__high), "=r"(__low): "i"(reg), "i"(sel)	\
	    : "$8");						\
	((uint64_t)__high << 32) | __low;			\
})

#define	write_c0_register64(reg, sel, value)			\
do {								\
       uint32_t __high = value >> 32;				\
       uint32_t __low = value & 0xffffffff;			\
	__asm__ __volatile__(					\
	    ".set	push\n\t"				\
	    ".set	noreorder\n\t"				\
	    ".set	mips64\n\t"				\
	    "dsll32	$8, %1, 0\n\t"				\
	    "dsll32	$9, %0, 0\n\t"				\
	    "dsrl32	$8, $8, 0\n\t"				\
	    "or		$8, $8, $9\n\t"				\
	    "dmtc0	$8, $%2, %3\n\t"			\
	    ".set	pop"					\
	    :: "r"(__high), "r"(__low),	 "i"(reg), "i"(sel)	\
	    :"$8", "$9");					\
} while(0)

#endif
/* functions to write to and read from the extended
 * cp0 registers.
 * EIRR : Extended Interrupt Request Register
 *        cp0 register 9 sel 6
 *        bits 0...7 are same as cause register 8...15
 * EIMR : Extended Interrupt Mask Register
 *        cp0 register 9 sel 7
 *        bits 0...7 are same as status register 8...15
 */
static __inline uint64_t
nlm_read_c0_eirr(void)
{

	return (read_c0_register64(9, 6));
}

static __inline void
nlm_write_c0_eirr(uint64_t val)
{

	write_c0_register64(9, 6, val);
}

static __inline uint64_t
nlm_read_c0_eimr(void)
{

	return (read_c0_register64(9, 7));
}

static __inline void
nlm_write_c0_eimr(uint64_t val)
{

	write_c0_register64(9, 7, val);
}

static __inline__ uint32_t
nlm_read_c0_ebase(void)
{

	return (read_c0_register32(15, 1));
}

static __inline__ int
nlm_nodeid(void)
{
	return (nlm_read_c0_ebase() >> 5) & 0x3;
}

static __inline__ int
nlm_cpuid(void)
{
	return nlm_read_c0_ebase() & 0x1f;
}

static __inline__ int
nlm_threadid(void)
{
	return nlm_read_c0_ebase() & 0x3;
}

static __inline__ int
nlm_coreid(void)
{
	return (nlm_read_c0_ebase() >> 2) & 0x7;
}
#endif

#define	XLP_MAX_NODES	4
#define	XLP_MAX_CORES	8
#define	XLP_MAX_THREADS	4

#endif
