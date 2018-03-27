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

int mc13xxx_irq_request(struct mc13xxx *mc13xxx, int irq,
		irq_handler_t handler, const char *name, void *dev);
int mc13xxx_irq_free(struct mc13xxx *mc13xxx, int irq, void *dev);

int mc13xxx_irq_status(struct mc13xxx *mc13xxx, int irq,
		int *enabled, int *pending);

int mc13xxx_get_flags(struct mc13xxx *mc13xxx);

int mc13xxx_adc_do_conversion(struct mc13xxx *mc13xxx,
		unsigned int mode, unsigned int channel,
		u8 ato, bool atox, unsigned int *sample);

/* Deprecated calls */
static inline int mc13xxx_irq_ack(struct mc13xxx *mc13xxx, int irq)
{
	return 0;
}

static inline int mc13xxx_irq_request_nounmask(struct mc13xxx *mc13xxx, int irq,
					       irq_handler_t handler,
					       const char *name, void *dev)
{
	return mc13xxx_irq_request(mc13xxx, irq, handler, name, dev);
}

int mc13xxx_irq_mask(struct mc13xxx *mc13xxx, int irq);
int mc13xxx_irq_unmask(struct mc13xxx *mc13xxx, int irq);

#define MC13783_AUDIO_RX0	36
#define MC13783_AUDIO_RX1	37
#define MC13783_AUDIO_TX	38
#define MC13783_SSI_NETWORK	39
#define MC13783_AUDIO_CODEC	40
#define MC13783_AUDIO_DAC	41

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

enum {
	/* MC13783 LED IDs */
	MC13783_LED_MD,
	MC13783_LED_AD,
	MC13783_LED_KP,
	MC13783_LED_R1,
	MC13783_LED_G1,
	MC13783_LED_B1,
	MC13783_LED_R2,
	MC13783_LED_G2,
	MC13783_LED_B2,
	MC13783_LED_R3,
	MC13783_LED_G3,
	MC13783_LED_B3,
	/* MC13892 LED IDs */
	MC13892_LED_MD,
	MC13892_LED_AD,
	MC13892_LED_KP,
	MC13892_LED_R,
	MC13892_LED_G,
	MC13892_LED_B,
	/* MC34708 LED IDs */
	MC34708_LED_R,
	MC34708_LED_G,
};

struct mc13xxx_led_platform_data {
	int id;
	const char *name;
	const char *default_trigger;
};

#define MAX_LED_CONTROL_REGS	6

/* MC13783 LED Control 0 */
#define MC13783_LED_C0_ENABLE		(1 << 0)
#define MC13783_LED_C0_TRIODE_MD	(1 << 7)
#define MC13783_LED_C0_TRIODE_AD	(1 << 8)
#define MC13783_LED_C0_TRIODE_KP	(1 << 9)
#define MC13783_LED_C0_BOOST		(1 << 10)
#define MC13783_LED_C0_ABMODE(x)	(((x) & 0x7) << 11)
#define MC13783_LED_C0_ABREF(x)		(((x) & 0x3) << 14)
/* MC13783 LED Control 1 */
#define MC13783_LED_C1_TC1HALF		(1 << 18)
#define MC13783_LED_C1_SLEWLIM		(1 << 23)
/* MC13783 LED Control 2 */
#define MC13783_LED_C2_CURRENT_MD(x)	(((x) & 0x7) << 0)
#define MC13783_LED_C2_CURRENT_AD(x)	(((x) & 0x7) << 3)
#define MC13783_LED_C2_CURRENT_KP(x)	(((x) & 0x7) << 6)
#define MC13783_LED_C2_PERIOD(x)	(((x) & 0x3) << 21)
#define MC13783_LED_C2_SLEWLIM		(1 << 23)
/* MC13783 LED Control 3 */
#define MC13783_LED_C3_CURRENT_R1(x)	(((x) & 0x3) << 0)
#define MC13783_LED_C3_CURRENT_G1(x)	(((x) & 0x3) << 2)
#define MC13783_LED_C3_CURRENT_B1(x)	(((x) & 0x3) << 4)
#define MC13783_LED_C3_PERIOD(x)	(((x) & 0x3) << 21)
#define MC13783_LED_C3_TRIODE_TC1	(1 << 23)
/* MC13783 LED Control 4 */
#define MC13783_LED_C4_CURRENT_R2(x)	(((x) & 0x3) << 0)
#define MC13783_LED_C4_CURRENT_G2(x)	(((x) & 0x3) << 2)
#define MC13783_LED_C4_CURRENT_B2(x)	(((x) & 0x3) << 4)
#define MC13783_LED_C4_PERIOD(x)	(((x) & 0x3) << 21)
#define MC13783_LED_C4_TRIODE_TC2	(1 << 23)
/* MC13783 LED Control 5 */
#define MC13783_LED_C5_CURRENT_R3(x)	(((x) & 0x3) << 0)
#define MC13783_LED_C5_CURRENT_G3(x)	(((x) & 0x3) << 2)
#define MC13783_LED_C5_CURRENT_B3(x)	(((x) & 0x3) << 4)
#define MC13783_LED_C5_PERIOD(x)	(((x) & 0x3) << 21)
#define MC13783_LED_C5_TRIODE_TC3	(1 << 23)
/* MC13892 LED Control 0 */
#define MC13892_LED_C0_CURRENT_MD(x)	(((x) & 0x7) << 9)
#define MC13892_LED_C0_CURRENT_AD(x)	(((x) & 0x7) << 21)
/* MC13892 LED Control 1 */
#define MC13892_LED_C1_CURRENT_KP(x)	(((x) & 0x7) << 9)
/* MC13892 LED Control 2 */
#define MC13892_LED_C2_CURRENT_R(x)	(((x) & 0x7) << 9)
#define MC13892_LED_C2_CURRENT_G(x)	(((x) & 0x7) << 21)
/* MC13892 LED Control 3 */
#define MC13892_LED_C3_CURRENT_B(x)	(((x) & 0x7) << 9)
/* MC34708 LED Control 0 */
#define MC34708_LED_C0_CURRENT_R(x)	(((x) & 0x3) << 9)
#define MC34708_LED_C0_CURRENT_G(x)	(((x) & 0x3) << 21)

struct mc13xxx_leds_platform_data {
	struct mc13xxx_led_platform_data *led;
	int num_leds;
	u32 led_control[MAX_LED_CONTROL_REGS];
};

#define MC13783_BUTTON_DBNC_0MS		0
#define MC13783_BUTTON_DBNC_30MS	1
#define MC13783_BUTTON_DBNC_150MS	2
#define MC13783_BUTTON_DBNC_750MS	3
#define MC13783_BUTTON_ENABLE		(1 << 2)
#define MC13783_BUTTON_POL_INVERT	(1 << 3)
#define MC13783_BUTTON_RESET_EN		(1 << 4)

struct mc13xxx_buttons_platform_data {
	int b1on_flags;
	unsigned short b1on_key;
	int b2on_flags;
	unsigned short b2on_key;
	int b3on_flags;
	unsigned short b3on_key;
};

#define MC13783_TS_ATO_FIRST	false
#define MC13783_TS_ATO_EACH	true

struct mc13xxx_ts_platform_data {
	/* Delay between Touchscreen polarization and ADC Conversion.
	 * Given in clock ticks of a 32 kHz clock which gives a granularity of
	 * about 30.5ms */
	u8 ato;
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

#define MC13XXX_USE_TOUCHSCREEN	(1 << 0)
#define MC13XXX_USE_CODEC	(1 << 1)
#define MC13XXX_USE_ADC		(1 << 2)
#define MC13XXX_USE_RTC		(1 << 3)

struct mc13xxx_platform_data {
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
#define MC13XXX_ADC0_ADIN7SEL_DIE	(1 << 4)
#define MC13XXX_ADC0_ADIN7SEL_UID	(2 << 4)
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
