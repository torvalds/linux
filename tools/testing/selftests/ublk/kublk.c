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
			printf("\tqueue %u: tid %d affinity(%s)\n",
					i, dev->q[i].tid, buf);
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

	io_uring_unregister_buffers(&q->ring);

	io_uring_unregister_ring_fd(&q->ring);

	if (q->ring.ring_fd > 0) {
		io_uring_unregister_files(&q->ring);
		close(q->ring.ring_fd);
		q->ring.ring_fd = -1;
	}

	if (q->io_cmd_buf)
		munmap(q->io_cmd_buf, ublk_queue_cmd_buf_sz(q));

	for (i = 0; i < nr_ios; i++)
		free(q->ios[i].buf_addr);
}

static int ublk_queue_init(struct ublk_queue *q)
{
	struct ublk_dev *dev = q->dev;
	int depth = dev->dev_info.queue_depth;
	int i, ret = -1;
	int cmd_buf_size, io_buf_size;
	unsigned long off;
	int ring_depth = dev->tgt.sq_depth, cq_depth = dev->tgt.cq_depth;

	q->tgt_ops = dev->tgt.ops;
	q->state = 0;
	q->q_depth = depth;
	q->cmd_inflight = 0;
	q->tid = gettid();

	if (dev->dev_info.flags & UBLK_F_SUPPORT_ZERO_COPY) {
		q->state |= UBLKSRV_NO_BUF;
		q->state |= UBLKSRV_ZC;
	}

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
		q->ios[i].flags = UBLKSRV_NEED_FETCH_RQ | UBLKSRV_IO_FREE;

		if (q->state & UBLKSRV_NO_BUF)
			continue;

		if (posix_memalign((void **)&q->ios[i].buf_addr,
					getpagesize(), io_buf_size)) {
			ublk_err("ublk dev %d queue %d io %d posix_memalign failed %m\n",
					dev->dev_info.dev_id, q->q_id, i);
			goto fail;
		}
	}

	ret = ublk_setup_ring(&q->ring, ring_depth, cq_depth,
			IORING_SETUP_COOP_TASKRUN |
			IORING_SETUP_SINGLE_ISSUER |
			IORING_SETUP_DEFER_TASKRUN);
	if (ret < 0) {
		ublk_err("ublk dev %d queue %d setup io_uring failed %d\n",
				q->dev->dev_info.dev_id, q->q_id, ret);
		goto fail;
	}

	if (dev->dev_info.flags & UBLK_F_SUPPORT_ZERO_COPY) {
		ret = io_uring_register_buffers_sparse(&q->ring, q->q_depth);
		if (ret) {
			ublk_err("ublk dev %d queue %d register spare buffers failed %d",
					dev->dev_info.dev_id, q->q_id, ret);
			goto fail;
		}
	}

	io_uring_register_ring_fd(&q->ring);

	ret = io_uring_register_files(&q->ring, dev->fds, dev->nr_fds);
	if (ret) {
		ublk_err("ublk dev %d queue %d register files failed %d\n",
				q->dev->dev_info.dev_id, q->q_id, ret);
		goto fail;
	}

	return 0;
 fail:
	ublk_queue_deinit(q);
	ublk_err("ublk dev %d queue %d failed\n",
			dev->dev_info.dev_id, q->q_id);
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

int ublk_queue_io_cmd(struct ublk_queue *q, struct ublk_io *io, unsigned tag)
{
	struct ublksrv_io_cmd *cmd;
	struct io_uring_sqe *sqe[1];
	unsigned int cmd_op = 0;
	__u64 user_data;

	/* only freed io can be issued */
	if (!(io->flags & UBLKSRV_IO_FREE))
		return 0;

	/*
	 * we issue because we need either fetching or committing or
	 * getting data
	 */
	if (!(io->flags &
		(UBLKSRV_NEED_FETCH_RQ | UBLKSRV_NEED_COMMIT_RQ_COMP | UBLKSRV_NEED_GET_DATA)))
		return 0;

	if (io->flags & UBLKSRV_NEED_GET_DATA)
		cmd_op = UBLK_U_IO_NEED_GET_DATA;
	else if (io->flags & UBLKSRV_NEED_COMMIT_RQ_COMP)
		cmd_op = UBLK_U_IO_COMMIT_AND_FETCH_REQ;
	else if (io->flags & UBLKSRV_NEED_FETCH_RQ)
		cmd_op = UBLK_U_IO_FETCH_REQ;

	if (io_uring_sq_space_left(&q->ring) < 1)
		io_uring_submit(&q->ring);

	ublk_queue_alloc_sqes(q, sqe, 1);
	if (!sqe[0]) {
		ublk_err("%s: run out of sqe %d, tag %d\n",
				__func__, q->q_id, tag);
		return -1;
	}

	cmd = (struct ublksrv_io_cmd *)ublk_get_sqe_cmd(sqe[0]);

	if (cmd_op == UBLK_U_IO_COMMIT_AND_FETCH_REQ)
		cmd->result = io->result;

	/* These fields should be written once, never change */
	ublk_set_sqe_cmd_op(sqe[0], cmd_op);
	sqe[0]->fd		= 0;	/* dev->fds[0] */
	sqe[0]->opcode	= IORING_OP_URING_CMD;
	sqe[0]->flags	= IOSQE_FIXED_FILE;
	sqe[0]->rw_flags	= 0;
	cmd->tag	= tag;
	cmd->q_id	= q->q_id;
	if (!(q->state & UBLKSRV_NO_BUF))
		cmd->addr	= (__u64) (uintptr_t) io->buf_addr;
	else
		cmd->addr	= 0;

	user_data = build_user_data(tag, _IOC_NR(cmd_op), 0, 0);
	io_uring_sqe_set_data64(sqe[0], user_data);

	io->flags = 0;

	q->cmd_inflight += 1;

	ublk_dbg(UBLK_DBG_IO_CMD, "%s: (qid %d tag %u cmd_op %u) iof %x stopping %d\n",
			__func__, q->q_id, tag, cmd_op,
			io->flags, !!(q->state & UBLKSRV_QUEUE_STOPPING));
	return 1;
}

static void ublk_submit_fetch_commands(struct ublk_queue *q)
{
	int i = 0;

	for (i = 0; i < q->q_depth; i++)
		ublk_queue_io_cmd(q, &q->ios[i], i);
}

static int ublk_queue_is_idle(struct ublk_queue *q)
{
	return !io_uring_sq_ready(&q->ring) && !q->io_inflight;
}

static int ublk_queue_is_done(struct ublk_queue *q)
{
	return (q->state & UBLKSRV_QUEUE_STOPPING) && ublk_queue_is_idle(q);
}

static inline void ublksrv_handle_tgt_cqe(struct ublk_queue *q,
		struct io_uring_cqe *cqe)
{
	unsigned tag = user_data_to_tag(cqe->user_data);

	if (cqe->res < 0 && cqe->res != -EAGAIN)
		ublk_err("%s: failed tgt io: res %d qid %u tag %u, cmd_op %u\n",
			__func__, cqe->res, q->q_id,
			user_data_to_tag(cqe->user_data),
			user_data_to_op(cqe->user_data));

	if (q->tgt_ops->tgt_io_done)
		q->tgt_ops->tgt_io_done(q, tag, cqe);
}

static void ublk_handle_cqe(struct io_uring *r,
		struct io_uring_cqe *cqe, void *data)
{
	struct ublk_queue *q = container_of(r, struct ublk_queue, ring);
	unsigned tag = user_data_to_tag(cqe->user_data);
	unsigned cmd_op = user_data_to_op(cqe->user_data);
	int fetch = (cqe->res != UBLK_IO_RES_ABORT) &&
		!(q->state & UBLKSRV_QUEUE_STOPPING);
	struct ublk_io *io;

	if (cqe->res < 0 && cqe->res != -ENODEV)
		ublk_err("%s: res %d userdata %llx queue state %x\n", __func__,
				cqe->res, cqe->user_data, q->state);

	ublk_dbg(UBLK_DBG_IO_CMD, "%s: res %d (qid %d tag %u cmd_op %u target %d/%d) stopping %d\n",
			__func__, cqe->res, q->q_id, tag, cmd_op,
			is_target_io(cqe->user_data),
			user_data_to_tgt_data(cqe->user_data),
			(q->state & UBLKSRV_QUEUE_STOPPING));

	/* Don't retrieve io in case of target io */
	if (is_target_io(cqe->user_data)) {
		ublksrv_handle_tgt_cqe(q, cqe);
		return;
	}

	io = &q->ios[tag];
	q->cmd_inflight--;

	if (!fetch) {
		q->state |= UBLKSRV_QUEUE_STOPPING;
		io->flags &= ~UBLKSRV_NEED_FETCH_RQ;
	}

	if (cqe->res == UBLK_IO_RES_OK) {
		assert(tag < q->q_depth);
		if (q->tgt_ops->queue_io)
			q->tgt_ops->queue_io(q, tag);
	} else if (cqe->res == UBLK_IO_RES_NEED_GET_DATA) {
		io->flags |= UBLKSRV_NEED_GET_DATA | UBLKSRV_IO_FREE;
		ublk_queue_io_cmd(q, io, tag);
	} else {
		/*
		 * COMMIT_REQ will be completed immediately since no fetching
		 * piggyback is required.
		 *
		 * Marking IO_FREE only, then this io won't be issued since
		 * we only issue io with (UBLKSRV_IO_FREE | UBLKSRV_NEED_*)
		 *
		 * */
		io->flags = UBLKSRV_IO_FREE;
	}
}

static int ublk_reap_events_uring(struct io_uring *r)
{
	struct io_uring_cqe *cqe;
	unsigned head;
	int count = 0;

	io_uring_for_each_cqe(r, head, cqe) {
		ublk_handle_cqe(r, cqe, NULL);
		count += 1;
	}
	io_uring_cq_advance(r, count);

	return count;
}

static int ublk_process_io(struct ublk_queue *q)
{
	int ret, reapped;

	ublk_dbg(UBLK_DBG_QUEUE, "dev%d-q%d: to_submit %d inflight cmd %u stopping %d\n",
				q->dev->dev_info.dev_id,
				q->q_id, io_uring_sq_ready(&q->ring),
				q->cmd_inflight,
				(q->state & UBLKSRV_QUEUE_STOPPING));

	if (ublk_queue_is_done(q))
		return -ENODEV;

	ret = io_uring_submit_and_wait(&q->ring, 1);
	reapped = ublk_reap_events_uring(&q->ring);

	ublk_dbg(UBLK_DBG_QUEUE, "submit result %d, reapped %d stop %d idle %d\n",
			ret, reapped, (q->state & UBLKSRV_QUEUE_STOPPING),
			(q->state & UBLKSRV_QUEUE_IDLE));

	return reapped;
}

static void ublk_queue_set_sched_affinity(const struct ublk_queue *q,
		cpu_set_t *cpuset)
{
        if (sched_setaffinity(0, sizeof(*cpuset), cpuset) < 0)
                ublk_err("ublk dev %u queue %u set affinity failed",
                                q->dev->dev_info.dev_id, q->q_id);
}

struct ublk_queue_info {
	struct ublk_queue 	*q;
	sem_t 			*queue_sem;
	cpu_set_t 		*affinity;
};

static void *ublk_io_handler_fn(void *data)
{
	struct ublk_queue_info *info = data;
	struct ublk_queue *q = info->q;
	int dev_id = q->dev->dev_info.dev_id;
	int ret;

	ret = ublk_queue_init(q);
	if (ret) {
		ublk_err("ublk dev %d queue %d init queue failed\n",
				dev_id, q->q_id);
		return NULL;
	}
	/* IO perf is sensitive with queue pthread affinity on NUMA machine*/
	ublk_queue_set_sched_affinity(q, info->affinity);
	sem_post(info->queue_sem);

	ublk_dbg(UBLK_DBG_QUEUE, "tid %d: ublk dev %d queue %d started\n",
			q->tid, dev_id, q->q_id);

	/* submit all io commands to ublk driver */
	ublk_submit_fetch_commands(q);
	do {
		if (ublk_process_io(q) < 0)
			break;
	} while (1);

	ublk_dbg(UBLK_DBG_QUEUE, "ublk dev %d queue %d exited\n", dev_id, q->q_id);
	ublk_queue_deinit(q);
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
	struct ublk_queue_info *qinfo;
	cpu_set_t *affinity_buf;
	void *thread_ret;
	sem_t queue_sem;
	int ret, i;

	ublk_dbg(UBLK_DBG_DEV, "%s enter\n", __func__);

	qinfo = (struct ublk_queue_info *)calloc(sizeof(struct ublk_queue_info),
			dinfo->nr_hw_queues);
	if (!qinfo)
		return -ENOMEM;

	sem_init(&queue_sem, 0, 0);
	ret = ublk_dev_prep(ctx, dev);
	if (ret)
		return ret;

	ret = ublk_ctrl_get_affinity(dev, &affinity_buf);
	if (ret)
		return ret;

	for (i = 0; i < dinfo->nr_hw_queues; i++) {
		dev->q[i].dev = dev;
		dev->q[i].q_id = i;

		qinfo[i].q = &dev->q[i];
		qinfo[i].queue_sem = &queue_sem;
		qinfo[i].affinity = &affinity_buf[i];
		pthread_create(&dev->q[i].thread, NULL,
				ublk_io_handler_fn,
				&qinfo[i]);
	}

	for (i = 0; i < dinfo->nr_hw_queues; i++)
		sem_wait(&queue_sem);
	free(qinfo);
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
	for (i = 0; i < dinfo->nr_hw_queues; i++)
		pthread_join(dev->q[i].thread, &thread_ret);
 fail:
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
	unsigned nr_queues = ctx->nr_hw_queues;
	const char *tgt_type = ctx->tgt_type;
	unsigned depth = ctx->queue_depth;
	__u64 features;
	const struct ublk_tgt_ops *ops;
	struct ublksrv_ctrl_dev_info *info;
	struct ublk_dev *dev;
	int dev_id = ctx->dev_id;
	int ret, i;

	ops = ublk_find_tgt(tgt_type);
	if (!ops) {
		ublk_err("%s: no such tgt type, type %s\n",
				__func__, tgt_type);
		return -ENODEV;
	}

	if (nr_queues > UBLK_MAX_QUEUES || depth > UBLK_QUEUE_DEPTH) {
		ublk_err("%s: invalid nr_queues or depth queues %u depth %u\n",
				__func__, nr_queues, depth);
		return -EINVAL;
	}

	dev = ublk_ctrl_init();
	if (!dev) {
		ublk_err("%s: can't alloc dev id %d, type %s\n",
				__func__, dev_id, tgt_type);
		return -ENOMEM;
	}

	/* kernel doesn't support get_features */
	ret = ublk_ctrl_get_features(dev, &features);
	if (ret < 0)
		return -EINVAL;

	if (!(features & UBLK_F_CMD_IOCTL_ENCODE))
		return -ENOTSUP;

	info = &dev->dev_info;
	info->dev_id = ctx->dev_id;
	info->nr_hw_queues = nr_queues;
	info->queue_depth = depth;
	info->flags = ctx->flags;
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
	static const char *feat_map[] = {
		[const_ilog2(UBLK_F_SUPPORT_ZERO_COPY)] = "ZERO_COPY",
		[const_ilog2(UBLK_F_URING_CMD_COMP_IN_TASK)] = "COMP_IN_TASK",
		[const_ilog2(UBLK_F_NEED_GET_DATA)] = "GET_DATA",
		[const_ilog2(UBLK_F_USER_RECOVERY)] = "USER_RECOVERY",
		[const_ilog2(UBLK_F_USER_RECOVERY_REISSUE)] = "RECOVERY_REISSUE",
		[const_ilog2(UBLK_F_UNPRIVILEGED_DEV)] = "UNPRIVILEGED_DEV",
		[const_ilog2(UBLK_F_CMD_IOCTL_ENCODE)] = "CMD_IOCTL_ENCODE",
		[const_ilog2(UBLK_F_USER_COPY)] = "USER_COPY",
		[const_ilog2(UBLK_F_ZONED)] = "ZONED",
		[const_ilog2(UBLK_F_USER_RECOVERY_FAIL_IO)] = "RECOVERY_FAIL_IO",
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
			if (i < sizeof(feat_map) / sizeof(feat_map[0]))
				feat = feat_map[i];
			else
				feat = "unknown";
			printf("\t%-20s: 0x%llx\n", feat, 1ULL << i);
		}
	}

	return ret;
}

static void __cmd_create_help(char *exe, bool recovery)
{
	int i;

	printf("%s %s -t [null|loop|stripe|fault_inject] [-q nr_queues] [-d depth] [-n dev_id]\n",
			exe, recovery ? "recover" : "add");
	printf("\t[--foreground] [--quiet] [-z] [--debug_mask mask] [-r 0|1 ] [-g]\n");
	printf("\t[-e 0|1 ] [-i 0|1]\n");
	printf("\t[target options] [backfile1] [backfile2] ...\n");
	printf("\tdefault: nr_queues=2(max 32), depth=128(max 1024), dev_id=-1(auto allocation)\n");

	for (i = 0; i < sizeof(tgt_ops_list) / sizeof(tgt_ops_list[0]); i++) {
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
	while ((opt = getopt_long(argc, argv, "t:n:d:q:r:e:i:gaz",
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
		case 0:
			if (!strcmp(longopts[option_idx].name, "debug_mask"))
				ublk_dbg_mask = strtol(optarg, NULL, 16);
			if (!strcmp(longopts[option_idx].name, "quiet"))
				ublk_dbg_mask = 0;
			if (!strcmp(longopts[option_idx].name, "foreground"))
				ctx.fg = 1;
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
	else
		cmd_dev_help(argv[0]);

	return ret;
}
