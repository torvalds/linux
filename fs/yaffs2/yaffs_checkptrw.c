/*
 * YAFFS: Yet Another Flash File System. A NAND-flash specific file system.
 *
 * Copyright (C) 2002-2010 Aleph One Ltd.
 *   for Toby Churchill Ltd and Brightstar Engineering
 *
 * Created by Charles Manning <charles@aleph1.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "yaffs_checkptrw.h"
#include "yaffs_getblockinfo.h"

static int yaffs_CheckpointSpaceOk(yaffs_Device *dev)
{
	int blocksAvailable = dev->nErasedBlocks - dev->param.nReservedBlocks;

	T(YAFFS_TRACE_CHECKPOINT,
		(TSTR("checkpt blocks available = %d" TENDSTR),
		blocksAvailable));

	return (blocksAvailable <= 0) ? 0 : 1;
}


static int yaffs_CheckpointErase(yaffs_Device *dev)
{
	int i;

	if (!dev->param.eraseBlockInNAND)
		return 0;
	T(YAFFS_TRACE_CHECKPOINT, (TSTR("checking blocks %d to %d"TENDSTR),
		dev->internalStartBlock, dev->internalEndBlock));

	for (i = dev->internalStartBlock; i <= dev->internalEndBlock; i++) {
		yaffs_BlockInfo *bi = yaffs_GetBlockInfo(dev, i);
		if (bi->blockState == YAFFS_BLOCK_STATE_CHECKPOINT) {
			T(YAFFS_TRACE_CHECKPOINT, (TSTR("erasing checkpt block %d"TENDSTR), i));

			dev->nBlockErasures++;

			if (dev->param.eraseBlockInNAND(dev, i - dev->blockOffset /* realign */)) {
				bi->blockState = YAFFS_BLOCK_STATE_EMPTY;
				dev->nErasedBlocks++;
				dev->nFreeChunks += dev->param.nChunksPerBlock;
			} else {
				dev->param.markNANDBlockBad(dev, i);
				bi->blockState = YAFFS_BLOCK_STATE_DEAD;
			}
		}
	}

	dev->blocksInCheckpoint = 0;

	return 1;
}


static void yaffs_CheckpointFindNextErasedBlock(yaffs_Device *dev)
{
	int  i;
	int blocksAvailable = dev->nErasedBlocks - dev->param.nReservedBlocks;
	T(YAFFS_TRACE_CHECKPOINT,
		(TSTR("allocating checkpt block: erased %d reserved %d avail %d next %d "TENDSTR),
		dev->nErasedBlocks, dev->param.nReservedBlocks, blocksAvailable, dev->checkpointNextBlock));

	if (dev->checkpointNextBlock >= 0 &&
			dev->checkpointNextBlock <= dev->internalEndBlock &&
			blocksAvailable > 0) {

		for (i = dev->checkpointNextBlock; i <= dev->internalEndBlock; i++) {
			yaffs_BlockInfo *bi = yaffs_GetBlockInfo(dev, i);
			if (bi->blockState == YAFFS_BLOCK_STATE_EMPTY) {
				dev->checkpointNextBlock = i + 1;
				dev->checkpointCurrentBlock = i;
				T(YAFFS_TRACE_CHECKPOINT, (TSTR("allocating checkpt block %d"TENDSTR), i));
				return;
			}
		}
	}
	T(YAFFS_TRACE_CHECKPOINT, (TSTR("out of checkpt blocks"TENDSTR)));

	dev->checkpointNextBlock = -1;
	dev->checkpointCurrentBlock = -1;
}

static void yaffs_CheckpointFindNextCheckpointBlock(yaffs_Device *dev)
{
	int  i;
	yaffs_ExtendedTags tags;

	T(YAFFS_TRACE_CHECKPOINT, (TSTR("find next checkpt block: start:  blocks %d next %d" TENDSTR),
		dev->blocksInCheckpoint, dev->checkpointNextBlock));

	if (dev->blocksInCheckpoint < dev->checkpointMaxBlocks)
		for (i = dev->checkpointNextBlock; i <= dev->internalEndBlock; i++) {
			int chunk = i * dev->param.nChunksPerBlock;
			int realignedChunk = chunk - dev->chunkOffset;

			dev->param.readChunkWithTagsFromNAND(dev, realignedChunk,
					NULL, &tags);
			T(YAFFS_TRACE_CHECKPOINT, (TSTR("find next checkpt block: search: block %d oid %d seq %d eccr %d" TENDSTR),
				i, tags.objectId, tags.sequenceNumber, tags.eccResult));

			if (tags.sequenceNumber == YAFFS_SEQUENCE_CHECKPOINT_DATA) {
				/* Right kind of block */
				dev->checkpointNextBlock = tags.objectId;
				dev->checkpointCurrentBlock = i;
				dev->checkpointBlockList[dev->blocksInCheckpoint] = i;
				dev->blocksInCheckpoint++;
				T(YAFFS_TRACE_CHECKPOINT, (TSTR("found checkpt block %d"TENDSTR), i));
				return;
			}
		}

	T(YAFFS_TRACE_CHECKPOINT, (TSTR("found no more checkpt blocks"TENDSTR)));

	dev->checkpointNextBlock = -1;
	dev->checkpointCurrentBlock = -1;
}


int yaffs_CheckpointOpen(yaffs_Device *dev, int forWriting)
{


	dev->checkpointOpenForWrite = forWriting;

	/* Got the functions we need? */
	if (!dev->param.writeChunkWithTagsToNAND ||
		!dev->param.readChunkWithTagsFromNAND ||
		!dev->param.eraseBlockInNAND ||
		!dev->param.markNANDBlockBad)
		return 0;

	if (forWriting && !yaffs_CheckpointSpaceOk(dev))
		return 0;

	if (!dev->checkpointBuffer)
		dev->checkpointBuffer = YMALLOC_DMA(dev->param.totalBytesPerChunk);
	if (!dev->checkpointBuffer)
		return 0;


	dev->checkpointPageSequence = 0;
	dev->checkpointByteCount = 0;
	dev->checkpointSum = 0;
	dev->checkpointXor = 0;
	dev->checkpointCurrentBlock = -1;
	dev->checkpointCurrentChunk = -1;
	dev->checkpointNextBlock = dev->internalStartBlock;

	/* Erase all the blocks in the checkpoint area */
	if (forWriting) {
		memset(dev->checkpointBuffer, 0, dev->nDataBytesPerChunk);
		dev->checkpointByteOffset = 0;
		return yaffs_CheckpointErase(dev);
	} else {
		int i;
		/* Set to a value that will kick off a read */
		dev->checkpointByteOffset = dev->nDataBytesPerChunk;
		/* A checkpoint block list of 1 checkpoint block per 16 block is (hopefully)
		 * going to be way more than we need */
		dev->blocksInCheckpoint = 0;
		dev->checkpointMaxBlocks = (dev->internalEndBlock - dev->internalStartBlock)/16 + 2;
		dev->checkpointBlockList = YMALLOC(sizeof(int) * dev->checkpointMaxBlocks);
		if(!dev->checkpointBlockList)
			return 0;

		for (i = 0; i < dev->checkpointMaxBlocks; i++)
			dev->checkpointBlockList[i] = -1;
	}

	return 1;
}

int yaffs_GetCheckpointSum(yaffs_Device *dev, __u32 *sum)
{
	__u32 compositeSum;
	compositeSum =  (dev->checkpointSum << 8) | (dev->checkpointXor & 0xFF);
	*sum = compositeSum;
	return 1;
}

static int yaffs_CheckpointFlushBuffer(yaffs_Device *dev)
{
	int chunk;
	int realignedChunk;

	yaffs_ExtendedTags tags;

	if (dev->checkpointCurrentBlock < 0) {
		yaffs_CheckpointFindNextErasedBlock(dev);
		dev->checkpointCurrentChunk = 0;
	}

	if (dev->checkpointCurrentBlock < 0)
		return 0;

	tags.chunkDeleted = 0;
	tags.objectId = dev->checkpointNextBlock; /* Hint to next place to look */
	tags.chunkId = dev->checkpointPageSequence + 1;
	tags.sequenceNumber =  YAFFS_SEQUENCE_CHECKPOINT_DATA;
	tags.byteCount = dev->nDataBytesPerChunk;
	if (dev->checkpointCurrentChunk == 0) {
		/* First chunk we write for the block? Set block state to
		   checkpoint */
		yaffs_BlockInfo *bi = yaffs_GetBlockInfo(dev, dev->checkpointCurrentBlock);
		bi->blockState = YAFFS_BLOCK_STATE_CHECKPOINT;
		dev->blocksInCheckpoint++;
	}

	chunk = dev->checkpointCurrentBlock * dev->param.nChunksPerBlock + dev->checkpointCurrentChunk;


	T(YAFFS_TRACE_CHECKPOINT, (TSTR("checkpoint wite buffer nand %d(%d:%d) objid %d chId %d" TENDSTR),
		chunk, dev->checkpointCurrentBlock, dev->checkpointCurrentChunk, tags.objectId, tags.chunkId));

	realignedChunk = chunk - dev->chunkOffset;

	dev->nPageWrites++;

	dev->param.writeChunkWithTagsToNAND(dev, realignedChunk,
			dev->checkpointBuffer, &tags);
	dev->checkpointByteOffset = 0;
	dev->checkpointPageSequence++;
	dev->checkpointCurrentChunk++;
	if (dev->checkpointCurrentChunk >= dev->param.nChunksPerBlock) {
		dev->checkpointCurrentChunk = 0;
		dev->checkpointCurrentBlock = -1;
	}
	memset(dev->checkpointBuffer, 0, dev->nDataBytesPerChunk);

	return 1;
}


int yaffs_CheckpointWrite(yaffs_Device *dev, const void *data, int nBytes)
{
	int i = 0;
	int ok = 1;


	__u8 * dataBytes = (__u8 *)data;



	if (!dev->checkpointBuffer)
		return 0;

	if (!dev->checkpointOpenForWrite)
		return -1;

	while (i < nBytes && ok) {
		dev->checkpointBuffer[dev->checkpointByteOffset] = *dataBytes;
		dev->checkpointSum += *dataBytes;
		dev->checkpointXor ^= *dataBytes;

		dev->checkpointByteOffset++;
		i++;
		dataBytes++;
		dev->checkpointByteCount++;


		if (dev->checkpointByteOffset < 0 ||
		   dev->checkpointByteOffset >= dev->nDataBytesPerChunk)
			ok = yaffs_CheckpointFlushBuffer(dev);
	}

	return i;
}

int yaffs_CheckpointRead(yaffs_Device *dev, void *data, int nBytes)
{
	int i = 0;
	int ok = 1;
	yaffs_ExtendedTags tags;


	int chunk;
	int realignedChunk;

	__u8 *dataBytes = (__u8 *)data;

	if (!dev->checkpointBuffer)
		return 0;

	if (dev->checkpointOpenForWrite)
		return -1;

	while (i < nBytes && ok) {


		if (dev->checkpointByteOffset < 0 ||
			dev->checkpointByteOffset >= dev->nDataBytesPerChunk) {

			if (dev->checkpointCurrentBlock < 0) {
				yaffs_CheckpointFindNextCheckpointBlock(dev);
				dev->checkpointCurrentChunk = 0;
			}

			if (dev->checkpointCurrentBlock < 0)
				ok = 0;
			else {
				chunk = dev->checkpointCurrentBlock *
					dev->param.nChunksPerBlock +
					dev->checkpointCurrentChunk;

				realignedChunk = chunk - dev->chunkOffset;
				
				dev->nPageReads++;

				/* read in the next chunk */
				/* printf("read checkpoint page %d\n",dev->checkpointPage); */
				dev->param.readChunkWithTagsFromNAND(dev,
						realignedChunk,
						dev->checkpointBuffer,
						&tags);

				if (tags.chunkId != (dev->checkpointPageSequence + 1) ||
					tags.eccResult > YAFFS_ECC_RESULT_FIXED ||
					tags.sequenceNumber != YAFFS_SEQUENCE_CHECKPOINT_DATA)
					ok = 0;

				dev->checkpointByteOffset = 0;
				dev->checkpointPageSequence++;
				dev->checkpointCurrentChunk++;

				if (dev->checkpointCurrentChunk >= dev->param.nChunksPerBlock)
					dev->checkpointCurrentBlock = -1;
			}
		}

		if (ok) {
			*dataBytes = dev->checkpointBuffer[dev->checkpointByteOffset];
			dev->checkpointSum += *dataBytes;
			dev->checkpointXor ^= *dataBytes;
			dev->checkpointByteOffset++;
			i++;
			dataBytes++;
			dev->checkpointByteCount++;
		}
	}

	return 	i;
}

int yaffs_CheckpointClose(yaffs_Device *dev)
{

	if (dev->checkpointOpenForWrite) {
		if (dev->checkpointByteOffset != 0)
			yaffs_CheckpointFlushBuffer(dev);
	} else if(dev->checkpointBlockList){
		int i;
		for (i = 0; i < dev->blocksInCheckpoint && dev->checkpointBlockList[i] >= 0; i++) {
			int blk = dev->checkpointBlockList[i];
			yaffs_BlockInfo *bi = NULL;
			if( dev->internalStartBlock <= blk && blk <= dev->internalEndBlock)
				bi = yaffs_GetBlockInfo(dev, blk);
			if (bi && bi->blockState == YAFFS_BLOCK_STATE_EMPTY)
				bi->blockState = YAFFS_BLOCK_STATE_CHECKPOINT;
			else {
				/* Todo this looks odd... */
			}
		}
		YFREE(dev->checkpointBlockList);
		dev->checkpointBlockList = NULL;
	}

	dev->nFreeChunks -= dev->blocksInCheckpoint * dev->param.nChunksPerBlock;
	dev->nErasedBlocks -= dev->blocksInCheckpoint;


	T(YAFFS_TRACE_CHECKPOINT, (TSTR("checkpoint byte count %d" TENDSTR),
			dev->checkpointByteCount));

	if (dev->checkpointBuffer) {
		/* free the buffer */
		YFREE(dev->checkpointBuffer);
		dev->checkpointBuffer = NULL;
		return 1;
	} else
		return 0;
}

int yaffs_CheckpointInvalidateStream(yaffs_Device *dev)
{
	/* Erase the checkpoint data */

	T(YAFFS_TRACE_CHECKPOINT, (TSTR("checkpoint invalidate of %d blocks"TENDSTR),
		dev->blocksInCheckpoint));

	return yaffs_CheckpointErase(dev);
}



