/*
* ar0832_focuser.h
*
* Copyright (c) 2011, NVIDIA, All Rights Reserved.
*
* This file is licensed under the terms of the GNU General Public License
* version 2. This program is licensed "as is" without any warranty of any
* kind, whether express or implied.
*/

#ifndef __AR0832_FOCUSER_H__
#define __AR0832_FOCUSER_H__

#include <linux/ioctl.h>  /* For IOCTL macros */
#include <linux/i2c.h>

#define AR0832_FOCUSER_IOCTL_GET_CONFIG		_IOR('o', 12, struct ar0832_focuser_config)
#define AR0832_FOCUSER_IOCTL_SET_POSITION	_IOW('o', 13, __u32)
#define AR0832_FOCUSER_IOCTL_SET_MODE		_IOW('o', 14, __u32)

struct ar0832_focuser_config {
	__u32 settle_time;
	__u32 actuator_range;
	__u32 pos_low;
	__u32 pos_high;
	/* To-Do */
	/*
	float focal_length;
	float fnumber;
	float max_aperture;
	*/
};

enum StereoCameraMode {
	MAIN = 0,
	/* Sets the stereo camera to stereo mode. */
	STEREO = 1,
	/* Only the sensor on the left is on. */
	LEFT_ONLY,
	/* Only the sensor on the right is on. */
	RIGHT_ONLY,
	/* Ignore -- Forces compilers to make 32-bit enums. */
	StereoCameraMode_Force32 = 0x7FFFFFFF
};

struct ar0832_focuser_info {
	struct i2c_client *i2c_client;
	struct i2c_client *i2c_client_right;
	struct regulator *regulator;
	struct ar0832_focuser_config config;
	__u8 focuser_init_flag;

	enum StereoCameraMode camera_mode;
};

#endif /* __AR0832_FOCUSER_H__ */
