// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/if_tun.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include "kselftest_harness.h"
#include "tuntap_helpers.h"

static const char param_dev_geneve_name[] = "geneve1";
static unsigned char param_hwaddr_outer_dst[] = { 0x00, 0xfe, 0x98,
						  0x14, 0x22, 0x42 };
static unsigned char param_hwaddr_outer_src[] = { 0x00, 0xfe, 0x98,
						  0x94, 0xd2, 0x43 };
static unsigned char param_hwaddr_inner_dst[] = { 0x00, 0xfe, 0x98,
						  0x94, 0x22, 0xcc };
static unsigned char param_hwaddr_inner_src[] = { 0x00, 0xfe, 0x98,
						  0x94, 0xd2, 0xdd };

static struct in_addr param_ipaddr4_outer_dst = {
	__constant_htonl(0xac100001),
};

static struct in_addr param_ipaddr4_outer_src = {
	__constant_htonl(0xac100002),
};

static struct in_addr param_ipaddr4_inner_dst = {
	__constant_htonl(0xac100101),
};

static struct in_addr param_ipaddr4_inner_src = {
	__constant_htonl(0xac100102),
};

static struct in6_addr param_ipaddr6_outer_dst = {
	{ { 0x20, 0x02, 0x0d, 0xb8, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 } },
};

static struct in6_addr param_ipaddr6_outer_src = {
	{ { 0x20, 0x02, 0x0d, 0xb8, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2 } },
};

static struct in6_addr param_ipaddr6_inner_dst = {
	{ { 0x20, 0x02, 0x0d, 0xb8, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 } },
};

static struct in6_addr param_ipaddr6_inner_src = {
	{ { 0x20, 0x02, 0x0d, 0xb8, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2 } },
};

#define VN_ID 1
#define VN_PORT 4789
#define UDP_SRC_PORT 22
#define UDP_DST_PORT 48878
#define IPPREFIX_LEN 24
#define IP6PREFIX_LEN 64
#define TIMEOUT_SEC 10
#define TIMEOUT_USEC 100000
#define MAX_RETRIES 20

#define UDP_TUNNEL_GENEVE_4IN4 0x01
#define UDP_TUNNEL_GENEVE_6IN4 0x02
#define UDP_TUNNEL_GENEVE_4IN6 0x04
#define UDP_TUNNEL_GENEVE_6IN6 0x08

#define UDP_TUNNEL_OUTER_IPV4 (UDP_TUNNEL_GENEVE_4IN4 | UDP_TUNNEL_GENEVE_6IN4)
#define UDP_TUNNEL_INNER_IPV4 (UDP_TUNNEL_GENEVE_4IN4 | UDP_TUNNEL_GENEVE_4IN6)

#define TUN_VNET_TNL_SIZE sizeof(struct virtio_net_hdr_v1_hash_tunnel)

struct geneve_setup_config {
	int family;
	union {
		struct in_addr r4;
		struct in6_addr r6;
	} remote;
	__be32 vnid;
	__be16 vnport;
	unsigned char hwaddr[6];
	uint8_t csum;
};

static int tun_attach(int fd, char *dev)
{
	struct ifreq ifr;

	memset(&ifr, 0, sizeof(ifr));
	strcpy(ifr.ifr_name, dev);
	ifr.ifr_flags = IFF_ATTACH_QUEUE;

	return ioctl(fd, TUNSETQUEUE, (void *)&ifr);
}

static int tun_detach(int fd, char *dev)
{
	struct ifreq ifr;

	memset(&ifr, 0, sizeof(ifr));
	strcpy(ifr.ifr_name, dev);
	ifr.ifr_flags = IFF_DETACH_QUEUE;

	return ioctl(fd, TUNSETQUEUE, (void *)&ifr);
}

static int tun_alloc(char *dev)
{
	struct ifreq ifr;
	int fd, err;

	fd = open("/dev/net/tun", O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "can't open tun: %s\n", strerror(errno));
		return fd;
	}

	memset(&ifr, 0, sizeof(ifr));
	strcpy(ifr.ifr_name, dev);
	ifr.ifr_flags = IFF_TAP | IFF_NAPI | IFF_MULTI_QUEUE;

	err = ioctl(fd, TUNSETIFF, (void *)&ifr);
	if (err < 0) {
		fprintf(stderr, "can't TUNSETIFF: %s\n", strerror(errno));
		close(fd);
		return err;
	}
	strcpy(dev, ifr.ifr_name);
	return fd;
}

static int tun_delete(char *dev)
{
	return ip_link_del(dev);
}

static int tun_open(char *dev, const int flags, const int hdrlen,
		    const int features, const unsigned char *mac_addr)
{
	struct ifreq ifr = { 0 };
	int fd, sk = -1;

	fd = open("/dev/net/tun", O_RDWR);
	if (fd < 0) {
		perror("open");
		return -1;
	}

	ifr.ifr_flags = flags;
	if (ioctl(fd, TUNSETIFF, (void *)&ifr) < 0) {
		perror("ioctl(TUNSETIFF)");
		goto err;
	}
	strcpy(dev, ifr.ifr_name);

	if (hdrlen > 0) {
		if (ioctl(fd, TUNSETVNETHDRSZ, &hdrlen) < 0) {
			perror("ioctl(TUNSETVNETHDRSZ)");
			goto err;
		}
	}

	if (features) {
		if (ioctl(fd, TUNSETOFFLOAD, features) < 0) {
			perror("ioctl(TUNSETOFFLOAD)");
			goto err;
		}
	}

	sk = socket(PF_INET, SOCK_DGRAM, 0);
	if (sk < 0) {
		perror("socket");
		goto err;
	}

	if (ioctl(sk, SIOCGIFFLAGS, &ifr) < 0) {
		perror("ioctl(SIOCGIFFLAGS)");
		goto err;
	}

	ifr.ifr_flags |= (IFF_UP | IFF_RUNNING);
	if (ioctl(sk, SIOCSIFFLAGS, &ifr) < 0) {
		perror("ioctl(SIOCSIFFLAGS)");
		goto err;
	}

	if (mac_addr && flags & IFF_TAP) {
		ifr.ifr_hwaddr.sa_family = ARPHRD_ETHER;
		memcpy(ifr.ifr_hwaddr.sa_data, mac_addr, ETH_ALEN);

		if (ioctl(sk, SIOCSIFHWADDR, &ifr) < 0) {
			perror("ioctl(SIOCSIFHWADDR)");
			goto err;
		}
	}

out:
	if (sk >= 0)
		close(sk);
	return fd;

err:
	close(fd);
	fd = -1;
	goto out;
}

static size_t sockaddr_len(int family)
{
	return (family == AF_INET) ? sizeof(struct sockaddr_in) :
				     sizeof(struct sockaddr_in6);
}

static int geneve_fill_newlink(struct rt_link_newlink_req *req, void *data)
{
	struct geneve_setup_config *cfg = data;

#define SET_GENEVE_REMOTE rt_link_newlink_req_set_linkinfo_data_geneve_remote
#define SET_GENEVE_REMOTE6 rt_link_newlink_req_set_linkinfo_data_geneve_remote6

	rt_link_newlink_req_set_address(req, cfg->hwaddr, ETH_ALEN);
	rt_link_newlink_req_set_linkinfo_data_geneve_id(req, cfg->vnid);
	rt_link_newlink_req_set_linkinfo_data_geneve_port(req, cfg->vnport);
	rt_link_newlink_req_set_linkinfo_data_geneve_udp_csum(req, cfg->csum);

	if (cfg->family == AF_INET)
		SET_GENEVE_REMOTE(req, cfg->remote.r4.s_addr);
	else
		SET_GENEVE_REMOTE6(req, &cfg->remote.r6,
				   sizeof(cfg->remote.r6));

	return 0;
}

static int geneve_create(const char *dev, int family, void *remote,
			 void *hwaddr)
{
	struct geneve_setup_config geneve;

	memset(&geneve, 0, sizeof(geneve));
	geneve.vnid = VN_ID;
	geneve.vnport = htons(VN_PORT);
	geneve.csum = 1;
	geneve.family = family;
	if (family == AF_INET)
		memcpy(&geneve.remote.r4, remote, sizeof(struct in_addr));
	else
		memcpy(&geneve.remote.r6, remote, sizeof(struct in6_addr));
	memcpy(geneve.hwaddr, hwaddr, ETH_ALEN);

	return ip_link_add(dev, "geneve", geneve_fill_newlink, (void *)&geneve);
}

static int set_pmtu_discover(int fd, bool is_ipv4)
{
	int level, name, val;

	if (is_ipv4) {
		level = SOL_IP;
		name = IP_MTU_DISCOVER;
		val = IP_PMTUDISC_DO;
	} else {
		level = SOL_IPV6;
		name = IPV6_MTU_DISCOVER;
		val = IPV6_PMTUDISC_DO;
	}

	return setsockopt(fd, level, name, &val, sizeof(val));
}

static int udp_socket_open(struct sockaddr_storage *ssa, bool do_frag,
			   bool do_connect, struct sockaddr_storage *dsa)
{
	struct timeval to = { .tv_sec = TIMEOUT_SEC };
	int fd, family = ssa->ss_family;
	int salen = sockaddr_len(family);

	fd = socket(family, SOCK_DGRAM, 0);
	if (fd < 0)
		return -1;

	if (bind(fd, (struct sockaddr *)ssa, salen) < 0) {
		perror("bind");
		goto err;
	}

	if (do_connect && connect(fd, (struct sockaddr *)dsa, salen) < 0) {
		perror("connect");
		goto err;
	}

	if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof(to)) < 0) {
		perror("setsockopt(SO_RCVTIMEO)");
		goto err;
	}

	if (!do_frag && set_pmtu_discover(fd, family == AF_INET) < 0) {
		perror("set_pmtu_discover");
		goto err;
	}
	return fd;

err:
	close(fd);
	return -1;
}

static void parse_route_rsp(struct rt_route_getroute_rsp *rsp, void *rtm_type)
{
	*(uint8_t *)rtm_type = rsp->_hdr.rtm_type;
}

static int ip_route_check(const char *intf, int family, void *addr)
{
	uint8_t rtm_type, table = RT_TABLE_LOCAL;
	int retries = MAX_RETRIES;

	while (retries-- > 0) {
		if (ip_route_get(intf, family, table, addr, parse_route_rsp,
				 &rtm_type) == 0 &&
		    rtm_type == RTN_LOCAL)
			break;

		usleep(TIMEOUT_USEC);
	}

	if (retries < 0)
		return -1;

	return 0;
}

FIXTURE(tun)
{
	char ifname[IFNAMSIZ];
	int fd, fd2;
};

FIXTURE_SETUP(tun)
{
	memset(self->ifname, 0, sizeof(self->ifname));

	self->fd = tun_alloc(self->ifname);
	ASSERT_GE(self->fd, 0);

	self->fd2 = tun_alloc(self->ifname);
	ASSERT_GE(self->fd2, 0);
}

FIXTURE_TEARDOWN(tun)
{
	if (self->fd >= 0)
		close(self->fd);
	if (self->fd2 >= 0)
		close(self->fd2);
}

TEST_F(tun, delete_detach_close)
{
	EXPECT_EQ(tun_delete(self->ifname), 0);
	EXPECT_EQ(tun_detach(self->fd, self->ifname), -1);
	EXPECT_EQ(errno, 22);
}

TEST_F(tun, detach_delete_close)
{
	EXPECT_EQ(tun_detach(self->fd, self->ifname), 0);
	EXPECT_EQ(tun_delete(self->ifname), 0);
}

TEST_F(tun, detach_close_delete)
{
	EXPECT_EQ(tun_detach(self->fd, self->ifname), 0);
	close(self->fd);
	self->fd = -1;
	EXPECT_EQ(tun_delete(self->ifname), 0);
}

TEST_F(tun, reattach_delete_close)
{
	EXPECT_EQ(tun_detach(self->fd, self->ifname), 0);
	EXPECT_EQ(tun_attach(self->fd, self->ifname), 0);
	EXPECT_EQ(tun_delete(self->ifname), 0);
}

TEST_F(tun, reattach_close_delete)
{
	EXPECT_EQ(tun_detach(self->fd, self->ifname), 0);
	EXPECT_EQ(tun_attach(self->fd, self->ifname), 0);
	close(self->fd);
	self->fd = -1;
	EXPECT_EQ(tun_delete(self->ifname), 0);
}

FIXTURE(tun_vnet_udptnl)
{
	char ifname[IFNAMSIZ];
	int fd, sock;
};

FIXTURE_VARIANT(tun_vnet_udptnl)
{
	int tunnel_type;
	bool is_tap;
};

/* clang-format off */
#define TUN_VNET_UDPTNL_VARIANT_ADD(type, desc)                              \
	FIXTURE_VARIANT_ADD(tun_vnet_udptnl, desc##udptnl) {                 \
		.tunnel_type = type,                                         \
		.is_tap = true,                                              \
	}
/* clang-format on */

TUN_VNET_UDPTNL_VARIANT_ADD(UDP_TUNNEL_GENEVE_4IN4, 4in4);
TUN_VNET_UDPTNL_VARIANT_ADD(UDP_TUNNEL_GENEVE_6IN4, 6in4);
TUN_VNET_UDPTNL_VARIANT_ADD(UDP_TUNNEL_GENEVE_4IN6, 4in6);
TUN_VNET_UDPTNL_VARIANT_ADD(UDP_TUNNEL_GENEVE_6IN6, 6in6);

static void assign_ifaddr_vars(int family, int is_outer, void **srcip,
			       void **dstip, void **srcmac, void **dstmac)
{
	if (is_outer) {
		if (family == AF_INET) {
			*srcip = (void *)&param_ipaddr4_outer_src;
			*dstip = (void *)&param_ipaddr4_outer_dst;
		} else {
			*srcip = (void *)&param_ipaddr6_outer_src;
			*dstip = (void *)&param_ipaddr6_outer_dst;
		}
		*srcmac = param_hwaddr_outer_src;
		*dstmac = param_hwaddr_outer_dst;
	} else {
		if (family == AF_INET) {
			*srcip = (void *)&param_ipaddr4_inner_src;
			*dstip = (void *)&param_ipaddr4_inner_dst;
		} else {
			*srcip = (void *)&param_ipaddr6_inner_src;
			*dstip = (void *)&param_ipaddr6_inner_dst;
		}
		*srcmac = param_hwaddr_inner_src;
		*dstmac = param_hwaddr_inner_dst;
	}
}

static void assign_sockaddr_vars(int family, int is_outer,
				 struct sockaddr_storage *src,
				 struct sockaddr_storage *dst)
{
	src->ss_family = family;
	dst->ss_family = family;

	if (family == AF_INET) {
		struct sockaddr_in *s4 = (struct sockaddr_in *)src;
		struct sockaddr_in *d4 = (struct sockaddr_in *)dst;

		s4->sin_addr = is_outer ? param_ipaddr4_outer_src :
					  param_ipaddr4_inner_src;
		d4->sin_addr = is_outer ? param_ipaddr4_outer_dst :
					  param_ipaddr4_inner_dst;
		if (!is_outer) {
			s4->sin_port = htons(UDP_SRC_PORT);
			d4->sin_port = htons(UDP_DST_PORT);
		}
	} else {
		struct sockaddr_in6 *s6 = (struct sockaddr_in6 *)src;
		struct sockaddr_in6 *d6 = (struct sockaddr_in6 *)dst;

		s6->sin6_addr = is_outer ? param_ipaddr6_outer_src :
					   param_ipaddr6_inner_src;
		d6->sin6_addr = is_outer ? param_ipaddr6_outer_dst :
					   param_ipaddr6_inner_dst;
		if (!is_outer) {
			s6->sin6_port = htons(UDP_SRC_PORT);
			d6->sin6_port = htons(UDP_DST_PORT);
		}
	}
}

FIXTURE_SETUP(tun_vnet_udptnl)
{
	int ret, family, prefix, flags, features;
	int tunnel_type = variant->tunnel_type;
	struct sockaddr_storage ssa, dsa;
	void *sip, *dip, *smac, *dmac;

	flags = (variant->is_tap ? IFF_TAP : IFF_TUN) | IFF_VNET_HDR |
		IFF_MULTI_QUEUE | IFF_NO_PI;
	features = TUN_F_CSUM | TUN_F_UDP_TUNNEL_GSO |
		   TUN_F_UDP_TUNNEL_GSO_CSUM | TUN_F_USO4 | TUN_F_USO6;
	self->fd = tun_open(self->ifname, flags, TUN_VNET_TNL_SIZE, features,
			    param_hwaddr_outer_src);
	ASSERT_GE(self->fd, 0);

	family = (tunnel_type & UDP_TUNNEL_OUTER_IPV4) ? AF_INET : AF_INET6;
	prefix = (family == AF_INET) ? IPPREFIX_LEN : IP6PREFIX_LEN;
	assign_ifaddr_vars(family, 1, &sip, &dip, &smac, &dmac);

	ret = ip_addr_add(self->ifname, family, sip, prefix);
	ASSERT_EQ(ret, 0);
	ret = ip_neigh_add(self->ifname, family, dip, dmac);
	ASSERT_EQ(ret, 0);
	ret = ip_route_check(self->ifname, family, sip);
	ASSERT_EQ(ret, 0);

	ret = geneve_create(param_dev_geneve_name, family, dip,
			    param_hwaddr_inner_src);
	ASSERT_EQ(ret, 0);

	family = (tunnel_type & UDP_TUNNEL_INNER_IPV4) ? AF_INET : AF_INET6;
	prefix = (family == AF_INET) ? IPPREFIX_LEN : IP6PREFIX_LEN;
	assign_ifaddr_vars(family, 0, &sip, &dip, &smac, &dmac);

	ret = ip_addr_add(param_dev_geneve_name, family, sip, prefix);
	ASSERT_EQ(ret, 0);
	ret = ip_neigh_add(param_dev_geneve_name, family, dip, dmac);
	ASSERT_EQ(ret, 0);
	ret = ip_route_check(param_dev_geneve_name, family, sip);
	ASSERT_EQ(ret, 0);

	assign_sockaddr_vars(family, 0, &ssa, &dsa);
	self->sock = udp_socket_open(&ssa, false, true, &dsa);
	ASSERT_GE(self->sock, 0);
}

FIXTURE_TEARDOWN(tun_vnet_udptnl)
{
	int ret;

	if (self->sock != -1)
		close(self->sock);

	ret = ip_link_del(param_dev_geneve_name);
	EXPECT_EQ(ret, 0);

	ret = tun_delete(self->ifname);
	EXPECT_EQ(ret, 0);
}

TEST_F(tun_vnet_udptnl, basic)
{
	int ret;
	char cmd[256] = { 0 };

	sprintf(cmd, "ip addr show %s > /dev/null 2>&1", param_dev_geneve_name);
	ret = system(cmd);
	ASSERT_EQ(ret, 0);
}

TEST_HARNESS_MAIN
