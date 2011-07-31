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

/* mtd interface for YAFFS2 */

#include "yportenv.h"
#include "yaffs_trace.h"

#include "yaffs_mtdif2.h"

#include "linux/mtd/mtd.h"
#include "linux/types.h"
#include "linux/time.h"

#include "yaffs_packedtags2.h"

#include "yaffs_linux.h"

/* NB For use with inband tags....
 * We assume that the data buffer is of size totalBytersPerChunk so that we can also
 * use it to load the tags.
 */
int nandmtd2_WriteChunkWithTagsToNAND(yaffs_Device *dev, int chunkInNAND,
				      const __u8 *data,
				      const yaffs_ExtendedTags *tags)
{
	struct mtd_info *mtd = yaffs_DeviceToContext(dev)->mtd;
#if (MTD_VERSION_CODE > MTD_VERSION(2, 6, 17))
	struct mtd_oob_ops ops;
#else
	size_t dummy;
#endif
	int retval = 0;

	loff_t addr;

	yaffs_PackedTags2 pt;

	int packed_tags_size = dev->param.noTagsECC ? sizeof(pt.t) : sizeof(pt);
	void * packed_tags_ptr = dev->param.noTagsECC ? (void *) &pt.t : (void *)&pt;

	T(YAFFS_TRACE_MTD,
	  (TSTR
	   ("nandmtd2_WriteChunkWithTagsToNAND chunk %d data %p tags %p"
	    TENDSTR), chunkInNAND, data, tags));


	addr  = ((loff_t) chunkInNAND) * dev->param.totalBytesPerChunk;

	/* For yaffs2 writing there must be both data and tags.
	 * If we're using inband tags, then the tags are stuffed into
	 * the end of the data buffer.
	 */
	if (!data || !tags)
		BUG();
	else if (dev->param.inbandTags) {
		yaffs_PackedTags2TagsPart *pt2tp;
		pt2tp = (yaffs_PackedTags2TagsPart *)(data + dev->nDataBytesPerChunk);
		yaffs_PackTags2TagsPart(pt2tp, tags);
	} else
		yaffs_PackTags2(&pt, tags, !dev->param.noTagsECC);

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 17))
	ops.mode = MTD_OOB_AUTO;
	ops.ooblen = (dev->param.inbandTags) ? 0 : packed_tags_size;
	ops.len = dev->param.totalBytesPerChunk;
	ops.ooboffs = 0;
	ops.datbuf = (__u8 *)data;
	ops.oobbuf = (dev->param.inbandTags) ? NULL : packed_tags_ptr;
	retval = mtd->write_oob(mtd, addr, &ops);

#else
	if (!dev->param.inbandTags) {
		retval =
		    mtd->write_ecc(mtd, addr, dev->nDataBytesPerChunk,
				   &dummy, data, (__u8 *) packed_tags_ptr, NULL);
	} else {
		retval =
		    mtd->write(mtd, addr, dev->param.totalBytesPerChunk, &dummy,
			       data);
	}
#endif

	if (retval == 0)
		return YAFFS_OK;
	else
		return YAFFS_FAIL;
}

int nandmtd2_ReadChunkWithTagsFromNAND(yaffs_Device *dev, int chunkInNAND,
				       __u8 *data, yaffs_ExtendedTags *tags)
{
	struct mtd_info *mtd = yaffs_DeviceToContext(dev)->mtd;
#if (MTD_VERSION_CODE > MTD_VERSION(2, 6, 17))
	struct mtd_oob_ops ops;
#endif
	size_t dummy;
	int retval = 0;
	int localData = 0;

	loff_t addr = ((loff_t) chunkInNAND) * dev->param.totalBytesPerChunk;

	yaffs_PackedTags2 pt;

	int packed_tags_size = dev->param.noTagsECC ? sizeof(pt.t) : sizeof(pt);
	void * packed_tags_ptr = dev->param.noTagsECC ? (void *) &pt.t: (void *)&pt;

	T(YAFFS_TRACE_MTD,
	  (TSTR
	   ("nandmtd2_ReadChunkWithTagsFromNAND chunk %d data %p tags %p"
	    TENDSTR), chunkInNAND, data, tags));

	if (dev->param.inbandTags) {

		if (!data) {
			localData = 1;
			data = yaffs_GetTempBuffer(dev, __LINE__);
		}


	}


#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 17))
	if (dev->param.inbandTags || (data && !tags))
		retval = mtd->read(mtd, addr, dev->param.totalBytesPerChunk,
				&dummy, data);
	else if (tags) {
		ops.mode = MTD_OOB_AUTO;
		ops.ooblen = packed_tags_size;
		ops.len = data ? dev->nDataBytesPerChunk : packed_tags_size;
		ops.ooboffs = 0;
		ops.datbuf = data;
		ops.oobbuf = yaffs_DeviceToContext(dev)->spareBuffer;
		retval = mtd->read_oob(mtd, addr, &ops);
	}
#else
	if (!dev->param.inbandTags && data && tags) {

		retval = mtd->read_ecc(mtd, addr, dev->nDataBytesPerChunk,
					  &dummy, data, dev->spareBuffer,
					  NULL);
	} else {
		if (data)
			retval =
			    mtd->read(mtd, addr, dev->nDataBytesPerChunk, &dummy,
				      data);
		if (!dev->param.inbandTags && tags)
			retval =
			    mtd->read_oob(mtd, addr, mtd->oobsize, &dummy,
					  dev->spareBuffer);
	}
#endif


	if (dev->param.inbandTags) {
		if (tags) {
			yaffs_PackedTags2TagsPart *pt2tp;
			pt2tp = (yaffs_PackedTags2TagsPart *)&data[dev->nDataBytesPerChunk];
			yaffs_UnpackTags2TagsPart(tags, pt2tp);
		}
	} else {
		if (tags) {
			memcpy(packed_tags_ptr, yaffs_DeviceToContext(dev)->spareBuffer, packed_tags_size);
			yaffs_UnpackTags2(tags, &pt, !dev->param.noTagsECC);
		}
	}

	if (localData)
		yaffs_ReleaseTempBuffer(dev, data, __LINE__);

	if (tags && retval == -EBADMSG && tags->eccResult == YAFFS_ECC_RESULT_NO_ERROR) {
		tags->eccResult = YAFFS_ECC_RESULT_UNFIXED;
		dev->eccUnfixed++;
	}
	if(tags && retval == -EUCLEAN && tags->eccResult == YAFFS_ECC_RESULT_NO_ERROR) {
		tags->eccResult = YAFFS_ECC_RESULT_FIXED;
		dev->eccFixed++;
	}
	if (retval == 0)
		return YAFFS_OK;
	else
		return YAFFS_FAIL;
}

int nandmtd2_MarkNANDBlockBad(struct yaffs_DeviceStruct *dev, int blockNo)
{
	struct mtd_info *mtd = yaffs_DeviceToContext(dev)->mtd;
	int retval;
	T(YAFFS_TRACE_MTD,
	  (TSTR("nandmtd2_MarkNANDBlockBad %d" TENDSTR), blockNo));

	retval =
	    mtd->block_markbad(mtd,
			       blockNo * dev->param.nChunksPerBlock *
			       dev->param.totalBytesPerChunk);

	if (retval == 0)
		return YAFFS_OK;
	else
		return YAFFS_FAIL;

}

int nandmtd2_QueryNANDBlock(struct yaffs_DeviceStruct *dev, int blockNo,
			    yaffs_BlockState *state, __u32 *sequenceNumber)
{
	struct mtd_info *mtd = yaffs_DeviceToContext(dev)->mtd;
	int retval;

	T(YAFFS_TRACE_MTD,
	  (TSTR("nandmtd2_QueryNANDBlock %d" TENDSTR), blockNo));
	retval =
	    mtd->block_isbad(mtd,
			     blockNo * dev->param.nChunksPerBlock *
			     dev->param.totalBytesPerChunk);

	if (retval) {
		T(YAFFS_TRACE_MTD, (TSTR("block is bad" TENDSTR)));

		*state = YAFFS_BLOCK_STATE_DEAD;
		*sequenceNumber = 0;
	} else {
		yaffs_ExtendedTags t;
		nandmtd2_ReadChunkWithTagsFromNAND(dev,
						   blockNo *
						   dev->param.nChunksPerBlock, NULL,
						   &t);

		if (t.chunkUsed) {
			*sequenceNumber = t.sequenceNumber;
			*state = YAFFS_BLOCK_STATE_NEEDS_SCANNING;
		} else {
			*sequenceNumber = 0;
			*state = YAFFS_BLOCK_STATE_EMPTY;
		}
	}
	T(YAFFS_TRACE_MTD,
	  (TSTR("block is bad seq %d state %d" TENDSTR), *sequenceNumber,
	   *state));

	if (retval == 0)
		return YAFFS_OK;
	else
		return YAFFS_FAIL;
}

