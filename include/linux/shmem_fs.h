#ifndef __SHMEM_FS_H
#define __SHMEM_FS_H

#include <linux/swap.h>
#include <linux/mempolicy.h>

/* inode in-kernel data */

#define SHMEM_NR_DIRECT 16

struct shmem_inode_info {
	spinlock_t		lock;
	unsigned long		flags;
	unsigned long		alloced;	/* data pages alloced to file */
	unsigned long		swapped;	/* subtotal assigned to swap */
	unsigned long		next_index;	/* highest alloced index + 1 */
	struct shared_policy	policy;		/* NUMA memory alloc policy */
	struct page		*i_indirect;	/* top indirect blocks page */
	swp_entry_t		i_direct[SHMEM_NR_DIRECT]; /* first blocks */
	struct list_head	swaplist;	/* chain of maybes on swap */
	struct inode		vfs_inode;
#ifdef CONFIG_TMPFS_POSIX_ACL
	struct posix_acl	*i_acl;
	struct posix_acl	*i_default_acl;
#endif
};

struct shmem_sb_info {
	unsigned long max_blocks;   /* How many blocks are allowed */
	unsigned long free_blocks;  /* How many are left for allocation */
	unsigned long max_inodes;   /* How many inodes are allowed */
	unsigned long free_inodes;  /* How many are left for allocation */
	int policy;		    /* Default NUMA memory alloc policy */
	nodemask_t policy_nodes;    /* nodemask for preferred and bind */
	spinlock_t    stat_lock;
};

static inline struct shmem_inode_info *SHMEM_I(struct inode *inode)
{
	return container_of(inode, struct shmem_inode_info, vfs_inode);
}

#ifdef CONFIG_TMPFS_POSIX_ACL
int shmem_permission(struct inode *, int, struct nameidata *);
int shmem_acl_init(struct inode *, struct inode *);
void shmem_acl_destroy_inode(struct inode *);

extern struct xattr_handler shmem_xattr_acl_access_handler;
extern struct xattr_handler shmem_xattr_acl_default_handler;

extern struct generic_acl_operations shmem_acl_ops;

#else
static inline int shmem_acl_init(struct inode *inode, struct inode *dir)
{
	return 0;
}
static inline void shmem_acl_destroy_inode(struct inode *inode)
{
}
#endif  /* CONFIG_TMPFS_POSIX_ACL */

#endif
