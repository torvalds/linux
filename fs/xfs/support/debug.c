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
#include "spin.h"

static char		message[256];	/* keep it off the stack */
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
	len = vsprintf(message, fp, ap);
	if (level != CE_DEBUG && message[len-1] != '\n')
		strcat(message, "\n");
	printk("%s%s", err_level[level], message);
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
	len = vsprintf(message, fmt, ap);
	if (level != CE_DEBUG && message[len-1] != '\n')
		strcat(message, "\n");
	spin_unlock_irqrestore(&xfs_err_lock,flags);
	printk("%s%s", err_level[level], message);
	BUG_ON(level == CE_PANIC);
}

void
assfail(char *expr, char *file, int line)
{
	printk("Assertion failed: %s, file: %s, line: %d\n", expr, file, line);
	BUG();
}

#if ((defined(DEBUG) || defined(INDUCE_IO_ERRROR)) && !defined(NO_WANT_RANDOM))
unsigned long random(void)
{
	static unsigned long	RandomValue = 1;
	/* cycles pseudo-randomly through all values between 1 and 2^31 - 2 */
	register long	rv = RandomValue;
	register long	lo;
	register long	hi;

	hi = rv / 127773;
	lo = rv % 127773;
	rv = 16807 * lo - 2836 * hi;
	if (rv <= 0) rv += 2147483647;
	return RandomValue = rv;
}
#endif /* DEBUG || INDUCE_IO_ERRROR || !NO_WANT_RANDOM */
