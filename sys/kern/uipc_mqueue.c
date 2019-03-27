/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 David Xu <davidxu@freebsd.org>
 * Copyright (c) 2016-2017 Robert N. M. Watson
 * All rights reserved.
 *
 * Portions of this software were developed by BAE Systems, the University of
 * Cambridge Computer Laboratory, and Memorial University under DARPA/AFRL
 * contract FA8650-15-C-7558 ("CADETS"), as part of the DARPA Transparent
 * Computing (TC) research program.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
 *
 */

/*
 * POSIX message queue implementation.
 *
 * 1) A mqueue filesystem can be mounted, each message queue appears
 *    in mounted directory, user can change queue's permission and
 *    ownership, or remove a queue. Manually creating a file in the
 *    directory causes a message queue to be created in the kernel with
 *    default message queue attributes applied and same name used, this
 *    method is not advocated since mq_open syscall allows user to specify
 *    different attributes. Also the file system can be mounted multiple
 *    times at different mount points but shows same contents.
 *
 * 2) Standard POSIX message queue API. The syscalls do not use vfs layer,
 *    but directly operate on internal data structure, this allows user to
 *    use the IPC facility without having to mount mqueue file system.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_capsicum.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/limits.h>
#include <sys/malloc.h>
#include <sys/buf.h>
#include <sys/capsicum.h>
#include <sys/dirent.h>
#include <sys/event.h>
#include <sys/eventhandler.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/jail.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mount.h>
#include <sys/mqueue.h>
#include <sys/mutex.h>
#include <sys/namei.h>
#include <sys/posix4.h>
#include <sys/poll.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/sysproto.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/syscallsubr.h>
#include <sys/sysent.h>
#include <sys/sx.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>
#include <sys/unistd.h>
#include <sys/user.h>
#include <sys/vnode.h>
#include <machine/atomic.h>

#include <security/audit/audit.h>

FEATURE(p1003_1b_mqueue, "POSIX P1003.1B message queues support");

/*
 * Limits and constants
 */
#define	MQFS_NAMELEN		NAME_MAX
#define MQFS_DELEN		(8 + MQFS_NAMELEN)

/* node types */
typedef enum {
	mqfstype_none = 0,
	mqfstype_root,
	mqfstype_dir,
	mqfstype_this,
	mqfstype_parent,
	mqfstype_file,
	mqfstype_symlink,
} mqfs_type_t;

struct mqfs_node;

/*
 * mqfs_info: describes a mqfs instance
 */
struct mqfs_info {
	struct sx		mi_lock;
	struct mqfs_node	*mi_root;
	struct unrhdr		*mi_unrhdr;
};

struct mqfs_vdata {
	LIST_ENTRY(mqfs_vdata)	mv_link;
	struct mqfs_node	*mv_node;
	struct vnode		*mv_vnode;
	struct task		mv_task;
};

/*
 * mqfs_node: describes a node (file or directory) within a mqfs
 */
struct mqfs_node {
	char			mn_name[MQFS_NAMELEN+1];
	struct mqfs_info	*mn_info;
	struct mqfs_node	*mn_parent;
	LIST_HEAD(,mqfs_node)	mn_children;
	LIST_ENTRY(mqfs_node)	mn_sibling;
	LIST_HEAD(,mqfs_vdata)	mn_vnodes;
	const void		*mn_pr_root;
	int			mn_refcount;
	mqfs_type_t		mn_type;
	int			mn_deleted;
	uint32_t		mn_fileno;
	void			*mn_data;
	struct timespec		mn_birth;
	struct timespec		mn_ctime;
	struct timespec		mn_atime;
	struct timespec		mn_mtime;
	uid_t			mn_uid;
	gid_t			mn_gid;
	int			mn_mode;
};

#define	VTON(vp)	(((struct mqfs_vdata *)((vp)->v_data))->mv_node)
#define VTOMQ(vp) 	((struct mqueue *)(VTON(vp)->mn_data))
#define	VFSTOMQFS(m)	((struct mqfs_info *)((m)->mnt_data))
#define	FPTOMQ(fp)	((struct mqueue *)(((struct mqfs_node *) \
				(fp)->f_data)->mn_data))

TAILQ_HEAD(msgq, mqueue_msg);

struct mqueue;

struct mqueue_notifier {
	LIST_ENTRY(mqueue_notifier)	nt_link;
	struct sigevent			nt_sigev;
	ksiginfo_t			nt_ksi;
	struct proc			*nt_proc;
};

struct mqueue {
	struct mtx	mq_mutex;
	int		mq_flags;
	long		mq_maxmsg;
	long		mq_msgsize;
	long		mq_curmsgs;
	long		mq_totalbytes;
	struct msgq	mq_msgq;
	int		mq_receivers;
	int		mq_senders;
	struct selinfo	mq_rsel;
	struct selinfo	mq_wsel;
	struct mqueue_notifier	*mq_notifier;
};

#define	MQ_RSEL		0x01
#define	MQ_WSEL		0x02

struct mqueue_msg {
	TAILQ_ENTRY(mqueue_msg)	msg_link;
	unsigned int	msg_prio;
	unsigned int	msg_size;
	/* following real data... */
};

static SYSCTL_NODE(_kern, OID_AUTO, mqueue, CTLFLAG_RW, 0,
	"POSIX real time message queue");

static int	default_maxmsg  = 10;
static int	default_msgsize = 1024;

static int	maxmsg = 100;
SYSCTL_INT(_kern_mqueue, OID_AUTO, maxmsg, CTLFLAG_RW,
    &maxmsg, 0, "Default maximum messages in queue");
static int	maxmsgsize = 16384;
SYSCTL_INT(_kern_mqueue, OID_AUTO, maxmsgsize, CTLFLAG_RW,
    &maxmsgsize, 0, "Default maximum message size");
static int	maxmq = 100;
SYSCTL_INT(_kern_mqueue, OID_AUTO, maxmq, CTLFLAG_RW,
    &maxmq, 0, "maximum message queues");
static int	curmq = 0;
SYSCTL_INT(_kern_mqueue, OID_AUTO, curmq, CTLFLAG_RW,
    &curmq, 0, "current message queue number");
static int	unloadable = 0;
static MALLOC_DEFINE(M_MQUEUEDATA, "mqdata", "mqueue data");

static eventhandler_tag exit_tag;

/* Only one instance per-system */
static struct mqfs_info		mqfs_data;
static uma_zone_t		mqnode_zone;
static uma_zone_t		mqueue_zone;
static uma_zone_t		mvdata_zone;
static uma_zone_t		mqnoti_zone;
static struct vop_vector	mqfs_vnodeops;
static struct fileops		mqueueops;
static unsigned			mqfs_osd_jail_slot;

/*
 * Directory structure construction and manipulation
 */
#ifdef notyet
static struct mqfs_node	*mqfs_create_dir(struct mqfs_node *parent,
	const char *name, int namelen, struct ucred *cred, int mode);
static struct mqfs_node	*mqfs_create_link(struct mqfs_node *parent,
	const char *name, int namelen, struct ucred *cred, int mode);
#endif

static struct mqfs_node	*mqfs_create_file(struct mqfs_node *parent,
	const char *name, int namelen, struct ucred *cred, int mode);
static int	mqfs_destroy(struct mqfs_node *mn);
static void	mqfs_fileno_alloc(struct mqfs_info *mi, struct mqfs_node *mn);
static void	mqfs_fileno_free(struct mqfs_info *mi, struct mqfs_node *mn);
static int	mqfs_allocv(struct mount *mp, struct vnode **vpp, struct mqfs_node *pn);
static int	mqfs_prison_remove(void *obj, void *data);

/*
 * Message queue construction and maniplation
 */
static struct mqueue	*mqueue_alloc(const struct mq_attr *attr);
static void	mqueue_free(struct mqueue *mq);
static int	mqueue_send(struct mqueue *mq, const char *msg_ptr,
			size_t msg_len, unsigned msg_prio, int waitok,
			const struct timespec *abs_timeout);
static int	mqueue_receive(struct mqueue *mq, char *msg_ptr,
			size_t msg_len, unsigned *msg_prio, int waitok,
			const struct timespec *abs_timeout);
static int	_mqueue_send(struct mqueue *mq, struct mqueue_msg *msg,
			int timo);
static int	_mqueue_recv(struct mqueue *mq, struct mqueue_msg **msg,
			int timo);
static void	mqueue_send_notification(struct mqueue *mq);
static void	mqueue_fdclose(struct thread *td, int fd, struct file *fp);
static void	mq_proc_exit(void *arg, struct proc *p);

/*
 * kqueue filters
 */
static void	filt_mqdetach(struct knote *kn);
static int	filt_mqread(struct knote *kn, long hint);
static int	filt_mqwrite(struct knote *kn, long hint);

struct filterops mq_rfiltops = {
	.f_isfd = 1,
	.f_detach = filt_mqdetach,
	.f_event = filt_mqread,
};
struct filterops mq_wfiltops = {
	.f_isfd = 1,
	.f_detach = filt_mqdetach,
	.f_event = filt_mqwrite,
};

/*
 * Initialize fileno bitmap
 */
static void
mqfs_fileno_init(struct mqfs_info *mi)
{
	struct unrhdr *up;

	up = new_unrhdr(1, INT_MAX, NULL);
	mi->mi_unrhdr = up;
}

/*
 * Tear down fileno bitmap
 */
static void
mqfs_fileno_uninit(struct mqfs_info *mi)
{
	struct unrhdr *up;

	up = mi->mi_unrhdr;
	mi->mi_unrhdr = NULL;
	delete_unrhdr(up);
}

/*
 * Allocate a file number
 */
static void
mqfs_fileno_alloc(struct mqfs_info *mi, struct mqfs_node *mn)
{
	/* make sure our parent has a file number */
	if (mn->mn_parent && !mn->mn_parent->mn_fileno)
		mqfs_fileno_alloc(mi, mn->mn_parent);

	switch (mn->mn_type) {
	case mqfstype_root:
	case mqfstype_dir:
	case mqfstype_file:
	case mqfstype_symlink:
		mn->mn_fileno = alloc_unr(mi->mi_unrhdr);
		break;
	case mqfstype_this:
		KASSERT(mn->mn_parent != NULL,
		    ("mqfstype_this node has no parent"));
		mn->mn_fileno = mn->mn_parent->mn_fileno;
		break;
	case mqfstype_parent:
		KASSERT(mn->mn_parent != NULL,
		    ("mqfstype_parent node has no parent"));
		if (mn->mn_parent == mi->mi_root) {
			mn->mn_fileno = mn->mn_parent->mn_fileno;
			break;
		}
		KASSERT(mn->mn_parent->mn_parent != NULL,
		    ("mqfstype_parent node has no grandparent"));
		mn->mn_fileno = mn->mn_parent->mn_parent->mn_fileno;
		break;
	default:
		KASSERT(0,
		    ("mqfs_fileno_alloc() called for unknown type node: %d",
			mn->mn_type));
		break;
	}
}

/*
 * Release a file number
 */
static void
mqfs_fileno_free(struct mqfs_info *mi, struct mqfs_node *mn)
{
	switch (mn->mn_type) {
	case mqfstype_root:
	case mqfstype_dir:
	case mqfstype_file:
	case mqfstype_symlink:
		free_unr(mi->mi_unrhdr, mn->mn_fileno);
		break;
	case mqfstype_this:
	case mqfstype_parent:
		/* ignore these, as they don't "own" their file number */
		break;
	default:
		KASSERT(0,
		    ("mqfs_fileno_free() called for unknown type node: %d", 
			mn->mn_type));
		break;
	}
}

static __inline struct mqfs_node *
mqnode_alloc(void)
{
	return uma_zalloc(mqnode_zone, M_WAITOK | M_ZERO);
}

static __inline void
mqnode_free(struct mqfs_node *node)
{
	uma_zfree(mqnode_zone, node);
}

static __inline void
mqnode_addref(struct mqfs_node *node)
{
	atomic_add_int(&node->mn_refcount, 1);
}

static __inline void
mqnode_release(struct mqfs_node *node)
{
	struct mqfs_info *mqfs;
	int old, exp;

	mqfs = node->mn_info;
	old = atomic_fetchadd_int(&node->mn_refcount, -1);
	if (node->mn_type == mqfstype_dir ||
	    node->mn_type == mqfstype_root)
		exp = 3; /* include . and .. */
	else
		exp = 1;
	if (old == exp) {
		int locked = sx_xlocked(&mqfs->mi_lock);
		if (!locked)
			sx_xlock(&mqfs->mi_lock);
		mqfs_destroy(node);
		if (!locked)
			sx_xunlock(&mqfs->mi_lock);
	}
}

/*
 * Add a node to a directory
 */
static int
mqfs_add_node(struct mqfs_node *parent, struct mqfs_node *node)
{
	KASSERT(parent != NULL, ("%s(): parent is NULL", __func__));
	KASSERT(parent->mn_info != NULL,
	    ("%s(): parent has no mn_info", __func__));
	KASSERT(parent->mn_type == mqfstype_dir ||
	    parent->mn_type == mqfstype_root,
	    ("%s(): parent is not a directory", __func__));

	node->mn_info = parent->mn_info;
	node->mn_parent = parent;
	LIST_INIT(&node->mn_children);
	LIST_INIT(&node->mn_vnodes);
	LIST_INSERT_HEAD(&parent->mn_children, node, mn_sibling);
	mqnode_addref(parent);
	return (0);
}

static struct mqfs_node *
mqfs_create_node(const char *name, int namelen, struct ucred *cred, int mode,
	int nodetype)
{
	struct mqfs_node *node;

	node = mqnode_alloc();
	strncpy(node->mn_name, name, namelen);
	node->mn_pr_root = cred->cr_prison->pr_root;
	node->mn_type = nodetype;
	node->mn_refcount = 1;
	vfs_timestamp(&node->mn_birth);
	node->mn_ctime = node->mn_atime = node->mn_mtime
		= node->mn_birth;
	node->mn_uid = cred->cr_uid;
	node->mn_gid = cred->cr_gid;
	node->mn_mode = mode;
	return (node);
}

/*
 * Create a file
 */
static struct mqfs_node *
mqfs_create_file(struct mqfs_node *parent, const char *name, int namelen,
	struct ucred *cred, int mode)
{
	struct mqfs_node *node;

	node = mqfs_create_node(name, namelen, cred, mode, mqfstype_file);
	if (mqfs_add_node(parent, node) != 0) {
		mqnode_free(node);
		return (NULL);
	}
	return (node);
}

/*
 * Add . and .. to a directory
 */
static int
mqfs_fixup_dir(struct mqfs_node *parent)
{
	struct mqfs_node *dir;

	dir = mqnode_alloc();
	dir->mn_name[0] = '.';
	dir->mn_type = mqfstype_this;
	dir->mn_refcount = 1;
	if (mqfs_add_node(parent, dir) != 0) {
		mqnode_free(dir);
		return (-1);
	}

	dir = mqnode_alloc();
	dir->mn_name[0] = dir->mn_name[1] = '.';
	dir->mn_type = mqfstype_parent;
	dir->mn_refcount = 1;

	if (mqfs_add_node(parent, dir) != 0) {
		mqnode_free(dir);
		return (-1);
	}

	return (0);
}

#ifdef notyet

/*
 * Create a directory
 */
static struct mqfs_node *
mqfs_create_dir(struct mqfs_node *parent, const char *name, int namelen,
	struct ucred *cred, int mode)
{
	struct mqfs_node *node;

	node = mqfs_create_node(name, namelen, cred, mode, mqfstype_dir);
	if (mqfs_add_node(parent, node) != 0) {
		mqnode_free(node);
		return (NULL);
	}

	if (mqfs_fixup_dir(node) != 0) {
		mqfs_destroy(node);
		return (NULL);
	}
	return (node);
}

/*
 * Create a symlink
 */
static struct mqfs_node *
mqfs_create_link(struct mqfs_node *parent, const char *name, int namelen,
	struct ucred *cred, int mode)
{
	struct mqfs_node *node;

	node = mqfs_create_node(name, namelen, cred, mode, mqfstype_symlink);
	if (mqfs_add_node(parent, node) != 0) {
		mqnode_free(node);
		return (NULL);
	}
	return (node);
}

#endif

/*
 * Destroy a node or a tree of nodes
 */
static int
mqfs_destroy(struct mqfs_node *node)
{
	struct mqfs_node *parent;

	KASSERT(node != NULL,
	    ("%s(): node is NULL", __func__));
	KASSERT(node->mn_info != NULL,
	    ("%s(): node has no mn_info", __func__));

	/* destroy children */
	if (node->mn_type == mqfstype_dir || node->mn_type == mqfstype_root)
		while (! LIST_EMPTY(&node->mn_children))
			mqfs_destroy(LIST_FIRST(&node->mn_children));

	/* unlink from parent */
	if ((parent = node->mn_parent) != NULL) {
		KASSERT(parent->mn_info == node->mn_info,
		    ("%s(): parent has different mn_info", __func__));
		LIST_REMOVE(node, mn_sibling);
	}

	if (node->mn_fileno != 0)
		mqfs_fileno_free(node->mn_info, node);
	if (node->mn_data != NULL)
		mqueue_free(node->mn_data);
	mqnode_free(node);
	return (0);
}

/*
 * Mount a mqfs instance
 */
static int
mqfs_mount(struct mount *mp)
{
	struct statfs *sbp;

	if (mp->mnt_flag & MNT_UPDATE)
		return (EOPNOTSUPP);

	mp->mnt_data = &mqfs_data;
	MNT_ILOCK(mp);
	mp->mnt_flag |= MNT_LOCAL;
	MNT_IUNLOCK(mp);
	vfs_getnewfsid(mp);

	sbp = &mp->mnt_stat;
	vfs_mountedfrom(mp, "mqueue");
	sbp->f_bsize = PAGE_SIZE;
	sbp->f_iosize = PAGE_SIZE;
	sbp->f_blocks = 1;
	sbp->f_bfree = 0;
	sbp->f_bavail = 0;
	sbp->f_files = 1;
	sbp->f_ffree = 0;
	return (0);
}

/*
 * Unmount a mqfs instance
 */
static int
mqfs_unmount(struct mount *mp, int mntflags)
{
	int error;

	error = vflush(mp, 0, (mntflags & MNT_FORCE) ?  FORCECLOSE : 0,
	    curthread);
	return (error);
}

/*
 * Return a root vnode
 */
static int
mqfs_root(struct mount *mp, int flags, struct vnode **vpp)
{
	struct mqfs_info *mqfs;
	int ret;

	mqfs = VFSTOMQFS(mp);
	ret = mqfs_allocv(mp, vpp, mqfs->mi_root);
	return (ret);
}

/*
 * Return filesystem stats
 */
static int
mqfs_statfs(struct mount *mp, struct statfs *sbp)
{
	/* XXX update statistics */
	return (0);
}

/*
 * Initialize a mqfs instance
 */
static int
mqfs_init(struct vfsconf *vfc)
{
	struct mqfs_node *root;
	struct mqfs_info *mi;
	osd_method_t methods[PR_MAXMETHOD] = {
	    [PR_METHOD_REMOVE] = mqfs_prison_remove,
	};

	mqnode_zone = uma_zcreate("mqnode", sizeof(struct mqfs_node),
		NULL, NULL, NULL, NULL, UMA_ALIGN_PTR, 0);
	mqueue_zone = uma_zcreate("mqueue", sizeof(struct mqueue),
		NULL, NULL, NULL, NULL, UMA_ALIGN_PTR, 0);
	mvdata_zone = uma_zcreate("mvdata",
		sizeof(struct mqfs_vdata), NULL, NULL, NULL,
		NULL, UMA_ALIGN_PTR, 0);
	mqnoti_zone = uma_zcreate("mqnotifier", sizeof(struct mqueue_notifier),
		NULL, NULL, NULL, NULL, UMA_ALIGN_PTR, 0);
	mi = &mqfs_data;
	sx_init(&mi->mi_lock, "mqfs lock");
	/* set up the root diretory */
	root = mqfs_create_node("/", 1, curthread->td_ucred, 01777,
		mqfstype_root);
	root->mn_info = mi;
	LIST_INIT(&root->mn_children);
	LIST_INIT(&root->mn_vnodes);
	mi->mi_root = root;
	mqfs_fileno_init(mi);
	mqfs_fileno_alloc(mi, root);
	mqfs_fixup_dir(root);
	exit_tag = EVENTHANDLER_REGISTER(process_exit, mq_proc_exit, NULL,
	    EVENTHANDLER_PRI_ANY);
	mq_fdclose = mqueue_fdclose;
	p31b_setcfg(CTL_P1003_1B_MESSAGE_PASSING, _POSIX_MESSAGE_PASSING);
	mqfs_osd_jail_slot = osd_jail_register(NULL, methods);
	return (0);
}

/*
 * Destroy a mqfs instance
 */
static int
mqfs_uninit(struct vfsconf *vfc)
{
	struct mqfs_info *mi;

	if (!unloadable)
		return (EOPNOTSUPP);
	osd_jail_deregister(mqfs_osd_jail_slot);
	EVENTHANDLER_DEREGISTER(process_exit, exit_tag);
	mi = &mqfs_data;
	mqfs_destroy(mi->mi_root);
	mi->mi_root = NULL;
	mqfs_fileno_uninit(mi);
	sx_destroy(&mi->mi_lock);
	uma_zdestroy(mqnode_zone);
	uma_zdestroy(mqueue_zone);
	uma_zdestroy(mvdata_zone);
	uma_zdestroy(mqnoti_zone);
	return (0);
}

/*
 * task routine
 */
static void
do_recycle(void *context, int pending __unused)
{
	struct vnode *vp = (struct vnode *)context;

	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	vrecycle(vp);
	VOP_UNLOCK(vp, 0);
	vdrop(vp);
}

/*
 * Allocate a vnode
 */
static int
mqfs_allocv(struct mount *mp, struct vnode **vpp, struct mqfs_node *pn)
{
	struct mqfs_vdata *vd;
	struct mqfs_info  *mqfs;
	struct vnode *newvpp;
	int error;

	mqfs = pn->mn_info;
	*vpp = NULL;
	sx_xlock(&mqfs->mi_lock);
	LIST_FOREACH(vd, &pn->mn_vnodes, mv_link) {
		if (vd->mv_vnode->v_mount == mp) {
			vhold(vd->mv_vnode);
			break;
		}
	}

	if (vd != NULL) {
found:
		*vpp = vd->mv_vnode;
		sx_xunlock(&mqfs->mi_lock);
		error = vget(*vpp, LK_RETRY | LK_EXCLUSIVE, curthread);
		vdrop(*vpp);
		return (error);
	}
	sx_xunlock(&mqfs->mi_lock);

	error = getnewvnode("mqueue", mp, &mqfs_vnodeops, &newvpp);
	if (error)
		return (error);
	vn_lock(newvpp, LK_EXCLUSIVE | LK_RETRY);
	error = insmntque(newvpp, mp);
	if (error != 0)
		return (error);

	sx_xlock(&mqfs->mi_lock);
	/*
	 * Check if it has already been allocated
	 * while we were blocked.
	 */
	LIST_FOREACH(vd, &pn->mn_vnodes, mv_link) {
		if (vd->mv_vnode->v_mount == mp) {
			vhold(vd->mv_vnode);
			sx_xunlock(&mqfs->mi_lock);

			vgone(newvpp);
			vput(newvpp);
			goto found;
		}
	}

	*vpp = newvpp;

	vd = uma_zalloc(mvdata_zone, M_WAITOK);
	(*vpp)->v_data = vd;
	vd->mv_vnode = *vpp;
	vd->mv_node = pn;
	TASK_INIT(&vd->mv_task, 0, do_recycle, *vpp);
	LIST_INSERT_HEAD(&pn->mn_vnodes, vd, mv_link);
	mqnode_addref(pn);
	switch (pn->mn_type) {
	case mqfstype_root:
		(*vpp)->v_vflag = VV_ROOT;
		/* fall through */
	case mqfstype_dir:
	case mqfstype_this:
	case mqfstype_parent:
		(*vpp)->v_type = VDIR;
		break;
	case mqfstype_file:
		(*vpp)->v_type = VREG;
		break;
	case mqfstype_symlink:
		(*vpp)->v_type = VLNK;
		break;
	case mqfstype_none:
		KASSERT(0, ("mqfs_allocf called for null node\n"));
	default:
		panic("%s has unexpected type: %d", pn->mn_name, pn->mn_type);
	}
	sx_xunlock(&mqfs->mi_lock);
	return (0);
}

/* 
 * Search a directory entry
 */
static struct mqfs_node *
mqfs_search(struct mqfs_node *pd, const char *name, int len, struct ucred *cred)
{
	struct mqfs_node *pn;
	const void *pr_root;

	sx_assert(&pd->mn_info->mi_lock, SX_LOCKED);
	pr_root = cred->cr_prison->pr_root;
	LIST_FOREACH(pn, &pd->mn_children, mn_sibling) {
		/* Only match names within the same prison root directory */
		if ((pn->mn_pr_root == NULL || pn->mn_pr_root == pr_root) &&
		    strncmp(pn->mn_name, name, len) == 0 &&
		    pn->mn_name[len] == '\0')
			return (pn);
	}
	return (NULL);
}

/*
 * Look up a file or directory.
 */
static int
mqfs_lookupx(struct vop_cachedlookup_args *ap)
{
	struct componentname *cnp;
	struct vnode *dvp, **vpp;
	struct mqfs_node *pd;
	struct mqfs_node *pn;
	struct mqfs_info *mqfs;
	int nameiop, flags, error, namelen;
	char *pname;
	struct thread *td;

	cnp = ap->a_cnp;
	vpp = ap->a_vpp;
	dvp = ap->a_dvp;
	pname = cnp->cn_nameptr;
	namelen = cnp->cn_namelen;
	td = cnp->cn_thread;
	flags = cnp->cn_flags;
	nameiop = cnp->cn_nameiop;
	pd = VTON(dvp);
	pn = NULL;
	mqfs = pd->mn_info;
	*vpp = NULLVP;

	if (dvp->v_type != VDIR)
		return (ENOTDIR);

	error = VOP_ACCESS(dvp, VEXEC, cnp->cn_cred, cnp->cn_thread);
	if (error)
		return (error);

	/* shortcut: check if the name is too long */
	if (cnp->cn_namelen >= MQFS_NAMELEN)
		return (ENOENT);

	/* self */
	if (namelen == 1 && pname[0] == '.') {
		if ((flags & ISLASTCN) && nameiop != LOOKUP)
			return (EINVAL);
		pn = pd;
		*vpp = dvp;
		VREF(dvp);
		return (0);
	}

	/* parent */
	if (cnp->cn_flags & ISDOTDOT) {
		if (dvp->v_vflag & VV_ROOT)
			return (EIO);
		if ((flags & ISLASTCN) && nameiop != LOOKUP)
			return (EINVAL);
		VOP_UNLOCK(dvp, 0);
		KASSERT(pd->mn_parent, ("non-root directory has no parent"));
		pn = pd->mn_parent;
		error = mqfs_allocv(dvp->v_mount, vpp, pn);
		vn_lock(dvp, LK_EXCLUSIVE | LK_RETRY);
		return (error);
	}

	/* named node */
	sx_xlock(&mqfs->mi_lock);
	pn = mqfs_search(pd, pname, namelen, cnp->cn_cred);
	if (pn != NULL)
		mqnode_addref(pn);
	sx_xunlock(&mqfs->mi_lock);
	
	/* found */
	if (pn != NULL) {
		/* DELETE */
		if (nameiop == DELETE && (flags & ISLASTCN)) {
			error = VOP_ACCESS(dvp, VWRITE, cnp->cn_cred, td);
			if (error) {
				mqnode_release(pn);
				return (error);
			}
			if (*vpp == dvp) {
				VREF(dvp);
				*vpp = dvp;
				mqnode_release(pn);
				return (0);
			}
		}

		/* allocate vnode */
		error = mqfs_allocv(dvp->v_mount, vpp, pn);
		mqnode_release(pn);
		if (error == 0 && cnp->cn_flags & MAKEENTRY)
			cache_enter(dvp, *vpp, cnp);
		return (error);
	}
	
	/* not found */

	/* will create a new entry in the directory ? */
	if ((nameiop == CREATE || nameiop == RENAME) && (flags & LOCKPARENT)
	    && (flags & ISLASTCN)) {
		error = VOP_ACCESS(dvp, VWRITE, cnp->cn_cred, td);
		if (error)
			return (error);
		cnp->cn_flags |= SAVENAME;
		return (EJUSTRETURN);
	}
	return (ENOENT);
}

#if 0
struct vop_lookup_args {
	struct vop_generic_args a_gen;
	struct vnode *a_dvp;
	struct vnode **a_vpp;
	struct componentname *a_cnp;
};
#endif

/*
 * vnode lookup operation
 */
static int
mqfs_lookup(struct vop_cachedlookup_args *ap)
{
	int rc;

	rc = mqfs_lookupx(ap);
	return (rc);
}

#if 0
struct vop_create_args {
	struct vnode *a_dvp;
	struct vnode **a_vpp;
	struct componentname *a_cnp;
	struct vattr *a_vap;
};
#endif

/*
 * vnode creation operation
 */
static int
mqfs_create(struct vop_create_args *ap)
{
	struct mqfs_info *mqfs = VFSTOMQFS(ap->a_dvp->v_mount);
	struct componentname *cnp = ap->a_cnp;
	struct mqfs_node *pd;
	struct mqfs_node *pn;
	struct mqueue *mq;
	int error;

	pd = VTON(ap->a_dvp);
	if (pd->mn_type != mqfstype_root && pd->mn_type != mqfstype_dir)
		return (ENOTDIR);
	mq = mqueue_alloc(NULL);
	if (mq == NULL)
		return (EAGAIN);
	sx_xlock(&mqfs->mi_lock);
	if ((cnp->cn_flags & HASBUF) == 0)
		panic("%s: no name", __func__);
	pn = mqfs_create_file(pd, cnp->cn_nameptr, cnp->cn_namelen,
		cnp->cn_cred, ap->a_vap->va_mode);
	if (pn == NULL) {
		sx_xunlock(&mqfs->mi_lock);
		error = ENOSPC;
	} else {
		mqnode_addref(pn);
		sx_xunlock(&mqfs->mi_lock);
		error = mqfs_allocv(ap->a_dvp->v_mount, ap->a_vpp, pn);
		mqnode_release(pn);
		if (error)
			mqfs_destroy(pn);
		else
			pn->mn_data = mq;
	}
	if (error)
		mqueue_free(mq);
	return (error);
}

/*
 * Remove an entry
 */
static
int do_unlink(struct mqfs_node *pn, struct ucred *ucred)
{
	struct mqfs_node *parent;
	struct mqfs_vdata *vd;
	int error = 0;

	sx_assert(&pn->mn_info->mi_lock, SX_LOCKED);

	if (ucred->cr_uid != pn->mn_uid &&
	    (error = priv_check_cred(ucred, PRIV_MQ_ADMIN)) != 0)
		error = EACCES;
	else if (!pn->mn_deleted) {
		parent = pn->mn_parent;
		pn->mn_parent = NULL;
		pn->mn_deleted = 1;
		LIST_REMOVE(pn, mn_sibling);
		LIST_FOREACH(vd, &pn->mn_vnodes, mv_link) {
			cache_purge(vd->mv_vnode);
			vhold(vd->mv_vnode);
			taskqueue_enqueue(taskqueue_thread, &vd->mv_task);
		}
		mqnode_release(pn);
		mqnode_release(parent);
	} else
		error = ENOENT;
	return (error);
}

#if 0
struct vop_remove_args {
	struct vnode *a_dvp;
	struct vnode *a_vp;
	struct componentname *a_cnp;
};
#endif

/*
 * vnode removal operation
 */
static int
mqfs_remove(struct vop_remove_args *ap)
{
	struct mqfs_info *mqfs = VFSTOMQFS(ap->a_dvp->v_mount);
	struct mqfs_node *pn;
	int error;

	if (ap->a_vp->v_type == VDIR)
                return (EPERM);
	pn = VTON(ap->a_vp);
	sx_xlock(&mqfs->mi_lock);
	error = do_unlink(pn, ap->a_cnp->cn_cred);
	sx_xunlock(&mqfs->mi_lock);
	return (error);
}

#if 0
struct vop_inactive_args {
	struct vnode *a_vp;
	struct thread *a_td;
};
#endif

static int
mqfs_inactive(struct vop_inactive_args *ap)
{
	struct mqfs_node *pn = VTON(ap->a_vp);

	if (pn->mn_deleted)
		vrecycle(ap->a_vp);
	return (0);
}

#if 0
struct vop_reclaim_args {
	struct vop_generic_args a_gen;
	struct vnode *a_vp;
	struct thread *a_td;
};
#endif

static int
mqfs_reclaim(struct vop_reclaim_args *ap)
{
	struct mqfs_info *mqfs = VFSTOMQFS(ap->a_vp->v_mount);
	struct vnode *vp = ap->a_vp;
	struct mqfs_node *pn;
	struct mqfs_vdata *vd;

	vd = vp->v_data;
	pn = vd->mv_node;
	sx_xlock(&mqfs->mi_lock);
	vp->v_data = NULL;
	LIST_REMOVE(vd, mv_link);
	uma_zfree(mvdata_zone, vd);
	mqnode_release(pn);
	sx_xunlock(&mqfs->mi_lock);
	return (0);
}

#if 0
struct vop_open_args {
	struct vop_generic_args a_gen;
	struct vnode *a_vp;
	int a_mode;
	struct ucred *a_cred;
	struct thread *a_td;
	struct file *a_fp;
};
#endif

static int
mqfs_open(struct vop_open_args *ap)
{
	return (0);
}

#if 0
struct vop_close_args {
	struct vop_generic_args a_gen;
	struct vnode *a_vp;
	int a_fflag;
	struct ucred *a_cred;
	struct thread *a_td;
};
#endif

static int
mqfs_close(struct vop_close_args *ap)
{
	return (0);
}

#if 0
struct vop_access_args {
	struct vop_generic_args a_gen;
	struct vnode *a_vp;
	accmode_t a_accmode;
	struct ucred *a_cred;
	struct thread *a_td;
};
#endif

/*
 * Verify permissions
 */
static int
mqfs_access(struct vop_access_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vattr vattr;
	int error;

	error = VOP_GETATTR(vp, &vattr, ap->a_cred);
	if (error)
		return (error);
	error = vaccess(vp->v_type, vattr.va_mode, vattr.va_uid,
	    vattr.va_gid, ap->a_accmode, ap->a_cred, NULL);
	return (error);
}

#if 0
struct vop_getattr_args {
	struct vop_generic_args a_gen;
	struct vnode *a_vp;
	struct vattr *a_vap;
	struct ucred *a_cred;
};
#endif

/*
 * Get file attributes
 */
static int
mqfs_getattr(struct vop_getattr_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct mqfs_node *pn = VTON(vp);
	struct vattr *vap = ap->a_vap;
	int error = 0;

	vap->va_type = vp->v_type;
	vap->va_mode = pn->mn_mode;
	vap->va_nlink = 1;
	vap->va_uid = pn->mn_uid;
	vap->va_gid = pn->mn_gid;
	vap->va_fsid = vp->v_mount->mnt_stat.f_fsid.val[0];
	vap->va_fileid = pn->mn_fileno;
	vap->va_size = 0;
	vap->va_blocksize = PAGE_SIZE;
	vap->va_bytes = vap->va_size = 0;
	vap->va_atime = pn->mn_atime;
	vap->va_mtime = pn->mn_mtime;
	vap->va_ctime = pn->mn_ctime;
	vap->va_birthtime = pn->mn_birth;
	vap->va_gen = 0;
	vap->va_flags = 0;
	vap->va_rdev = NODEV;
	vap->va_bytes = 0;
	vap->va_filerev = 0;
	return (error);
}

#if 0
struct vop_setattr_args {
	struct vop_generic_args a_gen;
	struct vnode *a_vp;
	struct vattr *a_vap;
	struct ucred *a_cred;
};
#endif
/*
 * Set attributes
 */
static int
mqfs_setattr(struct vop_setattr_args *ap)
{
	struct mqfs_node *pn;
	struct vattr *vap;
	struct vnode *vp;
	struct thread *td;
	int c, error;
	uid_t uid;
	gid_t gid;

	td = curthread;
	vap = ap->a_vap;
	vp = ap->a_vp;
	if ((vap->va_type != VNON) ||
	    (vap->va_nlink != VNOVAL) ||
	    (vap->va_fsid != VNOVAL) ||
	    (vap->va_fileid != VNOVAL) ||
	    (vap->va_blocksize != VNOVAL) ||
	    (vap->va_flags != VNOVAL && vap->va_flags != 0) ||
	    (vap->va_rdev != VNOVAL) ||
	    ((int)vap->va_bytes != VNOVAL) ||
	    (vap->va_gen != VNOVAL)) {
		return (EINVAL);
	}

	pn = VTON(vp);

	error = c = 0;
	if (vap->va_uid == (uid_t)VNOVAL)
		uid = pn->mn_uid;
	else
		uid = vap->va_uid;
	if (vap->va_gid == (gid_t)VNOVAL)
		gid = pn->mn_gid;
	else
		gid = vap->va_gid;

	if (uid != pn->mn_uid || gid != pn->mn_gid) {
		/*
		 * To modify the ownership of a file, must possess VADMIN
		 * for that file.
		 */
		if ((error = VOP_ACCESS(vp, VADMIN, ap->a_cred, td)))
			return (error);

		/*
		 * XXXRW: Why is there a privilege check here: shouldn't the
		 * check in VOP_ACCESS() be enough?  Also, are the group bits
		 * below definitely right?
		 */
		if (((ap->a_cred->cr_uid != pn->mn_uid) || uid != pn->mn_uid ||
		    (gid != pn->mn_gid && !groupmember(gid, ap->a_cred))) &&
		    (error = priv_check(td, PRIV_MQ_ADMIN)) != 0)
			return (error);
		pn->mn_uid = uid;
		pn->mn_gid = gid;
		c = 1;
	}

	if (vap->va_mode != (mode_t)VNOVAL) {
		if ((ap->a_cred->cr_uid != pn->mn_uid) &&
		    (error = priv_check(td, PRIV_MQ_ADMIN)))
			return (error);
		pn->mn_mode = vap->va_mode;
		c = 1;
	}

	if (vap->va_atime.tv_sec != VNOVAL || vap->va_mtime.tv_sec != VNOVAL) {
		/* See the comment in ufs_vnops::ufs_setattr(). */
		if ((error = VOP_ACCESS(vp, VADMIN, ap->a_cred, td)) &&
		    ((vap->va_vaflags & VA_UTIMES_NULL) == 0 ||
		    (error = VOP_ACCESS(vp, VWRITE, ap->a_cred, td))))
			return (error);
		if (vap->va_atime.tv_sec != VNOVAL) {
			pn->mn_atime = vap->va_atime;
		}
		if (vap->va_mtime.tv_sec != VNOVAL) {
			pn->mn_mtime = vap->va_mtime;
		}
		c = 1;
	}
	if (c) {
		vfs_timestamp(&pn->mn_ctime);
	}
	return (0);
}

#if 0
struct vop_read_args {
	struct vop_generic_args a_gen;
	struct vnode *a_vp;
	struct uio *a_uio;
	int a_ioflag;
	struct ucred *a_cred;
};
#endif

/*
 * Read from a file
 */
static int
mqfs_read(struct vop_read_args *ap)
{
	char buf[80];
	struct vnode *vp = ap->a_vp;
	struct uio *uio = ap->a_uio;
	struct mqueue *mq;
	int len, error;

	if (vp->v_type != VREG)
		return (EINVAL);

	mq = VTOMQ(vp);
	snprintf(buf, sizeof(buf),
		"QSIZE:%-10ld MAXMSG:%-10ld CURMSG:%-10ld MSGSIZE:%-10ld\n",
		mq->mq_totalbytes,
		mq->mq_maxmsg,
		mq->mq_curmsgs,
		mq->mq_msgsize);
	buf[sizeof(buf)-1] = '\0';
	len = strlen(buf);
	error = uiomove_frombuf(buf, len, uio);
	return (error);
}

#if 0
struct vop_readdir_args {
	struct vop_generic_args a_gen;
	struct vnode *a_vp;
	struct uio *a_uio;
	struct ucred *a_cred;
	int *a_eofflag;
	int *a_ncookies;
	u_long **a_cookies;
};
#endif

/*
 * Return directory entries.
 */
static int
mqfs_readdir(struct vop_readdir_args *ap)
{
	struct vnode *vp;
	struct mqfs_info *mi;
	struct mqfs_node *pd;
	struct mqfs_node *pn;
	struct dirent entry;
	struct uio *uio;
	const void *pr_root;
	int *tmp_ncookies = NULL;
	off_t offset;
	int error, i;

	vp = ap->a_vp;
	mi = VFSTOMQFS(vp->v_mount);
	pd = VTON(vp);
	uio = ap->a_uio;

	if (vp->v_type != VDIR)
		return (ENOTDIR);

	if (uio->uio_offset < 0)
		return (EINVAL);

	if (ap->a_ncookies != NULL) {
		tmp_ncookies = ap->a_ncookies;
		*ap->a_ncookies = 0;
		ap->a_ncookies = NULL;
        }

	error = 0;
	offset = 0;

	pr_root = ap->a_cred->cr_prison->pr_root;
	sx_xlock(&mi->mi_lock);

	LIST_FOREACH(pn, &pd->mn_children, mn_sibling) {
		entry.d_reclen = sizeof(entry);

		/*
		 * Only show names within the same prison root directory
		 * (or not associated with a prison, e.g. "." and "..").
		 */
		if (pn->mn_pr_root != NULL && pn->mn_pr_root != pr_root)
			continue;
		if (!pn->mn_fileno)
			mqfs_fileno_alloc(mi, pn);
		entry.d_fileno = pn->mn_fileno;
		for (i = 0; i < MQFS_NAMELEN - 1 && pn->mn_name[i] != '\0'; ++i)
			entry.d_name[i] = pn->mn_name[i];
		entry.d_namlen = i;
		switch (pn->mn_type) {
		case mqfstype_root:
		case mqfstype_dir:
		case mqfstype_this:
		case mqfstype_parent:
			entry.d_type = DT_DIR;
			break;
		case mqfstype_file:
			entry.d_type = DT_REG;
			break;
		case mqfstype_symlink:
			entry.d_type = DT_LNK;
			break;
		default:
			panic("%s has unexpected node type: %d", pn->mn_name,
				pn->mn_type);
		}
		dirent_terminate(&entry);
		if (entry.d_reclen > uio->uio_resid)
                        break;
		if (offset >= uio->uio_offset) {
			error = vfs_read_dirent(ap, &entry, offset);
                        if (error)
                                break;
                }
                offset += entry.d_reclen;
	}
	sx_xunlock(&mi->mi_lock);

	uio->uio_offset = offset;

	if (tmp_ncookies != NULL)
		ap->a_ncookies = tmp_ncookies;

	return (error);
}

#ifdef notyet

#if 0
struct vop_mkdir_args {
	struct vnode *a_dvp;
	struvt vnode **a_vpp;
	struvt componentname *a_cnp;
	struct vattr *a_vap;
};
#endif

/*
 * Create a directory.
 */
static int
mqfs_mkdir(struct vop_mkdir_args *ap)
{
	struct mqfs_info *mqfs = VFSTOMQFS(ap->a_dvp->v_mount);
	struct componentname *cnp = ap->a_cnp;
	struct mqfs_node *pd = VTON(ap->a_dvp);
	struct mqfs_node *pn;
	int error;

	if (pd->mn_type != mqfstype_root && pd->mn_type != mqfstype_dir)
		return (ENOTDIR);
	sx_xlock(&mqfs->mi_lock);
	if ((cnp->cn_flags & HASBUF) == 0)
		panic("%s: no name", __func__);
	pn = mqfs_create_dir(pd, cnp->cn_nameptr, cnp->cn_namelen,
		ap->a_vap->cn_cred, ap->a_vap->va_mode);
	if (pn != NULL)
		mqnode_addref(pn);
	sx_xunlock(&mqfs->mi_lock);
	if (pn == NULL) {
		error = ENOSPC;
	} else {
		error = mqfs_allocv(ap->a_dvp->v_mount, ap->a_vpp, pn);
		mqnode_release(pn);
	}
	return (error);
}

#if 0
struct vop_rmdir_args {
	struct vnode *a_dvp;
	struct vnode *a_vp;
	struct componentname *a_cnp;
};
#endif

/*
 * Remove a directory.
 */
static int
mqfs_rmdir(struct vop_rmdir_args *ap)
{
	struct mqfs_info *mqfs = VFSTOMQFS(ap->a_dvp->v_mount);
	struct mqfs_node *pn = VTON(ap->a_vp);
	struct mqfs_node *pt;

	if (pn->mn_type != mqfstype_dir)
		return (ENOTDIR);

	sx_xlock(&mqfs->mi_lock);
	if (pn->mn_deleted) {
		sx_xunlock(&mqfs->mi_lock);
		return (ENOENT);
	}

	pt = LIST_FIRST(&pn->mn_children);
	pt = LIST_NEXT(pt, mn_sibling);
	pt = LIST_NEXT(pt, mn_sibling);
	if (pt != NULL) {
		sx_xunlock(&mqfs->mi_lock);
		return (ENOTEMPTY);
	}
	pt = pn->mn_parent;
	pn->mn_parent = NULL;
	pn->mn_deleted = 1;
	LIST_REMOVE(pn, mn_sibling);
	mqnode_release(pn);
	mqnode_release(pt);
	sx_xunlock(&mqfs->mi_lock);
	cache_purge(ap->a_vp);
	return (0);
}

#endif /* notyet */

/*
 * See if this prison root is obsolete, and clean up associated queues if it is.
 */
static int
mqfs_prison_remove(void *obj, void *data __unused)
{
	const struct prison *pr = obj;
	const struct prison *tpr;
	struct mqfs_node *pn, *tpn;
	int found;

	found = 0;
	TAILQ_FOREACH(tpr, &allprison, pr_list) {
		if (tpr->pr_root == pr->pr_root && tpr != pr && tpr->pr_ref > 0)
			found = 1;
	}
	if (!found) {
		/*
		 * No jails are rooted in this directory anymore,
		 * so no queues should be either.
		 */
		sx_xlock(&mqfs_data.mi_lock);
		LIST_FOREACH_SAFE(pn, &mqfs_data.mi_root->mn_children,
		    mn_sibling, tpn) {
			if (pn->mn_pr_root == pr->pr_root)
				(void)do_unlink(pn, curthread->td_ucred);
		}
		sx_xunlock(&mqfs_data.mi_lock);
	}
	return (0);
}

/*
 * Allocate a message queue
 */
static struct mqueue *
mqueue_alloc(const struct mq_attr *attr)
{
	struct mqueue *mq;

	if (curmq >= maxmq)
		return (NULL);
	mq = uma_zalloc(mqueue_zone, M_WAITOK | M_ZERO);
	TAILQ_INIT(&mq->mq_msgq);
	if (attr != NULL) {
		mq->mq_maxmsg = attr->mq_maxmsg;
		mq->mq_msgsize = attr->mq_msgsize;
	} else {
		mq->mq_maxmsg = default_maxmsg;
		mq->mq_msgsize = default_msgsize;
	}
	mtx_init(&mq->mq_mutex, "mqueue lock", NULL, MTX_DEF);
	knlist_init_mtx(&mq->mq_rsel.si_note, &mq->mq_mutex);
	knlist_init_mtx(&mq->mq_wsel.si_note, &mq->mq_mutex);
	atomic_add_int(&curmq, 1);
	return (mq);
}

/*
 * Destroy a message queue
 */
static void
mqueue_free(struct mqueue *mq)
{
	struct mqueue_msg *msg;

	while ((msg = TAILQ_FIRST(&mq->mq_msgq)) != NULL) {
		TAILQ_REMOVE(&mq->mq_msgq, msg, msg_link);
		free(msg, M_MQUEUEDATA);
	}

	mtx_destroy(&mq->mq_mutex);
	seldrain(&mq->mq_rsel);
	seldrain(&mq->mq_wsel);
	knlist_destroy(&mq->mq_rsel.si_note);
	knlist_destroy(&mq->mq_wsel.si_note);
	uma_zfree(mqueue_zone, mq);
	atomic_add_int(&curmq, -1);
}

/*
 * Load a message from user space
 */
static struct mqueue_msg *
mqueue_loadmsg(const char *msg_ptr, size_t msg_size, int msg_prio)
{
	struct mqueue_msg *msg;
	size_t len;
	int error;

	len = sizeof(struct mqueue_msg) + msg_size;
	msg = malloc(len, M_MQUEUEDATA, M_WAITOK);
	error = copyin(msg_ptr, ((char *)msg) + sizeof(struct mqueue_msg),
	    msg_size);
	if (error) {
		free(msg, M_MQUEUEDATA);
		msg = NULL;
	} else {
		msg->msg_size = msg_size;
		msg->msg_prio = msg_prio;
	}
	return (msg);
}

/*
 * Save a message to user space
 */
static int
mqueue_savemsg(struct mqueue_msg *msg, char *msg_ptr, int *msg_prio)
{
	int error;

	error = copyout(((char *)msg) + sizeof(*msg), msg_ptr,
		msg->msg_size);
	if (error == 0 && msg_prio != NULL)
		error = copyout(&msg->msg_prio, msg_prio, sizeof(int));
	return (error);
}

/*
 * Free a message's memory
 */
static __inline void
mqueue_freemsg(struct mqueue_msg *msg)
{
	free(msg, M_MQUEUEDATA);
}

/*
 * Send a message. if waitok is false, thread will not be
 * blocked if there is no data in queue, otherwise, absolute
 * time will be checked.
 */
int
mqueue_send(struct mqueue *mq, const char *msg_ptr,
	size_t msg_len, unsigned msg_prio, int waitok,
	const struct timespec *abs_timeout)
{
	struct mqueue_msg *msg;
	struct timespec ts, ts2;
	struct timeval tv;
	int error;

	if (msg_prio >= MQ_PRIO_MAX)
		return (EINVAL);
	if (msg_len > mq->mq_msgsize)
		return (EMSGSIZE);
	msg = mqueue_loadmsg(msg_ptr, msg_len, msg_prio);
	if (msg == NULL)
		return (EFAULT);

	/* O_NONBLOCK case */
	if (!waitok) {
		error = _mqueue_send(mq, msg, -1);
		if (error)
			goto bad;
		return (0);
	}

	/* we allow a null timeout (wait forever) */
	if (abs_timeout == NULL) {
		error = _mqueue_send(mq, msg, 0);
		if (error)
			goto bad;
		return (0);
	}

	/* send it before checking time */
	error = _mqueue_send(mq, msg, -1);
	if (error == 0)
		return (0);

	if (error != EAGAIN)
		goto bad;

	if (abs_timeout->tv_nsec >= 1000000000 || abs_timeout->tv_nsec < 0) {
		error = EINVAL;
		goto bad;
	}
	for (;;) {
		getnanotime(&ts);
		timespecsub(abs_timeout, &ts, &ts2);
		if (ts2.tv_sec < 0 || (ts2.tv_sec == 0 && ts2.tv_nsec <= 0)) {
			error = ETIMEDOUT;
			break;
		}
		TIMESPEC_TO_TIMEVAL(&tv, &ts2);
		error = _mqueue_send(mq, msg, tvtohz(&tv));
		if (error != ETIMEDOUT)
			break;
	}
	if (error == 0)
		return (0);
bad:
	mqueue_freemsg(msg);
	return (error);
}

/*
 * Common routine to send a message
 */
static int
_mqueue_send(struct mqueue *mq, struct mqueue_msg *msg, int timo)
{	
	struct mqueue_msg *msg2;
	int error = 0;

	mtx_lock(&mq->mq_mutex);
	while (mq->mq_curmsgs >= mq->mq_maxmsg && error == 0) {
		if (timo < 0) {
			mtx_unlock(&mq->mq_mutex);
			return (EAGAIN);
		}
		mq->mq_senders++;
		error = msleep(&mq->mq_senders, &mq->mq_mutex,
			    PCATCH, "mqsend", timo);
		mq->mq_senders--;
		if (error == EAGAIN)
			error = ETIMEDOUT;
	}
	if (mq->mq_curmsgs >= mq->mq_maxmsg) {
		mtx_unlock(&mq->mq_mutex);
		return (error);
	}
	error = 0;
	if (TAILQ_EMPTY(&mq->mq_msgq)) {
		TAILQ_INSERT_HEAD(&mq->mq_msgq, msg, msg_link);
	} else {
		if (msg->msg_prio <= TAILQ_LAST(&mq->mq_msgq, msgq)->msg_prio) {
			TAILQ_INSERT_TAIL(&mq->mq_msgq, msg, msg_link);
		} else {
			TAILQ_FOREACH(msg2, &mq->mq_msgq, msg_link) {
				if (msg2->msg_prio < msg->msg_prio)
					break;
			}
			TAILQ_INSERT_BEFORE(msg2, msg, msg_link);
		}
	}
	mq->mq_curmsgs++;
	mq->mq_totalbytes += msg->msg_size;
	if (mq->mq_receivers)
		wakeup_one(&mq->mq_receivers);
	else if (mq->mq_notifier != NULL)
		mqueue_send_notification(mq);
	if (mq->mq_flags & MQ_RSEL) {
		mq->mq_flags &= ~MQ_RSEL;
		selwakeup(&mq->mq_rsel);
	}
	KNOTE_LOCKED(&mq->mq_rsel.si_note, 0);
	mtx_unlock(&mq->mq_mutex);
	return (0);
}

/*
 * Send realtime a signal to process which registered itself
 * successfully by mq_notify.
 */
static void
mqueue_send_notification(struct mqueue *mq)
{
	struct mqueue_notifier *nt;
	struct thread *td;
	struct proc *p;
	int error;

	mtx_assert(&mq->mq_mutex, MA_OWNED);
	nt = mq->mq_notifier;
	if (nt->nt_sigev.sigev_notify != SIGEV_NONE) {
		p = nt->nt_proc;
		error = sigev_findtd(p, &nt->nt_sigev, &td);
		if (error) {
			mq->mq_notifier = NULL;
			return;
		}
		if (!KSI_ONQ(&nt->nt_ksi)) {
			ksiginfo_set_sigev(&nt->nt_ksi, &nt->nt_sigev);
			tdsendsignal(p, td, nt->nt_ksi.ksi_signo, &nt->nt_ksi);
		}
		PROC_UNLOCK(p);
	}
	mq->mq_notifier = NULL;
}

/*
 * Get a message. if waitok is false, thread will not be
 * blocked if there is no data in queue, otherwise, absolute
 * time will be checked.
 */
int
mqueue_receive(struct mqueue *mq, char *msg_ptr,
	size_t msg_len, unsigned *msg_prio, int waitok,
	const struct timespec *abs_timeout)
{
	struct mqueue_msg *msg;
	struct timespec ts, ts2;
	struct timeval tv;
	int error;

	if (msg_len < mq->mq_msgsize)
		return (EMSGSIZE);

	/* O_NONBLOCK case */
	if (!waitok) {
		error = _mqueue_recv(mq, &msg, -1);
		if (error)
			return (error);
		goto received;
	}

	/* we allow a null timeout (wait forever). */
	if (abs_timeout == NULL) {
		error = _mqueue_recv(mq, &msg, 0);
		if (error)
			return (error);
		goto received;
	}

	/* try to get a message before checking time */
	error = _mqueue_recv(mq, &msg, -1);
	if (error == 0)
		goto received;

	if (error != EAGAIN)
		return (error);

	if (abs_timeout->tv_nsec >= 1000000000 || abs_timeout->tv_nsec < 0) {
		error = EINVAL;
		return (error);
	}

	for (;;) {
		getnanotime(&ts);
		timespecsub(abs_timeout, &ts, &ts2);
		if (ts2.tv_sec < 0 || (ts2.tv_sec == 0 && ts2.tv_nsec <= 0)) {
			error = ETIMEDOUT;
			return (error);
		}
		TIMESPEC_TO_TIMEVAL(&tv, &ts2);
		error = _mqueue_recv(mq, &msg, tvtohz(&tv));
		if (error == 0)
			break;
		if (error != ETIMEDOUT)
			return (error);
	}

received:
	error = mqueue_savemsg(msg, msg_ptr, msg_prio);
	if (error == 0) {
		curthread->td_retval[0] = msg->msg_size;
		curthread->td_retval[1] = 0;
	}
	mqueue_freemsg(msg);
	return (error);
}

/*
 * Common routine to receive a message
 */
static int
_mqueue_recv(struct mqueue *mq, struct mqueue_msg **msg, int timo)
{	
	int error = 0;
	
	mtx_lock(&mq->mq_mutex);
	while ((*msg = TAILQ_FIRST(&mq->mq_msgq)) == NULL && error == 0) {
		if (timo < 0) {
			mtx_unlock(&mq->mq_mutex);
			return (EAGAIN);
		}
		mq->mq_receivers++;
		error = msleep(&mq->mq_receivers, &mq->mq_mutex,
			    PCATCH, "mqrecv", timo);
		mq->mq_receivers--;
		if (error == EAGAIN)
			error = ETIMEDOUT;
	}
	if (*msg != NULL) {
		error = 0;
		TAILQ_REMOVE(&mq->mq_msgq, *msg, msg_link);
		mq->mq_curmsgs--;
		mq->mq_totalbytes -= (*msg)->msg_size;
		if (mq->mq_senders)
			wakeup_one(&mq->mq_senders);
		if (mq->mq_flags & MQ_WSEL) {
			mq->mq_flags &= ~MQ_WSEL;
			selwakeup(&mq->mq_wsel);
		}
		KNOTE_LOCKED(&mq->mq_wsel.si_note, 0);
	}
	if (mq->mq_notifier != NULL && mq->mq_receivers == 0 &&
	    !TAILQ_EMPTY(&mq->mq_msgq)) {
		mqueue_send_notification(mq);
	}
	mtx_unlock(&mq->mq_mutex);
	return (error);
}

static __inline struct mqueue_notifier *
notifier_alloc(void)
{
	return (uma_zalloc(mqnoti_zone, M_WAITOK | M_ZERO));
}

static __inline void
notifier_free(struct mqueue_notifier *p)
{
	uma_zfree(mqnoti_zone, p);
}

static struct mqueue_notifier *
notifier_search(struct proc *p, int fd)
{
	struct mqueue_notifier *nt;

	LIST_FOREACH(nt, &p->p_mqnotifier, nt_link) {
		if (nt->nt_ksi.ksi_mqd == fd)
			break;
	}
	return (nt);
}

static __inline void
notifier_insert(struct proc *p, struct mqueue_notifier *nt)
{
	LIST_INSERT_HEAD(&p->p_mqnotifier, nt, nt_link);
}

static __inline void
notifier_delete(struct proc *p, struct mqueue_notifier *nt)
{
	LIST_REMOVE(nt, nt_link);
	notifier_free(nt);
}

static void
notifier_remove(struct proc *p, struct mqueue *mq, int fd)
{
	struct mqueue_notifier *nt;

	mtx_assert(&mq->mq_mutex, MA_OWNED);
	PROC_LOCK(p);
	nt = notifier_search(p, fd);
	if (nt != NULL) {
		if (mq->mq_notifier == nt)
			mq->mq_notifier = NULL;
		sigqueue_take(&nt->nt_ksi);
		notifier_delete(p, nt);
	}
	PROC_UNLOCK(p);
}

static int
kern_kmq_open(struct thread *td, const char *upath, int flags, mode_t mode,
    const struct mq_attr *attr)
{
	char path[MQFS_NAMELEN + 1];
	struct mqfs_node *pn;
	struct filedesc *fdp;
	struct file *fp;
	struct mqueue *mq;
	int fd, error, len, cmode;

	AUDIT_ARG_FFLAGS(flags);
	AUDIT_ARG_MODE(mode);

	fdp = td->td_proc->p_fd;
	cmode = (((mode & ~fdp->fd_cmask) & ALLPERMS) & ~S_ISTXT);
	mq = NULL;
	if ((flags & O_CREAT) != 0 && attr != NULL) {
		if (attr->mq_maxmsg <= 0 || attr->mq_maxmsg > maxmsg)
			return (EINVAL);
		if (attr->mq_msgsize <= 0 || attr->mq_msgsize > maxmsgsize)
			return (EINVAL);
	}

	error = copyinstr(upath, path, MQFS_NAMELEN + 1, NULL);
        if (error)
		return (error);

	/*
	 * The first character of name must be a slash  (/) character
	 * and the remaining characters of name cannot include any slash
	 * characters. 
	 */
	len = strlen(path);
	if (len < 2 || path[0] != '/' || strchr(path + 1, '/') != NULL)
		return (EINVAL);
	AUDIT_ARG_UPATH1_CANON(path);

	error = falloc(td, &fp, &fd, O_CLOEXEC);
	if (error)
		return (error);

	sx_xlock(&mqfs_data.mi_lock);
	pn = mqfs_search(mqfs_data.mi_root, path + 1, len - 1, td->td_ucred);
	if (pn == NULL) {
		if (!(flags & O_CREAT)) {
			error = ENOENT;
		} else {
			mq = mqueue_alloc(attr);
			if (mq == NULL) {
				error = ENFILE;
			} else {
				pn = mqfs_create_file(mqfs_data.mi_root,
				         path + 1, len - 1, td->td_ucred,
					 cmode);
				if (pn == NULL) {
					error = ENOSPC;
					mqueue_free(mq);
				}
			}
		}

		if (error == 0) {
			pn->mn_data = mq;
		}
	} else {
		if ((flags & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL)) {
			error = EEXIST;
		} else {
			accmode_t accmode = 0;

			if (flags & FREAD)
				accmode |= VREAD;
			if (flags & FWRITE)
				accmode |= VWRITE;
			error = vaccess(VREG, pn->mn_mode, pn->mn_uid,
				    pn->mn_gid, accmode, td->td_ucred, NULL);
		}
	}

	if (error) {
		sx_xunlock(&mqfs_data.mi_lock);
		fdclose(td, fp, fd);
		fdrop(fp, td);
		return (error);
	}

	mqnode_addref(pn);
	sx_xunlock(&mqfs_data.mi_lock);

	finit(fp, flags & (FREAD | FWRITE | O_NONBLOCK), DTYPE_MQUEUE, pn,
	    &mqueueops);

	td->td_retval[0] = fd;
	fdrop(fp, td);
	return (0);
}

/*
 * Syscall to open a message queue.
 */
int
sys_kmq_open(struct thread *td, struct kmq_open_args *uap)
{
	struct mq_attr attr;
	int flags, error;

	if ((uap->flags & O_ACCMODE) == O_ACCMODE || uap->flags & O_EXEC)
		return (EINVAL);
	flags = FFLAGS(uap->flags);
	if ((flags & O_CREAT) != 0 && uap->attr != NULL) {
		error = copyin(uap->attr, &attr, sizeof(attr));
		if (error)
			return (error);
	}
	return (kern_kmq_open(td, uap->path, flags, uap->mode,
	    uap->attr != NULL ? &attr : NULL));
}

/*
 * Syscall to unlink a message queue.
 */
int
sys_kmq_unlink(struct thread *td, struct kmq_unlink_args *uap)
{
	char path[MQFS_NAMELEN+1];
	struct mqfs_node *pn;
	int error, len;

	error = copyinstr(uap->path, path, MQFS_NAMELEN + 1, NULL);
        if (error)
		return (error);

	len = strlen(path);
	if (len < 2 || path[0] != '/' || strchr(path + 1, '/') != NULL)
		return (EINVAL);
	AUDIT_ARG_UPATH1_CANON(path);

	sx_xlock(&mqfs_data.mi_lock);
	pn = mqfs_search(mqfs_data.mi_root, path + 1, len - 1, td->td_ucred);
	if (pn != NULL)
		error = do_unlink(pn, td->td_ucred);
	else
		error = ENOENT;
	sx_xunlock(&mqfs_data.mi_lock);
	return (error);
}

typedef int (*_fgetf)(struct thread *, int, cap_rights_t *, struct file **);

/*
 * Get message queue by giving file slot
 */
static int
_getmq(struct thread *td, int fd, cap_rights_t *rightsp, _fgetf func,
       struct file **fpp, struct mqfs_node **ppn, struct mqueue **pmq)
{
	struct mqfs_node *pn;
	int error;

	error = func(td, fd, rightsp, fpp);
	if (error)
		return (error);
	if (&mqueueops != (*fpp)->f_ops) {
		fdrop(*fpp, td);
		return (EBADF);
	}
	pn = (*fpp)->f_data;
	if (ppn)
		*ppn = pn;
	if (pmq)
		*pmq = pn->mn_data;
	return (0);
}

static __inline int
getmq(struct thread *td, int fd, struct file **fpp, struct mqfs_node **ppn,
	struct mqueue **pmq)
{

	return _getmq(td, fd, &cap_event_rights, fget,
	    fpp, ppn, pmq);
}

static __inline int
getmq_read(struct thread *td, int fd, struct file **fpp,
	 struct mqfs_node **ppn, struct mqueue **pmq)
{

	return _getmq(td, fd, &cap_read_rights, fget_read,
	    fpp, ppn, pmq);
}

static __inline int
getmq_write(struct thread *td, int fd, struct file **fpp,
	struct mqfs_node **ppn, struct mqueue **pmq)
{

	return _getmq(td, fd, &cap_write_rights, fget_write,
	    fpp, ppn, pmq);
}

static int
kern_kmq_setattr(struct thread *td, int mqd, const struct mq_attr *attr,
    struct mq_attr *oattr)
{
	struct mqueue *mq;
	struct file *fp;
	u_int oflag, flag;
	int error;

	AUDIT_ARG_FD(mqd);
	if (attr != NULL && (attr->mq_flags & ~O_NONBLOCK) != 0)
		return (EINVAL);
	error = getmq(td, mqd, &fp, NULL, &mq);
	if (error)
		return (error);
	oattr->mq_maxmsg  = mq->mq_maxmsg;
	oattr->mq_msgsize = mq->mq_msgsize;
	oattr->mq_curmsgs = mq->mq_curmsgs;
	if (attr != NULL) {
		do {
			oflag = flag = fp->f_flag;
			flag &= ~O_NONBLOCK;
			flag |= (attr->mq_flags & O_NONBLOCK);
		} while (atomic_cmpset_int(&fp->f_flag, oflag, flag) == 0);
	} else
		oflag = fp->f_flag;
	oattr->mq_flags = (O_NONBLOCK & oflag);
	fdrop(fp, td);
	return (error);
}

int
sys_kmq_setattr(struct thread *td, struct kmq_setattr_args *uap)
{
	struct mq_attr attr, oattr;
	int error;

	if (uap->attr != NULL) {
		error = copyin(uap->attr, &attr, sizeof(attr));
		if (error != 0)
			return (error);
	}
	error = kern_kmq_setattr(td, uap->mqd, uap->attr != NULL ? &attr : NULL,
	    &oattr);
	if (error == 0 && uap->oattr != NULL) {
		bzero(oattr.__reserved, sizeof(oattr.__reserved));
		error = copyout(&oattr, uap->oattr, sizeof(oattr));
	}
	return (error);
}

int
sys_kmq_timedreceive(struct thread *td, struct kmq_timedreceive_args *uap)
{
	struct mqueue *mq;
	struct file *fp;
	struct timespec *abs_timeout, ets;
	int error;
	int waitok;

	AUDIT_ARG_FD(uap->mqd);
	error = getmq_read(td, uap->mqd, &fp, NULL, &mq);
	if (error)
		return (error);
	if (uap->abs_timeout != NULL) {
		error = copyin(uap->abs_timeout, &ets, sizeof(ets));
		if (error != 0)
			return (error);
		abs_timeout = &ets;
	} else
		abs_timeout = NULL;
	waitok = !(fp->f_flag & O_NONBLOCK);
	error = mqueue_receive(mq, uap->msg_ptr, uap->msg_len,
		uap->msg_prio, waitok, abs_timeout);
	fdrop(fp, td);
	return (error);
}

int
sys_kmq_timedsend(struct thread *td, struct kmq_timedsend_args *uap)
{
	struct mqueue *mq;
	struct file *fp;
	struct timespec *abs_timeout, ets;
	int error, waitok;

	AUDIT_ARG_FD(uap->mqd);
	error = getmq_write(td, uap->mqd, &fp, NULL, &mq);
	if (error)
		return (error);
	if (uap->abs_timeout != NULL) {
		error = copyin(uap->abs_timeout, &ets, sizeof(ets));
		if (error != 0)
			return (error);
		abs_timeout = &ets;
	} else
		abs_timeout = NULL;
	waitok = !(fp->f_flag & O_NONBLOCK);
	error = mqueue_send(mq, uap->msg_ptr, uap->msg_len,
		uap->msg_prio, waitok, abs_timeout);
	fdrop(fp, td);
	return (error);
}

static int
kern_kmq_notify(struct thread *td, int mqd, struct sigevent *sigev)
{
	struct filedesc *fdp;
	struct proc *p;
	struct mqueue *mq;
	struct file *fp, *fp2;
	struct mqueue_notifier *nt, *newnt = NULL;
	int error;

	AUDIT_ARG_FD(mqd);
	if (sigev != NULL) {
		if (sigev->sigev_notify != SIGEV_SIGNAL &&
		    sigev->sigev_notify != SIGEV_THREAD_ID &&
		    sigev->sigev_notify != SIGEV_NONE)
			return (EINVAL);
		if ((sigev->sigev_notify == SIGEV_SIGNAL ||
		    sigev->sigev_notify == SIGEV_THREAD_ID) &&
		    !_SIG_VALID(sigev->sigev_signo))
			return (EINVAL);
	}
	p = td->td_proc;
	fdp = td->td_proc->p_fd;
	error = getmq(td, mqd, &fp, NULL, &mq);
	if (error)
		return (error);
again:
	FILEDESC_SLOCK(fdp);
	fp2 = fget_locked(fdp, mqd);
	if (fp2 == NULL) {
		FILEDESC_SUNLOCK(fdp);
		error = EBADF;
		goto out;
	}
#ifdef CAPABILITIES
	error = cap_check(cap_rights(fdp, mqd), &cap_event_rights);
	if (error) {
		FILEDESC_SUNLOCK(fdp);
		goto out;
	}
#endif
	if (fp2 != fp) {
		FILEDESC_SUNLOCK(fdp);
		error = EBADF;
		goto out;
	}
	mtx_lock(&mq->mq_mutex);
	FILEDESC_SUNLOCK(fdp);
	if (sigev != NULL) {
		if (mq->mq_notifier != NULL) {
			error = EBUSY;
		} else {
			PROC_LOCK(p);
			nt = notifier_search(p, mqd);
			if (nt == NULL) {
				if (newnt == NULL) {
					PROC_UNLOCK(p);
					mtx_unlock(&mq->mq_mutex);
					newnt = notifier_alloc();
					goto again;
				}
			}

			if (nt != NULL) {
				sigqueue_take(&nt->nt_ksi);
				if (newnt != NULL) {
					notifier_free(newnt);
					newnt = NULL;
				}
			} else {
				nt = newnt;
				newnt = NULL;
				ksiginfo_init(&nt->nt_ksi);
				nt->nt_ksi.ksi_flags |= KSI_INS | KSI_EXT;
				nt->nt_ksi.ksi_code = SI_MESGQ;
				nt->nt_proc = p;
				nt->nt_ksi.ksi_mqd = mqd;
				notifier_insert(p, nt);
			}
			nt->nt_sigev = *sigev;
			mq->mq_notifier = nt;
			PROC_UNLOCK(p);
			/*
			 * if there is no receivers and message queue
			 * is not empty, we should send notification
			 * as soon as possible.
			 */
			if (mq->mq_receivers == 0 &&
			    !TAILQ_EMPTY(&mq->mq_msgq))
				mqueue_send_notification(mq);
		}
	} else {
		notifier_remove(p, mq, mqd);
	}
	mtx_unlock(&mq->mq_mutex);

out:
	fdrop(fp, td);
	if (newnt != NULL)
		notifier_free(newnt);
	return (error);
}

int
sys_kmq_notify(struct thread *td, struct kmq_notify_args *uap)
{
	struct sigevent ev, *evp;
	int error;

	if (uap->sigev == NULL) {
		evp = NULL;
	} else {
		error = copyin(uap->sigev, &ev, sizeof(ev));
		if (error != 0)
			return (error);
		evp = &ev;
	}
	return (kern_kmq_notify(td, uap->mqd, evp));
}

static void
mqueue_fdclose(struct thread *td, int fd, struct file *fp)
{
	struct mqueue *mq;
#ifdef INVARIANTS
	struct filedesc *fdp;
 
	fdp = td->td_proc->p_fd;
	FILEDESC_LOCK_ASSERT(fdp);
#endif

	if (fp->f_ops == &mqueueops) {
		mq = FPTOMQ(fp);
		mtx_lock(&mq->mq_mutex);
		notifier_remove(td->td_proc, mq, fd);

		/* have to wakeup thread in same process */
		if (mq->mq_flags & MQ_RSEL) {
			mq->mq_flags &= ~MQ_RSEL;
			selwakeup(&mq->mq_rsel);
		}
		if (mq->mq_flags & MQ_WSEL) {
			mq->mq_flags &= ~MQ_WSEL;
			selwakeup(&mq->mq_wsel);
		}
		mtx_unlock(&mq->mq_mutex);
	}
}

static void
mq_proc_exit(void *arg __unused, struct proc *p)
{
	struct filedesc *fdp;
	struct file *fp;
	struct mqueue *mq;
	int i;

	fdp = p->p_fd;
	FILEDESC_SLOCK(fdp);
	for (i = 0; i < fdp->fd_nfiles; ++i) {
		fp = fget_locked(fdp, i);
		if (fp != NULL && fp->f_ops == &mqueueops) {
			mq = FPTOMQ(fp);
			mtx_lock(&mq->mq_mutex);
			notifier_remove(p, FPTOMQ(fp), i);
			mtx_unlock(&mq->mq_mutex);
		}
	}
	FILEDESC_SUNLOCK(fdp);
	KASSERT(LIST_EMPTY(&p->p_mqnotifier), ("mq notifiers left"));
}

static int
mqf_poll(struct file *fp, int events, struct ucred *active_cred,
	struct thread *td)
{
	struct mqueue *mq = FPTOMQ(fp);
	int revents = 0;

	mtx_lock(&mq->mq_mutex);
	if (events & (POLLIN | POLLRDNORM)) {
		if (mq->mq_curmsgs) {
			revents |= events & (POLLIN | POLLRDNORM);
		} else {
			mq->mq_flags |= MQ_RSEL;
			selrecord(td, &mq->mq_rsel);
 		}
	}
	if (events & POLLOUT) {
		if (mq->mq_curmsgs < mq->mq_maxmsg)
			revents |= POLLOUT;
		else {
			mq->mq_flags |= MQ_WSEL;
			selrecord(td, &mq->mq_wsel);
		}
	}
	mtx_unlock(&mq->mq_mutex);
	return (revents);
}

static int
mqf_close(struct file *fp, struct thread *td)
{
	struct mqfs_node *pn;

	fp->f_ops = &badfileops;
	pn = fp->f_data;
	fp->f_data = NULL;
	sx_xlock(&mqfs_data.mi_lock);
	mqnode_release(pn);
	sx_xunlock(&mqfs_data.mi_lock);
	return (0);
}

static int
mqf_stat(struct file *fp, struct stat *st, struct ucred *active_cred,
	struct thread *td)
{
	struct mqfs_node *pn = fp->f_data;

	bzero(st, sizeof *st);
	sx_xlock(&mqfs_data.mi_lock);
	st->st_atim = pn->mn_atime;
	st->st_mtim = pn->mn_mtime;
	st->st_ctim = pn->mn_ctime;
	st->st_birthtim = pn->mn_birth;
	st->st_uid = pn->mn_uid;
	st->st_gid = pn->mn_gid;
	st->st_mode = S_IFIFO | pn->mn_mode;
	sx_xunlock(&mqfs_data.mi_lock);
	return (0);
}

static int
mqf_chmod(struct file *fp, mode_t mode, struct ucred *active_cred,
    struct thread *td)
{
	struct mqfs_node *pn;
	int error;

	error = 0;
	pn = fp->f_data;
	sx_xlock(&mqfs_data.mi_lock);
	error = vaccess(VREG, pn->mn_mode, pn->mn_uid, pn->mn_gid, VADMIN,
	    active_cred, NULL);
	if (error != 0)
		goto out;
	pn->mn_mode = mode & ACCESSPERMS;
out:
	sx_xunlock(&mqfs_data.mi_lock);
	return (error);
}

static int
mqf_chown(struct file *fp, uid_t uid, gid_t gid, struct ucred *active_cred,
    struct thread *td)
{
	struct mqfs_node *pn;
	int error;

	error = 0;
	pn = fp->f_data;
	sx_xlock(&mqfs_data.mi_lock);
	if (uid == (uid_t)-1)
		uid = pn->mn_uid;
	if (gid == (gid_t)-1)
		gid = pn->mn_gid;
	if (((uid != pn->mn_uid && uid != active_cred->cr_uid) ||
	    (gid != pn->mn_gid && !groupmember(gid, active_cred))) &&
	    (error = priv_check_cred(active_cred, PRIV_VFS_CHOWN)))
		goto out;
	pn->mn_uid = uid;
	pn->mn_gid = gid;
out:
	sx_xunlock(&mqfs_data.mi_lock);
	return (error);
}

static int
mqf_kqfilter(struct file *fp, struct knote *kn)
{
	struct mqueue *mq = FPTOMQ(fp);
	int error = 0;

	if (kn->kn_filter == EVFILT_READ) {
		kn->kn_fop = &mq_rfiltops;
		knlist_add(&mq->mq_rsel.si_note, kn, 0);
	} else if (kn->kn_filter == EVFILT_WRITE) {
		kn->kn_fop = &mq_wfiltops;
		knlist_add(&mq->mq_wsel.si_note, kn, 0);
	} else
		error = EINVAL;
	return (error);
}

static void
filt_mqdetach(struct knote *kn)
{
	struct mqueue *mq = FPTOMQ(kn->kn_fp);

	if (kn->kn_filter == EVFILT_READ)
		knlist_remove(&mq->mq_rsel.si_note, kn, 0);
	else if (kn->kn_filter == EVFILT_WRITE)
		knlist_remove(&mq->mq_wsel.si_note, kn, 0);
	else
		panic("filt_mqdetach");
}

static int
filt_mqread(struct knote *kn, long hint)
{
	struct mqueue *mq = FPTOMQ(kn->kn_fp);

	mtx_assert(&mq->mq_mutex, MA_OWNED);
	return (mq->mq_curmsgs != 0);
}

static int
filt_mqwrite(struct knote *kn, long hint)
{
	struct mqueue *mq = FPTOMQ(kn->kn_fp);

	mtx_assert(&mq->mq_mutex, MA_OWNED);
	return (mq->mq_curmsgs < mq->mq_maxmsg);
}

static int
mqf_fill_kinfo(struct file *fp, struct kinfo_file *kif, struct filedesc *fdp)
{

	kif->kf_type = KF_TYPE_MQUEUE;
	return (0);
}

static struct fileops mqueueops = {
	.fo_read		= invfo_rdwr,
	.fo_write		= invfo_rdwr,
	.fo_truncate		= invfo_truncate,
	.fo_ioctl		= invfo_ioctl,
	.fo_poll		= mqf_poll,
	.fo_kqfilter		= mqf_kqfilter,
	.fo_stat		= mqf_stat,
	.fo_close		= mqf_close,
	.fo_chmod		= mqf_chmod,
	.fo_chown		= mqf_chown,
	.fo_sendfile		= invfo_sendfile,
	.fo_fill_kinfo		= mqf_fill_kinfo,
};

static struct vop_vector mqfs_vnodeops = {
	.vop_default 		= &default_vnodeops,
	.vop_access		= mqfs_access,
	.vop_cachedlookup	= mqfs_lookup,
	.vop_lookup		= vfs_cache_lookup,
	.vop_reclaim		= mqfs_reclaim,
	.vop_create		= mqfs_create,
	.vop_remove		= mqfs_remove,
	.vop_inactive		= mqfs_inactive,
	.vop_open		= mqfs_open,
	.vop_close		= mqfs_close,
	.vop_getattr		= mqfs_getattr,
	.vop_setattr		= mqfs_setattr,
	.vop_read		= mqfs_read,
	.vop_write		= VOP_EOPNOTSUPP,
	.vop_readdir		= mqfs_readdir,
	.vop_mkdir		= VOP_EOPNOTSUPP,
	.vop_rmdir		= VOP_EOPNOTSUPP
};

static struct vfsops mqfs_vfsops = {
	.vfs_init 		= mqfs_init,
	.vfs_uninit		= mqfs_uninit,
	.vfs_mount		= mqfs_mount,
	.vfs_unmount		= mqfs_unmount,
	.vfs_root		= mqfs_root,
	.vfs_statfs		= mqfs_statfs,
};

static struct vfsconf mqueuefs_vfsconf = {
	.vfc_version = VFS_VERSION,
	.vfc_name = "mqueuefs",
	.vfc_vfsops = &mqfs_vfsops,
	.vfc_typenum = -1,
	.vfc_flags = VFCF_SYNTHETIC
};

static struct syscall_helper_data mq_syscalls[] = {
	SYSCALL_INIT_HELPER(kmq_open),
	SYSCALL_INIT_HELPER_F(kmq_setattr, SYF_CAPENABLED),
	SYSCALL_INIT_HELPER_F(kmq_timedsend, SYF_CAPENABLED),
	SYSCALL_INIT_HELPER_F(kmq_timedreceive, SYF_CAPENABLED),
	SYSCALL_INIT_HELPER_F(kmq_notify, SYF_CAPENABLED),
	SYSCALL_INIT_HELPER(kmq_unlink),
	SYSCALL_INIT_LAST
};

#ifdef COMPAT_FREEBSD32
#include <compat/freebsd32/freebsd32.h>
#include <compat/freebsd32/freebsd32_proto.h>
#include <compat/freebsd32/freebsd32_signal.h>
#include <compat/freebsd32/freebsd32_syscall.h>
#include <compat/freebsd32/freebsd32_util.h>

static void
mq_attr_from32(const struct mq_attr32 *from, struct mq_attr *to)
{

	to->mq_flags = from->mq_flags;
	to->mq_maxmsg = from->mq_maxmsg;
	to->mq_msgsize = from->mq_msgsize;
	to->mq_curmsgs = from->mq_curmsgs;
}

static void
mq_attr_to32(const struct mq_attr *from, struct mq_attr32 *to)
{

	to->mq_flags = from->mq_flags;
	to->mq_maxmsg = from->mq_maxmsg;
	to->mq_msgsize = from->mq_msgsize;
	to->mq_curmsgs = from->mq_curmsgs;
}

int
freebsd32_kmq_open(struct thread *td, struct freebsd32_kmq_open_args *uap)
{
	struct mq_attr attr;
	struct mq_attr32 attr32;
	int flags, error;

	if ((uap->flags & O_ACCMODE) == O_ACCMODE || uap->flags & O_EXEC)
		return (EINVAL);
	flags = FFLAGS(uap->flags);
	if ((flags & O_CREAT) != 0 && uap->attr != NULL) {
		error = copyin(uap->attr, &attr32, sizeof(attr32));
		if (error)
			return (error);
		mq_attr_from32(&attr32, &attr);
	}
	return (kern_kmq_open(td, uap->path, flags, uap->mode,
	    uap->attr != NULL ? &attr : NULL));
}

int
freebsd32_kmq_setattr(struct thread *td, struct freebsd32_kmq_setattr_args *uap)
{
	struct mq_attr attr, oattr;
	struct mq_attr32 attr32, oattr32;
	int error;

	if (uap->attr != NULL) {
		error = copyin(uap->attr, &attr32, sizeof(attr32));
		if (error != 0)
			return (error);
		mq_attr_from32(&attr32, &attr);
	}
	error = kern_kmq_setattr(td, uap->mqd, uap->attr != NULL ? &attr : NULL,
	    &oattr);
	if (error == 0 && uap->oattr != NULL) {
		mq_attr_to32(&oattr, &oattr32);
		bzero(oattr32.__reserved, sizeof(oattr32.__reserved));
		error = copyout(&oattr32, uap->oattr, sizeof(oattr32));
	}
	return (error);
}

int
freebsd32_kmq_timedsend(struct thread *td,
    struct freebsd32_kmq_timedsend_args *uap)
{
	struct mqueue *mq;
	struct file *fp;
	struct timespec32 ets32;
	struct timespec *abs_timeout, ets;
	int error;
	int waitok;

	AUDIT_ARG_FD(uap->mqd);
	error = getmq_write(td, uap->mqd, &fp, NULL, &mq);
	if (error)
		return (error);
	if (uap->abs_timeout != NULL) {
		error = copyin(uap->abs_timeout, &ets32, sizeof(ets32));
		if (error != 0)
			return (error);
		CP(ets32, ets, tv_sec);
		CP(ets32, ets, tv_nsec);
		abs_timeout = &ets;
	} else
		abs_timeout = NULL;
	waitok = !(fp->f_flag & O_NONBLOCK);
	error = mqueue_send(mq, uap->msg_ptr, uap->msg_len,
		uap->msg_prio, waitok, abs_timeout);
	fdrop(fp, td);
	return (error);
}

int
freebsd32_kmq_timedreceive(struct thread *td,
    struct freebsd32_kmq_timedreceive_args *uap)
{
	struct mqueue *mq;
	struct file *fp;
	struct timespec32 ets32;
	struct timespec *abs_timeout, ets;
	int error, waitok;

	AUDIT_ARG_FD(uap->mqd);
	error = getmq_read(td, uap->mqd, &fp, NULL, &mq);
	if (error)
		return (error);
	if (uap->abs_timeout != NULL) {
		error = copyin(uap->abs_timeout, &ets32, sizeof(ets32));
		if (error != 0)
			return (error);
		CP(ets32, ets, tv_sec);
		CP(ets32, ets, tv_nsec);
		abs_timeout = &ets;
	} else
		abs_timeout = NULL;
	waitok = !(fp->f_flag & O_NONBLOCK);
	error = mqueue_receive(mq, uap->msg_ptr, uap->msg_len,
		uap->msg_prio, waitok, abs_timeout);
	fdrop(fp, td);
	return (error);
}

int
freebsd32_kmq_notify(struct thread *td, struct freebsd32_kmq_notify_args *uap)
{
	struct sigevent ev, *evp;
	struct sigevent32 ev32;
	int error;

	if (uap->sigev == NULL) {
		evp = NULL;
	} else {
		error = copyin(uap->sigev, &ev32, sizeof(ev32));
		if (error != 0)
			return (error);
		error = convert_sigevent32(&ev32, &ev);
		if (error != 0)
			return (error);
		evp = &ev;
	}
	return (kern_kmq_notify(td, uap->mqd, evp));
}

static struct syscall_helper_data mq32_syscalls[] = {
	SYSCALL32_INIT_HELPER(freebsd32_kmq_open),
	SYSCALL32_INIT_HELPER_F(freebsd32_kmq_setattr, SYF_CAPENABLED),
	SYSCALL32_INIT_HELPER_F(freebsd32_kmq_timedsend, SYF_CAPENABLED),
	SYSCALL32_INIT_HELPER_F(freebsd32_kmq_timedreceive, SYF_CAPENABLED),
	SYSCALL32_INIT_HELPER_F(freebsd32_kmq_notify, SYF_CAPENABLED),
	SYSCALL32_INIT_HELPER_COMPAT(kmq_unlink),
	SYSCALL_INIT_LAST
};
#endif

static int
mqinit(void)
{
	int error;

	error = syscall_helper_register(mq_syscalls, SY_THR_STATIC_KLD);
	if (error != 0)
		return (error);
#ifdef COMPAT_FREEBSD32
	error = syscall32_helper_register(mq32_syscalls, SY_THR_STATIC_KLD);
	if (error != 0)
		return (error);
#endif
	return (0);
}

static int
mqunload(void)
{

#ifdef COMPAT_FREEBSD32
	syscall32_helper_unregister(mq32_syscalls);
#endif
	syscall_helper_unregister(mq_syscalls);
	return (0);
}

static int
mq_modload(struct module *module, int cmd, void *arg)
{
	int error = 0;

	error = vfs_modevent(module, cmd, arg);
	if (error != 0)
		return (error);

	switch (cmd) {
	case MOD_LOAD:
		error = mqinit();
		if (error != 0)
			mqunload();
		break;
	case MOD_UNLOAD:
		error = mqunload();
		break;
	default:
		break;
	}
	return (error);
}

static moduledata_t mqueuefs_mod = {
	"mqueuefs",
	mq_modload,
	&mqueuefs_vfsconf
};
DECLARE_MODULE(mqueuefs, mqueuefs_mod, SI_SUB_VFS, SI_ORDER_MIDDLE);
MODULE_VERSION(mqueuefs, 1);
