/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NET_NEIGHBOUR_TABLES_H
#define _NET_NEIGHBOUR_TABLES_H

enum {
	NEIGH_ARP_TABLE = 0,
	NEIGH_ND_TABLE = 1,
	NEIGH_NR_TABLES,
	NEIGH_LINK_TABLE = NEIGH_NR_TABLES /* Pseudo table for neigh_xmit */
};

#endif
