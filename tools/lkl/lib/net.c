#include <string.h>
#include <stdio.h>
#include "endian.h"
#include <lkl_host.h>

static inline void set_sockaddr(struct lkl_sockaddr_in *sin, unsigned int addr,
				unsigned short port)
{
	sin->sin_family = LKL_AF_INET;
	sin->sin_addr.lkl_s_addr = addr;
	sin->sin_port = port;
}

static inline int ifindex_to_name(int sock, struct lkl_ifreq *ifr, int ifindex)
{
	ifr->lkl_ifr_ifindex = ifindex;
	return lkl_sys_ioctl(sock, LKL_SIOCGIFNAME, (long)ifr);
}

int lkl_if_up(int ifindex)
{
	struct lkl_ifreq ifr;
	int err, sock = lkl_sys_socket(LKL_AF_INET, LKL_SOCK_DGRAM, 0);

	if (sock < 0)
		return sock;
	err = ifindex_to_name(sock, &ifr, ifindex);
	if (err < 0)
		return err;

	err = lkl_sys_ioctl(sock, LKL_SIOCGIFFLAGS, (long)&ifr);
	if (!err) {
		ifr.lkl_ifr_flags |= LKL_IFF_UP;
		err = lkl_sys_ioctl(sock, LKL_SIOCSIFFLAGS, (long)&ifr);
	}

	lkl_sys_close(sock);

	return err;
}

int lkl_if_down(int ifindex)
{
	struct lkl_ifreq ifr;
	int err, sock;

	sock = lkl_sys_socket(LKL_AF_INET, LKL_SOCK_DGRAM, 0);
	if (sock < 0)
		return sock;

	err = ifindex_to_name(sock, &ifr, ifindex);
	if (err < 0)
		return err;

	err = lkl_sys_ioctl(sock, LKL_SIOCGIFFLAGS, (long)&ifr);
	if (!err) {
		ifr.lkl_ifr_flags &= ~LKL_IFF_UP;
		err = lkl_sys_ioctl(sock, LKL_SIOCSIFFLAGS, (long)&ifr);
	}

	lkl_sys_close(sock);

	return err;
}

int lkl_if_set_mtu(int ifindex, int mtu)
{
	struct lkl_ifreq ifr;
	int err, sock;

	sock = lkl_sys_socket(LKL_AF_INET, LKL_SOCK_DGRAM, 0);
	if (sock < 0)
		return sock;

	err = ifindex_to_name(sock, &ifr, ifindex);
	if (err < 0)
		return err;

	ifr.lkl_ifr_mtu = mtu;

	err = lkl_sys_ioctl(sock, LKL_SIOCSIFMTU, (long)&ifr);

	lkl_sys_close(sock);

	return err;
}

int lkl_if_set_ipv4(int ifindex, unsigned int addr, unsigned int netmask_len)
{
	struct lkl_ifreq ifr;
	struct lkl_sockaddr_in *sin;
	int err, sock;


	sock = lkl_sys_socket(LKL_AF_INET, LKL_SOCK_DGRAM, 0);
	if (sock < 0)
		return sock;

	err = ifindex_to_name(sock, &ifr, ifindex);
	if (err < 0)
		return err;

	if (netmask_len >= 31)
		return -LKL_EINVAL;

	sin = (struct lkl_sockaddr_in *)&ifr.lkl_ifr_addr;
	set_sockaddr(sin, addr, 0);

	err = lkl_sys_ioctl(sock, LKL_SIOCSIFADDR, (long)&ifr);
	if (!err) {
		int netmask = (((1<<netmask_len)-1))<<(32-netmask_len);

		sin = (struct lkl_sockaddr_in *)&ifr.lkl_ifr_netmask;
		set_sockaddr(sin, htonl(netmask), 0);
		err = lkl_sys_ioctl(sock, LKL_SIOCSIFNETMASK, (long)&ifr);
		if (!err) {
			set_sockaddr(sin, htonl(ntohl(addr)|~netmask), 0);
			err = lkl_sys_ioctl(sock, LKL_SIOCSIFBRDADDR, (long)&ifr);
		}
	}

	lkl_sys_close(sock);

	return err;
}

int lkl_set_ipv4_gateway(unsigned int addr)
{
	struct lkl_rtentry re;
	int err, sock = lkl_sys_socket(LKL_AF_INET, LKL_SOCK_DGRAM, 0);

	if (sock < 0)
		return sock;

	memset(&re, 0, sizeof(re));
	set_sockaddr((struct lkl_sockaddr_in *) &re.rt_dst, 0, 0);
	set_sockaddr((struct lkl_sockaddr_in *) &re.rt_genmask, 0, 0);
	set_sockaddr((struct lkl_sockaddr_in *) &re.rt_gateway, addr, 0);
	re.rt_flags = LKL_RTF_UP | LKL_RTF_GATEWAY;
	err = lkl_sys_ioctl(sock, LKL_SIOCADDRT, (long)&re);
	lkl_sys_close(sock);

	return err;
}

int lkl_if_set_ipv6(int ifindex, void* addr, unsigned int netprefix_len)
{
	struct lkl_in6_ifreq ifr6;
	int err, sock;

	if (netprefix_len >= 128)
		return -LKL_EINVAL;

	sock = lkl_sys_socket(LKL_AF_INET6, LKL_SOCK_DGRAM, 0);
	if (sock < 0)
		return sock;

	memcpy(&ifr6.ifr6_addr.lkl_s6_addr, addr, sizeof(struct lkl_in6_addr));
	ifr6.ifr6_ifindex = ifindex;
	ifr6.ifr6_prefixlen = netprefix_len;

	err = lkl_sys_ioctl(sock, LKL_SIOCSIFADDR, (long)&ifr6);
	lkl_sys_close(sock);
	return err;
}

int lkl_set_ipv6_gateway(void* addr)
{
	int err, sock;
	struct lkl_in6_rtmsg route;

	sock = lkl_sys_socket(LKL_AF_INET6, LKL_SOCK_DGRAM, 0);
	if (sock < 0)
		return sock;
	memset(&route, 0, sizeof(route));
	memcpy(&route.rtmsg_gateway, addr, sizeof(struct lkl_in6_addr));
	route.rtmsg_flags = LKL_RTF_UP | LKL_RTF_GATEWAY;

	err = lkl_sys_ioctl(sock, LKL_SIOCADDRT, (long)&route);
	lkl_sys_close(sock);
	return err;
}

int lkl_netdev_get_ifindex(int id)
{
	struct lkl_ifreq ifr;
	int sock, ret;

	sock = lkl_sys_socket(LKL_AF_INET, LKL_SOCK_DGRAM, 0);
	if (sock < 0)
		return sock;

	snprintf(ifr.lkl_ifr_name, sizeof(ifr.lkl_ifr_name), "eth%d", id);
	ret = lkl_sys_ioctl(sock, LKL_SIOCGIFINDEX, (long)&ifr);
	lkl_sys_close(sock);

	return ret < 0 ? ret : ifr.lkl_ifr_ifindex;
}

// Copied from iproute2/lib/libnetlink.c
static unsigned int seq = 0;
static int rtnl_talk(int fd, struct lkl_nlmsghdr *n)
{
	int status;
	struct lkl_nlmsghdr *h;
	struct lkl_sockaddr_nl nladdr = {.nl_family = LKL_AF_NETLINK};
	struct lkl_iovec iov = {.iov_base = (void *)n, .iov_len = n->nlmsg_len};
	struct lkl_user_msghdr msg = {
			.msg_name = &nladdr,
			.msg_namelen = sizeof(nladdr),
			.msg_iov = &iov,
			.msg_iovlen = 1,
	};
	char buf[32768] = {};

	n->nlmsg_seq = seq;
	n->nlmsg_flags |= LKL_NLM_F_ACK;

	status = lkl_sys_sendmsg(fd, &msg, 0);
	if (status < 0) {
		lkl_perror("Cannot talk to rtnetlink", status);
		return -1;
	}

	iov.iov_base = buf;
	while (1) {
		iov.iov_len = sizeof(buf);
		status = lkl_sys_recvmsg(fd, &msg, 0);

		if (status < 0) {
			if (status == -LKL_EINTR || status == -LKL_EAGAIN)
				continue;
			lkl_perror("netlink receive error \n", status);
			return status;
		}
		if (status == 0) {
			fprintf(stderr, "EOF on netlink\n");
			return -1;
		}
		if (msg.msg_namelen != sizeof(nladdr)) {
			fprintf(stderr, "sender address length == %d\n",
				msg.msg_namelen);
			return -1;
		}
		for (h = (struct lkl_nlmsghdr *)buf; status >= (int)sizeof(*h);) {
			int len = h->nlmsg_len;
			int l = len - sizeof(*h);

			if (l < 0 || len > status) {
				if (msg.msg_flags & LKL_MSG_TRUNC) {
					fprintf(stderr, "Truncated message\n");
					return -1;
				}
				fprintf(stderr, "!!!malformed message: len=%d\n",
					len);
				return -1;
			}
			if (nladdr.nl_pid != 0 || h->nlmsg_seq != seq++) {
				/* Don't forget to skip that message. */
				status -= LKL_NLMSG_ALIGN(len);
				h = (struct lkl_nlmsghdr *)((char *)h +
					    LKL_NLMSG_ALIGN(len));
				continue;
			}

			if (h->nlmsg_type == LKL_NLMSG_ERROR) {
				struct lkl_nlmsgerr *err =
					(struct lkl_nlmsgerr *)LKL_NLMSG_DATA(h);
				if (l < (int)sizeof(struct lkl_nlmsgerr)) {
					fprintf(stderr, "ERROR truncated\n");
				} else if (!err->error) {
					return 0;
				}
				fprintf(stderr, "RTNETLINK answers: %s\n",
					strerror(-err->error));
				return -1;
			}

			fprintf(stderr, "Unexpected reply!!!\n");

			status -= LKL_NLMSG_ALIGN(len);
			h = (struct lkl_nlmsghdr *)((char *)h +
				    LKL_NLMSG_ALIGN(len));
		}

		if (msg.msg_flags & LKL_MSG_TRUNC) {
			fprintf(stderr, "Message truncated\n");
			continue;
		}

		if (status) {
			fprintf(stderr, "!!!Remnant of size %d\n", status);
			return -1;
		}
	}
}

int lkl_add_neighbor(int ifindex, int af, void* ip, void* mac)
{
	struct lkl_sockaddr_nl la;
	struct {
		struct lkl_nlmsghdr n;
		struct lkl_ndmsg r;
		char buf[1024];
	} req2;
	int err, addr_sz, fd;
	int ndmsglen = LKL_NLMSG_LENGTH(sizeof(struct lkl_ndmsg));
	struct lkl_rtattr *dstattr;

	if (af == LKL_AF_INET)
		addr_sz = 4;
	else if (af == LKL_AF_INET6)
		addr_sz = 16;
	else {
		fprintf(stderr, "Bad address family: %d\n", af);
		return -1;
	}
	fd = lkl_sys_socket(LKL_AF_NETLINK, LKL_SOCK_DGRAM, LKL_NETLINK_ROUTE);
	if (fd < 0)
		return fd;

	memset(&la, 0, sizeof(la));
	la.nl_family = LKL_AF_NETLINK;
	err = lkl_sys_bind(fd, (struct lkl_sockaddr *)&la, sizeof(la));
	if (err < 0) goto exit;

	memset(&req2, 0, sizeof(req2));

	// create the IP attribute
	dstattr = (struct lkl_rtattr *)req2.buf;
	dstattr->rta_type = LKL_NDA_DST;
	dstattr->rta_len = sizeof(struct lkl_rtattr) + addr_sz;
	memcpy(((char *)dstattr) + sizeof(struct lkl_rtattr), ip, addr_sz);
	ndmsglen += dstattr->rta_len;

	// create the MAC attribute
	dstattr = (struct lkl_rtattr *)(req2.buf + dstattr->rta_len);
	dstattr->rta_type = LKL_NDA_LLADDR;
	dstattr->rta_len = sizeof(struct lkl_rtattr) + 6;
	memcpy(((char *)dstattr) + sizeof(struct lkl_rtattr), mac, 6);
	ndmsglen += dstattr->rta_len;

	// fill in the netlink message header
	req2.n.nlmsg_len = ndmsglen;
	req2.n.nlmsg_type = LKL_RTM_NEWNEIGH;
	req2.n.nlmsg_flags =
		LKL_NLM_F_REQUEST | LKL_NLM_F_CREATE | LKL_NLM_F_REPLACE;

	// fill in the netlink message NEWNEIGH
	req2.r.ndm_family = af;
	req2.r.ndm_ifindex = ifindex;
	req2.r.ndm_state = LKL_NUD_PERMANENT;

	err = rtnl_talk(fd, &req2.n);

exit:
	lkl_sys_close(fd);
	return err;
}
