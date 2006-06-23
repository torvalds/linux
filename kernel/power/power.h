#include <linux/suspend.h>
#include <linux/utsname.h>

struct swsusp_info {
	struct new_utsname	uts;
	u32			version_code;
	unsigned long		num_physpages;
	int			cpus;
	unsigned long		image_pages;
	unsigned long		pages;
	unsigned long		size;
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

extern struct pbe *pagedir_nosave;

/* Preferred image size in bytes (default 500 MB) */
extern unsigned long image_size;
extern int in_suspend;
extern dev_t swsusp_resume_device;

extern asmlinkage int swsusp_arch_suspend(void);
extern asmlinkage int swsusp_arch_resume(void);

extern unsigned int count_data_pages(void);

struct snapshot_handle {
	loff_t		offset;
	unsigned int	page;
	unsigned int	page_offset;
	unsigned int	prev;
	struct pbe	*pbe, *last_pbe;
	void		*buffer;
	unsigned int	buf_offset;
};

#define data_of(handle)	((handle).buffer + (handle).buf_offset)

extern int snapshot_read_next(struct snapshot_handle *handle, size_t count);
extern int snapshot_write_next(struct snapshot_handle *handle, size_t count);
int snapshot_image_loaded(struct snapshot_handle *handle);

#define SNAPSHOT_IOC_MAGIC	'3'
#define SNAPSHOT_FREEZE			_IO(SNAPSHOT_IOC_MAGIC, 1)
#define SNAPSHOT_UNFREEZE		_IO(SNAPSHOT_IOC_MAGIC, 2)
#define SNAPSHOT_ATOMIC_SNAPSHOT	_IOW(SNAPSHOT_IOC_MAGIC, 3, void *)
#define SNAPSHOT_ATOMIC_RESTORE		_IO(SNAPSHOT_IOC_MAGIC, 4)
#define SNAPSHOT_FREE			_IO(SNAPSHOT_IOC_MAGIC, 5)
#define SNAPSHOT_SET_IMAGE_SIZE		_IOW(SNAPSHOT_IOC_MAGIC, 6, unsigned long)
#define SNAPSHOT_AVAIL_SWAP		_IOR(SNAPSHOT_IOC_MAGIC, 7, void *)
#define SNAPSHOT_GET_SWAP_PAGE		_IOR(SNAPSHOT_IOC_MAGIC, 8, void *)
#define SNAPSHOT_FREE_SWAP_PAGES	_IO(SNAPSHOT_IOC_MAGIC, 9)
#define SNAPSHOT_SET_SWAP_FILE		_IOW(SNAPSHOT_IOC_MAGIC, 10, unsigned int)
#define SNAPSHOT_S2RAM			_IO(SNAPSHOT_IOC_MAGIC, 11)
#define SNAPSHOT_IOC_MAXNR	11

/**
 *	The bitmap is used for tracing allocated swap pages
 *
 *	The entire bitmap consists of a number of bitmap_page
 *	structures linked with the help of the .next member.
 *	Thus each page can be allocated individually, so we only
 *	need to make 0-order memory allocations to create
 *	the bitmap.
 */

#define BITMAP_PAGE_SIZE	(PAGE_SIZE - sizeof(void *))
#define BITMAP_PAGE_CHUNKS	(BITMAP_PAGE_SIZE / sizeof(long))
#define BITS_PER_CHUNK		(sizeof(long) * 8)
#define BITMAP_PAGE_BITS	(BITMAP_PAGE_CHUNKS * BITS_PER_CHUNK)

struct bitmap_page {
	unsigned long		chunks[BITMAP_PAGE_CHUNKS];
	struct bitmap_page	*next;
};

extern void free_bitmap(struct bitmap_page *bitmap);
extern struct bitmap_page *alloc_bitmap(unsigned int nr_bits);
extern unsigned long alloc_swap_page(int swap, struct bitmap_page *bitmap);
extern void free_all_swap_pages(int swap, struct bitmap_page *bitmap);

extern unsigned int count_special_pages(void);
extern int save_special_mem(void);
extern int restore_special_mem(void);

extern int swsusp_check(void);
extern int swsusp_shrink_memory(void);
extern void swsusp_free(void);
extern int swsusp_suspend(void);
extern int swsusp_resume(void);
extern int swsusp_read(void);
extern int swsusp_write(void);
extern void swsusp_close(void);
extern int suspend_enter(suspend_state_t state);
