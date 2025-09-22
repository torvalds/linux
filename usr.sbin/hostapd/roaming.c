/*	$OpenBSD: roaming.c,v 1.8 2019/06/28 13:32:47 deraadt Exp $	*/

/*
 * Copyright (c) 2005, 2006 Reyk Floeter <reyk@openbsd.org>
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

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/tree.h>
#include <sys/ioctl.h>

#include <net/if.h>
#include <net/if_media.h>
#include <net/if_arp.h>
#include <net/if_llc.h>
#include <net/route.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <arpa/inet.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>

#include "hostapd.h"

int	 hostapd_roaming_addr(struct hostapd_apme *, struct hostapd_inaddr *, int);
int	 hostapd_roaming_rt(struct hostapd_apme *, struct hostapd_inaddr *, int);

void
hostapd_roaming_init(struct hostapd_config *cfg)
{
	struct hostapd_iapp *iapp = &cfg->c_iapp;
	struct hostapd_apme *apme;
	struct ifreq ifr;
	int v;

	if ((cfg->c_flags & HOSTAPD_CFG_F_APME) == 0 ||
	    (iapp->i_flags & HOSTAPD_IAPP_F_ROAMING_ROUTE) == 0)
		return;

	if ((cfg->c_rtsock = socket(AF_ROUTE, SOCK_RAW, AF_INET)) == -1)
		hostapd_fatal("failed to init inet socket: %s\n",
		    strerror(errno));

	v = 0;
	if (setsockopt(cfg->c_rtsock, SOL_SOCKET, SO_USELOOPBACK,
	    &v, sizeof(v)) == -1)
		hostapd_fatal("failed to setup inet socket: %s\n",
		    strerror(errno));

	TAILQ_FOREACH(apme, &cfg->c_apmes, a_entries) {
		bzero(&ifr, sizeof(ifr));
		(void)strlcpy(ifr.ifr_name, apme->a_iface, sizeof(ifr.ifr_name));
		if (ioctl(cfg->c_apme_ctl, SIOCGIFADDR, &ifr) == -1)
			hostapd_fatal("ioctl %s on \"%s\" failed: %s\n",
			    "SIOCGIFADDR", ifr.ifr_name, strerror(errno));
		bcopy(&ifr.ifr_addr, &apme->a_addr,
		    sizeof(struct sockaddr_in));
		hostapd_log(HOSTAPD_LOG_VERBOSE,
		    "%s/%s: using gateway address %s",
		    apme->a_iface, iapp->i_iface,
		    inet_ntoa(apme->a_addr.sin_addr), apme->a_iface);
	}
}

void
hostapd_roaming_term(struct hostapd_apme *apme)
{
	struct hostapd_config *cfg = (struct hostapd_config *)apme->a_cfg;
	struct hostapd_iapp *iapp = &cfg->c_iapp;
	struct hostapd_entry *entry;

	if (iapp->i_flags & HOSTAPD_IAPP_F_ROAMING_ROUTE &&
	    iapp->i_route_tbl != NULL) {
		RB_FOREACH(entry, hostapd_tree, &iapp->i_route_tbl->t_tree) {
			if ((entry->e_flags & HOSTAPD_ENTRY_F_INADDR) == 0)
				continue;
			(void)hostapd_roaming_rt(apme, &entry->e_inaddr, 0);
		}
	}

	if (iapp->i_flags & HOSTAPD_IAPP_F_ROAMING_ADDRESS &&
	    iapp->i_addr_tbl != NULL) {
		RB_FOREACH(entry, hostapd_tree, &iapp->i_addr_tbl->t_tree) {
			if ((entry->e_flags & HOSTAPD_ENTRY_F_INADDR) == 0)
				continue;
			(void)hostapd_roaming_addr(apme, &entry->e_inaddr, 0);
		}
	}
}

int
hostapd_roaming(struct hostapd_apme *apme, struct hostapd_node *node, int add)
{
	struct hostapd_config *cfg = (struct hostapd_config *)apme->a_cfg;
	struct hostapd_iapp *iapp = &cfg->c_iapp;
	struct hostapd_entry *entry;
	int ret;

	if (iapp->i_flags & HOSTAPD_IAPP_F_ROAMING_ADDRESS &&
	    iapp->i_addr_tbl != NULL) {
		if ((entry = hostapd_entry_lookup(iapp->i_addr_tbl,
		    node->ni_macaddr)) == NULL ||
		    (entry->e_flags & HOSTAPD_ENTRY_F_INADDR) == 0)
			return (ESRCH);
		if ((ret = hostapd_roaming_addr(apme, &entry->e_inaddr,
		    add)) != 0)
			return (ret);
	}

	if (iapp->i_flags & HOSTAPD_IAPP_F_ROAMING_ROUTE &&
	    iapp->i_route_tbl != NULL) {
		if ((entry = hostapd_entry_lookup(iapp->i_addr_tbl,
		    node->ni_macaddr)) == NULL ||
		    (entry->e_flags & HOSTAPD_ENTRY_F_INADDR) == 0)
			return (ESRCH);
		if ((ret = hostapd_roaming_rt(apme, &entry->e_inaddr,
		    add)) != 0)
			return (ret);
	}

	return (0);
}


int
hostapd_roaming_addr(struct hostapd_apme *apme, struct hostapd_inaddr *addr,
    int add)
{
	struct hostapd_config *cfg = (struct hostapd_config *)apme->a_cfg;
	struct hostapd_iapp *iapp = &cfg->c_iapp;
	struct sockaddr_in *ifaddr, *ifmask, *ifbroadaddr;
	struct ifaliasreq ifra;

	bzero(&ifra, sizeof(ifra));

	ifaddr = (struct sockaddr_in *)&ifra.ifra_addr;
	ifaddr->sin_family = AF_INET;
	ifaddr->sin_len = sizeof(struct sockaddr_in);
	ifaddr->sin_addr.s_addr = addr->in_v4.s_addr;

	ifbroadaddr = (struct sockaddr_in *)&ifra.ifra_broadaddr;
	ifbroadaddr->sin_family = AF_INET;
	ifbroadaddr->sin_len = sizeof(struct sockaddr_in);
	ifbroadaddr->sin_addr.s_addr =
	    addr->in_v4.s_addr | htonl(0xffffffffUL >> addr->in_netmask);

	if (add) {
		ifmask = (struct sockaddr_in *)&ifra.ifra_mask;
		ifmask->sin_family = AF_INET;
		ifmask->sin_len = sizeof(struct sockaddr_in);
		ifmask->sin_addr.s_addr =
		    htonl(0xffffffff << (32 - addr->in_netmask));
	}

	(void)strlcpy(ifra.ifra_name, apme->a_iface, sizeof(ifra.ifra_name));
	if (ioctl(cfg->c_apme_ctl, SIOCDIFADDR, &ifra) == -1) {
		if (errno != EADDRNOTAVAIL) {
			hostapd_log(HOSTAPD_LOG_VERBOSE,
			    "%s/%s: failed to delete address %s",
			    apme->a_iface, iapp->i_iface,
			    inet_ntoa(addr->in_v4));
			return (errno);
		}
	}
	if (add && ioctl(cfg->c_apme_ctl, SIOCAIFADDR, &ifra) == -1) {
		if (errno != EEXIST) {
			hostapd_log(HOSTAPD_LOG_VERBOSE,
			    "%s/%s: failed to add address %s",
			    apme->a_iface, iapp->i_iface,
			    inet_ntoa(addr->in_v4));
			return (errno);
		}
	}

	hostapd_log(HOSTAPD_LOG_VERBOSE,
	    "%s/%s: %s address %s",
	    apme->a_iface, iapp->i_iface,
	    add ? "added" : "deleted",
	    inet_ntoa(addr->in_v4));

	return (0);
}

int
hostapd_roaming_rt(struct hostapd_apme *apme, struct hostapd_inaddr *addr,
    int add)
{
	struct hostapd_config *cfg = (struct hostapd_config *)apme->a_cfg;
	struct hostapd_iapp *iapp = &cfg->c_iapp;
	struct {
		struct rt_msghdr	rm_hdr;
		struct sockaddr_in	rm_dst;
		struct sockaddr_in	rm_gateway;
		struct sockaddr_in	rm_netmask;
		struct sockaddr_rtlabel	rm_label;
	} rm;
	size_t len = sizeof(rm);

	bzero(&rm, len);

	rm.rm_hdr.rtm_msglen = len;
	rm.rm_hdr.rtm_version = RTM_VERSION;
	rm.rm_hdr.rtm_type = add ? RTM_CHANGE : RTM_DELETE;
	rm.rm_hdr.rtm_flags = RTF_STATIC;
	rm.rm_hdr.rtm_seq = cfg->c_rtseq++;
	rm.rm_hdr.rtm_addrs = RTA_DST | RTA_GATEWAY | RTA_LABEL;
	rm.rm_hdr.rtm_hdrlen = sizeof(struct rt_msghdr);

	rm.rm_dst.sin_family = AF_INET;
	rm.rm_dst.sin_len = sizeof(rm.rm_dst);
	rm.rm_dst.sin_addr.s_addr = addr->in_v4.s_addr;

	rm.rm_gateway.sin_family = AF_INET;
	rm.rm_gateway.sin_len = sizeof(rm.rm_gateway);
	rm.rm_gateway.sin_addr.s_addr = apme->a_addr.sin_addr.s_addr;

	rm.rm_hdr.rtm_addrs |= RTA_NETMASK;
	rm.rm_netmask.sin_len = sizeof(rm.rm_netmask);
	rm.rm_netmask.sin_family = AF_INET;
	if (addr->in_netmask)
		rm.rm_netmask.sin_addr.s_addr =
		    htonl(0xffffffff << (32 - addr->in_netmask));
	else if (addr->in_netmask < 0)
		rm.rm_hdr.rtm_flags |= RTF_HOST;

	rm.rm_label.sr_len = sizeof(rm.rm_label);
	if (snprintf(rm.rm_label.sr_label, sizeof(rm.rm_label.sr_label),
	    "apme-%s", apme->a_iface) == -1)
		goto bad;

 retry:
	if (write(cfg->c_rtsock, &rm, len) == -1) {
		switch (errno) {
		case ESRCH:
			if (rm.rm_hdr.rtm_type == RTM_CHANGE) {
				rm.rm_hdr.rtm_type = RTM_ADD;
				goto retry;
			} else if (rm.rm_hdr.rtm_type == RTM_DELETE) {
				/* Ignore */
				break;
			}
			/* FALLTHROUGH */
		default:
			goto bad;
		}
	}

	hostapd_log(HOSTAPD_LOG_VERBOSE,
	    "%s/%s: %s route to %s",
	    apme->a_iface, iapp->i_iface,
	    add ? "added" : "deleted",
	    inet_ntoa(addr->in_v4));

	return (0);

 bad:
	hostapd_log(HOSTAPD_LOG_VERBOSE,
	    "%s/%s: failed to %s route to %s: %s",
	    apme->a_iface, iapp->i_iface,
	    add ? "add" : "delete",
	    inet_ntoa(addr->in_v4),
	    strerror(errno));

	return (ESRCH);
}

int
hostapd_roaming_add(struct hostapd_apme *apme, struct hostapd_node *node)
{
	return (hostapd_priv_roaming(apme, node, 1));
}

int
hostapd_roaming_del(struct hostapd_apme *apme, struct hostapd_node *node)
{
	return (hostapd_priv_roaming(apme, node, 0));
}

