/* linux/amba/pl093.h
 *
 * Copyright (c) 2008 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * AMBA PL093 SSMC (synchronous static memory controller)
 *  See DDI0236.pdf (r0p4) for more details
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#define SMB_BANK(x)	((x) * 0x20) /* each bank control set is 0x20 apart */

/* Offsets for SMBxxxxRy registers */

#define SMBIDCYR	(0x00)
#define SMBWSTRDR	(0x04)
#define SMBWSTWRR	(0x08)
#define SMBWSTOENR	(0x0C)
#define SMBWSTWENR	(0x10)
#define SMBCR		(0x14)
#define SMBSR		(0x18)
#define SMBWSTBRDR	(0x1C)

/* Masks for SMB registers */
#define IDCY_MASK	(0xf)
#define WSTRD_MASK	(0xf)
#define WSTWR_MASK	(0xf)
#define WSTOEN_MASK	(0xf)
#define WSTWEN_MASK	(0xf)

/* Notes from datasheet:
 *	WSTOEN <= WSTRD
 *	WSTWEN <= WSTWR
 *
 * WSTOEN is not used with nWAIT
 */

/* SMBCR bit definitions */
#define SMBCR_BIWRITEEN		(1 << 21)
#define SMBCR_ADDRVALIDWRITEEN	(1 << 20)
#define SMBCR_SYNCWRITE		(1 << 17)
#define SMBCR_BMWRITE		(1 << 16)
#define SMBCR_WRAPREAD		(1 << 14)
#define SMBCR_BIREADEN		(1 << 13)
#define SMBCR_ADDRVALIDREADEN	(1 << 12)
#define SMBCR_SYNCREAD		(1 << 9)
#define SMBCR_BMREAD		(1 << 8)
#define SMBCR_SMBLSPOL		(1 << 6)
#define SMBCR_WP		(1 << 3)
#define SMBCR_WAITEN		(1 << 2)
#define SMBCR_WAITPOL		(1 << 1)
#define SMBCR_RBLE		(1 << 0)

#define SMBCR_BURSTLENWRITE_MASK	(3 << 18)
#define SMBCR_BURSTLENWRITE_4		(0 << 18)
#define SMBCR_BURSTLENWRITE_8		(1 << 18)
#define SMBCR_BURSTLENWRITE_RESERVED	(2 << 18)
#define SMBCR_BURSTLENWRITE_CONTINUOUS	(3 << 18)

#define SMBCR_BURSTLENREAD_MASK		(3 << 10)
#define SMBCR_BURSTLENREAD_4		(0 << 10)
#define SMBCR_BURSTLENREAD_8		(1 << 10)
#define SMBCR_BURSTLENREAD_16		(2 << 10)
#define SMBCR_BURSTLENREAD_CONTINUOUS	(3 << 10)

#define SMBCR_MW_MASK			(3 << 4)
#define SMBCR_MW_8BIT			(0 << 4)
#define SMBCR_MW_16BIT			(1 << 4)
#define SMBCR_MW_M32BIT			(2 << 4)

/* SSMC status registers */
#define SSMCCSR		(0x200)
#define SSMCCR		(0x204)
#define SSMCITCR	(0x208)
#define SSMCITIP	(0x20C)
#define SSMCITIOP	(0x210)
