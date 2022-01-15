/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) 2008 Intel Corporation
 * Copyright (c) 2018 The Linux Foundation. All rights reserved.
 */

#ifndef _ASCII85_H_
#define _ASCII85_H_

#include <linux/math.h>
#include <linux/types.h>

#define ASCII85_BUFSZ 6

static inline long
ascii85_encode_len(long len)
{
	return DIV_ROUND_UP(len, 4);
}

static inline const char *
ascii85_encode(u32 in, char *out)
{
	int i;

	if (in == 0)
		return "z";

	out[5] = '\0';
	for (i = 5; i--; ) {
		out[i] = '!' + in % 85;
		in /= 85;
	}

	return out;
}

#endif
