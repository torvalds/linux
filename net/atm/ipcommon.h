/* net/atm/ipcommon.h - Common items for all ways of doing IP over ATM */

/* Written 1996-2000 by Werner Almesberger, EPFL LRC/ICA */


#ifndef NET_ATM_IPCOMMON_H
#define NET_ATM_IPCOMMON_H


#include <linux/string.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/atmdev.h>

/*
 * Appends all skbs from "from" to "to". The operation is atomic with respect
 * to all other skb operations on "from" or "to".
 */

void skb_migrate(struct sk_buff_head *from,struct sk_buff_head *to);

#endif
