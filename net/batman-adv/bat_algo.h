/* Copyright (C) 2011-2016  B.A.T.M.A.N. contributors:
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

struct seq_file;

int batadv_iv_init(void);

extern char batadv_routing_algo[];
extern struct list_head batadv_hardif_list;

void batadv_algo_init(void);
int batadv_algo_register(struct batadv_algo_ops *bat_algo_ops);
int batadv_algo_select(struct batadv_priv *bat_priv, char *name);
int batadv_algo_seq_print_text(struct seq_file *seq, void *offset);

#ifdef CONFIG_BATMAN_ADV_BATMAN_V

int batadv_v_init(void);
void batadv_v_hardif_init(struct batadv_hard_iface *hardif);
int batadv_v_mesh_init(struct batadv_priv *bat_priv);
void batadv_v_mesh_free(struct batadv_priv *bat_priv);

#else

static inline int batadv_v_init(void)
{
	return 0;
}

static inline void batadv_v_hardif_init(struct batadv_hard_iface *hardif)
{
}

static inline int batadv_v_mesh_init(struct batadv_priv *bat_priv)
{
	return 0;
}

static inline void batadv_v_mesh_free(struct batadv_priv *bat_priv)
{
}

#endif /* CONFIG_BATMAN_ADV_BATMAN_V */

#endif /* _NET_BATMAN_ADV_BAT_ALGO_H_ */
