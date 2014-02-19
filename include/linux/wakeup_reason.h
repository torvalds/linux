/*
 * include/linux/wakeup_reason.h
 *
 * Logs the reason which caused the kernel to resume
 * from the suspend mode.
 *
 * Copyright (C) 2014 Google, Inc.
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _LINUX_WAKEUP_REASON_H
#define _LINUX_WAKEUP_REASON_H

#define MAX_SUSPEND_ABORT_LEN 256

void log_wakeup_reason(int irq);
#ifdef CONFIG_SUSPEND
void log_suspend_abort_reason(const char *fmt, ...);
#else
static inline void log_suspend_abort_reason(const char *fmt, ...) { }
#endif

#endif /* _LINUX_WAKEUP_REASON_H */
