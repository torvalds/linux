/*	$OpenBSD: map.c,v 1.17 2022/12/28 21:30:15 jmc Exp $	*/

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

#include <unistd.h>

/*
 * Generation Numbers.
 *
 * Generation numbers are allocated to every node created
 * by amd.  When a filehandle is computed and sent to the
 * kernel, the generation number makes sure that it is safe
 * to reallocate a node slot even when the kernel has a cached
 * reference to its old incarnation.
 * No garbage collection is done, since it is assumed that
 * there is no way that 2^32 generation numbers could ever
 * be allocated by a single run of amd - there is simply
 * not enough cpu time available.
 */
static unsigned int am_gen = 2;	/* Initial generation number */
#define new_gen() (am_gen++)

am_node **exported_ap = (am_node **) 0;
int exported_ap_size = 0;
int first_free_map = 0;		/* First available free slot */
int last_used_map = -1;		/* Last unavailable used slot */
static int timeout_mp_id;	/* Id from last call to timeout */

/*
 * This is the default attributes field which
 * is copied into every new node to be created.
 * The individual filesystem fs_init() routines
 * patch the copy to represent the particular
 * details for the relevant filesystem type
 */
static struct fattr gen_fattr = {
	NFLNK,				/* type */
	NFSMODE_LNK | 0777,		/* mode */
	1,				/* nlink */
	0,				/* uid */
	0,				/* gid */
	0,				/* size */
	4096,				/* blocksize */
	0,				/* rdev */
	1,				/* blocks */
	0,				/* fsid */
	0,				/* fileid */
	{ 0, 0 },			/* atime */
	{ 0, 0 },			/* mtime */
	{ 0, 0 },			/* ctime */
};

/*
 * Resize exported_ap map
 */
static int
exported_ap_realloc_map(int nsize)
{
	/*
	 * this shouldn't happen, but...
	 */
	if (nsize < 0 || nsize == exported_ap_size)
		return 0;

	exported_ap = xreallocarray(exported_ap, nsize, sizeof *exported_ap);

	if (nsize > exported_ap_size)
		bzero(exported_ap+exported_ap_size,
			(nsize - exported_ap_size) * sizeof(am_node*));
	exported_ap_size = nsize;

	return 1;
}

/*
 * The root of the mount tree.
 */
am_node *root_node;

/*
 * Allocate a new mount slot and create
 * a new node.
 * Fills in the map number of the node,
 * but leaves everything else uninitialised.
 */
am_node *exported_ap_alloc(void)
{
	am_node *mp, **mpp;

	/*
	 * First check if there are any slots left, realloc if needed
	 */
	if (first_free_map >= exported_ap_size)
		if (!exported_ap_realloc_map(exported_ap_size + NEXP_AP))
			return 0;

	/*
	 * Grab the next free slot
	 */
	mpp = exported_ap + first_free_map;
	mp = *mpp = ALLOC(am_node);
	bzero(mp, sizeof(*mp));

	mp->am_mapno = first_free_map++;

	/*
	 * Update free pointer
	 */
	while (first_free_map < exported_ap_size && exported_ap[first_free_map])
		first_free_map++;

	if (first_free_map > last_used_map)
		last_used_map = first_free_map - 1;

	/*
	 * Shrink exported_ap if reasonable
	 */
	if (last_used_map < exported_ap_size - (NEXP_AP + NEXP_AP_MARGIN))
		exported_ap_realloc_map(exported_ap_size - NEXP_AP);

#ifdef DEBUG
	/*dlog("alloc_exp: last_used_map = %d, first_free_map = %d",
		last_used_map, first_free_map);*/
#endif /* DEBUG */

	return mp;
}

/*
 * Free a mount slot
 */
static void
exported_ap_free(am_node *mp)
{
	/*
	 * Sanity check
	 */
	if (!mp)
		return;

	/*
	 * Zero the slot pointer to avoid double free's
	 */
	exported_ap[mp->am_mapno] = 0;

	/*
	 * Update the free and last_used indices
	 */
	if (mp->am_mapno == last_used_map)
		while (last_used_map >= 0 && exported_ap[last_used_map] == 0)
			--last_used_map;

	if (first_free_map > mp->am_mapno)
		first_free_map = mp->am_mapno;

#ifdef DEBUG
	/*dlog("free_exp: last_used_map = %d, first_free_map = %d",
		last_used_map, first_free_map);*/
#endif /* DEBUG */

	/*
	 * Free the mount node
	 */
	free(mp);
}

/*
 * Insert mp into the correct place,
 * where p_mp is its parent node.
 * A new node gets placed as the youngest sibling
 * of any other children, and the parent's child
 * pointer is adjusted to point to the new child node.
 */
void
insert_am(am_node *mp, am_node *p_mp)
{
	/*
	 * If this is going in at the root then flag it
	 * so that it cannot be unmounted by amq.
	 */
	if (p_mp == root_node)
		mp->am_flags |= AMF_ROOT;
	/*
	 * Fill in n-way links
	 */
	mp->am_parent = p_mp;
	mp->am_osib = p_mp->am_child;
	if (mp->am_osib)
		mp->am_osib->am_ysib = mp;
	p_mp->am_child = mp;
}

/*
 * Remove am from its place in the mount tree
 */
static void
remove_am(am_node *mp)
{
	/*
	 * 1.  Consistency check
	 */
	if (mp->am_child && mp->am_parent) {
		plog(XLOG_WARNING, "children of \"%s\" still exist - deleting anyway", mp->am_path);
	}

	/*
	 * 2.  Update parent's child pointer
	 */
	if (mp->am_parent && mp->am_parent->am_child == mp)
		mp->am_parent->am_child = mp->am_osib;

	/*
	 * 3.  Unlink from sibling chain
	 */
	if (mp->am_ysib)
		mp->am_ysib->am_osib = mp->am_osib;
	if (mp->am_osib)
		mp->am_osib->am_ysib = mp->am_ysib;
}

/*
 * Compute a new time to live value for a node.
 */
void
new_ttl(am_node *mp)
{
	mp->am_timeo_w = 0;

	mp->am_ttl = clocktime();
	mp->am_fattr.atime.seconds = mp->am_ttl;
	mp->am_ttl += mp->am_timeo;	/* sun's -tl option */
}

void
mk_fattr(am_node *mp, int vntype)
{
	switch (vntype) {
	case NFDIR:
		mp->am_fattr.type = NFDIR;
		mp->am_fattr.mode = NFSMODE_DIR | 0555;
		mp->am_fattr.nlink = 2;
		mp->am_fattr.size = 512;
		break;
	case NFLNK:
		mp->am_fattr.type = NFLNK;
		mp->am_fattr.mode = NFSMODE_LNK | 0777;
		mp->am_fattr.nlink = 1;
		mp->am_fattr.size = 0;
		break;
	default:
		plog(XLOG_FATAL, "Unknown fattr type %d - ignored", vntype);
		break;
	}
}

/*
 * Initialise an allocated mount node.
 * It is assumed that the mount node was bzero'd
 * before getting here so anything that would
 * be set to zero isn't done here.
 */
void
init_map(am_node *mp, char *dir)
{
	/* mp->am_mapno initialized by exported_ap_alloc */
	mp->am_mnt = new_mntfs();
	mp->am_name = strdup(dir);
	mp->am_path = strdup(dir);
	/*mp->am_link = 0;*/
	/*mp->am_parent = 0;*/
	/*mp->am_ysib = 0;*/
	/*mp->am_osib = 0;*/
	/*mp->am_child = 0;*/
	/*mp->am_flags = 0;*/
	/*mp->am_error = 0;*/
	mp->am_gen = new_gen();
	/*mp->am_pref = 0;*/

	mp->am_timeo = am_timeo;
	mp->am_attr.status = NFS_OK;
	mp->am_fattr = gen_fattr;
	mp->am_fattr.fsid = 42;
	mp->am_fattr.fileid = 0;
	mp->am_fattr.atime.seconds = clocktime();
	mp->am_fattr.atime.useconds = 0;
	mp->am_fattr.mtime = mp->am_fattr.ctime = mp->am_fattr.atime;

	new_ttl(mp);
	mp->am_stats.s_mtime = mp->am_fattr.atime.seconds;
	/*mp->am_private = 0;*/
}

/*
 * Free a mount node.
 * The node must be already unmounted.
 */
void
free_map(am_node *mp)
{
	remove_am(mp);

	free(mp->am_link);
	free(mp->am_name);
	free(mp->am_path);
	free(mp->am_pref);

	if (mp->am_mnt)
		free_mntfs(mp->am_mnt);

	exported_ap_free(mp);
}

/*
 * Convert from file handle to
 * automount node.
 */
am_node *
fh_to_mp3(nfs_fh *fhp, int *rp, int c_or_d)
{
	struct am_fh *fp = (struct am_fh *) fhp;
	am_node *ap = 0;

	/*
	 * Check process id matches
	 * If it doesn't then it is probably
	 * from an old kernel cached filehandle
	 * which is now out of date.
	 */
	if (fp->fhh_pid != mypid)
		goto drop;

	/*
	 * Make sure the index is valid before
	 * exported_ap is referenced.
	 */
	if (fp->fhh_id < 0 || fp->fhh_id >= exported_ap_size)
		goto drop;

	/*
	 * Get hold of the supposed mount node
	 */
	ap = exported_ap[fp->fhh_id];

	/*
	 * If it exists then maybe...
	 */
	if (ap) {
		/*
		 * Check the generation number in the node
		 * matches the one from the kernel.  If not
		 * then the old node has been timed out and
		 * a new one allocated.
		 */
		if (ap->am_gen != fp->fhh_gen) {
			ap = 0;
			goto drop;
		}

		/*
		 * If the node is hung then locate a new node
		 * for it.  This implements the replicated filesystem
		 * retries.
		 */
		if (ap->am_mnt && FSRV_ISDOWN(ap->am_mnt->mf_server) && ap->am_parent) {
			int error;
			am_node *orig_ap = ap;
#ifdef DEBUG
			dlog("fh_to_mp3: %s (%s) is hung:- call lookup",
					orig_ap->am_path, orig_ap->am_mnt->mf_info);
#endif /* DEBUG */
			/*
			 * Update modify time of parent node.
			 * With any luck the kernel will re-stat
			 * the child node and get new information.
			 */
			orig_ap->am_fattr.mtime.seconds = clocktime();

			/*
			 * Call the parent's lookup routine for an object
			 * with the same name.  This may return -1 in error
			 * if a mount is in progress.  In any case, if no
			 * mount node is returned the error code is propagated
			 * to the caller.
			 */
			if (c_or_d == VLOOK_CREATE) {
				ap = (*orig_ap->am_parent->am_mnt->mf_ops->lookuppn)(orig_ap->am_parent,
						orig_ap->am_name, &error, c_or_d);
			} else {
				ap = 0;
				error = ESTALE;
			}
			if (ap == 0) {
				if (error < 0 && amd_state == Finishing)
					error = ENOENT;
				*rp = error;
				return 0;
			}
			/*
			 * Update last access to original node.  This
			 * avoids timing it out and so sending ESTALE
			 * back to the kernel.
			 * XXX - Not sure we need this anymore (jsp, 90/10/6).
			 */
			new_ttl(orig_ap);

		}
		/*
		 * Disallow references to objects being unmounted, unless
		 * they are automount points.
		 */
		if (ap->am_mnt && (ap->am_mnt->mf_flags & MFF_UNMOUNTING) &&
				!(ap->am_flags & AMF_ROOT)) {
			if (amd_state == Finishing)
				*rp = ENOENT;
			else
				*rp = -1;
			return 0;
		}
		new_ttl(ap);
	}

drop:
	if (!ap || !ap->am_mnt) {
		/*
		 * If we are shutting down then it is likely
		 * that this node has disappeared because of
		 * a fast timeout.  To avoid things thrashing
		 * just pretend it doesn't exist at all.  If
		 * ESTALE is returned, some NFS clients just
		 * keep retrying (stupid or what - if it's
		 * stale now, what's it going to be in 5 minutes?)
		 */
		if (amd_state == Finishing)
			*rp = ENOENT;
		else
			*rp = ESTALE;
		amd_stats.d_stale++;
	}

	return ap;
}

am_node *
fh_to_mp(nfs_fh *fhp)
{
	int dummy;
	return fh_to_mp2(fhp, &dummy);
}

/*
 * Convert from automount node to
 * file handle.
 */
void
mp_to_fh(am_node *mp, struct nfs_fh *fhp)
{
	struct am_fh *fp = (struct am_fh *) fhp;

	/*
	 * Take the process id
	 */
	fp->fhh_pid = mypid;
	/*
	 * .. the map number
	 */
	fp->fhh_id = mp->am_mapno;
	/*
	 * .. and the generation number
	 */
	fp->fhh_gen = mp->am_gen;
	/*
	 * .. to make a "unique" triple that will never
	 * be reallocated except across reboots (which doesn't matter)
	 * or if we are unlucky enough to be given the same
	 * pid as a previous amd (very unlikely).
	 */
}

static am_node *
find_ap2(char *dir, am_node *mp)
{
	if (mp) {
		am_node *mp2;
		if (strcmp(mp->am_path, dir) == 0)
			return mp;

		if ((mp->am_mnt->mf_flags & MFF_MOUNTED) &&
			strcmp(mp->am_mnt->mf_mount, dir) == 0)
			return mp;

		mp2 = find_ap2(dir, mp->am_osib);
		if (mp2)
			return mp2;
		return find_ap2(dir, mp->am_child);
	}

	return 0;
}

/*
 * Find the mount node corresponding
 * to dir.  dir can match either the
 * automount path or, if the node is
 * mounted, the mount location.
 */
am_node *
find_ap(char *dir)
{
	int i;

	for (i = last_used_map; i >= 0; --i) {
		am_node *mp = exported_ap[i];
		if (mp && (mp->am_flags & AMF_ROOT)) {
			mp = find_ap2(dir, exported_ap[i]);
			if (mp)
				return mp;
		}
	}

	return 0;
}

/*
 * Find the mount node corresponding
 * to the mntfs structure.
 */
am_node *
find_mf(mntfs *mf)
{
	int i;

	for (i = last_used_map; i >= 0; --i) {
		am_node *mp = exported_ap[i];
		if (mp && mp->am_mnt == mf)
			return mp;
	}
	return 0;
}

/*
 * Get the filehandle for a particular named directory.
 * This is used during the bootstrap to tell the kernel
 * the filehandles of the initial automount points.
 */
nfs_fh *
root_fh(char *dir)
{
	static nfs_fh nfh;
	am_node *mp = root_ap(dir, TRUE);
	if (mp) {
		mp_to_fh(mp, &nfh);
		/*
		 * Patch up PID to match main server...
		 */
		if (!foreground) {
			pid_t pid = getppid();
			((struct am_fh *) &nfh)->fhh_pid = pid;
#ifdef DEBUG
			dlog("root_fh substitutes pid %d", (int)pid);
#endif
		}
		return &nfh;
	}

	/*
	 * Should never get here...
	 */
	plog(XLOG_ERROR, "Can't find root filehandle for %s", dir);
	return 0;
}

am_node *
root_ap(char *dir, int path)
{
	am_node *mp = find_ap(dir);
	if (mp && mp->am_parent == root_node)
		return mp;

	return 0;
}

/*
 * Timeout all nodes waiting on
 * a given Fserver.
 */
void
map_flush_srvr(fserver *fs)
{
	int i;
	int done = 0;

	for (i = last_used_map; i >= 0; --i) {
		am_node *mp = exported_ap[i];
		if (mp && mp->am_mnt && mp->am_mnt->mf_server == fs) {
			plog(XLOG_INFO, "Flushed %s; dependent on %s", mp->am_path, fs->fs_host);
			mp->am_ttl = clocktime();
			done = 1;
		}
	}
	if (done)
		reschedule_timeout_mp();
}

/*
 * Mount a top level automount node
 * by calling lookup in the parent
 * (root) node which will cause the
 * automount node to be automounted.
 */
int
mount_auto_node(char *dir, void *arg)
{
	int error = 0;

	(void) afs_ops.lookuppn((am_node *) arg, dir, &error, VLOOK_CREATE);
	if (error > 0) {
		errno = error; /* XXX */
		plog(XLOG_ERROR, "Could not mount %s: %m", dir);
	}
	return error;
}

/*
 * Cause all the top-level mount nodes
 * to be automounted
 */
int
mount_exported(void)
{
	/*
	 * Iterate over all the nodes to be started
	 */
	return root_keyiter((void (*)(char *, void *)) mount_auto_node, root_node);
}

/*
 * Construct top-level node
 */
void
make_root_node(void)
{
	mntfs *root_mnt;
	char *rootmap = ROOT_MAP;
	root_node = exported_ap_alloc();

	/*
	 * Allocate a new map
	 */
	init_map(root_node, "");
	/*
	 * Allocate a new mounted filesystem
	 */
	root_mnt = find_mntfs(&root_ops, (am_opts *) 0, "", rootmap, "", "", "");
	/*
	 * Replace the initial null reference
	 */
	free_mntfs(root_node->am_mnt);
	root_node->am_mnt = root_mnt;

	/*
	 * Initialise the root
	 */
	if (root_mnt->mf_ops->fs_init)
		(*root_mnt->mf_ops->fs_init)(root_mnt);

	/*
	 * Mount the root
	 */
	root_mnt->mf_error = (*root_mnt->mf_ops->mount_fs)(root_node);
}

/*
 * Cause all the nodes to be unmounted by timing
 * them out.
 */
void
umount_exported(void)
{
	int i;

	for (i = last_used_map; i >= 0; --i) {
		am_node *mp = exported_ap[i];
		if (mp) {
			mntfs *mf = mp->am_mnt;
			if (mf->mf_flags & MFF_UNMOUNTING) {
				/*
				 * If this node is being unmounted then
				 * just ignore it.  However, this could
				 * prevent amd from finishing if the
				 * unmount gets blocked since the am_node
				 * will never be free'd.  am_unmounted needs
				 * telling about this possibility. - XXX
				 */
				continue;
			}
			if (mf && !(mf->mf_ops->fs_flags & FS_DIRECTORY)) {
				/*
				 * When shutting down this had better
				 * look like a directory, otherwise it
				 * can't be unmounted!
				 */
				mk_fattr(mp, NFDIR);
			}
			if ((--immediate_abort < 0 && !(mp->am_flags & AMF_ROOT) && mp->am_parent) ||
			    (mf->mf_flags & MFF_RESTART)) {
				/*
				 * Just throw this node away without
				 * bothering to unmount it.  If the
				 * server is not known to be up then
				 * don't discard the mounted on directory
				 * or Amd might hang...
				 */
				if (mf->mf_server &&
					(mf->mf_server->fs_flags & (FSF_DOWN|FSF_VALID)) != FSF_VALID)
					mf->mf_flags &= ~MFF_MKMNT;
				am_unmounted(mp);
			} else {
				/*
				 * Any other node gets forcibly
				 * timed out
				 */
				mp->am_flags &= ~AMF_NOTIMEOUT;
				mp->am_mnt->mf_flags &= ~MFF_RSTKEEP;
				mp->am_ttl = 0;
				mp->am_timeo = 1;
				mp->am_timeo_w = 0;
			}
		}
	}
}

static int
unmount_node(am_node *mp)
{
	mntfs *mf = mp->am_mnt;
	int error;

	if ((mf->mf_flags & MFF_ERROR) || mf->mf_refc > 1) {
		/*
		 * Just unlink
		 */
#ifdef DEBUG
		if (mf->mf_flags & MFF_ERROR)
			dlog("No-op unmount of error node %s", mf->mf_info);
#endif /* DEBUG */
		error = 0;
	} else {
#ifdef DEBUG
		dlog("Unmounting %s (%s)", mf->mf_mount, mf->mf_info);
#endif /* DEBUG */
		error = (*mf->mf_ops->umount_fs)(mp);
	}

	if (error) {
#ifdef DEBUG
		errno = error; /* XXX */
		dlog("%s: unmount: %m", mf->mf_mount);
#endif /* DEBUG */
	}

	return error;
}

#ifdef FLUSH_KERNEL_NAME_CACHE
static void
flush_kernel_name_cache(am_node *mp)
{
	int islink = (mp->am_mnt->mf_fattr.type == NFLNK);
	int isdir = (mp->am_mnt->mf_fattr.type == NFDIR);
	int elog = 0;

	if (islink) {
		if (unlink(mp->am_path) < 0)
			elog = 1;
	} else if (isdir) {
		if (rmdir(mp->am_path) < 0)
			elog = 1;
	}
	if (elog)
		plog(XLOG_WARNING, "failed to clear \"%s\" from dnlc: %m", mp->am_path);
}
#endif /* FLUSH_KERNEL_NAME_CACHE */

static int
unmount_node_wrap(void *vp)
{
#ifndef FLUSH_KERNEL_NAME_CACHE
	return unmount_node((am_node*) vp);
#else /* FLUSH_KERNEL_NAME_CACHE */
	/*
	 * This code should just say:
	 * return unmount_node((am_node *) vp);
	 *
	 * However...
	 * The kernel keeps a cached copy of filehandles,
	 * and doesn't ever uncache them (apparently).  So
	 * when Amd times out a node the kernel will have a
	 * stale filehandle.  When the kernel next uses the
	 * filehandle it gets ESTALE.
	 *
	 * The workaround:
	 * Arrange that when a node is removed an unlink or
	 * rmdir is done on that path so that the kernel
	 * cache is done.  Yes - yuck.
	 *
	 * This can all be removed (and the background
	 * unmount flag in sfs_ops) if/when the kernel does
	 * something smarter.
	 *
	 * If the unlink or rmdir failed then just log a warning,
	 * don't fail the unmount.  This can occur if the kernel
	 * client code decides that the object is still referenced
	 * and should be renamed rather than discarded.
	 *
	 * There is still a race condition here...
	 * if another process is trying to access the same
	 * filesystem at the time we get here, then
	 * it will block, since the MF_UNMOUNTING flag will
	 * be set.  That may, or may not, cause the entire
	 * system to deadlock.  Hmmm...
	 */
	am_node *mp = (am_node *) vp;
	int isauto = mp->am_parent && (mp->am_parent->am_mnt->mf_fattr.type == NFDIR);
	int error = unmount_node(mp);
	if (error)
		return error;
	if (isauto && (int)amd_state < (int)Finishing)
		flush_kernel_name_cache(mp);

	return 0;
#endif /* FLUSH_KERNEL_NAME_CACHE */
}

static void
free_map_if_success(int rc, int term, void *closure)
{
	am_node *mp = (am_node *) closure;
	mntfs *mf = mp->am_mnt;

	/*
	 * Not unmounting any more
	 */
	mf->mf_flags &= ~MFF_UNMOUNTING;

	/*
	 * If a timeout was deferred because the underlying filesystem
	 * was busy then arrange for a timeout as soon as possible.
	 */
	if (mf->mf_flags & MFF_WANTTIMO) {
		mf->mf_flags &= ~MFF_WANTTIMO;
		reschedule_timeout_mp();
	}

	if (term) {
		plog(XLOG_ERROR, "unmount for %s got signal %d", mp->am_path, term);
#if defined(DEBUG) && defined(SIGTRAP)
		/*
		 * dbx likes to put a trap on exit().
		 * Pretend it succeeded for now...
		 */
		if (term == SIGTRAP) {
			am_unmounted(mp);
		}
#endif /* DEBUG */
		amd_stats.d_uerr++;
	} else if (rc) {
		if (rc == EBUSY) {
			plog(XLOG_STATS, "\"%s\" on %s still active", mp->am_path, mf->mf_mount);
		} else {
			errno = rc;	/* XXX */
			plog(XLOG_ERROR, "%s: unmount: %m", mp->am_path);
		}
		amd_stats.d_uerr++;
	} else {
		am_unmounted(mp);
	}

	/*
	 * Wakeup anything waiting for this mount
	 */
	wakeup(mf);
}

static int
unmount_mp(am_node *mp)
{
	int was_backgrounded = 0;
	mntfs *mf = mp->am_mnt;

#ifdef notdef
	plog(XLOG_INFO, "\"%s\" on %s timed out", mp->am_path, mp->am_mnt->mf_mount);
#endif /* notdef */

	if ((mf->mf_ops->fs_flags & FS_UBACKGROUND) &&
			(mf->mf_flags & MFF_MOUNTED)) {
		if (mf->mf_refc == 1 && !FSRV_ISUP(mf->mf_server)) {
			/*
			 * Don't try to unmount from a server that is known to be down
			 */
			if (!(mf->mf_flags & MFF_LOGDOWN)) {
				/* Only log this once, otherwise gets a bit boring */
				plog(XLOG_STATS, "file server %s is down - timeout of \"%s\" ignored", mf->mf_server->fs_host, mp->am_path);
				mf->mf_flags |= MFF_LOGDOWN;
			}
		} else {
			/* Clear logdown flag - since the server must be up */
			mf->mf_flags &= ~MFF_LOGDOWN;
#ifdef DEBUG
			dlog("\"%s\" on %s timed out", mp->am_path, mp->am_mnt->mf_mount);
			/*dlog("Will background the unmount attempt");*/
#endif /* DEBUG */
			/*
			 * Note that we are unmounting this node
			 */
			mf->mf_flags |= MFF_UNMOUNTING;
			run_task(unmount_node_wrap, mp,
				 free_map_if_success, mp);
			was_backgrounded = 1;
#ifdef DEBUG
			dlog("unmount attempt backgrounded");
#endif /* DEBUG */
		}
	} else {
#ifdef DEBUG
		dlog("\"%s\" on %s timed out", mp->am_path, mp->am_mnt->mf_mount);
		dlog("Trying unmount in foreground");
#endif
		mf->mf_flags |= MFF_UNMOUNTING;
		free_map_if_success(unmount_node(mp), 0, mp);
#ifdef DEBUG
		dlog("unmount attempt done");
#endif /* DEBUG */
	}

	return was_backgrounded;
}

static void
timeout_mp(void *arg)
{
#define NEVER (time_t) 0
#define	smallest_t(t1, t2) \
	(t1 != NEVER ? (t2 != NEVER ? (t1 < t2 ? t1 : t2) : t1) : t2)
#define IGNORE_FLAGS (MFF_MOUNTING|MFF_UNMOUNTING|MFF_RESTART)

	int i;
	time_t t = NEVER;
	time_t now = clocktime();
	int backoff = 0;

#ifdef DEBUG
	dlog("Timing out automount points...");
#endif /* DEBUG */
	for (i = last_used_map; i >= 0; --i) {
		am_node *mp = exported_ap[i];
		mntfs *mf;
		/*
		 * Just continue if nothing mounted, or can't be timed out.
		 */
		if (!mp || (mp->am_flags & AMF_NOTIMEOUT))
			continue;
		/*
		 * Pick up mounted filesystem
		 */
		mf = mp->am_mnt;
		if (!mf)
			continue;
		/*
		 * Don't delete last reference to a restarted filesystem.
		 */
		if ((mf->mf_flags & MFF_RSTKEEP) && mf->mf_refc == 1)
			continue;
		/*
		 * If there is action on this filesystem then ignore it
		 */
		if (!(mf->mf_flags & IGNORE_FLAGS)) {
			int expired = 0;
			mf->mf_flags &= ~MFF_WANTTIMO;
#ifdef DEBUG
			/*dlog("t is initially @%d, zero in %d secs", t, t - now);*/
#endif /* DEBUG */
			if (now >= mp->am_ttl) {
				if (!backoff) {
					expired = 1;
					/*
					 * Move the ttl forward to avoid thrashing effects
					 * on the next call to timeout!
					 */
					/* sun's -tw option */
					if (mp->am_timeo_w < 4 * am_timeo_w)
						mp->am_timeo_w += am_timeo_w;
					mp->am_ttl = now + mp->am_timeo_w;
				} else {
					/*
					 * Just backoff this unmount for
					 * a couple of seconds to avoid
					 * many multiple unmounts being
					 * started in parallel.
					 */
					mp->am_ttl = now + backoff + 1;
				}
			}
			/*
			 * If the next ttl is smallest, use that
			 */
			t = smallest_t(t, mp->am_ttl);

#ifdef DEBUG
			/*dlog("after ttl t is @%d, zero in %d secs", t, t - now);*/
#endif /* DEBUG */

			if (!mp->am_child && mf->mf_error >= 0 && expired) {
				/*
				 * If the unmount was backgrounded then
				 * bump the backoff counter.
				 */
				if (unmount_mp(mp)) {
					backoff = 2;
#ifdef DEBUG
					/*dlog("backing off subsequent unmounts by at least %d seconds", backoff);*/
#endif
				}
			}
		} else if (mf->mf_flags & MFF_UNMOUNTING) {
			mf->mf_flags |= MFF_WANTTIMO;
		}
	}

	if (t == NEVER) {
#ifdef DEBUG
		dlog("No further timeouts");
#endif /* DEBUG */
		t = now + ONE_HOUR;
	}

	/*
	 * Sanity check to avoid runaways.
	 * Absolutely should never get this but
	 * if you do without this trap amd will thrash.
	 */
	if (t <= now) {
		t = now + 6;	/* XXX */
		plog(XLOG_ERROR, "Got a zero interval in timeout_mp()!");
	}
	/*
	 * XXX - when shutting down, make things happen faster
	 */
	if ((int)amd_state >= (int)Finishing)
		t = now + 1;
#ifdef DEBUG
	dlog("Next mount timeout in %ds", t - now);
#endif /* DEBUG */

	timeout_mp_id = timeout(t - now, timeout_mp, 0);

#undef NEVER
#undef smallest_t
#undef IGNORE_FLAGS
}

/*
 * Cause timeout_mp to be called soonest
 */
void reschedule_timeout_mp()
{
	if (timeout_mp_id)
		untimeout(timeout_mp_id);
	timeout_mp_id = timeout(0, timeout_mp, 0);
}
