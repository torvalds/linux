/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2020 Facebook */

#ifndef _TEST_TCP_HDR_OPTIONS_H
#define _TEST_TCP_HDR_OPTIONS_H

struct bpf_test_option {
	__u8 flags;
	__u8 max_delack_ms;
	__u8 rand;
} __attribute__((packed));

enum {
	OPTION_RESEND,
	OPTION_MAX_DELACK_MS,
	OPTION_RAND,
	__NR_OPTION_FLAGS,
};

#define OPTION_F_RESEND		(1 << OPTION_RESEND)
#define OPTION_F_MAX_DELACK_MS	(1 << OPTION_MAX_DELACK_MS)
#define OPTION_F_RAND		(1 << OPTION_RAND)
#define OPTION_MASK		((1 << __NR_OPTION_FLAGS) - 1)

#define TEST_OPTION_FLAGS(flags, option) (1 & ((flags) >> (option)))
#define SET_OPTION_FLAGS(flags, option)	((flags) |= (1 << (option)))

/* Store in bpf_sk_storage */
struct hdr_stg {
	bool active;
	bool resend_syn; /* active side only */
	bool syncookie;  /* passive side only */
	bool fastopen;	/* passive side only */
};

struct linum_err {
	unsigned int linum;
	int err;
};

#define TCPHDR_FIN 0x01
#define TCPHDR_SYN 0x02
#define TCPHDR_RST 0x04
#define TCPHDR_PSH 0x08
#define TCPHDR_ACK 0x10
#define TCPHDR_URG 0x20
#define TCPHDR_ECE 0x40
#define TCPHDR_CWR 0x80
#define TCPHDR_SYNACK (TCPHDR_SYN | TCPHDR_ACK)

#define TCPOPT_EOL		0
#define TCPOPT_NOP		1
#define TCPOPT_WINDOW		3
#define TCPOPT_EXP		254

#define TCP_BPF_EXPOPT_BASE_LEN 4
#define MAX_TCP_HDR_LEN		60
#define MAX_TCP_OPTION_SPACE	40

#ifdef BPF_PROG_TEST_TCP_HDR_OPTIONS

#define CG_OK	1
#define CG_ERR	0

#ifndef SOL_TCP
#define SOL_TCP 6
#endif

struct tcp_exprm_opt {
	__u8 kind;
	__u8 len;
	__u16 magic;
	union {
		__u8 data[4];
		__u32 data32;
	};
} __attribute__((packed));

struct tcp_opt {
	__u8 kind;
	__u8 len;
	union {
		__u8 data[4];
		__u32 data32;
	};
} __attribute__((packed));

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 2);
	__type(key, int);
	__type(value, struct linum_err);
} lport_linum_map SEC(".maps");

static inline unsigned int tcp_hdrlen(const struct tcphdr *th)
{
	return th->doff << 2;
}

static inline __u8 skops_tcp_flags(const struct bpf_sock_ops *skops)
{
	return skops->skb_tcp_flags;
}

static inline void clear_hdr_cb_flags(struct bpf_sock_ops *skops)
{
	bpf_sock_ops_cb_flags_set(skops,
				  skops->bpf_sock_ops_cb_flags &
				  ~(BPF_SOCK_OPS_PARSE_UNKNOWN_HDR_OPT_CB_FLAG |
				    BPF_SOCK_OPS_WRITE_HDR_OPT_CB_FLAG));
}

static inline void set_hdr_cb_flags(struct bpf_sock_ops *skops, __u32 extra)
{
	bpf_sock_ops_cb_flags_set(skops,
				  skops->bpf_sock_ops_cb_flags |
				  BPF_SOCK_OPS_PARSE_UNKNOWN_HDR_OPT_CB_FLAG |
				  BPF_SOCK_OPS_WRITE_HDR_OPT_CB_FLAG |
				  extra);
}
static inline void
clear_parse_all_hdr_cb_flags(struct bpf_sock_ops *skops)
{
	bpf_sock_ops_cb_flags_set(skops,
				  skops->bpf_sock_ops_cb_flags &
				  ~BPF_SOCK_OPS_PARSE_ALL_HDR_OPT_CB_FLAG);
}

static inline void
set_parse_all_hdr_cb_flags(struct bpf_sock_ops *skops)
{
	bpf_sock_ops_cb_flags_set(skops,
				  skops->bpf_sock_ops_cb_flags |
				  BPF_SOCK_OPS_PARSE_ALL_HDR_OPT_CB_FLAG);
}

#define RET_CG_ERR(__err) ({			\
	struct linum_err __linum_err;		\
	int __lport;				\
						\
	__linum_err.linum = __LINE__;		\
	__linum_err.err = __err;		\
	__lport = skops->local_port;		\
	bpf_map_update_elem(&lport_linum_map, &__lport, &__linum_err, BPF_NOEXIST); \
	clear_hdr_cb_flags(skops);					\
	clear_parse_all_hdr_cb_flags(skops);				\
	return CG_ERR;							\
})

#endif /* BPF_PROG_TEST_TCP_HDR_OPTIONS */

#endif /* _TEST_TCP_HDR_OPTIONS_H */
