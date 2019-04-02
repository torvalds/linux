/*
 * de.h - NTFS kernel de support. Part of the Linux-NTFS project.
 *
 * Copyright (c) 2001-2004 Anton Altaparmakov
 *
 * This program/include file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program/include file is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the Linux-NTFS
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation,Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _LINUX_NTFS_DE_H
#define _LINUX_NTFS_DE_H

#include <linux/fs.h>

#include "runlist.h"

#ifdef DE

extern int de_msgs;

extern __printf(4, 5)
void __ntfs_de(const char *file, int line, const char *function,
		  const char *format, ...);
/**
 * ntfs_de - write a de level message to syslog
 * @f:		a printf format string containing the message
 * @...:	the variables to substitute into @f
 *
 * ntfs_de() writes a DE level message to the syslog but only if the
 * driver was compiled with -DDE. Otherwise, the call turns into a NOP.
 */
#define ntfs_de(f, a...)						\
	__ntfs_de(__FILE__, __LINE__, __func__, f, ##a)

extern void ntfs_de_dump_runlist(const runlist_element *rl);

#else	/* !DE */

#define ntfs_de(fmt, ...)						\
do {									\
	if (0)								\
		no_printk(fmt, ##__VA_ARGS__);				\
} while (0)

#define ntfs_de_dump_runlist(rl)	do {} while (0)

#endif	/* !DE */

extern  __printf(3, 4)
void __ntfs_warning(const char *function, const struct super_block *sb,
		    const char *fmt, ...);
#define ntfs_warning(sb, f, a...)	__ntfs_warning(__func__, sb, f, ##a)

extern  __printf(3, 4)
void __ntfs_error(const char *function, const struct super_block *sb,
		  const char *fmt, ...);
#define ntfs_error(sb, f, a...)		__ntfs_error(__func__, sb, f, ##a)

#endif /* _LINUX_NTFS_DE_H */
