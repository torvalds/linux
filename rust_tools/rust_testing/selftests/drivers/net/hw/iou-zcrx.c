// SPDX-License-Identifier: GPL-2.0
#include <assert.h>
#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
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

static char *payload;
static void *area_ptr;
static void *ring_ptr;
static size_t ring_size;
static struct io_uring_zcrx_rq rq_ring;
static unsigned long area_token;
static int connfd;
static bool stop;
static size_t received;

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

	ring_size = rq_entries * sizeof(struct io_uring_zcrx_rqe);
	/* add space for the header (head/tail/etc.) */
	ring_size += page_size;
	return ALIGN_UP(ring_size, page_size);
}

static void setup_zcrx(struct io_uring *ring)
{
	unsigned int ifindex;
	unsigned int rq_entries = 4096;
	int ret;

	ifindex = if_nametoindex(cfg_ifname);
	if (!ifindex)
		error(1, 0, "bad interface name: %s", cfg_ifname);

	area_ptr = mmap(NULL,
			AREA_SIZE,
			PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE,
			0,
			0);
	if (area_ptr == MAP_FAILED)
		error(1, 0, "mmap(): zero copy area");

	ring_size = get_refill_ring_size(rq_entries);
	ring_ptr = mmap(NULL,
			ring_size,
			PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE,
			0,
			0);

	struct io_uring_region_desc region_reg = {
		.size = ring_size,
		.user_addr = (__u64)(unsigned long)ring_ptr,
		.flags = IORING_MEM_REGION_TYPE_USER,
	};

	struct io_uring_zcrx_area_reg area_reg = {
		.addr = (__u64)(unsigned long)area_ptr,
		.len = AREA_SIZE,
		.flags = 0,
	};

	struct io_uring_zcrx_ifq_reg reg = {
		.if_idx = ifindex,
		.if_rxq = cfg_queue_id,
		.rq_entries = rq_entries,
		.area_ptr = (__u64)(unsigned long)&area_reg,
		.region_ptr = (__u64)(unsigned long)&region_reg,
	};

	ret = io_uring_register_ifq(ring, &reg);
	if (ret)
		error(1, 0, "io_uring_register_ifq(): %d", ret);

	rq_ring.khead = (unsigned int *)((char *)ring_ptr + reg.offsets.head);
	rq_ring.ktail = (unsigned int *)((char *)ring_ptr + reg.offsets.tail);
	rq_ring.rqes = (struct io_uring_zcrx_rqe *)((char *)ring_ptr + reg.offsets.rqes);
	rq_ring.rq_tail = 0;
	rq_ring.ring_entries = reg.rq_entries;

	area_token = area_reg.rq_area_token;
}

static void add_accept(struct io_uring *ring, int sockfd)
{
	struct io_uring_sqe *sqe;

	sqe = io_uring_get_sqe(ring);

	io_uring_prep_accept(sqe, sockfd, NULL, NULL, 0);
	sqe->user_data = 1;
}

static void add_recvzc(struct io_uring *ring, int sockfd)
{
	struct io_uring_sqe *sqe;

	sqe = io_uring_get_sqe(ring);

	io_uring_prep_rw(IORING_OP_RECV_ZC, sqe, sockfd, NULL, 0, 0);
	sqe->ioprio |= IORING_RECV_MULTISHOT;
	sqe->user_data = 2;
}

static void add_recvzc_oneshot(struct io_uring *ring, int sockfd, size_t len)
{
	struct io_uring_sqe *sqe;

	sqe = io_uring_get_sqe(ring);

	io_uring_prep_rw(IORING_OP_RECV_ZC, sqe, sockfd, NULL, len, 0);
	sqe->ioprio |= IORING_RECV_MULTISHOT;
	sqe->user_data = 2;
}

static void process_accept(struct io_uring *ring, struct io_uring_cqe *cqe)
{
	if (cqe->res < 0)
		error(1, 0, "accept()");
	if (connfd)
		error(1, 0, "Unexpected second connection");

	connfd = cqe->res;
	if (cfg_oneshot)
		add_recvzc_oneshot(ring, connfd, page_size);
	else
		add_recvzc(ring, connfd);
}

static void process_recvzc(struct io_uring *ring, struct io_uring_cqe *cqe)
{
	unsigned rq_mask = rq_ring.ring_entries - 1;
	struct io_uring_zcrx_cqe *rcqe;
	struct io_uring_zcrx_rqe *rqe;
	struct io_uring_sqe *sqe;
	uint64_t mask;
	char *data;
	ssize_t n;
	int i;

	if (cqe->res == 0 && cqe->flags == 0 && cfg_oneshot_recvs == 0) {
		stop = true;
		return;
	}

	if (cqe->res < 0)
		error(1, 0, "recvzc(): %d", cqe->res);

	if (cfg_oneshot) {
		if (cqe->res == 0 && cqe->flags == 0 && cfg_oneshot_recvs) {
			add_recvzc_oneshot(ring, connfd, page_size);
			cfg_oneshot_recvs--;
		}
	} else if (!(cqe->flags & IORING_CQE_F_MORE)) {
		add_recvzc(ring, connfd);
	}

	rcqe = (struct io_uring_zcrx_cqe *)(cqe + 1);

	n = cqe->res;
	mask = (1ULL << IORING_ZCRX_AREA_SHIFT) - 1;
	data = (char *)area_ptr + (rcqe->off & mask);

	for (i = 0; i < n; i++) {
		if (*(data + i) != payload[(received + i)])
			error(1, 0, "payload mismatch at %d", i);
	}
	received += n;

	rqe = &rq_ring.rqes[(rq_ring.rq_tail & rq_mask)];
	rqe->off = (rcqe->off & ~IORING_ZCRX_AREA_MASK) | area_token;
	rqe->len = cqe->res;
	io_uring_smp_store_release(rq_ring.ktail, ++rq_ring.rq_tail);
}

static void server_loop(struct io_uring *ring)
{
	struct io_uring_cqe *cqe;
	unsigned int count = 0;
	unsigned int head;
	int i, ret;

	io_uring_submit_and_wait(ring, 1);

	io_uring_for_each_cqe(ring, head, cqe) {
		if (cqe->user_data == 1)
			process_accept(ring, cqe);
		else if (cqe->user_data == 2)
			process_recvzc(ring, cqe);
		else
			error(1, 0, "unknown cqe");
		count++;
	}
	io_uring_cq_advance(ring, count);
}

static void run_server(void)
{
	unsigned int flags = 0;
	struct io_uring ring;
	int fd, enable, ret;
	uint64_t tstop;

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

	if (listen(fd, 1024) < 0)
		error(1, 0, "listen()");

	flags |= IORING_SETUP_COOP_TASKRUN;
	flags |= IORING_SETUP_SINGLE_ISSUER;
	flags |= IORING_SETUP_DEFER_TASKRUN;
	flags |= IORING_SETUP_SUBMIT_ALL;
	flags |= IORING_SETUP_CQE32;

	io_uring_queue_init(512, &ring, flags);

	setup_zcrx(&ring);

	add_accept(&ring, fd);

	tstop = gettimeofday_ms() + 5000;
	while (!stop && gettimeofday_ms() < tstop)
		server_loop(&ring);

	if (!stop)
		error(1, 0, "test failed\n");
}

static void run_client(void)
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
}

static void usage(const char *filepath)
{
	error(1, 0, "Usage: %s (-4|-6) (-s|-c) -h<server_ip> -p<port> "
		    "-l<payload_size> -i<ifname> -q<rxq_id>", filepath);
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

	while ((c = getopt(argc, argv, "sch:p:l:i:q:o:z:")) != -1) {
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
