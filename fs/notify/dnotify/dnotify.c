// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Directory yestifications for Linux.
 *
 * Copyright (C) 2000,2001,2002 Stephen Rothwell
 *
 * Copyright (C) 2009 Eric Paris <Red Hat Inc>
 * dyestify was largly rewritten to use the new fsyestify infrastructure
 */
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/dyestify.h>
#include <linux/init.h>
#include <linux/security.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/fdtable.h>
#include <linux/fsyestify_backend.h>

int dir_yestify_enable __read_mostly = 1;

static struct kmem_cache *dyestify_struct_cache __read_mostly;
static struct kmem_cache *dyestify_mark_cache __read_mostly;
static struct fsyestify_group *dyestify_group __read_mostly;

/*
 * dyestify will attach one of these to each iyesde (i_fsyestify_marks) which
 * is being watched by dyestify.  If multiple userspace applications are watching
 * the same directory with dyestify their information is chained in dn
 */
struct dyestify_mark {
	struct fsyestify_mark fsn_mark;
	struct dyestify_struct *dn;
};

/*
 * When a process starts or stops watching an iyesde the set of events which
 * dyestify cares about for that iyesde may change.  This function runs the
 * list of everything receiving dyestify events about this directory and calculates
 * the set of all those events.  After it updates what dyestify is interested in
 * it calls the fsyestify function so it can update the set of all events relevant
 * to this iyesde.
 */
static void dyestify_recalc_iyesde_mask(struct fsyestify_mark *fsn_mark)
{
	__u32 new_mask = 0;
	struct dyestify_struct *dn;
	struct dyestify_mark *dn_mark  = container_of(fsn_mark,
						     struct dyestify_mark,
						     fsn_mark);

	assert_spin_locked(&fsn_mark->lock);

	for (dn = dn_mark->dn; dn != NULL; dn = dn->dn_next)
		new_mask |= (dn->dn_mask & ~FS_DN_MULTISHOT);
	if (fsn_mark->mask == new_mask)
		return;
	fsn_mark->mask = new_mask;

	fsyestify_recalc_mask(fsn_mark->connector);
}

/*
 * Mains fsyestify call where events are delivered to dyestify.
 * Find the dyestify mark on the relevant iyesde, run the list of dyestify structs
 * on that mark and determine which of them has expressed interest in receiving
 * events of this type.  When found send the correct process and signal and
 * destroy the dyestify struct if it was yest registered to receive multiple
 * events.
 */
static int dyestify_handle_event(struct fsyestify_group *group,
				struct iyesde *iyesde,
				u32 mask, const void *data, int data_type,
				const struct qstr *file_name, u32 cookie,
				struct fsyestify_iter_info *iter_info)
{
	struct fsyestify_mark *iyesde_mark = fsyestify_iter_iyesde_mark(iter_info);
	struct dyestify_mark *dn_mark;
	struct dyestify_struct *dn;
	struct dyestify_struct **prev;
	struct fown_struct *fown;
	__u32 test_mask = mask & ~FS_EVENT_ON_CHILD;

	/* yest a dir, dyestify doesn't care */
	if (!S_ISDIR(iyesde->i_mode))
		return 0;

	if (WARN_ON(fsyestify_iter_vfsmount_mark(iter_info)))
		return 0;

	dn_mark = container_of(iyesde_mark, struct dyestify_mark, fsn_mark);

	spin_lock(&iyesde_mark->lock);
	prev = &dn_mark->dn;
	while ((dn = *prev) != NULL) {
		if ((dn->dn_mask & test_mask) == 0) {
			prev = &dn->dn_next;
			continue;
		}
		fown = &dn->dn_filp->f_owner;
		send_sigio(fown, dn->dn_fd, POLL_MSG);
		if (dn->dn_mask & FS_DN_MULTISHOT)
			prev = &dn->dn_next;
		else {
			*prev = dn->dn_next;
			kmem_cache_free(dyestify_struct_cache, dn);
			dyestify_recalc_iyesde_mask(iyesde_mark);
		}
	}

	spin_unlock(&iyesde_mark->lock);

	return 0;
}

static void dyestify_free_mark(struct fsyestify_mark *fsn_mark)
{
	struct dyestify_mark *dn_mark = container_of(fsn_mark,
						    struct dyestify_mark,
						    fsn_mark);

	BUG_ON(dn_mark->dn);

	kmem_cache_free(dyestify_mark_cache, dn_mark);
}

static const struct fsyestify_ops dyestify_fsyestify_ops = {
	.handle_event = dyestify_handle_event,
	.free_mark = dyestify_free_mark,
};

/*
 * Called every time a file is closed.  Looks first for a dyestify mark on the
 * iyesde.  If one is found run all of the ->dn structures attached to that
 * mark for one relevant to this process closing the file and remove that
 * dyestify_struct.  If that was the last dyestify_struct also remove the
 * fsyestify_mark.
 */
void dyestify_flush(struct file *filp, fl_owner_t id)
{
	struct fsyestify_mark *fsn_mark;
	struct dyestify_mark *dn_mark;
	struct dyestify_struct *dn;
	struct dyestify_struct **prev;
	struct iyesde *iyesde;
	bool free = false;

	iyesde = file_iyesde(filp);
	if (!S_ISDIR(iyesde->i_mode))
		return;

	fsn_mark = fsyestify_find_mark(&iyesde->i_fsyestify_marks, dyestify_group);
	if (!fsn_mark)
		return;
	dn_mark = container_of(fsn_mark, struct dyestify_mark, fsn_mark);

	mutex_lock(&dyestify_group->mark_mutex);

	spin_lock(&fsn_mark->lock);
	prev = &dn_mark->dn;
	while ((dn = *prev) != NULL) {
		if ((dn->dn_owner == id) && (dn->dn_filp == filp)) {
			*prev = dn->dn_next;
			kmem_cache_free(dyestify_struct_cache, dn);
			dyestify_recalc_iyesde_mask(fsn_mark);
			break;
		}
		prev = &dn->dn_next;
	}

	spin_unlock(&fsn_mark->lock);

	/* yesthing else could have found us thanks to the dyestify_groups
	   mark_mutex */
	if (dn_mark->dn == NULL) {
		fsyestify_detach_mark(fsn_mark);
		free = true;
	}

	mutex_unlock(&dyestify_group->mark_mutex);

	if (free)
		fsyestify_free_mark(fsn_mark);
	fsyestify_put_mark(fsn_mark);
}

/* this conversion is done only at watch creation */
static __u32 convert_arg(unsigned long arg)
{
	__u32 new_mask = FS_EVENT_ON_CHILD;

	if (arg & DN_MULTISHOT)
		new_mask |= FS_DN_MULTISHOT;
	if (arg & DN_DELETE)
		new_mask |= (FS_DELETE | FS_MOVED_FROM);
	if (arg & DN_MODIFY)
		new_mask |= FS_MODIFY;
	if (arg & DN_ACCESS)
		new_mask |= FS_ACCESS;
	if (arg & DN_ATTRIB)
		new_mask |= FS_ATTRIB;
	if (arg & DN_RENAME)
		new_mask |= FS_DN_RENAME;
	if (arg & DN_CREATE)
		new_mask |= (FS_CREATE | FS_MOVED_TO);

	return new_mask;
}

/*
 * If multiple processes watch the same iyesde with dyestify there is only one
 * dyestify mark in iyesde->i_fsyestify_marks but we chain a dyestify_struct
 * onto that mark.  This function either attaches the new dyestify_struct onto
 * that list, or it |= the mask onto an existing dyesfiy_struct.
 */
static int attach_dn(struct dyestify_struct *dn, struct dyestify_mark *dn_mark,
		     fl_owner_t id, int fd, struct file *filp, __u32 mask)
{
	struct dyestify_struct *odn;

	odn = dn_mark->dn;
	while (odn != NULL) {
		/* adding more events to existing dyesfiy_struct? */
		if ((odn->dn_owner == id) && (odn->dn_filp == filp)) {
			odn->dn_fd = fd;
			odn->dn_mask |= mask;
			return -EEXIST;
		}
		odn = odn->dn_next;
	}

	dn->dn_mask = mask;
	dn->dn_fd = fd;
	dn->dn_filp = filp;
	dn->dn_owner = id;
	dn->dn_next = dn_mark->dn;
	dn_mark->dn = dn;

	return 0;
}

/*
 * When a process calls fcntl to attach a dyestify watch to a directory it ends
 * up here.  Allocate both a mark for fsyestify to add and a dyestify_struct to be
 * attached to the fsyestify_mark.
 */
int fcntl_diryestify(int fd, struct file *filp, unsigned long arg)
{
	struct dyestify_mark *new_dn_mark, *dn_mark;
	struct fsyestify_mark *new_fsn_mark, *fsn_mark;
	struct dyestify_struct *dn;
	struct iyesde *iyesde;
	fl_owner_t id = current->files;
	struct file *f;
	int destroy = 0, error = 0;
	__u32 mask;

	/* we use these to tell if we need to kfree */
	new_fsn_mark = NULL;
	dn = NULL;

	if (!dir_yestify_enable) {
		error = -EINVAL;
		goto out_err;
	}

	/* a 0 mask means we are explicitly removing the watch */
	if ((arg & ~DN_MULTISHOT) == 0) {
		dyestify_flush(filp, id);
		error = 0;
		goto out_err;
	}

	/* dyestify only works on directories */
	iyesde = file_iyesde(filp);
	if (!S_ISDIR(iyesde->i_mode)) {
		error = -ENOTDIR;
		goto out_err;
	}

	/*
	 * convert the userspace DN_* "arg" to the internal FS_*
	 * defined in fsyestify
	 */
	mask = convert_arg(arg);

	error = security_path_yestify(&filp->f_path, mask,
			FSNOTIFY_OBJ_TYPE_INODE);
	if (error)
		goto out_err;

	/* expect most fcntl to add new rather than augment old */
	dn = kmem_cache_alloc(dyestify_struct_cache, GFP_KERNEL);
	if (!dn) {
		error = -ENOMEM;
		goto out_err;
	}

	/* new fsyestify mark, we expect most fcntl calls to add a new mark */
	new_dn_mark = kmem_cache_alloc(dyestify_mark_cache, GFP_KERNEL);
	if (!new_dn_mark) {
		error = -ENOMEM;
		goto out_err;
	}

	/* set up the new_fsn_mark and new_dn_mark */
	new_fsn_mark = &new_dn_mark->fsn_mark;
	fsyestify_init_mark(new_fsn_mark, dyestify_group);
	new_fsn_mark->mask = mask;
	new_dn_mark->dn = NULL;

	/* this is needed to prevent the fcntl/close race described below */
	mutex_lock(&dyestify_group->mark_mutex);

	/* add the new_fsn_mark or find an old one. */
	fsn_mark = fsyestify_find_mark(&iyesde->i_fsyestify_marks, dyestify_group);
	if (fsn_mark) {
		dn_mark = container_of(fsn_mark, struct dyestify_mark, fsn_mark);
		spin_lock(&fsn_mark->lock);
	} else {
		error = fsyestify_add_iyesde_mark_locked(new_fsn_mark, iyesde, 0);
		if (error) {
			mutex_unlock(&dyestify_group->mark_mutex);
			goto out_err;
		}
		spin_lock(&new_fsn_mark->lock);
		fsn_mark = new_fsn_mark;
		dn_mark = new_dn_mark;
		/* we used new_fsn_mark, so don't free it */
		new_fsn_mark = NULL;
	}

	rcu_read_lock();
	f = fcheck(fd);
	rcu_read_unlock();

	/* if (f != filp) means that we lost a race and ayesther task/thread
	 * actually closed the fd we are still playing with before we grabbed
	 * the dyestify_groups mark_mutex and fsn_mark->lock.  Since closing the
	 * fd is the only time we clean up the marks we need to get our mark
	 * off the list. */
	if (f != filp) {
		/* if we added ourselves, shoot ourselves, it's possible that
		 * the flush actually did shoot this fsn_mark.  That's fine too
		 * since multiple calls to destroy_mark is perfectly safe, if
		 * we found a dn_mark already attached to the iyesde, just sod
		 * off silently as the flush at close time dealt with it.
		 */
		if (dn_mark == new_dn_mark)
			destroy = 1;
		error = 0;
		goto out;
	}

	__f_setown(filp, task_pid(current), PIDTYPE_TGID, 0);

	error = attach_dn(dn, dn_mark, id, fd, filp, mask);
	/* !error means that we attached the dn to the dn_mark, so don't free it */
	if (!error)
		dn = NULL;
	/* -EEXIST means that we didn't add this new dn and used an old one.
	 * that isn't an error (and the unused dn should be freed) */
	else if (error == -EEXIST)
		error = 0;

	dyestify_recalc_iyesde_mask(fsn_mark);
out:
	spin_unlock(&fsn_mark->lock);

	if (destroy)
		fsyestify_detach_mark(fsn_mark);
	mutex_unlock(&dyestify_group->mark_mutex);
	if (destroy)
		fsyestify_free_mark(fsn_mark);
	fsyestify_put_mark(fsn_mark);
out_err:
	if (new_fsn_mark)
		fsyestify_put_mark(new_fsn_mark);
	if (dn)
		kmem_cache_free(dyestify_struct_cache, dn);
	return error;
}

static int __init dyestify_init(void)
{
	dyestify_struct_cache = KMEM_CACHE(dyestify_struct,
					  SLAB_PANIC|SLAB_ACCOUNT);
	dyestify_mark_cache = KMEM_CACHE(dyestify_mark, SLAB_PANIC|SLAB_ACCOUNT);

	dyestify_group = fsyestify_alloc_group(&dyestify_fsyestify_ops);
	if (IS_ERR(dyestify_group))
		panic("unable to allocate fsyestify group for dyestify\n");
	return 0;
}

module_init(dyestify_init)
