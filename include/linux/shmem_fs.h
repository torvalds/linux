/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SHMEM_FS_H
#define __SHMEM_FS_H

#include <linux/file.h>
#include <linux/swap.h>
#include <linux/mempolicy.h>
#include <linux/pagemap.h>
#include <linux/percpu_counter.h>
#include <linux/xattr.h>

/* inode in-kernel data */

struct shmem_inode_info {
	spinlock_t		lock;
	unsigned int		seals;		/* shmem seals */
	unsigned long		flags;
	unsigned long		alloced;	/* data pages alloced to file */
	unsigned long		swapped;	/* subtotal assigned to swap */
	struct list_head        shrinklist;     /* shrinkable hpage inodes */
	struct list_head	swaplist;	/* chain of maybes on swap */
	struct shared_policy	policy;		/* NUMA memory alloc policy */
	struct simple_xattrs	xattrs;		/* list of xattrs */
	struct inode		vfs_inode;
};

struct shmem_sb_info {
	unsigned long max_blocks;   /* How many blocks are allowed */
	struct percpu_counter used_blocks;  /* How many are allocated */
	unsigned long max_inodes;   /* How many inodes are allowed */
	unsigned long free_inodes;  /* How many are left for allocation */
	spinlock_t stat_lock;	    /* Serialize shmem_sb_info changes */
	umode_t mode;		    /* Mount mode for root directory */
	unsigned char huge;	    /* Whether to try for hugepages */
	kuid_t uid;		    /* Mount uid for root directory */
	kgid_t gid;		    /* Mount gid for root directory */
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
extern int shmem_init(void);
extern int shmem_fill_super(struct super_block *sb, void *data, int silent);
extern struct file *shmem_file_setup(const char *name,
					loff_t size, unsigned long flags);
extern struct file *shmem_kernel_file_setup(const char *name, loff_t size,
					    unsigned long flags);
extern struct file *shmem_file_setup_with_mnt(struct vfsmount *mnt,
		const char *name, loff_t size, unsigned long flags);
extern int shmem_zero_setup(struct vm_area_struct *);
extern unsigned long shmem_get_unmapped_area(struct file *, unsigned long addr,
		unsigned long len, unsigned long pgoff, unsigned long flags);
extern int shmem_lock(struct file *file, int lock, struct user_struct *user);
#ifdef CONFIG_SHMEM
extern bool shmem_mapping(struct address_space *mapping);
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
extern int shmem_unuse(swp_entry_t entry, struct page *page);

extern unsigned long shmem_swap_usage(struct vm_area_struct *vma);
extern unsigned long shmem_partial_swap_usage(struct address_space *mapping,
						pgoff_t start, pgoff_t end);

/* Flag allocation requirements to shmem_getpage */
enum sgp_type {
	SGP_READ,	/* don't exceed i_size, don't allocate page */
	SGP_CACHE,	/* don't exceed i_size, may allocate page */
	SGP_NOHUGE,	/* like SGP_CACHE, but no huge pages */
	SGP_HUGE,	/* like SGP_CACHE, huge pages preferred */
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

extern bool shmem_charge(struct inode *inode, long pages);
extern void shmem_uncharge(struct inode *inode, long pages);

#ifdef CONFIG_TMPFS

extern int shmem_add_seals(struct file *file, unsigned int seals);
extern int shmem_get_seals(struct file *file);
extern long shmem_fcntl(struct file *file, unsigned int cmd, unsigned long arg);

#else

static inline long shmem_fcntl(struct file *f, unsigned int c, unsigned long a)
{
	return -EINVAL;
}

#endif

#ifdef CONFIG_TRANSPARENT_HUGE_PAGECACHE
extern bool shmem_huge_enabled(struct vm_area_struct *vma);
#else
static inline bool shmem_huge_enabled(struct vm_area_struct *vma)
{
	return false;
}
#endif

#ifdef CONFIG_SHMEM
extern int shmem_mcopy_atomic_pte(struct mm_struct *dst_mm, pmd_t *dst_pmd,
				  struct vm_area_struct *dst_vma,
				  unsigned long dst_addr,
				  unsigned long src_addr,
				  struct page **pagep);
extern int shmem_mfill_zeropage_pte(struct mm_struct *dst_mm,
				    pmd_t *dst_pmd,
				    struct vm_area_struct *dst_vma,
				    unsigned long dst_addr);
#else
#define shmem_mcopy_atomic_pte(dst_mm, dst_pte, dst_vma, dst_addr, \
			       src_addr, pagep)        ({ BUG(); 0; })
#define shmem_mfill_zeropage_pte(dst_mm, dst_pmd, dst_vma, \
				 dst_addr)      ({ BUG(); 0; })
#endif

#endif
