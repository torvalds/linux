/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "protocol.h"
#include "pvfs2-kernel.h"

/* tags assigned to kernel upcall operations */
static __u64 next_tag_value;
static DEFINE_SPINLOCK(next_tag_value_lock);

/* the pvfs2 memory caches */

/* a cache for pvfs2 upcall/downcall operations */
static struct kmem_cache *op_cache;

/* a cache for device (/dev/pvfs2-req) communication */
static struct kmem_cache *dev_req_cache;

/* a cache for pvfs2_kiocb objects (i.e pvfs2 iocb structures ) */
static struct kmem_cache *pvfs2_kiocb_cache;

int op_cache_initialize(void)
{
	op_cache = kmem_cache_create("pvfs2_op_cache",
				     sizeof(struct pvfs2_kernel_op_s),
				     0,
				     PVFS2_CACHE_CREATE_FLAGS,
				     NULL);

	if (!op_cache) {
		gossip_err("Cannot create pvfs2_op_cache\n");
		return -ENOMEM;
	}

	/* initialize our atomic tag counter */
	spin_lock(&next_tag_value_lock);
	next_tag_value = 100;
	spin_unlock(&next_tag_value_lock);
	return 0;
}

int op_cache_finalize(void)
{
	kmem_cache_destroy(op_cache);
	return 0;
}

char *get_opname_string(struct pvfs2_kernel_op_s *new_op)
{
	if (new_op) {
		__s32 type = new_op->upcall.type;

		if (type == PVFS2_VFS_OP_FILE_IO)
			return "OP_FILE_IO";
		else if (type == PVFS2_VFS_OP_LOOKUP)
			return "OP_LOOKUP";
		else if (type == PVFS2_VFS_OP_CREATE)
			return "OP_CREATE";
		else if (type == PVFS2_VFS_OP_GETATTR)
			return "OP_GETATTR";
		else if (type == PVFS2_VFS_OP_REMOVE)
			return "OP_REMOVE";
		else if (type == PVFS2_VFS_OP_MKDIR)
			return "OP_MKDIR";
		else if (type == PVFS2_VFS_OP_READDIR)
			return "OP_READDIR";
		else if (type == PVFS2_VFS_OP_READDIRPLUS)
			return "OP_READDIRPLUS";
		else if (type == PVFS2_VFS_OP_SETATTR)
			return "OP_SETATTR";
		else if (type == PVFS2_VFS_OP_SYMLINK)
			return "OP_SYMLINK";
		else if (type == PVFS2_VFS_OP_RENAME)
			return "OP_RENAME";
		else if (type == PVFS2_VFS_OP_STATFS)
			return "OP_STATFS";
		else if (type == PVFS2_VFS_OP_TRUNCATE)
			return "OP_TRUNCATE";
		else if (type == PVFS2_VFS_OP_MMAP_RA_FLUSH)
			return "OP_MMAP_RA_FLUSH";
		else if (type == PVFS2_VFS_OP_FS_MOUNT)
			return "OP_FS_MOUNT";
		else if (type == PVFS2_VFS_OP_FS_UMOUNT)
			return "OP_FS_UMOUNT";
		else if (type == PVFS2_VFS_OP_GETXATTR)
			return "OP_GETXATTR";
		else if (type == PVFS2_VFS_OP_SETXATTR)
			return "OP_SETXATTR";
		else if (type == PVFS2_VFS_OP_LISTXATTR)
			return "OP_LISTXATTR";
		else if (type == PVFS2_VFS_OP_REMOVEXATTR)
			return "OP_REMOVEXATTR";
		else if (type == PVFS2_VFS_OP_PARAM)
			return "OP_PARAM";
		else if (type == PVFS2_VFS_OP_PERF_COUNT)
			return "OP_PERF_COUNT";
		else if (type == PVFS2_VFS_OP_CANCEL)
			return "OP_CANCEL";
		else if (type == PVFS2_VFS_OP_FSYNC)
			return "OP_FSYNC";
		else if (type == PVFS2_VFS_OP_FSKEY)
			return "OP_FSKEY";
		else if (type == PVFS2_VFS_OP_FILE_IOX)
			return "OP_FILE_IOX";
	}
	return "OP_UNKNOWN?";
}

static struct pvfs2_kernel_op_s *op_alloc_common(__s32 op_linger, __s32 type)
{
	struct pvfs2_kernel_op_s *new_op = NULL;

	new_op = kmem_cache_alloc(op_cache, PVFS2_CACHE_ALLOC_FLAGS);
	if (new_op) {
		memset(new_op, 0, sizeof(struct pvfs2_kernel_op_s));

		INIT_LIST_HEAD(&new_op->list);
		spin_lock_init(&new_op->lock);
		init_waitqueue_head(&new_op->waitq);

		init_waitqueue_head(&new_op->io_completion_waitq);
		atomic_set(&new_op->aio_ref_count, 0);

		pvfs2_op_initialize(new_op);

		/* initialize the op specific tag and upcall credentials */
		spin_lock(&next_tag_value_lock);
		new_op->tag = next_tag_value++;
		if (next_tag_value == 0)
			next_tag_value = 100;
		spin_unlock(&next_tag_value_lock);
		new_op->upcall.type = type;
		new_op->attempts = 0;
		gossip_debug(GOSSIP_CACHE_DEBUG,
			     "Alloced OP (%p: %llu %s)\n",
			     new_op,
			     llu(new_op->tag),
			     get_opname_string(new_op));

		new_op->upcall.uid = from_kuid(current_user_ns(),
					       current_fsuid());

		new_op->upcall.gid = from_kgid(current_user_ns(),
					       current_fsgid());

		new_op->op_linger = new_op->op_linger_tmp = op_linger;
	} else {
		gossip_err("op_alloc: kmem_cache_alloc failed!\n");
	}
	return new_op;
}

struct pvfs2_kernel_op_s *op_alloc(__s32 type)
{
	return op_alloc_common(1, type);
}

struct pvfs2_kernel_op_s *op_alloc_trailer(__s32 type)
{
	return op_alloc_common(2, type);
}

void op_release(struct pvfs2_kernel_op_s *pvfs2_op)
{
	if (pvfs2_op) {
		gossip_debug(GOSSIP_CACHE_DEBUG,
			     "Releasing OP (%p: %llu)\n",
			     pvfs2_op,
			     llu(pvfs2_op->tag));
		pvfs2_op_initialize(pvfs2_op);
		kmem_cache_free(op_cache, pvfs2_op);
	} else {
		gossip_err("NULL pointer in op_release\n");
	}
}

int dev_req_cache_initialize(void)
{
	dev_req_cache = kmem_cache_create("pvfs2_devreqcache",
					  MAX_ALIGNED_DEV_REQ_DOWNSIZE,
					  0,
					  PVFS2_CACHE_CREATE_FLAGS,
					  NULL);

	if (!dev_req_cache) {
		gossip_err("Cannot create pvfs2_dev_req_cache\n");
		return -ENOMEM;
	}
	return 0;
}

int dev_req_cache_finalize(void)
{
	kmem_cache_destroy(dev_req_cache);
	return 0;
}

void *dev_req_alloc(void)
{
	void *buffer;

	buffer = kmem_cache_alloc(dev_req_cache, PVFS2_CACHE_ALLOC_FLAGS);
	if (buffer == NULL)
		gossip_err("Failed to allocate from dev_req_cache\n");
	else
		memset(buffer, 0, sizeof(MAX_ALIGNED_DEV_REQ_DOWNSIZE));
	return buffer;
}

void dev_req_release(void *buffer)
{
	if (buffer)
		kmem_cache_free(dev_req_cache, buffer);
	else
		gossip_err("NULL pointer passed to dev_req_release\n");
}

int kiocb_cache_initialize(void)
{
	pvfs2_kiocb_cache = kmem_cache_create("pvfs2_kiocbcache",
					      sizeof(struct pvfs2_kiocb_s),
					      0,
					      PVFS2_CACHE_CREATE_FLAGS,
					      NULL);

	if (!pvfs2_kiocb_cache) {
		gossip_err("Cannot create pvfs2_kiocb_cache!\n");
		return -ENOMEM;
	}
	return 0;
}

int kiocb_cache_finalize(void)
{
	kmem_cache_destroy(pvfs2_kiocb_cache);
	return 0;
}

struct pvfs2_kiocb_s *kiocb_alloc(void)
{
	struct pvfs2_kiocb_s *x = NULL;

	x = kmem_cache_alloc(pvfs2_kiocb_cache, PVFS2_CACHE_ALLOC_FLAGS);
	if (x == NULL)
		gossip_err("kiocb_alloc: kmem_cache_alloc failed!\n");
	else
		memset(x, 0, sizeof(struct pvfs2_kiocb_s));
	return x;
}

void kiocb_release(struct pvfs2_kiocb_s *x)
{
	if (x)
		kmem_cache_free(pvfs2_kiocb_cache, x);
	else
		gossip_err("kiocb_release: kmem_cache_free NULL pointer!\n");
}
