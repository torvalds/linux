/*
* ar0832_main.h
*
* Copyright (c) 2011, NVIDIA, All Rights Reserved.
*
* This file is licensed under the terms of the GNU General Public License
* version 2. This program is licensed "as is" without any warranty of any
* kind, whether express or implied.
*/

#ifndef __AR0832_MAIN_H__
#define __AR0832_MAIN_H__

#include <linux/ioctl.h> /* For IOCTL macros */

#define AR0832_IOCTL_SET_MODE			_IOW('o', 1, struct ar0832_mode)
#define AR0832_IOCTL_SET_FRAME_LENGTH		_IOW('o', 2, __u32)
#define AR0832_IOCTL_SET_COARSE_TIME		_IOW('o', 3, __u32)
#define AR0832_IOCTL_SET_GAIN			_IOW('o', 4, struct ar0832_gain)
#define AR0832_IOCTL_GET_STATUS			_IOR('o', 5, __u8)
#define AR0832_IOCTL_GET_OTP			_IOR('o', 6, struct ar0832_otp_data)
#define AR0832_IOCTL_TEST_PATTERN		_IOW('o', 7, enum ar0832_test_pattern)
#define AR0832_IOCTL_SET_POWER_ON		_IOW('o', 10, __u32)
#define AR0832_IOCTL_SET_SENSOR_REGION		_IOW('o', 11, struct ar0832_stereo_region)


enum ar0832_test_pattern {
	TEST_PATTERN_NONE,
	TEST_PATTERN_COLORBARS,
	TEST_PATTERN_CHECKERBOARD
};

struct ar0832_otp_data {
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

struct ar0832_gain{
	__u16 AnalogGain;
	__u16 DigitalGain_Upper;
	__u16 DigitalGain_Lower;
};

struct ar0832_mode {
	int xres;
	int yres;
	__u32 frame_length;
	__u32 coarse_time;
	struct ar0832_gain gain;
};
struct ar0832_point{
	int x;
	int y;
};
struct ar0832_reg {
	__u16 addr;
	__u16 val;
};
struct ar0832_stereo_region{
	int	camer_index;
	struct ar0832_point image_start;
	struct ar0832_point image_end;
};

#ifdef __KERNEL__
struct ar0832_platform_data {
	int (*power_on)(void);
	int (*power_off)(void);

};
#endif /* __KERNEL__ */

#endif

