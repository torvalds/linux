/* include/linux/capella_cm3602.h
 *
 * Copyright (C) 2009 Google, Inc.
 * Author: Iliyan Malchev <malchev@google.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __LINUX_CAPELLA_CM3602_H
#define __LINUX_CAPELLA_CM3602_H

#include <linux/types.h>
#include <linux/ioctl.h>

#define CAPELLA_CM3602_IOCTL_MAGIC 'c'
#define CAPELLA_CM3602_IOCTL_GET_ENABLED \
		_IOR(CAPELLA_CM3602_IOCTL_MAGIC, 1, int *)
#define CAPELLA_CM3602_IOCTL_ENABLE \
		_IOW(CAPELLA_CM3602_IOCTL_MAGIC, 2, int *)

#ifdef __KERNEL__
#define CAPELLA_CM3602 "capella_cm3602"
struct capella_cm3602_platform_data {
	int (*power)(int); /* power to the chip */
	int p_out; /* proximity-sensor outpuCAPELLA_CM3602_IOCTL_ENABLE,t */
	int irq_pin;
	int pwd_out_pin;
	int ps_shutdown_pin;
};
#endif /* __KERNEL__ */

#endif
