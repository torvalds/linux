/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NET_GRO_CELLS_H
#define _NET_GRO_CELLS_H

#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/netdevice.h>

struct gro_cell;

struct gro_cells {
	struct gro_cell __percpu	*cells;
};

int gro_cells_receive(struct gro_cells *gcells, struct sk_buff *skb);
int gro_cells_init(struct gro_cells *gcells, struct net_device *dev);
void gro_cells_destroy(struct gro_cells *gcells);

#endif
