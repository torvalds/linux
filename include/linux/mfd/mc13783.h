/*
 * Copyright 2009-2010 Pengutronix
 * Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#ifndef __LINUX_MFD_MC13783_H
#define __LINUX_MFD_MC13783_H

#include <linux/mfd/mc13xxx.h>

struct mc13783;

struct mc13xxx *mc13783_to_mc13xxx(struct mc13783 *mc13783);

static inline void mc13783_lock(struct mc13783 *mc13783)
{
	mc13xxx_lock(mc13783_to_mc13xxx(mc13783));
}

static inline void mc13783_unlock(struct mc13783 *mc13783)
{
	mc13xxx_unlock(mc13783_to_mc13xxx(mc13783));
}

static inline int mc13783_reg_read(struct mc13783 *mc13783,
		unsigned int offset, u32 *val)
{
	return mc13xxx_reg_read(mc13783_to_mc13xxx(mc13783), offset, val);
}

static inline int mc13783_reg_write(struct mc13783 *mc13783,
		unsigned int offset, u32 val)
{
	return mc13xxx_reg_write(mc13783_to_mc13xxx(mc13783), offset, val);
}

static inline int mc13783_reg_rmw(struct mc13783 *mc13783,
		unsigned int offset, u32 mask, u32 val)
{
	return mc13xxx_reg_rmw(mc13783_to_mc13xxx(mc13783), offset, mask, val);
}

static inline int mc13783_get_flags(struct mc13783 *mc13783)
{
	return mc13xxx_get_flags(mc13783_to_mc13xxx(mc13783));
}

static inline int mc13783_irq_request(struct mc13783 *mc13783, int irq,
		irq_handler_t handler, const char *name, void *dev)
{
	return mc13xxx_irq_request(mc13783_to_mc13xxx(mc13783), irq,
			handler, name, dev);
}

static inline int mc13783_irq_request_nounmask(struct mc13783 *mc13783, int irq,
		irq_handler_t handler, const char *name, void *dev)
{
	return mc13xxx_irq_request_nounmask(mc13783_to_mc13xxx(mc13783), irq,
			handler, name, dev);
}

static inline int mc13783_irq_free(struct mc13783 *mc13783, int irq, void *dev)
{
	return mc13xxx_irq_free(mc13783_to_mc13xxx(mc13783), irq, dev);
}

static inline int mc13783_irq_mask(struct mc13783 *mc13783, int irq)
{
	return mc13xxx_irq_mask(mc13783_to_mc13xxx(mc13783), irq);
}

static inline int mc13783_irq_unmask(struct mc13783 *mc13783, int irq)
{
	return mc13xxx_irq_unmask(mc13783_to_mc13xxx(mc13783), irq);
}
static inline int mc13783_irq_status(struct mc13783 *mc13783, int irq,
		int *enabled, int *pending)
{
	return mc13xxx_irq_status(mc13783_to_mc13xxx(mc13783),
			irq, enabled, pending);
}

static inline int mc13783_irq_ack(struct mc13783 *mc13783, int irq)
{
	return mc13xxx_irq_ack(mc13783_to_mc13xxx(mc13783), irq);
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

#define mc13783_regulator_init_data mc13xxx_regulator_init_data
#define mc13783_regulator_platform_data mc13xxx_regulator_platform_data
#define mc13783_led_platform_data mc13xxx_led_platform_data
#define mc13783_leds_platform_data mc13xxx_leds_platform_data

#define mc13783_platform_data mc13xxx_platform_data
#define MC13783_USE_TOUCHSCREEN	MC13XXX_USE_TOUCHSCREEN
#define MC13783_USE_CODEC	MC13XXX_USE_CODEC
#define MC13783_USE_ADC		MC13XXX_USE_ADC
#define MC13783_USE_RTC		MC13XXX_USE_RTC
#define MC13783_USE_REGULATOR	MC13XXX_USE_REGULATOR
#define MC13783_USE_LED		MC13XXX_USE_LED

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

#define MC13783_IRQ_ADCDONE	MC13XXX_IRQ_ADCDONE
#define MC13783_IRQ_ADCBISDONE	MC13XXX_IRQ_ADCBISDONE
#define MC13783_IRQ_TS		MC13XXX_IRQ_TS
#define MC13783_IRQ_WHIGH	3
#define MC13783_IRQ_WLOW	4
#define MC13783_IRQ_CHGDET	MC13XXX_IRQ_CHGDET
#define MC13783_IRQ_CHGOV	7
#define MC13783_IRQ_CHGREV	MC13XXX_IRQ_CHGREV
#define MC13783_IRQ_CHGSHORT	MC13XXX_IRQ_CHGSHORT
#define MC13783_IRQ_CCCV	MC13XXX_IRQ_CCCV
#define MC13783_IRQ_CHGCURR	MC13XXX_IRQ_CHGCURR
#define MC13783_IRQ_BPON	MC13XXX_IRQ_BPON
#define MC13783_IRQ_LOBATL	MC13XXX_IRQ_LOBATL
#define MC13783_IRQ_LOBATH	MC13XXX_IRQ_LOBATH
#define MC13783_IRQ_UDP		15
#define MC13783_IRQ_USB		16
#define MC13783_IRQ_ID		19
#define MC13783_IRQ_SE1		21
#define MC13783_IRQ_CKDET	22
#define MC13783_IRQ_UDM		23
#define MC13783_IRQ_1HZ		MC13XXX_IRQ_1HZ
#define MC13783_IRQ_TODA	MC13XXX_IRQ_TODA
#define MC13783_IRQ_ONOFD1	27
#define MC13783_IRQ_ONOFD2	28
#define MC13783_IRQ_ONOFD3	29
#define MC13783_IRQ_SYSRST	MC13XXX_IRQ_SYSRST
#define MC13783_IRQ_RTCRST	MC13XXX_IRQ_RTCRST
#define MC13783_IRQ_PC		MC13XXX_IRQ_PC
#define MC13783_IRQ_WARM	MC13XXX_IRQ_WARM
#define MC13783_IRQ_MEMHLD	MC13XXX_IRQ_MEMHLD
#define MC13783_IRQ_PWRRDY	35
#define MC13783_IRQ_THWARNL	MC13XXX_IRQ_THWARNL
#define MC13783_IRQ_THWARNH	MC13XXX_IRQ_THWARNH
#define MC13783_IRQ_CLK		MC13XXX_IRQ_CLK
#define MC13783_IRQ_SEMAF	39
#define MC13783_IRQ_MC2B	41
#define MC13783_IRQ_HSDET	42
#define MC13783_IRQ_HSL		43
#define MC13783_IRQ_ALSPTH	44
#define MC13783_IRQ_AHSSHORT	45
#define MC13783_NUM_IRQ		MC13XXX_NUM_IRQ

#endif /* ifndef __LINUX_MFD_MC13783_H */
