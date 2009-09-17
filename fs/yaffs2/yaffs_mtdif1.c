/*
 * YAFFS: Yet another FFS. A NAND-flash specific file system.
 * yaffs_mtdif1.c  NAND mtd interface functions for small-page NAND.
 *
 * Copyright (C) 2002 Aleph One Ltd.
 *   for Toby Churchill Ltd and Brightstar Engineering
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * This module provides the interface between yaffs_nand.c and the
 * MTD API.  This version is used when the MTD interface supports the
 * 'mtd_oob_ops' style calls to read_oob and write_oob, circa 2.6.17,
 * and we have small-page NAND device.
 *
 * These functions are invoked via function pointers in yaffs_nand.c.
 * This replaces functionality provided by functions in yaffs_mtdif.c
 * and the yaffs_TagsCompatability functions in yaffs_tagscompat.c that are
 * called in yaffs_mtdif.c when the function pointers are NULL.
 * We assume the MTD layer is performing ECC (useNANDECC is true).
 */

#include "yportenv.h"
#include "yaffs_guts.h"
#include "yaffs_packedtags1.h"
#include "yaffs_tagscompat.h"	/* for yaffs_CalcTagsECC */

#include "linux/kernel.h"
#include "linux/version.h"
#include "linux/types.h"
#include "linux/mtd/mtd.h"

/* Don't compile this module if we don't have MTD's mtd_oob_ops interface */
#if (MTD_VERSION_CODE > MTD_VERSION(2, 6, 17))

const char *yaffs_mtdif1_c_version = "$Id$";

#ifndef CONFIG_YAFFS_9BYTE_TAGS
# define YTAG1_SIZE 8
#else
# define YTAG1_SIZE 9
#endif

#if 0
/* Use the following nand_ecclayout with MTD when using
 * CONFIG_YAFFS_9BYTE_TAGS and the older on-NAND tags layout.
 * If you have existing Yaffs images and the byte order differs from this,
 * adjust 'oobfree' to match your existing Yaffs data.
 *
 * This nand_ecclayout scatters/gathers to/from the old-yaffs layout with the
 * pageStatus byte (at NAND spare offset 4) scattered/gathered from/to
 * the 9th byte.
 *
 * Old-style on-NAND format: T0,T1,T2,T3,P,B,T4,T5,E0,E1,E2,T6,T7,E3,E4,E5
 * We have/need PackedTags1 plus pageStatus: T0,T1,T2,T3,T4,T5,T6,T7,P
 * where Tn are the tag bytes, En are MTD's ECC bytes, P is the pageStatus
 * byte and B is the small-page bad-block indicator byte.
 */
static struct nand_ecclayout nand_oob_16 = {
	.eccbytes = 6,
	.eccpos = { 8, 9, 10, 13, 14, 15 },
	.oobavail = 9,
	.oobfree = { { 0, 4 }, { 6, 2 }, { 11, 2 }, { 4, 1 } }
};
#endif

/* Write a chunk (page) of data to NAND.
 *
 * Caller always provides ExtendedTags data which are converted to a more
 * compact (packed) form for storage in NAND.  A mini-ECC runs over the
 * contents of the tags meta-data; used to valid the tags when read.
 *
 *  - Pack ExtendedTags to PackedTags1 form
 *  - Compute mini-ECC for PackedTags1
 *  - Write data and packed tags to NAND.
 *
 * Note: Due to the use of the PackedTags1 meta-data which does not include
 * a full sequence number (as found in the larger PackedTags2 form) it is
 * necessary for Yaffs to re-write a chunk/page (just once) to mark it as
 * discarded and dirty.  This is not ideal: newer NAND parts are supposed
 * to be written just once.  When Yaffs performs this operation, this
 * function is called with a NULL data pointer -- calling MTD write_oob
 * without data is valid usage (2.6.17).
 *
 * Any underlying MTD error results in YAFFS_FAIL.
 * Returns YAFFS_OK or YAFFS_FAIL.
 */
int nandmtd1_WriteChunkWithTagsToNAND(yaffs_Device *dev,
	int chunkInNAND, const __u8 *data, const yaffs_ExtendedTags *etags)
{
	struct mtd_info *mtd = dev->genericDevice;
	int chunkBytes = dev->nDataBytesPerChunk;
	loff_t addr = ((loff_t)chunkInNAND) * chunkBytes;
	struct mtd_oob_ops ops;
	yaffs_PackedTags1 pt1;
	int retval;

	/* we assume that PackedTags1 and yaffs_Tags are compatible */
	compile_time_assertion(sizeof(yaffs_PackedTags1) == 12);
	compile_time_assertion(sizeof(yaffs_Tags) == 8);

	yaffs_PackTags1(&pt1, etags);
	yaffs_CalcTagsECC((yaffs_Tags *)&pt1);

	/* When deleting a chunk, the upper layer provides only skeletal
	 * etags, one with chunkDeleted set.  However, we need to update the
	 * tags, not erase them completely.  So we use the NAND write property
	 * that only zeroed-bits stick and set tag bytes to all-ones and
	 * zero just the (not) deleted bit.
	 */
#ifndef CONFIG_YAFFS_9BYTE_TAGS
	if (etags->chunkDeleted) {
		memset(&pt1, 0xff, 8);
		/* clear delete status bit to indicate deleted */
		pt1.deleted = 0;
	}
#else
	((__u8 *)&pt1)[8] = 0xff;
	if (etags->chunkDeleted) {
		memset(&pt1, 0xff, 8);
		/* zero pageStatus byte to indicate deleted */
		((__u8 *)&pt1)[8] = 0;
	}
#endif

	memset(&ops, 0, sizeof(ops));
	ops.mode = MTD_OOB_AUTO;
	ops.len = (data) ? chunkBytes : 0;
	ops.ooblen = YTAG1_SIZE;
	ops.datbuf = (__u8 *)data;
	ops.oobbuf = (__u8 *)&pt1;

	retval = mtd->write_oob(mtd, addr, &ops);
	if (retval) {
		yaffs_trace(YAFFS_TRACE_MTD,
			"write_oob failed, chunk %d, mtd error %d\n",
			chunkInNAND, retval);
	}
	return retval ? YAFFS_FAIL : YAFFS_OK;
}

/* Return with empty ExtendedTags but add eccResult.
 */
static int rettags(yaffs_ExtendedTags *etags, int eccResult, int retval)
{
	if (etags) {
		memset(etags, 0, sizeof(*etags));
		etags->eccResult = eccResult;
	}
	return retval;
}

/* Read a chunk (page) from NAND.
 *
 * Caller expects ExtendedTags data to be usable even on error; that is,
 * all members except eccResult and blockBad are zeroed.
 *
 *  - Check ECC results for data (if applicable)
 *  - Check for blank/erased block (return empty ExtendedTags if blank)
 *  - Check the PackedTags1 mini-ECC (correct if necessary/possible)
 *  - Convert PackedTags1 to ExtendedTags
 *  - Update eccResult and blockBad members to refect state.
 *
 * Returns YAFFS_OK or YAFFS_FAIL.
 */
int nandmtd1_ReadChunkWithTagsFromNAND(yaffs_Device *dev,
	int chunkInNAND, __u8 *data, yaffs_ExtendedTags *etags)
{
	struct mtd_info *mtd = dev->genericDevice;
	int chunkBytes = dev->nDataBytesPerChunk;
	loff_t addr = ((loff_t)chunkInNAND) * chunkBytes;
	int eccres = YAFFS_ECC_RESULT_NO_ERROR;
	struct mtd_oob_ops ops;
	yaffs_PackedTags1 pt1;
	int retval;
	int deleted;

	memset(&ops, 0, sizeof(ops));
	ops.mode = MTD_OOB_AUTO;
	ops.len = (data) ? chunkBytes : 0;
	ops.ooblen = YTAG1_SIZE;
	ops.datbuf = data;
	ops.oobbuf = (__u8 *)&pt1;

#if (MTD_VERSION_CODE < MTD_VERSION(2, 6, 20))
	/* In MTD 2.6.18 to 2.6.19 nand_base.c:nand_do_read_oob() has a bug;
	 * help it out with ops.len = ops.ooblen when ops.datbuf == NULL.
	 */
	ops.len = (ops.datbuf) ? ops.len : ops.ooblen;
#endif
	/* Read page and oob using MTD.
	 * Check status and determine ECC result.
	 */
	retval = mtd->read_oob(mtd, addr, &ops);
	if (retval) {
		yaffs_trace(YAFFS_TRACE_MTD,
			"read_oob failed, chunk %d, mtd error %d\n",
			chunkInNAND, retval);
	}

	switch (retval) {
	case 0:
		/* no error */
		break;

	case -EUCLEAN:
		/* MTD's ECC fixed the data */
		eccres = YAFFS_ECC_RESULT_FIXED;
		dev->eccFixed++;
		break;

	case -EBADMSG:
		/* MTD's ECC could not fix the data */
		dev->eccUnfixed++;
		/* fall into... */
	default:
		rettags(etags, YAFFS_ECC_RESULT_UNFIXED, 0);
		etags->blockBad = (mtd->block_isbad)(mtd, addr);
		return YAFFS_FAIL;
	}

	/* Check for a blank/erased chunk.
	 */
	if (yaffs_CheckFF((__u8 *)&pt1, 8)) {
		/* when blank, upper layers want eccResult to be <= NO_ERROR */
		return rettags(etags, YAFFS_ECC_RESULT_NO_ERROR, YAFFS_OK);
	}

#ifndef CONFIG_YAFFS_9BYTE_TAGS
	/* Read deleted status (bit) then return it to it's non-deleted
	 * state before performing tags mini-ECC check. pt1.deleted is
	 * inverted.
	 */
	deleted = !pt1.deleted;
	pt1.deleted = 1;
#else
	deleted = (yaffs_CountBits(((__u8 *)&pt1)[8]) < 7);
#endif

	/* Check the packed tags mini-ECC and correct if necessary/possible.
	 */
	retval = yaffs_CheckECCOnTags((yaffs_Tags *)&pt1);
	switch (retval) {
	case 0:
		/* no tags error, use MTD result */
		break;
	case 1:
		/* recovered tags-ECC error */
		dev->tagsEccFixed++;
		if (eccres == YAFFS_ECC_RESULT_NO_ERROR)
			eccres = YAFFS_ECC_RESULT_FIXED;
		break;
	default:
		/* unrecovered tags-ECC error */
		dev->tagsEccUnfixed++;
		return rettags(etags, YAFFS_ECC_RESULT_UNFIXED, YAFFS_FAIL);
	}

	/* Unpack the tags to extended form and set ECC result.
	 * [set shouldBeFF just to keep yaffs_UnpackTags1 happy]
	 */
	pt1.shouldBeFF = 0xFFFFFFFF;
	yaffs_UnpackTags1(etags, &pt1);
	etags->eccResult = eccres;

	/* Set deleted state */
	etags->chunkDeleted = deleted;
	return YAFFS_OK;
}

/* Mark a block bad.
 *
 * This is a persistant state.
 * Use of this function should be rare.
 *
 * Returns YAFFS_OK or YAFFS_FAIL.
 */
int nandmtd1_MarkNANDBlockBad(struct yaffs_DeviceStruct *dev, int blockNo)
{
	struct mtd_info *mtd = dev->genericDevice;
	int blocksize = dev->nChunksPerBlock * dev->nDataBytesPerChunk;
	int retval;

	yaffs_trace(YAFFS_TRACE_BAD_BLOCKS, "marking block %d bad\n", blockNo);

	retval = mtd->block_markbad(mtd, (loff_t)blocksize * blockNo);
	return (retval) ? YAFFS_FAIL : YAFFS_OK;
}

/* Check any MTD prerequists.
 *
 * Returns YAFFS_OK or YAFFS_FAIL.
 */
static int nandmtd1_TestPrerequists(struct mtd_info *mtd)
{
	/* 2.6.18 has mtd->ecclayout->oobavail */
	/* 2.6.21 has mtd->ecclayout->oobavail and mtd->oobavail */
	int oobavail = mtd->ecclayout->oobavail;

	if (oobavail < YTAG1_SIZE) {
		yaffs_trace(YAFFS_TRACE_ERROR,
			"mtd device has only %d bytes for tags, need %d\n",
			oobavail, YTAG1_SIZE);
		return YAFFS_FAIL;
	}
	return YAFFS_OK;
}

/* Query for the current state of a specific block.
 *
 * Examine the tags of the first chunk of the block and return the state:
 *  - YAFFS_BLOCK_STATE_DEAD, the block is marked bad
 *  - YAFFS_BLOCK_STATE_NEEDS_SCANNING, the block is in use
 *  - YAFFS_BLOCK_STATE_EMPTY, the block is clean
 *
 * Always returns YAFFS_OK.
 */
int nandmtd1_QueryNANDBlock(struct yaffs_DeviceStruct *dev, int blockNo,
	yaffs_BlockState *pState, __u32 *pSequenceNumber)
{
	struct mtd_info *mtd = dev->genericDevice;
	int chunkNo = blockNo * dev->nChunksPerBlock;
	loff_t addr = (loff_t)chunkNo * dev->nDataBytesPerChunk;
	yaffs_ExtendedTags etags;
	int state = YAFFS_BLOCK_STATE_DEAD;
	int seqnum = 0;
	int retval;

	/* We don't yet have a good place to test for MTD config prerequists.
	 * Do it here as we are called during the initial scan.
	 */
	if (nandmtd1_TestPrerequists(mtd) != YAFFS_OK)
		return YAFFS_FAIL;

	retval = nandmtd1_ReadChunkWithTagsFromNAND(dev, chunkNo, NULL, &etags);
	etags.blockBad = (mtd->block_isbad)(mtd, addr);
	if (etags.blockBad) {
		yaffs_trace(YAFFS_TRACE_BAD_BLOCKS,
			"block %d is marked bad\n", blockNo);
		state = YAFFS_BLOCK_STATE_DEAD;
	} else if (etags.eccResult != YAFFS_ECC_RESULT_NO_ERROR) {
		/* bad tags, need to look more closely */
		state = YAFFS_BLOCK_STATE_NEEDS_SCANNING;
	} else if (etags.chunkUsed) {
		state = YAFFS_BLOCK_STATE_NEEDS_SCANNING;
		seqnum = etags.sequenceNumber;
	} else {
		state = YAFFS_BLOCK_STATE_EMPTY;
	}

	*pState = state;
	*pSequenceNumber = seqnum;

	/* query always succeeds */
	return YAFFS_OK;
}

#endif /*MTD_VERSION*/
