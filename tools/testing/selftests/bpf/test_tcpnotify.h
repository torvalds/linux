// SPDX-License-Identifier: GPL-2.0

#ifndef _TEST_TCPBPF_H
#define _TEST_TCPBPF_H

struct tcpnotify_globals {
	__u32 total_retrans;
	__u32 ncalls;
};

struct tcp_notifier {
	__u8    type;
	__u8    subtype;
	__u8    source;
	__u8    hash;
};

#define	TESTPORT	12877
#endif
