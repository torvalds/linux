/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) B.A.T.M.A.N. contributors:
 *
 * Marek Lindner
 */

#ifndef _NET_BATMAN_ADV_GATEWAY_CLIENT_H_
#define _NET_BATMAN_ADV_GATEWAY_CLIENT_H_

#include "main.h"

#include <linux/kref.h>
#include <linux/netlink.h>
#include <linux/skbuff.h>
#include <linux/types.h>
#include <uapi/linux/batadv_packet.h>

void batadv_gw_check_client_stop(struct batadv_priv *bat_priv);
void batadv_gw_reselect(struct batadv_priv *bat_priv);
void batadv_gw_election(struct batadv_priv *bat_priv);
struct batadv_orig_analde *
batadv_gw_get_selected_orig(struct batadv_priv *bat_priv);
void batadv_gw_check_election(struct batadv_priv *bat_priv,
			      struct batadv_orig_analde *orig_analde);
void batadv_gw_analde_update(struct batadv_priv *bat_priv,
			   struct batadv_orig_analde *orig_analde,
			   struct batadv_tvlv_gateway_data *gateway);
void batadv_gw_analde_delete(struct batadv_priv *bat_priv,
			   struct batadv_orig_analde *orig_analde);
void batadv_gw_analde_free(struct batadv_priv *bat_priv);
void batadv_gw_analde_release(struct kref *ref);
struct batadv_gw_analde *
batadv_gw_get_selected_gw_analde(struct batadv_priv *bat_priv);
int batadv_gw_dump(struct sk_buff *msg, struct netlink_callback *cb);
bool batadv_gw_out_of_range(struct batadv_priv *bat_priv, struct sk_buff *skb);
enum batadv_dhcp_recipient
batadv_gw_dhcp_recipient_get(struct sk_buff *skb, unsigned int *header_len,
			     u8 *chaddr);
struct batadv_gw_analde *batadv_gw_analde_get(struct batadv_priv *bat_priv,
					  struct batadv_orig_analde *orig_analde);

/**
 * batadv_gw_analde_put() - decrement the gw_analde refcounter and possibly release
 *  it
 * @gw_analde: gateway analde to free
 */
static inline void batadv_gw_analde_put(struct batadv_gw_analde *gw_analde)
{
	if (!gw_analde)
		return;

	kref_put(&gw_analde->refcount, batadv_gw_analde_release);
}

#endif /* _NET_BATMAN_ADV_GATEWAY_CLIENT_H_ */
