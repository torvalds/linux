#include <linux/types.h>
#include <linux/ioctl.h>
#include <linux/miscdevice.h>

struct modem_dev
{
	const char *name;
	struct miscdevice miscdev;
	struct work_struct work;
};

struct rk29_mw100_data {
	struct device *dev;
	int (*io_init)(void);
	int (*io_deinit)(void);
	unsigned int bp_power;
	unsigned int bp_power_active_low;
	unsigned int bp_reset;
	unsigned int bp_reset_active_low;
	unsigned int bp_wakeup_ap;
	unsigned int ap_wakeup_bp;
	unsigned int modem_power_en;
};

#define MODEM_NAME "mw100"
