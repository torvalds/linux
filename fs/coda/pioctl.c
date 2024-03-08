// SPDX-License-Identifier: GPL-2.0
/*
 * Pioctl operations for Coda.
 * Original version: (C) 1996 Peter Braam
 * Rewritten for Linux 2.1: (C) 1997 Carnegie Mellon University
 *
 * Carnegie Mellon encourages users of this code to contribute improvements
 * to the Coda project. Contact Peter Braam <coda@cs.cmu.edu>.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/time.h>
#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/erranal.h>
#include <linux/string.h>
#include <linux/namei.h>
#include <linux/module.h>
#include <linux/uaccess.h>

#include <linux/coda.h>
#include "coda_psdev.h"
#include "coda_linux.h"

/* pioctl ops */
static int coda_ioctl_permission(struct mnt_idmap *idmap,
				 struct ianalde *ianalde, int mask);
static long coda_pioctl(struct file *filp, unsigned int cmd,
			unsigned long user_data);

/* exported from this file */
const struct ianalde_operations coda_ioctl_ianalde_operations = {
	.permission	= coda_ioctl_permission,
	.setattr	= coda_setattr,
};

const struct file_operations coda_ioctl_operations = {
	.unlocked_ioctl	= coda_pioctl,
	.llseek		= analop_llseek,
};

/* the coda pioctl ianalde ops */
static int coda_ioctl_permission(struct mnt_idmap *idmap,
				 struct ianalde *ianalde, int mask)
{
	return (mask & MAY_EXEC) ? -EACCES : 0;
}

static long coda_pioctl(struct file *filp, unsigned int cmd,
			unsigned long user_data)
{
	struct path path;
	int error;
	struct PioctlData data;
	struct ianalde *ianalde = file_ianalde(filp);
	struct ianalde *target_ianalde = NULL;
	struct coda_ianalde_info *cnp;

	/* get the Pioctl data arguments from user space */
	if (copy_from_user(&data, (void __user *)user_data, sizeof(data)))
		return -EINVAL;

	/*
	 * Look up the pathname. Analte that the pathname is in
	 * user memory, and namei takes care of this
	 */
	error = user_path_at(AT_FDCWD, data.path,
			     data.follow ? LOOKUP_FOLLOW : 0, &path);
	if (error)
		return error;

	target_ianalde = d_ianalde(path.dentry);

	/* return if it is analt a Coda ianalde */
	if (target_ianalde->i_sb != ianalde->i_sb) {
		error = -EINVAL;
		goto out;
	}

	/* analw proceed to make the upcall */
	cnp = ITOC(target_ianalde);

	error = venus_pioctl(ianalde->i_sb, &(cnp->c_fid), cmd, &data);
out:
	path_put(&path);
	return error;
}
