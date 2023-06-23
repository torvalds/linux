#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#ifdef __FreeBSD__
#include <sys/types.h>
#endif
#ifdef __MINGW32__
#include <winsock2.h>
#else
#ifdef __MSYS__
#include <cygwin/socket.h>
#endif
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#include <lkl.h>
#include <lkl_host.h>

#include "cla.h"
#include "test.h"

enum {
	BACKEND_TAP,
	BACKEND_MACVTAP,
	BACKEND_RAW,
	BACKEND_DPDK,
	BACKEND_PIPE,
	BACKEND_NONE,
	BACKEND_WINTAP,
};

const char *backends[] = { "tap", "macvtap", "raw", "dpdk", "pipe", "loopback",
	"wintap", NULL };
static struct {
	int backend;
	const char *ifname;
	int dhcp, nmlen;
	unsigned int ip, dst, gateway, sleep;
} cla = {
	.backend = BACKEND_NONE,
	.ip = INADDR_NONE,
	.gateway = INADDR_NONE,
	.dst = INADDR_NONE,
	.sleep = 0,
};


struct cl_arg args[] = {
	{"backend", 'b', "network backend type", 1, CL_ARG_STR_SET,
	 &cla.backend, backends},
	{"ifname", 'i', "interface name", 1, CL_ARG_STR, &cla.ifname},
	{"dhcp", 'd', "use dhcp to configure LKL", 0, CL_ARG_BOOL, &cla.dhcp},
	{"ip", 'I', "IPv4 address to use", 1, CL_ARG_IPV4, &cla.ip},
	{"netmask-len", 'n', "IPv4 netmask length", 1, CL_ARG_INT,
	 &cla.nmlen},
	{"gateway", 'g', "IPv4 gateway to use", 1, CL_ARG_IPV4, &cla.gateway},
	{"dst", 'D', "IPv4 destination address", 1, CL_ARG_IPV4, &cla.dst},
	{"sleep", 's', "sleep", 1, CL_ARG_INT, &cla.sleep},
	{0},
};

u_short
in_cksum(const u_short *addr, register int len, u_short csum)
{
	int nleft = len;
	const u_short *w = addr;
	u_short answer;
	int sum = csum;

	while (nleft > 1)  {
		sum += *w++;
		nleft -= 2;
	}

	if (nleft == 1)
		sum += htons(*(u_char *)w << 8);

	sum = (sum >> 16) + (sum & 0xffff);
	sum += (sum >> 16);
	answer = ~sum;
	return answer;
}

static int lkl_test_sleep(void)
{
	struct lkl_timespec ts = {
		.tv_sec = cla.sleep,
	};
	int ret;

	ret = lkl_sys_nanosleep((struct __lkl__kernel_timespec *)&ts, NULL);
	if (ret < 0) {
		lkl_test_logf("nanosleep error: %s\n", lkl_strerror(ret));
		return TEST_FAILURE;
	}

	return TEST_SUCCESS;
}

#define NUM_ICMP 10
static int lkl_test_icmp(void)
{
	int sock, ret, i;
	struct lkl_iphdr *iph;
	struct lkl_icmphdr *icmp;
	struct lkl_sockaddr_in saddr;
	struct lkl_pollfd pfd;
	char buf[32];

	if (cla.dst == INADDR_NONE)
		return TEST_SKIP;

	memset(&saddr, 0, sizeof(saddr));
	saddr.sin_family = AF_INET;
	saddr.sin_addr.lkl_s_addr = cla.dst;

	lkl_test_logf("pinging %s\n",
		      inet_ntoa(*(struct in_addr *)&saddr.sin_addr));

	sock = lkl_sys_socket(LKL_AF_INET, LKL_SOCK_RAW, LKL_IPPROTO_ICMP);
	if (sock < 0) {
		lkl_test_logf("socket error (%s)\n", lkl_strerror(sock));
		return TEST_FAILURE;
	}

	for (i = 0; i < NUM_ICMP; i++) {
		icmp = malloc(sizeof(struct lkl_icmphdr *));
		icmp->type = LKL_ICMP_ECHO;
		icmp->code = 0;
		icmp->checksum = 0;
		icmp->un.echo.sequence = htons(i);
		icmp->un.echo.id = 0;
		icmp->checksum = in_cksum((u_short *)icmp, sizeof(*icmp), 0);

		ret = lkl_sys_sendto(sock, icmp, sizeof(*icmp), 0,
				     (struct lkl_sockaddr *)&saddr,
				     sizeof(saddr));
		if (ret < 0) {
			lkl_test_logf("sendto error (%s)\n", lkl_strerror(ret));
			return TEST_FAILURE;
		}

		free(icmp);

		pfd.fd = sock;
		pfd.events = LKL_POLLIN;
		pfd.revents = 0;

		ret = lkl_sys_poll(&pfd, 1, 1000);
		if (ret < 0) {
			lkl_test_logf("poll error (%s)\n", lkl_strerror(ret));
			return TEST_FAILURE;
		}

		ret = lkl_sys_recv(sock, buf, sizeof(buf), LKL_MSG_DONTWAIT);
		if (ret < 0) {
			lkl_test_logf("recv error (%s)\n", lkl_strerror(ret));
			return TEST_FAILURE;
		}

		iph = (struct lkl_iphdr *)buf;
		icmp = (struct lkl_icmphdr *)(buf + iph->ihl * 4);
		/* DHCP server may issue an ICMP echo request to a dhcp client */
		if ((icmp->type != LKL_ICMP_ECHOREPLY || icmp->code != 0) &&
		    (icmp->type != LKL_ICMP_ECHO)) {
			lkl_test_logf("no ICMP echo reply (type=%d, code=%d)\n",
				      icmp->type, icmp->code);
			return TEST_FAILURE;
		}
		lkl_test_logf("ICMP echo reply (seq=%d)\n", ntohs(icmp->un.echo.sequence));

	}

	return TEST_SUCCESS;
}

static struct lkl_netdev *nd;

static int lkl_test_nd_create(void)
{
	switch (cla.backend) {
	case BACKEND_NONE:
		return TEST_SKIP;
	case BACKEND_TAP:
		nd = lkl_netdev_tap_create(cla.ifname, 0);
		break;
	case BACKEND_DPDK:
		nd = lkl_netdev_dpdk_create(cla.ifname, 0, NULL);
		break;
	case BACKEND_RAW:
		nd = lkl_netdev_raw_create(cla.ifname);
		break;
	case BACKEND_MACVTAP:
		nd = lkl_netdev_macvtap_create(cla.ifname, 0);
		break;
	case BACKEND_PIPE:
		nd = lkl_netdev_pipe_create(cla.ifname, 0);
		break;
	case BACKEND_WINTAP:
		nd = lkl_netdev_wintap_create(cla.ifname);
		break;
	}

	if (!nd) {
		lkl_test_logf("failed to create netdev\n");
		return TEST_BAILOUT;
	}

	return TEST_SUCCESS;
}

static int nd_id;

static int lkl_test_nd_add(void)
{
	if (cla.backend == BACKEND_NONE)
		return TEST_SKIP;

	struct lkl_netdev_args nd_args;
	__lkl__u8 mac[LKL_ETH_ALEN] = {0, 0x01, 0, 0, 0, 0xab};

	memset(&nd_args, 0, sizeof(struct lkl_netdev_args));
	nd_args.mac = mac;

	nd_id = lkl_netdev_add(nd, &nd_args);
	if (nd_id < 0) {
		lkl_test_logf("failed to add netdev: %s\n",
			      lkl_strerror(nd_id));
		return TEST_BAILOUT;
	}

	return TEST_SUCCESS;
}

static int lkl_test_nd_remove(void)
{
	if (cla.backend == BACKEND_NONE)
		return TEST_SKIP;

	lkl_netdev_remove(nd_id);
	lkl_netdev_free(nd);
	return TEST_SUCCESS;
}

LKL_TEST_CALL(start_kernel, lkl_start_kernel, 0,
	"mem=32M loglevel=8 %s", cla.dhcp ? "ip=dhcp" : "");
LKL_TEST_CALL(stop_kernel, lkl_sys_halt, 0);

static int nd_ifindex;

static int lkl_test_nd_ifindex(void)
{
	if (cla.backend == BACKEND_NONE)
		return TEST_SKIP;

	nd_ifindex = lkl_netdev_get_ifindex(nd_id);
	if (nd_ifindex < 0) {
		lkl_test_logf("failed to get ifindex for netdev id %d: %s\n",
			      nd_id, lkl_strerror(nd_ifindex));
		return TEST_BAILOUT;
	}

	return TEST_SUCCESS;
}

LKL_TEST_CALL(if_up, lkl_if_up, 0,
	      cla.backend == BACKEND_NONE ? 1 : nd_ifindex);

static int lkl_test_set_ipv4(void)
{
	int ret;

	if (cla.backend == BACKEND_NONE || cla.ip == LKL_INADDR_NONE)
		return TEST_SKIP;

	ret = lkl_if_set_ipv4(nd_ifindex, cla.ip, cla.nmlen);
	if (ret < 0) {
		lkl_test_logf("failed to set IPv4 address: %s\n",
			      lkl_strerror(ret));
		return TEST_BAILOUT;
	}

	return TEST_SUCCESS;
}

static int lkl_test_set_gateway(void)
{
	int ret;

	if (cla.backend == BACKEND_NONE || cla.gateway == LKL_INADDR_NONE)
		return TEST_SKIP;

	ret = lkl_set_ipv4_gateway(cla.gateway);
	if (ret < 0) {
		lkl_test_logf("failed to set IPv4 gateway: %s\n",
			      lkl_strerror(ret));
		return TEST_BAILOUT;
	}

	return TEST_SUCCESS;
}

struct lkl_test tests[] = {
	LKL_TEST(nd_create),
	LKL_TEST(nd_add),
	LKL_TEST(start_kernel),
	LKL_TEST(nd_ifindex),
	LKL_TEST(if_up),
	LKL_TEST(set_ipv4),
	LKL_TEST(set_gateway),
	LKL_TEST(sleep),
	LKL_TEST(icmp),
	LKL_TEST(nd_remove),
	LKL_TEST(stop_kernel),
};

int main(int argc, const char **argv)
{
	int ret;

	if (parse_args(argc, argv, args) < 0)
		return -1;

	if (cla.ip != LKL_INADDR_NONE && (cla.nmlen < 0 || cla.nmlen > 32)) {
		fprintf(stderr, "invalid netmask length %d\n", cla.nmlen);
		return -1;
	}

	lkl_host_ops.print = lkl_test_log;

	lkl_init(&lkl_host_ops);

	ret = lkl_test_run(tests, sizeof(tests)/sizeof(struct lkl_test),
			"net %s", backends[cla.backend]);

	lkl_cleanup();

	return ret;
}
