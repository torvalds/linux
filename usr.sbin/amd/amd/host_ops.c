/*	$OpenBSD: host_ops.c,v 1.20 2021/10/21 10:55:56 deraadt Exp $	*/

/*
 * Copyright (c) 1990 Jan-Simon Pendry
 * Copyright (c) 1990 Imperial College of Science, Technology & Medicine
 * Copyright (c) 1990, 1993
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
 *	from: @(#)host_ops.c	8.1 (Berkeley) 6/6/93
 */

#include "am.h"

#ifdef HAS_HOST

#include "mount.h"
#include <sys/stat.h>

/*
 * NFS host file system.
 * Mounts all exported filesystems from a given host.
 * This has now degenerated into a mess but will not
 * be rewritten.  Amd 6 will support the abstractions
 * needed to make this work correctly.
 */

/*
 * Define HOST_RPC_UDP to use dgram instead of stream RPC.
 * Datagrams are generally much faster.
 */
/*#define	HOST_RPC_UDP*/

/*
 * Define HOST_MKDIRS to make Amd automatically try
 * to create the mount points.
 */
#define HOST_MKDIRS

/*
 * Determine the mount point
 */
#define MAKE_MNTPT(mntpt, ex, mf) { \
			if (strcmp((ex)->ex_dir, "/") == 0) \
				strlcpy((mntpt), (mf)->mf_mount, sizeof((mntpt))); \
			else \
				snprintf((mntpt), sizeof(mntpt), "%s%s", (mf)->mf_mount, (ex)->ex_dir); \
}

/*
 * Execute needs the same as NFS plus a helper command
 */
static char *
host_match(am_opts *fo)
{
#ifdef HOST_EXEC
	if (!host_helper) {
		plog(XLOG_USER, "No host helper command given");
		return FALSE;
	}
#endif /* HOST_EXEC */

	/*
	 * Make sure rfs is specified to keep nfs_match happy...
	 */
	if (!fo->opt_rfs)
		fo->opt_rfs = "/";

	return (*nfs_ops.fs_match)(fo);
}

static int
host_init(mntfs *mf)
{
	if (strchr(mf->mf_info, ':') == 0)
		return ENOENT;
	return 0;
}

/*
 * Two implementations:
 * HOST_EXEC gets you the external version.  The program specified with
 * the -h option is called.  The external program is not published...
 * roll your own.
 *
 * Otherwise you get the native version.  Faster but makes the program
 * bigger.
 */

#ifndef HOST_EXEC

static bool_t
xdr_pri_free(xdrproc_t xdr_args, void *args_ptr)
{
	XDR xdr;

	xdr.x_op = XDR_FREE;
	return ((*xdr_args)(&xdr, args_ptr));
}

static int
do_mount(fhstatus *fhp, char *dir, char *fs_name, char *opts, mntfs *mf)
{
	struct stat stb;

#ifdef DEBUG
	dlog("host: mounting fs %s on %s", fs_name, dir);
#endif /* DEBUG */
#ifdef HOST_MKDIRS
	(void) mkdirs(dir, 0555);
#endif /* HOST_MKDIRS */
	if (stat(dir, &stb) < 0 || (stb.st_mode & S_IFMT) != S_IFDIR) {
		plog(XLOG_ERROR, "No mount point for %s - skipping", dir);
		return ENOENT;
	}

	return mount_nfs_fh(fhp, dir, fs_name, opts, mf);
}

static int
sortfun(const void *arg1, const void *arg2)
{
	const exports *a = arg1, *b = arg2;
	return strcmp((*a)->ex_dir, (*b)->ex_dir);
}

/*
 * Get filehandle
 */
static int
fetch_fhandle(CLIENT *client, char *dir, fhstatus *fhp)
{
	struct timeval tv;
	enum clnt_stat clnt_stat;

	/*
	 * Pick a number, any number...
	 */
	tv.tv_sec = 20;
	tv.tv_usec = 0;

#ifdef DEBUG
	dlog("Fetching fhandle for %s", dir);
#endif /* DEBUG */
	/*
	 * Call the mount daemon on the remote host to
	 * get the filehandle.
	 */
	fhp->fhs_vers = MOUNTVERS;
	clnt_stat = clnt_call(client, MOUNTPROC_MNT, xdr_dirpath, &dir, xdr_fhstatus, fhp, tv);
	if (clnt_stat != RPC_SUCCESS) {
		char *msg = clnt_sperrno(clnt_stat);
		plog(XLOG_ERROR, "mountd rpc failed: %s", msg);
		return EIO;
	}
	/*
	 * Check status of filehandle
	 */
	if (fhp->fhs_stat) {
#ifdef DEBUG
		errno = fhp->fhs_stat;
		dlog("fhandle fetch failed: %m");
#endif /* DEBUG */
		return fhp->fhs_stat;
	}
	return 0;
}

/*
 * Scan mount table to see if something already mounted
 */
static int
already_mounted(mntlist *mlist, char *dir)
{
	mntlist *ml;

	for (ml = mlist; ml; ml = ml->mnext)
		if (strcmp(ml->mnt->mnt_dir, dir) == 0)
			return 1;
	return 0;
}

/*
 * Mount the export tree from a host
 */
static int
host_fmount(mntfs *mf)
{
	struct timeval tv2;
	CLIENT *client;
	enum clnt_stat clnt_stat;
	int n_export;
	int j, k;
	exports exlist = 0, ex;
	exports *ep = 0;
	fhstatus *fp = 0;
	char *host = mf->mf_server->fs_host;
	int error = 0;
	struct sockaddr_in sin;
	int sock = RPC_ANYSOCK;
	int ok = FALSE;
	mntlist *mlist;
	char fs_name[PATH_MAX], *rfs_dir;
	char mntpt[PATH_MAX];
	struct timeval tv;
	tv.tv_sec = 10; tv.tv_usec = 0;

	/*
	 * Read the mount list
	 */
	mlist = read_mtab(mf->mf_mount);

	/*
	 * Take a copy of the server address
	 */
	sin = *mf->mf_server->fs_ip;

	/*
	 * Zero out the port - make sure we recompute
	 */
	sin.sin_port = 0;
	/*
	 * Make a client end-point.
	 * Try TCP first
	 */
	if ((client = clnttcp_create(&sin, MOUNTPROG, MOUNTVERS, &sock, 0, 0)) == NULL &&
		(client = clntudp_create(&sin, MOUNTPROG, MOUNTVERS, tv, &sock)) == NULL) {
		plog(XLOG_ERROR, "Failed to make rpc connection to mountd on %s", host);
		error = EIO;
		goto out;
	}

	if (!nfs_auth) {
		error = make_nfs_auth();
		if (error)
			goto out;
	}

	client->cl_auth = nfs_auth;

#ifdef DEBUG
	dlog("Fetching export list from %s", host);
#endif /* DEBUG */

	/*
	 * Fetch the export list
	 */
	tv2.tv_sec = 10; tv2.tv_usec = 0;
	clnt_stat = clnt_call(client, MOUNTPROC_EXPORT, xdr_void, 0, xdr_exports, &exlist, tv2);
	if (clnt_stat != RPC_SUCCESS) {
		/*clnt_perror(client, "rpc");*/
		error = EIO;
		goto out;
	}

	/*
	 * Figure out how many exports were returned
	 */
	for (n_export = 0, ex = exlist; ex; ex = ex->ex_next) {
		/*printf("export %s\n", ex->ex_dir);*/
		n_export++;
	}
#ifdef DEBUG
	/*dlog("%d exports returned", n_export);*/
#endif /* DEBUG */

	/*
	 * Allocate an array of pointers into the list
	 * so that they can be sorted.  If the filesystem
	 * is already mounted then ignore it.
	 */
	ep = xreallocarray(NULL, n_export, sizeof *ep);
	for (j = 0, ex = exlist; ex; ex = ex->ex_next) {
		MAKE_MNTPT(mntpt, ex, mf);
		if (!already_mounted(mlist, mntpt))
			ep[j++] = ex;
	}
	n_export = j;

	/*
	 * Sort into order.
	 * This way the mounts are done in order down the tree,
	 * instead of any random order returned by the mount
	 * daemon (the protocol doesn't specify...).
	 */
	qsort(ep, n_export, sizeof(exports), sortfun);

	/*
	 * Allocate an array of filehandles
	 */
	fp = xreallocarray(NULL, n_export, sizeof *fp);

	/*
	 * Try to obtain filehandles for each directory.
	 * If a fetch fails then just zero out the array
	 * reference but discard the error.
	 */
	for (j = k = 0; j < n_export; j++) {
		/* Check and avoid a duplicated export entry */
		if (j > k && ep[k] && strcmp(ep[j]->ex_dir, ep[k]->ex_dir) == 0) {
#ifdef DEBUG
			dlog("avoiding dup fhandle requested for %s", ep[j]->ex_dir);
#endif
			ep[j] = 0;
		} else {
			k = j;
			if ((error = fetch_fhandle(client, ep[j]->ex_dir, &fp[j])))
				ep[j] = 0;
		}
	}

	/*
	 * Mount each filesystem for which we have a filehandle.
	 * If any of the mounts succeed then mark "ok" and return
	 * error code 0 at the end.  If they all fail then return
	 * the last error code.
	 */
	strlcpy(fs_name, mf->mf_info, sizeof(fs_name));
	if ((rfs_dir = strchr(fs_name, ':')) == (char *) 0) {
		plog(XLOG_FATAL, "host_fmount: mf_info has no colon");
		error = EINVAL;
		goto out;
	}
	++rfs_dir;
	for (j = 0; j < n_export; j++) {
		ex = ep[j];
		if (ex) {
			strlcpy(rfs_dir, ex->ex_dir, fs_name + sizeof fs_name - rfs_dir);
			MAKE_MNTPT(mntpt, ex, mf);
			if (do_mount(&fp[j], mntpt, fs_name, mf->mf_mopts, mf) == 0)
				ok = TRUE;
		}
	}

	/*
	 * Clean up and exit
	 */
out:
	discard_mntlist(mlist);
	free(ep);
	free(fp);
	if (client)
		clnt_destroy(client);
	if (exlist)
		xdr_pri_free(xdr_exports, &exlist);
	if (ok)
		return 0;
	return error;
}

/*
 * Return true if pref is a directory prefix of dir.
 *
 * TODO:
 * Does not work if pref is "/".
 */
static int
directory_prefix(char *pref, char *dir)
{
	int len = strlen(pref);
	if (strncmp(pref, dir, len) != 0)
		return FALSE;
	if (dir[len] == '/' || dir[len] == '\0')
		return TRUE;
	return FALSE;
}

/*
 * Unmount a mount tree
 */
static int
host_fumount(mntfs *mf)
{
	mntlist *ml, *mprev;
	int xerror = 0;

	/*
	 * Read the mount list
	 */
	mntlist *mlist = read_mtab(mf->mf_mount);

	/*
	 * Reverse list...
	 */
	ml = mlist;
	mprev = 0;
	while (ml) {
		mntlist *ml2 = ml->mnext;
		ml->mnext = mprev;
		mprev = ml;
		ml = ml2;
	}
	mlist = mprev;

	/*
	 * Unmount all filesystems...
	 */
	for (ml = mlist; ml && !xerror; ml = ml->mnext) {
		char *dir = ml->mnt->mnt_dir;
		if (directory_prefix(mf->mf_mount, dir)) {
			int error;
#ifdef DEBUG
			dlog("host: unmounts %s", dir);
#endif /* DEBUG */
			/*
			 * Unmount "dir"
			 */
			error = umount_fs(dir);
			/*
			 * Keep track of errors
			 */
			if (error) {
				if (!xerror)
					xerror = error;
				if (error != EBUSY) {
					errno = error;
					plog(XLOG_ERROR, "Tree unmount of %s failed: %m", ml->mnt->mnt_dir);
				}
			} else {
#ifdef HOST_MKDIRS
				(void) rmdirs(dir);
#endif /* HOST_MKDIRS */
			}
		}
	}

	/*
	 * Throw away mount list
	 */
	discard_mntlist(mlist);

	/*
	 * Try to remount, except when we are shutting down.
	 */
	if (xerror && amd_state != Finishing) {
		xerror = host_fmount(mf);
		if (!xerror) {
			/*
			 * Don't log this - it's usually too verbose
			plog(XLOG_INFO, "Remounted host %s", mf->mf_info);
			 */
			xerror = EBUSY;
		}
	}
	return xerror;
}

/*
 * Tell mountd we're done.
 * This is not quite right, because we may still
 * have other filesystems mounted, but the existing
 * mountd protocol is badly broken anyway.
 */
static void host_umounted(am_node *mp)
{
}


#else /* HOST_EXEC */

static int
host_exec(char *op, char *host, char *fs, char *opts)
{
	int error;
	char *argv[7];

	/*
	 * Build arg vector
	 */
	argv[0] = host_helper;
	argv[1] = host_helper;
	argv[2] = op;
	argv[3] = host;
	argv[4] = fs;
	argv[5] = opts && *opts ? opts : "rw,default";
	argv[6] = 0;

	/*
	 * Put stdout to stderr
	 */
	(void) fclose(stdout);
	(void) dup(fileno(logfp));
	if (fileno(logfp) != fileno(stderr)) {
		(void) fclose(stderr);
		(void) dup(fileno(logfp));
	}
	/*
	 * Try the exec
	 */
#ifdef DEBUG
	Debug(D_FULL) {
		char **cp = argv;
		plog(XLOG_DEBUG, "executing (un)mount command...");
		while (*cp) {
			plog(XLOG_DEBUG, "arg[%d] = '%s'", cp-argv, *cp);
			cp++;
		}
	}
#endif /* DEBUG */
	if (argv[0] == 0 || argv[1] == 0) {
		errno = EINVAL;
		plog(XLOG_USER, "1st/2nd args missing to (un)mount program");
	} else {
		(void) execv(argv[0], argv+1);
	}
	/*
	 * Save error number
	 */
	error = errno;
	plog(XLOG_ERROR, "exec %s failed: %m", argv[0]);

	/*
	 * Return error
	 */
	return error;
}

static int
host_mount(am_node *mp)
{
	mntfs *mf = mp->am_mnt;

	return host_exec("mount", mf->mf_server->fs_host, mf->mf_mount, mf->mf_opts);
}

static int
host_umount(am_node *mp)
{
	mntfs *mf = mp->am_mnt;

	return host_exec("unmount", mf->mf_server->fs_host, mf->mf_mount, "xxx");
}

#endif /* HOST_EXEC */

/*
 * Ops structure
 */
am_ops host_ops = {
	"host",
	host_match,
	host_init,
	auto_fmount,
	host_fmount,
	auto_fumount,
	host_fumount,
	efs_lookuppn,
	efs_readdir,
	0, /* host_readlink */
	0, /* host_mounted */
#ifdef HOST_EXEC
	0, /* host_umounted */
#else
	host_umounted,
#endif
	find_nfs_srvr,
	FS_MKMNT|FS_BACKGROUND|FS_AMQINFO
};

#endif /* HAS_HOST */
