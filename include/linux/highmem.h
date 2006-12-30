#ifndef _LINUX_HIGHMEM_H
#define _LINUX_HIGHMEM_H

#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/uaccess.h>

#include <asm/cacheflush.h>

#ifndef ARCH_HAS_FLUSH_ANON_PAGE
static inline void flush_anon_page(struct vm_area_struct *vma, struct page *page, unsigned long vmaddr)
{
}
#endif

#ifndef ARCH_HAS_FLUSH_KERNEL_DCACHE_PAGE
static inline void flush_kernel_dcache_page(struct page *page)
{
}
#endif

#ifdef CONFIG_HIGHMEM

#include <asm/highmem.h>

/* declarations for linux/mm/highmem.c */
unsigned int nr_free_highpages(void);
extern unsigned long totalhigh_pages;

#else /* CONFIG_HIGHMEM */

static inline unsigned int nr_free_highpages(void) { return 0; }

#define totalhigh_pages 0

#ifndef ARCH_HAS_KMAP
static inline void *kmap(struct page *page)
{
	might_sleep();
	return page_address(page);
}

#define kunmap(page) do { (void) (page); } while (0)

#define kmap_atomic(page, idx) \
	({ pagefault_disable(); page_address(page); })
#define kunmap_atomic(addr, idx)	do { pagefault_enable(); } while (0)
#define kmap_atomic_pfn(pfn, idx)	kmap_atomic(pfn_to_page(pfn), (idx))
#define kmap_atomic_to_page(ptr)	virt_to_page(ptr)
#endif

#endif /* CONFIG_HIGHMEM */

/* when CONFIG_HIGHMEM is not set these will be plain clear/copy_page */
static inline void clear_user_highpage(struct page *page, unsigned long vaddr)
{
	void *addr = kmap_atomic(page, KM_USER0);
	clear_user_page(addr, vaddr, page);
	kunmap_atomic(addr, KM_USER0);
	/* Make sure this page is cleared on other CPU's too before using it */
	smp_wmb();
}

#ifndef __HAVE_ARCH_ALLOC_ZEROED_USER_HIGHPAGE
static inline struct page *
alloc_zeroed_user_highpage(struct vm_area_struct *vma, unsigned long vaddr)
{
	struct page *page = alloc_page_vma(GFP_HIGHUSER, vma, vaddr);

	if (page)
		clear_user_highpage(page, vaddr);

	return page;
}
#endif

static inline void clear_highpage(struct page *page)
{
	void *kaddr = kmap_atomic(page, KM_USER0);
	clear_page(kaddr);
	kunmap_atomic(kaddr, KM_USER0);
}

/*
 * Same but also flushes aliased cache contents to RAM.
 */
static inline void memclear_highpage_flush(struct page *page, unsigned int offset, unsigned int size)
{
	void *kaddr;

	BUG_ON(offset + size > PAGE_SIZE);

	kaddr = kmap_atomic(page, KM_USER0);
	memset((char *)kaddr + offset, 0, size);
	flush_dcache_page(page);
	kunmap_atomic(kaddr, KM_USER0);
}

#ifndef __HAVE_ARCH_COPY_USER_HIGHPAGE

static inline void copy_user_highpage(struct page *to, struct page *from,
	unsigned long vaddr, struct vm_area_struct *vma)
{
	char *vfrom, *vto;

	vfrom = kmap_atomic(from, KM_USER0);
	vto = kmap_atomic(to, KM_USER1);
	copy_user_page(vto, vfrom, vaddr, to);
	kunmap_atomic(vfrom, KM_USER0);
	kunmap_atomic(vto, KM_USER1);
	/* Make sure this page is cleared on other CPU's too before using it */
	smp_wmb();
}

#endif

static inline void copy_highpage(struct page *to, struct page *from)
{
	char *vfrom, *vto;

	vfrom = kmap_atomic(from, KM_USER0);
	vto = kmap_atomic(to, KM_USER1);
	copy_page(vto, vfrom);
	kunmap_atomic(vfrom, KM_USER0);
	kunmap_atomic(vto, KM_USER1);
}

#endif /* _LINUX_HIGHMEM_H */
