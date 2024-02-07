/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __LINUX_BLUETOOTH_POWER_H
#define __LINUX_BLUETOOTH_POWER_H

#include <linux/types.h>

/*
 * voltage regulator information required for configuring the
 * bluetooth chipset
 */
enum bt_power_modes {
	BT_POWER_DISABLE = 0,
	BT_POWER_ENABLE,
	BT_POWER_RETENTION
};

/* Hasting chipset version information */
enum {
	HASTINGS_SOC_ID_0100 = 0x400A0100,
	HASTINGS_SOC_ID_0101 = 0x400A0101,
	HASTINGS_SOC_ID_0110 = 0x400A0110,
	HASTINGS_SOC_ID_0200 = 0x400A0200,
};

struct log_index {
	int init;
	int crash;
};

struct bt_power_vreg_data {
	struct regulator *reg;  /* voltage regulator handle */
	const char *name;       /* regulator name */
	u32 min_vol;            /* min voltage level */
	u32 max_vol;            /* max voltage level */
	u32 load_curr;          /* current */
	bool is_enabled;        /* is this regulator enabled? */
	bool is_retention_supp; /* does this regulator support retention mode */
	struct log_index indx;  /* Index for reg. w.r.t init & crash */
};

struct bt_power {
	char compatible[32];
	struct bt_power_vreg_data *vregs;
	int num_vregs;
};

struct bt_power_clk_data {
	struct clk *clk;  /* clock regulator handle */
	const char *name; /* clock name */
	bool is_enabled;  /* is this clock enabled? */
};

struct btpower_tcs_table_info {
	resource_size_t tcs_cmd_base_addr;
	void __iomem *tcs_cmd_base_addr_io;
};
/*
 * Platform data for the bluetooth power driver.
 */
struct bluetooth_power_platform_data {
	int bt_gpio_sys_rst;                   /* Bluetooth reset gpio */
	int wl_gpio_sys_rst;                   /* Wlan reset gpio */
	int bt_gpio_sw_ctrl;                   /* Bluetooth sw_ctrl gpio */
	int bt_gpio_debug;                     /* Bluetooth debug gpio */
	int xo_gpio_sys_rst;                    /* XO reset gpio*/
	struct device *slim_dev;
	struct bt_power_vreg_data *vreg_info;  /* VDDIO voltage regulator */
	struct bt_power_clk_data *bt_chip_clk; /* bluetooth reference clock */
	int (*bt_power_setup)(int id); /* Bluetooth power setup function */
	char compatible[32]; /*Bluetooth SoC name */
	int num_vregs;
	struct btpower_tcs_table_info tcs_table_info;
};

int btpower_register_slimdev(struct device *dev);
int btpower_get_chipset_version(void);

#define BT_CMD_SLIM_TEST		0xbfac
#define BT_CMD_PWR_CTRL			0xbfad
#define BT_CMD_CHIPSET_VERS		0xbfae
#define BT_CMD_GET_CHIPSET_ID	0xbfaf
#define BT_CMD_CHECK_SW_CTRL	0xbfb0
#define BT_CMD_GETVAL_POWER_SRCS	0xbfb1
#define BT_CMD_SET_IPA_TCS_INFO  0xbfc0

#define TCS_CMD_IO_ADDR_OFFSET 0x4

#endif /* __LINUX_BLUETOOTH_POWER_H */
