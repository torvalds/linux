/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2009 IBM Corporation
 * Author: Mimi Zohar <zohar@us.ibm.com>
 */

#ifndef _LINUX_INTEGRITY_H
#define _LINUX_INTEGRITY_H

#include <linux/fs.h>

enum integrity_status {
	INTEGRITY_PASS = 0,
	INTEGRITY_PASS_IMMUTABLE,
	INTEGRITY_FAIL,
	INTEGRITY_FAIL_IMMUTABLE,
	INTEGRITY_NOLABEL,
	INTEGRITY_NOXATTRS,
	INTEGRITY_UNKNOWN,
};

#ifdef CONFIG_INTEGRITY
extern void __init integrity_load_keys(void);

#else
static inline void integrity_load_keys(void)
{
}
#endif /* CONFIG_INTEGRITY */

#endif /* _LINUX_INTEGRITY_H */
