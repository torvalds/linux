/* net/atm/atmarp.h - RFC1577 ATM ARP */
 
/* Written 1995-2000 by Werner Almesberger, EPFL LRC/ICA */
 
 
#ifndef _ATMCLIP_H
#define _ATMCLIP_H

#include <linux/netdevice.h>
#include <linux/atm.h>
#include <linux/atmdev.h>
#include <linux/atmarp.h>
#include <linux/spinlock.h>
#include <net/neighbour.h>


#define CLIP_VCC(vcc) ((struct clip_vcc *) ((vcc)->user_back))
#define NEIGH2ENTRY(neigh) ((struct atmarp_entry *) (neigh)->primary_key)

struct sk_buff;

struct clip_vcc {
	struct atm_vcc	*vcc;		/* VCC descriptor */
	struct atmarp_entry *entry;	/* ATMARP table entry, NULL if IP addr.
					   isn't known yet */
	int		xoff;		/* 1 if send buffer is full */
	unsigned char	encap;		/* 0: NULL, 1: LLC/SNAP */
	unsigned long	last_use;	/* last send or receive operation */
	unsigned long	idle_timeout;	/* keep open idle for so many jiffies*/
	void (*old_push)(struct atm_vcc *vcc,struct sk_buff *skb);
					/* keep old push fn for chaining */
	void (*old_pop)(struct atm_vcc *vcc,struct sk_buff *skb);
					/* keep old pop fn for chaining */
	struct clip_vcc	*next;		/* next VCC */
};


struct atmarp_entry {
	__be32		ip;		/* IP address */
	struct clip_vcc	*vccs;		/* active VCCs; NULL if resolution is
					   pending */
	unsigned long	expires;	/* entry expiration time */
	struct neighbour *neigh;	/* neighbour back-pointer */
};


#define PRIV(dev) ((struct clip_priv *) netdev_priv(dev))


struct clip_priv {
	int number;			/* for convenience ... */
	spinlock_t xoff_lock;		/* ensures that pop is atomic (SMP) */
	struct net_device *next;	/* next CLIP interface */
};


extern struct neigh_table *clip_tbl_hook;

#endif
