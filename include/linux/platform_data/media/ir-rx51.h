#ifndef _IR_RX51_H
#define _IR_RX51_H

struct ir_rx51_platform_data {
	int(*set_max_mpu_wakeup_lat)(struct device *dev, long t);
};

#endif
