/*-
 * Copyright (c) 2012, Adrian Chadd.
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

#include "opt_ah.h"

#include <sys/types.h>
#include <sys/file.h>
#include <sys/sockio.h>
#include <sys/socket.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_media.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <err.h>

#include <curses.h>

#include "ah.h"
#include "ah_desc.h"
#include "net80211/ieee80211_ioctl.h"
#include "net80211/ieee80211_radiotap.h"
#include "if_athioctl.h"
#include "if_athrate.h"

#include "ath_rate/sample/sample.h"

static int do_loop = 0;

/*
 * This needs to be big enough to fit the two TLVs, the rate table
 * and the rate statistics table for a single node.
 */
#define	STATS_BUF_SIZE	8192

#define	PRINTMSG(...) do {			\
	if (do_loop == 0)			\
		printf(__VA_ARGS__);		\
	else					\
		printw(__VA_ARGS__);		\
	} while (0)

#define	PRINTATTR_ON(_x) do {			\
	if (do_loop)				\
		attron(_x);			\
	} while(0)


#define	PRINTATTR_OFF(_x) do {			\
	if (do_loop)				\
		attroff(_x);			\
	} while(0)

struct ath_ratestats {
	int s;
	struct ath_rateioctl re;
};

static inline int
dot11rate(struct ath_rateioctl_rt *rt, int rix)
{

	if (rt->ratecode[rix] & IEEE80211_RATE_MCS)
		return rt->ratecode[rix] & ~(IEEE80211_RATE_MCS);
	else
		return (rt->ratecode[rix] / 2);
}

static const char *
dot11str(struct ath_rateioctl_rt *rt, int rix)
{
	if (rix == -1)
		return "";
	else if (rt->ratecode[rix] & IEEE80211_RATE_MCS)
		return "MCS";
	else
		return " Mb";
}

static void
ath_sample_stats(struct ath_ratestats *r, struct ath_rateioctl_rt *rt,
    struct sample_node *sn)
{
	uint64_t mask;
	int rix, y;

	PRINTMSG("static_rix (%d) ratemask 0x%llx\n",
	    sn->static_rix,
	    (long long) sn->ratemask);

	for (y = 0; y < NUM_PACKET_SIZE_BINS; y++) {
		PRINTATTR_ON(COLOR_PAIR(y+4) | A_BOLD);
		PRINTMSG("[%4u] cur rate %d %s since switch: "
		    "packets %d ticks %u\n",
		    bin_to_size(y),
		    dot11rate(rt, sn->current_rix[y]),
		    dot11str(rt, sn->current_rix[y]),
		    sn->packets_since_switch[y],
		    sn->ticks_since_switch[y]);

		PRINTMSG("[%4u] last sample (%d %s) cur sample (%d %s) "
		    "packets sent %d\n",
		    bin_to_size(y),
		    dot11rate(rt, sn->last_sample_rix[y]),
		    dot11str(rt, sn->last_sample_rix[y]),
		    dot11rate(rt, sn->current_sample_rix[y]),
		    dot11str(rt, sn->current_sample_rix[y]),
		    sn->packets_sent[y]);
		PRINTATTR_OFF(COLOR_PAIR(y+4) | A_BOLD);
		
		PRINTATTR_ON(COLOR_PAIR(3) | A_BOLD);
		PRINTMSG("[%4u] packets since sample %d sample tt %u\n",
		    bin_to_size(y),
		    sn->packets_since_sample[y],
		    sn->sample_tt[y]);
		PRINTATTR_OFF(COLOR_PAIR(3) | A_BOLD);
		PRINTMSG("\n");
	}
	PRINTMSG("   TX Rate     TXTOTAL:TXOK       EWMA          T/   F"
	    "     avg last xmit\n");
	for (mask = sn->ratemask, rix = 0; mask != 0; mask >>= 1, rix++) {
		if ((mask & 1) == 0)
				continue;
		for (y = 0; y < NUM_PACKET_SIZE_BINS; y++) {
			if (sn->stats[y][rix].total_packets == 0)
				continue;
			if (rix == sn->current_rix[y])
				PRINTATTR_ON(COLOR_PAIR(y+4) | A_BOLD);
			else if (rix == sn->last_sample_rix[y])
				PRINTATTR_ON(COLOR_PAIR(3) | A_BOLD);
#if 0
			else if (sn->stats[y][rix].ewma_pct / 10 < 50)
				PRINTATTR_ON(COLOR_PAIR(2) | A_BOLD);
			else if (sn->stats[y][rix].ewma_pct / 10 < 75)
				PRINTATTR_ON(COLOR_PAIR(1) | A_BOLD);
#endif
			PRINTMSG("[%2u %s:%4u] %8ju:%-8ju "
			    "(%3d.%1d%%) %8ju/%4d %5uuS %u\n",
			    dot11rate(rt, rix),
			    dot11str(rt, rix),
			    bin_to_size(y),
			    (uintmax_t) sn->stats[y][rix].total_packets,
			    (uintmax_t) sn->stats[y][rix].packets_acked,
			    sn->stats[y][rix].ewma_pct / 10,
			    sn->stats[y][rix].ewma_pct % 10,
			    (uintmax_t) sn->stats[y][rix].tries,
			    sn->stats[y][rix].successive_failures,
			    sn->stats[y][rix].average_tx_time,
			    sn->stats[y][rix].last_tx);
			if (rix == sn->current_rix[y])
				PRINTATTR_OFF(COLOR_PAIR(y+4) | A_BOLD);
			else if (rix == sn->last_sample_rix[y])
				PRINTATTR_OFF(COLOR_PAIR(3) | A_BOLD);
#if 0
			else if (sn->stats[y][rix].ewma_pct / 10 < 50)
				PRINTATTR_OFF(COLOR_PAIR(2) | A_BOLD);
			else if (sn->stats[y][rix].ewma_pct / 10 < 75)
				PRINTATTR_OFF(COLOR_PAIR(1) | A_BOLD);
#endif
		}
	}
}

static void
ath_setifname(struct ath_ratestats *r, const char *ifname)
{

	strncpy(r->re.if_name, ifname, sizeof (r->re.if_name));
}

static void
ath_setsta(struct ath_ratestats *r, uint8_t *mac)
{

	memcpy(&r->re.is_u.macaddr, mac, sizeof(r->re.is_u.macaddr));
}

static void
ath_rate_ioctl(struct ath_ratestats *r)
{

	if (ioctl(r->s, SIOCGATHNODERATESTATS, &r->re) < 0)
		err(1, "ioctl");
}

static int
rate_node_stats(struct ath_ratestats *r, struct ether_addr *e)
{
	struct ath_rateioctl_tlv *av;
	struct sample_node *sn = NULL;
	struct ath_rateioctl_rt *rt = NULL;
	int error = 0;
	uint8_t *buf = (uint8_t *) r->re.buf;

	/*
	 * For now, hard-code the TLV order and contents.  Ew!
	 */
	av = (struct ath_rateioctl_tlv *) buf;
	if (av->tlv_id != ATH_RATE_TLV_RATETABLE) {
		fprintf(stderr, "unexpected rate control TLV (got 0x%x, "
		    "expected 0x%x\n",
		    av->tlv_id,
		    ATH_RATE_TLV_RATETABLE);
		exit(127);
	}
	if (av->tlv_len != sizeof(struct ath_rateioctl_rt)) {
		fprintf(stderr, "unexpected TLV len (got %d bytes, "
		    "expected %d bytes\n",
		    av->tlv_len,
		    (int) sizeof(struct ath_rateioctl_rt));
		exit(127);
	}
	rt = (void *) (buf + sizeof(struct ath_rateioctl_tlv));

	/* Next */
	av = (void *) (buf + sizeof(struct ath_rateioctl_tlv) +
	    sizeof(struct ath_rateioctl_rt));
	if (av->tlv_id != ATH_RATE_TLV_SAMPLENODE) {
		fprintf(stderr, "unexpected rate control TLV (got 0x%x, "
		    "expected 0x%x\n",
		    av->tlv_id,
		    ATH_RATE_TLV_SAMPLENODE);
		exit(127);
	}
	if (av->tlv_len != sizeof(struct sample_node)) {
		fprintf(stderr, "unexpected TLV len (got %d bytes, "
		    "expected %d bytes\n",
		    av->tlv_len,
		    (int) sizeof(struct sample_node));
		exit(127);
	}
	sn = (void *) (buf + sizeof(struct ath_rateioctl_tlv) +
	    sizeof(struct ath_rateioctl_rt) +
	    sizeof(struct ath_rateioctl_tlv));

	ath_sample_stats(r, rt, sn);

	return (0);
}

static void
fetch_and_print_stats(struct ath_ratestats *r, struct ether_addr *e,
    uint8_t *buf)
{

	/* Zero the buffer before it's passed in */
	memset(buf, '\0', STATS_BUF_SIZE);

	/*
	 * Set the station address for this lookup.
	 */
	ath_setsta(r, e->octet);

	/*
	 * Fetch the data from the driver.
	 */
	ath_rate_ioctl(r);

	/*
	 * Decode and parse statistics.
	 */
	rate_node_stats(r, e);
}

int
main(int argc, char *argv[])
{
	char const *ifname = NULL, *macaddr = NULL;
	int c;
	int do_all = 0;
	struct ether_addr *e;
	struct ath_ratestats r;
	uint8_t *buf;
	useconds_t sleep_period;
	float f;
	short cf, cb;

	ifname = getenv("ATH");
	if (ifname == NULL)
		ifname = ATH_DEFAULT;

	while ((c = getopt(argc, argv, "ahi:m:s:")) != -1) {
		switch (c) {
		case 'a':
			do_all = 1;
			break;
		case 'i':
			ifname = optarg;
			break;
		case 'm':
			macaddr = optarg;
			break;
		case 's':
			sscanf(optarg, "%f", &f);
			do_loop = 1;
			sleep_period = (useconds_t) (f * 1000000.0);
			break;
		default:
			errx(1,
			    "usage: %s [-h] [-i ifname] [-a] [-m macaddr] [-s sleep period]\n",
			    argv[0]);
			/* NOTREACHED */
		}
	}

	if (macaddr == NULL) {
		errx(1, "%s: macaddress wasn't supplied and no -a given\n",
		    argv[0]);
		/* NOTREACHED */
	}
	e = ether_aton(macaddr);
	if (e == NULL)
		err(1, "ether_aton");

	bzero(&r, sizeof(r));

	/*
	 * Persistent buffer for each lookup
	 */
	buf = malloc(STATS_BUF_SIZE);
	if (buf == NULL)
		err(1, "calloc");

	r.re.buf = (char *) buf;
	r.re.len = STATS_BUF_SIZE;

	r.s = socket(AF_INET, SOCK_DGRAM, 0);
	if (r.s < 0) {
		err(1, "socket");
	}

	/* XXX error check */
	ath_setifname(&r, ifname);

	if (do_loop) {
		initscr();
		start_color();
		use_default_colors();
		pair_content(0, &cf, &cb);
		/* Error - medium */
		init_pair(1, COLOR_YELLOW, cb);
		/* Error - high */
		init_pair(2, COLOR_RED, cb);
		/* Sample */
		init_pair(3, COLOR_CYAN, cb);
		/* 250 byte frames */
		init_pair(4, COLOR_BLUE, cb);
		/* 1600 byte frames */
		init_pair(5, COLOR_MAGENTA, cb);
		cbreak();
		noecho();
		nonl();
		nodelay(stdscr, 1);
		intrflush(stdscr, FALSE);
		keypad(stdscr, TRUE);

		while (1) {
			clear();
			move(0, 0);
			fetch_and_print_stats(&r, e, buf);
			refresh();
			usleep(sleep_period);
		}
	} else
		fetch_and_print_stats(&r, e, buf);

	exit(0);
}
