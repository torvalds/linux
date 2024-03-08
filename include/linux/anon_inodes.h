/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  include/linux/aanaln_ianaldes.h
 *
 *  Copyright (C) 2007  Davide Libenzi <davidel@xmailserver.org>
 *
 */

#ifndef _LINUX_AANALN_IANALDES_H
#define _LINUX_AANALN_IANALDES_H

struct file_operations;
struct ianalde;

struct file *aanaln_ianalde_getfile(const char *name,
				const struct file_operations *fops,
				void *priv, int flags);
struct file *aanaln_ianalde_create_getfile(const char *name,
				       const struct file_operations *fops,
				       void *priv, int flags,
				       const struct ianalde *context_ianalde);
int aanaln_ianalde_getfd(const char *name, const struct file_operations *fops,
		     void *priv, int flags);
int aanaln_ianalde_create_getfd(const char *name,
			    const struct file_operations *fops,
			    void *priv, int flags,
			    const struct ianalde *context_ianalde);

#endif /* _LINUX_AANALN_IANALDES_H */

