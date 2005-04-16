/*
 * linux/include/asm-arm/arch-pxa/poodle.h
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * Based on:
 *   linux/include/asm-arm/arch-sa1100/collie.h
 *
 * ChangeLog:
 *   04-06-2001 Lineo Japan, Inc.
 *   04-16-2001 SHARP Corporation
 *   Update to 2.6 John Lenz
 */
#ifndef __ASM_ARCH_POODLE_H
#define __ASM_ARCH_POODLE_H  1

/*
 * GPIOs
 */
/* PXA GPIOs */
#define POODLE_GPIO_ON_KEY		(0)
#define POODLE_GPIO_AC_IN		(1)
#define POODLE_GPIO_CO			16
#define POODLE_GPIO_TP_INT		(5)
#define POODLE_GPIO_WAKEUP		(11)	/* change battery */
#define POODLE_GPIO_GA_INT		(10)
#define POODLE_GPIO_IR_ON		(22)
#define POODLE_GPIO_HP_IN		(4)
#define POODLE_GPIO_CF_IRQ		(17)
#define POODLE_GPIO_CF_CD		(14)
#define POODLE_GPIO_CF_STSCHG		(14)
#define POODLE_GPIO_SD_PWR		(33)
#define POODLE_GPIO_nSD_CLK		(6)
#define POODLE_GPIO_nSD_WP		(7)
#define POODLE_GPIO_nSD_INT		(8)
#define POODLE_GPIO_nSD_DETECT		(9)
#define POODLE_GPIO_MAIN_BAT_LOW	(13)
#define POODLE_GPIO_BAT_COVER		(13)
#define POODLE_GPIO_ADC_TEMP_ON		(21)
#define POODLE_GPIO_BYPASS_ON		(36)
#define POODLE_GPIO_CHRG_ON		(38)
#define POODLE_GPIO_CHRG_FULL		(16)

/* PXA GPIOs */
#define POODLE_IRQ_GPIO_ON_KEY		IRQ_GPIO0
#define POODLE_IRQ_GPIO_AC_IN		IRQ_GPIO1
#define POODLE_IRQ_GPIO_HP_IN		IRQ_GPIO4
#define POODLE_IRQ_GPIO_CO		IRQ_GPIO16
#define POODLE_IRQ_GPIO_TP_INT		IRQ_GPIO5
#define POODLE_IRQ_GPIO_WAKEUP		IRQ_GPIO11
#define POODLE_IRQ_GPIO_GA_INT		IRQ_GPIO10
#define POODLE_IRQ_GPIO_CF_IRQ		IRQ_GPIO17
#define POODLE_IRQ_GPIO_CF_CD		IRQ_GPIO14
#define POODLE_IRQ_GPIO_nSD_INT		IRQ_GPIO8
#define POODLE_IRQ_GPIO_nSD_DETECT	IRQ_GPIO9
#define POODLE_IRQ_GPIO_MAIN_BAT_LOW	IRQ_GPIO13

/* SCOOP GPIOs */
#define POODLE_SCOOP_CHARGE_ON	SCOOP_GPCR_PA11
#define POODLE_SCOOP_CP401	SCOOP_GPCR_PA13
#define POODLE_SCOOP_VPEN	SCOOP_GPCR_PA18
#define POODLE_SCOOP_L_PCLK	SCOOP_GPCR_PA20
#define POODLE_SCOOP_L_LCLK	SCOOP_GPCR_PA21
#define POODLE_SCOOP_HS_OUT	SCOOP_GPCR_PA22

#define POODLE_SCOOP_IO_DIR	( POODLE_SCOOP_VPEN | POODLE_SCOOP_HS_OUT )
#define POODLE_SCOOP_IO_OUT	( 0 )

#endif /* __ASM_ARCH_POODLE_H  */
