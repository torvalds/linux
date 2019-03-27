/* $FreeBSD$ */

#ifndef	__V4K_H__
#define	__V4K_H__

extern void eeprom_v4k_base_print(uint16_t *buf);
extern void eeprom_v4k_custdata_print(uint16_t *buf);
extern void eeprom_v4k_modal_print(uint16_t *buf);
extern void eeprom_v4k_calfreqpiers_print(uint16_t *buf);
extern void eeprom_v4k_ctl_print(uint16_t *buf);
extern void eeprom_v4k_print_targets(uint16_t *buf);
extern void eeprom_v4k_print_edges(uint16_t *buf);
extern void eeprom_v4k_print_other(uint16_t *buf);

#endif
