/*
 * Test functionality of BPF filters for SO_REUSEPORT.  The tests below will use
 * a BPF program (both classic and extended) to read the first word from an
 * incoming packet (expected to be in network byte-order), calculate a modulus
 * of that number, and then dispatch the packet to the Nth socket using the
 * result.  These tests are run for each supported address family and protocol.
 * Additionally, a few edge cases in the implementation are tested.
 */

#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <linux/bpf.h>
#include <linux/filter.h>
#include <linux/unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/resource.h>
#include <unistd.h>

#include "../kselftest.h"

struct test_params {
	int recv_family;
	int send_family;
	int protocol;
	size_t recv_socks;
	uint16_t recv_port;
	uint16_t send_port_min;
};

static size_t sockaddr_size(void)
{
	return sizeof(struct sockaddr_storage);
}

static struct sockaddr *new_any_sockaddr(int family, uint16_t port)
{
	struct sockaddr_storage *addr;
	struct sockaddr_in *addr4;
	struct sockaddr_in6 *addr6;

	addr = malloc(sizeof(struct sockaddr_storage));
	memset(addr, 0, sizeof(struct sockaddr_storage));

	switch (family) {
	case AF_INET:
		addr4 = (struct sockaddr_in *)addr;
		addr4->sin_family = AF_INET;
		addr4->sin_addr.s_addr = htonl(INADDR_ANY);
		addr4->sin_port = htons(port);
		break;
	case AF_INET6:
		addr6 = (struct sockaddr_in6 *)addr;
		addr6->sin6_family = AF_INET6;
		addr6->sin6_addr = in6addr_any;
		addr6->sin6_port = htons(port);
		break;
	default:
		error(1, 0, "Unsupported family %d", family);
	}
	return (struct sockaddr *)addr;
}

static struct sockaddr *new_loopback_sockaddr(int family, uint16_t port)
{
	struct sockaddr *addr = new_any_sockaddr(family, port);
	struct sockaddr_in *addr4;
	struct sockaddr_in6 *addr6;

	switch (family) {
	case AF_INET:
		addr4 = (struct sockaddr_in *)addr;
		addr4->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		break;
	case AF_INET6:
		addr6 = (struct sockaddr_in6 *)addr;
		addr6->sin6_addr = in6addr_loopback;
		break;
	default:
		error(1, 0, "Unsupported family %d", family);
	}
	return addr;
}

static void attach_ebpf(int fd, uint16_t mod)
{
	static char bpf_log_buf[65536];
	static const char bpf_license[] = "GPL";

	int bpf_fd;
	const struct bpf_insn prog[] = {
		/* BPF_MOV64_REG(BPF_REG_6, BPF_REG_1) */
		{ BPF_ALU64 | BPF_MOV | BPF_X, BPF_REG_6, BPF_REG_1, 0, 0 },
		/* BPF_LD_ABS(BPF_W, 0) R0 = (uint32_t)skb[0] */
		{ BPF_LD | BPF_ABS | BPF_W, 0, 0, 0, 0 },
		/* BPF_ALU64_IMM(BPF_MOD, BPF_REG_0, mod) */
		{ BPF_ALU64 | BPF_MOD | BPF_K, BPF_REG_0, 0, 0, mod },
		/* BPF_EXIT_INSN() */
		{ BPF_JMP | BPF_EXIT, 0, 0, 0, 0 }
	};
	union bpf_attr attr;

	memset(&attr, 0, sizeof(attr));
	attr.prog_type = BPF_PROG_TYPE_SOCKET_FILTER;
	attr.insn_cnt = ARRAY_SIZE(prog);
	attr.insns = (unsigned long) &prog;
	attr.license = (unsigned long) &bpf_license;
	attr.log_buf = (unsigned long) &bpf_log_buf;
	attr.log_size = sizeof(bpf_log_buf);
	attr.log_level = 1;
	attr.kern_version = 0;

	bpf_fd = syscall(__NR_bpf, BPF_PROG_LOAD, &attr, sizeof(attr));
	if (bpf_fd < 0)
		error(1, errno, "ebpf error. log:\n%s\n", bpf_log_buf);

	if (setsockopt(fd, SOL_SOCKET, SO_ATTACH_REUSEPORT_EBPF, &bpf_fd,
			sizeof(bpf_fd)))
		error(1, errno, "failed to set SO_ATTACH_REUSEPORT_EBPF");

	close(bpf_fd);
}

static void attach_cbpf(int fd, uint16_t mod)
{
	struct sock_filter code[] = {
		/* A = (uint32_t)skb[0] */
		{ BPF_LD  | BPF_W | BPF_ABS, 0, 0, 0 },
		/* A = A % mod */
		{ BPF_ALU | BPF_MOD, 0, 0, mod },
		/* return A */
		{ BPF_RET | BPF_A, 0, 0, 0 },
	};
	struct sock_fprog p = {
		.len = ARRAY_SIZE(code),
		.filter = code,
	};

	if (setsockopt(fd, SOL_SOCKET, SO_ATTACH_REUSEPORT_CBPF, &p, sizeof(p)))
		error(1, errno, "failed to set SO_ATTACH_REUSEPORT_CBPF");
}

static void build_recv_group(const struct test_params p, int fd[], uint16_t mod,
			     void (*attach_bpf)(int, uint16_t))
{
	struct sockaddr * const addr =
		new_any_sockaddr(p.recv_family, p.recv_port);
	int i, opt;

	for (i = 0; i < p.recv_socks; ++i) {
		fd[i] = socket(p.recv_family, p.protocol, 0);
		if (fd[i] < 0)
			error(1, errno, "failed to create recv %d", i);

		opt = 1;
		if (setsockopt(fd[i], SOL_SOCKET, SO_REUSEPORT, &opt,
			       sizeof(opt)))
			error(1, errno, "failed to set SO_REUSEPORT on %d", i);

		if (i == 0)
			attach_bpf(fd[i], mod);

		if (bind(fd[i], addr, sockaddr_size()))
			error(1, errno, "failed to bind recv socket %d", i);

		if (p.protocol == SOCK_STREAM) {
			opt = 4;
			if (setsockopt(fd[i], SOL_TCP, TCP_FASTOPEN, &opt,
				       sizeof(opt)))
				error(1, errno,
				      "failed to set TCP_FASTOPEN on %d", i);
			if (listen(fd[i], p.recv_socks * 10))
				error(1, errno, "failed to listen on socket");
		}
	}
	free(addr);
}

static void send_from(struct test_params p, uint16_t sport, char *buf,
		      size_t len)
{
	struct sockaddr * const saddr = new_any_sockaddr(p.send_family, sport);
	struct sockaddr * const daddr =
		new_loopback_sockaddr(p.send_family, p.recv_port);
	const int fd = socket(p.send_family, p.protocol, 0), one = 1;

	if (fd < 0)
		error(1, errno, "failed to create send socket");

	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)))
		error(1, errno, "failed to set reuseaddr");

	if (bind(fd, saddr, sockaddr_size()))
		error(1, errno, "failed to bind send socket");

	if (sendto(fd, buf, len, MSG_FASTOPEN, daddr, sockaddr_size()) < 0)
		error(1, errno, "failed to send message");

	close(fd);
	free(saddr);
	free(daddr);
}

static void test_recv_order(const struct test_params p, int fd[], int mod)
{
	char recv_buf[8], send_buf[8];
	struct msghdr msg;
	struct iovec recv_io = { recv_buf, 8 };
	struct epoll_event ev;
	int epfd, conn, i, sport, expected;
	uint32_t data, ndata;

	epfd = epoll_create(1);
	if (epfd < 0)
		error(1, errno, "failed to create epoll");
	for (i = 0; i < p.recv_socks; ++i) {
		ev.events = EPOLLIN;
		ev.data.fd = fd[i];
		if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd[i], &ev))
			error(1, errno, "failed to register sock %d epoll", i);
	}

	memset(&msg, 0, sizeof(msg));
	msg.msg_iov = &recv_io;
	msg.msg_iovlen = 1;

	for (data = 0; data < p.recv_socks * 2; ++data) {
		sport = p.send_port_min + data;
		ndata = htonl(data);
		memcpy(send_buf, &ndata, sizeof(ndata));
		send_from(p, sport, send_buf, sizeof(ndata));

		i = epoll_wait(epfd, &ev, 1, -1);
		if (i < 0)
			error(1, errno, "epoll wait failed");

		if (p.protocol == SOCK_STREAM) {
			conn = accept(ev.data.fd, NULL, NULL);
			if (conn < 0)
				error(1, errno, "error accepting");
			i = recvmsg(conn, &msg, 0);
			close(conn);
		} else {
			i = recvmsg(ev.data.fd, &msg, 0);
		}
		if (i < 0)
			error(1, errno, "recvmsg error");
		if (i != sizeof(ndata))
			error(1, 0, "expected size %zd got %d",
			      sizeof(ndata), i);

		for (i = 0; i < p.recv_socks; ++i)
			if (ev.data.fd == fd[i])
				break;
		memcpy(&ndata, recv_buf, sizeof(ndata));
		fprintf(stderr, "Socket %d: %d\n", i, ntohl(ndata));

		expected = (sport % mod);
		if (i != expected)
			error(1, 0, "expected socket %d", expected);
	}
}

static void test_reuseport_ebpf(struct test_params p)
{
	int i, fd[p.recv_socks];

	fprintf(stderr, "Testing EBPF mod %zd...\n", p.recv_socks);
	build_recv_group(p, fd, p.recv_socks, attach_ebpf);
	test_recv_order(p, fd, p.recv_socks);

	p.send_port_min += p.recv_socks * 2;
	fprintf(stderr, "Reprograming, testing mod %zd...\n", p.recv_socks / 2);
	attach_ebpf(fd[0], p.recv_socks / 2);
	test_recv_order(p, fd, p.recv_socks / 2);

	for (i = 0; i < p.recv_socks; ++i)
		close(fd[i]);
}

static void test_reuseport_cbpf(struct test_params p)
{
	int i, fd[p.recv_socks];

	fprintf(stderr, "Testing CBPF mod %zd...\n", p.recv_socks);
	build_recv_group(p, fd, p.recv_socks, attach_cbpf);
	test_recv_order(p, fd, p.recv_socks);

	p.send_port_min += p.recv_socks * 2;
	fprintf(stderr, "Reprograming, testing mod %zd...\n", p.recv_socks / 2);
	attach_cbpf(fd[0], p.recv_socks / 2);
	test_recv_order(p, fd, p.recv_socks / 2);

	for (i = 0; i < p.recv_socks; ++i)
		close(fd[i]);
}

static void test_extra_filter(const struct test_params p)
{
	struct sockaddr * const addr =
		new_any_sockaddr(p.recv_family, p.recv_port);
	int fd1, fd2, opt;

	fprintf(stderr, "Testing too many filters...\n");
	fd1 = socket(p.recv_family, p.protocol, 0);
	if (fd1 < 0)
		error(1, errno, "failed to create socket 1");
	fd2 = socket(p.recv_family, p.protocol, 0);
	if (fd2 < 0)
		error(1, errno, "failed to create socket 2");

	opt = 1;
	if (setsockopt(fd1, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)))
		error(1, errno, "failed to set SO_REUSEPORT on socket 1");
	if (setsockopt(fd2, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)))
		error(1, errno, "failed to set SO_REUSEPORT on socket 2");

	attach_ebpf(fd1, 10);
	attach_ebpf(fd2, 10);

	if (bind(fd1, addr, sockaddr_size()))
		error(1, errno, "failed to bind recv socket 1");

	if (!bind(fd2, addr, sockaddr_size()) && errno != EADDRINUSE)
		error(1, errno, "bind socket 2 should fail with EADDRINUSE");

	free(addr);
}

static void test_filter_no_reuseport(const struct test_params p)
{
	struct sockaddr * const addr =
		new_any_sockaddr(p.recv_family, p.recv_port);
	const char bpf_license[] = "GPL";
	struct bpf_insn ecode[] = {
		{ BPF_ALU64 | BPF_MOV | BPF_K, BPF_REG_0, 0, 0, 10 },
		{ BPF_JMP | BPF_EXIT, 0, 0, 0, 0 }
	};
	struct sock_filter ccode[] = {{ BPF_RET | BPF_A, 0, 0, 0 }};
	union bpf_attr eprog;
	struct sock_fprog cprog;
	int fd, bpf_fd;

	fprintf(stderr, "Testing filters on non-SO_REUSEPORT socket...\n");

	memset(&eprog, 0, sizeof(eprog));
	eprog.prog_type = BPF_PROG_TYPE_SOCKET_FILTER;
	eprog.insn_cnt = ARRAY_SIZE(ecode);
	eprog.insns = (unsigned long) &ecode;
	eprog.license = (unsigned long) &bpf_license;
	eprog.kern_version = 0;

	memset(&cprog, 0, sizeof(cprog));
	cprog.len = ARRAY_SIZE(ccode);
	cprog.filter = ccode;


	bpf_fd = syscall(__NR_bpf, BPF_PROG_LOAD, &eprog, sizeof(eprog));
	if (bpf_fd < 0)
		error(1, errno, "ebpf error");
	fd = socket(p.recv_family, p.protocol, 0);
	if (fd < 0)
		error(1, errno, "failed to create socket 1");

	if (bind(fd, addr, sockaddr_size()))
		error(1, errno, "failed to bind recv socket 1");

	errno = 0;
	if (!setsockopt(fd, SOL_SOCKET, SO_ATTACH_REUSEPORT_EBPF, &bpf_fd,
			sizeof(bpf_fd)) || errno != EINVAL)
		error(1, errno, "setsockopt should have returned EINVAL");

	errno = 0;
	if (!setsockopt(fd, SOL_SOCKET, SO_ATTACH_REUSEPORT_CBPF, &cprog,
		       sizeof(cprog)) || errno != EINVAL)
		error(1, errno, "setsockopt should have returned EINVAL");

	free(addr);
}

static void test_filter_without_bind(void)
{
	int fd1, fd2, opt = 1;

	fprintf(stderr, "Testing filter add without bind...\n");
	fd1 = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd1 < 0)
		error(1, errno, "failed to create socket 1");
	fd2 = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd2 < 0)
		error(1, errno, "failed to create socket 2");
	if (setsockopt(fd1, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)))
		error(1, errno, "failed to set SO_REUSEPORT on socket 1");
	if (setsockopt(fd2, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)))
		error(1, errno, "failed to set SO_REUSEPORT on socket 2");

	attach_ebpf(fd1, 10);
	attach_cbpf(fd2, 10);

	close(fd1);
	close(fd2);
}

void enable_fastopen(void)
{
	int fd = open("/proc/sys/net/ipv4/tcp_fastopen", 0);
	int rw_mask = 3;  /* bit 1: client side; bit-2 server side */
	int val, size;
	char buf[16];

	if (fd < 0)
		error(1, errno, "Unable to open tcp_fastopen sysctl");
	if (read(fd, buf, sizeof(buf)) <= 0)
		error(1, errno, "Unable to read tcp_fastopen sysctl");
	val = atoi(buf);
	close(fd);

	if ((val & rw_mask) != rw_mask) {
		fd = open("/proc/sys/net/ipv4/tcp_fastopen", O_RDWR);
		if (fd < 0)
			error(1, errno,
			      "Unable to open tcp_fastopen sysctl for writing");
		val |= rw_mask;
		size = snprintf(buf, 16, "%d", val);
		if (write(fd, buf, size) <= 0)
			error(1, errno, "Unable to write tcp_fastopen sysctl");
		close(fd);
	}
}

static struct rlimit rlim_old;

static  __attribute__((constructor)) void main_ctor(void)
{
	getrlimit(RLIMIT_MEMLOCK, &rlim_old);

	if (rlim_old.rlim_cur != RLIM_INFINITY) {
		struct rlimit rlim_new;

		rlim_new.rlim_cur = rlim_old.rlim_cur + (1UL << 20);
		rlim_new.rlim_max = rlim_old.rlim_max + (1UL << 20);
		setrlimit(RLIMIT_MEMLOCK, &rlim_new);
	}
}

static __attribute__((destructor)) void main_dtor(void)
{
	setrlimit(RLIMIT_MEMLOCK, &rlim_old);
}

int main(void)
{
	fprintf(stderr, "---- IPv4 UDP ----\n");
	/* NOTE: UDP socket lookups traverse a different code path when there
	 * are > 10 sockets in a group.  Run the bpf test through both paths.
	 */
	test_reuseport_ebpf((struct test_params) {
		.recv_family = AF_INET,
		.send_family = AF_INET,
		.protocol = SOCK_DGRAM,
		.recv_socks = 10,
		.recv_port = 8000,
		.send_port_min = 9000});
	test_reuseport_ebpf((struct test_params) {
		.recv_family = AF_INET,
		.send_family = AF_INET,
		.protocol = SOCK_DGRAM,
		.recv_socks = 20,
		.recv_port = 8000,
		.send_port_min = 9000});
	test_reuseport_cbpf((struct test_params) {
		.recv_family = AF_INET,
		.send_family = AF_INET,
		.protocol = SOCK_DGRAM,
		.recv_socks = 10,
		.recv_port = 8001,
		.send_port_min = 9020});
	test_reuseport_cbpf((struct test_params) {
		.recv_family = AF_INET,
		.send_family = AF_INET,
		.protocol = SOCK_DGRAM,
		.recv_socks = 20,
		.recv_port = 8001,
		.send_port_min = 9020});
	test_extra_filter((struct test_params) {
		.recv_family = AF_INET,
		.protocol = SOCK_DGRAM,
		.recv_port = 8002});
	test_filter_no_reuseport((struct test_params) {
		.recv_family = AF_INET,
		.protocol = SOCK_DGRAM,
		.recv_port = 8008});

	fprintf(stderr, "---- IPv6 UDP ----\n");
	test_reuseport_ebpf((struct test_params) {
		.recv_family = AF_INET6,
		.send_family = AF_INET6,
		.protocol = SOCK_DGRAM,
		.recv_socks = 10,
		.recv_port = 8003,
		.send_port_min = 9040});
	test_reuseport_ebpf((struct test_params) {
		.recv_family = AF_INET6,
		.send_family = AF_INET6,
		.protocol = SOCK_DGRAM,
		.recv_socks = 20,
		.recv_port = 8003,
		.send_port_min = 9040});
	test_reuseport_cbpf((struct test_params) {
		.recv_family = AF_INET6,
		.send_family = AF_INET6,
		.protocol = SOCK_DGRAM,
		.recv_socks = 10,
		.recv_port = 8004,
		.send_port_min = 9060});
	test_reuseport_cbpf((struct test_params) {
		.recv_family = AF_INET6,
		.send_family = AF_INET6,
		.protocol = SOCK_DGRAM,
		.recv_socks = 20,
		.recv_port = 8004,
		.send_port_min = 9060});
	test_extra_filter((struct test_params) {
		.recv_family = AF_INET6,
		.protocol = SOCK_DGRAM,
		.recv_port = 8005});
	test_filter_no_reuseport((struct test_params) {
		.recv_family = AF_INET6,
		.protocol = SOCK_DGRAM,
		.recv_port = 8009});

	fprintf(stderr, "---- IPv6 UDP w/ mapped IPv4 ----\n");
	test_reuseport_ebpf((struct test_params) {
		.recv_family = AF_INET6,
		.send_family = AF_INET,
		.protocol = SOCK_DGRAM,
		.recv_socks = 20,
		.recv_port = 8006,
		.send_port_min = 9080});
	test_reuseport_ebpf((struct test_params) {
		.recv_family = AF_INET6,
		.send_family = AF_INET,
		.protocol = SOCK_DGRAM,
		.recv_socks = 10,
		.recv_port = 8006,
		.send_port_min = 9080});
	test_reuseport_cbpf((struct test_params) {
		.recv_family = AF_INET6,
		.send_family = AF_INET,
		.protocol = SOCK_DGRAM,
		.recv_socks = 10,
		.recv_port = 8007,
		.send_port_min = 9100});
	test_reuseport_cbpf((struct test_params) {
		.recv_family = AF_INET6,
		.send_family = AF_INET,
		.protocol = SOCK_DGRAM,
		.recv_socks = 20,
		.recv_port = 8007,
		.send_port_min = 9100});

	/* TCP fastopen is required for the TCP tests */
	enable_fastopen();
	fprintf(stderr, "---- IPv4 TCP ----\n");
	test_reuseport_ebpf((struct test_params) {
		.recv_family = AF_INET,
		.send_family = AF_INET,
		.protocol = SOCK_STREAM,
		.recv_socks = 10,
		.recv_port = 8008,
		.send_port_min = 9120});
	test_reuseport_cbpf((struct test_params) {
		.recv_family = AF_INET,
		.send_family = AF_INET,
		.protocol = SOCK_STREAM,
		.recv_socks = 10,
		.recv_port = 8009,
		.send_port_min = 9160});
	test_extra_filter((struct test_params) {
		.recv_family = AF_INET,
		.protocol = SOCK_STREAM,
		.recv_port = 8010});
	test_filter_no_reuseport((struct test_params) {
		.recv_family = AF_INET,
		.protocol = SOCK_STREAM,
		.recv_port = 8011});

	fprintf(stderr, "---- IPv6 TCP ----\n");
	test_reuseport_ebpf((struct test_params) {
		.recv_family = AF_INET6,
		.send_family = AF_INET6,
		.protocol = SOCK_STREAM,
		.recv_socks = 10,
		.recv_port = 8012,
		.send_port_min = 9200});
	test_reuseport_cbpf((struct test_params) {
		.recv_family = AF_INET6,
		.send_family = AF_INET6,
		.protocol = SOCK_STREAM,
		.recv_socks = 10,
		.recv_port = 8013,
		.send_port_min = 9240});
	test_extra_filter((struct test_params) {
		.recv_family = AF_INET6,
		.protocol = SOCK_STREAM,
		.recv_port = 8014});
	test_filter_no_reuseport((struct test_params) {
		.recv_family = AF_INET6,
		.protocol = SOCK_STREAM,
		.recv_port = 8015});

	fprintf(stderr, "---- IPv6 TCP w/ mapped IPv4 ----\n");
	test_reuseport_ebpf((struct test_params) {
		.recv_family = AF_INET6,
		.send_family = AF_INET,
		.protocol = SOCK_STREAM,
		.recv_socks = 10,
		.recv_port = 8016,
		.send_port_min = 9320});
	test_reuseport_cbpf((struct test_params) {
		.recv_family = AF_INET6,
		.send_family = AF_INET,
		.protocol = SOCK_STREAM,
		.recv_socks = 10,
		.recv_port = 8017,
		.send_port_min = 9360});

	test_filter_without_bind();

	fprintf(stderr, "SUCCESS\n");
	return 0;
}
