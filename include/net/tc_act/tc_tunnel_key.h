/*
 * Copyright (c) 2016, Amir Vadai <amir@vadai.me>
 * Copyright (c) 2016, Mellanox Technologies. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __NET_TC_TUNNEL_KEY_H
#define __NET_TC_TUNNEL_KEY_H

#include <net/act_api.h>

struct tcf_tunnel_key_params {
	struct rcu_head		rcu;
	int			tcft_action;
	int			action;
	struct metadata_dst     *tcft_enc_metadata;
};

struct tcf_tunnel_key {
	struct tc_action	      common;
	struct tcf_tunnel_key_params __rcu *params;
};

#define to_tunnel_key(a) ((struct tcf_tunnel_key *)a)

#endif /* __NET_TC_TUNNEL_KEY_H */
