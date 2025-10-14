// SPDX-License-Identifier: GPL-2.0
#include <linux/anon_inodes.h>
#include <linux/exportfs.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/cgroup.h>
#include <linux/magic.h>
#include <linux/mount.h>
#include <linux/pid.h>
#include <linux/pidfs.h>
#include <linux/pid_namespace.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/proc_ns.h>
#include <linux/pseudo_fs.h>
#include <linux/ptrace.h>
#include <linux/seq_file.h>
#include <uapi/linux/pidfd.h>
#include <linux/ipc_namespace.h>
#include <linux/time_namespace.h>
#include <linux/utsname.h>
#include <net/net_namespace.h>
#include <linux/coredump.h>
#include <linux/xattr.h>

#include "internal.h"
#include "mount.h"

#define PIDFS_PID_DEAD ERR_PTR(-ESRCH)

static struct kmem_cache *pidfs_attr_cachep __ro_after_init;
static struct kmem_cache *pidfs_xattr_cachep __ro_after_init;

static struct path pidfs_root_path = {};

void pidfs_get_root(struct path *path)
{
	*path = pidfs_root_path;
	path_get(path);
}

/*
 * Stashes information that userspace needs to access even after the
 * process has been reaped.
 */
struct pidfs_exit_info {
	__u64 cgroupid;
	__s32 exit_code;
	__u32 coredump_mask;
};

struct pidfs_attr {
	struct simple_xattrs *xattrs;
	struct pidfs_exit_info __pei;
	struct pidfs_exit_info *exit_info;
};

static struct rb_root pidfs_ino_tree = RB_ROOT;

#if BITS_PER_LONG == 32
static inline unsigned long pidfs_ino(u64 ino)
{
	return lower_32_bits(ino);
}

/* On 32 bit the generation number are the upper 32 bits. */
static inline u32 pidfs_gen(u64 ino)
{
	return upper_32_bits(ino);
}

#else

/* On 64 bit simply return ino. */
static inline unsigned long pidfs_ino(u64 ino)
{
	return ino;
}

/* On 64 bit the generation number is 0. */
static inline u32 pidfs_gen(u64 ino)
{
	return 0;
}
#endif

static int pidfs_ino_cmp(struct rb_node *a, const struct rb_node *b)
{
	struct pid *pid_a = rb_entry(a, struct pid, pidfs_node);
	struct pid *pid_b = rb_entry(b, struct pid, pidfs_node);
	u64 pid_ino_a = pid_a->ino;
	u64 pid_ino_b = pid_b->ino;

	if (pid_ino_a < pid_ino_b)
		return -1;
	if (pid_ino_a > pid_ino_b)
		return 1;
	return 0;
}

void pidfs_add_pid(struct pid *pid)
{
	static u64 pidfs_ino_nr = 2;

	/*
	 * On 64 bit nothing special happens. The 64bit number assigned
	 * to struct pid is the inode number.
	 *
	 * On 32 bit the 64 bit number assigned to struct pid is split
	 * into two 32 bit numbers. The lower 32 bits are used as the
	 * inode number and the upper 32 bits are used as the inode
	 * generation number.
	 *
	 * On 32 bit pidfs_ino() will return the lower 32 bit. When
	 * pidfs_ino() returns zero a wrap around happened. When a
	 * wraparound happens the 64 bit number will be incremented by 2
	 * so inode numbering starts at 2 again.
	 *
	 * On 64 bit comparing two pidfds is as simple as comparing
	 * inode numbers.
	 *
	 * When a wraparound happens on 32 bit multiple pidfds with the
	 * same inode number are likely to exist (This isn't a problem
	 * since before pidfs pidfds used the anonymous inode meaning
	 * all pidfds had the same inode number.). Userspace can
	 * reconstruct the 64 bit identifier by retrieving both the
	 * inode number and the inode generation number to compare or
	 * use file handles.
	 */
	if (pidfs_ino(pidfs_ino_nr) == 0)
		pidfs_ino_nr += 2;

	pid->ino = pidfs_ino_nr;
	pid->stashed = NULL;
	pid->attr = NULL;
	pidfs_ino_nr++;

	write_seqcount_begin(&pidmap_lock_seq);
	rb_find_add_rcu(&pid->pidfs_node, &pidfs_ino_tree, pidfs_ino_cmp);
	write_seqcount_end(&pidmap_lock_seq);
}

void pidfs_remove_pid(struct pid *pid)
{
	write_seqcount_begin(&pidmap_lock_seq);
	rb_erase(&pid->pidfs_node, &pidfs_ino_tree);
	write_seqcount_end(&pidmap_lock_seq);
}

void pidfs_free_pid(struct pid *pid)
{
	struct pidfs_attr *attr __free(kfree) = no_free_ptr(pid->attr);
	struct simple_xattrs *xattrs __free(kfree) = NULL;

	/*
	 * Any dentry must've been wiped from the pid by now.
	 * Otherwise there's a reference count bug.
	 */
	VFS_WARN_ON_ONCE(pid->stashed);

	/*
	 * This if an error occurred during e.g., task creation that
	 * causes us to never go through the exit path.
	 */
	if (unlikely(!attr))
		return;

	/* This never had a pidfd created. */
	if (IS_ERR(attr))
		return;

	xattrs = no_free_ptr(attr->xattrs);
	if (xattrs)
		simple_xattrs_free(xattrs, NULL);
}

#ifdef CONFIG_PROC_FS
/**
 * pidfd_show_fdinfo - print information about a pidfd
 * @m: proc fdinfo file
 * @f: file referencing a pidfd
 *
 * Pid:
 * This function will print the pid that a given pidfd refers to in the
 * pid namespace of the procfs instance.
 * If the pid namespace of the process is not a descendant of the pid
 * namespace of the procfs instance 0 will be shown as its pid. This is
 * similar to calling getppid() on a process whose parent is outside of
 * its pid namespace.
 *
 * NSpid:
 * If pid namespaces are supported then this function will also print
 * the pid of a given pidfd refers to for all descendant pid namespaces
 * starting from the current pid namespace of the instance, i.e. the
 * Pid field and the first entry in the NSpid field will be identical.
 * If the pid namespace of the process is not a descendant of the pid
 * namespace of the procfs instance 0 will be shown as its first NSpid
 * entry and no others will be shown.
 * Note that this differs from the Pid and NSpid fields in
 * /proc/<pid>/status where Pid and NSpid are always shown relative to
 * the  pid namespace of the procfs instance. The difference becomes
 * obvious when sending around a pidfd between pid namespaces from a
 * different branch of the tree, i.e. where no ancestral relation is
 * present between the pid namespaces:
 * - create two new pid namespaces ns1 and ns2 in the initial pid
 *   namespace (also take care to create new mount namespaces in the
 *   new pid namespace and mount procfs)
 * - create a process with a pidfd in ns1
 * - send pidfd from ns1 to ns2
 * - read /proc/self/fdinfo/<pidfd> and observe that both Pid and NSpid
 *   have exactly one entry, which is 0
 */
static void pidfd_show_fdinfo(struct seq_file *m, struct file *f)
{
	struct pid *pid = pidfd_pid(f);
	struct pid_namespace *ns;
	pid_t nr = -1;

	if (likely(pid_has_task(pid, PIDTYPE_PID))) {
		ns = proc_pid_ns(file_inode(m->file)->i_sb);
		nr = pid_nr_ns(pid, ns);
	}

	seq_put_decimal_ll(m, "Pid:\t", nr);

#ifdef CONFIG_PID_NS
	seq_put_decimal_ll(m, "\nNSpid:\t", nr);
	if (nr > 0) {
		int i;

		/* If nr is non-zero it means that 'pid' is valid and that
		 * ns, i.e. the pid namespace associated with the procfs
		 * instance, is in the pid namespace hierarchy of pid.
		 * Start at one below the already printed level.
		 */
		for (i = ns->level + 1; i <= pid->level; i++)
			seq_put_decimal_ll(m, "\t", pid->numbers[i].nr);
	}
#endif
	seq_putc(m, '\n');
}
#endif

/*
 * Poll support for process exit notification.
 */
static __poll_t pidfd_poll(struct file *file, struct poll_table_struct *pts)
{
	struct pid *pid = pidfd_pid(file);
	struct task_struct *task;
	__poll_t poll_flags = 0;

	poll_wait(file, &pid->wait_pidfd, pts);
	/*
	 * Don't wake waiters if the thread-group leader exited
	 * prematurely. They either get notified when the last subthread
	 * exits or not at all if one of the remaining subthreads execs
	 * and assumes the struct pid of the old thread-group leader.
	 */
	guard(rcu)();
	task = pid_task(pid, PIDTYPE_PID);
	if (!task)
		poll_flags = EPOLLIN | EPOLLRDNORM | EPOLLHUP;
	else if (task->exit_state && !delay_group_leader(task))
		poll_flags = EPOLLIN | EPOLLRDNORM;

	return poll_flags;
}

static inline bool pid_in_current_pidns(const struct pid *pid)
{
	const struct pid_namespace *ns = task_active_pid_ns(current);

	if (ns->level <= pid->level)
		return pid->numbers[ns->level].ns == ns;

	return false;
}

static __u32 pidfs_coredump_mask(unsigned long mm_flags)
{
	switch (__get_dumpable(mm_flags)) {
	case SUID_DUMP_USER:
		return PIDFD_COREDUMP_USER;
	case SUID_DUMP_ROOT:
		return PIDFD_COREDUMP_ROOT;
	case SUID_DUMP_DISABLE:
		return PIDFD_COREDUMP_SKIP;
	default:
		WARN_ON_ONCE(true);
	}

	return 0;
}

static long pidfd_info(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct pidfd_info __user *uinfo = (struct pidfd_info __user *)arg;
	struct task_struct *task __free(put_task) = NULL;
	struct pid *pid = pidfd_pid(file);
	size_t usize = _IOC_SIZE(cmd);
	struct pidfd_info kinfo = {};
	struct pidfs_exit_info *exit_info;
	struct user_namespace *user_ns;
	struct pidfs_attr *attr;
	const struct cred *c;
	__u64 mask;

	if (!uinfo)
		return -EINVAL;
	if (usize < PIDFD_INFO_SIZE_VER0)
		return -EINVAL; /* First version, no smaller struct possible */

	if (copy_from_user(&mask, &uinfo->mask, sizeof(mask)))
		return -EFAULT;

	/*
	 * Restrict information retrieval to tasks within the caller's pid
	 * namespace hierarchy.
	 */
	if (!pid_in_current_pidns(pid))
		return -ESRCH;

	attr = READ_ONCE(pid->attr);
	if (mask & PIDFD_INFO_EXIT) {
		exit_info = READ_ONCE(attr->exit_info);
		if (exit_info) {
			kinfo.mask |= PIDFD_INFO_EXIT;
#ifdef CONFIG_CGROUPS
			kinfo.cgroupid = exit_info->cgroupid;
			kinfo.mask |= PIDFD_INFO_CGROUPID;
#endif
			kinfo.exit_code = exit_info->exit_code;
		}
	}

	if (mask & PIDFD_INFO_COREDUMP) {
		kinfo.mask |= PIDFD_INFO_COREDUMP;
		kinfo.coredump_mask = READ_ONCE(attr->__pei.coredump_mask);
	}

	task = get_pid_task(pid, PIDTYPE_PID);
	if (!task) {
		/*
		 * If the task has already been reaped, only exit
		 * information is available
		 */
		if (!(mask & PIDFD_INFO_EXIT))
			return -ESRCH;

		goto copy_out;
	}

	c = get_task_cred(task);
	if (!c)
		return -ESRCH;

	if ((kinfo.mask & PIDFD_INFO_COREDUMP) && !(kinfo.coredump_mask)) {
		task_lock(task);
		if (task->mm) {
			unsigned long flags = __mm_flags_get_dumpable(task->mm);

			kinfo.coredump_mask = pidfs_coredump_mask(flags);
		}
		task_unlock(task);
	}

	/* Unconditionally return identifiers and credentials, the rest only on request */

	user_ns = current_user_ns();
	kinfo.ruid = from_kuid_munged(user_ns, c->uid);
	kinfo.rgid = from_kgid_munged(user_ns, c->gid);
	kinfo.euid = from_kuid_munged(user_ns, c->euid);
	kinfo.egid = from_kgid_munged(user_ns, c->egid);
	kinfo.suid = from_kuid_munged(user_ns, c->suid);
	kinfo.sgid = from_kgid_munged(user_ns, c->sgid);
	kinfo.fsuid = from_kuid_munged(user_ns, c->fsuid);
	kinfo.fsgid = from_kgid_munged(user_ns, c->fsgid);
	kinfo.mask |= PIDFD_INFO_CREDS;
	put_cred(c);

#ifdef CONFIG_CGROUPS
	if (!kinfo.cgroupid) {
		struct cgroup *cgrp;

		rcu_read_lock();
		cgrp = task_dfl_cgroup(task);
		kinfo.cgroupid = cgroup_id(cgrp);
		kinfo.mask |= PIDFD_INFO_CGROUPID;
		rcu_read_unlock();
	}
#endif

	/*
	 * Copy pid/tgid last, to reduce the chances the information might be
	 * stale. Note that it is not possible to ensure it will be valid as the
	 * task might return as soon as the copy_to_user finishes, but that's ok
	 * and userspace expects that might happen and can act accordingly, so
	 * this is just best-effort. What we can do however is checking that all
	 * the fields are set correctly, or return ESRCH to avoid providing
	 * incomplete information. */

	kinfo.ppid = task_ppid_nr_ns(task, NULL);
	kinfo.tgid = task_tgid_vnr(task);
	kinfo.pid = task_pid_vnr(task);
	kinfo.mask |= PIDFD_INFO_PID;

	if (kinfo.pid == 0 || kinfo.tgid == 0)
		return -ESRCH;

copy_out:
	/*
	 * If userspace and the kernel have the same struct size it can just
	 * be copied. If userspace provides an older struct, only the bits that
	 * userspace knows about will be copied. If userspace provides a new
	 * struct, only the bits that the kernel knows about will be copied.
	 */
	return copy_struct_to_user(uinfo, usize, &kinfo, sizeof(kinfo), NULL);
}

static bool pidfs_ioctl_valid(unsigned int cmd)
{
	switch (cmd) {
	case FS_IOC_GETVERSION:
	case PIDFD_GET_CGROUP_NAMESPACE:
	case PIDFD_GET_IPC_NAMESPACE:
	case PIDFD_GET_MNT_NAMESPACE:
	case PIDFD_GET_NET_NAMESPACE:
	case PIDFD_GET_PID_FOR_CHILDREN_NAMESPACE:
	case PIDFD_GET_TIME_NAMESPACE:
	case PIDFD_GET_TIME_FOR_CHILDREN_NAMESPACE:
	case PIDFD_GET_UTS_NAMESPACE:
	case PIDFD_GET_USER_NAMESPACE:
	case PIDFD_GET_PID_NAMESPACE:
		return true;
	}

	/* Extensible ioctls require some more careful checks. */
	switch (_IOC_NR(cmd)) {
	case _IOC_NR(PIDFD_GET_INFO):
		/*
		 * Try to prevent performing a pidfd ioctl when someone
		 * erronously mistook the file descriptor for a pidfd.
		 * This is not perfect but will catch most cases.
		 */
		return extensible_ioctl_valid(cmd, PIDFD_GET_INFO, PIDFD_INFO_SIZE_VER0);
	}

	return false;
}

static long pidfd_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct task_struct *task __free(put_task) = NULL;
	struct nsproxy *nsp __free(put_nsproxy) = NULL;
	struct ns_common *ns_common = NULL;
	struct pid_namespace *pid_ns;

	if (!pidfs_ioctl_valid(cmd))
		return -ENOIOCTLCMD;

	if (cmd == FS_IOC_GETVERSION) {
		if (!arg)
			return -EINVAL;

		__u32 __user *argp = (__u32 __user *)arg;
		return put_user(file_inode(file)->i_generation, argp);
	}

	/* Extensible IOCTL that does not open namespace FDs, take a shortcut */
	if (_IOC_NR(cmd) == _IOC_NR(PIDFD_GET_INFO))
		return pidfd_info(file, cmd, arg);

	task = get_pid_task(pidfd_pid(file), PIDTYPE_PID);
	if (!task)
		return -ESRCH;

	if (arg)
		return -EINVAL;

	scoped_guard(task_lock, task) {
		nsp = task->nsproxy;
		if (nsp)
			get_nsproxy(nsp);
	}
	if (!nsp)
		return -ESRCH; /* just pretend it didn't exist */

	/*
	 * We're trying to open a file descriptor to the namespace so perform a
	 * filesystem cred ptrace check. Also, we mirror nsfs behavior.
	 */
	if (!ptrace_may_access(task, PTRACE_MODE_READ_FSCREDS))
		return -EACCES;

	switch (cmd) {
	/* Namespaces that hang of nsproxy. */
	case PIDFD_GET_CGROUP_NAMESPACE:
		if (IS_ENABLED(CONFIG_CGROUPS)) {
			get_cgroup_ns(nsp->cgroup_ns);
			ns_common = to_ns_common(nsp->cgroup_ns);
		}
		break;
	case PIDFD_GET_IPC_NAMESPACE:
		if (IS_ENABLED(CONFIG_IPC_NS)) {
			get_ipc_ns(nsp->ipc_ns);
			ns_common = to_ns_common(nsp->ipc_ns);
		}
		break;
	case PIDFD_GET_MNT_NAMESPACE:
		get_mnt_ns(nsp->mnt_ns);
		ns_common = to_ns_common(nsp->mnt_ns);
		break;
	case PIDFD_GET_NET_NAMESPACE:
		if (IS_ENABLED(CONFIG_NET_NS)) {
			ns_common = to_ns_common(nsp->net_ns);
			get_net_ns(ns_common);
		}
		break;
	case PIDFD_GET_PID_FOR_CHILDREN_NAMESPACE:
		if (IS_ENABLED(CONFIG_PID_NS)) {
			get_pid_ns(nsp->pid_ns_for_children);
			ns_common = to_ns_common(nsp->pid_ns_for_children);
		}
		break;
	case PIDFD_GET_TIME_NAMESPACE:
		if (IS_ENABLED(CONFIG_TIME_NS)) {
			get_time_ns(nsp->time_ns);
			ns_common = to_ns_common(nsp->time_ns);
		}
		break;
	case PIDFD_GET_TIME_FOR_CHILDREN_NAMESPACE:
		if (IS_ENABLED(CONFIG_TIME_NS)) {
			get_time_ns(nsp->time_ns_for_children);
			ns_common = to_ns_common(nsp->time_ns_for_children);
		}
		break;
	case PIDFD_GET_UTS_NAMESPACE:
		if (IS_ENABLED(CONFIG_UTS_NS)) {
			get_uts_ns(nsp->uts_ns);
			ns_common = to_ns_common(nsp->uts_ns);
		}
		break;
	/* Namespaces that don't hang of nsproxy. */
	case PIDFD_GET_USER_NAMESPACE:
		if (IS_ENABLED(CONFIG_USER_NS)) {
			rcu_read_lock();
			ns_common = to_ns_common(get_user_ns(task_cred_xxx(task, user_ns)));
			rcu_read_unlock();
		}
		break;
	case PIDFD_GET_PID_NAMESPACE:
		if (IS_ENABLED(CONFIG_PID_NS)) {
			rcu_read_lock();
			pid_ns = task_active_pid_ns(task);
			if (pid_ns)
				ns_common = to_ns_common(get_pid_ns(pid_ns));
			rcu_read_unlock();
		}
		break;
	default:
		return -ENOIOCTLCMD;
	}

	if (!ns_common)
		return -EOPNOTSUPP;

	/* open_namespace() unconditionally consumes the reference */
	return open_namespace(ns_common);
}

static const struct file_operations pidfs_file_operations = {
	.poll		= pidfd_poll,
#ifdef CONFIG_PROC_FS
	.show_fdinfo	= pidfd_show_fdinfo,
#endif
	.unlocked_ioctl	= pidfd_ioctl,
	.compat_ioctl   = compat_ptr_ioctl,
};

struct pid *pidfd_pid(const struct file *file)
{
	if (file->f_op != &pidfs_file_operations)
		return ERR_PTR(-EBADF);
	return file_inode(file)->i_private;
}

/*
 * We're called from release_task(). We know there's at least one
 * reference to struct pid being held that won't be released until the
 * task has been reaped which cannot happen until we're out of
 * release_task().
 *
 * If this struct pid has at least once been referred to by a pidfd then
 * pid->attr will be allocated. If not we mark the struct pid as dead so
 * anyone who is trying to register it with pidfs will fail to do so.
 * Otherwise we would hand out pidfs for reaped tasks without having
 * exit information available.
 *
 * Worst case is that we've filled in the info and the pid gets freed
 * right away in free_pid() when no one holds a pidfd anymore. Since
 * pidfs_exit() currently is placed after exit_task_work() we know that
 * it cannot be us aka the exiting task holding a pidfd to itself.
 */
void pidfs_exit(struct task_struct *tsk)
{
	struct pid *pid = task_pid(tsk);
	struct pidfs_attr *attr;
	struct pidfs_exit_info *exit_info;
#ifdef CONFIG_CGROUPS
	struct cgroup *cgrp;
#endif

	might_sleep();

	guard(spinlock_irq)(&pid->wait_pidfd.lock);
	attr = pid->attr;
	if (!attr) {
		/*
		 * No one ever held a pidfd for this struct pid.
		 * Mark it as dead so no one can add a pidfs
		 * entry anymore. We're about to be reaped and
		 * so no exit information would be available.
		 */
		pid->attr = PIDFS_PID_DEAD;
		return;
	}

	/*
	 * If @pid->attr is set someone might still legitimately hold a
	 * pidfd to @pid or someone might concurrently still be getting
	 * a reference to an already stashed dentry from @pid->stashed.
	 * So defer cleaning @pid->attr until the last reference to @pid
	 * is put
	 */

	exit_info = &attr->__pei;

#ifdef CONFIG_CGROUPS
	rcu_read_lock();
	cgrp = task_dfl_cgroup(tsk);
	exit_info->cgroupid = cgroup_id(cgrp);
	rcu_read_unlock();
#endif
	exit_info->exit_code = tsk->exit_code;

	/* Ensure that PIDFD_GET_INFO sees either all or nothing. */
	smp_store_release(&attr->exit_info, &attr->__pei);
}

#ifdef CONFIG_COREDUMP
void pidfs_coredump(const struct coredump_params *cprm)
{
	struct pid *pid = cprm->pid;
	struct pidfs_exit_info *exit_info;
	struct pidfs_attr *attr;
	__u32 coredump_mask = 0;

	attr = READ_ONCE(pid->attr);

	VFS_WARN_ON_ONCE(!attr);
	VFS_WARN_ON_ONCE(attr == PIDFS_PID_DEAD);

	exit_info = &attr->__pei;
	/* Note how we were coredumped. */
	coredump_mask = pidfs_coredump_mask(cprm->mm_flags);
	/* Note that we actually did coredump. */
	coredump_mask |= PIDFD_COREDUMPED;
	/* If coredumping is set to skip we should never end up here. */
	VFS_WARN_ON_ONCE(coredump_mask & PIDFD_COREDUMP_SKIP);
	smp_store_release(&exit_info->coredump_mask, coredump_mask);
}
#endif

static struct vfsmount *pidfs_mnt __ro_after_init;

/*
 * The vfs falls back to simple_setattr() if i_op->setattr() isn't
 * implemented. Let's reject it completely until we have a clean
 * permission concept for pidfds.
 */
static int pidfs_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
			 struct iattr *attr)
{
	return anon_inode_setattr(idmap, dentry, attr);
}

static int pidfs_getattr(struct mnt_idmap *idmap, const struct path *path,
			 struct kstat *stat, u32 request_mask,
			 unsigned int query_flags)
{
	return anon_inode_getattr(idmap, path, stat, request_mask, query_flags);
}

static ssize_t pidfs_listxattr(struct dentry *dentry, char *buf, size_t size)
{
	struct inode *inode = d_inode(dentry);
	struct pid *pid = inode->i_private;
	struct pidfs_attr *attr = pid->attr;
	struct simple_xattrs *xattrs;

	xattrs = READ_ONCE(attr->xattrs);
	if (!xattrs)
		return 0;

	return simple_xattr_list(inode, xattrs, buf, size);
}

static const struct inode_operations pidfs_inode_operations = {
	.getattr	= pidfs_getattr,
	.setattr	= pidfs_setattr,
	.listxattr	= pidfs_listxattr,
};

static void pidfs_evict_inode(struct inode *inode)
{
	struct pid *pid = inode->i_private;

	clear_inode(inode);
	put_pid(pid);
}

static const struct super_operations pidfs_sops = {
	.drop_inode	= inode_just_drop,
	.evict_inode	= pidfs_evict_inode,
	.statfs		= simple_statfs,
};

/*
 * 'lsof' has knowledge of out historical anon_inode use, and expects
 * the pidfs dentry name to start with 'anon_inode'.
 */
static char *pidfs_dname(struct dentry *dentry, char *buffer, int buflen)
{
	return dynamic_dname(buffer, buflen, "anon_inode:[pidfd]");
}

const struct dentry_operations pidfs_dentry_operations = {
	.d_dname	= pidfs_dname,
	.d_prune	= stashed_dentry_prune,
};

static int pidfs_encode_fh(struct inode *inode, u32 *fh, int *max_len,
			   struct inode *parent)
{
	const struct pid *pid = inode->i_private;

	if (*max_len < 2) {
		*max_len = 2;
		return FILEID_INVALID;
	}

	*max_len = 2;
	*(u64 *)fh = pid->ino;
	return FILEID_KERNFS;
}

static int pidfs_ino_find(const void *key, const struct rb_node *node)
{
	const u64 pid_ino = *(u64 *)key;
	const struct pid *pid = rb_entry(node, struct pid, pidfs_node);

	if (pid_ino < pid->ino)
		return -1;
	if (pid_ino > pid->ino)
		return 1;
	return 0;
}

/* Find a struct pid based on the inode number. */
static struct pid *pidfs_ino_get_pid(u64 ino)
{
	struct pid *pid;
	struct rb_node *node;
	unsigned int seq;

	guard(rcu)();
	do {
		seq = read_seqcount_begin(&pidmap_lock_seq);
		node = rb_find_rcu(&ino, &pidfs_ino_tree, pidfs_ino_find);
		if (node)
			break;
	} while (read_seqcount_retry(&pidmap_lock_seq, seq));

	if (!node)
		return NULL;

	pid = rb_entry(node, struct pid, pidfs_node);

	/* Within our pid namespace hierarchy? */
	if (pid_vnr(pid) == 0)
		return NULL;

	return get_pid(pid);
}

static struct dentry *pidfs_fh_to_dentry(struct super_block *sb,
					 struct fid *fid, int fh_len,
					 int fh_type)
{
	int ret;
	u64 pid_ino;
	struct path path;
	struct pid *pid;

	if (fh_len < 2)
		return NULL;

	switch (fh_type) {
	case FILEID_KERNFS:
		pid_ino = *(u64 *)fid;
		break;
	default:
		return NULL;
	}

	pid = pidfs_ino_get_pid(pid_ino);
	if (!pid)
		return NULL;

	ret = path_from_stashed(&pid->stashed, pidfs_mnt, pid, &path);
	if (ret < 0)
		return ERR_PTR(ret);

	VFS_WARN_ON_ONCE(!pid->attr);

	mntput(path.mnt);
	return path.dentry;
}

/*
 * Make sure that we reject any nonsensical flags that users pass via
 * open_by_handle_at(). Note that PIDFD_THREAD is defined as O_EXCL, and
 * PIDFD_NONBLOCK as O_NONBLOCK.
 */
#define VALID_FILE_HANDLE_OPEN_FLAGS \
	(O_RDONLY | O_WRONLY | O_RDWR | O_NONBLOCK | O_CLOEXEC | O_EXCL)

static int pidfs_export_permission(struct handle_to_path_ctx *ctx,
				   unsigned int oflags)
{
	if (oflags & ~(VALID_FILE_HANDLE_OPEN_FLAGS | O_LARGEFILE))
		return -EINVAL;

	/*
	 * pidfd_ino_get_pid() will verify that the struct pid is part
	 * of the caller's pid namespace hierarchy. No further
	 * permission checks are needed.
	 */
	return 0;
}

static struct file *pidfs_export_open(const struct path *path, unsigned int oflags)
{
	/*
	 * Clear O_LARGEFILE as open_by_handle_at() forces it and raise
	 * O_RDWR as pidfds always are.
	 */
	oflags &= ~O_LARGEFILE;
	return dentry_open(path, oflags | O_RDWR, current_cred());
}

static const struct export_operations pidfs_export_operations = {
	.encode_fh	= pidfs_encode_fh,
	.fh_to_dentry	= pidfs_fh_to_dentry,
	.open		= pidfs_export_open,
	.permission	= pidfs_export_permission,
};

static int pidfs_init_inode(struct inode *inode, void *data)
{
	const struct pid *pid = data;

	inode->i_private = data;
	inode->i_flags |= S_PRIVATE | S_ANON_INODE;
	/* We allow to set xattrs. */
	inode->i_flags &= ~S_IMMUTABLE;
	inode->i_mode |= S_IRWXU;
	inode->i_op = &pidfs_inode_operations;
	inode->i_fop = &pidfs_file_operations;
	inode->i_ino = pidfs_ino(pid->ino);
	inode->i_generation = pidfs_gen(pid->ino);
	return 0;
}

static void pidfs_put_data(void *data)
{
	struct pid *pid = data;
	put_pid(pid);
}

/**
 * pidfs_register_pid - register a struct pid in pidfs
 * @pid: pid to pin
 *
 * Register a struct pid in pidfs.
 *
 * Return: On success zero, on error a negative error code is returned.
 */
int pidfs_register_pid(struct pid *pid)
{
	struct pidfs_attr *new_attr __free(kfree) = NULL;
	struct pidfs_attr *attr;

	might_sleep();

	if (!pid)
		return 0;

	attr = READ_ONCE(pid->attr);
	if (unlikely(attr == PIDFS_PID_DEAD))
		return PTR_ERR(PIDFS_PID_DEAD);
	if (attr)
		return 0;

	new_attr = kmem_cache_zalloc(pidfs_attr_cachep, GFP_KERNEL);
	if (!new_attr)
		return -ENOMEM;

	/* Synchronize with pidfs_exit(). */
	guard(spinlock_irq)(&pid->wait_pidfd.lock);

	attr = pid->attr;
	if (unlikely(attr == PIDFS_PID_DEAD))
		return PTR_ERR(PIDFS_PID_DEAD);
	if (unlikely(attr))
		return 0;

	pid->attr = no_free_ptr(new_attr);
	return 0;
}

static struct dentry *pidfs_stash_dentry(struct dentry **stashed,
					 struct dentry *dentry)
{
	int ret;
	struct pid *pid = d_inode(dentry)->i_private;

	VFS_WARN_ON_ONCE(stashed != &pid->stashed);

	ret = pidfs_register_pid(pid);
	if (ret)
		return ERR_PTR(ret);

	return stash_dentry(stashed, dentry);
}

static const struct stashed_operations pidfs_stashed_ops = {
	.stash_dentry	= pidfs_stash_dentry,
	.init_inode	= pidfs_init_inode,
	.put_data	= pidfs_put_data,
};

static int pidfs_xattr_get(const struct xattr_handler *handler,
			   struct dentry *unused, struct inode *inode,
			   const char *suffix, void *value, size_t size)
{
	struct pid *pid = inode->i_private;
	struct pidfs_attr *attr = pid->attr;
	const char *name;
	struct simple_xattrs *xattrs;

	xattrs = READ_ONCE(attr->xattrs);
	if (!xattrs)
		return 0;

	name = xattr_full_name(handler, suffix);
	return simple_xattr_get(xattrs, name, value, size);
}

static int pidfs_xattr_set(const struct xattr_handler *handler,
			   struct mnt_idmap *idmap, struct dentry *unused,
			   struct inode *inode, const char *suffix,
			   const void *value, size_t size, int flags)
{
	struct pid *pid = inode->i_private;
	struct pidfs_attr *attr = pid->attr;
	const char *name;
	struct simple_xattrs *xattrs;
	struct simple_xattr *old_xattr;

	/* Ensure we're the only one to set @attr->xattrs. */
	WARN_ON_ONCE(!inode_is_locked(inode));

	xattrs = READ_ONCE(attr->xattrs);
	if (!xattrs) {
		xattrs = kmem_cache_zalloc(pidfs_xattr_cachep, GFP_KERNEL);
		if (!xattrs)
			return -ENOMEM;

		simple_xattrs_init(xattrs);
		smp_store_release(&pid->attr->xattrs, xattrs);
	}

	name = xattr_full_name(handler, suffix);
	old_xattr = simple_xattr_set(xattrs, name, value, size, flags);
	if (IS_ERR(old_xattr))
		return PTR_ERR(old_xattr);

	simple_xattr_free(old_xattr);
	return 0;
}

static const struct xattr_handler pidfs_trusted_xattr_handler = {
	.prefix = XATTR_TRUSTED_PREFIX,
	.get	= pidfs_xattr_get,
	.set	= pidfs_xattr_set,
};

static const struct xattr_handler *const pidfs_xattr_handlers[] = {
	&pidfs_trusted_xattr_handler,
	NULL
};

static int pidfs_init_fs_context(struct fs_context *fc)
{
	struct pseudo_fs_context *ctx;

	ctx = init_pseudo(fc, PID_FS_MAGIC);
	if (!ctx)
		return -ENOMEM;

	fc->s_iflags |= SB_I_NOEXEC;
	fc->s_iflags |= SB_I_NODEV;
	ctx->ops = &pidfs_sops;
	ctx->eops = &pidfs_export_operations;
	ctx->dops = &pidfs_dentry_operations;
	ctx->xattr = pidfs_xattr_handlers;
	fc->s_fs_info = (void *)&pidfs_stashed_ops;
	return 0;
}

static struct file_system_type pidfs_type = {
	.name			= "pidfs",
	.init_fs_context	= pidfs_init_fs_context,
	.kill_sb		= kill_anon_super,
};

struct file *pidfs_alloc_file(struct pid *pid, unsigned int flags)
{
	struct file *pidfd_file;
	struct path path __free(path_put) = {};
	int ret;

	/*
	 * Ensure that PIDFD_STALE can be passed as a flag without
	 * overloading other uapi pidfd flags.
	 */
	BUILD_BUG_ON(PIDFD_STALE == PIDFD_THREAD);
	BUILD_BUG_ON(PIDFD_STALE == PIDFD_NONBLOCK);

	ret = path_from_stashed(&pid->stashed, pidfs_mnt, get_pid(pid), &path);
	if (ret < 0)
		return ERR_PTR(ret);

	VFS_WARN_ON_ONCE(!pid->attr);

	flags &= ~PIDFD_STALE;
	flags |= O_RDWR;
	pidfd_file = dentry_open(&path, flags, current_cred());
	/* Raise PIDFD_THREAD explicitly as do_dentry_open() strips it. */
	if (!IS_ERR(pidfd_file))
		pidfd_file->f_flags |= (flags & PIDFD_THREAD);

	return pidfd_file;
}

void __init pidfs_init(void)
{
	pidfs_attr_cachep = kmem_cache_create("pidfs_attr_cache", sizeof(struct pidfs_attr), 0,
					 (SLAB_HWCACHE_ALIGN | SLAB_RECLAIM_ACCOUNT |
					  SLAB_ACCOUNT | SLAB_PANIC), NULL);

	pidfs_xattr_cachep = kmem_cache_create("pidfs_xattr_cache",
					       sizeof(struct simple_xattrs), 0,
					       (SLAB_HWCACHE_ALIGN | SLAB_RECLAIM_ACCOUNT |
						SLAB_ACCOUNT | SLAB_PANIC), NULL);

	pidfs_mnt = kern_mount(&pidfs_type);
	if (IS_ERR(pidfs_mnt))
		panic("Failed to mount pidfs pseudo filesystem");

	pidfs_root_path.mnt = pidfs_mnt;
	pidfs_root_path.dentry = pidfs_mnt->mnt_root;
}
