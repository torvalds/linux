/* Copyright (C) 2009-2015 B.A.T.M.A.N. contributors:
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

#include "gateway_common.h"
#include "main.h"

#include <linux/atomic.h>
#include <linux/byteorder/generic.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/stddef.h>
#include <linux/string.h>

#include "gateway_client.h"
#include "packet.h"

/**
 * batadv_parse_gw_bandwidth - parse supplied string buffer to extract download
 *  and upload bandwidth information
 * @net_dev: the soft interface net device
 * @buff: string buffer to parse
 * @down: pointer holding the returned download bandwidth information
 * @up: pointer holding the returned upload bandwidth information
 *
 * Returns false on parse error and true otherwise.
 */
static bool batadv_parse_gw_bandwidth(struct net_device *net_dev, char *buff,
				      uint32_t *down, uint32_t *up)
{
	enum batadv_bandwidth_units bw_unit_type = BATADV_BW_UNIT_KBIT;
	char *slash_ptr, *tmp_ptr;
	long ldown, lup;
	int ret;

	slash_ptr = strchr(buff, '/');
	if (slash_ptr)
		*slash_ptr = 0;

	if (strlen(buff) > 4) {
		tmp_ptr = buff + strlen(buff) - 4;

		if (strncasecmp(tmp_ptr, "mbit", 4) == 0)
			bw_unit_type = BATADV_BW_UNIT_MBIT;

		if ((strncasecmp(tmp_ptr, "kbit", 4) == 0) ||
		    (bw_unit_type == BATADV_BW_UNIT_MBIT))
			*tmp_ptr = '\0';
	}

	ret = kstrtol(buff, 10, &ldown);
	if (ret) {
		batadv_err(net_dev,
			   "Download speed of gateway mode invalid: %s\n",
			   buff);
		return false;
	}

	switch (bw_unit_type) {
	case BATADV_BW_UNIT_MBIT:
		*down = ldown * 10;
		break;
	case BATADV_BW_UNIT_KBIT:
	default:
		*down = ldown / 100;
		break;
	}

	/* we also got some upload info */
	if (slash_ptr) {
		bw_unit_type = BATADV_BW_UNIT_KBIT;

		if (strlen(slash_ptr + 1) > 4) {
			tmp_ptr = slash_ptr + 1 - 4 + strlen(slash_ptr + 1);

			if (strncasecmp(tmp_ptr, "mbit", 4) == 0)
				bw_unit_type = BATADV_BW_UNIT_MBIT;

			if ((strncasecmp(tmp_ptr, "kbit", 4) == 0) ||
			    (bw_unit_type == BATADV_BW_UNIT_MBIT))
				*tmp_ptr = '\0';
		}

		ret = kstrtol(slash_ptr + 1, 10, &lup);
		if (ret) {
			batadv_err(net_dev,
				   "Upload speed of gateway mode invalid: %s\n",
				   slash_ptr + 1);
			return false;
		}

		switch (bw_unit_type) {
		case BATADV_BW_UNIT_MBIT:
			*up = lup * 10;
			break;
		case BATADV_BW_UNIT_KBIT:
		default:
			*up = lup / 100;
			break;
		}
	}

	return true;
}

/**
 * batadv_gw_tvlv_container_update - update the gw tvlv container after gateway
 *  setting change
 * @bat_priv: the bat priv with all the soft interface information
 */
void batadv_gw_tvlv_container_update(struct batadv_priv *bat_priv)
{
	struct batadv_tvlv_gateway_data gw;
	uint32_t down, up;
	char gw_mode;

	gw_mode = atomic_read(&bat_priv->gw_mode);

	switch (gw_mode) {
	case BATADV_GW_MODE_OFF:
	case BATADV_GW_MODE_CLIENT:
		batadv_tvlv_container_unregister(bat_priv, BATADV_TVLV_GW, 1);
		break;
	case BATADV_GW_MODE_SERVER:
		down = atomic_read(&bat_priv->gw.bandwidth_down);
		up = atomic_read(&bat_priv->gw.bandwidth_up);
		gw.bandwidth_down = htonl(down);
		gw.bandwidth_up = htonl(up);
		batadv_tvlv_container_register(bat_priv, BATADV_TVLV_GW, 1,
					       &gw, sizeof(gw));
		break;
	}
}

ssize_t batadv_gw_bandwidth_set(struct net_device *net_dev, char *buff,
				size_t count)
{
	struct batadv_priv *bat_priv = netdev_priv(net_dev);
	uint32_t down_curr, up_curr, down_new = 0, up_new = 0;
	bool ret;

	down_curr = (unsigned int)atomic_read(&bat_priv->gw.bandwidth_down);
	up_curr = (unsigned int)atomic_read(&bat_priv->gw.bandwidth_up);

	ret = batadv_parse_gw_bandwidth(net_dev, buff, &down_new, &up_new);
	if (!ret)
		goto end;

	if (!down_new)
		down_new = 1;

	if (!up_new)
		up_new = down_new / 5;

	if (!up_new)
		up_new = 1;

	if ((down_curr == down_new) && (up_curr == up_new))
		return count;

	batadv_gw_reselect(bat_priv);
	batadv_info(net_dev,
		    "Changing gateway bandwidth from: '%u.%u/%u.%u MBit' to: '%u.%u/%u.%u MBit'\n",
		    down_curr / 10, down_curr % 10, up_curr / 10, up_curr % 10,
		    down_new / 10, down_new % 10, up_new / 10, up_new % 10);

	atomic_set(&bat_priv->gw.bandwidth_down, down_new);
	atomic_set(&bat_priv->gw.bandwidth_up, up_new);
	batadv_gw_tvlv_container_update(bat_priv);

end:
	return count;
}

/**
 * batadv_gw_tvlv_ogm_handler_v1 - process incoming gateway tvlv container
 * @bat_priv: the bat priv with all the soft interface information
 * @orig: the orig_node of the ogm
 * @flags: flags indicating the tvlv state (see batadv_tvlv_handler_flags)
 * @tvlv_value: tvlv buffer containing the gateway data
 * @tvlv_value_len: tvlv buffer length
 */
static void batadv_gw_tvlv_ogm_handler_v1(struct batadv_priv *bat_priv,
					  struct batadv_orig_node *orig,
					  uint8_t flags,
					  void *tvlv_value,
					  uint16_t tvlv_value_len)
{
	struct batadv_tvlv_gateway_data gateway, *gateway_ptr;

	/* only fetch the tvlv value if the handler wasn't called via the
	 * CIFNOTFND flag and if there is data to fetch
	 */
	if ((flags & BATADV_TVLV_HANDLER_OGM_CIFNOTFND) ||
	    (tvlv_value_len < sizeof(gateway))) {
		gateway.bandwidth_down = 0;
		gateway.bandwidth_up = 0;
	} else {
		gateway_ptr = tvlv_value;
		gateway.bandwidth_down = gateway_ptr->bandwidth_down;
		gateway.bandwidth_up = gateway_ptr->bandwidth_up;
		if ((gateway.bandwidth_down == 0) ||
		    (gateway.bandwidth_up == 0)) {
			gateway.bandwidth_down = 0;
			gateway.bandwidth_up = 0;
		}
	}

	batadv_gw_node_update(bat_priv, orig, &gateway);

	/* restart gateway selection if fast or late switching was enabled */
	if ((gateway.bandwidth_down != 0) &&
	    (atomic_read(&bat_priv->gw_mode) == BATADV_GW_MODE_CLIENT) &&
	    (atomic_read(&bat_priv->gw_sel_class) > 2))
		batadv_gw_check_election(bat_priv, orig);
}

/**
 * batadv_gw_init - initialise the gateway handling internals
 * @bat_priv: the bat priv with all the soft interface information
 */
void batadv_gw_init(struct batadv_priv *bat_priv)
{
	batadv_tvlv_handler_register(bat_priv, batadv_gw_tvlv_ogm_handler_v1,
				     NULL, BATADV_TVLV_GW, 1,
				     BATADV_TVLV_HANDLER_OGM_CIFNOTFND);
}

/**
 * batadv_gw_free - free the gateway handling internals
 * @bat_priv: the bat priv with all the soft interface information
 */
void batadv_gw_free(struct batadv_priv *bat_priv)
{
	batadv_tvlv_container_unregister(bat_priv, BATADV_TVLV_GW, 1);
	batadv_tvlv_handler_unregister(bat_priv, BATADV_TVLV_GW, 1);
}
