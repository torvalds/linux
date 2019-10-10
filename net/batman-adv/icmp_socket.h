/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2007-2019  B.A.T.M.A.N. contributors:
 *
 * Marek Lindner
 */

#ifndef _NET_BATMAN_ADV_ICMP_SOCKET_H_
#define _NET_BATMAN_ADV_ICMP_SOCKET_H_

#include "main.h"

#include <linux/types.h>
#include <uapi/linux/batadv_packet.h>

#define BATADV_ICMP_SOCKET "socket"

void batadv_socket_setup(struct batadv_priv *bat_priv);

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
