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
 *  Fixed a nasty interaction with with sys_umount(). If the accointing
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
 * ->mmap_sem to walk the vma list of current->mm. Nasty, since it leaks
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
#include <asm/uaccess.h>
#include <asm/div64.h>
#include <linux/blkdev.h> /* sector_div */
#include <linux/pid_namespace.h>

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
static void do_acct_process(struct bsd_acct_struct *acct,
		struct pid_namespace *ns, struct file *);

/*
 * This structure is used so that all the data protected by lock
 * can be placed in the same cache line as the lock.  This primes
 * the cache line to have the data after getting the lock.
 */
struct bsd_acct_struct {
	volatile int		active;
	volatile int		needcheck;
	struct file		*file;
	struct pid_namespace	*ns;
	struct timer_list	timer;
	struct list_head	list;
};

static DEFINE_SPINLOCK(acct_lock);
static LIST_HEAD(acct_list);

/*
 * Called whenever the timer says to check the free space.
 */
static void acct_timeout(unsigned long x)
{
	struct bsd_acct_struct *acct = (struct bsd_acct_struct *)x;
	acct->needcheck = 1;
}

/*
 * Check the amount of free space and suspend/resume accordingly.
 */
static int check_free_space(struct bsd_acct_struct *acct, struct file *file)
{
	struct kstatfs sbuf;
	int res;
	int act;
	sector_t resume;
	sector_t suspend;

	spin_lock(&acct_lock);
	res = acct->active;
	if (!file || !acct->needcheck)
		goto out;
	spin_unlock(&acct_lock);

	/* May block */
	if (vfs_statfs(file->f_path.dentry, &sbuf))
		return res;
	suspend = sbuf.f_blocks * SUSPEND;
	resume = sbuf.f_blocks * RESUME;

	sector_div(suspend, 100);
	sector_div(resume, 100);

	if (sbuf.f_bavail <= suspend)
		act = -1;
	else if (sbuf.f_bavail >= resume)
		act = 1;
	else
		act = 0;

	/*
	 * If some joker switched acct->file under us we'ld better be
	 * silent and _not_ touch anything.
	 */
	spin_lock(&acct_lock);
	if (file != acct->file) {
		if (act)
			res = act>0;
		goto out;
	}

	if (acct->active) {
		if (act < 0) {
			acct->active = 0;
			printk(KERN_INFO "Process accounting paused\n");
		}
	} else {
		if (act > 0) {
			acct->active = 1;
			printk(KERN_INFO "Process accounting resumed\n");
		}
	}

	del_timer(&acct->timer);
	acct->needcheck = 0;
	acct->timer.expires = jiffies + ACCT_TIMEOUT*HZ;
	add_timer(&acct->timer);
	res = acct->active;
out:
	spin_unlock(&acct_lock);
	return res;
}

/*
 * Close the old accounting file (if currently open) and then replace
 * it with file (if non-NULL).
 *
 * NOTE: acct_lock MUST be held on entry and exit.
 */
static void acct_file_reopen(struct bsd_acct_struct *acct, struct file *file,
		struct pid_namespace *ns)
{
	struct file *old_acct = NULL;
	struct pid_namespace *old_ns = NULL;

	if (acct->file) {
		old_acct = acct->file;
		old_ns = acct->ns;
		del_timer(&acct->timer);
		acct->active = 0;
		acct->needcheck = 0;
		acct->file = NULL;
		acct->ns = NULL;
		list_del(&acct->list);
	}
	if (file) {
		acct->file = file;
		acct->ns = ns;
		acct->needcheck = 0;
		acct->active = 1;
		list_add(&acct->list, &acct_list);
		/* It's been deleted if it was used before so this is safe */
		setup_timer(&acct->timer, acct_timeout, (unsigned long)acct);
		acct->timer.expires = jiffies + ACCT_TIMEOUT*HZ;
		add_timer(&acct->timer);
	}
	if (old_acct) {
		mnt_unpin(old_acct->f_path.mnt);
		spin_unlock(&acct_lock);
		do_acct_process(acct, old_ns, old_acct);
		filp_close(old_acct, NULL);
		spin_lock(&acct_lock);
	}
}

static int acct_on(char *name)
{
	struct file *file;
	struct vfsmount *mnt;
	int error;
	struct pid_namespace *ns;
	struct bsd_acct_struct *acct = NULL;

	/* Difference from BSD - they don't do O_APPEND */
	file = filp_open(name, O_WRONLY|O_APPEND|O_LARGEFILE, 0);
	if (IS_ERR(file))
		return PTR_ERR(file);

	if (!S_ISREG(file->f_path.dentry->d_inode->i_mode)) {
		filp_close(file, NULL);
		return -EACCES;
	}

	if (!file->f_op->write) {
		filp_close(file, NULL);
		return -EIO;
	}

	ns = task_active_pid_ns(current);
	if (ns->bacct == NULL) {
		acct = kzalloc(sizeof(struct bsd_acct_struct), GFP_KERNEL);
		if (acct == NULL) {
			filp_close(file, NULL);
			return -ENOMEM;
		}
	}

	error = security_acct(file);
	if (error) {
		kfree(acct);
		filp_close(file, NULL);
		return error;
	}

	spin_lock(&acct_lock);
	if (ns->bacct == NULL) {
		ns->bacct = acct;
		acct = NULL;
	}

	mnt = file->f_path.mnt;
	mnt_pin(mnt);
	acct_file_reopen(ns->bacct, file, ns);
	spin_unlock(&acct_lock);

	mntput(mnt); /* it's pinned, now give up active reference */
	kfree(acct);

	return 0;
}

/**
 * sys_acct - enable/disable process accounting
 * @name: file name for accounting records or NULL to shutdown accounting
 *
 * Returns 0 for success or negative errno values for failure.
 *
 * sys_acct() is the only system call needed to implement process
 * accounting. It takes the name of the file where accounting records
 * should be written. If the filename is NULL, accounting will be
 * shutdown.
 */
SYSCALL_DEFINE1(acct, const char __user *, name)
{
	int error;

	if (!capable(CAP_SYS_PACCT))
		return -EPERM;

	if (name) {
		char *tmp = getname(name);
		if (IS_ERR(tmp))
			return (PTR_ERR(tmp));
		error = acct_on(tmp);
		putname(tmp);
	} else {
		struct bsd_acct_struct *acct;

		acct = task_active_pid_ns(current)->bacct;
		if (acct == NULL)
			return 0;

		error = security_acct(NULL);
		if (!error) {
			spin_lock(&acct_lock);
			acct_file_reopen(acct, NULL, NULL);
			spin_unlock(&acct_lock);
		}
	}
	return error;
}

/**
 * acct_auto_close - turn off a filesystem's accounting if it is on
 * @m: vfsmount being shut down
 *
 * If the accounting is turned on for a file in the subtree pointed to
 * to by m, turn accounting off.  Done when m is about to die.
 */
void acct_auto_close_mnt(struct vfsmount *m)
{
	struct bsd_acct_struct *acct;

	spin_lock(&acct_lock);
restart:
	list_for_each_entry(acct, &acct_list, list)
		if (acct->file && acct->file->f_path.mnt == m) {
			acct_file_reopen(acct, NULL, NULL);
			goto restart;
		}
	spin_unlock(&acct_lock);
}

/**
 * acct_auto_close - turn off a filesystem's accounting if it is on
 * @sb: super block for the filesystem
 *
 * If the accounting is turned on for a file in the filesystem pointed
 * to by sb, turn accounting off.
 */
void acct_auto_close(struct super_block *sb)
{
	struct bsd_acct_struct *acct;

	spin_lock(&acct_lock);
restart:
	list_for_each_entry(acct, &acct_list, list)
		if (acct->file && acct->file->f_path.mnt->mnt_sb == sb) {
			acct_file_reopen(acct, NULL, NULL);
			goto restart;
		}
	spin_unlock(&acct_lock);
}

void acct_exit_ns(struct pid_namespace *ns)
{
	struct bsd_acct_struct *acct = ns->bacct;

	if (acct == NULL)
		return;

	del_timer_sync(&acct->timer);
	spin_lock(&acct_lock);
	if (acct->file != NULL)
		acct_file_reopen(acct, NULL, NULL);
	spin_unlock(&acct_lock);

	kfree(acct);
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

#if ACCT_VERSION==1 || ACCT_VERSION==2
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
#define MAXEXP2         ((1 <<EXPSIZE2) - 1)    /* Maximum exponent. */

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
#endif

#if ACCT_VERSION==3
/*
 * encode an u64 into a 32 bit IEEE float
 */
static u32 encode_float(u64 value)
{
	unsigned exp = 190;
	unsigned u;

	if (value==0) return 0;
	while ((s64)value > 0){
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

/*
 *  do_acct_process does all actual work. Caller holds the reference to file.
 */
static void do_acct_process(struct bsd_acct_struct *acct,
		struct pid_namespace *ns, struct file *file)
{
	struct pacct_struct *pacct = &current->signal->pacct;
	acct_t ac;
	mm_segment_t fs;
	unsigned long flim;
	u64 elapsed;
	u64 run_time;
	struct timespec uptime;
	struct tty_struct *tty;
	const struct cred *orig_cred;

	/* Perform file operations on behalf of whoever enabled accounting */
	orig_cred = override_creds(file->f_cred);

	/*
	 * First check to see if there is enough free_space to continue
	 * the process accounting system.
	 */
	if (!check_free_space(acct, file))
		goto out;

	/*
	 * Fill the accounting struct with the needed info as recorded
	 * by the different kernel functions.
	 */
	memset((caddr_t)&ac, 0, sizeof(acct_t));

	ac.ac_version = ACCT_VERSION | ACCT_BYTEORDER;
	strlcpy(ac.ac_comm, current->comm, sizeof(ac.ac_comm));

	/* calculate run_time in nsec*/
	do_posix_clock_monotonic_gettime(&uptime);
	run_time = (u64)uptime.tv_sec*NSEC_PER_SEC + uptime.tv_nsec;
	run_time -= (u64)current->group_leader->start_time.tv_sec * NSEC_PER_SEC
		       + current->group_leader->start_time.tv_nsec;
	/* convert nsec -> AHZ */
	elapsed = nsec_to_AHZ(run_time);
#if ACCT_VERSION==3
	ac.ac_etime = encode_float(elapsed);
#else
	ac.ac_etime = encode_comp_t(elapsed < (unsigned long) -1l ?
	                       (unsigned long) elapsed : (unsigned long) -1l);
#endif
#if ACCT_VERSION==1 || ACCT_VERSION==2
	{
		/* new enlarged etime field */
		comp2_t etime = encode_comp2_t(elapsed);
		ac.ac_etime_hi = etime >> 16;
		ac.ac_etime_lo = (u16) etime;
	}
#endif
	do_div(elapsed, AHZ);
	ac.ac_btime = get_seconds() - elapsed;
	/* we really need to bite the bullet and change layout */
	ac.ac_uid = orig_cred->uid;
	ac.ac_gid = orig_cred->gid;
#if ACCT_VERSION==2
	ac.ac_ahz = AHZ;
#endif
#if ACCT_VERSION==1 || ACCT_VERSION==2
	/* backward-compatible 16 bit fields */
	ac.ac_uid16 = ac.ac_uid;
	ac.ac_gid16 = ac.ac_gid;
#endif
#if ACCT_VERSION==3
	ac.ac_pid = task_tgid_nr_ns(current, ns);
	rcu_read_lock();
	ac.ac_ppid = task_tgid_nr_ns(rcu_dereference(current->real_parent), ns);
	rcu_read_unlock();
#endif

	spin_lock_irq(&current->sighand->siglock);
	tty = current->signal->tty;	/* Safe as we hold the siglock */
	ac.ac_tty = tty ? old_encode_dev(tty_devnum(tty)) : 0;
	ac.ac_utime = encode_comp_t(jiffies_to_AHZ(cputime_to_jiffies(pacct->ac_utime)));
	ac.ac_stime = encode_comp_t(jiffies_to_AHZ(cputime_to_jiffies(pacct->ac_stime)));
	ac.ac_flag = pacct->ac_flag;
	ac.ac_mem = encode_comp_t(pacct->ac_mem);
	ac.ac_minflt = encode_comp_t(pacct->ac_minflt);
	ac.ac_majflt = encode_comp_t(pacct->ac_majflt);
	ac.ac_exitcode = pacct->ac_exitcode;
	spin_unlock_irq(&current->sighand->siglock);
	ac.ac_io = encode_comp_t(0 /* current->io_usage */);	/* %% */
	ac.ac_rw = encode_comp_t(ac.ac_io / 1024);
	ac.ac_swaps = encode_comp_t(0);

	/*
	 * Kernel segment override to datasegment and write it
	 * to the accounting file.
	 */
	fs = get_fs();
	set_fs(KERNEL_DS);
	/*
	 * Accounting records are not subject to resource limits.
	 */
	flim = current->signal->rlim[RLIMIT_FSIZE].rlim_cur;
	current->signal->rlim[RLIMIT_FSIZE].rlim_cur = RLIM_INFINITY;
	file->f_op->write(file, (char *)&ac,
			       sizeof(acct_t), &file->f_pos);
	current->signal->rlim[RLIMIT_FSIZE].rlim_cur = flim;
	set_fs(fs);
out:
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
	unsigned long vsize = 0;

	if (group_dead && current->mm) {
		struct vm_area_struct *vma;
		down_read(&current->mm->mmap_sem);
		vma = current->mm->mmap;
		while (vma) {
			vsize += vma->vm_end - vma->vm_start;
			vma = vma->vm_next;
		}
		up_read(&current->mm->mmap_sem);
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
	pacct->ac_utime = cputime_add(pacct->ac_utime, current->utime);
	pacct->ac_stime = cputime_add(pacct->ac_stime, current->stime);
	pacct->ac_minflt += current->min_flt;
	pacct->ac_majflt += current->maj_flt;
	spin_unlock_irq(&current->sighand->siglock);
}

static void acct_process_in_ns(struct pid_namespace *ns)
{
	struct file *file = NULL;
	struct bsd_acct_struct *acct;

	acct = ns->bacct;
	/*
	 * accelerate the common fastpath:
	 */
	if (!acct || !acct->file)
		return;

	spin_lock(&acct_lock);
	file = acct->file;
	if (unlikely(!file)) {
		spin_unlock(&acct_lock);
		return;
	}
	get_file(file);
	spin_unlock(&acct_lock);

	do_acct_process(acct, ns, file);
	fput(file);
}

/**
 * acct_process - now just a wrapper around acct_process_in_ns,
 * which in turn is a wrapper around do_acct_process.
 *
 * handles process accounting for an exiting task
 */
void acct_process(void)
{
	struct pid_namespace *ns;

	/*
	 * This loop is safe lockless, since current is still
	 * alive and holds its namespace, which in turn holds
	 * its parent.
	 */
	for (ns = task_active_pid_ns(current); ns != NULL; ns = ns->parent)
		acct_process_in_ns(ns);
}
