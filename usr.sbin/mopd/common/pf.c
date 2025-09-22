/*	$OpenBSD: pf.c,v 1.19 2023/03/08 04:43:13 guenther Exp $ */

/*
 * Copyright (c) 1993-95 Mats O Jansson.  All rights reserved.
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is partly derived from rarpd.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <net/if.h>

#include <net/bpf.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <netdb.h>
#include <ctype.h>
#include <string.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <syslog.h>
#include <unistd.h>

#include "common/mopdef.h"

/*
 * Variables
 */

extern int promisc;

/*
 * Return information to device.c how to open device.
 * In this case the driver can handle both Ethernet type II and
 * IEEE 802.3 frames (SNAP) in a single pfOpen.
 */
int
pfTrans(char *interface)
{
	return (TRANS_ETHER + TRANS_8023 + TRANS_AND);
}

/*
 * Open and initialize packet filter.
 */
int
pfInit(char *interface, int mode, u_short protocol, int typ)
{
	int		fd;
	struct ifreq	ifr;
	u_int		dlt;
	int		immediate;

	static struct bpf_insn insns[] = {
		BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 12),
		BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0x4711, 4, 0),
		BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 20),
		BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0x4711, 0, 3),
		BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 14),
		BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0xaaaa, 0, 1),
		BPF_STMT(BPF_RET | BPF_K, 1520),
		BPF_STMT(BPF_RET | BPF_K, 0),
	};
	static struct bpf_program filter = {
		sizeof insns / sizeof(insns[0]),
		insns
	};

	if ((fd = open("/dev/bpf", mode)) == -1) {
		syslog(LOG_ERR,"pfInit: open bpf %m");
		return (-1);
	}

	/* Set immediate mode so packets are processed as they arrive. */
	immediate = 1;
	if (ioctl(fd, BIOCIMMEDIATE, &immediate) < 0) {
		syslog(LOG_ERR,"pfInit: BIOCIMMEDIATE: %m");
		return (-1);
	}
	strncpy(ifr.ifr_name, interface, sizeof ifr.ifr_name);
	if (ioctl(fd, BIOCSETIF, &ifr) < 0) {
		syslog(LOG_ERR,"pfInit: BIOCSETIF: %m");
		return (-1);
	}
	/* Check that the data link layer is an Ethernet; this code won't work
	 * with anything else. */
	if (ioctl(fd, BIOCGDLT, &dlt) < 0) {
		syslog(LOG_ERR,"pfInit: BIOCGDLT: %m");
		return (-1);
	}
	if (dlt != DLT_EN10MB) {
		syslog(LOG_ERR,"pfInit: %s is not ethernet", interface);
		return (-1);
	}
	if (promisc)
		/* Set promiscuous mode. */
		if (ioctl(fd, BIOCPROMISC, 0) < 0) {
			syslog(LOG_ERR,"pfInit: BIOCPROMISC: %m");
			return (-1);
		}

	/* Set filter program. */
	insns[1].k = protocol;
	insns[3].k = protocol;

	if (ioctl(fd, BIOCSETF, &filter) < 0) {
		syslog(LOG_ERR,"pfInit: BIOCSETF: %m");
		return (-1);
	}

	/* XXX set the same write filter (for protocol only) */
	if (ioctl(fd, BIOCSETWF, &filter) < 0) {
		syslog(LOG_ERR,"pfInit: BIOCSETWF: %m");
		return (-1);
	}

	/* Lock the interface to prevent further changes */
	if (ioctl(fd, BIOCLOCK) < 0) {
		syslog(LOG_ERR,"pfInit: BIOCLOCK: %m");
		return (-1);
	}

	return (fd);
}

/*
 * Add a Multicast address to the interface
 */
int
pfAddMulti(int s, char *interface, char *addr)
{
	struct ifreq	ifr;
	int		fd;

	strncpy(ifr.ifr_name, interface, sizeof(ifr.ifr_name) - 1);
	ifr.ifr_name[sizeof(ifr.ifr_name) - 1] = 0;

	ifr.ifr_addr.sa_family = AF_UNSPEC;
	bcopy(addr, ifr.ifr_addr.sa_data, 6);

	/*
	 * open a socket, temporarily, to use for SIOC* ioctls
	 */
	if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		syslog(LOG_ERR, "pfAddMulti: socket: %m");
		return (-1);
	}
	if (ioctl(fd, SIOCADDMULTI, &ifr) < 0) {
		syslog(LOG_ERR, "pfAddMulti: SIOCADDMULTI: %m");
		close(fd);
		return (-1);
	}
	close(fd);

	return (0);
}

/*
 * Delete a Multicast address from the interface
 */
int
pfDelMulti(int s, char *interface, char *addr)
{
	struct ifreq	ifr;
	int		fd;

	strncpy(ifr.ifr_name, interface, sizeof (ifr.ifr_name) - 1);
	ifr.ifr_name[sizeof(ifr.ifr_name)-1] = 0;

	ifr.ifr_addr.sa_family = AF_UNSPEC;
	bcopy(addr, ifr.ifr_addr.sa_data, 6);

	/*
	 * open a socket, temporarily, to use for SIOC* ioctls
	 *
	 */
	if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		syslog(LOG_ERR, "pfDelMulti: socket: %m");
		return (-1);
	}
	if (ioctl(fd, SIOCDELMULTI, &ifr) < 0) {
		syslog(LOG_ERR, "pfAddMulti: SIOCDELMULTI: %m");
		close(fd);
		return (-1);
	}
	close(fd);

	return (0);
}

/*
 * read a packet
 */
int
pfRead(int fd, u_char *buf, int len)
{
	return (read(fd, buf, len));
}

/*
 * write a packet
 */
int
pfWrite(int fd, u_char *buf, int len, int trans)
{
	struct iovec	iov[2];

	/* XXX */
	switch (trans) {
	case TRANS_8023:
		iov[0].iov_base = buf;
		iov[0].iov_len = 22;
		iov[1].iov_base = buf + 22;
		iov[1].iov_len = len - 22;
		break;
	default:
		iov[0].iov_base = buf;
		iov[0].iov_len = 14;
		iov[1].iov_base = buf + 14;
		iov[1].iov_len = len - 14;
		break;
	}

	if (writev(fd, iov, 2) == len)
		return (len);

	return (-1);
}

