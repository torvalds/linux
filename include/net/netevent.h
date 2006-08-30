#ifndef _NET_EVENT_H
#define _NET_EVENT_H

/*
 *	Generic netevent notifiers
 *
 *	Authors:
 *      Tom Tucker              <tom@opengridcomputing.com>
 *      Steve Wise              <swise@opengridcomputing.com>
 *
 * 	Changes:
 */
#ifdef __KERNEL__

#include <net/dst.h>

struct netevent_redirect {
	struct dst_entry *old;
	struct dst_entry *new;
};

enum netevent_notif_type {
	NETEVENT_NEIGH_UPDATE = 1, /* arg is struct neighbour ptr */
	NETEVENT_PMTU_UPDATE,	   /* arg is struct dst_entry ptr */
	NETEVENT_REDIRECT,	   /* arg is struct netevent_redirect ptr */
};

extern int register_netevent_notifier(struct notifier_block *nb);
extern int unregister_netevent_notifier(struct notifier_block *nb);
extern int call_netevent_notifiers(unsigned long val, void *v);

#endif
#endif
