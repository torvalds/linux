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

static char		message[1024];	/* keep it off the stack */
static DEFINE_SPINLOCK(xfs_err_lock);

/* Translate from CE_FOO to KERN_FOO, err_level(CE_FOO) == KERN_FOO */
#define XFS_MAX_ERR_LEVEL	7
#define XFS_ERR_MASK		((1 << 3) - 1)
static const char * const	err_level[XFS_MAX_ERR_LEVEL+1] =
					{KERN_EMERG, KERN_ALERT, KERN_CRIT,
					 KERN_ERR, KERN_WARNING, KERN_NOTICE,
					 KERN_INFO, KERN_DEBUG};

void
cmn_err(register int level, char *fmt, ...)
{
	char	*fp = fmt;
	int	len;
	ulong	flags;
	va_list	ap;

	level &= XFS_ERR_MASK;
	if (level > XFS_MAX_ERR_LEVEL)
		level = XFS_MAX_ERR_LEVEL;
	spin_lock_irqsave(&xfs_err_lock,flags);
	va_start(ap, fmt);
	if (*fmt == '!') fp++;
	len = vsnprintf(message, sizeof(message), fp, ap);
	if (len >= sizeof(message))
		len = sizeof(message) - 1;
	if (message[len-1] == '\n')
		message[len-1] = 0;
	printk("%s%s\n", err_level[level], message);
	va_end(ap);
	spin_unlock_irqrestore(&xfs_err_lock,flags);
	BUG_ON(level == CE_PANIC);
}

void
icmn_err(register int level, char *fmt, va_list ap)
{
	ulong	flags;
	int	len;

	level &= XFS_ERR_MASK;
	if(level > XFS_MAX_ERR_LEVEL)
		level = XFS_MAX_ERR_LEVEL;
	spin_lock_irqsave(&xfs_err_lock,flags);
	len = vsnprintf(message, sizeof(message), fmt, ap);
	if (len >= sizeof(message))
		len = sizeof(message) - 1;
	if (message[len-1] == '\n')
		message[len-1] = 0;
	printk("%s%s\n", err_level[level], message);
	spin_unlock_irqrestore(&xfs_err_lock,flags);
	BUG_ON(level == CE_PANIC);
}

void
assfail(char *expr, char *file, int line)
{
	printk("Assertion failed: %s, file: %s, line: %d\n", expr, file, line);
	BUG();
}

void
xfs_hex_dump(void *p, int length)
{
	print_hex_dump(KERN_ALERT, "", DUMP_PREFIX_OFFSET, 16, 1, p, length, 1);
}
