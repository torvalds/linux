// SPDX-License-Identifier: GPL-2.0
/*
 * Bring a ublk device live while some of its fetched io commands are
 * canceled.
 *
 * A cancel completes a fetched command and clears io->cmd, but the io
 * still counts as ready. Only ubq->canceling keeps ublk_queue_rq() away
 * from the NULL io->cmd. Each mode below loses ->canceling in another way:
 *
 * stop_start:    fetch every tag, STOP_DEV before START_DEV, START_DEV.
 *                STOP_DEV cancels the commands of the attached server,
 *                so START_DEV must get -EBUSY until that server is gone.
 *                An unfixed kernel starts the device over them.
 *
 * partial_fetch: task A fetches tags 0..depth-2 and dies, so its
 *                commands are canceled. Task B fetches the last tag, and
 *                an unfixed kernel clears ->canceling when the queue gets
 *                ready. /dev/ublkcN stays open all the time, so
 *                ublk_ch_release() never resets the queue.
 *
 * recovery:      a UBLK_F_USER_RECOVERY device with two queues loses its
 *                server. During recovery task Q0 fetches queue 0, which
 *                clears q0->canceling, then dies. An unfixed kernel still
 *                has ub->canceling set because queue 1 is not ready, so
 *                ublk_start_cancel() does not mark queue 0 again. Reads
 *                are issued on a CPU mapped to queue 0.
 *
 * In partial_fetch and recovery, START_DEV / END_USER_RECOVERY must refuse
 * with -EBUSY. An unfixed kernel oopses in ublk_queue_cmd() on a NULL
 * io->cmd.
 *
 * stop_restart:  control mode. STOP_DEV on a new device, before any
 *                server opened it, takes no command. A server started
 *                afterwards has to start the device and serve I/O.
 *
 * stop_attached: STOP_DEV on a device whose server opened it but fetched
 *                nothing stops that server too: FETCH gets ABORT and
 *                START_DEV -EBUSY, until a new server opens the device.
 *
 * stop_live_restart: STOP_DEV on a live device; once its server is gone,
 *                a new server has to fetch and start it again. An unfixed
 *                ublk_ch_release() skips the reset once the disk is gone,
 *                so the new FETCH gets -EBUSY.
 *
 * race_start:    STOP_DEV and START_DEV at the same time on a device whose
 *                server fetched every tag. START_DEV either wins, and
 *                reads complete (they fail once STOP_DEV removes the
 *                disk), or gets -EBUSY. An unfixed kernel cancels the
 *                commands of the live disk.
 *
 * race_fetch:    STOP_DEV while a server opens the device and fetches,
 *                then START_DEV. If STOP_DEV came before the open, it must
 *                not take any command, so START_DEV works and every read
 *                succeeds; otherwise START_DEV gets -EBUSY. An unfixed
 *                kernel takes commands fetched after its unlock and goes
 *                live over them.
 *
 * race_async_fetch: FETCH with IOSQE_ASYNC while STOP_DEV cancels,
 *                then close the ring. An unfixed FETCH marks its command
 *                cancelable only after publishing it and dropping
 *                ub->mutex; a cancel in between completes a command which
 *                is then put on io_uring's cancelable list. Closing the
 *                ring walks that list: KASAN reports a use after free.
 *                Needs a KASAN kernel to see the bug; otherwise it must
 *                just not crash.
 *
 * A dying task is a child process which fetches and then calls exec().
 * exec() cancels the task's uring_cmds before it returns, so the cancel
 * is done once the child is reaped. A thread exit does not cancel them,
 * and closing the ring cancels them later, from io_ring_exit_work().
 */
#include <sched.h>

#include "kublk.h"
#include "../kselftest.h"

#define NR_QUEUES	2
#define DEPTH		4
#define BUF_SIZE	(64 << 10)
#define DEV_SECTORS	(64 << 11)	/* 64MB */
#define NR_READS	(DEPTH * 2)
#define SERVE_DELAY_US	200000
#define RACE_LOOPS	50
#define ASYNC_LOOPS	300

/* one control handle per thread, the race modes send commands from two */
static __thread struct ublk_dev *ctrl_dev;
static int cdev_fd = -1;
static int dev_id = -1;
static int nr_queues;
static void *bufs[NR_QUEUES][DEPTH];
static const struct ublksrv_io_desc *iods[NR_QUEUES];
static size_t iods_len;

static struct io_uring_sqe *get_sqe(struct io_uring *ring)
{
	struct io_uring_sqe *sqe = io_uring_get_sqe(ring);

	if (!sqe) {
		fprintf(stderr, "out of sqes\n");
		exit(KSFT_FAIL);
	}
	return sqe;
}

static struct ublk_dev *ctrl(void)
{
	if (!ctrl_dev) {
		ctrl_dev = ublk_ctrl_init();
		if (!ctrl_dev) {
			fprintf(stderr, "ublk_ctrl_init failed\n");
			exit(KSFT_FAIL);
		}
	}
	ctrl_dev->dev_info.dev_id = dev_id;
	return ctrl_dev;
}

static void ctrl_put(void)
{
	if (ctrl_dev)
		ublk_ctrl_deinit(ctrl_dev);
	ctrl_dev = NULL;
}

static int dev_state(void)
{
	struct ublk_dev *dev = ctrl();
	int ret = ublk_ctrl_get_info(dev);

	return ret ? ret : dev->dev_info.state;
}

static int open_cdev(void)
{
	size_t max_len = UBLK_MAX_QUEUE_DEPTH * sizeof(struct ublksrv_io_desc);
	int pg = getpagesize();
	char path[64];

	snprintf(path, sizeof(path), "/dev/ublkc%d", dev_id);
	for (int i = 0; i < 100 && cdev_fd < 0; i++) {
		cdev_fd = open(path, O_RDWR);
		if (cdev_fd < 0)
			usleep(50000);
	}
	if (cdev_fd < 0)
		return -errno;

	/* queue q's descriptors start at q * the size for the max depth */
	max_len = (max_len + pg - 1) & ~(size_t)(pg - 1);
	iods_len = (DEPTH * sizeof(struct ublksrv_io_desc) + pg - 1) &
		~(size_t)(pg - 1);
	for (int q = 0; q < nr_queues; q++) {
		void *p = mmap(NULL, iods_len, PROT_READ,
			       MAP_SHARED | MAP_POPULATE, cdev_fd,
			       UBLKSRV_CMD_BUF_OFFSET + q * max_len);

		if (p == MAP_FAILED)
			return -errno;
		iods[q] = p;
	}
	return 0;
}

/* the last reference to /dev/ublkcN runs ublk_ch_release() */
static void close_cdev(void)
{
	for (int q = 0; q < nr_queues; q++) {
		if (iods[q])
			munmap((void *)iods[q], iods_len);
		iods[q] = NULL;
	}
	if (cdev_fd >= 0)
		close(cdev_fd);
	cdev_fd = -1;
}

/* ADD_DEV and SET_PARAMS, without opening /dev/ublkcN */
static int add_dev_noopen(int queues, __u64 flags)
{
	struct ublk_dev *dev = ctrl();
	struct ublksrv_ctrl_dev_info info = {
		.nr_hw_queues	= queues,
		.queue_depth	= DEPTH,
		.max_io_buf_bytes = BUF_SIZE,
		.dev_id		= -1,
		.flags		= UBLK_F_NO_AUTO_PART_SCAN | flags,
	};
	struct ublk_params p = {
		.types	= UBLK_PARAM_TYPE_BASIC,
		.basic	= {
			.logical_bs_shift	= 9,
			.physical_bs_shift	= 12,
			.io_opt_shift		= 12,
			.io_min_shift		= 9,
			.max_sectors		= BUF_SIZE >> 9,
			.dev_sectors		= DEV_SECTORS,
		},
	};
	int ret;

	nr_queues = queues;
	dev->dev_info = info;
	ret = ublk_ctrl_add_dev(dev);
	if (ret)
		return ret;
	dev_id = dev->dev_info.dev_id;

	ret = ublk_ctrl_set_params(ctrl(), &p);
	if (ret)
		return ret;

	for (int q = 0; q < queues; q++)
		for (int i = 0; i < DEPTH; i++)
			if (!bufs[q][i] &&
			    posix_memalign(&bufs[q][i], getpagesize(), BUF_SIZE))
				return -ENOMEM;
	return 0;
}

static int add_dev(int queues, __u64 flags)
{
	return add_dev_noopen(queues, flags) ?: open_cdev();
}

static void cleanup(void)
{
	if (dev_id < 0)
		return;
	ublk_ctrl_stop_dev(ctrl());
	close_cdev();
	ublk_ctrl_del_dev(ctrl());
	dev_id = -1;
}

/* race_async_fetch sets IOSQE_ASYNC on the io commands */
static int io_cmd_sqe_flags;

static void queue_io_cmd(struct io_uring *ring, __u32 op, int q, int tag,
			 int res)
{
	struct io_uring_sqe *sqe = get_sqe(ring);
	struct ublksrv_io_cmd *cmd = (struct ublksrv_io_cmd *)sqe->cmd;

	memset(sqe, 0, sizeof(*sqe));
	sqe->fd = cdev_fd;
	sqe->opcode = IORING_OP_URING_CMD;
	sqe->flags = io_cmd_sqe_flags;
	ublk_set_sqe_cmd_op(sqe, op);
	cmd->q_id = q;
	cmd->tag = tag;
	cmd->result = res;
	cmd->addr = (__u64)(uintptr_t)bufs[q][tag];
	io_uring_sqe_set_data64(sqe, (q << 16) | tag);
}

struct async_arg {
	pthread_barrier_t go, stopped;
};

/*
 * FETCH every tag with IOSQE_ASYNC, racing STOP_DEV, then close the ring once
 * STOP_DEV returned: that walks io_uring's list of cancelable commands.
 */
static void *race_async_fn(void *data)
{
	struct async_arg *a = data;
	struct io_uring ring;
	int ok = !io_uring_queue_init(DEPTH, &ring, 0);

	if (ok)
		for (int tag = 0; tag < DEPTH; tag++)
			queue_io_cmd(&ring, UBLK_U_IO_FETCH_REQ, 0, tag, 0);
	pthread_barrier_wait(&a->go);
	if (ok)
		io_uring_submit(&ring);
	pthread_barrier_wait(&a->stopped);
	if (ok)
		io_uring_queue_exit(&ring);
	return NULL;
}

/* reap @nr completions of canceled fetch commands, return how many */
static int reap_aborts(struct io_uring *ring, int nr)
{
	struct __kernel_timespec ts = { .tv_sec = 2 };
	struct io_uring_cqe *cqe;
	int aborted = 0;

	while (nr--) {
		if (io_uring_wait_cqe_timeout(ring, &cqe, &ts))
			break;
		if (cqe->res == UBLK_IO_RES_ABORT)
			aborted++;
		io_uring_cqe_seen(ring, cqe);
	}
	return aborted;
}

/*
 * A server thread fetches @nr_tags tags of queue @q, then completes each
 * request after @delay_us, until its commands are aborted.
 */
struct server {
	int q, first_tag, nr_tags, delay_us;
	int failed;	/* result which ended the serve loop, 0 if none */
	pthread_t thread;
	pthread_barrier_t fetched;
	/* race_fetch: wait for @go and @race_delay_us, open, fetch one by one */
	pthread_barrier_t go;
	int race, race_delay_us;
};

static void *server_fn(void *data)
{
	struct server *s = data;
	struct io_uring ring;
	struct io_uring_cqe *cqe;

	if (io_uring_queue_init(DEPTH, &ring, 0)) {
		pthread_barrier_wait(&s->fetched);
		return NULL;
	}
	if (s->race) {
		pthread_barrier_wait(&s->go);
		usleep(s->race_delay_us);
		if (open_cdev()) {
			pthread_barrier_wait(&s->fetched);
			io_uring_queue_exit(&ring);
			return NULL;
		}
	}
	for (int tag = s->first_tag; tag < s->first_tag + s->nr_tags; tag++) {
		queue_io_cmd(&ring, UBLK_U_IO_FETCH_REQ, s->q, tag, 0);
		if (s->race)
			io_uring_submit(&ring);
	}
	io_uring_submit(&ring);
	pthread_barrier_wait(&s->fetched);

	while (!io_uring_wait_cqe(&ring, &cqe)) {
		int tag = cqe->user_data & 0xffff;
		const struct ublksrv_io_desc *iod = &iods[s->q][tag];
		int res = cqe->res;

		io_uring_cqe_seen(&ring, cqe);
		if (res != UBLK_IO_RES_OK) {
			__atomic_store_n(&s->failed, res, __ATOMIC_RELEASE);
			break;
		}
		usleep(s->delay_us);
		res = ublksrv_get_op(iod) <= UBLK_IO_OP_WRITE ?
			iod->nr_sectors << 9 : 0;
		queue_io_cmd(&ring, UBLK_U_IO_COMMIT_AND_FETCH_REQ, s->q, tag,
			     res);
		io_uring_submit(&ring);
	}
	io_uring_queue_exit(&ring);
	return NULL;
}

/* returns once the fetch commands are issued; with @race, call race_go() */
static void server_start(struct server *s)
{
	pthread_barrier_init(&s->fetched, NULL, 2);
	pthread_barrier_init(&s->go, NULL, 2);
	pthread_create(&s->thread, NULL, server_fn, s);
	if (!s->race)
		pthread_barrier_wait(&s->fetched);
}

/* wait up to 5s for the serve loop of @s to end, return its result */
static int server_wait_failed(struct server *s)
{
	int res = 0;

	for (int i = 0; i < 500 && !res; i++) {
		res = __atomic_load_n(&s->failed, __ATOMIC_ACQUIRE);
		if (!res)
			usleep(10000);
	}
	return res;
}

/*
 * A task fetches @nr_tags tags of queue @q and dies. Returns once its
 * commands are canceled, see the top of this file.
 */
static int fetch_and_die(int q, int first_tag, int nr_tags)
{
	int status;
	pid_t pid = fork();

	if (pid < 0)
		return -1;
	if (!pid) {
		struct io_uring ring;

		if (io_uring_queue_init(DEPTH, &ring, 0))
			_exit(1);
		for (int tag = first_tag; tag < first_tag + nr_tags; tag++)
			queue_io_cmd(&ring, UBLK_U_IO_FETCH_REQ, q, tag, 0);
		if (io_uring_submit(&ring) != nr_tags)
			_exit(1);
		execlp("true", "true", NULL);
		_exit(1);
	}
	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) ||
	    WEXITSTATUS(status))
		return -1;
	printf("q%d: task died with %d fetch cmds in flight\n", q, nr_tags);
	return 0;
}

struct reads {
	struct io_uring ring;
	void *buf;
	int fd, done, ok, eio, other;
};

/*
 * Issue NR_READS reads at once, from @cpu if it is not negative: blk-mq
 * maps the submitting CPU to the hw queue. A server holding its live
 * tags for a while makes the other reads take the canceled tags.
 */
static int open_tries = 100;	/* 50ms each */

static int reads_submit(struct reads *r, int cpu)
{
	char path[64];
	cpu_set_t set, old;

	snprintf(path, sizeof(path), "/dev/ublkb%d", dev_id);
	r->fd = -1;
	for (int i = 0; i < open_tries && r->fd < 0; i++) {
		r->fd = open(path, O_RDONLY | O_DIRECT);
		if (r->fd < 0)
			usleep(50000);
	}
	if (r->fd < 0) {
		fprintf(stderr, "open %s: %s\n", path, strerror(errno));
		return -1;
	}
	if (posix_memalign(&r->buf, 4096, NR_READS * 4096) ||
	    io_uring_queue_init(NR_READS, &r->ring, 0))
		return -1;

	for (int i = 0; i < NR_READS; i++)
		io_uring_prep_read(get_sqe(&r->ring), r->fd,
				   r->buf + i * 4096, 4096, i * 4096);

	if (cpu >= 0) {
		sched_getaffinity(0, sizeof(old), &old);
		CPU_ZERO(&set);
		CPU_SET(cpu, &set);
		sched_setaffinity(0, sizeof(set), &set);
	}
	io_uring_submit(&r->ring);
	if (cpu >= 0)
		sched_setaffinity(0, sizeof(old), &old);
	return 0;
}

static void reads_reap(struct reads *r, int timeout_s)
{
	struct __kernel_timespec ts = { .tv_sec = timeout_s };
	struct io_uring_cqe *cqe;

	while (r->done < NR_READS &&
	       !io_uring_wait_cqe_timeout(&r->ring, &cqe, &ts)) {
		if (cqe->res == 4096)
			r->ok++;
		else if (cqe->res == -EIO)
			r->eio++;
		else
			r->other++;
		r->done++;
		io_uring_cqe_seen(&r->ring, cqe);
	}
}

static void reads_put(struct reads *r)
{
	io_uring_queue_exit(&r->ring);
	close(r->fd);
	free(r->buf);
}

static int reads_result(struct reads *r)
{
	printf("reads: %d ok, %d -EIO, %d other, %d not completed\n",
	       r->ok, r->eio, r->other, NR_READS - r->done);
	reads_put(r);
	return r->other || r->done < NR_READS ? KSFT_FAIL : KSFT_PASS;
}

static int start_dev(void)
{
	int ret = ublk_ctrl_start_dev(ctrl(), getpid());

	printf("START_DEV: %d\n", ret);
	return ret;
}

static int expect_err(const char *what, int ret, int want)
{
	if (ret == want)
		return KSFT_PASS;
	fprintf(stderr, "%s: %d, expected %d\n", what, ret, want);
	return KSFT_FAIL;
}

/* START_DEV must fail with @want; if it went live, show what a read does */
static int start_dev_expect(int want)
{
	struct reads r = {};
	int ret = start_dev();

	if (!ret && !reads_submit(&r, -1)) {
		reads_reap(&r, 10);
		reads_result(&r);
	}
	return expect_err("START_DEV", ret, want);
}

static int test_stop_start(void)
{
	struct io_uring ring;
	int ret;

	if (add_dev(1, 0))
		return KSFT_FAIL;
	if (io_uring_queue_init(DEPTH, &ring, 0))
		return KSFT_FAIL;
	for (int tag = 0; tag < DEPTH; tag++)
		queue_io_cmd(&ring, UBLK_U_IO_FETCH_REQ, 0, tag, 0);
	io_uring_submit(&ring);

	/* device is ready but not started: state is UBLK_S_DEV_DEAD */
	ret = ublk_ctrl_stop_dev(ctrl());
	printf("STOP_DEV: %d, canceled fetch cmds: %d/%d\n", ret,
	       reap_aborts(&ring, DEPTH), DEPTH);

	/* STOP_DEV canceled the attached server: no start until it exits */
	ret = start_dev_expect(-EBUSY);
	io_uring_queue_exit(&ring);
	return ret;
}

static int test_partial_fetch(void)
{
	struct server b = { .q = 0, .first_tag = DEPTH - 1, .nr_tags = 1,
			    .delay_us = SERVE_DELAY_US };
	int ret;

	if (add_dev(1, 0) || fetch_and_die(0, 0, DEPTH - 1))
		return KSFT_FAIL;

	/* the last FETCH makes the queue ready and clears ->canceling */
	server_start(&b);

	ret = start_dev_expect(-EBUSY);

	cleanup();
	pthread_join(b.thread, NULL);
	return ret;
}

static int test_stop_restart(void)
{
	struct server s = { .q = 0, .nr_tags = DEPTH };
	struct reads r = {};
	int ret;

	if (add_dev_noopen(1, 0))
		return KSFT_FAIL;

	/* no server is attached, so there is nothing to cancel */
	ret = ublk_ctrl_stop_dev(ctrl());
	printf("STOP_DEV: %d\n", ret);
	if (open_cdev())
		return KSFT_FAIL;

	server_start(&s);
	if (start_dev()) {
		ret = KSFT_FAIL;
	} else if (reads_submit(&r, -1)) {
		ret = KSFT_FAIL;
	} else {
		reads_reap(&r, 10);
		ret = reads_result(&r);
		if (r.ok != NR_READS)
			ret = KSFT_FAIL;
	}

	cleanup();
	pthread_join(s.thread, NULL);
	return ret;
}

struct start_arg {
	pthread_barrier_t go;
	int ret, reads_ok;
};

static void *race_start_fn(void *data)
{
	struct start_arg *a = data;
	struct reads r = {};

	pthread_barrier_wait(&a->go);
	a->ret = ublk_ctrl_start_dev(ctrl(), getpid());
	a->reads_ok = 1;
	/*
	 * The disk may be gone already if STOP_DEV came right after, and
	 * reads may fail then: they only have to complete.
	 */
	if (!a->ret && !reads_submit(&r, -1)) {
		reads_reap(&r, 5);
		a->reads_ok = r.done == NR_READS;
		if (a->reads_ok)
			reads_put(&r);
		else
			reads_result(&r);
	}
	ctrl_put();
	return NULL;
}

static int test_race_start(void)
{
	int live = 0, ebusy = 0;

	open_tries = 4;
	for (int i = 0; i < RACE_LOOPS; i++) {
		struct server s = { .q = 0, .nr_tags = DEPTH };
		struct start_arg a = {};
		pthread_t t;

		if (add_dev(1, 0))
			return KSFT_FAIL;
		server_start(&s);
		pthread_barrier_init(&a.go, NULL, 2);
		pthread_create(&t, NULL, race_start_fn, &a);
		pthread_barrier_wait(&a.go);
		ublk_ctrl_stop_dev(ctrl());
		pthread_join(t, NULL);
		cleanup();
		pthread_join(s.thread, NULL);

		if (a.ret == 0)
			live++;
		else if (a.ret == -EBUSY)
			ebusy++;
		if ((a.ret && a.ret != -EBUSY) || !a.reads_ok) {
			fprintf(stderr, "loop %d: START_DEV %d, reads %s\n",
				i, a.ret, a.reads_ok ? "ok" : "failed");
			return KSFT_FAIL;
		}
	}
	printf("%d loops: START_DEV won %d, got -EBUSY %d\n", RACE_LOOPS,
	       live, ebusy);
	return KSFT_PASS;
}

static int test_race_fetch(void)
{
	int live = 0, ebusy = 0;

	for (int i = 0; i < RACE_LOOPS; i++) {
		/* vary who goes first: STOP_DEV, or the server's open */
		struct server s = { .q = 0, .nr_tags = DEPTH, .race = 1,
				    .race_delay_us = (i % 10) * 50 };
		struct reads r = {};
		int ret;

		if (add_dev_noopen(1, 0))
			return KSFT_FAIL;
		server_start(&s);
		pthread_barrier_wait(&s.go);
		ublk_ctrl_stop_dev(ctrl());
		pthread_barrier_wait(&s.fetched);

		ret = ublk_ctrl_start_dev(ctrl(), getpid());
		if (!ret) {
			/* nothing was taken, so every read has to succeed */
			live++;
			if (reads_submit(&r, -1))
				return KSFT_FAIL;
			reads_reap(&r, 5);
			if (r.ok != NR_READS) {
				fprintf(stderr, "loop %d: live, but ", i);
				reads_result(&r);
				return KSFT_FAIL;
			}
			reads_put(&r);
		} else if (ret == -EBUSY) {
			ebusy++;
		} else {
			fprintf(stderr, "loop %d: START_DEV %d\n", i, ret);
			return KSFT_FAIL;
		}
		cleanup();
		pthread_join(s.thread, NULL);
	}
	printf("%d loops: START_DEV worked %d, got -EBUSY %d\n", RACE_LOOPS,
	       live, ebusy);
	return KSFT_PASS;
}

static int test_race_async_fetch(void)
{
	io_cmd_sqe_flags = IOSQE_ASYNC;
	for (int i = 0; i < ASYNC_LOOPS; i++) {
		struct async_arg a;
		pthread_t t;

		if (add_dev(1, 0))
			return KSFT_FAIL;
		pthread_barrier_init(&a.go, NULL, 2);
		pthread_barrier_init(&a.stopped, NULL, 2);
		pthread_create(&t, NULL, race_async_fn, &a);
		pthread_barrier_wait(&a.go);
		/* vary where STOP_DEV lands relative to the FETCHes */
		usleep((i % 5) * 1000);
		ublk_ctrl_stop_dev(ctrl());
		pthread_barrier_wait(&a.stopped);
		pthread_join(t, NULL);
		cleanup();
	}
	printf("%d loops done\n", ASYNC_LOOPS);
	return KSFT_PASS;
}

/*
 * The stopped server is gone: reopen /dev/ublkcN, start a new server and
 * the device, and read from it.
 */
static int restart_server(void)
{
	struct server s = { .q = 0, .nr_tags = DEPTH };
	struct reads r = {};
	int ret;

	close_cdev();
	if (open_cdev())
		return KSFT_FAIL;
	server_start(&s);
	usleep(200000);
	if (s.failed) {
		fprintf(stderr, "new server: FETCH failed %d\n", s.failed);
		cleanup();
		pthread_join(s.thread, NULL);
		return KSFT_FAIL;
	}
	/* -EEXIST until the old disk is freed, e.g. after a udev probe */
	for (int i = 0; i < 100; i++) {
		ret = start_dev();
		if (ret != -EEXIST)
			break;
		usleep(50000);
	}
	if (ret || reads_submit(&r, -1)) {
		fprintf(stderr, "new server: START_DEV %d\n", ret);
		ret = KSFT_FAIL;
	} else {
		reads_reap(&r, 10);
		ret = reads_result(&r);
		if (r.ok != NR_READS)
			ret = KSFT_FAIL;
	}
	cleanup();
	pthread_join(s.thread, NULL);
	return ret;
}

/*
 * stop_attached: STOP_DEV while a server has the device open but has
 * fetched nothing stops that server too: its FETCH gets ABORT and
 * START_DEV -EBUSY. Once it is gone, a new server can start the device.
 */
static int test_stop_attached(void)
{
	struct server s = { .q = 0, .nr_tags = DEPTH };
	int ret;

	if (add_dev(1, 0))
		return KSFT_FAIL;
	ret = ublk_ctrl_stop_dev(ctrl());
	printf("STOP_DEV: %d\n", ret);

	server_start(&s);
	ret = server_wait_failed(&s);
	printf("FETCH after STOP_DEV: %d\n", ret);
	if (ret != UBLK_IO_RES_ABORT) {
		cleanup();
		pthread_join(s.thread, NULL);
		return KSFT_FAIL;
	}
	pthread_join(s.thread, NULL);
	if (expect_err("START_DEV", start_dev(), -EBUSY))
		return KSFT_FAIL;
	return restart_server();
}

/*
 * stop_live_restart: STOP_DEV on a live device; once its server is gone,
 * a new server has to fetch and start it again.
 */
static int test_stop_live_restart(void)
{
	struct server s = { .q = 0, .nr_tags = DEPTH };
	int ret;

	if (add_dev(1, 0))
		return KSFT_FAIL;
	server_start(&s);
	if (expect_err("START_DEV", start_dev(), 0))
		return KSFT_FAIL;
	ret = ublk_ctrl_stop_dev(ctrl());
	printf("STOP_DEV: %d\n", ret);
	pthread_join(s.thread, NULL);
	return restart_server();
}

/* first CPU which blk-mq maps to hw queue @q */
static int queue_cpu(int q)
{
	char path[96];
	FILE *f;
	int cpu = -1;

	snprintf(path, sizeof(path), "/sys/block/ublkb%d/mq/%d/cpu_list",
		 dev_id, q);
	for (int i = 0; i < 100 && !(f = fopen(path, "r")); i++)
		usleep(50000);
	if (!f)
		return -1;
	if (fscanf(f, "%d", &cpu) != 1)
		cpu = -1;
	fclose(f);
	return cpu;
}

static int test_recovery(void)
{
	struct server q1 = { .q = 1, .nr_tags = DEPTH };
	struct io_uring ring;
	struct reads r = {};
	int ret, cpu;

	if (add_dev(NR_QUEUES, UBLK_F_USER_RECOVERY))
		return KSFT_FAIL;

	/* the first server: fetch everything, start, then die */
	if (io_uring_queue_init(NR_QUEUES * DEPTH, &ring, 0))
		return KSFT_FAIL;
	for (int q = 0; q < NR_QUEUES; q++)
		for (int tag = 0; tag < DEPTH; tag++)
			queue_io_cmd(&ring, UBLK_U_IO_FETCH_REQ, q, tag, 0);
	io_uring_submit(&ring);
	if (start_dev())
		return KSFT_FAIL;
	cpu = queue_cpu(0);
	if (cpu < 0) {
		printf("no CPU maps to queue 0, can't aim the reads\n");
		return KSFT_SKIP;
	}

	io_uring_queue_exit(&ring);
	close_cdev();
	for (int i = 0; i < 100 && dev_state() != UBLK_S_DEV_QUIESCED; i++)
		usleep(50000);
	printf("server exited, state %d (QUIESCED is %d)\n", dev_state(),
	       UBLK_S_DEV_QUIESCED);

	for (int i = 0; i < 100; i++) {
		ret = ublk_ctrl_start_user_recovery(ctrl());
		if (ret != -EBUSY)
			break;
		usleep(50000);
	}
	printf("START_USER_RECOVERY: %d\n", ret);
	if (ret || open_cdev())
		return KSFT_FAIL;

	/* queue 0 gets ready, then its task dies before queue 1 is ready */
	if (fetch_and_die(0, 0, DEPTH))
		return KSFT_FAIL;

	printf("reads on cpu %d, which maps to queue 0\n", cpu);
	if (reads_submit(&r, cpu))
		return KSFT_FAIL;

	server_start(&q1);
	ret = ublk_ctrl_end_user_recovery(ctrl(), getpid());
	printf("END_USER_RECOVERY: %d\n", ret);
	ret = expect_err("END_USER_RECOVERY", ret, -EBUSY);

	/*
	 * Requests held back on queue 0 wait for STOP_DEV to fail them.
	 * Close the disk before DEL_DEV, which waits for its last reference.
	 */
	reads_reap(&r, 1);
	ublk_ctrl_stop_dev(ctrl());
	reads_reap(&r, 5);
	if (reads_result(&r))
		ret = KSFT_FAIL;
	cleanup();
	pthread_join(q1.thread, NULL);
	return ret;
}

static const struct {
	const char *name;
	int (*fn)(void);
} modes[] = {
	{ "stop_start",		test_stop_start },
	{ "partial_fetch",	test_partial_fetch },
	{ "recovery",		test_recovery },
	{ "stop_restart",	test_stop_restart },
	{ "race_start",		test_race_start },
	{ "race_fetch",		test_race_fetch },
	{ "race_async_fetch",	test_race_async_fetch },
	{ "stop_attached",	test_stop_attached },
	{ "stop_live_restart",	test_stop_live_restart },
};

int main(int argc, char **argv)
{
	int (*fn)(void) = NULL;
	int ret;

	for (int i = 0; argc == 2 && i < ARRAY_SIZE(modes); i++)
		if (!strcmp(argv[1], modes[i].name))
			fn = modes[i].fn;
	if (!fn) {
		fprintf(stderr, "usage: %s MODE, modes:", argv[0]);
		for (int i = 0; i < ARRAY_SIZE(modes); i++)
			fprintf(stderr, " %s", modes[i].name);
		fprintf(stderr, "\n");
		return KSFT_FAIL;
	}

	/* keep the output of a run that ends in an oops */
	setvbuf(stdout, NULL, _IOLBF, 0);

	if (access(CTRL_DEV, F_OK)) {
		perror(CTRL_DEV);
		return KSFT_SKIP;
	}
	printf("%s\n", argv[1]);
	ret = fn();
	cleanup();
	ctrl_put();
	return ret;
}
