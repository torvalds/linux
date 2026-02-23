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

#ifndef BIT
#define BIT(nr) (1UL << (nr))
#endif

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

#define UDP_TUNNEL_MAX_SEGMENTS BIT(7)

#define UDP_TUNNEL_OUTER_IPV4 (UDP_TUNNEL_GENEVE_4IN4 | UDP_TUNNEL_GENEVE_6IN4)
#define UDP_TUNNEL_INNER_IPV4 (UDP_TUNNEL_GENEVE_4IN4 | UDP_TUNNEL_GENEVE_4IN6)

#define UDP_TUNNEL_GENEVE_4IN4_HDRLEN                        \
	(ETH_HLEN + 2 * sizeof(struct iphdr) + GENEVE_HLEN + \
	 2 * sizeof(struct udphdr))
#define UDP_TUNNEL_GENEVE_6IN6_HDRLEN                          \
	(ETH_HLEN + 2 * sizeof(struct ipv6hdr) + GENEVE_HLEN + \
	 2 * sizeof(struct udphdr))
#define UDP_TUNNEL_GENEVE_4IN6_HDRLEN                               \
	(ETH_HLEN + sizeof(struct iphdr) + sizeof(struct ipv6hdr) + \
	 GENEVE_HLEN + 2 * sizeof(struct udphdr))
#define UDP_TUNNEL_GENEVE_6IN4_HDRLEN                               \
	(ETH_HLEN + sizeof(struct ipv6hdr) + sizeof(struct iphdr) + \
	 GENEVE_HLEN + 2 * sizeof(struct udphdr))

#define UDP_TUNNEL_HDRLEN(type)                                             \
	((type) == UDP_TUNNEL_GENEVE_4IN4 ? UDP_TUNNEL_GENEVE_4IN4_HDRLEN : \
	 (type) == UDP_TUNNEL_GENEVE_6IN6 ? UDP_TUNNEL_GENEVE_6IN6_HDRLEN : \
	 (type) == UDP_TUNNEL_GENEVE_4IN6 ? UDP_TUNNEL_GENEVE_4IN6_HDRLEN : \
	 (type) == UDP_TUNNEL_GENEVE_6IN4 ? UDP_TUNNEL_GENEVE_6IN4_HDRLEN : \
					    0)

#define UDP_TUNNEL_MSS(type) (ETH_DATA_LEN - UDP_TUNNEL_HDRLEN(type))
#define UDP_TUNNEL_MAX(type, is_tap) \
	(ETH_MAX_MTU - UDP_TUNNEL_HDRLEN(type) - ((is_tap) ? ETH_HLEN : 0))

#define TUN_VNET_TNL_SIZE sizeof(struct virtio_net_hdr_v1_hash_tunnel)
#define MAX_VNET_TUNNEL_PACKET_SZ                                       \
	(TUN_VNET_TNL_SIZE + ETH_HLEN + UDP_TUNNEL_GENEVE_6IN6_HDRLEN + \
	 ETH_MAX_MTU)

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

static int send_gso_udp_msg(int socket, struct sockaddr_storage *addr,
			    uint8_t *send_buf, int send_len, int gso_size)
{
	char control[CMSG_SPACE(sizeof(uint16_t))] = { 0 };
	int alen = sockaddr_len(addr->ss_family);
	struct msghdr msg = { 0 };
	struct iovec iov = { 0 };
	int ret;

	iov.iov_base = send_buf;
	iov.iov_len = send_len;

	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_name = addr;
	msg.msg_namelen = alen;

	if (gso_size > 0) {
		struct cmsghdr *cmsg;

		msg.msg_control = control;
		msg.msg_controllen = sizeof(control);

		cmsg = CMSG_FIRSTHDR(&msg);
		cmsg->cmsg_level = SOL_UDP;
		cmsg->cmsg_type = UDP_SEGMENT;
		cmsg->cmsg_len = CMSG_LEN(sizeof(uint16_t));
		*(uint16_t *)CMSG_DATA(cmsg) = gso_size;
	}

	ret = sendmsg(socket, &msg, 0);
	if (ret < 0)
		perror("sendmsg");

	return ret;
}

static int validate_hdrlen(uint8_t **cur, int *len, int x)
{
	if (*len < x)
		return -1;
	*cur += x;
	*len -= x;
	return 0;
}

static int parse_udp_tunnel_vnet_packet(uint8_t *buf, int len, int tunnel_type,
					bool is_tap)
{
	struct ipv6hdr *iph6;
	struct udphdr *udph;
	struct iphdr *iph4;
	uint8_t *cur = buf;

	if (validate_hdrlen(&cur, &len, TUN_VNET_TNL_SIZE))
		return -1;

	if (is_tap) {
		if (validate_hdrlen(&cur, &len, ETH_HLEN))
			return -1;
	}

	if (tunnel_type & UDP_TUNNEL_OUTER_IPV4) {
		iph4 = (struct iphdr *)cur;
		if (validate_hdrlen(&cur, &len, sizeof(struct iphdr)))
			return -1;
		if (iph4->version != 4 || iph4->protocol != IPPROTO_UDP)
			return -1;
	} else {
		iph6 = (struct ipv6hdr *)cur;
		if (validate_hdrlen(&cur, &len, sizeof(struct ipv6hdr)))
			return -1;
		if (iph6->version != 6 || iph6->nexthdr != IPPROTO_UDP)
			return -1;
	}

	udph = (struct udphdr *)cur;
	if (validate_hdrlen(&cur, &len, sizeof(struct udphdr)))
		return -1;
	if (ntohs(udph->dest) != VN_PORT)
		return -1;

	if (validate_hdrlen(&cur, &len, GENEVE_HLEN))
		return -1;
	if (validate_hdrlen(&cur, &len, ETH_HLEN))
		return -1;

	if (tunnel_type & UDP_TUNNEL_INNER_IPV4) {
		iph4 = (struct iphdr *)cur;
		if (validate_hdrlen(&cur, &len, sizeof(struct iphdr)))
			return -1;
		if (iph4->version != 4 || iph4->protocol != IPPROTO_UDP)
			return -1;
	} else {
		iph6 = (struct ipv6hdr *)cur;
		if (validate_hdrlen(&cur, &len, sizeof(struct ipv6hdr)))
			return -1;
		if (iph6->version != 6 || iph6->nexthdr != IPPROTO_UDP)
			return -1;
	}

	udph = (struct udphdr *)cur;
	if (validate_hdrlen(&cur, &len, sizeof(struct udphdr)))
		return -1;
	if (ntohs(udph->dest) != UDP_DST_PORT)
		return -1;

	return len;
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
	int gso_size;
	int data_size;
	int r_num_mss;
	bool is_tap, no_gso;
};

/* clang-format off */
#define TUN_VNET_UDPTNL_VARIANT_ADD(type, desc)                              \
	FIXTURE_VARIANT_ADD(tun_vnet_udptnl, desc##_nogsosz_1byte) {         \
		/* no GSO: send a single byte */                             \
		.tunnel_type = type,                                         \
		.data_size = 1,                                              \
		.r_num_mss = 1,                                              \
		.is_tap = true,                                              \
		.no_gso = true,                                              \
	};                                                                   \
	FIXTURE_VARIANT_ADD(tun_vnet_udptnl, desc##_nogsosz_1mss) {          \
		/* no GSO: send a single MSS, fall back to no GSO */         \
		.tunnel_type = type,                                         \
		.data_size = UDP_TUNNEL_MSS(type),                           \
		.r_num_mss = 1,                                              \
		.is_tap = true,                                              \
		.no_gso = true,                                              \
	};                                                                   \
	FIXTURE_VARIANT_ADD(tun_vnet_udptnl, desc##_nogsosz_gtmss) {         \
		/* no GSO: send a single MSS + 1B: fail */                   \
		.tunnel_type = type,                                         \
		.data_size = UDP_TUNNEL_MSS(type) + 1,                       \
		.r_num_mss = 1,                                              \
		.is_tap = true,                                              \
		.no_gso = true,                                              \
	};                                                                   \
	FIXTURE_VARIANT_ADD(tun_vnet_udptnl, desc##_1byte) {                 \
		/* GSO: send 1 byte, gso 1 byte, fall back to no GSO */      \
		.tunnel_type = type,                                         \
		.gso_size = 1,                                               \
		.data_size = 1,                                              \
		.r_num_mss = 1,                                              \
		.is_tap = true,                                              \
		.no_gso = true,                                              \
	};                                                                   \
	FIXTURE_VARIANT_ADD(tun_vnet_udptnl, desc##_1mss) {                  \
		/* send a single MSS: fall back to no GSO */                 \
		.tunnel_type = type,                                         \
		.gso_size = UDP_TUNNEL_MSS(type),                            \
		.data_size = UDP_TUNNEL_MSS(type),                           \
		.r_num_mss = 1,                                              \
		.is_tap = true,                                              \
		.no_gso = true,                                              \
	};                                                                   \
	FIXTURE_VARIANT_ADD(tun_vnet_udptnl, desc##_ltgso) {                 \
		/* data <= MSS < gso: will fall back to no GSO */            \
		.tunnel_type = type,                                         \
		.gso_size = UDP_TUNNEL_MSS(type) + 1,                        \
		.data_size = UDP_TUNNEL_MSS(type),                           \
		.r_num_mss = 1,                                              \
		.is_tap = true,                                              \
		.no_gso = true,                                              \
	};                                                                   \
	FIXTURE_VARIANT_ADD(tun_vnet_udptnl, desc##_gtgso) {                 \
		/* GSO: a single MSS + 1B */                                 \
		.tunnel_type = type,                                         \
		.gso_size = UDP_TUNNEL_MSS(type),                            \
		.data_size = UDP_TUNNEL_MSS(type) + 1,                       \
		.r_num_mss = 2,                                              \
		.is_tap = true,                                              \
	};                                                                   \
	FIXTURE_VARIANT_ADD(tun_vnet_udptnl, desc##_2mss) {                  \
		/* no GSO: send exactly 2 MSS */                             \
		.tunnel_type = type,                                         \
		.gso_size = UDP_TUNNEL_MSS(type),                            \
		.data_size = UDP_TUNNEL_MSS(type) * 2,                       \
		.r_num_mss = 2,                                              \
		.is_tap = true,                                              \
	};                                                                   \
	FIXTURE_VARIANT_ADD(tun_vnet_udptnl, desc##_maxbytes) {              \
		/* GSO: send max bytes */                                    \
		.tunnel_type = type,                                         \
		.gso_size = UDP_TUNNEL_MSS(type),                            \
		.data_size = UDP_TUNNEL_MAX(type, true),                     \
		.r_num_mss = UDP_TUNNEL_MAX(type, true) /                    \
			     UDP_TUNNEL_MSS(type) + 1,                       \
		.is_tap = true,                                              \
	};                                                                   \
	FIXTURE_VARIANT_ADD(tun_vnet_udptnl, desc##_over_maxbytes) {         \
		/* GSO: send oversize max bytes: fail */                     \
		.tunnel_type = type,                                         \
		.gso_size = UDP_TUNNEL_MSS(type),                            \
		.data_size = ETH_MAX_MTU,                                    \
		.r_num_mss = ETH_MAX_MTU / UDP_TUNNEL_MSS(type) + 1,         \
		.is_tap = true,                                              \
	};                                                                   \
	FIXTURE_VARIANT_ADD(tun_vnet_udptnl, desc##_maxsegs) {               \
		/* GSO: send max number of min sized segments */             \
		.tunnel_type = type,                                         \
		.gso_size = 1,                                               \
		.data_size = UDP_TUNNEL_MAX_SEGMENTS,                        \
		.r_num_mss = UDP_TUNNEL_MAX_SEGMENTS,                        \
		.is_tap = true,                                              \
	};                                                                   \
	FIXTURE_VARIANT_ADD(tun_vnet_udptnl, desc##_5byte) {                 \
		/* GSO: send 5 bytes, gso 2 bytes */                         \
		.tunnel_type = type,                                         \
		.gso_size = 2,                                               \
		.data_size = 5,                                              \
		.r_num_mss = 3,                                              \
		.is_tap = true,                                              \
	} /* clang-format on */

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

static int build_gso_packet_into_tun(const FIXTURE_VARIANT(tun_vnet_udptnl) *
					     variant,
				     uint8_t *buf)
{
	int pktlen, hlen, proto, inner_family, outer_family;
	int tunnel_type = variant->tunnel_type;
	int payload_len = variant->data_size;
	int gso_size = variant->gso_size;
	uint8_t *outer_udph, *cur = buf;
	void *sip, *dip, *smac, *dmac;
	bool is_tap = variant->is_tap;

	hlen = (is_tap ? ETH_HLEN : 0) + UDP_TUNNEL_HDRLEN(tunnel_type);
	inner_family = (tunnel_type & UDP_TUNNEL_INNER_IPV4) ? AF_INET :
							       AF_INET6;
	outer_family = (tunnel_type & UDP_TUNNEL_OUTER_IPV4) ? AF_INET :
							       AF_INET6;

	cur += build_virtio_net_hdr_v1_hash_tunnel(cur, is_tap, hlen, gso_size,
						   outer_family, inner_family);

	pktlen = hlen + payload_len;
	assign_ifaddr_vars(outer_family, 1, &sip, &dip, &smac, &dmac);

	if (is_tap) {
		proto = outer_family == AF_INET ? ETH_P_IP : ETH_P_IPV6;
		pktlen -= ETH_HLEN;
		cur += build_eth(cur, proto, dmac, smac);
	}

	if (outer_family == AF_INET) {
		pktlen = pktlen - sizeof(struct iphdr);
		cur += build_ipv4_header(cur, IPPROTO_UDP, pktlen, dip, sip);
	} else {
		pktlen = pktlen - sizeof(struct ipv6hdr);
		cur += build_ipv6_header(cur, IPPROTO_UDP, 0, pktlen, dip, sip);
	}

	outer_udph = cur;
	assign_ifaddr_vars(inner_family, 0, &sip, &dip, &smac, &dmac);

	pktlen -= sizeof(struct udphdr);
	proto = inner_family == AF_INET ? ETH_P_IP : ETH_P_IPV6;
	cur += build_udp_header(cur, UDP_SRC_PORT, VN_PORT, pktlen);
	cur += build_geneve_header(cur, VN_ID);
	cur += build_eth(cur, proto, dmac, smac);

	pktlen = sizeof(struct udphdr) + payload_len;
	if (inner_family == AF_INET)
		cur += build_ipv4_header(cur, IPPROTO_UDP, pktlen, dip, sip);
	else
		cur += build_ipv6_header(cur, IPPROTO_UDP, 0, pktlen, dip, sip);

	cur += build_udp_packet(cur, UDP_DST_PORT, UDP_SRC_PORT, payload_len,
				inner_family, false);

	build_udp_packet_csum(outer_udph, outer_family, false);

	return cur - buf;
}

static int
receive_gso_packet_from_tunnel(FIXTURE_DATA(tun_vnet_udptnl) * self,
			       const FIXTURE_VARIANT(tun_vnet_udptnl) * variant,
			       int *r_num_mss)
{
	uint8_t packet_buf[MAX_VNET_TUNNEL_PACKET_SZ];
	int len, total_len = 0, socket = self->sock;
	int payload_len = variant->data_size;

	while (total_len < payload_len) {
		len = recv(socket, packet_buf, sizeof(packet_buf), 0);
		if (len <= 0) {
			if (len < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
				perror("recv");
			break;
		}

		(*r_num_mss)++;
		total_len += len;
	}

	return total_len;
}

static int send_gso_packet_into_tunnel(FIXTURE_DATA(tun_vnet_udptnl) * self,
				       const FIXTURE_VARIANT(tun_vnet_udptnl) *
					       variant)
{
	int family = (variant->tunnel_type & UDP_TUNNEL_INNER_IPV4) ? AF_INET :
								      AF_INET6;
	uint8_t buf[MAX_VNET_TUNNEL_PACKET_SZ] = { 0 };
	int payload_len = variant->data_size;
	int gso_size = variant->gso_size;
	struct sockaddr_storage ssa, dsa;

	assign_sockaddr_vars(family, 0, &ssa, &dsa);
	return send_gso_udp_msg(self->sock, &dsa, buf, payload_len, gso_size);
}

static int
receive_gso_packet_from_tun(FIXTURE_DATA(tun_vnet_udptnl) * self,
			    const FIXTURE_VARIANT(tun_vnet_udptnl) * variant,
			    struct virtio_net_hdr_v1_hash_tunnel *vnet_hdr)
{
	struct timeval timeout = { .tv_sec = TIMEOUT_SEC };
	uint8_t buf[MAX_VNET_TUNNEL_PACKET_SZ];
	int tunnel_type = variant->tunnel_type;
	int payload_len = variant->data_size;
	bool is_tap = variant->is_tap;
	int ret, len, total_len = 0;
	int tun_fd = self->fd;
	fd_set fdset;

	while (total_len < payload_len) {
		FD_ZERO(&fdset);
		FD_SET(tun_fd, &fdset);

		ret = select(tun_fd + 1, &fdset, NULL, NULL, &timeout);
		if (ret <= 0) {
			perror("select");
			break;
		}
		if (!FD_ISSET(tun_fd, &fdset))
			continue;

		len = read(tun_fd, buf, sizeof(buf));
		if (len <= 0) {
			if (len < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
				perror("read");
			break;
		}

		len = parse_udp_tunnel_vnet_packet(buf, len, tunnel_type,
						   is_tap);
		if (len < 0)
			continue;

		if (total_len == 0)
			memcpy(vnet_hdr, buf, TUN_VNET_TNL_SIZE);

		total_len += len;
	}

	return total_len;
}

TEST_F(tun_vnet_udptnl, send_gso_packet)
{
	uint8_t pkt[MAX_VNET_TUNNEL_PACKET_SZ];
	int r_num_mss = 0;
	int ret, off;

	memset(pkt, 0, sizeof(pkt));
	off = build_gso_packet_into_tun(variant, pkt);
	ret = write(self->fd, pkt, off);
	ASSERT_EQ(ret, off);

	ret = receive_gso_packet_from_tunnel(self, variant, &r_num_mss);
	ASSERT_EQ(ret, variant->data_size);
	ASSERT_EQ(r_num_mss, variant->r_num_mss);
}

TEST_F(tun_vnet_udptnl, recv_gso_packet)
{
	struct virtio_net_hdr_v1_hash_tunnel vnet_hdr = { 0 };
	struct virtio_net_hdr_v1 *vh = &vnet_hdr.hash_hdr.hdr;
	int ret, gso_type = VIRTIO_NET_HDR_GSO_UDP_L4;

	ret = send_gso_packet_into_tunnel(self, variant);
	ASSERT_EQ(ret, variant->data_size);

	memset(&vnet_hdr, 0, sizeof(vnet_hdr));
	ret = receive_gso_packet_from_tun(self, variant, &vnet_hdr);
	ASSERT_EQ(ret, variant->data_size);

	if (!variant->no_gso) {
		ASSERT_EQ(vh->gso_size, variant->gso_size);
		gso_type |= (variant->tunnel_type & UDP_TUNNEL_OUTER_IPV4) ?
				    (VIRTIO_NET_HDR_GSO_UDP_TUNNEL_IPV4) :
				    (VIRTIO_NET_HDR_GSO_UDP_TUNNEL_IPV6);
		ASSERT_EQ(vh->gso_type, gso_type);
	}
}

XFAIL_ADD(tun_vnet_udptnl, 4in4_nogsosz_gtmss, recv_gso_packet);
XFAIL_ADD(tun_vnet_udptnl, 6in4_nogsosz_gtmss, recv_gso_packet);
XFAIL_ADD(tun_vnet_udptnl, 4in6_nogsosz_gtmss, recv_gso_packet);
XFAIL_ADD(tun_vnet_udptnl, 6in6_nogsosz_gtmss, recv_gso_packet);

XFAIL_ADD(tun_vnet_udptnl, 4in4_over_maxbytes, send_gso_packet);
XFAIL_ADD(tun_vnet_udptnl, 6in4_over_maxbytes, send_gso_packet);
XFAIL_ADD(tun_vnet_udptnl, 4in6_over_maxbytes, send_gso_packet);
XFAIL_ADD(tun_vnet_udptnl, 6in6_over_maxbytes, send_gso_packet);

XFAIL_ADD(tun_vnet_udptnl, 4in4_over_maxbytes, recv_gso_packet);
XFAIL_ADD(tun_vnet_udptnl, 6in4_over_maxbytes, recv_gso_packet);
XFAIL_ADD(tun_vnet_udptnl, 4in6_over_maxbytes, recv_gso_packet);
XFAIL_ADD(tun_vnet_udptnl, 6in6_over_maxbytes, recv_gso_packet);

TEST_HARNESS_MAIN
