/*
 * linux/ipc/util.c
 * Copyright (C) 1992 Krishna Balasubramanian
 *
 * Sep 1997 - Call suser() last after "normal" permission checks so we
 *            get BSD style process accounting right.
 *            Occurs in several places in the IPC code.
 *            Chris Evans, <chris@ferret.lmh.ox.ac.uk>
 * Nov 1999 - ipc helper functions, unified SMP locking
 *	      Manfred Spraul <manfred@colorfullife.com>
 * Oct 2002 - One lock per IPC id. RCU ipc_free for lock-free grow_ary().
 *            Mingming Cao <cmm@us.ibm.com>
 */

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/shm.h>
#include <linux/init.h>
#include <linux/msg.h>
#include <linux/smp_lock.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/capability.h>
#include <linux/highuid.h>
#include <linux/security.h>
#include <linux/rcupdate.h>
#include <linux/workqueue.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>

#include <asm/unistd.h>

#include "util.h"

struct ipc_proc_iface {
	const char *path;
	const char *header;
	struct ipc_ids *ids;
	int (*show)(struct seq_file *, void *);
};

/**
 *	ipc_init	-	initialise IPC subsystem
 *
 *	The various system5 IPC resources (semaphores, messages and shared
 *	memory are initialised
 */
 
static int __init ipc_init(void)
{
	sem_init();
	msg_init();
	shm_init();
	return 0;
}
__initcall(ipc_init);

/**
 *	ipc_init_ids		-	initialise IPC identifiers
 *	@ids: Identifier set
 *	@size: Number of identifiers
 *
 *	Given a size for the ipc identifier range (limited below IPCMNI)
 *	set up the sequence range to use then allocate and initialise the
 *	array itself. 
 */
 
void __init ipc_init_ids(struct ipc_ids* ids, int size)
{
	int i;

	mutex_init(&ids->mutex);

	if(size > IPCMNI)
		size = IPCMNI;
	ids->in_use = 0;
	ids->max_id = -1;
	ids->seq = 0;
	{
		int seq_limit = INT_MAX/SEQ_MULTIPLIER;
		if(seq_limit > USHRT_MAX)
			ids->seq_max = USHRT_MAX;
		 else
		 	ids->seq_max = seq_limit;
	}

	ids->entries = ipc_rcu_alloc(sizeof(struct kern_ipc_perm *)*size +
				     sizeof(struct ipc_id_ary));

	if(ids->entries == NULL) {
		printk(KERN_ERR "ipc_init_ids() failed, ipc service disabled.\n");
		size = 0;
		ids->entries = &ids->nullentry;
	}
	ids->entries->size = size;
	for(i=0;i<size;i++)
		ids->entries->p[i] = NULL;
}

#ifdef CONFIG_PROC_FS
static struct file_operations sysvipc_proc_fops;
/**
 *	ipc_init_proc_interface	-  Create a proc interface for sysipc types
 *				   using a seq_file interface.
 *	@path: Path in procfs
 *	@header: Banner to be printed at the beginning of the file.
 *	@ids: ipc id table to iterate.
 *	@show: show routine.
 */
void __init ipc_init_proc_interface(const char *path, const char *header,
				    struct ipc_ids *ids,
				    int (*show)(struct seq_file *, void *))
{
	struct proc_dir_entry *pde;
	struct ipc_proc_iface *iface;

	iface = kmalloc(sizeof(*iface), GFP_KERNEL);
	if (!iface)
		return;
	iface->path	= path;
	iface->header	= header;
	iface->ids	= ids;
	iface->show	= show;

	pde = create_proc_entry(path,
				S_IRUGO,        /* world readable */
				NULL            /* parent dir */);
	if (pde) {
		pde->data = iface;
		pde->proc_fops = &sysvipc_proc_fops;
	} else {
		kfree(iface);
	}
}
#endif

/**
 *	ipc_findkey	-	find a key in an ipc identifier set	
 *	@ids: Identifier set
 *	@key: The key to find
 *	
 *	Requires ipc_ids.mutex locked.
 *	Returns the identifier if found or -1 if not.
 */
 
int ipc_findkey(struct ipc_ids* ids, key_t key)
{
	int id;
	struct kern_ipc_perm* p;
	int max_id = ids->max_id;

	/*
	 * rcu_dereference() is not needed here
	 * since ipc_ids.mutex is held
	 */
	for (id = 0; id <= max_id; id++) {
		p = ids->entries->p[id];
		if(p==NULL)
			continue;
		if (key == p->key)
			return id;
	}
	return -1;
}

/*
 * Requires ipc_ids.mutex locked
 */
static int grow_ary(struct ipc_ids* ids, int newsize)
{
	struct ipc_id_ary* new;
	struct ipc_id_ary* old;
	int i;
	int size = ids->entries->size;

	if(newsize > IPCMNI)
		newsize = IPCMNI;
	if(newsize <= size)
		return newsize;

	new = ipc_rcu_alloc(sizeof(struct kern_ipc_perm *)*newsize +
			    sizeof(struct ipc_id_ary));
	if(new == NULL)
		return size;
	new->size = newsize;
	memcpy(new->p, ids->entries->p, sizeof(struct kern_ipc_perm *)*size +
					sizeof(struct ipc_id_ary));
	for(i=size;i<newsize;i++) {
		new->p[i] = NULL;
	}
	old = ids->entries;

	/*
	 * Use rcu_assign_pointer() to make sure the memcpyed contents
	 * of the new array are visible before the new array becomes visible.
	 */
	rcu_assign_pointer(ids->entries, new);

	ipc_rcu_putref(old);
	return newsize;
}

/**
 *	ipc_addid 	-	add an IPC identifier
 *	@ids: IPC identifier set
 *	@new: new IPC permission set
 *	@size: new size limit for the id array
 *
 *	Add an entry 'new' to the IPC arrays. The permissions object is
 *	initialised and the first free entry is set up and the id assigned
 *	is returned. The list is returned in a locked state on success.
 *	On failure the list is not locked and -1 is returned.
 *
 *	Called with ipc_ids.mutex held.
 */
 
int ipc_addid(struct ipc_ids* ids, struct kern_ipc_perm* new, int size)
{
	int id;

	size = grow_ary(ids,size);

	/*
	 * rcu_dereference()() is not needed here since
	 * ipc_ids.mutex is held
	 */
	for (id = 0; id < size; id++) {
		if(ids->entries->p[id] == NULL)
			goto found;
	}
	return -1;
found:
	ids->in_use++;
	if (id > ids->max_id)
		ids->max_id = id;

	new->cuid = new->uid = current->euid;
	new->gid = new->cgid = current->egid;

	new->seq = ids->seq++;
	if(ids->seq > ids->seq_max)
		ids->seq = 0;

	spin_lock_init(&new->lock);
	new->deleted = 0;
	rcu_read_lock();
	spin_lock(&new->lock);
	ids->entries->p[id] = new;
	return id;
}

/**
 *	ipc_rmid	-	remove an IPC identifier
 *	@ids: identifier set
 *	@id: Identifier to remove
 *
 *	The identifier must be valid, and in use. The kernel will panic if
 *	fed an invalid identifier. The entry is removed and internal
 *	variables recomputed. The object associated with the identifier
 *	is returned.
 *	ipc_ids.mutex and the spinlock for this ID is hold before this function
 *	is called, and remain locked on the exit.
 */
 
struct kern_ipc_perm* ipc_rmid(struct ipc_ids* ids, int id)
{
	struct kern_ipc_perm* p;
	int lid = id % SEQ_MULTIPLIER;
	if(lid >= ids->entries->size)
		BUG();

	/* 
	 * do not need a rcu_dereference()() here to force ordering
	 * on Alpha, since the ipc_ids.mutex is held.
	 */	
	p = ids->entries->p[lid];
	ids->entries->p[lid] = NULL;
	if(p==NULL)
		BUG();
	ids->in_use--;

	if (lid == ids->max_id) {
		do {
			lid--;
			if(lid == -1)
				break;
		} while (ids->entries->p[lid] == NULL);
		ids->max_id = lid;
	}
	p->deleted = 1;
	return p;
}

/**
 *	ipc_alloc	-	allocate ipc space
 *	@size: size desired
 *
 *	Allocate memory from the appropriate pools and return a pointer to it.
 *	NULL is returned if the allocation fails
 */
 
void* ipc_alloc(int size)
{
	void* out;
	if(size > PAGE_SIZE)
		out = vmalloc(size);
	else
		out = kmalloc(size, GFP_KERNEL);
	return out;
}

/**
 *	ipc_free        -       free ipc space
 *	@ptr: pointer returned by ipc_alloc
 *	@size: size of block
 *
 *	Free a block created with ipc_alloc. The caller must know the size
 *	used in the allocation call.
 */

void ipc_free(void* ptr, int size)
{
	if(size > PAGE_SIZE)
		vfree(ptr);
	else
		kfree(ptr);
}

/*
 * rcu allocations:
 * There are three headers that are prepended to the actual allocation:
 * - during use: ipc_rcu_hdr.
 * - during the rcu grace period: ipc_rcu_grace.
 * - [only if vmalloc]: ipc_rcu_sched.
 * Their lifetime doesn't overlap, thus the headers share the same memory.
 * Unlike a normal union, they are right-aligned, thus some container_of
 * forward/backward casting is necessary:
 */
struct ipc_rcu_hdr
{
	int refcount;
	int is_vmalloc;
	void *data[0];
};


struct ipc_rcu_grace
{
	struct rcu_head rcu;
	/* "void *" makes sure alignment of following data is sane. */
	void *data[0];
};

struct ipc_rcu_sched
{
	struct work_struct work;
	/* "void *" makes sure alignment of following data is sane. */
	void *data[0];
};

#define HDRLEN_KMALLOC		(sizeof(struct ipc_rcu_grace) > sizeof(struct ipc_rcu_hdr) ? \
					sizeof(struct ipc_rcu_grace) : sizeof(struct ipc_rcu_hdr))
#define HDRLEN_VMALLOC		(sizeof(struct ipc_rcu_sched) > HDRLEN_KMALLOC ? \
					sizeof(struct ipc_rcu_sched) : HDRLEN_KMALLOC)

static inline int rcu_use_vmalloc(int size)
{
	/* Too big for a single page? */
	if (HDRLEN_KMALLOC + size > PAGE_SIZE)
		return 1;
	return 0;
}

/**
 *	ipc_rcu_alloc	-	allocate ipc and rcu space 
 *	@size: size desired
 *
 *	Allocate memory for the rcu header structure +  the object.
 *	Returns the pointer to the object.
 *	NULL is returned if the allocation fails. 
 */
 
void* ipc_rcu_alloc(int size)
{
	void* out;
	/* 
	 * We prepend the allocation with the rcu struct, and
	 * workqueue if necessary (for vmalloc). 
	 */
	if (rcu_use_vmalloc(size)) {
		out = vmalloc(HDRLEN_VMALLOC + size);
		if (out) {
			out += HDRLEN_VMALLOC;
			container_of(out, struct ipc_rcu_hdr, data)->is_vmalloc = 1;
			container_of(out, struct ipc_rcu_hdr, data)->refcount = 1;
		}
	} else {
		out = kmalloc(HDRLEN_KMALLOC + size, GFP_KERNEL);
		if (out) {
			out += HDRLEN_KMALLOC;
			container_of(out, struct ipc_rcu_hdr, data)->is_vmalloc = 0;
			container_of(out, struct ipc_rcu_hdr, data)->refcount = 1;
		}
	}

	return out;
}

void ipc_rcu_getref(void *ptr)
{
	container_of(ptr, struct ipc_rcu_hdr, data)->refcount++;
}

/**
 * ipc_schedule_free - free ipc + rcu space
 * @head: RCU callback structure for queued work
 * 
 * Since RCU callback function is called in bh,
 * we need to defer the vfree to schedule_work
 */
static void ipc_schedule_free(struct rcu_head *head)
{
	struct ipc_rcu_grace *grace =
		container_of(head, struct ipc_rcu_grace, rcu);
	struct ipc_rcu_sched *sched =
			container_of(&(grace->data[0]), struct ipc_rcu_sched, data[0]);

	INIT_WORK(&sched->work, vfree, sched);
	schedule_work(&sched->work);
}

/**
 * ipc_immediate_free - free ipc + rcu space
 * @head: RCU callback structure that contains pointer to be freed
 *
 * Free from the RCU callback context
 */
static void ipc_immediate_free(struct rcu_head *head)
{
	struct ipc_rcu_grace *free =
		container_of(head, struct ipc_rcu_grace, rcu);
	kfree(free);
}

void ipc_rcu_putref(void *ptr)
{
	if (--container_of(ptr, struct ipc_rcu_hdr, data)->refcount > 0)
		return;

	if (container_of(ptr, struct ipc_rcu_hdr, data)->is_vmalloc) {
		call_rcu(&container_of(ptr, struct ipc_rcu_grace, data)->rcu,
				ipc_schedule_free);
	} else {
		call_rcu(&container_of(ptr, struct ipc_rcu_grace, data)->rcu,
				ipc_immediate_free);
	}
}

/**
 *	ipcperms	-	check IPC permissions
 *	@ipcp: IPC permission set
 *	@flag: desired permission set.
 *
 *	Check user, group, other permissions for access
 *	to ipc resources. return 0 if allowed
 */
 
int ipcperms (struct kern_ipc_perm *ipcp, short flag)
{	/* flag will most probably be 0 or S_...UGO from <linux/stat.h> */
	int requested_mode, granted_mode;

	requested_mode = (flag >> 6) | (flag >> 3) | flag;
	granted_mode = ipcp->mode;
	if (current->euid == ipcp->cuid || current->euid == ipcp->uid)
		granted_mode >>= 6;
	else if (in_group_p(ipcp->cgid) || in_group_p(ipcp->gid))
		granted_mode >>= 3;
	/* is there some bit set in requested_mode but not in granted_mode? */
	if ((requested_mode & ~granted_mode & 0007) && 
	    !capable(CAP_IPC_OWNER))
		return -1;

	return security_ipc_permission(ipcp, flag);
}

/*
 * Functions to convert between the kern_ipc_perm structure and the
 * old/new ipc_perm structures
 */

/**
 *	kernel_to_ipc64_perm	-	convert kernel ipc permissions to user
 *	@in: kernel permissions
 *	@out: new style IPC permissions
 *
 *	Turn the kernel object 'in' into a set of permissions descriptions
 *	for returning to userspace (out).
 */
 

void kernel_to_ipc64_perm (struct kern_ipc_perm *in, struct ipc64_perm *out)
{
	out->key	= in->key;
	out->uid	= in->uid;
	out->gid	= in->gid;
	out->cuid	= in->cuid;
	out->cgid	= in->cgid;
	out->mode	= in->mode;
	out->seq	= in->seq;
}

/**
 *	ipc64_perm_to_ipc_perm	-	convert old ipc permissions to new
 *	@in: new style IPC permissions
 *	@out: old style IPC permissions
 *
 *	Turn the new style permissions object in into a compatibility
 *	object and store it into the 'out' pointer.
 */
 
void ipc64_perm_to_ipc_perm (struct ipc64_perm *in, struct ipc_perm *out)
{
	out->key	= in->key;
	SET_UID(out->uid, in->uid);
	SET_GID(out->gid, in->gid);
	SET_UID(out->cuid, in->cuid);
	SET_GID(out->cgid, in->cgid);
	out->mode	= in->mode;
	out->seq	= in->seq;
}

/*
 * So far only shm_get_stat() calls ipc_get() via shm_get(), so ipc_get()
 * is called with shm_ids.mutex locked.  Since grow_ary() is also called with
 * shm_ids.mutex down(for Shared Memory), there is no need to add read
 * barriers here to gurantee the writes in grow_ary() are seen in order 
 * here (for Alpha).
 *
 * However ipc_get() itself does not necessary require ipc_ids.mutex down. So
 * if in the future ipc_get() is used by other places without ipc_ids.mutex
 * down, then ipc_get() needs read memery barriers as ipc_lock() does.
 */
struct kern_ipc_perm* ipc_get(struct ipc_ids* ids, int id)
{
	struct kern_ipc_perm* out;
	int lid = id % SEQ_MULTIPLIER;
	if(lid >= ids->entries->size)
		return NULL;
	out = ids->entries->p[lid];
	return out;
}

struct kern_ipc_perm* ipc_lock(struct ipc_ids* ids, int id)
{
	struct kern_ipc_perm* out;
	int lid = id % SEQ_MULTIPLIER;
	struct ipc_id_ary* entries;

	rcu_read_lock();
	entries = rcu_dereference(ids->entries);
	if(lid >= entries->size) {
		rcu_read_unlock();
		return NULL;
	}
	out = entries->p[lid];
	if(out == NULL) {
		rcu_read_unlock();
		return NULL;
	}
	spin_lock(&out->lock);
	
	/* ipc_rmid() may have already freed the ID while ipc_lock
	 * was spinning: here verify that the structure is still valid
	 */
	if (out->deleted) {
		spin_unlock(&out->lock);
		rcu_read_unlock();
		return NULL;
	}
	return out;
}

void ipc_lock_by_ptr(struct kern_ipc_perm *perm)
{
	rcu_read_lock();
	spin_lock(&perm->lock);
}

void ipc_unlock(struct kern_ipc_perm* perm)
{
	spin_unlock(&perm->lock);
	rcu_read_unlock();
}

int ipc_buildid(struct ipc_ids* ids, int id, int seq)
{
	return SEQ_MULTIPLIER*seq + id;
}

int ipc_checkid(struct ipc_ids* ids, struct kern_ipc_perm* ipcp, int uid)
{
	if(uid/SEQ_MULTIPLIER != ipcp->seq)
		return 1;
	return 0;
}

#ifdef __ARCH_WANT_IPC_PARSE_VERSION


/**
 *	ipc_parse_version	-	IPC call version
 *	@cmd: pointer to command
 *
 *	Return IPC_64 for new style IPC and IPC_OLD for old style IPC. 
 *	The cmd value is turned from an encoding command and version into
 *	just the command code.
 */
 
int ipc_parse_version (int *cmd)
{
	if (*cmd & IPC_64) {
		*cmd ^= IPC_64;
		return IPC_64;
	} else {
		return IPC_OLD;
	}
}

#endif /* __ARCH_WANT_IPC_PARSE_VERSION */

#ifdef CONFIG_PROC_FS
static void *sysvipc_proc_next(struct seq_file *s, void *it, loff_t *pos)
{
	struct ipc_proc_iface *iface = s->private;
	struct kern_ipc_perm *ipc = it;
	loff_t p;

	/* If we had an ipc id locked before, unlock it */
	if (ipc && ipc != SEQ_START_TOKEN)
		ipc_unlock(ipc);

	/*
	 * p = *pos - 1 (because id 0 starts at position 1)
	 *          + 1 (because we increment the position by one)
	 */
	for (p = *pos; p <= iface->ids->max_id; p++) {
		if ((ipc = ipc_lock(iface->ids, p)) != NULL) {
			*pos = p + 1;
			return ipc;
		}
	}

	/* Out of range - return NULL to terminate iteration */
	return NULL;
}

/*
 * File positions: pos 0 -> header, pos n -> ipc id + 1.
 * SeqFile iterator: iterator value locked shp or SEQ_TOKEN_START.
 */
static void *sysvipc_proc_start(struct seq_file *s, loff_t *pos)
{
	struct ipc_proc_iface *iface = s->private;
	struct kern_ipc_perm *ipc;
	loff_t p;

	/*
	 * Take the lock - this will be released by the corresponding
	 * call to stop().
	 */
	mutex_lock(&iface->ids->mutex);

	/* pos < 0 is invalid */
	if (*pos < 0)
		return NULL;

	/* pos == 0 means header */
	if (*pos == 0)
		return SEQ_START_TOKEN;

	/* Find the (pos-1)th ipc */
	for (p = *pos - 1; p <= iface->ids->max_id; p++) {
		if ((ipc = ipc_lock(iface->ids, p)) != NULL) {
			*pos = p + 1;
			return ipc;
		}
	}
	return NULL;
}

static void sysvipc_proc_stop(struct seq_file *s, void *it)
{
	struct kern_ipc_perm *ipc = it;
	struct ipc_proc_iface *iface = s->private;

	/* If we had a locked segment, release it */
	if (ipc && ipc != SEQ_START_TOKEN)
		ipc_unlock(ipc);

	/* Release the lock we took in start() */
	mutex_unlock(&iface->ids->mutex);
}

static int sysvipc_proc_show(struct seq_file *s, void *it)
{
	struct ipc_proc_iface *iface = s->private;

	if (it == SEQ_START_TOKEN)
		return seq_puts(s, iface->header);

	return iface->show(s, it);
}

static struct seq_operations sysvipc_proc_seqops = {
	.start = sysvipc_proc_start,
	.stop  = sysvipc_proc_stop,
	.next  = sysvipc_proc_next,
	.show  = sysvipc_proc_show,
};

static int sysvipc_proc_open(struct inode *inode, struct file *file) {
	int ret;
	struct seq_file *seq;

	ret = seq_open(file, &sysvipc_proc_seqops);
	if (!ret) {
		seq = file->private_data;
		seq->private = PDE(inode)->data;
	}
	return ret;
}

static struct file_operations sysvipc_proc_fops = {
	.open    = sysvipc_proc_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release,
};
#endif /* CONFIG_PROC_FS */
