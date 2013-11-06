/*
 *  include/linux/mfd/rt5025.h
 *  Include header file for Richtek RT5025 Core file
 *
 *  Copyright (C) 2013 Richtek Electronics
 *  cy_huang <cy_huang@richtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_MFD_RT5025_H
#define __LINUX_MFD_RT5025_H

#include <linux/power_supply.h>
#include <linux/android_alarm.h>

#define RT5025_DEVICE_NAME "RT5025"
#define RT5025_DRV_VER	   "1.0.8_R"

enum {
	RT5025_RSTDELAY1_100MS,
	RT5025_RSTDELAY1_500MS,
	RT5025_RSTDELAY1_1S,
	RT5025_RSTDELAY1_2S,
};

enum {
	RT5025_RSTDELAY2_100MS,
	RT5025_RSTDELAY2_500MS,
	RT5025_RSTDELAY2_1S,
	RT5025_RSTDELAY2_2S,
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
};

enum {
	RT5025_STARTIME_100MS,
	RT5025_STARTIME_1S,
	RT5025_STARTIME_2S,
	RT5025_STARTIME_3S,
};

enum {
	RT5025_LPRESS_1S,
	RT5025_LPRESS_1P5S,
	RT5025_LPRESS_2S,
	RT5025_LPRESS_2P5S,
};

enum {
	RT5025_SHDNPRESS_4S,
	RT5025_SHDNPRESS_6S,
	RT5025_SHDNPRESS_8S,
	RT5025_SHDNPRESS_10S,
};

enum {
	RT5025_PGDLY_10MS,
	RT5025_PGDLY_50MS,
	RT5025_PGDLY_100MS,
	RT5025_PGDLY_200MS,
};

enum {
	RT5025_SHDNDLY_100MS,
	RT5025_SHDNDLY_500MS,
	RT5025_SHDNDLY_1S,
	RT5025_SHDNDLY_2S,
};

enum {
	RT5025_CCCHG_TO_4H,
	RT5025_CCCHG_TO_6H,
	RT5025_CCCHG_TO_8H,
	RT5025_CCCHG_TO_10H,
};

enum {
	RT5025_PRECHG_TO_30M,
	RT5025_PRECHG_TO_40M,
	RT5025_PRECHG_TO_50M,
	RT5025_PRECHG_TO_60M,
};

enum {
	RT5025_ICC_0P5A,
	RT5025_ICC_0P6A,
	RT5025_ICC_0P7A,
	RT5025_ICC_0P8A,
	RT5025_ICC_0P9A,
	RT5025_ICC_1A,
	RT5025_ICC_1P1A,
	RT5025_ICC_1P2A,
	RT5025_ICC_1P3A,
	RT5025_ICC_1P4A,
	RT5025_ICC_1P5A,
	RT5025_ICC_1P6A,
	RT5025_ICC_1P7A,
	RT5025_ICC_1P8A,
	RT5025_ICC_1P9A,
	RT5025_ICC_2A,
	RT5025_ICC_MAX,
};

enum {
	RT5025_AICR_100MA,
	RT5025_AICR_500MA,
	RT5025_AICR_1A,
	RT5025_AICR_NOLIMIT,
};

enum {
	RT5025_DPM_4V,
	RT5025_DPM_4P25V,
	RT5025_DPM_4P5V,
	RT5025_DPM_DIS,
};

enum {
	RT5025_VPREC_2V,
	RT5025_VPREC_2P2V,
	RT5025_VPREC_2P4V,
	RT5025_VPREC_2P6V,
	RT5025_VPREC_2P8V,
	RT5025_VPREC_3V,
	RT5025_VPREC_3V_1,
	RT5025_VPREC_3V_2,
};

enum {
	RT5025_IEOC_10P,
	RT5025_IEOC_20P,
};

enum {
	RT5025_IPREC_10P,
	RT5025_IPREC_20P,
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

struct rt5025_power_data {
	union {
		struct {
			unsigned char Resv1:1;
			unsigned char CHGBC_EN:1;
			unsigned char TE:1;
			unsigned char Resv2:1;
			unsigned char CCCHG_TIMEOUT:2;
			unsigned char PRECHG_TIMEOUT:2;
		}bitfield;
		unsigned char val;
	}CHGControl2;
	union {
		struct {
			unsigned char Resv:2;
			unsigned char VOREG:6;
		}bitfield;
		unsigned char val;
	}CHGControl3;
	union {
		struct {
			unsigned char AICR_CON:1;
			unsigned char AICR:2;
			unsigned char ICC:4;
			unsigned char CHG_RST:1;
		}bitfield;
		unsigned char val;
	}CHGControl4;
	union {
		struct {
			unsigned char Resv1:4;
			unsigned char DPM:2;
			unsigned char Resv2:2;
		}bitfield;
		unsigned char val;
	}CHGControl5;
	union {
		struct {
			unsigned char IPREC:1;
			unsigned char IEOC:1;
			unsigned char VPREC:3;
			unsigned char Resv:3;
		}bitfield;
		unsigned char val;
	}CHGControl6;
	union {
		struct {
			unsigned char Resv1:4;
			unsigned char CHGC_EN:1;
			unsigned char CHG_DCDC_MODE:1;
			unsigned char BATD_EN:1;
			unsigned char Resv2:1;
		}bitfield;
		unsigned char val;
	}CHGControl7;
};

struct rt5025_gpio_data {
	unsigned gpio_base;
	unsigned irq_base;
};

struct rt5025_misc_data {
	union {
		struct {
			unsigned char Action:2;
			unsigned char Delayed1:2;
			unsigned char Delayed2:2;
			unsigned char Resv:2;
		}bitfield;
		unsigned char val;
	}RSTCtrl;
	union {
		struct {
			unsigned char Resv:5;
			unsigned char VOFF:3;
		}bitfield;
		unsigned char val;
	}VSYSCtrl;
	union {
		struct {
			unsigned char PG_DLY:2;
			unsigned char SHDN_PRESS:2;
			unsigned char LPRESS_TIME:2;
			unsigned char START_TIME:2;
		}bitfield;
		unsigned char val;
	}PwrOnCfg;
	union {
		struct {
			unsigned char Resv:4;
			unsigned char SHDN_DLYTIME:2;
			unsigned char SHDN_TIMING:1;
			unsigned char SHDN_CTRL:1;
		}bitfield;
		unsigned char val;
	}SHDNCtrl;
	union {
		struct {
			unsigned char Resv:2;
			unsigned char OT_ENSHDN:1;
			unsigned char PWRON_ENSHDN:1;
			unsigned char DCDC3LV_ENSHDN:1;
			unsigned char DCDC2LV_ENSHDN:1;
			unsigned char DCDC1LV_ENSHDN:1;
			unsigned char SYSLV_ENSHDN:1;
		}bitfield;
		unsigned char val;
	}PwrOffCond;
};

struct rt5025_irq_data {
	union {
		struct {
			unsigned char BATABS:1;
			unsigned char Resv1:2;
			unsigned char INUSB_PLUGIN:1;
			unsigned char INUSBOVP:1;
			unsigned char Resv2:1;
			unsigned char INAC_PLUGIN:1;
			unsigned char INACOVP:1;
		}bitfield;
		unsigned char val;
	}irq_enable1;
	union {
		struct {
			unsigned char CHTERMI:1;
			unsigned char CHBATOVI:1;
			unsigned char CHGOODI_INUSB:1;
			unsigned char CHBADI_INUSB:1;
			unsigned char CHSLPI_INUSB:1;
			unsigned char CHGOODI_INAC:1;
			unsigned char CHBADI_INAC:1;
			unsigned char CHSLPI_INAC:1;
		}bitfield;
		unsigned char val;
	}irq_enable2;
	union {
		struct {
			unsigned char TIMEOUT_CC:1;
			unsigned char TIMEOUT_PC:1;
			unsigned char Resv:3;
			unsigned char CHVSREGI:1;
			unsigned char CHTREGI:1;
			unsigned char CHRCHGI:1;
		}bitfield;
		unsigned char val;
	}irq_enable3;
	union {
		struct {
			unsigned char SYSLV:1;
			unsigned char DCDC4LVHV:1;
			unsigned char PWRONLP:1;
			unsigned char PWRONSP:1;
			unsigned char DCDC3LV:1;
			unsigned char DCDC2LV:1;
			unsigned char DCDC1LV:1;
			unsigned char OT:1;
		}bitfield;
		unsigned char val;
	}irq_enable4;
	union {
		struct {
			unsigned char Resv:1;
			unsigned char GPIO0_IE:1;
			unsigned char GPIO1_IE:1;
			unsigned char GPIO2_IE:1;
			unsigned char RESETB:1;
			unsigned char PWRONF:1;
			unsigned char PWRONR:1;
			unsigned char KPSHDN:1;
		}bitfield;
		unsigned char val;
	}irq_enable5;
};

enum {
	JEITA_NO_CHARGE,
	JEITA_NORMAL_USB,
	JEITA_USB_TA,
	JEITA_AC_ADAPTER,
	JEITA_CHARGER_MAX,
};

struct rt5025_jeita_data {
	int* temp;
	u8* temp_scalar;
	int (*temp_cc)[5];
	int (*temp_cv)[5];
};

#define CHG_EVENT_INACOVP	(0x80<<16)
#define CHG_EVENT_INAC_PLUGIN	(0x40<<16)
#define CHG_EVENT_INUSBOVP	(0x10<<16)
#define CHG_EVENT_INUSB_PLUGIN	(0x08<<16)
#define CHG_EVENT_BAT_ABS	(0x01<<16)

#define CHG_EVENT_CHSLPI_INAC	(0x80<<8)
#define CHG_EVENT_CHBADI_INAC	(0x40<<8)
#define CHG_EVENT_CHGOODI_INAC	(0x20<<8)
#define CHG_EVENT_CHSLPI_INUSB	(0x10<<8)
#define CHG_EVENT_CHBADI_INUSB	(0x08<<8)
#define CHG_EVENT_CHGOODI_INUSB	(0x04<<8)
#define CHG_EVENT_CHBATOVI	(0x02<<8)
#define CHG_EVENT_CHTERMI	(0x01<<8)

#define CHG_EVENT_CHRCHGI	(0x80<<0)
#define CHG_EVENT_CHTREGI	(0x40<<0)
#define CHG_EVENT_CHVSREGI	(0x20<<0)
#define CHG_EVENT_TIMEOUTPC	(0x02<<0)
#define CHG_EVENT_TIMEOUTCC	(0x01<<0)

#define CHARGER_DETECT_MASK	(CHG_EVENT_INAC_PLUGIN | CHG_EVENT_INUSB_PLUGIN | \
				 CHG_EVENT_CHSLPI_INAC | CHG_EVENT_CHSLPI_INUSB | \
				 CHG_EVENT_CHBADI_INAC | CHG_EVENT_CHBADI_INUSB | \
				 CHG_EVENT_CHTERMI | CHG_EVENT_CHRCHGI)
 

#define PWR_EVENT_OTIQ		(0x80<<8)
#define PWR_EVENT_DCDC1LV	(0x40<<8)
#define PWR_EVENT_DCDC2LV	(0x20<<8)
#define PWR_EVENT_DCDC3LV	(0x10<<8)
#define PWR_EVENT_PWRONSP	(0x08<<8)
#define PWR_EVENT_PWRONLP	(0x04<<8)
#define PWR_EVENT_DCDC4LVHV	(0x02<<8)
#define PWR_EVENT_SYSLV		(0x01<<8)

#define PWR_EVENT_KPSHDN	(0x80<<0)
#define PWR_EVNET_PWRONR	(0x40<<0)
#define PWR_EVENT_PWRONF	(0x20<<0)
#define PWR_EVENT_RESETB	(0x10<<0)
#define PWR_EVENT_GPIO2IE	(0x08<<0)
#define PWR_EVENT_GPIO1IE	(0x04<<0)
#define PWR_EVENT_GPIO0IE	(0x02<<0)

struct rt5025_event_callback {
	#if 1
	void (*charger_event_callback)(uint32_t detected);
	void (*power_event_callkback)(uint32_t detected);
	#else
	void (*over_temperature_callback)(uint8_t detected);
	void (*charging_complete_callback)(void);
	void (*over_voltage_callback)(uint8_t detected);
	void (*under_voltage_callback)(uint8_t detected);
	void (*charge_fault_callback)(uint8_t detected);
	void (*charge_warning_callback)(uint8_t detected);
	#endif
};

struct rt5025_power_info {
	struct i2c_client	*i2c;
	struct device		*dev;
	struct rt5025_chip	*chip;
	//struct rt5025_gauge_callbacks *event_callback;
	struct power_supply	ac;
	struct power_supply	usb;
	struct mutex	var_lock;
	struct delayed_work usb_detect_work;
	int usb_cnt;
	int chg_term;
	int otg_en;
	unsigned		ac_online:1;
	unsigned		usb_online:1;
	unsigned		chg_stat:3;
};

struct rt5025_swjeita_info {
	struct i2c_client *i2c;
	struct rt5025_chip *chip;
	struct delayed_work thermal_reg_work;
	int *temp;
	u8 *temp_scalar;
	int (*temp_cc)[5];
	int (*temp_cv)[5];
	int dec_current;
	int cur_section;
	int cur_therm_region;
	int cur_cable;
	int cur_temp;
	int cur_inttemp;
	int init_once;
	int suspend;
};

struct rt5025_battery_info {
	struct i2c_client *client;
	struct rt5025_chip *chip;
	//struct rt5025_gauge_callbacks cb;

	struct power_supply	battery;
	
	struct delayed_work monitor_work;
	struct wake_lock monitor_wake_lock;
	struct wake_lock low_battery_wake_lock;
	struct wake_lock status_wake_lock;
//#if RT5025_TEST_WAKE_LOCK
	struct wake_lock test_wake_lock;
//#endif
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
//	ktime_t	last_event;
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
  
  u16 time_interval;
  u16 pre_gauge_timer;
    
  u8 online;
  u8 status;
  u8 internal_status;
  u8 health;
  u8 present;

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
  #if 0
  u32 time_to_empty;
  u32 time_to_full;
  #endif
  
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

  bool init_once;
  bool device_suspend;
  bool last_suspend;
  bool last_tp_flag;
  u32 cal_fcc;
  u8 test_temp;
};

struct rt5025_chip {
	struct i2c_client *i2c;
	struct workqueue_struct *wq;
	struct device *dev;
	struct rt5025_power_info *power_info;
	struct rt5025_swjeita_info *jeita_info;
	struct rt5025_battery_info *battery_info;
	int suspend;
	int irq;
	struct delayed_work delayed_work;
	struct mutex io_lock;
};

struct rt5025_platform_data {
	struct regulator_init_data* regulator[RT5025_MAX_REGULATOR];
	struct rt5025_power_data* power_data;
	struct rt5025_gpio_data* gpio_data;
	struct rt5025_misc_data* misc_data;
	struct rt5025_irq_data* irq_data;
	struct rt5025_jeita_data* jeita_data;
	struct rt5025_event_callback *cb;
	int (*pre_init)(struct rt5025_chip *rt5025_chip);
	/** Called after subdevices are set up */
	int (*post_init)(void);
	int (*set_otg_enable)(int);
	int intr_pin;
};

#ifdef CONFIG_MFD_RT5025_MISC
extern void rt5025_power_off(void);
extern int rt5025_cable_exist(void);
#endif /* CONFIG_MFD_RT5025_MISC */

#ifdef CONFIG_POWER_RT5025
extern int rt5025_charger_reset_and_reinit(struct rt5025_power_info *);
extern int rt5025_ext_set_charging_buck(int);
extern int rt5025_set_charging_buck(struct i2c_client *, int);
extern int rt5025_set_charging_current_switch(struct i2c_client *, int);
extern void rt5025_gauge_set_status(struct rt5025_battery_info *, int);
extern void rt5025_gauge_set_online(struct rt5025_battery_info *, bool);
extern void rt5025_gauge_irq_handler(struct rt5025_battery_info *, u8);
extern int rt5025_power_charge_detect(struct rt5025_power_info *);
extern int rt5025_notify_charging_cable(struct rt5025_swjeita_info *, int);
extern int rt5025_swjeita_irq_handler(struct rt5025_swjeita_info *, unsigned char);
#endif /* CONFIG_POEWR_RT5025 */

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
	printk(KERN_INFO "%s:%s() line-%d: " format, RT5025_DEVICE_NAME,__FUNCTION__,__LINE__, ##args)
#else
#define RTINFO(format,args...)
#endif /* CONFIG_MFD_RT_SHOW_INFO */

#endif /* __LINUX_MFD_RT5025_H */
