/*
 * Linux driver for Disk-On-Chip devices
 *
 * Copyright © 1999 Machine Vision Holdings, Inc.
 * Copyright © 1999-2010 David Woodhouse <dwmw2@infradead.org>
 * Copyright © 2002-2003 Greg Ungerer <gerg@snapgear.com>
 * Copyright © 2002-2003 SnapGear Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifndef __MTD_DOC2000_H__
#define __MTD_DOC2000_H__

#include <linux/mtd/mtd.h>
#include <linux/mutex.h>

#define DoC_Sig1 0
#define DoC_Sig2 1

#define DoC_ChipID		0x1000
#define DoC_DOCStatus		0x1001
#define DoC_DOCControl		0x1002
#define DoC_FloorSelect		0x1003
#define DoC_CDSNControl		0x1004
#define DoC_CDSNDeviceSelect 	0x1005
#define DoC_ECCConf 		0x1006
#define DoC_2k_ECCStatus	0x1007

#define DoC_CDSNSlowIO		0x100d
#define DoC_ECCSyndrome0	0x1010
#define DoC_ECCSyndrome1	0x1011
#define DoC_ECCSyndrome2	0x1012
#define DoC_ECCSyndrome3	0x1013
#define DoC_ECCSyndrome4	0x1014
#define DoC_ECCSyndrome5	0x1015
#define DoC_AliasResolution 	0x101b
#define DoC_ConfigInput		0x101c
#define DoC_ReadPipeInit 	0x101d
#define DoC_WritePipeTerm 	0x101e
#define DoC_LastDataRead 	0x101f
#define DoC_NOP 		0x1020

#define DoC_Mil_CDSN_IO 	0x0800
#define DoC_2k_CDSN_IO 		0x1800

#define DoC_Mplus_NOP			0x1002
#define DoC_Mplus_AliasResolution	0x1004
#define DoC_Mplus_DOCControl		0x1006
#define DoC_Mplus_AccessStatus		0x1008
#define DoC_Mplus_DeviceSelect		0x1008
#define DoC_Mplus_Configuration		0x100a
#define DoC_Mplus_OutputControl		0x100c
#define DoC_Mplus_FlashControl		0x1020
#define DoC_Mplus_FlashSelect 		0x1022
#define DoC_Mplus_FlashCmd		0x1024
#define DoC_Mplus_FlashAddress		0x1026
#define DoC_Mplus_FlashData0		0x1028
#define DoC_Mplus_FlashData1		0x1029
#define DoC_Mplus_ReadPipeInit		0x102a
#define DoC_Mplus_LastDataRead		0x102c
#define DoC_Mplus_LastDataRead1		0x102d
#define DoC_Mplus_WritePipeTerm 	0x102e
#define DoC_Mplus_ECCSyndrome0		0x1040
#define DoC_Mplus_ECCSyndrome1		0x1041
#define DoC_Mplus_ECCSyndrome2		0x1042
#define DoC_Mplus_ECCSyndrome3		0x1043
#define DoC_Mplus_ECCSyndrome4		0x1044
#define DoC_Mplus_ECCSyndrome5		0x1045
#define DoC_Mplus_ECCConf 		0x1046
#define DoC_Mplus_Toggle		0x1046
#define DoC_Mplus_DownloadStatus	0x1074
#define DoC_Mplus_CtrlConfirm		0x1076
#define DoC_Mplus_Power			0x1fff

/* How to access the device?
 * On ARM, it'll be mmap'd directly with 32-bit wide accesses.
 * On PPC, it's mmap'd and 16-bit wide.
 * Others use readb/writeb
 */
#if defined(__arm__)
static inline u8 ReadDOC_(u32 __iomem *addr, unsigned long reg)
{
	return __raw_readl(addr + reg);
}
static inline void WriteDOC_(u8 data, u32 __iomem *addr, unsigned long reg)
{
	__raw_writel(data, addr + reg);
	wmb();
}
#define DOC_IOREMAP_LEN 0x8000
#elif defined(__ppc__)
static inline u8 ReadDOC_(u16 __iomem *addr, unsigned long reg)
{
	return __raw_readw(addr + reg);
}
static inline void WriteDOC_(u8 data, u16 __iomem *addr, unsigned long reg)
{
	__raw_writew(data, addr + reg);
	wmb();
}
#define DOC_IOREMAP_LEN 0x4000
#else
#define ReadDOC_(adr, reg)      readb((void __iomem *)(adr) + (reg))
#define WriteDOC_(d, adr, reg)  writeb(d, (void __iomem *)(adr) + (reg))
#define DOC_IOREMAP_LEN 0x2000

#endif

#if defined(__i386__) || defined(__x86_64__)
#define USE_MEMCPY
#endif

/* These are provided to directly use the DoC_xxx defines */
#define ReadDOC(adr, reg)      ReadDOC_(adr,DoC_##reg)
#define WriteDOC(d, adr, reg)  WriteDOC_(d,adr,DoC_##reg)

#define DOC_MODE_RESET 		0
#define DOC_MODE_NORMAL 	1
#define DOC_MODE_RESERVED1 	2
#define DOC_MODE_RESERVED2 	3

#define DOC_MODE_CLR_ERR 	0x80
#define	DOC_MODE_RST_LAT	0x10
#define	DOC_MODE_BDECT		0x08
#define DOC_MODE_MDWREN 	0x04

#define DOC_ChipID_Doc2k 	0x20
#define DOC_ChipID_Doc2kTSOP 	0x21	/* internal number for MTD */
#define DOC_ChipID_DocMil 	0x30
#define DOC_ChipID_DocMilPlus32	0x40
#define DOC_ChipID_DocMilPlus16	0x41

#define CDSN_CTRL_FR_B 		0x80
#define CDSN_CTRL_FR_B0		0x40
#define CDSN_CTRL_FR_B1		0x80

#define CDSN_CTRL_ECC_IO 	0x20
#define CDSN_CTRL_FLASH_IO 	0x10
#define CDSN_CTRL_WP 		0x08
#define CDSN_CTRL_ALE 		0x04
#define CDSN_CTRL_CLE 		0x02
#define CDSN_CTRL_CE 		0x01

#define DOC_ECC_RESET 		0
#define DOC_ECC_ERROR 		0x80
#define DOC_ECC_RW 		0x20
#define DOC_ECC__EN 		0x08
#define DOC_TOGGLE_BIT 		0x04
#define DOC_ECC_RESV 		0x02
#define DOC_ECC_IGNORE		0x01

#define DOC_FLASH_CE		0x80
#define DOC_FLASH_WP		0x40
#define DOC_FLASH_BANK		0x02

/* We have to also set the reserved bit 1 for enable */
#define DOC_ECC_EN (DOC_ECC__EN | DOC_ECC_RESV)
#define DOC_ECC_DIS (DOC_ECC_RESV)

struct Nand {
	char floor, chip;
	unsigned long curadr;
	unsigned char curmode;
	/* Also some erase/write/pipeline info when we get that far */
};

#define MAX_FLOORS 4
#define MAX_CHIPS 4

#define MAX_FLOORS_MIL 1
#define MAX_CHIPS_MIL 1

#define MAX_FLOORS_MPLUS 2
#define MAX_CHIPS_MPLUS 1

#define ADDR_COLUMN 1
#define ADDR_PAGE 2
#define ADDR_COLUMN_PAGE 3

struct DiskOnChip {
	unsigned long physadr;
	void __iomem *virtadr;
	unsigned long totlen;
	unsigned char ChipID; /* Type of DiskOnChip */
	int ioreg;

	unsigned long mfr; /* Flash IDs - only one type of flash per device */
	unsigned long id;
	int chipshift;
	char page256;
	char pageadrlen;
	char interleave; /* Internal interleaving - Millennium Plus style */
	unsigned long erasesize;

	int curfloor;
	int curchip;

	int numchips;
	struct Nand *chips;
	struct mtd_info *nextdoc;
	struct mutex lock;
};

int doc_decode_ecc(unsigned char sector[512], unsigned char ecc1[6]);

#endif /* __MTD_DOC2000_H__ */
