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
struct rk2818_23d_data {
	unsigned int bp_power;
	unsigned int bp_reset;
	unsigned int bp_statue;
	unsigned int ap_statue;
	unsigned int ap_bp_wakeup;
	unsigned int bp_ap_wakeup;
};

#define MODEM_NAME "mtk23d"
