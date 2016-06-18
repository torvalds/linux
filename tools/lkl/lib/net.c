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

struct lkl_arpreq {
	struct lkl_sockaddr arp_pa;      /* protocol address */
	struct lkl_sockaddr arp_ha;      /* hardware address */
	int             arp_flags;   /* flags */
	struct lkl_sockaddr arp_netmask; /* netmask of protocol address */
	char            arp_dev[LKL_IFNAMSIZ];
};

#define LKL_ATF_PERM 0x04
#define LKL_ATF_COM 0x02

int lkl_add_arp_entry(int ifindex, unsigned int ip, void* mac) {
	struct lkl_arpreq req;
	int ret = 0;
	struct lkl_ifreq ifr;
	struct lkl_sockaddr_in* sin = (struct lkl_sockaddr_in*)&req.arp_pa;
	int sock;

	bzero(&req, sizeof(req));
	sin->sin_family = LKL_AF_INET;
	sin->sin_addr.lkl_s_addr = ip;
	memcpy(req.arp_ha.sa_data, mac, LKL_ETH_ALEN);

	sock = lkl_sys_socket(LKL_AF_INET, LKL_SOCK_DGRAM, 0);
	if (sock < 0) {
		return sock;
	}

	req.arp_flags = LKL_ATF_PERM | LKL_ATF_COM;

	ret = ifindex_to_name(sock, &ifr, ifindex);
	if (ret < 0) {
		lkl_sys_close(sock);
		return ret;
	}
	strcpy(req.arp_dev, ifr.ifr_ifrn.ifrn_name);

	ret = lkl_sys_ioctl(sock, LKL_SIOCSARP, (long)(&req));
	lkl_sys_close(sock);
	return ret;
}
