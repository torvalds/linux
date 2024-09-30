/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2024 Intel Corporation */

#ifndef __LIBETH_TYPES_H
#define __LIBETH_TYPES_H

#include <linux/types.h>

/**
 * struct libeth_sq_napi_stats - "hot" counters to update in Tx completion loop
 * @packets: completed frames counter
 * @bytes: sum of bytes of completed frames above
 * @raw: alias to access all the fields as an array
 */
struct libeth_sq_napi_stats {
	union {
		struct {
							u32 packets;
							u32 bytes;
		};
		DECLARE_FLEX_ARRAY(u32, raw);
	};
};

#endif /* __LIBETH_TYPES_H */
