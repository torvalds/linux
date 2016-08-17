/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://zfsonlinux.org/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
 *****************************************************************************
 *  Solaris Porting Layer (SPL) Error Implementation.
\*****************************************************************************/

#include <sys/sysmacros.h>
#include <sys/cmn_err.h>
#include <linux/ratelimit.h>

/*
 * Limit the number of stack traces dumped to not more than 5 every
 * 60 seconds to prevent denial-of-service attacks from debug code.
 */
DEFINE_RATELIMIT_STATE(dumpstack_ratelimit_state, 60 * HZ, 5);

void
spl_dumpstack(void)
{
	if (__ratelimit(&dumpstack_ratelimit_state)) {
		printk("Showing stack for process %d\n", current->pid);
		dump_stack();
	}
}
EXPORT_SYMBOL(spl_dumpstack);

int
spl_panic(const char *file, const char *func, int line, const char *fmt, ...) {
	const char *newfile;
	char msg[MAXMSGLEN];
	va_list ap;

	newfile = strrchr(file, '/');
	if (newfile != NULL)
		newfile = newfile + 1;
	else
		newfile = file;

	va_start(ap, fmt);
	(void) vsnprintf(msg, sizeof (msg), fmt, ap);
	va_end(ap);

	printk(KERN_EMERG "%s", msg);
	printk(KERN_EMERG "PANIC at %s:%d:%s()\n", newfile, line, func);
	spl_dumpstack();

	/* Halt the thread to facilitate further debugging */
	set_task_state(current, TASK_UNINTERRUPTIBLE);
	while (1)
		schedule();

	/* Unreachable */
	return (1);
}
EXPORT_SYMBOL(spl_panic);

void
vcmn_err(int ce, const char *fmt, va_list ap)
{
	char msg[MAXMSGLEN];

	vsnprintf(msg, MAXMSGLEN - 1, fmt, ap);

	switch (ce) {
	case CE_IGNORE:
		break;
	case CE_CONT:
		printk("%s", msg);
		break;
	case CE_NOTE:
		printk(KERN_NOTICE "NOTICE: %s\n", msg);
		break;
	case CE_WARN:
		printk(KERN_WARNING "WARNING: %s\n", msg);
		break;
	case CE_PANIC:
		printk(KERN_EMERG "PANIC: %s\n", msg);
		spl_dumpstack();

		/* Halt the thread to facilitate further debugging */
		set_task_state(current, TASK_UNINTERRUPTIBLE);
		while (1)
			schedule();
	}
} /* vcmn_err() */
EXPORT_SYMBOL(vcmn_err);

void
cmn_err(int ce, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vcmn_err(ce, fmt, ap);
	va_end(ap);
} /* cmn_err() */
EXPORT_SYMBOL(cmn_err);
