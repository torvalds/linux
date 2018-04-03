/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2011-2018  B.A.T.M.A.N. contributors:
 *
 * Marek Lindner, Linus Lüssing
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _NET_BATMAN_ADV_BAT_ALGO_H_
#define _NET_BATMAN_ADV_BAT_ALGO_H_

#include "main.h"

#include <linux/types.h>

struct netlink_callback;
struct seq_file;
struct sk_buff;

extern char batadv_routing_algo[];
extern struct list_head batadv_hardif_list;

void batadv_algo_init(void);
int batadv_algo_register(struct batadv_algo_ops *bat_algo_ops);
int batadv_algo_select(struct batadv_priv *bat_priv, char *name);
int batadv_algo_seq_print_text(struct seq_file *seq, void *offset);
int batadv_algo_dump(struct sk_buff *msg, struct netlink_callback *cb);

#endif /* _NET_BATMAN_ADV_BAT_ALGO_H_ */
