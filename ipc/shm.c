// SPDX-License-Identifier: GPL-2.0
/*
 * linux/ipc/shm.c
 * Copyright (C) 1992, 1993 Krishna Balasubramanian
 *	 Many improvements/fixes by Bruno Haible.
 * Replaced `struct shm_desc' by `struct vm_area_struct', July 1994.
 * Fixed the shm swap deallocation (shm_unuse()), August 1998 Andrea Arcangeli.
 *
 * /proc/sysvipc/shm support (c) 1999 Dragos Acostachioaie <dragos@iname.com>
 * BIGMEM support, Andrea Arcangeli <andrea@suse.de>
 * SMP thread shm, Jean-Luc Boyard <jean-luc.boyard@siemens.fr>
 * HIGHMEM support, Ingo Molnar <mingo@redhat.com>
 * Make shmmax, shmall, shmmni sysctl'able, Christoph Rohland <cr@sap.com>
 * Shared /dev/zero support, Kanoj Sarcar <kanoj@sgi.com>
 * Move the mm functionality over to mm/shmem.c, Christoph Rohland <cr@sap.com>
 *
 * support for audit of ipc object properties and permission changes
 * Dustin Kirkland <dustin.kirkland@us.ibm.com>
 *
 * namespaces support
 * OpenVZ, SWsoft Inc.
 * Pavel Emelianov <xemul@openvz.org>
 *
 * Better ipc lock (kern_ipc_perm.lock) handling
 * Davidlohr Bueso <davidlohr.bueso@hp.com>, June 2013.
 */

#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/shm.h>
#include <linux/init.h>
#include <linux/file.h>
#include <linux/mman.h>
#include <linux/shmem_fs.h>
#include <linux/security.h>
#include <linux/syscalls.h>
#include <linux/audit.h>
#include <linux/capability.h>
#include <linux/ptrace.h>
#include <linux/seq_file.h>
#include <linux/rwsem.h>
#include <linux/nsproxy.h>
#include <linux/mount.h>
#include <linux/ipc_namespace.h>

#include <linux/uaccess.h>

#include "util.h"

struct shm_file_data {
	int id;
	struct ipc_namespace *ns;
	struct file *file;
	const struct vm_operations_struct *vm_ops;
};

#define shm_file_data(file) (*((struct shm_file_data **)&(file)->private_data))

static const struct file_operations shm_file_operations;
static const struct vm_operations_struct shm_vm_ops;

#define shm_ids(ns)	((ns)->ids[IPC_SHM_IDS])

#define shm_unlock(shp)			\
	ipc_unlock(&(shp)->shm_perm)

static int newseg(struct ipc_namespace *, struct ipc_params *);
static void shm_open(struct vm_area_struct *vma);
static void shm_close(struct vm_area_struct *vma);
static void shm_destroy(struct ipc_namespace *ns, struct shmid_kernel *shp);
#ifdef CONFIG_PROC_FS
static int sysvipc_shm_proc_show(struct seq_file *s, void *it);
#endif

int shm_init_ns(struct ipc_namespace *ns)
{
	ns->shm_ctlmax = SHMMAX;
	ns->shm_ctlall = SHMALL;
	ns->shm_ctlmni = SHMMNI;
	ns->shm_rmid_forced = 0;
	ns->shm_tot = 0;
	return ipc_init_ids(&shm_ids(ns));
}

/*
 * Called with shm_ids.rwsem (writer) and the shp structure locked.
 * Only shm_ids.rwsem remains locked on exit.
 */
static void do_shm_rmid(struct ipc_namespace *ns, struct kern_ipc_perm *ipcp)
{
	struct shmid_kernel *shp;

	shp = container_of(ipcp, struct shmid_kernel, shm_perm);

	if (shp->shm_nattch) {
		shp->shm_perm.mode |= SHM_DEST;
		/* Do not find it any more */
		ipc_set_key_private(&shm_ids(ns), &shp->shm_perm);
		shm_unlock(shp);
	} else
		shm_destroy(ns, shp);
}

#ifdef CONFIG_IPC_NS
void shm_exit_ns(struct ipc_namespace *ns)
{
	free_ipcs(ns, &shm_ids(ns), do_shm_rmid);
	idr_destroy(&ns->ids[IPC_SHM_IDS].ipcs_idr);
	rhashtable_destroy(&ns->ids[IPC_SHM_IDS].key_ht);
}
#endif

static int __init ipc_ns_init(void)
{
	const int err = shm_init_ns(&init_ipc_ns);
	WARN(err, "ipc: sysv shm_init_ns failed: %d\n", err);
	return err;
}

pure_initcall(ipc_ns_init);

void __init shm_init(void)
{
	ipc_init_proc_interface("sysvipc/shm",
#if BITS_PER_LONG <= 32
				"       key      shmid perms       size  cpid  lpid nattch   uid   gid  cuid  cgid      atime      dtime      ctime        rss       swap\n",
#else
				"       key      shmid perms                  size  cpid  lpid nattch   uid   gid  cuid  cgid      atime      dtime      ctime                   rss                  swap\n",
#endif
				IPC_SHM_IDS, sysvipc_shm_proc_show);
}

static inline struct shmid_kernel *shm_obtain_object(struct ipc_namespace *ns, int id)
{
	struct kern_ipc_perm *ipcp = ipc_obtain_object_idr(&shm_ids(ns), id);

	if (IS_ERR(ipcp))
		return ERR_CAST(ipcp);

	return container_of(ipcp, struct shmid_kernel, shm_perm);
}

static inline struct shmid_kernel *shm_obtain_object_check(struct ipc_namespace *ns, int id)
{
	struct kern_ipc_perm *ipcp = ipc_obtain_object_check(&shm_ids(ns), id);

	if (IS_ERR(ipcp))
		return ERR_CAST(ipcp);

	return container_of(ipcp, struct shmid_kernel, shm_perm);
}

/*
 * shm_lock_(check_) routines are called in the paths where the rwsem
 * is not necessarily held.
 */
static inline struct shmid_kernel *shm_lock(struct ipc_namespace *ns, int id)
{
	struct kern_ipc_perm *ipcp = ipc_lock(&shm_ids(ns), id);

	/*
	 * Callers of shm_lock() must validate the status of the returned ipc
	 * object pointer (as returned by ipc_lock()), and error out as
	 * appropriate.
	 */
	if (IS_ERR(ipcp))
		return (void *)ipcp;
	return container_of(ipcp, struct shmid_kernel, shm_perm);
}

static inline void shm_lock_by_ptr(struct shmid_kernel *ipcp)
{
	rcu_read_lock();
	ipc_lock_object(&ipcp->shm_perm);
}

static void shm_rcu_free(struct rcu_head *head)
{
	struct kern_ipc_perm *ptr = container_of(head, struct kern_ipc_perm,
							rcu);
	struct shmid_kernel *shp = container_of(ptr, struct shmid_kernel,
							shm_perm);
	security_shm_free(shp);
	kvfree(shp);
}

static inline void shm_rmid(struct ipc_namespace *ns, struct shmid_kernel *s)
{
	list_del(&s->shm_clist);
	ipc_rmid(&shm_ids(ns), &s->shm_perm);
}


static int __shm_open(struct vm_area_struct *vma)
{
	struct file *file = vma->vm_file;
	struct shm_file_data *sfd = shm_file_data(file);
	struct shmid_kernel *shp;

	shp = shm_lock(sfd->ns, sfd->id);

	if (IS_ERR(shp))
		return PTR_ERR(shp);

	shp->shm_atim = ktime_get_real_seconds();
	shp->shm_lprid = task_tgid_vnr(current);
	shp->shm_nattch++;
	shm_unlock(shp);
	return 0;
}

/* This is called by fork, once for every shm attach. */
static void shm_open(struct vm_area_struct *vma)
{
	int err = __shm_open(vma);
	/*
	 * We raced in the idr lookup or with shm_destroy().
	 * Either way, the ID is busted.
	 */
	WARN_ON_ONCE(err);
}

/*
 * shm_destroy - free the struct shmid_kernel
 *
 * @ns: namespace
 * @shp: struct to free
 *
 * It has to be called with shp and shm_ids.rwsem (writer) locked,
 * but returns with shp unlocked and freed.
 */
static void shm_destroy(struct ipc_namespace *ns, struct shmid_kernel *shp)
{
	struct file *shm_file;

	shm_file = shp->shm_file;
	shp->shm_file = NULL;
	ns->shm_tot -= (shp->shm_segsz + PAGE_SIZE - 1) >> PAGE_SHIFT;
	shm_rmid(ns, shp);
	shm_unlock(shp);
	if (!is_file_hugepages(shm_file))
		shmem_lock(shm_file, 0, shp->mlock_user);
	else if (shp->mlock_user)
		user_shm_unlock(i_size_read(file_inode(shm_file)),
				shp->mlock_user);
	fput(shm_file);
	ipc_rcu_putref(&shp->shm_perm, shm_rcu_free);
}

/*
 * shm_may_destroy - identifies whether shm segment should be destroyed now
 *
 * Returns true if and only if there are no active users of the segment and
 * one of the following is true:
 *
 * 1) shmctl(id, IPC_RMID, NULL) was called for this shp
 *
 * 2) sysctl kernel.shm_rmid_forced is set to 1.
 */
static bool shm_may_destroy(struct ipc_namespace *ns, struct shmid_kernel *shp)
{
	return (shp->shm_nattch == 0) &&
	       (ns->shm_rmid_forced ||
		(shp->shm_perm.mode & SHM_DEST));
}

/*
 * remove the attach descriptor vma.
 * free memory for segment if it is marked destroyed.
 * The descriptor has already been removed from the current->mm->mmap list
 * and will later be kfree()d.
 */
static void shm_close(struct vm_area_struct *vma)
{
	struct file *file = vma->vm_file;
	struct shm_file_data *sfd = shm_file_data(file);
	struct shmid_kernel *shp;
	struct ipc_namespace *ns = sfd->ns;

	down_write(&shm_ids(ns).rwsem);
	/* remove from the list of attaches of the shm segment */
	shp = shm_lock(ns, sfd->id);

	/*
	 * We raced in the idr lookup or with shm_destroy().
	 * Either way, the ID is busted.
	 */
	if (WARN_ON_ONCE(IS_ERR(shp)))
		goto done; /* no-op */

	shp->shm_lprid = task_tgid_vnr(current);
	shp->shm_dtim = ktime_get_real_seconds();
	shp->shm_nattch--;
	if (shm_may_destroy(ns, shp))
		shm_destroy(ns, shp);
	else
		shm_unlock(shp);
done:
	up_write(&shm_ids(ns).rwsem);
}

/* Called with ns->shm_ids(ns).rwsem locked */
static int shm_try_destroy_orphaned(int id, void *p, void *data)
{
	struct ipc_namespace *ns = data;
	struct kern_ipc_perm *ipcp = p;
	struct shmid_kernel *shp = container_of(ipcp, struct shmid_kernel, shm_perm);

	/*
	 * We want to destroy segments without users and with already
	 * exit'ed originating process.
	 *
	 * As shp->* are changed under rwsem, it's safe to skip shp locking.
	 */
	if (shp->shm_creator != NULL)
		return 0;

	if (shm_may_destroy(ns, shp)) {
		shm_lock_by_ptr(shp);
		shm_destroy(ns, shp);
	}
	return 0;
}

void shm_destroy_orphaned(struct ipc_namespace *ns)
{
	down_write(&shm_ids(ns).rwsem);
	if (shm_ids(ns).in_use)
		idr_for_each(&shm_ids(ns).ipcs_idr, &shm_try_destroy_orphaned, ns);
	up_write(&shm_ids(ns).rwsem);
}

/* Locking assumes this will only be called with task == current */
void exit_shm(struct task_struct *task)
{
	struct ipc_namespace *ns = task->nsproxy->ipc_ns;
	struct shmid_kernel *shp, *n;

	if (list_empty(&task->sysvshm.shm_clist))
		return;

	/*
	 * If kernel.shm_rmid_forced is not set then only keep track of
	 * which shmids are orphaned, so that a later set of the sysctl
	 * can clean them up.
	 */
	if (!ns->shm_rmid_forced) {
		down_read(&shm_ids(ns).rwsem);
		list_for_each_entry(shp, &task->sysvshm.shm_clist, shm_clist)
			shp->shm_creator = NULL;
		/*
		 * Only under read lock but we are only called on current
		 * so no entry on the list will be shared.
		 */
		list_del(&task->sysvshm.shm_clist);
		up_read(&shm_ids(ns).rwsem);
		return;
	}

	/*
	 * Destroy all already created segments, that were not yet mapped,
	 * and mark any mapped as orphan to cover the sysctl toggling.
	 * Destroy is skipped if shm_may_destroy() returns false.
	 */
	down_write(&shm_ids(ns).rwsem);
	list_for_each_entry_safe(shp, n, &task->sysvshm.shm_clist, shm_clist) {
		shp->shm_creator = NULL;

		if (shm_may_destroy(ns, shp)) {
			shm_lock_by_ptr(shp);
			shm_destroy(ns, shp);
		}
	}

	/* Remove the list head from any segments still attached. */
	list_del(&task->sysvshm.shm_clist);
	up_write(&shm_ids(ns).rwsem);
}

static int shm_fault(struct vm_fault *vmf)
{
	struct file *file = vmf->vma->vm_file;
	struct shm_file_data *sfd = shm_file_data(file);

	return sfd->vm_ops->fault(vmf);
}

static int shm_split(struct vm_area_struct *vma, unsigned long addr)
{
	struct file *file = vma->vm_file;
	struct shm_file_data *sfd = shm_file_data(file);

	if (sfd->vm_ops && sfd->vm_ops->split)
		return sfd->vm_ops->split(vma, addr);

	return 0;
}

#ifdef CONFIG_NUMA
static int shm_set_policy(struct vm_area_struct *vma, struct mempolicy *new)
{
	struct file *file = vma->vm_file;
	struct shm_file_data *sfd = shm_file_data(file);
	int err = 0;

	if (sfd->vm_ops->set_policy)
		err = sfd->vm_ops->set_policy(vma, new);
	return err;
}

static struct mempolicy *shm_get_policy(struct vm_area_struct *vma,
					unsigned long addr)
{
	struct file *file = vma->vm_file;
	struct shm_file_data *sfd = shm_file_data(file);
	struct mempolicy *pol = NULL;

	if (sfd->vm_ops->get_policy)
		pol = sfd->vm_ops->get_policy(vma, addr);
	else if (vma->vm_policy)
		pol = vma->vm_policy;

	return pol;
}
#endif

static int shm_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct shm_file_data *sfd = shm_file_data(file);
	int ret;

	/*
	 * In case of remap_file_pages() emulation, the file can represent
	 * removed IPC ID: propogate shm_lock() error to caller.
	 */
	ret = __shm_open(vma);
	if (ret)
		return ret;

	ret = call_mmap(sfd->file, vma);
	if (ret) {
		shm_close(vma);
		return ret;
	}
	sfd->vm_ops = vma->vm_ops;
#ifdef CONFIG_MMU
	WARN_ON(!sfd->vm_ops->fault);
#endif
	vma->vm_ops = &shm_vm_ops;
	return 0;
}

static int shm_release(struct inode *ino, struct file *file)
{
	struct shm_file_data *sfd = shm_file_data(file);

	put_ipc_ns(sfd->ns);
	shm_file_data(file) = NULL;
	kfree(sfd);
	return 0;
}

static int shm_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	struct shm_file_data *sfd = shm_file_data(file);

	if (!sfd->file->f_op->fsync)
		return -EINVAL;
	return sfd->file->f_op->fsync(sfd->file, start, end, datasync);
}

static long shm_fallocate(struct file *file, int mode, loff_t offset,
			  loff_t len)
{
	struct shm_file_data *sfd = shm_file_data(file);

	if (!sfd->file->f_op->fallocate)
		return -EOPNOTSUPP;
	return sfd->file->f_op->fallocate(file, mode, offset, len);
}

static unsigned long shm_get_unmapped_area(struct file *file,
	unsigned long addr, unsigned long len, unsigned long pgoff,
	unsigned long flags)
{
	struct shm_file_data *sfd = shm_file_data(file);

	return sfd->file->f_op->get_unmapped_area(sfd->file, addr, len,
						pgoff, flags);
}

static const struct file_operations shm_file_operations = {
	.mmap		= shm_mmap,
	.fsync		= shm_fsync,
	.release	= shm_release,
	.get_unmapped_area	= shm_get_unmapped_area,
	.llseek		= noop_llseek,
	.fallocate	= shm_fallocate,
};

/*
 * shm_file_operations_huge is now identical to shm_file_operations,
 * but we keep it distinct for the sake of is_file_shm_hugepages().
 */
static const struct file_operations shm_file_operations_huge = {
	.mmap		= shm_mmap,
	.fsync		= shm_fsync,
	.release	= shm_release,
	.get_unmapped_area	= shm_get_unmapped_area,
	.llseek		= noop_llseek,
	.fallocate	= shm_fallocate,
};

bool is_file_shm_hugepages(struct file *file)
{
	return file->f_op == &shm_file_operations_huge;
}

static const struct vm_operations_struct shm_vm_ops = {
	.open	= shm_open,	/* callback for a new vm-area open */
	.close	= shm_close,	/* callback for when the vm-area is released */
	.fault	= shm_fault,
	.split	= shm_split,
#if defined(CONFIG_NUMA)
	.set_policy = shm_set_policy,
	.get_policy = shm_get_policy,
#endif
};

/**
 * newseg - Create a new shared memory segment
 * @ns: namespace
 * @params: ptr to the structure that contains key, size and shmflg
 *
 * Called with shm_ids.rwsem held as a writer.
 */
static int newseg(struct ipc_namespace *ns, struct ipc_params *params)
{
	key_t key = params->key;
	int shmflg = params->flg;
	size_t size = params->u.size;
	int error;
	struct shmid_kernel *shp;
	size_t numpages = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	struct file *file;
	char name[13];
	vm_flags_t acctflag = 0;

	if (size < SHMMIN || size > ns->shm_ctlmax)
		return -EINVAL;

	if (numpages << PAGE_SHIFT < size)
		return -ENOSPC;

	if (ns->shm_tot + numpages < ns->shm_tot ||
			ns->shm_tot + numpages > ns->shm_ctlall)
		return -ENOSPC;

	shp = kvmalloc(sizeof(*shp), GFP_KERNEL);
	if (unlikely(!shp))
		return -ENOMEM;

	shp->shm_perm.key = key;
	shp->shm_perm.mode = (shmflg & S_IRWXUGO);
	shp->mlock_user = NULL;

	shp->shm_perm.security = NULL;
	error = security_shm_alloc(shp);
	if (error) {
		kvfree(shp);
		return error;
	}

	sprintf(name, "SYSV%08x", key);
	if (shmflg & SHM_HUGETLB) {
		struct hstate *hs;
		size_t hugesize;

		hs = hstate_sizelog((shmflg >> SHM_HUGE_SHIFT) & SHM_HUGE_MASK);
		if (!hs) {
			error = -EINVAL;
			goto no_file;
		}
		hugesize = ALIGN(size, huge_page_size(hs));

		/* hugetlb_file_setup applies strict accounting */
		if (shmflg & SHM_NORESERVE)
			acctflag = VM_NORESERVE;
		file = hugetlb_file_setup(name, hugesize, acctflag,
				  &shp->mlock_user, HUGETLB_SHMFS_INODE,
				(shmflg >> SHM_HUGE_SHIFT) & SHM_HUGE_MASK);
	} else {
		/*
		 * Do not allow no accounting for OVERCOMMIT_NEVER, even
		 * if it's asked for.
		 */
		if  ((shmflg & SHM_NORESERVE) &&
				sysctl_overcommit_memory != OVERCOMMIT_NEVER)
			acctflag = VM_NORESERVE;
		file = shmem_kernel_file_setup(name, size, acctflag);
	}
	error = PTR_ERR(file);
	if (IS_ERR(file))
		goto no_file;

	shp->shm_cprid = task_tgid_vnr(current);
	shp->shm_lprid = 0;
	shp->shm_atim = shp->shm_dtim = 0;
	shp->shm_ctim = ktime_get_real_seconds();
	shp->shm_segsz = size;
	shp->shm_nattch = 0;
	shp->shm_file = file;
	shp->shm_creator = current;

	/* ipc_addid() locks shp upon success. */
	error = ipc_addid(&shm_ids(ns), &shp->shm_perm, ns->shm_ctlmni);
	if (error < 0)
		goto no_id;

	list_add(&shp->shm_clist, &current->sysvshm.shm_clist);

	/*
	 * shmid gets reported as "inode#" in /proc/pid/maps.
	 * proc-ps tools use this. Changing this will break them.
	 */
	file_inode(file)->i_ino = shp->shm_perm.id;

	ns->shm_tot += numpages;
	error = shp->shm_perm.id;

	ipc_unlock_object(&shp->shm_perm);
	rcu_read_unlock();
	return error;

no_id:
	if (is_file_hugepages(file) && shp->mlock_user)
		user_shm_unlock(size, shp->mlock_user);
	fput(file);
no_file:
	call_rcu(&shp->shm_perm.rcu, shm_rcu_free);
	return error;
}

/*
 * Called with shm_ids.rwsem and ipcp locked.
 */
static inline int shm_security(struct kern_ipc_perm *ipcp, int shmflg)
{
	struct shmid_kernel *shp;

	shp = container_of(ipcp, struct shmid_kernel, shm_perm);
	return security_shm_associate(shp, shmflg);
}

/*
 * Called with shm_ids.rwsem and ipcp locked.
 */
static inline int shm_more_checks(struct kern_ipc_perm *ipcp,
				struct ipc_params *params)
{
	struct shmid_kernel *shp;

	shp = container_of(ipcp, struct shmid_kernel, shm_perm);
	if (shp->shm_segsz < params->u.size)
		return -EINVAL;

	return 0;
}

long ksys_shmget(key_t key, size_t size, int shmflg)
{
	struct ipc_namespace *ns;
	static const struct ipc_ops shm_ops = {
		.getnew = newseg,
		.associate = shm_security,
		.more_checks = shm_more_checks,
	};
	struct ipc_params shm_params;

	ns = current->nsproxy->ipc_ns;

	shm_params.key = key;
	shm_params.flg = shmflg;
	shm_params.u.size = size;

	return ipcget(ns, &shm_ids(ns), &shm_ops, &shm_params);
}

SYSCALL_DEFINE3(shmget, key_t, key, size_t, size, int, shmflg)
{
	return ksys_shmget(key, size, shmflg);
}

static inline unsigned long copy_shmid_to_user(void __user *buf, struct shmid64_ds *in, int version)
{
	switch (version) {
	case IPC_64:
		return copy_to_user(buf, in, sizeof(*in));
	case IPC_OLD:
	    {
		struct shmid_ds out;

		memset(&out, 0, sizeof(out));
		ipc64_perm_to_ipc_perm(&in->shm_perm, &out.shm_perm);
		out.shm_segsz	= in->shm_segsz;
		out.shm_atime	= in->shm_atime;
		out.shm_dtime	= in->shm_dtime;
		out.shm_ctime	= in->shm_ctime;
		out.shm_cpid	= in->shm_cpid;
		out.shm_lpid	= in->shm_lpid;
		out.shm_nattch	= in->shm_nattch;

		return copy_to_user(buf, &out, sizeof(out));
	    }
	default:
		return -EINVAL;
	}
}

static inline unsigned long
copy_shmid_from_user(struct shmid64_ds *out, void __user *buf, int version)
{
	switch (version) {
	case IPC_64:
		if (copy_from_user(out, buf, sizeof(*out)))
			return -EFAULT;
		return 0;
	case IPC_OLD:
	    {
		struct shmid_ds tbuf_old;

		if (copy_from_user(&tbuf_old, buf, sizeof(tbuf_old)))
			return -EFAULT;

		out->shm_perm.uid	= tbuf_old.shm_perm.uid;
		out->shm_perm.gid	= tbuf_old.shm_perm.gid;
		out->shm_perm.mode	= tbuf_old.shm_perm.mode;

		return 0;
	    }
	default:
		return -EINVAL;
	}
}

static inline unsigned long copy_shminfo_to_user(void __user *buf, struct shminfo64 *in, int version)
{
	switch (version) {
	case IPC_64:
		return copy_to_user(buf, in, sizeof(*in));
	case IPC_OLD:
	    {
		struct shminfo out;

		if (in->shmmax > INT_MAX)
			out.shmmax = INT_MAX;
		else
			out.shmmax = (int)in->shmmax;

		out.shmmin	= in->shmmin;
		out.shmmni	= in->shmmni;
		out.shmseg	= in->shmseg;
		out.shmall	= in->shmall;

		return copy_to_user(buf, &out, sizeof(out));
	    }
	default:
		return -EINVAL;
	}
}

/*
 * Calculate and add used RSS and swap pages of a shm.
 * Called with shm_ids.rwsem held as a reader
 */
static void shm_add_rss_swap(struct shmid_kernel *shp,
	unsigned long *rss_add, unsigned long *swp_add)
{
	struct inode *inode;

	inode = file_inode(shp->shm_file);

	if (is_file_hugepages(shp->shm_file)) {
		struct address_space *mapping = inode->i_mapping;
		struct hstate *h = hstate_file(shp->shm_file);
		*rss_add += pages_per_huge_page(h) * mapping->nrpages;
	} else {
#ifdef CONFIG_SHMEM
		struct shmem_inode_info *info = SHMEM_I(inode);

		spin_lock_irq(&info->lock);
		*rss_add += inode->i_mapping->nrpages;
		*swp_add += info->swapped;
		spin_unlock_irq(&info->lock);
#else
		*rss_add += inode->i_mapping->nrpages;
#endif
	}
}

/*
 * Called with shm_ids.rwsem held as a reader
 */
static void shm_get_stat(struct ipc_namespace *ns, unsigned long *rss,
		unsigned long *swp)
{
	int next_id;
	int total, in_use;

	*rss = 0;
	*swp = 0;

	in_use = shm_ids(ns).in_use;

	for (total = 0, next_id = 0; total < in_use; next_id++) {
		struct kern_ipc_perm *ipc;
		struct shmid_kernel *shp;

		ipc = idr_find(&shm_ids(ns).ipcs_idr, next_id);
		if (ipc == NULL)
			continue;
		shp = container_of(ipc, struct shmid_kernel, shm_perm);

		shm_add_rss_swap(shp, rss, swp);

		total++;
	}
}

/*
 * This function handles some shmctl commands which require the rwsem
 * to be held in write mode.
 * NOTE: no locks must be held, the rwsem is taken inside this function.
 */
static int shmctl_down(struct ipc_namespace *ns, int shmid, int cmd,
		       struct shmid64_ds *shmid64)
{
	struct kern_ipc_perm *ipcp;
	struct shmid_kernel *shp;
	int err;

	down_write(&shm_ids(ns).rwsem);
	rcu_read_lock();

	ipcp = ipcctl_pre_down_nolock(ns, &shm_ids(ns), shmid, cmd,
				      &shmid64->shm_perm, 0);
	if (IS_ERR(ipcp)) {
		err = PTR_ERR(ipcp);
		goto out_unlock1;
	}

	shp = container_of(ipcp, struct shmid_kernel, shm_perm);

	err = security_shm_shmctl(shp, cmd);
	if (err)
		goto out_unlock1;

	switch (cmd) {
	case IPC_RMID:
		ipc_lock_object(&shp->shm_perm);
		/* do_shm_rmid unlocks the ipc object and rcu */
		do_shm_rmid(ns, ipcp);
		goto out_up;
	case IPC_SET:
		ipc_lock_object(&shp->shm_perm);
		err = ipc_update_perm(&shmid64->shm_perm, ipcp);
		if (err)
			goto out_unlock0;
		shp->shm_ctim = ktime_get_real_seconds();
		break;
	default:
		err = -EINVAL;
		goto out_unlock1;
	}

out_unlock0:
	ipc_unlock_object(&shp->shm_perm);
out_unlock1:
	rcu_read_unlock();
out_up:
	up_write(&shm_ids(ns).rwsem);
	return err;
}

static int shmctl_ipc_info(struct ipc_namespace *ns,
			   struct shminfo64 *shminfo)
{
	int err = security_shm_shmctl(NULL, IPC_INFO);
	if (!err) {
		memset(shminfo, 0, sizeof(*shminfo));
		shminfo->shmmni = shminfo->shmseg = ns->shm_ctlmni;
		shminfo->shmmax = ns->shm_ctlmax;
		shminfo->shmall = ns->shm_ctlall;
		shminfo->shmmin = SHMMIN;
		down_read(&shm_ids(ns).rwsem);
		err = ipc_get_maxid(&shm_ids(ns));
		up_read(&shm_ids(ns).rwsem);
		if (err < 0)
			err = 0;
	}
	return err;
}

static int shmctl_shm_info(struct ipc_namespace *ns,
			   struct shm_info *shm_info)
{
	int err = security_shm_shmctl(NULL, SHM_INFO);
	if (!err) {
		memset(shm_info, 0, sizeof(*shm_info));
		down_read(&shm_ids(ns).rwsem);
		shm_info->used_ids = shm_ids(ns).in_use;
		shm_get_stat(ns, &shm_info->shm_rss, &shm_info->shm_swp);
		shm_info->shm_tot = ns->shm_tot;
		shm_info->swap_attempts = 0;
		shm_info->swap_successes = 0;
		err = ipc_get_maxid(&shm_ids(ns));
		up_read(&shm_ids(ns).rwsem);
		if (err < 0)
			err = 0;
	}
	return err;
}

static int shmctl_stat(struct ipc_namespace *ns, int shmid,
			int cmd, struct shmid64_ds *tbuf)
{
	struct shmid_kernel *shp;
	int id = 0;
	int err;

	memset(tbuf, 0, sizeof(*tbuf));

	rcu_read_lock();
	if (cmd == SHM_STAT) {
		shp = shm_obtain_object(ns, shmid);
		if (IS_ERR(shp)) {
			err = PTR_ERR(shp);
			goto out_unlock;
		}
		id = shp->shm_perm.id;
	} else {
		shp = shm_obtain_object_check(ns, shmid);
		if (IS_ERR(shp)) {
			err = PTR_ERR(shp);
			goto out_unlock;
		}
	}

	err = -EACCES;
	if (ipcperms(ns, &shp->shm_perm, S_IRUGO))
		goto out_unlock;

	err = security_shm_shmctl(shp, cmd);
	if (err)
		goto out_unlock;

	ipc_lock_object(&shp->shm_perm);

	if (!ipc_valid_object(&shp->shm_perm)) {
		ipc_unlock_object(&shp->shm_perm);
		err = -EIDRM;
		goto out_unlock;
	}

	kernel_to_ipc64_perm(&shp->shm_perm, &tbuf->shm_perm);
	tbuf->shm_segsz	= shp->shm_segsz;
	tbuf->shm_atime	= shp->shm_atim;
	tbuf->shm_dtime	= shp->shm_dtim;
	tbuf->shm_ctime	= shp->shm_ctim;
	tbuf->shm_cpid	= shp->shm_cprid;
	tbuf->shm_lpid	= shp->shm_lprid;
	tbuf->shm_nattch = shp->shm_nattch;

	ipc_unlock_object(&shp->shm_perm);
	rcu_read_unlock();
	return id;

out_unlock:
	rcu_read_unlock();
	return err;
}

static int shmctl_do_lock(struct ipc_namespace *ns, int shmid, int cmd)
{
	struct shmid_kernel *shp;
	struct file *shm_file;
	int err;

	rcu_read_lock();
	shp = shm_obtain_object_check(ns, shmid);
	if (IS_ERR(shp)) {
		err = PTR_ERR(shp);
		goto out_unlock1;
	}

	audit_ipc_obj(&(shp->shm_perm));
	err = security_shm_shmctl(shp, cmd);
	if (err)
		goto out_unlock1;

	ipc_lock_object(&shp->shm_perm);

	/* check if shm_destroy() is tearing down shp */
	if (!ipc_valid_object(&shp->shm_perm)) {
		err = -EIDRM;
		goto out_unlock0;
	}

	if (!ns_capable(ns->user_ns, CAP_IPC_LOCK)) {
		kuid_t euid = current_euid();

		if (!uid_eq(euid, shp->shm_perm.uid) &&
		    !uid_eq(euid, shp->shm_perm.cuid)) {
			err = -EPERM;
			goto out_unlock0;
		}
		if (cmd == SHM_LOCK && !rlimit(RLIMIT_MEMLOCK)) {
			err = -EPERM;
			goto out_unlock0;
		}
	}

	shm_file = shp->shm_file;
	if (is_file_hugepages(shm_file))
		goto out_unlock0;

	if (cmd == SHM_LOCK) {
		struct user_struct *user = current_user();

		err = shmem_lock(shm_file, 1, user);
		if (!err && !(shp->shm_perm.mode & SHM_LOCKED)) {
			shp->shm_perm.mode |= SHM_LOCKED;
			shp->mlock_user = user;
		}
		goto out_unlock0;
	}

	/* SHM_UNLOCK */
	if (!(shp->shm_perm.mode & SHM_LOCKED))
		goto out_unlock0;
	shmem_lock(shm_file, 0, shp->mlock_user);
	shp->shm_perm.mode &= ~SHM_LOCKED;
	shp->mlock_user = NULL;
	get_file(shm_file);
	ipc_unlock_object(&shp->shm_perm);
	rcu_read_unlock();
	shmem_unlock_mapping(shm_file->f_mapping);

	fput(shm_file);
	return err;

out_unlock0:
	ipc_unlock_object(&shp->shm_perm);
out_unlock1:
	rcu_read_unlock();
	return err;
}

long ksys_shmctl(int shmid, int cmd, struct shmid_ds __user *buf)
{
	int err, version;
	struct ipc_namespace *ns;
	struct shmid64_ds sem64;

	if (cmd < 0 || shmid < 0)
		return -EINVAL;

	version = ipc_parse_version(&cmd);
	ns = current->nsproxy->ipc_ns;

	switch (cmd) {
	case IPC_INFO: {
		struct shminfo64 shminfo;
		err = shmctl_ipc_info(ns, &shminfo);
		if (err < 0)
			return err;
		if (copy_shminfo_to_user(buf, &shminfo, version))
			err = -EFAULT;
		return err;
	}
	case SHM_INFO: {
		struct shm_info shm_info;
		err = shmctl_shm_info(ns, &shm_info);
		if (err < 0)
			return err;
		if (copy_to_user(buf, &shm_info, sizeof(shm_info)))
			err = -EFAULT;
		return err;
	}
	case SHM_STAT:
	case IPC_STAT: {
		err = shmctl_stat(ns, shmid, cmd, &sem64);
		if (err < 0)
			return err;
		if (copy_shmid_to_user(buf, &sem64, version))
			err = -EFAULT;
		return err;
	}
	case IPC_SET:
		if (copy_shmid_from_user(&sem64, buf, version))
			return -EFAULT;
		/* fallthru */
	case IPC_RMID:
		return shmctl_down(ns, shmid, cmd, &sem64);
	case SHM_LOCK:
	case SHM_UNLOCK:
		return shmctl_do_lock(ns, shmid, cmd);
	default:
		return -EINVAL;
	}
}

SYSCALL_DEFINE3(shmctl, int, shmid, int, cmd, struct shmid_ds __user *, buf)
{
	return ksys_shmctl(shmid, cmd, buf);
}

#ifdef CONFIG_COMPAT

struct compat_shmid_ds {
	struct compat_ipc_perm shm_perm;
	int shm_segsz;
	compat_time_t shm_atime;
	compat_time_t shm_dtime;
	compat_time_t shm_ctime;
	compat_ipc_pid_t shm_cpid;
	compat_ipc_pid_t shm_lpid;
	unsigned short shm_nattch;
	unsigned short shm_unused;
	compat_uptr_t shm_unused2;
	compat_uptr_t shm_unused3;
};

struct compat_shminfo64 {
	compat_ulong_t shmmax;
	compat_ulong_t shmmin;
	compat_ulong_t shmmni;
	compat_ulong_t shmseg;
	compat_ulong_t shmall;
	compat_ulong_t __unused1;
	compat_ulong_t __unused2;
	compat_ulong_t __unused3;
	compat_ulong_t __unused4;
};

struct compat_shm_info {
	compat_int_t used_ids;
	compat_ulong_t shm_tot, shm_rss, shm_swp;
	compat_ulong_t swap_attempts, swap_successes;
};

static int copy_compat_shminfo_to_user(void __user *buf, struct shminfo64 *in,
					int version)
{
	if (in->shmmax > INT_MAX)
		in->shmmax = INT_MAX;
	if (version == IPC_64) {
		struct compat_shminfo64 info;
		memset(&info, 0, sizeof(info));
		info.shmmax = in->shmmax;
		info.shmmin = in->shmmin;
		info.shmmni = in->shmmni;
		info.shmseg = in->shmseg;
		info.shmall = in->shmall;
		return copy_to_user(buf, &info, sizeof(info));
	} else {
		struct shminfo info;
		memset(&info, 0, sizeof(info));
		info.shmmax = in->shmmax;
		info.shmmin = in->shmmin;
		info.shmmni = in->shmmni;
		info.shmseg = in->shmseg;
		info.shmall = in->shmall;
		return copy_to_user(buf, &info, sizeof(info));
	}
}

static int put_compat_shm_info(struct shm_info *ip,
				struct compat_shm_info __user *uip)
{
	struct compat_shm_info info;

	memset(&info, 0, sizeof(info));
	info.used_ids = ip->used_ids;
	info.shm_tot = ip->shm_tot;
	info.shm_rss = ip->shm_rss;
	info.shm_swp = ip->shm_swp;
	info.swap_attempts = ip->swap_attempts;
	info.swap_successes = ip->swap_successes;
	return copy_to_user(uip, &info, sizeof(info));
}

static int copy_compat_shmid_to_user(void __user *buf, struct shmid64_ds *in,
					int version)
{
	if (version == IPC_64) {
		struct compat_shmid64_ds v;
		memset(&v, 0, sizeof(v));
		to_compat_ipc64_perm(&v.shm_perm, &in->shm_perm);
		v.shm_atime = in->shm_atime;
		v.shm_dtime = in->shm_dtime;
		v.shm_ctime = in->shm_ctime;
		v.shm_segsz = in->shm_segsz;
		v.shm_nattch = in->shm_nattch;
		v.shm_cpid = in->shm_cpid;
		v.shm_lpid = in->shm_lpid;
		return copy_to_user(buf, &v, sizeof(v));
	} else {
		struct compat_shmid_ds v;
		memset(&v, 0, sizeof(v));
		to_compat_ipc_perm(&v.shm_perm, &in->shm_perm);
		v.shm_perm.key = in->shm_perm.key;
		v.shm_atime = in->shm_atime;
		v.shm_dtime = in->shm_dtime;
		v.shm_ctime = in->shm_ctime;
		v.shm_segsz = in->shm_segsz;
		v.shm_nattch = in->shm_nattch;
		v.shm_cpid = in->shm_cpid;
		v.shm_lpid = in->shm_lpid;
		return copy_to_user(buf, &v, sizeof(v));
	}
}

static int copy_compat_shmid_from_user(struct shmid64_ds *out, void __user *buf,
					int version)
{
	memset(out, 0, sizeof(*out));
	if (version == IPC_64) {
		struct compat_shmid64_ds __user *p = buf;
		return get_compat_ipc64_perm(&out->shm_perm, &p->shm_perm);
	} else {
		struct compat_shmid_ds __user *p = buf;
		return get_compat_ipc_perm(&out->shm_perm, &p->shm_perm);
	}
}

long compat_ksys_shmctl(int shmid, int cmd, void __user *uptr)
{
	struct ipc_namespace *ns;
	struct shmid64_ds sem64;
	int version = compat_ipc_parse_version(&cmd);
	int err;

	ns = current->nsproxy->ipc_ns;

	if (cmd < 0 || shmid < 0)
		return -EINVAL;

	switch (cmd) {
	case IPC_INFO: {
		struct shminfo64 shminfo;
		err = shmctl_ipc_info(ns, &shminfo);
		if (err < 0)
			return err;
		if (copy_compat_shminfo_to_user(uptr, &shminfo, version))
			err = -EFAULT;
		return err;
	}
	case SHM_INFO: {
		struct shm_info shm_info;
		err = shmctl_shm_info(ns, &shm_info);
		if (err < 0)
			return err;
		if (put_compat_shm_info(&shm_info, uptr))
			err = -EFAULT;
		return err;
	}
	case IPC_STAT:
	case SHM_STAT:
		err = shmctl_stat(ns, shmid, cmd, &sem64);
		if (err < 0)
			return err;
		if (copy_compat_shmid_to_user(uptr, &sem64, version))
			err = -EFAULT;
		return err;

	case IPC_SET:
		if (copy_compat_shmid_from_user(&sem64, uptr, version))
			return -EFAULT;
		/* fallthru */
	case IPC_RMID:
		return shmctl_down(ns, shmid, cmd, &sem64);
	case SHM_LOCK:
	case SHM_UNLOCK:
		return shmctl_do_lock(ns, shmid, cmd);
		break;
	default:
		return -EINVAL;
	}
	return err;
}

COMPAT_SYSCALL_DEFINE3(shmctl, int, shmid, int, cmd, void __user *, uptr)
{
	return compat_ksys_shmctl(shmid, cmd, uptr);
}
#endif

/*
 * Fix shmaddr, allocate descriptor, map shm, add attach descriptor to lists.
 *
 * NOTE! Despite the name, this is NOT a direct system call entrypoint. The
 * "raddr" thing points to kernel space, and there has to be a wrapper around
 * this.
 */
long do_shmat(int shmid, char __user *shmaddr, int shmflg,
	      ulong *raddr, unsigned long shmlba)
{
	struct shmid_kernel *shp;
	unsigned long addr = (unsigned long)shmaddr;
	unsigned long size;
	struct file *file;
	int    err;
	unsigned long flags = MAP_SHARED;
	unsigned long prot;
	int acc_mode;
	struct ipc_namespace *ns;
	struct shm_file_data *sfd;
	struct path path;
	fmode_t f_mode;
	unsigned long populate = 0;

	err = -EINVAL;
	if (shmid < 0)
		goto out;

	if (addr) {
		if (addr & (shmlba - 1)) {
			/*
			 * Round down to the nearest multiple of shmlba.
			 * For sane do_mmap_pgoff() parameters, avoid
			 * round downs that trigger nil-page and MAP_FIXED.
			 */
			if ((shmflg & SHM_RND) && addr >= shmlba)
				addr &= ~(shmlba - 1);
			else
#ifndef __ARCH_FORCE_SHMLBA
				if (addr & ~PAGE_MASK)
#endif
					goto out;
		}

		flags |= MAP_FIXED;
	} else if ((shmflg & SHM_REMAP))
		goto out;

	if (shmflg & SHM_RDONLY) {
		prot = PROT_READ;
		acc_mode = S_IRUGO;
		f_mode = FMODE_READ;
	} else {
		prot = PROT_READ | PROT_WRITE;
		acc_mode = S_IRUGO | S_IWUGO;
		f_mode = FMODE_READ | FMODE_WRITE;
	}
	if (shmflg & SHM_EXEC) {
		prot |= PROT_EXEC;
		acc_mode |= S_IXUGO;
	}

	/*
	 * We cannot rely on the fs check since SYSV IPC does have an
	 * additional creator id...
	 */
	ns = current->nsproxy->ipc_ns;
	rcu_read_lock();
	shp = shm_obtain_object_check(ns, shmid);
	if (IS_ERR(shp)) {
		err = PTR_ERR(shp);
		goto out_unlock;
	}

	err = -EACCES;
	if (ipcperms(ns, &shp->shm_perm, acc_mode))
		goto out_unlock;

	err = security_shm_shmat(shp, shmaddr, shmflg);
	if (err)
		goto out_unlock;

	ipc_lock_object(&shp->shm_perm);

	/* check if shm_destroy() is tearing down shp */
	if (!ipc_valid_object(&shp->shm_perm)) {
		ipc_unlock_object(&shp->shm_perm);
		err = -EIDRM;
		goto out_unlock;
	}

	path = shp->shm_file->f_path;
	path_get(&path);
	shp->shm_nattch++;
	size = i_size_read(d_inode(path.dentry));
	ipc_unlock_object(&shp->shm_perm);
	rcu_read_unlock();

	err = -ENOMEM;
	sfd = kzalloc(sizeof(*sfd), GFP_KERNEL);
	if (!sfd) {
		path_put(&path);
		goto out_nattch;
	}

	file = alloc_file(&path, f_mode,
			  is_file_hugepages(shp->shm_file) ?
				&shm_file_operations_huge :
				&shm_file_operations);
	err = PTR_ERR(file);
	if (IS_ERR(file)) {
		kfree(sfd);
		path_put(&path);
		goto out_nattch;
	}

	file->private_data = sfd;
	file->f_mapping = shp->shm_file->f_mapping;
	sfd->id = shp->shm_perm.id;
	sfd->ns = get_ipc_ns(ns);
	sfd->file = shp->shm_file;
	sfd->vm_ops = NULL;

	err = security_mmap_file(file, prot, flags);
	if (err)
		goto out_fput;

	if (down_write_killable(&current->mm->mmap_sem)) {
		err = -EINTR;
		goto out_fput;
	}

	if (addr && !(shmflg & SHM_REMAP)) {
		err = -EINVAL;
		if (addr + size < addr)
			goto invalid;

		if (find_vma_intersection(current->mm, addr, addr + size))
			goto invalid;
	}

	addr = do_mmap_pgoff(file, addr, size, prot, flags, 0, &populate, NULL);
	*raddr = addr;
	err = 0;
	if (IS_ERR_VALUE(addr))
		err = (long)addr;
invalid:
	up_write(&current->mm->mmap_sem);
	if (populate)
		mm_populate(addr, populate);

out_fput:
	fput(file);

out_nattch:
	down_write(&shm_ids(ns).rwsem);
	shp = shm_lock(ns, shmid);
	shp->shm_nattch--;
	if (shm_may_destroy(ns, shp))
		shm_destroy(ns, shp);
	else
		shm_unlock(shp);
	up_write(&shm_ids(ns).rwsem);
	return err;

out_unlock:
	rcu_read_unlock();
out:
	return err;
}

SYSCALL_DEFINE3(shmat, int, shmid, char __user *, shmaddr, int, shmflg)
{
	unsigned long ret;
	long err;

	err = do_shmat(shmid, shmaddr, shmflg, &ret, SHMLBA);
	if (err)
		return err;
	force_successful_syscall_return();
	return (long)ret;
}

#ifdef CONFIG_COMPAT

#ifndef COMPAT_SHMLBA
#define COMPAT_SHMLBA	SHMLBA
#endif

COMPAT_SYSCALL_DEFINE3(shmat, int, shmid, compat_uptr_t, shmaddr, int, shmflg)
{
	unsigned long ret;
	long err;

	err = do_shmat(shmid, compat_ptr(shmaddr), shmflg, &ret, COMPAT_SHMLBA);
	if (err)
		return err;
	force_successful_syscall_return();
	return (long)ret;
}
#endif

/*
 * detach and kill segment if marked destroyed.
 * The work is done in shm_close.
 */
long ksys_shmdt(char __user *shmaddr)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	unsigned long addr = (unsigned long)shmaddr;
	int retval = -EINVAL;
#ifdef CONFIG_MMU
	loff_t size = 0;
	struct file *file;
	struct vm_area_struct *next;
#endif

	if (addr & ~PAGE_MASK)
		return retval;

	if (down_write_killable(&mm->mmap_sem))
		return -EINTR;

	/*
	 * This function tries to be smart and unmap shm segments that
	 * were modified by partial mlock or munmap calls:
	 * - It first determines the size of the shm segment that should be
	 *   unmapped: It searches for a vma that is backed by shm and that
	 *   started at address shmaddr. It records it's size and then unmaps
	 *   it.
	 * - Then it unmaps all shm vmas that started at shmaddr and that
	 *   are within the initially determined size and that are from the
	 *   same shm segment from which we determined the size.
	 * Errors from do_munmap are ignored: the function only fails if
	 * it's called with invalid parameters or if it's called to unmap
	 * a part of a vma. Both calls in this function are for full vmas,
	 * the parameters are directly copied from the vma itself and always
	 * valid - therefore do_munmap cannot fail. (famous last words?)
	 */
	/*
	 * If it had been mremap()'d, the starting address would not
	 * match the usual checks anyway. So assume all vma's are
	 * above the starting address given.
	 */
	vma = find_vma(mm, addr);

#ifdef CONFIG_MMU
	while (vma) {
		next = vma->vm_next;

		/*
		 * Check if the starting address would match, i.e. it's
		 * a fragment created by mprotect() and/or munmap(), or it
		 * otherwise it starts at this address with no hassles.
		 */
		if ((vma->vm_ops == &shm_vm_ops) &&
			(vma->vm_start - addr)/PAGE_SIZE == vma->vm_pgoff) {

			/*
			 * Record the file of the shm segment being
			 * unmapped.  With mremap(), someone could place
			 * page from another segment but with equal offsets
			 * in the range we are unmapping.
			 */
			file = vma->vm_file;
			size = i_size_read(file_inode(vma->vm_file));
			do_munmap(mm, vma->vm_start, vma->vm_end - vma->vm_start, NULL);
			/*
			 * We discovered the size of the shm segment, so
			 * break out of here and fall through to the next
			 * loop that uses the size information to stop
			 * searching for matching vma's.
			 */
			retval = 0;
			vma = next;
			break;
		}
		vma = next;
	}

	/*
	 * We need look no further than the maximum address a fragment
	 * could possibly have landed at. Also cast things to loff_t to
	 * prevent overflows and make comparisons vs. equal-width types.
	 */
	size = PAGE_ALIGN(size);
	while (vma && (loff_t)(vma->vm_end - addr) <= size) {
		next = vma->vm_next;

		/* finding a matching vma now does not alter retval */
		if ((vma->vm_ops == &shm_vm_ops) &&
		    ((vma->vm_start - addr)/PAGE_SIZE == vma->vm_pgoff) &&
		    (vma->vm_file == file))
			do_munmap(mm, vma->vm_start, vma->vm_end - vma->vm_start, NULL);
		vma = next;
	}

#else	/* CONFIG_MMU */
	/* under NOMMU conditions, the exact address to be destroyed must be
	 * given
	 */
	if (vma && vma->vm_start == addr && vma->vm_ops == &shm_vm_ops) {
		do_munmap(mm, vma->vm_start, vma->vm_end - vma->vm_start, NULL);
		retval = 0;
	}

#endif

	up_write(&mm->mmap_sem);
	return retval;
}

SYSCALL_DEFINE1(shmdt, char __user *, shmaddr)
{
	return ksys_shmdt(shmaddr);
}

#ifdef CONFIG_PROC_FS
static int sysvipc_shm_proc_show(struct seq_file *s, void *it)
{
	struct user_namespace *user_ns = seq_user_ns(s);
	struct kern_ipc_perm *ipcp = it;
	struct shmid_kernel *shp;
	unsigned long rss = 0, swp = 0;

	shp = container_of(ipcp, struct shmid_kernel, shm_perm);
	shm_add_rss_swap(shp, &rss, &swp);

#if BITS_PER_LONG <= 32
#define SIZE_SPEC "%10lu"
#else
#define SIZE_SPEC "%21lu"
#endif

	seq_printf(s,
		   "%10d %10d  %4o " SIZE_SPEC " %5u %5u  "
		   "%5lu %5u %5u %5u %5u %10llu %10llu %10llu "
		   SIZE_SPEC " " SIZE_SPEC "\n",
		   shp->shm_perm.key,
		   shp->shm_perm.id,
		   shp->shm_perm.mode,
		   shp->shm_segsz,
		   shp->shm_cprid,
		   shp->shm_lprid,
		   shp->shm_nattch,
		   from_kuid_munged(user_ns, shp->shm_perm.uid),
		   from_kgid_munged(user_ns, shp->shm_perm.gid),
		   from_kuid_munged(user_ns, shp->shm_perm.cuid),
		   from_kgid_munged(user_ns, shp->shm_perm.cgid),
		   shp->shm_atim,
		   shp->shm_dtim,
		   shp->shm_ctim,
		   rss * PAGE_SIZE,
		   swp * PAGE_SIZE);

	return 0;
}
#endif
