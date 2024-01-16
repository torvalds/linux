/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright 2016-2018 NXP
 * Copyright (c) 2018-2019, Vladimir Oltean <olteanv@gmail.com>
 */
#ifndef _LINUX_PACKING_H
#define _LINUX_PACKING_H

#include <linux/types.h>
#include <linux/bitops.h>

#define QUIRK_MSB_ON_THE_RIGHT	BIT(0)
#define QUIRK_LITTLE_ENDIAN	BIT(1)
#define QUIRK_LSW32_IS_FIRST	BIT(2)

enum packing_op {
	PACK,
	UNPACK,
};

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
	    enum packing_op op, u8 quirks);

#endif
