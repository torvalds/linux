/*	$OpenBSD: iapp.c,v 1.20 2019/05/10 01:29:31 guenther Exp $	*/

/*
 * Copyright (c) 2004, 2005 Reyk Floeter <reyk@openbsd.org>
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

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/uio.h>

#include <net/if.h>
#include <net/if_media.h>
#include <net/if_arp.h>
#include <net/if_llc.h>
#include <net/bpf.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <arpa/inet.h>

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "hostapd.h"
#include "iapp.h"

void
hostapd_iapp_init(struct hostapd_config *cfg)
{
	struct hostapd_apme *apme;
	struct hostapd_iapp *iapp = &cfg->c_iapp;

	if ((cfg->c_flags & HOSTAPD_CFG_F_APME) == 0)
		return;

	TAILQ_FOREACH(apme, &cfg->c_apmes, a_entries) {
		/* Get Host AP's BSSID */
		hostapd_priv_apme_bssid(apme);
		hostapd_log(HOSTAPD_LOG,
		    "%s/%s: attached Host AP interface with BSSID %s",
		    apme->a_iface, iapp->i_iface,
		    etheraddr_string(apme->a_bssid));

		/* Deauthenticate all stations on startup */
		(void)hostapd_apme_deauth(apme);
	}
}

void
hostapd_iapp_term(struct hostapd_config *cfg)
{
	struct hostapd_apme *apme;
	struct hostapd_iapp *iapp = &cfg->c_iapp;

	if ((cfg->c_flags & HOSTAPD_CFG_F_APME) == 0)
		return;

	TAILQ_FOREACH(apme, &cfg->c_apmes, a_entries) {
		hostapd_log(HOSTAPD_LOG_VERBOSE,
		    "%s/%s: detaching from Host AP",
		    apme->a_iface, iapp->i_iface);
	}
}

int
hostapd_iapp_add_notify(struct hostapd_apme *apme, struct hostapd_node *node)
{
	struct hostapd_config *cfg = (struct hostapd_config *)apme->a_cfg;
	struct hostapd_iapp *iapp = &cfg->c_iapp;
	struct sockaddr_in *addr;
	struct {
		struct ieee80211_iapp_frame hdr;
		struct ieee80211_iapp_add_notify add;
	} __packed frame;

	if ((iapp->i_flags & HOSTAPD_IAPP_F_ADD_NOTIFY) == 0)
		return (0);

	/*
	 * Send an ADD.notify message to other access points to notify
	 * about a new association on our Host AP.
	 */
	bzero(&frame, sizeof(frame));

	frame.hdr.i_version = IEEE80211_IAPP_VERSION;
	frame.hdr.i_command = IEEE80211_IAPP_FRAME_ADD_NOTIFY;
	frame.hdr.i_identifier = htons(iapp->i_cnt++);
	frame.hdr.i_length = sizeof(struct ieee80211_iapp_add_notify);

	frame.add.a_length = IEEE80211_ADDR_LEN;
	frame.add.a_seqnum = htons(node->ni_rxseq);
	bcopy(node->ni_macaddr, frame.add.a_macaddr, IEEE80211_ADDR_LEN);

	if (cfg->c_flags & HOSTAPD_CFG_F_BRDCAST)
		addr = &iapp->i_broadcast;
	else
		addr = &iapp->i_multicast;

	if (sendto(iapp->i_udp, &frame, sizeof(frame),
	    0, (struct sockaddr *)addr, sizeof(struct sockaddr_in)) == -1) {
		hostapd_log(HOSTAPD_LOG,
		    "%s: failed to send ADD notification: %s",
		    iapp->i_iface, strerror(errno));
		return (errno);
	}

	hostapd_log(HOSTAPD_LOG, "%s/%s: sent ADD notification for %s",
	    apme->a_iface, iapp->i_iface,
	    etheraddr_string(frame.add.a_macaddr));

	/* Send a LLC XID frame, see llc.c for details */
	return (hostapd_priv_llc_xid(cfg, node));
}

int
hostapd_iapp_radiotap(struct hostapd_apme *apme, u_int8_t *buf,
    const u_int len)
{
	struct hostapd_config *cfg = (struct hostapd_config *)apme->a_cfg;
	struct hostapd_iapp *iapp = &cfg->c_iapp;
	struct sockaddr_in *addr;
	struct ieee80211_iapp_frame hdr;
	struct msghdr msg;
	struct iovec iov[2];

	/*
	 * Send an HOSTAPD.pcap/radiotap message to other access points
	 * with an appended network dump. This is an hostapd extension to
	 * IAPP.
	 */
	bzero(&hdr, sizeof(hdr));

	hdr.i_version = IEEE80211_IAPP_VERSION;
	if (cfg->c_apme_dlt == DLT_IEEE802_11_RADIO)
		hdr.i_command = IEEE80211_IAPP_FRAME_HOSTAPD_RADIOTAP;
	else if (cfg->c_apme_dlt == DLT_IEEE802_11)
		hdr.i_command = IEEE80211_IAPP_FRAME_HOSTAPD_PCAP;
	else
		return (EINVAL);
	hdr.i_identifier = htons(iapp->i_cnt++);
	hdr.i_length = len;

	if (cfg->c_flags & HOSTAPD_CFG_F_BRDCAST)
		addr = &iapp->i_broadcast;
	else
		addr = &iapp->i_multicast;

	iov[0].iov_base = &hdr;
	iov[0].iov_len = sizeof(hdr);
	iov[1].iov_base = buf;
	iov[1].iov_len = len;
	msg.msg_name = (caddr_t)addr;
	msg.msg_namelen = sizeof(struct sockaddr_in);
	msg.msg_iov = iov;
	msg.msg_iovlen = 2;
	msg.msg_control = 0;
	msg.msg_controllen = 0;
	msg.msg_flags = 0;

	if (sendmsg(iapp->i_udp, &msg, 0) == -1) {
		hostapd_log(HOSTAPD_LOG,
		    "%s: failed to send HOSTAPD %s: %s",
		    iapp->i_iface, cfg->c_apme_dlt ==
		    DLT_IEEE802_11_RADIO ? "radiotap" : "pcap",
		    strerror(errno));
		return (errno);
	}

	return (0);
}

void
hostapd_iapp_input(int fd, short sig, void *arg)
{
	struct hostapd_config *cfg = (struct hostapd_config *)arg;
	struct hostapd_iapp *iapp = &cfg->c_iapp;
	struct hostapd_apme *apme;
	struct sockaddr_in addr;
	socklen_t addr_len;
	ssize_t len;
	u_int8_t buf[IAPP_MAXSIZE];
	struct hostapd_node node;
	struct ieee80211_iapp_recv {
		struct ieee80211_iapp_frame hdr;
		union {
			struct ieee80211_iapp_add_notify add;
			u_int8_t buf[1];
		} u;
	} __packed *frame;
	u_int dlt;
	int ret = 0;

	/* Ignore invalid signals */
	if (sig != EV_READ)
		return;

	/*
	 * Listen to possible messages from other IAPP
	 */
	bzero(buf, sizeof(buf));

	if ((len = recvfrom(fd, buf, sizeof(buf), 0,
	    (struct sockaddr*)&addr, &addr_len)) < 1)
		return;

	if (bcmp(&iapp->i_addr.sin_addr, &addr.sin_addr,
	    sizeof(addr.sin_addr)) == 0)
		return;

	frame = (struct ieee80211_iapp_recv*)buf;

	/* Validate the IAPP version */
	if (len < (ssize_t)sizeof(struct ieee80211_iapp_frame) ||
	    frame->hdr.i_version != IEEE80211_IAPP_VERSION ||
	    addr_len < sizeof(struct sockaddr_in))
		return;

	cfg->c_stats.cn_rx_iapp++;

	/*
	 * Process the IAPP frame
	 */
	switch (frame->hdr.i_command) {
	case IEEE80211_IAPP_FRAME_ADD_NOTIFY:
		/* Short frame */
		if (len < (ssize_t)(sizeof(struct ieee80211_iapp_frame) +
		    sizeof(struct ieee80211_iapp_add_notify)))
			return;

		/* Don't support non-48bit MAC addresses, yet */
		if (frame->u.add.a_length != IEEE80211_ADDR_LEN)
			return;

		node.ni_rxseq = frame->u.add.a_seqnum;
		bcopy(frame->u.add.a_macaddr, node.ni_macaddr,
		    IEEE80211_ADDR_LEN);

		/*
		 * Try to remove a node from our Host AP and to free
		 * any allocated resources. Otherwise the received
		 * ADD.notify message will be ignored.
		 */
		if (iapp->i_flags & HOSTAPD_IAPP_F_ADD &&
		    cfg->c_flags & HOSTAPD_CFG_F_APME) {
			TAILQ_FOREACH(apme, &cfg->c_apmes, a_entries) {
				if (iapp->i_flags & HOSTAPD_IAPP_F_ROAMING)
					(void)hostapd_roaming_del(apme, &node);
				if (iapp->i_flags & HOSTAPD_IAPP_F_ADD_NOTIFY &&
				    (ret = hostapd_apme_delnode(apme,
				    &node)) == 0)
					cfg->c_stats.cn_tx_apme++;
			}
		} else
			ret = 0;

		hostapd_log(iapp->i_flags & HOSTAPD_IAPP_F_ADD ?
		    HOSTAPD_LOG : HOSTAPD_LOG_VERBOSE,
		    "%s: %s ADD notification for %s at %s",
		    iapp->i_iface, ret == 0 ?
		    "received" : "ignored",
		    etheraddr_string(node.ni_macaddr),
		    inet_ntoa(addr.sin_addr));
		break;

	case IEEE80211_IAPP_FRAME_HOSTAPD_PCAP:
	case IEEE80211_IAPP_FRAME_HOSTAPD_RADIOTAP:
		if ((iapp->i_flags & HOSTAPD_IAPP_F_RADIOTAP) == 0)
			return;

		/* Short frame */
		if (len <= (ssize_t)sizeof(struct ieee80211_iapp_frame) ||
		    frame->hdr.i_length < sizeof(struct ieee80211_frame))
			return;

		dlt = frame->hdr.i_command ==
		    IEEE80211_IAPP_FRAME_HOSTAPD_PCAP ?
		    DLT_IEEE802_11 : DLT_IEEE802_11_RADIO;

		hostapd_print_ieee80211(dlt, 1, (u_int8_t *)frame->u.buf,
		    len - sizeof(struct ieee80211_iapp_frame));
		return;

	case IEEE80211_IAPP_FRAME_MOVE_NOTIFY:
	case IEEE80211_IAPP_FRAME_MOVE_RESPONSE:
	case IEEE80211_IAPP_FRAME_SEND_SECURITY_BLOCK:
	case IEEE80211_IAPP_FRAME_ACK_SECURITY_BLOCK:
	case IEEE80211_IAPP_FRAME_CACHE_NOTIFY:
	case IEEE80211_IAPP_FRAME_CACHE_RESPONSE:

		/*
		 * XXX TODO
		 */

		hostapd_log(HOSTAPD_LOG_VERBOSE,
		    "%s: received unsupported IAPP message %d",
		    iapp->i_iface, frame->hdr.i_command);
		return;

	default:
		return;
	}
}
