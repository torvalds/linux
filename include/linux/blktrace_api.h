#ifndef BLKTRACE_H
#define BLKTRACE_H

#include <linux/blkdev.h>
#include <linux/relay.h>
#include <linux/compat.h>
#include <uapi/linux/blktrace_api.h>
#include <linux/list.h>

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
	struct dentry *dropped_file;
	struct dentry *msg_file;
	struct list_head running_list;
	atomic_t dropped;
};

extern int blk_trace_ioctl(struct block_device *, unsigned, char __user *);
extern void blk_trace_shutdown(struct request_queue *);
extern int do_blk_trace_setup(struct request_queue *q, char *name,
			      dev_t dev, struct block_device *bdev,
			      struct blk_user_trace_setup *buts);
extern __printf(2, 3)
void __trace_note_message(struct blk_trace *, const char *fmt, ...);

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
#define blk_add_trace_msg(q, fmt, ...)					\
	do {								\
		struct blk_trace *bt = (q)->blk_trace;			\
		if (unlikely(bt))					\
			__trace_note_message(bt, fmt, ##__VA_ARGS__);	\
	} while (0)
#define BLK_TN_MAX_MSG		128

static inline bool blk_trace_note_message_enabled(struct request_queue *q)
{
	struct blk_trace *bt = q->blk_trace;
	if (likely(!bt))
		return false;
	return bt->act_mask & BLK_TC_NOTIFY;
}

extern void blk_add_driver_data(struct request_queue *q, struct request *rq,
				void *data, size_t len);
extern int blk_trace_setup(struct request_queue *q, char *name, dev_t dev,
			   struct block_device *bdev,
			   char __user *arg);
extern int blk_trace_startstop(struct request_queue *q, int start);
extern int blk_trace_remove(struct request_queue *q);
extern void blk_trace_remove_sysfs(struct device *dev);
extern int blk_trace_init_sysfs(struct device *dev);

extern struct attribute_group blk_trace_attr_group;

#else /* !CONFIG_BLK_DEV_IO_TRACE */
# define blk_trace_ioctl(bdev, cmd, arg)		(-ENOTTY)
# define blk_trace_shutdown(q)				do { } while (0)
# define do_blk_trace_setup(q, name, dev, bdev, buts)	(-ENOTTY)
# define blk_add_driver_data(q, rq, data, len)		do {} while (0)
# define blk_trace_setup(q, name, dev, bdev, arg)	(-ENOTTY)
# define blk_trace_startstop(q, start)			(-ENOTTY)
# define blk_trace_remove(q)				(-ENOTTY)
# define blk_add_trace_msg(q, fmt, ...)			do { } while (0)
# define blk_trace_remove_sysfs(dev)			do { } while (0)
# define blk_trace_note_message_enabled(q)		(false)
static inline int blk_trace_init_sysfs(struct device *dev)
{
	return 0;
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

#if defined(CONFIG_EVENT_TRACING) && defined(CONFIG_BLOCK)

static inline int blk_cmd_buf_len(struct request *rq)
{
	return (rq->cmd_type == REQ_TYPE_BLOCK_PC) ? rq->cmd_len * 3 : 1;
}

extern void blk_dump_cmd(char *buf, struct request *rq);
extern void blk_fill_rwbs(char *rwbs, u32 rw, int bytes);

#endif /* CONFIG_EVENT_TRACING && CONFIG_BLOCK */

#endif
