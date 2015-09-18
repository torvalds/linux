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
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include "debug.h"

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
	struct va_format vaf;
	va_list args;
	int flen = 0;

#ifndef DEBUG
	if (!printk_ratelimit())
		return;
#endif
	if (function)
		flen = strlen(function);
	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	if (sb)
		pr_warn("(device %s): %s(): %pV\n",
			sb->s_id, flen ? function : "", &vaf);
	else
		pr_warn("%s(): %pV\n", flen ? function : "", &vaf);
	va_end(args);
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
	struct va_format vaf;
	va_list args;
	int flen = 0;

#ifndef DEBUG
	if (!printk_ratelimit())
		return;
#endif
	if (function)
		flen = strlen(function);
	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	if (sb)
		pr_err("(device %s): %s(): %pV\n",
		       sb->s_id, flen ? function : "", &vaf);
	else
		pr_err("%s(): %pV\n", flen ? function : "", &vaf);
	va_end(args);
}

#ifdef DEBUG

/* If 1, output debug messages, and if 0, don't. */
int debug_msgs = 0;

void __ntfs_debug(const char *file, int line, const char *function,
		const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;
	int flen = 0;

	if (!debug_msgs)
		return;
	if (function)
		flen = strlen(function);
	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	pr_debug("(%s, %d): %s(): %pV", file, line, flen ? function : "", &vaf);
	va_end(args);
}

/* Dump a runlist. Caller has to provide synchronisation for @rl. */
void ntfs_debug_dump_runlist(const runlist_element *rl)
{
	int i;
	const char *lcn_str[5] = { "LCN_HOLE         ", "LCN_RL_NOT_MAPPED",
				   "LCN_ENOENT       ", "LCN_unknown      " };

	if (!debug_msgs)
		return;
	pr_debug("Dumping runlist (values in hex):\n");
	if (!rl) {
		pr_debug("Run list not present.\n");
		return;
	}
	pr_debug("VCN              LCN               Run length\n");
	for (i = 0; ; i++) {
		LCN lcn = (rl + i)->lcn;

		if (lcn < (LCN)0) {
			int index = -lcn - 1;

			if (index > -LCN_ENOENT - 1)
				index = 3;
			pr_debug("%-16Lx %s %-16Lx%s\n",
					(long long)(rl + i)->vcn, lcn_str[index],
					(long long)(rl + i)->length,
					(rl + i)->length ? "" :
						" (runlist end)");
		} else
			pr_debug("%-16Lx %-16Lx  %-16Lx%s\n",
					(long long)(rl + i)->vcn,
					(long long)(rl + i)->lcn,
					(long long)(rl + i)->length,
					(rl + i)->length ? "" :
						" (runlist end)");
		if (!(rl + i)->length)
			break;
	}
}

#endif
