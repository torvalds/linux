/*	$OpenBSD: k1x-pstate.c,v 1.11 2021/08/11 18:31:48 tb Exp $ */
/*
 * Copyright (c) 2011 Bryan Steele <brynet@gmail.com>
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
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/* AMD K10/K11 pstate driver */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>

#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/bus.h>

#include "acpicpu.h"

#if NACPICPU > 0
#include <dev/acpi/acpidev.h>
#endif

extern int setperf_prio;

#define MSR_K1X_LIMIT		0xc0010061
#define MSR_K1X_CONTROL		0xc0010062
#define MSR_K1X_STATUS		0xc0010063
#define MSR_K1X_CONFIG		0xc0010064

/* MSR_K1X_LIMIT */
#define K1X_PSTATE_MAX_VAL(x)	(((x) >> 4) & 0x7)
#define K1X_PSTATE_LIMIT(x)	(((x)) & 0x7)

/* MSR_K1X_CONFIG */
#define K1X_FID(x)		((x) & 0x3f)
#define K1X_DID(x)		(((x) >> 6) & 0x07)

/* Maximum pstates */
#define K1X_MAX_STATES		16

struct k1x_state {
	int freq;
	u_int8_t fid;
};

struct k1x_cpu_state {
	struct k1x_state state_table[K1X_MAX_STATES];
	u_int n_states;
};

struct k1x_cpu_state *k1x_current_state;

void k1x_transition(struct k1x_cpu_state *, int);

#if NACPICPU > 0
void k1x_acpi_init(struct k1x_cpu_state *);
void k1x_acpi_states(struct k1x_cpu_state *, struct acpicpu_pss *, int);
#endif

void
k1x_setperf(int level)
{
	u_int i = 0;
	struct k1x_cpu_state *cstate;

	cstate = k1x_current_state;

	i = ((level * cstate->n_states) + 1) / 101;
	if (i >= cstate->n_states)
		i = cstate->n_states - 1;

	k1x_transition(cstate, i);
}

void
k1x_transition(struct k1x_cpu_state *cstate, int level)
{
	u_int64_t msr;
	int i, cfid, fid = cstate->state_table[level].fid;

	wrmsr(MSR_K1X_CONTROL, fid);
	for (i = 0; i < 100; i++) {
		msr = rdmsr(MSR_K1X_STATUS);
		cfid = K1X_FID(msr);
		if (cfid == fid)
			break;
		DELAY(100);
	}
	if (cfid == fid) {
		cpuspeed = cstate->state_table[level].freq;
#if 0
		(void)printf("Target: %d Current: %d Pstate: %d\n",
		    cstate->state_table[level].freq,
		    cpuspeed, cfid);
#endif
	}
}

#if NACPICPU > 0

void
k1x_acpi_states(struct k1x_cpu_state *cstate, struct acpicpu_pss *pss,
    int nstates)
{
	struct k1x_state state;
	int j, n;
	u_int32_t ctrl;

	for (n = 0; n < cstate->n_states; n++) {
		ctrl = pss[n].pss_ctrl;
		state.fid = K1X_FID(ctrl);
		state.freq = pss[n].pss_core_freq;
		j = n;
		while (j > 0 && cstate->state_table[j - 1].freq > state.freq) {
			memcpy(&cstate->state_table[j],
			    &cstate->state_table[j - 1],
			    sizeof(struct k1x_state));
			--j;
		}
		memcpy(&cstate->state_table[j], &state,
		    sizeof(struct k1x_state));
	}
}

void
k1x_acpi_init(struct k1x_cpu_state *cstate)
{
	struct acpicpu_pss *pss;

	cstate->n_states = acpicpu_fetch_pss(&pss);
	if (cstate->n_states == 0)
		return;

	k1x_acpi_states(cstate, pss, cstate->n_states);

	return;
}

#endif /* NACPICPU */

void
k1x_init(struct cpu_info *ci)
{
	struct k1x_cpu_state *cstate;
	struct k1x_state *state;
	u_int i;

	if (setperf_prio > 1)
		return;

	cstate = malloc(sizeof(struct k1x_cpu_state), M_DEVBUF, M_NOWAIT);
	if (!cstate)
		return;

	cstate->n_states = 0;

#if NACPICPU > 0
	k1x_acpi_init(cstate);
#endif
	if (cstate->n_states) {
		printf("%s: %d MHz: speeds:",
		    ci->ci_dev->dv_xname, cpuspeed);
		for (i = cstate->n_states; i > 0; i--) {
			state = &cstate->state_table[i-1];
			printf(" %d", state->freq);
		}
		printf(" MHz\n");
		k1x_current_state = cstate;
		cpu_setperf = k1x_setperf;
		setperf_prio = 1;
		return;
	}
	free(cstate, M_DEVBUF, sizeof(*cstate));
}
