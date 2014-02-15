/* Copyright (C) 2014 B.A.T.M.A.N. contributors:
 *
 * Linus Lüssing
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

#ifndef _NET_BATMAN_ADV_MULTICAST_H_
#define _NET_BATMAN_ADV_MULTICAST_H_

#ifdef CONFIG_BATMAN_ADV_MCAST

void batadv_mcast_mla_update(struct batadv_priv *bat_priv);

void batadv_mcast_free(struct batadv_priv *bat_priv);

#else

static inline void batadv_mcast_mla_update(struct batadv_priv *bat_priv)
{
	return;
}

static inline void batadv_mcast_free(struct batadv_priv *bat_priv)
{
	return;
}

#endif /* CONFIG_BATMAN_ADV_MCAST */

#endif /* _NET_BATMAN_ADV_MULTICAST_H_ */
