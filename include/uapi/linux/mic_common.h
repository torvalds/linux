/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2013 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Intel MIC driver.
 *
 */
#ifndef __MIC_COMMON_H_
#define __MIC_COMMON_H_

#include <linux/types.h>

/**
 * struct mic_bootparam: Virtio device independent information in device page
 *
 * @magic: A magic value used by the card to ensure it can see the host
 * @c2h_shutdown_db: Card to Host shutdown doorbell set by host
 * @h2c_shutdown_db: Host to Card shutdown doorbell set by card
 * @h2c_config_db: Host to Card Virtio config doorbell set by card
 * @shutdown_status: Card shutdown status set by card
 * @shutdown_card: Set to 1 by the host when a card shutdown is initiated
 */
struct mic_bootparam {
	__u32 magic;
	__s8 c2h_shutdown_db;
	__s8 h2c_shutdown_db;
	__s8 h2c_config_db;
	__u8 shutdown_status;
	__u8 shutdown_card;
} __aligned(8);

/* Device page size */
#define MIC_DP_SIZE 4096

#define MIC_MAGIC 0xc0ffee00

/**
 * enum mic_states - MIC states.
 */
enum mic_states {
	MIC_OFFLINE = 0,
	MIC_ONLINE,
	MIC_SHUTTING_DOWN,
	MIC_RESET_FAILED,
	MIC_LAST
};

/**
 * enum mic_status - MIC status reported by card after
 * a host or card initiated shutdown or a card crash.
 */
enum mic_status {
	MIC_NOP = 0,
	MIC_CRASHED,
	MIC_HALTED,
	MIC_POWER_OFF,
	MIC_RESTART,
	MIC_STATUS_LAST
};

#endif
