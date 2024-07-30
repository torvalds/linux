/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Xilinx Event Management Driver
 *
 * Copyright (C) 2024, Advanced Micro Devices, Inc.
 */

#ifndef _FIRMWARE_XLNX_EVENT_MANAGER_H_
#define _FIRMWARE_XLNX_EVENT_MANAGER_H_

#include <linux/firmware/xlnx-zynqmp.h>

#define CB_MAX_PAYLOAD_SIZE	(4U) /*In payload maximum 32bytes */

#define EVENT_SUBSYSTEM_RESTART		(4U)

#define PM_DEV_ACPU_0_0			(0x1810c0afU)
#define PM_DEV_ACPU_0			(0x1810c003U)

/************************** Exported Function *****************************/

typedef void (*event_cb_func_t)(const u32 *payload, void *data);

#if IS_REACHABLE(CONFIG_XLNX_EVENT_MANAGER)
int xlnx_register_event(const enum pm_api_cb_id cb_type, const u32 node_id,
			const u32 event, const bool wake,
			event_cb_func_t cb_fun, void *data);

int xlnx_unregister_event(const enum pm_api_cb_id cb_type, const u32 node_id,
			  const u32 event, event_cb_func_t cb_fun, void *data);
#else
static inline int xlnx_register_event(const enum pm_api_cb_id cb_type, const u32 node_id,
				      const u32 event, const bool wake,
				      event_cb_func_t cb_fun, void *data)
{
	return -ENODEV;
}

static inline int xlnx_unregister_event(const enum pm_api_cb_id cb_type, const u32 node_id,
					const u32 event, event_cb_func_t cb_fun, void *data)
{
	return -ENODEV;
}
#endif

#endif /* _FIRMWARE_XLNX_EVENT_MANAGER_H_ */
