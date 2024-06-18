/* SPDX-License-Identifier: GPL-2.0 */
#ifndef BLKTRACE_H
#define BLKTRACE_H

#include <linux/blk-mq.h>
#include <linux/relay.h>
#include <linux/compat.h>
#include <uapi/linux/blktrace_api.h>
#include <linux/list.h>
#include <linux/blk_types.h>

#if defined(CONFIG_BLK_DEV_IO_TRACE)

#include <linux/sysfs.h>

struct blk_trace {
	int trace_state;
	struct rchan *rchan;
	unsigned long __percpu *sequence;
	unsigned char __percpu *msg_data;
	u16 act_mask;
	u64 start_lba;
	u64 end_lba;
	u32 pid;
	u32 dev;
	struct dentry *dir;
	struct list_head running_list;
	atomic_t dropped;
};

extern int blk_trace_ioctl(struct block_device *, unsigned, char __user *);
extern void blk_trace_shutdown(struct request_queue *);
__printf(3, 4) void __blk_trace_note_message(struct blk_trace *bt,
		struct cgroup_subsys_state *css, const char *fmt, ...);

/**
 * blk_add_trace_msg - Add a (simple) message to the blktrace stream
 * @q:		queue the io is for
 * @fmt:	format to print message in
 * args...	Variable argument list for format
 *
 * Description:
 *     Records a (simple) message onto the blktrace stream.
 *
 *     NOTE: BLK_TN_MAX_MSG characters are output at most.
 *     NOTE: Can not use 'static inline' due to presence of var args...
 *
 **/
#define blk_add_cgroup_trace_msg(q, css, fmt, ...)			\
	do {								\
		struct blk_trace *bt;					\
									\
		rcu_read_lock();					\
		bt = rcu_dereference((q)->blk_trace);			\
		if (unlikely(bt))					\
			__blk_trace_note_message(bt, css, fmt, ##__VA_ARGS__);\
		rcu_read_unlock();					\
	} while (0)
#define blk_add_trace_msg(q, fmt, ...)					\
	blk_add_cgroup_trace_msg(q, NULL, fmt, ##__VA_ARGS__)
#define BLK_TN_MAX_MSG		128

static inline bool blk_trace_note_message_enabled(struct request_queue *q)
{
	struct blk_trace *bt;
	bool ret;

	rcu_read_lock();
	bt = rcu_dereference(q->blk_trace);
	ret = bt && (bt->act_mask & BLK_TC_NOTIFY);
	rcu_read_unlock();
	return ret;
}

extern void blk_add_driver_data(struct request *rq, void *data, size_t len);
extern int blk_trace_setup(struct request_queue *q, char *name, dev_t dev,
			   struct block_device *bdev,
			   char __user *arg);
extern int blk_trace_startstop(struct request_queue *q, int start);
extern int blk_trace_remove(struct request_queue *q);

#else /* !CONFIG_BLK_DEV_IO_TRACE */
# define blk_trace_ioctl(bdev, cmd, arg)		(-ENOTTY)
# define blk_trace_shutdown(q)				do { } while (0)
# define blk_add_driver_data(rq, data, len)		do {} while (0)
# define blk_trace_setup(q, name, dev, bdev, arg)	(-ENOTTY)
# define blk_trace_startstop(q, start)			(-ENOTTY)
# define blk_add_trace_msg(q, fmt, ...)			do { } while (0)
# define blk_add_cgroup_trace_msg(q, cg, fmt, ...)	do { } while (0)
# define blk_trace_note_message_enabled(q)		(false)

static inline int blk_trace_remove(struct request_queue *q)
{
	return -ENOTTY;
}
#endif /* CONFIG_BLK_DEV_IO_TRACE */

#ifdef CONFIG_COMPAT

struct compat_blk_user_trace_setup {
	char name[BLKTRACE_BDEV_SIZE];
	u16 act_mask;
	u32 buf_size;
	u32 buf_nr;
	compat_u64 start_lba;
	compat_u64 end_lba;
	u32 pid;
};
#define BLKTRACESETUP32 _IOWR(0x12, 115, struct compat_blk_user_trace_setup)

#endif

void blk_fill_rwbs(char *rwbs, blk_opf_t opf);

static inline sector_t blk_rq_trace_sector(struct request *rq)
{
	/*
	 * Tracing should ignore starting sector for passthrough requests and
	 * requests where starting sector didn't get set.
	 */
	if (blk_rq_is_passthrough(rq) || blk_rq_pos(rq) == (sector_t)-1)
		return 0;
	return blk_rq_pos(rq);
}

static inline unsigned int blk_rq_trace_nr_sectors(struct request *rq)
{
	return blk_rq_is_passthrough(rq) ? 0 : blk_rq_sectors(rq);
}

#endif
