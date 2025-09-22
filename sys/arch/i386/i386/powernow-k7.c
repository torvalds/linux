/* $OpenBSD: powernow-k7.c,v 1.44 2023/01/30 10:49:05 jsg Exp $ */

/*
 * Copyright (c) 2004 Martin Végiard.
 * Copyright (c) 2004-2005 Bruno Ducrot
 * Copyright (c) 2004 FUKUDA Nobuhiko <nfukuda@spa.is.uec.ac.jp>
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

/* AMD POWERNOW K7 driver */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>

#include <machine/cpufunc.h>
#include <machine/bus.h>

#include <dev/isa/isareg.h>
#include <i386/isa/isa_machdep.h>

#include "acpicpu.h"

#if NACPICPU > 0
#include <dev/acpi/acpidev.h>
#endif

#define BIOS_START			0xe0000
#define	BIOS_LEN			0x20000
#define BIOS_STEP			16

/*
 * MSRs and bits used by PowerNow! technology
 */
#define MSR_AMDK7_FIDVID_CTL		0xc0010041
#define MSR_AMDK7_FIDVID_STATUS		0xc0010042
#define AMD_PN_FID_VID			0x06
#define AMD_ERRATA_A0_CPUSIG		0x660

#define PN7_FLAG_ERRATA_A0		0x01
#define PN7_FLAG_DESKTOP_VRM		0x02

/* Bitfields used by K7 */
#define PN7_PSB_VERSION			0x12
#define PN7_CTR_FID(x)			((x) & 0x1f)
#define PN7_CTR_VID(x)			(((x) & 0x1f) << 8)
#define PN7_CTR_FIDC			0x00010000
#define PN7_CTR_VIDC			0x00020000
#define PN7_CTR_FIDCHRATIO		0x00100000
#define PN7_CTR_SGTC(x)			(((uint64_t)(x) & 0x000fffff) << 32)

#define PN7_STA_CFID(x)			((x) & 0x1f)
#define PN7_STA_SFID(x)			(((x) >> 8) & 0x1f)
#define PN7_STA_MFID(x)			(((x) >> 16) & 0x1f)
#define PN7_STA_CVID(x)			(((x) >> 32) & 0x1f)
#define PN7_STA_SVID(x)			(((x) >> 40) & 0x1f)
#define PN7_STA_MVID(x)			(((x) >> 48) & 0x1f)

/*
 * ACPI ctr_val status register to powernow k7 configuration
 */
#define PN7_ACPI_CTRL_TO_FID(x)		((x) & 0x1f)
#define PN7_ACPI_CTRL_TO_VID(x)		(((x) >> 5) & 0x1f)
#define PN7_ACPI_CTRL_TO_SGTC(x)	(((x) >> 10) & 0xffff)

#define WRITE_FIDVID(fid, vid, ctrl)	\
	wrmsr(MSR_AMDK7_FIDVID_CTL,	\
	    (((ctrl) << 32) | (1ULL << 16) | ((vid) << 8) | (fid)))

/*
 * Divide each value by 10 to get the processor multiplier.
 * Taken from powernow-k7.c/Linux by Dave Jones
 */
static int k7pnow_fid_to_mult[32] = {
	110, 115, 120, 125, 50, 55, 60, 65,
	70, 75, 80, 85, 90, 95, 100, 105,
	30, 190, 40, 200, 130, 135, 140, 210,
	150, 225, 160, 165, 170, 180, -1, -1
};

#define POWERNOW_MAX_STATES		16

struct k7pnow_state {
	int freq;
	int fid;
	int vid;
};

struct k7pnow_cpu_state {
	unsigned int fsb;
	unsigned int sgtc;
	struct k7pnow_state state_table[POWERNOW_MAX_STATES];
	unsigned int n_states;
	int flags;
};

struct psb_s {
	char signature[10];	/* AMDK7PNOW! */
	uint8_t version;
	uint8_t flags;
	uint16_t ttime;		/* Min Settling time */
	uint8_t reserved;
	uint8_t n_pst;
};

struct pst_s {
	uint32_t signature;
	uint8_t fsb;		/* Front Side Bus frequency (MHz) */
	uint8_t fid;		/* Max Frequency code */
	uint8_t vid;		/* Max Voltage code */
	uint8_t n_states;	/* Number of states */
};

struct k7pnow_cpu_state *k7pnow_current_state;
extern int setperf_prio;

int k7pnow_decode_pst(struct k7pnow_cpu_state *, uint8_t *, int);
int k7pnow_states(struct k7pnow_cpu_state *, uint32_t, unsigned int,
    unsigned int);

#if NACPICPU > 0
int k7pnow_acpi_init(struct k7pnow_cpu_state * cstate, uint64_t status);
int k7pnow_acpi_states(struct k7pnow_cpu_state * cstate,
    struct acpicpu_pss *pss, int nstates, uint64_t status);
void k7pnow_acpi_pss_changed(struct acpicpu_pss *pss, int npss);
#endif

void
k7_powernow_setperf(int level)
{
	unsigned int i;
	int cvid, cfid, vid = 0, fid = 0;
	uint64_t status, ctl;
	struct k7pnow_cpu_state * cstate;
	u_long s;

	cstate = k7pnow_current_state;

	i = ((level * cstate->n_states) + 1) / 101;
	if (i >= cstate->n_states)
		i = cstate->n_states - 1;
	fid = cstate->state_table[i].fid;
	vid = cstate->state_table[i].vid;

	if (fid == 0 || vid == 0)
		return;

	status = rdmsr(MSR_AMDK7_FIDVID_STATUS);
	cfid = PN7_STA_CFID(status);
	cvid = PN7_STA_CVID(status);

	/*
	 * We're already at the requested level.
	 */
	if (fid == cfid && vid == cvid)
		return;

	ctl = rdmsr(MSR_AMDK7_FIDVID_CTL) & PN7_CTR_FIDCHRATIO;

	ctl |= PN7_CTR_FID(fid);
	ctl |= PN7_CTR_VID(vid);
	ctl |= PN7_CTR_SGTC(cstate->sgtc);

	if (cstate->flags & PN7_FLAG_ERRATA_A0)
		s = intr_disable();

	if (k7pnow_fid_to_mult[fid] < k7pnow_fid_to_mult[cfid]) {
		wrmsr(MSR_AMDK7_FIDVID_CTL, ctl | PN7_CTR_FIDC);
		if (vid != cvid)
			wrmsr(MSR_AMDK7_FIDVID_CTL, ctl | PN7_CTR_VIDC);
	} else {
		wrmsr(MSR_AMDK7_FIDVID_CTL, ctl | PN7_CTR_VIDC);
		if (fid != cfid)
			wrmsr(MSR_AMDK7_FIDVID_CTL, ctl | PN7_CTR_FIDC);
	}

	if (cstate->flags & PN7_FLAG_ERRATA_A0)
		intr_restore(s);

	status = rdmsr(MSR_AMDK7_FIDVID_STATUS);
	cfid = PN7_STA_CFID(status);
	cvid = PN7_STA_CVID(status);
	if (cfid == fid || cvid == vid)
		cpuspeed = cstate->state_table[i].freq;
}

/*
 * Given a set of pair of fid/vid, and number of performance states,
 * compute state_table via an insertion sort.
 */
int
k7pnow_decode_pst(struct k7pnow_cpu_state * cstate, uint8_t *p, int npst)
{
	int i, j, n;
	struct k7pnow_state state;

	for (n = 0, i = 0; i < npst; ++i) {
		state.fid = *p++;
		state.vid = *p++;
		state.freq = k7pnow_fid_to_mult[state.fid]/10 * cstate->fsb;
		if ((cstate->flags & PN7_FLAG_ERRATA_A0) &&
		    (k7pnow_fid_to_mult[state.fid] % 10) == 5)
			continue;

		j = n;
		while (j > 0 && cstate->state_table[j - 1].freq > state.freq) {
			memcpy(&cstate->state_table[j],
			    &cstate->state_table[j - 1],
			    sizeof(struct k7pnow_state));
			--j;
		}
		memcpy(&cstate->state_table[j], &state,
		    sizeof(struct k7pnow_state));
		++n;
	}
	/*
	 * Fix powernow_max_states, if errata_a0 give us less states
	 * than expected.
	 */
	cstate->n_states = n;
	return 1;
}

int
k7pnow_states(struct k7pnow_cpu_state *cstate, uint32_t cpusig,
    unsigned int fid, unsigned int vid)
{
	int maxpst;
	struct psb_s *psb;
	struct pst_s *pst;
	uint8_t *p;

	/*
	 * Look in the 0xe0000 - 0x100000 physical address
	 * range for the pst tables; 16 byte blocks. End 10 bytes
	 * before the end of the range to avoid memcmp across a
	 * page boundary into unmapped memory.
	 */
	for (p = (u_int8_t *)ISA_HOLE_VADDR(BIOS_START);
	    p < (u_int8_t *)ISA_HOLE_VADDR(BIOS_START + BIOS_LEN) - 10;
	    p += BIOS_STEP) {
		if (memcmp(p, "AMDK7PNOW!", 10) == 0) {
			psb = (struct psb_s *)p;
			if (psb->version != PN7_PSB_VERSION)
				return 0;

			cstate->sgtc = psb->ttime * cstate->fsb;
			if (cstate->sgtc < 100 * cstate->fsb)
				cstate->sgtc = 100 * cstate->fsb;
			if (psb->flags & 1)
				cstate->flags |= PN7_FLAG_DESKTOP_VRM;
			p += sizeof(struct psb_s);

			for (maxpst = 0; maxpst < psb->n_pst; maxpst++) {
				pst = (struct pst_s*) p;

				if (cpusig == pst->signature && fid == pst->fid
				    && vid == pst->vid) {

					if (abs(cstate->fsb - pst->fsb) > 5)
						continue;
					cstate->n_states = pst->n_states;
					return (k7pnow_decode_pst(cstate,
					    p + sizeof(struct pst_s),
					    cstate->n_states));
				}
				p += sizeof(struct pst_s) +
				    (2 * pst->n_states);
			}
		}
	}

	return 0;
}

#if NACPICPU > 0

int
k7pnow_acpi_states(struct k7pnow_cpu_state * cstate, struct acpicpu_pss *pss,
    int nstates, uint64_t status)
{
	struct k7pnow_state state;
	int j, k, n;
	uint32_t ctrl;

	k = -1;
	for (n = 0; n < cstate->n_states; n++) {
		if (status == pss[n].pss_status)
			k = n;
		ctrl = pss[n].pss_ctrl;
		state.fid = PN7_ACPI_CTRL_TO_FID(ctrl);
		state.vid = PN7_ACPI_CTRL_TO_VID(ctrl);

		if ((cstate->flags & PN7_FLAG_ERRATA_A0) &&
		    (k7pnow_fid_to_mult[state.fid] % 10) == 5)
			continue;

		state.freq = pss[n].pss_core_freq;
		j = n;
		while (j > 0 && cstate->state_table[j - 1].freq > state.freq) {
			memcpy(&cstate->state_table[j],
			    &cstate->state_table[j - 1],
			sizeof(struct k7pnow_state));
			--j;
		}
		memcpy(&cstate->state_table[j], &state,
		    sizeof(struct k7pnow_state));
	}
	return k;
}

void
k7pnow_acpi_pss_changed(struct acpicpu_pss *pss, int npss)
{
	int curs;
	struct k7pnow_cpu_state *cstate;
	uint32_t ctrl;
	uint64_t status;

	status = rdmsr(MSR_AMDK7_FIDVID_STATUS);
	cstate = k7pnow_current_state;

	curs = k7pnow_acpi_states(cstate, pss, npss, status);
	ctrl = pss[curs].pss_ctrl;
	cstate->sgtc = PN7_ACPI_CTRL_TO_SGTC(ctrl);
	cstate->n_states = npss;
}

int
k7pnow_acpi_init(struct k7pnow_cpu_state *cstate, uint64_t status)
{
	int curs;
	uint32_t ctrl;
	struct acpicpu_pss *pss;
	int mfid;

	cstate->n_states = acpicpu_fetch_pss(&pss);
	if (cstate->n_states == 0)
		return 0;

	curs = k7pnow_acpi_states(cstate, pss, cstate->n_states, status);
	/* 
	 * XXX: Some BIOS supplied _PSS implementations have the wrong
	 * maximum frequency, if we encounter one of these punt and 
	 * hope the legacy tables have correct values.
	 */
	mfid = PN7_STA_MFID(status);
	if (mfid != cstate->state_table[cstate->n_states - 1].fid) {
		return 0;
	}

	acpicpu_set_notify(k7pnow_acpi_pss_changed);
	ctrl = pss[curs].pss_ctrl;
	cstate->sgtc = PN7_ACPI_CTRL_TO_SGTC(ctrl);

	return 1;
}

#endif /* NACPICPU */

void
k7_powernow_init(void)
{
	u_int regs[4];
	uint64_t status;
	u_int maxfid, startvid, currentfid;
	struct k7pnow_cpu_state *cstate;
	struct k7pnow_state *state;
	struct cpu_info *ci;
	char *techname = NULL;
	int i;

	if (setperf_prio > 1)
		return;

	ci = curcpu();

	cpuid(0x80000000, regs);
	if (regs[0] < 0x80000007)
		return;

	cpuid(0x80000007, regs);
	if (!(regs[3] & AMD_PN_FID_VID))
		return;

	/* Extended CPUID signature value */
	cpuid(0x80000001, regs);

	cstate = malloc(sizeof(struct k7pnow_cpu_state), M_DEVBUF, M_NOWAIT);
	if (!cstate)
		return;

	cstate->flags = cstate->n_states = 0;
	if (ci->ci_signature == AMD_ERRATA_A0_CPUSIG)
		cstate->flags |= PN7_FLAG_ERRATA_A0;

	status = rdmsr(MSR_AMDK7_FIDVID_STATUS);
	maxfid = PN7_STA_MFID(status);
	startvid = PN7_STA_SVID(status);
	currentfid = PN7_STA_CFID(status);

	cstate->fsb = cpuspeed / (k7pnow_fid_to_mult[currentfid]/10);

	if (!k7pnow_states(cstate, ci->ci_signature, maxfid, startvid))
		if (!k7pnow_states(cstate, regs[0], maxfid, startvid)) {
#if NACPICPU > 0
			/* If we have it try ACPI */
			k7pnow_acpi_init(cstate, status);
#endif
	}

	if (cstate->n_states) {
		if (cstate->flags & PN7_FLAG_DESKTOP_VRM)
			techname = "Cool'n'Quiet K7";
		else
			techname = "PowerNow! K7";
		printf("%s: %s %d MHz: speeds:",
		    ci->ci_dev->dv_xname, techname, cpuspeed);
		for (i = cstate->n_states; i > 0; i--) {
			state = &cstate->state_table[i-1];
			printf(" %d", state->freq);
		}
		printf(" MHz\n");

		k7pnow_current_state = cstate;
		cpu_setperf = k7_powernow_setperf;
		setperf_prio = 1;
		return;
	}
	free(cstate, M_DEVBUF, sizeof(*cstate));
}
