/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __DP501_H_
#define __DP501_H_

#include <linux/i2c.h>
#include<linux/earlysuspend.h>


#define DP501_P0_ADDR (0x30)
#define DP501_P1_ADDR (0x32)
#define DP501_P2_ADDR (0x34)
#define DP501_P3_ADDR (0x36)

#define DP501_SCL_RATE  (100*1000)
#define MAX_REG     	(0xff)



#define CHIP_ID_L	(0x80)
#define CHIP_ID_H	(0x81)

struct  dp501_platform_data {
	unsigned int dvdd33_en_pin;
	int 	     dvdd33_en_val;
	unsigned int dvdd18_en_pin;
	int 	     dvdd18_en_val;
	unsigned int edp_rst_pin;
	int (*power_ctl)(void);
};

struct dp501 {
	struct i2c_client *client;
	struct dp501_platform_data *pdata;
	int (*edp_init)(struct i2c_client *client);
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif 
};

#endif
