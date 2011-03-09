/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include <xfs.h>
#include "debug.h"

/* xfs_mount.h drags a lot of crap in, sorry.. */
#include "xfs_sb.h"
#include "xfs_inum.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_error.h"

void
cmn_err(
	const char	*lvl,
	const char	*fmt,
	...)
{
	struct va_format vaf;
	va_list		args;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;

	printk("%s%pV", lvl, &vaf);
	va_end(args);

	BUG_ON(strncmp(lvl, KERN_EMERG, strlen(KERN_EMERG)) == 0);
}

void
xfs_fs_cmn_err(
	const char		*lvl,
	struct xfs_mount	*mp,
	const char		*fmt,
	...)
{
	struct va_format	vaf;
	va_list			args;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;

	printk("%sFilesystem %s: %pV", lvl, mp->m_fsname, &vaf);
	va_end(args);

	BUG_ON(strncmp(lvl, KERN_EMERG, strlen(KERN_EMERG)) == 0);
}

/* All callers to xfs_cmn_err use CE_ALERT, so don't bother testing lvl */
void
xfs_cmn_err(
	int			panic_tag,
	const char		*lvl,
	struct xfs_mount	*mp,
	const char		*fmt,
	...)
{
	struct va_format	vaf;
	va_list			args;
	int			do_panic = 0;

	if (xfs_panic_mask && (xfs_panic_mask & panic_tag)) {
		printk(KERN_ALERT "XFS: Transforming an alert into a BUG.");
		do_panic = 1;
	}

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;

	printk(KERN_ALERT "Filesystem %s: %pV", mp->m_fsname, &vaf);
	va_end(args);

	BUG_ON(do_panic);
}

void
assfail(char *expr, char *file, int line)
{
	printk(KERN_CRIT "Assertion failed: %s, file: %s, line: %d\n", expr,
	       file, line);
	BUG();
}

void
xfs_hex_dump(void *p, int length)
{
	print_hex_dump(KERN_ALERT, "", DUMP_PREFIX_ADDRESS, 16, 1, p, length, 1);
}
