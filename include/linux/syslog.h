/* SPDX-License-Identifier: GPL-2.0-or-later */
/*  Syslog internals
 *
 *  Copyright 2010 Canonical, Ltd.
 *  Author: Kees Cook <kees.cook@canonical.com>
 */

#ifndef _LINUX_SYSLOG_H
#define _LINUX_SYSLOG_H

/* Close the log.  Currently a NOP. */
#define SYSLOG_ACTION_CLOSE          0
/* Open the log. Currently a NOP. */
#define SYSLOG_ACTION_OPEN           1
/* Read from the log. */
#define SYSLOG_ACTION_READ           2
/* Read all messages remaining in the ring buffer. */
#define SYSLOG_ACTION_READ_ALL       3
/* Read and clear all messages remaining in the ring buffer */
#define SYSLOG_ACTION_READ_CLEAR     4
/* Clear ring buffer. */
#define SYSLOG_ACTION_CLEAR          5
/* Disable printk's to console */
#define SYSLOG_ACTION_CONSOLE_OFF    6
/* Enable printk's to console */
#define SYSLOG_ACTION_CONSOLE_ON     7
/* Set level of messages printed to console */
#define SYSLOG_ACTION_CONSOLE_LEVEL  8
/* Return number of unread characters in the log buffer */
#define SYSLOG_ACTION_SIZE_UNREAD    9
/* Return size of the log buffer */
#define SYSLOG_ACTION_SIZE_BUFFER   10

#define SYSLOG_FROM_READER           0
#define SYSLOG_FROM_PROC             1

int do_syslog(int type, char __user *buf, int count, int source);

#endif /* _LINUX_SYSLOG_H */
