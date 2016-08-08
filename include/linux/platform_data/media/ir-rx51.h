#ifndef _LIRC_RX51_H
#define _LIRC_RX51_H

struct lirc_rx51_platform_data {
	int(*set_max_mpu_wakeup_lat)(struct device *dev, long t);
};

#endif
