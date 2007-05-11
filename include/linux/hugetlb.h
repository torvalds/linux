#ifndef _LINUX_HUGETLB_H
#define _LINUX_HUGETLB_H

#ifdef CONFIG_HUGETLB_PAGE

#include <linux/mempolicy.h>
#include <linux/shm.h>
#include <asm/tlbflush.h>

struct ctl_table;

static inline int is_vm_hugetlb_page(struct vm_area_struct *vma)
{
	return vma->vm_flags & VM_HUGETLB;
}

int hugetlb_sysctl_handler(struct ctl_table *, int, struct file *, void __user *, size_t *, loff_t *);
int copy_hugetlb_page_range(struct mm_struct *, struct mm_struct *, struct vm_area_struct *);
int follow_hugetlb_page(struct mm_struct *, struct vm_area_struct *, struct page **, struct vm_area_struct **, unsigned long *, int *, int);
void unmap_hugepage_range(struct vm_area_struct *, unsigned long, unsigned long);
void __unmap_hugepage_range(struct vm_area_struct *, unsigned long, unsigned long);
int hugetlb_prefault(struct address_space *, struct vm_area_struct *);
int hugetlb_report_meminfo(char *);
int hugetlb_report_node_meminfo(int, char *);
unsigned long hugetlb_total_pages(void);
int hugetlb_fault(struct mm_struct *mm, struct vm_area_struct *vma,
			unsigned long address, int write_access);
int hugetlb_reserve_pages(struct inode *inode, long from, long to);
void hugetlb_unreserve_pages(struct inode *inode, long offset, long freed);

extern unsigned long max_huge_pages;
extern const unsigned long hugetlb_zero, hugetlb_infinity;
extern int sysctl_hugetlb_shm_group;

/* arch callbacks */

pte_t *huge_pte_alloc(struct mm_struct *mm, unsigned long addr);
pte_t *huge_pte_offset(struct mm_struct *mm, unsigned long addr);
int huge_pmd_unshare(struct mm_struct *mm, unsigned long *addr, pte_t *ptep);
struct page *follow_huge_addr(struct mm_struct *mm, unsigned long address,
			      int write);
struct page *follow_huge_pmd(struct mm_struct *mm, unsigned long address,
				pmd_t *pmd, int write);
int pmd_huge(pmd_t pmd);
void hugetlb_change_protection(struct vm_area_struct *vma,
		unsigned long address, unsigned long end, pgprot_t newprot);

#ifndef ARCH_HAS_HUGEPAGE_ONLY_RANGE
#define is_hugepage_only_range(mm, addr, len)	0
#endif

#ifndef ARCH_HAS_HUGETLB_FREE_PGD_RANGE
#define hugetlb_free_pgd_range	free_pgd_range
#else
void hugetlb_free_pgd_range(struct mmu_gather **tlb, unsigned long addr,
			    unsigned long end, unsigned long floor,
			    unsigned long ceiling);
#endif

#ifndef ARCH_HAS_PREPARE_HUGEPAGE_RANGE
/*
 * If the arch doesn't supply something else, assume that hugepage
 * size aligned regions are ok without further preparation.
 */
static inline int prepare_hugepage_range(unsigned long addr, unsigned long len,
						pgoff_t pgoff)
{
	if (pgoff & (~HPAGE_MASK >> PAGE_SHIFT))
		return -EINVAL;
	if (len & ~HPAGE_MASK)
		return -EINVAL;
	if (addr & ~HPAGE_MASK)
		return -EINVAL;
	return 0;
}
#else
int prepare_hugepage_range(unsigned long addr, unsigned long len,
						pgoff_t pgoff);
#endif

#ifndef ARCH_HAS_SETCLEAR_HUGE_PTE
#define set_huge_pte_at(mm, addr, ptep, pte)	set_pte_at(mm, addr, ptep, pte)
#define huge_ptep_get_and_clear(mm, addr, ptep) ptep_get_and_clear(mm, addr, ptep)
#else
void set_huge_pte_at(struct mm_struct *mm, unsigned long addr,
		     pte_t *ptep, pte_t pte);
pte_t huge_ptep_get_and_clear(struct mm_struct *mm, unsigned long addr,
			      pte_t *ptep);
#endif

#ifndef ARCH_HAS_HUGETLB_PREFAULT_HOOK
#define hugetlb_prefault_arch_hook(mm)		do { } while (0)
#else
void hugetlb_prefault_arch_hook(struct mm_struct *mm);
#endif

#else /* !CONFIG_HUGETLB_PAGE */

static inline int is_vm_hugetlb_page(struct vm_area_struct *vma)
{
	return 0;
}
static inline unsigned long hugetlb_total_pages(void)
{
	return 0;
}

#define follow_hugetlb_page(m,v,p,vs,a,b,i)	({ BUG(); 0; })
#define follow_huge_addr(mm, addr, write)	ERR_PTR(-EINVAL)
#define copy_hugetlb_page_range(src, dst, vma)	({ BUG(); 0; })
#define hugetlb_prefault(mapping, vma)		({ BUG(); 0; })
#define unmap_hugepage_range(vma, start, end)	BUG()
#define hugetlb_report_meminfo(buf)		0
#define hugetlb_report_node_meminfo(n, buf)	0
#define follow_huge_pmd(mm, addr, pmd, write)	NULL
#define prepare_hugepage_range(addr,len,pgoff)	(-EINVAL)
#define pmd_huge(x)	0
#define is_hugepage_only_range(mm, addr, len)	0
#define hugetlb_free_pgd_range(tlb, addr, end, floor, ceiling) ({BUG(); 0; })
#define hugetlb_fault(mm, vma, addr, write)	({ BUG(); 0; })

#define hugetlb_change_protection(vma, address, end, newprot)

#ifndef HPAGE_MASK
#define HPAGE_MASK	PAGE_MASK		/* Keep the compiler happy */
#define HPAGE_SIZE	PAGE_SIZE
#endif

#endif /* !CONFIG_HUGETLB_PAGE */

#ifdef CONFIG_HUGETLBFS
struct hugetlbfs_config {
	uid_t   uid;
	gid_t   gid;
	umode_t mode;
	long	nr_blocks;
	long	nr_inodes;
};

struct hugetlbfs_sb_info {
	long	max_blocks;   /* blocks allowed */
	long	free_blocks;  /* blocks free */
	long	max_inodes;   /* inodes allowed */
	long	free_inodes;  /* inodes free */
	spinlock_t	stat_lock;
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
extern struct vm_operations_struct hugetlb_vm_ops;
struct file *hugetlb_zero_setup(size_t);
int hugetlb_get_quota(struct address_space *mapping);
void hugetlb_put_quota(struct address_space *mapping);

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

#define is_file_hugepages(file)		0
#define set_file_hugepages(file)	BUG()
#define hugetlb_zero_setup(size)	ERR_PTR(-ENOSYS)

#endif /* !CONFIG_HUGETLBFS */

#ifdef HAVE_ARCH_HUGETLB_UNMAPPED_AREA
unsigned long hugetlb_get_unmapped_area(struct file *file, unsigned long addr,
					unsigned long len, unsigned long pgoff,
					unsigned long flags);
#endif /* HAVE_ARCH_HUGETLB_UNMAPPED_AREA */

#endif /* _LINUX_HUGETLB_H */
