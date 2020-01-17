/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  include/linux/ayesn_iyesdes.h
 *
 *  Copyright (C) 2007  Davide Libenzi <davidel@xmailserver.org>
 *
 */

#ifndef _LINUX_ANON_INODES_H
#define _LINUX_ANON_INODES_H

struct file_operations;

struct file *ayesn_iyesde_getfile(const char *name,
				const struct file_operations *fops,
				void *priv, int flags);
int ayesn_iyesde_getfd(const char *name, const struct file_operations *fops,
		     void *priv, int flags);

#endif /* _LINUX_ANON_INODES_H */

