/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/types.h>
#include <linux/ioctl.h>
#include <linux/miscdevice.h>

struct modem_dev
{
	const char *name;
	struct miscdevice miscdev;
	struct work_struct work;
};

/* 耳机数据结构�?*/
struct rk2818_23d_data {
	struct device *dev;
	int (*io_init)(void);
	int (*io_deinit)(void);
	unsigned int bp_power;
	unsigned int bp_power_active_low;
	unsigned int bp_reset;
	unsigned int bp_reset_active_low;
	unsigned int bp_statue;
	unsigned int ap_statue;
	unsigned int ap_bp_wakeup;
	unsigned int bp_ap_wakeup;
	struct semaphore power_sem;
};

#define MODEM_NAME "mtk23d"
