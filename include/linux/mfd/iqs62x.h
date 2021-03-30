/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Azoteq IQS620A/621/622/624/625 Multi-Function Sensors
 *
 * Copyright (C) 2019 Jeff LaBundy <jeff@labundy.com>
 */

#ifndef __LINUX_MFD_IQS62X_H
#define __LINUX_MFD_IQS62X_H

#define IQS620_PROD_NUM				0x41
#define IQS621_PROD_NUM				0x46
#define IQS622_PROD_NUM				0x42
#define IQS624_PROD_NUM				0x43
#define IQS625_PROD_NUM				0x4E

#define IQS621_ALS_FLAGS			0x16
#define IQS622_ALS_FLAGS			0x14

#define IQS624_HALL_UI				0x70
#define IQS624_HALL_UI_WHL_EVENT		BIT(4)
#define IQS624_HALL_UI_INT_EVENT		BIT(3)
#define IQS624_HALL_UI_AUTO_CAL			BIT(2)

#define IQS624_INTERVAL_DIV			0x7D

#define IQS620_GLBL_EVENT_MASK			0xD7
#define IQS620_GLBL_EVENT_MASK_PMU		BIT(6)

#define IQS62X_NUM_KEYS				16
#define IQS62X_NUM_EVENTS			(IQS62X_NUM_KEYS + 6)

#define IQS62X_EVENT_SIZE			10

enum iqs62x_ui_sel {
	IQS62X_UI_PROX,
	IQS62X_UI_SAR1,
};

enum iqs62x_event_reg {
	IQS62X_EVENT_NONE,
	IQS62X_EVENT_SYS,
	IQS62X_EVENT_PROX,
	IQS62X_EVENT_HYST,
	IQS62X_EVENT_HALL,
	IQS62X_EVENT_ALS,
	IQS62X_EVENT_IR,
	IQS62X_EVENT_WHEEL,
	IQS62X_EVENT_INTER,
	IQS62X_EVENT_UI_LO,
	IQS62X_EVENT_UI_HI,
};

enum iqs62x_event_flag {
	/* keys */
	IQS62X_EVENT_PROX_CH0_T,
	IQS62X_EVENT_PROX_CH0_P,
	IQS62X_EVENT_PROX_CH1_T,
	IQS62X_EVENT_PROX_CH1_P,
	IQS62X_EVENT_PROX_CH2_T,
	IQS62X_EVENT_PROX_CH2_P,
	IQS62X_EVENT_HYST_POS_T,
	IQS62X_EVENT_HYST_POS_P,
	IQS62X_EVENT_HYST_NEG_T,
	IQS62X_EVENT_HYST_NEG_P,
	IQS62X_EVENT_SAR1_ACT,
	IQS62X_EVENT_SAR1_QRD,
	IQS62X_EVENT_SAR1_MOVE,
	IQS62X_EVENT_SAR1_HALT,
	IQS62X_EVENT_WHEEL_UP,
	IQS62X_EVENT_WHEEL_DN,

	/* switches */
	IQS62X_EVENT_HALL_N_T,
	IQS62X_EVENT_HALL_N_P,
	IQS62X_EVENT_HALL_S_T,
	IQS62X_EVENT_HALL_S_P,

	/* everything else */
	IQS62X_EVENT_SYS_RESET,
	IQS62X_EVENT_SYS_ATI,
};

struct iqs62x_event_data {
	u16 ui_data;
	u8 als_flags;
	u8 ir_flags;
	u8 interval;
};

struct iqs62x_event_desc {
	enum iqs62x_event_reg reg;
	u8 mask;
	u8 val;
};

struct iqs62x_dev_desc {
	const char *dev_name;
	const struct mfd_cell *sub_devs;
	int num_sub_devs;
	u8 prod_num;
	u8 sw_num;
	const u8 *cal_regs;
	int num_cal_regs;
	u8 prox_mask;
	u8 sar_mask;
	u8 hall_mask;
	u8 hyst_mask;
	u8 temp_mask;
	u8 als_mask;
	u8 ir_mask;
	u8 prox_settings;
	u8 als_flags;
	u8 hall_flags;
	u8 hyst_shift;
	u8 interval;
	u8 interval_div;
	const char *fw_name;
	const enum iqs62x_event_reg (*event_regs)[IQS62X_EVENT_SIZE];
};

struct iqs62x_core {
	const struct iqs62x_dev_desc *dev_desc;
	struct i2c_client *client;
	struct regmap *regmap;
	struct blocking_notifier_head nh;
	struct list_head fw_blk_head;
	struct completion ati_done;
	struct completion fw_done;
	enum iqs62x_ui_sel ui_sel;
	unsigned long event_cache;
};

extern const struct iqs62x_event_desc iqs62x_events[IQS62X_NUM_EVENTS];

#endif /* __LINUX_MFD_IQS62X_H */
