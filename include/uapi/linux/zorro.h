/*
 *  linux/zorro.h -- Amiga AutoConfig (Zorro) Bus Definitions
 *
 *  Copyright (C) 1995--2003 Geert Uytterhoeven
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive
 *  for more details.
 */

#ifndef _UAPI_LINUX_ZORRO_H
#define _UAPI_LINUX_ZORRO_H

#include <linux/types.h>


    /*
     *  Each Zorro board has a 32-bit ID of the form
     *
     *      mmmmmmmmmmmmmmmmppppppppeeeeeeee
     *
     *  with
     *
     *      mmmmmmmmmmmmmmmm	16-bit Manufacturer ID (assigned by CBM (sigh))
     *      pppppppp		8-bit Product ID (assigned by manufacturer)
     *      eeeeeeee		8-bit Extended Product ID (currently only used
     *				for some GVP boards)
     */


#define ZORRO_MANUF(id)		((id) >> 16)
#define ZORRO_PROD(id)		(((id) >> 8) & 0xff)
#define ZORRO_EPC(id)		((id) & 0xff)

#define ZORRO_ID(manuf, prod, epc) \
	((ZORRO_MANUF_##manuf << 16) | ((prod) << 8) | (epc))

typedef __u32 zorro_id;


/* Include the ID list */
#include <linux/zorro_ids.h>


    /*
     *  GVP identifies most of its products through the 'extended product code'
     *  (epc). The epc has to be ANDed with the GVP_PRODMASK before the
     *  identification.
     */

#define GVP_PRODMASK		(0xf8)
#define GVP_SCSICLKMASK		(0x01)

enum GVP_flags {
	GVP_IO			= 0x01,
	GVP_ACCEL		= 0x02,
	GVP_SCSI		= 0x04,
	GVP_24BITDMA		= 0x08,
	GVP_25BITDMA		= 0x10,
	GVP_NOBANK		= 0x20,
	GVP_14MHZ		= 0x40,
};


struct Node {
	__be32 ln_Succ;		/* Pointer to next (successor) */
	__be32 ln_Pred;		/* Pointer to previous (predecessor) */
	__u8   ln_Type;
	__s8   ln_Pri;		/* Priority, for sorting */
	__be32 ln_Name;		/* ID string, null terminated */
} __packed;

struct ExpansionRom {
	/* -First 16 bytes of the expansion ROM */
	__u8   er_Type;		/* Board type, size and flags */
	__u8   er_Product;	/* Product number, assigned by manufacturer */
	__u8   er_Flags;		/* Flags */
	__u8   er_Reserved03;	/* Must be zero ($ff inverted) */
	__be16 er_Manufacturer;	/* Unique ID, ASSIGNED BY COMMODORE-AMIGA! */
	__be32 er_SerialNumber;	/* Available for use by manufacturer */
	__be16 er_InitDiagVec;	/* Offset to optional "DiagArea" structure */
	__u8   er_Reserved0c;
	__u8   er_Reserved0d;
	__u8   er_Reserved0e;
	__u8   er_Reserved0f;
} __packed;

/* er_Type board type bits */
#define ERT_TYPEMASK	0xc0
#define ERT_ZORROII	0xc0
#define ERT_ZORROIII	0x80

/* other bits defined in er_Type */
#define ERTB_MEMLIST	5		/* Link RAM into free memory list */
#define ERTF_MEMLIST	(1<<5)

struct ConfigDev {
	struct Node	cd_Node;
	__u8		cd_Flags;	/* (read/write) */
	__u8		cd_Pad;		/* reserved */
	struct ExpansionRom cd_Rom;	/* copy of board's expansion ROM */
	__be32		cd_BoardAddr;	/* where in memory the board was placed */
	__be32		cd_BoardSize;	/* size of board in bytes */
	__be16		cd_SlotAddr;	/* which slot number (PRIVATE) */
	__be16		cd_SlotSize;	/* number of slots (PRIVATE) */
	__be32		cd_Driver;	/* pointer to node of driver */
	__be32		cd_NextCD;	/* linked list of drivers to config */
	__be32		cd_Unused[4];	/* for whatever the driver wants */
} __packed;

#define ZORRO_NUM_AUTO		16

#endif /* _UAPI_LINUX_ZORRO_H */
