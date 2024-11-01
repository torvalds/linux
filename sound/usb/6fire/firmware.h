/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Linux driver for TerraTec DMX 6Fire USB
 *
 * Author: Torsten Schenk
 * Created: Jan 01, 2011
 * Copyright:	(C) Torsten Schenk
 */

#ifndef USB6FIRE_FIRMWARE_H
#define USB6FIRE_FIRMWARE_H

#include "common.h"

enum /* firmware state of device */
{
	FW_READY = 0,
	FW_NOT_READY = 1
};

int usb6fire_fw_init(struct usb_interface *intf);
#endif /* USB6FIRE_FIRMWARE_H */

