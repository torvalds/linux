/*
 * atmel platform data
 *
 * GPL v2 Only
 */

#ifndef __ATMEL_H__
#define __ATMEL_H__

#include <linux/mtd/nand.h>

 /* NAND / SmartMedia */
struct atmel_nand_data {
	int		enable_pin;		/* chip enable */
	int		det_pin;		/* card detect */
	int		rdy_pin;		/* ready/busy */
	u8		rdy_pin_active_low;	/* rdy_pin value is inverted */
	u8		ale;			/* address line number connected to ALE */
	u8		cle;			/* address line number connected to CLE */
	u8		bus_width_16;		/* buswidth is 16 bit */
	u8		ecc_mode;		/* ecc mode */
	u8		on_flash_bbt;		/* bbt on flash */
	struct mtd_partition *parts;
	unsigned int	num_parts;
};

#endif /* __ATMEL_H__ */
