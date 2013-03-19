/*
 * Copyright 2013 Google Inc.
 * Author: Willem de Bruijn (willemb@google.com)
 *
 * A basic test of packet socket fanout behavior.
 *
 * Control:
 * - create fanout fails as expected with illegal flag combinations
 * - join   fanout fails as expected with diverging types or flags
 *
 * Datapath:
 *   Open a pair of packet sockets and a pair of INET sockets, send a known
 *   number of packets across the two INET sockets and count the number of
 *   packets enqueued onto the two packet sockets.
 *
 *   The test currently runs for
 *   - PACKET_FANOUT_HASH
 *   - PACKET_FANOUT_HASH with PACKET_FANOUT_FLAG_ROLLOVER
 *   - PACKET_FANOUT_ROLLOVER
 *
 * Todo:
 * - datapath: PACKET_FANOUT_LB
 * - datapath: PACKET_FANOUT_CPU
 * - functionality: PACKET_FANOUT_FLAG_DEFRAG
 *
 * License (GPLv2):
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. * See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <linux/filter.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* Hack: build even if local includes are old */
#ifndef PACKET_FANOUT
#define PACKET_FANOUT			18
#define PACKET_FANOUT_HASH		0
#define PACKET_FANOUT_LB		1
#define PACKET_FANOUT_CPU		2
#define PACKET_FANOUT_FLAG_DEFRAG	0x8000

#ifndef PACKET_FANOUT_ROLLOVER
#define PACKET_FANOUT_ROLLOVER		3
#endif

#ifndef PACKET_FANOUT_FLAG_ROLLOVER
#define PACKET_FANOUT_FLAG_ROLLOVER	0x1000
#endif

#endif

#define DATA_LEN			100
#define DATA_CHAR			'a'

static void pair_udp_open(int fds[], uint16_t port)
{
	struct sockaddr_in saddr, daddr;

	fds[0] = socket(PF_INET, SOCK_DGRAM, 0);
	fds[1] = socket(PF_INET, SOCK_DGRAM, 0);
	if (fds[0] == -1 || fds[1] == -1) {
		fprintf(stderr, "ERROR: socket dgram\n");
		exit(1);
	}

	memset(&saddr, 0, sizeof(saddr));
	saddr.sin_family = AF_INET;
	saddr.sin_port = htons(port);
	saddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	memset(&daddr, 0, sizeof(daddr));
	daddr.sin_family = AF_INET;
	daddr.sin_port = htons(port + 1);
	daddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	/* must bind both to get consistent hash result */
	if (bind(fds[1], (void *) &daddr, sizeof(daddr))) {
		perror("bind");
		exit(1);
	}
	if (bind(fds[0], (void *) &saddr, sizeof(saddr))) {
		perror("bind");
		exit(1);
	}
	if (connect(fds[0], (void *) &daddr, sizeof(daddr))) {
		perror("bind");
		exit(1);
	}
}

static void pair_udp_send(int fds[], int num)
{
	char buf[DATA_LEN], rbuf[DATA_LEN];

	memset(buf, DATA_CHAR, sizeof(buf));
	while (num--) {
		/* Should really handle EINTR and EAGAIN */
		if (write(fds[0], buf, sizeof(buf)) != sizeof(buf)) {
			fprintf(stderr, "ERROR: send failed left=%d\n", num);
			exit(1);
		}
		if (read(fds[1], rbuf, sizeof(rbuf)) != sizeof(rbuf)) {
			fprintf(stderr, "ERROR: recv failed left=%d\n", num);
			exit(1);
		}
		if (memcmp(buf, rbuf, sizeof(buf))) {
			fprintf(stderr, "ERROR: data failed left=%d\n", num);
			exit(1);
		}
	}
}

static void sock_fanout_setfilter(int fd)
{
	struct sock_filter bpf_filter[] = {
		{ 0x80, 0, 0, 0x00000000 },  /* LD  pktlen		      */
		{ 0x35, 0, 5, DATA_LEN   },  /* JGE DATA_LEN  [f goto nomatch]*/
		{ 0x30, 0, 0, 0x00000050 },  /* LD  ip[80]		      */
		{ 0x15, 0, 3, DATA_CHAR  },  /* JEQ DATA_CHAR [f goto nomatch]*/
		{ 0x30, 0, 0, 0x00000051 },  /* LD  ip[81]		      */
		{ 0x15, 0, 1, DATA_CHAR  },  /* JEQ DATA_CHAR [f goto nomatch]*/
		{ 0x6, 0, 0, 0x00000060  },  /* RET match	              */
/* nomatch */	{ 0x6, 0, 0, 0x00000000  },  /* RET no match		      */
	};
	struct sock_fprog bpf_prog;

	bpf_prog.filter = bpf_filter;
	bpf_prog.len = sizeof(bpf_filter) / sizeof(struct sock_filter);
	if (setsockopt(fd, SOL_SOCKET, SO_ATTACH_FILTER, &bpf_prog,
		       sizeof(bpf_prog))) {
		perror("setsockopt SO_ATTACH_FILTER");
		exit(1);
	}
}

/* Open a socket in a given fanout mode.
 * @return -1 if mode is bad, a valid socket otherwise */
static int sock_fanout_open(uint16_t typeflags, int num_packets)
{
	int fd, val;

	fd = socket(PF_PACKET, SOCK_DGRAM, htons(ETH_P_IP));
	if (fd < 0) {
		perror("socket packet");
		exit(1);
	}

	/* fanout group ID is always 0: tests whether old groups are deleted */
	val = ((int) typeflags) << 16;
	if (setsockopt(fd, SOL_PACKET, PACKET_FANOUT, &val, sizeof(val))) {
		if (close(fd)) {
			perror("close packet");
			exit(1);
		}
		return -1;
	}

	val = sizeof(struct iphdr) + sizeof(struct udphdr) + DATA_LEN;
	val *= num_packets;
	/* hack: apparently, the above calculation is too small (TODO: fix) */
	val *= 3;
	if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &val, sizeof(val))) {
		perror("setsockopt SO_RCVBUF");
		exit(1);
	}

	sock_fanout_setfilter(fd);
	return fd;
}

static void sock_fanout_read(int fds[], const int expect[])
{
	struct tpacket_stats stats;
	socklen_t ssize;
	int ret[2];

	ssize = sizeof(stats);
	if (getsockopt(fds[0], SOL_PACKET, PACKET_STATISTICS, &stats, &ssize)) {
		perror("getsockopt statistics 0");
		exit(1);
	}
	ret[0] = stats.tp_packets - stats.tp_drops;
	ssize = sizeof(stats);
	if (getsockopt(fds[1], SOL_PACKET, PACKET_STATISTICS, &stats, &ssize)) {
		perror("getsockopt statistics 1");
		exit(1);
	}
	ret[1] = stats.tp_packets - stats.tp_drops;

	fprintf(stderr, "info: count=%d,%d, expect=%d,%d\n",
			ret[0], ret[1], expect[0], expect[1]);

	if ((!(ret[0] == expect[0] && ret[1] == expect[1])) &&
	    (!(ret[0] == expect[1] && ret[1] == expect[0]))) {
		fprintf(stderr, "ERROR: incorrect queue lengths\n");
		exit(1);
	}
}

/* Test illegal mode + flag combination */
static void test_control_single(void)
{
	fprintf(stderr, "test: control single socket\n");

	if (sock_fanout_open(PACKET_FANOUT_ROLLOVER |
			       PACKET_FANOUT_FLAG_ROLLOVER, 0) != -1) {
		fprintf(stderr, "ERROR: opened socket with dual rollover\n");
		exit(1);
	}
}

/* Test illegal group with different modes or flags */
static void test_control_group(void)
{
	int fds[2];

	fprintf(stderr, "test: control multiple sockets\n");

	fds[0] = sock_fanout_open(PACKET_FANOUT_HASH, 20);
	if (fds[0] == -1) {
		fprintf(stderr, "ERROR: failed to open HASH socket\n");
		exit(1);
	}
	if (sock_fanout_open(PACKET_FANOUT_HASH |
			       PACKET_FANOUT_FLAG_DEFRAG, 10) != -1) {
		fprintf(stderr, "ERROR: joined group with wrong flag defrag\n");
		exit(1);
	}
	if (sock_fanout_open(PACKET_FANOUT_HASH |
			       PACKET_FANOUT_FLAG_ROLLOVER, 10) != -1) {
		fprintf(stderr, "ERROR: joined group with wrong flag ro\n");
		exit(1);
	}
	if (sock_fanout_open(PACKET_FANOUT_CPU, 10) != -1) {
		fprintf(stderr, "ERROR: joined group with wrong mode\n");
		exit(1);
	}
	fds[1] = sock_fanout_open(PACKET_FANOUT_HASH, 20);
	if (fds[1] == -1) {
		fprintf(stderr, "ERROR: failed to join group\n");
		exit(1);
	}
	if (close(fds[1]) || close(fds[0])) {
		fprintf(stderr, "ERROR: closing sockets\n");
		exit(1);
	}
}

static void test_datapath(uint16_t typeflags,
			  const int expect1[], const int expect2[])
{
	const int expect0[] = { 0, 0 };
	int fds[2], fds_udp[2][2];

	fprintf(stderr, "test: datapath 0x%hx\n", typeflags);

	fds[0] = sock_fanout_open(typeflags, 20);
	fds[1] = sock_fanout_open(typeflags, 20);
	if (fds[0] == -1 || fds[1] == -1) {
		fprintf(stderr, "ERROR: failed open\n");
		exit(1);
	}
	pair_udp_open(fds_udp[0], 8000);
	pair_udp_open(fds_udp[1], 8002);
	sock_fanout_read(fds, expect0);

	/* Send data, but not enough to overflow a queue */
	pair_udp_send(fds_udp[0], 15);
	pair_udp_send(fds_udp[1], 5);
	sock_fanout_read(fds, expect1);

	/* Send more data, overflow the queue */
	pair_udp_send(fds_udp[0], 15);
	/* TODO: ensure consistent order between expect1 and expect2 */
	sock_fanout_read(fds, expect2);

	if (close(fds_udp[1][1]) || close(fds_udp[1][0]) ||
	    close(fds_udp[0][1]) || close(fds_udp[0][0]) ||
	    close(fds[1]) || close(fds[0])) {
		fprintf(stderr, "close datapath\n");
		exit(1);
	}
}

int main(int argc, char **argv)
{
	const int expect_hash[2][2]	= { { 15, 5 }, { 5, 0 } };
	const int expect_hash_rb[2][2]	= { { 15, 5 }, { 5, 10 } };
	const int expect_rb[2][2]	= { { 20, 0 }, { 0, 15 } };

	test_control_single();
	test_control_group();

	test_datapath(PACKET_FANOUT_HASH, expect_hash[0], expect_hash[1]);
	test_datapath(PACKET_FANOUT_HASH | PACKET_FANOUT_FLAG_ROLLOVER,
		      expect_hash_rb[0], expect_hash_rb[1]);
	test_datapath(PACKET_FANOUT_ROLLOVER, expect_rb[0], expect_rb[1]);

	printf("OK. All tests passed\n");
	return 0;
}
