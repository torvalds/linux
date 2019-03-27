/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
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

/*
 * Many thanks to Nate Lawson for his helpful comments on this driver and
 * to Jung-uk Kim for testing.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/cpu.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/pcpu.h>
#include <sys/systm.h>

#include <machine/pc/bios.h>
#include <machine/md_var.h>
#include <machine/specialreg.h>
#include <machine/cputypes.h>
#include <machine/vmparam.h>
#include <sys/rman.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include "cpufreq_if.h"

#define PN7_TYPE	0
#define PN8_TYPE	1

/* Flags for some hardware bugs. */
#define A0_ERRATA	0x1	/* Bugs for the rev. A0 of Athlon (K7):
				 * Interrupts must be disabled and no half
				 * multipliers are allowed */
#define PENDING_STUCK	0x2	/* With some buggy chipset and some newer AMD64
				 * processor (Rev. G?):
				 * the pending bit from the msr FIDVID_STATUS
				 * is set forever.  No workaround :( */

/* Legacy configuration via BIOS table PSB. */
#define PSB_START	0
#define PSB_STEP	0x10
#define PSB_SIG		"AMDK7PNOW!"
#define PSB_LEN		10
#define PSB_OFF		0

struct psb_header {
	char		 signature[10];
	uint8_t		 version;
	uint8_t		 flags;
	uint16_t	 settlingtime;
	uint8_t		 res1;
	uint8_t		 numpst;
} __packed;

struct pst_header {
	uint32_t	 cpuid;
	uint8_t		 fsb;
	uint8_t		 maxfid;
	uint8_t		 startvid;
	uint8_t		 numpstates;
} __packed;

/*
 * MSRs and bits used by Powernow technology
 */
#define MSR_AMDK7_FIDVID_CTL		0xc0010041
#define MSR_AMDK7_FIDVID_STATUS		0xc0010042

/* Bitfields used by K7 */

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

/* ACPI ctr_val status register to powernow k7 configuration */
#define ACPI_PN7_CTRL_TO_FID(x)		((x) & 0x1f)
#define ACPI_PN7_CTRL_TO_VID(x)		(((x) >> 5) & 0x1f)
#define ACPI_PN7_CTRL_TO_SGTC(x)	(((x) >> 10) & 0xffff)

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

/* Reserved1 to powernow k8 configuration */
#define PN8_PSB_TO_RVO(x)		((x) & 0x03)
#define PN8_PSB_TO_IRT(x)		(((x) >> 2) & 0x03)
#define PN8_PSB_TO_MVS(x)		(((x) >> 4) & 0x03)
#define PN8_PSB_TO_BATT(x)		(((x) >> 6) & 0x03)

/* ACPI ctr_val status register to powernow k8 configuration */
#define ACPI_PN8_CTRL_TO_FID(x)		((x) & 0x3f)
#define ACPI_PN8_CTRL_TO_VID(x)		(((x) >> 6) & 0x1f)
#define ACPI_PN8_CTRL_TO_VST(x)		(((x) >> 11) & 0x1f)
#define ACPI_PN8_CTRL_TO_MVS(x)		(((x) >> 18) & 0x03)
#define ACPI_PN8_CTRL_TO_PLL(x)		(((x) >> 20) & 0x7f)
#define ACPI_PN8_CTRL_TO_RVO(x)		(((x) >> 28) & 0x03)
#define ACPI_PN8_CTRL_TO_IRT(x)		(((x) >> 30) & 0x03)


#define WRITE_FIDVID(fid, vid, ctrl)	\
	wrmsr(MSR_AMDK7_FIDVID_CTL,	\
	    (((ctrl) << 32) | (1ULL << 16) | ((vid) << 8) | (fid)))

#define COUNT_OFF_IRT(irt)	DELAY(10 * (1 << (irt)))
#define COUNT_OFF_VST(vst)	DELAY(20 * (vst))

#define FID_TO_VCO_FID(fid)	\
	(((fid) < 8) ? (8 + ((fid) << 1)) : (fid))

/*
 * Divide each value by 10 to get the processor multiplier.
 * Some of those tables are the same as the Linux powernow-k7
 * implementation by Dave Jones.
 */
static int pn7_fid_to_mult[32] = {
	110, 115, 120, 125, 50, 55, 60, 65,
	70, 75, 80, 85, 90, 95, 100, 105,
	30, 190, 40, 200, 130, 135, 140, 210,
	150, 225, 160, 165, 170, 180, 0, 0,
};


static int pn8_fid_to_mult[64] = {
	40, 45, 50, 55, 60, 65, 70, 75,
	80, 85, 90, 95, 100, 105, 110, 115,
	120, 125, 130, 135, 140, 145, 150, 155,
	160, 165, 170, 175, 180, 185, 190, 195,
	200, 205, 210, 215, 220, 225, 230, 235,
	240, 245, 250, 255, 260, 265, 270, 275,
	280, 285, 290, 295, 300, 305, 310, 315,
	320, 325, 330, 335, 340, 345, 350, 355,
};

/*
 * Units are in mV.
 */
/* Mobile VRM (K7) */
static int pn7_mobile_vid_to_volts[] = {
	2000, 1950, 1900, 1850, 1800, 1750, 1700, 1650,
	1600, 1550, 1500, 1450, 1400, 1350, 1300, 0,
	1275, 1250, 1225, 1200, 1175, 1150, 1125, 1100,
	1075, 1050, 1025, 1000, 975, 950, 925, 0,
};
/* Desktop VRM (K7) */
static int pn7_desktop_vid_to_volts[] = {
	2000, 1950, 1900, 1850, 1800, 1750, 1700, 1650,
	1600, 1550, 1500, 1450, 1400, 1350, 1300, 0,
	1275, 1250, 1225, 1200, 1175, 1150, 1125, 1100,
	1075, 1050, 1025, 1000, 975, 950, 925, 0,
};
/* Desktop and Mobile VRM (K8) */
static int pn8_vid_to_volts[] = {
	1550, 1525, 1500, 1475, 1450, 1425, 1400, 1375,
	1350, 1325, 1300, 1275, 1250, 1225, 1200, 1175,
	1150, 1125, 1100, 1075, 1050, 1025, 1000, 975,
	950, 925, 900, 875, 850, 825, 800, 0,
};

#define POWERNOW_MAX_STATES		16

struct powernow_state {
	int freq;
	int power;
	int fid;
	int vid;
};

struct pn_softc {
	device_t		 dev;
	int			 pn_type;
	struct powernow_state	 powernow_states[POWERNOW_MAX_STATES];
	u_int			 fsb;
	u_int			 sgtc;
	u_int			 vst;
	u_int			 mvs;
	u_int			 pll;
	u_int			 rvo;
	u_int			 irt;
	int			 low;
	int			 powernow_max_states;
	u_int			 powernow_state;
	u_int			 errata;
	int			*vid_to_volts;
};

/*
 * Offsets in struct cf_setting array for private values given by
 * acpi_perf driver.
 */
#define PX_SPEC_CONTROL		0
#define PX_SPEC_STATUS		1

static void	pn_identify(driver_t *driver, device_t parent);
static int	pn_probe(device_t dev);
static int	pn_attach(device_t dev);
static int	pn_detach(device_t dev);
static int	pn_set(device_t dev, const struct cf_setting *cf);
static int	pn_get(device_t dev, struct cf_setting *cf);
static int	pn_settings(device_t dev, struct cf_setting *sets,
		    int *count);
static int	pn_type(device_t dev, int *type);

static device_method_t pn_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify, pn_identify),
	DEVMETHOD(device_probe, pn_probe),
	DEVMETHOD(device_attach, pn_attach),
	DEVMETHOD(device_detach, pn_detach),

	/* cpufreq interface */
	DEVMETHOD(cpufreq_drv_set, pn_set),
	DEVMETHOD(cpufreq_drv_get, pn_get),
	DEVMETHOD(cpufreq_drv_settings, pn_settings),
	DEVMETHOD(cpufreq_drv_type, pn_type),

	{0, 0}
};

static devclass_t pn_devclass;
static driver_t pn_driver = {
	"powernow",
	pn_methods,
	sizeof(struct pn_softc),
};

DRIVER_MODULE(powernow, cpu, pn_driver, pn_devclass, 0, 0);

static int
pn7_setfidvid(struct pn_softc *sc, int fid, int vid)
{
	int cfid, cvid;
	uint64_t status, ctl;

	status = rdmsr(MSR_AMDK7_FIDVID_STATUS);
	cfid = PN7_STA_CFID(status);
	cvid = PN7_STA_CVID(status);

	/* We're already at the requested level. */
	if (fid == cfid && vid == cvid)
		return (0);

	ctl = rdmsr(MSR_AMDK7_FIDVID_CTL) & PN7_CTR_FIDCHRATIO;

	ctl |= PN7_CTR_FID(fid);
	ctl |= PN7_CTR_VID(vid);
	ctl |= PN7_CTR_SGTC(sc->sgtc);

	if (sc->errata & A0_ERRATA)
		disable_intr();

	if (pn7_fid_to_mult[fid] < pn7_fid_to_mult[cfid]) {
		wrmsr(MSR_AMDK7_FIDVID_CTL, ctl | PN7_CTR_FIDC);
		if (vid != cvid)
			wrmsr(MSR_AMDK7_FIDVID_CTL, ctl | PN7_CTR_VIDC);
	} else {
		wrmsr(MSR_AMDK7_FIDVID_CTL, ctl | PN7_CTR_VIDC);
		if (fid != cfid)
			wrmsr(MSR_AMDK7_FIDVID_CTL, ctl | PN7_CTR_FIDC);
	}

	if (sc->errata & A0_ERRATA)
		enable_intr();

	return (0);
}

static int
pn8_read_pending_wait(uint64_t *status)
{
	int i = 10000;

	do
		*status = rdmsr(MSR_AMDK7_FIDVID_STATUS);
	while (PN8_STA_PENDING(*status) && --i);

	return (i == 0 ? ENXIO : 0);
}

static int
pn8_write_fidvid(u_int fid, u_int vid, uint64_t ctrl, uint64_t *status)
{
	int i = 100;

	do
		WRITE_FIDVID(fid, vid, ctrl);
	while (pn8_read_pending_wait(status) && --i);

	return (i == 0 ? ENXIO : 0);
}

static int
pn8_setfidvid(struct pn_softc *sc, int fid, int vid)
{
	uint64_t status;
	int cfid, cvid;
	int rvo;
	int rv;
	u_int val;

	rv = pn8_read_pending_wait(&status);
	if (rv)
		return (rv);

	cfid = PN8_STA_CFID(status);
	cvid = PN8_STA_CVID(status);

	if (fid == cfid && vid == cvid)
		return (0);

	/*
	 * Phase 1: Raise core voltage to requested VID if frequency is
	 * going up.
	 */
	while (cvid > vid) {
		val = cvid - (1 << sc->mvs);
		rv = pn8_write_fidvid(cfid, (val > 0) ? val : 0, 1ULL, &status);
		if (rv) {
			sc->errata |= PENDING_STUCK;
			return (rv);
		}
		cvid = PN8_STA_CVID(status);
		COUNT_OFF_VST(sc->vst);
	}

	/* ... then raise to voltage + RVO (if required) */
	for (rvo = sc->rvo; rvo > 0 && cvid > 0; --rvo) {
		/* XXX It's not clear from spec if we have to do that
		 * in 0.25 step or in MVS.  Therefore do it as it's done
		 * under Linux */
		rv = pn8_write_fidvid(cfid, cvid - 1, 1ULL, &status);
		if (rv) {
			sc->errata |= PENDING_STUCK;
			return (rv);
		}
		cvid = PN8_STA_CVID(status);
		COUNT_OFF_VST(sc->vst);
	}

	/* Phase 2: change to requested core frequency */
	if (cfid != fid) {
		u_int vco_fid, vco_cfid, fid_delta;

		vco_fid = FID_TO_VCO_FID(fid);
		vco_cfid = FID_TO_VCO_FID(cfid);

		while (abs(vco_fid - vco_cfid) > 2) {
			fid_delta = (vco_cfid & 1) ? 1 : 2;
			if (fid > cfid) {
				if (cfid > 7)
					val = cfid + fid_delta;
				else
					val = FID_TO_VCO_FID(cfid) + fid_delta;
			} else
				val = cfid - fid_delta;
			rv = pn8_write_fidvid(val, cvid,
			    sc->pll * (uint64_t) sc->fsb,
			    &status);
			if (rv) {
				sc->errata |= PENDING_STUCK;
				return (rv);
			}
			cfid = PN8_STA_CFID(status);
			COUNT_OFF_IRT(sc->irt);

			vco_cfid = FID_TO_VCO_FID(cfid);
		}

		rv = pn8_write_fidvid(fid, cvid,
		    sc->pll * (uint64_t) sc->fsb,
		    &status);
		if (rv) {
			sc->errata |= PENDING_STUCK;
			return (rv);
		}
		cfid = PN8_STA_CFID(status);
		COUNT_OFF_IRT(sc->irt);
	}

	/* Phase 3: change to requested voltage */
	if (cvid != vid) {
		rv = pn8_write_fidvid(cfid, vid, 1ULL, &status);
		cvid = PN8_STA_CVID(status);
		COUNT_OFF_VST(sc->vst);
	}

	/* Check if transition failed. */
	if (cfid != fid || cvid != vid)
		rv = ENXIO;

	return (rv);
}

static int
pn_set(device_t dev, const struct cf_setting *cf)
{
	struct pn_softc *sc;
	int fid, vid;
	int i;
	int rv;

	if (cf == NULL)
		return (EINVAL);
	sc = device_get_softc(dev);

	if (sc->errata & PENDING_STUCK)
		return (ENXIO);

	for (i = 0; i < sc->powernow_max_states; ++i)
		if (CPUFREQ_CMP(sc->powernow_states[i].freq / 1000, cf->freq))
			break;

	fid = sc->powernow_states[i].fid;
	vid = sc->powernow_states[i].vid;

	rv = ENODEV;

	switch (sc->pn_type) {
	case PN7_TYPE:
		rv = pn7_setfidvid(sc, fid, vid);
		break;
	case PN8_TYPE:
		rv = pn8_setfidvid(sc, fid, vid);
		break;
	}

	return (rv);
}

static int
pn_get(device_t dev, struct cf_setting *cf)
{
	struct pn_softc *sc;
	u_int cfid = 0, cvid = 0;
	int i;
	uint64_t status;

	if (cf == NULL)
		return (EINVAL);
	sc = device_get_softc(dev);
	if (sc->errata & PENDING_STUCK)
		return (ENXIO);

	status = rdmsr(MSR_AMDK7_FIDVID_STATUS);

	switch (sc->pn_type) {
	case PN7_TYPE:
		cfid = PN7_STA_CFID(status);
		cvid = PN7_STA_CVID(status);
		break;
	case PN8_TYPE:
		cfid = PN8_STA_CFID(status);
		cvid = PN8_STA_CVID(status);
		break;
	}
	for (i = 0; i < sc->powernow_max_states; ++i)
		if (cfid == sc->powernow_states[i].fid &&
		    cvid == sc->powernow_states[i].vid)
			break;

	if (i < sc->powernow_max_states) {
		cf->freq = sc->powernow_states[i].freq / 1000;
		cf->power = sc->powernow_states[i].power;
		cf->lat = 200;
		cf->volts = sc->vid_to_volts[cvid];
		cf->dev = dev;
	} else {
		memset(cf, CPUFREQ_VAL_UNKNOWN, sizeof(*cf));
		cf->dev = NULL;
	}

	return (0);
}

static int
pn_settings(device_t dev, struct cf_setting *sets, int *count)
{
	struct pn_softc *sc;
	int i;

	if (sets == NULL|| count == NULL)
		return (EINVAL);
	sc = device_get_softc(dev);
	if (*count < sc->powernow_max_states)
		return (E2BIG);
	for (i = 0; i < sc->powernow_max_states; ++i) {
		sets[i].freq = sc->powernow_states[i].freq / 1000;
		sets[i].power = sc->powernow_states[i].power;
		sets[i].lat = 200;
		sets[i].volts = sc->vid_to_volts[sc->powernow_states[i].vid];
		sets[i].dev = dev;
	}
	*count = sc->powernow_max_states;

	return (0);
}

static int
pn_type(device_t dev, int *type)
{
	if (type == NULL)
		return (EINVAL);

	*type = CPUFREQ_TYPE_ABSOLUTE;

	return (0);
}

/*
 * Given a set of pair of fid/vid, and number of performance states,
 * compute powernow_states via an insertion sort.
 */
static int
decode_pst(struct pn_softc *sc, uint8_t *p, int npstates)
{
	int i, j, n;
	struct powernow_state state;

	for (i = 0; i < POWERNOW_MAX_STATES; ++i)
		sc->powernow_states[i].freq = CPUFREQ_VAL_UNKNOWN;

	for (n = 0, i = 0; i < npstates; ++i) {
		state.fid = *p++;
		state.vid = *p++;
		state.power = CPUFREQ_VAL_UNKNOWN;

		switch (sc->pn_type) {
		case PN7_TYPE:
			state.freq = 100 * pn7_fid_to_mult[state.fid] * sc->fsb;
			if ((sc->errata & A0_ERRATA) &&
			    (pn7_fid_to_mult[state.fid] % 10) == 5)
				continue;
			break;
		case PN8_TYPE:
			state.freq = 100 * pn8_fid_to_mult[state.fid] * sc->fsb;
			break;
		}

		j = n;
		while (j > 0 && sc->powernow_states[j - 1].freq < state.freq) {
			memcpy(&sc->powernow_states[j],
			    &sc->powernow_states[j - 1],
			    sizeof(struct powernow_state));
			--j;
		}
		memcpy(&sc->powernow_states[j], &state,
		    sizeof(struct powernow_state));
		++n;
	}

	/*
	 * Fix powernow_max_states, if errata a0 give us less states
	 * than expected.
	 */
	sc->powernow_max_states = n;

	if (bootverbose)
		for (i = 0; i < sc->powernow_max_states; ++i) {
			int fid = sc->powernow_states[i].fid;
			int vid = sc->powernow_states[i].vid;

			printf("powernow: %2i %8dkHz FID %02x VID %02x\n",
			    i,
			    sc->powernow_states[i].freq,
			    fid,
			    vid);
		}

	return (0);
}

static int
cpuid_is_k7(u_int cpuid)
{

	switch (cpuid) {
	case 0x760:
	case 0x761:
	case 0x762:
	case 0x770:
	case 0x771:
	case 0x780:
	case 0x781:
	case 0x7a0:
		return (TRUE);
	}
	return (FALSE);
}

static int
pn_decode_pst(device_t dev)
{
	int maxpst;
	struct pn_softc *sc;
	u_int cpuid, maxfid, startvid;
	u_long sig;
	struct psb_header *psb;
	uint8_t *p;
	u_int regs[4];
	uint64_t status;

	sc = device_get_softc(dev);

	do_cpuid(0x80000001, regs);
	cpuid = regs[0];

	if ((cpuid & 0xfff) == 0x760)
		sc->errata |= A0_ERRATA;

	status = rdmsr(MSR_AMDK7_FIDVID_STATUS);

	switch (sc->pn_type) {
	case PN7_TYPE:
		maxfid = PN7_STA_MFID(status);
		startvid = PN7_STA_SVID(status);
		break;
	case PN8_TYPE:
		maxfid = PN8_STA_MFID(status);
		/*
		 * we should actually use a variable named 'maxvid' if K8,
		 * but why introducing a new variable for that?
		 */
		startvid = PN8_STA_MVID(status);
		break;
	default:
		return (ENODEV);
	}

	if (bootverbose) {
		device_printf(dev, "STATUS: 0x%jx\n", status);
		device_printf(dev, "STATUS: maxfid: 0x%02x\n", maxfid);
		device_printf(dev, "STATUS: %s: 0x%02x\n",
		    sc->pn_type == PN7_TYPE ? "startvid" : "maxvid",
		    startvid);
	}

	sig = bios_sigsearch(PSB_START, PSB_SIG, PSB_LEN, PSB_STEP, PSB_OFF);
	if (sig) {
		struct pst_header *pst;

		psb = (struct psb_header*)(uintptr_t)BIOS_PADDRTOVADDR(sig);

		switch (psb->version) {
		default:
			return (ENODEV);
		case 0x14:
			/*
			 * We can't be picky about numpst since at least
			 * some systems have a value of 1 and some have 2.
			 * We trust that cpuid_is_k7() will be better at
			 * catching that we're on a K8 anyway.
			 */
			if (sc->pn_type != PN8_TYPE)
				return (EINVAL);
			sc->vst = psb->settlingtime;
			sc->rvo = PN8_PSB_TO_RVO(psb->res1);
			sc->irt = PN8_PSB_TO_IRT(psb->res1);
			sc->mvs = PN8_PSB_TO_MVS(psb->res1);
			sc->low = PN8_PSB_TO_BATT(psb->res1);
			if (bootverbose) {
				device_printf(dev, "PSB: VST: %d\n",
				    psb->settlingtime);
				device_printf(dev, "PSB: RVO %x IRT %d "
				    "MVS %d BATT %d\n",
				    sc->rvo,
				    sc->irt,
				    sc->mvs,
				    sc->low);
			}
			break;
		case 0x12:
			if (sc->pn_type != PN7_TYPE)
				return (EINVAL);
			sc->sgtc = psb->settlingtime * sc->fsb;
			if (sc->sgtc < 100 * sc->fsb)
				sc->sgtc = 100 * sc->fsb;
			break;
		}

		p = ((uint8_t *) psb) + sizeof(struct psb_header);
		pst = (struct pst_header*) p;

		maxpst = 200;

		do {
			struct pst_header *pst = (struct pst_header*) p;

			if (cpuid == pst->cpuid &&
			    maxfid == pst->maxfid &&
			    startvid == pst->startvid) {
				sc->powernow_max_states = pst->numpstates;
				switch (sc->pn_type) {
				case PN7_TYPE:
					if (abs(sc->fsb - pst->fsb) > 5)
						continue;
					break;
				case PN8_TYPE:
					break;
				}
				return (decode_pst(sc,
				    p + sizeof(struct pst_header),
				    sc->powernow_max_states));
			}

			p += sizeof(struct pst_header) + (2 * pst->numpstates);
		} while (cpuid_is_k7(pst->cpuid) && maxpst--);

		device_printf(dev, "no match for extended cpuid %.3x\n", cpuid);
	}

	return (ENODEV);
}

static int
pn_decode_acpi(device_t dev, device_t perf_dev)
{
	int i, j, n;
	uint64_t status;
	uint32_t ctrl;
	u_int cpuid;
	u_int regs[4];
	struct pn_softc *sc;
	struct powernow_state state;
	struct cf_setting sets[POWERNOW_MAX_STATES];
	int count = POWERNOW_MAX_STATES;
	int type;
	int rv;

	if (perf_dev == NULL)
		return (ENXIO);

	rv = CPUFREQ_DRV_SETTINGS(perf_dev, sets, &count);
	if (rv)
		return (ENXIO);
	rv = CPUFREQ_DRV_TYPE(perf_dev, &type);
	if (rv || (type & CPUFREQ_FLAG_INFO_ONLY) == 0)
		return (ENXIO);

	sc = device_get_softc(dev);

	do_cpuid(0x80000001, regs);
	cpuid = regs[0];
	if ((cpuid & 0xfff) == 0x760)
		sc->errata |= A0_ERRATA;

	ctrl = 0;
	sc->sgtc = 0;
	for (n = 0, i = 0; i < count; ++i) {
		ctrl = sets[i].spec[PX_SPEC_CONTROL];
		switch (sc->pn_type) {
		case PN7_TYPE:
			state.fid = ACPI_PN7_CTRL_TO_FID(ctrl);
			state.vid = ACPI_PN7_CTRL_TO_VID(ctrl);
			if ((sc->errata & A0_ERRATA) &&
			    (pn7_fid_to_mult[state.fid] % 10) == 5)
				continue;
			break;
		case PN8_TYPE:
			state.fid = ACPI_PN8_CTRL_TO_FID(ctrl);
			state.vid = ACPI_PN8_CTRL_TO_VID(ctrl);
			break;
		}
		state.freq = sets[i].freq * 1000;
		state.power = sets[i].power;

		j = n;
		while (j > 0 && sc->powernow_states[j - 1].freq < state.freq) {
			memcpy(&sc->powernow_states[j],
			    &sc->powernow_states[j - 1],
			    sizeof(struct powernow_state));
			--j;
		}
		memcpy(&sc->powernow_states[j], &state,
		    sizeof(struct powernow_state));
		++n;
	}

	sc->powernow_max_states = n;
	state = sc->powernow_states[0];
	status = rdmsr(MSR_AMDK7_FIDVID_STATUS);

	switch (sc->pn_type) {
	case PN7_TYPE:
		sc->sgtc = ACPI_PN7_CTRL_TO_SGTC(ctrl);
		/*
		 * XXX Some bios forget the max frequency!
		 * This maybe indicates we have the wrong tables.  Therefore,
		 * don't implement a quirk, but fallback to BIOS legacy
		 * tables instead.
		 */
		if (PN7_STA_MFID(status) != state.fid) {
			device_printf(dev, "ACPI MAX frequency not found\n");
			return (EINVAL);
		}
		sc->fsb = state.freq / 100 / pn7_fid_to_mult[state.fid];
		break;
	case PN8_TYPE:
		sc->vst = ACPI_PN8_CTRL_TO_VST(ctrl),
		sc->mvs = ACPI_PN8_CTRL_TO_MVS(ctrl),
		sc->pll = ACPI_PN8_CTRL_TO_PLL(ctrl),
		sc->rvo = ACPI_PN8_CTRL_TO_RVO(ctrl),
		sc->irt = ACPI_PN8_CTRL_TO_IRT(ctrl);
		sc->low = 0; /* XXX */

		/*
		 * powernow k8 supports only one low frequency.
		 */
		if (sc->powernow_max_states >= 2 &&
		    (sc->powernow_states[sc->powernow_max_states - 2].fid < 8))
			return (EINVAL);
		sc->fsb = state.freq / 100 / pn8_fid_to_mult[state.fid];
		break;
	}

	return (0);
}

static void
pn_identify(driver_t *driver, device_t parent)
{

	if ((amd_pminfo & AMDPM_FID) == 0 || (amd_pminfo & AMDPM_VID) == 0)
		return;
	switch (cpu_id & 0xf00) {
	case 0x600:
	case 0xf00:
		break;
	default:
		return;
	}
	if (device_find_child(parent, "powernow", -1) != NULL)
		return;
	if (BUS_ADD_CHILD(parent, 10, "powernow", -1) == NULL)
		device_printf(parent, "powernow: add child failed\n");
}

static int
pn_probe(device_t dev)
{
	struct pn_softc *sc;
	uint64_t status;
	uint64_t rate;
	struct pcpu *pc;
	u_int sfid, mfid, cfid;

	sc = device_get_softc(dev);
	sc->errata = 0;
	status = rdmsr(MSR_AMDK7_FIDVID_STATUS);

	pc = cpu_get_pcpu(dev);
	if (pc == NULL)
		return (ENODEV);

	cpu_est_clockrate(pc->pc_cpuid, &rate);

	switch (cpu_id & 0xf00) {
	case 0x600:
		sfid = PN7_STA_SFID(status);
		mfid = PN7_STA_MFID(status);
		cfid = PN7_STA_CFID(status);
		sc->pn_type = PN7_TYPE;
		sc->fsb = rate / 100000 / pn7_fid_to_mult[cfid];

		/*
		 * If start FID is different to max FID, then it is a
		 * mobile processor.  If not, it is a low powered desktop
		 * processor.
		 */
		if (PN7_STA_SFID(status) != PN7_STA_MFID(status)) {
			sc->vid_to_volts = pn7_mobile_vid_to_volts;
			device_set_desc(dev, "PowerNow! K7");
		} else {
			sc->vid_to_volts = pn7_desktop_vid_to_volts;
			device_set_desc(dev, "Cool`n'Quiet K7");
		}
		break;

	case 0xf00:
		sfid = PN8_STA_SFID(status);
		mfid = PN8_STA_MFID(status);
		cfid = PN8_STA_CFID(status);
		sc->pn_type = PN8_TYPE;
		sc->vid_to_volts = pn8_vid_to_volts;
		sc->fsb = rate / 100000 / pn8_fid_to_mult[cfid];

		if (PN8_STA_SFID(status) != PN8_STA_MFID(status))
			device_set_desc(dev, "PowerNow! K8");
		else
			device_set_desc(dev, "Cool`n'Quiet K8");
		break;
	default:
		return (ENODEV);
	}

	return (0);
}

static int
pn_attach(device_t dev)
{
	int rv;
	device_t child;

	child = device_find_child(device_get_parent(dev), "acpi_perf", -1);
	if (child) {
		rv = pn_decode_acpi(dev, child);
		if (rv)
			rv = pn_decode_pst(dev);
	} else
		rv = pn_decode_pst(dev);

	if (rv != 0)
		return (ENXIO);
	cpufreq_register(dev);
	return (0);
}

static int
pn_detach(device_t dev)
{

	return (cpufreq_unregister(dev));
}
