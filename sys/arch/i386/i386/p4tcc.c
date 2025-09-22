/*	$OpenBSD: p4tcc.c,v 1.19 2014/09/14 14:17:23 jsg Exp $ */
/*
 * Copyright (c) 2003 Ted Unangst
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*
 * Restrict power consumption by using thermal control circuit.
 * This operates independently of speedstep.
 * Found on Pentium 4 and later models (feature TM).
 *
 * References:
 * Intel Developer's manual v.3 #245472-012
 *
 * On some models, the cpu can hang if it's running at a slow speed.
 * Workarounds included below.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysctl.h>

#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/specialreg.h>

static struct {
	u_short level;
	u_short reg;
} tcc[] = {
	{ 88, 0 },
	{ 75, 7 },
	{ 63, 6 },
	{ 50, 5 },
	{ 38, 4 },
	{ 25, 3 },
	{ 13, 2 },
	{ 0, 1 }
};

#define TCC_LEVELS sizeof(tcc) / sizeof(tcc[0])

extern int setperf_prio;
int p4tcc_level;

int p4tcc_cpuspeed(int *);

void
p4tcc_init(int family, int step)
{
	if (setperf_prio > 1)
		return;

	switch (family) {
	case 0xf:	/* Pentium 4 */
		switch (step) {
		case 0x22:	/* errata O50 P44 and Z21 */
		case 0x24:
		case 0x25:
		case 0x27:
		case 0x29:
			/* hang with 12.5 */
			tcc[TCC_LEVELS - 1].reg = 2;
			break;
		case 0x07:	/* errata N44 and P18 */
		case 0x0a:
		case 0x12:
		case 0x13:
			/* hang at 12.5 and 25 */
			tcc[TCC_LEVELS - 1].reg = 3;
			tcc[TCC_LEVELS - 2].reg = 3;
			break;
		}
		break;
	}

	p4tcc_level = tcc[0].level;
	cpu_setperf = p4tcc_setperf;
	cpu_cpuspeed = p4tcc_cpuspeed;
	setperf_prio = 1;
}

int
p4tcc_cpuspeed(int *speed)
{
	*speed = cpuspeed * (p4tcc_level + 12) / 100;

	return 0;
}

void
p4tcc_setperf(int level)
{
	int i;
	uint64_t msreg, vet;

	for (i = 0; i < TCC_LEVELS; i++) {
		if (level >= tcc[i].level)
			break;
	}
	if (i == TCC_LEVELS)
		i = TCC_LEVELS - 1;

	msreg = rdmsr(MSR_THERM_CONTROL);
	msreg &= ~0x1e; /* bit 0 reserved */
	if (tcc[i].reg != 0) /* enable it */
		msreg |= tcc[i].reg << 1 | 1 << 4;
	wrmsr(MSR_THERM_CONTROL, msreg);
	vet = rdmsr(MSR_THERM_CONTROL);

	if ((vet & 0x1e) != (msreg & 0x1e))
		printf("p4_tcc: cpu did not honor request\n");
	else
		p4tcc_level = tcc[i].level;
}
