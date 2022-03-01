/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _FIRMWARE_XLNX_EVENT_MANAGER_H_
#define _FIRMWARE_XLNX_EVENT_MANAGER_H_

#include <linux/firmware/xlnx-zynqmp.h>

#define CB_MAX_PAYLOAD_SIZE	(4U) /*In payload maximum 32bytes */

/************************** Exported Function *****************************/

typedef void (*event_cb_func_t)(const u32 *payload, void *data);

#if IS_REACHABLE(CONFIG_XLNX_EVENT_MANAGER)
int xlnx_register_event(const enum pm_api_cb_id cb_type, const u32 node_id,
			const u32 event, const bool wake,
			event_cb_func_t cb_fun, void *data);

int xlnx_unregister_event(const enum pm_api_cb_id cb_type, const u32 node_id,
			  const u32 event, event_cb_func_t cb_fun);
#else
static inline int xlnx_register_event(const enum pm_api_cb_id cb_type, const u32 node_id,
				      const u32 event, const bool wake,
				      event_cb_func_t cb_fun, void *data)
{
	return -ENODEV;
}

static inline int xlnx_unregister_event(const enum pm_api_cb_id cb_type, const u32 node_id,
					 const u32 event, event_cb_func_t cb_fun)
{
	return -ENODEV;
}
#endif

#endif /* _FIRMWARE_XLNX_EVENT_MANAGER_H_ */
