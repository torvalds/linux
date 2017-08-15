#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "cla.h"
#include "test.h"

#ifndef __MINGW32__
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#ifdef __ANDROID__
#include <linux/icmp.h>
#endif

#include <lkl.h>
#include <lkl_host.h>

#include <net/if.h>
#include <linux/if_tun.h>
#include <sys/ioctl.h>

enum {
	BACKEND_TAP,
	BACKEND_MACVTAP,
	BACKEND_RAW,
	BACKEND_DPDK,
	BACKEND_PIPE,
	BACKEND_NONE,
};

const char *backends[] = { "tap", "macvtap", "raw", "dpdk", "pipe", NULL };

static struct {
	int printk;
	int backend;
	const char *ifname;
	int dhcp, nmlen;
	unsigned int ip, dst, gateway;
} cla = {
	.backend = BACKEND_NONE,
	.ip = INADDR_NONE,
	.gateway = INADDR_NONE,
	.dst = INADDR_NONE,
};


struct cl_arg args[] = {
	{"printk", 'p', "show Linux printks", 0, CL_ARG_BOOL, &cla.printk},
	{"backend", 'b', "network backend type", 1, CL_ARG_STR_SET,
	 &cla.backend, backends},
	{"ifname", 'i', "interface name", 1, CL_ARG_STR, &cla.ifname},
	{"dhcp", 'd', "use dhcp to configure LKL", 0, CL_ARG_BOOL, &cla.dhcp},
	{"ip", 'I', "IPv4 address to use", 1, CL_ARG_IPV4, &cla.ip},
	{"netmask-len", 'n', "IPv4 netmask length", 1, CL_ARG_INT,
	 &cla.nmlen},
	{"gateway", 'g', "IPv4 gateway to use", 1, CL_ARG_IPV4, &cla.gateway},
	{"dst", 'd', "IPv4 destination address", 1, CL_ARG_IPV4, &cla.dst},
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

static int test_icmp(char *str, int len)
{
	int sock, ret;
	struct iphdr *iph;
	struct icmphdr *icmp;
	struct sockaddr_in saddr;
	struct lkl_pollfd pfd;
	char buf[32];

	memset(&saddr, 0, sizeof(saddr));
	saddr.sin_family = AF_INET;
	saddr.sin_addr.s_addr = cla.dst;

	str += snprintf(str, len, "%s ", inet_ntoa(saddr.sin_addr));

	sock = lkl_sys_socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
	if (sock < 0) {
		snprintf(str, len, "socket error (%s)", lkl_strerror(sock));
		return TEST_FAILURE;
	}

	icmp = malloc(sizeof(struct icmphdr *));
	icmp->type = ICMP_ECHO;
	icmp->code = 0;
	icmp->checksum = 0;
	icmp->un.echo.sequence = 0;
	icmp->un.echo.id = 0;
	icmp->checksum = in_cksum((u_short *)icmp, sizeof(*icmp), 0);

	ret = lkl_sys_sendto(sock, icmp, sizeof(*icmp), 0,
			     (struct lkl_sockaddr*)&saddr,
			     sizeof(saddr));
	if (ret < 0) {
		snprintf(str, len, "sendto error (%s)", lkl_strerror(ret));
		return TEST_FAILURE;
	}

	free(icmp);

	pfd.fd = sock;
	pfd.events = LKL_POLLIN;
	pfd.revents = 0;

	ret = lkl_sys_poll(&pfd, 1, 1000);
	if (ret < 0) {
		snprintf(str, len, "poll error (%s)", lkl_strerror(ret));
		return TEST_FAILURE;
	}

	ret = lkl_sys_recv(sock, buf, sizeof(buf), MSG_DONTWAIT);
	if (ret < 0) {
		snprintf(str, len, "recv error (%s)", lkl_strerror(ret));
		return TEST_FAILURE;
	}

	iph = (struct iphdr *)buf;
	icmp = (struct icmphdr *)(buf + iph->ihl * 4);
	/* DHCP server may issue an ICMP echo request to a dhcp client */
	if ((icmp->type != ICMP_ECHOREPLY || icmp->code != 0) &&
	    (icmp->type != ICMP_ECHO)) {
		snprintf(str, len, "no ICMP echo reply (type=%d, code=%d)",
			 icmp->type, icmp->code);
		return TEST_FAILURE;
	}

	return TEST_SUCCESS;
}

static int test_net_init(int argc, const char **argv)
{
	int ret, nd_id = -1, nd_ifindex = -1;
	struct lkl_netdev *nd = NULL;

	if (parse_args(argc, argv, args) < 0)
		return -1;

	if (cla.ip != INADDR_NONE && (cla.nmlen < 0 || cla.nmlen > 32)) {
		fprintf(stderr, "invalid netmask length %d\n", cla.nmlen);
		return -1;
	}

	switch (cla.backend) {
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
	}

	if (nd) {
		nd_id = lkl_netdev_add(nd, NULL);
		if (nd_id < 0) {
			fprintf(stderr, "failed to add netdev: %s\n",
				lkl_strerror(nd_id));
		}
	}

	if (!cla.printk)
		lkl_host_ops.print = NULL;

	ret = lkl_start_kernel(&lkl_host_ops, "%s", cla.dhcp ? "ip=dhcp" : "");
	if (ret) {
		fprintf(stderr, "can't start kernel: %s\n", lkl_strerror(ret));
		return -1;
	}

	/* lo iff_up */
	lkl_if_up(1);

	if (nd_id >= 0) {
		nd_ifindex = lkl_netdev_get_ifindex(nd_id);
		if (nd_ifindex > 0)
			lkl_if_up(nd_ifindex);
		else
			fprintf(stderr, "failed to get ifindex for netdev id %d: %s\n",
				nd_id, lkl_strerror(nd_ifindex));
	}

	if (nd_ifindex >= 0 && cla.ip != INADDR_NONE) {
		ret = lkl_if_set_ipv4(nd_ifindex, cla.ip, cla.nmlen);
		if (ret < 0)
			fprintf(stderr, "failed to set IPv4 address: %s\n",
				lkl_strerror(ret));
	}

	if (nd_ifindex >= 0 && cla.gateway != INADDR_NONE) {
		ret = lkl_set_ipv4_gateway(cla.gateway);
		if (ret < 0)
			fprintf(stderr, "failed to set IPv4 gateway: %s %x\n",
				lkl_strerror(ret), INADDR_NONE);
	}

	return 0;
}
#endif /*!  __MINGW32__ */

int main(int argc, const char **argv)
{
#ifndef __MINGW32__
	if (test_net_init(argc, argv) < 0)
		return -1;

	TEST(icmp);
#endif /* ! __MIGW32__ */
	return g_test_pass;
}
