/* Copyright (C) 2012-2013 B.A.T.M.A.N. contributors:
 *
 * Martin Hundebøll, Jeppe Ledet-Pedersen
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 */

#ifndef _NET_BATMAN_ADV_NETWORK_CODING_H_
#define _NET_BATMAN_ADV_NETWORK_CODING_H_

#ifdef CONFIG_BATMAN_ADV_NC

int batadv_nc_init(struct batadv_priv *bat_priv);
void batadv_nc_free(struct batadv_priv *bat_priv);
void batadv_nc_init_bat_priv(struct batadv_priv *bat_priv);

#else /* ifdef CONFIG_BATMAN_ADV_NC */

static inline int batadv_nc_init(struct batadv_priv *bat_priv)
{
	return 0;
}

static inline void batadv_nc_free(struct batadv_priv *bat_priv)
{
	return;
}

static inline void batadv_nc_init_bat_priv(struct batadv_priv *bat_priv)
{
	return;
}

#endif /* ifdef CONFIG_BATMAN_ADV_NC */

#endif /* _NET_BATMAN_ADV_NETWORK_CODING_H_ */
