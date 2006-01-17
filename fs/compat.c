/*
 *  linux/fs/compat.c
 *
 *  Kernel compatibililty routines for e.g. 32 bit syscall support
 *  on 64 bit kernels.
 *
 *  Copyright (C) 2002       Stephen Rothwell, IBM Corporation
 *  Copyright (C) 1997-2000  Jakub Jelinek  (jakub@redhat.com)
 *  Copyright (C) 1998       Eddie C. Dost  (ecd@skynet.be)
 *  Copyright (C) 2001,2002  Andi Kleen, SuSE Labs 
 *  Copyright (C) 2003       Pavel Machek (pavel@suse.cz)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/linkage.h>
#include <linux/compat.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <linux/fs.h>
#include <linux/fcntl.h>
#include <linux/namei.h>
#include <linux/file.h>
#include <linux/vfs.h>
#include <linux/ioctl32.h>
#include <linux/ioctl.h>
#include <linux/init.h>
#include <linux/sockios.h>	/* for SIOCDEVPRIVATE */
#include <linux/smb.h>
#include <linux/smb_mount.h>
#include <linux/ncp_mount.h>
#include <linux/nfs4_mount.h>
#include <linux/smp_lock.h>
#include <linux/syscalls.h>
#include <linux/ctype.h>
#include <linux/module.h>
#include <linux/dirent.h>
#include <linux/fsnotify.h>
#include <linux/highuid.h>
#include <linux/sunrpc/svc.h>
#include <linux/nfsd/nfsd.h>
#include <linux/nfsd/syscall.h>
#include <linux/personality.h>
#include <linux/rwsem.h>
#include <linux/acct.h>
#include <linux/mm.h>

#include <net/sock.h>		/* siocdevprivate_ioctl */

#include <asm/uaccess.h>
#include <asm/mmu_context.h>
#include <asm/ioctls.h>

/*
 * Not all architectures have sys_utime, so implement this in terms
 * of sys_utimes.
 */
asmlinkage long compat_sys_utime(char __user *filename, struct compat_utimbuf __user *t)
{
	struct timeval tv[2];

	if (t) {
		if (get_user(tv[0].tv_sec, &t->actime) ||
		    get_user(tv[1].tv_sec, &t->modtime))
			return -EFAULT;
		tv[0].tv_usec = 0;
		tv[1].tv_usec = 0;
	}
	return do_utimes(filename, t ? tv : NULL);
}

asmlinkage long compat_sys_utimes(char __user *filename, struct compat_timeval __user *t)
{
	struct timeval tv[2];

	if (t) { 
		if (get_user(tv[0].tv_sec, &t[0].tv_sec) ||
		    get_user(tv[0].tv_usec, &t[0].tv_usec) ||
		    get_user(tv[1].tv_sec, &t[1].tv_sec) ||
		    get_user(tv[1].tv_usec, &t[1].tv_usec))
			return -EFAULT; 
	} 
	return do_utimes(filename, t ? tv : NULL);
}

asmlinkage long compat_sys_newstat(char __user * filename,
		struct compat_stat __user *statbuf)
{
	struct kstat stat;
	int error = vfs_stat(filename, &stat);

	if (!error)
		error = cp_compat_stat(&stat, statbuf);
	return error;
}

asmlinkage long compat_sys_newlstat(char __user * filename,
		struct compat_stat __user *statbuf)
{
	struct kstat stat;
	int error = vfs_lstat(filename, &stat);

	if (!error)
		error = cp_compat_stat(&stat, statbuf);
	return error;
}

asmlinkage long compat_sys_newfstat(unsigned int fd,
		struct compat_stat __user * statbuf)
{
	struct kstat stat;
	int error = vfs_fstat(fd, &stat);

	if (!error)
		error = cp_compat_stat(&stat, statbuf);
	return error;
}

static int put_compat_statfs(struct compat_statfs __user *ubuf, struct kstatfs *kbuf)
{
	
	if (sizeof ubuf->f_blocks == 4) {
		if ((kbuf->f_blocks | kbuf->f_bfree | kbuf->f_bavail) &
		    0xffffffff00000000ULL)
			return -EOVERFLOW;
		/* f_files and f_ffree may be -1; it's okay
		 * to stuff that into 32 bits */
		if (kbuf->f_files != 0xffffffffffffffffULL
		 && (kbuf->f_files & 0xffffffff00000000ULL))
			return -EOVERFLOW;
		if (kbuf->f_ffree != 0xffffffffffffffffULL
		 && (kbuf->f_ffree & 0xffffffff00000000ULL))
			return -EOVERFLOW;
	}
	if (!access_ok(VERIFY_WRITE, ubuf, sizeof(*ubuf)) ||
	    __put_user(kbuf->f_type, &ubuf->f_type) ||
	    __put_user(kbuf->f_bsize, &ubuf->f_bsize) ||
	    __put_user(kbuf->f_blocks, &ubuf->f_blocks) ||
	    __put_user(kbuf->f_bfree, &ubuf->f_bfree) ||
	    __put_user(kbuf->f_bavail, &ubuf->f_bavail) ||
	    __put_user(kbuf->f_files, &ubuf->f_files) ||
	    __put_user(kbuf->f_ffree, &ubuf->f_ffree) ||
	    __put_user(kbuf->f_namelen, &ubuf->f_namelen) ||
	    __put_user(kbuf->f_fsid.val[0], &ubuf->f_fsid.val[0]) ||
	    __put_user(kbuf->f_fsid.val[1], &ubuf->f_fsid.val[1]) ||
	    __put_user(kbuf->f_frsize, &ubuf->f_frsize) ||
	    __put_user(0, &ubuf->f_spare[0]) || 
	    __put_user(0, &ubuf->f_spare[1]) || 
	    __put_user(0, &ubuf->f_spare[2]) || 
	    __put_user(0, &ubuf->f_spare[3]) || 
	    __put_user(0, &ubuf->f_spare[4]))
		return -EFAULT;
	return 0;
}

/*
 * The following statfs calls are copies of code from fs/open.c and
 * should be checked against those from time to time
 */
asmlinkage long compat_sys_statfs(const char __user *path, struct compat_statfs __user *buf)
{
	struct nameidata nd;
	int error;

	error = user_path_walk(path, &nd);
	if (!error) {
		struct kstatfs tmp;
		error = vfs_statfs(nd.dentry->d_inode->i_sb, &tmp);
		if (!error)
			error = put_compat_statfs(buf, &tmp);
		path_release(&nd);
	}
	return error;
}

asmlinkage long compat_sys_fstatfs(unsigned int fd, struct compat_statfs __user *buf)
{
	struct file * file;
	struct kstatfs tmp;
	int error;

	error = -EBADF;
	file = fget(fd);
	if (!file)
		goto out;
	error = vfs_statfs(file->f_dentry->d_inode->i_sb, &tmp);
	if (!error)
		error = put_compat_statfs(buf, &tmp);
	fput(file);
out:
	return error;
}

static int put_compat_statfs64(struct compat_statfs64 __user *ubuf, struct kstatfs *kbuf)
{
	if (sizeof ubuf->f_blocks == 4) {
		if ((kbuf->f_blocks | kbuf->f_bfree | kbuf->f_bavail) &
		    0xffffffff00000000ULL)
			return -EOVERFLOW;
		/* f_files and f_ffree may be -1; it's okay
		 * to stuff that into 32 bits */
		if (kbuf->f_files != 0xffffffffffffffffULL
		 && (kbuf->f_files & 0xffffffff00000000ULL))
			return -EOVERFLOW;
		if (kbuf->f_ffree != 0xffffffffffffffffULL
		 && (kbuf->f_ffree & 0xffffffff00000000ULL))
			return -EOVERFLOW;
	}
	if (!access_ok(VERIFY_WRITE, ubuf, sizeof(*ubuf)) ||
	    __put_user(kbuf->f_type, &ubuf->f_type) ||
	    __put_user(kbuf->f_bsize, &ubuf->f_bsize) ||
	    __put_user(kbuf->f_blocks, &ubuf->f_blocks) ||
	    __put_user(kbuf->f_bfree, &ubuf->f_bfree) ||
	    __put_user(kbuf->f_bavail, &ubuf->f_bavail) ||
	    __put_user(kbuf->f_files, &ubuf->f_files) ||
	    __put_user(kbuf->f_ffree, &ubuf->f_ffree) ||
	    __put_user(kbuf->f_namelen, &ubuf->f_namelen) ||
	    __put_user(kbuf->f_fsid.val[0], &ubuf->f_fsid.val[0]) ||
	    __put_user(kbuf->f_fsid.val[1], &ubuf->f_fsid.val[1]) ||
	    __put_user(kbuf->f_frsize, &ubuf->f_frsize))
		return -EFAULT;
	return 0;
}

asmlinkage long compat_sys_statfs64(const char __user *path, compat_size_t sz, struct compat_statfs64 __user *buf)
{
	struct nameidata nd;
	int error;

	if (sz != sizeof(*buf))
		return -EINVAL;

	error = user_path_walk(path, &nd);
	if (!error) {
		struct kstatfs tmp;
		error = vfs_statfs(nd.dentry->d_inode->i_sb, &tmp);
		if (!error)
			error = put_compat_statfs64(buf, &tmp);
		path_release(&nd);
	}
	return error;
}

asmlinkage long compat_sys_fstatfs64(unsigned int fd, compat_size_t sz, struct compat_statfs64 __user *buf)
{
	struct file * file;
	struct kstatfs tmp;
	int error;

	if (sz != sizeof(*buf))
		return -EINVAL;

	error = -EBADF;
	file = fget(fd);
	if (!file)
		goto out;
	error = vfs_statfs(file->f_dentry->d_inode->i_sb, &tmp);
	if (!error)
		error = put_compat_statfs64(buf, &tmp);
	fput(file);
out:
	return error;
}

/* ioctl32 stuff, used by sparc64, parisc, s390x, ppc64, x86_64, MIPS */

#define IOCTL_HASHSIZE 256
static struct ioctl_trans *ioctl32_hash_table[IOCTL_HASHSIZE];

extern struct ioctl_trans ioctl_start[];
extern int ioctl_table_size;

static inline unsigned long ioctl32_hash(unsigned long cmd)
{
	return (((cmd >> 6) ^ (cmd >> 4) ^ cmd)) % IOCTL_HASHSIZE;
}

static void ioctl32_insert_translation(struct ioctl_trans *trans)
{
	unsigned long hash;
	struct ioctl_trans *t;

	hash = ioctl32_hash (trans->cmd);
	if (!ioctl32_hash_table[hash])
		ioctl32_hash_table[hash] = trans;
	else {
		t = ioctl32_hash_table[hash];
		while (t->next)
			t = t->next;
		trans->next = NULL;
		t->next = trans;
	}
}

static int __init init_sys32_ioctl(void)
{
	int i;

	for (i = 0; i < ioctl_table_size; i++) {
		if (ioctl_start[i].next != 0) { 
			printk("ioctl translation %d bad\n",i); 
			return -1;
		}

		ioctl32_insert_translation(&ioctl_start[i]);
	}
	return 0;
}

__initcall(init_sys32_ioctl);

static void compat_ioctl_error(struct file *filp, unsigned int fd,
		unsigned int cmd, unsigned long arg)
{
	char buf[10];
	char *fn = "?";
	char *path;

	/* find the name of the device. */
	path = (char *)__get_free_page(GFP_KERNEL);
	if (path) {
		fn = d_path(filp->f_dentry, filp->f_vfsmnt, path, PAGE_SIZE);
		if (IS_ERR(fn))
			fn = "?";
	}

	sprintf(buf,"'%c'", (cmd>>24) & 0x3f);
	if (!isprint(buf[1]))
		sprintf(buf, "%02x", buf[1]);
	printk("ioctl32(%s:%d): Unknown cmd fd(%d) "
			"cmd(%08x){%s} arg(%08x) on %s\n",
			current->comm, current->pid,
			(int)fd, (unsigned int)cmd, buf,
			(unsigned int)arg, fn);

	if (path)
		free_page((unsigned long)path);
}

asmlinkage long compat_sys_ioctl(unsigned int fd, unsigned int cmd,
				unsigned long arg)
{
	struct file *filp;
	int error = -EBADF;
	struct ioctl_trans *t;
	int fput_needed;

	filp = fget_light(fd, &fput_needed);
	if (!filp)
		goto out;

	/* RED-PEN how should LSM module know it's handling 32bit? */
	error = security_file_ioctl(filp, cmd, arg);
	if (error)
		goto out_fput;

	/*
	 * To allow the compat_ioctl handlers to be self contained
	 * we need to check the common ioctls here first.
	 * Just handle them with the standard handlers below.
	 */
	switch (cmd) {
	case FIOCLEX:
	case FIONCLEX:
	case FIONBIO:
	case FIOASYNC:
	case FIOQSIZE:
		break;

	case FIBMAP:
	case FIGETBSZ:
	case FIONREAD:
		if (S_ISREG(filp->f_dentry->d_inode->i_mode))
			break;
		/*FALL THROUGH*/

	default:
		if (filp->f_op && filp->f_op->compat_ioctl) {
			error = filp->f_op->compat_ioctl(filp, cmd, arg);
			if (error != -ENOIOCTLCMD)
				goto out_fput;
		}

		if (!filp->f_op ||
		    (!filp->f_op->ioctl && !filp->f_op->unlocked_ioctl))
			goto do_ioctl;
		break;
	}

	for (t = ioctl32_hash_table[ioctl32_hash(cmd)]; t; t = t->next) {
		if (t->cmd == cmd)
			goto found_handler;
	}

	if (S_ISSOCK(filp->f_dentry->d_inode->i_mode) &&
	    cmd >= SIOCDEVPRIVATE && cmd <= (SIOCDEVPRIVATE + 15)) {
		error = siocdevprivate_ioctl(fd, cmd, arg);
	} else {
		static int count;

		if (++count <= 50)
			compat_ioctl_error(filp, fd, cmd, arg);
		error = -EINVAL;
	}

	goto out_fput;

 found_handler:
	if (t->handler) {
		lock_kernel();
		error = t->handler(fd, cmd, arg, filp);
		unlock_kernel();
		goto out_fput;
	}

 do_ioctl:
	error = vfs_ioctl(filp, fd, cmd, arg);
 out_fput:
	fput_light(filp, fput_needed);
 out:
	return error;
}

static int get_compat_flock(struct flock *kfl, struct compat_flock __user *ufl)
{
	if (!access_ok(VERIFY_READ, ufl, sizeof(*ufl)) ||
	    __get_user(kfl->l_type, &ufl->l_type) ||
	    __get_user(kfl->l_whence, &ufl->l_whence) ||
	    __get_user(kfl->l_start, &ufl->l_start) ||
	    __get_user(kfl->l_len, &ufl->l_len) ||
	    __get_user(kfl->l_pid, &ufl->l_pid))
		return -EFAULT;
	return 0;
}

static int put_compat_flock(struct flock *kfl, struct compat_flock __user *ufl)
{
	if (!access_ok(VERIFY_WRITE, ufl, sizeof(*ufl)) ||
	    __put_user(kfl->l_type, &ufl->l_type) ||
	    __put_user(kfl->l_whence, &ufl->l_whence) ||
	    __put_user(kfl->l_start, &ufl->l_start) ||
	    __put_user(kfl->l_len, &ufl->l_len) ||
	    __put_user(kfl->l_pid, &ufl->l_pid))
		return -EFAULT;
	return 0;
}

#ifndef HAVE_ARCH_GET_COMPAT_FLOCK64
static int get_compat_flock64(struct flock *kfl, struct compat_flock64 __user *ufl)
{
	if (!access_ok(VERIFY_READ, ufl, sizeof(*ufl)) ||
	    __get_user(kfl->l_type, &ufl->l_type) ||
	    __get_user(kfl->l_whence, &ufl->l_whence) ||
	    __get_user(kfl->l_start, &ufl->l_start) ||
	    __get_user(kfl->l_len, &ufl->l_len) ||
	    __get_user(kfl->l_pid, &ufl->l_pid))
		return -EFAULT;
	return 0;
}
#endif

#ifndef HAVE_ARCH_PUT_COMPAT_FLOCK64
static int put_compat_flock64(struct flock *kfl, struct compat_flock64 __user *ufl)
{
	if (!access_ok(VERIFY_WRITE, ufl, sizeof(*ufl)) ||
	    __put_user(kfl->l_type, &ufl->l_type) ||
	    __put_user(kfl->l_whence, &ufl->l_whence) ||
	    __put_user(kfl->l_start, &ufl->l_start) ||
	    __put_user(kfl->l_len, &ufl->l_len) ||
	    __put_user(kfl->l_pid, &ufl->l_pid))
		return -EFAULT;
	return 0;
}
#endif

asmlinkage long compat_sys_fcntl64(unsigned int fd, unsigned int cmd,
		unsigned long arg)
{
	mm_segment_t old_fs;
	struct flock f;
	long ret;

	switch (cmd) {
	case F_GETLK:
	case F_SETLK:
	case F_SETLKW:
		ret = get_compat_flock(&f, compat_ptr(arg));
		if (ret != 0)
			break;
		old_fs = get_fs();
		set_fs(KERNEL_DS);
		ret = sys_fcntl(fd, cmd, (unsigned long)&f);
		set_fs(old_fs);
		if (cmd == F_GETLK && ret == 0) {
			/* GETLK was successfule and we need to return the data...
			 * but it needs to fit in the compat structure.
			 * l_start shouldn't be too big, unless the original
			 * start + end is greater than COMPAT_OFF_T_MAX, in which
			 * case the app was asking for trouble, so we return
			 * -EOVERFLOW in that case.
			 * l_len could be too big, in which case we just truncate it,
			 * and only allow the app to see that part of the conflicting
			 * lock that might make sense to it anyway
			 */

			if (f.l_start > COMPAT_OFF_T_MAX)
				ret = -EOVERFLOW;
			if (f.l_len > COMPAT_OFF_T_MAX)
				f.l_len = COMPAT_OFF_T_MAX;
			if (ret == 0)
				ret = put_compat_flock(&f, compat_ptr(arg));
		}
		break;

	case F_GETLK64:
	case F_SETLK64:
	case F_SETLKW64:
		ret = get_compat_flock64(&f, compat_ptr(arg));
		if (ret != 0)
			break;
		old_fs = get_fs();
		set_fs(KERNEL_DS);
		ret = sys_fcntl(fd, (cmd == F_GETLK64) ? F_GETLK :
				((cmd == F_SETLK64) ? F_SETLK : F_SETLKW),
				(unsigned long)&f);
		set_fs(old_fs);
		if (cmd == F_GETLK64 && ret == 0) {
			/* need to return lock information - see above for commentary */
			if (f.l_start > COMPAT_LOFF_T_MAX)
				ret = -EOVERFLOW;
			if (f.l_len > COMPAT_LOFF_T_MAX)
				f.l_len = COMPAT_LOFF_T_MAX;
			if (ret == 0)
				ret = put_compat_flock64(&f, compat_ptr(arg));
		}
		break;

	default:
		ret = sys_fcntl(fd, cmd, arg);
		break;
	}
	return ret;
}

asmlinkage long compat_sys_fcntl(unsigned int fd, unsigned int cmd,
		unsigned long arg)
{
	if ((cmd == F_GETLK64) || (cmd == F_SETLK64) || (cmd == F_SETLKW64))
		return -EINVAL;
	return compat_sys_fcntl64(fd, cmd, arg);
}

asmlinkage long
compat_sys_io_setup(unsigned nr_reqs, u32 __user *ctx32p)
{
	long ret;
	aio_context_t ctx64;

	mm_segment_t oldfs = get_fs();
	if (unlikely(get_user(ctx64, ctx32p)))
		return -EFAULT;

	set_fs(KERNEL_DS);
	/* The __user pointer cast is valid because of the set_fs() */
	ret = sys_io_setup(nr_reqs, (aio_context_t __user *) &ctx64);
	set_fs(oldfs);
	/* truncating is ok because it's a user address */
	if (!ret)
		ret = put_user((u32) ctx64, ctx32p);
	return ret;
}

asmlinkage long
compat_sys_io_getevents(aio_context_t ctx_id,
				 unsigned long min_nr,
				 unsigned long nr,
				 struct io_event __user *events,
				 struct compat_timespec __user *timeout)
{
	long ret;
	struct timespec t;
	struct timespec __user *ut = NULL;

	ret = -EFAULT;
	if (unlikely(!access_ok(VERIFY_WRITE, events, 
				nr * sizeof(struct io_event))))
		goto out;
	if (timeout) {
		if (get_compat_timespec(&t, timeout))
			goto out;

		ut = compat_alloc_user_space(sizeof(*ut));
		if (copy_to_user(ut, &t, sizeof(t)) )
			goto out;
	} 
	ret = sys_io_getevents(ctx_id, min_nr, nr, events, ut);
out:
	return ret;
}

static inline long
copy_iocb(long nr, u32 __user *ptr32, struct iocb __user * __user *ptr64)
{
	compat_uptr_t uptr;
	int i;

	for (i = 0; i < nr; ++i) {
		if (get_user(uptr, ptr32 + i))
			return -EFAULT;
		if (put_user(compat_ptr(uptr), ptr64 + i))
			return -EFAULT;
	}
	return 0;
}

#define MAX_AIO_SUBMITS 	(PAGE_SIZE/sizeof(struct iocb *))

asmlinkage long
compat_sys_io_submit(aio_context_t ctx_id, int nr, u32 __user *iocb)
{
	struct iocb __user * __user *iocb64; 
	long ret;

	if (unlikely(nr < 0))
		return -EINVAL;

	if (nr > MAX_AIO_SUBMITS)
		nr = MAX_AIO_SUBMITS;
	
	iocb64 = compat_alloc_user_space(nr * sizeof(*iocb64));
	ret = copy_iocb(nr, iocb, iocb64);
	if (!ret)
		ret = sys_io_submit(ctx_id, nr, iocb64);
	return ret;
}

struct compat_ncp_mount_data {
	compat_int_t version;
	compat_uint_t ncp_fd;
	__compat_uid_t mounted_uid;
	compat_pid_t wdog_pid;
	unsigned char mounted_vol[NCP_VOLNAME_LEN + 1];
	compat_uint_t time_out;
	compat_uint_t retry_count;
	compat_uint_t flags;
	__compat_uid_t uid;
	__compat_gid_t gid;
	compat_mode_t file_mode;
	compat_mode_t dir_mode;
};

struct compat_ncp_mount_data_v4 {
	compat_int_t version;
	compat_ulong_t flags;
	compat_ulong_t mounted_uid;
	compat_long_t wdog_pid;
	compat_uint_t ncp_fd;
	compat_uint_t time_out;
	compat_uint_t retry_count;
	compat_ulong_t uid;
	compat_ulong_t gid;
	compat_ulong_t file_mode;
	compat_ulong_t dir_mode;
};

static void *do_ncp_super_data_conv(void *raw_data)
{
	int version = *(unsigned int *)raw_data;

	if (version == 3) {
		struct compat_ncp_mount_data *c_n = raw_data;
		struct ncp_mount_data *n = raw_data;

		n->dir_mode = c_n->dir_mode;
		n->file_mode = c_n->file_mode;
		n->gid = c_n->gid;
		n->uid = c_n->uid;
		memmove (n->mounted_vol, c_n->mounted_vol, (sizeof (c_n->mounted_vol) + 3 * sizeof (unsigned int)));
		n->wdog_pid = c_n->wdog_pid;
		n->mounted_uid = c_n->mounted_uid;
	} else if (version == 4) {
		struct compat_ncp_mount_data_v4 *c_n = raw_data;
		struct ncp_mount_data_v4 *n = raw_data;

		n->dir_mode = c_n->dir_mode;
		n->file_mode = c_n->file_mode;
		n->gid = c_n->gid;
		n->uid = c_n->uid;
		n->retry_count = c_n->retry_count;
		n->time_out = c_n->time_out;
		n->ncp_fd = c_n->ncp_fd;
		n->wdog_pid = c_n->wdog_pid;
		n->mounted_uid = c_n->mounted_uid;
		n->flags = c_n->flags;
	} else if (version != 5) {
		return NULL;
	}

	return raw_data;
}

struct compat_smb_mount_data {
	compat_int_t version;
	__compat_uid_t mounted_uid;
	__compat_uid_t uid;
	__compat_gid_t gid;
	compat_mode_t file_mode;
	compat_mode_t dir_mode;
};

static void *do_smb_super_data_conv(void *raw_data)
{
	struct smb_mount_data *s = raw_data;
	struct compat_smb_mount_data *c_s = raw_data;

	if (c_s->version != SMB_MOUNT_OLDVERSION)
		goto out;
	s->dir_mode = c_s->dir_mode;
	s->file_mode = c_s->file_mode;
	s->gid = c_s->gid;
	s->uid = c_s->uid;
	s->mounted_uid = c_s->mounted_uid;
 out:
	return raw_data;
}

struct compat_nfs_string {
	compat_uint_t len;
	compat_uptr_t data;
};

static inline void compat_nfs_string(struct nfs_string *dst,
				     struct compat_nfs_string *src)
{
	dst->data = compat_ptr(src->data);
	dst->len = src->len;
}

struct compat_nfs4_mount_data_v1 {
	compat_int_t version;
	compat_int_t flags;
	compat_int_t rsize;
	compat_int_t wsize;
	compat_int_t timeo;
	compat_int_t retrans;
	compat_int_t acregmin;
	compat_int_t acregmax;
	compat_int_t acdirmin;
	compat_int_t acdirmax;
	struct compat_nfs_string client_addr;
	struct compat_nfs_string mnt_path;
	struct compat_nfs_string hostname;
	compat_uint_t host_addrlen;
	compat_uptr_t host_addr;
	compat_int_t proto;
	compat_int_t auth_flavourlen;
	compat_uptr_t auth_flavours;
};

static int do_nfs4_super_data_conv(void *raw_data)
{
	int version = *(compat_uint_t *) raw_data;

	if (version == 1) {
		struct compat_nfs4_mount_data_v1 *raw = raw_data;
		struct nfs4_mount_data *real = raw_data;

		/* copy the fields backwards */
		real->auth_flavours = compat_ptr(raw->auth_flavours);
		real->auth_flavourlen = raw->auth_flavourlen;
		real->proto = raw->proto;
		real->host_addr = compat_ptr(raw->host_addr);
		real->host_addrlen = raw->host_addrlen;
		compat_nfs_string(&real->hostname, &raw->hostname);
		compat_nfs_string(&real->mnt_path, &raw->mnt_path);
		compat_nfs_string(&real->client_addr, &raw->client_addr);
		real->acdirmax = raw->acdirmax;
		real->acdirmin = raw->acdirmin;
		real->acregmax = raw->acregmax;
		real->acregmin = raw->acregmin;
		real->retrans = raw->retrans;
		real->timeo = raw->timeo;
		real->wsize = raw->wsize;
		real->rsize = raw->rsize;
		real->flags = raw->flags;
		real->version = raw->version;
	}
	else {
		return -EINVAL;
	}

	return 0;
}

extern int copy_mount_options (const void __user *, unsigned long *);

#define SMBFS_NAME      "smbfs"
#define NCPFS_NAME      "ncpfs"
#define NFS4_NAME	"nfs4"

asmlinkage long compat_sys_mount(char __user * dev_name, char __user * dir_name,
				 char __user * type, unsigned long flags,
				 void __user * data)
{
	unsigned long type_page;
	unsigned long data_page;
	unsigned long dev_page;
	char *dir_page;
	int retval;

	retval = copy_mount_options (type, &type_page);
	if (retval < 0)
		goto out;

	dir_page = getname(dir_name);
	retval = PTR_ERR(dir_page);
	if (IS_ERR(dir_page))
		goto out1;

	retval = copy_mount_options (dev_name, &dev_page);
	if (retval < 0)
		goto out2;

	retval = copy_mount_options (data, &data_page);
	if (retval < 0)
		goto out3;

	retval = -EINVAL;

	if (type_page) {
		if (!strcmp((char *)type_page, SMBFS_NAME)) {
			do_smb_super_data_conv((void *)data_page);
		} else if (!strcmp((char *)type_page, NCPFS_NAME)) {
			do_ncp_super_data_conv((void *)data_page);
		} else if (!strcmp((char *)type_page, NFS4_NAME)) {
			if (do_nfs4_super_data_conv((void *) data_page))
				goto out4;
		}
	}

	lock_kernel();
	retval = do_mount((char*)dev_page, dir_page, (char*)type_page,
			flags, (void*)data_page);
	unlock_kernel();

 out4:
	free_page(data_page);
 out3:
	free_page(dev_page);
 out2:
	putname(dir_page);
 out1:
	free_page(type_page);
 out:
	return retval;
}

#define NAME_OFFSET(de) ((int) ((de)->d_name - (char __user *) (de)))
#define COMPAT_ROUND_UP(x) (((x)+sizeof(compat_long_t)-1) & \
				~(sizeof(compat_long_t)-1))

struct compat_old_linux_dirent {
	compat_ulong_t	d_ino;
	compat_ulong_t	d_offset;
	unsigned short	d_namlen;
	char		d_name[1];
};

struct compat_readdir_callback {
	struct compat_old_linux_dirent __user *dirent;
	int result;
};

static int compat_fillonedir(void *__buf, const char *name, int namlen,
			loff_t offset, ino_t ino, unsigned int d_type)
{
	struct compat_readdir_callback *buf = __buf;
	struct compat_old_linux_dirent __user *dirent;

	if (buf->result)
		return -EINVAL;
	buf->result++;
	dirent = buf->dirent;
	if (!access_ok(VERIFY_WRITE, dirent,
			(unsigned long)(dirent->d_name + namlen + 1) -
				(unsigned long)dirent))
		goto efault;
	if (	__put_user(ino, &dirent->d_ino) ||
		__put_user(offset, &dirent->d_offset) ||
		__put_user(namlen, &dirent->d_namlen) ||
		__copy_to_user(dirent->d_name, name, namlen) ||
		__put_user(0, dirent->d_name + namlen))
		goto efault;
	return 0;
efault:
	buf->result = -EFAULT;
	return -EFAULT;
}

asmlinkage long compat_sys_old_readdir(unsigned int fd,
	struct compat_old_linux_dirent __user *dirent, unsigned int count)
{
	int error;
	struct file *file;
	struct compat_readdir_callback buf;

	error = -EBADF;
	file = fget(fd);
	if (!file)
		goto out;

	buf.result = 0;
	buf.dirent = dirent;

	error = vfs_readdir(file, compat_fillonedir, &buf);
	if (error >= 0)
		error = buf.result;

	fput(file);
out:
	return error;
}

struct compat_linux_dirent {
	compat_ulong_t	d_ino;
	compat_ulong_t	d_off;
	unsigned short	d_reclen;
	char		d_name[1];
};

struct compat_getdents_callback {
	struct compat_linux_dirent __user *current_dir;
	struct compat_linux_dirent __user *previous;
	int count;
	int error;
};

static int compat_filldir(void *__buf, const char *name, int namlen,
		loff_t offset, ino_t ino, unsigned int d_type)
{
	struct compat_linux_dirent __user * dirent;
	struct compat_getdents_callback *buf = __buf;
	int reclen = COMPAT_ROUND_UP(NAME_OFFSET(dirent) + namlen + 2);

	buf->error = -EINVAL;	/* only used if we fail.. */
	if (reclen > buf->count)
		return -EINVAL;
	dirent = buf->previous;
	if (dirent) {
		if (__put_user(offset, &dirent->d_off))
			goto efault;
	}
	dirent = buf->current_dir;
	if (__put_user(ino, &dirent->d_ino))
		goto efault;
	if (__put_user(reclen, &dirent->d_reclen))
		goto efault;
	if (copy_to_user(dirent->d_name, name, namlen))
		goto efault;
	if (__put_user(0, dirent->d_name + namlen))
		goto efault;
	if (__put_user(d_type, (char  __user *) dirent + reclen - 1))
		goto efault;
	buf->previous = dirent;
	dirent = (void __user *)dirent + reclen;
	buf->current_dir = dirent;
	buf->count -= reclen;
	return 0;
efault:
	buf->error = -EFAULT;
	return -EFAULT;
}

asmlinkage long compat_sys_getdents(unsigned int fd,
		struct compat_linux_dirent __user *dirent, unsigned int count)
{
	struct file * file;
	struct compat_linux_dirent __user * lastdirent;
	struct compat_getdents_callback buf;
	int error;

	error = -EFAULT;
	if (!access_ok(VERIFY_WRITE, dirent, count))
		goto out;

	error = -EBADF;
	file = fget(fd);
	if (!file)
		goto out;

	buf.current_dir = dirent;
	buf.previous = NULL;
	buf.count = count;
	buf.error = 0;

	error = vfs_readdir(file, compat_filldir, &buf);
	if (error < 0)
		goto out_putf;
	error = buf.error;
	lastdirent = buf.previous;
	if (lastdirent) {
		if (put_user(file->f_pos, &lastdirent->d_off))
			error = -EFAULT;
		else
			error = count - buf.count;
	}

out_putf:
	fput(file);
out:
	return error;
}

#ifndef __ARCH_OMIT_COMPAT_SYS_GETDENTS64
#define COMPAT_ROUND_UP64(x) (((x)+sizeof(u64)-1) & ~(sizeof(u64)-1))

struct compat_getdents_callback64 {
	struct linux_dirent64 __user *current_dir;
	struct linux_dirent64 __user *previous;
	int count;
	int error;
};

static int compat_filldir64(void * __buf, const char * name, int namlen, loff_t offset,
		     ino_t ino, unsigned int d_type)
{
	struct linux_dirent64 __user *dirent;
	struct compat_getdents_callback64 *buf = __buf;
	int jj = NAME_OFFSET(dirent);
	int reclen = COMPAT_ROUND_UP64(jj + namlen + 1);
	u64 off;

	buf->error = -EINVAL;	/* only used if we fail.. */
	if (reclen > buf->count)
		return -EINVAL;
	dirent = buf->previous;

	if (dirent) {
		if (__put_user_unaligned(offset, &dirent->d_off))
			goto efault;
	}
	dirent = buf->current_dir;
	if (__put_user_unaligned(ino, &dirent->d_ino))
		goto efault;
	off = 0;
	if (__put_user_unaligned(off, &dirent->d_off))
		goto efault;
	if (__put_user(reclen, &dirent->d_reclen))
		goto efault;
	if (__put_user(d_type, &dirent->d_type))
		goto efault;
	if (copy_to_user(dirent->d_name, name, namlen))
		goto efault;
	if (__put_user(0, dirent->d_name + namlen))
		goto efault;
	buf->previous = dirent;
	dirent = (void __user *)dirent + reclen;
	buf->current_dir = dirent;
	buf->count -= reclen;
	return 0;
efault:
	buf->error = -EFAULT;
	return -EFAULT;
}

asmlinkage long compat_sys_getdents64(unsigned int fd,
		struct linux_dirent64 __user * dirent, unsigned int count)
{
	struct file * file;
	struct linux_dirent64 __user * lastdirent;
	struct compat_getdents_callback64 buf;
	int error;

	error = -EFAULT;
	if (!access_ok(VERIFY_WRITE, dirent, count))
		goto out;

	error = -EBADF;
	file = fget(fd);
	if (!file)
		goto out;

	buf.current_dir = dirent;
	buf.previous = NULL;
	buf.count = count;
	buf.error = 0;

	error = vfs_readdir(file, compat_filldir64, &buf);
	if (error < 0)
		goto out_putf;
	error = buf.error;
	lastdirent = buf.previous;
	if (lastdirent) {
		typeof(lastdirent->d_off) d_off = file->f_pos;
		__put_user_unaligned(d_off, &lastdirent->d_off);
		error = count - buf.count;
	}

out_putf:
	fput(file);
out:
	return error;
}
#endif /* ! __ARCH_OMIT_COMPAT_SYS_GETDENTS64 */

static ssize_t compat_do_readv_writev(int type, struct file *file,
			       const struct compat_iovec __user *uvector,
			       unsigned long nr_segs, loff_t *pos)
{
	typedef ssize_t (*io_fn_t)(struct file *, char __user *, size_t, loff_t *);
	typedef ssize_t (*iov_fn_t)(struct file *, const struct iovec *, unsigned long, loff_t *);

	compat_ssize_t tot_len;
	struct iovec iovstack[UIO_FASTIOV];
	struct iovec *iov=iovstack, *vector;
	ssize_t ret;
	int seg;
	io_fn_t fn;
	iov_fn_t fnv;

	/*
	 * SuS says "The readv() function *may* fail if the iovcnt argument
	 * was less than or equal to 0, or greater than {IOV_MAX}.  Linux has
	 * traditionally returned zero for zero segments, so...
	 */
	ret = 0;
	if (nr_segs == 0)
		goto out;

	/*
	 * First get the "struct iovec" from user memory and
	 * verify all the pointers
	 */
	ret = -EINVAL;
	if ((nr_segs > UIO_MAXIOV) || (nr_segs <= 0))
		goto out;
	if (!file->f_op)
		goto out;
	if (nr_segs > UIO_FASTIOV) {
		ret = -ENOMEM;
		iov = kmalloc(nr_segs*sizeof(struct iovec), GFP_KERNEL);
		if (!iov)
			goto out;
	}
	ret = -EFAULT;
	if (!access_ok(VERIFY_READ, uvector, nr_segs*sizeof(*uvector)))
		goto out;

	/*
	 * Single unix specification:
	 * We should -EINVAL if an element length is not >= 0 and fitting an
	 * ssize_t.  The total length is fitting an ssize_t
	 *
	 * Be careful here because iov_len is a size_t not an ssize_t
	 */
	tot_len = 0;
	vector = iov;
	ret = -EINVAL;
	for (seg = 0 ; seg < nr_segs; seg++) {
		compat_ssize_t tmp = tot_len;
		compat_ssize_t len;
		compat_uptr_t buf;

		if (__get_user(len, &uvector->iov_len) ||
		    __get_user(buf, &uvector->iov_base)) {
			ret = -EFAULT;
			goto out;
		}
		if (len < 0)	/* size_t not fitting an compat_ssize_t .. */
			goto out;
		tot_len += len;
		if (tot_len < tmp) /* maths overflow on the compat_ssize_t */
			goto out;
		vector->iov_base = compat_ptr(buf);
		vector->iov_len = (compat_size_t) len;
		uvector++;
		vector++;
	}
	if (tot_len == 0) {
		ret = 0;
		goto out;
	}

	ret = rw_verify_area(type, file, pos, tot_len);
	if (ret < 0)
		goto out;

	fnv = NULL;
	if (type == READ) {
		fn = file->f_op->read;
		fnv = file->f_op->readv;
	} else {
		fn = (io_fn_t)file->f_op->write;
		fnv = file->f_op->writev;
	}
	if (fnv) {
		ret = fnv(file, iov, nr_segs, pos);
		goto out;
	}

	/* Do it by hand, with file-ops */
	ret = 0;
	vector = iov;
	while (nr_segs > 0) {
		void __user * base;
		size_t len;
		ssize_t nr;

		base = vector->iov_base;
		len = vector->iov_len;
		vector++;
		nr_segs--;

		nr = fn(file, base, len, pos);

		if (nr < 0) {
			if (!ret) ret = nr;
			break;
		}
		ret += nr;
		if (nr != len)
			break;
	}
out:
	if (iov != iovstack)
		kfree(iov);
	if ((ret + (type == READ)) > 0) {
		struct dentry *dentry = file->f_dentry;
		if (type == READ)
			fsnotify_access(dentry);
		else
			fsnotify_modify(dentry);
	}
	return ret;
}

asmlinkage ssize_t
compat_sys_readv(unsigned long fd, const struct compat_iovec __user *vec, unsigned long vlen)
{
	struct file *file;
	ssize_t ret = -EBADF;

	file = fget(fd);
	if (!file)
		return -EBADF;

	if (!(file->f_mode & FMODE_READ))
		goto out;

	ret = -EINVAL;
	if (!file->f_op || (!file->f_op->readv && !file->f_op->read))
		goto out;

	ret = compat_do_readv_writev(READ, file, vec, vlen, &file->f_pos);

out:
	fput(file);
	return ret;
}

asmlinkage ssize_t
compat_sys_writev(unsigned long fd, const struct compat_iovec __user *vec, unsigned long vlen)
{
	struct file *file;
	ssize_t ret = -EBADF;

	file = fget(fd);
	if (!file)
		return -EBADF;
	if (!(file->f_mode & FMODE_WRITE))
		goto out;

	ret = -EINVAL;
	if (!file->f_op || (!file->f_op->writev && !file->f_op->write))
		goto out;

	ret = compat_do_readv_writev(WRITE, file, vec, vlen, &file->f_pos);

out:
	fput(file);
	return ret;
}

/*
 * Exactly like fs/open.c:sys_open(), except that it doesn't set the
 * O_LARGEFILE flag.
 */
asmlinkage long
compat_sys_open(const char __user *filename, int flags, int mode)
{
	return do_sys_open(filename, flags, mode);
}

/*
 * compat_count() counts the number of arguments/envelopes. It is basically
 * a copy of count() from fs/exec.c, except that it works with 32 bit argv
 * and envp pointers.
 */
static int compat_count(compat_uptr_t __user *argv, int max)
{
	int i = 0;

	if (argv != NULL) {
		for (;;) {
			compat_uptr_t p;

			if (get_user(p, argv))
				return -EFAULT;
			if (!p)
				break;
			argv++;
			if(++i > max)
				return -E2BIG;
		}
	}
	return i;
}

/*
 * compat_copy_strings() is basically a copy of copy_strings() from fs/exec.c
 * except that it works with 32 bit argv and envp pointers.
 */
static int compat_copy_strings(int argc, compat_uptr_t __user *argv,
				struct linux_binprm *bprm)
{
	struct page *kmapped_page = NULL;
	char *kaddr = NULL;
	int ret;

	while (argc-- > 0) {
		compat_uptr_t str;
		int len;
		unsigned long pos;

		if (get_user(str, argv+argc) ||
			!(len = strnlen_user(compat_ptr(str), bprm->p))) {
			ret = -EFAULT;
			goto out;
		}

		if (bprm->p < len)  {
			ret = -E2BIG;
			goto out;
		}

		bprm->p -= len;
		/* XXX: add architecture specific overflow check here. */
		pos = bprm->p;

		while (len > 0) {
			int i, new, err;
			int offset, bytes_to_copy;
			struct page *page;

			offset = pos % PAGE_SIZE;
			i = pos/PAGE_SIZE;
			page = bprm->page[i];
			new = 0;
			if (!page) {
				page = alloc_page(GFP_HIGHUSER);
				bprm->page[i] = page;
				if (!page) {
					ret = -ENOMEM;
					goto out;
				}
				new = 1;
			}

			if (page != kmapped_page) {
				if (kmapped_page)
					kunmap(kmapped_page);
				kmapped_page = page;
				kaddr = kmap(kmapped_page);
			}
			if (new && offset)
				memset(kaddr, 0, offset);
			bytes_to_copy = PAGE_SIZE - offset;
			if (bytes_to_copy > len) {
				bytes_to_copy = len;
				if (new)
					memset(kaddr+offset+len, 0,
						PAGE_SIZE-offset-len);
			}
			err = copy_from_user(kaddr+offset, compat_ptr(str),
						bytes_to_copy);
			if (err) {
				ret = -EFAULT;
				goto out;
			}

			pos += bytes_to_copy;
			str += bytes_to_copy;
			len -= bytes_to_copy;
		}
	}
	ret = 0;
out:
	if (kmapped_page)
		kunmap(kmapped_page);
	return ret;
}

#ifdef CONFIG_MMU

#define free_arg_pages(bprm) do { } while (0)

#else

static inline void free_arg_pages(struct linux_binprm *bprm)
{
	int i;

	for (i = 0; i < MAX_ARG_PAGES; i++) {
		if (bprm->page[i])
			__free_page(bprm->page[i]);
		bprm->page[i] = NULL;
	}
}

#endif /* CONFIG_MMU */

/*
 * compat_do_execve() is mostly a copy of do_execve(), with the exception
 * that it processes 32 bit argv and envp pointers.
 */
int compat_do_execve(char * filename,
	compat_uptr_t __user *argv,
	compat_uptr_t __user *envp,
	struct pt_regs * regs)
{
	struct linux_binprm *bprm;
	struct file *file;
	int retval;
	int i;

	retval = -ENOMEM;
	bprm = kmalloc(sizeof(*bprm), GFP_KERNEL);
	if (!bprm)
		goto out_ret;
	memset(bprm, 0, sizeof(*bprm));

	file = open_exec(filename);
	retval = PTR_ERR(file);
	if (IS_ERR(file))
		goto out_kfree;

	sched_exec();

	bprm->p = PAGE_SIZE*MAX_ARG_PAGES-sizeof(void *);
	bprm->file = file;
	bprm->filename = filename;
	bprm->interp = filename;
	bprm->mm = mm_alloc();
	retval = -ENOMEM;
	if (!bprm->mm)
		goto out_file;

	retval = init_new_context(current, bprm->mm);
	if (retval < 0)
		goto out_mm;

	bprm->argc = compat_count(argv, bprm->p / sizeof(compat_uptr_t));
	if ((retval = bprm->argc) < 0)
		goto out_mm;

	bprm->envc = compat_count(envp, bprm->p / sizeof(compat_uptr_t));
	if ((retval = bprm->envc) < 0)
		goto out_mm;

	retval = security_bprm_alloc(bprm);
	if (retval)
		goto out;

	retval = prepare_binprm(bprm);
	if (retval < 0)
		goto out;

	retval = copy_strings_kernel(1, &bprm->filename, bprm);
	if (retval < 0)
		goto out;

	bprm->exec = bprm->p;
	retval = compat_copy_strings(bprm->envc, envp, bprm);
	if (retval < 0)
		goto out;

	retval = compat_copy_strings(bprm->argc, argv, bprm);
	if (retval < 0)
		goto out;

	retval = search_binary_handler(bprm, regs);
	if (retval >= 0) {
		free_arg_pages(bprm);

		/* execve success */
		security_bprm_free(bprm);
		acct_update_integrals(current);
		kfree(bprm);
		return retval;
	}

out:
	/* Something went wrong, return the inode and free the argument pages*/
	for (i = 0 ; i < MAX_ARG_PAGES ; i++) {
		struct page * page = bprm->page[i];
		if (page)
			__free_page(page);
	}

	if (bprm->security)
		security_bprm_free(bprm);

out_mm:
	if (bprm->mm)
		mmdrop(bprm->mm);

out_file:
	if (bprm->file) {
		allow_write_access(bprm->file);
		fput(bprm->file);
	}

out_kfree:
	kfree(bprm);

out_ret:
	return retval;
}

#define __COMPAT_NFDBITS       (8 * sizeof(compat_ulong_t))

#define ROUND_UP(x,y) (((x)+(y)-1)/(y))

/*
 * Ooo, nasty.  We need here to frob 32-bit unsigned longs to
 * 64-bit unsigned longs.
 */
static
int compat_get_fd_set(unsigned long nr, compat_ulong_t __user *ufdset,
			unsigned long *fdset)
{
	nr = ROUND_UP(nr, __COMPAT_NFDBITS);
	if (ufdset) {
		unsigned long odd;

		if (!access_ok(VERIFY_WRITE, ufdset, nr*sizeof(compat_ulong_t)))
			return -EFAULT;

		odd = nr & 1UL;
		nr &= ~1UL;
		while (nr) {
			unsigned long h, l;
			__get_user(l, ufdset);
			__get_user(h, ufdset+1);
			ufdset += 2;
			*fdset++ = h << 32 | l;
			nr -= 2;
		}
		if (odd)
			__get_user(*fdset, ufdset);
	} else {
		/* Tricky, must clear full unsigned long in the
		 * kernel fdset at the end, this makes sure that
		 * actually happens.
		 */
		memset(fdset, 0, ((nr + 1) & ~1)*sizeof(compat_ulong_t));
	}
	return 0;
}

static
void compat_set_fd_set(unsigned long nr, compat_ulong_t __user *ufdset,
			unsigned long *fdset)
{
	unsigned long odd;
	nr = ROUND_UP(nr, __COMPAT_NFDBITS);

	if (!ufdset)
		return;

	odd = nr & 1UL;
	nr &= ~1UL;
	while (nr) {
		unsigned long h, l;
		l = *fdset++;
		h = l >> 32;
		__put_user(l, ufdset);
		__put_user(h, ufdset+1);
		ufdset += 2;
		nr -= 2;
	}
	if (odd)
		__put_user(*fdset, ufdset);
}


/*
 * This is a virtual copy of sys_select from fs/select.c and probably
 * should be compared to it from time to time
 */
static void *select_bits_alloc(int size)
{
	return kmalloc(6 * size, GFP_KERNEL);
}

static void select_bits_free(void *bits, int size)
{
	kfree(bits);
}

/*
 * We can actually return ERESTARTSYS instead of EINTR, but I'd
 * like to be certain this leads to no problems. So I return
 * EINTR just for safety.
 *
 * Update: ERESTARTSYS breaks at least the xview clock binary, so
 * I'm trying ERESTARTNOHAND which restart only when you want to.
 */
#define MAX_SELECT_SECONDS \
	((unsigned long) (MAX_SCHEDULE_TIMEOUT / HZ)-1)

asmlinkage long
compat_sys_select(int n, compat_ulong_t __user *inp, compat_ulong_t __user *outp,
		compat_ulong_t __user *exp, struct compat_timeval __user *tvp)
{
	fd_set_bits fds;
	char *bits;
	long timeout;
	int size, max_fdset, ret = -EINVAL;
	struct fdtable *fdt;

	timeout = MAX_SCHEDULE_TIMEOUT;
	if (tvp) {
		time_t sec, usec;

		if (!access_ok(VERIFY_READ, tvp, sizeof(*tvp))
		    || __get_user(sec, &tvp->tv_sec)
		    || __get_user(usec, &tvp->tv_usec)) {
			ret = -EFAULT;
			goto out_nofds;
		}

		if (sec < 0 || usec < 0)
			goto out_nofds;

		if ((unsigned long) sec < MAX_SELECT_SECONDS) {
			timeout = ROUND_UP(usec, 1000000/HZ);
			timeout += sec * (unsigned long) HZ;
		}
	}

	if (n < 0)
		goto out_nofds;

	/* max_fdset can increase, so grab it once to avoid race */
	rcu_read_lock();
	fdt = files_fdtable(current->files);
	max_fdset = fdt->max_fdset;
	rcu_read_unlock();
	if (n > max_fdset)
		n = max_fdset;

	/*
	 * We need 6 bitmaps (in/out/ex for both incoming and outgoing),
	 * since we used fdset we need to allocate memory in units of
	 * long-words.
	 */
	ret = -ENOMEM;
	size = FDS_BYTES(n);
	bits = select_bits_alloc(size);
	if (!bits)
		goto out_nofds;
	fds.in      = (unsigned long *)  bits;
	fds.out     = (unsigned long *) (bits +   size);
	fds.ex      = (unsigned long *) (bits + 2*size);
	fds.res_in  = (unsigned long *) (bits + 3*size);
	fds.res_out = (unsigned long *) (bits + 4*size);
	fds.res_ex  = (unsigned long *) (bits + 5*size);

	if ((ret = compat_get_fd_set(n, inp, fds.in)) ||
	    (ret = compat_get_fd_set(n, outp, fds.out)) ||
	    (ret = compat_get_fd_set(n, exp, fds.ex)))
		goto out;
	zero_fd_set(n, fds.res_in);
	zero_fd_set(n, fds.res_out);
	zero_fd_set(n, fds.res_ex);

	ret = do_select(n, &fds, &timeout);

	if (tvp && !(current->personality & STICKY_TIMEOUTS)) {
		time_t sec = 0, usec = 0;
		if (timeout) {
			sec = timeout / HZ;
			usec = timeout % HZ;
			usec *= (1000000/HZ);
		}
		if (put_user(sec, &tvp->tv_sec) ||
		    put_user(usec, &tvp->tv_usec))
			ret = -EFAULT;
	}

	if (ret < 0)
		goto out;
	if (!ret) {
		ret = -ERESTARTNOHAND;
		if (signal_pending(current))
			goto out;
		ret = 0;
	}

	compat_set_fd_set(n, inp, fds.res_in);
	compat_set_fd_set(n, outp, fds.res_out);
	compat_set_fd_set(n, exp, fds.res_ex);

out:
	select_bits_free(bits, size);
out_nofds:
	return ret;
}

#if defined(CONFIG_NFSD) || defined(CONFIG_NFSD_MODULE)
/* Stuff for NFS server syscalls... */
struct compat_nfsctl_svc {
	u16			svc32_port;
	s32			svc32_nthreads;
};

struct compat_nfsctl_client {
	s8			cl32_ident[NFSCLNT_IDMAX+1];
	s32			cl32_naddr;
	struct in_addr		cl32_addrlist[NFSCLNT_ADDRMAX];
	s32			cl32_fhkeytype;
	s32			cl32_fhkeylen;
	u8			cl32_fhkey[NFSCLNT_KEYMAX];
};

struct compat_nfsctl_export {
	char		ex32_client[NFSCLNT_IDMAX+1];
	char		ex32_path[NFS_MAXPATHLEN+1];
	compat_dev_t	ex32_dev;
	compat_ino_t	ex32_ino;
	compat_int_t	ex32_flags;
	__compat_uid_t	ex32_anon_uid;
	__compat_gid_t	ex32_anon_gid;
};

struct compat_nfsctl_fdparm {
	struct sockaddr		gd32_addr;
	s8			gd32_path[NFS_MAXPATHLEN+1];
	compat_int_t		gd32_version;
};

struct compat_nfsctl_fsparm {
	struct sockaddr		gd32_addr;
	s8			gd32_path[NFS_MAXPATHLEN+1];
	compat_int_t		gd32_maxlen;
};

struct compat_nfsctl_arg {
	compat_int_t		ca32_version;	/* safeguard */
	union {
		struct compat_nfsctl_svc	u32_svc;
		struct compat_nfsctl_client	u32_client;
		struct compat_nfsctl_export	u32_export;
		struct compat_nfsctl_fdparm	u32_getfd;
		struct compat_nfsctl_fsparm	u32_getfs;
	} u;
#define ca32_svc	u.u32_svc
#define ca32_client	u.u32_client
#define ca32_export	u.u32_export
#define ca32_getfd	u.u32_getfd
#define ca32_getfs	u.u32_getfs
};

union compat_nfsctl_res {
	__u8			cr32_getfh[NFS_FHSIZE];
	struct knfsd_fh		cr32_getfs;
};

static int compat_nfs_svc_trans(struct nfsctl_arg *karg, struct compat_nfsctl_arg __user *arg)
{
	int err;

	err = access_ok(VERIFY_READ, &arg->ca32_svc, sizeof(arg->ca32_svc));
	err |= get_user(karg->ca_version, &arg->ca32_version);
	err |= __get_user(karg->ca_svc.svc_port, &arg->ca32_svc.svc32_port);
	err |= __get_user(karg->ca_svc.svc_nthreads, &arg->ca32_svc.svc32_nthreads);
	return (err) ? -EFAULT : 0;
}

static int compat_nfs_clnt_trans(struct nfsctl_arg *karg, struct compat_nfsctl_arg __user *arg)
{
	int err;

	err = access_ok(VERIFY_READ, &arg->ca32_client, sizeof(arg->ca32_client));
	err |= get_user(karg->ca_version, &arg->ca32_version);
	err |= __copy_from_user(&karg->ca_client.cl_ident[0],
			  &arg->ca32_client.cl32_ident[0],
			  NFSCLNT_IDMAX);
	err |= __get_user(karg->ca_client.cl_naddr, &arg->ca32_client.cl32_naddr);
	err |= __copy_from_user(&karg->ca_client.cl_addrlist[0],
			  &arg->ca32_client.cl32_addrlist[0],
			  (sizeof(struct in_addr) * NFSCLNT_ADDRMAX));
	err |= __get_user(karg->ca_client.cl_fhkeytype,
		      &arg->ca32_client.cl32_fhkeytype);
	err |= __get_user(karg->ca_client.cl_fhkeylen,
		      &arg->ca32_client.cl32_fhkeylen);
	err |= __copy_from_user(&karg->ca_client.cl_fhkey[0],
			  &arg->ca32_client.cl32_fhkey[0],
			  NFSCLNT_KEYMAX);

	return (err) ? -EFAULT : 0;
}

static int compat_nfs_exp_trans(struct nfsctl_arg *karg, struct compat_nfsctl_arg __user *arg)
{
	int err;

	err = access_ok(VERIFY_READ, &arg->ca32_export, sizeof(arg->ca32_export));
	err |= get_user(karg->ca_version, &arg->ca32_version);
	err |= __copy_from_user(&karg->ca_export.ex_client[0],
			  &arg->ca32_export.ex32_client[0],
			  NFSCLNT_IDMAX);
	err |= __copy_from_user(&karg->ca_export.ex_path[0],
			  &arg->ca32_export.ex32_path[0],
			  NFS_MAXPATHLEN);
	err |= __get_user(karg->ca_export.ex_dev,
		      &arg->ca32_export.ex32_dev);
	err |= __get_user(karg->ca_export.ex_ino,
		      &arg->ca32_export.ex32_ino);
	err |= __get_user(karg->ca_export.ex_flags,
		      &arg->ca32_export.ex32_flags);
	err |= __get_user(karg->ca_export.ex_anon_uid,
		      &arg->ca32_export.ex32_anon_uid);
	err |= __get_user(karg->ca_export.ex_anon_gid,
		      &arg->ca32_export.ex32_anon_gid);
	SET_UID(karg->ca_export.ex_anon_uid, karg->ca_export.ex_anon_uid);
	SET_GID(karg->ca_export.ex_anon_gid, karg->ca_export.ex_anon_gid);

	return (err) ? -EFAULT : 0;
}

static int compat_nfs_getfd_trans(struct nfsctl_arg *karg, struct compat_nfsctl_arg __user *arg)
{
	int err;

	err = access_ok(VERIFY_READ, &arg->ca32_getfd, sizeof(arg->ca32_getfd));
	err |= get_user(karg->ca_version, &arg->ca32_version);
	err |= __copy_from_user(&karg->ca_getfd.gd_addr,
			  &arg->ca32_getfd.gd32_addr,
			  (sizeof(struct sockaddr)));
	err |= __copy_from_user(&karg->ca_getfd.gd_path,
			  &arg->ca32_getfd.gd32_path,
			  (NFS_MAXPATHLEN+1));
	err |= __get_user(karg->ca_getfd.gd_version,
		      &arg->ca32_getfd.gd32_version);

	return (err) ? -EFAULT : 0;
}

static int compat_nfs_getfs_trans(struct nfsctl_arg *karg, struct compat_nfsctl_arg __user *arg)
{
	int err;

	err = access_ok(VERIFY_READ, &arg->ca32_getfs, sizeof(arg->ca32_getfs));
	err |= get_user(karg->ca_version, &arg->ca32_version);
	err |= __copy_from_user(&karg->ca_getfs.gd_addr,
			  &arg->ca32_getfs.gd32_addr,
			  (sizeof(struct sockaddr)));
	err |= __copy_from_user(&karg->ca_getfs.gd_path,
			  &arg->ca32_getfs.gd32_path,
			  (NFS_MAXPATHLEN+1));
	err |= __get_user(karg->ca_getfs.gd_maxlen,
		      &arg->ca32_getfs.gd32_maxlen);

	return (err) ? -EFAULT : 0;
}

/* This really doesn't need translations, we are only passing
 * back a union which contains opaque nfs file handle data.
 */
static int compat_nfs_getfh_res_trans(union nfsctl_res *kres, union compat_nfsctl_res __user *res)
{
	int err;

	err = copy_to_user(res, kres, sizeof(*res));

	return (err) ? -EFAULT : 0;
}

asmlinkage long compat_sys_nfsservctl(int cmd, struct compat_nfsctl_arg __user *arg,
					union compat_nfsctl_res __user *res)
{
	struct nfsctl_arg *karg;
	union nfsctl_res *kres;
	mm_segment_t oldfs;
	int err;

	karg = kmalloc(sizeof(*karg), GFP_USER);
	kres = kmalloc(sizeof(*kres), GFP_USER);
	if(!karg || !kres) {
		err = -ENOMEM;
		goto done;
	}

	switch(cmd) {
	case NFSCTL_SVC:
		err = compat_nfs_svc_trans(karg, arg);
		break;

	case NFSCTL_ADDCLIENT:
		err = compat_nfs_clnt_trans(karg, arg);
		break;

	case NFSCTL_DELCLIENT:
		err = compat_nfs_clnt_trans(karg, arg);
		break;

	case NFSCTL_EXPORT:
	case NFSCTL_UNEXPORT:
		err = compat_nfs_exp_trans(karg, arg);
		break;

	case NFSCTL_GETFD:
		err = compat_nfs_getfd_trans(karg, arg);
		break;

	case NFSCTL_GETFS:
		err = compat_nfs_getfs_trans(karg, arg);
		break;

	default:
		err = -EINVAL;
		goto done;
	}

	oldfs = get_fs();
	set_fs(KERNEL_DS);
	/* The __user pointer casts are valid because of the set_fs() */
	err = sys_nfsservctl(cmd, (void __user *) karg, (void __user *) kres);
	set_fs(oldfs);

	if (err)
		goto done;

	if((cmd == NFSCTL_GETFD) ||
	   (cmd == NFSCTL_GETFS))
		err = compat_nfs_getfh_res_trans(kres, res);

done:
	kfree(karg);
	kfree(kres);
	return err;
}
#else /* !NFSD */
long asmlinkage compat_sys_nfsservctl(int cmd, void *notused, void *notused2)
{
	return sys_ni_syscall();
}
#endif
