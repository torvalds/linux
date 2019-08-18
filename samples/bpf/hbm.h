/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) 2019 Facebook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * Include file for Host Bandwidth Management (HBM) programs
 */
struct hbm_vqueue {
	struct bpf_spin_lock lock;
	/* 4 byte hole */
	unsigned long long lasttime;	/* In ns */
	int credit;			/* In bytes */
	unsigned int rate;		/* In bytes per NS << 20 */
};

struct hbm_queue_stats {
	unsigned long rate;		/* in Mbps*/
	unsigned long stats:1,		/* get HBM stats (marked, dropped,..) */
		loopback:1;		/* also limit flows using loopback */
	unsigned long long pkts_marked;
	unsigned long long bytes_marked;
	unsigned long long pkts_dropped;
	unsigned long long bytes_dropped;
	unsigned long long pkts_total;
	unsigned long long bytes_total;
	unsigned long long firstPacketTime;
	unsigned long long lastPacketTime;
};
