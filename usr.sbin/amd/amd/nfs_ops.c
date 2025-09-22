/*	$OpenBSD: nfs_ops.c,v 1.28 2024/04/23 13:34:51 jsg Exp $	*/

/*-
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
 */

#include "am.h"
#include <sys/stat.h>

#ifdef HAS_NFS

#define NFS
#define NFSCLIENT

#include "mount.h"

/*
 * Network file system
 */

/*
 * Convert from nfsstat to UN*X error code
 */
#define unx_error(e)	((int)(e))

/*
 * The NFS layer maintains a cache of file handles.
 * This is *fundamental* to the implementation and
 * also allows quick remounting when a filesystem
 * is accessed soon after timing out.
 *
 * The NFS server layer knows to flush this cache
 * when a server goes down so avoiding stale handles.
 *
 * Each cache entry keeps a hard reference to
 * the corresponding server.  This ensures that
 * the server keepalive information is maintained.
 *
 * The copy of the sockaddr_in here is taken so
 * that the port can be twiddled to talk to mountd
 * instead of portmap or the NFS server as used
 * elsewhere.
 * The port# is flushed if a server goes down.
 * The IP address is never flushed - we assume
 * that the address of a mounted machine never
 * changes.  If it does, then you have other
 * problems...
 */
typedef struct fh_cache fh_cache;
struct fh_cache {
	qelem	fh_q;			/* List header */
	void	*fh_wchan;		/* Wait channel */
	int	fh_error;		/* Valid data? */
	int	fh_id;			/* Unique id */
	int	fh_cid;			/* Callout id */
	fhstatus fh_handle;		/* Handle on filesystem */
	struct sockaddr_in fh_sin;	/* Address of mountd */
	fserver *fh_fs;			/* Server holding filesystem */
	char	*fh_path;		/* Filesystem on host */
};

/*
 * FH_TTL is the time a file handle will remain in the cache since
 * last being used.  If the file handle becomes invalid, then it
 * will be flushed anyway.
 */
#define	FH_TTL		(5 * 60)		/* five minutes */
#define	FH_TTL_ERROR	(30)			/* 30 seconds */

static int fh_id = 0;
#define	FHID_ALLOC()	(++fh_id)
extern qelem fh_head;
qelem fh_head = { &fh_head, &fh_head };

static int call_mountd(fh_cache*, unsigned long, fwd_fun, void *);

AUTH *nfs_auth;

static fh_cache *
find_nfs_fhandle_cache(void *idv, int done)
{
	fh_cache *fp, *fp2 = 0;
	/* XXX EVIL XXX */
	int id = (int) ((long)idv);

	ITER(fp, fh_cache, &fh_head) {
		if (fp->fh_id == id) {
			fp2 = fp;
			break;
		}
	}

#ifdef DEBUG
	if (fp2) {
		dlog("fh cache gives fp %#x, fs %s", fp2, fp2->fh_path);
	} else {
		dlog("fh cache search failed");
	}
#endif /* DEBUG */

	if (fp2 && !done) {
		fp2->fh_error = ETIMEDOUT;
		return 0;
	}

	return fp2;
}

/*
 * Called when a filehandle appears
 */
static void
got_nfs_fh(void *pkt, int len, struct sockaddr_in *sa,
    struct sockaddr_in *ia, void *idv, int done)
{
	fh_cache *fp = find_nfs_fhandle_cache(idv, done);
	if (fp) {
		fp->fh_handle.fhs_vers = MOUNTVERS;
		fp->fh_error = pickup_rpc_reply(pkt, len, &fp->fh_handle,
		    xdr_fhstatus);
		if (!fp->fh_error) {
#ifdef DEBUG
			dlog("got filehandle for %s:%s", fp->fh_fs->fs_host, fp->fh_path);
#endif /* DEBUG */
			/*
			 * Wakeup anything sleeping on this filehandle
			 */
			if (fp->fh_wchan) {
#ifdef DEBUG
				dlog("Calling wakeup on %#x", fp->fh_wchan);
#endif /* DEBUG */
				wakeup(fp->fh_wchan);
			}
		}
	}
}

void
flush_nfs_fhandle_cache(fserver *fs)
{
	fh_cache *fp;
	ITER(fp, fh_cache, &fh_head) {
		if (fp->fh_fs == fs || fs == 0) {
			fp->fh_sin.sin_port = (u_short) 0;
			fp->fh_error = -1;
		}
	}
}

static void
discard_fh(void *arg)
{
	fh_cache *fp = arg;

	rem_que(&fp->fh_q);
#ifdef DEBUG
	dlog("Discarding filehandle for %s:%s", fp->fh_fs->fs_host, fp->fh_path);
#endif /* DEBUG */
	free_srvr(fp->fh_fs);
	free(fp->fh_path);
	free(fp);
}

/*
 * Determine the file handle for a node
 */
static int
prime_nfs_fhandle_cache(char *path, fserver *fs, fhstatus *fhbuf, void *wchan)
{
	fh_cache *fp, *fp_save = 0;
	int error;
	int reuse_id = FALSE;

#ifdef DEBUG
	dlog("Searching cache for %s:%s", fs->fs_host, path);
#endif /* DEBUG */

	/*
	 * First search the cache
	 */
	ITER(fp, fh_cache, &fh_head) {
		if (fs == fp->fh_fs && strcmp(path, fp->fh_path) == 0) {
			switch (fp->fh_error) {
			case 0:
				error = fp->fh_error = unx_error(fp->fh_handle.fhs_stat);
				if (error == 0) {
					if (fhbuf)
						bcopy(&fp->fh_handle, fhbuf,
							sizeof(fp->fh_handle));
					if (fp->fh_cid)
						untimeout(fp->fh_cid);
					fp->fh_cid = timeout(FH_TTL,
					    discard_fh, fp);
				} else if (error == EACCES) {
					/*
					 * Now decode the file handle return code.
					 */
					plog(XLOG_INFO, "Filehandle denied for \"%s:%s\"",
						fs->fs_host, path);
				} else {
					errno = error;	/* XXX */
					plog(XLOG_INFO, "Filehandle error for \"%s:%s\": %m",
						fs->fs_host, path);
				}

				/*
				 * The error was returned from the remote mount daemon.
				 * Policy: this error will be cached for now...
				 */
				return error;

			case -1:
				/*
				 * Still thinking about it, but we can re-use.
				 */
				fp_save = fp;
				reuse_id = TRUE;
				break;

			default:
				/*
				 * Return the error.
				 * Policy: make sure we recompute if required again
				 * in case this was caused by a network failure.
				 * This can thrash mountd's though...  If you find
				 * your mountd going slowly then:
				 * 1.  Add a fork() loop to main.
				 * 2.  Remove the call to innetgr() and don't use
				 *     netgroups, especially if you don't use YP.
				 */
				error = fp->fh_error;
				fp->fh_error = -1;
				return error;
			}
			break;
		}
	}

	/*
	 * Not in cache
	 */
	if (fp_save) {
		fp = fp_save;
		/*
		 * Re-use existing slot
		 */
		untimeout(fp->fh_cid);
		free_srvr(fp->fh_fs);
		free(fp->fh_path);
	} else {
		fp = ALLOC(fh_cache);
		bzero(fp, sizeof(*fp));
		ins_que(&fp->fh_q, &fh_head);
	}
	if (!reuse_id)
		fp->fh_id = FHID_ALLOC();
	fp->fh_wchan = wchan;
	fp->fh_error = -1;
	fp->fh_cid = timeout(FH_TTL, discard_fh, fp);

	/*
	 * If the address has changed then don't try to re-use the
	 * port information
	 */
	if (fp->fh_sin.sin_addr.s_addr != fs->fs_ip->sin_addr.s_addr) {
		fp->fh_sin = *fs->fs_ip;
		fp->fh_sin.sin_port = 0;
	}
	fp->fh_fs = dup_srvr(fs);
	fp->fh_path = strdup(path);

	error = call_mountd(fp, MOUNTPROC_MNT, got_nfs_fh, wchan);
	if (error) {
		/*
		 * Local error - cache for a short period
		 * just to prevent thrashing.
		 */
		untimeout(fp->fh_cid);
		fp->fh_cid = timeout(error < 0 ? 2 * ALLOWED_MOUNT_TIME : FH_TTL_ERROR,
						discard_fh, fp);
		fp->fh_error = error;
	} else {
		error = fp->fh_error;
	}
	return error;
}

int
make_nfs_auth(void)
{
	/*
	 * From: Chris Metcalf <metcalf@masala.lcs.mit.edu>
	 * Use hostd, not just hostname.  Note that uids
	 * and gids and the gidlist are type *int* and not the
	 * system uid_t and gid_t types.
	 */
	static int group_wheel = 0;
	nfs_auth = authunix_create(hostd, 0, 0, 1, &group_wheel);
	if (!nfs_auth)
		return ENOBUFS;
	return 0;
}

static int
call_mountd(fh_cache *fp, u_long proc, fwd_fun f, void *wchan)
{
	struct rpc_msg mnt_msg;
	int len;
	char iobuf[8192];
	int error;

	if (!nfs_auth) {
		error = make_nfs_auth();
		if (error)
			return error;
	}

	if (fp->fh_sin.sin_port == 0) {
		u_short port;
		error = nfs_srvr_port(fp->fh_fs, &port, wchan);
		if (error)
			return error;
		fp->fh_sin.sin_port = port;
	}

	rpc_msg_init(&mnt_msg, MOUNTPROG, MOUNTVERS, (unsigned long) 0);
	len = make_rpc_packet(iobuf, sizeof(iobuf), proc,
			&mnt_msg, &fp->fh_path, xdr_nfspath,  nfs_auth);

	/*
	 * XXX EVIL!  We cast fh_id to a pointer, then back to an int
	 * XXX later.
	 */
	if (len > 0) {
		error = fwd_packet(MK_RPC_XID(RPC_XID_MOUNTD, fp->fh_id),
			iobuf, len, &fp->fh_sin, &fp->fh_sin,
			(void *)((long)fp->fh_id), f);
	} else {
		error = -len;
	}
/*
 * It may be the case that we're sending to the wrong MOUNTD port.  This
 * occurs if mountd is restarted on the server after the port has been
 * looked up and stored in the filehandle cache somewhere.  The correct
 * solution, if we're going to cache port numbers is to catch the ICMP
 * port unreachable reply from the server and cause the portmap request
 * to be redone.  The quick solution here is to invalidate the MOUNTD
 * port.
 */
	fp->fh_sin.sin_port = 0;

	return error;
}

/*-------------------------------------------------------------------------*/

/*
 * NFS needs the local filesystem, remote filesystem
 * remote hostname.
 * Local filesystem defaults to remote and vice-versa.
 */
static char *
nfs_match(am_opts *fo)
{
	char *xmtab;
	if (fo->opt_fs && !fo->opt_rfs)
		fo->opt_rfs = fo->opt_fs;
	if (!fo->opt_rfs) {
		plog(XLOG_USER, "nfs: no remote filesystem specified");
		return FALSE;
	}
	if (!fo->opt_rhost) {
		plog(XLOG_USER, "nfs: no remote host specified");
		return FALSE;
	}
	/*
	 * Determine magic cookie to put in mtab
	 */
	xmtab = xmalloc(strlen(fo->opt_rhost) + strlen(fo->opt_rfs) + 2);
	snprintf(xmtab, strlen(fo->opt_rhost) + strlen(fo->opt_rfs) + 2,
		"%s:%s", fo->opt_rhost, fo->opt_rfs);
#ifdef DEBUG
	dlog("NFS: mounting remote server \"%s\", remote fs \"%s\" on \"%s\"",
		fo->opt_rhost, fo->opt_rfs, fo->opt_fs);
#endif /* DEBUG */

	return xmtab;
}

/*
 * Initialise am structure for nfs
 */
static int
nfs_init(mntfs *mf)
{
	if (!mf->mf_private) {
		int error;
		fhstatus fhs;

		char *colon = strchr(mf->mf_info, ':');
		if (colon == 0)
			return ENOENT;

		error = prime_nfs_fhandle_cache(colon+1, mf->mf_server,
		    &fhs, mf);
		if (!error) {
			mf->mf_private = ALLOC(fhstatus);
			mf->mf_prfree = free;
			bcopy(&fhs, mf->mf_private, sizeof(fhs));
		}
		return error;
	}

	return 0;
}

int
mount_nfs_fh(fhstatus *fhp, char *dir, char *fs_name, char *opts,
    mntfs *mf)
{
	struct nfs_args nfs_args;
	struct mntent mnt;
	int retry;
	char *colon;
	/*char *path;*/
	char host[HOST_NAME_MAX+1 + PATH_MAX+2];
	fserver *fs = mf->mf_server;
	int flags;
	char *xopts;
	int error;
#ifdef notdef
	unsigned short port;
#endif /* notdef */

	const char *type = MOUNT_NFS;

	bzero(&nfs_args, sizeof(nfs_args));	/* Paranoid */

	/*
	 * Extract host name to give to kernel
	 */
	if (!(colon = strchr(fs_name, ':')))
		return ENOENT;
	strlcpy(host, fs_name, sizeof(host));
	/*path = colon + 1;*/

	if (mf->mf_remopts && *mf->mf_remopts && !islocalnet(fs->fs_ip->sin_addr.s_addr))
		xopts = strdup(mf->mf_remopts);
	else
		xopts = strdup(opts);

	bzero(&nfs_args, sizeof(nfs_args));

	mnt.mnt_dir = dir;
	mnt.mnt_fsname = fs_name;
	mnt.mnt_type = "nfs";
	mnt.mnt_opts = xopts;
	mnt.mnt_freq = 0;
	mnt.mnt_passno = 0;

	retry = hasmntval(&mnt, "retry");
	if (retry <= 0)
		retry = 1;	/* XXX */

/*again:*/

	/*
	 * set mount args
	 */
	nfs_args.fh = (void *)fhp->fhs_fhandle;
	nfs_args.fhsize = fhp->fhs_size;
	nfs_args.version = NFS_ARGSVERSION;

	nfs_args.hostname = host;
#ifdef HOSTNAMESZ
	/*
	 * Most kernels have a name length restriction.
	 */
	if (strlen(host) >= HOSTNAMESZ)
		strlcpy(host + HOSTNAMESZ - 3, "..", sizeof host - HOSTNAMESZ + 3);
#endif /* HOSTNAMESZ */

	if ((nfs_args.rsize = hasmntval(&mnt, "rsize")))
		nfs_args.flags |= NFSMNT_RSIZE;

#ifdef NFSMNT_READDIRSIZE
	if ((nfs_args.readdirsize = hasmntval(&mnt, "readdirsize"))) {
		nfs_args.flags |= NFSMNT_READDIRSIZE;
	} else if (nfs_args.rsize) {
		nfs_args.readdirsize = nfs_args.rsize;
		nfs_args.flags |= NFSMNT_READDIRSIZE;
	}
#endif

	if ((nfs_args.wsize = hasmntval(&mnt, "wsize")))
		nfs_args.flags |= NFSMNT_WSIZE;

	if ((nfs_args.timeo = hasmntval(&mnt, "timeo")))
		nfs_args.flags |= NFSMNT_TIMEO;

	if ((nfs_args.retrans = hasmntval(&mnt, "retrans")))
		nfs_args.flags |= NFSMNT_RETRANS;

#ifdef NFSMNT_BIODS
	if ((nfs_args.biods = hasmntval(&mnt, "biods")))
		nfs_args.flags |= NFSMNT_BIODS;

#endif /* NFSMNT_BIODS */

#ifdef NFSMNT_MAXGRPS
	if ((nfs_args.maxgrouplist = hasmntval(&mnt, "maxgroups")))
		nfs_args.flags |= NFSMNT_MAXGRPS;
#endif /* NFSMNT_MAXGRPS */

#ifdef NFSMNT_READAHEAD
	if ((nfs_args.readahead = hasmntval(&mnt, "readahead")))
		nfs_args.flags |= NFSMNT_READAHEAD;
#endif /* NFSMNT_READAHEAD */

#ifdef notdef
/*
 * This isn't supported by the ping algorithm yet.
 * In any case, it is all done in nfs_init().
 */
	if ((port = hasmntval(&mnt, "port")))
		sin.sin_port = htons(port);
	else
		sin.sin_port = htons(NFS_PORT);	/* XXX should use portmapper */
#endif /* notdef */

	if (hasmntopt(&mnt, "soft") != NULL)
		nfs_args.flags |= NFSMNT_SOFT;

#ifdef NFSMNT_SPONGY
	if (hasmntopt(&mnt, "spongy") != NULL) {
		nfs_args.flags |= NFSMNT_SPONGY;
		if (nfs_args.flags & NFSMNT_SOFT) {
			plog(XLOG_USER, "Mount opts soft and spongy are incompatible - soft ignored");
			nfs_args.flags &= ~NFSMNT_SOFT;
		}
	}
#endif /* MNTOPT_SPONGY */

	if (hasmntopt(&mnt, "intr") != NULL)
		nfs_args.flags |= NFSMNT_INT;

#ifdef MNTOPT_NODEVS
	if (hasmntopt(&mnt, MNTOPT_NODEVS) != NULL)
		nfs_args.flags |= NFSMNT_NODEVS;
#endif /* MNTOPT_NODEVS */


	if (hasmntopt(&mnt, "noconn") != NULL)
		nfs_args.flags |= NFSMNT_NOCONN;

	if (hasmntopt(&mnt, "resvport") != NULL)
		nfs_args.flags |= NFSMNT_RESVPORT;

#ifdef NFSMNT_PGTHRESH
	if ((nfs_args.pg_thresh = hasmntval(&mnt, "pgthresh")))
		nfs_args.flags |= NFSMNT_PGTHRESH;
#endif /* NFSMNT_PGTHRESH */

	nfs_args.addr = (struct sockaddr *)fs->fs_ip;
	nfs_args.addrlen = sizeof(*fs->fs_ip);
	nfs_args.sotype = SOCK_DGRAM;
	nfs_args.proto = 0;

	flags = compute_mount_flags(&mnt);

#ifdef NFSMNT_NOCTO
	if (hasmntopt(&mnt, "nocto") != NULL)
		nfs_args.flags |= NFSMNT_NOCTO;
#endif /* NFSMNT_NOCTO */

	if (hasmntopt(&mnt, "tcp") != NULL)
		nfs_args.sotype = SOCK_STREAM;



	error = mount_fs(&mnt, flags, (caddr_t) &nfs_args, retry, type);
	free(xopts);
	return error;
}

static int
mount_nfs(char *dir, char *fs_name, char *opts, mntfs *mf)
{
#ifdef notdef
	int error;
	fhstatus fhs;
	char *colon;

	if (!(colon = strchr(fs_name, ':')))
		return ENOENT;

#ifdef DEBUG
	dlog("locating fhandle for %s", fs_name);
#endif /* DEBUG */
	error = prime_nfs_fhandle_cache(colon+1, mf->mf_server, &fhs, NULL);

	if (error)
		return error;

	return mount_nfs_fh(&fhs, dir, fs_name, opts, mf);
#endif
	if (!mf->mf_private) {
		plog(XLOG_ERROR, "Missing filehandle for %s", fs_name);
		return EINVAL;
	}

	return mount_nfs_fh((fhstatus *) mf->mf_private, dir, fs_name, opts, mf);
}

static int
nfs_fmount(mntfs *mf)
{
	int error;

	error = mount_nfs(mf->mf_mount, mf->mf_info, mf->mf_mopts, mf);

#ifdef DEBUG
	if (error) {
		errno = error;
		dlog("mount_nfs: %m");
	}
#endif /* DEBUG */
	return error;
}

static int
nfs_fumount(mntfs *mf)
{
	return (umount_fs(mf->mf_mount));
}

static void
nfs_umounted(am_node *mp)
{

#ifdef KICK_KERNEL
	/* This should go into the mainline code, not in nfs_ops... */

	/*
	 * Run lstat over the underlying directory in
	 * case this was a direct mount.  This will
	 * get the kernel back in sync with reality.
	 */
	if (mp->am_parent && mp->am_parent->am_path &&
	    STREQ(mp->am_parent->am_mnt->mf_ops->fs_type, "direct")) {
		struct stat stb;
		pid_t pid;
		if ((pid = background()) == 0) {
			if (lstat(mp->am_parent->am_path, &stb) < 0) {
				plog(XLOG_ERROR, "lstat(%s) after unmount: %m", mp->am_parent->am_path);
#ifdef DEBUG
			} else {
				dlog("hack lstat(%s): ok", mp->am_parent->am_path);
#endif /* DEBUG */
			}
			_exit(0);
		}
	}
#endif /* KICK_KERNEL */
}

/*
 * Network file system
 */
am_ops nfs_ops = {
	"nfs",
	nfs_match,
	nfs_init,
	auto_fmount,
	nfs_fmount,
	auto_fumount,
	nfs_fumount,
	efs_lookuppn,
	efs_readdir,
	0, /* nfs_readlink */
	0, /* nfs_mounted */
	nfs_umounted,
	find_nfs_srvr,
	FS_MKMNT|FS_BACKGROUND|FS_AMQINFO
};

#endif /* HAS_NFS */
