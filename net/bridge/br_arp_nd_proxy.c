/*
 *  Handle bridge arp/nd proxy/suppress
 *
 *  Copyright (C) 2017 Cumulus Networks
 *  Copyright (c) 2017 Roopa Prabhu <roopa@cumulusnetworks.com>
 *
 *  Authors:
 *	Roopa Prabhu <roopa@cumulusnetworks.com>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include "br_private.h"

void br_recalculate_neigh_suppress_enabled(struct net_bridge *br)
{
	struct net_bridge_port *p;
	bool neigh_suppress = false;

	list_for_each_entry(p, &br->port_list, list) {
		if (p->flags & BR_NEIGH_SUPPRESS) {
			neigh_suppress = true;
			break;
		}
	}

	br->neigh_suppress_enabled = neigh_suppress;
}
