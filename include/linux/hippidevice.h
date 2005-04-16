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
 *			Ross Biro, <bir7@leland.Stanford.Edu>
 *			Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *			Alan Cox, <gw4pts@gw4pts.ampr.org>
 *			Lawrence V. Stefani, <stefani@lkg.dec.com>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _LINUX_HIPPIDEVICE_H
#define _LINUX_HIPPIDEVICE_H

#include <linux/if_hippi.h>

#ifdef __KERNEL__
extern unsigned short hippi_type_trans(struct sk_buff *skb,
				       struct net_device *dev);

extern struct net_device *alloc_hippi_dev(int sizeof_priv);
#endif

#endif	/* _LINUX_HIPPIDEVICE_H */
