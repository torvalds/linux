/*
 * linux/include/asm-arm/arch-omap/menelaus.h
 *
 * Functions to access Menelaus power management chip
 */

#ifndef __ASM_ARCH_MENELAUS_H
#define __ASM_ARCH_MENELAUS_H

extern void menelaus_mmc_register(void (*callback)(u8 card_mask),
				  unsigned long data);
extern void menelaus_mmc_remove(void);
extern void menelaus_mmc_opendrain(int enable);

#if defined(CONFIG_ARCH_OMAP24XX) && defined(CONFIG_MENELAUS)
#define omap_has_menelaus()	1
#else
#define omap_has_menelaus()	0
#endif

#endif

