/*	$OpenBSD: print-802_11.c,v 1.11 2019/05/10 01:29:31 guenther Exp $	*/

/*
 * Copyright (c) 2005 Reyk Floeter <reyk@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* usr.sbin/tcpdump/print-802_11.c,v 1.3 2005/03/09 11:43:17 deraadt Exp */

#include <sys/time.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/if_media.h>
#include <net/if_arp.h>
#include <net/if_llc.h>
#include <net/bpf.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <arpa/inet.h>

#include <net80211/ieee80211.h>
#include <net80211/ieee80211_radiotap.h>

#include <pcap.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

#include "hostapd.h"

const char *ieee80211_mgt_subtype_name[] = {
	"association request",
	"association response",
	"reassociation request",
	"reassociation response",
	"probe request",
	"probe response",
	"reserved#6",
	"reserved#7",
	"beacon",
	"atim",
	"disassociation",
	"authentication",
	"deauthentication",
	"reserved#13",
	"reserved#14",
	"reserved#15"
};

const u_int8_t *snapend;
int vflag = 1, eflag = 1;

int	 ieee80211_hdr(struct ieee80211_frame *);
void	 ieee80211_print_element(u_int8_t *, u_int);
void	 ieee80211_print_essid(u_int8_t *, u_int);
int	 ieee80211_elements(struct ieee80211_frame *);
int	 ieee80211_frame(struct ieee80211_frame *);
int	 ieee80211_print(struct ieee80211_frame *);
u_int	 ieee80211_any2ieee(u_int, u_int);
void	 ieee802_11_if_print(u_int8_t *, u_int);
void	 ieee802_11_radio_if_print(u_int8_t *, u_int);

#define TCARR(a)	TCHECK2(*a, sizeof(a))

int
ieee80211_hdr(struct ieee80211_frame *wh)
{
	struct ieee80211_frame_addr4 *w4;

	switch (wh->i_fc[1] & IEEE80211_FC1_DIR_MASK) {
	case IEEE80211_FC1_DIR_NODS:
		TCARR(wh->i_addr2);
		PRINTF("%s", etheraddr_string(wh->i_addr2));
		TCARR(wh->i_addr1);
		PRINTF(" > %s", etheraddr_string(wh->i_addr1));
		TCARR(wh->i_addr3);
		PRINTF(", bssid %s", etheraddr_string(wh->i_addr3));
		break;
	case IEEE80211_FC1_DIR_TODS:
		TCARR(wh->i_addr2);
		PRINTF("%s", etheraddr_string(wh->i_addr2));
		TCARR(wh->i_addr3);
		PRINTF(" > %s", etheraddr_string(wh->i_addr3));
		TCARR(wh->i_addr1);
		PRINTF(", bssid %s, > DS", etheraddr_string(wh->i_addr1));
		break;
	case IEEE80211_FC1_DIR_FROMDS:
		TCARR(wh->i_addr3);
		PRINTF("%s", etheraddr_string(wh->i_addr3));
		TCARR(wh->i_addr1);
		PRINTF(" > %s", etheraddr_string(wh->i_addr1));
		TCARR(wh->i_addr2);
		PRINTF(", bssid %s, DS >", etheraddr_string(wh->i_addr2));
		break;
	case IEEE80211_FC1_DIR_DSTODS:
		w4 = (struct ieee80211_frame_addr4 *) wh;
		TCARR(w4->i_addr4);
		PRINTF("%s", etheraddr_string(w4->i_addr4));
		TCARR(w4->i_addr3);
		PRINTF(" > %s", etheraddr_string(w4->i_addr3));
		TCARR(w4->i_addr2);
		PRINTF(", bssid %s", etheraddr_string(w4->i_addr2));
		TCARR(w4->i_addr1);
		PRINTF(" > %s, DS > DS", etheraddr_string(w4->i_addr1));
		break;
	}
	if (vflag) {
		TCARR(wh->i_seq);
		PRINTF(" (seq %u)", letoh16(*(u_int16_t *)&wh->i_seq[0]));
	}

	return (0);

 trunc:
	/* Truncated elements in frame */
	return (1);
}

/* Caller checks len */
void
ieee80211_print_element(u_int8_t *data, u_int len)
{
	u_int8_t *p;
	u_int i;

	PRINTF(" 0x");
	for (i = 0, p = data; i < len; i++, p++)
		PRINTF("%02x", *p);
}

/* Caller checks len */
void
ieee80211_print_essid(u_int8_t *essid, u_int len)
{
	u_int8_t *p;
	u_int i;

	if (len > IEEE80211_NWID_LEN)
		len = IEEE80211_NWID_LEN;

	/* determine printable or not */
	for (i = 0, p = essid; i < len; i++, p++) {
		if (*p < ' ' || *p > 0x7e)
			break;
	}
	if (i == len) {
		PRINTF(" (");
		for (i = 0, p = essid; i < len; i++, p++)
			PRINTF("%c", *p);
		PRINTF(")");
	} else
		ieee80211_print_element(essid, len);
}

int
ieee80211_elements(struct ieee80211_frame *wh)
{
	u_int8_t *frm;
	u_int8_t *tstamp, *bintval, *capinfo;
	int i;

	frm = (u_int8_t *)&wh[1];

	tstamp = frm;
	TCHECK2(*tstamp, 8);
	frm += 8;

	bintval = frm;
	TCHECK2(*bintval, 2);
	frm += 2;

	if (vflag)
		PRINTF(", interval %u", letoh16(*(u_int16_t *)bintval));

	capinfo = frm;
	TCHECK2(*capinfo, 2);
	frm += 2;

#if 0
	if (vflag)
		printb(", caps", letoh16(*(u_int16_t *)capinfo),
		    IEEE80211_CAPINFO_BITS);
#endif

	while (TTEST2(*frm, 2)) {
		u_int len = frm[1];
		u_int8_t *data = frm + 2;

		if (!TTEST2(*data, len))
			break;

#define ELEM_CHECK(l)	if (len != l) break

		switch (*frm) {
		case IEEE80211_ELEMID_SSID:
			PRINTF(", ssid");
			ieee80211_print_essid(data, len);
			break;
		case IEEE80211_ELEMID_RATES:
			if (!vflag)
				break;
			PRINTF(", rates");
			for (i = len; i > 0; i--, data++)
				PRINTF(" %uM",
				    (data[0] & IEEE80211_RATE_VAL) / 2);
			break;
		case IEEE80211_ELEMID_FHPARMS:
			ELEM_CHECK(5);
			PRINTF(", fh (dwell %u, chan %u, index %u)",
			    (data[1] << 8) | data[0],
			    (data[2] - 1) * 80 + data[3],	/* FH_CHAN */
			    data[4]);
			break;
		case IEEE80211_ELEMID_DSPARMS:
			ELEM_CHECK(1);
			if (!vflag)
				break;
			PRINTF(", ds");
			PRINTF(" (chan %u)", data[0]);
			break;
		case IEEE80211_ELEMID_CFPARMS:
			if (!vflag)
				break;
			PRINTF(", cf");
			ieee80211_print_element(data, len);
			break;
		case IEEE80211_ELEMID_TIM:
			if (!vflag)
				break;
			PRINTF(", tim");
			ieee80211_print_element(data, len);
			break;
		case IEEE80211_ELEMID_IBSSPARMS:
			if (!vflag)
				break;
			PRINTF(", ibss");
			ieee80211_print_element(data, len);
			break;
		case IEEE80211_ELEMID_COUNTRY:
			if (!vflag)
				break;
			PRINTF(", country");
			for (i = len; i > 0; i--, data++)
				PRINTF(" %u", data[0]);
			break;
		case IEEE80211_ELEMID_CHALLENGE:
			if (!vflag)
				break;
			PRINTF(", challenge");
			ieee80211_print_element(data, len);
			break;
		case IEEE80211_ELEMID_ERP:
			if (!vflag)
				break;
			PRINTF(", erp");
			ieee80211_print_element(data, len);
			break;
		case IEEE80211_ELEMID_RSN:
			if (!vflag)
				break;
			PRINTF(", rsn");
			ieee80211_print_element(data, len);
			break;
		case IEEE80211_ELEMID_XRATES:
			if (!vflag)
				break;
			PRINTF(", xrates");
			for (i = len; i > 0; i--, data++)
				PRINTF(" %uM",
				    (data[0] & IEEE80211_RATE_VAL) / 2);
			break;
		case IEEE80211_ELEMID_TPC_REQUEST:
			if (!vflag)
				break;
			PRINTF(", tpcrequest");
			ieee80211_print_element(data, len);
			break;
		case IEEE80211_ELEMID_TPC_REPORT:
			if (!vflag)
				break;
			PRINTF(", tpcreport");
			ieee80211_print_element(data, len);
			break;
		case IEEE80211_ELEMID_VENDOR:
			if (!vflag)
				break;
			PRINTF(", vendor");
			ieee80211_print_element(data, len);
			break;
		default:
			if (!vflag)
				break;
			PRINTF(", %u:%u", (u_int) *frm, len);
			ieee80211_print_element(data, len);
			break;
		}
		frm += len + 2;

		if (frm >= snapend)
			break;
	}

#undef ELEM_CHECK

	return (0);

 trunc:
	/* Truncated elements in frame */
	return (1);
}

int
ieee80211_frame(struct ieee80211_frame *wh)
{
	u_int8_t subtype, type, *frm;

	TCARR(wh->i_fc);

	type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;
	subtype = wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;

	frm = (u_int8_t *)&wh[1];

	switch (type) {
	case IEEE80211_FC0_TYPE_DATA:
		PRINTF(": data");
		break;
	case IEEE80211_FC0_TYPE_MGT:
		PRINTF(": %s", ieee80211_mgt_subtype_name[
		    subtype >> IEEE80211_FC0_SUBTYPE_SHIFT]);
		switch (subtype) {
		case IEEE80211_FC0_SUBTYPE_BEACON:
		case IEEE80211_FC0_SUBTYPE_PROBE_RESP:
			if (ieee80211_elements(wh) != 0)
				goto trunc;
			break;
		case IEEE80211_FC0_SUBTYPE_AUTH:
			TCHECK2(*frm, 2);		/* Auth Algorithm */
			switch (IEEE80211_AUTH_ALGORITHM(frm)) {
			case IEEE80211_AUTH_ALG_OPEN:
				TCHECK2(*frm, 4);	/* Auth Transaction */
				switch (IEEE80211_AUTH_TRANSACTION(frm)) {
				case IEEE80211_AUTH_OPEN_REQUEST:
					PRINTF(" request");
					break;
				case IEEE80211_AUTH_OPEN_RESPONSE:
					PRINTF(" response");
					break;
				}
				break;
			case IEEE80211_AUTH_ALG_SHARED:
				TCHECK2(*frm, 4);	/* Auth Transaction */
				switch (IEEE80211_AUTH_TRANSACTION(frm)) {
				case IEEE80211_AUTH_SHARED_REQUEST:
					PRINTF(" request");
					break;
				case IEEE80211_AUTH_SHARED_CHALLENGE:
					PRINTF(" challenge");
					break;
				case IEEE80211_AUTH_SHARED_RESPONSE:
					PRINTF(" response");
					break;
				case IEEE80211_AUTH_SHARED_PASS:
					PRINTF(" pass");
					break;
				}
				break;
			case IEEE80211_AUTH_ALG_LEAP:
				PRINTF(" (leap)");
				break;
			}
			break;
		}
		break;
	default:
		PRINTF(": type#%d", type);
		break;
	}

	if (wh->i_fc[1] & IEEE80211_FC1_WEP)
		PRINTF(", WEP");

	return (0);

 trunc:
	/* Truncated 802.11 frame */
	return (1);
}

u_int
ieee80211_any2ieee(u_int freq, u_int flags)
{
	if (flags & IEEE80211_CHAN_2GHZ) {
		if (freq == 2484)
			return 14;
		if (freq < 2484)
			return (freq - 2407) / 5;
		else
			return 15 + ((freq - 2512) / 20);
	} else if (flags & IEEE80211_CHAN_5GHZ) {
		return (freq - 5000) / 5;
	} else {
		/* Assume channel is already an IEEE number */
		return (freq);
	}
}

int
ieee80211_print(struct ieee80211_frame *wh)
{
	if (eflag)
		if (ieee80211_hdr(wh))
			return (1);

	return (ieee80211_frame(wh));
}

void
ieee802_11_if_print(u_int8_t *buf, u_int len)
{
	struct ieee80211_frame *wh = (struct ieee80211_frame*)buf;

	snapend = buf + len;

	if (ieee80211_print(wh) != 0)
		PRINTF("[|802.11]");

	PRINTF(NULL);
}

void
ieee802_11_radio_if_print(u_int8_t *buf, u_int len)
{
	struct ieee80211_radiotap_header *rh =
	    (struct ieee80211_radiotap_header*)buf;
	struct ieee80211_frame *wh;
	u_int8_t *t;
	u_int32_t present;
	u_int rh_len;

	snapend = buf + len;

	TCHECK(*rh);

	rh_len = letoh16(rh->it_len);
	if (rh->it_version != 0) {
		PRINTF("[?radiotap + 802.11 v:%u]", rh->it_version);
		goto out;
	}

	wh = (struct ieee80211_frame *)(buf + rh_len);
	if (len <= rh_len || ieee80211_print(wh))
		PRINTF("[|802.11]");

	t = (u_int8_t*)buf + sizeof(struct ieee80211_radiotap_header);

	if ((present = letoh32(rh->it_present)) == 0)
		goto out;

	PRINTF(", <radiotap v%u", rh->it_version);

#define RADIOTAP(_x)	\
	(present & (1 << IEEE80211_RADIOTAP_##_x))

	if (RADIOTAP(TSFT)) {
		u_int64_t tsf;
		u_int32_t tsf_v[2];

		TCHECK2(*t, 8);

		tsf = letoh64(*(u_int64_t *)t);
		tsf_v[0] = (u_int32_t)(tsf >> 32);
		tsf_v[1] = (u_int32_t)(tsf & 0x00000000ffffffff);
		if (vflag > 1)
			PRINTF(", tsf 0x%08x%08x", tsf_v[0], tsf_v[1]);
		t += 8;
	}

	if (RADIOTAP(FLAGS)) {
		u_int8_t flags = *(u_int8_t*)t;
		TCHECK2(*t, 1);

		if (flags & IEEE80211_RADIOTAP_F_CFP)
			PRINTF(", CFP");
		if (flags & IEEE80211_RADIOTAP_F_SHORTPRE)
			PRINTF(", SHORTPRE");
		if (flags & IEEE80211_RADIOTAP_F_WEP)
			PRINTF(", WEP");
		if (flags & IEEE80211_RADIOTAP_F_FRAG)
			PRINTF(", FRAG");
		t += 1;
	}

	if (RADIOTAP(RATE)) {
		TCHECK2(*t, 1);
		if (vflag)
			PRINTF(", %uMbit/s", (*(u_int8_t*)t) / 2);
		t += 1;
	}

	if (RADIOTAP(CHANNEL)) {
		u_int16_t freq, flags;
		TCHECK2(*t, 2);

		freq = letoh16(*(u_int16_t*)t);
		t += 2;
		TCHECK2(*t, 2);
		flags = letoh16(*(u_int16_t*)t);
		t += 2;

		PRINTF(", chan %u", ieee80211_any2ieee(freq, flags));

		if (flags & IEEE80211_CHAN_DYN &&
		    flags & IEEE80211_CHAN_2GHZ)
			PRINTF(", 11g");
		else if (flags & IEEE80211_CHAN_CCK &&
		    flags & IEEE80211_CHAN_2GHZ)
			PRINTF(", 11b");
		else if (flags & IEEE80211_CHAN_OFDM &&
		    flags & IEEE80211_CHAN_2GHZ)
			PRINTF(", 11G");
		else if (flags & IEEE80211_CHAN_OFDM &&
		    flags & IEEE80211_CHAN_5GHZ)
			PRINTF(", 11a");

		if (flags & IEEE80211_CHAN_XR)
			PRINTF(", XR");
	}

	if (RADIOTAP(FHSS)) {
		TCHECK2(*t, 2);
		PRINTF(", fhss %u/%u", *(u_int8_t*)t, *(u_int8_t*)t + 1);
		t += 2;
	}

	if (RADIOTAP(DBM_ANTSIGNAL)) {
		TCHECK(*t);
		PRINTF(", sig %ddBm", *(int8_t*)t);
		t += 1;
	}

	if (RADIOTAP(DBM_ANTNOISE)) {
		TCHECK(*t);
		PRINTF(", noise %ddBm", *(int8_t*)t);
		t += 1;
	}

	if (RADIOTAP(LOCK_QUALITY)) {
		TCHECK2(*t, 2);
		if (vflag)
			PRINTF(", quality %u", letoh16(*(u_int16_t*)t));
		t += 2;
	}

	if (RADIOTAP(TX_ATTENUATION)) {
		TCHECK2(*t, 2);
		if (vflag)
			PRINTF(", txatt %u",
			    letoh16(*(u_int16_t*)t));
		t += 2;
	}

	if (RADIOTAP(DB_TX_ATTENUATION)) {
		TCHECK2(*t, 2);
		if (vflag)
			PRINTF(", txatt %udB",
			    letoh16(*(u_int16_t*)t));
		t += 2;
	}

	if (RADIOTAP(DBM_TX_POWER)) {
		TCHECK(*t);
		PRINTF(", txpower %ddBm", *(int8_t*)t);
		t += 1;
	}

	if (RADIOTAP(ANTENNA)) {
		TCHECK(*t);
		if (vflag)
			PRINTF(", antenna %u", *(u_int8_t*)t);
		t += 1;
	}

	if (RADIOTAP(DB_ANTSIGNAL)) {
		TCHECK(*t);
		PRINTF(", signal %udB", *(u_int8_t*)t);
		t += 1;
	}

	if (RADIOTAP(DB_ANTNOISE)) {
		TCHECK(*t);
		PRINTF(", noise %udB", *(u_int8_t*)t);
		t += 1;
	}

	if (RADIOTAP(FCS)) {
		TCHECK2(*t, 4);
		if (vflag)
			PRINTF(", fcs %08x", letoh32(*(u_int32_t*)t));
		t += 4;
	}

	if (RADIOTAP(RSSI)) {
		u_int8_t rssi, max_rssi;
		TCHECK(*t);
		rssi = *(u_int8_t*)t;
		t += 1;
		TCHECK(*t);
		max_rssi = *(u_int8_t*)t;
		t += 1;

		PRINTF(", rssi %u/%u", rssi, max_rssi);
	}

#undef RADIOTAP

	PRINTF(">");
	goto out;

 trunc:
	/* Truncated frame */
	PRINTF("[|radiotap + 802.11]");

 out:
	PRINTF(NULL);
}

void
hostapd_print_ieee80211(u_int dlt, u_int verbose, u_int8_t *buf, u_int len)
{
	if (verbose)
		vflag = 1;
	else
		vflag = 0;

	if (dlt == DLT_IEEE802_11)
		ieee802_11_if_print(buf, len);
	else
		ieee802_11_radio_if_print(buf, len);
}
