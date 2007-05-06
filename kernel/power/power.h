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

extern int pfn_is_nosave(unsigned long);

extern struct mutex pm_mutex;

#define power_attr(_name) \
static struct subsys_attribute _name##_attr = {	\
	.attr	= {				\
		.name = __stringify(_name),	\
		.mode = 0644,			\
	},					\
	.show	= _name##_show,			\
	.store	= _name##_store,		\
}

extern struct kset power_subsys;

/* Preferred image size in bytes (default 500 MB) */
extern unsigned long image_size;
extern int in_suspend;
extern dev_t swsusp_resume_device;
extern sector_t swsusp_resume_block;

extern asmlinkage int swsusp_arch_suspend(void);
extern asmlinkage int swsusp_arch_resume(void);

extern int create_basic_memory_bitmaps(void);
extern void free_basic_memory_bitmaps(void);
extern unsigned int count_data_pages(void);

/**
 *	Auxiliary structure used for reading the snapshot image data and
 *	metadata from and writing them to the list of page backup entries
 *	(PBEs) which is the main data structure of swsusp.
 *
 *	Using struct snapshot_handle we can transfer the image, including its
 *	metadata, as a continuous sequence of bytes with the help of
 *	snapshot_read_next() and snapshot_write_next().
 *
 *	The code that writes the image to a storage or transfers it to
 *	the user land is required to use snapshot_read_next() for this
 *	purpose and it should not make any assumptions regarding the internal
 *	structure of the image.  Similarly, the code that reads the image from
 *	a storage or transfers it from the user land is required to use
 *	snapshot_write_next().
 *
 *	This may allow us to change the internal structure of the image
 *	in the future with considerably less effort.
 */

struct snapshot_handle {
	loff_t		offset;	/* number of the last byte ready for reading
				 * or writing in the sequence
				 */
	unsigned int	cur;	/* number of the block of PAGE_SIZE bytes the
				 * next operation will refer to (ie. current)
				 */
	unsigned int	cur_offset;	/* offset with respect to the current
					 * block (for the next operation)
					 */
	unsigned int	prev;	/* number of the block of PAGE_SIZE bytes that
				 * was the current one previously
				 */
	void		*buffer;	/* address of the block to read from
					 * or write to
					 */
	unsigned int	buf_offset;	/* location to read from or write to,
					 * given as a displacement from 'buffer'
					 */
	int		sync_read;	/* Set to one to notify the caller of
					 * snapshot_write_next() that it may
					 * need to call wait_on_bio_chain()
					 */
};

/* This macro returns the address from/to which the caller of
 * snapshot_read_next()/snapshot_write_next() is allowed to
 * read/write data after the function returns
 */
#define data_of(handle)	((handle).buffer + (handle).buf_offset)

extern unsigned int snapshot_additional_pages(struct zone *zone);
extern int snapshot_read_next(struct snapshot_handle *handle, size_t count);
extern int snapshot_write_next(struct snapshot_handle *handle, size_t count);
extern void snapshot_write_finalize(struct snapshot_handle *handle);
extern int snapshot_image_loaded(struct snapshot_handle *handle);

/*
 * This structure is used to pass the values needed for the identification
 * of the resume swap area from a user space to the kernel via the
 * SNAPSHOT_SET_SWAP_AREA ioctl
 */
struct resume_swap_area {
	loff_t offset;
	u_int32_t dev;
} __attribute__((packed));

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
#define SNAPSHOT_PMOPS			_IOW(SNAPSHOT_IOC_MAGIC, 12, unsigned int)
#define SNAPSHOT_SET_SWAP_AREA		_IOW(SNAPSHOT_IOC_MAGIC, 13, \
							struct resume_swap_area)
#define SNAPSHOT_IOC_MAXNR	13

#define PMOPS_PREPARE	1
#define PMOPS_ENTER	2
#define PMOPS_FINISH	3

/* If unset, the snapshot device cannot be open. */
extern atomic_t snapshot_device_available;

extern sector_t alloc_swapdev_block(int swap);
extern void free_all_swap_pages(int swap);
extern int swsusp_swap_in_use(void);

extern int swsusp_check(void);
extern int swsusp_shrink_memory(void);
extern void swsusp_free(void);
extern int swsusp_suspend(void);
extern int swsusp_resume(void);
extern int swsusp_read(void);
extern int swsusp_write(void);
extern void swsusp_close(void);
extern int suspend_enter(suspend_state_t state);

struct timeval;
extern void swsusp_show_speed(struct timeval *, struct timeval *,
				unsigned int, char *);
