/*
 * Driver for PowerMac Burgundy onboard soundchips
 * Copyright (c) 2001 by Takashi Iwai <tiwai@suse.de>
 *   based on dmasound.c.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */


#ifndef __BURGUNDY_H
#define __BURGUNDY_H

#define MASK_ADDR_BURGUNDY_INPSEL21 (0x11 << 12)
#define MASK_ADDR_BURGUNDY_INPSEL3 (0x12 << 12)

#define MASK_ADDR_BURGUNDY_GAINCH1 (0x13 << 12)
#define MASK_ADDR_BURGUNDY_GAINCH2 (0x14 << 12)
#define MASK_ADDR_BURGUNDY_GAINCH3 (0x15 << 12)
#define MASK_ADDR_BURGUNDY_GAINCH4 (0x16 << 12)

#define MASK_ADDR_BURGUNDY_VOLCH1 (0x20 << 12)
#define MASK_ADDR_BURGUNDY_VOLCH2 (0x21 << 12)
#define MASK_ADDR_BURGUNDY_VOLCH3 (0x22 << 12)
#define MASK_ADDR_BURGUNDY_VOLCH4 (0x23 << 12)

#define MASK_ADDR_BURGUNDY_OUTPUTSELECTS (0x2B << 12)
#define MASK_ADDR_BURGUNDY_OUTPUTENABLES (0x2F << 12)

#define MASK_ADDR_BURGUNDY_MASTER_VOLUME (0x30 << 12)

#define MASK_ADDR_BURGUNDY_MORE_OUTPUTENABLES (0x60 << 12)

#define MASK_ADDR_BURGUNDY_ATTENSPEAKER (0x62 << 12)
#define MASK_ADDR_BURGUNDY_ATTENLINEOUT (0x63 << 12)
#define MASK_ADDR_BURGUNDY_ATTENHP (0x64 << 12)

#define MASK_ADDR_BURGUNDY_VOLCD (MASK_ADDR_BURGUNDY_VOLCH1)
#define MASK_ADDR_BURGUNDY_VOLLINE (MASK_ADDR_BURGUNDY_VOLCH2)
#define MASK_ADDR_BURGUNDY_VOLMIC (MASK_ADDR_BURGUNDY_VOLCH3)
#define MASK_ADDR_BURGUNDY_VOLMODEM (MASK_ADDR_BURGUNDY_VOLCH4)

#define MASK_ADDR_BURGUNDY_GAINCD (MASK_ADDR_BURGUNDY_GAINCH1)
#define MASK_ADDR_BURGUNDY_GAINLINE (MASK_ADDR_BURGUNDY_GAINCH2)
#define MASK_ADDR_BURGUNDY_GAINMIC (MASK_ADDR_BURGUNDY_GAINCH3)
#define MASK_ADDR_BURGUNDY_GAINMODEM (MASK_ADDR_BURGUNDY_VOLCH4)


/* These are all default values for the burgundy */
#define DEF_BURGUNDY_INPSEL21 (0xAA)
#define DEF_BURGUNDY_INPSEL3 (0x0A)

#define DEF_BURGUNDY_GAINCD (0x33)
#define DEF_BURGUNDY_GAINLINE (0x44)
#define DEF_BURGUNDY_GAINMIC (0x44)
#define DEF_BURGUNDY_GAINMODEM (0x06)

/* Remember: lowest volume here is 0x9b */
#define DEF_BURGUNDY_VOLCD (0xCCCCCCCC)
#define DEF_BURGUNDY_VOLLINE (0x00000000)
#define DEF_BURGUNDY_VOLMIC (0x00000000)
#define DEF_BURGUNDY_VOLMODEM (0xCCCCCCCC)

#define DEF_BURGUNDY_OUTPUTSELECTS (0x010f010f)
#define DEF_BURGUNDY_OUTPUTENABLES (0x0A)

/* #define DEF_BURGUNDY_MASTER_VOLUME (0xFFFFFFFF) */ /* too loud */
#define DEF_BURGUNDY_MASTER_VOLUME (0xDDDDDDDD)

#define DEF_BURGUNDY_MORE_OUTPUTENABLES (0x7E)

#define DEF_BURGUNDY_ATTENSPEAKER (0x44)
#define DEF_BURGUNDY_ATTENLINEOUT (0xCC)
#define DEF_BURGUNDY_ATTENHP (0xCC)

/* OUTPUTENABLES bits */
#define BURGUNDY_OUTPUT_LEFT	0x02
#define BURGUNDY_OUTPUT_RIGHT	0x04
#define BURGUNDY_OUTPUT_INTERN	0x80

/* volume offset */
#define BURGUNDY_VOLUME_OFFSET	155

#endif /* __BURGUNDY_H */
