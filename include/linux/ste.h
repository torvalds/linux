/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/types.h>
#include <linux/ioctl.h>

#define STE_IOCTL_MAGIC 'd'
#define STE_IOCTL_GET_ACK \
		_IOR(STE_IOCTL_MAGIC, 1, int *)
#define STE_IOCTL_EN_APSEND_ACK \
		_IOW(STE_IOCTL_MAGIC, 2, int *)
#define STE_IOCTL_POWER_ON \
		_IOW(STE_IOCTL_MAGIC, 3, int *)
#define STE_IOCTL_POWER_OFF \
		_IOW(STE_IOCTL_MAGIC, 4, int *)

#define STE_NAME "STE"