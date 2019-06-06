/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the HIPPI handlers.
 *
 * Version:	@(#)hippidevice.h	1.0.0	05/26/97
 *
 * Author:	Jes Sorensen, <Jes.Sorensen@cern.ch>
 *
 *		hippidevice.h is based on previous fddidevice.h work by
 *			Ross Biro
 *			Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *			Alan Cox, <gw4pts@gw4pts.ampr.org>
 *			Lawrence V. Stefani, <stefani@lkg.dec.com>
 */
#ifndef _LINUX_HIPPIDEVICE_H
#define _LINUX_HIPPIDEVICE_H

#include <linux/if_hippi.h>

#ifdef __KERNEL__

struct hippi_cb {
	__u32	ifield;
};

__be16 hippi_type_trans(struct sk_buff *skb, struct net_device *dev);
int hippi_mac_addr(struct net_device *dev, void *p);
int hippi_neigh_setup_dev(struct net_device *dev, struct neigh_parms *p);
struct net_device *alloc_hippi_dev(int sizeof_priv);
#endif

#endif	/* _LINUX_HIPPIDEVICE_H */
