/*
 * linux/include/asm-arm/arch-omap/menelaus.h
 *
 * Functions to access Menelaus power management chip
 */

#ifndef __ASM_ARCH_MENELAUS_H
#define __ASM_ARCH_MENELAUS_H

extern int menelaus_register_mmc_callback(void (*callback)(void *data, u8 card_mask),
					  void *data);
extern void menelaus_unregister_mmc_callback(void);
extern int menelaus_set_mmc_opendrain(int slot, int enable);
extern int menelaus_set_mmc_slot(int slot, int enable, int power, int cd_on);

extern int menelaus_set_vmem(unsigned int mV);
extern int menelaus_set_vio(unsigned int mV);
extern int menelaus_set_vmmc(unsigned int mV);
extern int menelaus_set_vaux(unsigned int mV);
extern int menelaus_set_vdcdc(int dcdc, unsigned int mV);
extern int menelaus_set_slot_sel(int enable);
extern int menelaus_get_slot_pin_states(void);

#if defined(CONFIG_ARCH_OMAP24XX) && defined(CONFIG_MENELAUS)
#define omap_has_menelaus()	1
#else
#define omap_has_menelaus()	0
#endif

#endif

