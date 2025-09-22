/*	$OpenBSD: cpu_subr.c,v 1.8 2015/04/27 07:20:57 mpi Exp $	*/

/*
 * Copyright (c) 2013 Martin Pieuchot
 * Copyright (c) 2005 Mark Kettenis
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

#include <sys/param.h>

#include <powerpc/cpu.h>

int		ppc_cpuidle;		/* Support DOZE, NAP or DEEP NAP? */
int		ppc_altivec;		/* CPU has altivec support. */
int		ppc_proc_is_64b;	/* CPU is 64bit */
int		ppc_nobat;		/* Do not use BAT registers. */

struct patch {
	uint32_t *s;
	uint32_t *e;
};
extern struct patch	rfi_start, nop32_start, nopbat_start;
extern uint32_t		rfid_inst, nop_inst;

void
cpu_bootstrap(void)
{
	uint32_t cpu;
	uint32_t *inst;
	struct patch *p;

	cpu = ppc_mfpvr() >> 16;

	switch (cpu) {
	case PPC_CPU_IBM970:
	case PPC_CPU_IBM970FX:
	case PPC_CPU_IBM970MP:
		ppc_nobat = 1;
		ppc_proc_is_64b = 1;

		for (p = &rfi_start; p->s; p++) {
			for (inst = p->s; inst < p->e; inst++)
				*inst = rfid_inst;
			syncicache(p->s, (p->e - p->s) * sizeof(*p->e));
		}
		break;
	case PPC_CPU_MPC83xx:
		ppc_mtibat4u(0);
		ppc_mtibat5u(0);
		ppc_mtibat6u(0);
		ppc_mtibat7u(0);
		ppc_mtdbat4u(0);
		ppc_mtdbat5u(0);
		ppc_mtdbat6u(0);
		ppc_mtdbat7u(0);
		/* FALLTHROUGH */
	default:
		ppc_mtibat0u(0);
		ppc_mtibat1u(0);
		ppc_mtibat2u(0);
		ppc_mtibat3u(0);
		ppc_mtdbat0u(0);
		ppc_mtdbat1u(0);
		ppc_mtdbat2u(0);
		ppc_mtdbat3u(0);

		for (p = &nop32_start; p->s; p++) {
			for (inst = p->s; inst < p->e; inst++)
				*inst = nop_inst;
			syncicache(p->s, (p->e - p->s) * sizeof(*p->e));
		}
	}

	if (ppc_nobat) {
		for (p = &nopbat_start; p->s; p++) {
			for (inst = p->s; inst < p->e; inst++)
				*inst = nop_inst;
			syncicache(p->s, (p->e - p->s) * sizeof(*p->e));
		}
	}
}

void
ppc_mtscomc(u_int32_t val)
{
	int s;

	s = ppc_intr_disable();
	__asm volatile ("mtspr 276,%0; isync" :: "r" (val));
	ppc_intr_enable(s);
}

void
ppc_mtscomd(u_int32_t val)
{
	int s;

	s = ppc_intr_disable();
	__asm volatile ("mtspr 277,%0; isync" :: "r" (val));
	ppc_intr_enable(s);
}

u_int64_t
ppc64_mfscomc(void)
{
	u_int64_t ret;
	int s;

	s = ppc_intr_disable();
	__asm volatile ("mfspr %0,276;"
	    " mr %0+1, %0; srdi %0,%0,32" : "=r" (ret));
	ppc_intr_enable(s);
	return ret;
}

void
ppc64_mtscomc(u_int64_t val)
{
	int s;

	s = ppc_intr_disable();
	__asm volatile ("sldi %0,%0,32; or %0,%0,%0+1;"
	    " mtspr 276,%0; isync" :: "r" (val));
	ppc_intr_enable(s);
}

u_int64_t
ppc64_mfscomd(void)
{
	u_int64_t ret;
	int s;

	s = ppc_intr_disable();
	__asm volatile ("mfspr %0,277;"
            " mr %0+1, %0; srdi %0,%0,32" : "=r" (ret));
	ppc_intr_enable(s);
	return ret;
}

static __inline u_int32_t
ppc64_mfhid0(u_int32_t *lo)
{
	u_int32_t hid0_hi, hid0_lo;

	__asm volatile ("mfspr %0,1008;"
	    " mr %1, %0; srdi %0,%0,32;" : "=r" (hid0_hi), "=r" (hid0_lo));
	if (lo != NULL)
		*lo = hid0_lo;
	return hid0_hi;
}

static __inline void
ppc64_mthid0(u_int32_t hid0_hi, u_int32_t hid0_lo)
{
	/*
	 * No! It's not a joke (:
	 *
	 * Note 1 of the Table 2-3 from the 970MP User manual.
	 */
	__asm volatile ("sldi %0,%0,32; or %0,%0,%1;"
	    "sync; mtspr 1008,%0;"
	    "mfspr %0,1008; mfspr %0,1008; mfspr %0,1008;"
	    "mfspr %0,1008; mfspr %0,1008; mfspr %0,1008;"
	    "isync" :: "r" (hid0_hi), "r"(hid0_lo));
}

u_int32_t
ppc_mfhid0(void)
{
	u_int32_t ret;

	/* Since the lower 32 bits are reserved, do not expose them. */
	if (ppc_proc_is_64b)
		return ppc64_mfhid0(NULL);

	__asm volatile ("mfspr %0,1008" : "=r" (ret));
	return ret;
}

void
ppc_mthid0(u_int32_t val)
{
	if (ppc_proc_is_64b) {
		u_int32_t lo;

		/* Don't write any garbage in the lower 32 bits. */
		(void)ppc64_mfhid0(&lo);
		return ppc64_mthid0(val, lo);
	}

	__asm volatile ("mtspr 1008,%0; isync" :: "r" (val));
}

u_int64_t
ppc64_mfhid1(void)
{
	u_int64_t ret;

	__asm volatile ("mfspr %0,1009;"
            " mr %0+1, %0; srdi %0,%0,32" : "=r" (ret));
	return ret;
}

void
ppc64_mthid1(u_int64_t val)
{
	__asm volatile ("sldi %0,%0,32; or %0,%0,%0+1;"
	    "mtspr 1009,%0; mtspr 1009,%0; isync;" :: "r" (val));
}

u_int64_t
ppc64_mfhid4(void)
{
	u_int64_t ret;

	__asm volatile ("mfspr %0,1012;"
            " mr %0+1, %0; srdi %0,%0,32" : "=r" (ret));
	return ret;
}

void
ppc64_mthid4(u_int64_t val)
{
	__asm volatile ("sldi %0,%0,32; or %0,%0,%0+1;"
	    "sync; mtspr 1012,%0; isync;" :: "r" (val));
}

u_int64_t
ppc64_mfhid5(void)
{
	u_int64_t ret;

	__asm volatile ("mfspr %0,1014;"
            " mr %0+1, %0; srdi %0,%0,32" : "=r" (ret));
	return ret;
}

void
ppc64_mthid5(u_int64_t val)
{
	__asm volatile ("sldi %0,%0,32; or %0,%0,%0+1;"
	    "sync; mtspr 1014,%0; isync;" :: "r" (val));
}
