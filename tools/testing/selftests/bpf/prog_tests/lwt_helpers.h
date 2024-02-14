/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __LWT_HELPERS_H
#define __LWT_HELPERS_H

#include <time.h>
#include <net/if.h>
#include <linux/if_tun.h>
#include <linux/icmp.h>

#include "test_progs.h"

#define log_err(MSG, ...) \
	fprintf(stderr, "(%s:%d: errno: %s) " MSG "\n", \
		__FILE__, __LINE__, strerror(errno), ##__VA_ARGS__)

#define RUN_TEST(name)                                                        \
	({                                                                    \
		if (test__start_subtest(#name))                               \
			if (ASSERT_OK(netns_create(), "netns_create")) {      \
				struct nstoken *token = open_netns(NETNS);    \
				if (ASSERT_OK_PTR(token, "setns")) {          \
					test_ ## name();                      \
					close_netns(token);                   \
				}                                             \
				netns_delete();                               \
			}                                                     \
	})

#define NETNS "ns_lwt"

static inline int netns_create(void)
{
	return system("ip netns add " NETNS);
}

static inline int netns_delete(void)
{
	return system("ip netns del " NETNS ">/dev/null 2>&1");
}

static int open_tuntap(const char *dev_name, bool need_mac)
{
	int err = 0;
	struct ifreq ifr;
	int fd = open("/dev/net/tun", O_RDWR);

	if (!ASSERT_GT(fd, 0, "open(/dev/net/tun)"))
		return -1;

	ifr.ifr_flags = IFF_NO_PI | (need_mac ? IFF_TAP : IFF_TUN);
	strncpy(ifr.ifr_name, dev_name, IFNAMSIZ - 1);
	ifr.ifr_name[IFNAMSIZ - 1] = '\0';

	err = ioctl(fd, TUNSETIFF, &ifr);
	if (!ASSERT_OK(err, "ioctl(TUNSETIFF)")) {
		close(fd);
		return -1;
	}

	err = fcntl(fd, F_SETFL, O_NONBLOCK);
	if (!ASSERT_OK(err, "fcntl(O_NONBLOCK)")) {
		close(fd);
		return -1;
	}

	return fd;
}

#define ICMP_PAYLOAD_SIZE     100

/* Match an ICMP packet with payload len ICMP_PAYLOAD_SIZE */
static int __expect_icmp_ipv4(char *buf, ssize_t len)
{
	struct iphdr *ip = (struct iphdr *)buf;
	struct icmphdr *icmp = (struct icmphdr *)(ip + 1);
	ssize_t min_header_len = sizeof(*ip) + sizeof(*icmp);

	if (len < min_header_len)
		return -1;

	if (ip->protocol != IPPROTO_ICMP)
		return -1;

	if (icmp->type != ICMP_ECHO)
		return -1;

	return len == ICMP_PAYLOAD_SIZE + min_header_len;
}

typedef int (*filter_t) (char *, ssize_t);

/* wait_for_packet - wait for a packet that matches the filter
 *
 * @fd: tun fd/packet socket to read packet
 * @filter: filter function, returning 1 if matches
 * @timeout: timeout to wait for the packet
 *
 * Returns 1 if a matching packet is read, 0 if timeout expired, -1 on error.
 */
static int wait_for_packet(int fd, filter_t filter, struct timeval *timeout)
{
	char buf[4096];
	int max_retry = 5; /* in case we read some spurious packets */
	fd_set fds;

	FD_ZERO(&fds);
	while (max_retry--) {
		/* Linux modifies timeout arg... So make a copy */
		struct timeval copied_timeout = *timeout;
		ssize_t ret = -1;

		FD_SET(fd, &fds);

		ret = select(1 + fd, &fds, NULL, NULL, &copied_timeout);
		if (ret <= 0) {
			if (errno == EINTR)
				continue;
			else if (errno == EAGAIN || ret == 0)
				return 0;

			log_err("select failed");
			return -1;
		}

		ret = read(fd, buf, sizeof(buf));

		if (ret <= 0) {
			log_err("read(dev): %ld", ret);
			return -1;
		}

		if (filter && filter(buf, ret) > 0)
			return 1;
	}

	return 0;
}

#endif /* __LWT_HELPERS_H */
