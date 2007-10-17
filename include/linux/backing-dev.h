/*
 * include/linux/backing-dev.h
 *
 * low-level device information and state which is propagated up through
 * to high-level code.
 */

#ifndef _LINUX_BACKING_DEV_H
#define _LINUX_BACKING_DEV_H

#include <linux/percpu_counter.h>
#include <linux/log2.h>
#include <linux/proportions.h>
#include <asm/atomic.h>

struct page;

/*
 * Bits in backing_dev_info.state
 */
enum bdi_state {
	BDI_pdflush,		/* A pdflush thread is working this device */
	BDI_write_congested,	/* The write queue is getting full */
	BDI_read_congested,	/* The read queue is getting full */
	BDI_unused,		/* Available bits start here */
};

typedef int (congested_fn)(void *, int);

enum bdi_stat_item {
	BDI_RECLAIMABLE,
	BDI_WRITEBACK,
	NR_BDI_STAT_ITEMS
};

#define BDI_STAT_BATCH (8*(1+ilog2(nr_cpu_ids)))

struct backing_dev_info {
	unsigned long ra_pages;	/* max readahead in PAGE_CACHE_SIZE units */
	unsigned long state;	/* Always use atomic bitops on this */
	unsigned int capabilities; /* Device capabilities */
	congested_fn *congested_fn; /* Function pointer if device is md/dm */
	void *congested_data;	/* Pointer to aux data for congested func */
	void (*unplug_io_fn)(struct backing_dev_info *, struct page *);
	void *unplug_io_data;

	struct percpu_counter bdi_stat[NR_BDI_STAT_ITEMS];

	struct prop_local_percpu completions;
	int dirty_exceeded;
};

int bdi_init(struct backing_dev_info *bdi);
void bdi_destroy(struct backing_dev_info *bdi);

static inline void __add_bdi_stat(struct backing_dev_info *bdi,
		enum bdi_stat_item item, s64 amount)
{
	__percpu_counter_add(&bdi->bdi_stat[item], amount, BDI_STAT_BATCH);
}

static inline void __inc_bdi_stat(struct backing_dev_info *bdi,
		enum bdi_stat_item item)
{
	__add_bdi_stat(bdi, item, 1);
}

static inline void inc_bdi_stat(struct backing_dev_info *bdi,
		enum bdi_stat_item item)
{
	unsigned long flags;

	local_irq_save(flags);
	__inc_bdi_stat(bdi, item);
	local_irq_restore(flags);
}

static inline void __dec_bdi_stat(struct backing_dev_info *bdi,
		enum bdi_stat_item item)
{
	__add_bdi_stat(bdi, item, -1);
}

static inline void dec_bdi_stat(struct backing_dev_info *bdi,
		enum bdi_stat_item item)
{
	unsigned long flags;

	local_irq_save(flags);
	__dec_bdi_stat(bdi, item);
	local_irq_restore(flags);
}

static inline s64 bdi_stat(struct backing_dev_info *bdi,
		enum bdi_stat_item item)
{
	return percpu_counter_read_positive(&bdi->bdi_stat[item]);
}

static inline s64 __bdi_stat_sum(struct backing_dev_info *bdi,
		enum bdi_stat_item item)
{
	return percpu_counter_sum_positive(&bdi->bdi_stat[item]);
}

static inline s64 bdi_stat_sum(struct backing_dev_info *bdi,
		enum bdi_stat_item item)
{
	s64 sum;
	unsigned long flags;

	local_irq_save(flags);
	sum = __bdi_stat_sum(bdi, item);
	local_irq_restore(flags);

	return sum;
}

/*
 * maximal error of a stat counter.
 */
static inline unsigned long bdi_stat_error(struct backing_dev_info *bdi)
{
#ifdef CONFIG_SMP
	return nr_cpu_ids * BDI_STAT_BATCH;
#else
	return 1;
#endif
}

/*
 * Flags in backing_dev_info::capability
 * - The first two flags control whether dirty pages will contribute to the
 *   VM's accounting and whether writepages() should be called for dirty pages
 *   (something that would not, for example, be appropriate for ramfs)
 * - These flags let !MMU mmap() govern direct device mapping vs immediate
 *   copying more easily for MAP_PRIVATE, especially for ROM filesystems
 */
#define BDI_CAP_NO_ACCT_DIRTY	0x00000001	/* Dirty pages shouldn't contribute to accounting */
#define BDI_CAP_NO_WRITEBACK	0x00000002	/* Don't write pages back */
#define BDI_CAP_MAP_COPY	0x00000004	/* Copy can be mapped (MAP_PRIVATE) */
#define BDI_CAP_MAP_DIRECT	0x00000008	/* Can be mapped directly (MAP_SHARED) */
#define BDI_CAP_READ_MAP	0x00000010	/* Can be mapped for reading */
#define BDI_CAP_WRITE_MAP	0x00000020	/* Can be mapped for writing */
#define BDI_CAP_EXEC_MAP	0x00000040	/* Can be mapped for execution */
#define BDI_CAP_VMFLAGS \
	(BDI_CAP_READ_MAP | BDI_CAP_WRITE_MAP | BDI_CAP_EXEC_MAP)

#if defined(VM_MAYREAD) && \
	(BDI_CAP_READ_MAP != VM_MAYREAD || \
	 BDI_CAP_WRITE_MAP != VM_MAYWRITE || \
	 BDI_CAP_EXEC_MAP != VM_MAYEXEC)
#error please change backing_dev_info::capabilities flags
#endif

extern struct backing_dev_info default_backing_dev_info;
void default_unplug_io_fn(struct backing_dev_info *bdi, struct page *page);

int writeback_acquire(struct backing_dev_info *bdi);
int writeback_in_progress(struct backing_dev_info *bdi);
void writeback_release(struct backing_dev_info *bdi);

static inline int bdi_congested(struct backing_dev_info *bdi, int bdi_bits)
{
	if (bdi->congested_fn)
		return bdi->congested_fn(bdi->congested_data, bdi_bits);
	return (bdi->state & bdi_bits);
}

static inline int bdi_read_congested(struct backing_dev_info *bdi)
{
	return bdi_congested(bdi, 1 << BDI_read_congested);
}

static inline int bdi_write_congested(struct backing_dev_info *bdi)
{
	return bdi_congested(bdi, 1 << BDI_write_congested);
}

static inline int bdi_rw_congested(struct backing_dev_info *bdi)
{
	return bdi_congested(bdi, (1 << BDI_read_congested)|
				  (1 << BDI_write_congested));
}

void clear_bdi_congested(struct backing_dev_info *bdi, int rw);
void set_bdi_congested(struct backing_dev_info *bdi, int rw);
long congestion_wait(int rw, long timeout);

#define bdi_cap_writeback_dirty(bdi) \
	(!((bdi)->capabilities & BDI_CAP_NO_WRITEBACK))

#define bdi_cap_account_dirty(bdi) \
	(!((bdi)->capabilities & BDI_CAP_NO_ACCT_DIRTY))

#define mapping_cap_writeback_dirty(mapping) \
	bdi_cap_writeback_dirty((mapping)->backing_dev_info)

#define mapping_cap_account_dirty(mapping) \
	bdi_cap_account_dirty((mapping)->backing_dev_info)


#endif		/* _LINUX_BACKING_DEV_H */
