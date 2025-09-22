/*	$OpenBSD: bpf.c,v 1.20 2025/02/07 23:08:48 bluhm Exp $ */

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
#include <sys/socket.h>

#include <net/bpf.h>
#include <net/if.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dhcp.h"
#include "dhcpd.h"
#include "log.h"

/*
 * Called by get_interface_list for each interface that's discovered.
 * Opens a packet filter for each interface and adds it to the select
 * mask.
 */
int
if_register_bpf(struct interface_info *info)
{
	int sock;

	/* Open the BPF device */
	if ((sock = open("/dev/bpf", O_RDWR)) == -1)
		fatal("Can't open bpf device");

	/* Set the BPF device to point at this interface. */
	if (ioctl(sock, BIOCSETIF, &info->ifr) == -1)
		fatal("Can't attach interface %s to bpf device", info->name);

	return (sock);
}

void
if_register_send(struct interface_info *info)
{
	/*
	 * If we're using the bpf API for sending and receiving, we
	 * don't need to register this interface twice.
	 */
	info->wfdesc = info->rfdesc;
}

/*
 * Packet filter program: 'ip and udp and dst port CLIENT_PORT'
 */
struct bpf_insn dhcp_bpf_sfilter[] = {
	/* Make sure this is an IP packet... */
	BPF_STMT(BPF_LD + BPF_H + BPF_ABS, 12),
	BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, ETHERTYPE_IP, 0, 8),

	/* Make sure it's a UDP packet... */
	BPF_STMT(BPF_LD + BPF_B + BPF_ABS, 23),
	BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, IPPROTO_UDP, 0, 6),

	/* Make sure this isn't a fragment... */
	BPF_STMT(BPF_LD + BPF_H + BPF_ABS, 20),
	BPF_JUMP(BPF_JMP + BPF_JSET + BPF_K, 0x1fff, 4, 0),

	/* Get the IP header length... */
	BPF_STMT(BPF_LDX + BPF_B + BPF_MSH, 14),

	/* Make sure it's to the right port... */
	BPF_STMT(BPF_LD + BPF_H + BPF_IND, 16),
	BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, CLIENT_PORT, 0, 1),

	/* If we passed all the tests, ask for the whole packet. */
	BPF_STMT(BPF_RET+BPF_K, (u_int)-1),

	/* Otherwise, drop it. */
	BPF_STMT(BPF_RET+BPF_K, 0),
};

int dhcp_bpf_sfilter_len = sizeof(dhcp_bpf_sfilter) / sizeof(struct bpf_insn);

/*
 * Packet filter program: 'ip and udp and dst port SERVER_PORT'
 */
struct bpf_insn dhcp_bpf_filter[] = {
	/* Make sure this is an IP packet... */
	BPF_STMT(BPF_LD + BPF_H + BPF_ABS, 12),
	BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, ETHERTYPE_IP, 0, 8),

	/* Make sure it's a UDP packet... */
	BPF_STMT(BPF_LD + BPF_B + BPF_ABS, 23),
	BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, IPPROTO_UDP, 0, 6),

	/* Make sure this isn't a fragment... */
	BPF_STMT(BPF_LD + BPF_H + BPF_ABS, 20),
	BPF_JUMP(BPF_JMP + BPF_JSET + BPF_K, 0x1fff, 4, 0),

	/* Get the IP header length... */
	BPF_STMT(BPF_LDX + BPF_B + BPF_MSH, 14),

	/* Make sure it's to the right port... */
	BPF_STMT(BPF_LD + BPF_H + BPF_IND, 16),
	BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, SERVER_PORT, 0, 1),

	/* If we passed all the tests, ask for the whole packet. */
	BPF_STMT(BPF_RET+BPF_K, (u_int)-1),

	/* Otherwise, drop it. */
	BPF_STMT(BPF_RET+BPF_K, 0),
};

int dhcp_bpf_filter_len = sizeof(dhcp_bpf_filter) / sizeof(struct bpf_insn);

/*
 * Packet filter program: encapsulated 'ip and udp and dst port SERVER_PORT'
 */
struct bpf_insn dhcp_bpf_efilter[] = {
	/* Make sure this is an encapsulated AF_INET packet... */
	BPF_STMT(BPF_LD + BPF_W + BPF_ABS, 0),
	BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, AF_INET << 24, 0, 10),

	/* Make sure it's an IPIP packet... */
	BPF_STMT(BPF_LD + BPF_B + BPF_ABS, 21),
	BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, IPPROTO_IPIP, 0, 8),

	/* Make sure it's an encapsulated UDP packet... */
	BPF_STMT(BPF_LD + BPF_B + BPF_ABS, 41),
	BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, IPPROTO_UDP, 0, 6),

	/* Make sure this isn't a fragment... */
	BPF_STMT(BPF_LD + BPF_H + BPF_ABS, 38),
	BPF_JUMP(BPF_JMP + BPF_JSET + BPF_K, 0x1fff, 4, 0),

	/* Get the IP header length... */
	BPF_STMT(BPF_LDX + BPF_B + BPF_MSH, 32),

	/* Make sure it's to the right port... */
	BPF_STMT(BPF_LD + BPF_H + BPF_IND, 34),
	BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, SERVER_PORT, 0, 1),

	/* If we passed all the tests, ask for the whole packet. */
	BPF_STMT(BPF_RET+BPF_K, (u_int)-1),

	/* Otherwise, drop it. */
	BPF_STMT(BPF_RET+BPF_K, 0),
};

int dhcp_bpf_efilter_len = sizeof(dhcp_bpf_efilter) / sizeof(struct bpf_insn);

/*
 * Packet write filter program: 'ip and udp and src port CLIENT_PORT'
 */
struct bpf_insn dhcp_bpf_swfilter[] = {
	/* Make sure this is an IP packet... */
	BPF_STMT(BPF_LD + BPF_H + BPF_ABS, 12),
	BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, ETHERTYPE_IP, 0, 8),

	/* Make sure it's a UDP packet... */
	BPF_STMT(BPF_LD + BPF_B + BPF_ABS, 23),
	BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, IPPROTO_UDP, 0, 6),

	/* Make sure this isn't a fragment... */
	BPF_STMT(BPF_LD + BPF_H + BPF_ABS, 20),
	BPF_JUMP(BPF_JMP + BPF_JSET + BPF_K, 0x1fff, 4, 0),

	/* Get the IP header length... */
	BPF_STMT(BPF_LDX + BPF_B + BPF_MSH, 14),

	/* Make sure it's from the right port... */
	BPF_STMT(BPF_LD + BPF_H + BPF_IND, 14),
	BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, CLIENT_PORT, 0, 1),

	/* If we passed all the tests, ask for the whole packet. */
	BPF_STMT(BPF_RET+BPF_K, (u_int)-1),

	/* Otherwise, drop it. */
	BPF_STMT(BPF_RET+BPF_K, 0),
};

int dhcp_bpf_swfilter_len = sizeof(dhcp_bpf_swfilter) /
	sizeof(struct bpf_insn);

/*
 * Packet write filter program: 'ip and udp and src port SERVER_PORT'
 */
struct bpf_insn dhcp_bpf_wfilter[] = {
	/* Make sure this is an IP packet... */
	BPF_STMT(BPF_LD + BPF_H + BPF_ABS, 12),
	BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, ETHERTYPE_IP, 0, 8),

	/* Make sure it's a UDP packet... */
	BPF_STMT(BPF_LD + BPF_B + BPF_ABS, 23),
	BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, IPPROTO_UDP, 0, 6),

	/* Make sure this isn't a fragment... */
	BPF_STMT(BPF_LD + BPF_H + BPF_ABS, 20),
	BPF_JUMP(BPF_JMP + BPF_JSET + BPF_K, 0x1fff, 4, 0),

	/* Get the IP header length... */
	BPF_STMT(BPF_LDX + BPF_B + BPF_MSH, 14),

	/* Make sure it's from the right port... */
	BPF_STMT(BPF_LD + BPF_H + BPF_IND, 14),
	BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, SERVER_PORT, 0, 1),

	/* If we passed all the tests, ask for the whole packet. */
	BPF_STMT(BPF_RET+BPF_K, (u_int)-1),

	/* Otherwise, drop it. */
	BPF_STMT(BPF_RET+BPF_K, 0),
};

int dhcp_bpf_wfilter_len = sizeof(dhcp_bpf_wfilter) / sizeof(struct bpf_insn);

void
if_register_receive(struct interface_info *info, int isserver)
{
	struct bpf_version v;
	struct bpf_program p;
	int flag = 1, sz, cmplt = 0;

	/* Open a BPF device and hang it on this interface... */
	info->rfdesc = if_register_bpf(info);

	/* Make sure the BPF version is in range... */
	if (ioctl(info->rfdesc, BIOCVERSION, &v) == -1)
		fatal("Can't get BPF version");

	if (v.bv_major != BPF_MAJOR_VERSION ||
	    v.bv_minor < BPF_MINOR_VERSION)
		fatalx("Kernel BPF version out of range - recompile dhcpd!");

	/*
	 * Set immediate mode so that reads return as soon as a packet
	 * comes in, rather than waiting for the input buffer to fill
	 * with packets.
	 */
	if (ioctl(info->rfdesc, BIOCIMMEDIATE, &flag) == -1)
		fatal("Can't set immediate mode on bpf device");

	/* make sure kernel fills in the source ethernet address */
	if (ioctl(info->rfdesc, BIOCSHDRCMPLT, &cmplt) == -1)
		fatal("Can't set header complete flag on bpf device");

	/* Get the required BPF buffer length from the kernel. */
	if (ioctl(info->rfdesc, BIOCGBLEN, &sz) == -1)
		fatal("Can't get bpf buffer length");
	info->rbuf_max = sz;
	info->rbuf = malloc(info->rbuf_max);
	if (!info->rbuf)
		fatalx("Can't allocate %lu bytes for bpf input buffer.",
		    (unsigned long)info->rbuf_max);
	info->rbuf_offset = 0;
	info->rbuf_len = 0;

	/* Set up the bpf filter program structure. */
	if (isserver) {
		p.bf_len = dhcp_bpf_sfilter_len;
		p.bf_insns = dhcp_bpf_sfilter;
	} else if (info->hw_address.htype == HTYPE_IPSEC_TUNNEL) {
		p.bf_len = dhcp_bpf_efilter_len;
		p.bf_insns = dhcp_bpf_efilter;
	} else {
		p.bf_len = dhcp_bpf_filter_len;
		p.bf_insns = dhcp_bpf_filter;
	}
	if (ioctl(info->rfdesc, BIOCSETF, &p) == -1)
		fatal("Can't install packet filter program");

	/* Set up the bpf write filter program structure. */
	if (isserver) {
		p.bf_len = dhcp_bpf_swfilter_len;
		p.bf_insns = dhcp_bpf_swfilter;
	} else {
		p.bf_len = dhcp_bpf_wfilter_len;
		p.bf_insns = dhcp_bpf_wfilter;
	}

	if (ioctl(info->rfdesc, BIOCSETWF, &p) == -1)
		fatal("Can't install write filter program");

	/* make sure these settings cannot be changed after dropping privs */
	if (ioctl(info->rfdesc, BIOCLOCK) == -1)
		fatal("Failed to lock bpf descriptor");
}

ssize_t
send_packet(struct interface_info *interface,
    struct dhcp_packet *raw, size_t len, struct packet_ctx *pc)
{
	unsigned char buf[256];
	struct iovec iov[2];
	ssize_t bufp;
	int result;

	result = -1;

	if (interface->hw_address.htype == HTYPE_IPSEC_TUNNEL) {
		socklen_t slen = pc->pc_dst.ss_len;
		result = sendto(server_fd, raw, len, 0,
		    (struct sockaddr *)&pc->pc_dst, slen);
		goto done;
	}

	/* Assemble the headers... */
	if ((bufp = assemble_hw_header(buf, sizeof(buf), 0, pc,
	    interface->hw_address.htype)) == -1)
		goto done;
	if ((bufp = assemble_udp_ip_header(buf, sizeof(buf), bufp, pc,
	    (unsigned char *)raw, len)) == -1)
		goto done;

	/* Fire it off */
	iov[0].iov_base = (char *)buf;
	iov[0].iov_len = bufp;
	iov[1].iov_base = (char *)raw;
	iov[1].iov_len = len;

	result = writev(interface->wfdesc, iov, 2);
 done:
	if (result == -1)
		log_warn("send_packet");
	return (result);
}

ssize_t
receive_packet(struct interface_info *interface, unsigned char *buf,
    size_t len, struct packet_ctx *pc)
{
	int length = 0;
	ssize_t offset = 0;
	struct bpf_hdr hdr;

	/*
	 * All this complexity is because BPF doesn't guarantee that
	 * only one packet will be returned at a time.  We're getting
	 * what we deserve, though - this is a terrible abuse of the BPF
	 * interface.  Sigh.
	 */

	/* Process packets until we get one we can return or until we've
	 * done a read and gotten nothing we can return...
	 */
	do {
		/* If the buffer is empty, fill it. */
		if (interface->rbuf_offset == interface->rbuf_len) {
			length = read(interface->rfdesc, interface->rbuf,
			    interface->rbuf_max);
			if (length <= 0)
				return (length);
			interface->rbuf_offset = 0;
			interface->rbuf_len = length;
		}

		/*
		 * If there isn't room for a whole bpf header, something
		 * went wrong, but we'll ignore it and hope it goes
		 * away... XXX
		 */
		if (interface->rbuf_len - interface->rbuf_offset <
		    sizeof(hdr)) {
			interface->rbuf_offset = interface->rbuf_len;
			continue;
		}

		/* Copy out a bpf header... */
		memcpy(&hdr, &interface->rbuf[interface->rbuf_offset],
		    sizeof(hdr));

		/*
		 * If the bpf header plus data doesn't fit in what's
		 * left of the buffer, stick head in sand yet again...
		 */
		if (interface->rbuf_offset + hdr.bh_hdrlen + hdr.bh_caplen >
		    interface->rbuf_len) {
			interface->rbuf_offset = interface->rbuf_len;
			continue;
		}

		/*
		 * If the captured data wasn't the whole packet, or if
		 * the packet won't fit in the input buffer, all we can
		 * do is drop it.
		 */
		if (hdr.bh_caplen != hdr.bh_datalen) {
			interface->rbuf_offset += hdr.bh_hdrlen =
			    hdr.bh_caplen;
			continue;
		}

		/* Skip over the BPF header... */
		interface->rbuf_offset += hdr.bh_hdrlen;

		/* Decode the physical header... */
		offset = decode_hw_header(interface->rbuf,
		    interface->rbuf_len, interface->rbuf_offset, pc,
		    interface->hw_address.htype);

		/*
		 * If decoding or a physical layer checksum failed
		 * (dunno of any physical layer that supports this, but WTH),
		 * skip this packet.
		 */
		if (offset < 0) {
			interface->rbuf_offset += hdr.bh_caplen;
			continue;
		}

		/* Decode the IP and UDP headers... */
		offset = decode_udp_ip_header(interface->rbuf,
		    interface->rbuf_len, offset, pc, hdr.bh_csumflags);

		/* If the IP or UDP checksum was bad, skip the packet... */
		if (offset < 0) {
			interface->rbuf_offset += hdr.bh_caplen;
			continue;
		}

		hdr.bh_caplen -= offset - interface->rbuf_offset;
		interface->rbuf_offset = offset;

		/*
		 * If there's not enough room to stash the packet data,
		 * we have to skip it (this shouldn't happen in real
		 * life, though).
		 */
		if (hdr.bh_caplen > len) {
			interface->rbuf_offset += hdr.bh_caplen;
			continue;
		}

		/* Copy out the data in the packet... */
		memcpy(buf, interface->rbuf + interface->rbuf_offset,
		    hdr.bh_caplen);
		interface->rbuf_offset += hdr.bh_caplen;
		return (hdr.bh_caplen);
	} while (!length);
	return (0);
}
