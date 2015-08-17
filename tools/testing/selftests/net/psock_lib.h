/*
 * Copyright 2013 Google Inc.
 * Author: Willem de Bruijn <willemb@google.com>
 *         Daniel Borkmann <dborkman@redhat.com>
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

#ifndef PSOCK_LIB_H
#define PSOCK_LIB_H

#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>

#define DATA_LEN			100
#define DATA_CHAR			'a'
#define DATA_CHAR_1			'b'

#define PORT_BASE			8000

#ifndef __maybe_unused
# define __maybe_unused		__attribute__ ((__unused__))
#endif

static __maybe_unused void sock_setfilter(int fd, int lvl, int optnum)
{
	struct sock_filter bpf_filter[] = {
		{ 0x80, 0, 0, 0x00000000 },  /* LD  pktlen		      */
		{ 0x35, 0, 4, DATA_LEN   },  /* JGE DATA_LEN  [f goto nomatch]*/
		{ 0x30, 0, 0, 0x00000050 },  /* LD  ip[80]		      */
		{ 0x15, 1, 0, DATA_CHAR  },  /* JEQ DATA_CHAR   [t goto match]*/
		{ 0x15, 0, 1, DATA_CHAR_1},  /* JEQ DATA_CHAR_1 [t goto match]*/
		{ 0x06, 0, 0, 0x00000060 },  /* RET match	              */
		{ 0x06, 0, 0, 0x00000000 },  /* RET no match		      */
	};
	struct sock_fprog bpf_prog;

	if (lvl == SOL_PACKET && optnum == PACKET_FANOUT_DATA)
		bpf_filter[5].code = 0x16;   /* RET A			      */

	bpf_prog.filter = bpf_filter;
	bpf_prog.len = sizeof(bpf_filter) / sizeof(struct sock_filter);
	if (setsockopt(fd, lvl, optnum, &bpf_prog,
		       sizeof(bpf_prog))) {
		perror("setsockopt SO_ATTACH_FILTER");
		exit(1);
	}
}

static __maybe_unused void pair_udp_setfilter(int fd)
{
	sock_setfilter(fd, SOL_SOCKET, SO_ATTACH_FILTER);
}

static __maybe_unused void pair_udp_open(int fds[], uint16_t port)
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
		perror("connect");
		exit(1);
	}
}

static __maybe_unused void pair_udp_send_char(int fds[], int num, char payload)
{
	char buf[DATA_LEN], rbuf[DATA_LEN];

	memset(buf, payload, sizeof(buf));
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

static __maybe_unused void pair_udp_send(int fds[], int num)
{
	return pair_udp_send_char(fds, num, DATA_CHAR);
}

static __maybe_unused void pair_udp_close(int fds[])
{
	close(fds[0]);
	close(fds[1]);
}

#endif /* PSOCK_LIB_H */
