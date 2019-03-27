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

#include <sys/param.h>

#include "opt_ah.h"

/*
 * ath statistics class.
 */
#include <sys/types.h>
#include <sys/file.h>
#include <sys/sockio.h>
#include <sys/socket.h>
#include <net/if.h>
#include <net/if_media.h>
#include <net/if_var.h>

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <err.h>

#include "ah.h"
#include "ah_desc.h"
#include "net80211/ieee80211_ioctl.h"
#include "net80211/ieee80211_radiotap.h"
#include "if_athioctl.h"

#include "athaggrstats.h"

#define	NOTPRESENT	{ 0, "", "" }

#define	AFTER(prev)	((prev)+1)

static const struct fmt athaggrstats[] = {

#define	S_SINGLE_PKT	0
	{ 4,	"singlepkt",	"spkt",	"single frames scheduled" },
#define	S_NONBAW_PKT			AFTER(S_SINGLE_PKT)
	{ 5,	"nonbawpkt",	"nbpkt",	"frames outside of the BAW" },
#define	S_AGGR_PKT			AFTER(S_NONBAW_PKT)
	{ 6,	"aggrpkt",	"aggpkt",	"aggregate frames scheduled" },
#define	S_BAW_CLOSED_SINGLE_PKT		AFTER(S_AGGR_PKT)
	{ 8,	"bawclosedpkt",	"bawclpkt",	"single frames due to closed BAW" },
#define	S_LOW_HWQ_SINGLE_PKT		AFTER(S_BAW_CLOSED_SINGLE_PKT)
	{ 6,	"lhsinglepkt",	"lhspkt",	"single frames scheduled due to low HWQ depth" },
#define	S_SCHED_NOPKT			AFTER(S_LOW_HWQ_SINGLE_PKT)
	{ 6,	"schednopkt",	"snopkt",	"sched called with no frames" },
#define	S_RTS_AGGR_LIMITED		AFTER(S_SCHED_NOPKT)
	{ 8,	"rtsaggrlimit",	"rtslimit",	"RTS limited aggregates" },
#define	S_PKT0				AFTER(S_RTS_AGGR_LIMITED)
	{ 2,	"p0",	"p0",	"" },
#define	S_PKT1				AFTER(S_PKT0)
	{ 2,	"p1",	"p1",	"" },
#define	S_PKT2				AFTER(S_PKT1)
	{ 2,	"p2",	"p2",	"" },
#define	S_PKT3				AFTER(S_PKT2)
	{ 2,	"p3",	"p3",	"" },
#define	S_PKT4				AFTER(S_PKT3)
	{ 2,	"p4",	"p4",	"" },
#define	S_PKT5				AFTER(S_PKT4)
	{ 2,	"p5",	"p5",	"" },
#define	S_PKT6				AFTER(S_PKT5)
	{ 2,	"p6",	"p6",	"" },
#define	S_PKT7				AFTER(S_PKT6)
	{ 2,	"p7",	"p7",	"" },
#define	S_PKT8				AFTER(S_PKT7)
	{ 2,	"p8",	"p8",	"" },
#define	S_PKT9				AFTER(S_PKT8)
	{ 2,	"p9",	"p9",	"" },
#define	S_PKT10				AFTER(S_PKT9)
	{ 3,	"p10",	"p10",	"" },
#define	S_PKT11				AFTER(S_PKT10)
	{ 3,	"p11",	"p11",	"" },
#define	S_PKT12				AFTER(S_PKT11)
	{ 3,	"p12",	"p12",	"" },
#define	S_PKT13				AFTER(S_PKT12)
	{ 3,	"p13",	"p13",	"" },
#define	S_PKT14				AFTER(S_PKT13)
	{ 3,	"p14",	"p14",	"" },
#define	S_PKT15				AFTER(S_PKT14)
	{ 3,	"p15",	"p15",	"" },
#define	S_PKT16				AFTER(S_PKT15)
	{ 3,	"p16",	"p16",	"" },
#define	S_PKT17				AFTER(S_PKT16)
	{ 3,	"p17",	"p17",	"" },
#define	S_PKT18				AFTER(S_PKT17)
	{ 3,	"p18",	"p18",	"" },
#define	S_PKT19				AFTER(S_PKT18)
	{ 3,	"p19",	"p19",	"" },
#define	S_PKT20				AFTER(S_PKT19)
	{ 3,	"p20",	"p20",	"" },
#define	S_PKT21				AFTER(S_PKT20)
	{ 3,	"p21",	"p21",	"" },
#define	S_PKT22				AFTER(S_PKT21)
	{ 3,	"p22",	"p22",	"" },
#define	S_PKT23				AFTER(S_PKT22)
	{ 3,	"p23",	"p23",	"" },
#define	S_PKT24				AFTER(S_PKT23)
	{ 3,	"p24",	"p24",	"" },
#define	S_PKT25				AFTER(S_PKT24)
	{ 3,	"p25",	"p25",	"" },
#define	S_PKT26				AFTER(S_PKT25)
	{ 3,	"p26",	"p26",	"" },
#define	S_PKT27				AFTER(S_PKT26)
	{ 3,	"p27",	"p27",	"" },
#define	S_PKT28				AFTER(S_PKT27)
	{ 3,	"p28",	"p28",	"" },
#define	S_PKT29				AFTER(S_PKT28)
	{ 3,	"p29",	"p29",	"" },
#define	S_PKT30				AFTER(S_PKT29)
	{ 3,	"p30",	"p30",	"" },
#define	S_PKT31				AFTER(S_PKT30)
	{ 3,	"p31",	"p31",	"" },
};

#define	S_LAST		S_RTS_AGGR_LIMITED
#define	S_MAX		(S_PKT31 + 1)

struct athaggrstatfoo_p {
	struct athaggrstatfoo base;
	int s;
	int optstats;
	struct ifreq ifr;
	struct ath_diag atd;
	struct ath_tx_aggr_stats cur;
	struct ath_tx_aggr_stats total;
};

static void
ath_setifname(struct athaggrstatfoo *wf0, const char *ifname)
{
	struct athaggrstatfoo_p *wf = (struct athaggrstatfoo_p *) wf0;

	strncpy(wf->ifr.ifr_name, ifname, sizeof (wf->ifr.ifr_name));
}

static void 
ath_zerostats(struct athaggrstatfoo *wf0)
{
#if 0
	struct athaggrstatfoo_p *wf = (struct athaggrstatfoo_p *) wf0;

	if (ioctl(wf->s, SIOCZATHSTATS, &wf->ifr) < 0)
		err(-1, wf->ifr.ifr_name);
#endif
}

static void
ath_collect(struct athaggrstatfoo_p *wf, struct ath_tx_aggr_stats *stats)
{
	wf->ifr.ifr_data = (caddr_t) stats;
	if (ioctl(wf->s, SIOCGATHAGSTATS, &wf->ifr) < 0)
		err(1, "%s: ioctl: %s", __func__, wf->ifr.ifr_name);
}

static void
ath_collect_cur(struct bsdstat *sf)
{
	struct athaggrstatfoo_p *wf = (struct athaggrstatfoo_p *) sf;

	ath_collect(wf, &wf->cur);
}

static void
ath_collect_tot(struct bsdstat *sf)
{
	struct athaggrstatfoo_p *wf = (struct athaggrstatfoo_p *) sf;

	ath_collect(wf, &wf->total);
}

static void
ath_update_tot(struct bsdstat *sf)
{
	struct athaggrstatfoo_p *wf = (struct athaggrstatfoo_p *) sf;

	wf->total = wf->cur;
}

static void
snprintrate(char b[], size_t bs, int rate)
{
	if (rate & IEEE80211_RATE_MCS)
		snprintf(b, bs, "MCS%u", rate &~ IEEE80211_RATE_MCS);
	else if (rate & 1)
		snprintf(b, bs, "%u.5M", rate / 2);
	else
		snprintf(b, bs, "%uM", rate / 2);
}

static int
ath_get_curstat(struct bsdstat *sf, int s, char b[], size_t bs)
{
	struct athaggrstatfoo_p *wf = (struct athaggrstatfoo_p *) sf;
#define	STAT(x) \
	snprintf(b, bs, "%u", wf->cur.aggr_##x - wf->total.aggr_##x); return 1
#define	PKT(x) \
	snprintf(b, bs, "%u", wf->cur.aggr_pkts[x] - wf->total.aggr_pkts[x]); return 1

	switch (s) {
	case S_SINGLE_PKT:		STAT(single_pkt);
	case S_NONBAW_PKT:		STAT(nonbaw_pkt);
	case S_AGGR_PKT:		STAT(aggr_pkt);
	case S_BAW_CLOSED_SINGLE_PKT:	STAT(baw_closed_single_pkt);
	case S_LOW_HWQ_SINGLE_PKT:	STAT(low_hwq_single_pkt);
	case S_SCHED_NOPKT:		STAT(sched_nopkt);
	case S_RTS_AGGR_LIMITED:	STAT(rts_aggr_limited);
	case S_PKT0:			PKT(0);
	case S_PKT1:			PKT(1);
	case S_PKT2:			PKT(2);
	case S_PKT3:			PKT(3);
	case S_PKT4:			PKT(4);
	case S_PKT5:			PKT(5);
	case S_PKT6:			PKT(6);
	case S_PKT7:			PKT(7);
	case S_PKT8:			PKT(8);
	case S_PKT9:			PKT(9);
	case S_PKT10:			PKT(10);
	case S_PKT11:			PKT(11);
	case S_PKT12:			PKT(12);
	case S_PKT13:			PKT(13);
	case S_PKT14:			PKT(14);
	case S_PKT15:			PKT(15);
	case S_PKT16:			PKT(16);
	case S_PKT17:			PKT(17);
	case S_PKT18:			PKT(18);
	case S_PKT19:			PKT(19);
	case S_PKT20:			PKT(20);
	case S_PKT21:			PKT(21);
	case S_PKT22:			PKT(22);
	case S_PKT23:			PKT(23);
	case S_PKT24:			PKT(24);
	case S_PKT25:			PKT(25);
	case S_PKT26:			PKT(26);
	case S_PKT27:			PKT(27);
	case S_PKT28:			PKT(28);
	case S_PKT29:			PKT(29);
	case S_PKT30:			PKT(30);
	case S_PKT31:			PKT(31);
	}
	b[0] = '\0';
	return 0;
#undef PKT
#undef STAT
}

static int
ath_get_totstat(struct bsdstat *sf, int s, char b[], size_t bs)
{
	struct athaggrstatfoo_p *wf = (struct athaggrstatfoo_p *) sf;
#define	STAT(x) \
	snprintf(b, bs, "%u", wf->total.aggr_##x); return 1
#define	PKT(x) \
	snprintf(b, bs, "%u", wf->total.aggr_pkts[x]); return 1

	switch (s) {
	case S_SINGLE_PKT:		STAT(single_pkt);
	case S_NONBAW_PKT:		STAT(nonbaw_pkt);
	case S_AGGR_PKT:		STAT(aggr_pkt);
	case S_BAW_CLOSED_SINGLE_PKT:	STAT(baw_closed_single_pkt);
	case S_LOW_HWQ_SINGLE_PKT:	STAT(low_hwq_single_pkt);
	case S_SCHED_NOPKT:		STAT(sched_nopkt);
	case S_RTS_AGGR_LIMITED:	STAT(rts_aggr_limited);
	case S_PKT0:			PKT(0);
	case S_PKT1:			PKT(1);
	case S_PKT2:			PKT(2);
	case S_PKT3:			PKT(3);
	case S_PKT4:			PKT(4);
	case S_PKT5:			PKT(5);
	case S_PKT6:			PKT(6);
	case S_PKT7:			PKT(7);
	case S_PKT8:			PKT(8);
	case S_PKT9:			PKT(9);
	case S_PKT10:			PKT(10);
	case S_PKT11:			PKT(11);
	case S_PKT12:			PKT(12);
	case S_PKT13:			PKT(13);
	case S_PKT14:			PKT(14);
	case S_PKT15:			PKT(15);
	case S_PKT16:			PKT(16);
	case S_PKT17:			PKT(17);
	case S_PKT18:			PKT(18);
	case S_PKT19:			PKT(19);
	case S_PKT20:			PKT(20);
	case S_PKT21:			PKT(21);
	case S_PKT22:			PKT(22);
	case S_PKT23:			PKT(23);
	case S_PKT24:			PKT(24);
	case S_PKT25:			PKT(25);
	case S_PKT26:			PKT(26);
	case S_PKT27:			PKT(27);
	case S_PKT28:			PKT(28);
	case S_PKT29:			PKT(29);
	case S_PKT30:			PKT(30);
	case S_PKT31:			PKT(31);
	}
	b[0] = '\0';
	return 0;
#undef PKT
#undef STAT
}

static void
ath_print_verbose(struct bsdstat *sf, FILE *fd)
{
	struct athaggrstatfoo_p *wf = (struct athaggrstatfoo_p *) sf;
	const struct fmt *f;
	char s[32];
	const char *indent;
	int i, width;

	width = 0;
	for (i = 0; i < S_LAST; i++) {
		f = &sf->stats[i];
		if (f->width > width)
			width = f->width;
	}
	for (i = 0; i < S_LAST; i++) {
		if (ath_get_totstat(sf, i, s, sizeof(s)) && strcmp(s, "0")) {
			indent = "";
			fprintf(fd, "%s%-*s %s\n", indent, width, s,
			    athaggrstats[i].desc);
		}
	}

	fprintf(fd, "\nAggregate size profile:\n\n");
	for (i = 0; i < 64; i++) {
		fprintf(fd, "%2d: %12u%s",
		    i,
		    wf->total.aggr_pkts[i],
		    (i % 4 == 3) ? "\n" : " ");
	}
	fprintf(fd, "\n");
}

BSDSTAT_DEFINE_BOUNCE(athaggrstatfoo)

struct athaggrstatfoo *
athaggrstats_new(const char *ifname, const char *fmtstring)
{
	struct athaggrstatfoo_p *wf;

	wf = calloc(1, sizeof(struct athaggrstatfoo_p));
	if (wf != NULL) {
		bsdstat_init(&wf->base.base, "athaggrstats",
		    athaggrstats, nitems(athaggrstats));
		/* override base methods */
		wf->base.base.collect_cur = ath_collect_cur;
		wf->base.base.collect_tot = ath_collect_tot;
		wf->base.base.get_curstat = ath_get_curstat;
		wf->base.base.get_totstat = ath_get_totstat;
		wf->base.base.update_tot = ath_update_tot;
		wf->base.base.print_verbose = ath_print_verbose;

		/* setup bounce functions for public methods */
		BSDSTAT_BOUNCE(wf, athaggrstatfoo);

		/* setup our public methods */
		wf->base.setifname = ath_setifname;
#if 0
		wf->base.setstamac = wlan_setstamac;
#endif
		wf->base.zerostats = ath_zerostats;
		wf->s = socket(AF_INET, SOCK_DGRAM, 0);
		if (wf->s < 0)
			err(1, "socket");

		ath_setifname(&wf->base, ifname);
		wf->base.setfmt(&wf->base, fmtstring);
	}
	return &wf->base;
}
