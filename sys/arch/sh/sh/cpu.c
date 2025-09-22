/*	$OpenBSD: cpu.c,v 1.7 2024/11/05 18:58:59 miod Exp $	*/
/*	$NetBSD: cpu.c,v 1.8 2006/01/02 23:16:20 uwe Exp $	*/

/*-
 * Copyright (c) 2002 The NetBSD Foundation, Inc.
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/device.h>

#include <sh/cpu.h>
#include <sh/clock.h>
#include <sh/cache.h>
#include <sh/mmu.h>

#include <machine/autoconf.h>

int	cpu_match(struct device *, void *, void *);
void	cpu_attach(struct device *, struct device *, void *);

const struct cfattach cpu_ca = {
	sizeof(struct device), cpu_match, cpu_attach
};

struct cfdriver cpu_cd = {
	NULL, "cpu", DV_DULL
};

struct cpu_info cpu_info_store;

int
cpu_match(struct device *parent, void *vcf, void *aux)
{
	struct mainbus_attach_args *ma = aux;

	if (strcmp(ma->ma_name, cpu_cd.cd_name) != 0)
		return (0);

	return (1);
}

void
cpu_attach(struct device *parent, struct device *self, void *aux)
{
	extern char cpu_model[120];

#define	MHZ(x) ((x) / 1000000), (((x) % 1000000) / 1000)
	printf(": HITACHI %s %d.%02d MHz PCLOCK %d.%02d MHz\n",
	    cpu_model, MHZ(sh_clock_get_cpuclock()),
	    MHZ(sh_clock_get_pclock()));
#undef MHZ
	sh_cache_information();
	sh_mmu_information();
}

void
need_resched(struct cpu_info *ci)
{
	ci->ci_want_resched = 1;

	if (ci->ci_curproc)
		aston(ci->ci_curproc);
}
