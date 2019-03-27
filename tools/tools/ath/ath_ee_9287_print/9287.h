/* $FreeBSD$ */

#ifndef	__9287_H__
#define	__9287_H__

extern void eeprom_9287_base_print(uint16_t *buf);
extern void eeprom_9287_custdata_print(uint16_t *buf);
extern void eeprom_9287_modal_print(uint16_t *buf);
extern void eeprom_9287_calfreqpiers_print(uint16_t *buf);
extern void eeprom_9287_ctl_print(uint16_t *buf);
extern void eeprom_9287_print_targets(uint16_t *buf);
extern void eeprom_9287_print_edges(uint16_t *buf);
extern void eeprom_9287_print_other(uint16_t *buf);

#endif
