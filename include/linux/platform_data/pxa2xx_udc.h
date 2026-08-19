/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This supports machine-specific differences in how the PXA2xx
 * USB Device Controller (UDC) is wired.
 *
 * It is set in linux/arch/arm/mach-pxa/<machine>.c or in
 * linux/arch/mach-ixp4xx/<machine>.c and used in
 * the probe routine of linux/drivers/usb/gadget/pxa2xx_udc.c
 */
#ifndef PXA2XX_UDC_H
#define PXA2XX_UDC_H

#ifdef CONFIG_PXA27x
extern void pxa27x_clear_otgph(void);
#else
#define pxa27x_clear_otgph()	do {} while (0)
#endif

#endif
