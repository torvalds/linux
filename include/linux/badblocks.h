/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_BADBLOCKS_H
#define _LINUX_BADBLOCKS_H

#include <linux/seqlock.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/types.h>

#define BB_LEN_MASK	(0x00000000000001FFULL)
#define BB_OFFSET_MASK	(0x7FFFFFFFFFFFFE00ULL)
#define BB_ACK_MASK	(0x8000000000000000ULL)
#define BB_MAX_LEN	512
#define BB_OFFSET(x)	(((x) & BB_OFFSET_MASK) >> 9)
#define BB_LEN(x)	(((x) & BB_LEN_MASK) + 1)
#define BB_ACK(x)	(!!((x) & BB_ACK_MASK))
#define BB_END(x)	(BB_OFFSET(x) + BB_LEN(x))
#define BB_MAKE(a, l, ack) (((a)<<9) | ((l)-1) | ((u64)(!!(ack)) << 63))

/* Bad block numbers are stored sorted in a single page.
 * 64bits is used for each block or extent.
 * 54 bits are sector number, 9 bits are extent size,
 * 1 bit is an 'acknowledged' flag.
 */
#define MAX_BADBLOCKS	(PAGE_SIZE/8)

struct badblocks {
	struct device *dev;	/* set by devm_init_badblocks */
	int count;		/* count of bad blocks */
	int unacked_exist;	/* there probably are unacknowledged
				 * bad blocks.  This is only cleared
				 * when a read discovers none
				 */
	int shift;		/* shift from sectors to block size
				 * a -ve shift means badblocks are
				 * disabled.*/
	u64 *page;		/* badblock list */
	int changed;
	seqlock_t lock;
	sector_t sector;
	sector_t size;		/* in sectors */
};

struct badblocks_context {
	sector_t	start;
	sector_t	len;
	int		ack;
};

int badblocks_check(struct badblocks *bb, sector_t s, int sectors,
		   sector_t *first_bad, int *bad_sectors);
int badblocks_set(struct badblocks *bb, sector_t s, int sectors,
			int acknowledged);
int badblocks_clear(struct badblocks *bb, sector_t s, int sectors);
void ack_all_badblocks(struct badblocks *bb);
ssize_t badblocks_show(struct badblocks *bb, char *page, int unack);
ssize_t badblocks_store(struct badblocks *bb, const char *page, size_t len,
			int unack);
int badblocks_init(struct badblocks *bb, int enable);
void badblocks_exit(struct badblocks *bb);
struct device;
int devm_init_badblocks(struct device *dev, struct badblocks *bb);
static inline void devm_exit_badblocks(struct device *dev, struct badblocks *bb)
{
	if (bb->dev != dev) {
		dev_WARN_ONCE(dev, 1, "%s: badblocks instance not associated\n",
				__func__);
		return;
	}
	badblocks_exit(bb);
}

static inline int badblocks_full(struct badblocks *bb)
{
	return (bb->count >= MAX_BADBLOCKS);
}

static inline int badblocks_empty(struct badblocks *bb)
{
	return (bb->count == 0);
}

static inline void set_changed(struct badblocks *bb)
{
	if (bb->changed != 1)
		bb->changed = 1;
}

static inline void clear_changed(struct badblocks *bb)
{
	if (bb->changed != 0)
		bb->changed = 0;
}

#endif
