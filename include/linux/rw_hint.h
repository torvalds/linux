/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_RW_HINT_H
#define _LINUX_RW_HINT_H

#include <linux/build_bug.h>
#include <linux/compiler_attributes.h>
#include <uapi/linux/fcntl.h>

/* Block storage write lifetime hint values. */
enum rw_hint {
	WRITE_LIFE_NOT_SET	= RWH_WRITE_LIFE_NOT_SET,
	WRITE_LIFE_NONE		= RWH_WRITE_LIFE_NONE,
	WRITE_LIFE_SHORT	= RWH_WRITE_LIFE_SHORT,
	WRITE_LIFE_MEDIUM	= RWH_WRITE_LIFE_MEDIUM,
	WRITE_LIFE_LONG		= RWH_WRITE_LIFE_LONG,
	WRITE_LIFE_EXTREME	= RWH_WRITE_LIFE_EXTREME,
} __packed;

/* Sparse ignores __packed annotations on enums, hence the #ifndef below. */
#ifndef __CHECKER__
static_assert(sizeof(enum rw_hint) == 1);
#endif

#endif /* _LINUX_RW_HINT_H */
