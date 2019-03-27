/*-
 * Copyright (c) 2002-2007 Sam Leffler, Errno Consulting
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

/*
 * wlanstats [-i interface]
 * (default interface is wlan0).
 */

#include <sys/param.h>
#include <sys/socket.h>

#include <net/ethernet.h>
#include <net80211/_ieee80211.h>

#include <err.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>

#include "wlanstats.h"

static struct {
	const char *tag;
	const char *fmt;
} tags[] = {
  { "default",
    "input,rx_mgmt,output,rx_badkeyid,scan_active,scan_bg,bmiss,rssi,noise,rate"
  },
  { "ampdu",
    "input,output,ampdu_reorder,ampdu_oor,rx_dup,ampdu_flush,ampdu_move,"
    "ampdu_drop,ampdu_bar,ampdu_baroow,ampdu_barmove,ampdu_bartx,"
    "ampdu_bartxfail,ampdu_bartxretry,rssi,rate"
  },
  {
    "amsdu",
    "input,output,amsdu_tooshort,amsdu_split,amsdu_decap,amsdu_encap,rssi,rate"
  },
};

static const char *
getfmt(const char *tag)
{
	int i;
	for (i = 0; i < nitems(tags); i++)
		if (strcasecmp(tags[i].tag, tag) == 0)
			return tags[i].fmt;
	return tag;
}

static int signalled;

static void
catchalarm(int signo __unused)
{
	signalled = 1;
}

#if 0
static void
print_sta_stats(FILE *fd, const u_int8_t macaddr[IEEE80211_ADDR_LEN])
{
#define	STAT(x,fmt) \
	if (ns->ns_##x) { fprintf(fd, "%s" #x " " fmt, sep, ns->ns_##x); sep = " "; }
	struct ieee80211req ireq;
	struct ieee80211req_sta_stats stats;
	const struct ieee80211_nodestats *ns = &stats.is_stats;
	const char *sep;

	(void) memset(&ireq, 0, sizeof(ireq));
	(void) strncpy(ireq.i_name, ifr.ifr_name, sizeof(ireq.i_name));
	ireq.i_type = IEEE80211_IOC_STA_STATS;
	ireq.i_data = &stats;
	ireq.i_len = sizeof(stats);
	memcpy(stats.is_u.macaddr, macaddr, IEEE80211_ADDR_LEN);
	if (ioctl(s, SIOCG80211, &ireq) < 0)
		err(1, "unable to get station stats for %s",
			ether_ntoa((const struct ether_addr*) macaddr));

	fprintf(fd, "%s:\n", ether_ntoa((const struct ether_addr*) macaddr));

	sep = "\t";
	STAT(rx_data, "%u");
	STAT(rx_mgmt, "%u");
	STAT(rx_ctrl, "%u");
	STAT(rx_beacons, "%u");
	STAT(rx_proberesp, "%u");
	STAT(rx_ucast, "%u");
	STAT(rx_mcast, "%u");
	STAT(rx_bytes, "%llu");
	STAT(rx_dup, "%u");
	STAT(rx_noprivacy, "%u");
	STAT(rx_wepfail, "%u");
	STAT(rx_demicfail, "%u");
	STAT(rx_decap, "%u");
	STAT(rx_defrag, "%u");
	STAT(rx_disassoc, "%u");
	STAT(rx_deauth, "%u");
	STAT(rx_decryptcrc, "%u");
	STAT(rx_unauth, "%u");
	STAT(rx_unencrypted, "%u");
	fprintf(fd, "\n");

	sep = "\t";
	STAT(tx_data, "%u");
	STAT(tx_mgmt, "%u");
	STAT(tx_probereq, "%u");
	STAT(tx_ucast, "%u");
	STAT(tx_mcast, "%u");
	STAT(tx_bytes, "%llu");
	STAT(tx_novlantag, "%u");
	STAT(tx_vlanmismatch, "%u");
	fprintf(fd, "\n");

	sep = "\t";
	STAT(tx_assoc, "%u");
	STAT(tx_assoc_fail, "%u");
	STAT(tx_auth, "%u");
	STAT(tx_auth_fail, "%u");
	STAT(tx_deauth, "%u");
	STAT(tx_deauth_code, "%llu");
	STAT(tx_disassoc, "%u");
	STAT(tx_disassoc_code, "%u");
	fprintf(fd, "\n");

#undef STAT
}
#endif

void
usage(void) {
	printf("wlanstats: [-ah] [-i ifname] [-l] [-m station MAC address] [-o fmt] [interval]\n");
}

int
main(int argc, char *argv[])
{
	struct wlanstatfoo *wf;
	struct ether_addr *ea;
	const uint8_t *mac = NULL;
	const char *ifname;
	int allnodes = 0;
	int c, mode;

	ifname = getenv("WLAN");
	if (ifname == NULL)
		ifname = "wlan0";
	wf = wlanstats_new(ifname, getfmt("default"));
	while ((c = getopt(argc, argv, "ahi:lm:o:")) != -1) {
		switch (c) {
		case 'a':
			allnodes++;
			break;
		case 'h':
			usage();
			exit(0);
		case 'i':
			wf->setifname(wf, optarg);
			break;
		case 'l':
			wf->print_fields(wf, stdout);
			return 0;
		case 'm':
			ea = ether_aton(optarg);
			if (!ea)
				errx(1, "%s: invalid ethernet address", optarg);
			mac = ea->octet;
			break;
		case 'o':
			wf->setfmt(wf, getfmt(optarg));
			break;
		default:
			usage();
			exit(1);
			/*NOTREACHED*/
		}
	}
	argc -= optind;
	argv += optind;

	mode = wf->getopmode(wf);
	wf->setstamac(wf, mac);

	if (argc > 0) {
		u_long interval = strtoul(argv[0], NULL, 0);
		int line, omask;

		if (interval < 1)
			interval = 1;
		signal(SIGALRM, catchalarm);
		signalled = 0;
		alarm(interval);
	banner:
		wf->print_header(wf, stdout);
		line = 0;
	loop:
		if (line != 0) {
			wf->collect_cur(wf);
			wf->print_current(wf, stdout);
			wf->update_tot(wf);
		} else {
			wf->collect_tot(wf);
			wf->print_total(wf, stdout);
		}
		fflush(stdout);
		omask = sigblock(sigmask(SIGALRM));
		if (!signalled)
			sigpause(0);
		sigsetmask(omask);
		signalled = 0;
		alarm(interval);
		line++;
		/* refresh every display in case sta roams */
		if (mac == NULL && mode == IEEE80211_M_STA)
			wf->setstamac(wf, NULL);
		if (line == 21)		/* XXX tty line count */
			goto banner;
		else
			goto loop;
		/*NOTREACHED*/
#if 0
	} else if (allnodes) {
		struct ieee80211req_sta_info *si;
		union {
			struct ieee80211req_sta_req req;
			uint8_t buf[24*1024];
		} u;
		uint8_t *cp;
		struct ieee80211req ireq;
		int len;

		/*
		 * Retrieve station/neighbor table and print stats for each.
		 */
		(void) memset(&ireq, 0, sizeof(ireq));
		(void) strncpy(ireq.i_name, ifr.ifr_name, sizeof(ireq.i_name));
		ireq.i_type = IEEE80211_IOC_STA_INFO;
		memset(&u.req.macaddr, 0xff, sizeof(u.req.macaddr));
		ireq.i_data = &u;
		ireq.i_len = sizeof(u);
		if (ioctl(s, SIOCG80211, &ireq) < 0)
			err(1, "unable to get station information");
		len = ireq.i_len;
		if (len >= sizeof(struct ieee80211req_sta_info)) {
			cp = u.req.info;
			do {
				si = (struct ieee80211req_sta_info *) cp;
				if (si->isi_len < sizeof(*si))
					break;
				print_sta_stats(stdout, si->isi_macaddr);
				cp += si->isi_len, len -= si->isi_len;
			} while (len >= sizeof(struct ieee80211req_sta_info));
		}
#endif
	} else {
		wf->collect_tot(wf);
		wf->print_verbose(wf, stdout);
	}
	return 0;
}
