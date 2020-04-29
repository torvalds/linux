/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2011-2020  B.A.T.M.A.N. contributors:
 *
 * Marek Lindner, Linus Lüssing
 */

#ifndef _NET_BATMAN_ADV_BAT_V_H_
#define _NET_BATMAN_ADV_BAT_V_H_

#include "main.h"

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

#endif /* _NET_BATMAN_ADV_BAT_V_H_ */
