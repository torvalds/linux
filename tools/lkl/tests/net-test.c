#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "test.h"

#ifndef __MINGW32__
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>

#include <lkl.h>
#include <lkl_host.h>

#include <net/if.h>
#include <linux/if_tun.h>
#include <sys/ioctl.h>

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

static char *dst;
static int test_icmp(char *str, int len)
{
	int sock, ret;
	struct iphdr *iph;
	struct icmphdr *icmp;
	struct sockaddr_in saddr;
	struct lkl_pollfd pfd;
	char buf[32];

	str += snprintf(str, len, "%s ", dst);

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

	memset(&saddr, 0, sizeof(saddr));
	saddr.sin_family = AF_INET;
	inet_aton(dst, &saddr.sin_addr);

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

static int test_net_init(int argc, char **argv)
{
	char *iftype, *ifname, *ip, *netmask_len;
	char *gateway = NULL;
	char *debug = getenv("LKL_DEBUG");
	int ret, nd_id = -1, nd_ifindex = -1;
	struct lkl_netdev *nd = NULL;
	char boot_cmdline[256] = "\0";

	if (argc < 4) {
		printf("usage %s <iftype: tap|dpdk|raw> <ifname> <dstaddr> <v4addr>|dhcp <v4mask> [gateway]\n", argv[0]);
		exit(0);
	}

	iftype = argv[1];
	ifname = argv[2];
	dst = argv[3];
	ip = argv[4];
	netmask_len = argv[5];

	if (argc == 7)
		gateway = argv[6];

	if (iftype && ifname && (strncmp(iftype, "tap", 3) == 0))
		nd = lkl_netdev_tap_create(ifname, 0);
#ifdef CONFIG_AUTO_LKL_VIRTIO_NET_DPDK
	else if (iftype && ifname && (strncmp(iftype, "dpdk", 4) == 0))
		nd = lkl_netdev_dpdk_create(ifname);
#endif /* CONFIG_AUTO_LKL_VIRTIO_NET_DPDK */
	else if (iftype && ifname && (strncmp(iftype, "raw", 3) == 0))
		nd = lkl_netdev_raw_create(ifname);
	else if (iftype && ifname && (strncmp(iftype, "macvtap", 7) == 0))
		nd = lkl_netdev_macvtap_create(ifname, 0);

	if (!nd) {
		fprintf(stderr, "init netdev failed\n");
		return -1;
	}

	ret = lkl_netdev_add(nd, NULL);
	if (ret < 0) {
		fprintf(stderr, "failed to add netdev: %s\n",
			lkl_strerror(ret));
	}
	nd_id = ret;

	if (!debug)
		lkl_host_ops.print = NULL;


	if ((ip && !strcmp(ip, "dhcp")) && (nd_id != -1))
		snprintf(boot_cmdline, sizeof(boot_cmdline), "ip=dhcp");

	ret = lkl_start_kernel(&lkl_host_ops, boot_cmdline);
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

	if (nd_ifindex >= 0 && ip && netmask_len) {
		unsigned int addr = inet_addr(ip);
		int nmlen = atoi(netmask_len);

		if (addr != INADDR_NONE && nmlen > 0 && nmlen < 32) {
			ret = lkl_if_set_ipv4(nd_ifindex, addr, nmlen);
			if (ret < 0)
				fprintf(stderr, "failed to set IPv4 address: %s\n",
					lkl_strerror(ret));
		}
	}

	if (nd_ifindex >= 0 && gateway) {
		unsigned int addr = inet_addr(gateway);

		if (addr != INADDR_NONE) {
			ret = lkl_set_ipv4_gateway(addr);
			if (ret < 0)
				fprintf(stderr, "failed to set IPv4 gateway: %s\n",
					lkl_strerror(ret));
		}
	}

	return 0;
}
#endif /*!  __MINGW32__ */

int main(int argc, char **argv)
{
#ifndef __MINGW32__
	if (test_net_init(argc, argv) < 0)
		return -1;

	TEST(icmp);
#endif /* ! __MIGW32__ */
	return g_test_pass;
}
