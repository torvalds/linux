/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000,2004
 *	Poul-Henning Kamp.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Neither the name of the University nor the names of its contributors
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
 * From: FreeBSD: src/sys/miscfs/kernfs/kernfs_vfsops.c 1.36
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/dirent.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/sx.h>
#include <sys/sysctl.h>
#include <sys/vnode.h>

#include <sys/kdb.h>

#include <fs/devfs/devfs.h>
#include <fs/devfs/devfs_int.h>

#include <security/mac/mac_framework.h>

/*
 * The one true (but secret) list of active devices in the system.
 * Locked by dev_lock()/devmtx
 */
struct cdev_priv_list cdevp_list = TAILQ_HEAD_INITIALIZER(cdevp_list);

struct unrhdr *devfs_inos;


static MALLOC_DEFINE(M_DEVFS2, "DEVFS2", "DEVFS data 2");
static MALLOC_DEFINE(M_DEVFS3, "DEVFS3", "DEVFS data 3");
static MALLOC_DEFINE(M_CDEVP, "DEVFS1", "DEVFS cdev_priv storage");

SYSCTL_NODE(_vfs, OID_AUTO, devfs, CTLFLAG_RW, 0, "DEVFS filesystem");

static unsigned devfs_generation;
SYSCTL_UINT(_vfs_devfs, OID_AUTO, generation, CTLFLAG_RD,
	&devfs_generation, 0, "DEVFS generation number");

unsigned devfs_rule_depth = 1;
SYSCTL_UINT(_vfs_devfs, OID_AUTO, rule_depth, CTLFLAG_RW,
	&devfs_rule_depth, 0, "Max depth of ruleset include");

/*
 * Helper sysctl for devname(3).  We're given a dev_t and return the
 * name, if any, registered by the device driver.
 */
static int
sysctl_devname(SYSCTL_HANDLER_ARGS)
{
	int error;
	dev_t ud;
#ifdef COMPAT_FREEBSD11
	uint32_t ud_compat;
#endif
	struct cdev_priv *cdp;
	struct cdev *dev;

#ifdef COMPAT_FREEBSD11
	if (req->newlen == sizeof(ud_compat)) {
		error = SYSCTL_IN(req, &ud_compat, sizeof(ud_compat));
		if (error == 0)
			ud = ud_compat == (uint32_t)NODEV ? NODEV : ud_compat;
	} else
#endif
		error = SYSCTL_IN(req, &ud, sizeof (ud));
	if (error)
		return (error);
	if (ud == NODEV)
		return (EINVAL);
	dev = NULL;
	dev_lock();
	TAILQ_FOREACH(cdp, &cdevp_list, cdp_list)
		if (cdp->cdp_inode == ud) {
			dev = &cdp->cdp_c;
			dev_refl(dev);
			break;
		}
	dev_unlock();
	if (dev == NULL)
		return (ENOENT);
	error = SYSCTL_OUT(req, dev->si_name, strlen(dev->si_name) + 1);
	dev_rel(dev);
	return (error);
}

SYSCTL_PROC(_kern, OID_AUTO, devname,
    CTLTYPE_OPAQUE|CTLFLAG_RW|CTLFLAG_ANYBODY|CTLFLAG_MPSAFE,
    NULL, 0, sysctl_devname, "", "devname(3) handler");

SYSCTL_INT(_debug_sizeof, OID_AUTO, cdev, CTLFLAG_RD,
    SYSCTL_NULL_INT_PTR, sizeof(struct cdev), "sizeof(struct cdev)");

SYSCTL_INT(_debug_sizeof, OID_AUTO, cdev_priv, CTLFLAG_RD,
    SYSCTL_NULL_INT_PTR, sizeof(struct cdev_priv), "sizeof(struct cdev_priv)");

struct cdev *
devfs_alloc(int flags)
{
	struct cdev_priv *cdp;
	struct cdev *cdev;
	struct timespec ts;

	cdp = malloc(sizeof *cdp, M_CDEVP, M_ZERO |
	    ((flags & MAKEDEV_NOWAIT) ? M_NOWAIT : M_WAITOK));
	if (cdp == NULL)
		return (NULL);

	cdp->cdp_dirents = &cdp->cdp_dirent0;

	cdev = &cdp->cdp_c;
	LIST_INIT(&cdev->si_children);
	vfs_timestamp(&ts);
	cdev->si_atime = cdev->si_mtime = cdev->si_ctime = ts;

	return (cdev);
}

int
devfs_dev_exists(const char *name)
{
	struct cdev_priv *cdp;

	mtx_assert(&devmtx, MA_OWNED);

	TAILQ_FOREACH(cdp, &cdevp_list, cdp_list) {
		if ((cdp->cdp_flags & CDP_ACTIVE) == 0)
			continue;
		if (devfs_pathpath(cdp->cdp_c.si_name, name) != 0)
			return (1);
		if (devfs_pathpath(name, cdp->cdp_c.si_name) != 0)
			return (1);
	}
	if (devfs_dir_find(name) != 0)
		return (1);

	return (0);
}

void
devfs_free(struct cdev *cdev)
{
	struct cdev_priv *cdp;

	cdp = cdev2priv(cdev);
	if (cdev->si_cred != NULL)
		crfree(cdev->si_cred);
	devfs_free_cdp_inode(cdp->cdp_inode);
	if (cdp->cdp_maxdirent > 0) 
		free(cdp->cdp_dirents, M_DEVFS2);
	free(cdp, M_CDEVP);
}

struct devfs_dirent *
devfs_find(struct devfs_dirent *dd, const char *name, int namelen, int type)
{
	struct devfs_dirent *de;

	TAILQ_FOREACH(de, &dd->de_dlist, de_list) {
		if (namelen != de->de_dirent->d_namlen)
			continue;
		if (type != 0 && type != de->de_dirent->d_type)
			continue;

		/*
		 * The race with finding non-active name is not
		 * completely closed by the check, but it is similar
		 * to the devfs_allocv() in making it unlikely enough.
		 */
		if (de->de_dirent->d_type == DT_CHR &&
		    (de->de_cdp->cdp_flags & CDP_ACTIVE) == 0)
			continue;

		if (bcmp(name, de->de_dirent->d_name, namelen) != 0)
			continue;
		break;
	}
	KASSERT(de == NULL || (de->de_flags & DE_DOOMED) == 0,
	    ("devfs_find: returning a doomed entry"));
	return (de);
}

struct devfs_dirent *
devfs_newdirent(char *name, int namelen)
{
	int i;
	struct devfs_dirent *de;
	struct dirent d;

	d.d_namlen = namelen;
	i = sizeof(*de) + GENERIC_DIRSIZ(&d);
	de = malloc(i, M_DEVFS3, M_WAITOK | M_ZERO);
	de->de_dirent = (struct dirent *)(de + 1);
	de->de_dirent->d_namlen = namelen;
	de->de_dirent->d_reclen = GENERIC_DIRSIZ(&d);
	bcopy(name, de->de_dirent->d_name, namelen);
	dirent_terminate(de->de_dirent);
	vfs_timestamp(&de->de_ctime);
	de->de_mtime = de->de_atime = de->de_ctime;
	de->de_links = 1;
	de->de_holdcnt = 1;
#ifdef MAC
	mac_devfs_init(de);
#endif
	return (de);
}

struct devfs_dirent *
devfs_parent_dirent(struct devfs_dirent *de)
{

	if (de->de_dirent->d_type != DT_DIR)
		return (de->de_dir);

	if (de->de_flags & (DE_DOT | DE_DOTDOT))
		return (NULL);

	de = TAILQ_FIRST(&de->de_dlist);	/* "." */
	if (de == NULL)
		return (NULL);
	de = TAILQ_NEXT(de, de_list);		/* ".." */
	if (de == NULL)
		return (NULL);

	return (de->de_dir);
}

struct devfs_dirent *
devfs_vmkdir(struct devfs_mount *dmp, char *name, int namelen,
    struct devfs_dirent *dotdot, u_int inode)
{
	struct devfs_dirent *dd;
	struct devfs_dirent *de;

	/* Create the new directory */
	dd = devfs_newdirent(name, namelen);
	TAILQ_INIT(&dd->de_dlist);
	dd->de_dirent->d_type = DT_DIR;
	dd->de_mode = 0555;
	dd->de_links = 2;
	dd->de_dir = dd;
	if (inode != 0)
		dd->de_inode = inode;
	else
		dd->de_inode = alloc_unr(devfs_inos);

	/*
	 * "." and ".." are always the two first entries in the
	 * de_dlist list.
	 *
	 * Create the "." entry in the new directory.
	 */
	de = devfs_newdirent(".", 1);
	de->de_dirent->d_type = DT_DIR;
	de->de_flags |= DE_DOT;
	TAILQ_INSERT_TAIL(&dd->de_dlist, de, de_list);
	de->de_dir = dd;

	/* Create the ".." entry in the new directory. */
	de = devfs_newdirent("..", 2);
	de->de_dirent->d_type = DT_DIR;
	de->de_flags |= DE_DOTDOT;
	TAILQ_INSERT_TAIL(&dd->de_dlist, de, de_list);
	if (dotdot == NULL) {
		de->de_dir = dd;
	} else {
		de->de_dir = dotdot;
		sx_assert(&dmp->dm_lock, SX_XLOCKED);
		TAILQ_INSERT_TAIL(&dotdot->de_dlist, dd, de_list);
		dotdot->de_links++;
		devfs_rules_apply(dmp, dd);
	}

#ifdef MAC
	mac_devfs_create_directory(dmp->dm_mount, name, namelen, dd);
#endif
	return (dd);
}

void
devfs_dirent_free(struct devfs_dirent *de)
{
	struct vnode *vp;

	vp = de->de_vnode;
	mtx_lock(&devfs_de_interlock);
	if (vp != NULL && vp->v_data == de)
		vp->v_data = NULL;
	mtx_unlock(&devfs_de_interlock);
	free(de, M_DEVFS3);
}

/*
 * Removes a directory if it is empty. Also empty parent directories are
 * removed recursively.
 */
static void
devfs_rmdir_empty(struct devfs_mount *dm, struct devfs_dirent *de)
{
	struct devfs_dirent *dd, *de_dot, *de_dotdot;

	sx_assert(&dm->dm_lock, SX_XLOCKED);

	for (;;) {
		KASSERT(de->de_dirent->d_type == DT_DIR,
		    ("devfs_rmdir_empty: de is not a directory"));

		if ((de->de_flags & DE_DOOMED) != 0 || de == dm->dm_rootdir)
			return;

		de_dot = TAILQ_FIRST(&de->de_dlist);
		KASSERT(de_dot != NULL, ("devfs_rmdir_empty: . missing"));
		de_dotdot = TAILQ_NEXT(de_dot, de_list);
		KASSERT(de_dotdot != NULL, ("devfs_rmdir_empty: .. missing"));
		/* Return if the directory is not empty. */
		if (TAILQ_NEXT(de_dotdot, de_list) != NULL)
			return;

		dd = devfs_parent_dirent(de);
		KASSERT(dd != NULL, ("devfs_rmdir_empty: NULL dd"));
		TAILQ_REMOVE(&de->de_dlist, de_dot, de_list);
		TAILQ_REMOVE(&de->de_dlist, de_dotdot, de_list);
		TAILQ_REMOVE(&dd->de_dlist, de, de_list);
		DEVFS_DE_HOLD(dd);
		devfs_delete(dm, de, DEVFS_DEL_NORECURSE);
		devfs_delete(dm, de_dot, DEVFS_DEL_NORECURSE);
		devfs_delete(dm, de_dotdot, DEVFS_DEL_NORECURSE);
		if (DEVFS_DE_DROP(dd)) {
			devfs_dirent_free(dd);
			return;
		}

		de = dd;
	}
}

/*
 * The caller needs to hold the dm for the duration of the call since
 * dm->dm_lock may be temporary dropped.
 */
void
devfs_delete(struct devfs_mount *dm, struct devfs_dirent *de, int flags)
{
	struct devfs_dirent *dd;
	struct vnode *vp;

	KASSERT((de->de_flags & DE_DOOMED) == 0,
		("devfs_delete doomed dirent"));
	de->de_flags |= DE_DOOMED;

	if ((flags & DEVFS_DEL_NORECURSE) == 0) {
		dd = devfs_parent_dirent(de);
		if (dd != NULL)
			DEVFS_DE_HOLD(dd);
		if (de->de_flags & DE_USER) {
			KASSERT(dd != NULL, ("devfs_delete: NULL dd"));
			devfs_dir_unref_de(dm, dd);
		}
	} else
		dd = NULL;

	mtx_lock(&devfs_de_interlock);
	vp = de->de_vnode;
	if (vp != NULL) {
		VI_LOCK(vp);
		mtx_unlock(&devfs_de_interlock);
		vholdl(vp);
		sx_unlock(&dm->dm_lock);
		if ((flags & DEVFS_DEL_VNLOCKED) == 0)
			vn_lock(vp, LK_EXCLUSIVE | LK_INTERLOCK | LK_RETRY);
		else
			VI_UNLOCK(vp);
		vgone(vp);
		if ((flags & DEVFS_DEL_VNLOCKED) == 0)
			VOP_UNLOCK(vp, 0);
		vdrop(vp);
		sx_xlock(&dm->dm_lock);
	} else
		mtx_unlock(&devfs_de_interlock);
	if (de->de_symlink) {
		free(de->de_symlink, M_DEVFS);
		de->de_symlink = NULL;
	}
#ifdef MAC
	mac_devfs_destroy(de);
#endif
	if (de->de_inode > DEVFS_ROOTINO) {
		devfs_free_cdp_inode(de->de_inode);
		de->de_inode = 0;
	}
	if (DEVFS_DE_DROP(de))
		devfs_dirent_free(de);

	if (dd != NULL) {
		if (DEVFS_DE_DROP(dd))
			devfs_dirent_free(dd);
		else
			devfs_rmdir_empty(dm, dd);
	}
}

/*
 * Called on unmount.
 * Recursively removes the entire tree.
 * The caller needs to hold the dm for the duration of the call.
 */

static void
devfs_purge(struct devfs_mount *dm, struct devfs_dirent *dd)
{
	struct devfs_dirent *de;

	sx_assert(&dm->dm_lock, SX_XLOCKED);

	DEVFS_DE_HOLD(dd);
	for (;;) {
		/*
		 * Use TAILQ_LAST() to remove "." and ".." last.
		 * We might need ".." to resolve a path in
		 * devfs_dir_unref_de().
		 */
		de = TAILQ_LAST(&dd->de_dlist, devfs_dlist_head);
		if (de == NULL)
			break;
		TAILQ_REMOVE(&dd->de_dlist, de, de_list);
		if (de->de_flags & DE_USER)
			devfs_dir_unref_de(dm, dd);
		if (de->de_flags & (DE_DOT | DE_DOTDOT))
			devfs_delete(dm, de, DEVFS_DEL_NORECURSE);
		else if (de->de_dirent->d_type == DT_DIR)
			devfs_purge(dm, de);
		else
			devfs_delete(dm, de, DEVFS_DEL_NORECURSE);
	}
	if (DEVFS_DE_DROP(dd))
		devfs_dirent_free(dd);
	else if ((dd->de_flags & DE_DOOMED) == 0)
		devfs_delete(dm, dd, DEVFS_DEL_NORECURSE);
}

/*
 * Each cdev_priv has an array of pointers to devfs_dirent which is indexed
 * by the mount points dm_idx.
 * This function extends the array when necessary, taking into account that
 * the default array is 1 element and not malloc'ed.
 */
static void
devfs_metoo(struct cdev_priv *cdp, struct devfs_mount *dm)
{
	struct devfs_dirent **dep;
	int siz;

	siz = (dm->dm_idx + 1) * sizeof *dep;
	dep = malloc(siz, M_DEVFS2, M_WAITOK | M_ZERO);
	dev_lock();
	if (dm->dm_idx <= cdp->cdp_maxdirent) {
		/* We got raced */
		dev_unlock();
		free(dep, M_DEVFS2);
		return;
	} 
	memcpy(dep, cdp->cdp_dirents, (cdp->cdp_maxdirent + 1) * sizeof *dep);
	if (cdp->cdp_maxdirent > 0)
		free(cdp->cdp_dirents, M_DEVFS2);
	cdp->cdp_dirents = dep;
	/*
	 * XXX: if malloc told us how much we actually got this could
	 * XXX: be optimized.
	 */
	cdp->cdp_maxdirent = dm->dm_idx;
	dev_unlock();
}

/*
 * The caller needs to hold the dm for the duration of the call.
 */
static int
devfs_populate_loop(struct devfs_mount *dm, int cleanup)
{
	struct cdev_priv *cdp;
	struct devfs_dirent *de;
	struct devfs_dirent *dd, *dt;
	struct cdev *pdev;
	int de_flags, depth, j;
	char *q, *s;

	sx_assert(&dm->dm_lock, SX_XLOCKED);
	dev_lock();
	TAILQ_FOREACH(cdp, &cdevp_list, cdp_list) {

		KASSERT(cdp->cdp_dirents != NULL, ("NULL cdp_dirents"));

		/*
		 * If we are unmounting, or the device has been destroyed,
		 * clean up our dirent.
		 */
		if ((cleanup || !(cdp->cdp_flags & CDP_ACTIVE)) &&
		    dm->dm_idx <= cdp->cdp_maxdirent &&
		    cdp->cdp_dirents[dm->dm_idx] != NULL) {
			de = cdp->cdp_dirents[dm->dm_idx];
			cdp->cdp_dirents[dm->dm_idx] = NULL;
			KASSERT(cdp == de->de_cdp,
			    ("%s %d %s %p %p", __func__, __LINE__,
			    cdp->cdp_c.si_name, cdp, de->de_cdp));
			KASSERT(de->de_dir != NULL, ("Null de->de_dir"));
			dev_unlock();

			TAILQ_REMOVE(&de->de_dir->de_dlist, de, de_list);
			de->de_cdp = NULL;
			de->de_inode = 0;
			devfs_delete(dm, de, 0);
			dev_lock();
			cdp->cdp_inuse--;
			dev_unlock();
			return (1);
		}
		/*
	 	 * GC any lingering devices
		 */
		if (!(cdp->cdp_flags & CDP_ACTIVE)) {
			if (cdp->cdp_inuse > 0)
				continue;
			TAILQ_REMOVE(&cdevp_list, cdp, cdp_list);
			dev_unlock();
			dev_rel(&cdp->cdp_c);
			return (1);
		}
		/*
		 * Don't create any new dirents if we are unmounting
		 */
		if (cleanup)
			continue;
		KASSERT((cdp->cdp_flags & CDP_ACTIVE), ("Bogons, I tell ya'!"));

		if (dm->dm_idx <= cdp->cdp_maxdirent &&
		    cdp->cdp_dirents[dm->dm_idx] != NULL) {
			de = cdp->cdp_dirents[dm->dm_idx];
			KASSERT(cdp == de->de_cdp, ("inconsistent cdp"));
			continue;
		}


		cdp->cdp_inuse++;
		dev_unlock();

		if (dm->dm_idx > cdp->cdp_maxdirent)
		        devfs_metoo(cdp, dm);

		dd = dm->dm_rootdir;
		s = cdp->cdp_c.si_name;
		for (;;) {
			for (q = s; *q != '/' && *q != '\0'; q++)
				continue;
			if (*q != '/')
				break;
			de = devfs_find(dd, s, q - s, 0);
			if (de == NULL)
				de = devfs_vmkdir(dm, s, q - s, dd, 0);
			else if (de->de_dirent->d_type == DT_LNK) {
				de = devfs_find(dd, s, q - s, DT_DIR);
				if (de == NULL)
					de = devfs_vmkdir(dm, s, q - s, dd, 0);
				de->de_flags |= DE_COVERED;
			}
			s = q + 1;
			dd = de;
			KASSERT(dd->de_dirent->d_type == DT_DIR &&
			    (dd->de_flags & (DE_DOT | DE_DOTDOT)) == 0,
			    ("%s: invalid directory (si_name=%s)",
			    __func__, cdp->cdp_c.si_name));

		}
		de_flags = 0;
		de = devfs_find(dd, s, q - s, DT_LNK);
		if (de != NULL)
			de_flags |= DE_COVERED;

		de = devfs_newdirent(s, q - s);
		if (cdp->cdp_c.si_flags & SI_ALIAS) {
			de->de_uid = 0;
			de->de_gid = 0;
			de->de_mode = 0755;
			de->de_dirent->d_type = DT_LNK;
			pdev = cdp->cdp_c.si_parent;
			dt = dd;
			depth = 0;
			while (dt != dm->dm_rootdir &&
			    (dt = devfs_parent_dirent(dt)) != NULL)
				depth++;
			j = depth * 3 + strlen(pdev->si_name) + 1;
			de->de_symlink = malloc(j, M_DEVFS, M_WAITOK);
			de->de_symlink[0] = 0;
			while (depth-- > 0)
				strcat(de->de_symlink, "../");
			strcat(de->de_symlink, pdev->si_name);
		} else {
			de->de_uid = cdp->cdp_c.si_uid;
			de->de_gid = cdp->cdp_c.si_gid;
			de->de_mode = cdp->cdp_c.si_mode;
			de->de_dirent->d_type = DT_CHR;
		}
		de->de_flags |= de_flags;
		de->de_inode = cdp->cdp_inode;
		de->de_cdp = cdp;
#ifdef MAC
		mac_devfs_create_device(cdp->cdp_c.si_cred, dm->dm_mount,
		    &cdp->cdp_c, de);
#endif
		de->de_dir = dd;
		TAILQ_INSERT_TAIL(&dd->de_dlist, de, de_list);
		devfs_rules_apply(dm, de);
		dev_lock();
		/* XXX: could check that cdp is still active here */
		KASSERT(cdp->cdp_dirents[dm->dm_idx] == NULL,
		    ("%s %d\n", __func__, __LINE__));
		cdp->cdp_dirents[dm->dm_idx] = de;
		KASSERT(de->de_cdp != (void *)0xdeadc0de,
		    ("%s %d\n", __func__, __LINE__));
		dev_unlock();
		return (1);
	}
	dev_unlock();
	return (0);
}

/*
 * The caller needs to hold the dm for the duration of the call.
 */
void
devfs_populate(struct devfs_mount *dm)
{
	unsigned gen;

	sx_assert(&dm->dm_lock, SX_XLOCKED);
	gen = devfs_generation;
	if (dm->dm_generation == gen)
		return;
	while (devfs_populate_loop(dm, 0))
		continue;
	dm->dm_generation = gen;
}

/*
 * The caller needs to hold the dm for the duration of the call.
 */
void
devfs_cleanup(struct devfs_mount *dm)
{

	sx_assert(&dm->dm_lock, SX_XLOCKED);
	while (devfs_populate_loop(dm, 1))
		continue;
	devfs_purge(dm, dm->dm_rootdir);
}

/*
 * devfs_create() and devfs_destroy() are called from kern_conf.c and
 * in both cases the devlock() mutex is held, so no further locking
 * is necessary and no sleeping allowed.
 */

void
devfs_create(struct cdev *dev)
{
	struct cdev_priv *cdp;

	mtx_assert(&devmtx, MA_OWNED);
	cdp = cdev2priv(dev);
	cdp->cdp_flags |= CDP_ACTIVE;
	cdp->cdp_inode = alloc_unrl(devfs_inos);
	dev_refl(dev);
	TAILQ_INSERT_TAIL(&cdevp_list, cdp, cdp_list);
	devfs_generation++;
}

void
devfs_destroy(struct cdev *dev)
{
	struct cdev_priv *cdp;

	mtx_assert(&devmtx, MA_OWNED);
	cdp = cdev2priv(dev);
	cdp->cdp_flags &= ~CDP_ACTIVE;
	devfs_generation++;
}

ino_t
devfs_alloc_cdp_inode(void)
{

	return (alloc_unr(devfs_inos));
}

void
devfs_free_cdp_inode(ino_t ino)
{

	if (ino > 0)
		free_unr(devfs_inos, ino);
}

static void
devfs_devs_init(void *junk __unused)
{

	devfs_inos = new_unrhdr(DEVFS_ROOTINO + 1, INT_MAX, &devmtx);
}

SYSINIT(devfs_devs, SI_SUB_DEVFS, SI_ORDER_FIRST, devfs_devs_init, NULL);
