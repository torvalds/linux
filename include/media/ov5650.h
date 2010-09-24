/*
 * Copyright (C) 2010 Motorola, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307, USA
 */

#ifndef __OV5650_H__
#define __OV5650_H__

#include <linux/ioctl.h>  /* For IOCTL macros */

#define OV5650_IOCTL_SET_MODE		_IOW('o', 1, struct ov5650_mode)
#define OV5650_IOCTL_SET_FRAME_LENGTH	_IOW('o', 2, __u32)
#define OV5650_IOCTL_SET_COARSE_TIME	_IOW('o', 3, __u32)
#define OV5650_IOCTL_SET_GAIN		_IOW('o', 4, __u16)
#define OV5650_IOCTL_GET_STATUS		_IOR('o', 5, __u8)
#define OV5650_IOCTL_GET_OTP            _IOR('o', 6, struct ov5650_otp_data)
#define OV5650_IOCTL_TEST_PATTERN       _IOW('o', 7, enum ov5650_test_pattern)

enum ov5650_test_pattern {
	TEST_PATTERN_NONE,
	TEST_PATTERN_COLORBARS,
	TEST_PATTERN_CHECKERBOARD
};

struct ov5650_otp_data {
	/* Only the first 5 bytes are actually used. */
	__u8 sensor_serial_num[6];
	__u8 part_num[8];
	__u8 lens_id[1];
	__u8 manufacture_id[2];
	__u8 factory_id[2];
	__u8 manufacture_date[9];
	__u8 manufacture_line[2];

	__u32 module_serial_num;
	__u8 focuser_liftoff[2];
	__u8 focuser_macro[2];
	__u8 reserved1[12];
	__u8 shutter_cal[16];
	__u8 reserved2[183];

	/* Big-endian. CRC16 over 0x00-0x41 (inclusive) */
	__u16 crc;
	__u8 reserved3[3];
	__u8 auto_load[2];
} __attribute__ ((packed));

struct ov5650_mode {
	int xres;
	int yres;
	__u32 frame_length;
	__u32 coarse_time;
	__u16 gain;
};
#ifdef __KERNEL__
struct ov5650_platform_data {
	int (*power_on)(void);
	int (*power_off)(void);

};
#endif /* __KERNEL__ */

#endif  /* __OV5650_H__ */

