// SPDX-License-Identifier: GPL-2.0-only
#include "cgroup-internal.h"

#include <linux/ctype.h>
#include <linux/kmod.h>
#include <linux/sort.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/magic.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/delayacct.h>
#include <linux/pid_namespace.h>
#include <linux/cgroupstats.h>
#include <linux/fs_parser.h>

#include <trace/events/cgroup.h>

/*
 * pidlists linger the following amount before being destroyed.  The goal
 * is avoiding frequent destruction in the middle of consecutive read calls
 * Expiring in the middle is a performance problem not a correctness one.
 * 1 sec should be enough.
 */
#define CGROUP_PIDLIST_DESTROY_DELAY	HZ

/* Controllers blocked by the commandline in v1 */
static u16 cgroup_no_v1_mask;

/* disable named v1 mounts */
static bool cgroup_no_v1_named;

/*
 * pidlist destructions need to be flushed on cgroup destruction.  Use a
 * separate workqueue as flush domain.
 */
static struct workqueue_struct *cgroup_pidlist_destroy_wq;

/* protects cgroup_subsys->release_agent_path */
static DEFINE_SPINLOCK(release_agent_path_lock);

bool cgroup1_ssid_disabled(int ssid)
{
	return cgroup_no_v1_mask & (1 << ssid);
}

/**
 * cgroup_attach_task_all - attach task 'tsk' to all cgroups of task 'from'
 * @from: attach to all cgroups of a given task
 * @tsk: the task to be attached
 */
int cgroup_attach_task_all(struct task_struct *from, struct task_struct *tsk)
{
	struct cgroup_root *root;
	int retval = 0;

	mutex_lock(&cgroup_mutex);
	percpu_down_write(&cgroup_threadgroup_rwsem);
	for_each_root(root) {
		struct cgroup *from_cgrp;

		if (root == &cgrp_dfl_root)
			continue;

		spin_lock_irq(&css_set_lock);
		from_cgrp = task_cgroup_from_root(from, root);
		spin_unlock_irq(&css_set_lock);

		retval = cgroup_attach_task(from_cgrp, tsk, false);
		if (retval)
			break;
	}
	percpu_up_write(&cgroup_threadgroup_rwsem);
	mutex_unlock(&cgroup_mutex);

	return retval;
}
EXPORT_SYMBOL_GPL(cgroup_attach_task_all);

/**
 * cgroup_trasnsfer_tasks - move tasks from one cgroup to another
 * @to: cgroup to which the tasks will be moved
 * @from: cgroup in which the tasks currently reside
 *
 * Locking rules between cgroup_post_fork() and the migration path
 * guarantee that, if a task is forking while being migrated, the new child
 * is guaranteed to be either visible in the source cgroup after the
 * parent's migration is complete or put into the target cgroup.  No task
 * can slip out of migration through forking.
 */
int cgroup_transfer_tasks(struct cgroup *to, struct cgroup *from)
{
	DEFINE_CGROUP_MGCTX(mgctx);
	struct cgrp_cset_link *link;
	struct css_task_iter it;
	struct task_struct *task;
	int ret;

	if (cgroup_on_dfl(to))
		return -EINVAL;

	ret = cgroup_migrate_vet_dst(to);
	if (ret)
		return ret;

	mutex_lock(&cgroup_mutex);

	percpu_down_write(&cgroup_threadgroup_rwsem);

	/* all tasks in @from are being moved, all csets are source */
	spin_lock_irq(&css_set_lock);
	list_for_each_entry(link, &from->cset_links, cset_link)
		cgroup_migrate_add_src(link->cset, to, &mgctx);
	spin_unlock_irq(&css_set_lock);

	ret = cgroup_migrate_prepare_dst(&mgctx);
	if (ret)
		goto out_err;

	/*
	 * Migrate tasks one-by-one until @from is empty.  This fails iff
	 * ->can_attach() fails.
	 */
	do {
		css_task_iter_start(&from->self, 0, &it);

		do {
			task = css_task_iter_next(&it);
		} while (task && (task->flags & PF_EXITING));

		if (task)
			get_task_struct(task);
		css_task_iter_end(&it);

		if (task) {
			ret = cgroup_migrate(task, false, &mgctx);
			if (!ret)
				TRACE_CGROUP_PATH(transfer_tasks, to, task, false);
			put_task_struct(task);
		}
	} while (task && !ret);
out_err:
	cgroup_migrate_finish(&mgctx);
	percpu_up_write(&cgroup_threadgroup_rwsem);
	mutex_unlock(&cgroup_mutex);
	return ret;
}

/*
 * Stuff for reading the 'tasks'/'procs' files.
 *
 * Reading this file can return large amounts of data if a cgroup has
 * *lots* of attached tasks. So it may need several calls to read(),
 * but we cannot guarantee that the information we produce is correct
 * unless we produce it entirely atomically.
 *
 */

/* which pidlist file are we talking about? */
enum cgroup_filetype {
	CGROUP_FILE_PROCS,
	CGROUP_FILE_TASKS,
};

/*
 * A pidlist is a list of pids that virtually represents the contents of one
 * of the cgroup files ("procs" or "tasks"). We keep a list of such pidlists,
 * a pair (one each for procs, tasks) for each pid namespace that's relevant
 * to the cgroup.
 */
struct cgroup_pidlist {
	/*
	 * used to find which pidlist is wanted. doesn't change as long as
	 * this particular list stays in the list.
	*/
	struct { enum cgroup_filetype type; struct pid_namespace *ns; } key;
	/* array of xids */
	pid_t *list;
	/* how many elements the above list has */
	int length;
	/* each of these stored in a list by its cgroup */
	struct list_head links;
	/* pointer to the cgroup we belong to, for list removal purposes */
	struct cgroup *owner;
	/* for delayed destruction */
	struct delayed_work destroy_dwork;
};

/*
 * Used to destroy all pidlists lingering waiting for destroy timer.  None
 * should be left afterwards.
 */
void cgroup1_pidlist_destroy_all(struct cgroup *cgrp)
{
	struct cgroup_pidlist *l, *tmp_l;

	mutex_lock(&cgrp->pidlist_mutex);
	list_for_each_entry_safe(l, tmp_l, &cgrp->pidlists, links)
		mod_delayed_work(cgroup_pidlist_destroy_wq, &l->destroy_dwork, 0);
	mutex_unlock(&cgrp->pidlist_mutex);

	flush_workqueue(cgroup_pidlist_destroy_wq);
	BUG_ON(!list_empty(&cgrp->pidlists));
}

static void cgroup_pidlist_destroy_work_fn(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct cgroup_pidlist *l = container_of(dwork, struct cgroup_pidlist,
						destroy_dwork);
	struct cgroup_pidlist *tofree = NULL;

	mutex_lock(&l->owner->pidlist_mutex);

	/*
	 * Destroy iff we didn't get queued again.  The state won't change
	 * as destroy_dwork can only be queued while locked.
	 */
	if (!delayed_work_pending(dwork)) {
		list_del(&l->links);
		kvfree(l->list);
		put_pid_ns(l->key.ns);
		tofree = l;
	}

	mutex_unlock(&l->owner->pidlist_mutex);
	kfree(tofree);
}

/*
 * pidlist_uniq - given a kmalloc()ed list, strip out all duplicate entries
 * Returns the number of unique elements.
 */
static int pidlist_uniq(pid_t *list, int length)
{
	int src, dest = 1;

	/*
	 * we presume the 0th element is unique, so i starts at 1. trivial
	 * edge cases first; no work needs to be done for either
	 */
	if (length == 0 || length == 1)
		return length;
	/* src and dest walk down the list; dest counts unique elements */
	for (src = 1; src < length; src++) {
		/* find next unique element */
		while (list[src] == list[src-1]) {
			src++;
			if (src == length)
				goto after;
		}
		/* dest always points to where the next unique element goes */
		list[dest] = list[src];
		dest++;
	}
after:
	return dest;
}

/*
 * The two pid files - task and cgroup.procs - guaranteed that the result
 * is sorted, which forced this whole pidlist fiasco.  As pid order is
 * different per namespace, each namespace needs differently sorted list,
 * making it impossible to use, for example, single rbtree of member tasks
 * sorted by task pointer.  As pidlists can be fairly large, allocating one
 * per open file is dangerous, so cgroup had to implement shared pool of
 * pidlists keyed by cgroup and namespace.
 */
static int cmppid(const void *a, const void *b)
{
	return *(pid_t *)a - *(pid_t *)b;
}

static struct cgroup_pidlist *cgroup_pidlist_find(struct cgroup *cgrp,
						  enum cgroup_filetype type)
{
	struct cgroup_pidlist *l;
	/* don't need task_nsproxy() if we're looking at ourself */
	struct pid_namespace *ns = task_active_pid_ns(current);

	lockdep_assert_held(&cgrp->pidlist_mutex);

	list_for_each_entry(l, &cgrp->pidlists, links)
		if (l->key.type == type && l->key.ns == ns)
			return l;
	return NULL;
}

/*
 * find the appropriate pidlist for our purpose (given procs vs tasks)
 * returns with the lock on that pidlist already held, and takes care
 * of the use count, or returns NULL with no locks held if we're out of
 * memory.
 */
static struct cgroup_pidlist *cgroup_pidlist_find_create(struct cgroup *cgrp,
						enum cgroup_filetype type)
{
	struct cgroup_pidlist *l;

	lockdep_assert_held(&cgrp->pidlist_mutex);

	l = cgroup_pidlist_find(cgrp, type);
	if (l)
		return l;

	/* entry not found; create a new one */
	l = kzalloc(sizeof(struct cgroup_pidlist), GFP_KERNEL);
	if (!l)
		return l;

	INIT_DELAYED_WORK(&l->destroy_dwork, cgroup_pidlist_destroy_work_fn);
	l->key.type = type;
	/* don't need task_nsproxy() if we're looking at ourself */
	l->key.ns = get_pid_ns(task_active_pid_ns(current));
	l->owner = cgrp;
	list_add(&l->links, &cgrp->pidlists);
	return l;
}

/*
 * Load a cgroup's pidarray with either procs' tgids or tasks' pids
 */
static int pidlist_array_load(struct cgroup *cgrp, enum cgroup_filetype type,
			      struct cgroup_pidlist **lp)
{
	pid_t *array;
	int length;
	int pid, n = 0; /* used for populating the array */
	struct css_task_iter it;
	struct task_struct *tsk;
	struct cgroup_pidlist *l;

	lockdep_assert_held(&cgrp->pidlist_mutex);

	/*
	 * If cgroup gets more users after we read count, we won't have
	 * enough space - tough.  This race is indistinguishable to the
	 * caller from the case that the additional cgroup users didn't
	 * show up until sometime later on.
	 */
	length = cgroup_task_count(cgrp);
	array = kvmalloc_array(length, sizeof(pid_t), GFP_KERNEL);
	if (!array)
		return -ENOMEM;
	/* now, populate the array */
	css_task_iter_start(&cgrp->self, 0, &it);
	while ((tsk = css_task_iter_next(&it))) {
		if (unlikely(n == length))
			break;
		/* get tgid or pid for procs or tasks file respectively */
		if (type == CGROUP_FILE_PROCS)
			pid = task_tgid_vnr(tsk);
		else
			pid = task_pid_vnr(tsk);
		if (pid > 0) /* make sure to only use valid results */
			array[n++] = pid;
	}
	css_task_iter_end(&it);
	length = n;
	/* now sort & (if procs) strip out duplicates */
	sort(array, length, sizeof(pid_t), cmppid, NULL);
	if (type == CGROUP_FILE_PROCS)
		length = pidlist_uniq(array, length);

	l = cgroup_pidlist_find_create(cgrp, type);
	if (!l) {
		kvfree(array);
		return -ENOMEM;
	}

	/* store array, freeing old if necessary */
	kvfree(l->list);
	l->list = array;
	l->length = length;
	*lp = l;
	return 0;
}

/*
 * seq_file methods for the tasks/procs files. The seq_file position is the
 * next pid to display; the seq_file iterator is a pointer to the pid
 * in the cgroup->l->list array.
 */

static void *cgroup_pidlist_start(struct seq_file *s, loff_t *pos)
{
	/*
	 * Initially we receive a position value that corresponds to
	 * one more than the last pid shown (or 0 on the first call or
	 * after a seek to the start). Use a binary-search to find the
	 * next pid to display, if any
	 */
	struct kernfs_open_file *of = s->private;
	struct cgroup *cgrp = seq_css(s)->cgroup;
	struct cgroup_pidlist *l;
	enum cgroup_filetype type = seq_cft(s)->private;
	int index = 0, pid = *pos;
	int *iter, ret;

	mutex_lock(&cgrp->pidlist_mutex);

	/*
	 * !NULL @of->priv indicates that this isn't the first start()
	 * after open.  If the matching pidlist is around, we can use that.
	 * Look for it.  Note that @of->priv can't be used directly.  It
	 * could already have been destroyed.
	 */
	if (of->priv)
		of->priv = cgroup_pidlist_find(cgrp, type);

	/*
	 * Either this is the first start() after open or the matching
	 * pidlist has been destroyed inbetween.  Create a new one.
	 */
	if (!of->priv) {
		ret = pidlist_array_load(cgrp, type,
					 (struct cgroup_pidlist **)&of->priv);
		if (ret)
			return ERR_PTR(ret);
	}
	l = of->priv;

	if (pid) {
		int end = l->length;

		while (index < end) {
			int mid = (index + end) / 2;
			if (l->list[mid] == pid) {
				index = mid;
				break;
			} else if (l->list[mid] <= pid)
				index = mid + 1;
			else
				end = mid;
		}
	}
	/* If we're off the end of the array, we're done */
	if (index >= l->length)
		return NULL;
	/* Update the abstract position to be the actual pid that we found */
	iter = l->list + index;
	*pos = *iter;
	return iter;
}

static void cgroup_pidlist_stop(struct seq_file *s, void *v)
{
	struct kernfs_open_file *of = s->private;
	struct cgroup_pidlist *l = of->priv;

	if (l)
		mod_delayed_work(cgroup_pidlist_destroy_wq, &l->destroy_dwork,
				 CGROUP_PIDLIST_DESTROY_DELAY);
	mutex_unlock(&seq_css(s)->cgroup->pidlist_mutex);
}

static void *cgroup_pidlist_next(struct seq_file *s, void *v, loff_t *pos)
{
	struct kernfs_open_file *of = s->private;
	struct cgroup_pidlist *l = of->priv;
	pid_t *p = v;
	pid_t *end = l->list + l->length;
	/*
	 * Advance to the next pid in the array. If this goes off the
	 * end, we're done
	 */
	p++;
	if (p >= end) {
		(*pos)++;
		return NULL;
	} else {
		*pos = *p;
		return p;
	}
}

static int cgroup_pidlist_show(struct seq_file *s, void *v)
{
	seq_printf(s, "%d\n", *(int *)v);

	return 0;
}

static ssize_t __cgroup1_procs_write(struct kernfs_open_file *of,
				     char *buf, size_t nbytes, loff_t off,
				     bool threadgroup)
{
	struct cgroup *cgrp;
	struct task_struct *task;
	const struct cred *cred, *tcred;
	ssize_t ret;
	bool locked;

	cgrp = cgroup_kn_lock_live(of->kn, false);
	if (!cgrp)
		return -ENODEV;

	task = cgroup_procs_write_start(buf, threadgroup, &locked);
	ret = PTR_ERR_OR_ZERO(task);
	if (ret)
		goto out_unlock;

	/*
	 * Even if we're attaching all tasks in the thread group, we only
	 * need to check permissions on one of them.
	 */
	cred = current_cred();
	tcred = get_task_cred(task);
	if (!uid_eq(cred->euid, GLOBAL_ROOT_UID) &&
	    !uid_eq(cred->euid, tcred->uid) &&
	    !uid_eq(cred->euid, tcred->suid) &&
	    !ns_capable(tcred->user_ns, CAP_SYS_NICE))
		ret = -EACCES;
	put_cred(tcred);
	if (ret)
		goto out_finish;

	ret = cgroup_attach_task(cgrp, task, threadgroup);

out_finish:
	cgroup_procs_write_finish(task, locked);
out_unlock:
	cgroup_kn_unlock(of->kn);

	return ret ?: nbytes;
}

static ssize_t cgroup1_procs_write(struct kernfs_open_file *of,
				   char *buf, size_t nbytes, loff_t off)
{
	return __cgroup1_procs_write(of, buf, nbytes, off, true);
}

static ssize_t cgroup1_tasks_write(struct kernfs_open_file *of,
				   char *buf, size_t nbytes, loff_t off)
{
	return __cgroup1_procs_write(of, buf, nbytes, off, false);
}

static ssize_t cgroup_release_agent_write(struct kernfs_open_file *of,
					  char *buf, size_t nbytes, loff_t off)
{
	struct cgroup *cgrp;

	BUILD_BUG_ON(sizeof(cgrp->root->release_agent_path) < PATH_MAX);

	cgrp = cgroup_kn_lock_live(of->kn, false);
	if (!cgrp)
		return -ENODEV;
	spin_lock(&release_agent_path_lock);
	strlcpy(cgrp->root->release_agent_path, strstrip(buf),
		sizeof(cgrp->root->release_agent_path));
	spin_unlock(&release_agent_path_lock);
	cgroup_kn_unlock(of->kn);
	return nbytes;
}

static int cgroup_release_agent_show(struct seq_file *seq, void *v)
{
	struct cgroup *cgrp = seq_css(seq)->cgroup;

	spin_lock(&release_agent_path_lock);
	seq_puts(seq, cgrp->root->release_agent_path);
	spin_unlock(&release_agent_path_lock);
	seq_putc(seq, '\n');
	return 0;
}

static int cgroup_sane_behavior_show(struct seq_file *seq, void *v)
{
	seq_puts(seq, "0\n");
	return 0;
}

static u64 cgroup_read_notify_on_release(struct cgroup_subsys_state *css,
					 struct cftype *cft)
{
	return notify_on_release(css->cgroup);
}

static int cgroup_write_notify_on_release(struct cgroup_subsys_state *css,
					  struct cftype *cft, u64 val)
{
	if (val)
		set_bit(CGRP_NOTIFY_ON_RELEASE, &css->cgroup->flags);
	else
		clear_bit(CGRP_NOTIFY_ON_RELEASE, &css->cgroup->flags);
	return 0;
}

static u64 cgroup_clone_children_read(struct cgroup_subsys_state *css,
				      struct cftype *cft)
{
	return test_bit(CGRP_CPUSET_CLONE_CHILDREN, &css->cgroup->flags);
}

static int cgroup_clone_children_write(struct cgroup_subsys_state *css,
				       struct cftype *cft, u64 val)
{
	if (val)
		set_bit(CGRP_CPUSET_CLONE_CHILDREN, &css->cgroup->flags);
	else
		clear_bit(CGRP_CPUSET_CLONE_CHILDREN, &css->cgroup->flags);
	return 0;
}

/* cgroup core interface files for the legacy hierarchies */
struct cftype cgroup1_base_files[] = {
	{
		.name = "cgroup.procs",
		.seq_start = cgroup_pidlist_start,
		.seq_next = cgroup_pidlist_next,
		.seq_stop = cgroup_pidlist_stop,
		.seq_show = cgroup_pidlist_show,
		.private = CGROUP_FILE_PROCS,
		.write = cgroup1_procs_write,
	},
	{
		.name = "cgroup.clone_children",
		.read_u64 = cgroup_clone_children_read,
		.write_u64 = cgroup_clone_children_write,
	},
	{
		.name = "cgroup.sane_behavior",
		.flags = CFTYPE_ONLY_ON_ROOT,
		.seq_show = cgroup_sane_behavior_show,
	},
	{
		.name = "tasks",
		.seq_start = cgroup_pidlist_start,
		.seq_next = cgroup_pidlist_next,
		.seq_stop = cgroup_pidlist_stop,
		.seq_show = cgroup_pidlist_show,
		.private = CGROUP_FILE_TASKS,
		.write = cgroup1_tasks_write,
	},
	{
		.name = "notify_on_release",
		.read_u64 = cgroup_read_notify_on_release,
		.write_u64 = cgroup_write_notify_on_release,
	},
	{
		.name = "release_agent",
		.flags = CFTYPE_ONLY_ON_ROOT,
		.seq_show = cgroup_release_agent_show,
		.write = cgroup_release_agent_write,
		.max_write_len = PATH_MAX - 1,
	},
	{ }	/* terminate */
};

/* Display information about each subsystem and each hierarchy */
int proc_cgroupstats_show(struct seq_file *m, void *v)
{
	struct cgroup_subsys *ss;
	int i;

	seq_puts(m, "#subsys_name\thierarchy\tnum_cgroups\tenabled\n");
	/*
	 * ideally we don't want subsystems moving around while we do this.
	 * cgroup_mutex is also necessary to guarantee an atomic snapshot of
	 * subsys/hierarchy state.
	 */
	mutex_lock(&cgroup_mutex);

	for_each_subsys(ss, i)
		seq_printf(m, "%s\t%d\t%d\t%d\n",
			   ss->legacy_name, ss->root->hierarchy_id,
			   atomic_read(&ss->root->nr_cgrps),
			   cgroup_ssid_enabled(i));

	mutex_unlock(&cgroup_mutex);
	return 0;
}

/**
 * cgroupstats_build - build and fill cgroupstats
 * @stats: cgroupstats to fill information into
 * @dentry: A dentry entry belonging to the cgroup for which stats have
 * been requested.
 *
 * Build and fill cgroupstats so that taskstats can export it to user
 * space.
 */
int cgroupstats_build(struct cgroupstats *stats, struct dentry *dentry)
{
	struct kernfs_node *kn = kernfs_node_from_dentry(dentry);
	struct cgroup *cgrp;
	struct css_task_iter it;
	struct task_struct *tsk;

	/* it should be kernfs_node belonging to cgroupfs and is a directory */
	if (dentry->d_sb->s_type != &cgroup_fs_type || !kn ||
	    kernfs_type(kn) != KERNFS_DIR)
		return -EINVAL;

	mutex_lock(&cgroup_mutex);

	/*
	 * We aren't being called from kernfs and there's no guarantee on
	 * @kn->priv's validity.  For this and css_tryget_online_from_dir(),
	 * @kn->priv is RCU safe.  Let's do the RCU dancing.
	 */
	rcu_read_lock();
	cgrp = rcu_dereference(*(void __rcu __force **)&kn->priv);
	if (!cgrp || cgroup_is_dead(cgrp)) {
		rcu_read_unlock();
		mutex_unlock(&cgroup_mutex);
		return -ENOENT;
	}
	rcu_read_unlock();

	css_task_iter_start(&cgrp->self, 0, &it);
	while ((tsk = css_task_iter_next(&it))) {
		switch (tsk->state) {
		case TASK_RUNNING:
			stats->nr_running++;
			break;
		case TASK_INTERRUPTIBLE:
			stats->nr_sleeping++;
			break;
		case TASK_UNINTERRUPTIBLE:
			stats->nr_uninterruptible++;
			break;
		case TASK_STOPPED:
			stats->nr_stopped++;
			break;
		default:
			if (delayacct_is_task_waiting_on_io(tsk))
				stats->nr_io_wait++;
			break;
		}
	}
	css_task_iter_end(&it);

	mutex_unlock(&cgroup_mutex);
	return 0;
}

void cgroup1_check_for_release(struct cgroup *cgrp)
{
	if (notify_on_release(cgrp) && !cgroup_is_populated(cgrp) &&
	    !css_has_online_children(&cgrp->self) && !cgroup_is_dead(cgrp))
		schedule_work(&cgrp->release_agent_work);
}

/*
 * Notify userspace when a cgroup is released, by running the
 * configured release agent with the name of the cgroup (path
 * relative to the root of cgroup file system) as the argument.
 *
 * Most likely, this user command will try to rmdir this cgroup.
 *
 * This races with the possibility that some other task will be
 * attached to this cgroup before it is removed, or that some other
 * user task will 'mkdir' a child cgroup of this cgroup.  That's ok.
 * The presumed 'rmdir' will fail quietly if this cgroup is no longer
 * unused, and this cgroup will be reprieved from its death sentence,
 * to continue to serve a useful existence.  Next time it's released,
 * we will get notified again, if it still has 'notify_on_release' set.
 *
 * The final arg to call_usermodehelper() is UMH_WAIT_EXEC, which
 * means only wait until the task is successfully execve()'d.  The
 * separate release agent task is forked by call_usermodehelper(),
 * then control in this thread returns here, without waiting for the
 * release agent task.  We don't bother to wait because the caller of
 * this routine has no use for the exit status of the release agent
 * task, so no sense holding our caller up for that.
 */
void cgroup1_release_agent(struct work_struct *work)
{
	struct cgroup *cgrp =
		container_of(work, struct cgroup, release_agent_work);
	char *pathbuf, *agentbuf;
	char *argv[3], *envp[3];
	int ret;

	/* snoop agent path and exit early if empty */
	if (!cgrp->root->release_agent_path[0])
		return;

	/* prepare argument buffers */
	pathbuf = kmalloc(PATH_MAX, GFP_KERNEL);
	agentbuf = kmalloc(PATH_MAX, GFP_KERNEL);
	if (!pathbuf || !agentbuf)
		goto out_free;

	spin_lock(&release_agent_path_lock);
	strlcpy(agentbuf, cgrp->root->release_agent_path, PATH_MAX);
	spin_unlock(&release_agent_path_lock);
	if (!agentbuf[0])
		goto out_free;

	ret = cgroup_path_ns(cgrp, pathbuf, PATH_MAX, &init_cgroup_ns);
	if (ret < 0 || ret >= PATH_MAX)
		goto out_free;

	argv[0] = agentbuf;
	argv[1] = pathbuf;
	argv[2] = NULL;

	/* minimal command environment */
	envp[0] = "HOME=/";
	envp[1] = "PATH=/sbin:/bin:/usr/sbin:/usr/bin";
	envp[2] = NULL;

	call_usermodehelper(argv[0], argv, envp, UMH_WAIT_EXEC);
out_free:
	kfree(agentbuf);
	kfree(pathbuf);
}

/*
 * cgroup_rename - Only allow simple rename of directories in place.
 */
static int cgroup1_rename(struct kernfs_node *kn, struct kernfs_node *new_parent,
			  const char *new_name_str)
{
	struct cgroup *cgrp = kn->priv;
	int ret;

	if (kernfs_type(kn) != KERNFS_DIR)
		return -ENOTDIR;
	if (kn->parent != new_parent)
		return -EIO;

	/*
	 * We're gonna grab cgroup_mutex which nests outside kernfs
	 * active_ref.  kernfs_rename() doesn't require active_ref
	 * protection.  Break them before grabbing cgroup_mutex.
	 */
	kernfs_break_active_protection(new_parent);
	kernfs_break_active_protection(kn);

	mutex_lock(&cgroup_mutex);

	ret = kernfs_rename(kn, new_parent, new_name_str);
	if (!ret)
		TRACE_CGROUP_PATH(rename, cgrp);

	mutex_unlock(&cgroup_mutex);

	kernfs_unbreak_active_protection(kn);
	kernfs_unbreak_active_protection(new_parent);
	return ret;
}

static int cgroup1_show_options(struct seq_file *seq, struct kernfs_root *kf_root)
{
	struct cgroup_root *root = cgroup_root_from_kf(kf_root);
	struct cgroup_subsys *ss;
	int ssid;

	for_each_subsys(ss, ssid)
		if (root->subsys_mask & (1 << ssid))
			seq_show_option(seq, ss->legacy_name, NULL);
	if (root->flags & CGRP_ROOT_NOPREFIX)
		seq_puts(seq, ",noprefix");
	if (root->flags & CGRP_ROOT_XATTR)
		seq_puts(seq, ",xattr");
	if (root->flags & CGRP_ROOT_CPUSET_V2_MODE)
		seq_puts(seq, ",cpuset_v2_mode");

	spin_lock(&release_agent_path_lock);
	if (strlen(root->release_agent_path))
		seq_show_option(seq, "release_agent",
				root->release_agent_path);
	spin_unlock(&release_agent_path_lock);

	if (test_bit(CGRP_CPUSET_CLONE_CHILDREN, &root->cgrp.flags))
		seq_puts(seq, ",clone_children");
	if (strlen(root->name))
		seq_show_option(seq, "name", root->name);
	return 0;
}

enum cgroup1_param {
	Opt_all,
	Opt_clone_children,
	Opt_cpuset_v2_mode,
	Opt_name,
	Opt_none,
	Opt_noprefix,
	Opt_release_agent,
	Opt_xattr,
};

const struct fs_parameter_spec cgroup1_fs_parameters[] = {
	fsparam_flag  ("all",		Opt_all),
	fsparam_flag  ("clone_children", Opt_clone_children),
	fsparam_flag  ("cpuset_v2_mode", Opt_cpuset_v2_mode),
	fsparam_string("name",		Opt_name),
	fsparam_flag  ("none",		Opt_none),
	fsparam_flag  ("noprefix",	Opt_noprefix),
	fsparam_string("release_agent",	Opt_release_agent),
	fsparam_flag  ("xattr",		Opt_xattr),
	{}
};

int cgroup1_parse_param(struct fs_context *fc, struct fs_parameter *param)
{
	struct cgroup_fs_context *ctx = cgroup_fc2context(fc);
	struct cgroup_subsys *ss;
	struct fs_parse_result result;
	int opt, i;

	opt = fs_parse(fc, cgroup1_fs_parameters, param, &result);
	if (opt == -ENOPARAM) {
		if (strcmp(param->key, "source") == 0) {
			if (fc->source)
				return invalf(fc, "Multiple sources not supported");
			fc->source = param->string;
			param->string = NULL;
			return 0;
		}
		for_each_subsys(ss, i) {
			if (strcmp(param->key, ss->legacy_name))
				continue;
			ctx->subsys_mask |= (1 << i);
			return 0;
		}
		return invalfc(fc, "Unknown subsys name '%s'", param->key);
	}
	if (opt < 0)
		return opt;

	switch (opt) {
	case Opt_none:
		/* Explicitly have no subsystems */
		ctx->none = true;
		break;
	case Opt_all:
		ctx->all_ss = true;
		break;
	case Opt_noprefix:
		ctx->flags |= CGRP_ROOT_NOPREFIX;
		break;
	case Opt_clone_children:
		ctx->cpuset_clone_children = true;
		break;
	case Opt_cpuset_v2_mode:
		ctx->flags |= CGRP_ROOT_CPUSET_V2_MODE;
		break;
	case Opt_xattr:
		ctx->flags |= CGRP_ROOT_XATTR;
		break;
	case Opt_release_agent:
		/* Specifying two release agents is forbidden */
		if (ctx->release_agent)
			return invalfc(fc, "release_agent respecified");
		ctx->release_agent = param->string;
		param->string = NULL;
		break;
	case Opt_name:
		/* blocked by boot param? */
		if (cgroup_no_v1_named)
			return -ENOENT;
		/* Can't specify an empty name */
		if (!param->size)
			return invalfc(fc, "Empty name");
		if (param->size > MAX_CGROUP_ROOT_NAMELEN - 1)
			return invalfc(fc, "Name too long");
		/* Must match [\w.-]+ */
		for (i = 0; i < param->size; i++) {
			char c = param->string[i];
			if (isalnum(c))
				continue;
			if ((c == '.') || (c == '-') || (c == '_'))
				continue;
			return invalfc(fc, "Invalid name");
		}
		/* Specifying two names is forbidden */
		if (ctx->name)
			return invalfc(fc, "name respecified");
		ctx->name = param->string;
		param->string = NULL;
		break;
	}
	return 0;
}

static int check_cgroupfs_options(struct fs_context *fc)
{
	struct cgroup_fs_context *ctx = cgroup_fc2context(fc);
	u16 mask = U16_MAX;
	u16 enabled = 0;
	struct cgroup_subsys *ss;
	int i;

#ifdef CONFIG_CPUSETS
	mask = ~((u16)1 << cpuset_cgrp_id);
#endif
	for_each_subsys(ss, i)
		if (cgroup_ssid_enabled(i) && !cgroup1_ssid_disabled(i))
			enabled |= 1 << i;

	ctx->subsys_mask &= enabled;

	/*
	 * In absense of 'none', 'name=' or subsystem name options,
	 * let's default to 'all'.
	 */
	if (!ctx->subsys_mask && !ctx->none && !ctx->name)
		ctx->all_ss = true;

	if (ctx->all_ss) {
		/* Mutually exclusive option 'all' + subsystem name */
		if (ctx->subsys_mask)
			return invalfc(fc, "subsys name conflicts with all");
		/* 'all' => select all the subsystems */
		ctx->subsys_mask = enabled;
	}

	/*
	 * We either have to specify by name or by subsystems. (So all
	 * empty hierarchies must have a name).
	 */
	if (!ctx->subsys_mask && !ctx->name)
		return invalfc(fc, "Need name or subsystem set");

	/*
	 * Option noprefix was introduced just for backward compatibility
	 * with the old cpuset, so we allow noprefix only if mounting just
	 * the cpuset subsystem.
	 */
	if ((ctx->flags & CGRP_ROOT_NOPREFIX) && (ctx->subsys_mask & mask))
		return invalfc(fc, "noprefix used incorrectly");

	/* Can't specify "none" and some subsystems */
	if (ctx->subsys_mask && ctx->none)
		return invalfc(fc, "none used incorrectly");

	return 0;
}

int cgroup1_reconfigure(struct fs_context *fc)
{
	struct cgroup_fs_context *ctx = cgroup_fc2context(fc);
	struct kernfs_root *kf_root = kernfs_root_from_sb(fc->root->d_sb);
	struct cgroup_root *root = cgroup_root_from_kf(kf_root);
	int ret = 0;
	u16 added_mask, removed_mask;

	cgroup_lock_and_drain_offline(&cgrp_dfl_root.cgrp);

	/* See what subsystems are wanted */
	ret = check_cgroupfs_options(fc);
	if (ret)
		goto out_unlock;

	if (ctx->subsys_mask != root->subsys_mask || ctx->release_agent)
		pr_warn("option changes via remount are deprecated (pid=%d comm=%s)\n",
			task_tgid_nr(current), current->comm);

	added_mask = ctx->subsys_mask & ~root->subsys_mask;
	removed_mask = root->subsys_mask & ~ctx->subsys_mask;

	/* Don't allow flags or name to change at remount */
	if ((ctx->flags ^ root->flags) ||
	    (ctx->name && strcmp(ctx->name, root->name))) {
		errorfc(fc, "option or name mismatch, new: 0x%x \"%s\", old: 0x%x \"%s\"",
		       ctx->flags, ctx->name ?: "", root->flags, root->name);
		ret = -EINVAL;
		goto out_unlock;
	}

	/* remounting is not allowed for populated hierarchies */
	if (!list_empty(&root->cgrp.self.children)) {
		ret = -EBUSY;
		goto out_unlock;
	}

	ret = rebind_subsystems(root, added_mask);
	if (ret)
		goto out_unlock;

	WARN_ON(rebind_subsystems(&cgrp_dfl_root, removed_mask));

	if (ctx->release_agent) {
		spin_lock(&release_agent_path_lock);
		strcpy(root->release_agent_path, ctx->release_agent);
		spin_unlock(&release_agent_path_lock);
	}

	trace_cgroup_remount(root);

 out_unlock:
	mutex_unlock(&cgroup_mutex);
	return ret;
}

struct kernfs_syscall_ops cgroup1_kf_syscall_ops = {
	.rename			= cgroup1_rename,
	.show_options		= cgroup1_show_options,
	.mkdir			= cgroup_mkdir,
	.rmdir			= cgroup_rmdir,
	.show_path		= cgroup_show_path,
};

/*
 * The guts of cgroup1 mount - find or create cgroup_root to use.
 * Called with cgroup_mutex held; returns 0 on success, -E... on
 * error and positive - in case when the candidate is busy dying.
 * On success it stashes a reference to cgroup_root into given
 * cgroup_fs_context; that reference is *NOT* counting towards the
 * cgroup_root refcount.
 */
static int cgroup1_root_to_use(struct fs_context *fc)
{
	struct cgroup_fs_context *ctx = cgroup_fc2context(fc);
	struct cgroup_root *root;
	struct cgroup_subsys *ss;
	int i, ret;

	/* First find the desired set of subsystems */
	ret = check_cgroupfs_options(fc);
	if (ret)
		return ret;

	/*
	 * Destruction of cgroup root is asynchronous, so subsystems may
	 * still be dying after the previous unmount.  Let's drain the
	 * dying subsystems.  We just need to ensure that the ones
	 * unmounted previously finish dying and don't care about new ones
	 * starting.  Testing ref liveliness is good enough.
	 */
	for_each_subsys(ss, i) {
		if (!(ctx->subsys_mask & (1 << i)) ||
		    ss->root == &cgrp_dfl_root)
			continue;

		if (!percpu_ref_tryget_live(&ss->root->cgrp.self.refcnt))
			return 1;	/* restart */
		cgroup_put(&ss->root->cgrp);
	}

	for_each_root(root) {
		bool name_match = false;

		if (root == &cgrp_dfl_root)
			continue;

		/*
		 * If we asked for a name then it must match.  Also, if
		 * name matches but sybsys_mask doesn't, we should fail.
		 * Remember whether name matched.
		 */
		if (ctx->name) {
			if (strcmp(ctx->name, root->name))
				continue;
			name_match = true;
		}

		/*
		 * If we asked for subsystems (or explicitly for no
		 * subsystems) then they must match.
		 */
		if ((ctx->subsys_mask || ctx->none) &&
		    (ctx->subsys_mask != root->subsys_mask)) {
			if (!name_match)
				continue;
			return -EBUSY;
		}

		if (root->flags ^ ctx->flags)
			pr_warn("new mount options do not match the existing superblock, will be ignored\n");

		ctx->root = root;
		return 0;
	}

	/*
	 * No such thing, create a new one.  name= matching without subsys
	 * specification is allowed for already existing hierarchies but we
	 * can't create new one without subsys specification.
	 */
	if (!ctx->subsys_mask && !ctx->none)
		return invalfc(fc, "No subsys list or none specified");

	/* Hierarchies may only be created in the initial cgroup namespace. */
	if (ctx->ns != &init_cgroup_ns)
		return -EPERM;

	root = kzalloc(sizeof(*root), GFP_KERNEL);
	if (!root)
		return -ENOMEM;

	ctx->root = root;
	init_cgroup_root(ctx);

	ret = cgroup_setup_root(root, ctx->subsys_mask);
	if (ret)
		cgroup_free_root(root);
	return ret;
}

int cgroup1_get_tree(struct fs_context *fc)
{
	struct cgroup_fs_context *ctx = cgroup_fc2context(fc);
	int ret;

	/* Check if the caller has permission to mount. */
	if (!ns_capable(ctx->ns->user_ns, CAP_SYS_ADMIN))
		return -EPERM;

	cgroup_lock_and_drain_offline(&cgrp_dfl_root.cgrp);

	ret = cgroup1_root_to_use(fc);
	if (!ret && !percpu_ref_tryget_live(&ctx->root->cgrp.self.refcnt))
		ret = 1;	/* restart */

	mutex_unlock(&cgroup_mutex);

	if (!ret)
		ret = cgroup_do_get_tree(fc);

	if (!ret && percpu_ref_is_dying(&ctx->root->cgrp.self.refcnt)) {
		struct super_block *sb = fc->root->d_sb;
		dput(fc->root);
		deactivate_locked_super(sb);
		ret = 1;
	}

	if (unlikely(ret > 0)) {
		msleep(10);
		return restart_syscall();
	}
	return ret;
}

static int __init cgroup1_wq_init(void)
{
	/*
	 * Used to destroy pidlists and separate to serve as flush domain.
	 * Cap @max_active to 1 too.
	 */
	cgroup_pidlist_destroy_wq = alloc_workqueue("cgroup_pidlist_destroy",
						    0, 1);
	BUG_ON(!cgroup_pidlist_destroy_wq);
	return 0;
}
core_initcall(cgroup1_wq_init);

static int __init cgroup_no_v1(char *str)
{
	struct cgroup_subsys *ss;
	char *token;
	int i;

	while ((token = strsep(&str, ",")) != NULL) {
		if (!*token)
			continue;

		if (!strcmp(token, "all")) {
			cgroup_no_v1_mask = U16_MAX;
			continue;
		}

		if (!strcmp(token, "named")) {
			cgroup_no_v1_named = true;
			continue;
		}

		for_each_subsys(ss, i) {
			if (strcmp(token, ss->name) &&
			    strcmp(token, ss->legacy_name))
				continue;

			cgroup_no_v1_mask |= 1 << i;
		}
	}
	return 1;
}
__setup("cgroup_no_v1=", cgroup_no_v1);
