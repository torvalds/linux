/******************************************************************************
 * Xen balloon functionality
 */

#define RETRY_UNLIMITED	0

struct balloon_stats {
	/* We aim for 'current allocation' == 'target allocation'. */
	unsigned long current_pages;
	unsigned long target_pages;
	/* Number of pages in high- and low-memory balloons. */
	unsigned long balloon_low;
	unsigned long balloon_high;
	unsigned long schedule_delay;
	unsigned long max_schedule_delay;
	unsigned long retry_count;
	unsigned long max_retry_count;
#ifdef CONFIG_XEN_BALLOON_MEMORY_HOTPLUG
	unsigned long hotplug_pages;
	unsigned long balloon_hotplug;
#endif
};

extern struct balloon_stats balloon_stats;

void balloon_set_new_target(unsigned long target);

int alloc_xenballooned_pages(int nr_pages, struct page **pages,
		bool highmem);
void free_xenballooned_pages(int nr_pages, struct page **pages);

struct sys_device;
#ifdef CONFIG_XEN_SELFBALLOONING
extern int register_xen_selfballooning(struct sys_device *sysdev);
#else
static inline int register_xen_selfballooning(struct sys_device *sysdev)
{
	return -ENOSYS;
}
#endif
