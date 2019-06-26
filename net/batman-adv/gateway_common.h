/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2009-2019  B.A.T.M.A.N. contributors:
 *
 * Marek Lindner
 */

#ifndef _NET_BATMAN_ADV_GATEWAY_COMMON_H_
#define _NET_BATMAN_ADV_GATEWAY_COMMON_H_

#include "main.h"

#include <linux/types.h>

struct net_device;

/**
 * enum batadv_bandwidth_units - bandwidth unit types
 */
enum batadv_bandwidth_units {
	/** @BATADV_BW_UNIT_KBIT: unit type kbit */
	BATADV_BW_UNIT_KBIT,

	/** @BATADV_BW_UNIT_MBIT: unit type mbit */
	BATADV_BW_UNIT_MBIT,
};

#define BATADV_GW_MODE_OFF_NAME	"off"
#define BATADV_GW_MODE_CLIENT_NAME	"client"
#define BATADV_GW_MODE_SERVER_NAME	"server"

ssize_t batadv_gw_bandwidth_set(struct net_device *net_dev, char *buff,
				size_t count);
void batadv_gw_tvlv_container_update(struct batadv_priv *bat_priv);
void batadv_gw_init(struct batadv_priv *bat_priv);
void batadv_gw_free(struct batadv_priv *bat_priv);
bool batadv_parse_throughput(struct net_device *net_dev, char *buff,
			     const char *description, u32 *throughput);

#endif /* _NET_BATMAN_ADV_GATEWAY_COMMON_H_ */
