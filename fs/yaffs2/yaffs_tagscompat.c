/*
 * YAFFS: Yet Another Flash File System. A NAND-flash specific file system.
 *
 * Copyright (C) 2002-2007 Aleph One Ltd.
 *   for Toby Churchill Ltd and Brightstar Engineering
 *
 * Created by Charles Manning <charles@aleph1.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "yaffs_guts.h"
#include "yaffs_tagscompat.h"
#include "yaffs_ecc.h"
#include "yaffs_getblockinfo.h"

static void yaffs_HandleReadDataError(yaffs_Device *dev, int chunkInNAND);
#ifdef NOTYET
static void yaffs_CheckWrittenBlock(yaffs_Device *dev, int chunkInNAND);
static void yaffs_HandleWriteChunkOk(yaffs_Device *dev, int chunkInNAND,
				     const __u8 *data,
				     const yaffs_Spare *spare);
static void yaffs_HandleUpdateChunk(yaffs_Device *dev, int chunkInNAND,
				    const yaffs_Spare *spare);
static void yaffs_HandleWriteChunkError(yaffs_Device *dev, int chunkInNAND);
#endif

static const char yaffs_countBitsTable[256] = {
	0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4,
	1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
	1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
	2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
	1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
	2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
	2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
	3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
	1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
	2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
	2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
	3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
	2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
	3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
	3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
	4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8
};

int yaffs_CountBits(__u8 x)
{
	int retVal;
	retVal = yaffs_countBitsTable[x];
	return retVal;
}

/********** Tags ECC calculations  *********/

void yaffs_CalcECC(const __u8 *data, yaffs_Spare *spare)
{
	yaffs_ECCCalculate(data, spare->ecc1);
	yaffs_ECCCalculate(&data[256], spare->ecc2);
}

void yaffs_CalcTagsECC(yaffs_Tags *tags)
{
	/* Calculate an ecc */

	unsigned char *b = ((yaffs_TagsUnion *) tags)->asBytes;
	unsigned i, j;
	unsigned ecc = 0;
	unsigned bit = 0;

	tags->ecc = 0;

	for (i = 0; i < 8; i++) {
		for (j = 1; j & 0xff; j <<= 1) {
			bit++;
			if (b[i] & j)
				ecc ^= bit;
		}
	}

	tags->ecc = ecc;

}

int yaffs_CheckECCOnTags(yaffs_Tags *tags)
{
	unsigned ecc = tags->ecc;

	yaffs_CalcTagsECC(tags);

	ecc ^= tags->ecc;

	if (ecc && ecc <= 64) {
		/* TODO: Handle the failure better. Retire? */
		unsigned char *b = ((yaffs_TagsUnion *) tags)->asBytes;

		ecc--;

		b[ecc / 8] ^= (1 << (ecc & 7));

		/* Now recvalc the ecc */
		yaffs_CalcTagsECC(tags);

		return 1;	/* recovered error */
	} else if (ecc) {
		/* Wierd ecc failure value */
		/* TODO Need to do somethiong here */
		return -1;	/* unrecovered error */
	}

	return 0;
}

/********** Tags **********/

static void yaffs_LoadTagsIntoSpare(yaffs_Spare *sparePtr,
				yaffs_Tags *tagsPtr)
{
	yaffs_TagsUnion *tu = (yaffs_TagsUnion *) tagsPtr;

	yaffs_CalcTagsECC(tagsPtr);

	sparePtr->tagByte0 = tu->asBytes[0];
	sparePtr->tagByte1 = tu->asBytes[1];
	sparePtr->tagByte2 = tu->asBytes[2];
	sparePtr->tagByte3 = tu->asBytes[3];
	sparePtr->tagByte4 = tu->asBytes[4];
	sparePtr->tagByte5 = tu->asBytes[5];
	sparePtr->tagByte6 = tu->asBytes[6];
	sparePtr->tagByte7 = tu->asBytes[7];
}

static void yaffs_GetTagsFromSpare(yaffs_Device *dev, yaffs_Spare *sparePtr,
				yaffs_Tags *tagsPtr)
{
	yaffs_TagsUnion *tu = (yaffs_TagsUnion *) tagsPtr;
	int result;

	tu->asBytes[0] = sparePtr->tagByte0;
	tu->asBytes[1] = sparePtr->tagByte1;
	tu->asBytes[2] = sparePtr->tagByte2;
	tu->asBytes[3] = sparePtr->tagByte3;
	tu->asBytes[4] = sparePtr->tagByte4;
	tu->asBytes[5] = sparePtr->tagByte5;
	tu->asBytes[6] = sparePtr->tagByte6;
	tu->asBytes[7] = sparePtr->tagByte7;

	result = yaffs_CheckECCOnTags(tagsPtr);
	if (result > 0)
		dev->tagsEccFixed++;
	else if (result < 0)
		dev->tagsEccUnfixed++;
}

static void yaffs_SpareInitialise(yaffs_Spare *spare)
{
	memset(spare, 0xFF, sizeof(yaffs_Spare));
}

static int yaffs_WriteChunkToNAND(struct yaffs_DeviceStruct *dev,
				int chunkInNAND, const __u8 *data,
				yaffs_Spare *spare)
{
	if (chunkInNAND < dev->startBlock * dev->nChunksPerBlock) {
		T(YAFFS_TRACE_ERROR,
		  (TSTR("**>> yaffs chunk %d is not valid" TENDSTR),
		   chunkInNAND));
		return YAFFS_FAIL;
	}

	dev->nPageWrites++;
	return dev->writeChunkToNAND(dev, chunkInNAND, data, spare);
}

static int yaffs_ReadChunkFromNAND(struct yaffs_DeviceStruct *dev,
				   int chunkInNAND,
				   __u8 *data,
				   yaffs_Spare *spare,
				   yaffs_ECCResult *eccResult,
				   int doErrorCorrection)
{
	int retVal;
	yaffs_Spare localSpare;

	dev->nPageReads++;

	if (!spare && data) {
		/* If we don't have a real spare, then we use a local one. */
		/* Need this for the calculation of the ecc */
		spare = &localSpare;
	}

	if (!dev->useNANDECC) {
		retVal = dev->readChunkFromNAND(dev, chunkInNAND, data, spare);
		if (data && doErrorCorrection) {
			/* Do ECC correction */
			/* Todo handle any errors */
			int eccResult1, eccResult2;
			__u8 calcEcc[3];

			yaffs_ECCCalculate(data, calcEcc);
			eccResult1 =
			    yaffs_ECCCorrect(data, spare->ecc1, calcEcc);
			yaffs_ECCCalculate(&data[256], calcEcc);
			eccResult2 =
			    yaffs_ECCCorrect(&data[256], spare->ecc2, calcEcc);

			if (eccResult1 > 0) {
				T(YAFFS_TRACE_ERROR,
				  (TSTR
				   ("**>>yaffs ecc error fix performed on chunk %d:0"
				    TENDSTR), chunkInNAND));
				dev->eccFixed++;
			} else if (eccResult1 < 0) {
				T(YAFFS_TRACE_ERROR,
				  (TSTR
				   ("**>>yaffs ecc error unfixed on chunk %d:0"
				    TENDSTR), chunkInNAND));
				dev->eccUnfixed++;
			}

			if (eccResult2 > 0) {
				T(YAFFS_TRACE_ERROR,
				  (TSTR
				   ("**>>yaffs ecc error fix performed on chunk %d:1"
				    TENDSTR), chunkInNAND));
				dev->eccFixed++;
			} else if (eccResult2 < 0) {
				T(YAFFS_TRACE_ERROR,
				  (TSTR
				   ("**>>yaffs ecc error unfixed on chunk %d:1"
				    TENDSTR), chunkInNAND));
				dev->eccUnfixed++;
			}

			if (eccResult1 || eccResult2) {
				/* We had a data problem on this page */
				yaffs_HandleReadDataError(dev, chunkInNAND);
			}

			if (eccResult1 < 0 || eccResult2 < 0)
				*eccResult = YAFFS_ECC_RESULT_UNFIXED;
			else if (eccResult1 > 0 || eccResult2 > 0)
				*eccResult = YAFFS_ECC_RESULT_FIXED;
			else
				*eccResult = YAFFS_ECC_RESULT_NO_ERROR;
		}
	} else {
		/* Must allocate enough memory for spare+2*sizeof(int) */
		/* for ecc results from device. */
		struct yaffs_NANDSpare nspare;

		memset(&nspare, 0, sizeof(nspare));

		retVal = dev->readChunkFromNAND(dev, chunkInNAND, data,
					(yaffs_Spare *) &nspare);
		memcpy(spare, &nspare, sizeof(yaffs_Spare));
		if (data && doErrorCorrection) {
			if (nspare.eccres1 > 0) {
				T(YAFFS_TRACE_ERROR,
				  (TSTR
				   ("**>>mtd ecc error fix performed on chunk %d:0"
				    TENDSTR), chunkInNAND));
			} else if (nspare.eccres1 < 0) {
				T(YAFFS_TRACE_ERROR,
				  (TSTR
				   ("**>>mtd ecc error unfixed on chunk %d:0"
				    TENDSTR), chunkInNAND));
			}

			if (nspare.eccres2 > 0) {
				T(YAFFS_TRACE_ERROR,
				  (TSTR
				   ("**>>mtd ecc error fix performed on chunk %d:1"
				    TENDSTR), chunkInNAND));
			} else if (nspare.eccres2 < 0) {
				T(YAFFS_TRACE_ERROR,
				  (TSTR
				   ("**>>mtd ecc error unfixed on chunk %d:1"
				    TENDSTR), chunkInNAND));
			}

			if (nspare.eccres1 || nspare.eccres2) {
				/* We had a data problem on this page */
				yaffs_HandleReadDataError(dev, chunkInNAND);
			}

			if (nspare.eccres1 < 0 || nspare.eccres2 < 0)
				*eccResult = YAFFS_ECC_RESULT_UNFIXED;
			else if (nspare.eccres1 > 0 || nspare.eccres2 > 0)
				*eccResult = YAFFS_ECC_RESULT_FIXED;
			else
				*eccResult = YAFFS_ECC_RESULT_NO_ERROR;

		}
	}
	return retVal;
}

#ifdef NOTYET
static int yaffs_CheckChunkErased(struct yaffs_DeviceStruct *dev,
				  int chunkInNAND)
{
	static int init;
	static __u8 cmpbuf[YAFFS_BYTES_PER_CHUNK];
	static __u8 data[YAFFS_BYTES_PER_CHUNK];
	/* Might as well always allocate the larger size for */
	/* dev->useNANDECC == true; */
	static __u8 spare[sizeof(struct yaffs_NANDSpare)];

	dev->readChunkFromNAND(dev, chunkInNAND, data, (yaffs_Spare *) spare);

	if (!init) {
		memset(cmpbuf, 0xff, YAFFS_BYTES_PER_CHUNK);
		init = 1;
	}

	if (memcmp(cmpbuf, data, YAFFS_BYTES_PER_CHUNK))
		return YAFFS_FAIL;
	if (memcmp(cmpbuf, spare, 16))
		return YAFFS_FAIL;

	return YAFFS_OK;

}
#endif

/*
 * Functions for robustisizing
 */

static void yaffs_HandleReadDataError(yaffs_Device *dev, int chunkInNAND)
{
	int blockInNAND = chunkInNAND / dev->nChunksPerBlock;

	/* Mark the block for retirement */
	yaffs_GetBlockInfo(dev, blockInNAND + dev->blockOffset)->needsRetiring = 1;
	T(YAFFS_TRACE_ERROR | YAFFS_TRACE_BAD_BLOCKS,
	  (TSTR("**>>Block %d marked for retirement" TENDSTR), blockInNAND));

	/* TODO:
	 * Just do a garbage collection on the affected block
	 * then retire the block
	 * NB recursion
	 */
}

#ifdef NOTYET
static void yaffs_CheckWrittenBlock(yaffs_Device *dev, int chunkInNAND)
{
}

static void yaffs_HandleWriteChunkOk(yaffs_Device *dev, int chunkInNAND,
				     const __u8 *data,
				     const yaffs_Spare *spare)
{
}

static void yaffs_HandleUpdateChunk(yaffs_Device *dev, int chunkInNAND,
				    const yaffs_Spare *spare)
{
}

static void yaffs_HandleWriteChunkError(yaffs_Device *dev, int chunkInNAND)
{
	int blockInNAND = chunkInNAND / dev->nChunksPerBlock;

	/* Mark the block for retirement */
	yaffs_GetBlockInfo(dev, blockInNAND)->needsRetiring = 1;
	/* Delete the chunk */
	yaffs_DeleteChunk(dev, chunkInNAND, 1, __LINE__);
}

static int yaffs_VerifyCompare(const __u8 *d0, const __u8 *d1,
			       const yaffs_Spare *s0, const yaffs_Spare *s1)
{

	if (memcmp(d0, d1, YAFFS_BYTES_PER_CHUNK) != 0 ||
	    s0->tagByte0 != s1->tagByte0 ||
	    s0->tagByte1 != s1->tagByte1 ||
	    s0->tagByte2 != s1->tagByte2 ||
	    s0->tagByte3 != s1->tagByte3 ||
	    s0->tagByte4 != s1->tagByte4 ||
	    s0->tagByte5 != s1->tagByte5 ||
	    s0->tagByte6 != s1->tagByte6 ||
	    s0->tagByte7 != s1->tagByte7 ||
	    s0->ecc1[0] != s1->ecc1[0] ||
	    s0->ecc1[1] != s1->ecc1[1] ||
	    s0->ecc1[2] != s1->ecc1[2] ||
	    s0->ecc2[0] != s1->ecc2[0] ||
	    s0->ecc2[1] != s1->ecc2[1] || s0->ecc2[2] != s1->ecc2[2]) {
		return 0;
	}

	return 1;
}
#endif				/* NOTYET */

int yaffs_TagsCompatabilityWriteChunkWithTagsToNAND(yaffs_Device *dev,
						int chunkInNAND,
						const __u8 *data,
						const yaffs_ExtendedTags *eTags)
{
	yaffs_Spare spare;
	yaffs_Tags tags;

	yaffs_SpareInitialise(&spare);

	if (eTags->chunkDeleted)
		spare.pageStatus = 0;
	else {
		tags.objectId = eTags->objectId;
		tags.chunkId = eTags->chunkId;

		tags.byteCountLSB = eTags->byteCount & 0x3ff;

		if (dev->nDataBytesPerChunk >= 1024)
			tags.byteCountMSB = (eTags->byteCount >> 10) & 3;
		else
			tags.byteCountMSB = 3;


		tags.serialNumber = eTags->serialNumber;

		if (!dev->useNANDECC && data)
			yaffs_CalcECC(data, &spare);

		yaffs_LoadTagsIntoSpare(&spare, &tags);

	}

	return yaffs_WriteChunkToNAND(dev, chunkInNAND, data, &spare);
}

int yaffs_TagsCompatabilityReadChunkWithTagsFromNAND(yaffs_Device *dev,
						     int chunkInNAND,
						     __u8 *data,
						     yaffs_ExtendedTags *eTags)
{

	yaffs_Spare spare;
	yaffs_Tags tags;
	yaffs_ECCResult eccResult = YAFFS_ECC_RESULT_UNKNOWN;

	static yaffs_Spare spareFF;
	static int init;

	if (!init) {
		memset(&spareFF, 0xFF, sizeof(spareFF));
		init = 1;
	}

	if (yaffs_ReadChunkFromNAND
	    (dev, chunkInNAND, data, &spare, &eccResult, 1)) {
		/* eTags may be NULL */
		if (eTags) {

			int deleted =
			    (yaffs_CountBits(spare.pageStatus) < 7) ? 1 : 0;

			eTags->chunkDeleted = deleted;
			eTags->eccResult = eccResult;
			eTags->blockBad = 0;	/* We're reading it */
			/* therefore it is not a bad block */
			eTags->chunkUsed =
			    (memcmp(&spareFF, &spare, sizeof(spareFF)) !=
			     0) ? 1 : 0;

			if (eTags->chunkUsed) {
				yaffs_GetTagsFromSpare(dev, &spare, &tags);

				eTags->objectId = tags.objectId;
				eTags->chunkId = tags.chunkId;
				eTags->byteCount = tags.byteCountLSB;

				if (dev->nDataBytesPerChunk >= 1024)
					eTags->byteCount |= (((unsigned) tags.byteCountMSB) << 10);

				eTags->serialNumber = tags.serialNumber;
			}
		}

		return YAFFS_OK;
	} else {
		return YAFFS_FAIL;
	}
}

int yaffs_TagsCompatabilityMarkNANDBlockBad(struct yaffs_DeviceStruct *dev,
					    int blockInNAND)
{

	yaffs_Spare spare;

	memset(&spare, 0xff, sizeof(yaffs_Spare));

	spare.blockStatus = 'Y';

	yaffs_WriteChunkToNAND(dev, blockInNAND * dev->nChunksPerBlock, NULL,
			       &spare);
	yaffs_WriteChunkToNAND(dev, blockInNAND * dev->nChunksPerBlock + 1,
			       NULL, &spare);

	return YAFFS_OK;

}

int yaffs_TagsCompatabilityQueryNANDBlock(struct yaffs_DeviceStruct *dev,
					  int blockNo,
					  yaffs_BlockState *state,
					  __u32 *sequenceNumber)
{

	yaffs_Spare spare0, spare1;
	static yaffs_Spare spareFF;
	static int init;
	yaffs_ECCResult dummy;

	if (!init) {
		memset(&spareFF, 0xFF, sizeof(spareFF));
		init = 1;
	}

	*sequenceNumber = 0;

	yaffs_ReadChunkFromNAND(dev, blockNo * dev->nChunksPerBlock, NULL,
				&spare0, &dummy, 1);
	yaffs_ReadChunkFromNAND(dev, blockNo * dev->nChunksPerBlock + 1, NULL,
				&spare1, &dummy, 1);

	if (yaffs_CountBits(spare0.blockStatus & spare1.blockStatus) < 7)
		*state = YAFFS_BLOCK_STATE_DEAD;
	else if (memcmp(&spareFF, &spare0, sizeof(spareFF)) == 0)
		*state = YAFFS_BLOCK_STATE_EMPTY;
	else
		*state = YAFFS_BLOCK_STATE_NEEDS_SCANNING;

	return YAFFS_OK;
}
