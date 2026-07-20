/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_DEVICE_ID_WMI_H
#define LINUX_DEVICE_ID_WMI_H

/* WMI */

#define WMI_MODULE_PREFIX	"wmi:"

/**
 * struct wmi_device_id - WMI device identifier
 * @guid_string: 36 char string of the form fa50ff2b-f2e8-45de-83fa-65417f2f49ba
 * @context: pointer to driver specific data
 */
struct wmi_device_id {
	const char guid_string[UUID_STRING_LEN+1];
	const void *context;
};

#endif /* ifndef LINUX_DEVICE_ID_WMI_H */
