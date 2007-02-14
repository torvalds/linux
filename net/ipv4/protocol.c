/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		INET protocol dispatch tables.
 *
 * Version:	$Id: protocol.c,v 1.14 2001/05/18 02:25:49 davem Exp $
 *
 * Authors:	Ross Biro
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 * Fixes:
 *		Alan Cox	: Ahah! udp icmp errors don't work because
 *				  udp_err is never called!
 *		Alan Cox	: Added new fields for init and ready for
 *				  proper fragmentation (_NO_ 4K limits!)
 *		Richard Colella	: Hang on hash collision
 *		Vince Laviano	: Modified inet_del_protocol() to correctly
 *				  maintain copy bit.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/timer.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/icmp.h>
#include <net/udp.h>
#include <net/ipip.h>
#include <linux/igmp.h>

struct net_protocol *inet_protos[MAX_INET_PROTOS];
static DEFINE_SPINLOCK(inet_proto_lock);

/*
 *	Add a protocol handler to the hash tables
 */

int inet_add_protocol(struct net_protocol *prot, unsigned char protocol)
{
	int hash, ret;

	hash = protocol & (MAX_INET_PROTOS - 1);

	spin_lock_bh(&inet_proto_lock);
	if (inet_protos[hash]) {
		ret = -1;
	} else {
		inet_protos[hash] = prot;
		ret = 0;
	}
	spin_unlock_bh(&inet_proto_lock);

	return ret;
}

/*
 *	Remove a protocol from the hash tables.
 */

int inet_del_protocol(struct net_protocol *prot, unsigned char protocol)
{
	int hash, ret;

	hash = protocol & (MAX_INET_PROTOS - 1);

	spin_lock_bh(&inet_proto_lock);
	if (inet_protos[hash] == prot) {
		inet_protos[hash] = NULL;
		ret = 0;
	} else {
		ret = -1;
	}
	spin_unlock_bh(&inet_proto_lock);

	synchronize_net();

	return ret;
}

EXPORT_SYMBOL(inet_add_protocol);
EXPORT_SYMBOL(inet_del_protocol);
