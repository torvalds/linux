/*
 *	inftl.h -- defines to support the Inverse NAND Flash Translation Layer
 *
 *	(C) Copyright 2002, Greg Ungerer (gerg@snapgear.com)
 *
 *	$Id: inftl.h,v 1.7 2005/06/13 13:08:45 sean Exp $
 */

#ifndef __MTD_INFTL_H__
#define __MTD_INFTL_H__

#ifndef __KERNEL__
#error This is a kernel header. Perhaps include nftl-user.h instead?
#endif

#include <linux/mtd/blktrans.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nftl.h>

#include <mtd/inftl-user.h>

#ifndef INFTL_MAJOR
#define INFTL_MAJOR 96
#endif
#define INFTL_PARTN_BITS 4

#ifdef __KERNEL__

struct INFTLrecord {
	struct mtd_blktrans_dev mbd;
	__u16 MediaUnit;
	__u32 EraseSize;
	struct INFTLMediaHeader MediaHdr;
	int usecount;
	unsigned char heads;
	unsigned char sectors;
	unsigned short cylinders;
	__u16 numvunits;
	__u16 firstEUN;
	__u16 lastEUN;
	__u16 numfreeEUNs;
	__u16 LastFreeEUN; 		/* To speed up finding a free EUN */
	int head,sect,cyl;
	__u16 *PUtable;	 		/* Physical Unit Table  */
	__u16 *VUtable; 		/* Virtual Unit Table */
        unsigned int nb_blocks;		/* number of physical blocks */
        unsigned int nb_boot_blocks;	/* number of blocks used by the bios */
        struct erase_info instr;
        struct nand_ecclayout oobinfo;
};

int INFTL_mount(struct INFTLrecord *s);
int INFTL_formatblock(struct INFTLrecord *s, int block);

extern char inftlmountrev[];

void INFTL_dumptables(struct INFTLrecord *s);
void INFTL_dumpVUchains(struct INFTLrecord *s);

#endif /* __KERNEL__ */

#endif /* __MTD_INFTL_H__ */
