/*
 *  include/linux/mfd/rt5025/rt5025.h
 *  Include header file for Richtek RT5025 Core file
 *
 *  Copyright (C) 2013 Richtek Technology Corp.
 *  cy_huang <cy_huang@richtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#ifndef __LINUX_MFD_RT5025_H
#define __LINUX_MFD_RT5025_H

#include <linux/power_supply.h>
#include <linux/alarmtimer.h>
#include <linux/wakelock.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif /* CONFIG_HAS_EARLYSUSPEND */

#define RT5025_DEV_NAME "rt5025"
#define RT5025_DRV_VER	   "1.1.1_R"

#define RT_BATT_NAME	"rt-battery"

enum {
	RT5025_REG_DEVID,
	RT5025_REG_RANGE1START = RT5025_REG_DEVID,
	RT5025_REG_CHGCTL1,
	RT5025_REG_CHGCTL2,
	RT5025_REG_CHGCTL3,
	RT5025_REG_CHGCTL4,
	RT5025_REG_CHGCTL5,
	RT5025_REG_CHGCTL6,
	RT5025_REG_CHGCTL7,
	RT5025_REG_DCDCCTL1,
	RT5025_REG_DCDCCTL2,
	RT5025_REG_DCDCCTL3,
	RT5025_REG_VRCCTL,
	RT5025_REG_DCDCCTL4,
	RT5025_REG_LDOCTL1,
	RT5025_REG_LDOCTL2,
	RT5025_REG_LDOCTL3,
	RT5025_REG_LDOCTL4,
	RT5025_REG_LDOCTL5,
	RT5025_REG_LDOCTL6,
	RT5025_REG_RESV0,
	RT5025_REG_LDOOMS,
	RT5025_REG_MISC1,
	RT5025_REG_ONEVENT,
	RT5025_REG_DCDCONOFF,
	RT5025_REG_LDOONOFF,
	RT5025_REG_MISC2,
	RT5025_REG_MISC3,
	RT5025_REG_MISC4,
	RT5025_REG_GPIO0,
	RT5025_REG_GPIO1,
	RT5025_REG_GPIO2,
	RT5025_REG_RANGE1END = RT5025_REG_GPIO2,
	RT5025_REG_OFFEVENT = 0x20,
	RT5025_REG_RANGE2START = RT5025_REG_OFFEVENT,
	RT5025_REG_RESV1,
	RT5025_REG_RESV2,
	RT5025_REG_RESV3,
	RT5025_REG_RESV4,
	RT5025_REG_RESV5,
	RT5025_REG_RESV6,
	RT5025_REG_RESV7,
	RT5025_REG_RESV8,
	RT5025_REG_RESV9,
	RT5025_REG_RESV10,
	RT5025_REG_RESV11,
	RT5025_REG_RESV12,
	RT5025_REG_RESV13,
	RT5025_REG_RESV14,
	RT5025_REG_RESV15,
	RT5025_REG_IRQEN1,
	RT5025_REG_IRQSTAT1,
	RT5025_REG_IRQEN2,
	RT5025_REG_IRQSTAT2,
	RT5025_REG_IRQEN3,
	RT5025_REG_IRQSTAT3,
	RT5025_REG_IRQEN4,
	RT5025_REG_IRQSTAT4,
	RT5025_REG_IRQEN5,
	RT5025_REG_IRQSTAT5,
	RT5025_REG_RANGE2END = RT5025_REG_IRQSTAT5,
	RT5025_REG_IRQCTL = 0x50,
	RT5025_REG_RANGE3START = RT5025_REG_IRQCTL,
	RT5025_REG_IRQFLG,
	RT5025_REG_FGRESV1,
	RT5025_REG_VALRTMAX,
	RT5025_REG_VALRTMIN1,
	RT5025_REG_VALRTMIN2,
	RT5025_REG_TALRTMAX,
	RT5025_REG_TALRTMIN,
	RT5025_REG_VBATSH,
	RT5025_REG_VBATSL,
	RT5025_REG_INTEMPH,
	RT5025_REG_INTEMPL,
	RT5025_REG_FGRESV2,
	RT5025_REG_CONFIG,
	RT5025_REG_AINH,
	RT5025_REG_AINL,
	RT5025_REG_TIMERH,
	RT5025_REG_TIMERL,
	RT5025_REG_CHANNELH,
	RT5025_REG_CHANNELL,
	RT5025_REG_INACVLTH,
	RT5025_REG_INACVLTL,
	RT5025_REG_INUSBVLTH,
	RT5025_REG_INUSBVLTL,
	RT5025_REG_VSYSVLTH,
	RT5025_REG_VSYSVLTL,
	RT5025_REG_GPIO0VLTH,
	RT5025_REG_GPIO0VLTL,
	RT5025_REG_GPIO1VLTH,
	RT5025_REG_GPIO1VLTL,
	RT5025_REG_GPIO2VLTH,
	RT5025_REG_GPIO2VLTL,
	RT5025_REG_DCDC1VLTH,
	RT5025_REG_DCDC1VLTL,
	RT5025_REG_DCDC2VLTH,
	RT5025_REG_DCDC2VLTL,
	RT5025_REG_DCDC3VLTH,
	RT5025_REG_DCDC3VLTL,
	RT5025_REG_CURRH,
	RT5025_REG_CURRL,
	RT5025_REG_QCHGHH,
	RT5025_REG_QCHGHL,
	RT5025_REG_QCHGLH,
	RT5025_REG_QCHGLL,
	RT5025_REG_QDCHGHH,
	RT5025_REG_QDCHGHL,
	RT5025_REG_QDCHGLH,
	RT5025_REG_QDCHGLL,
	RT5025_REG_RANGE3END = RT5025_REG_QDCHGLL,
	RT5025_REG_DCDC4OVP = 0xA9,
	RT5025_REG_RANGE4START = RT5025_REG_DCDC4OVP,
	RT5025_REG_RANGE4END = RT5025_REG_DCDC4OVP,
	RT5025_REG_MAX,
};

enum {
	RT5025_VOFF_2P8V,
	RT5025_VOFF_2P9V,
	RT5025_VOFF_3P0V,
	RT5025_VOFF_3P1V,
	RT5025_VOFF_3P2V,
	RT5025_VOFF_3P3V,
	RT5025_VOFF_3P4V,
	RT5025_VOFF_3P5V,
	RT5025_VOFF_MAX = RT5025_VOFF_3P5V,
};

enum {
	RT5025_STARTIME_100MS,
	RT5025_STARTIME_1S,
	RT5025_STARTIME_2S,
	RT5025_STARTIME_3S,
	RT5025_STARTIME_MAX = RT5025_STARTIME_3S,
};

enum {
	RT5025_SHDNPRESS_4S,
	RT5025_SHDNPRESS_6S,
	RT5025_SHDNPRESS_8S,
	RT5025_SHDNPRESS_10S,
	RT5025_SHDNPRESS_MAX = RT5025_SHDNPRESS_10S,
};

enum {
	RT5025_VDPM_4V,
	RT5025_VDPM_4P25V,
	RT5025_VDPM_4P5V,
	RT5025_VDPM_DIS,
	RT5025_VDPM_MAX = RT5025_VDPM_DIS,
};

enum {
	RT5025_IEOC_10P,
	RT5025_IEOC_20P,
	RT5025_IEOC_MAX = RT5025_IEOC_20P,
};

enum {
	RT5025_VPREC_2V,
	RT5025_VPREC_2P2V,
	RT5025_VPREC_2P4V,
	RT5025_VPREC_2P6V,
	RT5025_VPREC_2P8V,
	RT5025_VPREC_3V,
	RT5025_VPREC_MAX = RT5025_VPREC_3V,
};

enum {
	RT5025_IPREC_10P,
	RT5025_IPREC_20P,
	RT5025_IPREC_MAX = RT5025_IPREC_20P,
};

enum {
	RT5025_ID_DCDC1,
	RT5025_ID_DCDC2,
	RT5025_ID_DCDC3,
	RT5025_ID_DCDC4,
	RT5025_ID_LDO1,
	RT5025_ID_LDO2,
	RT5025_ID_LDO3,
	RT5025_ID_LDO4,
	RT5025_ID_LDO5,
	RT5025_ID_LDO6,
	RT5025_MAX_REGULATOR,
};

typedef void (*rt_irq_handler)(void *info, int eventno);

#define RT5025_DCDCRAMP_MAX	0x03
struct rt5025_regulator_ramp {
	unsigned char ramp_sel:2;
};

struct rt5025_charger_info {
	struct i2c_client	*i2c;
	struct device		*dev;
	struct power_supply	psy;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif /* CONFIG_HAS_EARLYSUSPEND */
	struct delayed_work tempmon_work;
	int temp[4];
	u32 temp_scalar[8];
	unsigned int te_en:1;
	unsigned int online:1;
	unsigned int batabs:1;
	unsigned int battemp_region:3;
	unsigned int inttemp_region:2;
	unsigned int otg_en:1;
	unsigned int init_once:1;
	unsigned int suspend:1;
	unsigned int screenon_adjust:1;
	unsigned int screen_on:1;
	int chg_status;
	int charger_cable;
	int chg_volt;
	int acchg_icc;
	int usbtachg_icc;
	int usbchg_icc;
	int screenon_icc;
};

struct rt5025_battery_info {
	struct i2c_client *client;
	struct power_supply	battery;
	struct delayed_work monitor_work;
	struct wake_lock monitor_wake_lock;
	struct wake_lock low_battery_wake_lock;
	struct wake_lock status_wake_lock;
	struct wake_lock smooth0_wake_lock;
	struct wake_lock smooth100_wake_lock;
	struct wake_lock full_battery_wake_lock;
	/*#if RT5025_TEST_WAKE_LOCK
	//	struct wake_lock test_wake_lock;
	//#endif*/
	struct mutex status_change_lock;
	struct alarm wakeup_alarm;

	bool temp_range_0_5;
	bool temp_range_5_10;
	bool temp_range_10_15;
	bool temp_range_15_20;
	bool temp_range_20_30;
	bool temp_range_30_35;
	bool temp_range_35_40;
	bool temp_range_40_45;
	bool temp_range_45_50;

	bool range_0_5_done;
	bool range_5_10_done;
	bool range_10_15_done;
	bool range_15_20_done;
	bool range_20_30_done;
	bool range_30_35_done;
	bool range_35_40_done;
	bool range_40_45_done;
	bool range_45_50_done;

	bool	suspend_poll;
	ktime_t	last_poll;
	/*	ktime_t	last_event;*/
	struct timespec last_event;

	u16 update_time;

	/* previous battery voltage */
	u16 pre_vcell;
	/* previous battery current */
	s16 pre_curr;
	/* battery voltage */
	u16 vcell;
	/* battery current */
	s16 curr;
	/* battery current offset */
	u16 curr_offset;
	/* AIN voltage */
	u16 ain_volt;
	/* battery internal temperature */
	s16 int_temp;
	/* battery external temperature */
	s16 ext_temp;
	/* charge coulomb counter */
	u32 chg_cc;
	u32 chg_cc_unuse;
	/* discharge coulomb counter */
	u32 dchg_cc;
	u32 dchg_cc_unuse;
	/* battery capacity */
	u16 soc;
	u16 temp_soc;
	u16 pre_soc;
	u16 last_soc;

	u16 time_interval;
	u16 pre_gauge_timer;

	u8 online;
	u8 status;
	u8 internal_status;
	u8 health;
	u8 present;
	u8 batt_present;

	/* IRQ flag */
	u8 irq_flag;

	/* max voltage IRQ flag */
	bool max_volt_irq;
	/* min voltage1 IRQ flag */
	bool min_volt1_irq;
	/* min voltage2 IRQ flag */
	bool min_volt2_irq;
	/* max temperature IRQ flag */
	bool max_temp_irq;
	/* min temperature IRQ flag */
	bool min_temp_irq;

	bool min_volt2_alert;

	u8 temp_high_cnt;
	u8 temp_low_cnt;
	u8 temp_recover_cnt;

	bool init_cap;
	bool avg_flag;

	/* remain capacity */
	u32 rm;
	/* SOC permille  */
	u16 permille;
	/* full capccity */
	u16 fcc_aging;
	u16 fcc;
	u16	dc;
	s16 tempcmp;

	bool edv_flag;
	bool edv_detection;
	u8 edv_cnt;

	bool tp_flag;
	u8 tp_cnt;

	u8 cycle_cnt;
	u32 acc_dchg_cap;

	bool smooth_flag;

	u16 gauge_timer;
	s16 curr_raw;
	u32 empty_edv;
	u8  edv_region;
	u32  soc1_lock_cnt;
	u32  soc99_lock_cnt;

	bool init_once;
	bool device_suspend;
	bool last_suspend;
	bool last_tp_flag;
	bool fcc_update_flag;
	u32 cal_fcc;
	u8 test_temp;
	u8  last_tp;
	u32 cal_eoc_fcc;
	u32 cal_soc_offset;
};


struct rt5025_charger_data {
	int *temp;
	u32 *temp_scalar;
	int chg_volt;
	int acchg_icc;
	int usbtachg_icc;
	int usbchg_icc;
	int screenon_icc;
	unsigned int ieoc:1;
	unsigned int vdpm:2;
	unsigned int te_en:1;
	unsigned int vprec:3;
	unsigned int iprec:1;
	unsigned int screenon_adjust:1;
};

struct rt5025_gpio_data {
	int ngpio;
};

struct rt5025_misc_data {
	unsigned char vsyslv:3;
	unsigned char shdnlpress_time:2;
	unsigned char startlpress_time:2;
	unsigned char vsyslv_enshdn:1;
};

struct rt5025_irq_data {
	int irq_gpio;
};

struct rt5025_chip;
struct rt5025_platform_data {
	struct regulator_init_data *regulator[RT5025_MAX_REGULATOR];
	struct rt5025_charger_data *chg_pdata;
	struct rt5025_gpio_data *gpio_pdata;
	struct rt5025_misc_data *misc_pdata;
	struct rt5025_irq_data *irq_pdata;
	int (*pre_init)(struct rt5025_chip *rt5025_chip);
	/** Called after subdevices are set up */
	int (*post_init)(void);
};

struct rt5025_misc_info {
	struct i2c_client *i2c;
	struct device *dev;
};

struct rt5025_chip {
	struct i2c_client *i2c;
	struct device *dev;
	struct rt5025_charger_info *charger_info;
	struct rt5025_battery_info *battery_info;
	struct rt5025_misc_info *misc_info;
	struct mutex io_lock;
	int suspend;
};

#ifdef CONFIG_CHARGER_RT5025
void rt5025_charger_irq_handler(struct rt5025_charger_info *ci, unsigned int event);
#endif /* #ifdef CONFIG_CHARGER_RT5025 */
#ifdef CONFIG_MISC_RT5025
void rt5025_misc_irq_handler(struct rt5025_misc_info *mi, unsigned int event);
#endif /* #ifdef CONFIG_MISC_RT5025 */
#ifdef CONFIG_BATTERY_RT5025
void rt5025_gauge_irq_handler(struct rt5025_battery_info *bi, unsigned int event);
#endif /* #ifdef CONFIG_BATTERY_RT5025 */

extern int rt5025_reg_block_read(struct i2c_client *, int, int, void *);
extern int rt5025_reg_block_write(struct i2c_client *, int, int, void *);
extern int rt5025_reg_read(struct i2c_client *, int);
extern int rt5025_reg_write(struct i2c_client *, int, unsigned char);
extern int rt5025_assign_bits(struct i2c_client *, int, unsigned char, unsigned char);
extern int rt5025_set_bits(struct i2c_client *, int, unsigned char);
extern int rt5025_clr_bits(struct i2c_client *, int, unsigned char);

extern int rt5025_core_init(struct rt5025_chip *, struct rt5025_platform_data *);
extern int rt5025_core_deinit(struct rt5025_chip *);

#ifdef CONFIG_MFD_RT_SHOW_INFO
#define RTINFO(format, args...) \
	printk(KERN_INFO "%s:%s() line-%d: " format, RT5025_DEV_NAME, __FUNCTION__, __LINE__, ##args)
#else
#define RTINFO(format, args...)
#endif /* CONFIG_MFD_RT_SHOW_INFO */

#endif /* __LINUX_MFD_RT5025_H */
