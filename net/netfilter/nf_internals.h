/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NF_INTERNALS_H
#define _NF_INTERNALS_H

#include <linux/list.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>

/* nf_queue.c */
void nf_queue_nf_hook_drop(struct net *net);

/* nf_log.c */
int __init netfilter_log_init(void);

/* core.c */
void nf_hook_entries_delete_raw(struct nf_hook_entries __rcu **pp,
				const struct nf_hook_ops *reg);
int nf_hook_entries_insert_raw(struct nf_hook_entries __rcu **pp,
				const struct nf_hook_ops *reg);
#endif
