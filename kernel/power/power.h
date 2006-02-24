#include <linux/suspend.h>
#include <linux/utsname.h>

struct swsusp_info {
	struct new_utsname	uts;
	u32			version_code;
	unsigned long		num_physpages;
	int			cpus;
	unsigned long		image_pages;
	unsigned long		pages;
} __attribute__((aligned(PAGE_SIZE)));



#ifdef CONFIG_SOFTWARE_SUSPEND
extern int pm_suspend_disk(void);

#else
static inline int pm_suspend_disk(void)
{
	return -EPERM;
}
#endif
extern struct semaphore pm_sem;
#define power_attr(_name) \
static struct subsys_attribute _name##_attr = {	\
	.attr	= {				\
		.name = __stringify(_name),	\
		.mode = 0644,			\
	},					\
	.show	= _name##_show,			\
	.store	= _name##_store,		\
}

extern struct subsystem power_subsys;

/* References to section boundaries */
extern const void __nosave_begin, __nosave_end;

extern unsigned int nr_copy_pages;
extern struct pbe *pagedir_nosave;

/* Preferred image size in bytes (default 500 MB) */
extern unsigned long image_size;

extern asmlinkage int swsusp_arch_suspend(void);
extern asmlinkage int swsusp_arch_resume(void);

extern unsigned int count_data_pages(void);
extern void free_pagedir(struct pbe *pblist);
extern void release_eaten_pages(void);
extern struct pbe *alloc_pagedir(unsigned nr_pages, gfp_t gfp_mask, int safe_needed);
extern void swsusp_free(void);
extern int alloc_data_pages(struct pbe *pblist, gfp_t gfp_mask, int safe_needed);
extern unsigned int snapshot_nr_pages(void);
extern struct pbe *snapshot_pblist(void);
extern void snapshot_pblist_set(struct pbe *pblist);
