/*
 * debug.c - NTFS kernel debug support. Part of the Linux-NTFS project.
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

#include "debug.h"

/*
 * A static buffer to hold the error string being displayed and a spinlock
 * to protect concurrent accesses to it.
 */
static char err_buf[1024];
static DEFINE_SPINLOCK(err_buf_lock);

/**
 * __ntfs_warning - output a warning to the syslog
 * @function:	name of function outputting the warning
 * @sb:		super block of mounted ntfs filesystem
 * @fmt:	warning string containing format specifications
 * @...:	a variable number of arguments specified in @fmt
 *
 * Outputs a warning to the syslog for the mounted ntfs filesystem described
 * by @sb.
 *
 * @fmt and the corresponding @... is printf style format string containing
 * the warning string and the corresponding format arguments, respectively.
 *
 * @function is the name of the function from which __ntfs_warning is being
 * called.
 *
 * Note, you should be using debug.h::ntfs_warning(@sb, @fmt, @...) instead
 * as this provides the @function parameter automatically.
 */
void __ntfs_warning(const char *function, const struct super_block *sb,
		const char *fmt, ...)
{
	va_list args;
	int flen = 0;

#ifndef DEBUG
	if (!printk_ratelimit())
		return;
#endif
	if (function)
		flen = strlen(function);
	spin_lock(&err_buf_lock);
	va_start(args, fmt);
	vsnprintf(err_buf, sizeof(err_buf), fmt, args);
	va_end(args);
	if (sb)
		printk(KERN_ERR "NTFS-fs warning (device %s): %s(): %s\n",
				sb->s_id, flen ? function : "", err_buf);
	else
		printk(KERN_ERR "NTFS-fs warning: %s(): %s\n",
				flen ? function : "", err_buf);
	spin_unlock(&err_buf_lock);
}

/**
 * __ntfs_error - output an error to the syslog
 * @function:	name of function outputting the error
 * @sb:		super block of mounted ntfs filesystem
 * @fmt:	error string containing format specifications
 * @...:	a variable number of arguments specified in @fmt
 *
 * Outputs an error to the syslog for the mounted ntfs filesystem described
 * by @sb.
 *
 * @fmt and the corresponding @... is printf style format string containing
 * the error string and the corresponding format arguments, respectively.
 *
 * @function is the name of the function from which __ntfs_error is being
 * called.
 *
 * Note, you should be using debug.h::ntfs_error(@sb, @fmt, @...) instead
 * as this provides the @function parameter automatically.
 */
void __ntfs_error(const char *function, const struct super_block *sb,
		const char *fmt, ...)
{
	va_list args;
	int flen = 0;

#ifndef DEBUG
	if (!printk_ratelimit())
		return;
#endif
	if (function)
		flen = strlen(function);
	spin_lock(&err_buf_lock);
	va_start(args, fmt);
	vsnprintf(err_buf, sizeof(err_buf), fmt, args);
	va_end(args);
	if (sb)
		printk(KERN_ERR "NTFS-fs error (device %s): %s(): %s\n",
				sb->s_id, flen ? function : "", err_buf);
	else
		printk(KERN_ERR "NTFS-fs error: %s(): %s\n",
				flen ? function : "", err_buf);
	spin_unlock(&err_buf_lock);
}

#ifdef DEBUG

/* If 1, output debug messages, and if 0, don't. */
int debug_msgs = 0;

void __ntfs_debug (const char *file, int line, const char *function,
		const char *fmt, ...)
{
	va_list args;
	int flen = 0;

	if (!debug_msgs)
		return;
	if (function)
		flen = strlen(function);
	spin_lock(&err_buf_lock);
	va_start(args, fmt);
	vsnprintf(err_buf, sizeof(err_buf), fmt, args);
	va_end(args);
	printk(KERN_DEBUG "NTFS-fs DEBUG (%s, %d): %s(): %s\n", file, line,
			flen ? function : "", err_buf);
	spin_unlock(&err_buf_lock);
}

/* Dump a runlist. Caller has to provide synchronisation for @rl. */
void ntfs_debug_dump_runlist(const runlist_element *rl)
{
	int i;
	const char *lcn_str[5] = { "LCN_HOLE         ", "LCN_RL_NOT_MAPPED",
				   "LCN_ENOENT       ", "LCN_unknown      " };

	if (!debug_msgs)
		return;
	printk(KERN_DEBUG "NTFS-fs DEBUG: Dumping runlist (values in hex):\n");
	if (!rl) {
		printk(KERN_DEBUG "Run list not present.\n");
		return;
	}
	printk(KERN_DEBUG "VCN              LCN               Run length\n");
	for (i = 0; ; i++) {
		LCN lcn = (rl + i)->lcn;

		if (lcn < (LCN)0) {
			int index = -lcn - 1;

			if (index > -LCN_ENOENT - 1)
				index = 3;
			printk(KERN_DEBUG "%-16Lx %s %-16Lx%s\n",
					(rl + i)->vcn, lcn_str[index],
					(rl + i)->length, (rl + i)->length ?
					"" : " (runlist end)");
		} else
			printk(KERN_DEBUG "%-16Lx %-16Lx  %-16Lx%s\n",
					(rl + i)->vcn, (rl + i)->lcn,
					(rl + i)->length, (rl + i)->length ?
					"" : " (runlist end)");
		if (!(rl + i)->length)
			break;
	}
}

#endif
