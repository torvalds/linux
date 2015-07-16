/*
 * Copyright (c) 2014 Jiri Pirko <jiri@resnulli.us>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __LINUX_TC_VLAN_H
#define __LINUX_TC_VLAN_H

#include <linux/pkt_cls.h>

#define TCA_ACT_VLAN 12

#define TCA_VLAN_ACT_POP	1
#define TCA_VLAN_ACT_PUSH	2

struct tc_vlan {
	tc_gen;
	int v_action;
};

enum {
	TCA_VLAN_UNSPEC,
	TCA_VLAN_TM,
	TCA_VLAN_PARMS,
	TCA_VLAN_PUSH_VLAN_ID,
	TCA_VLAN_PUSH_VLAN_PROTOCOL,
	__TCA_VLAN_MAX,
};
#define TCA_VLAN_MAX (__TCA_VLAN_MAX - 1)

#endif
