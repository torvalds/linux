/*-
 * Copyright (c) 2018 The FreeBSD Foundation
 *
 * This software was developed by Mark Johnston under sponsorship from
 * the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <net/bpf.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <atf-c.h>

struct lopacket {
	u_int		family;
	struct ip	hdr;
	char		payload[];
};

static void
update_cksum(struct ip *ip)
{
	size_t i;
	uint32_t cksum;
	uint16_t *cksump;

	ip->ip_sum = 0;
	cksump = (uint16_t *)ip;
	for (cksum = 0, i = 0; i < sizeof(*ip) / sizeof(*cksump); cksump++, i++)
		cksum += ntohs(*cksump);
	cksum = (cksum >> 16) + (cksum & 0xffff);
	cksum = ~(cksum + (cksum >> 16));
	ip->ip_sum = htons((uint16_t)cksum);
}

static struct lopacket *
alloc_lopacket(in_addr_t dstaddr, size_t payloadlen)
{
	struct ip *ip;
	struct lopacket *packet;
	size_t pktlen;

	pktlen = sizeof(*packet) + payloadlen;
	packet = malloc(pktlen);
	ATF_REQUIRE(packet != NULL);

	memset(packet, 0, pktlen);
	packet->family = AF_INET;

	ip = &packet->hdr;
	ip->ip_hl = sizeof(struct ip) >> 2;
	ip->ip_v = 4;
	ip->ip_tos = 0;
	ip->ip_len = htons(sizeof(*ip) + payloadlen);
	ip->ip_id = 0;
	ip->ip_off = 0;
	ip->ip_ttl = 1;
	ip->ip_p = IPPROTO_IP;
	ip->ip_sum = 0;
	ip->ip_src.s_addr = dstaddr;
	ip->ip_dst.s_addr = dstaddr;
	update_cksum(ip);

	return (packet);
}

static void
free_lopacket(struct lopacket *packet)
{

	free(packet);
}

static void
write_lopacket(int bpffd, struct lopacket *packet)
{
	struct timespec ts;
	ssize_t n;
	size_t len;

	len = sizeof(packet->family) + ntohs(packet->hdr.ip_len);
	n = write(bpffd, packet, len);
	ATF_REQUIRE_MSG(n >= 0, "packet write failed: %s", strerror(errno));
	ATF_REQUIRE_MSG((size_t)n == len, "wrote %zd bytes instead of %zu",
	    n, len);

	/*
	 * Loopback packets are dispatched asynchronously, give netisr some
	 * time.
	 */
	ts.tv_sec = 0;
	ts.tv_nsec = 5000000; /* 5ms */
	(void)nanosleep(&ts, NULL);
}

static int
open_lobpf(in_addr_t *addrp)
{
	struct ifreq ifr;
	struct ifaddrs *ifa, *ifap;
	int error, fd;

	fd = open("/dev/bpf0", O_RDWR);
	if (fd < 0 && errno == ENOENT)
		atf_tc_skip("no BPF device available");
	ATF_REQUIRE_MSG(fd >= 0, "open(/dev/bpf0): %s", strerror(errno));

	error = getifaddrs(&ifap);
	ATF_REQUIRE(error == 0);
	for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next)
		if ((ifa->ifa_flags & IFF_LOOPBACK) != 0 &&
		    ifa->ifa_addr->sa_family == AF_INET)
			break;
	if (ifa == NULL)
		atf_tc_skip("no loopback address found");

	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, ifa->ifa_name, IFNAMSIZ);
	error = ioctl(fd, BIOCSETIF, &ifr);
	ATF_REQUIRE_MSG(error == 0, "ioctl(BIOCSETIF): %s", strerror(errno));

	*addrp = ((struct sockaddr_in *)(void *)ifa->ifa_addr)->sin_addr.s_addr;

	freeifaddrs(ifap);

	return (fd);
}

static void
get_ipstat(struct ipstat *stat)
{
	size_t len;
	int error;

	memset(stat, 0, sizeof(*stat));
	len = sizeof(*stat);
	error = sysctlbyname("net.inet.ip.stats", stat, &len, NULL, 0);
	ATF_REQUIRE_MSG(error == 0, "sysctl(net.inet.ip.stats) failed: %s",
	    strerror(errno));
	ATF_REQUIRE(len == sizeof(*stat));
}

#define	CHECK_IP_COUNTER(oldp, newp, counter)				\
	ATF_REQUIRE_MSG((oldp)->ips_ ## counter < (newp)->ips_ ## counter, \
	    "ips_" #counter " wasn't incremented (%ju vs. %ju)",	\
	    (uintmax_t)old.ips_ ## counter, (uintmax_t)new.ips_## counter);

/*
 * Make sure a fragment with MF set doesn't come after the last fragment of a
 * packet.  Make sure that multiple fragments with MF clear have the same offset
 * and length.
 */
ATF_TC(ip_reass__multiple_last_fragments);
ATF_TC_HEAD(ip_reass__multiple_last_fragments, tc)
{
	atf_tc_set_md_var(tc, "require.user", "root");
}
ATF_TC_BODY(ip_reass__multiple_last_fragments, tc)
{
	struct ipstat old, new;
	struct ip *ip;
	struct lopacket *packet1, *packet2, *packet3, *packet4;
	in_addr_t addr;
	int error, fd;
	uint16_t ipid;

	fd = open_lobpf(&addr);
	ipid = arc4random_uniform(UINT16_MAX + 1);

	packet1 = alloc_lopacket(addr, 16);
	ip = &packet1->hdr;
	ip->ip_id = ipid;
	ip->ip_off = htons(0x10);
	update_cksum(ip);

	packet2 = alloc_lopacket(addr, 16);
	ip = &packet2->hdr;
	ip->ip_id = ipid;
	ip->ip_off = htons(0x20);
	update_cksum(ip);

	packet3 = alloc_lopacket(addr, 16);
	ip = &packet3->hdr;
	ip->ip_id = ipid;
	ip->ip_off = htons(0x8);
	update_cksum(ip);

	packet4 = alloc_lopacket(addr, 32);
	ip = &packet4->hdr;
	ip->ip_id = ipid;
	ip->ip_off = htons(0x10);
	update_cksum(ip);

	write_lopacket(fd, packet1);

	/* packet2 comes after packet1. */
	get_ipstat(&old);
	write_lopacket(fd, packet2);
	get_ipstat(&new);
	CHECK_IP_COUNTER(&old, &new, fragdropped);

	/* packet2 comes after packet1 and has MF set. */
	packet2->hdr.ip_off = htons(IP_MF | 0x20);
	update_cksum(&packet2->hdr);
	get_ipstat(&old);
	write_lopacket(fd, packet2);
	get_ipstat(&new);
	CHECK_IP_COUNTER(&old, &new, fragdropped);

	/* packet3 comes before packet1 but overlaps. */
	get_ipstat(&old);
	write_lopacket(fd, packet3);
	get_ipstat(&new);
	CHECK_IP_COUNTER(&old, &new, fragdropped);

	/* packet4 has the same offset as packet1 but is longer. */
	get_ipstat(&old);
	write_lopacket(fd, packet4);
	get_ipstat(&new);
	CHECK_IP_COUNTER(&old, &new, fragdropped);

	error = close(fd);
	ATF_REQUIRE(error == 0);
	free_lopacket(packet1);
	free_lopacket(packet2);
	free_lopacket(packet3);
	free_lopacket(packet4);
}

/*
 * Make sure that we reject zero-length fragments.
 */
ATF_TC(ip_reass__zero_length_fragment);
ATF_TC_HEAD(ip_reass__zero_length_fragment, tc)
{
	atf_tc_set_md_var(tc, "require.user", "root");
}
ATF_TC_BODY(ip_reass__zero_length_fragment, tc)
{
	struct ipstat old, new;
	struct ip *ip;
	struct lopacket *packet1, *packet2;
	in_addr_t addr;
	int error, fd;
	uint16_t ipid;

	fd = open_lobpf(&addr);
	ipid = arc4random_uniform(UINT16_MAX + 1);

	/*
	 * Create two packets, one with MF set, one without.
	 */
	packet1 = alloc_lopacket(addr, 0);
	ip = &packet1->hdr;
	ip->ip_id = ipid;
	ip->ip_off = htons(IP_MF | 0x10);
	update_cksum(ip);

	packet2 = alloc_lopacket(addr, 0);
	ip = &packet2->hdr;
	ip->ip_id = ~ipid;
	ip->ip_off = htons(0x10);
	update_cksum(ip);

	get_ipstat(&old);
	write_lopacket(fd, packet1);
	get_ipstat(&new);
	CHECK_IP_COUNTER(&old, &new, toosmall);
	CHECK_IP_COUNTER(&old, &new, fragdropped);

	get_ipstat(&old);
	write_lopacket(fd, packet2);
	get_ipstat(&new);
	CHECK_IP_COUNTER(&old, &new, toosmall);
	CHECK_IP_COUNTER(&old, &new, fragdropped);

	error = close(fd);
	ATF_REQUIRE(error == 0);
	free_lopacket(packet1);
	free_lopacket(packet2);
}

ATF_TC(ip_reass__large_fragment);
ATF_TC_HEAD(ip_reass__large_fragment, tc)
{
	atf_tc_set_md_var(tc, "require.user", "root");
}
ATF_TC_BODY(ip_reass__large_fragment, tc)
{
	struct ipstat old, new;
	struct ip *ip;
	struct lopacket *packet1, *packet2;
	in_addr_t addr;
	int error, fd;
	uint16_t ipid;

	fd = open_lobpf(&addr);
	ipid = arc4random_uniform(UINT16_MAX + 1);

	/*
	 * Create two packets, one with MF set, one without.
	 *
	 * 16 + (0x1fff << 3) > IP_MAXPACKET, so these should fail the check.
	 */
	packet1 = alloc_lopacket(addr, 16);
	ip = &packet1->hdr;
	ip->ip_id = ipid;
	ip->ip_off = htons(IP_MF | 0x1fff);
	update_cksum(ip);

	packet2 = alloc_lopacket(addr, 16);
	ip = &packet2->hdr;
	ip->ip_id = ipid;
	ip->ip_off = htons(0x1fff);
	update_cksum(ip);

	get_ipstat(&old);
	write_lopacket(fd, packet1);
	get_ipstat(&new);
	CHECK_IP_COUNTER(&old, &new, toolong);
	CHECK_IP_COUNTER(&old, &new, fragdropped);

	get_ipstat(&old);
	write_lopacket(fd, packet2);
	get_ipstat(&new);
	CHECK_IP_COUNTER(&old, &new, toolong);
	CHECK_IP_COUNTER(&old, &new, fragdropped);

	error = close(fd);
	ATF_REQUIRE(error == 0);
	free_lopacket(packet1);
	free_lopacket(packet2);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, ip_reass__multiple_last_fragments);
	ATF_TP_ADD_TC(tp, ip_reass__zero_length_fragment);
	ATF_TP_ADD_TC(tp, ip_reass__large_fragment);

	return (atf_no_error());
}
