/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
    hwmon-vid.h - VID/VRM/VRD voltage conversions

    Originally part of lm_sensors
    Copyright (c) 2002 Mark D. Studebaker <mdsxyz123@yahoo.com>
    With assistance from Trent Piepho <xyzzy@speakeasy.org>

*/

#ifndef _LINUX_HWMON_VID_H
#define _LINUX_HWMON_VID_H

int vid_from_reg(int val, u8 vrm);
u8 vid_which_vrm(void);

/* vrm is the VRM/VRD document version multiplied by 10.
   val is in mV to avoid floating point in the kernel.
   Returned value is the 4-, 5- or 6-bit VID code.
   Note that only VRM 9.x is supported for now. */
static inline int vid_to_reg(int val, u8 vrm)
{
	switch (vrm) {
	case 91:		/* VRM 9.1 */
	case 90:		/* VRM 9.0 */
		return ((val >= 1100) && (val <= 1850) ?
			((18499 - val * 10) / 25 + 5) / 10 : -1);
	default:
		return -EINVAL;
	}
}

#endif /* _LINUX_HWMON_VID_H */
