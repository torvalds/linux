// SPDX-License-Identifier: GPL-2.0
/*
 * ipsec.c - Check xfrm on veth inside a net-ns.
 * Copyright (c) 2018 Dmitry Safonov
 */

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <asm/types.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/limits.h>
#include <linux/netlink.h>
#include <linux/random.h>
#include <linux/rtnetlink.h>
#include <linux/veth.h>
#include <linux/xfrm.h>
#include <netinet/in.h>
#include <net/if.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../kselftest.h"

#define printk(fmt, ...)						\
	ksft_print_msg("%d[%u] " fmt "\n", getpid(), __LINE__, ##__VA_ARGS__)

#define pr_err(fmt, ...)	printk(fmt ": %m", ##__VA_ARGS__)

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#define BUILD_BUG_ON(condition) ((void)sizeof(char[1 - 2*!!(condition)]))

#define IPV4_STR_SZ	16	/* xxx.xxx.xxx.xxx is longest + \0 */
#define MAX_PAYLOAD	2048
#define XFRM_ALGO_KEY_BUF_SIZE	512
#define MAX_PROCESSES	(1 << 14) /* /16 mask divided by /30 subnets */
#define INADDR_A	((in_addr_t) 0x0a000000) /* 10.0.0.0 */
#define INADDR_B	((in_addr_t) 0xc0a80000) /* 192.168.0.0 */

/* /30 mask for one veth connection */
#define PREFIX_LEN	30
#define child_ip(nr)	(4*nr + 1)
#define grchild_ip(nr)	(4*nr + 2)

#define VETH_FMT	"ktst-%d"
#define VETH_LEN	12

static int nsfd_parent	= -1;
static int nsfd_childa	= -1;
static int nsfd_childb	= -1;
static long page_size;

/*
 * ksft_cnt is static in kselftest, so isn't shared with children.
 * We have to send a test result back to parent and count there.
 * results_fd is a pipe with test feedback from children.
 */
static int results_fd[2];

const unsigned int ping_delay_nsec	= 50 * 1000 * 1000;
const unsigned int ping_timeout		= 300;
const unsigned int ping_count		= 100;
const unsigned int ping_success		= 80;

static void randomize_buffer(void *buf, size_t buflen)
{
	int *p = (int *)buf;
	size_t words = buflen / sizeof(int);
	size_t leftover = buflen % sizeof(int);

	if (!buflen)
		return;

	while (words--)
		*p++ = rand();

	if (leftover) {
		int tmp = rand();

		memcpy(buf + buflen - leftover, &tmp, leftover);
	}

	return;
}

static int unshare_open(void)
{
	const char *netns_path = "/proc/self/ns/net";
	int fd;

	if (unshare(CLONE_NEWNET) != 0) {
		pr_err("unshare()");
		return -1;
	}

	fd = open(netns_path, O_RDONLY);
	if (fd <= 0) {
		pr_err("open(%s)", netns_path);
		return -1;
	}

	return fd;
}

static int switch_ns(int fd)
{
	if (setns(fd, CLONE_NEWNET)) {
		pr_err("setns()");
		return -1;
	}
	return 0;
}

/*
 * Running the test inside a new parent net namespace to bother less
 * about cleanup on error-path.
 */
static int init_namespaces(void)
{
	nsfd_parent = unshare_open();
	if (nsfd_parent <= 0)
		return -1;

	nsfd_childa = unshare_open();
	if (nsfd_childa <= 0)
		return -1;

	if (switch_ns(nsfd_parent))
		return -1;

	nsfd_childb = unshare_open();
	if (nsfd_childb <= 0)
		return -1;

	if (switch_ns(nsfd_parent))
		return -1;
	return 0;
}

static int netlink_sock(int *sock, uint32_t *seq_nr, int proto)
{
	if (*sock > 0) {
		seq_nr++;
		return 0;
	}

	*sock = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, proto);
	if (*sock <= 0) {
		pr_err("socket(AF_NETLINK)");
		return -1;
	}

	randomize_buffer(seq_nr, sizeof(*seq_nr));

	return 0;
}

static inline struct rtattr *rtattr_hdr(struct nlmsghdr *nh)
{
	return (struct rtattr *)((char *)(nh) + RTA_ALIGN((nh)->nlmsg_len));
}

static int rtattr_pack(struct nlmsghdr *nh, size_t req_sz,
		unsigned short rta_type, const void *payload, size_t size)
{
	/* NLMSG_ALIGNTO == RTA_ALIGNTO, nlmsg_len already aligned */
	struct rtattr *attr = rtattr_hdr(nh);
	size_t nl_size = RTA_ALIGN(nh->nlmsg_len) + RTA_LENGTH(size);

	if (req_sz < nl_size) {
		printk("req buf is too small: %zu < %zu", req_sz, nl_size);
		return -1;
	}
	nh->nlmsg_len = nl_size;

	attr->rta_len = RTA_LENGTH(size);
	attr->rta_type = rta_type;
	memcpy(RTA_DATA(attr), payload, size);

	return 0;
}

static struct rtattr *_rtattr_begin(struct nlmsghdr *nh, size_t req_sz,
		unsigned short rta_type, const void *payload, size_t size)
{
	struct rtattr *ret = rtattr_hdr(nh);

	if (rtattr_pack(nh, req_sz, rta_type, payload, size))
		return 0;

	return ret;
}

static inline struct rtattr *rtattr_begin(struct nlmsghdr *nh, size_t req_sz,
		unsigned short rta_type)
{
	return _rtattr_begin(nh, req_sz, rta_type, 0, 0);
}

static inline void rtattr_end(struct nlmsghdr *nh, struct rtattr *attr)
{
	char *nlmsg_end = (char *)nh + nh->nlmsg_len;

	attr->rta_len = nlmsg_end - (char *)attr;
}

static int veth_pack_peerb(struct nlmsghdr *nh, size_t req_sz,
		const char *peer, int ns)
{
	struct ifinfomsg pi;
	struct rtattr *peer_attr;

	memset(&pi, 0, sizeof(pi));
	pi.ifi_family	= AF_UNSPEC;
	pi.ifi_change	= 0xFFFFFFFF;

	peer_attr = _rtattr_begin(nh, req_sz, VETH_INFO_PEER, &pi, sizeof(pi));
	if (!peer_attr)
		return -1;

	if (rtattr_pack(nh, req_sz, IFLA_IFNAME, peer, strlen(peer)))
		return -1;

	if (rtattr_pack(nh, req_sz, IFLA_NET_NS_FD, &ns, sizeof(ns)))
		return -1;

	rtattr_end(nh, peer_attr);

	return 0;
}

static int netlink_check_answer(int sock)
{
	struct nlmsgerror {
		struct nlmsghdr hdr;
		int error;
		struct nlmsghdr orig_msg;
	} answer;

	if (recv(sock, &answer, sizeof(answer), 0) < 0) {
		pr_err("recv()");
		return -1;
	} else if (answer.hdr.nlmsg_type != NLMSG_ERROR) {
		printk("expected NLMSG_ERROR, got %d", (int)answer.hdr.nlmsg_type);
		return -1;
	} else if (answer.error) {
		printk("NLMSG_ERROR: %d: %s",
			answer.error, strerror(-answer.error));
		return answer.error;
	}

	return 0;
}

static int veth_add(int sock, uint32_t seq, const char *peera, int ns_a,
		const char *peerb, int ns_b)
{
	uint16_t flags = NLM_F_REQUEST | NLM_F_ACK | NLM_F_EXCL | NLM_F_CREATE;
	struct {
		struct nlmsghdr		nh;
		struct ifinfomsg	info;
		char			attrbuf[MAX_PAYLOAD];
	} req;
	const char veth_type[] = "veth";
	struct rtattr *link_info, *info_data;

	memset(&req, 0, sizeof(req));
	req.nh.nlmsg_len	= NLMSG_LENGTH(sizeof(req.info));
	req.nh.nlmsg_type	= RTM_NEWLINK;
	req.nh.nlmsg_flags	= flags;
	req.nh.nlmsg_seq	= seq;
	req.info.ifi_family	= AF_UNSPEC;
	req.info.ifi_change	= 0xFFFFFFFF;

	if (rtattr_pack(&req.nh, sizeof(req), IFLA_IFNAME, peera, strlen(peera)))
		return -1;

	if (rtattr_pack(&req.nh, sizeof(req), IFLA_NET_NS_FD, &ns_a, sizeof(ns_a)))
		return -1;

	link_info = rtattr_begin(&req.nh, sizeof(req), IFLA_LINKINFO);
	if (!link_info)
		return -1;

	if (rtattr_pack(&req.nh, sizeof(req), IFLA_INFO_KIND, veth_type, sizeof(veth_type)))
		return -1;

	info_data = rtattr_begin(&req.nh, sizeof(req), IFLA_INFO_DATA);
	if (!info_data)
		return -1;

	if (veth_pack_peerb(&req.nh, sizeof(req), peerb, ns_b))
		return -1;

	rtattr_end(&req.nh, info_data);
	rtattr_end(&req.nh, link_info);

	if (send(sock, &req, req.nh.nlmsg_len, 0) < 0) {
		pr_err("send()");
		return -1;
	}
	return netlink_check_answer(sock);
}

static int ip4_addr_set(int sock, uint32_t seq, const char *intf,
		struct in_addr addr, uint8_t prefix)
{
	uint16_t flags = NLM_F_REQUEST | NLM_F_ACK | NLM_F_EXCL | NLM_F_CREATE;
	struct {
		struct nlmsghdr		nh;
		struct ifaddrmsg	info;
		char			attrbuf[MAX_PAYLOAD];
	} req;

	memset(&req, 0, sizeof(req));
	req.nh.nlmsg_len	= NLMSG_LENGTH(sizeof(req.info));
	req.nh.nlmsg_type	= RTM_NEWADDR;
	req.nh.nlmsg_flags	= flags;
	req.nh.nlmsg_seq	= seq;
	req.info.ifa_family	= AF_INET;
	req.info.ifa_prefixlen	= prefix;
	req.info.ifa_index	= if_nametoindex(intf);

#ifdef DEBUG
	{
		char addr_str[IPV4_STR_SZ] = {};

		strncpy(addr_str, inet_ntoa(addr), IPV4_STR_SZ - 1);

		printk("ip addr set %s", addr_str);
	}
#endif

	if (rtattr_pack(&req.nh, sizeof(req), IFA_LOCAL, &addr, sizeof(addr)))
		return -1;

	if (rtattr_pack(&req.nh, sizeof(req), IFA_ADDRESS, &addr, sizeof(addr)))
		return -1;

	if (send(sock, &req, req.nh.nlmsg_len, 0) < 0) {
		pr_err("send()");
		return -1;
	}
	return netlink_check_answer(sock);
}

static int link_set_up(int sock, uint32_t seq, const char *intf)
{
	struct {
		struct nlmsghdr		nh;
		struct ifinfomsg	info;
		char			attrbuf[MAX_PAYLOAD];
	} req;

	memset(&req, 0, sizeof(req));
	req.nh.nlmsg_len	= NLMSG_LENGTH(sizeof(req.info));
	req.nh.nlmsg_type	= RTM_NEWLINK;
	req.nh.nlmsg_flags	= NLM_F_REQUEST | NLM_F_ACK;
	req.nh.nlmsg_seq	= seq;
	req.info.ifi_family	= AF_UNSPEC;
	req.info.ifi_change	= 0xFFFFFFFF;
	req.info.ifi_index	= if_nametoindex(intf);
	req.info.ifi_flags	= IFF_UP;
	req.info.ifi_change	= IFF_UP;

	if (send(sock, &req, req.nh.nlmsg_len, 0) < 0) {
		pr_err("send()");
		return -1;
	}
	return netlink_check_answer(sock);
}

static int ip4_route_set(int sock, uint32_t seq, const char *intf,
		struct in_addr src, struct in_addr dst)
{
	struct {
		struct nlmsghdr	nh;
		struct rtmsg	rt;
		char		attrbuf[MAX_PAYLOAD];
	} req;
	unsigned int index = if_nametoindex(intf);

	memset(&req, 0, sizeof(req));
	req.nh.nlmsg_len	= NLMSG_LENGTH(sizeof(req.rt));
	req.nh.nlmsg_type	= RTM_NEWROUTE;
	req.nh.nlmsg_flags	= NLM_F_REQUEST | NLM_F_ACK | NLM_F_CREATE;
	req.nh.nlmsg_seq	= seq;
	req.rt.rtm_family	= AF_INET;
	req.rt.rtm_dst_len	= 32;
	req.rt.rtm_table	= RT_TABLE_MAIN;
	req.rt.rtm_protocol	= RTPROT_BOOT;
	req.rt.rtm_scope	= RT_SCOPE_LINK;
	req.rt.rtm_type		= RTN_UNICAST;

	if (rtattr_pack(&req.nh, sizeof(req), RTA_DST, &dst, sizeof(dst)))
		return -1;

	if (rtattr_pack(&req.nh, sizeof(req), RTA_PREFSRC, &src, sizeof(src)))
		return -1;

	if (rtattr_pack(&req.nh, sizeof(req), RTA_OIF, &index, sizeof(index)))
		return -1;

	if (send(sock, &req, req.nh.nlmsg_len, 0) < 0) {
		pr_err("send()");
		return -1;
	}

	return netlink_check_answer(sock);
}

static int tunnel_set_route(int route_sock, uint32_t *route_seq, char *veth,
		struct in_addr tunsrc, struct in_addr tundst)
{
	if (ip4_addr_set(route_sock, (*route_seq)++, "lo",
			tunsrc, PREFIX_LEN)) {
		printk("Failed to set ipv4 addr");
		return -1;
	}

	if (ip4_route_set(route_sock, (*route_seq)++, veth, tunsrc, tundst)) {
		printk("Failed to set ipv4 route");
		return -1;
	}

	return 0;
}

static int init_child(int nsfd, char *veth, unsigned int src, unsigned int dst)
{
	struct in_addr intsrc = inet_makeaddr(INADDR_B, src);
	struct in_addr tunsrc = inet_makeaddr(INADDR_A, src);
	struct in_addr tundst = inet_makeaddr(INADDR_A, dst);
	int route_sock = -1, ret = -1;
	uint32_t route_seq;

	if (switch_ns(nsfd))
		return -1;

	if (netlink_sock(&route_sock, &route_seq, NETLINK_ROUTE)) {
		printk("Failed to open netlink route socket in child");
		return -1;
	}

	if (ip4_addr_set(route_sock, route_seq++, veth, intsrc, PREFIX_LEN)) {
		printk("Failed to set ipv4 addr");
		goto err;
	}

	if (link_set_up(route_sock, route_seq++, veth)) {
		printk("Failed to bring up %s", veth);
		goto err;
	}

	if (tunnel_set_route(route_sock, &route_seq, veth, tunsrc, tundst)) {
		printk("Failed to add tunnel route on %s", veth);
		goto err;
	}
	ret = 0;

err:
	close(route_sock);
	return ret;
}

#define ALGO_LEN	64
enum desc_type {
	CREATE_TUNNEL	= 0,
	ALLOCATE_SPI,
	MONITOR_ACQUIRE,
	EXPIRE_STATE,
	EXPIRE_POLICY,
	SPDINFO_ATTRS,
};
const char *desc_name[] = {
	"create tunnel",
	"alloc spi",
	"monitor acquire",
	"expire state",
	"expire policy",
	"spdinfo attributes",
	""
};
struct xfrm_desc {
	enum desc_type	type;
	uint8_t		proto;
	char		a_algo[ALGO_LEN];
	char		e_algo[ALGO_LEN];
	char		c_algo[ALGO_LEN];
	char		ae_algo[ALGO_LEN];
	unsigned int	icv_len;
	/* unsigned key_len; */
};

enum msg_type {
	MSG_ACK		= 0,
	MSG_EXIT,
	MSG_PING,
	MSG_XFRM_PREPARE,
	MSG_XFRM_ADD,
	MSG_XFRM_DEL,
	MSG_XFRM_CLEANUP,
};

struct test_desc {
	enum msg_type type;
	union {
		struct {
			in_addr_t reply_ip;
			unsigned int port;
		} ping;
		struct xfrm_desc xfrm_desc;
	} body;
};

struct test_result {
	struct xfrm_desc desc;
	unsigned int res;
};

static void write_test_result(unsigned int res, struct xfrm_desc *d)
{
	struct test_result tr = {};
	ssize_t ret;

	tr.desc = *d;
	tr.res = res;

	ret = write(results_fd[1], &tr, sizeof(tr));
	if (ret != sizeof(tr))
		pr_err("Failed to write the result in pipe %zd", ret);
}

static void write_msg(int fd, struct test_desc *msg, bool exit_of_fail)
{
	ssize_t bytes = write(fd, msg, sizeof(*msg));

	/* Make sure that write/read is atomic to a pipe */
	BUILD_BUG_ON(sizeof(struct test_desc) > PIPE_BUF);

	if (bytes < 0) {
		pr_err("write()");
		if (exit_of_fail)
			exit(KSFT_FAIL);
	}
	if (bytes != sizeof(*msg)) {
		pr_err("sent part of the message %zd/%zu", bytes, sizeof(*msg));
		if (exit_of_fail)
			exit(KSFT_FAIL);
	}
}

static void read_msg(int fd, struct test_desc *msg, bool exit_of_fail)
{
	ssize_t bytes = read(fd, msg, sizeof(*msg));

	if (bytes < 0) {
		pr_err("read()");
		if (exit_of_fail)
			exit(KSFT_FAIL);
	}
	if (bytes != sizeof(*msg)) {
		pr_err("got incomplete message %zd/%zu", bytes, sizeof(*msg));
		if (exit_of_fail)
			exit(KSFT_FAIL);
	}
}

static int udp_ping_init(struct in_addr listen_ip, unsigned int u_timeout,
		unsigned int *server_port, int sock[2])
{
	struct sockaddr_in server;
	struct timeval t = { .tv_sec = 0, .tv_usec = u_timeout };
	socklen_t s_len = sizeof(server);

	sock[0] = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock[0] < 0) {
		pr_err("socket()");
		return -1;
	}

	server.sin_family	= AF_INET;
	server.sin_port		= 0;
	memcpy(&server.sin_addr.s_addr, &listen_ip, sizeof(struct in_addr));

	if (bind(sock[0], (struct sockaddr *)&server, s_len)) {
		pr_err("bind()");
		goto err_close_server;
	}

	if (getsockname(sock[0], (struct sockaddr *)&server, &s_len)) {
		pr_err("getsockname()");
		goto err_close_server;
	}

	*server_port = ntohs(server.sin_port);

	if (setsockopt(sock[0], SOL_SOCKET, SO_RCVTIMEO, (const char *)&t, sizeof t)) {
		pr_err("setsockopt()");
		goto err_close_server;
	}

	sock[1] = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock[1] < 0) {
		pr_err("socket()");
		goto err_close_server;
	}

	return 0;

err_close_server:
	close(sock[0]);
	return -1;
}

static int udp_ping_send(int sock[2], in_addr_t dest_ip, unsigned int port,
		char *buf, size_t buf_len)
{
	struct sockaddr_in server;
	const struct sockaddr *dest_addr = (struct sockaddr *)&server;
	char *sock_buf[buf_len];
	ssize_t r_bytes, s_bytes;

	server.sin_family	= AF_INET;
	server.sin_port		= htons(port);
	server.sin_addr.s_addr	= dest_ip;

	s_bytes = sendto(sock[1], buf, buf_len, 0, dest_addr, sizeof(server));
	if (s_bytes < 0) {
		pr_err("sendto()");
		return -1;
	} else if (s_bytes != buf_len) {
		printk("send part of the message: %zd/%zu", s_bytes, sizeof(server));
		return -1;
	}

	r_bytes = recv(sock[0], sock_buf, buf_len, 0);
	if (r_bytes < 0) {
		if (errno != EAGAIN)
			pr_err("recv()");
		return -1;
	} else if (r_bytes == 0) { /* EOF */
		printk("EOF on reply to ping");
		return -1;
	} else if (r_bytes != buf_len || memcmp(buf, sock_buf, buf_len)) {
		printk("ping reply packet is corrupted %zd/%zu", r_bytes, buf_len);
		return -1;
	}

	return 0;
}

static int udp_ping_reply(int sock[2], in_addr_t dest_ip, unsigned int port,
		char *buf, size_t buf_len)
{
	struct sockaddr_in server;
	const struct sockaddr *dest_addr = (struct sockaddr *)&server;
	char *sock_buf[buf_len];
	ssize_t r_bytes, s_bytes;

	server.sin_family	= AF_INET;
	server.sin_port		= htons(port);
	server.sin_addr.s_addr	= dest_ip;

	r_bytes = recv(sock[0], sock_buf, buf_len, 0);
	if (r_bytes < 0) {
		if (errno != EAGAIN)
			pr_err("recv()");
		return -1;
	}
	if (r_bytes == 0) { /* EOF */
		printk("EOF on reply to ping");
		return -1;
	}
	if (r_bytes != buf_len || memcmp(buf, sock_buf, buf_len)) {
		printk("ping reply packet is corrupted %zd/%zu", r_bytes, buf_len);
		return -1;
	}

	s_bytes = sendto(sock[1], buf, buf_len, 0, dest_addr, sizeof(server));
	if (s_bytes < 0) {
		pr_err("sendto()");
		return -1;
	} else if (s_bytes != buf_len) {
		printk("send part of the message: %zd/%zu", s_bytes, sizeof(server));
		return -1;
	}

	return 0;
}

typedef int (*ping_f)(int sock[2], in_addr_t dest_ip, unsigned int port,
		char *buf, size_t buf_len);
static int do_ping(int cmd_fd, char *buf, size_t buf_len, struct in_addr from,
		bool init_side, int d_port, in_addr_t to, ping_f func)
{
	struct test_desc msg;
	unsigned int s_port, i, ping_succeeded = 0;
	int ping_sock[2];
	char to_str[IPV4_STR_SZ] = {}, from_str[IPV4_STR_SZ] = {};

	if (udp_ping_init(from, ping_timeout, &s_port, ping_sock)) {
		printk("Failed to init ping");
		return -1;
	}

	memset(&msg, 0, sizeof(msg));
	msg.type		= MSG_PING;
	msg.body.ping.port	= s_port;
	memcpy(&msg.body.ping.reply_ip, &from, sizeof(from));

	write_msg(cmd_fd, &msg, 0);
	if (init_side) {
		/* The other end sends ip to ping */
		read_msg(cmd_fd, &msg, 0);
		if (msg.type != MSG_PING)
			return -1;
		to = msg.body.ping.reply_ip;
		d_port = msg.body.ping.port;
	}

	for (i = 0; i < ping_count ; i++) {
		struct timespec sleep_time = {
			.tv_sec = 0,
			.tv_nsec = ping_delay_nsec,
		};

		ping_succeeded += !func(ping_sock, to, d_port, buf, page_size);
		nanosleep(&sleep_time, 0);
	}

	close(ping_sock[0]);
	close(ping_sock[1]);

	strncpy(to_str, inet_ntoa(*(struct in_addr *)&to), IPV4_STR_SZ - 1);
	strncpy(from_str, inet_ntoa(from), IPV4_STR_SZ - 1);

	if (ping_succeeded < ping_success) {
		printk("ping (%s) %s->%s failed %u/%u times",
			init_side ? "send" : "reply", from_str, to_str,
			ping_count - ping_succeeded, ping_count);
		return -1;
	}

#ifdef DEBUG
	printk("ping (%s) %s->%s succeeded %u/%u times",
		init_side ? "send" : "reply", from_str, to_str,
		ping_succeeded, ping_count);
#endif

	return 0;
}

static int xfrm_fill_key(char *name, char *buf,
		size_t buf_len, unsigned int *key_len)
{
	/* TODO: use set/map instead */
	if (strncmp(name, "digest_null", ALGO_LEN) == 0)
		*key_len = 0;
	else if (strncmp(name, "ecb(cipher_null)", ALGO_LEN) == 0)
		*key_len = 0;
	else if (strncmp(name, "cbc(des)", ALGO_LEN) == 0)
		*key_len = 64;
	else if (strncmp(name, "hmac(md5)", ALGO_LEN) == 0)
		*key_len = 128;
	else if (strncmp(name, "cmac(aes)", ALGO_LEN) == 0)
		*key_len = 128;
	else if (strncmp(name, "xcbc(aes)", ALGO_LEN) == 0)
		*key_len = 128;
	else if (strncmp(name, "cbc(cast5)", ALGO_LEN) == 0)
		*key_len = 128;
	else if (strncmp(name, "cbc(serpent)", ALGO_LEN) == 0)
		*key_len = 128;
	else if (strncmp(name, "hmac(sha1)", ALGO_LEN) == 0)
		*key_len = 160;
	else if (strncmp(name, "hmac(rmd160)", ALGO_LEN) == 0)
		*key_len = 160;
	else if (strncmp(name, "cbc(des3_ede)", ALGO_LEN) == 0)
		*key_len = 192;
	else if (strncmp(name, "hmac(sha256)", ALGO_LEN) == 0)
		*key_len = 256;
	else if (strncmp(name, "cbc(aes)", ALGO_LEN) == 0)
		*key_len = 256;
	else if (strncmp(name, "cbc(camellia)", ALGO_LEN) == 0)
		*key_len = 256;
	else if (strncmp(name, "cbc(twofish)", ALGO_LEN) == 0)
		*key_len = 256;
	else if (strncmp(name, "rfc3686(ctr(aes))", ALGO_LEN) == 0)
		*key_len = 288;
	else if (strncmp(name, "hmac(sha384)", ALGO_LEN) == 0)
		*key_len = 384;
	else if (strncmp(name, "cbc(blowfish)", ALGO_LEN) == 0)
		*key_len = 448;
	else if (strncmp(name, "hmac(sha512)", ALGO_LEN) == 0)
		*key_len = 512;
	else if (strncmp(name, "rfc4106(gcm(aes))-128", ALGO_LEN) == 0)
		*key_len = 160;
	else if (strncmp(name, "rfc4543(gcm(aes))-128", ALGO_LEN) == 0)
		*key_len = 160;
	else if (strncmp(name, "rfc4309(ccm(aes))-128", ALGO_LEN) == 0)
		*key_len = 152;
	else if (strncmp(name, "rfc4106(gcm(aes))-192", ALGO_LEN) == 0)
		*key_len = 224;
	else if (strncmp(name, "rfc4543(gcm(aes))-192", ALGO_LEN) == 0)
		*key_len = 224;
	else if (strncmp(name, "rfc4309(ccm(aes))-192", ALGO_LEN) == 0)
		*key_len = 216;
	else if (strncmp(name, "rfc4106(gcm(aes))-256", ALGO_LEN) == 0)
		*key_len = 288;
	else if (strncmp(name, "rfc4543(gcm(aes))-256", ALGO_LEN) == 0)
		*key_len = 288;
	else if (strncmp(name, "rfc4309(ccm(aes))-256", ALGO_LEN) == 0)
		*key_len = 280;
	else if (strncmp(name, "rfc7539(chacha20,poly1305)-128", ALGO_LEN) == 0)
		*key_len = 0;

	if (*key_len > buf_len) {
		printk("Can't pack a key - too big for buffer");
		return -1;
	}

	randomize_buffer(buf, *key_len);

	return 0;
}

static int xfrm_state_pack_algo(struct nlmsghdr *nh, size_t req_sz,
		struct xfrm_desc *desc)
{
	struct {
		union {
			struct xfrm_algo	alg;
			struct xfrm_algo_aead	aead;
			struct xfrm_algo_auth	auth;
		} u;
		char buf[XFRM_ALGO_KEY_BUF_SIZE];
	} alg = {};
	size_t alen, elen, clen, aelen;
	unsigned short type;

	alen = strlen(desc->a_algo);
	elen = strlen(desc->e_algo);
	clen = strlen(desc->c_algo);
	aelen = strlen(desc->ae_algo);

	/* Verify desc */
	switch (desc->proto) {
	case IPPROTO_AH:
		if (!alen || elen || clen || aelen) {
			printk("BUG: buggy ah desc");
			return -1;
		}
		strncpy(alg.u.alg.alg_name, desc->a_algo, ALGO_LEN - 1);
		if (xfrm_fill_key(desc->a_algo, alg.u.alg.alg_key,
				sizeof(alg.buf), &alg.u.alg.alg_key_len))
			return -1;
		type = XFRMA_ALG_AUTH;
		break;
	case IPPROTO_COMP:
		if (!clen || elen || alen || aelen) {
			printk("BUG: buggy comp desc");
			return -1;
		}
		strncpy(alg.u.alg.alg_name, desc->c_algo, ALGO_LEN - 1);
		if (xfrm_fill_key(desc->c_algo, alg.u.alg.alg_key,
				sizeof(alg.buf), &alg.u.alg.alg_key_len))
			return -1;
		type = XFRMA_ALG_COMP;
		break;
	case IPPROTO_ESP:
		if (!((alen && elen) ^ aelen) || clen) {
			printk("BUG: buggy esp desc");
			return -1;
		}
		if (aelen) {
			alg.u.aead.alg_icv_len = desc->icv_len;
			strncpy(alg.u.aead.alg_name, desc->ae_algo, ALGO_LEN - 1);
			if (xfrm_fill_key(desc->ae_algo, alg.u.aead.alg_key,
						sizeof(alg.buf), &alg.u.aead.alg_key_len))
				return -1;
			type = XFRMA_ALG_AEAD;
		} else {

			strncpy(alg.u.alg.alg_name, desc->e_algo, ALGO_LEN - 1);
			type = XFRMA_ALG_CRYPT;
			if (xfrm_fill_key(desc->e_algo, alg.u.alg.alg_key,
						sizeof(alg.buf), &alg.u.alg.alg_key_len))
				return -1;
			if (rtattr_pack(nh, req_sz, type, &alg, sizeof(alg)))
				return -1;

			strncpy(alg.u.alg.alg_name, desc->a_algo, ALGO_LEN);
			type = XFRMA_ALG_AUTH;
			if (xfrm_fill_key(desc->a_algo, alg.u.alg.alg_key,
						sizeof(alg.buf), &alg.u.alg.alg_key_len))
				return -1;
		}
		break;
	default:
		printk("BUG: unknown proto in desc");
		return -1;
	}

	if (rtattr_pack(nh, req_sz, type, &alg, sizeof(alg)))
		return -1;

	return 0;
}

static inline uint32_t gen_spi(struct in_addr src)
{
	return htonl(inet_lnaof(src));
}

static int xfrm_state_add(int xfrm_sock, uint32_t seq, uint32_t spi,
		struct in_addr src, struct in_addr dst,
		struct xfrm_desc *desc)
{
	struct {
		struct nlmsghdr		nh;
		struct xfrm_usersa_info	info;
		char			attrbuf[MAX_PAYLOAD];
	} req;

	memset(&req, 0, sizeof(req));
	req.nh.nlmsg_len	= NLMSG_LENGTH(sizeof(req.info));
	req.nh.nlmsg_type	= XFRM_MSG_NEWSA;
	req.nh.nlmsg_flags	= NLM_F_REQUEST | NLM_F_ACK;
	req.nh.nlmsg_seq	= seq;

	/* Fill selector. */
	memcpy(&req.info.sel.daddr, &dst, sizeof(dst));
	memcpy(&req.info.sel.saddr, &src, sizeof(src));
	req.info.sel.family		= AF_INET;
	req.info.sel.prefixlen_d	= PREFIX_LEN;
	req.info.sel.prefixlen_s	= PREFIX_LEN;

	/* Fill id */
	memcpy(&req.info.id.daddr, &dst, sizeof(dst));
	/* Note: zero-spi cannot be deleted */
	req.info.id.spi = spi;
	req.info.id.proto	= desc->proto;

	memcpy(&req.info.saddr, &src, sizeof(src));

	/* Fill lifteme_cfg */
	req.info.lft.soft_byte_limit	= XFRM_INF;
	req.info.lft.hard_byte_limit	= XFRM_INF;
	req.info.lft.soft_packet_limit	= XFRM_INF;
	req.info.lft.hard_packet_limit	= XFRM_INF;

	req.info.family		= AF_INET;
	req.info.mode		= XFRM_MODE_TUNNEL;

	if (xfrm_state_pack_algo(&req.nh, sizeof(req), desc))
		return -1;

	if (send(xfrm_sock, &req, req.nh.nlmsg_len, 0) < 0) {
		pr_err("send()");
		return -1;
	}

	return netlink_check_answer(xfrm_sock);
}

static bool xfrm_usersa_found(struct xfrm_usersa_info *info, uint32_t spi,
		struct in_addr src, struct in_addr dst,
		struct xfrm_desc *desc)
{
	if (memcmp(&info->sel.daddr, &dst, sizeof(dst)))
		return false;

	if (memcmp(&info->sel.saddr, &src, sizeof(src)))
		return false;

	if (info->sel.family != AF_INET					||
			info->sel.prefixlen_d != PREFIX_LEN		||
			info->sel.prefixlen_s != PREFIX_LEN)
		return false;

	if (info->id.spi != spi || info->id.proto != desc->proto)
		return false;

	if (memcmp(&info->id.daddr, &dst, sizeof(dst)))
		return false;

	if (memcmp(&info->saddr, &src, sizeof(src)))
		return false;

	if (info->lft.soft_byte_limit != XFRM_INF			||
			info->lft.hard_byte_limit != XFRM_INF		||
			info->lft.soft_packet_limit != XFRM_INF		||
			info->lft.hard_packet_limit != XFRM_INF)
		return false;

	if (info->family != AF_INET || info->mode != XFRM_MODE_TUNNEL)
		return false;

	/* XXX: check xfrm algo, see xfrm_state_pack_algo(). */

	return true;
}

static int xfrm_state_check(int xfrm_sock, uint32_t seq, uint32_t spi,
		struct in_addr src, struct in_addr dst,
		struct xfrm_desc *desc)
{
	struct {
		struct nlmsghdr		nh;
		char			attrbuf[MAX_PAYLOAD];
	} req;
	struct {
		struct nlmsghdr		nh;
		union {
			struct xfrm_usersa_info	info;
			int error;
		};
		char			attrbuf[MAX_PAYLOAD];
	} answer;
	struct xfrm_address_filter filter = {};
	bool found = false;


	memset(&req, 0, sizeof(req));
	req.nh.nlmsg_len	= NLMSG_LENGTH(0);
	req.nh.nlmsg_type	= XFRM_MSG_GETSA;
	req.nh.nlmsg_flags	= NLM_F_REQUEST | NLM_F_DUMP;
	req.nh.nlmsg_seq	= seq;

	/*
	 * Add dump filter by source address as there may be other tunnels
	 * in this netns (if tests run in parallel).
	 */
	filter.family = AF_INET;
	filter.splen = 0x1f;	/* 0xffffffff mask see addr_match() */
	memcpy(&filter.saddr, &src, sizeof(src));
	if (rtattr_pack(&req.nh, sizeof(req), XFRMA_ADDRESS_FILTER,
				&filter, sizeof(filter)))
		return -1;

	if (send(xfrm_sock, &req, req.nh.nlmsg_len, 0) < 0) {
		pr_err("send()");
		return -1;
	}

	while (1) {
		if (recv(xfrm_sock, &answer, sizeof(answer), 0) < 0) {
			pr_err("recv()");
			return -1;
		}
		if (answer.nh.nlmsg_type == NLMSG_ERROR) {
			printk("NLMSG_ERROR: %d: %s",
				answer.error, strerror(-answer.error));
			return -1;
		} else if (answer.nh.nlmsg_type == NLMSG_DONE) {
			if (found)
				return 0;
			printk("didn't find allocated xfrm state in dump");
			return -1;
		} else if (answer.nh.nlmsg_type == XFRM_MSG_NEWSA) {
			if (xfrm_usersa_found(&answer.info, spi, src, dst, desc))
				found = true;
		}
	}
}

static int xfrm_set(int xfrm_sock, uint32_t *seq,
		struct in_addr src, struct in_addr dst,
		struct in_addr tunsrc, struct in_addr tundst,
		struct xfrm_desc *desc)
{
	int err;

	err = xfrm_state_add(xfrm_sock, (*seq)++, gen_spi(src), src, dst, desc);
	if (err) {
		printk("Failed to add xfrm state");
		return -1;
	}

	err = xfrm_state_add(xfrm_sock, (*seq)++, gen_spi(src), dst, src, desc);
	if (err) {
		printk("Failed to add xfrm state");
		return -1;
	}

	/* Check dumps for XFRM_MSG_GETSA */
	err = xfrm_state_check(xfrm_sock, (*seq)++, gen_spi(src), src, dst, desc);
	err |= xfrm_state_check(xfrm_sock, (*seq)++, gen_spi(src), dst, src, desc);
	if (err) {
		printk("Failed to check xfrm state");
		return -1;
	}

	return 0;
}

static int xfrm_policy_add(int xfrm_sock, uint32_t seq, uint32_t spi,
		struct in_addr src, struct in_addr dst, uint8_t dir,
		struct in_addr tunsrc, struct in_addr tundst, uint8_t proto)
{
	struct {
		struct nlmsghdr			nh;
		struct xfrm_userpolicy_info	info;
		char				attrbuf[MAX_PAYLOAD];
	} req;
	struct xfrm_user_tmpl tmpl;

	memset(&req, 0, sizeof(req));
	memset(&tmpl, 0, sizeof(tmpl));
	req.nh.nlmsg_len	= NLMSG_LENGTH(sizeof(req.info));
	req.nh.nlmsg_type	= XFRM_MSG_NEWPOLICY;
	req.nh.nlmsg_flags	= NLM_F_REQUEST | NLM_F_ACK;
	req.nh.nlmsg_seq	= seq;

	/* Fill selector. */
	memcpy(&req.info.sel.daddr, &dst, sizeof(tundst));
	memcpy(&req.info.sel.saddr, &src, sizeof(tunsrc));
	req.info.sel.family		= AF_INET;
	req.info.sel.prefixlen_d	= PREFIX_LEN;
	req.info.sel.prefixlen_s	= PREFIX_LEN;

	/* Fill lifteme_cfg */
	req.info.lft.soft_byte_limit	= XFRM_INF;
	req.info.lft.hard_byte_limit	= XFRM_INF;
	req.info.lft.soft_packet_limit	= XFRM_INF;
	req.info.lft.hard_packet_limit	= XFRM_INF;

	req.info.dir = dir;

	/* Fill tmpl */
	memcpy(&tmpl.id.daddr, &dst, sizeof(dst));
	/* Note: zero-spi cannot be deleted */
	tmpl.id.spi = spi;
	tmpl.id.proto	= proto;
	tmpl.family	= AF_INET;
	memcpy(&tmpl.saddr, &src, sizeof(src));
	tmpl.mode	= XFRM_MODE_TUNNEL;
	tmpl.aalgos = (~(uint32_t)0);
	tmpl.ealgos = (~(uint32_t)0);
	tmpl.calgos = (~(uint32_t)0);

	if (rtattr_pack(&req.nh, sizeof(req), XFRMA_TMPL, &tmpl, sizeof(tmpl)))
		return -1;

	if (send(xfrm_sock, &req, req.nh.nlmsg_len, 0) < 0) {
		pr_err("send()");
		return -1;
	}

	return netlink_check_answer(xfrm_sock);
}

static int xfrm_prepare(int xfrm_sock, uint32_t *seq,
		struct in_addr src, struct in_addr dst,
		struct in_addr tunsrc, struct in_addr tundst, uint8_t proto)
{
	if (xfrm_policy_add(xfrm_sock, (*seq)++, gen_spi(src), src, dst,
				XFRM_POLICY_OUT, tunsrc, tundst, proto)) {
		printk("Failed to add xfrm policy");
		return -1;
	}

	if (xfrm_policy_add(xfrm_sock, (*seq)++, gen_spi(src), dst, src,
				XFRM_POLICY_IN, tunsrc, tundst, proto)) {
		printk("Failed to add xfrm policy");
		return -1;
	}

	return 0;
}

static int xfrm_policy_del(int xfrm_sock, uint32_t seq,
		struct in_addr src, struct in_addr dst, uint8_t dir,
		struct in_addr tunsrc, struct in_addr tundst)
{
	struct {
		struct nlmsghdr			nh;
		struct xfrm_userpolicy_id	id;
		char				attrbuf[MAX_PAYLOAD];
	} req;

	memset(&req, 0, sizeof(req));
	req.nh.nlmsg_len	= NLMSG_LENGTH(sizeof(req.id));
	req.nh.nlmsg_type	= XFRM_MSG_DELPOLICY;
	req.nh.nlmsg_flags	= NLM_F_REQUEST | NLM_F_ACK;
	req.nh.nlmsg_seq	= seq;

	/* Fill id */
	memcpy(&req.id.sel.daddr, &dst, sizeof(tundst));
	memcpy(&req.id.sel.saddr, &src, sizeof(tunsrc));
	req.id.sel.family		= AF_INET;
	req.id.sel.prefixlen_d		= PREFIX_LEN;
	req.id.sel.prefixlen_s		= PREFIX_LEN;
	req.id.dir = dir;

	if (send(xfrm_sock, &req, req.nh.nlmsg_len, 0) < 0) {
		pr_err("send()");
		return -1;
	}

	return netlink_check_answer(xfrm_sock);
}

static int xfrm_cleanup(int xfrm_sock, uint32_t *seq,
		struct in_addr src, struct in_addr dst,
		struct in_addr tunsrc, struct in_addr tundst)
{
	if (xfrm_policy_del(xfrm_sock, (*seq)++, src, dst,
				XFRM_POLICY_OUT, tunsrc, tundst)) {
		printk("Failed to add xfrm policy");
		return -1;
	}

	if (xfrm_policy_del(xfrm_sock, (*seq)++, dst, src,
				XFRM_POLICY_IN, tunsrc, tundst)) {
		printk("Failed to add xfrm policy");
		return -1;
	}

	return 0;
}

static int xfrm_state_del(int xfrm_sock, uint32_t seq, uint32_t spi,
		struct in_addr src, struct in_addr dst, uint8_t proto)
{
	struct {
		struct nlmsghdr		nh;
		struct xfrm_usersa_id	id;
		char			attrbuf[MAX_PAYLOAD];
	} req;
	xfrm_address_t saddr = {};

	memset(&req, 0, sizeof(req));
	req.nh.nlmsg_len	= NLMSG_LENGTH(sizeof(req.id));
	req.nh.nlmsg_type	= XFRM_MSG_DELSA;
	req.nh.nlmsg_flags	= NLM_F_REQUEST | NLM_F_ACK;
	req.nh.nlmsg_seq	= seq;

	memcpy(&req.id.daddr, &dst, sizeof(dst));
	req.id.family		= AF_INET;
	req.id.proto		= proto;
	/* Note: zero-spi cannot be deleted */
	req.id.spi = spi;

	memcpy(&saddr, &src, sizeof(src));
	if (rtattr_pack(&req.nh, sizeof(req), XFRMA_SRCADDR, &saddr, sizeof(saddr)))
		return -1;

	if (send(xfrm_sock, &req, req.nh.nlmsg_len, 0) < 0) {
		pr_err("send()");
		return -1;
	}

	return netlink_check_answer(xfrm_sock);
}

static int xfrm_delete(int xfrm_sock, uint32_t *seq,
		struct in_addr src, struct in_addr dst,
		struct in_addr tunsrc, struct in_addr tundst, uint8_t proto)
{
	if (xfrm_state_del(xfrm_sock, (*seq)++, gen_spi(src), src, dst, proto)) {
		printk("Failed to remove xfrm state");
		return -1;
	}

	if (xfrm_state_del(xfrm_sock, (*seq)++, gen_spi(src), dst, src, proto)) {
		printk("Failed to remove xfrm state");
		return -1;
	}

	return 0;
}

static int xfrm_state_allocspi(int xfrm_sock, uint32_t *seq,
		uint32_t spi, uint8_t proto)
{
	struct {
		struct nlmsghdr			nh;
		struct xfrm_userspi_info	spi;
	} req;
	struct {
		struct nlmsghdr			nh;
		union {
			struct xfrm_usersa_info	info;
			int error;
		};
	} answer;

	memset(&req, 0, sizeof(req));
	req.nh.nlmsg_len	= NLMSG_LENGTH(sizeof(req.spi));
	req.nh.nlmsg_type	= XFRM_MSG_ALLOCSPI;
	req.nh.nlmsg_flags	= NLM_F_REQUEST;
	req.nh.nlmsg_seq	= (*seq)++;

	req.spi.info.family	= AF_INET;
	req.spi.min		= spi;
	req.spi.max		= spi;
	req.spi.info.id.proto	= proto;

	if (send(xfrm_sock, &req, req.nh.nlmsg_len, 0) < 0) {
		pr_err("send()");
		return KSFT_FAIL;
	}

	if (recv(xfrm_sock, &answer, sizeof(answer), 0) < 0) {
		pr_err("recv()");
		return KSFT_FAIL;
	} else if (answer.nh.nlmsg_type == XFRM_MSG_NEWSA) {
		uint32_t new_spi = htonl(answer.info.id.spi);

		if (new_spi != spi) {
			printk("allocated spi is different from requested: %#x != %#x",
					new_spi, spi);
			return KSFT_FAIL;
		}
		return KSFT_PASS;
	} else if (answer.nh.nlmsg_type != NLMSG_ERROR) {
		printk("expected NLMSG_ERROR, got %d", (int)answer.nh.nlmsg_type);
		return KSFT_FAIL;
	}

	printk("NLMSG_ERROR: %d: %s", answer.error, strerror(-answer.error));
	return (answer.error) ? KSFT_FAIL : KSFT_PASS;
}

static int netlink_sock_bind(int *sock, uint32_t *seq, int proto, uint32_t groups)
{
	struct sockaddr_nl snl = {};
	socklen_t addr_len;
	int ret = -1;

	snl.nl_family = AF_NETLINK;
	snl.nl_groups = groups;

	if (netlink_sock(sock, seq, proto)) {
		printk("Failed to open xfrm netlink socket");
		return -1;
	}

	if (bind(*sock, (struct sockaddr *)&snl, sizeof(snl)) < 0) {
		pr_err("bind()");
		goto out_close;
	}

	addr_len = sizeof(snl);
	if (getsockname(*sock, (struct sockaddr *)&snl, &addr_len) < 0) {
		pr_err("getsockname()");
		goto out_close;
	}
	if (addr_len != sizeof(snl)) {
		printk("Wrong address length %d", addr_len);
		goto out_close;
	}
	if (snl.nl_family != AF_NETLINK) {
		printk("Wrong address family %d", snl.nl_family);
		goto out_close;
	}
	return 0;

out_close:
	close(*sock);
	return ret;
}

static int xfrm_monitor_acquire(int xfrm_sock, uint32_t *seq, unsigned int nr)
{
	struct {
		struct nlmsghdr nh;
		union {
			struct xfrm_user_acquire acq;
			int error;
		};
		char attrbuf[MAX_PAYLOAD];
	} req;
	struct xfrm_user_tmpl xfrm_tmpl = {};
	int xfrm_listen = -1, ret = KSFT_FAIL;
	uint32_t seq_listen;

	if (netlink_sock_bind(&xfrm_listen, &seq_listen, NETLINK_XFRM, XFRMNLGRP_ACQUIRE))
		return KSFT_FAIL;

	memset(&req, 0, sizeof(req));
	req.nh.nlmsg_len	= NLMSG_LENGTH(sizeof(req.acq));
	req.nh.nlmsg_type	= XFRM_MSG_ACQUIRE;
	req.nh.nlmsg_flags	= NLM_F_REQUEST | NLM_F_ACK;
	req.nh.nlmsg_seq	= (*seq)++;

	req.acq.policy.sel.family	= AF_INET;
	req.acq.aalgos	= 0xfeed;
	req.acq.ealgos	= 0xbaad;
	req.acq.calgos	= 0xbabe;

	xfrm_tmpl.family = AF_INET;
	xfrm_tmpl.id.proto = IPPROTO_ESP;
	if (rtattr_pack(&req.nh, sizeof(req), XFRMA_TMPL, &xfrm_tmpl, sizeof(xfrm_tmpl)))
		goto out_close;

	if (send(xfrm_sock, &req, req.nh.nlmsg_len, 0) < 0) {
		pr_err("send()");
		goto out_close;
	}

	if (recv(xfrm_sock, &req, sizeof(req), 0) < 0) {
		pr_err("recv()");
		goto out_close;
	} else if (req.nh.nlmsg_type != NLMSG_ERROR) {
		printk("expected NLMSG_ERROR, got %d", (int)req.nh.nlmsg_type);
		goto out_close;
	}

	if (req.error) {
		printk("NLMSG_ERROR: %d: %s", req.error, strerror(-req.error));
		ret = req.error;
		goto out_close;
	}

	if (recv(xfrm_listen, &req, sizeof(req), 0) < 0) {
		pr_err("recv()");
		goto out_close;
	}

	if (req.acq.aalgos != 0xfeed || req.acq.ealgos != 0xbaad
			|| req.acq.calgos != 0xbabe) {
		printk("xfrm_user_acquire has changed  %x %x %x",
				req.acq.aalgos, req.acq.ealgos, req.acq.calgos);
		goto out_close;
	}

	ret = KSFT_PASS;
out_close:
	close(xfrm_listen);
	return ret;
}

static int xfrm_expire_state(int xfrm_sock, uint32_t *seq,
		unsigned int nr, struct xfrm_desc *desc)
{
	struct {
		struct nlmsghdr nh;
		union {
			struct xfrm_user_expire expire;
			int error;
		};
	} req;
	struct in_addr src, dst;
	int xfrm_listen = -1, ret = KSFT_FAIL;
	uint32_t seq_listen;

	src = inet_makeaddr(INADDR_B, child_ip(nr));
	dst = inet_makeaddr(INADDR_B, grchild_ip(nr));

	if (xfrm_state_add(xfrm_sock, (*seq)++, gen_spi(src), src, dst, desc)) {
		printk("Failed to add xfrm state");
		return KSFT_FAIL;
	}

	if (netlink_sock_bind(&xfrm_listen, &seq_listen, NETLINK_XFRM, XFRMNLGRP_EXPIRE))
		return KSFT_FAIL;

	memset(&req, 0, sizeof(req));
	req.nh.nlmsg_len	= NLMSG_LENGTH(sizeof(req.expire));
	req.nh.nlmsg_type	= XFRM_MSG_EXPIRE;
	req.nh.nlmsg_flags	= NLM_F_REQUEST | NLM_F_ACK;
	req.nh.nlmsg_seq	= (*seq)++;

	memcpy(&req.expire.state.id.daddr, &dst, sizeof(dst));
	req.expire.state.id.spi		= gen_spi(src);
	req.expire.state.id.proto	= desc->proto;
	req.expire.state.family		= AF_INET;
	req.expire.hard			= 0xff;

	if (send(xfrm_sock, &req, req.nh.nlmsg_len, 0) < 0) {
		pr_err("send()");
		goto out_close;
	}

	if (recv(xfrm_sock, &req, sizeof(req), 0) < 0) {
		pr_err("recv()");
		goto out_close;
	} else if (req.nh.nlmsg_type != NLMSG_ERROR) {
		printk("expected NLMSG_ERROR, got %d", (int)req.nh.nlmsg_type);
		goto out_close;
	}

	if (req.error) {
		printk("NLMSG_ERROR: %d: %s", req.error, strerror(-req.error));
		ret = req.error;
		goto out_close;
	}

	if (recv(xfrm_listen, &req, sizeof(req), 0) < 0) {
		pr_err("recv()");
		goto out_close;
	}

	if (req.expire.hard != 0x1) {
		printk("expire.hard is not set: %x", req.expire.hard);
		goto out_close;
	}

	ret = KSFT_PASS;
out_close:
	close(xfrm_listen);
	return ret;
}

static int xfrm_expire_policy(int xfrm_sock, uint32_t *seq,
		unsigned int nr, struct xfrm_desc *desc)
{
	struct {
		struct nlmsghdr nh;
		union {
			struct xfrm_user_polexpire expire;
			int error;
		};
	} req;
	struct in_addr src, dst, tunsrc, tundst;
	int xfrm_listen = -1, ret = KSFT_FAIL;
	uint32_t seq_listen;

	src = inet_makeaddr(INADDR_B, child_ip(nr));
	dst = inet_makeaddr(INADDR_B, grchild_ip(nr));
	tunsrc = inet_makeaddr(INADDR_A, child_ip(nr));
	tundst = inet_makeaddr(INADDR_A, grchild_ip(nr));

	if (xfrm_policy_add(xfrm_sock, (*seq)++, gen_spi(src), src, dst,
				XFRM_POLICY_OUT, tunsrc, tundst, desc->proto)) {
		printk("Failed to add xfrm policy");
		return KSFT_FAIL;
	}

	if (netlink_sock_bind(&xfrm_listen, &seq_listen, NETLINK_XFRM, XFRMNLGRP_EXPIRE))
		return KSFT_FAIL;

	memset(&req, 0, sizeof(req));
	req.nh.nlmsg_len	= NLMSG_LENGTH(sizeof(req.expire));
	req.nh.nlmsg_type	= XFRM_MSG_POLEXPIRE;
	req.nh.nlmsg_flags	= NLM_F_REQUEST | NLM_F_ACK;
	req.nh.nlmsg_seq	= (*seq)++;

	/* Fill selector. */
	memcpy(&req.expire.pol.sel.daddr, &dst, sizeof(tundst));
	memcpy(&req.expire.pol.sel.saddr, &src, sizeof(tunsrc));
	req.expire.pol.sel.family	= AF_INET;
	req.expire.pol.sel.prefixlen_d	= PREFIX_LEN;
	req.expire.pol.sel.prefixlen_s	= PREFIX_LEN;
	req.expire.pol.dir		= XFRM_POLICY_OUT;
	req.expire.hard			= 0xff;

	if (send(xfrm_sock, &req, req.nh.nlmsg_len, 0) < 0) {
		pr_err("send()");
		goto out_close;
	}

	if (recv(xfrm_sock, &req, sizeof(req), 0) < 0) {
		pr_err("recv()");
		goto out_close;
	} else if (req.nh.nlmsg_type != NLMSG_ERROR) {
		printk("expected NLMSG_ERROR, got %d", (int)req.nh.nlmsg_type);
		goto out_close;
	}

	if (req.error) {
		printk("NLMSG_ERROR: %d: %s", req.error, strerror(-req.error));
		ret = req.error;
		goto out_close;
	}

	if (recv(xfrm_listen, &req, sizeof(req), 0) < 0) {
		pr_err("recv()");
		goto out_close;
	}

	if (req.expire.hard != 0x1) {
		printk("expire.hard is not set: %x", req.expire.hard);
		goto out_close;
	}

	ret = KSFT_PASS;
out_close:
	close(xfrm_listen);
	return ret;
}

static int xfrm_spdinfo_set_thresh(int xfrm_sock, uint32_t *seq,
		unsigned thresh4_l, unsigned thresh4_r,
		unsigned thresh6_l, unsigned thresh6_r,
		bool add_bad_attr)

{
	struct {
		struct nlmsghdr		nh;
		union {
			uint32_t	unused;
			int		error;
		};
		char			attrbuf[MAX_PAYLOAD];
	} req;
	struct xfrmu_spdhthresh thresh;

	memset(&req, 0, sizeof(req));
	req.nh.nlmsg_len	= NLMSG_LENGTH(sizeof(req.unused));
	req.nh.nlmsg_type	= XFRM_MSG_NEWSPDINFO;
	req.nh.nlmsg_flags	= NLM_F_REQUEST | NLM_F_ACK;
	req.nh.nlmsg_seq	= (*seq)++;

	thresh.lbits = thresh4_l;
	thresh.rbits = thresh4_r;
	if (rtattr_pack(&req.nh, sizeof(req), XFRMA_SPD_IPV4_HTHRESH, &thresh, sizeof(thresh)))
		return -1;

	thresh.lbits = thresh6_l;
	thresh.rbits = thresh6_r;
	if (rtattr_pack(&req.nh, sizeof(req), XFRMA_SPD_IPV6_HTHRESH, &thresh, sizeof(thresh)))
		return -1;

	if (add_bad_attr) {
		BUILD_BUG_ON(XFRMA_IF_ID <= XFRMA_SPD_MAX + 1);
		if (rtattr_pack(&req.nh, sizeof(req), XFRMA_IF_ID, NULL, 0)) {
			pr_err("adding attribute failed: no space");
			return -1;
		}
	}

	if (send(xfrm_sock, &req, req.nh.nlmsg_len, 0) < 0) {
		pr_err("send()");
		return -1;
	}

	if (recv(xfrm_sock, &req, sizeof(req), 0) < 0) {
		pr_err("recv()");
		return -1;
	} else if (req.nh.nlmsg_type != NLMSG_ERROR) {
		printk("expected NLMSG_ERROR, got %d", (int)req.nh.nlmsg_type);
		return -1;
	}

	if (req.error) {
		printk("NLMSG_ERROR: %d: %s", req.error, strerror(-req.error));
		return -1;
	}

	return 0;
}

static int xfrm_spdinfo_attrs(int xfrm_sock, uint32_t *seq)
{
	struct {
		struct nlmsghdr			nh;
		union {
			uint32_t	unused;
			int		error;
		};
		char			attrbuf[MAX_PAYLOAD];
	} req;

	if (xfrm_spdinfo_set_thresh(xfrm_sock, seq, 32, 31, 120, 16, false)) {
		pr_err("Can't set SPD HTHRESH");
		return KSFT_FAIL;
	}

	memset(&req, 0, sizeof(req));

	req.nh.nlmsg_len	= NLMSG_LENGTH(sizeof(req.unused));
	req.nh.nlmsg_type	= XFRM_MSG_GETSPDINFO;
	req.nh.nlmsg_flags	= NLM_F_REQUEST;
	req.nh.nlmsg_seq	= (*seq)++;
	if (send(xfrm_sock, &req, req.nh.nlmsg_len, 0) < 0) {
		pr_err("send()");
		return KSFT_FAIL;
	}

	if (recv(xfrm_sock, &req, sizeof(req), 0) < 0) {
		pr_err("recv()");
		return KSFT_FAIL;
	} else if (req.nh.nlmsg_type == XFRM_MSG_NEWSPDINFO) {
		size_t len = NLMSG_PAYLOAD(&req.nh, sizeof(req.unused));
		struct rtattr *attr = (void *)req.attrbuf;
		int got_thresh = 0;

		for (; RTA_OK(attr, len); attr = RTA_NEXT(attr, len)) {
			if (attr->rta_type == XFRMA_SPD_IPV4_HTHRESH) {
				struct xfrmu_spdhthresh *t = RTA_DATA(attr);

				got_thresh++;
				if (t->lbits != 32 || t->rbits != 31) {
					pr_err("thresh differ: %u, %u",
							t->lbits, t->rbits);
					return KSFT_FAIL;
				}
			}
			if (attr->rta_type == XFRMA_SPD_IPV6_HTHRESH) {
				struct xfrmu_spdhthresh *t = RTA_DATA(attr);

				got_thresh++;
				if (t->lbits != 120 || t->rbits != 16) {
					pr_err("thresh differ: %u, %u",
							t->lbits, t->rbits);
					return KSFT_FAIL;
				}
			}
		}
		if (got_thresh != 2) {
			pr_err("only %d thresh returned by XFRM_MSG_GETSPDINFO", got_thresh);
			return KSFT_FAIL;
		}
	} else if (req.nh.nlmsg_type != NLMSG_ERROR) {
		printk("expected NLMSG_ERROR, got %d", (int)req.nh.nlmsg_type);
		return KSFT_FAIL;
	} else {
		printk("NLMSG_ERROR: %d: %s", req.error, strerror(-req.error));
		return -1;
	}

	/* Restore the default */
	if (xfrm_spdinfo_set_thresh(xfrm_sock, seq, 32, 32, 128, 128, false)) {
		pr_err("Can't restore SPD HTHRESH");
		return KSFT_FAIL;
	}

	/*
	 * At this moment xfrm uses nlmsg_parse_deprecated(), which
	 * implies NL_VALIDATE_LIBERAL - ignoring attributes with
	 * (type > maxtype). nla_parse_depricated_strict() would enforce
	 * it. Or even stricter nla_parse().
	 * Right now it's not expected to fail, but to be ignored.
	 */
	if (xfrm_spdinfo_set_thresh(xfrm_sock, seq, 32, 32, 128, 128, true))
		return KSFT_PASS;

	return KSFT_PASS;
}

static int child_serv(int xfrm_sock, uint32_t *seq,
		unsigned int nr, int cmd_fd, void *buf, struct xfrm_desc *desc)
{
	struct in_addr src, dst, tunsrc, tundst;
	struct test_desc msg;
	int ret = KSFT_FAIL;

	src = inet_makeaddr(INADDR_B, child_ip(nr));
	dst = inet_makeaddr(INADDR_B, grchild_ip(nr));
	tunsrc = inet_makeaddr(INADDR_A, child_ip(nr));
	tundst = inet_makeaddr(INADDR_A, grchild_ip(nr));

	/* UDP pinging without xfrm */
	if (do_ping(cmd_fd, buf, page_size, src, true, 0, 0, udp_ping_send)) {
		printk("ping failed before setting xfrm");
		return KSFT_FAIL;
	}

	memset(&msg, 0, sizeof(msg));
	msg.type = MSG_XFRM_PREPARE;
	memcpy(&msg.body.xfrm_desc, desc, sizeof(*desc));
	write_msg(cmd_fd, &msg, 1);

	if (xfrm_prepare(xfrm_sock, seq, src, dst, tunsrc, tundst, desc->proto)) {
		printk("failed to prepare xfrm");
		goto cleanup;
	}

	memset(&msg, 0, sizeof(msg));
	msg.type = MSG_XFRM_ADD;
	memcpy(&msg.body.xfrm_desc, desc, sizeof(*desc));
	write_msg(cmd_fd, &msg, 1);
	if (xfrm_set(xfrm_sock, seq, src, dst, tunsrc, tundst, desc)) {
		printk("failed to set xfrm");
		goto delete;
	}

	/* UDP pinging with xfrm tunnel */
	if (do_ping(cmd_fd, buf, page_size, tunsrc,
				true, 0, 0, udp_ping_send)) {
		printk("ping failed for xfrm");
		goto delete;
	}

	ret = KSFT_PASS;
delete:
	/* xfrm delete */
	memset(&msg, 0, sizeof(msg));
	msg.type = MSG_XFRM_DEL;
	memcpy(&msg.body.xfrm_desc, desc, sizeof(*desc));
	write_msg(cmd_fd, &msg, 1);

	if (xfrm_delete(xfrm_sock, seq, src, dst, tunsrc, tundst, desc->proto)) {
		printk("failed ping to remove xfrm");
		ret = KSFT_FAIL;
	}

cleanup:
	memset(&msg, 0, sizeof(msg));
	msg.type = MSG_XFRM_CLEANUP;
	memcpy(&msg.body.xfrm_desc, desc, sizeof(*desc));
	write_msg(cmd_fd, &msg, 1);
	if (xfrm_cleanup(xfrm_sock, seq, src, dst, tunsrc, tundst)) {
		printk("failed ping to cleanup xfrm");
		ret = KSFT_FAIL;
	}
	return ret;
}

static int child_f(unsigned int nr, int test_desc_fd, int cmd_fd, void *buf)
{
	struct xfrm_desc desc;
	struct test_desc msg;
	int xfrm_sock = -1;
	uint32_t seq;

	if (switch_ns(nsfd_childa))
		exit(KSFT_FAIL);

	if (netlink_sock(&xfrm_sock, &seq, NETLINK_XFRM)) {
		printk("Failed to open xfrm netlink socket");
		exit(KSFT_FAIL);
	}

	/* Check that seq sock is ready, just for sure. */
	memset(&msg, 0, sizeof(msg));
	msg.type = MSG_ACK;
	write_msg(cmd_fd, &msg, 1);
	read_msg(cmd_fd, &msg, 1);
	if (msg.type != MSG_ACK) {
		printk("Ack failed");
		exit(KSFT_FAIL);
	}

	for (;;) {
		ssize_t received = read(test_desc_fd, &desc, sizeof(desc));
		int ret;

		if (received == 0) /* EOF */
			break;

		if (received != sizeof(desc)) {
			pr_err("read() returned %zd", received);
			exit(KSFT_FAIL);
		}

		switch (desc.type) {
		case CREATE_TUNNEL:
			ret = child_serv(xfrm_sock, &seq, nr,
					 cmd_fd, buf, &desc);
			break;
		case ALLOCATE_SPI:
			ret = xfrm_state_allocspi(xfrm_sock, &seq,
						  -1, desc.proto);
			break;
		case MONITOR_ACQUIRE:
			ret = xfrm_monitor_acquire(xfrm_sock, &seq, nr);
			break;
		case EXPIRE_STATE:
			ret = xfrm_expire_state(xfrm_sock, &seq, nr, &desc);
			break;
		case EXPIRE_POLICY:
			ret = xfrm_expire_policy(xfrm_sock, &seq, nr, &desc);
			break;
		case SPDINFO_ATTRS:
			ret = xfrm_spdinfo_attrs(xfrm_sock, &seq);
			break;
		default:
			printk("Unknown desc type %d", desc.type);
			exit(KSFT_FAIL);
		}
		write_test_result(ret, &desc);
	}

	close(xfrm_sock);

	msg.type = MSG_EXIT;
	write_msg(cmd_fd, &msg, 1);
	exit(KSFT_PASS);
}

static void grand_child_serv(unsigned int nr, int cmd_fd, void *buf,
		struct test_desc *msg, int xfrm_sock, uint32_t *seq)
{
	struct in_addr src, dst, tunsrc, tundst;
	bool tun_reply;
	struct xfrm_desc *desc = &msg->body.xfrm_desc;

	src = inet_makeaddr(INADDR_B, grchild_ip(nr));
	dst = inet_makeaddr(INADDR_B, child_ip(nr));
	tunsrc = inet_makeaddr(INADDR_A, grchild_ip(nr));
	tundst = inet_makeaddr(INADDR_A, child_ip(nr));

	switch (msg->type) {
	case MSG_EXIT:
		exit(KSFT_PASS);
	case MSG_ACK:
		write_msg(cmd_fd, msg, 1);
		break;
	case MSG_PING:
		tun_reply = memcmp(&dst, &msg->body.ping.reply_ip, sizeof(in_addr_t));
		/* UDP pinging without xfrm */
		if (do_ping(cmd_fd, buf, page_size, tun_reply ? tunsrc : src,
				false, msg->body.ping.port,
				msg->body.ping.reply_ip, udp_ping_reply)) {
			printk("ping failed before setting xfrm");
		}
		break;
	case MSG_XFRM_PREPARE:
		if (xfrm_prepare(xfrm_sock, seq, src, dst, tunsrc, tundst,
					desc->proto)) {
			xfrm_cleanup(xfrm_sock, seq, src, dst, tunsrc, tundst);
			printk("failed to prepare xfrm");
		}
		break;
	case MSG_XFRM_ADD:
		if (xfrm_set(xfrm_sock, seq, src, dst, tunsrc, tundst, desc)) {
			xfrm_cleanup(xfrm_sock, seq, src, dst, tunsrc, tundst);
			printk("failed to set xfrm");
		}
		break;
	case MSG_XFRM_DEL:
		if (xfrm_delete(xfrm_sock, seq, src, dst, tunsrc, tundst,
					desc->proto)) {
			xfrm_cleanup(xfrm_sock, seq, src, dst, tunsrc, tundst);
			printk("failed to remove xfrm");
		}
		break;
	case MSG_XFRM_CLEANUP:
		if (xfrm_cleanup(xfrm_sock, seq, src, dst, tunsrc, tundst)) {
			printk("failed to cleanup xfrm");
		}
		break;
	default:
		printk("got unknown msg type %d", msg->type);
	}
}

static int grand_child_f(unsigned int nr, int cmd_fd, void *buf)
{
	struct test_desc msg;
	int xfrm_sock = -1;
	uint32_t seq;

	if (switch_ns(nsfd_childb))
		exit(KSFT_FAIL);

	if (netlink_sock(&xfrm_sock, &seq, NETLINK_XFRM)) {
		printk("Failed to open xfrm netlink socket");
		exit(KSFT_FAIL);
	}

	do {
		read_msg(cmd_fd, &msg, 1);
		grand_child_serv(nr, cmd_fd, buf, &msg, xfrm_sock, &seq);
	} while (1);

	close(xfrm_sock);
	exit(KSFT_FAIL);
}

static int start_child(unsigned int nr, char *veth, int test_desc_fd[2])
{
	int cmd_sock[2];
	void *data_map;
	pid_t child;

	if (init_child(nsfd_childa, veth, child_ip(nr), grchild_ip(nr)))
		return -1;

	if (init_child(nsfd_childb, veth, grchild_ip(nr), child_ip(nr)))
		return -1;

	child = fork();
	if (child < 0) {
		pr_err("fork()");
		return -1;
	} else if (child) {
		/* in parent - selftest */
		return switch_ns(nsfd_parent);
	}

	if (close(test_desc_fd[1])) {
		pr_err("close()");
		return -1;
	}

	/* child */
	data_map = mmap(0, page_size, PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (data_map == MAP_FAILED) {
		pr_err("mmap()");
		return -1;
	}

	randomize_buffer(data_map, page_size);

	if (socketpair(PF_LOCAL, SOCK_SEQPACKET, 0, cmd_sock)) {
		pr_err("socketpair()");
		return -1;
	}

	child = fork();
	if (child < 0) {
		pr_err("fork()");
		return -1;
	} else if (child) {
		if (close(cmd_sock[0])) {
			pr_err("close()");
			return -1;
		}
		return child_f(nr, test_desc_fd[0], cmd_sock[1], data_map);
	}
	if (close(cmd_sock[1])) {
		pr_err("close()");
		return -1;
	}
	return grand_child_f(nr, cmd_sock[0], data_map);
}

static void exit_usage(char **argv)
{
	printk("Usage: %s [nr_process]", argv[0]);
	exit(KSFT_FAIL);
}

static int __write_desc(int test_desc_fd, struct xfrm_desc *desc)
{
	ssize_t ret;

	ret = write(test_desc_fd, desc, sizeof(*desc));

	if (ret == sizeof(*desc))
		return 0;

	pr_err("Writing test's desc failed %ld", ret);

	return -1;
}

static int write_desc(int proto, int test_desc_fd,
		char *a, char *e, char *c, char *ae)
{
	struct xfrm_desc desc = {};

	desc.type = CREATE_TUNNEL;
	desc.proto = proto;

	if (a)
		strncpy(desc.a_algo, a, ALGO_LEN - 1);
	if (e)
		strncpy(desc.e_algo, e, ALGO_LEN - 1);
	if (c)
		strncpy(desc.c_algo, c, ALGO_LEN - 1);
	if (ae)
		strncpy(desc.ae_algo, ae, ALGO_LEN - 1);

	return __write_desc(test_desc_fd, &desc);
}

int proto_list[] = { IPPROTO_AH, IPPROTO_COMP, IPPROTO_ESP };
char *ah_list[] = {
	"digest_null", "hmac(md5)", "hmac(sha1)", "hmac(sha256)",
	"hmac(sha384)", "hmac(sha512)", "hmac(rmd160)",
	"xcbc(aes)", "cmac(aes)"
};
char *comp_list[] = {
	"deflate",
#if 0
	/* No compression backend realization */
	"lzs", "lzjh"
#endif
};
char *e_list[] = {
	"ecb(cipher_null)", "cbc(des)", "cbc(des3_ede)", "cbc(cast5)",
	"cbc(blowfish)", "cbc(aes)", "cbc(serpent)", "cbc(camellia)",
	"cbc(twofish)", "rfc3686(ctr(aes))"
};
char *ae_list[] = {
#if 0
	/* not implemented */
	"rfc4106(gcm(aes))", "rfc4309(ccm(aes))", "rfc4543(gcm(aes))",
	"rfc7539esp(chacha20,poly1305)"
#endif
};

const unsigned int proto_plan = ARRAY_SIZE(ah_list) + ARRAY_SIZE(comp_list) \
				+ (ARRAY_SIZE(ah_list) * ARRAY_SIZE(e_list)) \
				+ ARRAY_SIZE(ae_list);

static int write_proto_plan(int fd, int proto)
{
	unsigned int i;

	switch (proto) {
	case IPPROTO_AH:
		for (i = 0; i < ARRAY_SIZE(ah_list); i++) {
			if (write_desc(proto, fd, ah_list[i], 0, 0, 0))
				return -1;
		}
		break;
	case IPPROTO_COMP:
		for (i = 0; i < ARRAY_SIZE(comp_list); i++) {
			if (write_desc(proto, fd, 0, 0, comp_list[i], 0))
				return -1;
		}
		break;
	case IPPROTO_ESP:
		for (i = 0; i < ARRAY_SIZE(ah_list); i++) {
			int j;

			for (j = 0; j < ARRAY_SIZE(e_list); j++) {
				if (write_desc(proto, fd, ah_list[i],
							e_list[j], 0, 0))
					return -1;
			}
		}
		for (i = 0; i < ARRAY_SIZE(ae_list); i++) {
			if (write_desc(proto, fd, 0, 0, 0, ae_list[i]))
				return -1;
		}
		break;
	default:
		printk("BUG: Specified unknown proto %d", proto);
		return -1;
	}

	return 0;
}

/*
 * Some structures in xfrm uapi header differ in size between
 * 64-bit and 32-bit ABI:
 *
 *             32-bit UABI               |            64-bit UABI
 *  -------------------------------------|-------------------------------------
 *   sizeof(xfrm_usersa_info)     = 220  |  sizeof(xfrm_usersa_info)     = 224
 *   sizeof(xfrm_userpolicy_info) = 164  |  sizeof(xfrm_userpolicy_info) = 168
 *   sizeof(xfrm_userspi_info)    = 228  |  sizeof(xfrm_userspi_info)    = 232
 *   sizeof(xfrm_user_acquire)    = 276  |  sizeof(xfrm_user_acquire)    = 280
 *   sizeof(xfrm_user_expire)     = 224  |  sizeof(xfrm_user_expire)     = 232
 *   sizeof(xfrm_user_polexpire)  = 168  |  sizeof(xfrm_user_polexpire)  = 176
 *
 * Check the affected by the UABI difference structures.
 * Also, check translation for xfrm_set_spdinfo: it has it's own attributes
 * which needs to be correctly copied, but not translated.
 */
const unsigned int compat_plan = 5;
static int write_compat_struct_tests(int test_desc_fd)
{
	struct xfrm_desc desc = {};

	desc.type = ALLOCATE_SPI;
	desc.proto = IPPROTO_AH;
	strncpy(desc.a_algo, ah_list[0], ALGO_LEN - 1);

	if (__write_desc(test_desc_fd, &desc))
		return -1;

	desc.type = MONITOR_ACQUIRE;
	if (__write_desc(test_desc_fd, &desc))
		return -1;

	desc.type = EXPIRE_STATE;
	if (__write_desc(test_desc_fd, &desc))
		return -1;

	desc.type = EXPIRE_POLICY;
	if (__write_desc(test_desc_fd, &desc))
		return -1;

	desc.type = SPDINFO_ATTRS;
	if (__write_desc(test_desc_fd, &desc))
		return -1;

	return 0;
}

static int write_test_plan(int test_desc_fd)
{
	unsigned int i;
	pid_t child;

	child = fork();
	if (child < 0) {
		pr_err("fork()");
		return -1;
	}
	if (child) {
		if (close(test_desc_fd))
			printk("close(): %m");
		return 0;
	}

	if (write_compat_struct_tests(test_desc_fd))
		exit(KSFT_FAIL);

	for (i = 0; i < ARRAY_SIZE(proto_list); i++) {
		if (write_proto_plan(test_desc_fd, proto_list[i]))
			exit(KSFT_FAIL);
	}

	exit(KSFT_PASS);
}

static int children_cleanup(void)
{
	unsigned ret = KSFT_PASS;

	while (1) {
		int status;
		pid_t p = wait(&status);

		if ((p < 0) && errno == ECHILD)
			break;

		if (p < 0) {
			pr_err("wait()");
			return KSFT_FAIL;
		}

		if (!WIFEXITED(status)) {
			ret = KSFT_FAIL;
			continue;
		}

		if (WEXITSTATUS(status) == KSFT_FAIL)
			ret = KSFT_FAIL;
	}

	return ret;
}

typedef void (*print_res)(const char *, ...);

static int check_results(void)
{
	struct test_result tr = {};
	struct xfrm_desc *d = &tr.desc;
	int ret = KSFT_PASS;

	while (1) {
		ssize_t received = read(results_fd[0], &tr, sizeof(tr));
		print_res result;

		if (received == 0) /* EOF */
			break;

		if (received != sizeof(tr)) {
			pr_err("read() returned %zd", received);
			return KSFT_FAIL;
		}

		switch (tr.res) {
		case KSFT_PASS:
			result = ksft_test_result_pass;
			break;
		case KSFT_FAIL:
		default:
			result = ksft_test_result_fail;
			ret = KSFT_FAIL;
		}

		result(" %s: [%u, '%s', '%s', '%s', '%s', %u]\n",
		       desc_name[d->type], (unsigned int)d->proto, d->a_algo,
		       d->e_algo, d->c_algo, d->ae_algo, d->icv_len);
	}

	return ret;
}

int main(int argc, char **argv)
{
	unsigned int nr_process = 1;
	int route_sock = -1, ret = KSFT_SKIP;
	int test_desc_fd[2];
	uint32_t route_seq;
	unsigned int i;

	if (argc > 2)
		exit_usage(argv);

	if (argc > 1) {
		char *endptr;

		errno = 0;
		nr_process = strtol(argv[1], &endptr, 10);
		if ((errno == ERANGE && (nr_process == LONG_MAX || nr_process == LONG_MIN))
				|| (errno != 0 && nr_process == 0)
				|| (endptr == argv[1]) || (*endptr != '\0')) {
			printk("Failed to parse [nr_process]");
			exit_usage(argv);
		}

		if (nr_process > MAX_PROCESSES || !nr_process) {
			printk("nr_process should be between [1; %u]",
					MAX_PROCESSES);
			exit_usage(argv);
		}
	}

	srand(time(NULL));
	page_size = sysconf(_SC_PAGESIZE);
	if (page_size < 1)
		ksft_exit_skip("sysconf(): %m\n");

	if (pipe2(test_desc_fd, O_DIRECT) < 0)
		ksft_exit_skip("pipe(): %m\n");

	if (pipe2(results_fd, O_DIRECT) < 0)
		ksft_exit_skip("pipe(): %m\n");

	if (init_namespaces())
		ksft_exit_skip("Failed to create namespaces\n");

	if (netlink_sock(&route_sock, &route_seq, NETLINK_ROUTE))
		ksft_exit_skip("Failed to open netlink route socket\n");

	for (i = 0; i < nr_process; i++) {
		char veth[VETH_LEN];

		snprintf(veth, VETH_LEN, VETH_FMT, i);

		if (veth_add(route_sock, route_seq++, veth, nsfd_childa, veth, nsfd_childb)) {
			close(route_sock);
			ksft_exit_fail_msg("Failed to create veth device");
		}

		if (start_child(i, veth, test_desc_fd)) {
			close(route_sock);
			ksft_exit_fail_msg("Child %u failed to start", i);
		}
	}

	if (close(route_sock) || close(test_desc_fd[0]) || close(results_fd[1]))
		ksft_exit_fail_msg("close(): %m");

	ksft_set_plan(proto_plan + compat_plan);

	if (write_test_plan(test_desc_fd[1]))
		ksft_exit_fail_msg("Failed to write test plan to pipe");

	ret = check_results();

	if (children_cleanup() == KSFT_FAIL)
		exit(KSFT_FAIL);

	exit(ret);
}
