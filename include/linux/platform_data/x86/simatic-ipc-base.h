/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Siemens SIMATIC IPC drivers
 *
 * Copyright (c) Siemens AG, 2018-2021
 *
 * Authors:
 *  Henning Schild <henning.schild@siemens.com>
 *  Gerd Haeussler <gerd.haeussler.ext@siemens.com>
 */

#ifndef __PLATFORM_DATA_X86_SIMATIC_IPC_BASE_H
#define __PLATFORM_DATA_X86_SIMATIC_IPC_BASE_H

#include <linux/types.h>

#define SIMATIC_IPC_DEVICE_NONE 0
#define SIMATIC_IPC_DEVICE_227D 1
#define SIMATIC_IPC_DEVICE_427E 2
#define SIMATIC_IPC_DEVICE_127E 3
#define SIMATIC_IPC_DEVICE_227E 4

struct simatic_ipc_platform {
	u8	devmode;
};

#endif /* __PLATFORM_DATA_X86_SIMATIC_IPC_BASE_H */
