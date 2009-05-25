/*
 * Moved here from drivers/net/net_init.c, which is:
 *	Written 1993,1994,1995 by Donald Becker.
 */

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/if_ltalk.h>

static void ltalk_setup(struct net_device *dev)
{
	/* Fill in the fields of the device structure with localtalk-generic values. */

	dev->type		= ARPHRD_LOCALTLK;
	dev->hard_header_len 	= LTALK_HLEN;
	dev->mtu		= LTALK_MTU;
	dev->addr_len		= LTALK_ALEN;
	dev->tx_queue_len	= 10;

	dev->broadcast[0]	= 0xFF;

	dev->flags		= IFF_BROADCAST|IFF_MULTICAST|IFF_NOARP;
}

/**
 * alloc_ltalkdev - Allocates and sets up an localtalk device
 * @sizeof_priv: Size of additional driver-private structure to be allocated
 *	for this localtalk device
 *
 * Fill in the fields of the device structure with localtalk-generic
 * values. Basically does everything except registering the device.
 *
 * Constructs a new net device, complete with a private data area of
 * size @sizeof_priv.  A 32-byte (not bit) alignment is enforced for
 * this private data area.
 */

struct net_device *alloc_ltalkdev(int sizeof_priv)
{
	return alloc_netdev(sizeof_priv, "lt%d", ltalk_setup);
}
EXPORT_SYMBOL(alloc_ltalkdev);
