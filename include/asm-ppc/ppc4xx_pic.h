/*
 * include/asm-ppc/ppc4xx_pic.h
 *
 * Interrupt controller driver for PowerPC 4xx-based processors.
 *
 * Copyright (c) 1999 Grant Erickson <grant@lcse.umn.edu>
 *
 * Eugene Surovegin <eugene.surovegin@zultys.com> or <ebs@ebshome.net>
 * Copyright (c) 2004 Zultys Technologies
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef	__PPC4XX_PIC_H__
#define	__PPC4XX_PIC_H__

#include <linux/types.h>
#include <linux/irq.h>

/* "Fixed" UIC settings (they are chip, not board specific),
 * e.g. polarity/triggerring for internal interrupt sources.
 *
 * Platform port should provide NR_UICS-sized array named ppc4xx_core_uic_cfg
 * with these "fixed" settings: .polarity contains exact value which will
 * be written (masked with "ext_irq_mask") into UICx_PR register,
 * .triggering - to UICx_TR.
 *
 * Settings for external IRQs can be specified separately by the
 * board support code. In this case properly sized array of unsigned
 * char named ppc4xx_uic_ext_irq_cfg should be filled with correct
 * values using IRQ_SENSE_XXXXX and IRQ_POLARITY_XXXXXXX defines.
 *
 * If these arrays aren't provided, UIC initialization code keeps firmware
 * configuration. Also, ppc4xx_uic_ext_irq_cfg implies ppc4xx_core_uic_cfg
 * is defined.
 *
 * Both ppc4xx_core_uic_cfg and ppc4xx_uic_ext_irq_cfg are declared as
 * "weak" symbols in ppc4xx_pic.c
 *
 */
struct ppc4xx_uic_settings {
	u32 polarity;
	u32 triggering;
	u32 ext_irq_mask;
};

extern void ppc4xx_pic_init(void);

#endif				/* __PPC4XX_PIC_H__ */
