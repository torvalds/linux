/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2010 Marcel Moolenaar
 * Copyright (c) 1999-2004 Poul-Henning Kamp
 * Copyright (c) 1999 Michael Smith
 * Copyright (c) 1989, 1993
 *      The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "opt_rootdevname.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/cons.h>
#include <sys/fcntl.h>
#include <sys/jail.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mdioctl.h>
#include <sys/mount.h>
#include <sys/mutex.h>
#include <sys/namei.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/filedesc.h>
#include <sys/reboot.h>
#include <sys/sbuf.h>
#include <sys/stat.h>
#include <sys/syscallsubr.h>
#include <sys/sysproto.h>
#include <sys/sx.h>
#include <sys/sysctl.h>
#include <sys/sysent.h>
#include <sys/systm.h>
#include <sys/vnode.h>

#include <geom/geom.h>

/*
 * The root filesystem is detailed in the kernel environment variable
 * vfs.root.mountfrom, which is expected to be in the general format
 *
 * <vfsname>:[<path>][	<vfsname>:[<path>] ...]
 * vfsname   := the name of a VFS known to the kernel and capable
 *              of being mounted as root
 * path      := disk device name or other data used by the filesystem
 *              to locate its physical store
 *
 * If the environment variable vfs.root.mountfrom is a space separated list,
 * each list element is tried in turn and the root filesystem will be mounted
 * from the first one that succeeds.
 *
 * The environment variable vfs.root.mountfrom.options is a comma delimited
 * set of string mount options.  These mount options must be parseable
 * by nmount() in the kernel.
 */

static int parse_mount(char **);
static struct mntarg *parse_mountroot_options(struct mntarg *, const char *);
static int sysctl_vfs_root_mount_hold(SYSCTL_HANDLER_ARGS);
static void vfs_mountroot_wait(void);
static int vfs_mountroot_wait_if_neccessary(const char *fs, const char *dev);

/*
 * The vnode of the system's root (/ in the filesystem, without chroot
 * active.)
 */
struct vnode *rootvnode;

/*
 * Mount of the system's /dev.
 */
struct mount *rootdevmp;

char *rootdevnames[2] = {NULL, NULL};

struct mtx root_holds_mtx;
MTX_SYSINIT(root_holds, &root_holds_mtx, "root_holds", MTX_DEF);

struct root_hold_token {
	const char			*who;
	LIST_ENTRY(root_hold_token)	list;
};

static LIST_HEAD(, root_hold_token)	root_holds =
    LIST_HEAD_INITIALIZER(root_holds);

enum action {
	A_CONTINUE,
	A_PANIC,
	A_REBOOT,
	A_RETRY
};

static enum action root_mount_onfail = A_CONTINUE;

static int root_mount_mddev;
static int root_mount_complete;

/* By default wait up to 3 seconds for devices to appear. */
static int root_mount_timeout = 3;
TUNABLE_INT("vfs.mountroot.timeout", &root_mount_timeout);

static int root_mount_always_wait = 0;
SYSCTL_INT(_vfs, OID_AUTO, root_mount_always_wait, CTLFLAG_RDTUN,
    &root_mount_always_wait, 0,
    "Wait for root mount holds even if the root device already exists");

SYSCTL_PROC(_vfs, OID_AUTO, root_mount_hold,
    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE,
    NULL, 0, sysctl_vfs_root_mount_hold, "A",
    "List of root mount hold tokens");

static int
sysctl_vfs_root_mount_hold(SYSCTL_HANDLER_ARGS)
{
	struct sbuf sb;
	struct root_hold_token *h;
	int error;

	sbuf_new(&sb, NULL, 256, SBUF_AUTOEXTEND | SBUF_INCLUDENUL);

	mtx_lock(&root_holds_mtx);
	LIST_FOREACH(h, &root_holds, list) {
		if (h != LIST_FIRST(&root_holds))
			sbuf_putc(&sb, ' ');
		sbuf_printf(&sb, "%s", h->who);
	}
	mtx_unlock(&root_holds_mtx);

	error = sbuf_finish(&sb);
	if (error == 0)
		error = SYSCTL_OUT(req, sbuf_data(&sb), sbuf_len(&sb));
	sbuf_delete(&sb);
	return (error);
}

struct root_hold_token *
root_mount_hold(const char *identifier)
{
	struct root_hold_token *h;

	h = malloc(sizeof *h, M_DEVBUF, M_ZERO | M_WAITOK);
	h->who = identifier;
	mtx_lock(&root_holds_mtx);
	TSHOLD("root mount");
	LIST_INSERT_HEAD(&root_holds, h, list);
	mtx_unlock(&root_holds_mtx);
	return (h);
}

void
root_mount_rel(struct root_hold_token *h)
{

	if (h == NULL)
		return;

	mtx_lock(&root_holds_mtx);
	LIST_REMOVE(h, list);
	TSRELEASE("root mount");
	wakeup(&root_holds);
	mtx_unlock(&root_holds_mtx);
	free(h, M_DEVBUF);
}

int
root_mounted(void)
{

	/* No mutex is acquired here because int stores are atomic. */
	return (root_mount_complete);
}

static void
set_rootvnode(void)
{
	struct proc *p;

	if (VFS_ROOT(TAILQ_FIRST(&mountlist), LK_EXCLUSIVE, &rootvnode))
		panic("set_rootvnode: Cannot find root vnode");

	VOP_UNLOCK(rootvnode, 0);

	p = curthread->td_proc;
	FILEDESC_XLOCK(p->p_fd);

	if (p->p_fd->fd_cdir != NULL)
		vrele(p->p_fd->fd_cdir);
	p->p_fd->fd_cdir = rootvnode;
	VREF(rootvnode);

	if (p->p_fd->fd_rdir != NULL)
		vrele(p->p_fd->fd_rdir);
	p->p_fd->fd_rdir = rootvnode;
	VREF(rootvnode);

	FILEDESC_XUNLOCK(p->p_fd);
}

static int
vfs_mountroot_devfs(struct thread *td, struct mount **mpp)
{
	struct vfsoptlist *opts;
	struct vfsconf *vfsp;
	struct mount *mp;
	int error;

	*mpp = NULL;

	if (rootdevmp != NULL) {
		/*
		 * Already have /dev; this happens during rerooting.
		 */
		error = vfs_busy(rootdevmp, 0);
		if (error != 0)
			return (error);
		*mpp = rootdevmp;
	} else {
		vfsp = vfs_byname("devfs");
		KASSERT(vfsp != NULL, ("Could not find devfs by name"));
		if (vfsp == NULL)
			return (ENOENT);

		mp = vfs_mount_alloc(NULLVP, vfsp, "/dev", td->td_ucred);

		error = VFS_MOUNT(mp);
		KASSERT(error == 0, ("VFS_MOUNT(devfs) failed %d", error));
		if (error)
			return (error);

		opts = malloc(sizeof(struct vfsoptlist), M_MOUNT, M_WAITOK);
		TAILQ_INIT(opts);
		mp->mnt_opt = opts;

		mtx_lock(&mountlist_mtx);
		TAILQ_INSERT_HEAD(&mountlist, mp, mnt_list);
		mtx_unlock(&mountlist_mtx);

		*mpp = mp;
		rootdevmp = mp;
	}

	set_rootvnode();

	error = kern_symlinkat(td, "/", AT_FDCWD, "dev", UIO_SYSSPACE);
	if (error)
		printf("kern_symlink /dev -> / returns %d\n", error);

	return (error);
}

static void
vfs_mountroot_shuffle(struct thread *td, struct mount *mpdevfs)
{
	struct nameidata nd;
	struct mount *mporoot, *mpnroot;
	struct vnode *vp, *vporoot, *vpdevfs;
	char *fspath;
	int error;

	mpnroot = TAILQ_NEXT(mpdevfs, mnt_list);

	/* Shuffle the mountlist. */
	mtx_lock(&mountlist_mtx);
	mporoot = TAILQ_FIRST(&mountlist);
	TAILQ_REMOVE(&mountlist, mpdevfs, mnt_list);
	if (mporoot != mpdevfs) {
		TAILQ_REMOVE(&mountlist, mpnroot, mnt_list);
		TAILQ_INSERT_HEAD(&mountlist, mpnroot, mnt_list);
	}
	TAILQ_INSERT_TAIL(&mountlist, mpdevfs, mnt_list);
	mtx_unlock(&mountlist_mtx);

	cache_purgevfs(mporoot, true);
	if (mporoot != mpdevfs)
		cache_purgevfs(mpdevfs, true);

	if (VFS_ROOT(mporoot, LK_EXCLUSIVE, &vporoot))
		panic("vfs_mountroot_shuffle: Cannot find root vnode");

	VI_LOCK(vporoot);
	vporoot->v_iflag &= ~VI_MOUNT;
	VI_UNLOCK(vporoot);
	vporoot->v_mountedhere = NULL;
	mporoot->mnt_flag &= ~MNT_ROOTFS;
	mporoot->mnt_vnodecovered = NULL;
	vput(vporoot);

	/* Set up the new rootvnode, and purge the cache */
	mpnroot->mnt_vnodecovered = NULL;
	set_rootvnode();
	cache_purgevfs(rootvnode->v_mount, true);

	if (mporoot != mpdevfs) {
		/* Remount old root under /.mount or /mnt */
		fspath = "/.mount";
		NDINIT(&nd, LOOKUP, FOLLOW | LOCKLEAF, UIO_SYSSPACE,
		    fspath, td);
		error = namei(&nd);
		if (error) {
			NDFREE(&nd, NDF_ONLY_PNBUF);
			fspath = "/mnt";
			NDINIT(&nd, LOOKUP, FOLLOW | LOCKLEAF, UIO_SYSSPACE,
			    fspath, td);
			error = namei(&nd);
		}
		if (!error) {
			vp = nd.ni_vp;
			error = (vp->v_type == VDIR) ? 0 : ENOTDIR;
			if (!error)
				error = vinvalbuf(vp, V_SAVE, 0, 0);
			if (!error) {
				cache_purge(vp);
				mporoot->mnt_vnodecovered = vp;
				vp->v_mountedhere = mporoot;
				strlcpy(mporoot->mnt_stat.f_mntonname,
				    fspath, MNAMELEN);
				VOP_UNLOCK(vp, 0);
			} else
				vput(vp);
		}
		NDFREE(&nd, NDF_ONLY_PNBUF);

		if (error)
			printf("mountroot: unable to remount previous root "
			    "under /.mount or /mnt (error %d)\n", error);
	}

	/* Remount devfs under /dev */
	NDINIT(&nd, LOOKUP, FOLLOW | LOCKLEAF, UIO_SYSSPACE, "/dev", td);
	error = namei(&nd);
	if (!error) {
		vp = nd.ni_vp;
		error = (vp->v_type == VDIR) ? 0 : ENOTDIR;
		if (!error)
			error = vinvalbuf(vp, V_SAVE, 0, 0);
		if (!error) {
			vpdevfs = mpdevfs->mnt_vnodecovered;
			if (vpdevfs != NULL) {
				cache_purge(vpdevfs);
				vpdevfs->v_mountedhere = NULL;
				vrele(vpdevfs);
			}
			mpdevfs->mnt_vnodecovered = vp;
			vp->v_mountedhere = mpdevfs;
			VOP_UNLOCK(vp, 0);
		} else
			vput(vp);
	}
	if (error)
		printf("mountroot: unable to remount devfs under /dev "
		    "(error %d)\n", error);
	NDFREE(&nd, NDF_ONLY_PNBUF);

	if (mporoot == mpdevfs) {
		vfs_unbusy(mpdevfs);
		/* Unlink the no longer needed /dev/dev -> / symlink */
		error = kern_unlinkat(td, AT_FDCWD, "/dev/dev",
		    UIO_SYSSPACE, 0, 0);
		if (error)
			printf("mountroot: unable to unlink /dev/dev "
			    "(error %d)\n", error);
	}
}

/*
 * Configuration parser.
 */

/* Parser character classes. */
#define	CC_WHITESPACE		-1
#define	CC_NONWHITESPACE	-2

/* Parse errors. */
#define	PE_EOF			-1
#define	PE_EOL			-2

static __inline int
parse_peek(char **conf)
{

	return (**conf);
}

static __inline void
parse_poke(char **conf, int c)
{

	**conf = c;
}

static __inline void
parse_advance(char **conf)
{

	(*conf)++;
}

static int
parse_skipto(char **conf, int mc)
{
	int c, match;

	while (1) {
		c = parse_peek(conf);
		if (c == 0)
			return (PE_EOF);
		switch (mc) {
		case CC_WHITESPACE:
			match = (c == ' ' || c == '\t' || c == '\n') ? 1 : 0;
			break;
		case CC_NONWHITESPACE:
			if (c == '\n')
				return (PE_EOL);
			match = (c != ' ' && c != '\t') ? 1 : 0;
			break;
		default:
			match = (c == mc) ? 1 : 0;
			break;
		}
		if (match)
			break;
		parse_advance(conf);
	}
	return (0);
}

static int
parse_token(char **conf, char **tok)
{
	char *p;
	size_t len;
	int error;

	*tok = NULL;
	error = parse_skipto(conf, CC_NONWHITESPACE);
	if (error)
		return (error);
	p = *conf;
	error = parse_skipto(conf, CC_WHITESPACE);
	len = *conf - p;
	*tok = malloc(len + 1, M_TEMP, M_WAITOK | M_ZERO);
	bcopy(p, *tok, len);
	return (0);
}

static void
parse_dir_ask_printenv(const char *var)
{
	char *val;

	val = kern_getenv(var);
	if (val != NULL) {
		printf("  %s=%s\n", var, val);
		freeenv(val);
	}
}

static int
parse_dir_ask(char **conf)
{
	char name[80];
	char *mnt;
	int error;

	vfs_mountroot_wait();

	printf("\nLoader variables:\n");
	parse_dir_ask_printenv("vfs.root.mountfrom");
	parse_dir_ask_printenv("vfs.root.mountfrom.options");

	printf("\nManual root filesystem specification:\n");
	printf("  <fstype>:<device> [options]\n");
	printf("      Mount <device> using filesystem <fstype>\n");
	printf("      and with the specified (optional) option list.\n");
	printf("\n");
	printf("    eg. ufs:/dev/da0s1a\n");
	printf("        zfs:zroot/ROOT/default\n");
	printf("        cd9660:/dev/cd0 ro\n");
	printf("          (which is equivalent to: ");
	printf("mount -t cd9660 -o ro /dev/cd0 /)\n");
	printf("\n");
	printf("  ?               List valid disk boot devices\n");
	printf("  .               Yield 1 second (for background tasks)\n");
	printf("  <empty line>    Abort manual input\n");

	do {
		error = EINVAL;
		printf("\nmountroot> ");
		cngets(name, sizeof(name), GETS_ECHO);
		if (name[0] == '\0')
			break;
		if (name[0] == '?' && name[1] == '\0') {
			printf("\nList of GEOM managed disk devices:\n  ");
			g_dev_print();
			continue;
		}
		if (name[0] == '.' && name[1] == '\0') {
			pause("rmask", hz);
			continue;
		}
		mnt = name;
		error = parse_mount(&mnt);
		if (error == -1)
			printf("Invalid file system specification.\n");
	} while (error != 0);

	return (error);
}

static int
parse_dir_md(char **conf)
{
	struct stat sb;
	struct thread *td;
	struct md_ioctl *mdio;
	char *path, *tok;
	int error, fd, len;

	td = curthread;

	error = parse_token(conf, &tok);
	if (error)
		return (error);

	len = strlen(tok);
	mdio = malloc(sizeof(*mdio) + len + 1, M_TEMP, M_WAITOK | M_ZERO);
	path = (void *)(mdio + 1);
	bcopy(tok, path, len);
	free(tok, M_TEMP);

	/* Get file status. */
	error = kern_statat(td, 0, AT_FDCWD, path, UIO_SYSSPACE, &sb, NULL);
	if (error)
		goto out;

	/* Open /dev/mdctl so that we can attach/detach. */
	error = kern_openat(td, AT_FDCWD, "/dev/" MDCTL_NAME, UIO_SYSSPACE,
	    O_RDWR, 0);
	if (error)
		goto out;

	fd = td->td_retval[0];
	mdio->md_version = MDIOVERSION;
	mdio->md_type = MD_VNODE;

	if (root_mount_mddev != -1) {
		mdio->md_unit = root_mount_mddev;
		(void)kern_ioctl(td, fd, MDIOCDETACH, (void *)mdio);
		/* Ignore errors. We don't care. */
		root_mount_mddev = -1;
	}

	mdio->md_file = (void *)(mdio + 1);
	mdio->md_options = MD_AUTOUNIT | MD_READONLY;
	mdio->md_mediasize = sb.st_size;
	mdio->md_unit = 0;
	error = kern_ioctl(td, fd, MDIOCATTACH, (void *)mdio);
	if (error)
		goto out;

	if (mdio->md_unit > 9) {
		printf("rootmount: too many md units\n");
		mdio->md_file = NULL;
		mdio->md_options = 0;
		mdio->md_mediasize = 0;
		error = kern_ioctl(td, fd, MDIOCDETACH, (void *)mdio);
		/* Ignore errors. We don't care. */
		error = ERANGE;
		goto out;
	}

	root_mount_mddev = mdio->md_unit;
	printf(MD_NAME "%u attached to %s\n", root_mount_mddev, mdio->md_file);

	error = kern_close(td, fd);

 out:
	free(mdio, M_TEMP);
	return (error);
}

static int
parse_dir_onfail(char **conf)
{
	char *action;
	int error;

	error = parse_token(conf, &action);
	if (error)
		return (error);

	if (!strcmp(action, "continue"))
		root_mount_onfail = A_CONTINUE;
	else if (!strcmp(action, "panic"))
		root_mount_onfail = A_PANIC;
	else if (!strcmp(action, "reboot"))
		root_mount_onfail = A_REBOOT;
	else if (!strcmp(action, "retry"))
		root_mount_onfail = A_RETRY;
	else {
		printf("rootmount: %s: unknown action\n", action);
		error = EINVAL;
	}

	free(action, M_TEMP);
	return (0);
}

static int
parse_dir_timeout(char **conf)
{
	char *tok, *endtok;
	long secs;
	int error;

	error = parse_token(conf, &tok);
	if (error)
		return (error);

	secs = strtol(tok, &endtok, 0);
	error = (secs < 0 || *endtok != '\0') ? EINVAL : 0;
	if (!error)
		root_mount_timeout = secs;
	free(tok, M_TEMP);
	return (error);
}

static int
parse_directive(char **conf)
{
	char *dir;
	int error;

	error = parse_token(conf, &dir);
	if (error)
		return (error);

	if (strcmp(dir, ".ask") == 0)
		error = parse_dir_ask(conf);
	else if (strcmp(dir, ".md") == 0)
		error = parse_dir_md(conf);
	else if (strcmp(dir, ".onfail") == 0)
		error = parse_dir_onfail(conf);
	else if (strcmp(dir, ".timeout") == 0)
		error = parse_dir_timeout(conf);
	else {
		printf("mountroot: invalid directive `%s'\n", dir);
		/* Ignore the rest of the line. */
		(void)parse_skipto(conf, '\n');
		error = EINVAL;
	}
	free(dir, M_TEMP);
	return (error);
}

static int
parse_mount_dev_present(const char *dev)
{
	struct nameidata nd;
	int error;

	NDINIT(&nd, LOOKUP, FOLLOW | LOCKLEAF, UIO_SYSSPACE, dev, curthread);
	error = namei(&nd);
	if (!error)
		vput(nd.ni_vp);
	NDFREE(&nd, NDF_ONLY_PNBUF);
	return (error != 0) ? 0 : 1;
}

#define	ERRMSGL	255
static int
parse_mount(char **conf)
{
	char *errmsg;
	struct mntarg *ma;
	char *dev, *fs, *opts, *tok;
	int delay, error, timeout;

	error = parse_token(conf, &tok);
	if (error)
		return (error);
	fs = tok;
	error = parse_skipto(&tok, ':');
	if (error) {
		free(fs, M_TEMP);
		return (error);
	}
	parse_poke(&tok, '\0');
	parse_advance(&tok);
	dev = tok;

	if (root_mount_mddev != -1) {
		/* Handle substitution for the md unit number. */
		tok = strstr(dev, "md#");
		if (tok != NULL)
			tok[2] = '0' + root_mount_mddev;
	}

	/* Parse options. */
	error = parse_token(conf, &tok);
	opts = (error == 0) ? tok : NULL;

	printf("Trying to mount root from %s:%s [%s]...\n", fs, dev,
	    (opts != NULL) ? opts : "");

	errmsg = malloc(ERRMSGL, M_TEMP, M_WAITOK | M_ZERO);

	if (vfs_byname(fs) == NULL) {
		strlcpy(errmsg, "unknown file system", ERRMSGL);
		error = ENOENT;
		goto out;
	}

	error = vfs_mountroot_wait_if_neccessary(fs, dev);
	if (error != 0)
		goto out;

	delay = hz / 10;
	timeout = root_mount_timeout * hz;

	for (;;) {
		ma = NULL;
		ma = mount_arg(ma, "fstype", fs, -1);
		ma = mount_arg(ma, "fspath", "/", -1);
		ma = mount_arg(ma, "from", dev, -1);
		ma = mount_arg(ma, "errmsg", errmsg, ERRMSGL);
		ma = mount_arg(ma, "ro", NULL, 0);
		ma = parse_mountroot_options(ma, opts);

		error = kernel_mount(ma, MNT_ROOTFS);
		if (error == 0 || timeout <= 0)
			break;

		if (root_mount_timeout * hz == timeout ||
		    (bootverbose && timeout % hz == 0)) {
			printf("Mounting from %s:%s failed with error %d; "
			    "retrying for %d more second%s\n", fs, dev, error,
			    timeout / hz, (timeout / hz > 1) ? "s" : "");
		}
		pause("rmretry", delay);
		timeout -= delay;
	}
 out:
	if (error) {
		printf("Mounting from %s:%s failed with error %d",
		    fs, dev, error);
		if (errmsg[0] != '\0')
			printf(": %s", errmsg);
		printf(".\n");
	}
	free(fs, M_TEMP);
	free(errmsg, M_TEMP);
	if (opts != NULL)
		free(opts, M_TEMP);
	/* kernel_mount can return -1 on error. */
	return ((error < 0) ? EDOOFUS : error);
}
#undef ERRMSGL

static int
vfs_mountroot_parse(struct sbuf *sb, struct mount *mpdevfs)
{
	struct mount *mp;
	char *conf;
	int error;

	root_mount_mddev = -1;

retry:
	conf = sbuf_data(sb);
	mp = TAILQ_NEXT(mpdevfs, mnt_list);
	error = (mp == NULL) ? 0 : EDOOFUS;
	root_mount_onfail = A_CONTINUE;
	while (mp == NULL) {
		error = parse_skipto(&conf, CC_NONWHITESPACE);
		if (error == PE_EOL) {
			parse_advance(&conf);
			continue;
		}
		if (error < 0)
			break;
		switch (parse_peek(&conf)) {
		case '#':
			error = parse_skipto(&conf, '\n');
			break;
		case '.':
			error = parse_directive(&conf);
			break;
		default:
			error = parse_mount(&conf);
			if (error == -1) {
				printf("mountroot: invalid file system "
				    "specification.\n");
				error = 0;
			}
			break;
		}
		if (error < 0)
			break;
		/* Ignore any trailing garbage on the line. */
		if (parse_peek(&conf) != '\n') {
			printf("mountroot: advancing to next directive...\n");
			(void)parse_skipto(&conf, '\n');
		}
		mp = TAILQ_NEXT(mpdevfs, mnt_list);
	}
	if (mp != NULL)
		return (0);

	/*
	 * We failed to mount (a new) root.
	 */
	switch (root_mount_onfail) {
	case A_CONTINUE:
		break;
	case A_PANIC:
		panic("mountroot: unable to (re-)mount root.");
		/* NOTREACHED */
	case A_RETRY:
		goto retry;
	case A_REBOOT:
		kern_reboot(RB_NOSYNC);
		/* NOTREACHED */
	}

	return (error);
}

static void
vfs_mountroot_conf0(struct sbuf *sb)
{
	char *s, *tok, *mnt, *opt;
	int error;

	sbuf_printf(sb, ".onfail panic\n");
	sbuf_printf(sb, ".timeout %d\n", root_mount_timeout);
	if (boothowto & RB_ASKNAME)
		sbuf_printf(sb, ".ask\n");
#ifdef ROOTDEVNAME
	if (boothowto & RB_DFLTROOT)
		sbuf_printf(sb, "%s\n", ROOTDEVNAME);
#endif
	if (boothowto & RB_CDROM) {
		sbuf_printf(sb, "cd9660:/dev/cd0 ro\n");
		sbuf_printf(sb, ".timeout 0\n");
		sbuf_printf(sb, "cd9660:/dev/cd1 ro\n");
		sbuf_printf(sb, ".timeout %d\n", root_mount_timeout);
	}
	s = kern_getenv("vfs.root.mountfrom");
	if (s != NULL) {
		opt = kern_getenv("vfs.root.mountfrom.options");
		tok = s;
		error = parse_token(&tok, &mnt);
		while (!error) {
			sbuf_printf(sb, "%s %s\n", mnt,
			    (opt != NULL) ? opt : "");
			free(mnt, M_TEMP);
			error = parse_token(&tok, &mnt);
		}
		if (opt != NULL)
			freeenv(opt);
		freeenv(s);
	}
	if (rootdevnames[0] != NULL)
		sbuf_printf(sb, "%s\n", rootdevnames[0]);
	if (rootdevnames[1] != NULL)
		sbuf_printf(sb, "%s\n", rootdevnames[1]);
#ifdef ROOTDEVNAME
	if (!(boothowto & RB_DFLTROOT))
		sbuf_printf(sb, "%s\n", ROOTDEVNAME);
#endif
	if (!(boothowto & RB_ASKNAME))
		sbuf_printf(sb, ".ask\n");
}

static int
vfs_mountroot_readconf(struct thread *td, struct sbuf *sb)
{
	static char buf[128];
	struct nameidata nd;
	off_t ofs;
	ssize_t resid;
	int error, flags, len;

	NDINIT(&nd, LOOKUP, FOLLOW, UIO_SYSSPACE, "/.mount.conf", td);
	flags = FREAD;
	error = vn_open(&nd, &flags, 0, NULL);
	if (error)
		return (error);

	NDFREE(&nd, NDF_ONLY_PNBUF);
	ofs = 0;
	len = sizeof(buf) - 1;
	while (1) {
		error = vn_rdwr(UIO_READ, nd.ni_vp, buf, len, ofs,
		    UIO_SYSSPACE, IO_NODELOCKED, td->td_ucred,
		    NOCRED, &resid, td);
		if (error)
			break;
		if (resid == len)
			break;
		buf[len - resid] = 0;
		sbuf_printf(sb, "%s", buf);
		ofs += len - resid;
	}

	VOP_UNLOCK(nd.ni_vp, 0);
	vn_close(nd.ni_vp, FREAD, td->td_ucred, td);
	return (error);
}

static void
vfs_mountroot_wait(void)
{
	struct root_hold_token *h;
	struct timeval lastfail;
	int curfail;

	TSENTER();

	curfail = 0;
	while (1) {
		g_waitidle();
		mtx_lock(&root_holds_mtx);
		if (LIST_EMPTY(&root_holds)) {
			mtx_unlock(&root_holds_mtx);
			break;
		}
		if (ppsratecheck(&lastfail, &curfail, 1)) {
			printf("Root mount waiting for:");
			LIST_FOREACH(h, &root_holds, list)
				printf(" %s", h->who);
			printf("\n");
		}
		TSWAIT("root mount");
		msleep(&root_holds, &root_holds_mtx, PZERO | PDROP, "roothold",
		    hz);
		TSUNWAIT("root mount");
	}

	TSEXIT();
}

static int
vfs_mountroot_wait_if_neccessary(const char *fs, const char *dev)
{
	int delay, timeout;

	/*
	 * In case of ZFS and NFS we don't have a way to wait for
	 * specific device.  Also do the wait if the user forced that
	 * behaviour by setting vfs.root_mount_always_wait=1.
	 */
	if (strcmp(fs, "zfs") == 0 || strstr(fs, "nfs") != NULL ||
	    dev[0] == '\0' || root_mount_always_wait != 0) {
		vfs_mountroot_wait();
		return (0);
	}

	/*
	 * Otherwise, no point in waiting if the device is already there.
	 * Note that we must wait for GEOM to finish reconfiguring itself,
	 * eg for geom_part(4) to finish tasting.
	 */
	g_waitidle();
	if (parse_mount_dev_present(dev))
		return (0);

	/*
	 * No luck.  Let's wait.  This code looks weird, but it's that way
	 * to behave exactly as it used to work before.
	 */
	vfs_mountroot_wait();
	printf("mountroot: waiting for device %s...\n", dev);
	delay = hz / 10;
	timeout = root_mount_timeout * hz;
	do {
		pause("rmdev", delay);
		timeout -= delay;
	} while (timeout > 0 && !parse_mount_dev_present(dev));

	if (timeout <= 0)
		return (ENODEV);

	return (0);
}

void
vfs_mountroot(void)
{
	struct mount *mp;
	struct sbuf *sb;
	struct thread *td;
	time_t timebase;
	int error;
	
	mtx_assert(&Giant, MA_NOTOWNED);

	TSENTER();

	td = curthread;

	sb = sbuf_new_auto();
	vfs_mountroot_conf0(sb);
	sbuf_finish(sb);

	error = vfs_mountroot_devfs(td, &mp);
	while (!error) {
		error = vfs_mountroot_parse(sb, mp);
		if (!error) {
			vfs_mountroot_shuffle(td, mp);
			sbuf_clear(sb);
			error = vfs_mountroot_readconf(td, sb);
			sbuf_finish(sb);
		}
	}

	sbuf_delete(sb);

	/*
	 * Iterate over all currently mounted file systems and use
	 * the time stamp found to check and/or initialize the RTC.
	 * Call inittodr() only once and pass it the largest of the
	 * timestamps we encounter.
	 */
	timebase = 0;
	mtx_lock(&mountlist_mtx);
	mp = TAILQ_FIRST(&mountlist);
	while (mp != NULL) {
		if (mp->mnt_time > timebase)
			timebase = mp->mnt_time;
		mp = TAILQ_NEXT(mp, mnt_list);
	}
	mtx_unlock(&mountlist_mtx);
	inittodr(timebase);

	/* Keep prison0's root in sync with the global rootvnode. */
	mtx_lock(&prison0.pr_mtx);
	prison0.pr_root = rootvnode;
	vref(prison0.pr_root);
	mtx_unlock(&prison0.pr_mtx);

	mtx_lock(&root_holds_mtx);
	atomic_store_rel_int(&root_mount_complete, 1);
	wakeup(&root_mount_complete);
	mtx_unlock(&root_holds_mtx);

	EVENTHANDLER_INVOKE(mountroot);

	TSEXIT();
}

static struct mntarg *
parse_mountroot_options(struct mntarg *ma, const char *options)
{
	char *p;
	char *name, *name_arg;
	char *val, *val_arg;
	char *opts;

	if (options == NULL || options[0] == '\0')
		return (ma);

	p = opts = strdup(options, M_MOUNT);
	if (opts == NULL) {
		return (ma);
	}

	while((name = strsep(&p, ",")) != NULL) {
		if (name[0] == '\0')
			break;

		val = strchr(name, '=');
		if (val != NULL) {
			*val = '\0';
			++val;
		}
		if( strcmp(name, "rw") == 0 ||
		    strcmp(name, "noro") == 0) {
			/*
			 * The first time we mount the root file system,
			 * we need to mount 'ro', so We need to ignore
			 * 'rw' and 'noro' mount options.
			 */
			continue;
		}
		name_arg = strdup(name, M_MOUNT);
		val_arg = NULL;
		if (val != NULL)
			val_arg = strdup(val, M_MOUNT);

		ma = mount_arg(ma, name_arg, val_arg,
		    (val_arg != NULL ? -1 : 0));
	}
	free(opts, M_MOUNT);
	return (ma);
}
