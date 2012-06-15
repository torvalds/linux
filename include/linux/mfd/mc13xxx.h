/*
 * Copyright 2009-2010 Pengutronix
 * Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#ifndef __LINUX_MFD_MC13XXX_H
#define __LINUX_MFD_MC13XXX_H

#include <linux/interrupt.h>

struct mc13xxx;

void mc13xxx_lock(struct mc13xxx *mc13xxx);
void mc13xxx_unlock(struct mc13xxx *mc13xxx);

int mc13xxx_reg_read(struct mc13xxx *mc13xxx, unsigned int offset, u32 *val);
int mc13xxx_reg_write(struct mc13xxx *mc13xxx, unsigned int offset, u32 val);
int mc13xxx_reg_rmw(struct mc13xxx *mc13xxx, unsigned int offset,
		u32 mask, u32 val);

int mc13xxx_get_flags(struct mc13xxx *mc13xxx);

int mc13xxx_irq_request(struct mc13xxx *mc13xxx, int irq,
		irq_handler_t handler, const char *name, void *dev);
int mc13xxx_irq_request_nounmask(struct mc13xxx *mc13xxx, int irq,
		irq_handler_t handler, const char *name, void *dev);
int mc13xxx_irq_free(struct mc13xxx *mc13xxx, int irq, void *dev);

int mc13xxx_irq_mask(struct mc13xxx *mc13xxx, int irq);
int mc13xxx_irq_unmask(struct mc13xxx *mc13xxx, int irq);
int mc13xxx_irq_status(struct mc13xxx *mc13xxx, int irq,
		int *enabled, int *pending);
int mc13xxx_irq_ack(struct mc13xxx *mc13xxx, int irq);

int mc13xxx_get_flags(struct mc13xxx *mc13xxx);

int mc13xxx_adc_do_conversion(struct mc13xxx *mc13xxx,
		unsigned int mode, unsigned int channel,
		u8 ato, bool atox, unsigned int *sample);

#define MC13XXX_IRQ_ADCDONE	0
#define MC13XXX_IRQ_ADCBISDONE	1
#define MC13XXX_IRQ_TS		2
#define MC13XXX_IRQ_CHGDET	6
#define MC13XXX_IRQ_CHGREV	8
#define MC13XXX_IRQ_CHGSHORT	9
#define MC13XXX_IRQ_CCCV	10
#define MC13XXX_IRQ_CHGCURR	11
#define MC13XXX_IRQ_BPON	12
#define MC13XXX_IRQ_LOBATL	13
#define MC13XXX_IRQ_LOBATH	14
#define MC13XXX_IRQ_1HZ		24
#define MC13XXX_IRQ_TODA	25
#define MC13XXX_IRQ_SYSRST	30
#define MC13XXX_IRQ_RTCRST	31
#define MC13XXX_IRQ_PC		32
#define MC13XXX_IRQ_WARM	33
#define MC13XXX_IRQ_MEMHLD	34
#define MC13XXX_IRQ_THWARNL	36
#define MC13XXX_IRQ_THWARNH	37
#define MC13XXX_IRQ_CLK		38

#define MC13XXX_NUM_IRQ		46

struct regulator_init_data;

struct mc13xxx_regulator_init_data {
	int id;
	struct regulator_init_data *init_data;
	struct device_node *node;
};

struct mc13xxx_regulator_platform_data {
	int num_regulators;
	struct mc13xxx_regulator_init_data *regulators;
};

struct mc13xxx_led_platform_data {
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

struct mc13xxx_leds_platform_data {
	int num_leds;
	struct mc13xxx_led_platform_data *led;

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

struct mc13xxx_buttons_platform_data {
#define MC13783_BUTTON_DBNC_0MS		0
#define MC13783_BUTTON_DBNC_30MS	1
#define MC13783_BUTTON_DBNC_150MS	2
#define MC13783_BUTTON_DBNC_750MS	3
#define MC13783_BUTTON_ENABLE		(1 << 2)
#define MC13783_BUTTON_POL_INVERT	(1 << 3)
#define MC13783_BUTTON_RESET_EN		(1 << 4)
	int b1on_flags;
	unsigned short b1on_key;
	int b2on_flags;
	unsigned short b2on_key;
	int b3on_flags;
	unsigned short b3on_key;
};

struct mc13xxx_ts_platform_data {
	/* Delay between Touchscreen polarization and ADC Conversion.
	 * Given in clock ticks of a 32 kHz clock which gives a granularity of
	 * about 30.5ms */
	u8 ato;

#define MC13783_TS_ATO_FIRST false
#define MC13783_TS_ATO_EACH  true
	/* Use the ATO delay only for the first conversion or for each one */
	bool atox;
};

enum mc13783_ssi_port {
	MC13783_SSI1_PORT,
	MC13783_SSI2_PORT,
};

struct mc13xxx_codec_platform_data {
	enum mc13783_ssi_port adc_ssi_port;
	enum mc13783_ssi_port dac_ssi_port;
};

struct mc13xxx_platform_data {
#define MC13XXX_USE_TOUCHSCREEN (1 << 0)
#define MC13XXX_USE_CODEC	(1 << 1)
#define MC13XXX_USE_ADC		(1 << 2)
#define MC13XXX_USE_RTC		(1 << 3)
	unsigned int flags;

	struct mc13xxx_regulator_platform_data regulators;
	struct mc13xxx_leds_platform_data *leds;
	struct mc13xxx_buttons_platform_data *buttons;
	struct mc13xxx_ts_platform_data touch;
	struct mc13xxx_codec_platform_data *codec;
};

#define MC13XXX_ADC_MODE_TS		1
#define MC13XXX_ADC_MODE_SINGLE_CHAN	2
#define MC13XXX_ADC_MODE_MULT_CHAN	3

#define MC13XXX_ADC0		43
#define MC13XXX_ADC0_LICELLCON		(1 << 0)
#define MC13XXX_ADC0_CHRGICON		(1 << 1)
#define MC13XXX_ADC0_BATICON		(1 << 2)
#define MC13XXX_ADC0_ADREFEN		(1 << 10)
#define MC13XXX_ADC0_TSMOD0		(1 << 12)
#define MC13XXX_ADC0_TSMOD1		(1 << 13)
#define MC13XXX_ADC0_TSMOD2		(1 << 14)
#define MC13XXX_ADC0_ADINC1		(1 << 16)
#define MC13XXX_ADC0_ADINC2		(1 << 17)

#define MC13XXX_ADC0_TSMOD_MASK		(MC13XXX_ADC0_TSMOD0 | \
					MC13XXX_ADC0_TSMOD1 | \
					MC13XXX_ADC0_TSMOD2)

#define MC13XXX_ADC0_CONFIG_MASK	(MC13XXX_ADC0_TSMOD_MASK | \
					MC13XXX_ADC0_LICELLCON | \
					MC13XXX_ADC0_CHRGICON | \
					MC13XXX_ADC0_BATICON)

#endif /* ifndef __LINUX_MFD_MC13XXX_H */
