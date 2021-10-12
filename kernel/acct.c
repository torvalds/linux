// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/kernel/acct.c
 *
 *  BSD Process Accounting for Linux
 *
 *  Author: Marco van Wieringen <mvw@planets.elm.net>
 *
 *  Some code based on ideas and code from:
 *  Thomas K. Dyas <tdyas@eden.rutgers.edu>
 *
 *  This file implements BSD-style process accounting. Whenever any
 *  process exits, an accounting record of type "struct acct" is
 *  written to the file specified with the acct() system call. It is
 *  up to user-level programs to do useful things with the accounting
 *  log. The kernel just provides the raw accounting information.
 *
 * (C) Copyright 1995 - 1997 Marco van Wieringen - ELM Consultancy B.V.
 *
 *  Plugged two leaks. 1) It didn't return acct_file into the free_filps if
 *  the file happened to be read-only. 2) If the accounting was suspended
 *  due to the lack of space it happily allowed to reopen it and completely
 *  lost the old acct_file. 3/10/98, Al Viro.
 *
 *  Now we silently close acct_file on attempt to reopen. Cleaned sys_acct().
 *  XTerms and EMACS are manifestations of pure evil. 21/10/98, AV.
 *
 *  Fixed a nasty interaction with sys_umount(). If the accounting
 *  was suspeneded we failed to stop it on umount(). Messy.
 *  Another one: remount to readonly didn't stop accounting.
 *	Question: what should we do if we have CAP_SYS_ADMIN but not
 *  CAP_SYS_PACCT? Current code does the following: umount returns -EBUSY
 *  unless we are messing with the root. In that case we are getting a
 *  real mess with do_remount_sb(). 9/11/98, AV.
 *
 *  Fixed a bunch of races (and pair of leaks). Probably not the best way,
 *  but this one obviously doesn't introduce deadlocks. Later. BTW, found
 *  one race (and leak) in BSD implementation.
 *  OK, that's better. ANOTHER race and leak in BSD variant. There always
 *  is one more bug... 10/11/98, AV.
 *
 *	Oh, fsck... Oopsable SMP race in do_process_acct() - we must hold
 * ->mmap_lock to walk the vma list of current->mm. Nasty, since it leaks
 * a struct file opened for write. Fixed. 2/6/2000, AV.
 */

#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/acct.h>
#include <linux/capability.h>
#include <linux/file.h>
#include <linux/tty.h>
#include <linux/security.h>
#include <linux/vfs.h>
#include <linux/jiffies.h>
#include <linux/times.h>
#include <linux/syscalls.h>
#include <linux/mount.h>
#include <linux/uaccess.h>
#include <linux/sched/cputime.h>

#include <asm/div64.h>
#include <linux/blkdev.h> /* sector_div */
#include <linux/pid_namespace.h>
#include <linux/fs_pin.h>

/*
 * These constants control the amount of freespace that suspend and
 * resume the process accounting system, and the time delay between
 * each check.
 * Turned into sysctl-controllable parameters. AV, 12/11/98
 */

int acct_parm[3] = {4, 2, 30};
#define RESUME		(acct_parm[0])	/* >foo% free space - resume */
#define SUSPEND		(acct_parm[1])	/* <foo% free space - suspend */
#define ACCT_TIMEOUT	(acct_parm[2])	/* foo second timeout between checks */

/*
 * External references and all of the globals.
 */

struct bsd_acct_struct {
	struct fs_pin		pin;
	atomic_long_t		count;
	struct rcu_head		rcu;
	struct mutex		lock;
	int			active;
	unsigned long		needcheck;
	struct file		*file;
	struct pid_namespace	*ns;
	struct work_struct	work;
	struct completion	done;
};

static void do_acct_process(struct bsd_acct_struct *acct);

/*
 * Check the amount of free space and suspend/resume accordingly.
 */
static int check_free_space(struct bsd_acct_struct *acct)
{
	struct kstatfs sbuf;

	if (time_is_after_jiffies(acct->needcheck))
		goto out;

	/* May block */
	if (vfs_statfs(&acct->file->f_path, &sbuf))
		goto out;

	if (acct->active) {
		u64 suspend = sbuf.f_blocks * SUSPEND;
		do_div(suspend, 100);
		if (sbuf.f_bavail <= suspend) {
			acct->active = 0;
			pr_info("Process accounting paused\n");
		}
	} else {
		u64 resume = sbuf.f_blocks * RESUME;
		do_div(resume, 100);
		if (sbuf.f_bavail >= resume) {
			acct->active = 1;
			pr_info("Process accounting resumed\n");
		}
	}

	acct->needcheck = jiffies + ACCT_TIMEOUT*HZ;
out:
	return acct->active;
}

static void acct_put(struct bsd_acct_struct *p)
{
	if (atomic_long_dec_and_test(&p->count))
		kfree_rcu(p, rcu);
}

static inline struct bsd_acct_struct *to_acct(struct fs_pin *p)
{
	return p ? container_of(p, struct bsd_acct_struct, pin) : NULL;
}

static struct bsd_acct_struct *acct_get(struct pid_namespace *ns)
{
	struct bsd_acct_struct *res;
again:
	smp_rmb();
	rcu_read_lock();
	res = to_acct(READ_ONCE(ns->bacct));
	if (!res) {
		rcu_read_unlock();
		return NULL;
	}
	if (!atomic_long_inc_not_zero(&res->count)) {
		rcu_read_unlock();
		cpu_relax();
		goto again;
	}
	rcu_read_unlock();
	mutex_lock(&res->lock);
	if (res != to_acct(READ_ONCE(ns->bacct))) {
		mutex_unlock(&res->lock);
		acct_put(res);
		goto again;
	}
	return res;
}

static void acct_pin_kill(struct fs_pin *pin)
{
	struct bsd_acct_struct *acct = to_acct(pin);
	mutex_lock(&acct->lock);
	do_acct_process(acct);
	schedule_work(&acct->work);
	wait_for_completion(&acct->done);
	cmpxchg(&acct->ns->bacct, pin, NULL);
	mutex_unlock(&acct->lock);
	pin_remove(pin);
	acct_put(acct);
}

static void close_work(struct work_struct *work)
{
	struct bsd_acct_struct *acct = container_of(work, struct bsd_acct_struct, work);
	struct file *file = acct->file;
	if (file->f_op->flush)
		file->f_op->flush(file, NULL);
	__fput_sync(file);
	complete(&acct->done);
}

static int acct_on(struct filename *pathname)
{
	struct file *file;
	struct vfsmount *mnt, *internal;
	struct pid_namespace *ns = task_active_pid_ns(current);
	struct bsd_acct_struct *acct;
	struct fs_pin *old;
	int err;

	acct = kzalloc(sizeof(struct bsd_acct_struct), GFP_KERNEL);
	if (!acct)
		return -ENOMEM;

	/* Difference from BSD - they don't do O_APPEND */
	file = file_open_name(pathname, O_WRONLY|O_APPEND|O_LARGEFILE, 0);
	if (IS_ERR(file)) {
		kfree(acct);
		return PTR_ERR(file);
	}

	if (!S_ISREG(file_inode(file)->i_mode)) {
		kfree(acct);
		filp_close(file, NULL);
		return -EACCES;
	}

	if (!(file->f_mode & FMODE_CAN_WRITE)) {
		kfree(acct);
		filp_close(file, NULL);
		return -EIO;
	}
	internal = mnt_clone_internal(&file->f_path);
	if (IS_ERR(internal)) {
		kfree(acct);
		filp_close(file, NULL);
		return PTR_ERR(internal);
	}
	err = __mnt_want_write(internal);
	if (err) {
		mntput(internal);
		kfree(acct);
		filp_close(file, NULL);
		return err;
	}
	mnt = file->f_path.mnt;
	file->f_path.mnt = internal;

	atomic_long_set(&acct->count, 1);
	init_fs_pin(&acct->pin, acct_pin_kill);
	acct->file = file;
	acct->needcheck = jiffies;
	acct->ns = ns;
	mutex_init(&acct->lock);
	INIT_WORK(&acct->work, close_work);
	init_completion(&acct->done);
	mutex_lock_nested(&acct->lock, 1);	/* nobody has seen it yet */
	pin_insert(&acct->pin, mnt);

	rcu_read_lock();
	old = xchg(&ns->bacct, &acct->pin);
	mutex_unlock(&acct->lock);
	pin_kill(old);
	__mnt_drop_write(mnt);
	mntput(mnt);
	return 0;
}

static DEFINE_MUTEX(acct_on_mutex);

/**
 * sys_acct - enable/disable process accounting
 * @name: file name for accounting records or NULL to shutdown accounting
 *
 * sys_acct() is the only system call needed to implement process
 * accounting. It takes the name of the file where accounting records
 * should be written. If the filename is NULL, accounting will be
 * shutdown.
 *
 * Returns: 0 for success or negative errno values for failure.
 */
SYSCALL_DEFINE1(acct, const char __user *, name)
{
	int error = 0;

	if (!capable(CAP_SYS_PACCT))
		return -EPERM;

	if (name) {
		struct filename *tmp = getname(name);

		if (IS_ERR(tmp))
			return PTR_ERR(tmp);
		mutex_lock(&acct_on_mutex);
		error = acct_on(tmp);
		mutex_unlock(&acct_on_mutex);
		putname(tmp);
	} else {
		rcu_read_lock();
		pin_kill(task_active_pid_ns(current)->bacct);
	}

	return error;
}

void acct_exit_ns(struct pid_namespace *ns)
{
	rcu_read_lock();
	pin_kill(ns->bacct);
}

/*
 *  encode an unsigned long into a comp_t
 *
 *  This routine has been adopted from the encode_comp_t() function in
 *  the kern_acct.c file of the FreeBSD operating system. The encoding
 *  is a 13-bit fraction with a 3-bit (base 8) exponent.
 */

#define	MANTSIZE	13			/* 13 bit mantissa. */
#define	EXPSIZE		3			/* Base 8 (3 bit) exponent. */
#define	MAXFRACT	((1 << MANTSIZE) - 1)	/* Maximum fractional value. */

static comp_t encode_comp_t(unsigned long value)
{
	int exp, rnd;

	exp = rnd = 0;
	while (value > MAXFRACT) {
		rnd = value & (1 << (EXPSIZE - 1));	/* Round up? */
		value >>= EXPSIZE;	/* Base 8 exponent == 3 bit shift. */
		exp++;
	}

	/*
	 * If we need to round up, do it (and handle overflow correctly).
	 */
	if (rnd && (++value > MAXFRACT)) {
		value >>= EXPSIZE;
		exp++;
	}

	/*
	 * Clean it up and polish it off.
	 */
	exp <<= MANTSIZE;		/* Shift the exponent into place */
	exp += value;			/* and add on the mantissa. */
	return exp;
}

#if ACCT_VERSION == 1 || ACCT_VERSION == 2
/*
 * encode an u64 into a comp2_t (24 bits)
 *
 * Format: 5 bit base 2 exponent, 20 bits mantissa.
 * The leading bit of the mantissa is not stored, but implied for
 * non-zero exponents.
 * Largest encodable value is 50 bits.
 */

#define MANTSIZE2       20                      /* 20 bit mantissa. */
#define EXPSIZE2        5                       /* 5 bit base 2 exponent. */
#define MAXFRACT2       ((1ul << MANTSIZE2) - 1) /* Maximum fractional value. */
#define MAXEXP2         ((1 << EXPSIZE2) - 1)    /* Maximum exponent. */

static comp2_t encode_comp2_t(u64 value)
{
	int exp, rnd;

	exp = (value > (MAXFRACT2>>1));
	rnd = 0;
	while (value > MAXFRACT2) {
		rnd = value & 1;
		value >>= 1;
		exp++;
	}

	/*
	 * If we need to round up, do it (and handle overflow correctly).
	 */
	if (rnd && (++value > MAXFRACT2)) {
		value >>= 1;
		exp++;
	}

	if (exp > MAXEXP2) {
		/* Overflow. Return largest representable number instead. */
		return (1ul << (MANTSIZE2+EXPSIZE2-1)) - 1;
	} else {
		return (value & (MAXFRACT2>>1)) | (exp << (MANTSIZE2-1));
	}
}
#elif ACCT_VERSION == 3
/*
 * encode an u64 into a 32 bit IEEE float
 */
static u32 encode_float(u64 value)
{
	unsigned exp = 190;
	unsigned u;

	if (value == 0)
		return 0;
	while ((s64)value > 0) {
		value <<= 1;
		exp--;
	}
	u = (u32)(value >> 40) & 0x7fffffu;
	return u | (exp << 23);
}
#endif

/*
 *  Write an accounting entry for an exiting process
 *
 *  The acct_process() call is the workhorse of the process
 *  accounting system. The struct acct is built here and then written
 *  into the accounting file. This function should only be called from
 *  do_exit() or when switching to a different output file.
 */

static void fill_ac(acct_t *ac)
{
	struct pacct_struct *pacct = &current->signal->pacct;
	u64 elapsed, run_time;
	time64_t btime;
	struct tty_struct *tty;

	/*
	 * Fill the accounting struct with the needed info as recorded
	 * by the different kernel functions.
	 */
	memset(ac, 0, sizeof(acct_t));

	ac->ac_version = ACCT_VERSION | ACCT_BYTEORDER;
	strlcpy(ac->ac_comm, current->comm, sizeof(ac->ac_comm));

	/* calculate run_time in nsec*/
	run_time = ktime_get_ns();
	run_time -= current->group_leader->start_time;
	/* convert nsec -> AHZ */
	elapsed = nsec_to_AHZ(run_time);
#if ACCT_VERSION == 3
	ac->ac_etime = encode_float(elapsed);
#else
	ac->ac_etime = encode_comp_t(elapsed < (unsigned long) -1l ?
				(unsigned long) elapsed : (unsigned long) -1l);
#endif
#if ACCT_VERSION == 1 || ACCT_VERSION == 2
	{
		/* new enlarged etime field */
		comp2_t etime = encode_comp2_t(elapsed);

		ac->ac_etime_hi = etime >> 16;
		ac->ac_etime_lo = (u16) etime;
	}
#endif
	do_div(elapsed, AHZ);
	btime = ktime_get_real_seconds() - elapsed;
	ac->ac_btime = clamp_t(time64_t, btime, 0, U32_MAX);
#if ACCT_VERSION==2
	ac->ac_ahz = AHZ;
#endif

	spin_lock_irq(&current->sighand->siglock);
	tty = current->signal->tty;	/* Safe as we hold the siglock */
	ac->ac_tty = tty ? old_encode_dev(tty_devnum(tty)) : 0;
	ac->ac_utime = encode_comp_t(nsec_to_AHZ(pacct->ac_utime));
	ac->ac_stime = encode_comp_t(nsec_to_AHZ(pacct->ac_stime));
	ac->ac_flag = pacct->ac_flag;
	ac->ac_mem = encode_comp_t(pacct->ac_mem);
	ac->ac_minflt = encode_comp_t(pacct->ac_minflt);
	ac->ac_majflt = encode_comp_t(pacct->ac_majflt);
	ac->ac_exitcode = pacct->ac_exitcode;
	spin_unlock_irq(&current->sighand->siglock);
}
/*
 *  do_acct_process does all actual work. Caller holds the reference to file.
 */
static void do_acct_process(struct bsd_acct_struct *acct)
{
	acct_t ac;
	unsigned long flim;
	const struct cred *orig_cred;
	struct file *file = acct->file;

	/*
	 * Accounting records are not subject to resource limits.
	 */
	flim = rlimit(RLIMIT_FSIZE);
	current->signal->rlim[RLIMIT_FSIZE].rlim_cur = RLIM_INFINITY;
	/* Perform file operations on behalf of whoever enabled accounting */
	orig_cred = override_creds(file->f_cred);

	/*
	 * First check to see if there is enough free_space to continue
	 * the process accounting system.
	 */
	if (!check_free_space(acct))
		goto out;

	fill_ac(&ac);
	/* we really need to bite the bullet and change layout */
	ac.ac_uid = from_kuid_munged(file->f_cred->user_ns, orig_cred->uid);
	ac.ac_gid = from_kgid_munged(file->f_cred->user_ns, orig_cred->gid);
#if ACCT_VERSION == 1 || ACCT_VERSION == 2
	/* backward-compatible 16 bit fields */
	ac.ac_uid16 = ac.ac_uid;
	ac.ac_gid16 = ac.ac_gid;
#elif ACCT_VERSION == 3
	{
		struct pid_namespace *ns = acct->ns;

		ac.ac_pid = task_tgid_nr_ns(current, ns);
		rcu_read_lock();
		ac.ac_ppid = task_tgid_nr_ns(rcu_dereference(current->real_parent),
					     ns);
		rcu_read_unlock();
	}
#endif
	/*
	 * Get freeze protection. If the fs is frozen, just skip the write
	 * as we could deadlock the system otherwise.
	 */
	if (file_start_write_trylock(file)) {
		/* it's been opened O_APPEND, so position is irrelevant */
		loff_t pos = 0;
		__kernel_write(file, &ac, sizeof(acct_t), &pos);
		file_end_write(file);
	}
out:
	current->signal->rlim[RLIMIT_FSIZE].rlim_cur = flim;
	revert_creds(orig_cred);
}

/**
 * acct_collect - collect accounting information into pacct_struct
 * @exitcode: task exit code
 * @group_dead: not 0, if this thread is the last one in the process.
 */
void acct_collect(long exitcode, int group_dead)
{
	struct pacct_struct *pacct = &current->signal->pacct;
	u64 utime, stime;
	unsigned long vsize = 0;

	if (group_dead && current->mm) {
		struct vm_area_struct *vma;

		mmap_read_lock(current->mm);
		vma = current->mm->mmap;
		while (vma) {
			vsize += vma->vm_end - vma->vm_start;
			vma = vma->vm_next;
		}
		mmap_read_unlock(current->mm);
	}

	spin_lock_irq(&current->sighand->siglock);
	if (group_dead)
		pacct->ac_mem = vsize / 1024;
	if (thread_group_leader(current)) {
		pacct->ac_exitcode = exitcode;
		if (current->flags & PF_FORKNOEXEC)
			pacct->ac_flag |= AFORK;
	}
	if (current->flags & PF_SUPERPRIV)
		pacct->ac_flag |= ASU;
	if (current->flags & PF_DUMPCORE)
		pacct->ac_flag |= ACORE;
	if (current->flags & PF_SIGNALED)
		pacct->ac_flag |= AXSIG;

	task_cputime(current, &utime, &stime);
	pacct->ac_utime += utime;
	pacct->ac_stime += stime;
	pacct->ac_minflt += current->min_flt;
	pacct->ac_majflt += current->maj_flt;
	spin_unlock_irq(&current->sighand->siglock);
}

static void slow_acct_process(struct pid_namespace *ns)
{
	for ( ; ns; ns = ns->parent) {
		struct bsd_acct_struct *acct = acct_get(ns);
		if (acct) {
			do_acct_process(acct);
			mutex_unlock(&acct->lock);
			acct_put(acct);
		}
	}
}

/**
 * acct_process - handles process accounting for an exiting task
 */
void acct_process(void)
{
	struct pid_namespace *ns;

	/*
	 * This loop is safe lockless, since current is still
	 * alive and holds its namespace, which in turn holds
	 * its parent.
	 */
	for (ns = task_active_pid_ns(current); ns != NULL; ns = ns->parent) {
		if (ns->bacct)
			break;
	}
	if (unlikely(ns))
		slow_acct_process(ns);
}
