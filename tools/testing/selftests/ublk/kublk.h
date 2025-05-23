/* SPDX-License-Identifier: GPL-2.0 */
#ifndef KUBLK_INTERNAL_H
#define KUBLK_INTERNAL_H

#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <pthread.h>
#include <getopt.h>
#include <limits.h>
#include <poll.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/inotify.h>
#include <sys/wait.h>
#include <sys/eventfd.h>
#include <sys/uio.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <linux/io_uring.h>
#include <liburing.h>
#include <semaphore.h>

/* allow ublk_dep.h to override ublk_cmd.h */
#include "ublk_dep.h"
#include <linux/ublk_cmd.h>

#define __maybe_unused __attribute__((unused))
#define MAX_BACK_FILES   4
#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

/****************** part 1: libublk ********************/

#define CTRL_DEV		"/dev/ublk-control"
#define UBLKC_DEV		"/dev/ublkc"
#define UBLKB_DEV		"/dev/ublkb"
#define UBLK_CTRL_RING_DEPTH            32
#define ERROR_EVTFD_DEVID 	-2

/* queue idle timeout */
#define UBLKSRV_IO_IDLE_SECS		20

#define UBLK_IO_MAX_BYTES               (1 << 20)
#define UBLK_MAX_QUEUES                 32
#define UBLK_QUEUE_DEPTH                1024

#define UBLK_DBG_DEV            (1U << 0)
#define UBLK_DBG_QUEUE          (1U << 1)
#define UBLK_DBG_IO_CMD         (1U << 2)
#define UBLK_DBG_IO             (1U << 3)
#define UBLK_DBG_CTRL_CMD       (1U << 4)
#define UBLK_LOG                (1U << 5)

struct ublk_dev;
struct ublk_queue;

struct stripe_ctx {
	/* stripe */
	unsigned int    chunk_size;
};

struct fault_inject_ctx {
	/* fault_inject */
	unsigned long   delay_us;
};

struct dev_ctx {
	char tgt_type[16];
	unsigned long flags;
	unsigned nr_hw_queues;
	unsigned queue_depth;
	int dev_id;
	int nr_files;
	char *files[MAX_BACK_FILES];
	unsigned int	logging:1;
	unsigned int	all:1;
	unsigned int	fg:1;
	unsigned int	recovery:1;

	int _evtfd;
	int _shmid;

	/* built from shmem, only for ublk_dump_dev() */
	struct ublk_dev *shadow_dev;

	union {
		struct stripe_ctx 	stripe;
		struct fault_inject_ctx fault_inject;
	};
};

struct ublk_ctrl_cmd_data {
	__u32 cmd_op;
#define CTRL_CMD_HAS_DATA	1
#define CTRL_CMD_HAS_BUF	2
	__u32 flags;

	__u64 data[2];
	__u64 addr;
	__u32 len;
};

struct ublk_io {
	char *buf_addr;

#define UBLKSRV_NEED_FETCH_RQ		(1UL << 0)
#define UBLKSRV_NEED_COMMIT_RQ_COMP	(1UL << 1)
#define UBLKSRV_IO_FREE			(1UL << 2)
#define UBLKSRV_NEED_GET_DATA           (1UL << 3)
	unsigned short flags;
	unsigned short refs;		/* used by target code only */

	int result;

	unsigned short tgt_ios;
	void *private_data;
};

struct ublk_tgt_ops {
	const char *name;
	int (*init_tgt)(const struct dev_ctx *ctx, struct ublk_dev *);
	void (*deinit_tgt)(struct ublk_dev *);

	int (*queue_io)(struct ublk_queue *, int tag);
	void (*tgt_io_done)(struct ublk_queue *,
			int tag, const struct io_uring_cqe *);

	/*
	 * Target specific command line handling
	 *
	 * each option requires argument for target command line
	 */
	void (*parse_cmd_line)(struct dev_ctx *ctx, int argc, char *argv[]);
	void (*usage)(const struct ublk_tgt_ops *ops);
};

struct ublk_tgt {
	unsigned long dev_size;
	unsigned int  sq_depth;
	unsigned int  cq_depth;
	const struct ublk_tgt_ops *ops;
	struct ublk_params params;

	int nr_backing_files;
	unsigned long backing_file_size[MAX_BACK_FILES];
	char backing_file[MAX_BACK_FILES][PATH_MAX];
};

struct ublk_queue {
	int q_id;
	int q_depth;
	unsigned int cmd_inflight;
	unsigned int io_inflight;
	struct ublk_dev *dev;
	const struct ublk_tgt_ops *tgt_ops;
	struct ublksrv_io_desc *io_cmd_buf;
	struct io_uring ring;
	struct ublk_io ios[UBLK_QUEUE_DEPTH];
#define UBLKSRV_QUEUE_STOPPING	(1U << 0)
#define UBLKSRV_QUEUE_IDLE	(1U << 1)
#define UBLKSRV_NO_BUF		(1U << 2)
#define UBLKSRV_ZC		(1U << 3)
	unsigned state;
	pid_t tid;
	pthread_t thread;
};

struct ublk_dev {
	struct ublk_tgt tgt;
	struct ublksrv_ctrl_dev_info  dev_info;
	struct ublk_queue q[UBLK_MAX_QUEUES];

	int fds[MAX_BACK_FILES + 1];	/* fds[0] points to /dev/ublkcN */
	int nr_fds;
	int ctrl_fd;
	struct io_uring ring;

	void *private_data;
};

#ifndef offsetof
#define offsetof(TYPE, MEMBER)  ((size_t)&((TYPE *)0)->MEMBER)
#endif

#ifndef container_of
#define container_of(ptr, type, member) ({                              \
	unsigned long __mptr = (unsigned long)(ptr);                    \
	((type *)(__mptr - offsetof(type, member))); })
#endif

#define round_up(val, rnd) \
	(((val) + ((rnd) - 1)) & ~((rnd) - 1))


extern unsigned int ublk_dbg_mask;
extern int ublk_queue_io_cmd(struct ublk_queue *q, struct ublk_io *io, unsigned tag);

static inline int is_target_io(__u64 user_data)
{
	return (user_data & (1ULL << 63)) != 0;
}

static inline __u64 build_user_data(unsigned tag, unsigned op,
		unsigned tgt_data, unsigned is_target_io)
{
	assert(!(tag >> 16) && !(op >> 8) && !(tgt_data >> 16));

	return tag | (op << 16) | (tgt_data << 24) | (__u64)is_target_io << 63;
}

static inline unsigned int user_data_to_tag(__u64 user_data)
{
	return user_data & 0xffff;
}

static inline unsigned int user_data_to_op(__u64 user_data)
{
	return (user_data >> 16) & 0xff;
}

static inline unsigned int user_data_to_tgt_data(__u64 user_data)
{
	return (user_data >> 24) & 0xffff;
}

static inline unsigned short ublk_cmd_op_nr(unsigned int op)
{
	return _IOC_NR(op);
}

static inline void ublk_err(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
}

static inline void ublk_log(const char *fmt, ...)
{
	if (ublk_dbg_mask & UBLK_LOG) {
		va_list ap;

		va_start(ap, fmt);
		vfprintf(stdout, fmt, ap);
	}
}

static inline void ublk_dbg(int level, const char *fmt, ...)
{
	if (level & ublk_dbg_mask) {
		va_list ap;

		va_start(ap, fmt);
		vfprintf(stdout, fmt, ap);
	}
}

static inline int ublk_queue_alloc_sqes(struct ublk_queue *q,
		struct io_uring_sqe *sqes[], int nr_sqes)
{
	unsigned left = io_uring_sq_space_left(&q->ring);
	int i;

	if (left < nr_sqes)
		io_uring_submit(&q->ring);

	for (i = 0; i < nr_sqes; i++) {
		sqes[i] = io_uring_get_sqe(&q->ring);
		if (!sqes[i])
			return i;
	}

	return nr_sqes;
}

static inline void io_uring_prep_buf_register(struct io_uring_sqe *sqe,
		int dev_fd, int tag, int q_id, __u64 index)
{
	struct ublksrv_io_cmd *cmd = (struct ublksrv_io_cmd *)sqe->cmd;

	io_uring_prep_read(sqe, dev_fd, 0, 0, 0);
	sqe->opcode		= IORING_OP_URING_CMD;
	sqe->flags		|= IOSQE_FIXED_FILE;
	sqe->cmd_op		= UBLK_U_IO_REGISTER_IO_BUF;

	cmd->tag		= tag;
	cmd->addr		= index;
	cmd->q_id		= q_id;
}

static inline void io_uring_prep_buf_unregister(struct io_uring_sqe *sqe,
		int dev_fd, int tag, int q_id, __u64 index)
{
	struct ublksrv_io_cmd *cmd = (struct ublksrv_io_cmd *)sqe->cmd;

	io_uring_prep_read(sqe, dev_fd, 0, 0, 0);
	sqe->opcode		= IORING_OP_URING_CMD;
	sqe->flags		|= IOSQE_FIXED_FILE;
	sqe->cmd_op		= UBLK_U_IO_UNREGISTER_IO_BUF;

	cmd->tag		= tag;
	cmd->addr		= index;
	cmd->q_id		= q_id;
}

static inline void *ublk_get_sqe_cmd(const struct io_uring_sqe *sqe)
{
	return (void *)&sqe->cmd;
}

static inline void ublk_set_io_res(struct ublk_queue *q, int tag, int res)
{
	q->ios[tag].result = res;
}

static inline int ublk_get_io_res(const struct ublk_queue *q, unsigned tag)
{
	return q->ios[tag].result;
}

static inline void ublk_mark_io_done(struct ublk_io *io, int res)
{
	io->flags |= (UBLKSRV_NEED_COMMIT_RQ_COMP | UBLKSRV_IO_FREE);
	io->result = res;
}

static inline const struct ublksrv_io_desc *ublk_get_iod(const struct ublk_queue *q, int tag)
{
	return &q->io_cmd_buf[tag];
}

static inline void ublk_set_sqe_cmd_op(struct io_uring_sqe *sqe, __u32 cmd_op)
{
	__u32 *addr = (__u32 *)&sqe->off;

	addr[0] = cmd_op;
	addr[1] = 0;
}

static inline struct ublk_io *ublk_get_io(struct ublk_queue *q, unsigned tag)
{
	return &q->ios[tag];
}

static inline int ublk_complete_io(struct ublk_queue *q, unsigned tag, int res)
{
	struct ublk_io *io = &q->ios[tag];

	ublk_mark_io_done(io, res);

	return ublk_queue_io_cmd(q, io, tag);
}

static inline void ublk_queued_tgt_io(struct ublk_queue *q, unsigned tag, int queued)
{
	if (queued < 0)
		ublk_complete_io(q, tag, queued);
	else {
		struct ublk_io *io = ublk_get_io(q, tag);

		q->io_inflight += queued;
		io->tgt_ios = queued;
		io->result = 0;
	}
}

static inline int ublk_completed_tgt_io(struct ublk_queue *q, unsigned tag)
{
	struct ublk_io *io = ublk_get_io(q, tag);

	q->io_inflight--;

	return --io->tgt_ios == 0;
}

static inline int ublk_queue_use_zc(const struct ublk_queue *q)
{
	return q->state & UBLKSRV_ZC;
}

extern const struct ublk_tgt_ops null_tgt_ops;
extern const struct ublk_tgt_ops loop_tgt_ops;
extern const struct ublk_tgt_ops stripe_tgt_ops;
extern const struct ublk_tgt_ops fault_inject_tgt_ops;

void backing_file_tgt_deinit(struct ublk_dev *dev);
int backing_file_tgt_init(struct ublk_dev *dev);

static inline unsigned int ilog2(unsigned int x)
{
	if (x == 0)
		return 0;
	return (sizeof(x) * 8 - 1) - __builtin_clz(x);
}
#endif
