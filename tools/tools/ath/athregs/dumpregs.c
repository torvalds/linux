/*-
 * Copyright (c) 2002-2008 Sam Leffler, Errno Consulting
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
/* XXX cheat, 5212 has a superset of the key table defs */
#include "ar5212/ar5212reg.h"

#include "dumpregs.h"

#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <err.h>

#include "ctrl.h"

typedef struct {
	HAL_REVS revs;
	u_int32_t regdata[0xffff / sizeof(u_int32_t)];
#define	MAXREGS	5*1024
	struct dumpreg *regs[MAXREGS];
	u_int nregs;
	u_int	show_names	: 1,
		show_addrs	: 1;
} dumpregs_t;
static	dumpregs_t state;

#undef OS_REG_READ
#define	OS_REG_READ(ah, off)	state.regdata[(off) >> 2]

static	int ath_hal_anyregs(int what);
static	int ath_hal_setupregs(struct ath_diag *atd, int what);
static	u_int ath_hal_setupdiagregs(const HAL_REGRANGE regs[], u_int nr);
static	void ath_hal_dumpregs(FILE *fd, int what);
static	void ath_hal_dumprange(FILE *fd, u_int a, u_int b);
static	void ath_hal_dumpkeycache(FILE *fd, int nkeys);
static	void ath_hal_dumpint(FILE *fd, int what);
static	void ath_hal_dumpqcu(FILE *fd, int what);
static	void ath_hal_dumpdcu(FILE *fd, int what);
static	void ath_hal_dumpbb(FILE *fd, int what);

static void
usage(void)
{
	fprintf(stderr, "usage: athregs [-i interface] [-abdkmqxz]\n");
	fprintf(stderr, "-a	display all registers\n");
	fprintf(stderr, "-b	display baseband registers\n");
	fprintf(stderr, "-d	display DCU registers\n");
	fprintf(stderr, "-k	display key cache registers\n");
	fprintf(stderr, "-m	display \"MAC\" registers (default)\n");
	fprintf(stderr, "-q	display QCU registers\n");
	fprintf(stderr, "-x	display XR registers\n");
	fprintf(stderr, "-z	display interrupt registers\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "-A	display register address\n");
	fprintf(stderr, "-N	suppress display of register name\n");
	exit(-1);
}

int
main(int argc, char *argv[])
{
	struct ath_diag atd;
	const char *ifname;
	u_int32_t *data;
	u_int32_t *dp, *ep;
	int what, c, i;
	struct ath_driver_req req;

	ath_driver_req_init(&req);

	ifname = getenv("ATH");
	if (!ifname)
		ifname = ATH_DEFAULT;

	what = 0;
	state.show_addrs = 0;
	state.show_names = 1;
	while ((c = getopt(argc, argv, "i:aAbdkmNqxz")) != -1)
		switch (c) {
		case 'a':
			what |= DUMP_ALL;
			break;
		case 'A':
			state.show_addrs = 1;
			break;
		case 'b':
			what |= DUMP_BASEBAND;
			break;
		case 'd':
			what |= DUMP_DCU;
			break;
		case 'k':
			what |= DUMP_KEYCACHE;
			break;
		case 'i':
			ifname = optarg;
			break;
		case 'm':
			what |= DUMP_BASIC;
			break;
		case 'N':
			state.show_names = 0;
			break;
		case 'q':
			what |= DUMP_QCU;
			break;
		case 'x':
			what |= DUMP_XR;
			break;
		case 'z':
			what |= DUMP_INTERRUPT;
			break;
		default:
			usage();
			/*NOTREACHED*/
		}

	/* Initialise the driver interface */
	if (ath_driver_req_open(&req, ifname) < 0) {
		exit(127);
	}

	/*
	 * Whilst we're doing the ath_diag pieces, we have to set this
	 * ourselves.
	 */
	strncpy(atd.ad_name, ifname, sizeof (atd.ad_name));

	argc -= optind;
	argv += optind;
	if (what == 0)
		what = DUMP_BASIC;

	atd.ad_id = HAL_DIAG_REVS;
	atd.ad_out_data = (caddr_t) &state.revs;
	atd.ad_out_size = sizeof(state.revs);

	if (ath_driver_req_fetch_diag(&req, SIOCGATHDIAG, &atd) < 0)
		err(1, "%s", atd.ad_name);

	if (ath_hal_setupregs(&atd, what) == 0)
		errx(-1, "no registers are known for this part "
		    "(devid 0x%x mac %d.%d phy %d)", state.revs.ah_devid,
		    state.revs.ah_macVersion, state.revs.ah_macRev,
		    state.revs.ah_phyRev);

	atd.ad_out_size = ath_hal_setupdiagregs((HAL_REGRANGE *) atd.ad_in_data,
		atd.ad_in_size / sizeof(HAL_REGRANGE));
	atd.ad_out_data = (caddr_t) malloc(atd.ad_out_size);
	if (atd.ad_out_data == NULL) {
		fprintf(stderr, "Cannot malloc output buffer, size %u\n",
			atd.ad_out_size);
		exit(-1);
	}
	atd.ad_id = HAL_DIAG_REGS | ATH_DIAG_IN | ATH_DIAG_DYN;

	if (ath_driver_req_fetch_diag(&req, SIOCGATHDIAG, &atd) < 0)
		err(1, "%s", atd.ad_name);

	/*
	 * Expand register data into global space that can be
	 * indexed directly by register offset.
	 */
	dp = (u_int32_t *)atd.ad_out_data;
	ep = (u_int32_t *)(atd.ad_out_data + atd.ad_out_size);
	while (dp < ep) {
		u_int r = dp[0];	/* start of range */
		u_int e = dp[1];	/* end of range */
		dp++;
		dp++;
		/* convert offsets to indices */
		r >>= 2; e >>= 2;
		do {
			if (dp >= ep) {
				fprintf(stderr, "Warning, botched return data;"
					"register at offset 0x%x not present\n",
					r << 2);
				break;
			}
			state.regdata[r++] = *dp++;
		} while (r <= e);
	} 

	if (what & DUMP_BASIC)
		ath_hal_dumpregs(stdout, DUMP_BASIC);
	if ((what & DUMP_INTERRUPT) && ath_hal_anyregs(DUMP_INTERRUPT)) {
		if (what & DUMP_BASIC)
			putchar('\n');
		if (state.show_addrs)
			ath_hal_dumpregs(stdout, DUMP_INTERRUPT);
		else
			ath_hal_dumpint(stdout, what);
	}
	if ((what & DUMP_QCU) && ath_hal_anyregs(DUMP_QCU)) {
		if (what & (DUMP_BASIC|DUMP_INTERRUPT))
			putchar('\n');
		if (state.show_addrs)
			ath_hal_dumpregs(stdout, DUMP_QCU);
		else
			ath_hal_dumpqcu(stdout, what);
	}
	if ((what & DUMP_DCU) && ath_hal_anyregs(DUMP_DCU)) {
		if (what & (DUMP_BASIC|DUMP_INTERRUPT|DUMP_QCU))
			putchar('\n');
		if (state.show_addrs)
			ath_hal_dumpregs(stdout, DUMP_DCU);
		else
			ath_hal_dumpdcu(stdout, what);
	}
	if (what & DUMP_KEYCACHE) {
		if (state.show_addrs) {
			if (what & (DUMP_BASIC|DUMP_INTERRUPT|DUMP_QCU|DUMP_DCU))
				putchar('\n');
			ath_hal_dumpregs(stdout, DUMP_KEYCACHE);
		} else
			ath_hal_dumpkeycache(stdout, 128);
	}
	if (what & DUMP_BASEBAND) {
		if (what &~ DUMP_BASEBAND)
			fprintf(stdout, "\n");
		ath_hal_dumpbb(stdout, what);
	}
	ath_driver_req_close(&req);
	return 0;
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
			     nr->name != NULL && r->name != NULL &&
			     strcmp(nr->name, r->name) == 0)) {
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
#define	SET(r, a) do { \
	r->addr = a; r->type = DUMP_KEYCACHE; r++; \
} while(0)
	struct dumpreg *keyregs, *r;
	int i;

	keyregs = (struct dumpreg *) calloc(nslots, 8*sizeof(struct dumpreg));
	if (keyregs == NULL)
		errx(-1, "no space to %d keycache slots\n", nslots);
	r = keyregs;
	for (i = 0; i < nslots; i++) {
		SET(r, AR_KEYTABLE_KEY0(i));
		SET(r, AR_KEYTABLE_KEY1(i));
		SET(r, AR_KEYTABLE_KEY2(i));
		SET(r, AR_KEYTABLE_KEY3(i));
		SET(r, AR_KEYTABLE_KEY4(i));
		SET(r, AR_KEYTABLE_TYPE(i));
		SET(r, AR_KEYTABLE_MAC0(i));
		SET(r, AR_KEYTABLE_MAC1(i));
	}
	register_regs(keyregs, 8*nslots,
	    def_srev_min, def_srev_max, def_phy_min, def_phy_max);
#undef SET
}

void
register_range(u_int brange, u_int erange, int type,
	int def_srev_min, int def_srev_max, int def_phy_min, int def_phy_max)
{
	struct dumpreg *bbregs, *r;
	int i, nregs;

	nregs = (erange - brange) / sizeof(uint32_t);
	bbregs = (struct dumpreg *) calloc(nregs, sizeof(struct dumpreg));
	if (bbregs == NULL)
		errx(-1, "no space for %d register slots (type %d)\n",
		    nregs, type);
	r = bbregs;
	for (i = 0; i < nregs; i++) {
		r->addr = brange + (i<<2);
		r->type = type;
		r++;
	}
	register_regs(bbregs, nregs,
	    def_srev_min, def_srev_max, def_phy_min, def_phy_max);
}

static __inline int
match(const struct dumpreg *dr, const HAL_REVS *revs)
{
	if (!MAC_MATCH(dr, revs->ah_macVersion, revs->ah_macRev))
		return 0;
	if ((dr->type & DUMP_BASEBAND) && !PHY_MATCH(dr, revs->ah_phyRev))
		return 0;
	return 1;
}

static int
ath_hal_anyregs(int what)
{
	const HAL_REVS *revs = &state.revs;
	int i;

	for (i = 0; i < state.nregs; i++) {
		const struct dumpreg *dr = state.regs[i];
		if ((what & dr->type) && match(dr, revs))
			return 1;
	}
	return 0;
}

static int
ath_hal_setupregs(struct ath_diag *atd, int what)
{
	const HAL_REVS *revs = &state.revs;
	HAL_REGRANGE r;
	size_t space = 0;
	u_int8_t *cp;
	int i, brun, erun;

	brun = erun = -1;
	for (i = 0; i < state.nregs; i++) {
		const struct dumpreg *dr = state.regs[i];
		if ((what & dr->type) && match(dr, revs)) {
			if (erun + 4 != dr->addr) {
				if (brun != -1)
					space += sizeof(HAL_REGRANGE);
				brun = erun = dr->addr;
			} else
				erun = dr->addr;
		}
	}
	space += sizeof(HAL_REGRANGE);

	atd->ad_in_data = (caddr_t) malloc(space);
	if (atd->ad_in_data == NULL) {
		fprintf(stderr, "Cannot malloc memory for registers!\n");
		exit(-1);
	}
	atd->ad_in_size = space;
	cp = (u_int8_t *) atd->ad_in_data;
	brun = erun = -1;
	for (i = 0; i < state.nregs; i++) {
		const struct dumpreg *dr = state.regs[i];
		if ((what & dr->type) && match(dr, revs)) {
			if (erun + 4 != dr->addr) {
				if (brun != -1) {
					r.start = brun, r.end = erun;
					memcpy(cp, &r, sizeof(r));
					cp += sizeof(r);
				}
				brun = erun = dr->addr;
			} else
				erun = dr->addr;
		}
	}
	if (brun != -1) {
		r.start = brun, r.end = erun;
		memcpy(cp, &r, sizeof(r));
		cp += sizeof(r);
	}
	return space / sizeof(uint32_t);
}

static void
ath_hal_dumpregs(FILE *fd, int what)
{
	const HAL_REVS *revs = &state.revs;
	const char *sep = "";
	int i, count, itemsperline;

	count = 0;
	itemsperline = 4;
	if (state.show_names && state.show_addrs)
		itemsperline--;
	for (i = 0; i < state.nregs; i++) {
		const struct dumpreg *dr = state.regs[i];
		if ((what & dr->type) && match(dr, revs)) {
			if (state.show_names && dr->name != NULL) {
				fprintf(fd, "%s%-8s", sep, dr->name);
				if (state.show_addrs)
					fprintf(fd, " [%04x]", dr->addr);
			} else
				fprintf(fd, "%s%04x", sep, dr->addr);
			fprintf(fd, " %08x", OS_REG_READ(ah, dr->addr));
			sep = " ";
			if ((++count % itemsperline) == 0)
				sep = "\n";
		}
	}
	if (count)
		fprintf(fd, "\n");
}

static void
ath_hal_dumprange(FILE *fd, u_int a, u_int b)
{
	u_int r;

	for (r = a; r+16 <= b; r += 5*4)
		fprintf(fd,
			"%04x %08x  %04x %08x  %04x %08x  %04x %08x  %04x %08x\n"
			, r, OS_REG_READ(ah, r)
			, r+4, OS_REG_READ(ah, r+4)
			, r+8, OS_REG_READ(ah, r+8)
			, r+12, OS_REG_READ(ah, r+12)
			, r+16, OS_REG_READ(ah, r+16)
		);
	switch (b-r) {
	case 16:
		fprintf(fd
			, "%04x %08x  %04x %08x  %04x %08x  %04x %08x\n"
			, r, OS_REG_READ(ah, r)
			, r+4, OS_REG_READ(ah, r+4)
			, r+8, OS_REG_READ(ah, r+8)
			, r+12, OS_REG_READ(ah, r+12)
		);
		break;
	case 12:
		fprintf(fd, "%04x %08x  %04x %08x  %04x %08x\n"
			, r, OS_REG_READ(ah, r)
			, r+4, OS_REG_READ(ah, r+4)
			, r+8, OS_REG_READ(ah, r+8)
		);
		break;
	case 8:
		fprintf(fd, "%04x %08x  %04x %08x\n"
			, r, OS_REG_READ(ah, r)
			, r+4, OS_REG_READ(ah, r+4)
		);
		break;
	case 4:
		fprintf(fd, "%04x %08x\n"
			, r, OS_REG_READ(ah, r)
		);
		break;
	}
}

static void
ath_hal_dumpint(FILE *fd, int what)
{
	int i;

	/* Interrupt registers */
	fprintf(fd, "IMR: %08x S0 %08x S1 %08x S2 %08x S3 %08x S4 %08x\n"
		, OS_REG_READ(ah, AR_IMR)
		, OS_REG_READ(ah, AR_IMR_S0)
		, OS_REG_READ(ah, AR_IMR_S1)
		, OS_REG_READ(ah, AR_IMR_S2)
		, OS_REG_READ(ah, AR_IMR_S3)
		, OS_REG_READ(ah, AR_IMR_S4)
	);
	fprintf(fd, "ISR: %08x S0 %08x S1 %08x S2 %08x S3 %08x S4 %08x\n"
		, OS_REG_READ(ah, AR_ISR)
		, OS_REG_READ(ah, AR_ISR_S0)
		, OS_REG_READ(ah, AR_ISR_S1)
		, OS_REG_READ(ah, AR_ISR_S2)
		, OS_REG_READ(ah, AR_ISR_S3)
		, OS_REG_READ(ah, AR_ISR_S4)
	);
}

static void
ath_hal_dumpqcu(FILE *fd, int what)
{
	int i;

	/* QCU registers */
	fprintf(fd, "%-8s %08x  %-8s %08x  %-8s %08x\n"
		, "Q_TXE", OS_REG_READ(ah, AR_Q_TXE)
		, "Q_TXD", OS_REG_READ(ah, AR_Q_TXD)
		, "Q_RDYTIMSHD", OS_REG_READ(ah, AR_Q_RDYTIMESHDN)
	);
	fprintf(fd, "Q_ONESHOTARM_SC %08x  Q_ONESHOTARM_CC %08x\n"
		, OS_REG_READ(ah, AR_Q_ONESHOTARM_SC)
		, OS_REG_READ(ah, AR_Q_ONESHOTARM_CC)
	);
	for (i = 0; i < 10; i++)
		fprintf(fd, "Q[%u] TXDP %08x CBR %08x RDYT %08x MISC %08x STS %08x\n"
			, i
			, OS_REG_READ(ah, AR_QTXDP(i))
			, OS_REG_READ(ah, AR_QCBRCFG(i))
			, OS_REG_READ(ah, AR_QRDYTIMECFG(i))
			, OS_REG_READ(ah, AR_QMISC(i))
			, OS_REG_READ(ah, AR_QSTS(i))
		);
}

static void
ath_hal_dumpdcu(FILE *fd, int what)
{
	int i;

	/* DCU registers */
	for (i = 0; i < 10; i++)
		fprintf(fd, "D[%u] MASK %08x IFS %08x RTRY %08x CHNT %08x MISC %06x\n"
			, i
			, OS_REG_READ(ah, AR_DQCUMASK(i))
			, OS_REG_READ(ah, AR_DLCL_IFS(i))
			, OS_REG_READ(ah, AR_DRETRY_LIMIT(i))
			, OS_REG_READ(ah, AR_DCHNTIME(i))
			, OS_REG_READ(ah, AR_DMISC(i))
		);
}

static void
ath_hal_dumpbb(FILE *fd, int what)
{
	const HAL_REVS *revs = &state.revs;
	int i, brun, erun;

	brun = erun = 0;
	for (i = 0; i < state.nregs; i++) {
		const struct dumpreg *dr = state.regs[i];
		if (!match(dr, revs))
			continue;
		if (dr->type & DUMP_BASEBAND) {
			if (brun == 0) {
				brun = erun = dr->addr;
			} else if (dr->addr == erun + sizeof(uint32_t)) {
				erun = dr->addr;
			} else {
				ath_hal_dumprange(fd, brun, erun);
				brun = erun = dr->addr;
			}
		} else {
			if (brun != 0)
				ath_hal_dumprange(fd, brun, erun);
			brun = erun = 0;
		}
	}
	if (brun != 0)
		ath_hal_dumprange(fd, brun, erun);
}

static u_int
ath_hal_setupdiagregs(const HAL_REGRANGE regs[], u_int nr)
{
	u_int space;
	int i;

	space = 0;
	for (i = 0; i < nr; i++) {
		u_int n = sizeof(HAL_REGRANGE) + sizeof(u_int32_t);	/* reg range + first */
		if (regs[i].end) {
			if (regs[i].end < regs[i].start) {
				fprintf(stderr, "%s: bad register range, "
					"end 0x%x < start 0x%x\n",
					__func__, regs[i].end, regs[i].end);
				exit(-1);
			}
			n += regs[i].end - regs[i].start;
		}
		space += n;
	}
	return space;
}

/*
 * Format an Ethernet MAC for printing.
 */
static const char*
ether_sprintf(const u_int8_t *mac)
{
	static char etherbuf[18];
	snprintf(etherbuf, sizeof(etherbuf), "%02x:%02x:%02x:%02x:%02x:%02x",
		mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	return etherbuf;
}

#ifndef isclr
#define	setbit(a,i)	((a)[(i)/NBBY] |= 1<<((i)%NBBY))
#define	clrbit(a,i)	((a)[(i)/NBBY] &= ~(1<<((i)%NBBY)))
#define	isset(a,i)	((a)[(i)/NBBY] & (1<<((i)%NBBY)))
#define	isclr(a,i)	(((a)[(i)/NBBY] & (1<<((i)%NBBY))) == 0)
#endif

static void
ath_hal_dumpkeycache(FILE *fd, int nkeys)
{
	static const char *keytypenames[] = {
		"WEP-40", 	/* AR_KEYTABLE_TYPE_40 */
		"WEP-104",	/* AR_KEYTABLE_TYPE_104 */
		"#2",
		"WEP-128",	/* AR_KEYTABLE_TYPE_128 */
		"TKIP",		/* AR_KEYTABLE_TYPE_TKIP */
		"AES-OCB",	/* AR_KEYTABLE_TYPE_AES */
		"AES-CCM",	/* AR_KEYTABLE_TYPE_CCM */
		"CLR",		/* AR_KEYTABLE_TYPE_CLR */
	};
	int micEnabled = SREV(state.revs.ah_macVersion, state.revs.ah_macRev) < SREV(4,8) ? 0 :
	       OS_REG_READ(ah, AR_STA_ID1) & AR_STA_ID1_CRPT_MIC_ENABLE;
	u_int8_t mac[IEEE80211_ADDR_LEN];
	u_int8_t ismic[128/NBBY];
	int entry;
	int first = 1;

	memset(ismic, 0, sizeof(ismic));
	for (entry = 0; entry < nkeys; entry++) {
		u_int32_t macLo, macHi, type;
		u_int32_t key0, key1, key2, key3, key4;

		macHi = OS_REG_READ(ah, AR_KEYTABLE_MAC1(entry));
		if ((macHi & AR_KEYTABLE_VALID) == 0 && isclr(ismic, entry))
			continue;
		macLo = OS_REG_READ(ah, AR_KEYTABLE_MAC0(entry));
		macHi <<= 1;
		if (macLo & (1<<31))
			macHi |= 1;
		macLo <<= 1;
		mac[4] = macHi & 0xff;
		mac[5] = macHi >> 8;
		mac[0] = macLo & 0xff;
		mac[1] = macLo >> 8;
		mac[2] = macLo >> 16;
		mac[3] = macLo >> 24;
		type = OS_REG_READ(ah, AR_KEYTABLE_TYPE(entry));
		if ((type & 7) == AR_KEYTABLE_TYPE_TKIP && micEnabled)
			setbit(ismic, entry+64);
		key0 = OS_REG_READ(ah, AR_KEYTABLE_KEY0(entry));
		key1 = OS_REG_READ(ah, AR_KEYTABLE_KEY1(entry));
		key2 = OS_REG_READ(ah, AR_KEYTABLE_KEY2(entry));
		key3 = OS_REG_READ(ah, AR_KEYTABLE_KEY3(entry));
		key4 = OS_REG_READ(ah, AR_KEYTABLE_KEY4(entry));
		if (first) {
			fprintf(fd, "\n");
			first = 0;
		}
		fprintf(fd, "KEY[%03u] MAC %s %-7s %02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x\n"
			, entry
			, ether_sprintf(mac)
			, isset(ismic, entry) ? "MIC" : keytypenames[type & 7]
			, (key0 >>  0) & 0xff
			, (key0 >>  8) & 0xff
			, (key0 >> 16) & 0xff
			, (key0 >> 24) & 0xff
			, (key1 >>  0) & 0xff
			, (key1 >>  8) & 0xff
			, (key2 >>  0) & 0xff
			, (key2 >>  8) & 0xff
			, (key2 >> 16) & 0xff
			, (key2 >> 24) & 0xff
			, (key3 >>  0) & 0xff
			, (key3 >>  8) & 0xff
			, (key4 >>  0) & 0xff
			, (key4 >>  8) & 0xff
			, (key4 >> 16) & 0xff
			, (key4 >> 24) & 0xff
		);
	}
}
