#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/fdtable.h>
#include <linux/string.h>
#include <linux/random.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/cache.h>
#include <linux/bug.h>
#include <linux/err.h>
#include <linux/kcmp.h>

#include <asm/unistd.h>

/*
 * We don't expose the real in-memory order of objects for security reasons.
 * But still the comparison results should be suitable for sorting. So we
 * obfuscate kernel pointers values and compare the production instead.
 *
 * The obfuscation is done in two steps. First we xor the kernel pointer with
 * a random value, which puts pointer into a new position in a reordered space.
 * Secondly we multiply the xor production with a large odd random number to
 * permute its bits even more (the odd multiplier guarantees that the product
 * is unique ever after the high bits are truncated, since any odd number is
 * relative prime to 2^n).
 *
 * Note also that the obfuscation itself is invisible to userspace and if needed
 * it can be changed to an alternate scheme.
 */
static unsigned long cookies[KCMP_TYPES][2] __read_mostly;

static long kptr_obfuscate(long v, int type)
{
	return (v ^ cookies[type][0]) * cookies[type][1];
}

/*
 * 0 - equal, i.e. v1 = v2
 * 1 - less than, i.e. v1 < v2
 * 2 - greater than, i.e. v1 > v2
 * 3 - not equal but ordering unavailable (reserved for future)
 */
static int kcmp_ptr(void *v1, void *v2, enum kcmp_type type)
{
	long ret;

	ret = kptr_obfuscate((long)v1, type) - kptr_obfuscate((long)v2, type);

	return (ret < 0) | ((ret > 0) << 1);
}

/* The caller must have pinned the task */
static struct file *
get_file_raw_ptr(struct task_struct *task, unsigned int idx)
{
	struct file *file = NULL;

	task_lock(task);
	rcu_read_lock();

	if (task->files)
		file = fcheck_files(task->files, idx);

	rcu_read_unlock();
	task_unlock(task);

	return file;
}

static void kcmp_unlock(struct mutex *m1, struct mutex *m2)
{
	if (likely(m2 != m1))
		mutex_unlock(m2);
	mutex_unlock(m1);
}

static int kcmp_lock(struct mutex *m1, struct mutex *m2)
{
	int err;

	if (m2 > m1)
		swap(m1, m2);

	err = mutex_lock_killable(m1);
	if (!err && likely(m1 != m2)) {
		err = mutex_lock_killable_nested(m2, SINGLE_DEPTH_NESTING);
		if (err)
			mutex_unlock(m1);
	}

	return err;
}

SYSCALL_DEFINE5(kcmp, pid_t, pid1, pid_t, pid2, int, type,
		unsigned long, idx1, unsigned long, idx2)
{
	struct task_struct *task1, *task2;
	int ret;

	rcu_read_lock();

	/*
	 * Tasks are looked up in caller's PID namespace only.
	 */
	task1 = find_task_by_vpid(pid1);
	task2 = find_task_by_vpid(pid2);
	if (!task1 || !task2)
		goto err_no_task;

	get_task_struct(task1);
	get_task_struct(task2);

	rcu_read_unlock();

	/*
	 * One should have enough rights to inspect task details.
	 */
	ret = kcmp_lock(&task1->signal->cred_guard_mutex,
			&task2->signal->cred_guard_mutex);
	if (ret)
		goto err;
	if (!ptrace_may_access(task1, PTRACE_MODE_READ) ||
	    !ptrace_may_access(task2, PTRACE_MODE_READ)) {
		ret = -EPERM;
		goto err_unlock;
	}

	switch (type) {
	case KCMP_FILE: {
		struct file *filp1, *filp2;

		filp1 = get_file_raw_ptr(task1, idx1);
		filp2 = get_file_raw_ptr(task2, idx2);

		if (filp1 && filp2)
			ret = kcmp_ptr(filp1, filp2, KCMP_FILE);
		else
			ret = -EBADF;
		break;
	}
	case KCMP_VM:
		ret = kcmp_ptr(task1->mm, task2->mm, KCMP_VM);
		break;
	case KCMP_FILES:
		ret = kcmp_ptr(task1->files, task2->files, KCMP_FILES);
		break;
	case KCMP_FS:
		ret = kcmp_ptr(task1->fs, task2->fs, KCMP_FS);
		break;
	case KCMP_SIGHAND:
		ret = kcmp_ptr(task1->sighand, task2->sighand, KCMP_SIGHAND);
		break;
	case KCMP_IO:
		ret = kcmp_ptr(task1->io_context, task2->io_context, KCMP_IO);
		break;
	case KCMP_SYSVSEM:
#ifdef CONFIG_SYSVIPC
		ret = kcmp_ptr(task1->sysvsem.undo_list,
			       task2->sysvsem.undo_list,
			       KCMP_SYSVSEM);
#else
		ret = -EOPNOTSUPP;
#endif
		break;
	default:
		ret = -EINVAL;
		break;
	}

err_unlock:
	kcmp_unlock(&task1->signal->cred_guard_mutex,
		    &task2->signal->cred_guard_mutex);
err:
	put_task_struct(task1);
	put_task_struct(task2);

	return ret;

err_no_task:
	rcu_read_unlock();
	return -ESRCH;
}

static __init int kcmp_cookies_init(void)
{
	int i;

	get_random_bytes(cookies, sizeof(cookies));

	for (i = 0; i < KCMP_TYPES; i++)
		cookies[i][1] |= (~(~0UL >>  1) | 1);

	return 0;
}
arch_initcall(kcmp_cookies_init);
