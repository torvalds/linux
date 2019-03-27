/*-
 * Copyright (c) 2002-2009 Sam Leffler, Errno Consulting
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 *
 * $FreeBSD$
 */
#include "diag.h"

#include "ah.h"
#include "ah_internal.h"
#include "ah_decode.h"

#include "dumpregs.h"

#include <stdlib.h>
#include <string.h>
#include <err.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/mman.h>

typedef struct {
	HAL_REVS revs;
	int chipnum;
#define	MAXREGS	5*1024
	struct dumpreg *regs[MAXREGS];
	u_int nregs;
} dumpregs_t;
static	dumpregs_t state;

static void opdevice(const struct athregrec *r);
static const char* opmark(FILE *, int, const struct athregrec *);
static void oprw(FILE *fd, int recnum, struct athregrec *r);

int
main(int argc, char *argv[])
{
	int fd, i, nrecs, same;
	struct stat sb;
	void *addr;
	const char *filename = "/tmp/ath_hal.log";
	struct athregrec *rprev;

	if (argc > 1)
		filename = argv[1];
	fd = open(filename, O_RDONLY);
	if (fd < 0)
		err(1, "open: %s", filename);
	if (fstat(fd, &sb) < 0)
		err(1, "fstat");
	addr = mmap(0, sb.st_size, PROT_READ, MAP_PRIVATE|MAP_NOCORE, fd, 0);
	if (addr == MAP_FAILED)
		err(1, "mmap");
	nrecs = sb.st_size / sizeof (struct athregrec);
	printf("%u records", nrecs);
	rprev = NULL;
	same = 0;
	state.chipnum = 5210;
	for (i = 0; i < nrecs; i++) {
		struct athregrec *r = &((struct athregrec *) addr)[i];
		if (rprev && bcmp(r, rprev, sizeof (*r)) == 0) {
			same++;
			continue;
		}
		if (same)
			printf("\t\t+%u time%s", same, same == 1 ? "" : "s");
		switch (r->op) {
		case OP_DEVICE:
			opdevice(r);
			break;
		case OP_READ:
		case OP_WRITE:
			oprw(stdout, i, r);
			break;
		case OP_MARK:
			opmark(stdout, i, r);
			break;
		}
		rprev = r;
		same = 0;
	}
	putchar('\n');
	return 0;
}

static const char*
opmark(FILE *fd, int i, const struct athregrec *r)
{
	fprintf(fd, "\n%05d: ", i);
	switch (r->reg) {
	case AH_MARK_RESET:
		fprintf(fd, "ar%uReset %s", state.chipnum,
			r->val ? "change channel" : "no channel change");
		break;
	case AH_MARK_RESET_LINE:
		fprintf(fd, "ar%u_reset.c; line %u", state.chipnum, r->val);
		break;
	case AH_MARK_RESET_DONE:
		if (r->val)
			fprintf(fd, "ar%uReset (done), FAIL, error %u",
				state.chipnum, r->val);
		else
			fprintf(fd, "ar%uReset (done), OK", state.chipnum);
		break;
	case AH_MARK_CHIPRESET:
		fprintf(fd, "ar%uChipReset, channel %u MHz", state.chipnum, r->val);
		break;
	case AH_MARK_PERCAL:
		fprintf(fd, "ar%uPerCalibration, channel %u MHz", state.chipnum, r->val);
		break;
	case AH_MARK_SETCHANNEL:
		fprintf(fd, "ar%uSetChannel, channel %u MHz", state.chipnum, r->val);
		break;
	case AH_MARK_ANI_RESET:
		switch (r->val) {
		case HAL_M_STA:
			fprintf(fd, "ar%uAniReset, HAL_M_STA", state.chipnum);
			break;
		case HAL_M_IBSS:
			fprintf(fd, "ar%uAniReset, HAL_M_IBSS", state.chipnum);
			break;
		case HAL_M_HOSTAP:
			fprintf(fd, "ar%uAniReset, HAL_M_HOSTAP", state.chipnum);
			break;
		case HAL_M_MONITOR:
			fprintf(fd, "ar%uAniReset, HAL_M_MONITOR", state.chipnum);
			break;
		default:
			fprintf(fd, "ar%uAniReset, opmode %u", state.chipnum, r->val);
			break;
		}
		break;
	case AH_MARK_ANI_POLL:
		fprintf(fd, "ar%uAniPoll, listenTime %u", state.chipnum, r->val);
		break;
	case AH_MARK_ANI_CONTROL:
		switch (r->val) {
		case HAL_ANI_PRESENT:
			fprintf(fd, "ar%uAniControl, PRESENT", state.chipnum);
			break;
		case HAL_ANI_NOISE_IMMUNITY_LEVEL:
			fprintf(fd, "ar%uAniControl, NOISE_IMMUNITY", state.chipnum);
			break;
		case HAL_ANI_OFDM_WEAK_SIGNAL_DETECTION:
			fprintf(fd, "ar%uAniControl, OFDM_WEAK_SIGNAL", state.chipnum);
			break;
		case HAL_ANI_CCK_WEAK_SIGNAL_THR:
			fprintf(fd, "ar%uAniControl, CCK_WEAK_SIGNAL", state.chipnum);
			break;
		case HAL_ANI_FIRSTEP_LEVEL:
			fprintf(fd, "ar%uAniControl, FIRSTEP_LEVEL", state.chipnum);
			break;
		case HAL_ANI_SPUR_IMMUNITY_LEVEL:
			fprintf(fd, "ar%uAniControl, SPUR_IMMUNITY", state.chipnum);
			break;
		case HAL_ANI_MODE:
			fprintf(fd, "ar%uAniControl, MODE", state.chipnum);
			break;
		case HAL_ANI_PHYERR_RESET:
			fprintf(fd, "ar%uAniControl, PHYERR_RESET", state.chipnum);
			break;
		default:
			fprintf(fd, "ar%uAniControl, cmd %u", state.chipnum, r->val);
			break;
		}
		break;
	default:
		fprintf(fd, "mark #%u value %u/0x%x", r->reg, r->val, r->val);
		break;
	}
	return (NULL);
}

#include "ah_devid.h"

static void
opdevice(const struct athregrec *r)
{
	switch (r->val) {
	case AR5210_PROD:
	case AR5210_DEFAULT:
		state.chipnum = 5210;
		state.revs.ah_macVersion = 1;
		state.revs.ah_macRev = 0;
		break;
	case AR5211_DEVID:
	case AR5311_DEVID:
	case AR5211_DEFAULT:
	case AR5211_FPGA11B:
		state.chipnum = 5211;
		state.revs.ah_macVersion = 2;
		state.revs.ah_macRev = 0;
		break;
	/* AR5212 */
	case AR5212_DEFAULT:
	case AR5212_DEVID:
	case AR5212_FPGA:
	case AR5212_DEVID_IBM:
	case AR5212_AR5312_REV2:
	case AR5212_AR5312_REV7:
	case AR5212_AR2313_REV8:
	case AR5212_AR2315_REV6:
	case AR5212_AR2315_REV7:
	case AR5212_AR2317_REV1:
	case AR5212_AR2317_REV2:

	/* AR5212 compatible devid's also attach to 5212 */
	case AR5212_DEVID_0014:
	case AR5212_DEVID_0015:
	case AR5212_DEVID_0016:
	case AR5212_DEVID_0017:
	case AR5212_DEVID_0018:
	case AR5212_DEVID_0019:
	case AR5212_AR2413:
	case AR5212_AR5413:
	case AR5212_AR5424:
	case AR5212_AR2417:
	case AR5212_DEVID_FF19:
		state.chipnum = 5212;
		state.revs.ah_macVersion = 4;
		state.revs.ah_macRev = 5;
		break;

	/* AR5213 */
	case AR5213_SREV_1_0:
	case AR5213_SREV_REG:
		state.chipnum = 5213;
		state.revs.ah_macVersion = 5;
		state.revs.ah_macRev = 9;
		break;

	/* AR5416 compatible devid's  */
	case AR5416_DEVID_PCI:
	case AR5416_DEVID_PCIE:
	case AR9160_DEVID_PCI:
	case AR9280_DEVID_PCI:
	case AR9280_DEVID_PCIE:
	case AR9285_DEVID_PCIE:
	case AR9287_DEVID_PCI:
	case AR9287_DEVID_PCIE:
	case AR9300_DEVID_AR9330:
		state.chipnum = 5416;
		state.revs.ah_macVersion = 13;
		state.revs.ah_macRev = 8;
		break;
	default:
		printf("Unknown device id 0x%x\n", r->val);
		exit(-1);
	}
}

static int
regcompar(const void *a, const void *b)
{
	const struct dumpreg *ra = *(const struct dumpreg **)a;
	const struct dumpreg *rb = *(const struct dumpreg **)b;
	return ra->addr - rb->addr;
}

void
register_regs(struct dumpreg *chipregs, u_int nchipregs,
	int def_srev_min, int def_srev_max, int def_phy_min, int def_phy_max)
{
	const int existing_regs = state.nregs;
	int i, j;

	for (i = 0; i < nchipregs; i++) {
		struct dumpreg *nr = &chipregs[i];
		if (nr->srevMin == 0)
			nr->srevMin = def_srev_min;
		if (nr->srevMax == 0)
			nr->srevMax = def_srev_max;
		if (nr->phyMin == 0)
			nr->phyMin = def_phy_min;
		if (nr->phyMax == 0)
			nr->phyMax = def_phy_max;
		for (j = 0; j < existing_regs; j++) {
			struct dumpreg *r = state.regs[j];
			/*
			 * Check if we can just expand the mac+phy
			 * coverage for the existing entry.
			 */
			if (nr->addr == r->addr &&
			    (nr->name == r->name ||
			     (nr->name != NULL && r->name != NULL &&
			     strcmp(nr->name, r->name) == 0))) {
				if (nr->srevMin < r->srevMin &&
				    (r->srevMin <= nr->srevMax &&
				     nr->srevMax+1 <= r->srevMax)) {
					r->srevMin = nr->srevMin;
					goto skip;
				}
				if (nr->srevMax > r->srevMax &&
				    (r->srevMin <= nr->srevMin &&
				     nr->srevMin <= r->srevMax)) {
					r->srevMax = nr->srevMax;
					goto skip;
				}
			}
			if (r->addr > nr->addr)
				break;
		}
		/*
		 * New item, add to the end, it'll be sorted below.
		 */
		if (state.nregs == MAXREGS)
			errx(-1, "too many registers; bump MAXREGS");
		state.regs[state.nregs++] = nr;
	skip:
		;
	}
	qsort(state.regs, state.nregs, sizeof(struct dumpreg *), regcompar);
}

void
register_keycache(u_int nslots,
	int def_srev_min, int def_srev_max, int def_phy_min, int def_phy_max)
{
	/* discard, no use */
}

void
register_range(u_int brange, u_int erange, int type,
	int def_srev_min, int def_srev_max, int def_phy_min, int def_phy_max)
{
	/* discard, no use */
}

static const struct dumpreg *
findreg(int reg)
{
	const HAL_REVS *revs = &state.revs;
	int i;

	for (i = 0; i < state.nregs; i++) {
		const struct dumpreg *dr = state.regs[i];
		if (dr->addr == reg &&
		    MAC_MATCH(dr, revs->ah_macVersion, revs->ah_macRev))
			return dr;
	}
	return NULL;
}

/* XXX cheat, 5212 has a superset of the key table defs */
#include "ar5212/ar5212reg.h"
#include "ar5212/ar5212phy.h"

#define PWR_TABLE_SIZE	64

static void
oprw(FILE *fd, int recnum, struct athregrec *r)
{
	const struct dumpreg *dr;
	char buf[64];
	const char* bits;
	int i;

	fprintf(fd, "\n%05d: [%d] ", recnum, r->threadid);
	dr = findreg(r->reg);
	if (dr != NULL && dr->name != NULL) {
		snprintf(buf, sizeof (buf), "AR_%s (0x%x)", dr->name, r->reg);
		bits = dr->bits;
	} else if (AR_KEYTABLE(0) <= r->reg && r->reg < AR_KEYTABLE(128)) {
		snprintf(buf, sizeof (buf), "AR_KEYTABLE%u(%u) (0x%x)",
			((r->reg - AR_KEYTABLE_0) >> 2) & 7,
			(r->reg - AR_KEYTABLE_0) >> 5, r->reg);
		bits = NULL;
#if 0
	} else if (AR_PHY_PCDAC_TX_POWER(0) <= r->reg && r->reg < AR_PHY_PCDAC_TX_POWER(PWR_TABLE_SIZE/2)) {
		snprintf(buf, sizeof (buf), "AR_PHY_PCDAC_TX_POWER(%u) (0x%x)",
			(r->reg - AR_PHY_PCDAC_TX_POWER_0) >> 2, r->reg);
		bits = NULL;
#endif
	} else if (AR_RATE_DURATION(0) <= r->reg && r->reg < AR_RATE_DURATION(32)) {
		snprintf(buf, sizeof (buf), "AR_RATE_DURATION(0x%x) (0x%x)",
			(r->reg - AR_RATE_DURATION_0) >> 2, r->reg);
		bits = NULL;
	} else if (AR_PHY_BASE <= r->reg) {
		snprintf(buf, sizeof (buf), "AR_PHY(%u) (0x%x)",
			(r->reg - AR_PHY_BASE) >> 2, r->reg);
		bits = NULL;
	} else {
		snprintf(buf, sizeof (buf), "0x%x", r->reg);
		bits = NULL;
	}
	fprintf(fd, "%-30s %s 0x%x", buf, r->op ? "<=" : "=>", r->val);
	if (bits) {
		const char *p = bits;
		int tmp, n;

		for (tmp = 0, p++; *p;) {
			n = *p++;
			if (r->val & (1 << (n - 1))) {
				putc(tmp ? ',' : '<', fd);
				for (; (n = *p) > ' '; ++p)
					putc(n, fd);
				tmp = 1;
			} else
				for (; *p > ' '; ++p)
					continue;
		}
		if (tmp)
			putc('>', fd);
	}
}
