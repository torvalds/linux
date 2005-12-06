/* linux/include/asm-arm/arch-s3c2410/hardware.h
 *
 * (c) 2003 Simtec Electronics
 *  Ben Dooks <ben@simtec.co.uk>
 *
 * S3C2410 - hardware
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Changelog:
 *  21-May-2003 BJD  Created file
 *  06-Jun-2003 BJD  Added CPU frequency settings
 *  03-Sep-2003 BJD  Linux v2.6 support
 *  12-Mar-2004 BJD  Fixed include protection, fixed type of clock vars
 *  14-Sep-2004 BJD  Added misccr and getpin to gpio
 *  01-Oct-2004 BJD  Added the new gpio functions
 *  16-Oct-2004 BJD  Removed the clock variables
*/

#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

#ifndef __ASSEMBLY__

/* external functions for GPIO support
 *
 * These allow various different clients to access the same GPIO
 * registers without conflicting. If your driver only owns the entire
 * GPIO register, then it is safe to ioremap/__raw_{read|write} to it.
*/

/* s3c2410_gpio_cfgpin
 *
 * set the configuration of the given pin to the value passed.
 *
 * eg:
 *    s3c2410_gpio_cfgpin(S3C2410_GPA0, S3C2410_GPA0_ADDR0);
 *    s3c2410_gpio_cfgpin(S3C2410_GPE8, S3C2410_GPE8_SDDAT1);
*/

extern void s3c2410_gpio_cfgpin(unsigned int pin, unsigned int function);

extern unsigned int s3c2410_gpio_getcfg(unsigned int pin);

/* s3c2410_gpio_getirq
 *
 * turn the given pin number into the corresponding IRQ number
 *
 * returns:
 *	< 0 = no interrupt for this pin
 *	>=0 = interrupt number for the pin
*/

extern int s3c2410_gpio_getirq(unsigned int pin);

/* s3c2410_gpio_irqfilter
 *
 * set the irq filtering on the given pin
 *
 * on = 0 => disable filtering
 *      1 => enable filtering
 *
 * config = S3C2410_EINTFLT_PCLK or S3C2410_EINTFLT_EXTCLK orred with
 *          width of filter (0 through 63)
 *
 *
*/

extern int s3c2410_gpio_irqfilter(unsigned int pin, unsigned int on,
				  unsigned int config);

/* s3c2410_gpio_pullup
 *
 * configure the pull-up control on the given pin
 *
 * to = 1 => disable the pull-up
 *      0 => enable the pull-up
 *
 * eg;
 *
 *   s3c2410_gpio_pullup(S3C2410_GPB0, 0);
 *   s3c2410_gpio_pullup(S3C2410_GPE8, 0);
*/

extern void s3c2410_gpio_pullup(unsigned int pin, unsigned int to);

extern void s3c2410_gpio_setpin(unsigned int pin, unsigned int to);

extern unsigned int s3c2410_gpio_getpin(unsigned int pin);

extern unsigned int s3c2410_modify_misccr(unsigned int clr, unsigned int chg);

#ifdef CONFIG_CPU_S3C2440

extern int s3c2440_set_dsc(unsigned int pin, unsigned int value);

#endif /* CONFIG_CPU_S3C2440 */


#endif /* __ASSEMBLY__ */

#include <asm/sizes.h>
#include <asm/arch/map.h>

/* machine specific hardware definitions should go after this */

/* currently here until moved into config (todo) */
#define CONFIG_NO_MULTIWORD_IO

#endif /* __ASM_ARCH_HARDWARE_H */
