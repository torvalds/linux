/*
 * Copyright (c) 2014 Jiri Pirko <jiri@resnulli.us>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __NET_TC_VLAN_H
#define __NET_TC_VLAN_H

#include <net/act_api.h>

#define VLAN_F_POP		0x1
#define VLAN_F_PUSH		0x2

struct tcf_vlan {
	struct tc_action	common;
	int			tcfv_action;
	u16			tcfv_push_vid;
	__be16			tcfv_push_proto;
};
#define to_vlan(a) ((struct tcf_vlan *)a)

#endif /* __NET_TC_VLAN_H */
