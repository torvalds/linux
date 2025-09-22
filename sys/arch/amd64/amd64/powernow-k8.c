/*	$OpenBSD: powernow-k8.c,v 1.29 2022/02/21 08:16:08 jsg Exp $ */
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
/* AMD POWERNOW K8 driver */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>

#include <dev/isa/isareg.h>
#include <amd64/include/isa_machdep.h>

#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/bus.h>

#include "acpicpu.h"

#if NACPICPU > 0
#include <dev/acpi/acpidev.h>
#endif

#define BIOS_START			0xe0000
#define	BIOS_LEN			0x20000

extern int setperf_prio;
extern int perflevel;

/*
 * MSRs and bits used by PowerNow technology
 */
#define MSR_AMDK7_FIDVID_CTL		0xc0010041
#define MSR_AMDK7_FIDVID_STATUS		0xc0010042

/* Bitfields used by K8 */

#define PN8_CTR_FID(x)			((x) & 0x3f)
#define PN8_CTR_VID(x)			(((x) & 0x1f) << 8)
#define PN8_CTR_PENDING(x)		(((x) & 1) << 32)

#define PN8_STA_CFID(x)			((x) & 0x3f)
#define PN8_STA_SFID(x)			(((x) >> 8) & 0x3f)
#define PN8_STA_MFID(x)			(((x) >> 16) & 0x3f)
#define PN8_STA_PENDING(x)		(((x) >> 31) & 0x01)
#define PN8_STA_CVID(x)			(((x) >> 32) & 0x1f)
#define PN8_STA_SVID(x)			(((x) >> 40) & 0x1f)
#define PN8_STA_MVID(x)			(((x) >> 48) & 0x1f)

/* Reserved1 to PowerNow K8 configuration */
#define PN8_PSB_TO_RVO(x)		((x) & 0x03)
#define PN8_PSB_TO_IRT(x)		(((x) >> 2) & 0x03)
#define PN8_PSB_TO_MVS(x)		(((x) >> 4) & 0x03)
#define PN8_PSB_TO_BATT(x)		(((x) >> 6) & 0x03)

/* ACPI ctr_val status register to PowerNow K8 configuration */
#define PN8_ACPI_CTRL_TO_FID(x)		((x) & 0x3f)
#define PN8_ACPI_CTRL_TO_VID(x)		(((x) >> 6) & 0x1f)
#define PN8_ACPI_CTRL_TO_VST(x)		(((x) >> 11) & 0x1f)
#define PN8_ACPI_CTRL_TO_MVS(x)		(((x) >> 18) & 0x03)
#define PN8_ACPI_CTRL_TO_PLL(x)		(((x) >> 20) & 0x7f)
#define PN8_ACPI_CTRL_TO_RVO(x)		(((x) >> 28) & 0x03)
#define PN8_ACPI_CTRL_TO_IRT(x)		(((x) >> 30) & 0x03)

#define PN8_PSS_CFID(x)			((x) & 0x3f)
#define PN8_PSS_CVID(x)			(((x) >> 6) & 0x1f)

#define WRITE_FIDVID(fid, vid, ctrl)	\
	wrmsr(MSR_AMDK7_FIDVID_CTL,	\
	    (((ctrl) << 32) | (1ULL << 16) | ((vid) << 8) | (fid)))


#define COUNT_OFF_IRT(irt)	DELAY(10 * (1 << (irt)))
#define COUNT_OFF_VST(vst)	DELAY(20 * (vst))

#define FID_TO_VCO_FID(fid)	\
	(((fid) < 8) ? (8 + ((fid) << 1)) : (fid))

#define POWERNOW_MAX_STATES		16

struct k8pnow_state {
	int freq;
	uint8_t fid;
	uint8_t vid;
};

struct k8pnow_cpu_state {
	struct k8pnow_state state_table[POWERNOW_MAX_STATES];
	unsigned int n_states;
	unsigned int sgtc;
	unsigned int vst;
	unsigned int mvs;
	unsigned int pll;
	unsigned int rvo;
	unsigned int irt;
	int low;
};

struct psb_s {
	char signature[10];     /* AMDK7PNOW! */
	uint8_t version;
	uint8_t flags;
	uint16_t ttime;		/* Min Settling time */
	uint8_t reserved;
	uint8_t n_pst;
};

struct pst_s {
	uint32_t cpuid;
	uint8_t pll;
	uint8_t fid;
	uint8_t vid;
	uint8_t n_states;
};

struct k8pnow_cpu_state *k8pnow_current_state;

int k8pnow_read_pending_wait(uint64_t *);
int k8pnow_decode_pst(struct k8pnow_cpu_state *, uint8_t *);
int k8pnow_states(struct k8pnow_cpu_state *, uint32_t, unsigned int, unsigned int);
void k8pnow_transition(struct k8pnow_cpu_state *e, int);

#if NACPICPU > 0
int k8pnow_acpi_init(struct k8pnow_cpu_state *, uint64_t);
void k8pnow_acpi_pss_changed(struct acpicpu_pss *, int);
int k8pnow_acpi_states(struct k8pnow_cpu_state *, struct acpicpu_pss *, int,
    uint64_t);
#endif

int
k8pnow_read_pending_wait(uint64_t *status)
{
	unsigned int i = 100000;

	while (i--) {
		*status = rdmsr(MSR_AMDK7_FIDVID_STATUS);
		if (!PN8_STA_PENDING(*status))
			return 0;

	}
	printf("k8pnow_read_pending_wait: change pending stuck.\n");
	return 1;
}

void
k8_powernow_setperf(int level)
{
	unsigned int i;
	struct k8pnow_cpu_state *cstate;

	cstate = k8pnow_current_state;

	i = ((level * cstate->n_states) + 1) / 101;
	if (i >= cstate->n_states)
		i = cstate->n_states - 1;

	k8pnow_transition(cstate, i);
}

void
k8pnow_transition(struct k8pnow_cpu_state *cstate, int level)
{
	uint64_t status;
	int cfid, cvid, fid = 0, vid = 0;
	int rvo;
	u_int val;

	/*
	 * We dont do a k8pnow_read_pending_wait here, need to ensure that the
	 * change pending bit isn't stuck,
	 */
	status = rdmsr(MSR_AMDK7_FIDVID_STATUS);
	if (PN8_STA_PENDING(status))
		return;
	cfid = PN8_STA_CFID(status);
	cvid = PN8_STA_CVID(status);

	fid = cstate->state_table[level].fid;
	vid = cstate->state_table[level].vid;

	if (fid == cfid && vid == cvid)
		return;

	/*
	 * Phase 1: Raise core voltage to requested VID if frequency is
	 * going up.
	 */
	while (cvid > vid) {
		val = cvid - (1 << cstate->mvs);
		WRITE_FIDVID(cfid, (val > 0) ? val : 0, 1ULL);
		if (k8pnow_read_pending_wait(&status))
			return;
		cvid = PN8_STA_CVID(status);
		COUNT_OFF_VST(cstate->vst);
	}

	/* ... then raise to voltage + RVO (if required) */
	for (rvo = cstate->rvo; rvo > 0 && cvid > 0; --rvo) {
		/* XXX It's not clear from spec if we have to do that
		 * in 0.25 step or in MVS.  Therefore do it as it's done
		 * under Linux */
		WRITE_FIDVID(cfid, cvid - 1, 1ULL);
		if (k8pnow_read_pending_wait(&status))
			return;
		cvid = PN8_STA_CVID(status);
		COUNT_OFF_VST(cstate->vst);
	}

	/* Phase 2: change to requested core frequency */
	if (cfid != fid) {
		int vco_fid, vco_cfid;

		vco_fid = FID_TO_VCO_FID(fid);
		vco_cfid = FID_TO_VCO_FID(cfid);

		while (abs(vco_fid - vco_cfid) > 2) {
			if (fid > cfid) {
				if (cfid > 6)
					val = cfid + 2;
				else
					val = FID_TO_VCO_FID(cfid) + 2;
			} else
				val = cfid - 2;
			WRITE_FIDVID(val, cvid, (uint64_t)cstate->pll * 1000 / 5);

			if (k8pnow_read_pending_wait(&status))
				return;
			cfid = PN8_STA_CFID(status);
			COUNT_OFF_IRT(cstate->irt);

			vco_cfid = FID_TO_VCO_FID(cfid);
		}

		WRITE_FIDVID(fid, cvid, (uint64_t) cstate->pll * 1000 / 5);
		if (k8pnow_read_pending_wait(&status))
			return;
		cfid = PN8_STA_CFID(status);
		COUNT_OFF_IRT(cstate->irt);
	}

	/* Phase 3: change to requested voltage */
	if (cvid != vid) {
		WRITE_FIDVID(cfid, vid, 1ULL);
		if (k8pnow_read_pending_wait(&status))
			return;
		cvid = PN8_STA_CVID(status);
		COUNT_OFF_VST(cstate->vst);
	}

	if (cfid == fid || cvid == vid)
		cpuspeed = cstate->state_table[level].freq;
}

/*
 * Given a set of pair of fid/vid, and number of performance states,
 * compute state_table via an insertion sort.
 */
int
k8pnow_decode_pst(struct k8pnow_cpu_state *cstate, uint8_t *p)
{
	int i, j, n;
	struct k8pnow_state state;
	for (n = 0, i = 0; i < cstate->n_states; i++) {
		state.fid = *p++;
		state.vid = *p++;

		/*
		 * The minimum supported frequency per the data sheet is 800MHz
		 * The maximum supported frequency is 5000MHz.
		 */
		state.freq = 800 + state.fid * 100;
		j = n;
		while (j > 0 && cstate->state_table[j - 1].freq > state.freq) {
			memcpy(&cstate->state_table[j],
			    &cstate->state_table[j - 1],
			    sizeof(struct k8pnow_state));
			--j;
		}
		memcpy(&cstate->state_table[j], &state,
		    sizeof(struct k8pnow_state));
		n++;
	}
	return 1;
}

#if NACPICPU > 0

int
k8pnow_acpi_states(struct k8pnow_cpu_state * cstate, struct acpicpu_pss * pss,
    int nstates, uint64_t status)
{
	struct k8pnow_state state;
	int j, k, n;
	uint32_t ctrl;

	k = -1;

	for (n = 0; n < cstate->n_states; n++) {
		if ((PN8_STA_CFID(status) == PN8_PSS_CFID(pss[n].pss_status)) &&
		    (PN8_STA_CVID(status) == PN8_PSS_CVID(pss[n].pss_status)))
			k = n;
		ctrl = pss[n].pss_ctrl;
		state.fid = PN8_ACPI_CTRL_TO_FID(ctrl);
		state.vid = PN8_ACPI_CTRL_TO_VID(ctrl);

		state.freq = pss[n].pss_core_freq;
		j = n;
		while (j > 0 && cstate->state_table[j - 1].freq > state.freq) {
			memcpy(&cstate->state_table[j],
			    &cstate->state_table[j - 1],
			    sizeof(struct k8pnow_state));
			--j;
		}
		memcpy(&cstate->state_table[j], &state,
		    sizeof(struct k8pnow_state));
	}

	return k;
}

void
k8pnow_acpi_pss_changed(struct acpicpu_pss * pss, int npss)
{
	int curs, needtran;
	struct k8pnow_cpu_state *cstate, *nstate;
	uint32_t ctrl;
	uint64_t status;

	status = rdmsr(MSR_AMDK7_FIDVID_STATUS);
	cstate = k8pnow_current_state;

	nstate = malloc(sizeof(struct k8pnow_cpu_state), M_DEVBUF, M_NOWAIT);
	if (!nstate)
		return;

	curs = k8pnow_acpi_states(nstate, pss, npss, status);
	needtran = 0;

	if (curs < 0) {
		/*
		 * Our current operating state is not among
		 * the ones found the new PSS.
		 */
		curs = ((perflevel * npss) + 1) / 101;
		if (curs >= npss)
			curs = npss - 1;
		needtran = 1;
	}

	ctrl = pss[curs].pss_ctrl;

	nstate->rvo = PN8_ACPI_CTRL_TO_RVO(ctrl);
	nstate->vst = PN8_ACPI_CTRL_TO_VST(ctrl);
	nstate->mvs = PN8_ACPI_CTRL_TO_MVS(ctrl);
	nstate->pll = PN8_ACPI_CTRL_TO_PLL(ctrl);
	nstate->irt = PN8_ACPI_CTRL_TO_IRT(ctrl);
	nstate->low = 0;
	nstate->n_states = npss;

	if (needtran)
		k8pnow_transition(nstate, curs);

	free(cstate, M_DEVBUF, sizeof(*cstate));
	k8pnow_current_state = nstate;
}

int
k8pnow_acpi_init(struct k8pnow_cpu_state * cstate, uint64_t status)
{
	int curs;
	uint32_t ctrl;
	struct acpicpu_pss *pss;

	cstate->n_states = acpicpu_fetch_pss(&pss);
	if (cstate->n_states == 0)
		return 0;
	acpicpu_set_notify(k8pnow_acpi_pss_changed);

	curs = k8pnow_acpi_states(cstate, pss, cstate->n_states, status);
	ctrl = pss[curs].pss_ctrl;

	cstate->rvo = PN8_ACPI_CTRL_TO_RVO(ctrl);
	cstate->vst = PN8_ACPI_CTRL_TO_VST(ctrl);
	cstate->mvs = PN8_ACPI_CTRL_TO_MVS(ctrl);
	cstate->pll = PN8_ACPI_CTRL_TO_PLL(ctrl);
	cstate->irt = PN8_ACPI_CTRL_TO_IRT(ctrl);
	cstate->low = 0;

	return 1;
}

#endif /* NACPICPU */

int
k8pnow_states(struct k8pnow_cpu_state *cstate, uint32_t cpusig,
    unsigned int fid, unsigned int vid)
{
	struct psb_s *psb;
	struct pst_s *pst;
	uint8_t *p;
	int i;

	for (p = (u_int8_t *)ISA_HOLE_VADDR(BIOS_START);
	    p < (u_int8_t *)ISA_HOLE_VADDR(BIOS_START + BIOS_LEN); p += 16) {
		if (memcmp(p, "AMDK7PNOW!", 10) == 0) {
			psb = (struct psb_s *)p;
			if (psb->version != 0x14)
				return 0;

			cstate->vst = psb->ttime;
			cstate->rvo = PN8_PSB_TO_RVO(psb->reserved);
			cstate->irt = PN8_PSB_TO_IRT(psb->reserved);
			cstate->mvs = PN8_PSB_TO_MVS(psb->reserved);
			cstate->low = PN8_PSB_TO_BATT(psb->reserved);
			p+= sizeof(struct psb_s);

			for(i = 0; i < psb->n_pst; ++i) {
				pst = (struct pst_s *) p;

				cstate->pll = pst->pll;
				cstate->n_states = pst->n_states;
				if (cpusig == pst->cpuid &&
				    pst->fid == fid && pst->vid == vid) {
					return (k8pnow_decode_pst(cstate,
					    p+= sizeof (struct pst_s)));
				}
				p += sizeof(struct pst_s) + 2
				    * cstate->n_states;
			}
		}
	}

	return 0;

}

void
k8_powernow_init(struct cpu_info *ci)
{
	uint64_t status;
	u_int maxfid, maxvid, i;
	u_int32_t extcpuid, dummy;
	struct k8pnow_cpu_state *cstate;
	struct k8pnow_state *state;
	char * techname = NULL;

	if (setperf_prio > 1)
		return;

	cstate = malloc(sizeof(struct k8pnow_cpu_state), M_DEVBUF, M_NOWAIT);
	if (!cstate)
		return;

	cstate->n_states = 0;
	status = rdmsr(MSR_AMDK7_FIDVID_STATUS);
	maxfid = PN8_STA_MFID(status);
	maxvid = PN8_STA_MVID(status);

	/*
	* If start FID is different to max FID, then it is a
	* mobile processor.  If not, it is a low powered desktop
	* processor.
	*/
	if (PN8_STA_SFID(status) != PN8_STA_MFID(status))
		techname = "PowerNow! K8";
	else
		techname = "Cool'n'Quiet K8";

#if NACPICPU > 0
	/* If we have acpi check acpi first */
	if (!k8pnow_acpi_init(cstate, status))
#endif
	{
		if (!k8pnow_states(cstate, ci->ci_signature, maxfid, maxvid)) {
			/* Extended CPUID signature value */
			CPUID(0x80000001, extcpuid, dummy, dummy, dummy);
			k8pnow_states(cstate, extcpuid, maxfid, maxvid);
		}
	}
	if (cstate->n_states) {
		printf("%s: %s %d MHz: speeds:",
		    ci->ci_dev->dv_xname, techname, cpuspeed);
		for (i = cstate->n_states; i > 0; i--) {
			state = &cstate->state_table[i-1];
			printf(" %d", state->freq);
		}
		printf(" MHz\n");
		k8pnow_current_state = cstate;
		cpu_setperf = k8_powernow_setperf;
		setperf_prio = 1;
		return;
	}
	free(cstate, M_DEVBUF, sizeof(*cstate));
}
