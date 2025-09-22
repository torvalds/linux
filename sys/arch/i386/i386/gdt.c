/*	$OpenBSD: gdt.c,v 1.44 2023/01/30 10:49:05 jsg Exp $	*/
/*	$NetBSD: gdt.c,v 1.28 2002/12/14 09:38:50 junyoung Exp $	*/

/*-
 * Copyright (c) 1996, 1997 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by John T. Kohl and Charles M. Hannum.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * The initial GDT is setup for the boot processor.  The GDT holds
 * NGDT descriptors.
 *
 * Every CPU in a system has its own copy of the GDT.  The only real difference
 * between the two are currently that there is a cpu-specific segment holding
 * the struct cpu_info of the processor, for simplicity at getting cpu_info
 * fields from assembly.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mutex.h>

#include <machine/gdt.h>
#include <machine/tss.h>

struct mutex gdt_lock_store = MUTEX_INITIALIZER(IPL_HIGH);

/*
 * Lock and unlock the GDT.
 */
#define gdt_lock()	(mtx_enter(&gdt_lock_store))
#define gdt_unlock()	(mtx_leave(&gdt_lock_store))

/* XXX needs spinlocking if we ever mean to go finegrained. */
void
setgdt(int sel, void *base, size_t limit, int type, int dpl, int def32,
    int gran)
{
	struct segment_descriptor *sd = &cpu_info_primary.ci_gdt[sel].sd;
	CPU_INFO_ITERATOR cii;
	struct cpu_info *ci;

	KASSERT(sel < NGDT);

	setsegment(sd, base, limit, type, dpl, def32, gran);
	CPU_INFO_FOREACH(cii, ci)
		if (ci->ci_gdt != NULL && ci != &cpu_info_primary)
			ci->ci_gdt[sel].sd = *sd;
}

/*
 * Initialize the GDT subsystem.  Called from autoconf().
 */
void
gdt_init(void)
{
	struct cpu_info *ci = &cpu_info_primary;

	setsegment(&ci->ci_gdt[GCPU_SEL].sd, ci, sizeof(struct cpu_info)-1,
	    SDT_MEMRWA, SEL_KPL, 0, 0);

	gdt_init_cpu(ci);
}

#ifdef MULTIPROCESSOR
/*
 * Allocate shadow GDT for a slave cpu.
 */
void
gdt_alloc_cpu(struct cpu_info *ci)
{
	bcopy(cpu_info_primary.ci_gdt, ci->ci_gdt, GDT_SIZE);
	setsegment(&ci->ci_gdt[GCPU_SEL].sd, ci, sizeof(struct cpu_info)-1,
	    SDT_MEMRWA, SEL_KPL, 0, 0);
}
#endif	/* MULTIPROCESSOR */


/*
 * Load appropriate gdt descriptor; we better be running on *ci
 * (for the most part, this is how a cpu knows who it is).
 */
void
gdt_init_cpu(struct cpu_info *ci)
{
	struct region_descriptor region;

	setsegment(&ci->ci_gdt[GTSS_SEL].sd, ci->ci_tss,
	    sizeof(*ci->ci_tss)-1, SDT_SYS386TSS, SEL_KPL, 0, 0);
	setsegment(&ci->ci_gdt[GNMITSS_SEL].sd, ci->ci_nmi_tss,
	    sizeof(*ci->ci_nmi_tss)-1, SDT_SYS386TSS, SEL_KPL, 0, 0);

	setregion(&region, ci->ci_gdt, GDT_SIZE - 1);
	lgdt(&region);

	ltr(GSEL(GTSS_SEL, SEL_KPL));
	lldt(0);
}
