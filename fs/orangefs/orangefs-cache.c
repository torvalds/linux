/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "protocol.h"
#include "orangefs-kernel.h"

/* tags assigned to kernel upcall operations */
static __u64 next_tag_value;
static DEFINE_SPINLOCK(next_tag_value_lock);

/* the orangefs memory caches */

/* a cache for orangefs upcall/downcall operations */
static struct kmem_cache *op_cache;

int op_cache_initialize(void)
{
	op_cache = kmem_cache_create("orangefs_op_cache",
				     sizeof(struct orangefs_kernel_op_s),
				     0,
				     ORANGEFS_CACHE_CREATE_FLAGS,
				     NULL);

	if (!op_cache) {
		gossip_err("Cannot create orangefs_op_cache\n");
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

char *get_opname_string(struct orangefs_kernel_op_s *new_op)
{
	if (new_op) {
		__s32 type = new_op->upcall.type;

		if (type == ORANGEFS_VFS_OP_FILE_IO)
			return "OP_FILE_IO";
		else if (type == ORANGEFS_VFS_OP_LOOKUP)
			return "OP_LOOKUP";
		else if (type == ORANGEFS_VFS_OP_CREATE)
			return "OP_CREATE";
		else if (type == ORANGEFS_VFS_OP_GETATTR)
			return "OP_GETATTR";
		else if (type == ORANGEFS_VFS_OP_REMOVE)
			return "OP_REMOVE";
		else if (type == ORANGEFS_VFS_OP_MKDIR)
			return "OP_MKDIR";
		else if (type == ORANGEFS_VFS_OP_READDIR)
			return "OP_READDIR";
		else if (type == ORANGEFS_VFS_OP_READDIRPLUS)
			return "OP_READDIRPLUS";
		else if (type == ORANGEFS_VFS_OP_SETATTR)
			return "OP_SETATTR";
		else if (type == ORANGEFS_VFS_OP_SYMLINK)
			return "OP_SYMLINK";
		else if (type == ORANGEFS_VFS_OP_RENAME)
			return "OP_RENAME";
		else if (type == ORANGEFS_VFS_OP_STATFS)
			return "OP_STATFS";
		else if (type == ORANGEFS_VFS_OP_TRUNCATE)
			return "OP_TRUNCATE";
		else if (type == ORANGEFS_VFS_OP_MMAP_RA_FLUSH)
			return "OP_MMAP_RA_FLUSH";
		else if (type == ORANGEFS_VFS_OP_FS_MOUNT)
			return "OP_FS_MOUNT";
		else if (type == ORANGEFS_VFS_OP_FS_UMOUNT)
			return "OP_FS_UMOUNT";
		else if (type == ORANGEFS_VFS_OP_GETXATTR)
			return "OP_GETXATTR";
		else if (type == ORANGEFS_VFS_OP_SETXATTR)
			return "OP_SETXATTR";
		else if (type == ORANGEFS_VFS_OP_LISTXATTR)
			return "OP_LISTXATTR";
		else if (type == ORANGEFS_VFS_OP_REMOVEXATTR)
			return "OP_REMOVEXATTR";
		else if (type == ORANGEFS_VFS_OP_PARAM)
			return "OP_PARAM";
		else if (type == ORANGEFS_VFS_OP_PERF_COUNT)
			return "OP_PERF_COUNT";
		else if (type == ORANGEFS_VFS_OP_CANCEL)
			return "OP_CANCEL";
		else if (type == ORANGEFS_VFS_OP_FSYNC)
			return "OP_FSYNC";
		else if (type == ORANGEFS_VFS_OP_FSKEY)
			return "OP_FSKEY";
	}
	return "OP_UNKNOWN?";
}

void orangefs_new_tag(struct orangefs_kernel_op_s *op)
{
	spin_lock(&next_tag_value_lock);
	op->tag = next_tag_value++;
	if (next_tag_value == 0)
		next_tag_value = 100;
	spin_unlock(&next_tag_value_lock);
}

struct orangefs_kernel_op_s *op_alloc(__s32 type)
{
	struct orangefs_kernel_op_s *new_op = NULL;

	new_op = kmem_cache_zalloc(op_cache, GFP_KERNEL);
	if (new_op) {
		INIT_LIST_HEAD(&new_op->list);
		spin_lock_init(&new_op->lock);
		init_completion(&new_op->waitq);

		new_op->upcall.type = ORANGEFS_VFS_OP_INVALID;
		new_op->downcall.type = ORANGEFS_VFS_OP_INVALID;
		new_op->downcall.status = -1;

		new_op->op_state = OP_VFS_STATE_UNKNOWN;

		/* initialize the op specific tag and upcall credentials */
		orangefs_new_tag(new_op);
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
	} else {
		gossip_err("op_alloc: kmem_cache_zalloc failed!\n");
	}
	return new_op;
}

void op_release(struct orangefs_kernel_op_s *orangefs_op)
{
	if (orangefs_op) {
		gossip_debug(GOSSIP_CACHE_DEBUG,
			     "Releasing OP (%p: %llu)\n",
			     orangefs_op,
			     llu(orangefs_op->tag));
		kmem_cache_free(op_cache, orangefs_op);
	} else {
		gossip_err("NULL pointer in op_release\n");
	}
}
