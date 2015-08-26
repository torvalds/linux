/*
 * Copyright 2005-2015 Freescale Semiconductor, Inc.
 */

/*
 * The code contained herein is licensed under the GNU Lesser General
 * Public License.  You may obtain a copy of the GNU Lesser General
 * Public License Version 2.1 or later at the following locations:
 *
 * http://www.opensource.org/licenses/lgpl-license.html
 * http://www.gnu.org/copyleft/lgpl.html
 */

/*!
 * @defgroup IPU MXC Image Processing Unit (IPU) Driver
 */
/*!
 * @file linux/ipu.h
 *
 * @brief This file contains the IPU driver API declarations.
 *
 * @ingroup IPU
 */

#ifndef __LINUX_IPU_H__
#define __LINUX_IPU_H__

#include <linux/interrupt.h>
#include <uapi/linux/ipu.h>

unsigned int fmt_to_bpp(unsigned int pixelformat);
cs_t colorspaceofpixel(int fmt);
int need_csc(int ifmt, int ofmt);

int ipu_queue_task(struct ipu_task *task);
int ipu_check_task(struct ipu_task *task);

#endif
