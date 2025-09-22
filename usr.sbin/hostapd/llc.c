/*	$OpenBSD: llc.c,v 1.7 2019/05/10 01:29:31 guenther Exp $	*/

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

#include <net/if.h>
#include <net/if_media.h>
#include <net/if_arp.h>
#include <net/if_llc.h>
#include <net/bpf.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <arpa/inet.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "hostapd.h"

void
hostapd_llc_init(struct hostapd_config *cfg)
{
	struct hostapd_iapp *iapp = &cfg->c_iapp;
	struct ifreq ifr;
	u_int i;

	iapp->i_raw = hostapd_bpf_open(O_WRONLY);
	cfg->c_flags |= HOSTAPD_CFG_F_RAW;

	bzero(&ifr, sizeof(struct ifreq));
	(void)strlcpy(ifr.ifr_name, iapp->i_iface, sizeof(ifr.ifr_name));

	/* Associate the wired network interface to the BPF descriptor */
	if (ioctl(iapp->i_raw, BIOCSETIF, &ifr) == -1)
		hostapd_fatal("failed to set BPF interface \"%s\": %s\n",
		    iapp->i_iface, strerror(errno));

	i = 1;
	if (ioctl(iapp->i_raw, BIOCSHDRCMPLT, &i) == -1)
		hostapd_fatal("failed to set BPF header completion: %s\n",
		    strerror(errno));

	/* Lock the BPF descriptor, no further configuration */
	if (ioctl(iapp->i_raw, BIOCLOCK, NULL) == -1)
		hostapd_fatal("failed to lock BPF interface on \"%s\": %s\n",
		    iapp->i_iface, strerror(errno));
}

int
hostapd_llc_send_xid(struct hostapd_config *cfg, struct hostapd_node *node)
{
	struct hostapd_iapp *iapp = &cfg->c_iapp;
	struct hostapd_llc *llc;
	u_int8_t buf[ETHER_HDR_LEN + (LLC_UFRAMELEN * 2)];

	/*
	 * Send an IEEE 802.3 LLC XID frame which will possibly force
	 * our switch port on the wired network to learn the station's
	 * new MAC address.
	 */
	bzero(&buf, sizeof(buf));
	llc = (struct hostapd_llc *)&buf;
	memset(&llc->x_hdr.ether_dhost, 0xff,
	    sizeof(llc->x_hdr.ether_dhost));
	bcopy(&node->ni_macaddr, &llc->x_hdr.ether_shost,
	    sizeof(llc->x_hdr.ether_shost));
	llc->x_hdr.ether_type = htons(sizeof(buf));
	llc->x_llc.llc_control = IAPP_LLC;
	llc->x_llc.llc_fid = IAPP_LLC_XID;
	llc->x_llc.llc_class = IAPP_LLC_CLASS;
	llc->x_llc.llc_window = IAPP_LLC_WINDOW;

	if (write(iapp->i_raw, &buf, sizeof(buf)) == -1)
		return (errno);
	return (0);
}
