/*-
 * Copyright (c) 2014 Adrian Chadd <adrian@FreeBSD.org>
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

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <err.h>
#include <net/if.h>
#include <sys/endian.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/sysctl.h>

#include "net80211/ieee80211_ioctl.h"
#include "net80211/ieee80211_radiotap.h"

#include "if_iwn_ioctl.h"
#include "if_iwnreg.h"
#include "iwnstats.h"
#include "iwn_ioctl.h"

#define	IWN_DEFAULT_IF		"iwn0"

static struct iwnstats *
iwnstats_new(const char *ifname)
{
	struct iwnstats *is;
	char buf[128];

	is = calloc(1, sizeof(struct iwnstats));
	if (is == NULL)
		return (NULL);

	snprintf(buf, sizeof(buf), "/dev/%s", ifname);
	is->s = open(buf, O_RDWR);
	if (is->s < 0)
		err(1, "open");

	return (is);
}

static void
iwn_stats_phy_print(struct iwnstats *is, struct iwn_rx_phy_stats *rxphy,
    const char *prefix)
{

	printf("%s: %s: ina=%d, fina=%d, bad_plcp=%d, bad_crc32=%d, overrun=%d, eoverrun=%d\n",
	        __func__,
		prefix,
		le32toh(rxphy->ina),
		le32toh(rxphy->fina),
		le32toh(rxphy->bad_plcp),
		le32toh(rxphy->bad_crc32),
		le32toh(rxphy->overrun),
		le32toh(rxphy->eoverrun));

	printf("%s: %s: fa=%d, bad_fina_sync=%d, sfd_timeout=%d, fina_timeout=%d, no_rts_ack=%d\n",
	        __func__,
		prefix,
		le32toh(rxphy->fa),
		le32toh(rxphy->bad_fina_sync),
		le32toh(rxphy->sfd_timeout),
		le32toh(rxphy->fina_timeout),
		le32toh(rxphy->no_rts_ack));

	printf("%s: %s: rxe_limit=%d, ack=%d, cts=%d, ba_resp=%d, dsp_kill=%d, bad_mh=%d, rssi_sum=%d\n",
	        __func__,
		prefix,
		le32toh(rxphy->rxe_limit),
		le32toh(rxphy->ack),
		le32toh(rxphy->cts),
		le32toh(rxphy->ba_resp),
		le32toh(rxphy->dsp_kill),
		le32toh(rxphy->bad_mh),
		le32toh(rxphy->rssi_sum));
}

static void
iwn_stats_rx_general_print(struct iwnstats *is, struct iwn_rx_general_stats *g)
{

	printf("%s: bad_cts=%d, bad_ack=%d, not_bss=%d, filtered=%d, bad_chan=%d, beacons=%d\n",
	    __func__,
	    le32toh(g->bad_cts),
	    le32toh(g->bad_ack),
	    le32toh(g->not_bss),
	    le32toh(g->filtered),
	    le32toh(g->bad_chan),
	    le32toh(g->beacons));

	/* XXX it'd be nice to have adc/ina saturated as a % of time */
	printf("%s: missed_beacons=%d, adc_saturated=%d, ina_searched=%d\n",
	    __func__,
	    le32toh(g->missed_beacons),
	    le32toh(g->adc_saturated),
	    le32toh(g->ina_searched));

	printf("%s: noise=[%d, %d, %d] flags=0x%08x, load=%d, fa=%d\n",
	    __func__,
	    le32toh(g->noise[0]),
	    le32toh(g->noise[1]),
	    le32toh(g->noise[2]),
	    le32toh(g->flags),
	    le32toh(g->load),
	    le32toh(g->fa));

	printf("%s: rssi=[%d, %d, %d] energy=[%d %d %d]\n",
	    __func__,
	    le32toh(g->rssi[0]),
	    le32toh(g->rssi[1]),
	    le32toh(g->rssi[2]),
	    le32toh(g->energy[0]),
	    le32toh(g->energy[1]),
	    le32toh(g->energy[2]));
}

static void
iwn_stats_tx_print(struct iwnstats *is, struct iwn_tx_stats *tx)
{

	printf("%s: preamble=%d, rx_detected=%d, bt_defer=%d, bt_kill=%d, short_len=%d\n",
	    __func__,
	    le32toh(tx->preamble),
	    le32toh(tx->rx_detected),
	    le32toh(tx->bt_defer),
	    le32toh(tx->bt_kill),
	    le32toh(tx->short_len));

	printf("%s: cts_timeout=%d, ack_timeout=%d, exp_ack=%d, ack=%d, msdu=%d\n",
	    __func__,
	    le32toh(tx->cts_timeout),
	    le32toh(tx->ack_timeout),
	    le32toh(tx->exp_ack),
	    le32toh(tx->ack),
	    le32toh(tx->msdu));

	printf("%s: burst_err1=%d, burst_err2=%d, cts_collision=%d, ack_collision=%d\n",
	    __func__,
	    le32toh(tx->burst_err1),
	    le32toh(tx->burst_err2),
	    le32toh(tx->cts_collision),
	    le32toh(tx->ack_collision));

	printf("%s: ba_timeout=%d, ba_resched=%d, query_ampdu=%d, query=%d, query_ampdu_frag=%d\n",
	    __func__,
	    le32toh(tx->ba_timeout),
	    le32toh(tx->ba_resched),
	    le32toh(tx->query_ampdu),
	    le32toh(tx->query),
	    le32toh(tx->query_ampdu_frag));

	printf("%s: query_mismatch=%d, not_ready=%d, underrun=%d, bt_ht_kill=%d, rx_ba_resp=%d\n",
	    __func__,
	    le32toh(tx->query_mismatch),
	    le32toh(tx->not_ready),
	    le32toh(tx->underrun),
	    le32toh(tx->bt_ht_kill),
	    le32toh(tx->rx_ba_resp));
}

static void
iwn_stats_ht_phy_print(struct iwnstats *is, struct iwn_rx_ht_phy_stats *ht)
{

	printf("%s: bad_plcp=%d, overrun=%d, eoverrun=%d, good_crc32=%d, bad_crc32=%d\n",
	    __func__,
	    le32toh(ht->bad_plcp),
	    le32toh(ht->overrun),
	    le32toh(ht->eoverrun),
	    le32toh(ht->good_crc32),
	    le32toh(ht->bad_crc32));

	printf("%s: bad_mh=%d, good_ampdu_crc32=%d, ampdu=%d, fragment=%d\n",
	    __func__,
	    le32toh(ht->bad_plcp),
	    le32toh(ht->good_ampdu_crc32),
	    le32toh(ht->ampdu),
	    le32toh(ht->fragment));
}


static void
iwn_stats_general_print(struct iwnstats *is, struct iwn_stats *stats)
{

	/* General */
	printf("%s: temp=%d, temp_m=%d, burst_check=%d, burst=%d, sleep=%d, slot_out=%d, slot_idle=%d\n",
	        __func__,
		le32toh(stats->general.temp),
		le32toh(stats->general.temp_m),
		le32toh(stats->general.burst_check),
		le32toh(stats->general.burst),
		le32toh(stats->general.sleep),
		le32toh(stats->general.slot_out),
		le32toh(stats->general.slot_idle));
	printf("%s: slot_out=%d, ttl_tstamp=0x%08x, tx_ant_a=%d, tx_ant_b=%d, exec=%d, probe=%d\n",
	        __func__,
		le32toh(stats->general.slot_out),
		le32toh(stats->general.ttl_tstamp),
		le32toh(stats->general.tx_ant_a),
		le32toh(stats->general.tx_ant_b),
		le32toh(stats->general.exec),
		le32toh(stats->general.probe));
	printf("%s: rx_enabled=%d\n",
	        __func__,
		le32toh(stats->general.rx_enabled));
}

static void
iwn_print(struct iwnstats *is)
{
	struct iwn_stats *s;
	struct timeval tv;

	s = &is->st;

	gettimeofday(&tv, NULL);
	printf("time=%ld.%.6ld\n", (long)tv.tv_sec, (long)tv.tv_usec);

	iwn_stats_general_print(is, s);

	/* RX */
	iwn_stats_phy_print(is, &s->rx.ofdm, "ofdm");
	iwn_stats_phy_print(is, &s->rx.cck, "cck");
	iwn_stats_ht_phy_print(is, &s->rx.ht);
	iwn_stats_rx_general_print(is, &s->rx.general);

	/* TX */
	iwn_stats_tx_print(is, &s->tx);
	printf("--\n");
}

static void
usage(void)
{
	printf("Usage: iwnstats [-h] [-i ifname]\n");
	printf("    -h:			Help\n");
	printf("    -i <ifname>:	Use ifname (default %s)\n",
	    IWN_DEFAULT_IF);
}

int
main(int argc, char *argv[])
{
	struct iwnstats *is;
	int ch;
	char *ifname;
	bool first;
	char *sysctlname;
	size_t len;
	int ret;

	ifname = strdup(IWN_DEFAULT_IF);

	/* Parse command line arguments */
	while ((ch = getopt(argc, argv,
	    "hi:")) != -1) {
		switch (ch) {
		case 'i':
			if (ifname)
				free(ifname);
			ifname = strdup(optarg);
			break;
		default:
		case '?':
		case 'h':
			usage();
			exit(1);
		}
	}

	is = iwnstats_new(ifname);

	if (is == NULL) {
		fprintf(stderr, "%s: couldn't allocate new stats structure\n",
		    argv[0]);
		exit(127);
	}

	/* begin fetching data */
	first = true;
	while (1) {
		if (iwn_collect(is) != 0) {
			fprintf(stderr, "%s: fetch failed\n", argv[0]);
			if (first)
				return 1;
			goto next;
		}

		iwn_print(is);

	next:
		usleep(100 * 1000);
		first = false;
	}

	exit(0);
}
