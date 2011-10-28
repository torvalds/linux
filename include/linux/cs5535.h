/*
 * AMD CS5535/CS5536 definitions
 * Copyright (C) 2006  Advanced Micro Devices, Inc.
 * Copyright (C) 2009  Andres Salomon <dilinger@collabora.co.uk>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 */

#ifndef _CS5535_H
#define _CS5535_H

/* MSRs */
#define MSR_GLIU_P2D_RO0	0x10000029

#define MSR_LX_GLD_MSR_CONFIG	0x48002001
#define MSR_LX_MSR_PADSEL	0x48002011	/* NOT 0x48000011; the data
						 * sheet has the wrong value */
#define MSR_GLCP_SYS_RSTPLL	0x4C000014
#define MSR_GLCP_DOTPLL		0x4C000015

#define MSR_LBAR_SMB		0x5140000B
#define MSR_LBAR_GPIO		0x5140000C
#define MSR_LBAR_MFGPT		0x5140000D
#define MSR_LBAR_ACPI		0x5140000E
#define MSR_LBAR_PMS		0x5140000F

#define MSR_DIVIL_SOFT_RESET	0x51400017

#define MSR_PIC_YSEL_LOW	0x51400020
#define MSR_PIC_YSEL_HIGH	0x51400021
#define MSR_PIC_ZSEL_LOW	0x51400022
#define MSR_PIC_ZSEL_HIGH	0x51400023
#define MSR_PIC_IRQM_LPC	0x51400025

#define MSR_MFGPT_IRQ		0x51400028
#define MSR_MFGPT_NR		0x51400029
#define MSR_MFGPT_SETUP		0x5140002B

#define MSR_LX_SPARE_MSR	0x80000011	/* DC-specific */

#define MSR_GX_GLD_MSR_CONFIG	0xC0002001
#define MSR_GX_MSR_PADSEL	0xC0002011

/* resource sizes */
#define LBAR_GPIO_SIZE		0xFF
#define LBAR_MFGPT_SIZE		0x40
#define LBAR_ACPI_SIZE		0x40
#define LBAR_PMS_SIZE		0x80

/* VSA2 magic values */
#define VSA_VRC_INDEX		0xAC1C
#define VSA_VRC_DATA		0xAC1E
#define VSA_VR_UNLOCK		0xFC53  /* unlock virtual register */
#define VSA_VR_SIGNATURE	0x0003
#define VSA_VR_MEM_SIZE		0x0200
#define AMD_VSA_SIG		0x4132  /* signature is ascii 'VSA2' */
#define GSW_VSA_SIG		0x534d  /* General Software signature */

#include <linux/io.h>

static inline int cs5535_has_vsa2(void)
{
	static int has_vsa2 = -1;

	if (has_vsa2 == -1) {
		uint16_t val;

		/*
		 * The VSA has virtual registers that we can query for a
		 * signature.
		 */
		outw(VSA_VR_UNLOCK, VSA_VRC_INDEX);
		outw(VSA_VR_SIGNATURE, VSA_VRC_INDEX);

		val = inw(VSA_VRC_DATA);
		has_vsa2 = (val == AMD_VSA_SIG || val == GSW_VSA_SIG);
	}

	return has_vsa2;
}

/* GPIOs */
#define GPIO_OUTPUT_VAL		0x00
#define GPIO_OUTPUT_ENABLE	0x04
#define GPIO_OUTPUT_OPEN_DRAIN	0x08
#define GPIO_OUTPUT_INVERT	0x0C
#define GPIO_OUTPUT_AUX1	0x10
#define GPIO_OUTPUT_AUX2	0x14
#define GPIO_PULL_UP		0x18
#define GPIO_PULL_DOWN		0x1C
#define GPIO_INPUT_ENABLE	0x20
#define GPIO_INPUT_INVERT	0x24
#define GPIO_INPUT_FILTER	0x28
#define GPIO_INPUT_EVENT_COUNT	0x2C
#define GPIO_READ_BACK		0x30
#define GPIO_INPUT_AUX1		0x34
#define GPIO_EVENTS_ENABLE	0x38
#define GPIO_LOCK_ENABLE	0x3C
#define GPIO_POSITIVE_EDGE_EN	0x40
#define GPIO_NEGATIVE_EDGE_EN	0x44
#define GPIO_POSITIVE_EDGE_STS	0x48
#define GPIO_NEGATIVE_EDGE_STS	0x4C

#define GPIO_FLTR7_AMOUNT	0xD8

#define GPIO_MAP_X		0xE0
#define GPIO_MAP_Y		0xE4
#define GPIO_MAP_Z		0xE8
#define GPIO_MAP_W		0xEC

#define GPIO_FE7_SEL		0xF7

void cs5535_gpio_set(unsigned offset, unsigned int reg);
void cs5535_gpio_clear(unsigned offset, unsigned int reg);
int cs5535_gpio_isset(unsigned offset, unsigned int reg);
int cs5535_gpio_set_irq(unsigned group, unsigned irq);
void cs5535_gpio_setup_event(unsigned offset, int pair, int pme);

/* MFGPTs */

#define MFGPT_MAX_TIMERS	8
#define MFGPT_TIMER_ANY		(-1)

#define MFGPT_DOMAIN_WORKING	1
#define MFGPT_DOMAIN_STANDBY	2
#define MFGPT_DOMAIN_ANY	(MFGPT_DOMAIN_WORKING | MFGPT_DOMAIN_STANDBY)

#define MFGPT_CMP1		0
#define MFGPT_CMP2		1

#define MFGPT_EVENT_IRQ		0
#define MFGPT_EVENT_NMI		1
#define MFGPT_EVENT_RESET	3

#define MFGPT_REG_CMP1		0
#define MFGPT_REG_CMP2		2
#define MFGPT_REG_COUNTER	4
#define MFGPT_REG_SETUP		6

#define MFGPT_SETUP_CNTEN	(1 << 15)
#define MFGPT_SETUP_CMP2	(1 << 14)
#define MFGPT_SETUP_CMP1	(1 << 13)
#define MFGPT_SETUP_SETUP	(1 << 12)
#define MFGPT_SETUP_STOPEN	(1 << 11)
#define MFGPT_SETUP_EXTEN	(1 << 10)
#define MFGPT_SETUP_REVEN	(1 << 5)
#define MFGPT_SETUP_CLKSEL	(1 << 4)

struct cs5535_mfgpt_timer;

extern uint16_t cs5535_mfgpt_read(struct cs5535_mfgpt_timer *timer,
		uint16_t reg);
extern void cs5535_mfgpt_write(struct cs5535_mfgpt_timer *timer, uint16_t reg,
		uint16_t value);

extern int cs5535_mfgpt_toggle_event(struct cs5535_mfgpt_timer *timer, int cmp,
		int event, int enable);
extern int cs5535_mfgpt_set_irq(struct cs5535_mfgpt_timer *timer, int cmp,
		int *irq, int enable);
extern struct cs5535_mfgpt_timer *cs5535_mfgpt_alloc_timer(int timer,
		int domain);
extern void cs5535_mfgpt_free_timer(struct cs5535_mfgpt_timer *timer);

static inline int cs5535_mfgpt_setup_irq(struct cs5535_mfgpt_timer *timer,
		int cmp, int *irq)
{
	return cs5535_mfgpt_set_irq(timer, cmp, irq, 1);
}

static inline int cs5535_mfgpt_release_irq(struct cs5535_mfgpt_timer *timer,
		int cmp, int *irq)
{
	return cs5535_mfgpt_set_irq(timer, cmp, irq, 0);
}

#endif
