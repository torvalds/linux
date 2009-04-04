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

#include "yaffs_packedtags1.h"
#include "yportenv.h"

void yaffs_PackTags1(yaffs_PackedTags1 *pt, const yaffs_ExtendedTags *t)
{
	pt->chunkId = t->chunkId;
	pt->serialNumber = t->serialNumber;
	pt->byteCount = t->byteCount;
	pt->objectId = t->objectId;
	pt->ecc = 0;
	pt->deleted = (t->chunkDeleted) ? 0 : 1;
	pt->unusedStuff = 0;
	pt->shouldBeFF = 0xFFFFFFFF;

}

void yaffs_UnpackTags1(yaffs_ExtendedTags *t, const yaffs_PackedTags1 *pt)
{
	static const __u8 allFF[] =
	    { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
0xff };

	if (memcmp(allFF, pt, sizeof(yaffs_PackedTags1))) {
		t->blockBad = 0;
		if (pt->shouldBeFF != 0xFFFFFFFF)
			t->blockBad = 1;
		t->chunkUsed = 1;
		t->objectId = pt->objectId;
		t->chunkId = pt->chunkId;
		t->byteCount = pt->byteCount;
		t->eccResult = YAFFS_ECC_RESULT_NO_ERROR;
		t->chunkDeleted = (pt->deleted) ? 0 : 1;
		t->serialNumber = pt->serialNumber;
	} else {
		memset(t, 0, sizeof(yaffs_ExtendedTags));
	}
}
