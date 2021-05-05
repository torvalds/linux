#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/ktime.h>
#include <linux/debugfs.h>
#include "gup_test.h"

static void put_back_pages(unsigned int cmd, struct page **pages,
			   unsigned long nr_pages, unsigned int gup_test_flags)
{
	unsigned long i;

	switch (cmd) {
	case GUP_FAST_BENCHMARK:
	case GUP_BASIC_TEST:
		for (i = 0; i < nr_pages; i++)
			put_page(pages[i]);
		break;

	case PIN_FAST_BENCHMARK:
	case PIN_BASIC_TEST:
	case PIN_LONGTERM_BENCHMARK:
		unpin_user_pages(pages, nr_pages);
		break;
	case DUMP_USER_PAGES_TEST:
		if (gup_test_flags & GUP_TEST_FLAG_DUMP_PAGES_USE_PIN) {
			unpin_user_pages(pages, nr_pages);
		} else {
			for (i = 0; i < nr_pages; i++)
				put_page(pages[i]);

		}
		break;
	}
}

static void verify_dma_pinned(unsigned int cmd, struct page **pages,
			      unsigned long nr_pages)
{
	unsigned long i;
	struct page *page;

	switch (cmd) {
	case PIN_FAST_BENCHMARK:
	case PIN_BASIC_TEST:
	case PIN_LONGTERM_BENCHMARK:
		for (i = 0; i < nr_pages; i++) {
			page = pages[i];
			if (WARN(!page_maybe_dma_pinned(page),
				 "pages[%lu] is NOT dma-pinned\n", i)) {

				dump_page(page, "gup_test failure");
				break;
			}
		}
		break;
	}
}

static void dump_pages_test(struct gup_test *gup, struct page **pages,
			    unsigned long nr_pages)
{
	unsigned int index_to_dump;
	unsigned int i;

	/*
	 * Zero out any user-supplied page index that is out of range. Remember:
	 * .which_pages[] contains a 1-based set of page indices.
	 */
	for (i = 0; i < GUP_TEST_MAX_PAGES_TO_DUMP; i++) {
		if (gup->which_pages[i] > nr_pages) {
			pr_warn("ZEROING due to out of range: .which_pages[%u]: %u\n",
				i, gup->which_pages[i]);
			gup->which_pages[i] = 0;
		}
	}

	for (i = 0; i < GUP_TEST_MAX_PAGES_TO_DUMP; i++) {
		index_to_dump = gup->which_pages[i];

		if (index_to_dump) {
			index_to_dump--; // Decode from 1-based, to 0-based
			pr_info("---- page #%u, starting from user virt addr: 0x%llx\n",
				index_to_dump, gup->addr);
			dump_page(pages[index_to_dump],
				  "gup_test: dump_pages() test");
		}
	}
}

static int __gup_test_ioctl(unsigned int cmd,
		struct gup_test *gup)
{
	ktime_t start_time, end_time;
	unsigned long i, nr_pages, addr, next;
	long nr;
	struct page **pages;
	int ret = 0;
	bool needs_mmap_lock =
		cmd != GUP_FAST_BENCHMARK && cmd != PIN_FAST_BENCHMARK;

	if (gup->size > ULONG_MAX)
		return -EINVAL;

	nr_pages = gup->size / PAGE_SIZE;
	pages = kvcalloc(nr_pages, sizeof(void *), GFP_KERNEL);
	if (!pages)
		return -ENOMEM;

	if (needs_mmap_lock && mmap_read_lock_killable(current->mm)) {
		ret = -EINTR;
		goto free_pages;
	}

	i = 0;
	nr = gup->nr_pages_per_call;
	start_time = ktime_get();
	for (addr = gup->addr; addr < gup->addr + gup->size; addr = next) {
		if (nr != gup->nr_pages_per_call)
			break;

		next = addr + nr * PAGE_SIZE;
		if (next > gup->addr + gup->size) {
			next = gup->addr + gup->size;
			nr = (next - addr) / PAGE_SIZE;
		}

		switch (cmd) {
		case GUP_FAST_BENCHMARK:
			nr = get_user_pages_fast(addr, nr, gup->gup_flags,
						 pages + i);
			break;
		case GUP_BASIC_TEST:
			nr = get_user_pages(addr, nr, gup->gup_flags, pages + i,
					    NULL);
			break;
		case PIN_FAST_BENCHMARK:
			nr = pin_user_pages_fast(addr, nr, gup->gup_flags,
						 pages + i);
			break;
		case PIN_BASIC_TEST:
			nr = pin_user_pages(addr, nr, gup->gup_flags, pages + i,
					    NULL);
			break;
		case PIN_LONGTERM_BENCHMARK:
			nr = pin_user_pages(addr, nr,
					    gup->gup_flags | FOLL_LONGTERM,
					    pages + i, NULL);
			break;
		case DUMP_USER_PAGES_TEST:
			if (gup->test_flags & GUP_TEST_FLAG_DUMP_PAGES_USE_PIN)
				nr = pin_user_pages(addr, nr, gup->gup_flags,
						    pages + i, NULL);
			else
				nr = get_user_pages(addr, nr, gup->gup_flags,
						    pages + i, NULL);
			break;
		default:
			ret = -EINVAL;
			goto unlock;
		}

		if (nr <= 0)
			break;
		i += nr;
	}
	end_time = ktime_get();

	/* Shifting the meaning of nr_pages: now it is actual number pinned: */
	nr_pages = i;

	gup->get_delta_usec = ktime_us_delta(end_time, start_time);
	gup->size = addr - gup->addr;

	/*
	 * Take an un-benchmark-timed moment to verify DMA pinned
	 * state: print a warning if any non-dma-pinned pages are found:
	 */
	verify_dma_pinned(cmd, pages, nr_pages);

	if (cmd == DUMP_USER_PAGES_TEST)
		dump_pages_test(gup, pages, nr_pages);

	start_time = ktime_get();

	put_back_pages(cmd, pages, nr_pages, gup->test_flags);

	end_time = ktime_get();
	gup->put_delta_usec = ktime_us_delta(end_time, start_time);

unlock:
	if (needs_mmap_lock)
		mmap_read_unlock(current->mm);
free_pages:
	kvfree(pages);
	return ret;
}

static long gup_test_ioctl(struct file *filep, unsigned int cmd,
		unsigned long arg)
{
	struct gup_test gup;
	int ret;

	switch (cmd) {
	case GUP_FAST_BENCHMARK:
	case PIN_FAST_BENCHMARK:
	case PIN_LONGTERM_BENCHMARK:
	case GUP_BASIC_TEST:
	case PIN_BASIC_TEST:
	case DUMP_USER_PAGES_TEST:
		break;
	default:
		return -EINVAL;
	}

	if (copy_from_user(&gup, (void __user *)arg, sizeof(gup)))
		return -EFAULT;

	ret = __gup_test_ioctl(cmd, &gup);
	if (ret)
		return ret;

	if (copy_to_user((void __user *)arg, &gup, sizeof(gup)))
		return -EFAULT;

	return 0;
}

static const struct file_operations gup_test_fops = {
	.open = nonseekable_open,
	.unlocked_ioctl = gup_test_ioctl,
};

static int __init gup_test_init(void)
{
	debugfs_create_file_unsafe("gup_test", 0600, NULL, NULL,
				   &gup_test_fops);

	return 0;
}

late_initcall(gup_test_init);
