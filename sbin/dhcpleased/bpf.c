/*	$OpenBSD: bpf.c,v 1.2 2021/03/02 19:20:13 florian Exp $	*/

/* BPF socket interface code, originally contributed by Archie Cobbs. */

/*
 * Copyright (c) 1995, 1996, 1998, 1999
 * The Internet Software Consortium.    All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of The Internet Software Consortium nor the names
 *    of its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INTERNET SOFTWARE CONSORTIUM AND
 * CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE INTERNET SOFTWARE CONSORTIUM OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This software has been written for the Internet Software Consortium
 * by Ted Lemon <mellon@fugue.com> in cooperation with Vixie
 * Enterprises.  To learn more about the Internet Software Consortium,
 * see ``http://www.vix.com/isc''.  To learn more about Vixie
 * Enterprises, see ``http://www.vix.com''.
 */

#include <sys/types.h>
#include <sys/ioctl.h>

#include <net/bpf.h>
#include <net/if.h>
#include <net/ethertypes.h>
#include <netinet/in.h>
#include <netinet/ip.h>

#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "bpf.h"
#include "log.h"

#define	CLIENT_PORT	68

/*
 * Packet filter program.
 */
struct bpf_insn dhcp_bpf_filter[] = {
	/* Make sure this is an IP packet. */
	BPF_STMT(BPF_LD + BPF_H + BPF_ABS, 12),
	BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, ETHERTYPE_IP, 0, 8),

	/* Make sure it's a UDP packet. */
	BPF_STMT(BPF_LD + BPF_B + BPF_ABS, 23),
	BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, IPPROTO_UDP, 0, 6),

	/* Make sure this isn't a fragment. */
	BPF_STMT(BPF_LD + BPF_H + BPF_ABS, 20),
	BPF_JUMP(BPF_JMP + BPF_JSET + BPF_K, 0x1fff, 4, 0),

	/* Get the IP header length. */
	BPF_STMT(BPF_LDX + BPF_B + BPF_MSH, 14),

	/* Make sure it's to the right port. */
	BPF_STMT(BPF_LD + BPF_H + BPF_IND, 16),
	BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, CLIENT_PORT, 0, 1),

	/* If we passed all the tests, ask for the whole packet. */
	BPF_STMT(BPF_RET+BPF_K, (unsigned int)-1),

	/* Otherwise, drop it. */
	BPF_STMT(BPF_RET+BPF_K, 0),
};

/*
 * Packet write filter program:
 * 'ip and udp and src port bootpc and dst port bootps'
 */
struct bpf_insn dhcp_bpf_wfilter[] = {
	BPF_STMT(BPF_LD + BPF_B + BPF_IND, 14),
	BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, (IPVERSION << 4) + 5, 0, 12),

	/* Make sure this is an IP packet. */
	BPF_STMT(BPF_LD + BPF_H + BPF_ABS, 12),
	BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, ETHERTYPE_IP, 0, 10),

	/* Make sure it's a UDP packet. */
	BPF_STMT(BPF_LD + BPF_B + BPF_ABS, 23),
	BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, IPPROTO_UDP, 0, 8),

	/* Make sure this isn't a fragment. */
	BPF_STMT(BPF_LD + BPF_H + BPF_ABS, 20),
	BPF_JUMP(BPF_JMP + BPF_JSET + BPF_K, 0x1fff, 6, 0),	/* patched */

	/* Get the IP header length. */
	BPF_STMT(BPF_LDX + BPF_B + BPF_MSH, 14),

	/* Make sure it's from the right port. */
	BPF_STMT(BPF_LD + BPF_H + BPF_IND, 14),
	BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, 68, 0, 3),

	/* Make sure it is to the right ports. */
	BPF_STMT(BPF_LD + BPF_H + BPF_IND, 16),
	BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, 67, 0, 1),

	/* If we passed all the tests, ask for the whole packet. */
	BPF_STMT(BPF_RET+BPF_K, (unsigned int)-1),

	/* Otherwise, drop it. */
	BPF_STMT(BPF_RET+BPF_K, 0),
};

int
get_bpf_sock(const char *name)
{
	struct bpf_program	 p;
	struct ifreq		 ifr;
	u_int			 sz;
	int			 flag = 1, fildrop = BPF_FILDROP_CAPTURE;
	int			 bpffd;

	if ((bpffd = open("/dev/bpf", O_RDWR | O_CLOEXEC | O_NONBLOCK)) == -1)
		fatal("open(/dev/bpf)");

	sz = BPFLEN;
	/* Set the BPF buffer length. */
	if (ioctl(bpffd, BIOCSBLEN, &sz) == -1)
		fatal("BIOCSBLEN");
	if (sz != BPFLEN)
		fatal("BIOCSBLEN, expected %u, got %u", BPFLEN, sz);

	/*
	 * Set immediate mode so that reads return as soon as a packet
	 * comes in, rather than waiting for the input buffer to fill
	 * with packets.
	 */
	if (ioctl(bpffd, BIOCIMMEDIATE, &flag) == -1)
		fatal("BIOCIMMEDIATE");

	if (ioctl(bpffd, BIOCSFILDROP, &fildrop) == -1)
		fatal("BIOCSFILDROP");

	/* Set up the bpf filter program structure. */
	p.bf_len = sizeof(dhcp_bpf_filter) / sizeof(struct bpf_insn);
	p.bf_insns = dhcp_bpf_filter;

	if (ioctl(bpffd, BIOCSETF, &p) == -1)
		fatal("BIOCSETF");

	/* Set up the bpf write filter program structure. */
	p.bf_len = sizeof(dhcp_bpf_wfilter) / sizeof(struct bpf_insn);
	p.bf_insns = dhcp_bpf_wfilter;

	if (dhcp_bpf_wfilter[7].k == 0x1fff)
		dhcp_bpf_wfilter[7].k = htons(IP_MF|IP_OFFMASK);

	if (ioctl(bpffd, BIOCSETWF, &p) == -1)
		fatal("BIOCSETWF");

	strlcpy(ifr.ifr_name, name, IFNAMSIZ);
	if (ioctl(bpffd, BIOCSETIF, &ifr) == -1) {
		log_warn("BIOCSETIF"); /* interface might have disappeared */
		close(bpffd);
		return -1;
	}

	if (ioctl(bpffd, BIOCLOCK, NULL) == -1)
		fatal("BIOCLOCK");

	return bpffd;
}
