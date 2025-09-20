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
#include <sys/ipc.h>
#include <sys/shm.h>
#include <linux/io_uring.h>
#include <liburing.h>
#include <semaphore.h>

/* allow ublk_dep.h to override ublk_cmd.h */
#include "ublk_dep.h"
#include <linux/ublk_cmd.h>

#include "utils.h"

#define MAX_BACK_FILES   4

/****************** part 1: libublk ********************/

#define CTRL_DEV		"/dev/ublk-control"
#define UBLKC_DEV		"/dev/ublkc"
#define UBLKB_DEV		"/dev/ublkb"
#define UBLK_CTRL_RING_DEPTH            32
#define ERROR_EVTFD_DEVID 	-2

#define UBLK_IO_MAX_BYTES               (1 << 20)
#define UBLK_MAX_QUEUES_SHIFT		5
#define UBLK_MAX_QUEUES                 (1 << UBLK_MAX_QUEUES_SHIFT)
#define UBLK_MAX_THREADS_SHIFT		5
#define UBLK_MAX_THREADS		(1 << UBLK_MAX_THREADS_SHIFT)
#define UBLK_QUEUE_DEPTH                1024

struct ublk_dev;
struct ublk_queue;
struct ublk_thread;

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
	unsigned short nthreads;
	unsigned queue_depth;
	int dev_id;
	int nr_files;
	char *files[MAX_BACK_FILES];
	unsigned int	logging:1;
	unsigned int	all:1;
	unsigned int	fg:1;
	unsigned int	recovery:1;
	unsigned int	auto_zc_fallback:1;
	unsigned int	per_io_tasks:1;
	unsigned int	no_ublk_fixed_fd:1;

	int _evtfd;
	int _shmid;

	/* built from shmem, only for ublk_dump_dev() */
	struct ublk_dev *shadow_dev;

	/* for 'update_size' command */
	unsigned long long size;

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

#define UBLKS_IO_NEED_FETCH_RQ		(1UL << 0)
#define UBLKS_IO_NEED_COMMIT_RQ_COMP	(1UL << 1)
#define UBLKS_IO_FREE			(1UL << 2)
#define UBLKS_IO_NEED_GET_DATA           (1UL << 3)
#define UBLKS_IO_NEED_REG_BUF            (1UL << 4)
	unsigned short flags;
	unsigned short refs;		/* used by target code only */

	int tag;

	int result;

	unsigned short buf_index;
	unsigned short tgt_ios;
	void *private_data;
};

struct ublk_tgt_ops {
	const char *name;
	int (*init_tgt)(const struct dev_ctx *ctx, struct ublk_dev *);
	void (*deinit_tgt)(struct ublk_dev *);

	int (*queue_io)(struct ublk_thread *, struct ublk_queue *, int tag);
	void (*tgt_io_done)(struct ublk_thread *, struct ublk_queue *,
			    const struct io_uring_cqe *);

	/*
	 * Target specific command line handling
	 *
	 * each option requires argument for target command line
	 */
	void (*parse_cmd_line)(struct dev_ctx *ctx, int argc, char *argv[]);
	void (*usage)(const struct ublk_tgt_ops *ops);

	/* return buffer index for UBLK_F_AUTO_BUF_REG */
	unsigned short (*buf_index)(const struct ublk_queue *, int tag);
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
	struct ublk_dev *dev;
	const struct ublk_tgt_ops *tgt_ops;
	struct ublksrv_io_desc *io_cmd_buf;

/* borrow one bit of ublk uapi flags, which may never be used */
#define UBLKS_Q_AUTO_BUF_REG_FALLBACK	(1ULL << 63)
#define UBLKS_Q_NO_UBLK_FIXED_FD	(1ULL << 62)
	__u64 flags;
	int ublk_fd;	/* cached ublk char device fd */
	struct ublk_io ios[UBLK_QUEUE_DEPTH];
};

struct ublk_thread {
	struct ublk_dev *dev;
	struct io_uring ring;
	unsigned int cmd_inflight;
	unsigned int io_inflight;

	pthread_t thread;
	unsigned idx;

#define UBLKS_T_STOPPING	(1U << 0)
#define UBLKS_T_IDLE	(1U << 1)
	unsigned state;
};

struct ublk_dev {
	struct ublk_tgt tgt;
	struct ublksrv_ctrl_dev_info  dev_info;
	struct ublk_queue q[UBLK_MAX_QUEUES];
	struct ublk_thread threads[UBLK_MAX_THREADS];
	unsigned nthreads;
	unsigned per_io_tasks;

	int fds[MAX_BACK_FILES + 1];	/* fds[0] points to /dev/ublkcN */
	int nr_fds;
	int ctrl_fd;
	struct io_uring ring;

	void *private_data;
};

extern int ublk_queue_io_cmd(struct ublk_thread *t, struct ublk_io *io);


static inline int ublk_io_auto_zc_fallback(const struct ublksrv_io_desc *iod)
{
	return !!(iod->op_flags & UBLK_IO_F_NEED_REG_BUF);
}

static inline int is_target_io(__u64 user_data)
{
	return (user_data & (1ULL << 63)) != 0;
}

static inline __u64 build_user_data(unsigned tag, unsigned op,
		unsigned tgt_data, unsigned q_id, unsigned is_target_io)
{
	/* we only have 7 bits to encode q_id */
	_Static_assert(UBLK_MAX_QUEUES_SHIFT <= 7);
	assert(!(tag >> 16) && !(op >> 8) && !(tgt_data >> 16) && !(q_id >> 7));

	return tag | (op << 16) | (tgt_data << 24) |
		(__u64)q_id << 56 | (__u64)is_target_io << 63;
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

static inline unsigned int user_data_to_q_id(__u64 user_data)
{
	return (user_data >> 56) & 0x7f;
}

static inline unsigned short ublk_cmd_op_nr(unsigned int op)
{
	return _IOC_NR(op);
}

static inline struct ublk_queue *ublk_io_to_queue(const struct ublk_io *io)
{
	return container_of(io, struct ublk_queue, ios[io->tag]);
}

static inline int ublk_io_alloc_sqes(struct ublk_thread *t,
		struct io_uring_sqe *sqes[], int nr_sqes)
{
	struct io_uring *ring = &t->ring;
	unsigned left = io_uring_sq_space_left(ring);
	int i;

	if (left < nr_sqes)
		io_uring_submit(ring);

	for (i = 0; i < nr_sqes; i++) {
		sqes[i] = io_uring_get_sqe(ring);
		if (!sqes[i])
			return i;
	}

	return nr_sqes;
}

static inline int ublk_get_registered_fd(struct ublk_queue *q, int fd_index)
{
	if (q->flags & UBLKS_Q_NO_UBLK_FIXED_FD) {
		if (fd_index == 0)
			/* Return the raw ublk FD for index 0 */
			return q->ublk_fd;
		/* Adjust index for backing files (index 1 becomes 0, etc.) */
		return fd_index - 1;
	}
	return fd_index;
}

static inline void __io_uring_prep_buf_reg_unreg(struct io_uring_sqe *sqe,
		struct ublk_queue *q, int tag, int q_id, __u64 index)
{
	struct ublksrv_io_cmd *cmd = (struct ublksrv_io_cmd *)sqe->cmd;
	int dev_fd = ublk_get_registered_fd(q, 0);

	io_uring_prep_read(sqe, dev_fd, 0, 0, 0);
	sqe->opcode		= IORING_OP_URING_CMD;
	if (q->flags & UBLKS_Q_NO_UBLK_FIXED_FD)
		sqe->flags	&= ~IOSQE_FIXED_FILE;
	else
		sqe->flags	|= IOSQE_FIXED_FILE;

	cmd->tag		= tag;
	cmd->addr		= index;
	cmd->q_id		= q_id;
}

static inline void io_uring_prep_buf_register(struct io_uring_sqe *sqe,
		struct ublk_queue *q, int tag, int q_id, __u64 index)
{
	__io_uring_prep_buf_reg_unreg(sqe, q, tag, q_id, index);
	sqe->cmd_op		= UBLK_U_IO_REGISTER_IO_BUF;
}

static inline void io_uring_prep_buf_unregister(struct io_uring_sqe *sqe,
		struct ublk_queue *q, int tag, int q_id, __u64 index)
{
	__io_uring_prep_buf_reg_unreg(sqe, q, tag, q_id, index);
	sqe->cmd_op		= UBLK_U_IO_UNREGISTER_IO_BUF;
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
	io->flags |= (UBLKS_IO_NEED_COMMIT_RQ_COMP | UBLKS_IO_FREE);
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

static inline int ublk_complete_io(struct ublk_thread *t, struct ublk_queue *q,
				   unsigned tag, int res)
{
	struct ublk_io *io = &q->ios[tag];

	ublk_mark_io_done(io, res);

	return ublk_queue_io_cmd(t, io);
}

static inline void ublk_queued_tgt_io(struct ublk_thread *t, struct ublk_queue *q,
				      unsigned tag, int queued)
{
	if (queued < 0)
		ublk_complete_io(t, q, tag, queued);
	else {
		struct ublk_io *io = ublk_get_io(q, tag);

		t->io_inflight += queued;
		io->tgt_ios = queued;
		io->result = 0;
	}
}

static inline int ublk_completed_tgt_io(struct ublk_thread *t,
					struct ublk_queue *q, unsigned tag)
{
	struct ublk_io *io = ublk_get_io(q, tag);

	t->io_inflight--;

	return --io->tgt_ios == 0;
}

static inline int ublk_queue_use_zc(const struct ublk_queue *q)
{
	return q->flags & UBLK_F_SUPPORT_ZERO_COPY;
}

static inline int ublk_queue_use_auto_zc(const struct ublk_queue *q)
{
	return q->flags & UBLK_F_AUTO_BUF_REG;
}

static inline int ublk_queue_auto_zc_fallback(const struct ublk_queue *q)
{
	return q->flags & UBLKS_Q_AUTO_BUF_REG_FALLBACK;
}

static inline int ublk_queue_no_buf(const struct ublk_queue *q)
{
	return ublk_queue_use_zc(q) || ublk_queue_use_auto_zc(q);
}

extern const struct ublk_tgt_ops null_tgt_ops;
extern const struct ublk_tgt_ops loop_tgt_ops;
extern const struct ublk_tgt_ops stripe_tgt_ops;
extern const struct ublk_tgt_ops fault_inject_tgt_ops;

void backing_file_tgt_deinit(struct ublk_dev *dev);
int backing_file_tgt_init(struct ublk_dev *dev);

#endif
