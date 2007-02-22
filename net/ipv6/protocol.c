/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		PF_INET6 protocol dispatch tables.
 *
 * Version:	$Id: protocol.c,v 1.10 2001/05/18 02:25:49 davem Exp $
 *
 * Authors:	Pedro Roque	<roque@di.fc.ul.pt>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

/*
 *      Changes:
 *
 *      Vince Laviano (vince@cs.stanford.edu)       16 May 2001
 *      - Removed unused variable 'inet6_protocol_base'
 *      - Modified inet6_del_protocol() to correctly maintain copy bit.
 */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/in6.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>

#include <net/sock.h>
#include <net/snmp.h>

#include <net/ipv6.h>
#include <net/protocol.h>

struct inet6_protocol *inet6_protos[MAX_INET_PROTOS];
static DEFINE_SPINLOCK(inet6_proto_lock);


int inet6_add_protocol(struct inet6_protocol *prot, unsigned char protocol)
{
	int ret, hash = protocol & (MAX_INET_PROTOS - 1);

	spin_lock_bh(&inet6_proto_lock);

	if (inet6_protos[hash]) {
		ret = -1;
	} else {
		inet6_protos[hash] = prot;
		ret = 0;
	}

	spin_unlock_bh(&inet6_proto_lock);

	return ret;
}

EXPORT_SYMBOL(inet6_add_protocol);

/*
 *	Remove a protocol from the hash tables.
 */

int inet6_del_protocol(struct inet6_protocol *prot, unsigned char protocol)
{
	int ret, hash = protocol & (MAX_INET_PROTOS - 1);

	spin_lock_bh(&inet6_proto_lock);

	if (inet6_protos[hash] != prot) {
		ret = -1;
	} else {
		inet6_protos[hash] = NULL;
		ret = 0;
	}

	spin_unlock_bh(&inet6_proto_lock);

	synchronize_net();

	return ret;
}

EXPORT_SYMBOL(inet6_del_protocol);
