#ifndef __HID_ROCCAT_H
#define __HID_ROCCAT_H

/*
 * Copyright (c) 2010 Stefan Achatz <erazor_de@users.sourceforge.net>
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/hid.h>
#include <linux/types.h>

#define ROCCATIOCGREPSIZE _IOR('H', 0xf1, int)

#ifdef __KERNEL__

int roccat_connect(struct class *klass, struct hid_device *hid,
		int report_size);
void roccat_disconnect(int minor);
int roccat_report_event(int minor, u8 const *data);

#endif

#endif
