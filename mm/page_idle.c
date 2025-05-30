// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/memblock.h>
#include <linux/fs.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/memory_hotplug.h>
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/pagemap.h>
#include <linux/rmap.h>
#include <linux/mmu_notifier.h>
#include <linux/page_ext.h>
#include <linux/page_idle.h>

#include "internal.h"

#define BITMAP_CHUNK_SIZE	sizeof(u64)
#define BITMAP_CHUNK_BITS	(BITMAP_CHUNK_SIZE * BITS_PER_BYTE)

/*
 * Idle page tracking only considers user memory pages, for other types of
 * pages the idle flag is always unset and an attempt to set it is silently
 * ignored.
 *
 * We treat a page as a user memory page if it is on an LRU list, because it is
 * always safe to pass such a page to rmap_walk(), which is essential for idle
 * page tracking. With such an indicator of user pages we can skip isolated
 * pages, but since there are not usually many of them, it will hardly affect
 * the overall result.
 *
 * This function tries to get a user memory page by pfn as described above.
 */
static struct folio *page_idle_get_folio(unsigned long pfn)
{
	struct page *page = pfn_to_online_page(pfn);
	struct folio *folio;

	if (!page || PageTail(page))
		return NULL;

	folio = page_folio(page);
	if (!folio_test_lru(folio) || !folio_try_get(folio))
		return NULL;
	if (unlikely(page_folio(page) != folio || !folio_test_lru(folio))) {
		folio_put(folio);
		folio = NULL;
	}
	return folio;
}

static bool page_idle_clear_pte_refs_one(struct folio *folio,
					struct vm_area_struct *vma,
					unsigned long addr, void *arg)
{
	DEFINE_FOLIO_VMA_WALK(pvmw, folio, vma, addr, 0);
	bool referenced = false;

	while (page_vma_mapped_walk(&pvmw)) {
		addr = pvmw.address;
		if (pvmw.pte) {
			/*
			 * For PTE-mapped THP, one sub page is referenced,
			 * the whole THP is referenced.
			 *
			 * PFN swap PTEs, such as device-exclusive ones, that
			 * actually map pages are "old" from a CPU perspective.
			 * The MMU notifier takes care of any device aspects.
			 */
			if (likely(pte_present(ptep_get(pvmw.pte))))
				referenced |= ptep_test_and_clear_young(vma, addr, pvmw.pte);
			referenced |= mmu_notifier_clear_young(vma->vm_mm, addr, addr + PAGE_SIZE);
		} else if (IS_ENABLED(CONFIG_TRANSPARENT_HUGEPAGE)) {
			if (pmdp_clear_young_notify(vma, addr, pvmw.pmd))
				referenced = true;
		} else {
			/* unexpected pmd-mapped page? */
			WARN_ON_ONCE(1);
		}
	}

	if (referenced) {
		folio_clear_idle(folio);
		/*
		 * We cleared the referenced bit in a mapping to this page. To
		 * avoid interference with page reclaim, mark it young so that
		 * folio_referenced() will return > 0.
		 */
		folio_set_young(folio);
	}
	return true;
}

static void page_idle_clear_pte_refs(struct folio *folio)
{
	/*
	 * Since rwc.try_lock is unused, rwc is effectively immutable, so we
	 * can make it static to save some cycles and stack.
	 */
	static struct rmap_walk_control rwc = {
		.rmap_one = page_idle_clear_pte_refs_one,
		.anon_lock = folio_lock_anon_vma_read,
	};
	bool need_lock;

	if (!folio_mapped(folio) || !folio_raw_mapping(folio))
		return;

	need_lock = !folio_test_anon(folio) || folio_test_ksm(folio);
	if (need_lock && !folio_trylock(folio))
		return;

	rmap_walk(folio, &rwc);

	if (need_lock)
		folio_unlock(folio);
}

static ssize_t page_idle_bitmap_read(struct file *file, struct kobject *kobj,
				     const struct bin_attribute *attr, char *buf,
				     loff_t pos, size_t count)
{
	u64 *out = (u64 *)buf;
	struct folio *folio;
	unsigned long pfn, end_pfn;
	int bit;

	if (pos % BITMAP_CHUNK_SIZE || count % BITMAP_CHUNK_SIZE)
		return -EINVAL;

	pfn = pos * BITS_PER_BYTE;
	if (pfn >= max_pfn)
		return 0;

	end_pfn = pfn + count * BITS_PER_BYTE;
	if (end_pfn > max_pfn)
		end_pfn = max_pfn;

	for (; pfn < end_pfn; pfn++) {
		bit = pfn % BITMAP_CHUNK_BITS;
		if (!bit)
			*out = 0ULL;
		folio = page_idle_get_folio(pfn);
		if (folio) {
			if (folio_test_idle(folio)) {
				/*
				 * The page might have been referenced via a
				 * pte, in which case it is not idle. Clear
				 * refs and recheck.
				 */
				page_idle_clear_pte_refs(folio);
				if (folio_test_idle(folio))
					*out |= 1ULL << bit;
			}
			folio_put(folio);
		}
		if (bit == BITMAP_CHUNK_BITS - 1)
			out++;
		cond_resched();
	}
	return (char *)out - buf;
}

static ssize_t page_idle_bitmap_write(struct file *file, struct kobject *kobj,
				      const struct bin_attribute *attr, char *buf,
				      loff_t pos, size_t count)
{
	const u64 *in = (u64 *)buf;
	struct folio *folio;
	unsigned long pfn, end_pfn;
	int bit;

	if (pos % BITMAP_CHUNK_SIZE || count % BITMAP_CHUNK_SIZE)
		return -EINVAL;

	pfn = pos * BITS_PER_BYTE;
	if (pfn >= max_pfn)
		return -ENXIO;

	end_pfn = pfn + count * BITS_PER_BYTE;
	if (end_pfn > max_pfn)
		end_pfn = max_pfn;

	for (; pfn < end_pfn; pfn++) {
		bit = pfn % BITMAP_CHUNK_BITS;
		if ((*in >> bit) & 1) {
			folio = page_idle_get_folio(pfn);
			if (folio) {
				page_idle_clear_pte_refs(folio);
				folio_set_idle(folio);
				folio_put(folio);
			}
		}
		if (bit == BITMAP_CHUNK_BITS - 1)
			in++;
		cond_resched();
	}
	return (char *)in - buf;
}

static const struct bin_attribute page_idle_bitmap_attr =
		__BIN_ATTR(bitmap, 0600,
			   page_idle_bitmap_read, page_idle_bitmap_write, 0);

static const struct bin_attribute *const page_idle_bin_attrs[] = {
	&page_idle_bitmap_attr,
	NULL,
};

static const struct attribute_group page_idle_attr_group = {
	.bin_attrs = page_idle_bin_attrs,
	.name = "page_idle",
};

static int __init page_idle_init(void)
{
	int err;

	err = sysfs_create_group(mm_kobj, &page_idle_attr_group);
	if (err) {
		pr_err("page_idle: register sysfs failed\n");
		return err;
	}
	return 0;
}
subsys_initcall(page_idle_init);
