/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2011-2020  B.A.T.M.A.N. contributors:
 *
 * Marek Lindner, Linus Lüssing
 */

#ifndef _NET_BATMAN_ADV_BAT_ALGO_H_
#define _NET_BATMAN_ADV_BAT_ALGO_H_

#include "main.h"

#include <linux/netlink.h>
#include <linux/skbuff.h>
#include <linux/types.h>

extern char batadv_routing_algo[];
extern struct list_head batadv_hardif_list;

void batadv_algo_init(void);
struct batadv_algo_ops *batadv_algo_get(const char *name);
int batadv_algo_register(struct batadv_algo_ops *bat_algo_ops);
int batadv_algo_select(struct batadv_priv *bat_priv, const char *name);
int batadv_algo_dump(struct sk_buff *msg, struct netlink_callback *cb);

#endif /* _NET_BATMAN_ADV_BAT_ALGO_H_ */
