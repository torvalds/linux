/*
 * Copyright (C) 2009-2011 B.A.T.M.A.N. contributors:
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 *
 */

#ifndef _NET_BATMAN_ADV_GATEWAY_COMMON_H_
#define _NET_BATMAN_ADV_GATEWAY_COMMON_H_

enum gw_modes {
	GW_MODE_OFF,
	GW_MODE_CLIENT,
	GW_MODE_SERVER,
};

#define GW_MODE_OFF_NAME	"off"
#define GW_MODE_CLIENT_NAME	"client"
#define GW_MODE_SERVER_NAME	"server"

void gw_bandwidth_to_kbit(uint8_t gw_class, int *down, int *up);
ssize_t gw_bandwidth_set(struct net_device *net_dev, char *buff, size_t count);

#endif /* _NET_BATMAN_ADV_GATEWAY_COMMON_H_ */
