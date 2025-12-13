/* SPDX-License-Identifier: GPL-2.0 */
/*
 * HFS/HFS+ common definitions, inline functions,
 * and shared functionality.
 */

#ifndef _HFS_COMMON_H_
#define _HFS_COMMON_H_

#ifdef pr_fmt
#undef pr_fmt
#endif

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#define hfs_dbg(fmt, ...)							\
	pr_debug("pid %d:%s:%d %s(): " fmt,					\
		 current->pid, __FILE__, __LINE__, __func__, ##__VA_ARGS__)	\

#endif /* _HFS_COMMON_H_ */
