/*
 * Copyright (c) 1989 Jan-Simon Pendry
 * Copyright (c) 1989 Imperial College of Science, Technology & Medicine
 * Copyright (c) 1989, 1993
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
 *	from: @(#)srvr_afs.c	8.1 (Berkeley) 6/6/93
 *	$Id: srvr_afs.c,v 1.8 2015/12/05 21:15:01 mmcc Exp $
 */

/*
 * Automount FS server ("localhost") modeling
 */

#include "am.h"

extern qelem afs_srvr_list;
qelem afs_srvr_list = { &afs_srvr_list, &afs_srvr_list };

static fserver *localhost;

/*
 * Find an nfs server for the local host
 */
fserver *
find_afs_srvr(mntfs *mf)
{
	fserver *fs = localhost;

	if (!fs) {
		fs = ALLOC(fserver);
		fs->fs_refc = 0;
		fs->fs_host = strdup("localhost");
		fs->fs_ip = 0;
		fs->fs_cid = 0;
		fs->fs_pinger = 0;
		fs->fs_flags = FSF_VALID;
		fs->fs_type = "local";
		fs->fs_private = 0;
		fs->fs_prfree = 0;

		ins_que(&fs->fs_q, &afs_srvr_list);

		srvrlog(fs, "starts up");

		localhost = fs;
	}

	fs->fs_refc++;

	return fs;
}

/*------------------------------------------------------------------*/
		/* Generic routines follow */

/*
 * Wakeup anything waiting for this server
 */
void
wakeup_srvr(fserver *fs)
{
	fs->fs_flags &= ~FSF_WANT;
	wakeup(fs);
}

/*
 * Called when final ttl of server has expired
 */
static void
timeout_srvr(void *arg)
{
	fserver *fs = arg;

	/*
	 * If the reference count is still zero then
	 * we are free to remove this node
	 */
	if (fs->fs_refc == 0) {
#ifdef DEBUG
		dlog("Deleting file server %s", fs->fs_host);
#endif /* DEBUG */
		if (fs->fs_flags & FSF_WANT)
			wakeup_srvr(fs);

		/*
		 * Remove from queue.
		 */
		rem_que(&fs->fs_q);
		/*
		 * (Possibly) call the private free routine.
		 */
		if (fs->fs_private && fs->fs_prfree)
			(*fs->fs_prfree)(fs->fs_private);

		/*
		 * Free the net address
		 */
		free(fs->fs_ip);

		/*
		 * Free the host name.
		 */
		free(fs->fs_host);

		/*
		 * Discard the fserver object.
		 */
		free(fs);
	}
}

/*
 * Free a file server
 */
void
free_srvr(fserver *fs)
{
	if (--fs->fs_refc == 0) {
		/*
		 * The reference count is now zero,
		 * so arrange for this node to be
		 * removed in AM_TTL seconds if no
		 * other mntfs is referencing it.
		 */
		int ttl = (fs->fs_flags & (FSF_DOWN|FSF_ERROR)) ? 19 : AM_TTL;
#ifdef DEBUG
		dlog("Last hard reference to file server %s - will timeout in %ds", fs->fs_host, ttl);
#endif /* DEBUG */
		if (fs->fs_cid) {
			untimeout(fs->fs_cid);
			/*
			 * Turn off pinging - XXX
			 */
			fs->fs_flags &= ~FSF_PINGING;
		}
		/*
		 * Keep structure lying around for a while
		 */
		fs->fs_cid = timeout(ttl, timeout_srvr, fs);
		/*
		 * Mark the fileserver down and invalid again
		 */
		fs->fs_flags &= ~FSF_VALID;
		fs->fs_flags |= FSF_DOWN;
	}
}

/*
 * Make a duplicate fserver reference
 */
fserver *
dup_srvr(fserver *fs)
{
	fs->fs_refc++;
	return fs;
}

/*
 * Log state change
 */
void srvrlog(fserver *fs, char *state)
{
	plog(XLOG_INFO, "file server %s type %s %s", fs->fs_host, fs->fs_type, state);
}
