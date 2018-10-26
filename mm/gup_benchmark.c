#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/ktime.h>
#include <linux/debugfs.h>

#define GUP_FAST_BENCHMARK	_IOWR('g', 1, struct gup_benchmark)

struct gup_benchmark {
	__u64 get_delta_usec;
	__u64 put_delta_usec;
	__u64 addr;
	__u64 size;
	__u32 nr_pages_per_call;
	__u32 flags;
	__u64 expansion[10];	/* For future use */
};

static int __gup_benchmark_ioctl(unsigned int cmd,
		struct gup_benchmark *gup)
{
	ktime_t start_time, end_time;
	unsigned long i, nr_pages, addr, next;
	int nr;
	struct page **pages;

	nr_pages = gup->size / PAGE_SIZE;
	pages = kvcalloc(nr_pages, sizeof(void *), GFP_KERNEL);
	if (!pages)
		return -ENOMEM;

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

		nr = get_user_pages_fast(addr, nr, gup->flags & 1, pages + i);
		if (nr <= 0)
			break;
		i += nr;
	}
	end_time = ktime_get();

	gup->get_delta_usec = ktime_us_delta(end_time, start_time);
	gup->size = addr - gup->addr;

	start_time = ktime_get();
	for (i = 0; i < nr_pages; i++) {
		if (!pages[i])
			break;
		put_page(pages[i]);
	}
	end_time = ktime_get();
	gup->put_delta_usec = ktime_us_delta(end_time, start_time);

	kvfree(pages);
	return 0;
}

static long gup_benchmark_ioctl(struct file *filep, unsigned int cmd,
		unsigned long arg)
{
	struct gup_benchmark gup;
	int ret;

	if (cmd != GUP_FAST_BENCHMARK)
		return -EINVAL;

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
	void *ret;

	ret = debugfs_create_file_unsafe("gup_benchmark", 0600, NULL, NULL,
			&gup_benchmark_fops);
	if (!ret)
		pr_warn("Failed to create gup_benchmark in debugfs");

	return 0;
}

late_initcall(gup_benchmark_init);
