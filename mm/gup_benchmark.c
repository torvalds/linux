#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/ktime.h>
#include <linux/debugfs.h>

#define GUP_FAST_BENCHMARK	_IOWR('g', 1, struct gup_benchmark)
#define GUP_BENCHMARK		_IOWR('g', 2, struct gup_benchmark)
#define PIN_FAST_BENCHMARK	_IOWR('g', 3, struct gup_benchmark)
#define PIN_BENCHMARK		_IOWR('g', 4, struct gup_benchmark)
#define PIN_LONGTERM_BENCHMARK	_IOWR('g', 5, struct gup_benchmark)

struct gup_benchmark {
	__u64 get_delta_usec;
	__u64 put_delta_usec;
	__u64 addr;
	__u64 size;
	__u32 nr_pages_per_call;
	__u32 flags;
	__u64 expansion[10];	/* For future use */
};

static void put_back_pages(unsigned int cmd, struct page **pages,
			   unsigned long nr_pages)
{
	unsigned long i;

	switch (cmd) {
	case GUP_FAST_BENCHMARK:
	case GUP_BENCHMARK:
		for (i = 0; i < nr_pages; i++)
			put_page(pages[i]);
		break;

	case PIN_FAST_BENCHMARK:
	case PIN_BENCHMARK:
	case PIN_LONGTERM_BENCHMARK:
		unpin_user_pages(pages, nr_pages);
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
	case PIN_BENCHMARK:
	case PIN_LONGTERM_BENCHMARK:
		for (i = 0; i < nr_pages; i++) {
			page = pages[i];
			if (WARN(!page_maybe_dma_pinned(page),
				 "pages[%lu] is NOT dma-pinned\n", i)) {

				dump_page(page, "gup_benchmark failure");
				break;
			}
		}
		break;
	}
}

static int __gup_benchmark_ioctl(unsigned int cmd,
		struct gup_benchmark *gup)
{
	ktime_t start_time, end_time;
	unsigned long i, nr_pages, addr, next;
	int nr;
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

		/* Filter out most gup flags: only allow a tiny subset here: */
		gup->flags &= FOLL_WRITE;

		switch (cmd) {
		case GUP_FAST_BENCHMARK:
			nr = get_user_pages_fast(addr, nr, gup->flags,
						 pages + i);
			break;
		case GUP_BENCHMARK:
			nr = get_user_pages(addr, nr, gup->flags, pages + i,
					    NULL);
			break;
		case PIN_FAST_BENCHMARK:
			nr = pin_user_pages_fast(addr, nr, gup->flags,
						 pages + i);
			break;
		case PIN_BENCHMARK:
			nr = pin_user_pages(addr, nr, gup->flags, pages + i,
					    NULL);
			break;
		case PIN_LONGTERM_BENCHMARK:
			nr = pin_user_pages(addr, nr,
					    gup->flags | FOLL_LONGTERM,
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

	start_time = ktime_get();

	put_back_pages(cmd, pages, nr_pages);

	end_time = ktime_get();
	gup->put_delta_usec = ktime_us_delta(end_time, start_time);

unlock:
	if (needs_mmap_lock)
		mmap_read_unlock(current->mm);
free_pages:
	kvfree(pages);
	return ret;
}

static long gup_benchmark_ioctl(struct file *filep, unsigned int cmd,
		unsigned long arg)
{
	struct gup_benchmark gup;
	int ret;

	switch (cmd) {
	case GUP_FAST_BENCHMARK:
	case GUP_BENCHMARK:
	case PIN_FAST_BENCHMARK:
	case PIN_BENCHMARK:
	case PIN_LONGTERM_BENCHMARK:
		break;
	default:
		return -EINVAL;
	}

	if (copy_from_user(&gup, (void __user *)arg, sizeof(gup)))
		return -EFAULT;

	ret = __gup_benchmark_ioctl(cmd, &gup);
	if (ret)
		return ret;

	if (copy_to_user((void __user *)arg, &gup, sizeof(gup)))
		return -EFAULT;

	return 0;
}

static const struct file_operations gup_benchmark_fops = {
	.open = nonseekable_open,
	.unlocked_ioctl = gup_benchmark_ioctl,
};

static int gup_benchmark_init(void)
{
	debugfs_create_file_unsafe("gup_benchmark", 0600, NULL, NULL,
				   &gup_benchmark_fops);

	return 0;
}

late_initcall(gup_benchmark_init);
