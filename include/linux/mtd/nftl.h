/*
 * $Id: nftl.h,v 1.16 2004/06/30 14:49:00 dbrown Exp $
 *
 * (C) 1999-2003 David Woodhouse <dwmw2@infradead.org>
 */

#ifndef __MTD_NFTL_H__
#define __MTD_NFTL_H__

#include <linux/mtd/mtd.h>
#include <linux/mtd/blktrans.h>

#include <mtd/nftl-user.h>

/* these info are used in ReplUnitTable */
#define BLOCK_NIL          0xffff /* last block of a chain */
#define BLOCK_FREE         0xfffe /* free block */
#define BLOCK_NOTEXPLORED  0xfffd /* non explored block, only used during mounting */
#define BLOCK_RESERVED     0xfffc /* bios block or bad block */

struct NFTLrecord {
	struct mtd_blktrans_dev mbd;
	__u16 MediaUnit, SpareMediaUnit;
	__u32 EraseSize;
	struct NFTLMediaHeader MediaHdr;
	int usecount;
	unsigned char heads;
	unsigned char sectors;
	unsigned short cylinders;
	__u16 numvunits;
	__u16 lastEUN;                  /* should be suppressed */
	__u16 numfreeEUNs;
	__u16 LastFreeEUN; 		/* To speed up finding a free EUN */
	int head,sect,cyl;
	__u16 *EUNtable; 		/* [numvunits]: First EUN for each virtual unit  */
	__u16 *ReplUnitTable; 		/* [numEUNs]: ReplUnitNumber for each */
        unsigned int nb_blocks;		/* number of physical blocks */
        unsigned int nb_boot_blocks;	/* number of blocks used by the bios */
        struct erase_info instr;
	struct nand_oobinfo oobinfo;
};

int NFTL_mount(struct NFTLrecord *s);
int NFTL_formatblock(struct NFTLrecord *s, int block);

#ifndef NFTL_MAJOR
#define NFTL_MAJOR 93
#endif

#define MAX_NFTLS 16
#define MAX_SECTORS_PER_UNIT 64
#define NFTL_PARTN_BITS 4

#endif /* __MTD_NFTL_H__ */
