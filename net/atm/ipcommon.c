/* net/atm/ipcommon.c - Common items for all ways of doing IP over ATM */

/* Written 1996-2000 by Werner Almesberger, EPFL LRC/ICA */


#include <linux/module.h>
#include <linux/string.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/in.h>
#include <linux/atmdev.h>
#include <linux/atmclip.h>

#include "common.h"
#include "ipcommon.h"


#if 0
#define DPRINTK(format,args...) printk(KERN_DEBUG format,##args)
#else
#define DPRINTK(format,args...)
#endif


/*
 * skb_migrate appends the list at "from" to "to", emptying "from" in the
 * process. skb_migrate is atomic with respect to all other skb operations on
 * "from" and "to". Note that it locks both lists at the same time, so beware
 * of potential deadlocks.
 *
 * This function should live in skbuff.c or skbuff.h.
 */


void skb_migrate(struct sk_buff_head *from,struct sk_buff_head *to)
{
	unsigned long flags;
	struct sk_buff *skb_from = (struct sk_buff *) from;
	struct sk_buff *skb_to = (struct sk_buff *) to;
	struct sk_buff *prev;

	spin_lock_irqsave(&from->lock,flags);
	spin_lock(&to->lock);
	prev = from->prev;
	from->next->prev = to->prev;
	prev->next = skb_to;
	to->prev->next = from->next;
	to->prev = from->prev;
	to->qlen += from->qlen;
	spin_unlock(&to->lock);
	from->prev = skb_from;
	from->next = skb_from;
	from->qlen = 0;
	spin_unlock_irqrestore(&from->lock,flags);
}


EXPORT_SYMBOL(skb_migrate);
