/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __LINUX_USB_USBPD_H
#define __LINUX_USB_USBPD_H

#include <linux/list.h>

struct usbpd;

/* Standard IDs */
#define USBPD_SID			0xff00

/* Structured VDM Command Type */
enum usbpd_svdm_cmd_type {
	SVDM_CMD_TYPE_INITIATOR,
	SVDM_CMD_TYPE_RESP_ACK,
	SVDM_CMD_TYPE_RESP_NAK,
	SVDM_CMD_TYPE_RESP_BUSY,
};

/* Structured VDM Commands */
#define USBPD_SVDM_DISCOVER_IDENTITY	0x1
#define USBPD_SVDM_DISCOVER_SVIDS	0x2
#define USBPD_SVDM_DISCOVER_MODES	0x3
#define USBPD_SVDM_ENTER_MODE		0x4
#define USBPD_SVDM_EXIT_MODE		0x5
#define USBPD_SVDM_ATTENTION		0x6

/*
 * Implemented by client
 */
struct usbpd_svid_handler {
	u16 svid;

	/* Notified when VDM session established/reset; must be implemented */
	void (*connect)(struct usbpd_svid_handler *hdlr,
			bool supports_usb_comm);
	void (*disconnect)(struct usbpd_svid_handler *hdlr);

	/* DP driver -> PE driver for requesting USB SS lanes */
	int (*request_usb_ss_lane)(struct usbpd *pd,
			struct usbpd_svid_handler *hdlr);

	/* Unstructured VDM */
	void (*vdm_received)(struct usbpd_svid_handler *hdlr, u32 vdm_hdr,
			const u32 *vdos, int num_vdos);

	/* Structured VDM */
	void (*svdm_received)(struct usbpd_svid_handler *hdlr, u8 cmd,
			enum usbpd_svdm_cmd_type cmd_type, const u32 *vdos,
			int num_vdos);

	/* client should leave these blank; private members used by PD driver */
	struct list_head entry;
	bool discovered;
};

enum plug_orientation {
	ORIENTATION_NONE,
	ORIENTATION_CC1,
	ORIENTATION_CC2,
};

#if IS_ENABLED(CONFIG_USB_PD_POLICY)
/*
 * Obtains an instance of usbpd from a DT phandle
 */
struct usbpd *devm_usbpd_get_by_phandle(struct device *dev,
		const char *phandle);

/*
 * Called by client to handle specific SVID messages.
 * Specify callback functions in the usbpd_svid_handler argument
 */
int usbpd_register_svid(struct usbpd *pd, struct usbpd_svid_handler *hdlr);

void usbpd_unregister_svid(struct usbpd *pd, struct usbpd_svid_handler *hdlr);

/*
 * Transmit a VDM message.
 */
int usbpd_send_vdm(struct usbpd *pd, u32 vdm_hdr, const u32 *vdos,
		int num_vdos);

/*
 * Transmit a Structured VDM message.
 */
int usbpd_send_svdm(struct usbpd *pd, u16 svid, u8 cmd,
		enum usbpd_svdm_cmd_type cmd_type, int obj_pos,
		const u32 *vdos, int num_vdos);

/*
 * Get current status of CC pin orientation.
 *
 * Return: ORIENTATION_CC1 or ORIENTATION_CC2 if attached,
 *         otherwise ORIENTATION_NONE if not attached
 */
enum plug_orientation usbpd_get_plug_orientation(struct usbpd *pd);

void usbpd_vdm_in_suspend(struct usbpd *pd, bool in_suspend);
#else
static inline struct usbpd *devm_usbpd_get_by_phandle(struct device *dev,
		const char *phandle)
{
	return ERR_PTR(-ENODEV);
}

static inline int usbpd_register_svid(struct usbpd *pd,
		struct usbpd_svid_handler *hdlr)
{
	return -EINVAL;
}

static inline void usbpd_unregister_svid(struct usbpd *pd,
		struct usbpd_svid_handler *hdlr)
{
}

static inline int usbpd_send_vdm(struct usbpd *pd, u32 vdm_hdr, const u32 *vdos,
		int num_vdos)
{
	return -EINVAL;
}

static inline int usbpd_send_svdm(struct usbpd *pd, u16 svid, u8 cmd,
		enum usbpd_svdm_cmd_type cmd_type, int obj_pos,
		const u32 *vdos, int num_vdos)
{
	return -EINVAL;
}

static inline enum plug_orientation usbpd_get_plug_orientation(struct usbpd *pd)
{
	return ORIENTATION_NONE;
}

static inline void usbpd_vdm_in_suspend(struct usbpd *pd, bool in_suspend) { }
#endif /* IS_ENABLED(CONFIG_USB_PD_POLICY) */

/*
 * Additional helpers for Enter/Exit Mode commands
 */

static inline int usbpd_enter_mode(struct usbpd *pd, u16 svid, int mode,
		const u32 *vdo)
{
	return usbpd_send_svdm(pd, svid, USBPD_SVDM_ENTER_MODE,
			SVDM_CMD_TYPE_INITIATOR, mode, vdo, vdo ? 1 : 0);
}

static inline int usbpd_exit_mode(struct usbpd *pd, u16 svid, int mode,
		const u32 *vdo)
{
	return usbpd_send_svdm(pd, svid, USBPD_SVDM_EXIT_MODE,
			SVDM_CMD_TYPE_INITIATOR, mode, vdo, vdo ? 1 : 0);
}

#endif /* __LINUX_USB_USBPD_H */
