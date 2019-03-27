/*-
 * Copyright (c) 2012-2016 Solarflare Communications Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation are
 * those of the authors and should not be interpreted as representing official
 * policies, either expressed or implied, of the FreeBSD Project.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "efx.h"
#include "efx_impl.h"

#if EFSYS_OPT_HUNTINGTON || EFSYS_OPT_MEDFORD || EFSYS_OPT_MEDFORD2

#if EFSYS_OPT_VPD || EFSYS_OPT_NVRAM

#include "ef10_tlv_layout.h"

/* Cursor for TLV partition format */
typedef struct tlv_cursor_s {
	uint32_t	*block;			/* Base of data block */
	uint32_t	*current;		/* Cursor position */
	uint32_t	*end;			/* End tag position */
	uint32_t	*limit;			/* Last dword of data block */
} tlv_cursor_t;

typedef struct nvram_partition_s {
	uint16_t type;
	uint8_t chip_select;
	uint8_t flags;
	/*
	 * The full length of the NVRAM partition.
	 * This is different from tlv_partition_header.total_length,
	 *  which can be smaller.
	 */
	uint32_t length;
	uint32_t erase_size;
	uint32_t *data;
	tlv_cursor_t tlv_cursor;
} nvram_partition_t;


static	__checkReturn		efx_rc_t
tlv_validate_state(
	__inout			tlv_cursor_t *cursor);


static				void
tlv_init_block(
	__out	uint32_t	*block)
{
	*block = __CPU_TO_LE_32(TLV_TAG_END);
}

static				uint32_t
tlv_tag(
	__in	tlv_cursor_t	*cursor)
{
	uint32_t dword, tag;

	dword = cursor->current[0];
	tag = __LE_TO_CPU_32(dword);

	return (tag);
}

static				size_t
tlv_length(
	__in	tlv_cursor_t	*cursor)
{
	uint32_t dword, length;

	if (tlv_tag(cursor) == TLV_TAG_END)
		return (0);

	dword = cursor->current[1];
	length = __LE_TO_CPU_32(dword);

	return ((size_t)length);
}

static				uint8_t *
tlv_value(
	__in	tlv_cursor_t	*cursor)
{
	if (tlv_tag(cursor) == TLV_TAG_END)
		return (NULL);

	return ((uint8_t *)(&cursor->current[2]));
}

static				uint8_t *
tlv_item(
	__in	tlv_cursor_t	*cursor)
{
	if (tlv_tag(cursor) == TLV_TAG_END)
		return (NULL);

	return ((uint8_t *)cursor->current);
}

/*
 * TLV item DWORD length is tag + length + value (rounded up to DWORD)
 * equivalent to tlv_n_words_for_len in mc-comms tlv.c
 */
#define	TLV_DWORD_COUNT(length) \
	(1 + 1 + (((length) + sizeof (uint32_t) - 1) / sizeof (uint32_t)))


static				uint32_t *
tlv_next_item_ptr(
	__in	tlv_cursor_t	*cursor)
{
	uint32_t length;

	length = tlv_length(cursor);

	return (cursor->current + TLV_DWORD_COUNT(length));
}

static	__checkReturn		efx_rc_t
tlv_advance(
	__inout	tlv_cursor_t	*cursor)
{
	efx_rc_t rc;

	if ((rc = tlv_validate_state(cursor)) != 0)
		goto fail1;

	if (cursor->current == cursor->end) {
		/* No more tags after END tag */
		cursor->current = NULL;
		rc = ENOENT;
		goto fail2;
	}

	/* Advance to next item and validate */
	cursor->current = tlv_next_item_ptr(cursor);

	if ((rc = tlv_validate_state(cursor)) != 0)
		goto fail3;

	return (0);

fail3:
	EFSYS_PROBE(fail3);
fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

static				efx_rc_t
tlv_rewind(
	__in	tlv_cursor_t	*cursor)
{
	efx_rc_t rc;

	cursor->current = cursor->block;

	if ((rc = tlv_validate_state(cursor)) != 0)
		goto fail1;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

static				efx_rc_t
tlv_find(
	__inout	tlv_cursor_t	*cursor,
	__in	uint32_t	tag)
{
	efx_rc_t rc;

	rc = tlv_rewind(cursor);
	while (rc == 0) {
		if (tlv_tag(cursor) == tag)
			break;

		rc = tlv_advance(cursor);
	}
	return (rc);
}

static	__checkReturn		efx_rc_t
tlv_validate_state(
	__inout	tlv_cursor_t	*cursor)
{
	efx_rc_t rc;

	/* Check cursor position */
	if (cursor->current < cursor->block) {
		rc = EINVAL;
		goto fail1;
	}
	if (cursor->current > cursor->limit) {
		rc = EINVAL;
		goto fail2;
	}

	if (tlv_tag(cursor) != TLV_TAG_END) {
		/* Check current item has space for tag and length */
		if (cursor->current > (cursor->limit - 1)) {
			cursor->current = NULL;
			rc = EFAULT;
			goto fail3;
		}

		/* Check we have value data for current item and an END tag */
		if (tlv_next_item_ptr(cursor) > cursor->limit) {
			cursor->current = NULL;
			rc = EFAULT;
			goto fail4;
		}
	}

	return (0);

fail4:
	EFSYS_PROBE(fail4);
fail3:
	EFSYS_PROBE(fail3);
fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

static				efx_rc_t
tlv_init_cursor(
	__out	tlv_cursor_t	*cursor,
	__in	uint32_t	*block,
	__in	uint32_t	*limit,
	__in	uint32_t	*current)
{
	cursor->block	= block;
	cursor->limit	= limit;

	cursor->current	= current;
	cursor->end	= NULL;

	return (tlv_validate_state(cursor));
}

static	__checkReturn		efx_rc_t
tlv_init_cursor_from_size(
	__out	tlv_cursor_t	*cursor,
	__in_bcount(size)
		uint8_t		*block,
	__in	size_t		size)
{
	uint32_t *limit;
	limit = (uint32_t *)(block + size - sizeof (uint32_t));
	return (tlv_init_cursor(cursor, (uint32_t *)block,
		limit, (uint32_t *)block));
}

static	__checkReturn		efx_rc_t
tlv_init_cursor_at_offset(
	__out	tlv_cursor_t	*cursor,
	__in_bcount(size)
		uint8_t		*block,
	__in	size_t		size,
	__in	size_t		offset)
{
	uint32_t *limit;
	uint32_t *current;
	limit = (uint32_t *)(block + size - sizeof (uint32_t));
	current = (uint32_t *)(block + offset);
	return (tlv_init_cursor(cursor, (uint32_t *)block, limit, current));
}

static	__checkReturn		efx_rc_t
tlv_require_end(
	__inout	tlv_cursor_t	*cursor)
{
	uint32_t *pos;
	efx_rc_t rc;

	if (cursor->end == NULL) {
		pos = cursor->current;
		if ((rc = tlv_find(cursor, TLV_TAG_END)) != 0)
			goto fail1;

		cursor->end = cursor->current;
		cursor->current = pos;
	}

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

static				size_t
tlv_block_length_used(
	__inout	tlv_cursor_t	*cursor)
{
	efx_rc_t rc;

	if ((rc = tlv_validate_state(cursor)) != 0)
		goto fail1;

	if ((rc = tlv_require_end(cursor)) != 0)
		goto fail2;

	/* Return space used (including the END tag) */
	return (cursor->end + 1 - cursor->block) * sizeof (uint32_t);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (0);
}

static		uint32_t *
tlv_last_segment_end(
	__in	tlv_cursor_t *cursor)
{
	tlv_cursor_t segment_cursor;
	uint32_t *last_segment_end = cursor->block;
	uint32_t *segment_start = cursor->block;

	/*
	 * Go through each segment and check that it has an end tag. If there
	 * is no end tag then the previous segment was the last valid one,
	 * so return the pointer to its end tag.
	 */
	for (;;) {
		if (tlv_init_cursor(&segment_cursor, segment_start,
		    cursor->limit, segment_start) != 0)
			break;
		if (tlv_require_end(&segment_cursor) != 0)
			break;
		last_segment_end = segment_cursor.end;
		segment_start = segment_cursor.end + 1;
	}

	return (last_segment_end);
}


static				uint32_t *
tlv_write(
	__in			tlv_cursor_t *cursor,
	__in			uint32_t tag,
	__in_bcount(size)	uint8_t *data,
	__in			size_t size)
{
	uint32_t len = size;
	uint32_t *ptr;

	ptr = cursor->current;

	*ptr++ = __CPU_TO_LE_32(tag);
	*ptr++ = __CPU_TO_LE_32(len);

	if (len > 0) {
		ptr[(len - 1) / sizeof (uint32_t)] = 0;
		memcpy(ptr, data, len);
		ptr += P2ROUNDUP(len, sizeof (uint32_t)) / sizeof (*ptr);
	}

	return (ptr);
}

static	__checkReturn		efx_rc_t
tlv_insert(
	__inout	tlv_cursor_t	*cursor,
	__in	uint32_t	tag,
	__in_bcount(size)
		uint8_t		*data,
	__in	size_t		size)
{
	unsigned int delta;
	uint32_t *last_segment_end;
	efx_rc_t rc;

	if ((rc = tlv_validate_state(cursor)) != 0)
		goto fail1;

	if ((rc = tlv_require_end(cursor)) != 0)
		goto fail2;

	if (tag == TLV_TAG_END) {
		rc = EINVAL;
		goto fail3;
	}

	last_segment_end = tlv_last_segment_end(cursor);

	delta = TLV_DWORD_COUNT(size);
	if (last_segment_end + 1 + delta > cursor->limit) {
		rc = ENOSPC;
		goto fail4;
	}

	/* Move data up: new space at cursor->current */
	memmove(cursor->current + delta, cursor->current,
	    (last_segment_end + 1 - cursor->current) * sizeof (uint32_t));

	/* Adjust the end pointer */
	cursor->end += delta;

	/* Write new TLV item */
	tlv_write(cursor, tag, data, size);

	return (0);

fail4:
	EFSYS_PROBE(fail4);
fail3:
	EFSYS_PROBE(fail3);
fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

static	__checkReturn		efx_rc_t
tlv_delete(
	__inout	tlv_cursor_t	*cursor)
{
	unsigned int delta;
	uint32_t *last_segment_end;
	efx_rc_t rc;

	if ((rc = tlv_validate_state(cursor)) != 0)
		goto fail1;

	if (tlv_tag(cursor) == TLV_TAG_END) {
		rc = EINVAL;
		goto fail2;
	}

	delta = TLV_DWORD_COUNT(tlv_length(cursor));

	if ((rc = tlv_require_end(cursor)) != 0)
		goto fail3;

	last_segment_end = tlv_last_segment_end(cursor);

	/* Shuffle things down, destroying the item at cursor->current */
	memmove(cursor->current, cursor->current + delta,
	    (last_segment_end + 1 - cursor->current) * sizeof (uint32_t));
	/* Zero the new space at the end of the TLV chain */
	memset(last_segment_end + 1 - delta, 0, delta * sizeof (uint32_t));
	/* Adjust the end pointer */
	cursor->end -= delta;

	return (0);

fail3:
	EFSYS_PROBE(fail3);
fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

static	__checkReturn		efx_rc_t
tlv_modify(
	__inout	tlv_cursor_t	*cursor,
	__in	uint32_t	tag,
	__in_bcount(size)
		uint8_t		*data,
	__in	size_t		size)
{
	uint32_t *pos;
	unsigned int old_ndwords;
	unsigned int new_ndwords;
	unsigned int delta;
	uint32_t *last_segment_end;
	efx_rc_t rc;

	if ((rc = tlv_validate_state(cursor)) != 0)
		goto fail1;

	if (tlv_tag(cursor) == TLV_TAG_END) {
		rc = EINVAL;
		goto fail2;
	}
	if (tlv_tag(cursor) != tag) {
		rc = EINVAL;
		goto fail3;
	}

	old_ndwords = TLV_DWORD_COUNT(tlv_length(cursor));
	new_ndwords = TLV_DWORD_COUNT(size);

	if ((rc = tlv_require_end(cursor)) != 0)
		goto fail4;

	last_segment_end = tlv_last_segment_end(cursor);

	if (new_ndwords > old_ndwords) {
		/* Expand space used for TLV item */
		delta = new_ndwords - old_ndwords;
		pos = cursor->current + old_ndwords;

		if (last_segment_end + 1 + delta > cursor->limit) {
			rc = ENOSPC;
			goto fail5;
		}

		/* Move up: new space at (cursor->current + old_ndwords) */
		memmove(pos + delta, pos,
		    (last_segment_end + 1 - pos) * sizeof (uint32_t));

		/* Adjust the end pointer */
		cursor->end += delta;

	} else if (new_ndwords < old_ndwords) {
		/* Shrink space used for TLV item */
		delta = old_ndwords - new_ndwords;
		pos = cursor->current + new_ndwords;

		/* Move down: remove words at (cursor->current + new_ndwords) */
		memmove(pos, pos + delta,
		    (last_segment_end + 1 - pos) * sizeof (uint32_t));

		/* Zero the new space at the end of the TLV chain */
		memset(last_segment_end + 1 - delta, 0,
		    delta * sizeof (uint32_t));

		/* Adjust the end pointer */
		cursor->end -= delta;
	}

	/* Write new data */
	tlv_write(cursor, tag, data, size);

	return (0);

fail5:
	EFSYS_PROBE(fail5);
fail4:
	EFSYS_PROBE(fail4);
fail3:
	EFSYS_PROBE(fail3);
fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

static uint32_t checksum_tlv_partition(
	__in	nvram_partition_t *partition)
{
	tlv_cursor_t *cursor;
	uint32_t *ptr;
	uint32_t *end;
	uint32_t csum;
	size_t len;

	cursor = &partition->tlv_cursor;
	len = tlv_block_length_used(cursor);
	EFSYS_ASSERT3U((len & 3), ==, 0);

	csum = 0;
	ptr = partition->data;
	end = &ptr[len >> 2];

	while (ptr < end)
		csum += __LE_TO_CPU_32(*ptr++);

	return (csum);
}

static	__checkReturn		efx_rc_t
tlv_update_partition_len_and_cks(
	__in	tlv_cursor_t *cursor)
{
	efx_rc_t rc;
	nvram_partition_t partition;
	struct tlv_partition_header *header;
	struct tlv_partition_trailer *trailer;
	size_t new_len;

	/*
	 * We just modified the partition, so the total length may not be
	 * valid. Don't use tlv_find(), which performs some sanity checks
	 * that may fail here.
	 */
	partition.data = cursor->block;
	memcpy(&partition.tlv_cursor, cursor, sizeof (*cursor));
	header = (struct tlv_partition_header *)partition.data;
	/* Sanity check. */
	if (__LE_TO_CPU_32(header->tag) != TLV_TAG_PARTITION_HEADER) {
		rc = EFAULT;
		goto fail1;
	}
	new_len =  tlv_block_length_used(&partition.tlv_cursor);
	if (new_len == 0) {
		rc = EFAULT;
		goto fail2;
	}
	header->total_length = __CPU_TO_LE_32(new_len);
	/* Ensure the modified partition always has a new generation count. */
	header->generation = __CPU_TO_LE_32(
	    __LE_TO_CPU_32(header->generation) + 1);

	trailer = (struct tlv_partition_trailer *)((uint8_t *)header +
	    new_len - sizeof (*trailer) - sizeof (uint32_t));
	trailer->generation = header->generation;
	trailer->checksum = __CPU_TO_LE_32(
	    __LE_TO_CPU_32(trailer->checksum) -
	    checksum_tlv_partition(&partition));

	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

/* Validate buffer contents (before writing to flash) */
	__checkReturn		efx_rc_t
ef10_nvram_buffer_validate(
	__in			uint32_t partn,
	__in_bcount(partn_size)	caddr_t partn_data,
	__in			size_t partn_size)
{
	tlv_cursor_t cursor;
	struct tlv_partition_header *header;
	struct tlv_partition_trailer *trailer;
	size_t total_length;
	uint32_t cksum;
	int pos;
	efx_rc_t rc;

	EFX_STATIC_ASSERT(sizeof (*header) <= EF10_NVRAM_CHUNK);

	if ((partn_data == NULL) || (partn_size == 0)) {
		rc = EINVAL;
		goto fail1;
	}

	/* The partition header must be the first item (at offset zero) */
	if ((rc = tlv_init_cursor_from_size(&cursor, (uint8_t *)partn_data,
		    partn_size)) != 0) {
		rc = EFAULT;
		goto fail2;
	}
	if (tlv_tag(&cursor) != TLV_TAG_PARTITION_HEADER) {
		rc = EINVAL;
		goto fail3;
	}
	header = (struct tlv_partition_header *)tlv_item(&cursor);

	/* Check TLV partition length (includes the END tag) */
	total_length = __LE_TO_CPU_32(header->total_length);
	if (total_length > partn_size) {
		rc = EFBIG;
		goto fail4;
	}

	/* Check partition header matches partn */
	if (__LE_TO_CPU_16(header->type_id) != partn) {
		rc = EINVAL;
		goto fail5;
	}

	/* Check partition ends with PARTITION_TRAILER and END tags */
	if ((rc = tlv_find(&cursor, TLV_TAG_PARTITION_TRAILER)) != 0) {
		rc = EINVAL;
		goto fail6;
	}
	trailer = (struct tlv_partition_trailer *)tlv_item(&cursor);

	if ((rc = tlv_advance(&cursor)) != 0) {
		rc = EINVAL;
		goto fail7;
	}
	if (tlv_tag(&cursor) != TLV_TAG_END) {
		rc = EINVAL;
		goto fail8;
	}

	/* Check generation counts are consistent */
	if (trailer->generation != header->generation) {
		rc = EINVAL;
		goto fail9;
	}

	/* Verify partition checksum */
	cksum = 0;
	for (pos = 0; (size_t)pos < total_length; pos += sizeof (uint32_t)) {
		cksum += *((uint32_t *)(partn_data + pos));
	}
	if (cksum != 0) {
		rc = EINVAL;
		goto fail10;
	}

	return (0);

fail10:
	EFSYS_PROBE(fail10);
fail9:
	EFSYS_PROBE(fail9);
fail8:
	EFSYS_PROBE(fail8);
fail7:
	EFSYS_PROBE(fail7);
fail6:
	EFSYS_PROBE(fail6);
fail5:
	EFSYS_PROBE(fail5);
fail4:
	EFSYS_PROBE(fail4);
fail3:
	EFSYS_PROBE(fail3);
fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

			void
ef10_nvram_buffer_init(
	__out_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size)
{
	uint32_t *buf = (uint32_t *)bufferp;

	memset(buf, 0xff, buffer_size);

	tlv_init_block(buf);
}

	__checkReturn		efx_rc_t
ef10_nvram_buffer_create(
	__in			uint32_t partn_type,
	__out_bcount(partn_size)
				caddr_t partn_data,
	__in			size_t partn_size)
{
	uint32_t *buf = (uint32_t *)partn_data;
	efx_rc_t rc;
	tlv_cursor_t cursor;
	struct tlv_partition_header header;
	struct tlv_partition_trailer trailer;

	unsigned int min_buf_size = sizeof (struct tlv_partition_header) +
	    sizeof (struct tlv_partition_trailer);
	if (partn_size < min_buf_size) {
		rc = EINVAL;
		goto fail1;
	}

	ef10_nvram_buffer_init(partn_data, partn_size);

	if ((rc = tlv_init_cursor(&cursor, buf,
	    (uint32_t *)((uint8_t *)buf + partn_size),
	    buf)) != 0) {
		goto fail2;
	}

	header.tag = __CPU_TO_LE_32(TLV_TAG_PARTITION_HEADER);
	header.length = __CPU_TO_LE_32(sizeof (header) - 8);
	header.type_id = __CPU_TO_LE_16(partn_type);
	header.preset = 0;
	header.generation = __CPU_TO_LE_32(1);
	header.total_length = 0;  /* This will be fixed below. */
	if ((rc = tlv_insert(
	    &cursor, TLV_TAG_PARTITION_HEADER,
	    (uint8_t *)&header.type_id, sizeof (header) - 8)) != 0)
		goto fail3;
	if ((rc = tlv_advance(&cursor)) != 0)
		goto fail4;

	trailer.tag = __CPU_TO_LE_32(TLV_TAG_PARTITION_TRAILER);
	trailer.length = __CPU_TO_LE_32(sizeof (trailer) - 8);
	trailer.generation = header.generation;
	trailer.checksum = 0;  /* This will be fixed below. */
	if ((rc = tlv_insert(&cursor, TLV_TAG_PARTITION_TRAILER,
	    (uint8_t *)&trailer.generation, sizeof (trailer) - 8)) != 0)
		goto fail5;

	if ((rc = tlv_update_partition_len_and_cks(&cursor)) != 0)
		goto fail6;

	/* Check that the partition is valid. */
	if ((rc = ef10_nvram_buffer_validate(partn_type,
	    partn_data, partn_size)) != 0)
		goto fail7;

	return (0);

fail7:
	EFSYS_PROBE(fail7);
fail6:
	EFSYS_PROBE(fail6);
fail5:
	EFSYS_PROBE(fail5);
fail4:
	EFSYS_PROBE(fail4);
fail3:
	EFSYS_PROBE(fail3);
fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

static			uint32_t
byte_offset(
	__in		uint32_t *position,
	__in		uint32_t *base)
{
	return (uint32_t)((uint8_t *)position - (uint8_t *)base);
}

	__checkReturn		efx_rc_t
ef10_nvram_buffer_find_item_start(
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size,
	__out			uint32_t *startp)
{
	/* Read past partition header to find start address of the first key */
	tlv_cursor_t cursor;
	efx_rc_t rc;

	/* A PARTITION_HEADER tag must be the first item (at offset zero) */
	if ((rc = tlv_init_cursor_from_size(&cursor, (uint8_t *)bufferp,
			buffer_size)) != 0) {
		rc = EFAULT;
		goto fail1;
	}
	if (tlv_tag(&cursor) != TLV_TAG_PARTITION_HEADER) {
		rc = EINVAL;
		goto fail2;
	}

	if ((rc = tlv_advance(&cursor)) != 0) {
		rc = EINVAL;
		goto fail3;
	}
	*startp = byte_offset(cursor.current, cursor.block);

	if ((rc = tlv_require_end(&cursor)) != 0)
		goto fail4;

	return (0);

fail4:
	EFSYS_PROBE(fail4);
fail3:
	EFSYS_PROBE(fail3);
fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn		efx_rc_t
ef10_nvram_buffer_find_end(
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size,
	__in			uint32_t offset,
	__out			uint32_t *endp)
{
	/* Read to end of partition */
	tlv_cursor_t cursor;
	efx_rc_t rc;
	uint32_t *segment_used;

	_NOTE(ARGUNUSED(offset))

	if ((rc = tlv_init_cursor_from_size(&cursor, (uint8_t *)bufferp,
			buffer_size)) != 0) {
		rc = EFAULT;
		goto fail1;
	}

	segment_used = cursor.block;

	/*
	 * Go through each segment and check that it has an end tag. If there
	 * is no end tag then the previous segment was the last valid one,
	 * so return the used space including that end tag.
	 */
	while (tlv_tag(&cursor) == TLV_TAG_PARTITION_HEADER) {
		if (tlv_require_end(&cursor) != 0) {
			if (segment_used == cursor.block) {
				/*
				 * First segment is corrupt, so there is
				 * no valid data in partition.
				 */
				rc = EINVAL;
				goto fail2;
			}
			break;
		}
		segment_used = cursor.end + 1;

		cursor.current = segment_used;
	}
	/* Return space used (including the END tag) */
	*endp = (segment_used - cursor.block) * sizeof (uint32_t);

	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn	__success(return != B_FALSE)	boolean_t
ef10_nvram_buffer_find_item(
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size,
	__in			uint32_t offset,
	__out			uint32_t *startp,
	__out			uint32_t *lengthp)
{
	/* Find TLV at offset and return key start and length */
	tlv_cursor_t cursor;
	uint8_t *key;
	uint32_t tag;

	if (tlv_init_cursor_at_offset(&cursor, (uint8_t *)bufferp,
			buffer_size, offset) != 0) {
		return (B_FALSE);
	}

	while ((key = tlv_item(&cursor)) != NULL) {
		tag = tlv_tag(&cursor);
		if (tag == TLV_TAG_PARTITION_HEADER ||
		    tag == TLV_TAG_PARTITION_TRAILER) {
			if (tlv_advance(&cursor) != 0) {
				break;
			}
			continue;
		}
		*startp = byte_offset(cursor.current, cursor.block);
		*lengthp = byte_offset(tlv_next_item_ptr(&cursor),
		    cursor.current);
		return (B_TRUE);
	}

	return (B_FALSE);
}

	__checkReturn		efx_rc_t
ef10_nvram_buffer_peek_item(
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size,
	__in			uint32_t offset,
	__out			uint32_t *tagp,
	__out			uint32_t *lengthp,
	__out			uint32_t *value_offsetp)
{
	efx_rc_t rc;
	tlv_cursor_t cursor;
	uint32_t tag;

	if ((rc = tlv_init_cursor_at_offset(&cursor, (uint8_t *)bufferp,
			buffer_size, offset)) != 0) {
		goto fail1;
	}

	tag = tlv_tag(&cursor);
	*tagp = tag;
	if (tag == TLV_TAG_END) {
		/*
		 * To allow stepping over the END tag, report the full tag
		 * length and a zero length value.
		 */
		*lengthp = sizeof (tag);
		*value_offsetp = sizeof (tag);
	} else {
		*lengthp = byte_offset(tlv_next_item_ptr(&cursor),
			    cursor.current);
		*value_offsetp = byte_offset((uint32_t *)tlv_value(&cursor),
			    cursor.current);
	}
	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn		efx_rc_t
ef10_nvram_buffer_get_item(
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size,
	__in			uint32_t offset,
	__in			uint32_t length,
	__out			uint32_t *tagp,
	__out_bcount_part(value_max_size, *lengthp)
				caddr_t valuep,
	__in			size_t value_max_size,
	__out			uint32_t *lengthp)
{
	efx_rc_t rc;
	tlv_cursor_t cursor;
	uint32_t value_length;

	if (buffer_size < (offset + length)) {
		rc = ENOSPC;
		goto fail1;
	}

	if ((rc = tlv_init_cursor_at_offset(&cursor, (uint8_t *)bufferp,
			buffer_size, offset)) != 0) {
		goto fail2;
	}

	value_length = tlv_length(&cursor);
	if (value_max_size < value_length) {
		rc = ENOSPC;
		goto fail3;
	}
	memcpy(valuep, tlv_value(&cursor), value_length);

	*tagp = tlv_tag(&cursor);
	*lengthp = value_length;

	return (0);

fail3:
	EFSYS_PROBE(fail3);
fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn		efx_rc_t
ef10_nvram_buffer_insert_item(
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size,
	__in			uint32_t offset,
	__in			uint32_t tag,
	__in_bcount(length)	caddr_t valuep,
	__in			uint32_t length,
	__out			uint32_t *lengthp)
{
	efx_rc_t rc;
	tlv_cursor_t cursor;

	if ((rc = tlv_init_cursor_at_offset(&cursor, (uint8_t *)bufferp,
			buffer_size, offset)) != 0) {
		goto fail1;
	}

	rc = tlv_insert(&cursor, tag, (uint8_t *)valuep, length);

	if (rc != 0)
		goto fail2;

	*lengthp = byte_offset(tlv_next_item_ptr(&cursor),
		    cursor.current);

	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn		efx_rc_t
ef10_nvram_buffer_modify_item(
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size,
	__in			uint32_t offset,
	__in			uint32_t tag,
	__in_bcount(length)	caddr_t valuep,
	__in			uint32_t length,
	__out			uint32_t *lengthp)
{
	efx_rc_t rc;
	tlv_cursor_t cursor;

	if ((rc = tlv_init_cursor_at_offset(&cursor, (uint8_t *)bufferp,
			buffer_size, offset)) != 0) {
		goto fail1;
	}

	rc = tlv_modify(&cursor, tag, (uint8_t *)valuep, length);

	if (rc != 0) {
		goto fail2;
	}

	*lengthp = byte_offset(tlv_next_item_ptr(&cursor),
		    cursor.current);

	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}


	__checkReturn		efx_rc_t
ef10_nvram_buffer_delete_item(
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size,
	__in			uint32_t offset,
	__in			uint32_t length,
	__in			uint32_t end)
{
	efx_rc_t rc;
	tlv_cursor_t cursor;

	_NOTE(ARGUNUSED(length, end))

	if ((rc = tlv_init_cursor_at_offset(&cursor, (uint8_t *)bufferp,
			buffer_size, offset)) != 0) {
		goto fail1;
	}

	if ((rc = tlv_delete(&cursor)) != 0)
		goto fail2;

	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn		efx_rc_t
ef10_nvram_buffer_finish(
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size)
{
	efx_rc_t rc;
	tlv_cursor_t cursor;

	if ((rc = tlv_init_cursor_from_size(&cursor, (uint8_t *)bufferp,
			buffer_size)) != 0) {
		rc = EFAULT;
		goto fail1;
	}

	if ((rc = tlv_require_end(&cursor)) != 0)
		goto fail2;

	if ((rc = tlv_update_partition_len_and_cks(&cursor)) != 0)
		goto fail3;

	return (0);

fail3:
	EFSYS_PROBE(fail3);
fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}



/*
 * Read and validate a segment from a partition. A segment is a complete
 * tlv chain between PARTITION_HEADER and PARTITION_END tags. There may
 * be multiple segments in a partition, so seg_offset allows segments
 * beyond the first to be read.
 */
static	__checkReturn			efx_rc_t
ef10_nvram_read_tlv_segment(
	__in				efx_nic_t *enp,
	__in				uint32_t partn,
	__in				size_t seg_offset,
	__in_bcount(max_seg_size)	caddr_t seg_data,
	__in				size_t max_seg_size)
{
	tlv_cursor_t cursor;
	struct tlv_partition_header *header;
	struct tlv_partition_trailer *trailer;
	size_t total_length;
	uint32_t cksum;
	int pos;
	efx_rc_t rc;

	EFX_STATIC_ASSERT(sizeof (*header) <= EF10_NVRAM_CHUNK);

	if ((seg_data == NULL) || (max_seg_size == 0)) {
		rc = EINVAL;
		goto fail1;
	}

	/* Read initial chunk of the segment, starting at offset */
	if ((rc = ef10_nvram_partn_read_mode(enp, partn, seg_offset, seg_data,
		    EF10_NVRAM_CHUNK,
		    MC_CMD_NVRAM_READ_IN_V2_TARGET_CURRENT)) != 0) {
		goto fail2;
	}

	/* A PARTITION_HEADER tag must be the first item at the given offset */
	if ((rc = tlv_init_cursor_from_size(&cursor, (uint8_t *)seg_data,
		    max_seg_size)) != 0) {
		rc = EFAULT;
		goto fail3;
	}
	if (tlv_tag(&cursor) != TLV_TAG_PARTITION_HEADER) {
		rc = EINVAL;
		goto fail4;
	}
	header = (struct tlv_partition_header *)tlv_item(&cursor);

	/* Check TLV segment length (includes the END tag) */
	total_length = __LE_TO_CPU_32(header->total_length);
	if (total_length > max_seg_size) {
		rc = EFBIG;
		goto fail5;
	}

	/* Read the remaining segment content */
	if (total_length > EF10_NVRAM_CHUNK) {
		if ((rc = ef10_nvram_partn_read_mode(enp, partn,
			    seg_offset + EF10_NVRAM_CHUNK,
			    seg_data + EF10_NVRAM_CHUNK,
			    total_length - EF10_NVRAM_CHUNK,
			    MC_CMD_NVRAM_READ_IN_V2_TARGET_CURRENT)) != 0)
			goto fail6;
	}

	/* Check segment ends with PARTITION_TRAILER and END tags */
	if ((rc = tlv_find(&cursor, TLV_TAG_PARTITION_TRAILER)) != 0) {
		rc = EINVAL;
		goto fail7;
	}
	trailer = (struct tlv_partition_trailer *)tlv_item(&cursor);

	if ((rc = tlv_advance(&cursor)) != 0) {
		rc = EINVAL;
		goto fail8;
	}
	if (tlv_tag(&cursor) != TLV_TAG_END) {
		rc = EINVAL;
		goto fail9;
	}

	/* Check data read from segment is consistent */
	if (trailer->generation != header->generation) {
		/*
		 * The partition data may have been modified between successive
		 * MCDI NVRAM_READ requests by the MC or another PCI function.
		 *
		 * The caller must retry to obtain consistent partition data.
		 */
		rc = EAGAIN;
		goto fail10;
	}

	/* Verify segment checksum */
	cksum = 0;
	for (pos = 0; (size_t)pos < total_length; pos += sizeof (uint32_t)) {
		cksum += *((uint32_t *)(seg_data + pos));
	}
	if (cksum != 0) {
		rc = EINVAL;
		goto fail11;
	}

	return (0);

fail11:
	EFSYS_PROBE(fail11);
fail10:
	EFSYS_PROBE(fail10);
fail9:
	EFSYS_PROBE(fail9);
fail8:
	EFSYS_PROBE(fail8);
fail7:
	EFSYS_PROBE(fail7);
fail6:
	EFSYS_PROBE(fail6);
fail5:
	EFSYS_PROBE(fail5);
fail4:
	EFSYS_PROBE(fail4);
fail3:
	EFSYS_PROBE(fail3);
fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

/*
 * Read a single TLV item from a host memory
 * buffer containing a TLV formatted segment.
 */
	__checkReturn		efx_rc_t
ef10_nvram_buf_read_tlv(
	__in				efx_nic_t *enp,
	__in_bcount(max_seg_size)	caddr_t seg_data,
	__in				size_t max_seg_size,
	__in				uint32_t tag,
	__deref_out_bcount_opt(*sizep)	caddr_t *datap,
	__out				size_t *sizep)
{
	tlv_cursor_t cursor;
	caddr_t data;
	size_t length;
	caddr_t value;
	efx_rc_t rc;

	_NOTE(ARGUNUSED(enp))

	if ((seg_data == NULL) || (max_seg_size == 0)) {
		rc = EINVAL;
		goto fail1;
	}

	/* Find requested TLV tag in segment data */
	if ((rc = tlv_init_cursor_from_size(&cursor, (uint8_t *)seg_data,
		    max_seg_size)) != 0) {
		rc = EFAULT;
		goto fail2;
	}
	if ((rc = tlv_find(&cursor, tag)) != 0) {
		rc = ENOENT;
		goto fail3;
	}
	value = (caddr_t)tlv_value(&cursor);
	length = tlv_length(&cursor);

	if (length == 0)
		data = NULL;
	else {
		/* Copy out data from TLV item */
		EFSYS_KMEM_ALLOC(enp->en_esip, length, data);
		if (data == NULL) {
			rc = ENOMEM;
			goto fail4;
		}
		memcpy(data, value, length);
	}

	*datap = data;
	*sizep = length;

	return (0);

fail4:
	EFSYS_PROBE(fail4);
fail3:
	EFSYS_PROBE(fail3);
fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

/* Read a single TLV item from the first segment in a TLV formatted partition */
	__checkReturn		efx_rc_t
ef10_nvram_partn_read_tlv(
	__in					efx_nic_t *enp,
	__in					uint32_t partn,
	__in					uint32_t tag,
	__deref_out_bcount_opt(*seg_sizep)	caddr_t *seg_datap,
	__out					size_t *seg_sizep)
{
	caddr_t seg_data = NULL;
	size_t partn_size = 0;
	size_t length;
	caddr_t data;
	int retry;
	efx_rc_t rc;

	/* Allocate sufficient memory for the entire partition */
	if ((rc = ef10_nvram_partn_size(enp, partn, &partn_size)) != 0)
		goto fail1;

	if (partn_size == 0) {
		rc = ENOENT;
		goto fail2;
	}

	EFSYS_KMEM_ALLOC(enp->en_esip, partn_size, seg_data);
	if (seg_data == NULL) {
		rc = ENOMEM;
		goto fail3;
	}

	/*
	 * Read the first segment in a TLV partition. Retry until consistent
	 * segment contents are returned. Inconsistent data may be read if:
	 *  a) the segment contents are invalid
	 *  b) the MC has rebooted while we were reading the partition
	 *  c) the partition has been modified while we were reading it
	 * Limit retry attempts to ensure forward progress.
	 */
	retry = 10;
	do {
		if ((rc = ef10_nvram_read_tlv_segment(enp, partn, 0,
		    seg_data, partn_size)) != 0)
			--retry;
	} while ((rc == EAGAIN) && (retry > 0));

	if (rc != 0) {
		/* Failed to obtain consistent segment data */
		if (rc == EAGAIN)
			rc = EIO;

		goto fail4;
	}

	if ((rc = ef10_nvram_buf_read_tlv(enp, seg_data, partn_size,
		    tag, &data, &length)) != 0)
		goto fail5;

	EFSYS_KMEM_FREE(enp->en_esip, partn_size, seg_data);

	*seg_datap = data;
	*seg_sizep = length;

	return (0);

fail5:
	EFSYS_PROBE(fail5);
fail4:
	EFSYS_PROBE(fail4);

	EFSYS_KMEM_FREE(enp->en_esip, partn_size, seg_data);
fail3:
	EFSYS_PROBE(fail3);
fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

/* Compute the size of a segment. */
	static	__checkReturn	efx_rc_t
ef10_nvram_buf_segment_size(
	__in			caddr_t seg_data,
	__in			size_t max_seg_size,
	__out			size_t *seg_sizep)
{
	efx_rc_t rc;
	tlv_cursor_t cursor;
	struct tlv_partition_header *header;
	uint32_t cksum;
	int pos;
	uint32_t *end_tag_position;
	uint32_t segment_length;

	/* A PARTITION_HEADER tag must be the first item at the given offset */
	if ((rc = tlv_init_cursor_from_size(&cursor, (uint8_t *)seg_data,
		    max_seg_size)) != 0) {
		rc = EFAULT;
		goto fail1;
	}
	if (tlv_tag(&cursor) != TLV_TAG_PARTITION_HEADER) {
		rc = EINVAL;
		goto fail2;
	}
	header = (struct tlv_partition_header *)tlv_item(&cursor);

	/* Check TLV segment length (includes the END tag) */
	*seg_sizep = __LE_TO_CPU_32(header->total_length);
	if (*seg_sizep > max_seg_size) {
		rc = EFBIG;
		goto fail3;
	}

	/* Check segment ends with PARTITION_TRAILER and END tags */
	if ((rc = tlv_find(&cursor, TLV_TAG_PARTITION_TRAILER)) != 0) {
		rc = EINVAL;
		goto fail4;
	}

	if ((rc = tlv_advance(&cursor)) != 0) {
		rc = EINVAL;
		goto fail5;
	}
	if (tlv_tag(&cursor) != TLV_TAG_END) {
		rc = EINVAL;
		goto fail6;
	}
	end_tag_position = cursor.current;

	/* Verify segment checksum */
	cksum = 0;
	for (pos = 0; (size_t)pos < *seg_sizep; pos += sizeof (uint32_t)) {
		cksum += *((uint32_t *)(seg_data + pos));
	}
	if (cksum != 0) {
		rc = EINVAL;
		goto fail7;
	}

	/*
	 * Calculate total length from HEADER to END tags and compare to
	 * max_seg_size and the total_length field in the HEADER tag.
	 */
	segment_length = tlv_block_length_used(&cursor);

	if (segment_length > max_seg_size) {
		rc = EINVAL;
		goto fail8;
	}

	if (segment_length != *seg_sizep) {
		rc = EINVAL;
		goto fail9;
	}

	/* Skip over the first HEADER tag. */
	rc = tlv_rewind(&cursor);
	rc = tlv_advance(&cursor);

	while (rc == 0) {
		if (tlv_tag(&cursor) == TLV_TAG_END) {
			/* Check that the END tag is the one found earlier. */
			if (cursor.current != end_tag_position)
				goto fail10;
			break;
		}
		/* Check for duplicate HEADER tags before the END tag. */
		if (tlv_tag(&cursor) == TLV_TAG_PARTITION_HEADER) {
			rc = EINVAL;
			goto fail11;
		}

		rc = tlv_advance(&cursor);
	}
	if (rc != 0)
		goto fail12;

	return (0);

fail12:
	EFSYS_PROBE(fail12);
fail11:
	EFSYS_PROBE(fail11);
fail10:
	EFSYS_PROBE(fail10);
fail9:
	EFSYS_PROBE(fail9);
fail8:
	EFSYS_PROBE(fail8);
fail7:
	EFSYS_PROBE(fail7);
fail6:
	EFSYS_PROBE(fail6);
fail5:
	EFSYS_PROBE(fail5);
fail4:
	EFSYS_PROBE(fail4);
fail3:
	EFSYS_PROBE(fail3);
fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

/*
 * Add or update a single TLV item in a host memory buffer containing a TLV
 * formatted segment. Historically partitions consisted of only one segment.
 */
	__checkReturn			efx_rc_t
ef10_nvram_buf_write_tlv(
	__inout_bcount(max_seg_size)	caddr_t seg_data,
	__in				size_t max_seg_size,
	__in				uint32_t tag,
	__in_bcount(tag_size)		caddr_t tag_data,
	__in				size_t tag_size,
	__out				size_t *total_lengthp)
{
	tlv_cursor_t cursor;
	struct tlv_partition_header *header;
	struct tlv_partition_trailer *trailer;
	uint32_t generation;
	uint32_t cksum;
	int pos;
	efx_rc_t rc;

	/* A PARTITION_HEADER tag must be the first item (at offset zero) */
	if ((rc = tlv_init_cursor_from_size(&cursor, (uint8_t *)seg_data,
			max_seg_size)) != 0) {
		rc = EFAULT;
		goto fail1;
	}
	if (tlv_tag(&cursor) != TLV_TAG_PARTITION_HEADER) {
		rc = EINVAL;
		goto fail2;
	}
	header = (struct tlv_partition_header *)tlv_item(&cursor);

	/* Update the TLV chain to contain the new data */
	if ((rc = tlv_find(&cursor, tag)) == 0) {
		/* Modify existing TLV item */
		if ((rc = tlv_modify(&cursor, tag,
			    (uint8_t *)tag_data, tag_size)) != 0)
			goto fail3;
	} else {
		/* Insert a new TLV item before the PARTITION_TRAILER */
		rc = tlv_find(&cursor, TLV_TAG_PARTITION_TRAILER);
		if (rc != 0) {
			rc = EINVAL;
			goto fail4;
		}
		if ((rc = tlv_insert(&cursor, tag,
			    (uint8_t *)tag_data, tag_size)) != 0) {
			rc = EINVAL;
			goto fail5;
		}
	}

	/* Find the trailer tag */
	if ((rc = tlv_find(&cursor, TLV_TAG_PARTITION_TRAILER)) != 0) {
		rc = EINVAL;
		goto fail6;
	}
	trailer = (struct tlv_partition_trailer *)tlv_item(&cursor);

	/* Update PARTITION_HEADER and PARTITION_TRAILER fields */
	*total_lengthp = tlv_block_length_used(&cursor);
	if (*total_lengthp > max_seg_size) {
		rc = ENOSPC;
		goto fail7;
	}
	generation = __LE_TO_CPU_32(header->generation) + 1;

	header->total_length	= __CPU_TO_LE_32(*total_lengthp);
	header->generation	= __CPU_TO_LE_32(generation);
	trailer->generation	= __CPU_TO_LE_32(generation);

	/* Recompute PARTITION_TRAILER checksum */
	trailer->checksum = 0;
	cksum = 0;
	for (pos = 0; (size_t)pos < *total_lengthp; pos += sizeof (uint32_t)) {
		cksum += *((uint32_t *)(seg_data + pos));
	}
	trailer->checksum = ~cksum + 1;

	return (0);

fail7:
	EFSYS_PROBE(fail7);
fail6:
	EFSYS_PROBE(fail6);
fail5:
	EFSYS_PROBE(fail5);
fail4:
	EFSYS_PROBE(fail4);
fail3:
	EFSYS_PROBE(fail3);
fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

/*
 * Add or update a single TLV item in the first segment of a TLV formatted
 * dynamic config partition. The first segment is the current active
 * configuration.
 */
	__checkReturn		efx_rc_t
ef10_nvram_partn_write_tlv(
	__in			efx_nic_t *enp,
	__in			uint32_t partn,
	__in			uint32_t tag,
	__in_bcount(size)	caddr_t data,
	__in			size_t size)
{
	return ef10_nvram_partn_write_segment_tlv(enp, partn, tag, data,
	    size, B_FALSE);
}

/*
 * Read a segment from nvram at the given offset into a buffer (segment_data)
 * and optionally write a new tag to it.
 */
static	__checkReturn		efx_rc_t
ef10_nvram_segment_write_tlv(
	__in			efx_nic_t *enp,
	__in			uint32_t partn,
	__in			uint32_t tag,
	__in_bcount(size)	caddr_t data,
	__in			size_t size,
	__inout			caddr_t *seg_datap,
	__inout			size_t *partn_offsetp,
	__inout			size_t *src_remain_lenp,
	__inout			size_t *dest_remain_lenp,
	__in			boolean_t write)
{
	efx_rc_t rc;
	efx_rc_t status;
	size_t original_segment_size;
	size_t modified_segment_size;

	/*
	 * Read the segment from NVRAM into the segment_data buffer and validate
	 * it, returning if it does not validate. This is not a failure unless
	 * this is the first segment in a partition. In this case the caller
	 * must propagate the error.
	 */
	status = ef10_nvram_read_tlv_segment(enp, partn, *partn_offsetp,
	    *seg_datap, *src_remain_lenp);
	if (status != 0) {
		rc = EINVAL;
		goto fail1;
	}

	status = ef10_nvram_buf_segment_size(*seg_datap,
	    *src_remain_lenp, &original_segment_size);
	if (status != 0) {
		rc = EINVAL;
		goto fail2;
	}

	if (write) {
		/* Update the contents of the segment in the buffer */
		if ((rc = ef10_nvram_buf_write_tlv(*seg_datap,
			*dest_remain_lenp, tag, data, size,
			&modified_segment_size)) != 0) {
			goto fail3;
		}
		*dest_remain_lenp -= modified_segment_size;
		*seg_datap += modified_segment_size;
	} else {
		/*
		 * We won't modify this segment, but still need to update the
		 * remaining lengths and pointers.
		 */
		*dest_remain_lenp -= original_segment_size;
		*seg_datap += original_segment_size;
	}

	*partn_offsetp += original_segment_size;
	*src_remain_lenp -= original_segment_size;

	return (0);

fail3:
	EFSYS_PROBE(fail3);
fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

/*
 * Add or update a single TLV item in either the first segment or in all
 * segments in a TLV formatted dynamic config partition. Dynamic config
 * partitions on boards that support RFID are divided into a number of segments,
 * each formatted like a partition, with header, trailer and end tags. The first
 * segment is the current active configuration.
 *
 * The segments are initialised by manftest and each contain a different
 * configuration e.g. firmware variant. The firmware can be instructed
 * via RFID to copy a segment to replace the first segment, hence changing the
 * active configuration.  This allows ops to change the configuration of a board
 * prior to shipment using RFID.
 *
 * Changes to the dynamic config may need to be written to all segments (e.g.
 * firmware versions) or just the first segment (changes to the active
 * configuration). See SF-111324-SW "The use of RFID in Solarflare Products".
 * If only the first segment is written the code still needs to be aware of the
 * possible presence of subsequent segments as writing to a segment may cause
 * its size to increase, which would overwrite the subsequent segments and
 * invalidate them.
 */
	__checkReturn		efx_rc_t
ef10_nvram_partn_write_segment_tlv(
	__in			efx_nic_t *enp,
	__in			uint32_t partn,
	__in			uint32_t tag,
	__in_bcount(size)	caddr_t data,
	__in			size_t size,
	__in			boolean_t all_segments)
{
	size_t partn_size = 0;
	caddr_t partn_data;
	size_t total_length = 0;
	efx_rc_t rc;
	size_t current_offset = 0;
	size_t remaining_original_length;
	size_t remaining_modified_length;
	caddr_t segment_data;

	EFSYS_ASSERT3U(partn, ==, NVRAM_PARTITION_TYPE_DYNAMIC_CONFIG);

	/* Allocate sufficient memory for the entire partition */
	if ((rc = ef10_nvram_partn_size(enp, partn, &partn_size)) != 0)
		goto fail1;

	EFSYS_KMEM_ALLOC(enp->en_esip, partn_size, partn_data);
	if (partn_data == NULL) {
		rc = ENOMEM;
		goto fail2;
	}

	remaining_original_length = partn_size;
	remaining_modified_length = partn_size;
	segment_data = partn_data;

	/* Lock the partition */
	if ((rc = ef10_nvram_partn_lock(enp, partn)) != 0)
		goto fail3;

	/* Iterate over each (potential) segment to update it. */
	do {
		boolean_t write = all_segments || current_offset == 0;

		rc = ef10_nvram_segment_write_tlv(enp, partn, tag, data, size,
		    &segment_data, &current_offset, &remaining_original_length,
		    &remaining_modified_length, write);
		if (rc != 0) {
			if (current_offset == 0) {
				/*
				 * If no data has been read then the first
				 * segment is invalid, which is an error.
				 */
				goto fail4;
			}
			break;
		}
	} while (current_offset < partn_size);

	total_length = segment_data - partn_data;

	/*
	 * We've run out of space.  This should actually be dealt with by
	 * ef10_nvram_buf_write_tlv returning ENOSPC.
	 */
	if (total_length > partn_size) {
		rc = ENOSPC;
		goto fail5;
	}

	/* Erase the whole partition in NVRAM */
	if ((rc = ef10_nvram_partn_erase(enp, partn, 0, partn_size)) != 0)
		goto fail6;

	/* Write new partition contents from the buffer to NVRAM */
	if ((rc = ef10_nvram_partn_write(enp, partn, 0, partn_data,
		    total_length)) != 0)
		goto fail7;

	/* Unlock the partition */
	(void) ef10_nvram_partn_unlock(enp, partn, NULL);

	EFSYS_KMEM_FREE(enp->en_esip, partn_size, partn_data);

	return (0);

fail7:
	EFSYS_PROBE(fail7);
fail6:
	EFSYS_PROBE(fail6);
fail5:
	EFSYS_PROBE(fail5);
fail4:
	EFSYS_PROBE(fail4);

	(void) ef10_nvram_partn_unlock(enp, partn, NULL);
fail3:
	EFSYS_PROBE(fail3);

	EFSYS_KMEM_FREE(enp->en_esip, partn_size, partn_data);
fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

/*
 * Get the size of a NVRAM partition. This is the total size allocated in nvram,
 * not the data used by the segments in the partition.
 */
	__checkReturn		efx_rc_t
ef10_nvram_partn_size(
	__in			efx_nic_t *enp,
	__in			uint32_t partn,
	__out			size_t *sizep)
{
	efx_rc_t rc;

	if ((rc = efx_mcdi_nvram_info(enp, partn, sizep,
	    NULL, NULL, NULL)) != 0)
		goto fail1;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn		efx_rc_t
ef10_nvram_partn_lock(
	__in			efx_nic_t *enp,
	__in			uint32_t partn)
{
	efx_rc_t rc;

	if ((rc = efx_mcdi_nvram_update_start(enp, partn)) != 0)
		goto fail1;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn		efx_rc_t
ef10_nvram_partn_read_mode(
	__in			efx_nic_t *enp,
	__in			uint32_t partn,
	__in			unsigned int offset,
	__out_bcount(size)	caddr_t data,
	__in			size_t size,
	__in			uint32_t mode)
{
	size_t chunk;
	efx_rc_t rc;

	while (size > 0) {
		chunk = MIN(size, EF10_NVRAM_CHUNK);

		if ((rc = efx_mcdi_nvram_read(enp, partn, offset,
			    data, chunk, mode)) != 0) {
			goto fail1;
		}

		size -= chunk;
		data += chunk;
		offset += chunk;
	}

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn		efx_rc_t
ef10_nvram_partn_read(
	__in			efx_nic_t *enp,
	__in			uint32_t partn,
	__in			unsigned int offset,
	__out_bcount(size)	caddr_t data,
	__in			size_t size)
{
	/*
	 * An A/B partition has two data stores (current and backup).
	 * Read requests which come in through the EFX API expect to read the
	 * current, active store of an A/B partition. For non A/B partitions,
	 * there is only a single store and so the mode param is ignored.
	 */
	return ef10_nvram_partn_read_mode(enp, partn, offset, data, size,
			    MC_CMD_NVRAM_READ_IN_V2_TARGET_CURRENT);
}

	__checkReturn		efx_rc_t
ef10_nvram_partn_read_backup(
	__in			efx_nic_t *enp,
	__in			uint32_t partn,
	__in			unsigned int offset,
	__out_bcount(size)	caddr_t data,
	__in			size_t size)
{
	/*
	 * An A/B partition has two data stores (current and backup).
	 * Read the backup store of an A/B partition (i.e. the store currently
	 * being written to if the partition is locked).
	 *
	 * This is needed when comparing the existing partition content to avoid
	 * unnecessary writes, or to read back what has been written to check
	 * that the writes have succeeded.
	 */
	return ef10_nvram_partn_read_mode(enp, partn, offset, data, size,
			    MC_CMD_NVRAM_READ_IN_V2_TARGET_BACKUP);
}

	__checkReturn		efx_rc_t
ef10_nvram_partn_erase(
	__in			efx_nic_t *enp,
	__in			uint32_t partn,
	__in			unsigned int offset,
	__in			size_t size)
{
	efx_rc_t rc;
	uint32_t erase_size;

	if ((rc = efx_mcdi_nvram_info(enp, partn, NULL, NULL,
	    &erase_size, NULL)) != 0)
		goto fail1;

	if (erase_size == 0) {
		if ((rc = efx_mcdi_nvram_erase(enp, partn, offset, size)) != 0)
			goto fail2;
	} else {
		if (size % erase_size != 0) {
			rc = EINVAL;
			goto fail3;
		}
		while (size > 0) {
			if ((rc = efx_mcdi_nvram_erase(enp, partn, offset,
			    erase_size)) != 0)
				goto fail4;
			offset += erase_size;
			size -= erase_size;
		}
	}

	return (0);

fail4:
	EFSYS_PROBE(fail4);
fail3:
	EFSYS_PROBE(fail3);
fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn		efx_rc_t
ef10_nvram_partn_write(
	__in			efx_nic_t *enp,
	__in			uint32_t partn,
	__in			unsigned int offset,
	__in_bcount(size)	caddr_t data,
	__in			size_t size)
{
	size_t chunk;
	uint32_t write_size;
	efx_rc_t rc;

	if ((rc = efx_mcdi_nvram_info(enp, partn, NULL, NULL,
	    NULL, &write_size)) != 0)
		goto fail1;

	if (write_size != 0) {
		/*
		 * Check that the size is a multiple of the write chunk size if
		 * the write chunk size is available.
		 */
		if (size % write_size != 0) {
			rc = EINVAL;
			goto fail2;
		}
	} else {
		write_size = EF10_NVRAM_CHUNK;
	}

	while (size > 0) {
		chunk = MIN(size, write_size);

		if ((rc = efx_mcdi_nvram_write(enp, partn, offset,
			    data, chunk)) != 0) {
			goto fail3;
		}

		size -= chunk;
		data += chunk;
		offset += chunk;
	}

	return (0);

fail3:
	EFSYS_PROBE(fail3);
fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn		efx_rc_t
ef10_nvram_partn_unlock(
	__in			efx_nic_t *enp,
	__in			uint32_t partn,
	__out_opt		uint32_t *verify_resultp)
{
	boolean_t reboot = B_FALSE;
	efx_rc_t rc;

	if (verify_resultp != NULL)
		*verify_resultp = MC_CMD_NVRAM_VERIFY_RC_UNKNOWN;

	rc = efx_mcdi_nvram_update_finish(enp, partn, reboot, verify_resultp);
	if (rc != 0)
		goto fail1;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn		efx_rc_t
ef10_nvram_partn_set_version(
	__in			efx_nic_t *enp,
	__in			uint32_t partn,
	__in_ecount(4)		uint16_t version[4])
{
	struct tlv_partition_version partn_version;
	size_t size;
	efx_rc_t rc;

	/* Add or modify partition version TLV item */
	partn_version.version_w = __CPU_TO_LE_16(version[0]);
	partn_version.version_x = __CPU_TO_LE_16(version[1]);
	partn_version.version_y = __CPU_TO_LE_16(version[2]);
	partn_version.version_z = __CPU_TO_LE_16(version[3]);

	size = sizeof (partn_version) - (2 * sizeof (uint32_t));

	/* Write the version number to all segments in the partition */
	if ((rc = ef10_nvram_partn_write_segment_tlv(enp,
		    NVRAM_PARTITION_TYPE_DYNAMIC_CONFIG,
		    TLV_TAG_PARTITION_VERSION(partn),
		    (caddr_t)&partn_version.version_w, size, B_TRUE)) != 0)
		goto fail1;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

#endif /* EFSYS_OPT_VPD || EFSYS_OPT_NVRAM */

#if EFSYS_OPT_NVRAM

typedef struct ef10_parttbl_entry_s {
	unsigned int		partn;
	unsigned int		port_mask;
	efx_nvram_type_t	nvtype;
} ef10_parttbl_entry_t;

/* Port mask values */
#define	PORT_1		(1u << 1)
#define	PORT_2		(1u << 2)
#define	PORT_3		(1u << 3)
#define	PORT_4		(1u << 4)
#define	PORT_ALL	(0xffffffffu)

#define	PARTN_MAP_ENTRY(partn, port_mask, nvtype)	\
{ (NVRAM_PARTITION_TYPE_##partn), (PORT_##port_mask), (EFX_NVRAM_##nvtype) }

/* Translate EFX NVRAM types to firmware partition types */
static ef10_parttbl_entry_t hunt_parttbl[] = {
	/*		partn			ports	nvtype */
	PARTN_MAP_ENTRY(MC_FIRMWARE,		ALL,	MC_FIRMWARE),
	PARTN_MAP_ENTRY(MC_FIRMWARE_BACKUP,	ALL,	MC_GOLDEN),
	PARTN_MAP_ENTRY(EXPANSION_ROM,		ALL,	BOOTROM),
	PARTN_MAP_ENTRY(EXPROM_CONFIG_PORT0,	1,	BOOTROM_CFG),
	PARTN_MAP_ENTRY(EXPROM_CONFIG_PORT1,	2,	BOOTROM_CFG),
	PARTN_MAP_ENTRY(EXPROM_CONFIG_PORT2,	3,	BOOTROM_CFG),
	PARTN_MAP_ENTRY(EXPROM_CONFIG_PORT3,	4,	BOOTROM_CFG),
	PARTN_MAP_ENTRY(DYNAMIC_CONFIG,		ALL,	DYNAMIC_CFG),
	PARTN_MAP_ENTRY(FPGA,			ALL,	FPGA),
	PARTN_MAP_ENTRY(FPGA_BACKUP,		ALL,	FPGA_BACKUP),
	PARTN_MAP_ENTRY(LICENSE,		ALL,	LICENSE),
};

static ef10_parttbl_entry_t medford_parttbl[] = {
	/*		partn			ports	nvtype */
	PARTN_MAP_ENTRY(MC_FIRMWARE,		ALL,	MC_FIRMWARE),
	PARTN_MAP_ENTRY(MC_FIRMWARE_BACKUP,	ALL,	MC_GOLDEN),
	PARTN_MAP_ENTRY(EXPANSION_ROM,		ALL,	BOOTROM),
	PARTN_MAP_ENTRY(EXPROM_CONFIG,		ALL,	BOOTROM_CFG),
	PARTN_MAP_ENTRY(DYNAMIC_CONFIG,		ALL,	DYNAMIC_CFG),
	PARTN_MAP_ENTRY(FPGA,			ALL,	FPGA),
	PARTN_MAP_ENTRY(FPGA_BACKUP,		ALL,	FPGA_BACKUP),
	PARTN_MAP_ENTRY(LICENSE,		ALL,	LICENSE),
	PARTN_MAP_ENTRY(EXPANSION_UEFI,		ALL,	UEFIROM),
	PARTN_MAP_ENTRY(MUM_FIRMWARE,		ALL,	MUM_FIRMWARE),
};

static ef10_parttbl_entry_t medford2_parttbl[] = {
	/*		partn			ports	nvtype */
	PARTN_MAP_ENTRY(MC_FIRMWARE,		ALL,	MC_FIRMWARE),
	PARTN_MAP_ENTRY(MC_FIRMWARE_BACKUP,	ALL,	MC_GOLDEN),
	PARTN_MAP_ENTRY(EXPANSION_ROM,		ALL,	BOOTROM),
	PARTN_MAP_ENTRY(EXPROM_CONFIG,		ALL,	BOOTROM_CFG),
	PARTN_MAP_ENTRY(DYNAMIC_CONFIG,		ALL,	DYNAMIC_CFG),
	PARTN_MAP_ENTRY(FPGA,			ALL,	FPGA),
	PARTN_MAP_ENTRY(FPGA_BACKUP,		ALL,	FPGA_BACKUP),
	PARTN_MAP_ENTRY(LICENSE,		ALL,	LICENSE),
	PARTN_MAP_ENTRY(EXPANSION_UEFI,		ALL,	UEFIROM),
	PARTN_MAP_ENTRY(MUM_FIRMWARE,		ALL,	MUM_FIRMWARE),
	PARTN_MAP_ENTRY(DYNCONFIG_DEFAULTS,	ALL,	DYNCONFIG_DEFAULTS),
	PARTN_MAP_ENTRY(ROMCONFIG_DEFAULTS,	ALL,	ROMCONFIG_DEFAULTS),
};

static	__checkReturn		efx_rc_t
ef10_parttbl_get(
	__in			efx_nic_t *enp,
	__out			ef10_parttbl_entry_t **parttblp,
	__out			size_t *parttbl_rowsp)
{
	switch (enp->en_family) {
	case EFX_FAMILY_HUNTINGTON:
		*parttblp = hunt_parttbl;
		*parttbl_rowsp = EFX_ARRAY_SIZE(hunt_parttbl);
		break;

	case EFX_FAMILY_MEDFORD:
		*parttblp = medford_parttbl;
		*parttbl_rowsp = EFX_ARRAY_SIZE(medford_parttbl);
		break;

	case EFX_FAMILY_MEDFORD2:
		*parttblp = medford2_parttbl;
		*parttbl_rowsp = EFX_ARRAY_SIZE(medford2_parttbl);
		break;

	default:
		EFSYS_ASSERT(B_FALSE);
		return (EINVAL);
	}
	return (0);
}

	__checkReturn		efx_rc_t
ef10_nvram_type_to_partn(
	__in			efx_nic_t *enp,
	__in			efx_nvram_type_t type,
	__out			uint32_t *partnp)
{
	efx_mcdi_iface_t *emip = &(enp->en_mcdi.em_emip);
	ef10_parttbl_entry_t *parttbl = NULL;
	size_t parttbl_rows = 0;
	unsigned int i;

	EFSYS_ASSERT3U(type, !=, EFX_NVRAM_INVALID);
	EFSYS_ASSERT3U(type, <, EFX_NVRAM_NTYPES);
	EFSYS_ASSERT(partnp != NULL);

	if (ef10_parttbl_get(enp, &parttbl, &parttbl_rows) == 0) {
		for (i = 0; i < parttbl_rows; i++) {
			ef10_parttbl_entry_t *entry = &parttbl[i];

			if ((entry->nvtype == type) &&
			    (entry->port_mask & (1u << emip->emi_port))) {
				*partnp = entry->partn;
				return (0);
			}
		}
	}

	return (ENOTSUP);
}

#if EFSYS_OPT_DIAG

static	__checkReturn		efx_rc_t
ef10_nvram_partn_to_type(
	__in			efx_nic_t *enp,
	__in			uint32_t partn,
	__out			efx_nvram_type_t *typep)
{
	efx_mcdi_iface_t *emip = &(enp->en_mcdi.em_emip);
	ef10_parttbl_entry_t *parttbl = NULL;
	size_t parttbl_rows = 0;
	unsigned int i;

	EFSYS_ASSERT(typep != NULL);

	if (ef10_parttbl_get(enp, &parttbl, &parttbl_rows) == 0) {
		for (i = 0; i < parttbl_rows; i++) {
			ef10_parttbl_entry_t *entry = &parttbl[i];

			if ((entry->partn == partn) &&
			    (entry->port_mask & (1u << emip->emi_port))) {
				*typep = entry->nvtype;
				return (0);
			}
		}
	}

	return (ENOTSUP);
}

	__checkReturn		efx_rc_t
ef10_nvram_test(
	__in			efx_nic_t *enp)
{
	efx_nvram_type_t type;
	unsigned int npartns = 0;
	uint32_t *partns = NULL;
	size_t size;
	unsigned int i;
	efx_rc_t rc;

	/* Read available partitions from NVRAM partition map */
	size = MC_CMD_NVRAM_PARTITIONS_OUT_TYPE_ID_MAXNUM * sizeof (uint32_t);
	EFSYS_KMEM_ALLOC(enp->en_esip, size, partns);
	if (partns == NULL) {
		rc = ENOMEM;
		goto fail1;
	}

	if ((rc = efx_mcdi_nvram_partitions(enp, (caddr_t)partns, size,
		    &npartns)) != 0) {
		goto fail2;
	}

	for (i = 0; i < npartns; i++) {
		/* Check if the partition is supported for this port */
		if ((rc = ef10_nvram_partn_to_type(enp, partns[i], &type)) != 0)
			continue;

		if ((rc = efx_mcdi_nvram_test(enp, partns[i])) != 0)
			goto fail3;
	}

	EFSYS_KMEM_FREE(enp->en_esip, size, partns);
	return (0);

fail3:
	EFSYS_PROBE(fail3);
fail2:
	EFSYS_PROBE(fail2);
	EFSYS_KMEM_FREE(enp->en_esip, size, partns);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);
	return (rc);
}

#endif	/* EFSYS_OPT_DIAG */

	__checkReturn		efx_rc_t
ef10_nvram_partn_get_version(
	__in			efx_nic_t *enp,
	__in			uint32_t partn,
	__out			uint32_t *subtypep,
	__out_ecount(4)		uint16_t version[4])
{
	efx_rc_t rc;

	/* FIXME: get highest partn version from all ports */
	/* FIXME: return partn description if available */

	if ((rc = efx_mcdi_nvram_metadata(enp, partn, subtypep,
		    version, NULL, 0)) != 0)
		goto fail1;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn		efx_rc_t
ef10_nvram_partn_rw_start(
	__in			efx_nic_t *enp,
	__in			uint32_t partn,
	__out			size_t *chunk_sizep)
{
	uint32_t write_size = 0;
	efx_rc_t rc;

	if ((rc = efx_mcdi_nvram_info(enp, partn, NULL, NULL,
	    NULL, &write_size)) != 0)
		goto fail1;

	if ((rc = ef10_nvram_partn_lock(enp, partn)) != 0)
		goto fail2;

	if (chunk_sizep != NULL) {
		if (write_size == 0)
			*chunk_sizep = EF10_NVRAM_CHUNK;
		else
			*chunk_sizep = write_size;
	}

	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn		efx_rc_t
ef10_nvram_partn_rw_finish(
	__in			efx_nic_t *enp,
	__in			uint32_t partn,
	__out_opt		uint32_t *verify_resultp)
{
	efx_rc_t rc;

	if ((rc = ef10_nvram_partn_unlock(enp, partn, verify_resultp)) != 0)
		goto fail1;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

#endif	/* EFSYS_OPT_NVRAM */

#endif	/* EFSYS_OPT_HUNTINGTON || EFSYS_OPT_MEDFORD || EFSYS_OPT_MEDFORD2 */
