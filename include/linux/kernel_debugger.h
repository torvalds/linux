/*
 * include/linux/kernel_debugger.h
 *
 * Copyright (C) 2008 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _LINUX_KERNEL_DEBUGGER_H_
#define _LINUX_KERNEL_DEBUGGER_H_

struct kdbg_ctxt {
	int (*printf)(void *cookie, const char *fmt, ...);
	void *cookie;
};

/* kernel_debugger() is called from IRQ context and should
 * use the kdbg_ctxt.printf to write output (do NOT call
 * printk, do operations not safe from IRQ context, etc).
 *
 * kdbg_ctxt.printf will return -1 if there is not enough
 * buffer space or if you are being aborted.  In this case
 * you must return as soon as possible.
 *
 * Return non-zero if more data is available -- if buffer
 * space ran and you had to stop, but could print more,
 * for example.
 *
 * Additional calls where cmd is "more" will be made if
 * the additional data is desired.
 */
int kernel_debugger(struct kdbg_ctxt *ctxt, char *cmd);

#endif
