#ifndef BLK_IOPOLL_H
#define BLK_IOPOLL_H

struct blk_iopoll;
typedef int (blk_iopoll_fn)(struct blk_iopoll *, int);

struct blk_iopoll {
	struct list_head list;
	unsigned long state;
	unsigned long data;
	int weight;
	int max;
	blk_iopoll_fn *poll;
};

enum {
	IOPOLL_F_SCHED		= 0,
	IOPOLL_F_DISABLE	= 1,
};

static inline int blk_iopoll_sched_prep(struct blk_iopoll *iop)
{
	return !test_bit(IOPOLL_F_DISABLE, &iop->state) &&
		!test_and_set_bit(IOPOLL_F_SCHED, &iop->state);
}

static inline int blk_iopoll_disable_pending(struct blk_iopoll *iop)
{
	return test_bit(IOPOLL_F_DISABLE, &iop->state);
}

extern void blk_iopoll_sched(struct blk_iopoll *);
extern void blk_iopoll_init(struct blk_iopoll *, int, blk_iopoll_fn *);
extern void blk_iopoll_complete(struct blk_iopoll *);
extern void __blk_iopoll_complete(struct blk_iopoll *);
extern void blk_iopoll_enable(struct blk_iopoll *);
extern void blk_iopoll_disable(struct blk_iopoll *);

extern int blk_iopoll_enabled;

#endif
