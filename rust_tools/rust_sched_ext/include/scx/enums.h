/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Define struct scx_enums that stores the load-time values of enums
 * used by the BPF program.
 *
 * Copyright (c) 2024 Meta Platforms, Inc. and affiliates.
 */

#ifndef __SCX_ENUMS_H
#define __SCX_ENUMS_H

static inline void __ENUM_set(u64 *val, char *type, char *name)
{
	bool res;

	res = __COMPAT_read_enum(type, name, val);
	if (!res)
		*val = 0;
}

#define SCX_ENUM_SET(skel, type, name) do {			\
	__ENUM_set(&skel->rodata->__##name, #type, #name);	\
	} while (0)


#include "enums.autogen.h"

#endif /* __SCX_ENUMS_H */
