/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SHMEM_FS_H
#define __SHMEM_FS_H

#include <linux/file.h>
#include <linux/swap.h>
#include <linux/mempolicy.h>
#include <linux/pagemap.h>
#include <linux/percpu_counter.h>
#include <linux/xattr.h>
#include <linux/fs_parser.h>

/* inode in-kernel data */

struct shmem_inode_info {
	spinlock_t		lock;
	unsigned int		seals;		/* shmem seals */
	unsigned long		flags;
	unsigned long		alloced;	/* data pages alloced to file */
	unsigned long		swapped;	/* subtotal assigned to swap */
	pgoff_t			fallocend;	/* highest fallocate endindex */
	struct list_head        shrinklist;     /* shrinkable hpage inodes */
	struct list_head	swaplist;	/* chain of maybes on swap */
	struct shared_policy	policy;		/* NUMA memory alloc policy */
	struct simple_xattrs	xattrs;		/* list of xattrs */
	atomic_t		stop_eviction;	/* hold when working on inode */
	struct timespec64	i_crtime;	/* file creation time */
	unsigned int		fsflags;	/* flags for FS_IOC_[SG]ETFLAGS */
	struct inode		vfs_inode;
};

#define SHMEM_FL_USER_VISIBLE		FS_FL_USER_VISIBLE
#define SHMEM_FL_USER_MODIFIABLE \
	(FS_IMMUTABLE_FL | FS_APPEND_FL | FS_NODUMP_FL | FS_NOATIME_FL)
#define SHMEM_FL_INHERITED		(FS_NODUMP_FL | FS_NOATIME_FL)

struct shmem_sb_info {
	unsigned long max_blocks;   /* How many blocks are allowed */
	struct percpu_counter used_blocks;  /* How many are allocated */
	unsigned long max_inodes;   /* How many inodes are allowed */
	unsigned long free_inodes;  /* How many are left for allocation */
	raw_spinlock_t stat_lock;   /* Serialize shmem_sb_info changes */
	umode_t mode;		    /* Mount mode for root directory */
	unsigned char huge;	    /* Whether to try for hugepages */
	kuid_t uid;		    /* Mount uid for root directory */
	kgid_t gid;		    /* Mount gid for root directory */
	bool full_inums;	    /* If i_ino should be uint or ino_t */
	ino_t next_ino;		    /* The next per-sb inode number to use */
	ino_t __percpu *ino_batch;  /* The next per-cpu inode number to use */
	struct mempolicy *mpol;     /* default memory policy for mappings */
	spinlock_t shrinklist_lock;   /* Protects shrinklist */
	struct list_head shrinklist;  /* List of shinkable inodes */
	unsigned long shrinklist_len; /* Length of shrinklist */
};

static inline struct shmem_inode_info *SHMEM_I(struct inode *inode)
{
	return container_of(inode, struct shmem_inode_info, vfs_inode);
}

/*
 * Functions in mm/shmem.c called directly from elsewhere:
 */
extern const struct fs_parameter_spec shmem_fs_parameters[];
extern void shmem_init(void);
extern int shmem_init_fs_context(struct fs_context *fc);
extern struct file *shmem_file_setup(const char *name,
					loff_t size, unsigned long flags);
extern struct file *shmem_kernel_file_setup(const char *name, loff_t size,
					    unsigned long flags);
extern struct file *shmem_file_setup_with_mnt(struct vfsmount *mnt,
		const char *name, loff_t size, unsigned long flags);
extern int shmem_zero_setup(struct vm_area_struct *);
extern unsigned long shmem_get_unmapped_area(struct file *, unsigned long addr,
		unsigned long len, unsigned long pgoff, unsigned long flags);
extern int shmem_lock(struct file *file, int lock, struct ucounts *ucounts);
#ifdef CONFIG_SHMEM
extern const struct address_space_operations shmem_aops;
static inline bool shmem_mapping(struct address_space *mapping)
{
	return mapping->a_ops == &shmem_aops;
}
#else
static inline bool shmem_mapping(struct address_space *mapping)
{
	return false;
}
#endif /* CONFIG_SHMEM */
extern void shmem_unlock_mapping(struct address_space *mapping);
extern struct page *shmem_read_mapping_page_gfp(struct address_space *mapping,
					pgoff_t index, gfp_t gfp_mask);
extern void shmem_truncate_range(struct inode *inode, loff_t start, loff_t end);
int shmem_unuse(unsigned int type);

extern bool shmem_is_huge(struct vm_area_struct *vma,
			  struct inode *inode, pgoff_t index);
static inline bool shmem_huge_enabled(struct vm_area_struct *vma)
{
	return shmem_is_huge(vma, file_inode(vma->vm_file), vma->vm_pgoff);
}
extern unsigned long shmem_swap_usage(struct vm_area_struct *vma);
extern unsigned long shmem_partial_swap_usage(struct address_space *mapping,
						pgoff_t start, pgoff_t end);

/* Flag allocation requirements to shmem_getpage */
enum sgp_type {
	SGP_READ,	/* don't exceed i_size, don't allocate page */
	SGP_NOALLOC,	/* similar, but fail on hole or use fallocated page */
	SGP_CACHE,	/* don't exceed i_size, may allocate page */
	SGP_WRITE,	/* may exceed i_size, may allocate !Uptodate page */
	SGP_FALLOC,	/* like SGP_WRITE, but make existing page Uptodate */
};

extern int shmem_getpage(struct inode *inode, pgoff_t index,
		struct page **pagep, enum sgp_type sgp);

static inline struct page *shmem_read_mapping_page(
				struct address_space *mapping, pgoff_t index)
{
	return shmem_read_mapping_page_gfp(mapping, index,
					mapping_gfp_mask(mapping));
}

static inline bool shmem_file(struct file *file)
{
	if (!IS_ENABLED(CONFIG_SHMEM))
		return false;
	if (!file || !file->f_mapping)
		return false;
	return shmem_mapping(file->f_mapping);
}

/*
 * If fallocate(FALLOC_FL_KEEP_SIZE) has been used, there may be pages
 * beyond i_size's notion of EOF, which fallocate has committed to reserving:
 * which split_huge_page() must therefore not delete.  This use of a single
 * "fallocend" per inode errs on the side of not deleting a reservation when
 * in doubt: there are plenty of cases when it preserves unreserved pages.
 */
static inline pgoff_t shmem_fallocend(struct inode *inode, pgoff_t eof)
{
	return max(eof, SHMEM_I(inode)->fallocend);
}

extern bool shmem_charge(struct inode *inode, long pages);
extern void shmem_uncharge(struct inode *inode, long pages);

#ifdef CONFIG_USERFAULTFD
#ifdef CONFIG_SHMEM
extern int shmem_mfill_atomic_pte(struct mm_struct *dst_mm, pmd_t *dst_pmd,
				  struct vm_area_struct *dst_vma,
				  unsigned long dst_addr,
				  unsigned long src_addr,
				  bool zeropage, bool wp_copy,
				  struct page **pagep);
#else /* !CONFIG_SHMEM */
#define shmem_mfill_atomic_pte(dst_mm, dst_pmd, dst_vma, dst_addr, \
			       src_addr, zeropage, wp_copy, pagep) ({ BUG(); 0; })
#endif /* CONFIG_SHMEM */
#endif /* CONFIG_USERFAULTFD */

#endif
