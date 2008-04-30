/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright © 2001-2007 Red Hat, Inc.
 *
 * Created by David Woodhouse <dwmw2@infradead.org>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 */

#include <linux/fs.h>
#include "nodelist.h"

int jffs2_ioctl(struct inode *inode, struct file *filp, unsigned int cmd,
		unsigned long arg)
{
	/* Later, this will provide for lsattr.jffs2 and chattr.jffs2, which
	   will include compression support etc. */
	return -ENOTTY;
}

