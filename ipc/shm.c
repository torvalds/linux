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
 */

#include <linux/config.h>
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

#include <asm/uaccess.h>

#include "util.h"

static struct file_operations shm_file_operations;
static struct vm_operations_struct shm_vm_ops;

static struct ipc_ids shm_ids;

#define shm_lock(id)	((struct shmid_kernel*)ipc_lock(&shm_ids,id))
#define shm_unlock(shp)	ipc_unlock(&(shp)->shm_perm)
#define shm_get(id)	((struct shmid_kernel*)ipc_get(&shm_ids,id))
#define shm_buildid(id, seq) \
	ipc_buildid(&shm_ids, id, seq)

static int newseg (key_t key, int shmflg, size_t size);
static void shm_open (struct vm_area_struct *shmd);
static void shm_close (struct vm_area_struct *shmd);
#ifdef CONFIG_PROC_FS
static int sysvipc_shm_proc_show(struct seq_file *s, void *it);
#endif

size_t	shm_ctlmax = SHMMAX;
size_t 	shm_ctlall = SHMALL;
int 	shm_ctlmni = SHMMNI;

static int shm_tot; /* total number of shared memory pages */

void __init shm_init (void)
{
	ipc_init_ids(&shm_ids, 1);
	ipc_init_proc_interface("sysvipc/shm",
				"       key      shmid perms       size  cpid  lpid nattch   uid   gid  cuid  cgid      atime      dtime      ctime\n",
				&shm_ids,
				sysvipc_shm_proc_show);
}

static inline int shm_checkid(struct shmid_kernel *s, int id)
{
	if (ipc_checkid(&shm_ids,&s->shm_perm,id))
		return -EIDRM;
	return 0;
}

static inline struct shmid_kernel *shm_rmid(int id)
{
	return (struct shmid_kernel *)ipc_rmid(&shm_ids,id);
}

static inline int shm_addid(struct shmid_kernel *shp)
{
	return ipc_addid(&shm_ids, &shp->shm_perm, shm_ctlmni);
}



static inline void shm_inc (int id) {
	struct shmid_kernel *shp;

	if(!(shp = shm_lock(id)))
		BUG();
	shp->shm_atim = get_seconds();
	shp->shm_lprid = current->tgid;
	shp->shm_nattch++;
	shm_unlock(shp);
}

/* This is called by fork, once for every shm attach. */
static void shm_open (struct vm_area_struct *shmd)
{
	shm_inc (shmd->vm_file->f_dentry->d_inode->i_ino);
}

/*
 * shm_destroy - free the struct shmid_kernel
 *
 * @shp: struct to free
 *
 * It has to be called with shp and shm_ids.sem locked,
 * but returns with shp unlocked and freed.
 */
static void shm_destroy (struct shmid_kernel *shp)
{
	shm_tot -= (shp->shm_segsz + PAGE_SIZE - 1) >> PAGE_SHIFT;
	shm_rmid (shp->id);
	shm_unlock(shp);
	if (!is_file_hugepages(shp->shm_file))
		shmem_lock(shp->shm_file, 0, shp->mlock_user);
	else
		user_shm_unlock(shp->shm_file->f_dentry->d_inode->i_size,
						shp->mlock_user);
	fput (shp->shm_file);
	security_shm_free(shp);
	ipc_rcu_putref(shp);
}

/*
 * remove the attach descriptor shmd.
 * free memory for segment if it is marked destroyed.
 * The descriptor has already been removed from the current->mm->mmap list
 * and will later be kfree()d.
 */
static void shm_close (struct vm_area_struct *shmd)
{
	struct file * file = shmd->vm_file;
	int id = file->f_dentry->d_inode->i_ino;
	struct shmid_kernel *shp;

	down (&shm_ids.sem);
	/* remove from the list of attaches of the shm segment */
	if(!(shp = shm_lock(id)))
		BUG();
	shp->shm_lprid = current->tgid;
	shp->shm_dtim = get_seconds();
	shp->shm_nattch--;
	if(shp->shm_nattch == 0 &&
	   shp->shm_perm.mode & SHM_DEST)
		shm_destroy (shp);
	else
		shm_unlock(shp);
	up (&shm_ids.sem);
}

static int shm_mmap(struct file * file, struct vm_area_struct * vma)
{
	int ret;

	ret = shmem_mmap(file, vma);
	if (ret == 0) {
		vma->vm_ops = &shm_vm_ops;
		shm_inc(file->f_dentry->d_inode->i_ino);
	}

	return ret;
}

static struct file_operations shm_file_operations = {
	.mmap	= shm_mmap,
#ifndef CONFIG_MMU
	.get_unmapped_area = shmem_get_unmapped_area,
#endif
};

static struct vm_operations_struct shm_vm_ops = {
	.open	= shm_open,	/* callback for a new vm-area open */
	.close	= shm_close,	/* callback for when the vm-area is released */
	.nopage	= shmem_nopage,
#if defined(CONFIG_NUMA) && defined(CONFIG_SHMEM)
	.set_policy = shmem_set_policy,
	.get_policy = shmem_get_policy,
#endif
};

static int newseg (key_t key, int shmflg, size_t size)
{
	int error;
	struct shmid_kernel *shp;
	int numpages = (size + PAGE_SIZE -1) >> PAGE_SHIFT;
	struct file * file;
	char name[13];
	int id;

	if (size < SHMMIN || size > shm_ctlmax)
		return -EINVAL;

	if (shm_tot + numpages >= shm_ctlall)
		return -ENOSPC;

	shp = ipc_rcu_alloc(sizeof(*shp));
	if (!shp)
		return -ENOMEM;

	shp->shm_perm.key = key;
	shp->shm_perm.mode = (shmflg & S_IRWXUGO);
	shp->mlock_user = NULL;

	shp->shm_perm.security = NULL;
	error = security_shm_alloc(shp);
	if (error) {
		ipc_rcu_putref(shp);
		return error;
	}

	if (shmflg & SHM_HUGETLB) {
		/* hugetlb_zero_setup takes care of mlock user accounting */
		file = hugetlb_zero_setup(size);
		shp->mlock_user = current->user;
	} else {
		int acctflag = VM_ACCOUNT;
		/*
		 * Do not allow no accounting for OVERCOMMIT_NEVER, even
	 	 * if it's asked for.
		 */
		if  ((shmflg & SHM_NORESERVE) &&
				sysctl_overcommit_memory != OVERCOMMIT_NEVER)
			acctflag = 0;
		sprintf (name, "SYSV%08x", key);
		file = shmem_file_setup(name, size, acctflag);
	}
	error = PTR_ERR(file);
	if (IS_ERR(file))
		goto no_file;

	error = -ENOSPC;
	id = shm_addid(shp);
	if(id == -1) 
		goto no_id;

	shp->shm_cprid = current->tgid;
	shp->shm_lprid = 0;
	shp->shm_atim = shp->shm_dtim = 0;
	shp->shm_ctim = get_seconds();
	shp->shm_segsz = size;
	shp->shm_nattch = 0;
	shp->id = shm_buildid(id,shp->shm_perm.seq);
	shp->shm_file = file;
	file->f_dentry->d_inode->i_ino = shp->id;

	/* Hugetlb ops would have already been assigned. */
	if (!(shmflg & SHM_HUGETLB))
		file->f_op = &shm_file_operations;

	shm_tot += numpages;
	shm_unlock(shp);
	return shp->id;

no_id:
	fput(file);
no_file:
	security_shm_free(shp);
	ipc_rcu_putref(shp);
	return error;
}

asmlinkage long sys_shmget (key_t key, size_t size, int shmflg)
{
	struct shmid_kernel *shp;
	int err, id = 0;

	down(&shm_ids.sem);
	if (key == IPC_PRIVATE) {
		err = newseg(key, shmflg, size);
	} else if ((id = ipc_findkey(&shm_ids, key)) == -1) {
		if (!(shmflg & IPC_CREAT))
			err = -ENOENT;
		else
			err = newseg(key, shmflg, size);
	} else if ((shmflg & IPC_CREAT) && (shmflg & IPC_EXCL)) {
		err = -EEXIST;
	} else {
		shp = shm_lock(id);
		if(shp==NULL)
			BUG();
		if (shp->shm_segsz < size)
			err = -EINVAL;
		else if (ipcperms(&shp->shm_perm, shmflg))
			err = -EACCES;
		else {
			int shmid = shm_buildid(id, shp->shm_perm.seq);
			err = security_shm_associate(shp, shmflg);
			if (!err)
				err = shmid;
		}
		shm_unlock(shp);
	}
	up(&shm_ids.sem);

	return err;
}

static inline unsigned long copy_shmid_to_user(void __user *buf, struct shmid64_ds *in, int version)
{
	switch(version) {
	case IPC_64:
		return copy_to_user(buf, in, sizeof(*in));
	case IPC_OLD:
	    {
		struct shmid_ds out;

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

struct shm_setbuf {
	uid_t	uid;
	gid_t	gid;
	mode_t	mode;
};	

static inline unsigned long copy_shmid_from_user(struct shm_setbuf *out, void __user *buf, int version)
{
	switch(version) {
	case IPC_64:
	    {
		struct shmid64_ds tbuf;

		if (copy_from_user(&tbuf, buf, sizeof(tbuf)))
			return -EFAULT;

		out->uid	= tbuf.shm_perm.uid;
		out->gid	= tbuf.shm_perm.gid;
		out->mode	= tbuf.shm_perm.mode;

		return 0;
	    }
	case IPC_OLD:
	    {
		struct shmid_ds tbuf_old;

		if (copy_from_user(&tbuf_old, buf, sizeof(tbuf_old)))
			return -EFAULT;

		out->uid	= tbuf_old.shm_perm.uid;
		out->gid	= tbuf_old.shm_perm.gid;
		out->mode	= tbuf_old.shm_perm.mode;

		return 0;
	    }
	default:
		return -EINVAL;
	}
}

static inline unsigned long copy_shminfo_to_user(void __user *buf, struct shminfo64 *in, int version)
{
	switch(version) {
	case IPC_64:
		return copy_to_user(buf, in, sizeof(*in));
	case IPC_OLD:
	    {
		struct shminfo out;

		if(in->shmmax > INT_MAX)
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

static void shm_get_stat(unsigned long *rss, unsigned long *swp) 
{
	int i;

	*rss = 0;
	*swp = 0;

	for (i = 0; i <= shm_ids.max_id; i++) {
		struct shmid_kernel *shp;
		struct inode *inode;

		shp = shm_get(i);
		if(!shp)
			continue;

		inode = shp->shm_file->f_dentry->d_inode;

		if (is_file_hugepages(shp->shm_file)) {
			struct address_space *mapping = inode->i_mapping;
			*rss += (HPAGE_SIZE/PAGE_SIZE)*mapping->nrpages;
		} else {
			struct shmem_inode_info *info = SHMEM_I(inode);
			spin_lock(&info->lock);
			*rss += inode->i_mapping->nrpages;
			*swp += info->swapped;
			spin_unlock(&info->lock);
		}
	}
}

asmlinkage long sys_shmctl (int shmid, int cmd, struct shmid_ds __user *buf)
{
	struct shm_setbuf setbuf;
	struct shmid_kernel *shp;
	int err, version;

	if (cmd < 0 || shmid < 0) {
		err = -EINVAL;
		goto out;
	}

	version = ipc_parse_version(&cmd);

	switch (cmd) { /* replace with proc interface ? */
	case IPC_INFO:
	{
		struct shminfo64 shminfo;

		err = security_shm_shmctl(NULL, cmd);
		if (err)
			return err;

		memset(&shminfo,0,sizeof(shminfo));
		shminfo.shmmni = shminfo.shmseg = shm_ctlmni;
		shminfo.shmmax = shm_ctlmax;
		shminfo.shmall = shm_ctlall;

		shminfo.shmmin = SHMMIN;
		if(copy_shminfo_to_user (buf, &shminfo, version))
			return -EFAULT;
		/* reading a integer is always atomic */
		err= shm_ids.max_id;
		if(err<0)
			err = 0;
		goto out;
	}
	case SHM_INFO:
	{
		struct shm_info shm_info;

		err = security_shm_shmctl(NULL, cmd);
		if (err)
			return err;

		memset(&shm_info,0,sizeof(shm_info));
		down(&shm_ids.sem);
		shm_info.used_ids = shm_ids.in_use;
		shm_get_stat (&shm_info.shm_rss, &shm_info.shm_swp);
		shm_info.shm_tot = shm_tot;
		shm_info.swap_attempts = 0;
		shm_info.swap_successes = 0;
		err = shm_ids.max_id;
		up(&shm_ids.sem);
		if(copy_to_user (buf, &shm_info, sizeof(shm_info))) {
			err = -EFAULT;
			goto out;
		}

		err = err < 0 ? 0 : err;
		goto out;
	}
	case SHM_STAT:
	case IPC_STAT:
	{
		struct shmid64_ds tbuf;
		int result;
		memset(&tbuf, 0, sizeof(tbuf));
		shp = shm_lock(shmid);
		if(shp==NULL) {
			err = -EINVAL;
			goto out;
		} else if(cmd==SHM_STAT) {
			err = -EINVAL;
			if (shmid > shm_ids.max_id)
				goto out_unlock;
			result = shm_buildid(shmid, shp->shm_perm.seq);
		} else {
			err = shm_checkid(shp,shmid);
			if(err)
				goto out_unlock;
			result = 0;
		}
		err=-EACCES;
		if (ipcperms (&shp->shm_perm, S_IRUGO))
			goto out_unlock;
		err = security_shm_shmctl(shp, cmd);
		if (err)
			goto out_unlock;
		kernel_to_ipc64_perm(&shp->shm_perm, &tbuf.shm_perm);
		tbuf.shm_segsz	= shp->shm_segsz;
		tbuf.shm_atime	= shp->shm_atim;
		tbuf.shm_dtime	= shp->shm_dtim;
		tbuf.shm_ctime	= shp->shm_ctim;
		tbuf.shm_cpid	= shp->shm_cprid;
		tbuf.shm_lpid	= shp->shm_lprid;
		if (!is_file_hugepages(shp->shm_file))
			tbuf.shm_nattch	= shp->shm_nattch;
		else
			tbuf.shm_nattch = file_count(shp->shm_file) - 1;
		shm_unlock(shp);
		if(copy_shmid_to_user (buf, &tbuf, version))
			err = -EFAULT;
		else
			err = result;
		goto out;
	}
	case SHM_LOCK:
	case SHM_UNLOCK:
	{
		shp = shm_lock(shmid);
		if(shp==NULL) {
			err = -EINVAL;
			goto out;
		}
		err = shm_checkid(shp,shmid);
		if(err)
			goto out_unlock;

		if (!capable(CAP_IPC_LOCK)) {
			err = -EPERM;
			if (current->euid != shp->shm_perm.uid &&
			    current->euid != shp->shm_perm.cuid)
				goto out_unlock;
			if (cmd == SHM_LOCK &&
			    !current->signal->rlim[RLIMIT_MEMLOCK].rlim_cur)
				goto out_unlock;
		}

		err = security_shm_shmctl(shp, cmd);
		if (err)
			goto out_unlock;
		
		if(cmd==SHM_LOCK) {
			struct user_struct * user = current->user;
			if (!is_file_hugepages(shp->shm_file)) {
				err = shmem_lock(shp->shm_file, 1, user);
				if (!err) {
					shp->shm_perm.mode |= SHM_LOCKED;
					shp->mlock_user = user;
				}
			}
		} else if (!is_file_hugepages(shp->shm_file)) {
			shmem_lock(shp->shm_file, 0, shp->mlock_user);
			shp->shm_perm.mode &= ~SHM_LOCKED;
			shp->mlock_user = NULL;
		}
		shm_unlock(shp);
		goto out;
	}
	case IPC_RMID:
	{
		/*
		 *	We cannot simply remove the file. The SVID states
		 *	that the block remains until the last person
		 *	detaches from it, then is deleted. A shmat() on
		 *	an RMID segment is legal in older Linux and if 
		 *	we change it apps break...
		 *
		 *	Instead we set a destroyed flag, and then blow
		 *	the name away when the usage hits zero.
		 */
		down(&shm_ids.sem);
		shp = shm_lock(shmid);
		err = -EINVAL;
		if (shp == NULL) 
			goto out_up;
		err = shm_checkid(shp, shmid);
		if(err)
			goto out_unlock_up;

		if (current->euid != shp->shm_perm.uid &&
		    current->euid != shp->shm_perm.cuid && 
		    !capable(CAP_SYS_ADMIN)) {
			err=-EPERM;
			goto out_unlock_up;
		}

		err = security_shm_shmctl(shp, cmd);
		if (err)
			goto out_unlock_up;

		if (shp->shm_nattch){
			shp->shm_perm.mode |= SHM_DEST;
			/* Do not find it any more */
			shp->shm_perm.key = IPC_PRIVATE;
			shm_unlock(shp);
		} else
			shm_destroy (shp);
		up(&shm_ids.sem);
		goto out;
	}

	case IPC_SET:
	{
		if (copy_shmid_from_user (&setbuf, buf, version)) {
			err = -EFAULT;
			goto out;
		}
		if ((err = audit_ipc_perms(0, setbuf.uid, setbuf.gid, setbuf.mode)))
			return err;
		down(&shm_ids.sem);
		shp = shm_lock(shmid);
		err=-EINVAL;
		if(shp==NULL)
			goto out_up;
		err = shm_checkid(shp,shmid);
		if(err)
			goto out_unlock_up;
		err=-EPERM;
		if (current->euid != shp->shm_perm.uid &&
		    current->euid != shp->shm_perm.cuid && 
		    !capable(CAP_SYS_ADMIN)) {
			goto out_unlock_up;
		}

		err = security_shm_shmctl(shp, cmd);
		if (err)
			goto out_unlock_up;
		
		shp->shm_perm.uid = setbuf.uid;
		shp->shm_perm.gid = setbuf.gid;
		shp->shm_perm.mode = (shp->shm_perm.mode & ~S_IRWXUGO)
			| (setbuf.mode & S_IRWXUGO);
		shp->shm_ctim = get_seconds();
		break;
	}

	default:
		err = -EINVAL;
		goto out;
	}

	err = 0;
out_unlock_up:
	shm_unlock(shp);
out_up:
	up(&shm_ids.sem);
	goto out;
out_unlock:
	shm_unlock(shp);
out:
	return err;
}

/*
 * Fix shmaddr, allocate descriptor, map shm, add attach descriptor to lists.
 *
 * NOTE! Despite the name, this is NOT a direct system call entrypoint. The
 * "raddr" thing points to kernel space, and there has to be a wrapper around
 * this.
 */
long do_shmat(int shmid, char __user *shmaddr, int shmflg, ulong *raddr)
{
	struct shmid_kernel *shp;
	unsigned long addr;
	unsigned long size;
	struct file * file;
	int    err;
	unsigned long flags;
	unsigned long prot;
	unsigned long o_flags;
	int acc_mode;
	void *user_addr;

	if (shmid < 0) {
		err = -EINVAL;
		goto out;
	} else if ((addr = (ulong)shmaddr)) {
		if (addr & (SHMLBA-1)) {
			if (shmflg & SHM_RND)
				addr &= ~(SHMLBA-1);	   /* round down */
			else
#ifndef __ARCH_FORCE_SHMLBA
				if (addr & ~PAGE_MASK)
#endif
					return -EINVAL;
		}
		flags = MAP_SHARED | MAP_FIXED;
	} else {
		if ((shmflg & SHM_REMAP))
			return -EINVAL;

		flags = MAP_SHARED;
	}

	if (shmflg & SHM_RDONLY) {
		prot = PROT_READ;
		o_flags = O_RDONLY;
		acc_mode = S_IRUGO;
	} else {
		prot = PROT_READ | PROT_WRITE;
		o_flags = O_RDWR;
		acc_mode = S_IRUGO | S_IWUGO;
	}
	if (shmflg & SHM_EXEC) {
		prot |= PROT_EXEC;
		acc_mode |= S_IXUGO;
	}

	/*
	 * We cannot rely on the fs check since SYSV IPC does have an
	 * additional creator id...
	 */
	shp = shm_lock(shmid);
	if(shp == NULL) {
		err = -EINVAL;
		goto out;
	}
	err = shm_checkid(shp,shmid);
	if (err) {
		shm_unlock(shp);
		goto out;
	}
	if (ipcperms(&shp->shm_perm, acc_mode)) {
		shm_unlock(shp);
		err = -EACCES;
		goto out;
	}

	err = security_shm_shmat(shp, shmaddr, shmflg);
	if (err) {
		shm_unlock(shp);
		return err;
	}
		
	file = shp->shm_file;
	size = i_size_read(file->f_dentry->d_inode);
	shp->shm_nattch++;
	shm_unlock(shp);

	down_write(&current->mm->mmap_sem);
	if (addr && !(shmflg & SHM_REMAP)) {
		user_addr = ERR_PTR(-EINVAL);
		if (find_vma_intersection(current->mm, addr, addr + size))
			goto invalid;
		/*
		 * If shm segment goes below stack, make sure there is some
		 * space left for the stack to grow (at least 4 pages).
		 */
		if (addr < current->mm->start_stack &&
		    addr > current->mm->start_stack - size - PAGE_SIZE * 5)
			goto invalid;
	}
		
	user_addr = (void*) do_mmap (file, addr, size, prot, flags, 0);

invalid:
	up_write(&current->mm->mmap_sem);

	down (&shm_ids.sem);
	if(!(shp = shm_lock(shmid)))
		BUG();
	shp->shm_nattch--;
	if(shp->shm_nattch == 0 &&
	   shp->shm_perm.mode & SHM_DEST)
		shm_destroy (shp);
	else
		shm_unlock(shp);
	up (&shm_ids.sem);

	*raddr = (unsigned long) user_addr;
	err = 0;
	if (IS_ERR(user_addr))
		err = PTR_ERR(user_addr);
out:
	return err;
}

asmlinkage long sys_shmat(int shmid, char __user *shmaddr, int shmflg)
{
	unsigned long ret;
	long err;

	err = do_shmat(shmid, shmaddr, shmflg, &ret);
	if (err)
		return err;
	force_successful_syscall_return();
	return (long)ret;
}

/*
 * detach and kill segment if marked destroyed.
 * The work is done in shm_close.
 */
asmlinkage long sys_shmdt(char __user *shmaddr)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma, *next;
	unsigned long addr = (unsigned long)shmaddr;
	loff_t size = 0;
	int retval = -EINVAL;

	if (addr & ~PAGE_MASK)
		return retval;

	down_write(&mm->mmap_sem);

	/*
	 * This function tries to be smart and unmap shm segments that
	 * were modified by partial mlock or munmap calls:
	 * - It first determines the size of the shm segment that should be
	 *   unmapped: It searches for a vma that is backed by shm and that
	 *   started at address shmaddr. It records it's size and then unmaps
	 *   it.
	 * - Then it unmaps all shm vmas that started at shmaddr and that
	 *   are within the initially determined size.
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

	while (vma) {
		next = vma->vm_next;

		/*
		 * Check if the starting address would match, i.e. it's
		 * a fragment created by mprotect() and/or munmap(), or it
		 * otherwise it starts at this address with no hassles.
		 */
		if ((vma->vm_ops == &shm_vm_ops || is_vm_hugetlb_page(vma)) &&
			(vma->vm_start - addr)/PAGE_SIZE == vma->vm_pgoff) {


			size = vma->vm_file->f_dentry->d_inode->i_size;
			do_munmap(mm, vma->vm_start, vma->vm_end - vma->vm_start);
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
	 * prevent overflows and make comparisions vs. equal-width types.
	 */
	size = PAGE_ALIGN(size);
	while (vma && (loff_t)(vma->vm_end - addr) <= size) {
		next = vma->vm_next;

		/* finding a matching vma now does not alter retval */
		if ((vma->vm_ops == &shm_vm_ops || is_vm_hugetlb_page(vma)) &&
			(vma->vm_start - addr)/PAGE_SIZE == vma->vm_pgoff)

			do_munmap(mm, vma->vm_start, vma->vm_end - vma->vm_start);
		vma = next;
	}

	up_write(&mm->mmap_sem);
	return retval;
}

#ifdef CONFIG_PROC_FS
static int sysvipc_shm_proc_show(struct seq_file *s, void *it)
{
	struct shmid_kernel *shp = it;
	char *format;

#define SMALL_STRING "%10d %10d  %4o %10u %5u %5u  %5d %5u %5u %5u %5u %10lu %10lu %10lu\n"
#define BIG_STRING   "%10d %10d  %4o %21u %5u %5u  %5d %5u %5u %5u %5u %10lu %10lu %10lu\n"

	if (sizeof(size_t) <= sizeof(int))
		format = SMALL_STRING;
	else
		format = BIG_STRING;
	return seq_printf(s, format,
			  shp->shm_perm.key,
			  shp->id,
			  shp->shm_perm.mode,
			  shp->shm_segsz,
			  shp->shm_cprid,
			  shp->shm_lprid,
			  is_file_hugepages(shp->shm_file) ? (file_count(shp->shm_file) - 1) : shp->shm_nattch,
			  shp->shm_perm.uid,
			  shp->shm_perm.gid,
			  shp->shm_perm.cuid,
			  shp->shm_perm.cgid,
			  shp->shm_atim,
			  shp->shm_dtim,
			  shp->shm_ctim);
}
#endif
