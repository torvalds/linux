/*-
 * Copyright (c) 2016 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * Portions of this software were developed by SRI International and the
 * University of Cambridge Computer Laboratory under DARPA/AFRL contract
 * FA8750-10-C-0237 ("CTSRD"), as part of the DARPA CRASH research programme.
 *
 * Portions of this software were developed by the University of Cambridge
 * Computer Laboratory as part of the CTSRD Project, with support from the
 * UK Higher Education Innovation Fund (HEIF).
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
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/smp.h>
#include <sys/systm.h>

#include <machine/cpufunc.h>
#include <machine/hwfunc.h>
#include <machine/md_var.h>
#include <machine/smp.h>

#define	VPECONF0_VPA	(1 << 0)
#define	MVPCONTROL_VPC	(1 << 1)
#define	MVPCONF0_PVPE_SHIFT	10
#define	MVPCONF0_PVPE_MASK	(0xf << MVPCONF0_PVPE_SHIFT)
#define	TCSTATUS_A	(1 << 13)

unsigned malta_ap_boot = ~0;

#define	C_SW0		(1 << 8)
#define	C_SW1		(1 << 9)
#define	C_IRQ0		(1 << 10)
#define	C_IRQ1		(1 << 11)
#define	C_IRQ2		(1 << 12)
#define	C_IRQ3		(1 << 13)
#define	C_IRQ4		(1 << 14)
#define	C_IRQ5		(1 << 15)

static inline void
evpe(void)
{
	__asm __volatile(
	"	.set push			\n"
	"	.set noreorder			\n"
	"	.set noat			\n"
	"	.set mips32r2			\n"
	"	.word	0x41600021	# evpe	\n"
	"	ehb				\n"
	"	.set pop			\n");
}

static inline void
ehb(void)
{
	__asm __volatile(
	"	.set mips32r2	\n"
	"	ehb		\n"
	"	.set mips0	\n");
}

#define	mttc0(rd, sel, val)						\
({									\
	__asm __volatile(						\
	"	.set push					\n"	\
	"	.set mips32r2					\n"	\
	"	.set noat					\n"	\
	"	move	$1, %0					\n"	\
	"	.word 0x41810000 | (" #rd " << 11) | " #sel "	\n"	\
	"	.set pop					\n"	\
	:: "r" (val));							\
})

#define	mftc0(rt, sel)							\
({									\
	unsigned long __res;						\
	__asm __volatile(						\
	"	.set push					\n"	\
	"	.set mips32r2					\n"	\
	"	.set noat					\n"	\
	"	.word 0x41000800 | (" #rt " << 16) | " #sel "	\n"	\
	"	move	%0, $1					\n"	\
	"	.set pop					\n"	\
	: "=r" (__res));						\
	 __res;								\
})

#define	write_c0_register32(reg, sel, val)				\
({									\
	__asm __volatile(						\
	"	.set push					\n"	\
	"	.set mips32					\n"	\
	"	mtc0	%0, $%1, %2				\n"	\
	"	.set pop					\n"	\
	:: "r" (val), "i" (reg), "i" (sel));				\
})

#define	read_c0_register32(reg, sel)					\
({									\
	uint32_t __retval;						\
	__asm __volatile(						\
	"	.set push					\n"	\
	"	.set mips32					\n"	\
	"	mfc0	%0, $%1, %2				\n"	\
	"	.set pop					\n"	\
	: "=r" (__retval) : "i" (reg), "i" (sel));			\
	__retval;							\
})

static void
set_thread_context(int cpuid)
{
	uint32_t reg;

	reg = read_c0_register32(1, 1);
	reg &= ~(0xff);
	reg |= cpuid;
	write_c0_register32(1, 1, reg);

	ehb();
}

void
platform_ipi_send(int cpuid)
{
	uint32_t reg;

	set_thread_context(cpuid);

	/* Set cause */
	reg = mftc0(13, 0);
	reg |= (C_SW1);
	mttc0(13, 0, reg);
}

void
platform_ipi_clear(void)
{
	uint32_t reg;

	reg = mips_rd_cause();
	reg &= ~(C_SW1);
	mips_wr_cause(reg);
}

int
platform_ipi_hardintr_num(void)
{

	return (-1);
}

int
platform_ipi_softintr_num(void)
{

	return (1);
}

void
platform_init_ap(int cpuid)
{
	uint32_t clock_int_mask;
	uint32_t ipi_intr_mask;

	/*
	 * Clear any pending IPIs.
	 */
	platform_ipi_clear();

	/*
	 * Unmask the clock and ipi interrupts.
	 */
	ipi_intr_mask = soft_int_mask(platform_ipi_softintr_num());
	clock_int_mask = hard_int_mask(5);
	set_intr_mask(ipi_intr_mask | clock_int_mask);

	mips_wbflush();
}

void
platform_cpu_mask(cpuset_t *mask)
{
	uint32_t i, ncpus, reg;

	reg = mftc0(0, 2);
	ncpus = ((reg & MVPCONF0_PVPE_MASK) >> MVPCONF0_PVPE_SHIFT) + 1;

	CPU_ZERO(mask);
	for (i = 0; i < ncpus; i++)
		CPU_SET(i, mask);
}

struct cpu_group *
platform_smp_topo(void)
{

	return (smp_topo_none());
}

int
platform_start_ap(int cpuid)
{
	uint32_t reg;
	int timeout;

	/* Enter into configuration */
	reg = read_c0_register32(0, 1);
	reg |= (MVPCONTROL_VPC);
	write_c0_register32(0, 1, reg);

	set_thread_context(cpuid);

	/*
	 * Hint: how to set entry point.
	 * reg = 0x80000000;
	 * mttc0(2, 3, reg);
	 */

	/* Enable thread */
	reg = mftc0(2, 1);
	reg |= (TCSTATUS_A);
	mttc0(2, 1, reg);

	/* Unhalt CPU core */
	mttc0(2, 4, 0);

	/* Activate VPE */
	reg = mftc0(1, 2);
	reg |= (VPECONF0_VPA);
	mttc0(1, 2, reg);

	/* Out of configuration */
	reg = read_c0_register32(0, 1);
	reg &= ~(MVPCONTROL_VPC);
	write_c0_register32(0, 1, reg);

	evpe();

	if (atomic_cmpset_32(&malta_ap_boot, ~0, cpuid) == 0)
		return (-1);

	printf("Waiting for cpu%d to start\n", cpuid);

	timeout = 100;
	do {
		DELAY(1000);
		if (atomic_cmpset_32(&malta_ap_boot, 0, ~0) != 0) {
			printf("CPU %d started\n", cpuid);
			return (0);
		}
	} while (timeout--);

	printf("CPU %d failed to start\n", cpuid);

	return (0);
}
