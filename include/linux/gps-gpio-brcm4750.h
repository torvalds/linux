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

#ifndef _GPS_GPIO_BRCM4750_H_
#define _GPS_GPIO_BRCM4750_H_

#include <linux/android_alarm.h>
#include <linux/ioctl.h>
#include <linux/wakelock.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#define GPS_GPIO_DRIVER_NAME "gps_brcm4750"

#define GPS_GPIO_IOCTL_BASE	'w'

#define IOC_GPS_GPIO_RESET       _IOW(GPS_GPIO_IOCTL_BASE, 0x0, int)
#define IOC_GPS_GPIO_STANDBY     _IOW(GPS_GPIO_IOCTL_BASE, 0x1, int)
/* start single shot wake up timer, set the value in msecs */
#define IOC_GPS_START_TIMER      _IOW(GPS_GPIO_IOCTL_BASE, 0x2, int)
/* stop wake up timer */
#define IOC_GPS_STOP_TIMER       _IOW(GPS_GPIO_IOCTL_BASE, 0x3, int)

#ifdef __KERNEL__
struct gps_gpio_brcm4750_platform_data {
      void (*set_reset_gpio)(unsigned int gpio_val);
      void (*set_standby_gpio)(unsigned int gpio_val);
      void (*free_gpio)(void);
      struct alarm alarm;
      struct wake_lock gps_brcm4750_wake;
      wait_queue_head_t gps_brcm4750_wq;
      int timer_status;
} __attribute__ ((packed));

#endif  /* __KERNEL__ */
#endif  /* _GPS_GPIO_BRCM4750_H_ */
