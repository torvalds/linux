/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
    bt819.h - bt819 notifications

    Copyright (C) 2009 Hans Verkuil (hverkuil@kernel.org)

*/

#ifndef _BT819_H_
#define _BT819_H_

#include <linux/ioctl.h>

/* v4l2_device notifications. */

/* Needed to reset the FIFO buffer when changing the input
   or the video standard.

   Note: these ioctls that internal to the kernel and are never called
   from userspace. */
#define BT819_FIFO_RESET_LOW	_IO('b', 0)
#define BT819_FIFO_RESET_HIGH	_IO('b', 1)

#endif
