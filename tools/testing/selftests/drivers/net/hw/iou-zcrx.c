// SPDX-License-Identifier: GPL-2.0
#include <assert.h>
#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <linux/mman.h>
#include <linux/errqueue.h>
#include <linux/if_packet.h>
#include <linux/ipv6.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>

#include <liburing.h>
#include <ynl.h>
#include "netdev-user.h"

#define SKIP_CODE	42

struct t_io_uring_zcrx_ifq_reg {
	__u32	if_idx;
	__u32	if_rxq;
	__u32	rq_entries;
	__u32	flags;

	__u64	area_ptr; /* pointer to struct io_uring_zcrx_area_reg */
	__u64	region_ptr; /* struct io_uring_region_desc * */

	struct io_uring_zcrx_offsets offsets;
	__u32	zcrx_id;
	__u32	rx_buf_len;
	__u64	__resv[3];
};

static long page_size;
#define AREA_SIZE (8192 * page_size)
#define SEND_SIZE (512 * 4096)
#define min(a, b) \
	({ \
		typeof(a) _a = (a); \
		typeof(b) _b = (b); \
		_a < _b ? _a : _b; \
	})
#define min_t(t, a, b) \
	({ \
		t _ta = (a); \
		t _tb = (b); \
		min(_ta, _tb); \
	})

#define ALIGN_UP(v, align) (((v) + (align) - 1) & ~((align) - 1))

static int cfg_server;
static int cfg_client;
static int cfg_port = 8000;
static int cfg_payload_len;
static const char *cfg_ifname;
static int cfg_queue_id = -1;
static bool cfg_oneshot;
static int cfg_oneshot_recvs;
static int cfg_send_size = SEND_SIZE;
static struct sockaddr_in6 cfg_addr;
static unsigned int cfg_rx_buf_len;
static bool cfg_dry_run;
static int cfg_num_threads = 1;

static char *payload;

#define MAX_CONNS_PER_THREAD	64

struct thread_ctx {
	struct io_uring		ring;
	void			*area_ptr;
	void			*ring_ptr;
	size_t			ring_size;
	struct io_uring_zcrx_rq	rq_ring;
	unsigned long		area_token;
	int			queue_id;
	int			napi_id;
	pthread_barrier_t	*setup_done;
	pthread_barrier_t	*dispatch_done;

	int			connfds[MAX_CONNS_PER_THREAD];
	size_t			received[MAX_CONNS_PER_THREAD];
	int			oneshot_recvs[MAX_CONNS_PER_THREAD];
	int			nr_conns;
};

static unsigned long gettimeofday_ms(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
}

static int parse_address(const char *str, int port, struct sockaddr_in6 *sin6)
{
	int ret;

	sin6->sin6_family = AF_INET6;
	sin6->sin6_port = htons(port);

	ret = inet_pton(sin6->sin6_family, str, &sin6->sin6_addr);
	if (ret != 1) {
		/* fallback to plain IPv4 */
		ret = inet_pton(AF_INET, str, &sin6->sin6_addr.s6_addr32[3]);
		if (ret != 1)
			return -1;

		/* add ::ffff prefix */
		sin6->sin6_addr.s6_addr32[0] = 0;
		sin6->sin6_addr.s6_addr32[1] = 0;
		sin6->sin6_addr.s6_addr16[4] = 0;
		sin6->sin6_addr.s6_addr16[5] = 0xffff;
	}

	return 0;
}

static inline size_t get_refill_ring_size(unsigned int rq_entries)
{
	size_t size;

	size = rq_entries * sizeof(struct io_uring_zcrx_rqe);
	/* add space for the header (head/tail/etc.) */
	size += page_size;
	return ALIGN_UP(size, page_size);
}

static void setup_zcrx(struct thread_ctx *ctx)
{
	unsigned int rq_entries = AREA_SIZE / page_size;
	unsigned int ifindex;
	int ret;

	ifindex = if_nametoindex(cfg_ifname);
	if (!ifindex)
		error(1, 0, "bad interface name: %s", cfg_ifname);

	if (cfg_rx_buf_len && cfg_rx_buf_len != page_size) {
		ctx->area_ptr = mmap(NULL,
				     AREA_SIZE,
				     PROT_READ | PROT_WRITE,
				     MAP_ANONYMOUS | MAP_PRIVATE |
				     MAP_HUGETLB | MAP_HUGE_2MB,
				     -1,
				     0);
		if (ctx->area_ptr == MAP_FAILED) {
			printf("Can't allocate huge pages\n");
			exit(SKIP_CODE);
		}
	} else {
		ctx->area_ptr = mmap(NULL,
				     AREA_SIZE,
				     PROT_READ | PROT_WRITE,
				     MAP_ANONYMOUS | MAP_PRIVATE,
				     0,
				     0);
		if (ctx->area_ptr == MAP_FAILED)
			error(1, 0, "mmap(): zero copy area");
	}

	ctx->ring_size = get_refill_ring_size(rq_entries);
	ctx->ring_ptr = mmap(NULL,
			     ctx->ring_size,
			     PROT_READ | PROT_WRITE,
			     MAP_ANONYMOUS | MAP_PRIVATE,
			     0,
			     0);

	struct io_uring_region_desc region_reg = {
		.size = ctx->ring_size,
		.user_addr = (__u64)(unsigned long)ctx->ring_ptr,
		.flags = IORING_MEM_REGION_TYPE_USER,
	};

	struct io_uring_zcrx_area_reg area_reg = {
		.addr = (__u64)(unsigned long)ctx->area_ptr,
		.len = AREA_SIZE,
		.flags = 0,
	};

	struct t_io_uring_zcrx_ifq_reg reg = {
		.if_idx = ifindex,
		.if_rxq = ctx->queue_id,
		.rq_entries = rq_entries,
		.area_ptr = (__u64)(unsigned long)&area_reg,
		.region_ptr = (__u64)(unsigned long)&region_reg,
		.rx_buf_len = cfg_rx_buf_len,
	};

	ret = io_uring_register_ifq(&ctx->ring, (void *)&reg);
	if (cfg_rx_buf_len && (ret == -EINVAL || ret == -EOPNOTSUPP ||
			       ret == -ERANGE)) {
		printf("Large chunks are not supported %i\n", ret);
		exit(SKIP_CODE);
	} else if (ret) {
		error(1, 0, "io_uring_register_ifq(): %d", ret);
	}

	ctx->rq_ring.khead = (unsigned int *)((char *)ctx->ring_ptr + reg.offsets.head);
	ctx->rq_ring.ktail = (unsigned int *)((char *)ctx->ring_ptr + reg.offsets.tail);
	ctx->rq_ring.rqes = (struct io_uring_zcrx_rqe *)((char *)ctx->ring_ptr + reg.offsets.rqes);
	ctx->rq_ring.rq_tail = 0;
	ctx->rq_ring.ring_entries = reg.rq_entries;

	ctx->area_token = area_reg.rq_area_token;
}

static void add_recvzc(struct thread_ctx *ctx, int conn_idx)
{
	struct io_uring_sqe *sqe;

	sqe = io_uring_get_sqe(&ctx->ring);

	io_uring_prep_rw(IORING_OP_RECV_ZC, sqe, ctx->connfds[conn_idx],
			 NULL, 0, 0);
	sqe->ioprio |= IORING_RECV_MULTISHOT;
	sqe->user_data = conn_idx;
}

static void add_recvzc_oneshot(struct thread_ctx *ctx, int conn_idx, size_t len)
{
	struct io_uring_sqe *sqe;

	sqe = io_uring_get_sqe(&ctx->ring);

	io_uring_prep_rw(IORING_OP_RECV_ZC, sqe, ctx->connfds[conn_idx],
			 NULL, len, 0);
	sqe->ioprio |= IORING_RECV_MULTISHOT;
	sqe->user_data = conn_idx;
}

static void process_recvzc(struct thread_ctx *ctx, struct io_uring_cqe *cqe,
			   int conn_idx)
{
	unsigned int rq_mask = ctx->rq_ring.ring_entries - 1;
	struct io_uring_zcrx_cqe *rcqe;
	struct io_uring_zcrx_rqe *rqe;
	uint64_t mask;
	char *data;
	ssize_t n;
	int i;

	if (cqe->res == 0 && cqe->flags == 0 &&
	    ctx->oneshot_recvs[conn_idx] == 0) {
		ctx->nr_conns--;
		return;
	}

	if (cqe->res < 0)
		error(1, 0, "recvzc(): %d", cqe->res);

	if (cfg_oneshot) {
		if (cqe->res == 0 && cqe->flags == 0 &&
		    ctx->oneshot_recvs[conn_idx]) {
			add_recvzc_oneshot(ctx, conn_idx, page_size);
			ctx->oneshot_recvs[conn_idx]--;
			return;
		}
	} else if (!(cqe->flags & IORING_CQE_F_MORE)) {
		add_recvzc(ctx, conn_idx);
	}

	rcqe = (struct io_uring_zcrx_cqe *)(cqe + 1);

	n = cqe->res;
	mask = (1ULL << IORING_ZCRX_AREA_SHIFT) - 1;
	data = (char *)ctx->area_ptr + (rcqe->off & mask);

	for (i = 0; i < n; i++) {
		if (*(data + i) != payload[(ctx->received[conn_idx] + i)])
			error(1, 0, "payload mismatch at %d", i);
	}
	ctx->received[conn_idx] += n;

	rqe = &ctx->rq_ring.rqes[(ctx->rq_ring.rq_tail & rq_mask)];
	rqe->off = (rcqe->off & ~IORING_ZCRX_AREA_MASK) | ctx->area_token;
	rqe->len = cqe->res;
	io_uring_smp_store_release(ctx->rq_ring.ktail, ++ctx->rq_ring.rq_tail);
}

static void server_loop(struct thread_ctx *ctx)
{
	struct io_uring_cqe *cqe;
	unsigned int count = 0;
	unsigned int head;

	io_uring_submit_and_wait(&ctx->ring, 1);

	io_uring_for_each_cqe(&ctx->ring, head, cqe) {
		process_recvzc(ctx, cqe, cqe->user_data);
		count++;
	}
	io_uring_cq_advance(&ctx->ring, count);
}

static void *server_worker(void *arg)
{
	struct io_uring_params params = { };
	struct thread_ctx *ctx = arg;
	uint64_t tstop;
	int nr_conns;
	int i;

	params.flags |= IORING_SETUP_COOP_TASKRUN;
	params.flags |= IORING_SETUP_SINGLE_ISSUER;
	params.flags |= IORING_SETUP_DEFER_TASKRUN;
	params.flags |= IORING_SETUP_SUBMIT_ALL;
	params.flags |= IORING_SETUP_CQE32;
	params.flags |= IORING_SETUP_CQSIZE;
	params.cq_entries = AREA_SIZE / page_size;

	io_uring_queue_init_params(512, &ctx->ring, &params);
	setup_zcrx(ctx);

	if (cfg_dry_run)
		return NULL;

	pthread_barrier_wait(ctx->setup_done);
	pthread_barrier_wait(ctx->dispatch_done);

	nr_conns = ctx->nr_conns;

	for (i = 0; i < ctx->nr_conns; i++) {
		if (cfg_oneshot) {
			ctx->oneshot_recvs[i] = cfg_oneshot_recvs;
			add_recvzc_oneshot(ctx, i, page_size);
		} else {
			add_recvzc(ctx, i);
		}
	}

	tstop = gettimeofday_ms() + 5000;
	while (ctx->nr_conns > 0 && gettimeofday_ms() < tstop)
		server_loop(ctx);

	if (ctx->nr_conns != 0)
		error(1, 0, "test failed: %d connections incomplete",
		      ctx->nr_conns);

	for (i = 0; i < nr_conns; i++) {
		if (cfg_oneshot) {
			if (!ctx->received[i])
				error(1, 0, "connection %d received no data", i);
		} else if (ctx->received[i] != (size_t)cfg_send_size) {
			error(1, 0, "connection %d received %zu of %d bytes",
			      i, ctx->received[i], cfg_send_size);
		}
	}

	return NULL;
}

static int query_napi_id(unsigned int ifindex, int queue_id)
{
	struct netdev_queue_get_req *req;
	struct netdev_queue_get_rsp *rsp;
	struct ynl_error yerr;
	struct ynl_sock *ys;
	int napi_id;

	ys = ynl_sock_create(&ynl_netdev_family, &yerr);
	if (!ys)
		error(1, 0, "ynl_sock_create: %s", yerr.msg);

	req = netdev_queue_get_req_alloc();
	netdev_queue_get_req_set_ifindex(req, ifindex);
	netdev_queue_get_req_set_type(req, NETDEV_QUEUE_TYPE_RX);
	netdev_queue_get_req_set_id(req, queue_id);

	rsp = netdev_queue_get(ys, req);
	if (!rsp)
		error(1, 0, "netdev_queue_get(q=%d): %s", queue_id,
		      ys->err.msg);
	if (!rsp->_present.napi_id)
		error(1, 0, "netdev_queue_get(q=%d): napi_id not present",
		      queue_id);

	napi_id = rsp->napi_id;

	netdev_queue_get_req_free(req);
	netdev_queue_get_rsp_free(rsp);
	ynl_sock_destroy(ys);

	return napi_id;
}

static int find_thread_by_conn(struct thread_ctx *ctxs, int connfd)
{
	socklen_t len = sizeof(int);
	int napi_id, i;

	if (getsockopt(connfd, SOL_SOCKET, SO_INCOMING_NAPI_ID, &napi_id, &len))
		error(1, errno, "getsockopt(SO_INCOMING_NAPI_ID)");

	for (i = 0; i < cfg_num_threads; i++) {
		if (ctxs[i].napi_id == napi_id)
			return i;
	}

	error(1, 0, "unknown NAPI ID: %d", napi_id);
	return -1;
}

static void run_server(void)
{
	pthread_barrier_t setup_done, dispatch_done;
	int total_conns, accepted = 0, connfd;
	struct thread_ctx *ctxs;
	int fd, ret, enable, i;
	unsigned int ifindex;
	pthread_t *threads;

	ctxs = calloc(cfg_num_threads, sizeof(*ctxs));
	threads = calloc(cfg_num_threads, sizeof(*threads));
	if (!ctxs || !threads)
		error(1, 0, "calloc()");

	fd = socket(AF_INET6, SOCK_STREAM, 0);
	if (fd == -1)
		error(1, 0, "socket()");

	enable = 1;
	ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
	if (ret < 0)
		error(1, 0, "setsockopt(SO_REUSEADDR)");

	ret = bind(fd, (struct sockaddr *)&cfg_addr, sizeof(cfg_addr));
	if (ret < 0)
		error(1, 0, "bind()");

	pthread_barrier_init(&setup_done, NULL, cfg_num_threads + 1);
	pthread_barrier_init(&dispatch_done, NULL, cfg_num_threads + 1);

	for (i = 0; i < cfg_num_threads; i++) {
		ctxs[i].queue_id = cfg_queue_id + i;
		ctxs[i].setup_done = &setup_done;
		ctxs[i].dispatch_done = &dispatch_done;
	}

	for (i = 0; i < cfg_num_threads; i++) {
		ret = pthread_create(&threads[i], NULL,
				     server_worker, &ctxs[i]);
		if (ret)
			error(1, ret, "pthread_create()");
	}

	if (cfg_dry_run)
		goto join;

	pthread_barrier_wait(&setup_done);

	if (listen(fd, 1024) < 0)
		error(1, 0, "listen()");

	if (cfg_num_threads > 1) {
		ifindex = if_nametoindex(cfg_ifname);
		if (!ifindex)
			error(1, 0, "bad interface name: %s", cfg_ifname);
		for (i = 0; i < cfg_num_threads; i++)
			ctxs[i].napi_id = query_napi_id(ifindex,
							ctxs[i].queue_id);
	}

	total_conns = cfg_num_threads * cfg_num_threads;

	while (accepted < total_conns) {
		int idx = 0;

		connfd = accept(fd, NULL, NULL);
		if (connfd < 0)
			error(1, errno, "accept()");

		if (cfg_num_threads > 1)
			idx = find_thread_by_conn(ctxs, connfd);

		if (ctxs[idx].nr_conns >= MAX_CONNS_PER_THREAD)
			error(1, 0, "worker %d connection overflow", idx);
		ctxs[idx].connfds[ctxs[idx].nr_conns++] = connfd;
		accepted++;
	}

	pthread_barrier_wait(&dispatch_done);

join:
	for (i = 0; i < cfg_num_threads; i++)
		pthread_join(threads[i], NULL);

	pthread_barrier_destroy(&setup_done);
	pthread_barrier_destroy(&dispatch_done);
	close(fd);
	free(threads);
	free(ctxs);
}

static void *client_worker(void *arg)
{
	ssize_t to_send = cfg_send_size;
	ssize_t sent = 0;
	ssize_t chunk, res;
	int fd;

	fd = socket(AF_INET6, SOCK_STREAM, 0);
	if (fd == -1)
		error(1, 0, "socket()");

	if (connect(fd, (struct sockaddr *)&cfg_addr, sizeof(cfg_addr)))
		error(1, 0, "connect()");

	while (to_send) {
		void *src = &payload[sent];

		chunk = min_t(ssize_t, cfg_payload_len, to_send);
		res = send(fd, src, chunk, 0);
		if (res < 0)
			error(1, 0, "send(): %zd", sent);
		sent += res;
		to_send -= res;
	}

	close(fd);
	return NULL;
}

static void run_client(void)
{
	int total_conns = cfg_num_threads * cfg_num_threads;
	pthread_t *threads;
	int i, ret;

	threads = calloc(total_conns, sizeof(*threads));
	if (!threads)
		error(1, 0, "calloc()");

	for (i = 0; i < total_conns; i++) {
		ret = pthread_create(&threads[i], NULL, client_worker, NULL);
		if (ret)
			error(1, ret, "pthread_create()");
	}

	for (i = 0; i < total_conns; i++)
		pthread_join(threads[i], NULL);

	free(threads);
}

static void usage(const char *filepath)
{
	error(1, 0, "Usage: %s (-4|-6) (-s|-c) -h<server_ip> -p<port> "
		    "-l<payload_size> -i<ifname> -q<rxq_id> -t<num_threads>",
		    filepath);
}

static void parse_opts(int argc, char **argv)
{
	const int max_payload_len = SEND_SIZE -
				    sizeof(struct ipv6hdr) -
				    sizeof(struct tcphdr) -
				    40 /* max tcp options */;
	struct sockaddr_in6 *addr6 = (void *) &cfg_addr;
	char *addr = NULL;
	int ret;
	int c;

	if (argc <= 1)
		usage(argv[0]);
	cfg_payload_len = max_payload_len;

	while ((c = getopt(argc, argv, "sch:p:l:i:q:o:z:x:dt:")) != -1) {
		switch (c) {
		case 's':
			if (cfg_client)
				error(1, 0, "Pass one of -s or -c");
			cfg_server = 1;
			break;
		case 'c':
			if (cfg_server)
				error(1, 0, "Pass one of -s or -c");
			cfg_client = 1;
			break;
		case 'h':
			addr = optarg;
			break;
		case 'p':
			cfg_port = strtoul(optarg, NULL, 0);
			break;
		case 'l':
			cfg_payload_len = strtoul(optarg, NULL, 0);
			break;
		case 'i':
			cfg_ifname = optarg;
			break;
		case 'q':
			cfg_queue_id = strtoul(optarg, NULL, 0);
			break;
		case 'o': {
			cfg_oneshot = true;
			cfg_oneshot_recvs = strtoul(optarg, NULL, 0);
			break;
		}
		case 'z':
			cfg_send_size = strtoul(optarg, NULL, 0);
			break;
		case 'x':
			cfg_rx_buf_len = page_size * strtoul(optarg, NULL, 0);
			break;
		case 'd':
			cfg_dry_run = true;
			break;
		case 't':
			cfg_num_threads = strtoul(optarg, NULL, 0);
			break;
		}
	}

	if (cfg_server && addr)
		error(1, 0, "Receiver cannot have -h specified");

	memset(addr6, 0, sizeof(*addr6));
	addr6->sin6_family = AF_INET6;
	addr6->sin6_port = htons(cfg_port);
	addr6->sin6_addr = in6addr_any;
	if (addr) {
		ret = parse_address(addr, cfg_port, addr6);
		if (ret)
			error(1, 0, "receiver address parse error: %s", addr);
	}

	if (cfg_payload_len > max_payload_len)
		error(1, 0, "-l: payload exceeds max (%d)", max_payload_len);
}

int main(int argc, char **argv)
{
	const char *cfg_test = argv[argc - 1];
	int i;

	page_size = sysconf(_SC_PAGESIZE);
	if (page_size < 0)
		return 1;

	if (posix_memalign((void **)&payload, page_size, SEND_SIZE))
		return 1;

	parse_opts(argc, argv);

	for (i = 0; i < SEND_SIZE; i++)
		payload[i] = 'a' + (i % 26);

	if (cfg_server)
		run_server();
	else if (cfg_client)
		run_client();

	return 0;
}
