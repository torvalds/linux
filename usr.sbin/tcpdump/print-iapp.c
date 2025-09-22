/*	$OpenBSD: print-iapp.c,v 1.6 2018/07/06 05:47:22 dlg Exp $	*/

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

#include <sys/time.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/ioctl.h>

#include <net/if.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net80211/ieee80211.h>

#include <pcap.h>
#include <stdio.h>
#include <string.h>

#include "addrtoname.h"
#include "interface.h"
#include "iapp.h"

const char *ieee80211_iapp_frame_type_name[] =
    IEEE80211_IAPP_FRAME_TYPE_NAME;

extern int ieee80211_encap;

void
iapp_print(const u_char *p, u_int len)
{
	struct ieee80211_iapp_frame *wf = (struct ieee80211_iapp_frame *)p;
	struct ieee80211_iapp_add_notify *add;
	struct pcap_pkthdr fakeh;
	const u_char *data;

	TCHECK2(*wf, sizeof(struct ieee80211_iapp_frame));

	/* Print common IAPP information */
	printf("IAPPv%u ", wf->i_version);
	if (wf->i_command & 0xf0)
		printf("unknown: 0x%0x ", wf->i_command);
	else
		printf("%s ", ieee80211_iapp_frame_type_name[wf->i_command]);
	printf("(id %u) %u: ", wf->i_identifier, wf->i_length);


	data = p + sizeof(struct ieee80211_iapp_frame);

	switch (wf->i_command) {
	case IEEE80211_IAPP_FRAME_ADD_NOTIFY:
		/*
		 * Print details about the IAPP ADD.notify message.
		 */
		TCHECK2(*data, sizeof(struct ieee80211_iapp_add_notify));
		add = (struct ieee80211_iapp_add_notify *)data;

		printf("octets %u, ", add->a_length);
		if (add->a_reserved)
			printf("reserved %u, ", add->a_reserved);
		if (add->a_length == IEEE80211_ADDR_LEN)
			printf("lladdr %s, ", etheraddr_string(add->a_macaddr));
		printf("seq %u", add->a_seqnum);
		break;
	case IEEE80211_IAPP_FRAME_HOSTAPD_RADIOTAP:
	case IEEE80211_IAPP_FRAME_HOSTAPD_PCAP:
		/*
		 * hostapd(8) uses its own subtypes to send IEEE 802.11
		 * frame dumps to the IAPP group (either with or without
		 * radiotap header). Decode it using the IEEE 802.11
		 * printer.
		 */
		bzero(&fakeh, sizeof(fakeh));
		fakeh.len = wf->i_length;
		fakeh.caplen = snapend - data;

		ieee80211_encap = 1;
		if (wf->i_command == IEEE80211_IAPP_FRAME_HOSTAPD_RADIOTAP)
			ieee802_11_radio_if_print(NULL, &fakeh, data);
		else
			ieee802_11_if_print(NULL, &fakeh, data);
		ieee80211_encap = 0;
		break;
	}
	return;

trunc:
	printf("[|IAPP]");
}

