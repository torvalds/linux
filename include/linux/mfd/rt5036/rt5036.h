/*
 *  include/linux/mfd/rt5036/rt5036.h
 *  Include header file for Richtek RT5036 Core file
 *
 *  Copyright (C) 2014 Richtek Technology Corp.
 *  cy_huang <cy_huang@richtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#ifndef _LINUX_MFD_RT5036_H
#define _LINUX_MFD_RT5036_H
#include <linux/power_supply.h>

#define RT5036_DEV_NAME		"rt5036"
#define RT5036_DRV_VER		"1.0.9_R"

enum {
	RT5036_ID_DCDC1,
	RT5036_ID_DCDC2,
	RT5036_ID_DCDC3,
	RT5036_ID_DCDC4,
	RT5036_ID_LDO1,
	RT5036_ID_LDO2,
	RT5036_ID_LDO3,
	RT5036_ID_LDO4,
	RT5036_ID_LSW1,
	RT5036_ID_LSW2,
	RT5036_MAX_REGULATOR,
};

enum {
	RT5036_REG_DEVID,
	RT5036_REG_RANGE1START = RT5036_REG_DEVID,
	RT5036_REG_CHGCTL1,
	RT5036_REG_CHGCTL2,
	RT5036_REG_RESV1,
	RT5036_REG_CHGCTL3,
	RT5036_REG_CHGCTL4,
	RT5036_REG_CHGCTL5,
	RT5036_REG_CHGCTL6,
	RT5036_REG_CHGCTL7,
	RT5036_REG_RSTCHG,
	RT5036_REG_RANGE1END = RT5036_REG_RSTCHG,
	RT5036_REG_CHGIRQ1 = 0x10,
	RT5036_REG_RANGE2START = RT5036_REG_CHGIRQ1,
	RT5036_REG_CHGIRQ2,
	RT5036_REG_CHGIRQ3,
	RT5036_REG_CHGIRQMASK1,
	RT5036_REG_CHGIRQMASK2,
	RT5036_REG_CHGIRQMASK3,
	RT5036_REG_CHGSTAT1,
	RT5036_REG_CHGSTAT2,
	RT5036_REG_CHGSTAT2MASK,
	RT5036_REG_RANGE2END = RT5036_REG_CHGSTAT2MASK,
	RT5036_REG_BUCKVN1 = 0x41,
	RT5036_REG_RANGE3START = RT5036_REG_BUCKVN1,
	RT5036_REG_BUCKVN2,
	RT5036_REG_BUCKVN3,
	RT5036_REG_BUCKVN4,
	RT5036_REG_BUCKVRCN,
	RT5036_REG_BUCKVRCNEN,
	RT5036_REG_BUCKMODE,
	RT5036_REG_LDOVN1,
	RT5036_REG_LDOVN2,
	RT5036_REG_LDOVN3,
	RT5036_REG_LDOVN4,
	RT5036_REG_LDOVRCN,
	RT5036_REG_LDOVRCNEN,
	RT5036_REG_LDOMODE,
	RT5036_REG_BUCKLDONEN,
	RT5036_REG_LSWEN,
	RT5036_REG_MISC1,
	RT5036_REG_MISC2,
	RT5036_REG_MISC3,
	RT5036_REG_MISC4,
	RT5036_REG_MISC5,
	RT5036_REG_ONOFFEVENT,
	RT5036_REG_BUCKLDOIRQ,
	RT5036_REG_LSWBASEIRQ,
	RT5036_REG_PWRKEYIRQ,
	RT5036_REG_BUCKLDOIRQMASK,
	RT5036_REG_LSWBASEIRQMASK,
	RT5036_REG_PWRKEYIRQMASK,
	RT5036_REG_RANGE3END = RT5036_REG_PWRKEYIRQMASK,
	RT5036_REG_MISC6 = 0x65,
	RT5036_REG_RANGE4START = RT5036_REG_MISC6,
	RT5036_REG_RANGE4END = RT5036_REG_MISC6,
	RT5036_REG_BUCKVS1 = 0x71,
	RT5036_REG_RANGE5START = RT5036_REG_BUCKVS1,
	RT5036_REG_BUCKVS2,
	RT5036_REG_BUCKVS3,
	RT5036_REG_BUCKVS4,
	RT5036_REG_BUCKVRCS,
	RT5036_REG_BUCKVRCSEN,
	RT5036_REG_RESV2,
	RT5036_REG_LDOVS1,
	RT5036_REG_LDOVS2,
	RT5036_REG_LDOVS3,
	RT5036_REG_LDOVS4,
	RT5036_REG_LDOVRCS,
	RT5036_REG_LDOVRCSEN,
	RT5036_REG_RESV3,
	RT5036_REG_BUCKLDOSEN,
	RT5036_REG_LSWVN2,
	RT5036_REG_LSWVN1,
	RT5036_REG_LSWVS2,
	RT5036_REG_LSWVS1,
	RT5036_REG_LSWVRC,
	RT5036_REG_LSWVRCEN,
	RT5036_REG_BUCKOCPSEL,
	RT5036_REG_RANGE5END = RT5036_REG_BUCKOCPSEL,
	RT5036_REG_RTCADJ = 0x90,
	RT5036_REG_RANGE6START = RT5036_REG_RTCADJ,
	RT5036_REG_RTCTSEC,
	RT5036_REG_RTCTMINUTE,
	RT5036_REG_RTCTHOUR,
	RT5036_REG_RTCTYEAR,
	RT5036_REG_RTCTMON,
	RT5036_REG_RTCTDATEW,
	RT5036_REG_STBMODE,
	RT5036_REG_RTCASEC,
	RT5036_REG_RTCAMINUTE,
	RT5036_REG_RTCAHOUR,
	RT5036_REG_RTCAYEAR,
	RT5036_REG_RTCAMONTH,
	RT5036_REG_RTCADATE,
	RT5036_REG_STBCDSEC,
	RT5036_REG_STBCDMINUTE,
	RT5036_REG_STBCDHOUR,
	RT5036_REG_STBCDDATEL,
	RT5036_REG_STBCDDATEH,
	RT5036_REG_RESV4,
	RT5036_REG_STBWACKIRQ,
	RT5036_REG_STBWACKIRQMASK,
	RT5036_REG_RANGE6END = RT5036_REG_STBWACKIRQMASK,
	RT5036_REG_MAX,
};

enum {
	RT5036_RAMP_25mV,
	RT5036_RAMP_50mV,
	RT5036_RAMP_75mV,
	RT5036_RAMP_100mV,
	RT5036_RAMP_MAX = RT5036_RAMP_100mV,
};

enum {
	RT5036_SHDHPRESS_4S,
	RT5036_SHDNPRESS_6S,
	RT5036_SHDNPRESS_8S,
	RT5036_SHDNPRESS_10S,
	RT5036_SHDNPRESS_MAX = RT5036_SHDNPRESS_10S,
};

enum {
	RT5036_STB_DISABLE,
	RT5036_STB_EN1MS,
	RT5036_STB_EN2MS,
	RT5036_STB_EN4MS,
	RT5036_STB_MAX = RT5036_STB_EN4MS,
};

enum {
	RT5036_SYSLV_2P8V,
	RT5036_SYSLV_2P9V,
	RT5036_SYSLV_3P0V,
	RT5036_SYSLV_3P1V,
	RT5036_SYSLV_3P2V,
	RT5036_SYSLV_3P3V,
	RT5036_SYSLV_3P4V,
	RT5036_SYSLV_3P5V,
	RT5036_SYSLV_MAX = RT5036_SYSLV_3P5V
};

enum {
	RT5036_IPREC_150mA,
	RT5036_IPREC_250mA,
	RT5036_IPREC_350mA,
	RT5036_IPREC_450mA,
	RT5036_IPREC_MAX = RT5036_IPREC_450mA,
};

enum {
	RT5036_IEOC_DISABLE,
	RT5036_IEOC_150mA,
	RT5036_IEOC_200mA,
	RT5036_IEOC_250mA,
	RT5036_IEOC_300mA,
	RT5036_IEOC_400mA,
	RT5036_IEOC_500mA,
	RT5036_IEOC_600mA,
	RT5036_IEOC_MAX = RT5036_IEOC_600mA,
};

enum {
	RT5036_VPREC_2P3V,
	RT5036_VPREC_2P4V,
	RT5036_VPREC_2P5V,
	RT5036_VPREC_2P6V,
	RT5036_VPREC_2P7V,
	RT5036_VPREC_2P8V,
	RT5036_VPREC_2P9V,
	RT5036_VPREC_3P0V,
	RT5036_VPREC_3P1V,
	RT5036_VPREC_3P2V,
	RT5036_VPREC_3P3V,
	RT5036_VPREC_3P4V,
	RT5036_VPREC_3P5V,
	RT5036_VPREC_3P6V,
	RT5036_VPREC_3P7V,
	RT5036_VPREC_3P8V,
	RT5036_VPREC_MAX = RT5036_VPREC_3P8V,
};

enum {
	RT5036_BATLV_2P4V,
	RT5036_BATLV_2P5V,
	RT5036_BATLV_2P6V,
	RT5036_BATLV_2P7V,
	RT5036_BATLV_2P8V,
	RT5036_BATLV_2P9V,
	RT5036_BATLV_3P0V,
	RT5036_BATLV_3P1V,
	RT5036_BATLV_MAX = RT5036_BATLV_3P1V,
};

enum {
	RT5036_VRECHG_0P1V,
	RT5036_VRECHG_0P2V,
	RT5036_VRECHG_0P3V,
	RT5036_VRECHG_0P3V_1,
	RT5036_VRECHG_MAX = RT5036_VRECHG_0P3V_1,
};

typedef void (*rt_irq_handler) (void *info, int eventno);

struct rt5036_regulator_ramp {
	unsigned char nramp_sel:2;
	unsigned char sramp_sel:2;
};

struct rt5036_chg_data {
	int chg_volt;
	int otg_volt;
	int acchg_icc;
	int usbtachg_icc;
	int usbchg_icc;
#ifdef CONFIG_RT_SUPPORT_ACUSB_DUALIN
	int acdet_gpio;
	int usbdet_gpio;
#endif				/* #ifdef CONFIG_RT_SUPPORT_ACUSB_DUALIN */
	u16 te_en:1;
	u16 iprec:2;
	u16 ieoc:3;
	u16 vprec:4;
	u16 batlv:3;
	u16 vrechg:2;
};

struct rt5036_misc_data {
	u16 shdn_press:2;
	u16 stb_en:2;
	u16 lp_enshdn:1;
	u16 vsysuvlo:3;
	u16 syslv_enshdn:1;
};

struct rt5036_irq_data {
	int irq_gpio;
};

struct rt5036_chip;
struct rt5036_platform_data {
	struct regulator_init_data *regulator[RT5036_MAX_REGULATOR];
	struct rt5036_chg_data *chg_pdata;
	struct rt5036_misc_data *misc_pdata;
	struct rt5036_irq_data *irq_pdata;
	int (*pre_init)(struct rt5036_chip *rt5036_chip);
	int (*post_init)(void);
};

struct rt5036_charger_info {
	struct i2c_client *i2c;
	struct device *dev;
	struct power_supply psy;
	struct delayed_work dwork;
	int chg_volt;
	int otg_volt;
	int acchg_icc;
	int usbtachg_icc;
	int usbchg_icc;
	int charge_cable;
#ifdef CONFIG_RT_SUPPORT_ACUSB_DUALIN
	int acdet_gpio;
	int usbdet_gpio;
	int acdet_irq;
	int usbdet_irq;
	unsigned char usbinit_delay:1;
#endif				/* #ifdef CONFIG_RT_SUPPORT_ACUSB_DUALIN */
	unsigned char online:1;
	unsigned char batabs:1;
	unsigned char te_en:1;
	unsigned char otg_en:1;
	unsigned char stat2;
};

struct rt5036_misc_info {
	struct i2c_client *i2c;
	struct device *dev;
#ifdef CONFIG_MISC_RT5036_PWRKEY
	struct input_dev *pwr_key;
	unsigned char pwr_key_pressed:1;
#endif				/* #ifdef CONFIG_MISC_RT5036_PWRKEY */
};

struct rt5036_rtc_info {
	struct i2c_client *i2c;
	struct device *dev;
	struct rtc_device *rtc;
};

struct rt5036_chip {
	struct i2c_client *i2c;
	struct rt5036_charger_info *chg_info;
	struct rt5036_misc_info *misc_info;
	struct rt5036_rtc_info *rtc_info;
	struct mutex io_lock;
	unsigned char suspend:1;
};

#ifdef CONFIG_CHARGER_RT5036
void rt5036_charger_irq_handler(struct rt5036_charger_info *ci,
				unsigned int event);
#endif /* #ifdef CONFIG_CHARGER_RT5036 */
#ifdef CONFIG_MISC_RT5036
void rt5036_misc_irq_handler(struct rt5036_misc_info *mi, unsigned int event);
#endif /* #ifdef CONFIG_MISC_RT5036 */
#ifdef CONFIG_RTC_RT5036
void rt5036_rtc_irq_handler(struct rt5036_rtc_info *ri, unsigned int event);
#endif /* #ifdef CONFIG_RTC_RT5036 */

extern int rt5036_reg_block_read(struct i2c_client *i2c, int reg, int byte,
				 void *dest);
extern int rt5036_reg_block_write(struct i2c_client *i2c, int reg, int byte,
				  void *dest);
extern int rt5036_reg_read(struct i2c_client *i2c, int reg);
extern int rt5036_reg_write(struct i2c_client *i2c, int reg,
			    unsigned char data);
extern int rt5036_assign_bits(struct i2c_client *i2c, int reg,
			      unsigned char mask, unsigned char data);
extern int rt5036_set_bits(struct i2c_client *i2c, int reg, unsigned char mask);
extern int rt5036_clr_bits(struct i2c_client *i2c, int reg, unsigned char mask);

extern int rt5036_core_init(struct device *dev,
			    struct rt5036_platform_data *pdata);
extern int rt5036_core_deinit(struct device *dev);

#ifdef CONFIG_MFD_RT5036_DBGINFO
#define RTINFO(format, args...) \
	pr_info("%s:%s() line-%d: " format, RT5036_DEV_NAME, __func__, \
		__LINE__, ##args)
#else
#define RTINFO(format, args...)
#endif /* CONFIG_MFD_RT5036_DBGINFO */
#endif /* #ifndef _LINUX_MFD_RT5036_H */
