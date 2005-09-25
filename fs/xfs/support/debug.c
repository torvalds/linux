/*
 * Copyright (c) 2000-2003 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */

#include "debug.h"
#include "spin.h"

#include <asm/page.h>
#include <linux/sched.h>
#include <linux/kernel.h>

static char		message[256];	/* keep it off the stack */
static DEFINE_SPINLOCK(xfs_err_lock);

/* Translate from CE_FOO to KERN_FOO, err_level(CE_FOO) == KERN_FOO */
#define XFS_MAX_ERR_LEVEL	7
#define XFS_ERR_MASK		((1 << 3) - 1)
static char		*err_level[XFS_MAX_ERR_LEVEL+1] =
					{KERN_EMERG, KERN_ALERT, KERN_CRIT,
					 KERN_ERR, KERN_WARNING, KERN_NOTICE,
					 KERN_INFO, KERN_DEBUG};

void
assfail(char *a, char *f, int l)
{
    printk("XFS assertion failed: %s, file: %s, line: %d\n", a, f, l);
    BUG();
}

#if ((defined(DEBUG) || defined(INDUCE_IO_ERRROR)) && !defined(NO_WANT_RANDOM))

unsigned long
random(void)
{
	static unsigned long	RandomValue = 1;
	/* cycles pseudo-randomly through all values between 1 and 2^31 - 2 */
	register long	rv = RandomValue;
	register long	lo;
	register long	hi;

	hi = rv / 127773;
	lo = rv % 127773;
	rv = 16807 * lo - 2836 * hi;
	if( rv <= 0 ) rv += 2147483647;
	return( RandomValue = rv );
}

int
get_thread_id(void)
{
	return current->pid;
}

#endif /* DEBUG || INDUCE_IO_ERRROR || !NO_WANT_RANDOM */

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
	if (message[len-1] != '\n')
		strcat(message, "\n");
	printk("%s%s", err_level[level], message);
	va_end(ap);
	spin_unlock_irqrestore(&xfs_err_lock,flags);

	if (level == CE_PANIC)
		BUG();
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
	if (message[len-1] != '\n')
		strcat(message, "\n");
	spin_unlock_irqrestore(&xfs_err_lock,flags);
	printk("%s%s", err_level[level], message);
	if (level == CE_PANIC)
		BUG();
}
