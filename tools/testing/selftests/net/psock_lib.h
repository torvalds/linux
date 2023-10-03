/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2013 Google Inc.
 * Author: Willem de Bruijn <willemb@google.com>
 *         Daniel Borkmann <dborkman@redhat.com>
 */

#ifndef PSOCK_LIB_H
#define PSOCK_LIB_H

#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "kselftest.h"

#define DATA_LEN			100
#define DATA_CHAR			'a'
#define DATA_CHAR_1			'b'

#define PORT_BASE			8000

#ifndef __maybe_unused
# define __maybe_unused		__attribute__ ((__unused__))
#endif

static __maybe_unused void pair_udp_setfilter(int fd)
{
	/* the filter below checks for all of the following conditions that
	 * are based on the contents of create_payload()
	 *  ether type 0x800 and
	 *  ip proto udp     and
	 *  skb->len == DATA_LEN and
	 *  udp[38] == 'a' or udp[38] == 'b'
	 * It can be generated from the following bpf_asm input:
	 *	ldh [12]
	 *	jne #0x800, drop	; ETH_P_IP
	 *	ldb [23]
	 *	jneq #17, drop		; IPPROTO_UDP
	 *	ld len			; ld skb->len
	 *	jlt #100, drop		; DATA_LEN
	 *	ldb [80]
	 *	jeq #97, pass		; DATA_CHAR
	 *	jne #98, drop		; DATA_CHAR_1
	 *	pass:
	 *	  ret #-1
	 *	drop:
	 *	  ret #0
	 */
	struct sock_filter bpf_filter[] = {
		{ 0x28,  0,  0, 0x0000000c },
		{ 0x15,  0,  8, 0x00000800 },
		{ 0x30,  0,  0, 0x00000017 },
		{ 0x15,  0,  6, 0x00000011 },
		{ 0x80,  0,  0, 0000000000 },
		{ 0x35,  0,  4, 0x00000064 },
		{ 0x30,  0,  0, 0x00000050 },
		{ 0x15,  1,  0, 0x00000061 },
		{ 0x15,  0,  1, 0x00000062 },
		{ 0x06,  0,  0, 0xffffffff },
		{ 0x06,  0,  0, 0000000000 },
	};
	struct sock_fprog bpf_prog;

	bpf_prog.filter = bpf_filter;
	bpf_prog.len = ARRAY_SIZE(bpf_filter);

	if (setsockopt(fd, SOL_SOCKET, SO_ATTACH_FILTER, &bpf_prog,
		       sizeof(bpf_prog))) {
		perror("setsockopt SO_ATTACH_FILTER");
		exit(1);
	}
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
