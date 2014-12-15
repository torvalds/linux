/*
 *
 * $Id: stk_defines.h,v 1.0 2011/03/05 11:12:08 jsgood Exp $
 *
 * Copyright (C) 2011 Patrick Chang <patrick_chang@sitronix.com.tw>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 *
 *	SenseTek/Sitronix Proximity / Ambient Light  Sensor Driver
 */

/*
-------------------------------------------------------------------------
	root@android:/sys/devices/platform/stk-oss # ls -al
	ls -al
	drwxr-xr-x root     root              1970-01-01 00:00 DBG
	-rwxrwxrwx root     root         4096 1970-01-01 00:00 als_enable
	-r--r--r-- root     root         4096 1970-01-02 00:00 dist_mode
	-r--r--r-- root     root         4096 1970-01-02 00:00 dist_res
	-rw-rw-rw- root     root         4096 1970-01-02 00:00 distance
	-r--r--r-- root     root         4096 1970-01-02 00:00 distance_range
	-rw-rw-rw- root     root         4096 1970-01-02 00:00 lux
	-r--r--r-- root     root         4096 1970-01-02 00:00 lux_range
	-r--r--r-- root     root         4096 1970-01-02 00:00 lux_res
	-rw-rw-rw- root     root         4096 1970-01-02 00:00 ps_cali
	-rwxrwxrwx root     root         4096 1970-01-01 00:00 ps_enable

-------------------------------------------------------------------------
*/
#ifndef __STK_DEFINES_H
#define __STK_DEFINES_H

#define ALS_NAME "stk220x"
#define PS_NAME "proximity"

#define ps_enable_path              "/sys/devices/platform/stk-oss/ps_enable"
#define als_enable_path             "/sys/devices/platform/stk-oss/als_enable"
#define ps_distance_mode_path       "/sys/devices/platform/stk-oss/dist_mode"
#define ps_distance_range_path      "/sys/devices/platform/stk-oss/distance_range"
#define als_lux_range_path          "/sys/devices/platform/stk-oss/lux_range"

#define STK_DRIVER_VER          	"1.74"


#define EVENT_TYPE_PROXIMITY        ABS_DISTANCE
#define EVENT_TYPE_LIGHT            ABS_MISC

#endif // __STK_DEFINE_H
