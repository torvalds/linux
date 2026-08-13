/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 Isovalent */

#ifndef _BPF_KSOCK_H
#define _BPF_KSOCK_H

#include <linux/types.h>
#include <linux/in.h>
#include <linux/in6.h>

/**
 * struct bpf_ksock_create_opts - BPF kernel socket creation parameters
 * @family:	Address family: AF_INET or AF_INET6.
 * @type:	Socket type: only SOCK_DGRAM supported for now.
 * @protocol:	Protocol number (e.g. IPPROTO_UDP), or 0 for the default protocol
 *		of the given type.
 * @reserved:	Must be zero. Reserved for future use.
 */
struct bpf_ksock_create_opts {
	__u8 family;
	__u8 type;
	__u8 protocol;
	__u8 reserved;
};

/**
 * union bpf_ksock_addr - IPv4 or IPv6 socket address
 * @sin: IPv4 socket address.
 * @sin6: IPv6 socket address.
 */
union bpf_ksock_addr {
	struct sockaddr_in sin;
	struct sockaddr_in6 sin6;
};

#endif /* _BPF_KSOCK_H */
