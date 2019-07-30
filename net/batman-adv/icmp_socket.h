/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2007-2019  B.A.T.M.A.N. contributors:
 *
 * Marek Lindner
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

#ifndef _NET_BATMAN_ADV_ICMP_SOCKET_H_
#define _NET_BATMAN_ADV_ICMP_SOCKET_H_

#include "main.h"

#include <linux/types.h>

struct batadv_icmp_header;

#define BATADV_ICMP_SOCKET "socket"

int batadv_socket_setup(struct batadv_priv *bat_priv);

#ifdef CONFIG_BATMAN_ADV_DEBUGFS

void batadv_socket_init(void);
void batadv_socket_receive_packet(struct batadv_icmp_header *icmph,
				  size_t icmp_len);

#else

static inline void batadv_socket_init(void)
{
}

static inline void
batadv_socket_receive_packet(struct batadv_icmp_header *icmph, size_t icmp_len)
{
}

#endif

#endif /* _NET_BATMAN_ADV_ICMP_SOCKET_H_ */
