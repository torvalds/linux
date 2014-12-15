//$VER$  v1.4.0
//$TIME$ 2013-08-23-15:33:46
//$------------------$






/*
 *  mm3a310.h - Linux kernel modules for 3-Axis Accelerometer
 *
 *  Copyright (C) 2011-2012 MiraMEMS Sensing Technology Co., Ltd.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 */

#ifndef    __MM3A310_H__
#define    __MM3A310_H__

#include    <linux/ioctl.h>    /* For IOCTL macros */
#include    <linux/input.h>

#ifndef DEBUG
#define DEBUG
#endif

struct mm3a310_cali_s {

    unsigned char x_ok;
    unsigned short x_off;

    unsigned char y_ok;
    unsigned short y_off;

    unsigned char z_ok;
    unsigned short z_off;
    short           z_dir;
};

/* Don't change the value or index unless you know what you do */

#define SAD0L                       0x00
#define SAD0H                       0x01
#define MM3A310_ACC_I2C_SADROOT     0x0C
#define MM3A310_ACC_I2C_SAD_L       ((MM3A310_ACC_I2C_SADROOT<<1)|SAD0L)
#define MM3A310_ACC_I2C_SAD_H       ((MM3A310_ACC_I2C_SADROOT<<1)|SAD0H)
#define MM3A310_ACC_DEV_NAME        "mm3a310_acc"

#define MM3A310_DRV_NAME    "mm3a310"

#define MM3A310_DRV_ADDR    0x26 /* When SA0=1 then 27. When SA0=0 then 26*/


/* SAO pad is connected to GND, set LSB of SAD '0' */
#define MM3A310_ACC_I2C_ADDR        MM3A310_ACC_I2C_SAD_L
#define MM3A310_ACC_I2C_NAME        MM3A310_ACC_DEV_NAME

#define MM3A310_RANGE               1000000
#define MM3A310_PRECISION           12
#define MM3A310_BOUNDARY            (0x1 << (MM3A310_PRECISION - 1))
#define MM3A310_GRAVITY_STEP        (MM3A310_RANGE/MM3A310_BOUNDARY)

#ifdef RK3066_ANDROID_4P1

#define MM3A310_ACC_IOCTL_BASE      77
#define IOCTL_INDEX_BASE            0x80

#define MM3A310_DISABLE 0x7F
#define MM3A310_ENABLE (1 << 7)

#else

#define MM3A310_ACC_IOCTL_BASE      88
#define IOCTL_INDEX_BASE            0x00

#endif

/* The following define the IOCTL command values via the ioctl macros */
#define MM3A310_ACC_IOCTL_SET_DELAY             _IOW(MM3A310_ACC_IOCTL_BASE, IOCTL_INDEX_BASE, int)
#define MM3A310_ACC_IOCTL_GET_DELAY             _IOR(MM3A310_ACC_IOCTL_BASE, IOCTL_INDEX_BASE+1, int)
#define MM3A310_ACC_IOCTL_SET_ENABLE            _IOW(MM3A310_ACC_IOCTL_BASE, IOCTL_INDEX_BASE+2, int)
#define MM3A310_ACC_IOCTL_GET_ENABLE            _IOR(MM3A310_ACC_IOCTL_BASE, IOCTL_INDEX_BASE+3, int)
#define MM3A310_ACC_IOCTL_SET_FULLSCALE         _IOW(MM3A310_ACC_IOCTL_BASE, IOCTL_INDEX_BASE+4, int)
#define MM3A310_ACC_IOCTL_SET_G_RANGE           MM3A310_ACC_IOCTL_SET_FULLSCALE
#define MM3A310_ACC_IOCTL_GET_G_RANGE           _IOR(MM3A310_ACC_IOCTL_BASE, IOCTL_INDEX_BASE+5, int)

#define MM3A310_ACC_IOCTL_SET_CTRL_REG3         _IOW(MM3A310_ACC_IOCTL_BASE, IOCTL_INDEX_BASE+6, int)
#define MM3A310_ACC_IOCTL_SET_CTRL_REG6         _IOW(MM3A310_ACC_IOCTL_BASE, IOCTL_INDEX_BASE+7, int)
#define MM3A310_ACC_IOCTL_SET_DURATION1         _IOW(MM3A310_ACC_IOCTL_BASE, IOCTL_INDEX_BASE+8, int)
#define MM3A310_ACC_IOCTL_SET_THRESHOLD1        _IOW(MM3A310_ACC_IOCTL_BASE, IOCTL_INDEX_BASE+9, int)
#define MM3A310_ACC_IOCTL_SET_CONFIG1           _IOW(MM3A310_ACC_IOCTL_BASE, IOCTL_INDEX_BASE+10, int)

#define MM3A310_ACC_IOCTL_SET_DURATION2         _IOW(MM3A310_ACC_IOCTL_BASE, IOCTL_INDEX_BASE+11, int)
#define MM3A310_ACC_IOCTL_SET_THRESHOLD2        _IOW(MM3A310_ACC_IOCTL_BASE, IOCTL_INDEX_BASE+12, int)
#define MM3A310_ACC_IOCTL_SET_CONFIG2           _IOW(MM3A310_ACC_IOCTL_BASE, IOCTL_INDEX_BASE+13, int)

#define MM3A310_ACC_IOCTL_GET_SOURCE1           _IOW(MM3A310_ACC_IOCTL_BASE, IOCTL_INDEX_BASE+14, int)
#define MM3A310_ACC_IOCTL_GET_SOURCE2           _IOW(MM3A310_ACC_IOCTL_BASE, IOCTL_INDEX_BASE+15, int)

#define MM3A310_ACC_IOCTL_GET_TAP_SOURCE        _IOW(MM3A310_ACC_IOCTL_BASE, IOCTL_INDEX_BASE+16, int)

#define MM3A310_ACC_IOCTL_SET_TAP_TW            _IOW(MM3A310_ACC_IOCTL_BASE, IOCTL_INDEX_BASE+17, int)
#define MM3A310_ACC_IOCTL_SET_TAP_CFG           _IOW(MM3A310_ACC_IOCTL_BASE, IOCTL_INDEX_BASE+18, int)
#define MM3A310_ACC_IOCTL_SET_TAP_TLIM          _IOW(MM3A310_ACC_IOCTL_BASE, IOCTL_INDEX_BASE+19, int)
#define MM3A310_ACC_IOCTL_SET_TAP_THS           _IOW(MM3A310_ACC_IOCTL_BASE, IOCTL_INDEX_BASE+20, int)
#define MM3A310_ACC_IOCTL_SET_TAP_TLAT          _IOW(MM3A310_ACC_IOCTL_BASE, IOCTL_INDEX_BASE+21, int)

#define MM3A310_ACC_IOCTL_GET_COOR_XYZ          _IOW(MM3A310_ACC_IOCTL_BASE, IOCTL_INDEX_BASE+22, int)
#define MM3A310_ACC_IOCTL_CALIBRATION           _IOW(MM3A310_ACC_IOCTL_BASE, IOCTL_INDEX_BASE+23, int)
#define MM3A310_ACC_IOCTL_UPDATE_OFFSET     	_IOW(MM3A310_ACC_IOCTL_BASE, IOCTL_INDEX_BASE+24, int)


/* Accelerometer Sensor Full Scale */
#define MM3A310_ACC_FS_MASK         0x30
#define MM3A310_ACC_G_2G            0x00
#define MM3A310_ACC_G_4G            0x10
#define MM3A310_ACC_G_8G            0x20
#define MM3A310_ACC_G_16G           0x30


/* Accelerometer Sensor Operating Mode */
#define MM3A310_ACC_ENABLE          0x01
#define MM3A310_ACC_DISABLE         0x00

//#if defined(CONFIG_MACH_SP6810A)
#define I2C_BUS_NUM_STATIC_ALLOC
//#define I2C_STATIC_BUS_NUM        (0)
//#endif

#ifdef    __KERNEL__
struct mm3a310_acc_platform_data {
    int poll_interval;
    int min_interval;

    u8 g_range;

    u8 axis_map_x;
    u8 axis_map_y;
    u8 axis_map_z;

    u8 negate_x;
    u8 negate_y;
    u8 negate_z;

    int (*init)(void);
    void (*exit)(void);
    int (*power_on)(void);
    int (*power_off)(void);

    int gpio_int1;
    int gpio_int2;
};
#endif    /* __KERNEL__ */

#define MM3A310_SUCCESS                     0
#define MM3A310_ERR_I2C                     -1
#define MM3A310_ERR_STATUS                  -3
#define MM3A310_ERR_SETUP_FAILURE           -4
#define MM3A310_ERR_GETGSENSORDATA          -5
#define MM3A310_ERR_IDENTIFICATION          -6

extern struct acc_hw* mm3a310_get_cust_acc_hw(void);
#define MM3A310_BUFSIZE                     256

#define MM3A310_DISABLE 0x7F
#define MM3A310_ENABLE (1 << 7)
#endif    /* __MM3A310_H__ */
