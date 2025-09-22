/*
 * Copyright (c) 1989, 1990 Jan-Simon Pendry
 * Copyright (c) 1989, 1990 Imperial College of Science, Technology & Medicine
 * Copyright (c) 1989, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jan-Simon Pendry at Imperial College, London.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: @(#)mtab.c	8.1 (Berkeley) 6/6/93
 *	$Id: mtab.c,v 1.7 2014/10/20 06:55:59 guenther Exp $
 */

#include "am.h"

/*
 * Firewall /etc/mtab entries
 */
void
mnt_free(struct mntent *mp)
{
	free(mp->mnt_fsname);
	free(mp->mnt_dir);
	free(mp->mnt_type);
	free(mp->mnt_opts);
	free(mp);
}

/*
 * Discard memory allocated for mount list
 */
void
discard_mntlist(mntlist *mp)
{
	mntlist *mp2;

	while ((mp2 = mp)) {
		mp = mp->mnext;
		if (mp2->mnt)
			mnt_free(mp2->mnt);
		free(mp2);
	}
}

/*
 * Throw away a mount list
 */
void
free_mntlist(mntlist *mp)
{
	discard_mntlist(mp);
}

/*
 * Utility routine which determines the value of a
 * numeric option in the mount options (such as port=%d).
 * Returns 0 if the option is not specified.
 */
int
hasmntval(struct mntent *mnt, char *opt)
{
	char *str = hasmntopt(mnt, opt);
	if (str) {
		char *eq = strchr(str, '=');
		if (eq)
			return atoi(eq+1);
		else
			plog(XLOG_USER, "bad numeric option \"%s\" in \"%s\"", opt, str);
	}

	return 0;
}

static struct mntent *
mnt_dup(struct statfs *mp)
{
	struct mntent *new_mp = ALLOC(mntent);

	new_mp->mnt_fsname = strdup(mp->f_mntfromname);
	new_mp->mnt_dir = strdup(mp->f_mntonname);
	new_mp->mnt_type = strdup(mp->f_fstypename);
	new_mp->mnt_opts = strdup("unset");
	new_mp->mnt_freq = 0;
	new_mp->mnt_passno = 0;

	return new_mp;
}

/*
 * Read a mount table into memory
 */
mntlist *
read_mtab(char *fs)
{
	mntlist **mpp, *mhp;
	struct statfs *mntbufp, *mntp;

	int nloc = getmntinfo(&mntbufp, MNT_NOWAIT);

	if (nloc == 0) {
		plog(XLOG_ERROR, "Can't read mount table");
		return 0;
	}

	mpp = &mhp;
	for (mntp = mntbufp; mntp < mntbufp + nloc; mntp++) {
		/*
		 * Allocate a new slot
		 */
		*mpp = ALLOC(mntlist);

		/*
		 * Copy the data returned by getmntent
		 */
		(*mpp)->mnt = mnt_dup(mntp);

		/*
		 * Move to next pointer
		 */
		mpp = &(*mpp)->mnext;
	}

	/*
	 * Terminate the list
	 */
	*mpp = 0;

	return mhp;
}
