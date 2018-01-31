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

struct rk29_mt6229_data {
	struct device *dev;
	int (*io_init)(void);
	int (*io_deinit)(void);
	unsigned int bp_power;
	unsigned int bp_reset;
	unsigned int bp_wakeup_ap;
	unsigned int modem_usb_en;
	unsigned int modem_uart_en;
	unsigned int modem_power_en;
	unsigned int ap_ready;
};

#define MODEM_NAME "mt6229"
