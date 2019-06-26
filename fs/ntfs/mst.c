// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * mst.c - NTFS multi sector transfer protection handling code. Part of the
 *	   Linux-NTFS project.
 *
 * Copyright (c) 2001-2004 Anton Altaparmakov
 */

#include "ntfs.h"

/**
 * post_read_mst_fixup - deprotect multi sector transfer protected data
 * @b:		pointer to the data to deprotect
 * @size:	size in bytes of @b
 *
 * Perform the necessary post read multi sector transfer fixup and detect the
 * presence of incomplete multi sector transfers. - In that case, overwrite the
 * magic of the ntfs record header being processed with "BAAD" (in memory only!)
 * and abort processing.
 *
 * Return 0 on success and -EINVAL on error ("BAAD" magic will be present).
 *
 * NOTE: We consider the absence / invalidity of an update sequence array to
 * mean that the structure is not protected at all and hence doesn't need to
 * be fixed up. Thus, we return success and not failure in this case. This is
 * in contrast to pre_write_mst_fixup(), see below.
 */
int post_read_mst_fixup(NTFS_RECORD *b, const u32 size)
{
	u16 usa_ofs, usa_count, usn;
	u16 *usa_pos, *data_pos;

	/* Setup the variables. */
	usa_ofs = le16_to_cpu(b->usa_ofs);
	/* Decrement usa_count to get number of fixups. */
	usa_count = le16_to_cpu(b->usa_count) - 1;
	/* Size and alignment checks. */
	if ( size & (NTFS_BLOCK_SIZE - 1)	||
	     usa_ofs & 1			||
	     usa_ofs + (usa_count * 2) > size	||
	     (size >> NTFS_BLOCK_SIZE_BITS) != usa_count)
		return 0;
	/* Position of usn in update sequence array. */
	usa_pos = (u16*)b + usa_ofs/sizeof(u16);
	/*
	 * The update sequence number which has to be equal to each of the
	 * u16 values before they are fixed up. Note no need to care for
	 * endianness since we are comparing and moving data for on disk
	 * structures which means the data is consistent. - If it is
	 * consistenty the wrong endianness it doesn't make any difference.
	 */
	usn = *usa_pos;
	/*
	 * Position in protected data of first u16 that needs fixing up.
	 */
	data_pos = (u16*)b + NTFS_BLOCK_SIZE/sizeof(u16) - 1;
	/*
	 * Check for incomplete multi sector transfer(s).
	 */
	while (usa_count--) {
		if (*data_pos != usn) {
			/*
			 * Incomplete multi sector transfer detected! )-:
			 * Set the magic to "BAAD" and return failure.
			 * Note that magic_BAAD is already converted to le32.
			 */
			b->magic = magic_BAAD;
			return -EINVAL;
		}
		data_pos += NTFS_BLOCK_SIZE/sizeof(u16);
	}
	/* Re-setup the variables. */
	usa_count = le16_to_cpu(b->usa_count) - 1;
	data_pos = (u16*)b + NTFS_BLOCK_SIZE/sizeof(u16) - 1;
	/* Fixup all sectors. */
	while (usa_count--) {
		/*
		 * Increment position in usa and restore original data from
		 * the usa into the data buffer.
		 */
		*data_pos = *(++usa_pos);
		/* Increment position in data as well. */
		data_pos += NTFS_BLOCK_SIZE/sizeof(u16);
	}
	return 0;
}

/**
 * pre_write_mst_fixup - apply multi sector transfer protection
 * @b:		pointer to the data to protect
 * @size:	size in bytes of @b
 *
 * Perform the necessary pre write multi sector transfer fixup on the data
 * pointer to by @b of @size.
 *
 * Return 0 if fixup applied (success) or -EINVAL if no fixup was performed
 * (assumed not needed). This is in contrast to post_read_mst_fixup() above.
 *
 * NOTE: We consider the absence / invalidity of an update sequence array to
 * mean that the structure is not subject to protection and hence doesn't need
 * to be fixed up. This means that you have to create a valid update sequence
 * array header in the ntfs record before calling this function, otherwise it
 * will fail (the header needs to contain the position of the update sequence
 * array together with the number of elements in the array). You also need to
 * initialise the update sequence number before calling this function
 * otherwise a random word will be used (whatever was in the record at that
 * position at that time).
 */
int pre_write_mst_fixup(NTFS_RECORD *b, const u32 size)
{
	le16 *usa_pos, *data_pos;
	u16 usa_ofs, usa_count, usn;
	le16 le_usn;

	/* Sanity check + only fixup if it makes sense. */
	if (!b || ntfs_is_baad_record(b->magic) ||
			ntfs_is_hole_record(b->magic))
		return -EINVAL;
	/* Setup the variables. */
	usa_ofs = le16_to_cpu(b->usa_ofs);
	/* Decrement usa_count to get number of fixups. */
	usa_count = le16_to_cpu(b->usa_count) - 1;
	/* Size and alignment checks. */
	if ( size & (NTFS_BLOCK_SIZE - 1)	||
	     usa_ofs & 1			||
	     usa_ofs + (usa_count * 2) > size	||
	     (size >> NTFS_BLOCK_SIZE_BITS) != usa_count)
		return -EINVAL;
	/* Position of usn in update sequence array. */
	usa_pos = (le16*)((u8*)b + usa_ofs);
	/*
	 * Cyclically increment the update sequence number
	 * (skipping 0 and -1, i.e. 0xffff).
	 */
	usn = le16_to_cpup(usa_pos) + 1;
	if (usn == 0xffff || !usn)
		usn = 1;
	le_usn = cpu_to_le16(usn);
	*usa_pos = le_usn;
	/* Position in data of first u16 that needs fixing up. */
	data_pos = (le16*)b + NTFS_BLOCK_SIZE/sizeof(le16) - 1;
	/* Fixup all sectors. */
	while (usa_count--) {
		/*
		 * Increment the position in the usa and save the
		 * original data from the data buffer into the usa.
		 */
		*(++usa_pos) = *data_pos;
		/* Apply fixup to data. */
		*data_pos = le_usn;
		/* Increment position in data as well. */
		data_pos += NTFS_BLOCK_SIZE/sizeof(le16);
	}
	return 0;
}

/**
 * post_write_mst_fixup - fast deprotect multi sector transfer protected data
 * @b:		pointer to the data to deprotect
 *
 * Perform the necessary post write multi sector transfer fixup, not checking
 * for any errors, because we assume we have just used pre_write_mst_fixup(),
 * thus the data will be fine or we would never have gotten here.
 */
void post_write_mst_fixup(NTFS_RECORD *b)
{
	le16 *usa_pos, *data_pos;

	u16 usa_ofs = le16_to_cpu(b->usa_ofs);
	u16 usa_count = le16_to_cpu(b->usa_count) - 1;

	/* Position of usn in update sequence array. */
	usa_pos = (le16*)b + usa_ofs/sizeof(le16);

	/* Position in protected data of first u16 that needs fixing up. */
	data_pos = (le16*)b + NTFS_BLOCK_SIZE/sizeof(le16) - 1;

	/* Fixup all sectors. */
	while (usa_count--) {
		/*
		 * Increment position in usa and restore original data from
		 * the usa into the data buffer.
		 */
		*data_pos = *(++usa_pos);

		/* Increment position in data as well. */
		data_pos += NTFS_BLOCK_SIZE/sizeof(le16);
	}
}
