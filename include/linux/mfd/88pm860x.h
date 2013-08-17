/*
 * Marvell 88PM860x Interface
 *
 * Copyright (C) 2009 Marvell International Ltd.
 * 	Haojian Zhuang <haojian.zhuang@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_MFD_88PM860X_H
#define __LINUX_MFD_88PM860X_H

#include <linux/interrupt.h>

#define MFD_NAME_SIZE		(40)

enum {
	CHIP_INVALID = 0,
	CHIP_PM8606,
	CHIP_PM8607,
	CHIP_MAX,
};

enum {
	PM8606_ID_INVALID,
	PM8606_ID_BACKLIGHT,
	PM8606_ID_LED,
	PM8606_ID_VIBRATOR,
	PM8606_ID_TOUCH,
	PM8606_ID_SOUND,
	PM8606_ID_CHARGER,
	PM8606_ID_MAX,
};

enum {
	PM8606_BACKLIGHT1 = 0,
	PM8606_BACKLIGHT2,
	PM8606_BACKLIGHT3,
};

enum {
	PM8606_LED1_RED = 0,
	PM8606_LED1_GREEN,
	PM8606_LED1_BLUE,
	PM8606_LED2_RED,
	PM8606_LED2_GREEN,
	PM8606_LED2_BLUE,
	PM8607_LED_VIBRATOR,
};


/* 8606 Registers */
#define PM8606_DCM_BOOST		(0x00)
#define PM8606_PWM			(0x01)

/* Backlight Registers */
#define PM8606_WLED1A			(0x02)
#define PM8606_WLED1B			(0x03)
#define PM8606_WLED2A			(0x04)
#define PM8606_WLED2B			(0x05)
#define PM8606_WLED3A			(0x06)
#define PM8606_WLED3B			(0x07)

/* LED Registers */
#define PM8606_RGB2A			(0x08)
#define PM8606_RGB2B			(0x09)
#define PM8606_RGB2C			(0x0A)
#define PM8606_RGB2D			(0x0B)
#define PM8606_RGB1A			(0x0C)
#define PM8606_RGB1B			(0x0D)
#define PM8606_RGB1C			(0x0E)
#define PM8606_RGB1D			(0x0F)

#define PM8606_PREREGULATORA		(0x10)
#define PM8606_PREREGULATORB		(0x11)
#define PM8606_VIBRATORA		(0x12)
#define PM8606_VIBRATORB		(0x13)
#define PM8606_VCHG			(0x14)
#define PM8606_VSYS			(0x15)
#define PM8606_MISC			(0x16)
#define PM8606_CHIP_ID			(0x17)
#define PM8606_STATUS			(0x18)
#define PM8606_FLAGS			(0x19)
#define PM8606_PROTECTA			(0x1A)
#define PM8606_PROTECTB			(0x1B)
#define PM8606_PROTECTC			(0x1C)

/* Bit definitions of PM8606 registers */
#define PM8606_DCM_500MA		(0x0)	/* current limit */
#define PM8606_DCM_750MA		(0x1)
#define PM8606_DCM_1000MA		(0x2)
#define PM8606_DCM_1250MA		(0x3)
#define PM8606_DCM_250MV		(0x0 << 2)
#define PM8606_DCM_300MV		(0x1 << 2)
#define PM8606_DCM_350MV		(0x2 << 2)
#define PM8606_DCM_400MV		(0x3 << 2)

#define PM8606_PWM_31200HZ		(0x0)
#define PM8606_PWM_15600HZ		(0x1)
#define PM8606_PWM_7800HZ		(0x2)
#define PM8606_PWM_3900HZ		(0x3)
#define PM8606_PWM_1950HZ		(0x4)
#define PM8606_PWM_976HZ		(0x5)
#define PM8606_PWM_488HZ		(0x6)
#define PM8606_PWM_244HZ		(0x7)
#define PM8606_PWM_FREQ_MASK		(0x7)

#define PM8606_WLED_ON			(1 << 0)
#define PM8606_WLED_CURRENT(x)		((x & 0x1F) << 1)

#define PM8606_LED_CURRENT(x)		(((x >> 2) & 0x07) << 5)

#define PM8606_VSYS_EN			(1 << 1)

#define PM8606_MISC_OSC_EN		(1 << 4)

enum {
	PM8607_ID_BUCK1 = 0,
	PM8607_ID_BUCK2,
	PM8607_ID_BUCK3,

	PM8607_ID_LDO1,
	PM8607_ID_LDO2,
	PM8607_ID_LDO3,
	PM8607_ID_LDO4,
	PM8607_ID_LDO5,
	PM8607_ID_LDO6,
	PM8607_ID_LDO7,
	PM8607_ID_LDO8,
	PM8607_ID_LDO9,
	PM8607_ID_LDO10,
	PM8607_ID_LDO11,
	PM8607_ID_LDO12,
	PM8607_ID_LDO13,
	PM8607_ID_LDO14,
	PM8607_ID_LDO15,

	PM8607_ID_RG_MAX,
};

/* 8607 chip ID is 0x40 or 0x50 */
#define PM8607_VERSION_MASK		(0xF0)	/* 8607 chip ID mask */

/* Interrupt Registers */
#define PM8607_STATUS_1			(0x01)
#define PM8607_STATUS_2			(0x02)
#define PM8607_INT_STATUS1		(0x03)
#define PM8607_INT_STATUS2		(0x04)
#define PM8607_INT_STATUS3		(0x05)
#define PM8607_INT_MASK_1		(0x06)
#define PM8607_INT_MASK_2		(0x07)
#define PM8607_INT_MASK_3		(0x08)

/* Regulator Control Registers */
#define PM8607_LDO1			(0x10)
#define PM8607_LDO2			(0x11)
#define PM8607_LDO3			(0x12)
#define PM8607_LDO4			(0x13)
#define PM8607_LDO5			(0x14)
#define PM8607_LDO6			(0x15)
#define PM8607_LDO7			(0x16)
#define PM8607_LDO8			(0x17)
#define PM8607_LDO9			(0x18)
#define PM8607_LDO10			(0x19)
#define PM8607_LDO12			(0x1A)
#define PM8607_LDO14			(0x1B)
#define PM8607_SLEEP_MODE1		(0x1C)
#define PM8607_SLEEP_MODE2		(0x1D)
#define PM8607_SLEEP_MODE3		(0x1E)
#define PM8607_SLEEP_MODE4		(0x1F)
#define PM8607_GO			(0x20)
#define PM8607_SLEEP_BUCK1		(0x21)
#define PM8607_SLEEP_BUCK2		(0x22)
#define PM8607_SLEEP_BUCK3		(0x23)
#define PM8607_BUCK1			(0x24)
#define PM8607_BUCK2			(0x25)
#define PM8607_BUCK3			(0x26)
#define PM8607_BUCK_CONTROLS		(0x27)
#define PM8607_SUPPLIES_EN11		(0x2B)
#define PM8607_SUPPLIES_EN12		(0x2C)
#define PM8607_GROUP1			(0x2D)
#define PM8607_GROUP2			(0x2E)
#define PM8607_GROUP3			(0x2F)
#define PM8607_GROUP4			(0x30)
#define PM8607_GROUP5			(0x31)
#define PM8607_GROUP6			(0x32)
#define PM8607_SUPPLIES_EN21		(0x33)
#define PM8607_SUPPLIES_EN22		(0x34)

/* Vibrator Control Registers */
#define PM8607_VIBRATOR_SET		(0x28)
#define PM8607_VIBRATOR_PWM		(0x29)

/* GPADC Registers */
#define PM8607_GP_BIAS1			(0x4F)
#define PM8607_MEAS_EN1			(0x50)
#define PM8607_MEAS_EN2			(0x51)
#define PM8607_MEAS_EN3			(0x52)
#define PM8607_MEAS_OFF_TIME1		(0x53)
#define PM8607_MEAS_OFF_TIME2		(0x54)
#define PM8607_TSI_PREBIAS		(0x55)	/* prebias time */
#define PM8607_PD_PREBIAS		(0x56)	/* prebias time */
#define PM8607_GPADC_MISC1		(0x57)

/* RTC Control Registers */
#define PM8607_RTC1			(0xA0)
#define PM8607_RTC_COUNTER1		(0xA1)
#define PM8607_RTC_COUNTER2		(0xA2)
#define PM8607_RTC_COUNTER3		(0xA3)
#define PM8607_RTC_COUNTER4		(0xA4)
#define PM8607_RTC_EXPIRE1		(0xA5)
#define PM8607_RTC_EXPIRE2		(0xA6)
#define PM8607_RTC_EXPIRE3		(0xA7)
#define PM8607_RTC_EXPIRE4		(0xA8)
#define PM8607_RTC_TRIM1		(0xA9)
#define PM8607_RTC_TRIM2		(0xAA)
#define PM8607_RTC_TRIM3		(0xAB)
#define PM8607_RTC_TRIM4		(0xAC)
#define PM8607_RTC_MISC1		(0xAD)
#define PM8607_RTC_MISC2		(0xAE)
#define PM8607_RTC_MISC3		(0xAF)

/* Misc Registers */
#define PM8607_CHIP_ID			(0x00)
#define PM8607_B0_MISC1			(0x0C)
#define PM8607_LDO1			(0x10)
#define PM8607_DVC3			(0x26)
#define PM8607_A1_MISC1			(0x40)

/* bit definitions of Status Query Interface */
#define PM8607_STATUS_CC		(1 << 3)
#define PM8607_STATUS_PEN		(1 << 4)
#define PM8607_STATUS_HEADSET		(1 << 5)
#define PM8607_STATUS_HOOK		(1 << 6)
#define PM8607_STATUS_MICIN		(1 << 7)
#define PM8607_STATUS_ONKEY		(1 << 8)
#define PM8607_STATUS_EXTON		(1 << 9)
#define PM8607_STATUS_CHG		(1 << 10)
#define PM8607_STATUS_BAT		(1 << 11)
#define PM8607_STATUS_VBUS		(1 << 12)
#define PM8607_STATUS_OV		(1 << 13)

/* bit definitions of BUCK3 */
#define PM8607_BUCK3_DOUBLE		(1 << 6)

/* bit definitions of Misc1 */
#define PM8607_A1_MISC1_PI2C		(1 << 0)
#define PM8607_B0_MISC1_INV_INT		(1 << 0)
#define PM8607_B0_MISC1_INT_CLEAR	(1 << 1)
#define PM8607_B0_MISC1_INT_MASK	(1 << 2)
#define PM8607_B0_MISC1_PI2C		(1 << 3)
#define PM8607_B0_MISC1_RESET		(1 << 6)

/* bits definitions of GPADC */
#define PM8607_GPADC_EN			(1 << 0)
#define PM8607_GPADC_PREBIAS_MASK	(3 << 1)
#define PM8607_GPADC_SLOT_CYCLE_MASK	(3 << 3)	/* slow mode */
#define PM8607_GPADC_OFF_SCALE_MASK	(3 << 5)	/* GP sleep mode */
#define PM8607_GPADC_SW_CAL_MASK	(1 << 7)

#define PM8607_PD_PREBIAS_MASK		(0x1F << 0)
#define PM8607_PD_PRECHG_MASK		(7 << 5)

#define PM8606_REF_GP_OSC_OFF         0
#define PM8606_REF_GP_OSC_ON          1
#define PM8606_REF_GP_OSC_UNKNOWN     2

/* Clients of reference group and 8MHz oscillator in 88PM8606 */
enum pm8606_ref_gp_and_osc_clients {
	REF_GP_NO_CLIENTS       = 0,
	WLED1_DUTY              = (1<<0), /*PF 0x02.7:0*/
	WLED2_DUTY              = (1<<1), /*PF 0x04.7:0*/
	WLED3_DUTY              = (1<<2), /*PF 0x06.7:0*/
	RGB1_ENABLE             = (1<<3), /*PF 0x07.1*/
	RGB2_ENABLE             = (1<<4), /*PF 0x07.2*/
	LDO_VBR_EN              = (1<<5), /*PF 0x12.0*/
	REF_GP_MAX_CLIENT       = 0xFFFF
};

/* Interrupt Number in 88PM8607 */
enum {
	PM8607_IRQ_ONKEY,
	PM8607_IRQ_EXTON,
	PM8607_IRQ_CHG,
	PM8607_IRQ_BAT,
	PM8607_IRQ_RTC,
	PM8607_IRQ_CC,
	PM8607_IRQ_VBAT,
	PM8607_IRQ_VCHG,
	PM8607_IRQ_VSYS,
	PM8607_IRQ_TINT,
	PM8607_IRQ_GPADC0,
	PM8607_IRQ_GPADC1,
	PM8607_IRQ_GPADC2,
	PM8607_IRQ_GPADC3,
	PM8607_IRQ_AUDIO_SHORT,
	PM8607_IRQ_PEN,
	PM8607_IRQ_HEADSET,
	PM8607_IRQ_HOOK,
	PM8607_IRQ_MICIN,
	PM8607_IRQ_CHG_FAIL,
	PM8607_IRQ_CHG_DONE,
	PM8607_IRQ_CHG_FAULT,
};

enum {
	PM8607_CHIP_A0 = 0x40,
	PM8607_CHIP_A1 = 0x41,
	PM8607_CHIP_B0 = 0x48,
};

struct pm860x_chip {
	struct device		*dev;
	struct mutex		irq_lock;
	struct mutex		osc_lock;
	struct i2c_client	*client;
	struct i2c_client	*companion;	/* companion chip client */
	struct regmap           *regmap;
	struct regmap           *regmap_companion;

	int			buck3_double;	/* DVC ramp slope double */
	unsigned short		companion_addr;
	unsigned short		osc_vote;
	int			id;
	int			irq_mode;
	int			irq_base;
	int			core_irq;
	unsigned char		chip_version;
	unsigned char		osc_status;

	unsigned int            wakeup_flag;
};

enum {
	GI2C_PORT = 0,
	PI2C_PORT,
};

struct pm860x_backlight_pdata {
	int		id;
	int		pwm;
	int		iset;
	unsigned long	flags;
};

struct pm860x_led_pdata {
	int		id;
	int		iset;
	unsigned long	flags;
};

struct pm860x_rtc_pdata {
	int		(*sync)(unsigned int ticks);
	int		vrtc;
};

struct pm860x_touch_pdata {
	int		gpadc_prebias;
	int		slot_cycle;
	int		off_scale;
	int		sw_cal;
	int		tsi_prebias;	/* time, slot */
	int		pen_prebias;	/* time, slot */
	int		pen_prechg;	/* time, slot */
	int		res_x;		/* resistor of Xplate */
	unsigned long	flags;
};

struct pm860x_power_pdata {
	unsigned	fast_charge;	/* charge current */
};

struct pm860x_platform_data {
	struct pm860x_backlight_pdata	*backlight;
	struct pm860x_led_pdata		*led;
	struct pm860x_rtc_pdata		*rtc;
	struct pm860x_touch_pdata	*touch;
	struct pm860x_power_pdata	*power;
	struct regulator_init_data	*regulator;

	unsigned short	companion_addr;	/* I2C address of companion chip */
	int		i2c_port;	/* Controlled by GI2C or PI2C */
	int		irq_mode;	/* Clear interrupt by read/write(0/1) */
	int		irq_base;	/* IRQ base number of 88pm860x */
	int		num_leds;
	int		num_backlights;
	int		num_regulators;
};

extern int pm8606_osc_enable(struct pm860x_chip *, unsigned short);
extern int pm8606_osc_disable(struct pm860x_chip *, unsigned short);

extern int pm860x_reg_read(struct i2c_client *, int);
extern int pm860x_reg_write(struct i2c_client *, int, unsigned char);
extern int pm860x_bulk_read(struct i2c_client *, int, int, unsigned char *);
extern int pm860x_bulk_write(struct i2c_client *, int, int, unsigned char *);
extern int pm860x_set_bits(struct i2c_client *, int, unsigned char,
			   unsigned char);
extern int pm860x_page_reg_read(struct i2c_client *, int);
extern int pm860x_page_reg_write(struct i2c_client *, int, unsigned char);
extern int pm860x_page_bulk_read(struct i2c_client *, int, int,
				 unsigned char *);
extern int pm860x_page_bulk_write(struct i2c_client *, int, int,
				  unsigned char *);
extern int pm860x_page_set_bits(struct i2c_client *, int, unsigned char,
				unsigned char);

extern int pm860x_device_init(struct pm860x_chip *chip,
			      struct pm860x_platform_data *pdata) __devinit ;
extern void pm860x_device_exit(struct pm860x_chip *chip) __devexit ;

#endif /* __LINUX_MFD_88PM860X_H */
