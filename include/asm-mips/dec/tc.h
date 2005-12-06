/*
 * Interface to the TURBOchannel related routines
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1998 Harald Koerfgen
 */
#ifndef __ASM_DEC_TC_H
#define __ASM_DEC_TC_H

/*
 * Search for a TURBOchannel Option Module
 * with a certain name. Returns slot number
 * of the first card not in use or -ENODEV
 * if none found.
 */
extern int search_tc_card(const char *);
/*
 * Marks the card in slot as used
 */
extern void claim_tc_card(int);
/*
 * Marks the card in slot as free
 */
extern void release_tc_card(int);
/*
 * Return base address of card in slot
 */
extern unsigned long get_tc_base_addr(int);
/*
 * Return interrupt number of slot
 */
extern unsigned long get_tc_irq_nr(int);
/*
 * Return TURBOchannel clock frequency in Hz
 */
extern unsigned long get_tc_speed(void);

#endif /* __ASM_DEC_TC_H */
