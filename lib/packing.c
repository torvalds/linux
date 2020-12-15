// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2016-2018, NXP Semiconductors
 * Copyright (c) 2018-2019, Vladimir Oltean <olteanv@gmail.com>
 */
#include <linux/packing.h>
#include <linux/module.h>
#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/types.h>

static int get_le_offset(int offset)
{
	int closest_multiple_of_4;

	closest_multiple_of_4 = (offset / 4) * 4;
	offset -= closest_multiple_of_4;
	return closest_multiple_of_4 + (3 - offset);
}

static int get_reverse_lsw32_offset(int offset, size_t len)
{
	int closest_multiple_of_4;
	int word_index;

	word_index = offset / 4;
	closest_multiple_of_4 = word_index * 4;
	offset -= closest_multiple_of_4;
	word_index = (len / 4) - word_index - 1;
	return word_index * 4 + offset;
}

static u64 bit_reverse(u64 val, unsigned int width)
{
	u64 new_val = 0;
	unsigned int bit;
	unsigned int i;

	for (i = 0; i < width; i++) {
		bit = (val & (1 << i)) != 0;
		new_val |= (bit << (width - i - 1));
	}
	return new_val;
}

static void adjust_for_msb_right_quirk(u64 *to_write, int *box_start_bit,
				       int *box_end_bit, u8 *box_mask)
{
	int box_bit_width = *box_start_bit - *box_end_bit + 1;
	int new_box_start_bit, new_box_end_bit;

	*to_write >>= *box_end_bit;
	*to_write = bit_reverse(*to_write, box_bit_width);
	*to_write <<= *box_end_bit;

	new_box_end_bit   = box_bit_width - *box_start_bit - 1;
	new_box_start_bit = box_bit_width - *box_end_bit - 1;
	*box_mask = GENMASK_ULL(new_box_start_bit, new_box_end_bit);
	*box_start_bit = new_box_start_bit;
	*box_end_bit   = new_box_end_bit;
}

/**
 * packing - Convert numbers (currently u64) between a packed and an unpacked
 *	     format. Unpacked means laid out in memory in the CPU's native
 *	     understanding of integers, while packed means anything else that
 *	     requires translation.
 *
 * @pbuf: Pointer to a buffer holding the packed value.
 * @uval: Pointer to an u64 holding the unpacked value.
 * @startbit: The index (in logical notation, compensated for quirks) where
 *	      the packed value starts within pbuf. Must be larger than, or
 *	      equal to, endbit.
 * @endbit: The index (in logical notation, compensated for quirks) where
 *	    the packed value ends within pbuf. Must be smaller than, or equal
 *	    to, startbit.
 * @pbuflen: The length in bytes of the packed buffer pointed to by @pbuf.
 * @op: If PACK, then uval will be treated as const pointer and copied (packed)
 *	into pbuf, between startbit and endbit.
 *	If UNPACK, then pbuf will be treated as const pointer and the logical
 *	value between startbit and endbit will be copied (unpacked) to uval.
 * @quirks: A bit mask of QUIRK_LITTLE_ENDIAN, QUIRK_LSW32_IS_FIRST and
 *	    QUIRK_MSB_ON_THE_RIGHT.
 *
 * Return: 0 on success, EINVAL or ERANGE if called incorrectly. Assuming
 *	   correct usage, return code may be discarded.
 *	   If op is PACK, pbuf is modified.
 *	   If op is UNPACK, uval is modified.
 */
int packing(void *pbuf, u64 *uval, int startbit, int endbit, size_t pbuflen,
	    enum packing_op op, u8 quirks)
{
	/* Number of bits for storing "uval"
	 * also width of the field to access in the pbuf
	 */
	u64 value_width;
	/* Logical byte indices corresponding to the
	 * start and end of the field.
	 */
	int plogical_first_u8, plogical_last_u8, box;

	/* startbit is expected to be larger than endbit */
	if (startbit < endbit)
		/* Invalid function call */
		return -EINVAL;

	value_width = startbit - endbit + 1;
	if (value_width > 64)
		return -ERANGE;

	/* Check if "uval" fits in "value_width" bits.
	 * If value_width is 64, the check will fail, but any
	 * 64-bit uval will surely fit.
	 */
	if (op == PACK && value_width < 64 && (*uval >= (1ull << value_width)))
		/* Cannot store "uval" inside "value_width" bits.
		 * Truncating "uval" is most certainly not desirable,
		 * so simply erroring out is appropriate.
		 */
		return -ERANGE;

	/* Initialize parameter */
	if (op == UNPACK)
		*uval = 0;

	/* Iterate through an idealistic view of the pbuf as an u64 with
	 * no quirks, u8 by u8 (aligned at u8 boundaries), from high to low
	 * logical bit significance. "box" denotes the current logical u8.
	 */
	plogical_first_u8 = startbit / 8;
	plogical_last_u8  = endbit / 8;

	for (box = plogical_first_u8; box >= plogical_last_u8; box--) {
		/* Bit indices into the currently accessed 8-bit box */
		int box_start_bit, box_end_bit, box_addr;
		u8  box_mask;
		/* Corresponding bits from the unpacked u64 parameter */
		int proj_start_bit, proj_end_bit;
		u64 proj_mask;

		/* This u8 may need to be accessed in its entirety
		 * (from bit 7 to bit 0), or not, depending on the
		 * input arguments startbit and endbit.
		 */
		if (box == plogical_first_u8)
			box_start_bit = startbit % 8;
		else
			box_start_bit = 7;
		if (box == plogical_last_u8)
			box_end_bit = endbit % 8;
		else
			box_end_bit = 0;

		/* We have determined the box bit start and end.
		 * Now we calculate where this (masked) u8 box would fit
		 * in the unpacked (CPU-readable) u64 - the u8 box's
		 * projection onto the unpacked u64. Though the
		 * box is u8, the projection is u64 because it may fall
		 * anywhere within the unpacked u64.
		 */
		proj_start_bit = ((box * 8) + box_start_bit) - endbit;
		proj_end_bit   = ((box * 8) + box_end_bit) - endbit;
		proj_mask = GENMASK_ULL(proj_start_bit, proj_end_bit);
		box_mask  = GENMASK_ULL(box_start_bit, box_end_bit);

		/* Determine the offset of the u8 box inside the pbuf,
		 * adjusted for quirks. The adjusted box_addr will be used for
		 * effective addressing inside the pbuf (so it's not
		 * logical any longer).
		 */
		box_addr = pbuflen - box - 1;
		if (quirks & QUIRK_LITTLE_ENDIAN)
			box_addr = get_le_offset(box_addr);
		if (quirks & QUIRK_LSW32_IS_FIRST)
			box_addr = get_reverse_lsw32_offset(box_addr,
							    pbuflen);

		if (op == UNPACK) {
			u64 pval;

			/* Read from pbuf, write to uval */
			pval = ((u8 *)pbuf)[box_addr] & box_mask;
			if (quirks & QUIRK_MSB_ON_THE_RIGHT)
				adjust_for_msb_right_quirk(&pval,
							   &box_start_bit,
							   &box_end_bit,
							   &box_mask);

			pval >>= box_end_bit;
			pval <<= proj_end_bit;
			*uval &= ~proj_mask;
			*uval |= pval;
		} else {
			u64 pval;

			/* Write to pbuf, read from uval */
			pval = (*uval) & proj_mask;
			pval >>= proj_end_bit;
			if (quirks & QUIRK_MSB_ON_THE_RIGHT)
				adjust_for_msb_right_quirk(&pval,
							   &box_start_bit,
							   &box_end_bit,
							   &box_mask);

			pval <<= box_end_bit;
			((u8 *)pbuf)[box_addr] &= ~box_mask;
			((u8 *)pbuf)[box_addr] |= pval;
		}
	}
	return 0;
}
EXPORT_SYMBOL(packing);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Generic bitfield packing and unpacking");
