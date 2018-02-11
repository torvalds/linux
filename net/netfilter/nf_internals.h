/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NF_INTERNALS_H
#define _NF_INTERNALS_H

#include <linux/list.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>

/* nf_queue.c */
int nf_queue(struct sk_buff *skb, struct nf_hook_state *state,
	     const struct nf_hook_entries *entries, unsigned int index,
	     unsigned int verdict);
unsigned int nf_queue_nf_hook_drop(struct net *net);

/* nf_log.c */
int __init netfilter_log_init(void);

#endif
