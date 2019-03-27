/*-
 * Copyright (c) 2009 Sam Leffler, Errno Consulting
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

#include "dumpregs.h"

#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>

typedef struct {
	HAL_REVS revs;
#define	MAXREGS	5*1024
	struct dumpreg *regs[MAXREGS];
	u_int nregs;
} dumpregs_t;
static	dumpregs_t state;

static uint32_t regread(int s, struct ath_diag *atd, uint32_t r);
static void regwrite(int s, struct ath_diag *atd, uint32_t r, uint32_t v);
static const struct dumpreg *reglookup(const char *v);

static void
usage(void)
{
	fprintf(stderr, "usage: athpoke [-i interface] [reg[=value]] ...\n");
	exit(-1);
}

int
main(int argc, char *argv[])
{
	struct ath_diag atd;
	const char *ifname;
	char *eptr;
	int c, s;

	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0)
		err(1, "socket");
	ifname = getenv("ATH");
	if (!ifname)
		ifname = ATH_DEFAULT;

	while ((c = getopt(argc, argv, "i:")) != -1)
		switch (c) {
		case 'i':
			ifname = optarg;
			break;
		default:
			usage();
			/*NOTREACHED*/
		}
	strncpy(atd.ad_name, ifname, sizeof (atd.ad_name));

	atd.ad_id = HAL_DIAG_REVS;
	atd.ad_out_data = (caddr_t) &state.revs;
	atd.ad_out_size = sizeof(state.revs);
	if (ioctl(s, SIOCGATHDIAG, &atd) < 0)
		err(1, atd.ad_name);

	argc -= optind;
	argv += optind;

	for (; argc > 0; argc--, argv++) {
		char *cp;
		const struct dumpreg *dr;
		uint32_t reg;

		cp = strchr(argv[0], '=');
		if (cp != NULL)
			*cp++ = '\0';
		dr = reglookup(argv[0]);
		if (dr == NULL) {
			errno = 0;
			reg = (uint32_t) strtoul(argv[0], &eptr, 0);
			if (argv[0] == eptr || eptr[0] != '\0')
				errx(1, "invalid register \"%s\"", argv[0]);
		} else
			reg = dr->addr;
		if (cp != NULL)
			regwrite(s, &atd, reg, (uint32_t) strtoul(cp, NULL, 0));
		printf("%s = %08x\n", argv[0], regread(s, &atd, reg));
	}
	return 0;
}

static uint32_t
regread(int s, struct ath_diag *atd, uint32_t r)
{
	HAL_REGRANGE ra;
	uint32_t v[3];

	ra.start = r;
	ra.end = 0;

	atd->ad_in_data = (caddr_t) &ra;
	atd->ad_in_size = sizeof(ra);
	atd->ad_out_data = (caddr_t) v;
	atd->ad_out_size = sizeof(v);
	atd->ad_id = HAL_DIAG_REGS | ATH_DIAG_IN | ATH_DIAG_DYN;
	if (ioctl(s, SIOCGATHDIAG, atd) < 0)
		err(1, atd->ad_name);
	return v[2];
}

static void
regwrite(int s, struct ath_diag *atd, uint32_t r, uint32_t v)
{
	HAL_REGWRITE rw;

	rw.addr = r;
	rw.value = v;
	atd->ad_in_data = (caddr_t) &rw;
	atd->ad_in_size = sizeof(rw);
	atd->ad_id = HAL_DIAG_SETREGS | ATH_DIAG_IN;
	if (ioctl(s, SIOCGATHDIAG, atd) < 0)
		err(1, atd->ad_name);
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
	/* discard, no use */
}

void
register_range(u_int brange, u_int erange, int type,
	int def_srev_min, int def_srev_max, int def_phy_min, int def_phy_max)
{
	/* discard, no use */
}

static const struct dumpreg *
reglookup(const char *v)
{
	const HAL_REVS *revs = &state.revs;
	int i;

	if (strncasecmp(v, "AR_", 3) == 0)
		v += 3;
	for (i = 0; i < state.nregs; i++) {
		const struct dumpreg *dr = state.regs[i];
		if (MAC_MATCH(dr, revs->ah_macVersion, revs->ah_macRev) &&
		    strcasecmp(v, dr->name) == 0)
			return dr;
	}
	return NULL;
}
