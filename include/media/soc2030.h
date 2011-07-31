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

#ifndef __SOC2030_H__
#define __SOC2030_H__

#include <linux/ioctl.h>  /* For IOCTL macros */

#define SOC2030_IOCTL_SET_MODE		_IOWR('o', 1, struct soc2030_mode)
#define SOC2030_IOCTL_GET_STATUS	_IOC(_IOC_READ, 'o', 2, 10)
#define SOC2030_IOCTL_SET_PRIVATE	_IOWR('o', 3, struct soc2030_regs)
#define SOC2030_IOCTL_GET_MODES		_IO('o', 4)
#define SOC2030_IOCTL_GET_NUM_MODES	_IOR('o', 5, unsigned int)
#define SOC2030_IOCTL_SET_EFFECT	_IOWR('o', 6, unsigned int)
#define SOC2030_IOCTL_SET_WHITEBALANCE	_IOWR('o', 7, unsigned int)
#define SOC2030_IOCTL_SET_EXP_COMP	_IOWR('o', 8, int)

#define SOC2030_POLL_WAITMS 50
#define SOC2030_MAX_RETRIES 3
#define SOC2030_POLL_RETRIES 5

#define SOC2030_MAX_PRIVATE_SIZE 1024
#define SOC2030_MAX_NUM_MODES 6

#define SOC_EV_MAX 2
#define SOC_EV_MIN -2
#define EXP_TARGET 0x32

enum {
	REG_TABLE_END,
	WRITE_REG_DATA,
	WRITE_REG_BIT_H,
	WRITE_REG_BIT_L,
	POLL_REG_DATA,
	POLL_REG_BIT_H,
	POLL_REG_BIT_L,
	WRITE_VAR_DATA,
	POLL_VAR_DATA,
	DELAY_MS,
};

enum {
	EFFECT_NONE,
	EFFECT_BW,
	EFFECT_NEGATIVE,
	EFFECT_POSTERIZE,
	EFFECT_SEPIA,
	EFFECT_SOLARIZE,
	EFFECT_AQUA,
	EFFECT_MAX,
};

enum {
	WB_AUTO,
	WB_INCANDESCENT,
	WB_FLUORESCENT,
	WB_DAYLIGHT,
	WB_CLOUDYDAYLIGHT,
	WB_NIGHT,
	WB_MAX,
};

struct soc2030_regs {
	__u8 op;
	__u16 addr;
	__u16 val;
};

struct soc2030_mode {
	int xres;
	int yres;
	int fps;
	struct soc2030_regs *regset;
};

#ifdef __KERNEL__
struct soc2030_platform_data {
	int (*power_on)(void);
	int (*power_off)(void);

};
#endif /* __KERNEL__ */

#endif  /* __SOC2030_H__ */

