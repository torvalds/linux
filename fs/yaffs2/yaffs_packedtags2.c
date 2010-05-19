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

#include "yaffs_packedtags2.h"
#include "yportenv.h"
#include "yaffs_trace.h"
#include "yaffs_tagsvalidity.h"

/* This code packs a set of extended tags into a binary structure for
 * NAND storage
 */

/* Some of the information is "extra" struff which can be packed in to
 * speed scanning
 * This is defined by having the EXTRA_HEADER_INFO_FLAG set.
 */

/* Extra flags applied to chunk_id */

#define EXTRA_HEADER_INFO_FLAG	0x80000000
#define EXTRA_SHRINK_FLAG	0x40000000
#define EXTRA_SHADOWS_FLAG	0x20000000
#define EXTRA_SPARE_FLAGS	0x10000000

#define ALL_EXTRA_FLAGS		0xF0000000

/* Also, the top 4 bits of the object Id are set to the object type. */
#define EXTRA_OBJECT_TYPE_SHIFT (28)
#define EXTRA_OBJECT_TYPE_MASK  ((0x0F) << EXTRA_OBJECT_TYPE_SHIFT)

static void yaffs_dump_packed_tags2_tags_only(const struct
					      yaffs_packed_tags2_tags_only *ptt)
{
	yaffs_trace(YAFFS_TRACE_MTD,
		"packed tags obj %d chunk %d byte %d seq %d",
		ptt->obj_id, ptt->chunk_id, ptt->n_bytes, ptt->seq_number);
}

static void yaffs_dump_packed_tags2(const struct yaffs_packed_tags2 *pt)
{
	yaffs_dump_packed_tags2_tags_only(&pt->t);
}

static void yaffs_dump_tags2(const struct yaffs_ext_tags *t)
{
	yaffs_trace(YAFFS_TRACE_MTD,
		"ext.tags eccres %d blkbad %d chused %d obj %d chunk%d byte %d del %d ser %d seq %d",
		t->ecc_result, t->block_bad, t->chunk_used, t->obj_id,
		t->chunk_id, t->n_bytes, t->is_deleted, t->serial_number,
		t->seq_number);

}

void yaffs_pack_tags2_tags_only(struct yaffs_packed_tags2_tags_only *ptt,
				const struct yaffs_ext_tags *t)
{
	ptt->chunk_id = t->chunk_id;
	ptt->seq_number = t->seq_number;
	ptt->n_bytes = t->n_bytes;
	ptt->obj_id = t->obj_id;

	if (t->chunk_id == 0 && t->extra_available) {
		/* Store the extra header info instead */
		/* We save the parent object in the chunk_id */
		ptt->chunk_id = EXTRA_HEADER_INFO_FLAG | t->extra_parent_id;
		if (t->extra_is_shrink)
			ptt->chunk_id |= EXTRA_SHRINK_FLAG;
		if (t->extra_shadows)
			ptt->chunk_id |= EXTRA_SHADOWS_FLAG;

		ptt->obj_id &= ~EXTRA_OBJECT_TYPE_MASK;
		ptt->obj_id |= (t->extra_obj_type << EXTRA_OBJECT_TYPE_SHIFT);

		if (t->extra_obj_type == YAFFS_OBJECT_TYPE_HARDLINK)
			ptt->n_bytes = t->extra_equiv_id;
		else if (t->extra_obj_type == YAFFS_OBJECT_TYPE_FILE)
			ptt->n_bytes = t->extra_length;
		else
			ptt->n_bytes = 0;
	}

	yaffs_dump_packed_tags2_tags_only(ptt);
	yaffs_dump_tags2(t);
}

void yaffs_pack_tags2(struct yaffs_packed_tags2 *pt,
		      const struct yaffs_ext_tags *t, int tags_ecc)
{
	yaffs_pack_tags2_tags_only(&pt->t, t);

	if (tags_ecc)
		yaffs_ecc_calc_other((unsigned char *)&pt->t,
				     sizeof(struct
					    yaffs_packed_tags2_tags_only),
				     &pt->ecc);
}

void yaffs_unpack_tags2_tags_only(struct yaffs_ext_tags *t,
				  struct yaffs_packed_tags2_tags_only *ptt)
{

	memset(t, 0, sizeof(struct yaffs_ext_tags));

	yaffs_init_tags(t);

	if (ptt->seq_number != 0xFFFFFFFF) {
		t->block_bad = 0;
		t->chunk_used = 1;
		t->obj_id = ptt->obj_id;
		t->chunk_id = ptt->chunk_id;
		t->n_bytes = ptt->n_bytes;
		t->is_deleted = 0;
		t->serial_number = 0;
		t->seq_number = ptt->seq_number;

		/* Do extra header info stuff */

		if (ptt->chunk_id & EXTRA_HEADER_INFO_FLAG) {
			t->chunk_id = 0;
			t->n_bytes = 0;

			t->extra_available = 1;
			t->extra_parent_id =
			    ptt->chunk_id & (~(ALL_EXTRA_FLAGS));
			t->extra_is_shrink =
			    (ptt->chunk_id & EXTRA_SHRINK_FLAG) ? 1 : 0;
			t->extra_shadows =
			    (ptt->chunk_id & EXTRA_SHADOWS_FLAG) ? 1 : 0;
			t->extra_obj_type =
			    ptt->obj_id >> EXTRA_OBJECT_TYPE_SHIFT;
			t->obj_id &= ~EXTRA_OBJECT_TYPE_MASK;

			if (t->extra_obj_type == YAFFS_OBJECT_TYPE_HARDLINK)
				t->extra_equiv_id = ptt->n_bytes;
			else
				t->extra_length = ptt->n_bytes;
		}
	}

	yaffs_dump_packed_tags2_tags_only(ptt);
	yaffs_dump_tags2(t);

}

void yaffs_unpack_tags2(struct yaffs_ext_tags *t, struct yaffs_packed_tags2 *pt,
			int tags_ecc)
{

	enum yaffs_ecc_result ecc_result = YAFFS_ECC_RESULT_NO_ERROR;

	if (pt->t.seq_number != 0xFFFFFFFF && tags_ecc) {
		/* Chunk is in use and we need to do ECC */

		struct yaffs_ecc_other ecc;
		int result;
		yaffs_ecc_calc_other((unsigned char *)&pt->t,
				     sizeof(struct
					    yaffs_packed_tags2_tags_only),
				     &ecc);
		result =
		    yaffs_ecc_correct_other((unsigned char *)&pt->t,
					    sizeof(struct
						   yaffs_packed_tags2_tags_only),
					    &pt->ecc, &ecc);
		switch (result) {
		case 0:
			ecc_result = YAFFS_ECC_RESULT_NO_ERROR;
			break;
		case 1:
			ecc_result = YAFFS_ECC_RESULT_FIXED;
			break;
		case -1:
			ecc_result = YAFFS_ECC_RESULT_UNFIXED;
			break;
		default:
			ecc_result = YAFFS_ECC_RESULT_UNKNOWN;
		}
	}

	yaffs_unpack_tags2_tags_only(t, &pt->t);

	t->ecc_result = ecc_result;

	yaffs_dump_packed_tags2(pt);
	yaffs_dump_tags2(t);
}
