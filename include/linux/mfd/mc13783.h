/*
 * Copyright 2009 Pengutronix
 * Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#ifndef __LINUX_MFD_MC13783_H
#define __LINUX_MFD_MC13783_H

#include <linux/interrupt.h>

struct mc13783;

void mc13783_lock(struct mc13783 *mc13783);
void mc13783_unlock(struct mc13783 *mc13783);

int mc13783_reg_read(struct mc13783 *mc13783, unsigned int offset, u32 *val);
int mc13783_reg_write(struct mc13783 *mc13783, unsigned int offset, u32 val);
int mc13783_reg_rmw(struct mc13783 *mc13783, unsigned int offset,
		u32 mask, u32 val);

int mc13783_get_flags(struct mc13783 *mc13783);

int mc13783_irq_request(struct mc13783 *mc13783, int irq,
		irq_handler_t handler, const char *name, void *dev);
int mc13783_irq_request_nounmask(struct mc13783 *mc13783, int irq,
		irq_handler_t handler, const char *name, void *dev);
int mc13783_irq_free(struct mc13783 *mc13783, int irq, void *dev);

int mc13783_irq_mask(struct mc13783 *mc13783, int irq);
int mc13783_irq_unmask(struct mc13783 *mc13783, int irq);
int mc13783_irq_status(struct mc13783 *mc13783, int irq,
		int *enabled, int *pending);
int mc13783_irq_ack(struct mc13783 *mc13783, int irq);

static inline int mc13783_mask(struct mc13783 *mc13783, int irq) __deprecated;
static inline int mc13783_mask(struct mc13783 *mc13783, int irq)
{
	return mc13783_irq_mask(mc13783, irq);
}

static inline int mc13783_unmask(struct mc13783 *mc13783, int irq) __deprecated;
static inline int mc13783_unmask(struct mc13783 *mc13783, int irq)
{
	return mc13783_irq_unmask(mc13783, irq);
}

static inline int mc13783_ackirq(struct mc13783 *mc13783, int irq) __deprecated;
static inline int mc13783_ackirq(struct mc13783 *mc13783, int irq)
{
	return mc13783_irq_ack(mc13783, irq);
}

#define MC13783_ADC0		43
#define MC13783_ADC0_ADREFEN		(1 << 10)
#define MC13783_ADC0_ADREFMODE		(1 << 11)
#define MC13783_ADC0_TSMOD0		(1 << 12)
#define MC13783_ADC0_TSMOD1		(1 << 13)
#define MC13783_ADC0_TSMOD2		(1 << 14)
#define MC13783_ADC0_ADINC1		(1 << 16)
#define MC13783_ADC0_ADINC2		(1 << 17)

#define MC13783_ADC0_TSMOD_MASK		(MC13783_ADC0_TSMOD0 | \
					MC13783_ADC0_TSMOD1 | \
					MC13783_ADC0_TSMOD2)

struct mc13783_led_platform_data {
#define MC13783_LED_MD		0
#define MC13783_LED_AD		1
#define MC13783_LED_KP		2
#define MC13783_LED_R1		3
#define MC13783_LED_G1		4
#define MC13783_LED_B1		5
#define MC13783_LED_R2		6
#define MC13783_LED_G2		7
#define MC13783_LED_B2		8
#define MC13783_LED_R3		9
#define MC13783_LED_G3		10
#define MC13783_LED_B3		11
#define MC13783_LED_MAX MC13783_LED_B3
	int id;
	const char *name;
	const char *default_trigger;

/* Three or two bits current selection depending on the led */
	char max_current;
};

struct mc13783_leds_platform_data {
	int num_leds;
	struct mc13783_led_platform_data *led;

#define MC13783_LED_TRIODE_MD	(1 << 0)
#define MC13783_LED_TRIODE_AD	(1 << 1)
#define MC13783_LED_TRIODE_KP	(1 << 2)
#define MC13783_LED_BOOST_EN	(1 << 3)
#define MC13783_LED_TC1HALF	(1 << 4)
#define MC13783_LED_SLEWLIMTC	(1 << 5)
#define MC13783_LED_SLEWLIMBL	(1 << 6)
#define MC13783_LED_TRIODE_TC1	(1 << 7)
#define MC13783_LED_TRIODE_TC2	(1 << 8)
#define MC13783_LED_TRIODE_TC3	(1 << 9)
	int flags;

#define MC13783_LED_AB_DISABLED		0
#define MC13783_LED_AB_MD1		1
#define MC13783_LED_AB_MD12		2
#define MC13783_LED_AB_MD123		3
#define MC13783_LED_AB_MD1234		4
#define MC13783_LED_AB_MD1234_AD1	5
#define MC13783_LED_AB_MD1234_AD12	6
#define MC13783_LED_AB_MD1_AD		7
	char abmode;

#define MC13783_LED_ABREF_200MV	0
#define MC13783_LED_ABREF_400MV	1
#define MC13783_LED_ABREF_600MV	2
#define MC13783_LED_ABREF_800MV	3
	char abref;

#define MC13783_LED_PERIOD_10MS		0
#define MC13783_LED_PERIOD_100MS	1
#define MC13783_LED_PERIOD_500MS	2
#define MC13783_LED_PERIOD_2S		3
	char bl_period;
	char tc1_period;
	char tc2_period;
	char tc3_period;
};

/* to be cleaned up */
struct regulator_init_data;

struct mc13783_regulator_init_data {
	int id;
	struct regulator_init_data *init_data;
};

struct mc13783_regulator_platform_data {
	int num_regulators;
	struct mc13783_regulator_init_data *regulators;
};

struct mc13783_platform_data {
	int num_regulators;
	struct mc13783_regulator_init_data *regulators;
	struct mc13783_leds_platform_data *leds;

#define MC13783_USE_TOUCHSCREEN (1 << 0)
#define MC13783_USE_CODEC	(1 << 1)
#define MC13783_USE_ADC		(1 << 2)
#define MC13783_USE_RTC		(1 << 3)
#define MC13783_USE_REGULATOR	(1 << 4)
#define MC13783_USE_LED		(1 << 5)
	unsigned int flags;
};

#define MC13783_ADC_MODE_TS		1
#define MC13783_ADC_MODE_SINGLE_CHAN	2
#define MC13783_ADC_MODE_MULT_CHAN	3

int mc13783_adc_do_conversion(struct mc13783 *mc13783, unsigned int mode,
		unsigned int channel, unsigned int *sample);


#define	MC13783_SW_SW1A		0
#define	MC13783_SW_SW1B		1
#define	MC13783_SW_SW2A		2
#define	MC13783_SW_SW2B		3
#define	MC13783_SW_SW3		4
#define	MC13783_SW_PLL		5
#define	MC13783_REGU_VAUDIO	6
#define	MC13783_REGU_VIOHI	7
#define	MC13783_REGU_VIOLO	8
#define	MC13783_REGU_VDIG	9
#define	MC13783_REGU_VGEN	10
#define	MC13783_REGU_VRFDIG	11
#define	MC13783_REGU_VRFREF	12
#define	MC13783_REGU_VRFCP	13
#define	MC13783_REGU_VSIM	14
#define	MC13783_REGU_VESIM	15
#define	MC13783_REGU_VCAM	16
#define	MC13783_REGU_VRFBG	17
#define	MC13783_REGU_VVIB	18
#define	MC13783_REGU_VRF1	19
#define	MC13783_REGU_VRF2	20
#define	MC13783_REGU_VMMC1	21
#define	MC13783_REGU_VMMC2	22
#define	MC13783_REGU_GPO1	23
#define	MC13783_REGU_GPO2	24
#define	MC13783_REGU_GPO3	25
#define	MC13783_REGU_GPO4	26
#define	MC13783_REGU_V1		27
#define	MC13783_REGU_V2		28
#define	MC13783_REGU_V3		29
#define	MC13783_REGU_V4		30
#define	MC13783_REGU_PWGT1SPI	31
#define	MC13783_REGU_PWGT2SPI	32

#define MC13783_IRQ_ADCDONE	0
#define MC13783_IRQ_ADCBISDONE	1
#define MC13783_IRQ_TS		2
#define MC13783_IRQ_WHIGH	3
#define MC13783_IRQ_WLOW	4
#define MC13783_IRQ_CHGDET	6
#define MC13783_IRQ_CHGOV	7
#define MC13783_IRQ_CHGREV	8
#define MC13783_IRQ_CHGSHORT	9
#define MC13783_IRQ_CCCV	10
#define MC13783_IRQ_CHGCURR	11
#define MC13783_IRQ_BPON	12
#define MC13783_IRQ_LOBATL	13
#define MC13783_IRQ_LOBATH	14
#define MC13783_IRQ_UDP		15
#define MC13783_IRQ_USB		16
#define MC13783_IRQ_ID		19
#define MC13783_IRQ_SE1		21
#define MC13783_IRQ_CKDET	22
#define MC13783_IRQ_UDM		23
#define MC13783_IRQ_1HZ		24
#define MC13783_IRQ_TODA	25
#define MC13783_IRQ_ONOFD1	27
#define MC13783_IRQ_ONOFD2	28
#define MC13783_IRQ_ONOFD3	29
#define MC13783_IRQ_SYSRST	30
#define MC13783_IRQ_RTCRST	31
#define MC13783_IRQ_PC		32
#define MC13783_IRQ_WARM	33
#define MC13783_IRQ_MEMHLD	34
#define MC13783_IRQ_PWRRDY	35
#define MC13783_IRQ_THWARNL	36
#define MC13783_IRQ_THWARNH	37
#define MC13783_IRQ_CLK		38
#define MC13783_IRQ_SEMAF	39
#define MC13783_IRQ_MC2B	41
#define MC13783_IRQ_HSDET	42
#define MC13783_IRQ_HSL		43
#define MC13783_IRQ_ALSPTH	44
#define MC13783_IRQ_AHSSHORT	45
#define MC13783_NUM_IRQ		46

#endif /* __LINUX_MFD_MC13783_H */
