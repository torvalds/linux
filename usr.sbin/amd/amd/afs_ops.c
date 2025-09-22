/*	$OpenBSD: afs_ops.c,v 1.21 2022/12/28 21:30:15 jmc Exp $	*/

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
 *	from: @(#)afs_ops.c	8.1 (Berkeley) 6/6/93
 */

#include "am.h"

#define NFS
#define NFSCLIENT

#include <unistd.h>
#include <sys/stat.h>

#include "mount.h"

/*
 * Automount file system
 * Direct file system
 * Root file system
 * Top-level file system
 */

/*
 * Interval between forced retries of a mount.
 */
#define RETRY_INTERVAL	2

/*
 * AFS needs nothing in particular.
 */
static char *
afs_match(am_opts *fo)
{
	char *p = fo->opt_rfs;

	if (!fo->opt_rfs) {
		plog(XLOG_USER, "auto: no mount point named (rfs:=)");
		return 0;
	}
	if (!fo->opt_fs) {
		plog(XLOG_USER, "auto: no map named (fs:=)");
		return 0;
	}
	/*
	 * Swap round fs:= and rfs:= options
	 * ... historical (jsp)
	 */
	fo->opt_rfs = fo->opt_fs;
	fo->opt_fs = p;
	/*
	 * mtab entry turns out to be the name of the mount map
	 */
	return strdup(fo->opt_rfs ? fo->opt_rfs : ".");
}

/*
 * Mount an automounter directory.
 * The automounter is connected into the system
 * as a user-level NFS server.  mount_toplvl constructs
 * the necessary NFS parameters to be given to the
 * kernel so that it will talk back to us.
 */
static int
mount_toplvl(char *dir, char *opts)
{
	struct nfs_args nfs_args;
	struct mntent mnt;
	int retry;
	struct sockaddr_in sin;
	unsigned short port;
	int flags;
	nfs_fh *fhp;
	char fs_hostname[HOST_NAME_MAX+1 + PATH_MAX+1];

	const char *type = MOUNT_NFS;

	bzero(&nfs_args, sizeof(nfs_args));	/* Paranoid */

	mnt.mnt_dir = dir;
	mnt.mnt_fsname = pid_fsname;
	mnt.mnt_type = "auto";			/* fake type */
	mnt.mnt_opts = opts;
	mnt.mnt_freq = 0;
	mnt.mnt_passno = 0;

	retry = hasmntval(&mnt, "retry");
	if (retry <= 0)
		retry = 2;	/* XXX */

	/*
	 * get fhandle of remote path for automount point
	 */
	fhp = root_fh(dir);
	if (!fhp) {
		plog(XLOG_FATAL, "Can't find root file handle for %s", dir);
		return EINVAL;
	}

	nfs_args.fh = (void *)fhp;
	nfs_args.fhsize = NFSX_V2FH;
	nfs_args.version = NFS_ARGSVERSION;

	/*
	 * Create sockaddr to point to the local machine.  127.0.0.1
	 * is not used since that will not work in HP-UX clusters and
	 * this is no more expensive.
	 */
	bzero(&sin, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr = myipaddr;
	if ((port = hasmntval(&mnt, "port"))) {
		sin.sin_port = htons(port);
	} else {
		plog(XLOG_ERROR, "no port number specified for %s", dir);
		return EINVAL;
	}

	/*
	 * set mount args
	 */
	nfs_args.addr = (struct sockaddr *)&sin;
	nfs_args.addrlen = sizeof sin;
	nfs_args.sotype = SOCK_DGRAM;
	nfs_args.proto = 0;

	/*
	 * Make a ``hostname'' string for the kernel
	 */
#ifndef HOSTNAMESZ
#define	SHORT_MOUNT_NAME
#endif /* HOSTNAMESZ */
	snprintf(fs_hostname, sizeof(fs_hostname), "amd:%ld",
	    foreground ? (long)mypid : (long)getppid());
	nfs_args.hostname = fs_hostname;
#ifdef HOSTNAMESZ
	/*
	 * Most kernels have a name length restriction.
	 */
	if (strlen(fs_hostname) >= HOSTNAMESZ)
		strlcpy(fs_hostname + HOSTNAMESZ - 3, "..", 3);
#endif /* HOSTNAMESZ */

#ifdef NFSMNT_DUMBTIMR
	nfs_args.flags |= NFSMNT_DUMBTIMR;
	plog(XLOG_INFO, "defeating nfs window computation");
#endif

	/*
	 * Parse a subset of the standard nfs options.  The
	 * others are probably irrelevant for this application
	 */
	if ((nfs_args.timeo = hasmntval(&mnt, "timeo")))
		nfs_args.flags |= NFSMNT_TIMEO;

	if ((nfs_args.retrans = hasmntval(&mnt, "retrans")))
		nfs_args.flags |= NFSMNT_RETRANS;

#ifdef NFSMNT_BIODS
	if (nfs_args.biods = hasmntval(&mnt, "biods"))
		nfs_args.flags |= NFSMNT_BIODS;

#endif /* NFSMNT_BIODS */

#if defined(NFSMNT_ACREGMIN) && defined(NFSMNT_ACREGMAX)
	/*
	 * Don't cache attributes - they are changing under
	 * the kernel's feet...
	 */
	nfs_args.acregmin = nfs_args.acregmax = 1;
	nfs_args.flags |= NFSMNT_ACREGMIN|NFSMNT_ACREGMAX;
#endif /* defined(NFSMNT_ACREGMIN) && defined(NFSMNT_ACREGMAX) */
	/*
	 * These two are constructed internally by the calling routine
	 */
	if (hasmntopt(&mnt, "soft") != NULL)
		nfs_args.flags |= NFSMNT_SOFT;

	if (hasmntopt(&mnt, "intr") != NULL)
		nfs_args.flags |= NFSMNT_INT;

	flags = compute_mount_flags(&mnt);
	return mount_fs(&mnt, flags, (caddr_t) &nfs_args, retry, type);
}

static void
afs_mkcacheref(mntfs *mf)
{
	/*
	 * Build a new map cache for this node, or re-use
	 * an existing cache for the same map.
	 */
	char *cache;
	if (mf->mf_fo && mf->mf_fo->opt_cache)
		cache = mf->mf_fo->opt_cache;
	else
		cache = "none";
	mf->mf_private = mapc_find(mf->mf_info, cache);
	mf->mf_prfree = mapc_free;
}

/*
 * Mount the root...
 */
static int
root_mount(am_node *mp)
{
	mntfs *mf = mp->am_mnt;

	mf->mf_mount = strealloc(mf->mf_mount, pid_fsname);
	mf->mf_private = mapc_find(mf->mf_info, "");
	mf->mf_prfree = mapc_free;

	return 0;
}

/*
 * Mount a sub-mount
 */
static int
afs_mount(am_node *mp)
{
	mntfs *mf = mp->am_mnt;

	/*
	 * Pseudo-directories are used to provide some structure
	 * to the automounted directories instead
	 * of putting them all in the top-level automount directory.
	 *
	 * Here, just increment the parent's link count.
	 */
	mp->am_parent->am_fattr.nlink++;
	/*
	 * Info field of . means use parent's info field.
	 * Historical - not documented.
	 */
	if (mf->mf_info[0] == '.' && mf->mf_info[1] == '\0')
		mf->mf_info = strealloc(mf->mf_info, mp->am_parent->am_mnt->mf_info);
	/*
	 * Compute prefix:
	 *
	 * If there is an option prefix then use that else
	 * If the parent had a prefix then use that with name
	 *	of this node appended else
	 * Use the name of this node.
	 *
	 * That means if you want no prefix you must say so
	 * in the map.
	 */
	if (mf->mf_fo->opt_pref) {
		/*
		 * the prefix specified as an option
		 */
		mp->am_pref = strdup(mf->mf_fo->opt_pref);
	} else {
		/*
		 * else the parent's prefix
		 * followed by the name
		 * followed by /
		 */
		char *ppref = mp->am_parent->am_pref;
		if (ppref == 0)
			ppref = "";
		mp->am_pref = str3cat((char *) 0, ppref, mp->am_name, "/");
	}

	/*
	 * Attach a map cache
	 */
	afs_mkcacheref(mf);

	return 0;
}

/*
 * Mount the top-level
 */
static int
toplvl_mount(am_node *mp)
{
	mntfs *mf = mp->am_mnt;
	struct stat stb;
	char opts[256];
	int error;
	char *mnttype;

	/*
	 * Mounting the automounter.
	 * Make sure the mount directory exists, construct
	 * the mount options and call the mount_toplvl routine.
	 */

	if (stat(mp->am_path, &stb) < 0) {
		return errno;
	} else if ((stb.st_mode & S_IFMT) != S_IFDIR) {
		plog(XLOG_WARNING, "%s is not a directory", mp->am_path);
		return ENOTDIR;
	}

	if (mf->mf_ops == &toplvl_ops) mnttype = "indirect";
	else if (mf->mf_ops == &dfs_ops) mnttype = "direct";
#ifdef HAS_UNION_FS
	else if (mf->mf_ops == &union_ops) mnttype = "union";
#endif
	else mnttype = "auto";

	/*
	 * Construct some mount options
	 */
	snprintf(opts, sizeof(opts),
		"%s,%s,%s=%d,%s=%d,%s=%d,%s",
		"intr",
		"rw",
		"port", nfs_port,
		"timeo", afs_timeo,
		"retrans", afs_retrans,
		mnttype);

	error = mount_toplvl(mf->mf_mount, opts);
	if (error) {
		errno = error;
		plog(XLOG_FATAL, "mount_toplvl: %m");
		return error;
	}

	return 0;
}

static void
toplvl_mounted(mntfs *mf)
{
	afs_mkcacheref(mf);
}

#ifdef HAS_UNION_FS
/*
 * Create a reference to a union'ed entry
 */
static int
create_union_node(char *dir, void *arg)
{
	if (strcmp(dir, "/defaults") != 0) {
		int error = 0;
		(void) toplvl_ops.lookuppn(arg, dir, &error, VLOOK_CREATE);
		if (error > 0) {
			errno = error; /* XXX */
			plog(XLOG_ERROR, "Could not mount %s: %m", dir);
		}
		return error;
	}
	return 0;
}

static void
union_mounted(mntfs *mf)
{
	int i;

	afs_mkcacheref(mf);

	/*
	 * Having made the union mount point,
	 * populate all the entries...
	 */
	for (i = 0; i <= last_used_map; i++) {
		am_node *mp = exported_ap[i];
		if (mp && mp->am_mnt == mf) {
			/* return value from create_union_node is ignored by mapc_keyiter */
			(void) mapc_keyiter((mnt_map *) mp->am_mnt->mf_private,
				(void (*)(char *, void *)) create_union_node, mp);
			break;
		}
	}

#ifdef notdef
	/*
	 * would be nice to flush most of the cache, but we need to
	 * keep the wildcard and /defaults entries...
	 */
	mapc_free(mf->mf_private);
	mf->mf_private = mapc_find(mf->mf_info, "inc");
/*	mapc_add_kv(mf->mf_private, strdup("/defaults"),
		strdup("type:=link;opts:=nounmount;sublink:=${key}")); */
#endif
}
#endif /* HAS_UNION_FS */

/*
 * Unmount an automount sub-node
 */
static int
afs_umount(am_node *mp)
{
	return 0;
}

/*
 * Unmount a top-level automount node
 */
static int
toplvl_umount(am_node *mp)
{
	int error;

	struct stat stb;
again:
	/*
	 * The lstat is needed if this mount is type=direct.
	 * When that happens, the kernel cache gets confused
	 * between the underlying type (dir) and the mounted
	 * type (link) and so needs to be re-synced before
	 * the unmount.  This is all because the unmount system
	 * call follows links and so can't actually unmount
	 * a link (stupid!).  It was noted that doing an ls -ld
	 * of the mount point to see why things were not working
	 * actually fixed the problem - so simulate an ls -ld here.
	 */
	if (lstat(mp->am_path, &stb) < 0) {
#ifdef DEBUG
		dlog("lstat(%s): %m", mp->am_path);
#endif /* DEBUG */
	}
	error = umount_fs(mp->am_path);
	if (error == EBUSY) {
		plog(XLOG_WARNING, "afs_unmount retrying %s in 1s", mp->am_path);
		sleep(1);	/* XXX */
		goto again;
	}

	return error;
}

/*
 * Unmount an automount node
 */
static void
afs_umounted(am_node *mp)
{
	/*
	 * If this is a pseudo-directory then just adjust the link count
	 * in the parent, otherwise call the generic unmount routine
	 */
	if (mp->am_parent && mp->am_parent->am_parent)
		--mp->am_parent->am_fattr.nlink;
}

/*
 * Mounting a file system may take a significant period of time.  The
 * problem is that if this is done in the main process thread then
 * the entire automounter could be blocked, possibly hanging lots of
 * processes on the system.  Instead we use a continuation scheme to
 * allow mounts to be attempted in a sub-process.  When the sub-process
 * exits we pick up the exit status (by convention a UN*X error number)
 * and continue in a notifier.  The notifier gets handed a data structure
 * and can then determine whether the mount was successful or not.  If
 * not, it updates the data structure and tries again until there are no
 * more ways to try the mount, or some other permanent error occurs.
 * In the mean time no RPC reply is sent, even after the mount is successful.
 * We rely on the RPC retry mechanism to resend the lookup request which
 * can then be handled.
 */


struct continuation {
	char **ivec;		/* Current mount info */
	am_node *mp;		/* Node we are trying to mount */
	char *key;		/* Map key */
	char *info;		/* Info string */
	char **xivec;		/* Saved strsplit vector */
	char *auto_opts;	/* Automount options */
	am_opts fs_opts;	/* Filesystem options */
	char *def_opts;		/* Default automount options */
	int retry;		/* Try again? */
	int tried;		/* Have we tried any yet? */
	time_t start;		/* Time we started this mount */
	int callout;		/* Callout identifier */
};

#define	IN_PROGRESS(cp) ((cp)->mp->am_mnt->mf_flags & MFF_MOUNTING)

/*
 * Discard an old continuation
 */
static void
free_continuation(struct continuation *cp)
{
	if (cp->callout)
		untimeout(cp->callout);
	free(cp->key);
	free(cp->xivec);
	free(cp->info);
	free(cp->auto_opts);
	free(cp->def_opts);
	free_opts(&cp->fs_opts);
	free(cp);
}

static int afs_bgmount(struct continuation *, int);

/*
 * Discard the underlying mount point and replace
 * with a reference to an error filesystem.
 */
static void
assign_error_mntfs(am_node *mp)
{
	if (mp->am_error > 0) {
		/*
		 * Save the old error code
		 */
		int error = mp->am_error;
		if (error <= 0)
			error = mp->am_mnt->mf_error;
		/*
		 * Discard the old filesystem
		 */
		free_mntfs(mp->am_mnt);
		/*
		 * Allocate a new error reference
		 */
		mp->am_mnt = new_mntfs();
		/*
		 * Put back the error code
		 */
		mp->am_mnt->mf_error = error;
		mp->am_mnt->mf_flags |= MFF_ERROR;
		/*
		 * Zero the error in the mount point
		 */
		mp->am_error = 0;
	}
}

/*
 * The continuation function.  This is called by
 * the task notifier when a background mount attempt
 * completes.
 */
static void
afs_cont(int rc, int term, void *closure)
{
	struct continuation *cp = (struct continuation *) closure;
	mntfs *mf = cp->mp->am_mnt;

	/*
	 * Definitely not trying to mount at the moment
	 */
	mf->mf_flags &= ~MFF_MOUNTING;
	/*
	 * While we are mounting - try to avoid race conditions
	 */
	new_ttl(cp->mp);

	/*
	 * Wakeup anything waiting for this mount
	 */
	wakeup(mf);

	/*
	 * Check for termination signal or exit status...
	 */
	if (rc || term) {
		am_node *xmp;

		if (term) {
			/*
			 * Not sure what to do for an error code.
			 */
			mf->mf_error = EIO;	/* XXX ? */
			mf->mf_flags |= MFF_ERROR;
			plog(XLOG_ERROR, "mount for %s got signal %d", cp->mp->am_path, term);
		} else {
			/*
			 * Check for exit status...
			 */
			mf->mf_error = rc;
			mf->mf_flags |= MFF_ERROR;
			errno = rc;	/* XXX */
			plog(XLOG_ERROR, "%s: mount (afs_cont): %m", cp->mp->am_path);
		}

		/*
		 * If we get here then that attempt didn't work, so
		 * move the info vector pointer along by one and
		 * call the background mount routine again
		 */
		amd_stats.d_merr++;
		cp->ivec++;
		xmp = cp->mp;
		(void) afs_bgmount(cp, 0);
		assign_error_mntfs(xmp);
	} else {
		/*
		 * The mount worked.
		 */
		am_mounted(cp->mp);
		free_continuation(cp);
	}

	reschedule_timeout_mp();
}

/*
 * Retry a mount
 */
static void
afs_retry(int rc, int term, void *closure)
{
	struct continuation *cp = (struct continuation *) closure;
	int error = 0;

#ifdef DEBUG
	dlog("Commencing retry for mount of %s", cp->mp->am_path);
#endif /* DEBUG */

	new_ttl(cp->mp);

	if ((cp->start + ALLOWED_MOUNT_TIME) < clocktime()) {
		/*
		 * The entire mount has timed out.
		 * Set the error code and skip past
		 * all the info vectors so that
		 * afs_bgmount will not have any more
		 * ways to try the mount, so causing
		 * an error.
		 */
		plog(XLOG_INFO, "mount of \"%s\" has timed out", cp->mp->am_path);
		error = ETIMEDOUT;
		while (*cp->ivec)
			cp->ivec++;
	}

	if (error || !IN_PROGRESS(cp)) {
		(void) afs_bgmount(cp, error);
	}
	reschedule_timeout_mp();
}

/*
 * Try to mount a file system.  Can be called
 * directly or in a sub-process by run_task
 */
static int
try_mount(void *mvp)
{
	/*
	 * Mount it!
	 */
	int error;
	am_node *mp = (am_node *) mvp;
	mntfs *mf = mp->am_mnt;

	/*
	 * If the directory is not yet made and
	 * it needs to be made, then make it!
	 * This may be run in a background process
	 * in which case the flag setting won't be
	 * noticed later - but it is set anyway
	 * just after run_task is called.  It
	 * should probably go away totally...
	 */
	if (!(mf->mf_flags & MFF_MKMNT) && mf->mf_ops->fs_flags & FS_MKMNT) {
		error = mkdirs(mf->mf_mount, 0555);
		if (!error)
			mf->mf_flags |= MFF_MKMNT;
	}

	error = mount_node(mp);
#ifdef DEBUG
	if (error > 0) {
		errno = error;
		dlog("afs call to mount_node failed: %m");
	}
#endif /* DEBUG */
	return error;
}

/*
 * Pick a file system to try mounting and
 * do that in the background if necessary
 *
For each location:
	if it is new -defaults then
		extract and process
		continue;
	fi
	if it is a cut then
		if a location has been tried then
			break;
		fi
		continue;
	fi
	parse mount location
	discard previous mount location if required
	find matching mounted filesystem
	if not applicable then
		this_error = No such file or directory
		continue
	fi
	if the filesystem failed to be mounted then
		this_error = error from filesystem
	elif the filesystem is mounting or unmounting then
		this_error = -1
	elif the fileserver is down then
		this_error = -1
	elif the filesystem is already mounted
		this_error = 0
		break
	fi
	if no error on this mount then
		this_error = initialise mount point
	fi
	if no error on this mount and mount is delayed then
		this_error = -1
	fi
	if this_error < 0 then
		retry = true
	fi
	if no error on this mount then
		make mount point if required
	fi
	if no error on this mount then
		if mount in background then
			run mount in background
			return -1
		else
			this_error = mount in foreground
		fi
	fi
	if an error occured on this mount then
		update stats
		save error in mount point
	fi
endfor
 */

static int
afs_bgmount(struct continuation *cp, int mpe)
{
	mntfs *mf = cp->mp->am_mnt;	/* Current mntfs */
	mntfs *mf_retry = 0;		/* First mntfs which needed retrying */
	int this_error = -1;		/* Per-mount error */
	int hard_error = -1;
	int mp_error = mpe;

	/*
	 * Try to mount each location.
	 * At the end:
	 * hard_error == 0 indicates something was mounted.
	 * hard_error > 0 indicates everything failed with a hard error
	 * hard_error < 0 indicates nothing could be mounted now
	 */
	for (; this_error && *cp->ivec; cp->ivec++) {
		am_ops *p;
		am_node *mp = cp->mp;
		char *link_dir;
		int dont_retry;

		if (hard_error < 0)
			hard_error = this_error;

		this_error = -1;

		if (**cp->ivec == '-') {
			/*
			 * Pick up new defaults
			 */
			if (cp->auto_opts && *cp->auto_opts)
				cp->def_opts = str3cat(cp->def_opts, cp->auto_opts, ";", *cp->ivec+1);
			else
				cp->def_opts = strealloc(cp->def_opts, *cp->ivec+1);
#ifdef DEBUG
			dlog("Setting def_opts to \"%s\"", cp->def_opts);
#endif /* DEBUG */
			continue;
		}

		/*
		 * If a mount has been attempted, and we find
		 * a cut then don't try any more locations.
		 */
		if (strcmp(*cp->ivec, "/") == 0 || strcmp(*cp->ivec, "||") == 0) {
			if (cp->tried) {
#ifdef DEBUG
				dlog("Cut: not trying any more locations for %s",
					mp->am_path);
#endif /* DEBUG */
				break;
			}
			continue;
		}

#ifdef SUNOS4_COMPAT
#ifdef nomore
		/*
		 * By default, you only get this bit on SunOS4.
		 * If you want this anyway, then define SUNOS4_COMPAT
		 * in the relevant "os-blah.h" file.
		 *
		 * We make the observation that if the local key line contains
		 * no '=' signs then either it is sick, or it is a SunOS4-style
		 * "host:fs[:link]" line.  In the latter case the am_opts field
		 * is also assumed to be in old-style, so you can't mix & match.
		 * You can use ${} expansions for the fs and link bits though...
		 *
		 * Actually, this doesn't really cover all the possibilities for
		 * the latest SunOS automounter and it is debatable whether there
		 * is any point bothering.
		 */
		if (strchr(*cp->ivec, '=') == 0)
			p = sunos4_match(&cp->fs_opts, *cp->ivec, cp->def_opts, mp->am_path, cp->key, mp->am_parent->am_mnt->mf_info);
		else
#endif
#endif /* SUNOS4_COMPAT */
			p = ops_match(&cp->fs_opts, *cp->ivec, cp->def_opts, mp->am_path, cp->key, mp->am_parent->am_mnt->mf_info);

		/*
		 * Find a mounted filesystem for this node.
		 */
		mp->am_mnt = mf = realloc_mntfs(mf, p, &cp->fs_opts, cp->fs_opts.opt_fs,
			cp->fs_opts.fs_mtab, cp->auto_opts, cp->fs_opts.opt_opts, cp->fs_opts.opt_remopts);

		p = mf->mf_ops;
#ifdef DEBUG
		dlog("Got a hit with %s", p->fs_type);
#endif /* DEBUG */
		/*
		 * Note whether this is a real mount attempt
		 */
		if (p == &efs_ops) {
			plog(XLOG_MAP, "Map entry %s for %s failed to match", *cp->ivec, mp->am_path);
			if (this_error <= 0)
				this_error = ENOENT;
			continue;
		} else {
			if (cp->fs_opts.fs_mtab) {
				plog(XLOG_MAP, "Trying mount of %s on %s fstype %s",
					cp->fs_opts.fs_mtab, mp->am_path, p->fs_type);
			}
			cp->tried = TRUE;
		}

		this_error = 0;
		dont_retry = FALSE;

		if (mp->am_link) {
			free(mp->am_link);
			mp->am_link = 0;
		}

		link_dir = mf->mf_fo->opt_sublink;

		if (link_dir && *link_dir) {
			if (*link_dir == '/') {
				mp->am_link = strdup(link_dir);
			} else {
				mp->am_link = str3cat((char *) 0,
					mf->mf_fo->opt_fs, "/", link_dir);
				normalize_slash(mp->am_link);
			}
		}

		if (mf->mf_error > 0) {
			this_error = mf->mf_error;
		} else if (mf->mf_flags & (MFF_MOUNTING|MFF_UNMOUNTING)) {
			/*
			 * Still mounting - retry later
			 */
#ifdef DEBUG
			dlog("Duplicate pending mount fstype %s", p->fs_type);
#endif /* DEBUG */
			this_error = -1;
		} else if (FSRV_ISDOWN(mf->mf_server)) {
			/*
			 * Would just mount from the same place
			 * as a hung mount - so give up
			 */
#ifdef DEBUG
			dlog("%s is already hung - giving up", mf->mf_mount);
#endif /* DEBUG */
			mp_error = EWOULDBLOCK;
			dont_retry = TRUE;
			this_error = -1;
		} else if (mf->mf_flags & MFF_MOUNTED) {
#ifdef DEBUG
			dlog("duplicate mount of \"%s\" ...", mf->mf_info);
#endif /* DEBUG */
			/*
			 * Just call mounted()
			 */
			am_mounted(mp);

			this_error = 0;
			break;
		}

		/*
		 * Will usually need to play around with the mount nodes
		 * file attribute structure.  This must be done here.
		 * Try and get things initialised, even if the fileserver
		 * is not known to be up.  In the common case this will
		 * progress things faster.
		 */
		if (!this_error) {
			/*
			 * Fill in attribute fields.
			 */
			if (mf->mf_ops->fs_flags & FS_DIRECTORY)
				mk_fattr(mp, NFDIR);
			else
				mk_fattr(mp, NFLNK);

			mp->am_fattr.fileid = mp->am_gen;

			if (p->fs_init)
				this_error = (*p->fs_init)(mf);
		}

		/*
		 * Make sure the fileserver is UP before doing any more work
		 */
		if (!FSRV_ISUP(mf->mf_server)) {
#ifdef DEBUG
			dlog("waiting for server %s to become available", mf->mf_server->fs_host);
#endif
			this_error =  -1;
		}

		if (!this_error && mf->mf_fo->opt_delay) {
			/*
			 * If there is a delay timer on the mount
			 * then don't try to mount if the timer
			 * has not expired.
			 */
			int i = atoi(mf->mf_fo->opt_delay);
			if (i > 0 && clocktime() < (cp->start + i)) {
#ifdef DEBUG
				dlog("Mount of %s delayed by %ds", mf->mf_mount, i - clocktime() + cp->start);
#endif /* DEBUG */
				this_error = -1;
			}
		}

		if (this_error < 0 && !dont_retry) {
			if (!mf_retry)
				mf_retry = dup_mntfs(mf);
			cp->retry = TRUE;
		}

		if (!this_error) {
			if ((p->fs_flags & FS_MBACKGROUND)) {
				mf->mf_flags |= MFF_MOUNTING;	/*XXX*/
#ifdef DEBUG
				dlog("backgrounding mount of \"%s\"", mf->mf_mount);
#endif /* DEBUG */
				if (cp->callout) {
					untimeout(cp->callout);
					cp->callout = 0;
				}
				run_task(try_mount, mp, afs_cont, cp);
				mf->mf_flags |= MFF_MKMNT;	/* XXX */
				if (mf_retry) free_mntfs(mf_retry);
				return -1;
			} else {
#ifdef DEBUG
				dlog("foreground mount of \"%s\" ...", mf->mf_info);
#endif /* DEBUG */
				this_error = try_mount(mp);
				if (this_error < 0) {
					if (!mf_retry)
						mf_retry = dup_mntfs(mf);
					cp->retry = TRUE;
				}
			}
		}

		if (this_error >= 0) {
			if (this_error > 0) {
				amd_stats.d_merr++;
				if (mf != mf_retry) {
					mf->mf_error = this_error;
					mf->mf_flags |= MFF_ERROR;
				}
			}
			/*
			 * Wakeup anything waiting for this mount
			 */
			wakeup(mf);
		}
	}

	if (this_error && cp->retry) {
		free_mntfs(mf);
		mf = cp->mp->am_mnt = mf_retry;
		/*
		 * Not retrying again (so far)
		 */
		cp->retry = FALSE;
		cp->tried = FALSE;
		/*
		 * Start at the beginning.
		 * Rewind the location vector and
		 * reset the default options.
		 */
		cp->ivec = cp->xivec;
		cp->def_opts = strealloc(cp->def_opts, cp->auto_opts);
		/*
		 * Arrange that afs_bgmount is called
		 * after anything else happens.
		 */
#ifdef DEBUG
		dlog("Arranging to retry mount of %s", cp->mp->am_path);
#endif /* DEBUG */
		sched_task(afs_retry, cp, mf);
		if (cp->callout)
			untimeout(cp->callout);
		cp->callout = timeout(RETRY_INTERVAL, wakeup, mf);

		cp->mp->am_ttl = clocktime() + RETRY_INTERVAL;

		/*
		 * Not done yet - so don't return anything
		 */
		return -1;
	}

	if (hard_error < 0 || this_error == 0)
		hard_error = this_error;

	/*
	 * Discard handle on duff filesystem.
	 * This should never happen since it
	 * should be caught by the case above.
	 */
	if (mf_retry) {
		if (hard_error)
			plog(XLOG_ERROR, "discarding a retry mntfs for %s", mf_retry->mf_mount);
		free_mntfs(mf_retry);
	}

	/*
	 * If we get here, then either the mount succeeded or
	 * there is no more mount information available.
	 */
	if (hard_error < 0 && mp_error)
		hard_error = cp->mp->am_error = mp_error;
	if (hard_error > 0) {
		/*
		 * Set a small(ish) timeout on an error node if
		 * the error was not a time out.
		 */
		switch (hard_error) {
		case ETIMEDOUT:
		case EWOULDBLOCK:
			cp->mp->am_timeo = 5;
			break;
		default:
			cp->mp->am_timeo = 17;
			break;
		}
		new_ttl(cp->mp);
	}

	/*
	 * Make sure that the error value in the mntfs has a
	 * reasonable value.
	 */
	if (mf->mf_error < 0) {
		mf->mf_error = hard_error;
		if (hard_error)
			mf->mf_flags |= MFF_ERROR;
	}

	/*
	 * In any case we don't need the continuation any more
	 */
	free_continuation(cp);

	return hard_error;
}

/*
 * Automount interface to RPC lookup routine
 */
static am_node *
afs_lookuppn(am_node *mp, char *fname, int *error_return, int op)
{
#define ereturn(x) { *error_return = x; return 0; }

	/*
	 * Find the corresponding entry and return
	 * the file handle for it.
	 */
	am_node *ap, *new_mp, *ap_hung;
	char *info;			/* Mount info - where to get the file system */
	char **ivec, **xivec;		/* Split version of info */
	char *auto_opts;		/* Automount options */
	int error = 0;			/* Error so far */
	char path_name[PATH_MAX];	/* General path name buffer */
	char *pfname;			/* Path for database lookup */
	struct continuation *cp;	/* Continuation structure if we need to mount */
	int in_progress = 0;		/* # of (un)mount in progress */
	char *dflts;
	mntfs *mf;

#ifdef DEBUG
	dlog("in afs_lookuppn");
#endif /* DEBUG */

	/*
	 * If the server is shutting down
	 * then don't return information
	 * about the mount point.
	 */
	if (amd_state == Finishing) {
#ifdef DEBUG
		if ((mf = mp->am_mnt) == 0 || mf->mf_ops == &dfs_ops)
			dlog("%s mount ignored - going down", fname);
		else
			dlog("%s/%s mount ignored - going down", mp->am_path, fname);
#endif /* DEBUG */
		ereturn(ENOENT);
	}

	/*
	 * Handle special case of "." and ".."
	 */
	if (fname[0] == '.') {
		if (fname[1] == '\0')
			return mp;	/* "." is the current node */
		if (fname[1] == '.' && fname[2] == '\0') {
			if (mp->am_parent) {
#ifdef DEBUG
				dlog(".. in %s gives %s", mp->am_path, mp->am_parent->am_path);
#endif /* DEBUG */
				return mp->am_parent;	/* ".." is the parent node */
			}
			ereturn(ESTALE);
		}
	}

	/*
	 * Check for valid key name.
	 * If it is invalid then pretend it doesn't exist.
	 */
	if (!valid_key(fname)) {
		plog(XLOG_WARNING, "Key \"%s\" contains a disallowed character", fname);
		ereturn(ENOENT);
	}

	/*
	 * Expand key name.
	 * fname is now a private copy.
	 */
	fname = expand_key(fname);

	for (ap_hung = 0, ap = mp->am_child; ap; ap = ap->am_osib) {
		/*
		 * Otherwise search children of this node
		 */
		if (FSTREQ(ap->am_name, fname)) {
			mf = ap->am_mnt;
			if (ap->am_error) {
				error = ap->am_error;
				continue;
			}

			/*
			 * If the error code is undefined then it must be
			 * in progress.
			 */
			if (mf->mf_error < 0)
				goto in_progrss;

			/*
			 * Check for a hung node
			 */
			if (FSRV_ISDOWN(mf->mf_server)) {
#ifdef DEBUG
				dlog("server hung");
#endif /* DEBUG */
				error = ap->am_error;
				ap_hung = ap;
				continue;
			}

			/*
			 * If there was a previous error with this node
			 * then return that error code.
			 */
			if (mf->mf_flags & MFF_ERROR) {
				error = mf->mf_error;
				continue;
			}

			if (!(mf->mf_flags & MFF_MOUNTED) /*|| (mf->mf_flags & MFF_UNMOUNTING)*/) {
in_progrss:
				/*
				 * If the fs is not mounted or it is unmounting then there
				 * is a background (un)mount in progress.  In this case
				 * we just drop the RPC request (return nil) and
				 * wait for a retry, by which time the (un)mount may
				 * have completed.
				 */
#ifdef DEBUG
				dlog("ignoring mount of %s in %s -- in progress",
					fname, mf->mf_mount);
#endif /* DEBUG */
				in_progress++;
				continue;
			}

			/*
			 * Otherwise we have a hit: return the current mount point.
			 */
#ifdef DEBUG
			dlog("matched %s in %s", fname, ap->am_path);
#endif /* DEBUG */
			free(fname);
			return ap;
		}
	}

	if (in_progress) {
#ifdef DEBUG
		dlog("Waiting while %d mount(s) in progress", in_progress);
#endif /* DEBUG */
		free(fname);
		ereturn(-1);
	}

	/*
	 * If an error occured then return it.
	 */
	if (error) {
#ifdef DEBUG
		errno = error; /* XXX */
		dlog("Returning error: %m", error);
#endif /* DEBUG */
		free(fname);
		ereturn(error);
	}

	/*
	 * If doing a delete then don't create again!
	 */
	switch (op) {
	case VLOOK_DELETE:
		ereturn(ENOENT);
		break;

	case VLOOK_CREATE:
		break;

	default:
		plog(XLOG_FATAL, "Unknown op to afs_lookuppn: 0x%x", op);
		ereturn(EINVAL);
		break;
	}

	/*
	 * If the server is going down then just return,
	 * don't try to mount any more file systems
	 */
	if ((int)amd_state >= (int)Finishing) {
#ifdef DEBUG
		dlog("not found - server going down anyway");
#endif /* DEBUG */
		free(fname);
		ereturn(ENOENT);
	}

	/*
	 * If we get there then this is a reference to an,
	 * as yet, unknown name so we need to search the mount
	 * map for it.
	 */
	if (mp->am_pref) {
		snprintf(path_name, sizeof(path_name), "%s%s", mp->am_pref, fname);
		pfname = path_name;
	} else {
		pfname = fname;
	}

	mf = mp->am_mnt;

#ifdef DEBUG
	dlog("will search map info in %s to find %s", mf->mf_info, pfname);
#endif /* DEBUG */
	/*
	 * Consult the oracle for some mount information.
	 * info is malloc'ed and belongs to this routine.
	 * It ends up being free'd in free_continuation().
	 *
	 * Note that this may return -1 indicating that information
	 * is not yet available.
	 */
	error = mapc_search((mnt_map*) mf->mf_private, pfname, &info);
	if (error) {
		if (error > 0)
			plog(XLOG_MAP, "No map entry for %s", pfname);
		else
			plog(XLOG_MAP, "Waiting on map entry for %s", pfname);
		free(fname);
		ereturn(error);
	}

#ifdef DEBUG
	dlog("mount info is %s", info);
#endif /* DEBUG */

	/*
	 * Split info into an argument vector.
	 * The vector is malloc'ed and belongs to
	 * this routine.  It is free'd in free_continuation()
	 */
	xivec = ivec = strsplit(info, ' ', '\"');

	/*
	 * Default error code...
	 */
	if (ap_hung)
		error = EWOULDBLOCK;
	else
		error = ENOENT;

	/*
	 * Allocate a new map
	 */
	new_mp = exported_ap_alloc();
	if (new_mp == 0) {
		free(xivec);
		free(info);
		free(fname);
		ereturn(ENOSPC);
	}

	if (mf->mf_auto)
		auto_opts = mf->mf_auto;
	else
		auto_opts = "";

	auto_opts = strdup(auto_opts);

#ifdef DEBUG
	dlog("searching for /defaults entry");
#endif /* DEBUG */
	if (mapc_search((mnt_map*) mf->mf_private, "/defaults", &dflts) == 0) {
		char *dfl;
		char **rvec;
#ifdef DEBUG
		dlog("/defaults gave %s", dflts);
#endif /* DEBUG */
		if (*dflts == '-')
			dfl = dflts+1;
		else
			dfl = dflts;

		/*
		 * Chop the defaults up
		 */
		rvec = strsplit(dfl, ' ', '\"');
		/*
		 * Extract first value
		 */
		dfl = rvec[0];

		/*
		 * If there were any values at all...
		 */
		if (dfl) {
			/*
			 * Log error if there were other values
			 */
			if (rvec[1]) {
#ifdef DEBUG
				dlog("/defaults chopped into %s", dfl);
#endif /* DEBUG */
				plog(XLOG_USER, "More than a single value for /defaults in %s", mf->mf_info);
			}

			/*
			 * Prepend to existing defaults if they exist,
			 * otherwise just use these defaults.
			 */
			if (*auto_opts && *dfl) {
				char *nopts = xmalloc(strlen(auto_opts)+strlen(dfl)+2);
				snprintf(nopts,
				    strlen(auto_opts) + strlen(dfl) + 2,
				    "%s;%s", dfl, auto_opts);
				free(auto_opts);
				auto_opts = nopts;
			} else if (*dfl) {
				auto_opts = strealloc(auto_opts, dfl);
			}
		}
		free(dflts);
		/*
		 * Don't need info vector any more
		 */
		free(rvec);
	}

	/*
	 * Fill it in
	 */
	init_map(new_mp, fname);

	/*
	 * Put it in the table
	 */
	insert_am(new_mp, mp);

	/*
	 * Fill in some other fields,
	 * path and mount point.
	 *
	 * bugfix: do not prepend old am_path if direct map
	 *         <wls@astro.umd.edu> William Sebok
	 */
	new_mp->am_path = str3cat(new_mp->am_path,
		mf->mf_ops == &dfs_ops ? "" : mp->am_path,
		*fname == '/' ? "" : "/", fname);

#ifdef DEBUG
	dlog("setting path to %s", new_mp->am_path);
#endif /* DEBUG */

	/*
	 * Take private copy of pfname
	 */
	pfname = strdup(pfname);

	/*
	 * Construct a continuation
	 */
	cp = ALLOC(continuation);
	cp->mp = new_mp;
	cp->xivec = xivec;
	cp->ivec = ivec;
	cp->info = info;
	cp->key = pfname;
	cp->auto_opts = auto_opts;
	cp->retry = FALSE;
	cp->tried = FALSE;
	cp->start = clocktime();
	cp->def_opts = strdup(auto_opts);
	bzero(&cp->fs_opts, sizeof(cp->fs_opts));

	/*
	 * Try and mount the file system
	 * If this succeeds immediately (possible
	 * for a ufs file system) then return
	 * the attributes, otherwise just
	 * return an error.
	 */
	error = afs_bgmount(cp, error);
	reschedule_timeout_mp();
	if (!error) {
		free(fname);
		return new_mp;
	}

	if (error && (new_mp->am_mnt->mf_ops == &efs_ops))
		new_mp->am_error = error;

	assign_error_mntfs(new_mp);

	free(fname);

	ereturn(error);
#undef ereturn
}

/*
 * Locate next node in sibling list which is mounted
 * and is not an error node.
 */
static am_node *
next_nonerror_node(am_node *xp)
{
	mntfs *mf;

	/*
	 * Bug report (7/12/89) from Rein Tollevik <rein@ifi.uio.no>
	 * Fixes a race condition when mounting direct automounts.
	 * Also fixes a problem when doing a readdir on a directory
	 * containing hung automounts.
	 */
	while (xp &&
	       (!(mf = xp->am_mnt) ||			/* No mounted filesystem */
	        mf->mf_error != 0 ||			/* There was a mntfs error */
	        xp->am_error != 0 ||			/* There was a mount error */
	        !(mf->mf_flags & MFF_MOUNTED) ||	/* The fs is not mounted */
	        (mf->mf_server->fs_flags & FSF_DOWN))	/* The fs may be down */
		)
		xp = xp->am_osib;

	return xp;
}

static int
afs_readdir(am_node *mp, nfscookie cookie, struct dirlist *dp,
    struct entry *ep, int count)
{
	unsigned int gen = *(unsigned int*) cookie;
	am_node *xp;

	dp->eof = FALSE;

	if (gen == 0) {
		/*
		 * In the default instance (which is used to
		 * start a search) we return "." and "..".
		 *
		 * This assumes that the count is big enough
		 * to allow both "." and ".." to be returned in
		 * a single packet.  If it isn't (which would
		 * be fairly unbelievable) then tough.
		 */
#ifdef DEBUG
		dlog("default search");
#endif /* DEBUG */
		/*
		 * Check for enough room.  This is extremely
		 * approximate but is more than enough space.
		 * Really need 2 times:
		 *	4byte fileid
		 *	4byte cookie
		 *	4byte name length
		 *	4byte name
		 * plus the dirlist structure
		 */
		if (count <
			(2 * (2 * (sizeof(*ep) + sizeof("..") + 4)
					+ sizeof(*dp))))
			return EINVAL;

		xp = next_nonerror_node(mp->am_child);
		dp->entries = ep;

		/* construct "." */
		ep[0].fileid = mp->am_gen;
		ep[0].name = ".";
		ep[0].nextentry = &ep[1];
		*(unsigned int *) ep[0].cookie = 0;

		/* construct ".." */
		if (mp->am_parent)
			ep[1].fileid = mp->am_parent->am_gen;
		else
			ep[1].fileid = mp->am_gen;
		ep[1].name = "..";
		ep[1].nextentry = 0;
		*(unsigned int *) ep[1].cookie =
			xp ? xp->am_gen : ~(unsigned int)0;

		if (!xp) dp->eof = TRUE;
		return 0;
	}

#ifdef DEBUG
	dlog("real child");
#endif /* DEBUG */

	if (gen == ~(unsigned int)0) {
#ifdef DEBUG
		dlog("End of readdir in %s", mp->am_path);
#endif /* DEBUG */
		dp->eof = TRUE;
		dp->entries = 0;
		return 0;
	}

	xp = mp->am_child;
	while (xp && xp->am_gen != gen)
		xp = xp->am_osib;

	if (xp) {
		int nbytes = count / 2;		/* conservative */
		int todo = MAX_READDIR_ENTRIES;
		dp->entries = ep;
		do {
			am_node *xp_next = next_nonerror_node(xp->am_osib);

			if (xp_next) {
				*(unsigned int *) ep->cookie = xp_next->am_gen;
			} else {
				*(unsigned int *) ep->cookie = ~(unsigned int)0;
				dp->eof = TRUE;
			}

			ep->fileid = xp->am_gen;
			ep->name = xp->am_name;
			nbytes -= sizeof(*ep) + strlen(xp->am_name) + 1;

			xp = xp_next;

			if (nbytes > 0 && !dp->eof && todo > 1) {
				ep->nextentry = ep + 1;
				ep++;
				--todo;
			} else {
				todo = 0;
			}
		} while (todo > 0);

		ep->nextentry = 0;

		return 0;
	}

	return ESTALE;

}

static am_node *
dfs_readlink(am_node *mp, int *error_return)
{
	am_node *xp;
	int rc = 0;

	xp = next_nonerror_node(mp->am_child);
	if (!xp) {
		if (!mp->am_mnt->mf_private)
			afs_mkcacheref(mp->am_mnt);	/* XXX */
		xp = afs_lookuppn(mp, mp->am_path+1, &rc, VLOOK_CREATE);
	}

	if (xp) {
		new_ttl(xp);	/* (7/12/89) from Rein Tollevik */
		return xp;
	}
	if (amd_state == Finishing)
		rc = ENOENT;
	*error_return = rc;
	return 0;
}

/*
 * Ops structure
 */
am_ops root_ops = {
	"root",
	0, /* root_match */
	0, /* root_init */
	root_mount,
	0,
	afs_umount,
	0,
	afs_lookuppn,
	afs_readdir,
	0, /* root_readlink */
	0, /* root_mounted */
	0, /* root_umounted */
	find_afs_srvr,
	FS_NOTIMEOUT|FS_AMQINFO|FS_DIRECTORY
};

am_ops afs_ops = {
	"auto",
	afs_match,
	0, /* afs_init */
	afs_mount,
	0,
	afs_umount,
	0,
	afs_lookuppn,
	afs_readdir,
	0, /* afs_readlink */
	0, /* afs_mounted */
	afs_umounted,
	find_afs_srvr,
	FS_AMQINFO|FS_DIRECTORY
};

am_ops toplvl_ops = {
	"toplvl",
	afs_match,
	0, /* afs_init */
	toplvl_mount,
	0,
	toplvl_umount,
	0,
	afs_lookuppn,
	afs_readdir,
	0, /* toplvl_readlink */
	toplvl_mounted,
	0, /* toplvl_umounted */
	find_afs_srvr,
	FS_MKMNT|FS_NOTIMEOUT|FS_BACKGROUND|FS_AMQINFO|FS_DIRECTORY
};

am_ops dfs_ops = {
	"direct",
	afs_match,
	0, /* dfs_init */
	toplvl_mount,
	0,
	toplvl_umount,
	0,
	efs_lookuppn,
	efs_readdir,
	dfs_readlink,
	toplvl_mounted,
	0, /* afs_umounted */
	find_afs_srvr,
	FS_MKMNT|FS_NOTIMEOUT|FS_BACKGROUND|FS_AMQINFO
};

#ifdef HAS_UNION_FS
am_ops union_ops = {
	"union",
	afs_match,
	0, /* afs_init */
	toplvl_mount,
	0,
	toplvl_umount,
	0,
	afs_lookuppn,
	afs_readdir,
	0, /* toplvl_readlink */
	union_mounted,
	0, /* toplvl_umounted */
	find_afs_srvr,
	FS_MKMNT|FS_NOTIMEOUT|FS_BACKGROUND|FS_AMQINFO|FS_DIRECTORY
};
#endif /* HAS_UNION_FS */
