#ifndef _LINUX_HUGETLB_H
#define _LINUX_HUGETLB_H

#include <linux/mm_types.h>
#include <linux/fs.h>
#include <linux/hugetlb_inline.h>

struct ctl_table;
struct user_struct;

#ifdef CONFIG_HUGETLB_PAGE

#include <linux/mempolicy.h>
#include <linux/shm.h>
#include <asm/tlbflush.h>

int PageHuge(struct page *page);

void reset_vma_resv_huge_pages(struct vm_area_struct *vma);
int hugetlb_sysctl_handler(struct ctl_table *, int, void __user *, size_t *, loff_t *);
int hugetlb_overcommit_handler(struct ctl_table *, int, void __user *, size_t *, loff_t *);
int hugetlb_treat_movable_handler(struct ctl_table *, int, void __user *, size_t *, loff_t *);

#ifdef CONFIG_NUMA
int hugetlb_mempolicy_sysctl_handler(struct ctl_table *, int,
					void __user *, size_t *, loff_t *);
#endif

int copy_hugetlb_page_range(struct mm_struct *, struct mm_struct *, struct vm_area_struct *);
int follow_hugetlb_page(struct mm_struct *, struct vm_area_struct *,
			struct page **, struct vm_area_struct **,
			unsigned long *, int *, int, unsigned int flags);
void unmap_hugepage_range(struct vm_area_struct *,
			unsigned long, unsigned long, struct page *);
void __unmap_hugepage_range(struct vm_area_struct *,
			unsigned long, unsigned long, struct page *);
int hugetlb_prefault(struct address_space *, struct vm_area_struct *);
void hugetlb_report_meminfo(struct seq_file *);
int hugetlb_report_node_meminfo(int, char *);
unsigned long hugetlb_total_pages(void);
int hugetlb_fault(struct mm_struct *mm, struct vm_area_struct *vma,
			unsigned long address, unsigned int flags);
int hugetlb_reserve_pages(struct inode *inode, long from, long to,
						struct vm_area_struct *vma,
						vm_flags_t vm_flags);
void hugetlb_unreserve_pages(struct inode *inode, long offset, long freed);
int dequeue_hwpoisoned_huge_page(struct page *page);
void copy_huge_page(struct page *dst, struct page *src);

extern unsigned long hugepages_treat_as_movable;
extern const unsigned long hugetlb_zero, hugetlb_infinity;
extern int sysctl_hugetlb_shm_group;
extern struct list_head huge_boot_pages;

/* arch callbacks */

pte_t *huge_pte_alloc(struct mm_struct *mm,
			unsigned long addr, unsigned long sz);
pte_t *huge_pte_offset(struct mm_struct *mm, unsigned long addr);
int huge_pmd_unshare(struct mm_struct *mm, unsigned long *addr, pte_t *ptep);
struct page *follow_huge_addr(struct mm_struct *mm, unsigned long address,
			      int write);
struct page *follow_huge_pmd(struct mm_struct *mm, unsigned long address,
				pmd_t *pmd, int write);
struct page *follow_huge_pud(struct mm_struct *mm, unsigned long address,
				pud_t *pud, int write);
int pmd_huge(pmd_t pmd);
int pud_huge(pud_t pmd);
void hugetlb_change_protection(struct vm_area_struct *vma,
		unsigned long address, unsigned long end, pgprot_t newprot);

#else /* !CONFIG_HUGETLB_PAGE */

static inline int PageHuge(struct page *page)
{
	return 0;
}

static inline void reset_vma_resv_huge_pages(struct vm_area_struct *vma)
{
}

static inline unsigned long hugetlb_total_pages(void)
{
	return 0;
}

#define follow_hugetlb_page(m,v,p,vs,a,b,i,w)	({ BUG(); 0; })
#define follow_huge_addr(mm, addr, write)	ERR_PTR(-EINVAL)
#define copy_hugetlb_page_range(src, dst, vma)	({ BUG(); 0; })
#define hugetlb_prefault(mapping, vma)		({ BUG(); 0; })
#define unmap_hugepage_range(vma, start, end, page)	BUG()
static inline void hugetlb_report_meminfo(struct seq_file *m)
{
}
#define hugetlb_report_node_meminfo(n, buf)	0
#define follow_huge_pmd(mm, addr, pmd, write)	NULL
#define follow_huge_pud(mm, addr, pud, write)	NULL
#define prepare_hugepage_range(file, addr, len)	(-EINVAL)
#define pmd_huge(x)	0
#define pud_huge(x)	0
#define is_hugepage_only_range(mm, addr, len)	0
#define hugetlb_free_pgd_range(tlb, addr, end, floor, ceiling) ({BUG(); 0; })
#define hugetlb_fault(mm, vma, addr, flags)	({ BUG(); 0; })
#define huge_pte_offset(mm, address)	0
#define dequeue_hwpoisoned_huge_page(page)	0
static inline void copy_huge_page(struct page *dst, struct page *src)
{
}

#define hugetlb_change_protection(vma, address, end, newprot)

#ifndef HPAGE_MASK
#define HPAGE_MASK	PAGE_MASK		/* Keep the compiler happy */
#define HPAGE_SIZE	PAGE_SIZE
#endif

#endif /* !CONFIG_HUGETLB_PAGE */

#define HUGETLB_ANON_FILE "anon_hugepage"

enum {
	/*
	 * The file will be used as an shm file so shmfs accounting rules
	 * apply
	 */
	HUGETLB_SHMFS_INODE     = 1,
	/*
	 * The file is being created on the internal vfs mount and shmfs
	 * accounting rules do not apply
	 */
	HUGETLB_ANONHUGE_INODE  = 2,
};

#ifdef CONFIG_HUGETLBFS
struct hugetlbfs_config {
	uid_t   uid;
	gid_t   gid;
	umode_t mode;
	long	nr_blocks;
	long	nr_inodes;
	struct hstate *hstate;
};

struct hugetlbfs_sb_info {
	long	max_blocks;   /* blocks allowed */
	long	free_blocks;  /* blocks free */
	long	max_inodes;   /* inodes allowed */
	long	free_inodes;  /* inodes free */
	spinlock_t	stat_lock;
	struct hstate *hstate;
};


struct hugetlbfs_inode_info {
	struct shared_policy policy;
	struct inode vfs_inode;
};

static inline struct hugetlbfs_inode_info *HUGETLBFS_I(struct inode *inode)
{
	return container_of(inode, struct hugetlbfs_inode_info, vfs_inode);
}

static inline struct hugetlbfs_sb_info *HUGETLBFS_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}

extern const struct file_operations hugetlbfs_file_operations;
extern const struct vm_operations_struct hugetlb_vm_ops;
struct file *hugetlb_file_setup(const char *name, size_t size, vm_flags_t acct,
				struct user_struct **user, int creat_flags);
int hugetlb_get_quota(struct address_space *mapping, long delta);
void hugetlb_put_quota(struct address_space *mapping, long delta);

static inline int is_file_hugepages(struct file *file)
{
	if (file->f_op == &hugetlbfs_file_operations)
		return 1;
	if (is_file_shm_hugepages(file))
		return 1;

	return 0;
}

static inline void set_file_hugepages(struct file *file)
{
	file->f_op = &hugetlbfs_file_operations;
}
#else /* !CONFIG_HUGETLBFS */

#define is_file_hugepages(file)			0
#define set_file_hugepages(file)		BUG()
static inline struct file *hugetlb_file_setup(const char *name, size_t size,
		vm_flags_t acctflag, struct user_struct **user, int creat_flags)
{
	return ERR_PTR(-ENOSYS);
}

#endif /* !CONFIG_HUGETLBFS */

#ifdef HAVE_ARCH_HUGETLB_UNMAPPED_AREA
unsigned long hugetlb_get_unmapped_area(struct file *file, unsigned long addr,
					unsigned long len, unsigned long pgoff,
					unsigned long flags);
#endif /* HAVE_ARCH_HUGETLB_UNMAPPED_AREA */

#ifdef CONFIG_HUGETLB_PAGE

#define HSTATE_NAME_LEN 32
/* Defines one hugetlb page size */
struct hstate {
	int next_nid_to_alloc;
	int next_nid_to_free;
	unsigned int order;
	unsigned long mask;
	unsigned long max_huge_pages;
	unsigned long nr_huge_pages;
	unsigned long free_huge_pages;
	unsigned long resv_huge_pages;
	unsigned long surplus_huge_pages;
	unsigned long nr_overcommit_huge_pages;
	struct list_head hugepage_freelists[MAX_NUMNODES];
	unsigned int nr_huge_pages_node[MAX_NUMNODES];
	unsigned int free_huge_pages_node[MAX_NUMNODES];
	unsigned int surplus_huge_pages_node[MAX_NUMNODES];
	char name[HSTATE_NAME_LEN];
};

struct huge_bootmem_page {
	struct list_head list;
	struct hstate *hstate;
#ifdef CONFIG_HIGHMEM
	phys_addr_t phys;
#endif
};

struct page *alloc_huge_page_node(struct hstate *h, int nid);

/* arch callback */
int __init alloc_bootmem_huge_page(struct hstate *h);

void __init hugetlb_add_hstate(unsigned order);
struct hstate *size_to_hstate(unsigned long size);

#ifndef HUGE_MAX_HSTATE
#define HUGE_MAX_HSTATE 1
#endif

extern struct hstate hstates[HUGE_MAX_HSTATE];
extern unsigned int default_hstate_idx;

#define default_hstate (hstates[default_hstate_idx])

static inline struct hstate *hstate_inode(struct inode *i)
{
	struct hugetlbfs_sb_info *hsb;
	hsb = HUGETLBFS_SB(i->i_sb);
	return hsb->hstate;
}

static inline struct hstate *hstate_file(struct file *f)
{
	return hstate_inode(f->f_dentry->d_inode);
}

static inline struct hstate *hstate_vma(struct vm_area_struct *vma)
{
	return hstate_file(vma->vm_file);
}

static inline unsigned long huge_page_size(struct hstate *h)
{
	return (unsigned long)PAGE_SIZE << h->order;
}

extern unsigned long vma_kernel_pagesize(struct vm_area_struct *vma);

extern unsigned long vma_mmu_pagesize(struct vm_area_struct *vma);

static inline unsigned long huge_page_mask(struct hstate *h)
{
	return h->mask;
}

static inline unsigned int huge_page_order(struct hstate *h)
{
	return h->order;
}

static inline unsigned huge_page_shift(struct hstate *h)
{
	return h->order + PAGE_SHIFT;
}

static inline unsigned int pages_per_huge_page(struct hstate *h)
{
	return 1 << h->order;
}

static inline unsigned int blocks_per_huge_page(struct hstate *h)
{
	return huge_page_size(h) / 512;
}

#include <asm/hugetlb.h>

static inline struct hstate *page_hstate(struct page *page)
{
	return size_to_hstate(PAGE_SIZE << compound_order(page));
}

static inline unsigned hstate_index_to_shift(unsigned index)
{
	return hstates[index].order + PAGE_SHIFT;
}

#else
struct hstate {};
#define alloc_huge_page_node(h, nid) NULL
#define alloc_bootmem_huge_page(h) NULL
#define hstate_file(f) NULL
#define hstate_vma(v) NULL
#define hstate_inode(i) NULL
#define huge_page_size(h) PAGE_SIZE
#define huge_page_mask(h) PAGE_MASK
#define vma_kernel_pagesize(v) PAGE_SIZE
#define vma_mmu_pagesize(v) PAGE_SIZE
#define huge_page_order(h) 0
#define huge_page_shift(h) PAGE_SHIFT
static inline unsigned int pages_per_huge_page(struct hstate *h)
{
	return 1;
}
#define hstate_index_to_shift(index) 0
#endif

#endif /* _LINUX_HUGETLB_H */
