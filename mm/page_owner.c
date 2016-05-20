#include <linux/debugfs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/bootmem.h>
#include <linux/stacktrace.h>
#include <linux/page_owner.h>
#include <linux/jump_label.h>
#include <linux/migrate.h>
#include "internal.h"

static bool page_owner_disabled = true;
DEFINE_STATIC_KEY_FALSE(page_owner_inited);

static void init_early_allocated_pages(void);

static int early_page_owner_param(char *buf)
{
	if (!buf)
		return -EINVAL;

	if (strcmp(buf, "on") == 0)
		page_owner_disabled = false;

	return 0;
}
early_param("page_owner", early_page_owner_param);

static bool need_page_owner(void)
{
	if (page_owner_disabled)
		return false;

	return true;
}

static void init_page_owner(void)
{
	if (page_owner_disabled)
		return;

	static_branch_enable(&page_owner_inited);
	init_early_allocated_pages();
}

struct page_ext_operations page_owner_ops = {
	.need = need_page_owner,
	.init = init_page_owner,
};

void __reset_page_owner(struct page *page, unsigned int order)
{
	int i;
	struct page_ext *page_ext;

	for (i = 0; i < (1 << order); i++) {
		page_ext = lookup_page_ext(page + i);
		__clear_bit(PAGE_EXT_OWNER, &page_ext->flags);
	}
}

void __set_page_owner(struct page *page, unsigned int order, gfp_t gfp_mask)
{
	struct page_ext *page_ext = lookup_page_ext(page);
	struct stack_trace trace = {
		.nr_entries = 0,
		.max_entries = ARRAY_SIZE(page_ext->trace_entries),
		.entries = &page_ext->trace_entries[0],
		.skip = 3,
	};

	save_stack_trace(&trace);

	page_ext->order = order;
	page_ext->gfp_mask = gfp_mask;
	page_ext->nr_entries = trace.nr_entries;
	page_ext->last_migrate_reason = -1;

	__set_bit(PAGE_EXT_OWNER, &page_ext->flags);
}

void __set_page_owner_migrate_reason(struct page *page, int reason)
{
	struct page_ext *page_ext = lookup_page_ext(page);

	page_ext->last_migrate_reason = reason;
}

gfp_t __get_page_owner_gfp(struct page *page)
{
	struct page_ext *page_ext = lookup_page_ext(page);

	return page_ext->gfp_mask;
}

void __copy_page_owner(struct page *oldpage, struct page *newpage)
{
	struct page_ext *old_ext = lookup_page_ext(oldpage);
	struct page_ext *new_ext = lookup_page_ext(newpage);
	int i;

	new_ext->order = old_ext->order;
	new_ext->gfp_mask = old_ext->gfp_mask;
	new_ext->nr_entries = old_ext->nr_entries;

	for (i = 0; i < ARRAY_SIZE(new_ext->trace_entries); i++)
		new_ext->trace_entries[i] = old_ext->trace_entries[i];

	/*
	 * We don't clear the bit on the oldpage as it's going to be freed
	 * after migration. Until then, the info can be useful in case of
	 * a bug, and the overal stats will be off a bit only temporarily.
	 * Also, migrate_misplaced_transhuge_page() can still fail the
	 * migration and then we want the oldpage to retain the info. But
	 * in that case we also don't need to explicitly clear the info from
	 * the new page, which will be freed.
	 */
	__set_bit(PAGE_EXT_OWNER, &new_ext->flags);
}

static ssize_t
print_page_owner(char __user *buf, size_t count, unsigned long pfn,
		struct page *page, struct page_ext *page_ext)
{
	int ret;
	int pageblock_mt, page_mt;
	char *kbuf;
	struct stack_trace trace = {
		.nr_entries = page_ext->nr_entries,
		.entries = &page_ext->trace_entries[0],
	};

	kbuf = kmalloc(count, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	ret = snprintf(kbuf, count,
			"Page allocated via order %u, mask %#x(%pGg)\n",
			page_ext->order, page_ext->gfp_mask,
			&page_ext->gfp_mask);

	if (ret >= count)
		goto err;

	/* Print information relevant to grouping pages by mobility */
	pageblock_mt = get_pfnblock_migratetype(page, pfn);
	page_mt  = gfpflags_to_migratetype(page_ext->gfp_mask);
	ret += snprintf(kbuf + ret, count - ret,
			"PFN %lu type %s Block %lu type %s Flags %#lx(%pGp)\n",
			pfn,
			migratetype_names[page_mt],
			pfn >> pageblock_order,
			migratetype_names[pageblock_mt],
			page->flags, &page->flags);

	if (ret >= count)
		goto err;

	ret += snprint_stack_trace(kbuf + ret, count - ret, &trace, 0);
	if (ret >= count)
		goto err;

	if (page_ext->last_migrate_reason != -1) {
		ret += snprintf(kbuf + ret, count - ret,
			"Page has been migrated, last migrate reason: %s\n",
			migrate_reason_names[page_ext->last_migrate_reason]);
		if (ret >= count)
			goto err;
	}

	ret += snprintf(kbuf + ret, count - ret, "\n");
	if (ret >= count)
		goto err;

	if (copy_to_user(buf, kbuf, ret))
		ret = -EFAULT;

	kfree(kbuf);
	return ret;

err:
	kfree(kbuf);
	return -ENOMEM;
}

void __dump_page_owner(struct page *page)
{
	struct page_ext *page_ext = lookup_page_ext(page);
	struct stack_trace trace = {
		.nr_entries = page_ext->nr_entries,
		.entries = &page_ext->trace_entries[0],
	};
	gfp_t gfp_mask = page_ext->gfp_mask;
	int mt = gfpflags_to_migratetype(gfp_mask);

	if (!test_bit(PAGE_EXT_OWNER, &page_ext->flags)) {
		pr_alert("page_owner info is not active (free page?)\n");
		return;
	}

	pr_alert("page allocated via order %u, migratetype %s, gfp_mask %#x(%pGg)\n",
		 page_ext->order, migratetype_names[mt], gfp_mask, &gfp_mask);
	print_stack_trace(&trace, 0);

	if (page_ext->last_migrate_reason != -1)
		pr_alert("page has been migrated, last migrate reason: %s\n",
			migrate_reason_names[page_ext->last_migrate_reason]);
}

static ssize_t
read_page_owner(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	unsigned long pfn;
	struct page *page;
	struct page_ext *page_ext;

	if (!static_branch_unlikely(&page_owner_inited))
		return -EINVAL;

	page = NULL;
	pfn = min_low_pfn + *ppos;

	/* Find a valid PFN or the start of a MAX_ORDER_NR_PAGES area */
	while (!pfn_valid(pfn) && (pfn & (MAX_ORDER_NR_PAGES - 1)) != 0)
		pfn++;

	drain_all_pages(NULL);

	/* Find an allocated page */
	for (; pfn < max_pfn; pfn++) {
		/*
		 * If the new page is in a new MAX_ORDER_NR_PAGES area,
		 * validate the area as existing, skip it if not
		 */
		if ((pfn & (MAX_ORDER_NR_PAGES - 1)) == 0 && !pfn_valid(pfn)) {
			pfn += MAX_ORDER_NR_PAGES - 1;
			continue;
		}

		/* Check for holes within a MAX_ORDER area */
		if (!pfn_valid_within(pfn))
			continue;

		page = pfn_to_page(pfn);
		if (PageBuddy(page)) {
			unsigned long freepage_order = page_order_unsafe(page);

			if (freepage_order < MAX_ORDER)
				pfn += (1UL << freepage_order) - 1;
			continue;
		}

		page_ext = lookup_page_ext(page);

		/*
		 * Some pages could be missed by concurrent allocation or free,
		 * because we don't hold the zone lock.
		 */
		if (!test_bit(PAGE_EXT_OWNER, &page_ext->flags))
			continue;

		/* Record the next PFN to read in the file offset */
		*ppos = (pfn - min_low_pfn) + 1;

		return print_page_owner(buf, count, pfn, page, page_ext);
	}

	return 0;
}

static void init_pages_in_zone(pg_data_t *pgdat, struct zone *zone)
{
	struct page *page;
	struct page_ext *page_ext;
	unsigned long pfn = zone->zone_start_pfn, block_end_pfn;
	unsigned long end_pfn = pfn + zone->spanned_pages;
	unsigned long count = 0;

	/* Scan block by block. First and last block may be incomplete */
	pfn = zone->zone_start_pfn;

	/*
	 * Walk the zone in pageblock_nr_pages steps. If a page block spans
	 * a zone boundary, it will be double counted between zones. This does
	 * not matter as the mixed block count will still be correct
	 */
	for (; pfn < end_pfn; ) {
		if (!pfn_valid(pfn)) {
			pfn = ALIGN(pfn + 1, MAX_ORDER_NR_PAGES);
			continue;
		}

		block_end_pfn = ALIGN(pfn + 1, pageblock_nr_pages);
		block_end_pfn = min(block_end_pfn, end_pfn);

		page = pfn_to_page(pfn);

		for (; pfn < block_end_pfn; pfn++) {
			if (!pfn_valid_within(pfn))
				continue;

			page = pfn_to_page(pfn);

			if (page_zone(page) != zone)
				continue;

			/*
			 * We are safe to check buddy flag and order, because
			 * this is init stage and only single thread runs.
			 */
			if (PageBuddy(page)) {
				pfn += (1UL << page_order(page)) - 1;
				continue;
			}

			if (PageReserved(page))
				continue;

			page_ext = lookup_page_ext(page);

			/* Maybe overraping zone */
			if (test_bit(PAGE_EXT_OWNER, &page_ext->flags))
				continue;

			/* Found early allocated page */
			set_page_owner(page, 0, 0);
			count++;
		}
	}

	pr_info("Node %d, zone %8s: page owner found early allocated %lu pages\n",
		pgdat->node_id, zone->name, count);
}

static void init_zones_in_node(pg_data_t *pgdat)
{
	struct zone *zone;
	struct zone *node_zones = pgdat->node_zones;
	unsigned long flags;

	for (zone = node_zones; zone - node_zones < MAX_NR_ZONES; ++zone) {
		if (!populated_zone(zone))
			continue;

		spin_lock_irqsave(&zone->lock, flags);
		init_pages_in_zone(pgdat, zone);
		spin_unlock_irqrestore(&zone->lock, flags);
	}
}

static void init_early_allocated_pages(void)
{
	pg_data_t *pgdat;

	drain_all_pages(NULL);
	for_each_online_pgdat(pgdat)
		init_zones_in_node(pgdat);
}

static const struct file_operations proc_page_owner_operations = {
	.read		= read_page_owner,
};

static int __init pageowner_init(void)
{
	struct dentry *dentry;

	if (!static_branch_unlikely(&page_owner_inited)) {
		pr_info("page_owner is disabled\n");
		return 0;
	}

	dentry = debugfs_create_file("page_owner", S_IRUSR, NULL,
			NULL, &proc_page_owner_operations);
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);

	return 0;
}
late_initcall(pageowner_init)
