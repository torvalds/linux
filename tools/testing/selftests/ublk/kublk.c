/* SPDX-License-Identifier: MIT */
/*
 * Description: uring_cmd based ublk
 */

#include "kublk.h"

#define MAX_NR_TGT_ARG 	64

unsigned int ublk_dbg_mask = UBLK_LOG;
static const struct ublk_tgt_ops *tgt_ops_list[] = {
	&null_tgt_ops,
	&loop_tgt_ops,
	&stripe_tgt_ops,
	&fault_inject_tgt_ops,
};

static const struct ublk_tgt_ops *ublk_find_tgt(const char *name)
{
	int i;

	if (name == NULL)
		return NULL;

	for (i = 0; i < ARRAY_SIZE(tgt_ops_list); i++)
		if (strcmp(tgt_ops_list[i]->name, name) == 0)
			return tgt_ops_list[i];
	return NULL;
}

static inline int ublk_setup_ring(struct io_uring *r, int depth,
		int cq_depth, unsigned flags)
{
	struct io_uring_params p;

	memset(&p, 0, sizeof(p));
	p.flags = flags | IORING_SETUP_CQSIZE;
	p.cq_entries = cq_depth;

	return io_uring_queue_init_params(depth, r, &p);
}

static void ublk_ctrl_init_cmd(struct ublk_dev *dev,
		struct io_uring_sqe *sqe,
		struct ublk_ctrl_cmd_data *data)
{
	struct ublksrv_ctrl_dev_info *info = &dev->dev_info;
	struct ublksrv_ctrl_cmd *cmd = (struct ublksrv_ctrl_cmd *)ublk_get_sqe_cmd(sqe);

	sqe->fd = dev->ctrl_fd;
	sqe->opcode = IORING_OP_URING_CMD;
	sqe->ioprio = 0;

	if (data->flags & CTRL_CMD_HAS_BUF) {
		cmd->addr = data->addr;
		cmd->len = data->len;
	}

	if (data->flags & CTRL_CMD_HAS_DATA)
		cmd->data[0] = data->data[0];

	cmd->dev_id = info->dev_id;
	cmd->queue_id = -1;

	ublk_set_sqe_cmd_op(sqe, data->cmd_op);

	io_uring_sqe_set_data(sqe, cmd);
}

static int __ublk_ctrl_cmd(struct ublk_dev *dev,
		struct ublk_ctrl_cmd_data *data)
{
	struct io_uring_sqe *sqe;
	struct io_uring_cqe *cqe;
	int ret = -EINVAL;

	sqe = io_uring_get_sqe(&dev->ring);
	if (!sqe) {
		ublk_err("%s: can't get sqe ret %d\n", __func__, ret);
		return ret;
	}

	ublk_ctrl_init_cmd(dev, sqe, data);

	ret = io_uring_submit(&dev->ring);
	if (ret < 0) {
		ublk_err("uring submit ret %d\n", ret);
		return ret;
	}

	ret = io_uring_wait_cqe(&dev->ring, &cqe);
	if (ret < 0) {
		ublk_err("wait cqe: %s\n", strerror(-ret));
		return ret;
	}
	io_uring_cqe_seen(&dev->ring, cqe);

	return cqe->res;
}

static int ublk_ctrl_stop_dev(struct ublk_dev *dev)
{
	struct ublk_ctrl_cmd_data data = {
		.cmd_op	= UBLK_U_CMD_STOP_DEV,
	};

	return __ublk_ctrl_cmd(dev, &data);
}

static int ublk_ctrl_start_dev(struct ublk_dev *dev,
		int daemon_pid)
{
	struct ublk_ctrl_cmd_data data = {
		.cmd_op	= UBLK_U_CMD_START_DEV,
		.flags	= CTRL_CMD_HAS_DATA,
	};

	dev->dev_info.ublksrv_pid = data.data[0] = daemon_pid;

	return __ublk_ctrl_cmd(dev, &data);
}

static int ublk_ctrl_start_user_recovery(struct ublk_dev *dev)
{
	struct ublk_ctrl_cmd_data data = {
		.cmd_op	= UBLK_U_CMD_START_USER_RECOVERY,
	};

	return __ublk_ctrl_cmd(dev, &data);
}

static int ublk_ctrl_end_user_recovery(struct ublk_dev *dev, int daemon_pid)
{
	struct ublk_ctrl_cmd_data data = {
		.cmd_op	= UBLK_U_CMD_END_USER_RECOVERY,
		.flags	= CTRL_CMD_HAS_DATA,
	};

	dev->dev_info.ublksrv_pid = data.data[0] = daemon_pid;

	return __ublk_ctrl_cmd(dev, &data);
}

static int ublk_ctrl_add_dev(struct ublk_dev *dev)
{
	struct ublk_ctrl_cmd_data data = {
		.cmd_op	= UBLK_U_CMD_ADD_DEV,
		.flags	= CTRL_CMD_HAS_BUF,
		.addr = (__u64) (uintptr_t) &dev->dev_info,
		.len = sizeof(struct ublksrv_ctrl_dev_info),
	};

	return __ublk_ctrl_cmd(dev, &data);
}

static int ublk_ctrl_del_dev(struct ublk_dev *dev)
{
	struct ublk_ctrl_cmd_data data = {
		.cmd_op = UBLK_U_CMD_DEL_DEV,
		.flags = 0,
	};

	return __ublk_ctrl_cmd(dev, &data);
}

static int ublk_ctrl_get_info(struct ublk_dev *dev)
{
	struct ublk_ctrl_cmd_data data = {
		.cmd_op	= UBLK_U_CMD_GET_DEV_INFO,
		.flags	= CTRL_CMD_HAS_BUF,
		.addr = (__u64) (uintptr_t) &dev->dev_info,
		.len = sizeof(struct ublksrv_ctrl_dev_info),
	};

	return __ublk_ctrl_cmd(dev, &data);
}

static int ublk_ctrl_set_params(struct ublk_dev *dev,
		struct ublk_params *params)
{
	struct ublk_ctrl_cmd_data data = {
		.cmd_op	= UBLK_U_CMD_SET_PARAMS,
		.flags	= CTRL_CMD_HAS_BUF,
		.addr = (__u64) (uintptr_t) params,
		.len = sizeof(*params),
	};
	params->len = sizeof(*params);
	return __ublk_ctrl_cmd(dev, &data);
}

static int ublk_ctrl_get_params(struct ublk_dev *dev,
		struct ublk_params *params)
{
	struct ublk_ctrl_cmd_data data = {
		.cmd_op	= UBLK_U_CMD_GET_PARAMS,
		.flags	= CTRL_CMD_HAS_BUF,
		.addr = (__u64)params,
		.len = sizeof(*params),
	};

	params->len = sizeof(*params);

	return __ublk_ctrl_cmd(dev, &data);
}

static int ublk_ctrl_get_features(struct ublk_dev *dev,
		__u64 *features)
{
	struct ublk_ctrl_cmd_data data = {
		.cmd_op	= UBLK_U_CMD_GET_FEATURES,
		.flags	= CTRL_CMD_HAS_BUF,
		.addr = (__u64) (uintptr_t) features,
		.len = sizeof(*features),
	};

	return __ublk_ctrl_cmd(dev, &data);
}

static int ublk_ctrl_update_size(struct ublk_dev *dev,
		__u64 nr_sects)
{
	struct ublk_ctrl_cmd_data data = {
		.cmd_op	= UBLK_U_CMD_UPDATE_SIZE,
		.flags	= CTRL_CMD_HAS_DATA,
	};

	data.data[0] = nr_sects;
	return __ublk_ctrl_cmd(dev, &data);
}

static int ublk_ctrl_quiesce_dev(struct ublk_dev *dev,
				 unsigned int timeout_ms)
{
	struct ublk_ctrl_cmd_data data = {
		.cmd_op	= UBLK_U_CMD_QUIESCE_DEV,
		.flags	= CTRL_CMD_HAS_DATA,
	};

	data.data[0] = timeout_ms;
	return __ublk_ctrl_cmd(dev, &data);
}

static const char *ublk_dev_state_desc(struct ublk_dev *dev)
{
	switch (dev->dev_info.state) {
	case UBLK_S_DEV_DEAD:
		return "DEAD";
	case UBLK_S_DEV_LIVE:
		return "LIVE";
	case UBLK_S_DEV_QUIESCED:
		return "QUIESCED";
	default:
		return "UNKNOWN";
	};
}

static void ublk_print_cpu_set(const cpu_set_t *set, char *buf, unsigned len)
{
	unsigned done = 0;
	int i;

	for (i = 0; i < CPU_SETSIZE; i++) {
		if (CPU_ISSET(i, set))
			done += snprintf(&buf[done], len - done, "%d ", i);
	}
}

static void ublk_adjust_affinity(cpu_set_t *set)
{
	int j, updated = 0;

	/*
	 * Just keep the 1st CPU now.
	 *
	 * In future, auto affinity selection can be tried.
	 */
	for (j = 0; j < CPU_SETSIZE; j++) {
		if (CPU_ISSET(j, set)) {
			if (!updated) {
				updated = 1;
				continue;
			}
			CPU_CLR(j, set);
		}
	}
}

/* Caller must free the allocated buffer */
static int ublk_ctrl_get_affinity(struct ublk_dev *ctrl_dev, cpu_set_t **ptr_buf)
{
	struct ublk_ctrl_cmd_data data = {
		.cmd_op	= UBLK_U_CMD_GET_QUEUE_AFFINITY,
		.flags	= CTRL_CMD_HAS_DATA | CTRL_CMD_HAS_BUF,
	};
	cpu_set_t *buf;
	int i, ret;

	buf = malloc(sizeof(cpu_set_t) * ctrl_dev->dev_info.nr_hw_queues);
	if (!buf)
		return -ENOMEM;

	for (i = 0; i < ctrl_dev->dev_info.nr_hw_queues; i++) {
		data.data[0] = i;
		data.len = sizeof(cpu_set_t);
		data.addr = (__u64)&buf[i];

		ret = __ublk_ctrl_cmd(ctrl_dev, &data);
		if (ret < 0) {
			free(buf);
			return ret;
		}
		ublk_adjust_affinity(&buf[i]);
	}

	*ptr_buf = buf;
	return 0;
}

static void ublk_ctrl_dump(struct ublk_dev *dev)
{
	struct ublksrv_ctrl_dev_info *info = &dev->dev_info;
	struct ublk_params p;
	cpu_set_t *affinity;
	int ret;

	ret = ublk_ctrl_get_params(dev, &p);
	if (ret < 0) {
		ublk_err("failed to get params %d %s\n", ret, strerror(-ret));
		return;
	}

	ret = ublk_ctrl_get_affinity(dev, &affinity);
	if (ret < 0) {
		ublk_err("failed to get affinity %m\n");
		return;
	}

	ublk_log("dev id %d: nr_hw_queues %d queue_depth %d block size %d dev_capacity %lld\n",
			info->dev_id, info->nr_hw_queues, info->queue_depth,
			1 << p.basic.logical_bs_shift, p.basic.dev_sectors);
	ublk_log("\tmax rq size %d daemon pid %d flags 0x%llx state %s\n",
			info->max_io_buf_bytes, info->ublksrv_pid, info->flags,
			ublk_dev_state_desc(dev));

	if (affinity) {
		char buf[512];
		int i;

		for (i = 0; i < info->nr_hw_queues; i++) {
			ublk_print_cpu_set(&affinity[i], buf, sizeof(buf));
			printf("\tqueue %u: affinity(%s)\n",
					i, buf);
		}
		free(affinity);
	}

	fflush(stdout);
}

static void ublk_ctrl_deinit(struct ublk_dev *dev)
{
	close(dev->ctrl_fd);
	free(dev);
}

static struct ublk_dev *ublk_ctrl_init(void)
{
	struct ublk_dev *dev = (struct ublk_dev *)calloc(1, sizeof(*dev));
	struct ublksrv_ctrl_dev_info *info = &dev->dev_info;
	int ret;

	dev->ctrl_fd = open(CTRL_DEV, O_RDWR);
	if (dev->ctrl_fd < 0) {
		free(dev);
		return NULL;
	}

	info->max_io_buf_bytes = UBLK_IO_MAX_BYTES;

	ret = ublk_setup_ring(&dev->ring, UBLK_CTRL_RING_DEPTH,
			UBLK_CTRL_RING_DEPTH, IORING_SETUP_SQE128);
	if (ret < 0) {
		ublk_err("queue_init: %s\n", strerror(-ret));
		free(dev);
		return NULL;
	}
	dev->nr_fds = 1;

	return dev;
}

static int __ublk_queue_cmd_buf_sz(unsigned depth)
{
	int size =  depth * sizeof(struct ublksrv_io_desc);
	unsigned int page_sz = getpagesize();

	return round_up(size, page_sz);
}

static int ublk_queue_max_cmd_buf_sz(void)
{
	return __ublk_queue_cmd_buf_sz(UBLK_MAX_QUEUE_DEPTH);
}

static int ublk_queue_cmd_buf_sz(struct ublk_queue *q)
{
	return __ublk_queue_cmd_buf_sz(q->q_depth);
}

static void ublk_queue_deinit(struct ublk_queue *q)
{
	int i;
	int nr_ios = q->q_depth;

	if (q->io_cmd_buf)
		munmap(q->io_cmd_buf, ublk_queue_cmd_buf_sz(q));

	for (i = 0; i < nr_ios; i++)
		free(q->ios[i].buf_addr);
}

static void ublk_thread_deinit(struct ublk_thread *t)
{
	io_uring_unregister_buffers(&t->ring);

	io_uring_unregister_ring_fd(&t->ring);

	if (t->ring.ring_fd > 0) {
		io_uring_unregister_files(&t->ring);
		close(t->ring.ring_fd);
		t->ring.ring_fd = -1;
	}
}

static int ublk_queue_init(struct ublk_queue *q, unsigned long long extra_flags)
{
	struct ublk_dev *dev = q->dev;
	int depth = dev->dev_info.queue_depth;
	int i;
	int cmd_buf_size, io_buf_size;
	unsigned long off;

	q->tgt_ops = dev->tgt.ops;
	q->flags = 0;
	q->q_depth = depth;
	q->flags = dev->dev_info.flags;
	q->flags |= extra_flags;

	/* Cache fd in queue for fast path access */
	q->ublk_fd = dev->fds[0];

	cmd_buf_size = ublk_queue_cmd_buf_sz(q);
	off = UBLKSRV_CMD_BUF_OFFSET + q->q_id * ublk_queue_max_cmd_buf_sz();
	q->io_cmd_buf = mmap(0, cmd_buf_size, PROT_READ,
			MAP_SHARED | MAP_POPULATE, dev->fds[0], off);
	if (q->io_cmd_buf == MAP_FAILED) {
		ublk_err("ublk dev %d queue %d map io_cmd_buf failed %m\n",
				q->dev->dev_info.dev_id, q->q_id);
		goto fail;
	}

	io_buf_size = dev->dev_info.max_io_buf_bytes;
	for (i = 0; i < q->q_depth; i++) {
		q->ios[i].buf_addr = NULL;
		q->ios[i].flags = UBLKS_IO_NEED_FETCH_RQ | UBLKS_IO_FREE;
		q->ios[i].tag = i;

		if (ublk_queue_no_buf(q))
			continue;

		if (posix_memalign((void **)&q->ios[i].buf_addr,
					getpagesize(), io_buf_size)) {
			ublk_err("ublk dev %d queue %d io %d posix_memalign failed %m\n",
					dev->dev_info.dev_id, q->q_id, i);
			goto fail;
		}
	}

	return 0;
 fail:
	ublk_queue_deinit(q);
	ublk_err("ublk dev %d queue %d failed\n",
			dev->dev_info.dev_id, q->q_id);
	return -ENOMEM;
}

static int ublk_thread_init(struct ublk_thread *t, unsigned long long extra_flags)
{
	struct ublk_dev *dev = t->dev;
	unsigned long long flags = dev->dev_info.flags | extra_flags;
	int ring_depth = dev->tgt.sq_depth, cq_depth = dev->tgt.cq_depth;
	int ret;

	ret = ublk_setup_ring(&t->ring, ring_depth, cq_depth,
			IORING_SETUP_COOP_TASKRUN |
			IORING_SETUP_SINGLE_ISSUER |
			IORING_SETUP_DEFER_TASKRUN);
	if (ret < 0) {
		ublk_err("ublk dev %d thread %d setup io_uring failed %d\n",
				dev->dev_info.dev_id, t->idx, ret);
		goto fail;
	}

	if (dev->dev_info.flags & (UBLK_F_SUPPORT_ZERO_COPY | UBLK_F_AUTO_BUF_REG)) {
		unsigned nr_ios = dev->dev_info.queue_depth * dev->dev_info.nr_hw_queues;
		unsigned max_nr_ios_per_thread = nr_ios / dev->nthreads;
		max_nr_ios_per_thread += !!(nr_ios % dev->nthreads);
		ret = io_uring_register_buffers_sparse(
			&t->ring, max_nr_ios_per_thread);
		if (ret) {
			ublk_err("ublk dev %d thread %d register spare buffers failed %d",
					dev->dev_info.dev_id, t->idx, ret);
			goto fail;
		}
	}

	io_uring_register_ring_fd(&t->ring);

	if (flags & UBLKS_Q_NO_UBLK_FIXED_FD) {
		/* Register only backing files starting from index 1, exclude ublk control device */
		if (dev->nr_fds > 1) {
			ret = io_uring_register_files(&t->ring, &dev->fds[1], dev->nr_fds - 1);
		} else {
			/* No backing files to register, skip file registration */
			ret = 0;
		}
	} else {
		ret = io_uring_register_files(&t->ring, dev->fds, dev->nr_fds);
	}
	if (ret) {
		ublk_err("ublk dev %d thread %d register files failed %d\n",
				t->dev->dev_info.dev_id, t->idx, ret);
		goto fail;
	}

	return 0;
fail:
	ublk_thread_deinit(t);
	ublk_err("ublk dev %d thread %d init failed\n",
			dev->dev_info.dev_id, t->idx);
	return -ENOMEM;
}

#define WAIT_USEC 	100000
#define MAX_WAIT_USEC 	(3 * 1000000)
static int ublk_dev_prep(const struct dev_ctx *ctx, struct ublk_dev *dev)
{
	int dev_id = dev->dev_info.dev_id;
	unsigned int wait_usec = 0;
	int ret = 0, fd = -1;
	char buf[64];

	snprintf(buf, 64, "%s%d", UBLKC_DEV, dev_id);

	while (wait_usec < MAX_WAIT_USEC) {
		fd = open(buf, O_RDWR);
		if (fd >= 0)
			break;
		usleep(WAIT_USEC);
		wait_usec += WAIT_USEC;
	}
	if (fd < 0) {
		ublk_err("can't open %s %s\n", buf, strerror(errno));
		return -1;
	}

	dev->fds[0] = fd;
	if (dev->tgt.ops->init_tgt)
		ret = dev->tgt.ops->init_tgt(ctx, dev);
	if (ret)
		close(dev->fds[0]);
	return ret;
}

static void ublk_dev_unprep(struct ublk_dev *dev)
{
	if (dev->tgt.ops->deinit_tgt)
		dev->tgt.ops->deinit_tgt(dev);
	close(dev->fds[0]);
}

static void ublk_set_auto_buf_reg(const struct ublk_queue *q,
				  struct io_uring_sqe *sqe,
				  unsigned short tag)
{
	struct ublk_auto_buf_reg buf = {};

	if (q->tgt_ops->buf_index)
		buf.index = q->tgt_ops->buf_index(q, tag);
	else
		buf.index = q->ios[tag].buf_index;

	if (ublk_queue_auto_zc_fallback(q))
		buf.flags = UBLK_AUTO_BUF_REG_FALLBACK;

	sqe->addr = ublk_auto_buf_reg_to_sqe_addr(&buf);
}

int ublk_queue_io_cmd(struct ublk_thread *t, struct ublk_io *io)
{
	struct ublk_queue *q = ublk_io_to_queue(io);
	struct ublksrv_io_cmd *cmd;
	struct io_uring_sqe *sqe[1];
	unsigned int cmd_op = 0;
	__u64 user_data;

	/* only freed io can be issued */
	if (!(io->flags & UBLKS_IO_FREE))
		return 0;

	/*
	 * we issue because we need either fetching or committing or
	 * getting data
	 */
	if (!(io->flags &
		(UBLKS_IO_NEED_FETCH_RQ | UBLKS_IO_NEED_COMMIT_RQ_COMP | UBLKS_IO_NEED_GET_DATA)))
		return 0;

	if (io->flags & UBLKS_IO_NEED_GET_DATA)
		cmd_op = UBLK_U_IO_NEED_GET_DATA;
	else if (io->flags & UBLKS_IO_NEED_COMMIT_RQ_COMP)
		cmd_op = UBLK_U_IO_COMMIT_AND_FETCH_REQ;
	else if (io->flags & UBLKS_IO_NEED_FETCH_RQ)
		cmd_op = UBLK_U_IO_FETCH_REQ;

	if (io_uring_sq_space_left(&t->ring) < 1)
		io_uring_submit(&t->ring);

	ublk_io_alloc_sqes(t, sqe, 1);
	if (!sqe[0]) {
		ublk_err("%s: run out of sqe. thread %u, tag %d\n",
				__func__, t->idx, io->tag);
		return -1;
	}

	cmd = (struct ublksrv_io_cmd *)ublk_get_sqe_cmd(sqe[0]);

	if (cmd_op == UBLK_U_IO_COMMIT_AND_FETCH_REQ)
		cmd->result = io->result;

	/* These fields should be written once, never change */
	ublk_set_sqe_cmd_op(sqe[0], cmd_op);
	sqe[0]->fd	= ublk_get_registered_fd(q, 0);	/* dev->fds[0] */
	sqe[0]->opcode	= IORING_OP_URING_CMD;
	if (q->flags & UBLKS_Q_NO_UBLK_FIXED_FD)
		sqe[0]->flags	= 0;  /* Use raw FD, not fixed file */
	else
		sqe[0]->flags	= IOSQE_FIXED_FILE;
	sqe[0]->rw_flags	= 0;
	cmd->tag	= io->tag;
	cmd->q_id	= q->q_id;
	if (!ublk_queue_no_buf(q))
		cmd->addr	= (__u64) (uintptr_t) io->buf_addr;
	else
		cmd->addr	= 0;

	if (ublk_queue_use_auto_zc(q))
		ublk_set_auto_buf_reg(q, sqe[0], io->tag);

	user_data = build_user_data(io->tag, _IOC_NR(cmd_op), 0, q->q_id, 0);
	io_uring_sqe_set_data64(sqe[0], user_data);

	io->flags = 0;

	t->cmd_inflight += 1;

	ublk_dbg(UBLK_DBG_IO_CMD, "%s: (thread %u qid %d tag %u cmd_op %u) iof %x stopping %d\n",
			__func__, t->idx, q->q_id, io->tag, cmd_op,
			io->flags, !!(t->state & UBLKS_T_STOPPING));
	return 1;
}

static void ublk_submit_fetch_commands(struct ublk_thread *t)
{
	struct ublk_queue *q;
	struct ublk_io *io;
	int i = 0, j = 0;

	if (t->dev->per_io_tasks) {
		/*
		 * Lexicographically order all the (qid,tag) pairs, with
		 * qid taking priority (so (1,0) > (0,1)). Then make
		 * this thread the daemon for every Nth entry in this
		 * list (N is the number of threads), starting at this
		 * thread's index. This ensures that each queue is
		 * handled by as many ublk server threads as possible,
		 * so that load that is concentrated on one or a few
		 * queues can make use of all ublk server threads.
		 */
		const struct ublksrv_ctrl_dev_info *dinfo = &t->dev->dev_info;
		int nr_ios = dinfo->nr_hw_queues * dinfo->queue_depth;
		for (i = t->idx; i < nr_ios; i += t->dev->nthreads) {
			int q_id = i / dinfo->queue_depth;
			int tag = i % dinfo->queue_depth;
			q = &t->dev->q[q_id];
			io = &q->ios[tag];
			io->buf_index = j++;
			ublk_queue_io_cmd(t, io);
		}
	} else {
		/*
		 * Service exclusively the queue whose q_id matches our
		 * thread index.
		 */
		struct ublk_queue *q = &t->dev->q[t->idx];
		for (i = 0; i < q->q_depth; i++) {
			io = &q->ios[i];
			io->buf_index = i;
			ublk_queue_io_cmd(t, io);
		}
	}
}

static int ublk_thread_is_idle(struct ublk_thread *t)
{
	return !io_uring_sq_ready(&t->ring) && !t->io_inflight;
}

static int ublk_thread_is_done(struct ublk_thread *t)
{
	return (t->state & UBLKS_T_STOPPING) && ublk_thread_is_idle(t);
}

static inline void ublksrv_handle_tgt_cqe(struct ublk_thread *t,
					  struct ublk_queue *q,
					  struct io_uring_cqe *cqe)
{
	if (cqe->res < 0 && cqe->res != -EAGAIN)
		ublk_err("%s: failed tgt io: res %d qid %u tag %u, cmd_op %u\n",
			__func__, cqe->res, q->q_id,
			user_data_to_tag(cqe->user_data),
			user_data_to_op(cqe->user_data));

	if (q->tgt_ops->tgt_io_done)
		q->tgt_ops->tgt_io_done(t, q, cqe);
}

static void ublk_handle_uring_cmd(struct ublk_thread *t,
				  struct ublk_queue *q,
				  const struct io_uring_cqe *cqe)
{
	int fetch = (cqe->res != UBLK_IO_RES_ABORT) &&
		!(t->state & UBLKS_T_STOPPING);
	unsigned tag = user_data_to_tag(cqe->user_data);
	struct ublk_io *io = &q->ios[tag];

	if (!fetch) {
		t->state |= UBLKS_T_STOPPING;
		io->flags &= ~UBLKS_IO_NEED_FETCH_RQ;
	}

	if (cqe->res == UBLK_IO_RES_OK) {
		assert(tag < q->q_depth);
		if (q->tgt_ops->queue_io)
			q->tgt_ops->queue_io(t, q, tag);
	} else if (cqe->res == UBLK_IO_RES_NEED_GET_DATA) {
		io->flags |= UBLKS_IO_NEED_GET_DATA | UBLKS_IO_FREE;
		ublk_queue_io_cmd(t, io);
	} else {
		/*
		 * COMMIT_REQ will be completed immediately since no fetching
		 * piggyback is required.
		 *
		 * Marking IO_FREE only, then this io won't be issued since
		 * we only issue io with (UBLKS_IO_FREE | UBLKSRV_NEED_*)
		 *
		 * */
		io->flags = UBLKS_IO_FREE;
	}
}

static void ublk_handle_cqe(struct ublk_thread *t,
		struct io_uring_cqe *cqe, void *data)
{
	struct ublk_dev *dev = t->dev;
	unsigned q_id = user_data_to_q_id(cqe->user_data);
	struct ublk_queue *q = &dev->q[q_id];
	unsigned cmd_op = user_data_to_op(cqe->user_data);

	if (cqe->res < 0 && cqe->res != -ENODEV)
		ublk_err("%s: res %d userdata %llx queue state %x\n", __func__,
				cqe->res, cqe->user_data, q->flags);

	ublk_dbg(UBLK_DBG_IO_CMD, "%s: res %d (qid %d tag %u cmd_op %u target %d/%d) stopping %d\n",
			__func__, cqe->res, q->q_id, user_data_to_tag(cqe->user_data),
			cmd_op, is_target_io(cqe->user_data),
			user_data_to_tgt_data(cqe->user_data),
			(t->state & UBLKS_T_STOPPING));

	/* Don't retrieve io in case of target io */
	if (is_target_io(cqe->user_data)) {
		ublksrv_handle_tgt_cqe(t, q, cqe);
		return;
	}

	t->cmd_inflight--;

	ublk_handle_uring_cmd(t, q, cqe);
}

static int ublk_reap_events_uring(struct ublk_thread *t)
{
	struct io_uring_cqe *cqe;
	unsigned head;
	int count = 0;

	io_uring_for_each_cqe(&t->ring, head, cqe) {
		ublk_handle_cqe(t, cqe, NULL);
		count += 1;
	}
	io_uring_cq_advance(&t->ring, count);

	return count;
}

static int ublk_process_io(struct ublk_thread *t)
{
	int ret, reapped;

	ublk_dbg(UBLK_DBG_THREAD, "dev%d-t%u: to_submit %d inflight cmd %u stopping %d\n",
				t->dev->dev_info.dev_id,
				t->idx, io_uring_sq_ready(&t->ring),
				t->cmd_inflight,
				(t->state & UBLKS_T_STOPPING));

	if (ublk_thread_is_done(t))
		return -ENODEV;

	ret = io_uring_submit_and_wait(&t->ring, 1);
	reapped = ublk_reap_events_uring(t);

	ublk_dbg(UBLK_DBG_THREAD, "submit result %d, reapped %d stop %d idle %d\n",
			ret, reapped, (t->state & UBLKS_T_STOPPING),
			(t->state & UBLKS_T_IDLE));

	return reapped;
}

static void ublk_thread_set_sched_affinity(const struct ublk_thread *t,
		cpu_set_t *cpuset)
{
        if (sched_setaffinity(0, sizeof(*cpuset), cpuset) < 0)
		ublk_err("ublk dev %u thread %u set affinity failed",
				t->dev->dev_info.dev_id, t->idx);
}

struct ublk_thread_info {
	struct ublk_dev 	*dev;
	unsigned		idx;
	sem_t 			*ready;
	cpu_set_t 		*affinity;
	unsigned long long	extra_flags;
};

static void *ublk_io_handler_fn(void *data)
{
	struct ublk_thread_info *info = data;
	struct ublk_thread *t = &info->dev->threads[info->idx];
	int dev_id = info->dev->dev_info.dev_id;
	int ret;

	t->dev = info->dev;
	t->idx = info->idx;

	ret = ublk_thread_init(t, info->extra_flags);
	if (ret) {
		ublk_err("ublk dev %d thread %u init failed\n",
				dev_id, t->idx);
		return NULL;
	}
	/* IO perf is sensitive with queue pthread affinity on NUMA machine*/
	if (info->affinity)
		ublk_thread_set_sched_affinity(t, info->affinity);
	sem_post(info->ready);

	ublk_dbg(UBLK_DBG_THREAD, "tid %d: ublk dev %d thread %u started\n",
			gettid(), dev_id, t->idx);

	/* submit all io commands to ublk driver */
	ublk_submit_fetch_commands(t);
	do {
		if (ublk_process_io(t) < 0)
			break;
	} while (1);

	ublk_dbg(UBLK_DBG_THREAD, "tid %d: ublk dev %d thread %d exiting\n",
		 gettid(), dev_id, t->idx);
	ublk_thread_deinit(t);
	return NULL;
}

static void ublk_set_parameters(struct ublk_dev *dev)
{
	int ret;

	ret = ublk_ctrl_set_params(dev, &dev->tgt.params);
	if (ret)
		ublk_err("dev %d set basic parameter failed %d\n",
				dev->dev_info.dev_id, ret);
}

static int ublk_send_dev_event(const struct dev_ctx *ctx, struct ublk_dev *dev, int dev_id)
{
	uint64_t id;
	int evtfd = ctx->_evtfd;

	if (evtfd < 0)
		return -EBADF;

	if (dev_id >= 0)
		id = dev_id + 1;
	else
		id = ERROR_EVTFD_DEVID;

	if (dev && ctx->shadow_dev)
		memcpy(&ctx->shadow_dev->q, &dev->q, sizeof(dev->q));

	if (write(evtfd, &id, sizeof(id)) != sizeof(id))
		return -EINVAL;

	close(evtfd);
	shmdt(ctx->shadow_dev);

	return 0;
}


static int ublk_start_daemon(const struct dev_ctx *ctx, struct ublk_dev *dev)
{
	const struct ublksrv_ctrl_dev_info *dinfo = &dev->dev_info;
	struct ublk_thread_info *tinfo;
	unsigned long long extra_flags = 0;
	cpu_set_t *affinity_buf;
	void *thread_ret;
	sem_t ready;
	int ret, i;

	ublk_dbg(UBLK_DBG_DEV, "%s enter\n", __func__);

	tinfo = calloc(sizeof(struct ublk_thread_info), dev->nthreads);
	if (!tinfo)
		return -ENOMEM;

	sem_init(&ready, 0, 0);
	ret = ublk_dev_prep(ctx, dev);
	if (ret)
		return ret;

	ret = ublk_ctrl_get_affinity(dev, &affinity_buf);
	if (ret)
		return ret;

	if (ctx->auto_zc_fallback)
		extra_flags = UBLKS_Q_AUTO_BUF_REG_FALLBACK;
	if (ctx->no_ublk_fixed_fd)
		extra_flags |= UBLKS_Q_NO_UBLK_FIXED_FD;

	for (i = 0; i < dinfo->nr_hw_queues; i++) {
		dev->q[i].dev = dev;
		dev->q[i].q_id = i;

		ret = ublk_queue_init(&dev->q[i], extra_flags);
		if (ret) {
			ublk_err("ublk dev %d queue %d init queue failed\n",
				 dinfo->dev_id, i);
			goto fail;
		}
	}

	for (i = 0; i < dev->nthreads; i++) {
		tinfo[i].dev = dev;
		tinfo[i].idx = i;
		tinfo[i].ready = &ready;
		tinfo[i].extra_flags = extra_flags;

		/*
		 * If threads are not tied 1:1 to queues, setting thread
		 * affinity based on queue affinity makes little sense.
		 * However, thread CPU affinity has significant impact
		 * on performance, so to compare fairly, we'll still set
		 * thread CPU affinity based on queue affinity where
		 * possible.
		 */
		if (dev->nthreads == dinfo->nr_hw_queues)
			tinfo[i].affinity = &affinity_buf[i];
		pthread_create(&dev->threads[i].thread, NULL,
				ublk_io_handler_fn,
				&tinfo[i]);
	}

	for (i = 0; i < dev->nthreads; i++)
		sem_wait(&ready);
	free(tinfo);
	free(affinity_buf);

	/* everything is fine now, start us */
	if (ctx->recovery)
		ret = ublk_ctrl_end_user_recovery(dev, getpid());
	else {
		ublk_set_parameters(dev);
		ret = ublk_ctrl_start_dev(dev, getpid());
	}
	if (ret < 0) {
		ublk_err("%s: ublk_ctrl_start_dev failed: %d\n", __func__, ret);
		goto fail;
	}

	ublk_ctrl_get_info(dev);
	if (ctx->fg)
		ublk_ctrl_dump(dev);
	else
		ublk_send_dev_event(ctx, dev, dev->dev_info.dev_id);

	/* wait until we are terminated */
	for (i = 0; i < dev->nthreads; i++)
		pthread_join(dev->threads[i].thread, &thread_ret);
 fail:
	for (i = 0; i < dinfo->nr_hw_queues; i++)
		ublk_queue_deinit(&dev->q[i]);
	ublk_dev_unprep(dev);
	ublk_dbg(UBLK_DBG_DEV, "%s exit\n", __func__);

	return ret;
}

static int wait_ublk_dev(const char *path, int evt_mask, unsigned timeout)
{
#define EV_SIZE (sizeof(struct inotify_event))
#define EV_BUF_LEN (128 * (EV_SIZE + 16))
	struct pollfd pfd;
	int fd, wd;
	int ret = -EINVAL;
	const char *dev_name = basename(path);

	fd = inotify_init();
	if (fd < 0) {
		ublk_dbg(UBLK_DBG_DEV, "%s: inotify init failed\n", __func__);
		return fd;
	}

	wd = inotify_add_watch(fd, "/dev", evt_mask);
	if (wd == -1) {
		ublk_dbg(UBLK_DBG_DEV, "%s: add watch for /dev failed\n", __func__);
		goto fail;
	}

	pfd.fd = fd;
	pfd.events = POLL_IN;
	while (1) {
		int i = 0;
		char buffer[EV_BUF_LEN];
		ret = poll(&pfd, 1, 1000 * timeout);

		if (ret == -1) {
			ublk_err("%s: poll inotify failed: %d\n", __func__, ret);
			goto rm_watch;
		} else if (ret == 0) {
			ublk_err("%s: poll inotify timeout\n", __func__);
			ret = -ETIMEDOUT;
			goto rm_watch;
		}

		ret = read(fd, buffer, EV_BUF_LEN);
		if (ret < 0) {
			ublk_err("%s: read inotify fd failed\n", __func__);
			goto rm_watch;
		}

		while (i < ret) {
			struct inotify_event *event = (struct inotify_event *)&buffer[i];

			ublk_dbg(UBLK_DBG_DEV, "%s: inotify event %x %s\n",
					__func__, event->mask, event->name);
			if (event->mask & evt_mask) {
				if (!strcmp(event->name, dev_name)) {
					ret = 0;
					goto rm_watch;
				}
			}
			i += EV_SIZE + event->len;
		}
	}
rm_watch:
	inotify_rm_watch(fd, wd);
fail:
	close(fd);
	return ret;
}

static int ublk_stop_io_daemon(const struct ublk_dev *dev)
{
	int daemon_pid = dev->dev_info.ublksrv_pid;
	int dev_id = dev->dev_info.dev_id;
	char ublkc[64];
	int ret = 0;

	if (daemon_pid < 0)
		return 0;

	/* daemon may be dead already */
	if (kill(daemon_pid, 0) < 0)
		goto wait;

	snprintf(ublkc, sizeof(ublkc), "/dev/%s%d", "ublkc", dev_id);

	/* ublk char device may be gone already */
	if (access(ublkc, F_OK) != 0)
		goto wait;

	/* Wait until ublk char device is closed, when the daemon is shutdown */
	ret = wait_ublk_dev(ublkc, IN_CLOSE, 10);
	/* double check and since it may be closed before starting inotify */
	if (ret == -ETIMEDOUT)
		ret = kill(daemon_pid, 0) < 0;
wait:
	waitpid(daemon_pid, NULL, 0);
	ublk_dbg(UBLK_DBG_DEV, "%s: pid %d dev_id %d ret %d\n",
			__func__, daemon_pid, dev_id, ret);

	return ret;
}

static int __cmd_dev_add(const struct dev_ctx *ctx)
{
	unsigned nthreads = ctx->nthreads;
	unsigned nr_queues = ctx->nr_hw_queues;
	const char *tgt_type = ctx->tgt_type;
	unsigned depth = ctx->queue_depth;
	__u64 features;
	const struct ublk_tgt_ops *ops;
	struct ublksrv_ctrl_dev_info *info;
	struct ublk_dev *dev = NULL;
	int dev_id = ctx->dev_id;
	int ret, i;

	ops = ublk_find_tgt(tgt_type);
	if (!ops) {
		ublk_err("%s: no such tgt type, type %s\n",
				__func__, tgt_type);
		ret = -ENODEV;
		goto fail;
	}

	if (nr_queues > UBLK_MAX_QUEUES || depth > UBLK_QUEUE_DEPTH) {
		ublk_err("%s: invalid nr_queues or depth queues %u depth %u\n",
				__func__, nr_queues, depth);
		ret = -EINVAL;
		goto fail;
	}

	/* default to 1:1 threads:queues if nthreads is unspecified */
	if (!nthreads)
		nthreads = nr_queues;

	if (nthreads > UBLK_MAX_THREADS) {
		ublk_err("%s: %u is too many threads (max %u)\n",
				__func__, nthreads, UBLK_MAX_THREADS);
		ret = -EINVAL;
		goto fail;
	}

	if (nthreads != nr_queues && !ctx->per_io_tasks) {
		ublk_err("%s: threads %u must be same as queues %u if "
			"not using per_io_tasks\n",
			__func__, nthreads, nr_queues);
		ret = -EINVAL;
		goto fail;
	}

	dev = ublk_ctrl_init();
	if (!dev) {
		ublk_err("%s: can't alloc dev id %d, type %s\n",
				__func__, dev_id, tgt_type);
		ret = -ENOMEM;
		goto fail;
	}

	/* kernel doesn't support get_features */
	ret = ublk_ctrl_get_features(dev, &features);
	if (ret < 0) {
		ret = -EINVAL;
		goto fail;
	}

	if (!(features & UBLK_F_CMD_IOCTL_ENCODE)) {
		ret = -ENOTSUP;
		goto fail;
	}

	info = &dev->dev_info;
	info->dev_id = ctx->dev_id;
	info->nr_hw_queues = nr_queues;
	info->queue_depth = depth;
	info->flags = ctx->flags;
	if ((features & UBLK_F_QUIESCE) &&
			(info->flags & UBLK_F_USER_RECOVERY))
		info->flags |= UBLK_F_QUIESCE;
	dev->nthreads = nthreads;
	dev->per_io_tasks = ctx->per_io_tasks;
	dev->tgt.ops = ops;
	dev->tgt.sq_depth = depth;
	dev->tgt.cq_depth = depth;

	for (i = 0; i < MAX_BACK_FILES; i++) {
		if (ctx->files[i]) {
			strcpy(dev->tgt.backing_file[i], ctx->files[i]);
			dev->tgt.nr_backing_files++;
		}
	}

	if (ctx->recovery)
		ret = ublk_ctrl_start_user_recovery(dev);
	else
		ret = ublk_ctrl_add_dev(dev);
	if (ret < 0) {
		ublk_err("%s: can't add dev id %d, type %s ret %d\n",
				__func__, dev_id, tgt_type, ret);
		goto fail;
	}

	ret = ublk_start_daemon(ctx, dev);
	ublk_dbg(UBLK_DBG_DEV, "%s: daemon exit %d\b", ret);
	if (ret < 0)
		ublk_ctrl_del_dev(dev);

fail:
	if (ret < 0)
		ublk_send_dev_event(ctx, dev, -1);
	if (dev)
		ublk_ctrl_deinit(dev);
	return ret;
}

static int __cmd_dev_list(struct dev_ctx *ctx);

static int cmd_dev_add(struct dev_ctx *ctx)
{
	int res;

	if (ctx->fg)
		goto run;

	ctx->_shmid = shmget(IPC_PRIVATE, sizeof(struct ublk_dev), IPC_CREAT | 0666);
	if (ctx->_shmid < 0) {
		ublk_err("%s: failed to shmget %s\n", __func__, strerror(errno));
		exit(-1);
	}
	ctx->shadow_dev = (struct ublk_dev *)shmat(ctx->_shmid, NULL, 0);
	if (ctx->shadow_dev == (struct ublk_dev *)-1) {
		ublk_err("%s: failed to shmat %s\n", __func__, strerror(errno));
		exit(-1);
	}
	ctx->_evtfd = eventfd(0, 0);
	if (ctx->_evtfd < 0) {
		ublk_err("%s: failed to create eventfd %s\n", __func__, strerror(errno));
		exit(-1);
	}

	res = fork();
	if (res == 0) {
		int res2;

		setsid();
		res2 = fork();
		if (res2 == 0) {
			/* prepare for detaching */
			close(STDIN_FILENO);
			close(STDOUT_FILENO);
			close(STDERR_FILENO);
run:
			res = __cmd_dev_add(ctx);
			return res;
		} else {
			/* detached from the foreground task */
			exit(EXIT_SUCCESS);
		}
	} else if (res > 0) {
		uint64_t id;
		int exit_code = EXIT_FAILURE;

		res = read(ctx->_evtfd, &id, sizeof(id));
		close(ctx->_evtfd);
		if (res == sizeof(id) && id != ERROR_EVTFD_DEVID) {
			ctx->dev_id = id - 1;
			if (__cmd_dev_list(ctx) >= 0)
				exit_code = EXIT_SUCCESS;
		}
		shmdt(ctx->shadow_dev);
		shmctl(ctx->_shmid, IPC_RMID, NULL);
		/* wait for child and detach from it */
		wait(NULL);
		if (exit_code == EXIT_FAILURE)
			ublk_err("%s: command failed\n", __func__);
		exit(exit_code);
	} else {
		exit(EXIT_FAILURE);
	}
}

static int __cmd_dev_del(struct dev_ctx *ctx)
{
	int number = ctx->dev_id;
	struct ublk_dev *dev;
	int ret;

	dev = ublk_ctrl_init();
	dev->dev_info.dev_id = number;

	ret = ublk_ctrl_get_info(dev);
	if (ret < 0)
		goto fail;

	ret = ublk_ctrl_stop_dev(dev);
	if (ret < 0)
		ublk_err("%s: stop dev %d failed ret %d\n", __func__, number, ret);

	ret = ublk_stop_io_daemon(dev);
	if (ret < 0)
		ublk_err("%s: stop daemon id %d dev %d, ret %d\n",
				__func__, dev->dev_info.ublksrv_pid, number, ret);
	ublk_ctrl_del_dev(dev);
fail:
	ublk_ctrl_deinit(dev);

	return (ret >= 0) ? 0 : ret;
}

static int cmd_dev_del(struct dev_ctx *ctx)
{
	int i;

	if (ctx->dev_id >= 0 || !ctx->all)
		return __cmd_dev_del(ctx);

	for (i = 0; i < 255; i++) {
		ctx->dev_id = i;
		__cmd_dev_del(ctx);
	}
	return 0;
}

static int __cmd_dev_list(struct dev_ctx *ctx)
{
	struct ublk_dev *dev = ublk_ctrl_init();
	int ret;

	if (!dev)
		return -ENODEV;

	dev->dev_info.dev_id = ctx->dev_id;

	ret = ublk_ctrl_get_info(dev);
	if (ret < 0) {
		if (ctx->logging)
			ublk_err("%s: can't get dev info from %d: %d\n",
					__func__, ctx->dev_id, ret);
	} else {
		if (ctx->shadow_dev)
			memcpy(&dev->q, ctx->shadow_dev->q, sizeof(dev->q));

		ublk_ctrl_dump(dev);
	}

	ublk_ctrl_deinit(dev);

	return ret;
}

static int cmd_dev_list(struct dev_ctx *ctx)
{
	int i;

	if (ctx->dev_id >= 0 || !ctx->all)
		return __cmd_dev_list(ctx);

	ctx->logging = false;
	for (i = 0; i < 255; i++) {
		ctx->dev_id = i;
		__cmd_dev_list(ctx);
	}
	return 0;
}

static int cmd_dev_get_features(void)
{
#define const_ilog2(x) (63 - __builtin_clzll(x))
#define FEAT_NAME(f) [const_ilog2(f)] = #f
	static const char *feat_map[] = {
		FEAT_NAME(UBLK_F_SUPPORT_ZERO_COPY),
		FEAT_NAME(UBLK_F_URING_CMD_COMP_IN_TASK),
		FEAT_NAME(UBLK_F_NEED_GET_DATA),
		FEAT_NAME(UBLK_F_USER_RECOVERY),
		FEAT_NAME(UBLK_F_USER_RECOVERY_REISSUE),
		FEAT_NAME(UBLK_F_UNPRIVILEGED_DEV),
		FEAT_NAME(UBLK_F_CMD_IOCTL_ENCODE),
		FEAT_NAME(UBLK_F_USER_COPY),
		FEAT_NAME(UBLK_F_ZONED),
		FEAT_NAME(UBLK_F_USER_RECOVERY_FAIL_IO),
		FEAT_NAME(UBLK_F_UPDATE_SIZE),
		FEAT_NAME(UBLK_F_AUTO_BUF_REG),
		FEAT_NAME(UBLK_F_QUIESCE),
		FEAT_NAME(UBLK_F_PER_IO_DAEMON),
		FEAT_NAME(UBLK_F_BUF_REG_OFF_DAEMON),
	};
	struct ublk_dev *dev;
	__u64 features = 0;
	int ret;

	dev = ublk_ctrl_init();
	if (!dev) {
		fprintf(stderr, "ublksrv_ctrl_init failed id\n");
		return -EOPNOTSUPP;
	}

	ret = ublk_ctrl_get_features(dev, &features);
	if (!ret) {
		int i;

		printf("ublk_drv features: 0x%llx\n", features);

		for (i = 0; i < sizeof(features) * 8; i++) {
			const char *feat;

			if (!((1ULL << i)  & features))
				continue;
			if (i < ARRAY_SIZE(feat_map))
				feat = feat_map[i];
			else
				feat = "unknown";
			printf("0x%-16llx: %s\n", 1ULL << i, feat);
		}
	}

	return ret;
}

static int cmd_dev_update_size(struct dev_ctx *ctx)
{
	struct ublk_dev *dev = ublk_ctrl_init();
	struct ublk_params p;
	int ret = -EINVAL;

	if (!dev)
		return -ENODEV;

	if (ctx->dev_id < 0) {
		fprintf(stderr, "device id isn't provided\n");
		goto out;
	}

	dev->dev_info.dev_id = ctx->dev_id;
	ret = ublk_ctrl_get_params(dev, &p);
	if (ret < 0) {
		ublk_err("failed to get params %d %s\n", ret, strerror(-ret));
		goto out;
	}

	if (ctx->size & ((1 << p.basic.logical_bs_shift) - 1)) {
		ublk_err("size isn't aligned with logical block size\n");
		ret = -EINVAL;
		goto out;
	}

	ret = ublk_ctrl_update_size(dev, ctx->size >> 9);
out:
	ublk_ctrl_deinit(dev);
	return ret;
}

static int cmd_dev_quiesce(struct dev_ctx *ctx)
{
	struct ublk_dev *dev = ublk_ctrl_init();
	int ret = -EINVAL;

	if (!dev)
		return -ENODEV;

	if (ctx->dev_id < 0) {
		fprintf(stderr, "device id isn't provided for quiesce\n");
		goto out;
	}
	dev->dev_info.dev_id = ctx->dev_id;
	ret = ublk_ctrl_quiesce_dev(dev, 10000);

out:
	ublk_ctrl_deinit(dev);
	return ret;
}

static void __cmd_create_help(char *exe, bool recovery)
{
	int i;

	printf("%s %s -t [null|loop|stripe|fault_inject] [-q nr_queues] [-d depth] [-n dev_id]\n",
			exe, recovery ? "recover" : "add");
	printf("\t[--foreground] [--quiet] [-z] [--auto_zc] [--auto_zc_fallback] [--debug_mask mask] [-r 0|1 ] [-g]\n");
	printf("\t[-e 0|1 ] [-i 0|1] [--no_ublk_fixed_fd]\n");
	printf("\t[--nthreads threads] [--per_io_tasks]\n");
	printf("\t[target options] [backfile1] [backfile2] ...\n");
	printf("\tdefault: nr_queues=2(max 32), depth=128(max 1024), dev_id=-1(auto allocation)\n");
	printf("\tdefault: nthreads=nr_queues");

	for (i = 0; i < ARRAY_SIZE(tgt_ops_list); i++) {
		const struct ublk_tgt_ops *ops = tgt_ops_list[i];

		if (ops->usage)
			ops->usage(ops);
	}
}

static void cmd_add_help(char *exe)
{
	__cmd_create_help(exe, false);
	printf("\n");
}

static void cmd_recover_help(char *exe)
{
	__cmd_create_help(exe, true);
	printf("\tPlease provide exact command line for creating this device with real dev_id\n");
	printf("\n");
}

static int cmd_dev_help(char *exe)
{
	cmd_add_help(exe);
	cmd_recover_help(exe);

	printf("%s del [-n dev_id] -a \n", exe);
	printf("\t -a delete all devices -n delete specified device\n\n");
	printf("%s list [-n dev_id] -a \n", exe);
	printf("\t -a list all devices, -n list specified device, default -a \n\n");
	printf("%s features\n", exe);
	printf("%s update_size -n dev_id -s|--size size_in_bytes \n", exe);
	printf("%s quiesce -n dev_id\n", exe);
	return 0;
}

int main(int argc, char *argv[])
{
	static const struct option longopts[] = {
		{ "all",		0,	NULL, 'a' },
		{ "type",		1,	NULL, 't' },
		{ "number",		1,	NULL, 'n' },
		{ "queues",		1,	NULL, 'q' },
		{ "depth",		1,	NULL, 'd' },
		{ "debug_mask",		1,	NULL,  0  },
		{ "quiet",		0,	NULL,  0  },
		{ "zero_copy",          0,      NULL, 'z' },
		{ "foreground",		0,	NULL,  0  },
		{ "recovery", 		1,      NULL, 'r' },
		{ "recovery_fail_io",	1,	NULL, 'e'},
		{ "recovery_reissue",	1,	NULL, 'i'},
		{ "get_data",		1,	NULL, 'g'},
		{ "auto_zc",		0,	NULL,  0 },
		{ "auto_zc_fallback", 	0,	NULL,  0 },
		{ "size",		1,	NULL, 's'},
		{ "nthreads",		1,	NULL,  0 },
		{ "per_io_tasks",	0,	NULL,  0 },
		{ "no_ublk_fixed_fd",	0,	NULL,  0 },
		{ 0, 0, 0, 0 }
	};
	const struct ublk_tgt_ops *ops = NULL;
	int option_idx, opt;
	const char *cmd = argv[1];
	struct dev_ctx ctx = {
		.queue_depth	=	128,
		.nr_hw_queues	=	2,
		.dev_id		=	-1,
		.tgt_type	=	"unknown",
	};
	int ret = -EINVAL, i;
	int tgt_argc = 1;
	char *tgt_argv[MAX_NR_TGT_ARG] = { NULL };
	int value;

	if (argc == 1)
		return ret;

	opterr = 0;
	optind = 2;
	while ((opt = getopt_long(argc, argv, "t:n:d:q:r:e:i:s:gaz",
				  longopts, &option_idx)) != -1) {
		switch (opt) {
		case 'a':
			ctx.all = 1;
			break;
		case 'n':
			ctx.dev_id = strtol(optarg, NULL, 10);
			break;
		case 't':
			if (strlen(optarg) < sizeof(ctx.tgt_type))
				strcpy(ctx.tgt_type, optarg);
			break;
		case 'q':
			ctx.nr_hw_queues = strtol(optarg, NULL, 10);
			break;
		case 'd':
			ctx.queue_depth = strtol(optarg, NULL, 10);
			break;
		case 'z':
			ctx.flags |= UBLK_F_SUPPORT_ZERO_COPY | UBLK_F_USER_COPY;
			break;
		case 'r':
			value = strtol(optarg, NULL, 10);
			if (value)
				ctx.flags |= UBLK_F_USER_RECOVERY;
			break;
		case 'e':
			value = strtol(optarg, NULL, 10);
			if (value)
				ctx.flags |= UBLK_F_USER_RECOVERY | UBLK_F_USER_RECOVERY_FAIL_IO;
			break;
		case 'i':
			value = strtol(optarg, NULL, 10);
			if (value)
				ctx.flags |= UBLK_F_USER_RECOVERY | UBLK_F_USER_RECOVERY_REISSUE;
			break;
		case 'g':
			ctx.flags |= UBLK_F_NEED_GET_DATA;
			break;
		case 's':
			ctx.size = strtoull(optarg, NULL, 10);
			break;
		case 0:
			if (!strcmp(longopts[option_idx].name, "debug_mask"))
				ublk_dbg_mask = strtol(optarg, NULL, 16);
			if (!strcmp(longopts[option_idx].name, "quiet"))
				ublk_dbg_mask = 0;
			if (!strcmp(longopts[option_idx].name, "foreground"))
				ctx.fg = 1;
			if (!strcmp(longopts[option_idx].name, "auto_zc"))
				ctx.flags |= UBLK_F_AUTO_BUF_REG;
			if (!strcmp(longopts[option_idx].name, "auto_zc_fallback"))
				ctx.auto_zc_fallback = 1;
			if (!strcmp(longopts[option_idx].name, "nthreads"))
				ctx.nthreads = strtol(optarg, NULL, 10);
			if (!strcmp(longopts[option_idx].name, "per_io_tasks"))
				ctx.per_io_tasks = 1;
			if (!strcmp(longopts[option_idx].name, "no_ublk_fixed_fd"))
				ctx.no_ublk_fixed_fd = 1;
			break;
		case '?':
			/*
			 * target requires every option must have argument
			 */
			if (argv[optind][0] == '-' || argv[optind - 1][0] != '-') {
				fprintf(stderr, "every target option requires argument: %s %s\n",
						argv[optind - 1], argv[optind]);
				exit(EXIT_FAILURE);
			}

			if (tgt_argc < (MAX_NR_TGT_ARG - 1) / 2) {
				tgt_argv[tgt_argc++] = argv[optind - 1];
				tgt_argv[tgt_argc++] = argv[optind];
			} else {
				fprintf(stderr, "too many target options\n");
				exit(EXIT_FAILURE);
			}
			optind += 1;
			break;
		}
	}

	/* auto_zc_fallback depends on F_AUTO_BUF_REG & F_SUPPORT_ZERO_COPY */
	if (ctx.auto_zc_fallback &&
	    !((ctx.flags & UBLK_F_AUTO_BUF_REG) &&
		    (ctx.flags & UBLK_F_SUPPORT_ZERO_COPY))) {
		ublk_err("%s: auto_zc_fallback is set but neither "
				"F_AUTO_BUF_REG nor F_SUPPORT_ZERO_COPY is enabled\n",
					__func__);
		return -EINVAL;
	}

	i = optind;
	while (i < argc && ctx.nr_files < MAX_BACK_FILES) {
		ctx.files[ctx.nr_files++] = argv[i++];
	}

	ops = ublk_find_tgt(ctx.tgt_type);
	if (ops && ops->parse_cmd_line) {
		optind = 0;

		tgt_argv[0] = ctx.tgt_type;
		ops->parse_cmd_line(&ctx, tgt_argc, tgt_argv);
	}

	if (!strcmp(cmd, "add"))
		ret = cmd_dev_add(&ctx);
	else if (!strcmp(cmd, "recover")) {
		if (ctx.dev_id < 0) {
			fprintf(stderr, "device id isn't provided for recovering\n");
			ret = -EINVAL;
		} else {
			ctx.recovery = 1;
			ret = cmd_dev_add(&ctx);
		}
	} else if (!strcmp(cmd, "del"))
		ret = cmd_dev_del(&ctx);
	else if (!strcmp(cmd, "list")) {
		ctx.all = 1;
		ret = cmd_dev_list(&ctx);
	} else if (!strcmp(cmd, "help"))
		ret = cmd_dev_help(argv[0]);
	else if (!strcmp(cmd, "features"))
		ret = cmd_dev_get_features();
	else if (!strcmp(cmd, "update_size"))
		ret = cmd_dev_update_size(&ctx);
	else if (!strcmp(cmd, "quiesce"))
		ret = cmd_dev_quiesce(&ctx);
	else
		cmd_dev_help(argv[0]);

	return ret;
}
