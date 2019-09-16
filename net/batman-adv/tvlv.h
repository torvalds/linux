/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2007-2019  B.A.T.M.A.N. contributors:
 *
 * Marek Lindner, Simon Wunderlich
 */

#ifndef _NET_BATMAN_ADV_TVLV_H_
#define _NET_BATMAN_ADV_TVLV_H_

#include "main.h"

#include <linux/types.h>
#include <uapi/linux/batadv_packet.h>

void batadv_tvlv_container_register(struct batadv_priv *bat_priv,
				    u8 type, u8 version,
				    void *tvlv_value, u16 tvlv_value_len);
u16 batadv_tvlv_container_ogm_append(struct batadv_priv *bat_priv,
				     unsigned char **packet_buff,
				     int *packet_buff_len, int packet_min_len);
void batadv_tvlv_ogm_receive(struct batadv_priv *bat_priv,
			     struct batadv_ogm_packet *batadv_ogm_packet,
			     struct batadv_orig_node *orig_node);
void batadv_tvlv_container_unregister(struct batadv_priv *bat_priv,
				      u8 type, u8 version);

void batadv_tvlv_handler_register(struct batadv_priv *bat_priv,
				  void (*optr)(struct batadv_priv *bat_priv,
					       struct batadv_orig_node *orig,
					       u8 flags,
					       void *tvlv_value,
					       u16 tvlv_value_len),
				  int (*uptr)(struct batadv_priv *bat_priv,
					      u8 *src, u8 *dst,
					      void *tvlv_value,
					      u16 tvlv_value_len),
				  u8 type, u8 version, u8 flags);
void batadv_tvlv_handler_unregister(struct batadv_priv *bat_priv,
				    u8 type, u8 version);
int batadv_tvlv_containers_process(struct batadv_priv *bat_priv,
				   bool ogm_source,
				   struct batadv_orig_node *orig_node,
				   u8 *src, u8 *dst,
				   void *tvlv_buff, u16 tvlv_buff_len);
void batadv_tvlv_unicast_send(struct batadv_priv *bat_priv, u8 *src,
			      u8 *dst, u8 type, u8 version,
			      void *tvlv_value, u16 tvlv_value_len);

#endif /* _NET_BATMAN_ADV_TVLV_H_ */
