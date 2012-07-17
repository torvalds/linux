#include <linux/types.h>
#include <linux/ioctl.h>
#include <linux/miscdevice.h>

struct modem_dev
{
	const char *name;
	struct miscdevice miscdev;
	struct work_struct work;
};

/* 耳机数据结构体 */
struct rk30_sew868_data {
	int (*io_init)(void);
	int (*io_deinit)(void);
	unsigned int bp_sys;
	unsigned int bp_power;
	unsigned int bp_power_active_low;
	unsigned int bp_reset;
	unsigned int bp_reset_active_low;
	unsigned int bp_wakeup_ap;
	unsigned int ap_wakeup_bp;
};

#define MODEM_NAME "sew868"
