/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2003 H. Peter Anvin - All Rights Reserved
 *
 * Public interface to the RAID6 P/Q calculation and recovery library.
 */
#ifndef LINUX_RAID_PQ_H
#define LINUX_RAID_PQ_H

#include <linux/types.h>

#define RAID6_MIN_DISKS		4

void raid6_gen_syndrome(int disks, size_t bytes, void **ptrs);
void raid6_xor_syndrome(int disks, int start, int stop, size_t bytes,
		void **ptrs);
bool raid6_can_xor_syndrome(void);

void raid6_recov_2data(int disks, size_t bytes, int faila, int failb,
		void **ptrs);
void raid6_recov_datap(int disks, size_t bytes, int faila,
		void **ptrs);

#endif /* LINUX_RAID_PQ_H */
