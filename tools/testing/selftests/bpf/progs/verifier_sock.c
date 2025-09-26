// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/sock.c */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

struct {
	__uint(type, BPF_MAP_TYPE_REUSEPORT_SOCKARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u64);
} map_reuseport_array SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_SOCKHASH);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, int);
} map_sockhash SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_SOCKMAP);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, int);
} map_sockmap SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_XSKMAP);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, int);
} map_xskmap SEC(".maps");

struct val {
	int cnt;
	struct bpf_spin_lock l;
};

struct {
	__uint(type, BPF_MAP_TYPE_SK_STORAGE);
	__uint(max_entries, 0);
	__type(key, int);
	__type(value, struct val);
	__uint(map_flags, BPF_F_NO_PREALLOC);
} sk_storage_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__uint(max_entries, 1);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(__u32));
} jmp_table SEC(".maps");

SEC("cgroup/skb")
__description("skb->sk: no NULL check")
__failure __msg("invalid mem access 'sock_common_or_null'")
__failure_unpriv
__naked void skb_sk_no_null_check(void)
{
	asm volatile ("					\
	r1 = *(u64*)(r1 + %[__sk_buff_sk]);		\
	r0 = *(u32*)(r1 + 0);				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__sk_buff_sk, offsetof(struct __sk_buff, sk))
	: __clobber_all);
}

SEC("cgroup/skb")
__description("skb->sk: sk->family [non fullsock field]")
__success __success_unpriv __retval(0)
__naked void sk_family_non_fullsock_field_1(void)
{
	asm volatile ("					\
	r1 = *(u64*)(r1 + %[__sk_buff_sk]);		\
	if r1 != 0 goto l0_%=;				\
	r0 = 0;						\
	exit;						\
l0_%=:	r0 = *(u32*)(r1 + %[bpf_sock_family]);		\
	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__sk_buff_sk, offsetof(struct __sk_buff, sk)),
	  __imm_const(bpf_sock_family, offsetof(struct bpf_sock, family))
	: __clobber_all);
}

SEC("cgroup/skb")
__description("skb->sk: sk->type [fullsock field]")
__failure __msg("invalid sock_common access")
__failure_unpriv
__naked void sk_sk_type_fullsock_field_1(void)
{
	asm volatile ("					\
	r1 = *(u64*)(r1 + %[__sk_buff_sk]);		\
	if r1 != 0 goto l0_%=;				\
	r0 = 0;						\
	exit;						\
l0_%=:	r0 = *(u32*)(r1 + %[bpf_sock_type]);		\
	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__sk_buff_sk, offsetof(struct __sk_buff, sk)),
	  __imm_const(bpf_sock_type, offsetof(struct bpf_sock, type))
	: __clobber_all);
}

SEC("cgroup/skb")
__description("bpf_sk_fullsock(skb->sk): no !skb->sk check")
__failure __msg("type=sock_common_or_null expected=sock_common")
__failure_unpriv
__naked void sk_no_skb_sk_check_1(void)
{
	asm volatile ("					\
	r1 = *(u64*)(r1 + %[__sk_buff_sk]);		\
	call %[bpf_sk_fullsock];			\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_sk_fullsock),
	  __imm_const(__sk_buff_sk, offsetof(struct __sk_buff, sk))
	: __clobber_all);
}

SEC("cgroup/skb")
__description("sk_fullsock(skb->sk): no NULL check on ret")
__failure __msg("invalid mem access 'sock_or_null'")
__failure_unpriv
__naked void no_null_check_on_ret_1(void)
{
	asm volatile ("					\
	r1 = *(u64*)(r1 + %[__sk_buff_sk]);		\
	if r1 != 0 goto l0_%=;				\
	r0 = 0;						\
	exit;						\
l0_%=:	call %[bpf_sk_fullsock];			\
	r0 = *(u32*)(r0 + %[bpf_sock_type]);		\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_sk_fullsock),
	  __imm_const(__sk_buff_sk, offsetof(struct __sk_buff, sk)),
	  __imm_const(bpf_sock_type, offsetof(struct bpf_sock, type))
	: __clobber_all);
}

SEC("cgroup/skb")
__description("sk_fullsock(skb->sk): sk->type [fullsock field]")
__success __success_unpriv __retval(0)
__naked void sk_sk_type_fullsock_field_2(void)
{
	asm volatile ("					\
	r1 = *(u64*)(r1 + %[__sk_buff_sk]);		\
	if r1 != 0 goto l0_%=;				\
	r0 = 0;						\
	exit;						\
l0_%=:	call %[bpf_sk_fullsock];			\
	if r0 != 0 goto l1_%=;				\
	r0 = 0;						\
	exit;						\
l1_%=:	r0 = *(u32*)(r0 + %[bpf_sock_type]);		\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_sk_fullsock),
	  __imm_const(__sk_buff_sk, offsetof(struct __sk_buff, sk)),
	  __imm_const(bpf_sock_type, offsetof(struct bpf_sock, type))
	: __clobber_all);
}

SEC("cgroup/skb")
__description("sk_fullsock(skb->sk): sk->family [non fullsock field]")
__success __success_unpriv __retval(0)
__naked void sk_family_non_fullsock_field_2(void)
{
	asm volatile ("					\
	r1 = *(u64*)(r1 + %[__sk_buff_sk]);		\
	if r1 != 0 goto l0_%=;				\
	r0 = 0;						\
	exit;						\
l0_%=:	call %[bpf_sk_fullsock];			\
	if r0 != 0 goto l1_%=;				\
	exit;						\
l1_%=:	r0 = *(u32*)(r0 + %[bpf_sock_family]);		\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_sk_fullsock),
	  __imm_const(__sk_buff_sk, offsetof(struct __sk_buff, sk)),
	  __imm_const(bpf_sock_family, offsetof(struct bpf_sock, family))
	: __clobber_all);
}

SEC("cgroup/skb")
__description("sk_fullsock(skb->sk): sk->state [narrow load]")
__success __success_unpriv __retval(0)
__naked void sk_sk_state_narrow_load(void)
{
	asm volatile ("					\
	r1 = *(u64*)(r1 + %[__sk_buff_sk]);		\
	if r1 != 0 goto l0_%=;				\
	r0 = 0;						\
	exit;						\
l0_%=:	call %[bpf_sk_fullsock];			\
	if r0 != 0 goto l1_%=;				\
	r0 = 0;						\
	exit;						\
l1_%=:	r0 = *(u8*)(r0 + %[bpf_sock_state]);		\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_sk_fullsock),
	  __imm_const(__sk_buff_sk, offsetof(struct __sk_buff, sk)),
	  __imm_const(bpf_sock_state, offsetof(struct bpf_sock, state))
	: __clobber_all);
}

SEC("cgroup/skb")
__description("sk_fullsock(skb->sk): sk->dst_port [word load] (backward compatibility)")
__success __success_unpriv __retval(0)
__naked void port_word_load_backward_compatibility(void)
{
	asm volatile ("					\
	r1 = *(u64*)(r1 + %[__sk_buff_sk]);		\
	if r1 != 0 goto l0_%=;				\
	r0 = 0;						\
	exit;						\
l0_%=:	call %[bpf_sk_fullsock];			\
	if r0 != 0 goto l1_%=;				\
	r0 = 0;						\
	exit;						\
l1_%=:	r0 = *(u32*)(r0 + %[bpf_sock_dst_port]);	\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_sk_fullsock),
	  __imm_const(__sk_buff_sk, offsetof(struct __sk_buff, sk)),
	  __imm_const(bpf_sock_dst_port, offsetof(struct bpf_sock, dst_port))
	: __clobber_all);
}

SEC("cgroup/skb")
__description("sk_fullsock(skb->sk): sk->dst_port [half load]")
__success __success_unpriv __retval(0)
__naked void sk_dst_port_half_load(void)
{
	asm volatile ("					\
	r1 = *(u64*)(r1 + %[__sk_buff_sk]);		\
	if r1 != 0 goto l0_%=;				\
	r0 = 0;						\
	exit;						\
l0_%=:	call %[bpf_sk_fullsock];			\
	if r0 != 0 goto l1_%=;				\
	r0 = 0;						\
	exit;						\
l1_%=:	r0 = *(u16*)(r0 + %[bpf_sock_dst_port]);	\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_sk_fullsock),
	  __imm_const(__sk_buff_sk, offsetof(struct __sk_buff, sk)),
	  __imm_const(bpf_sock_dst_port, offsetof(struct bpf_sock, dst_port))
	: __clobber_all);
}

SEC("cgroup/skb")
__description("sk_fullsock(skb->sk): sk->dst_port [half load] (invalid)")
__failure __msg("invalid sock access")
__failure_unpriv
__naked void dst_port_half_load_invalid_1(void)
{
	asm volatile ("					\
	r1 = *(u64*)(r1 + %[__sk_buff_sk]);		\
	if r1 != 0 goto l0_%=;				\
	r0 = 0;						\
	exit;						\
l0_%=:	call %[bpf_sk_fullsock];			\
	if r0 != 0 goto l1_%=;				\
	r0 = 0;						\
	exit;						\
l1_%=:	r0 = *(u16*)(r0 + %[__imm_0]);			\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_sk_fullsock),
	  __imm_const(__imm_0, offsetof(struct bpf_sock, dst_port) + 2),
	  __imm_const(__sk_buff_sk, offsetof(struct __sk_buff, sk))
	: __clobber_all);
}

SEC("cgroup/skb")
__description("sk_fullsock(skb->sk): sk->dst_port [byte load]")
__success __success_unpriv __retval(0)
__naked void sk_dst_port_byte_load(void)
{
	asm volatile ("					\
	r1 = *(u64*)(r1 + %[__sk_buff_sk]);		\
	if r1 != 0 goto l0_%=;				\
	r0 = 0;						\
	exit;						\
l0_%=:	call %[bpf_sk_fullsock];			\
	if r0 != 0 goto l1_%=;				\
	r0 = 0;						\
	exit;						\
l1_%=:	r2 = *(u8*)(r0 + %[bpf_sock_dst_port]);		\
	r2 = *(u8*)(r0 + %[__imm_0]);			\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_sk_fullsock),
	  __imm_const(__imm_0, offsetof(struct bpf_sock, dst_port) + 1),
	  __imm_const(__sk_buff_sk, offsetof(struct __sk_buff, sk)),
	  __imm_const(bpf_sock_dst_port, offsetof(struct bpf_sock, dst_port))
	: __clobber_all);
}

SEC("cgroup/skb")
__description("sk_fullsock(skb->sk): sk->dst_port [byte load] (invalid)")
__failure __msg("invalid sock access")
__failure_unpriv
__naked void dst_port_byte_load_invalid(void)
{
	asm volatile ("					\
	r1 = *(u64*)(r1 + %[__sk_buff_sk]);		\
	if r1 != 0 goto l0_%=;				\
	r0 = 0;						\
	exit;						\
l0_%=:	call %[bpf_sk_fullsock];			\
	if r0 != 0 goto l1_%=;				\
	r0 = 0;						\
	exit;						\
l1_%=:	r0 = *(u8*)(r0 + %[__imm_0]);			\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_sk_fullsock),
	  __imm_const(__imm_0, offsetof(struct bpf_sock, dst_port) + 2),
	  __imm_const(__sk_buff_sk, offsetof(struct __sk_buff, sk))
	: __clobber_all);
}

SEC("cgroup/skb")
__description("sk_fullsock(skb->sk): past sk->dst_port [half load] (invalid)")
__failure __msg("invalid sock access")
__failure_unpriv
__naked void dst_port_half_load_invalid_2(void)
{
	asm volatile ("					\
	r1 = *(u64*)(r1 + %[__sk_buff_sk]);		\
	if r1 != 0 goto l0_%=;				\
	r0 = 0;						\
	exit;						\
l0_%=:	call %[bpf_sk_fullsock];			\
	if r0 != 0 goto l1_%=;				\
	r0 = 0;						\
	exit;						\
l1_%=:	r0 = *(u16*)(r0 + %[bpf_sock_dst_port__end]);	\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_sk_fullsock),
	  __imm_const(__sk_buff_sk, offsetof(struct __sk_buff, sk)),
	  __imm_const(bpf_sock_dst_port__end, offsetofend(struct bpf_sock, dst_port))
	: __clobber_all);
}

SEC("cgroup/skb")
__description("sk_fullsock(skb->sk): sk->dst_ip6 [load 2nd byte]")
__success __success_unpriv __retval(0)
__naked void dst_ip6_load_2nd_byte(void)
{
	asm volatile ("					\
	r1 = *(u64*)(r1 + %[__sk_buff_sk]);		\
	if r1 != 0 goto l0_%=;				\
	r0 = 0;						\
	exit;						\
l0_%=:	call %[bpf_sk_fullsock];			\
	if r0 != 0 goto l1_%=;				\
	r0 = 0;						\
	exit;						\
l1_%=:	r0 = *(u8*)(r0 + %[__imm_0]);			\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_sk_fullsock),
	  __imm_const(__imm_0, offsetof(struct bpf_sock, dst_ip6[0]) + 1),
	  __imm_const(__sk_buff_sk, offsetof(struct __sk_buff, sk))
	: __clobber_all);
}

SEC("cgroup/skb")
__description("sk_fullsock(skb->sk): sk->type [narrow load]")
__success __success_unpriv __retval(0)
__naked void sk_sk_type_narrow_load(void)
{
	asm volatile ("					\
	r1 = *(u64*)(r1 + %[__sk_buff_sk]);		\
	if r1 != 0 goto l0_%=;				\
	r0 = 0;						\
	exit;						\
l0_%=:	call %[bpf_sk_fullsock];			\
	if r0 != 0 goto l1_%=;				\
	r0 = 0;						\
	exit;						\
l1_%=:	r0 = *(u8*)(r0 + %[bpf_sock_type]);		\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_sk_fullsock),
	  __imm_const(__sk_buff_sk, offsetof(struct __sk_buff, sk)),
	  __imm_const(bpf_sock_type, offsetof(struct bpf_sock, type))
	: __clobber_all);
}

SEC("cgroup/skb")
__description("sk_fullsock(skb->sk): sk->protocol [narrow load]")
__success __success_unpriv __retval(0)
__naked void sk_sk_protocol_narrow_load(void)
{
	asm volatile ("					\
	r1 = *(u64*)(r1 + %[__sk_buff_sk]);		\
	if r1 != 0 goto l0_%=;				\
	r0 = 0;						\
	exit;						\
l0_%=:	call %[bpf_sk_fullsock];			\
	if r0 != 0 goto l1_%=;				\
	r0 = 0;						\
	exit;						\
l1_%=:	r0 = *(u8*)(r0 + %[bpf_sock_protocol]);		\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_sk_fullsock),
	  __imm_const(__sk_buff_sk, offsetof(struct __sk_buff, sk)),
	  __imm_const(bpf_sock_protocol, offsetof(struct bpf_sock, protocol))
	: __clobber_all);
}

SEC("cgroup/skb")
__description("sk_fullsock(skb->sk): beyond last field")
__failure __msg("invalid sock access")
__failure_unpriv
__naked void skb_sk_beyond_last_field_1(void)
{
	asm volatile ("					\
	r1 = *(u64*)(r1 + %[__sk_buff_sk]);		\
	if r1 != 0 goto l0_%=;				\
	r0 = 0;						\
	exit;						\
l0_%=:	call %[bpf_sk_fullsock];			\
	if r0 != 0 goto l1_%=;				\
	r0 = 0;						\
	exit;						\
l1_%=:	r0 = *(u32*)(r0 + %[bpf_sock_rx_queue_mapping__end]);\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_sk_fullsock),
	  __imm_const(__sk_buff_sk, offsetof(struct __sk_buff, sk)),
	  __imm_const(bpf_sock_rx_queue_mapping__end, offsetofend(struct bpf_sock, rx_queue_mapping))
	: __clobber_all);
}

SEC("cgroup/skb")
__description("bpf_tcp_sock(skb->sk): no !skb->sk check")
__failure __msg("type=sock_common_or_null expected=sock_common")
__failure_unpriv
__naked void sk_no_skb_sk_check_2(void)
{
	asm volatile ("					\
	r1 = *(u64*)(r1 + %[__sk_buff_sk]);		\
	call %[bpf_tcp_sock];				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_tcp_sock),
	  __imm_const(__sk_buff_sk, offsetof(struct __sk_buff, sk))
	: __clobber_all);
}

SEC("cgroup/skb")
__description("bpf_tcp_sock(skb->sk): no NULL check on ret")
__failure __msg("invalid mem access 'tcp_sock_or_null'")
__failure_unpriv
__naked void no_null_check_on_ret_2(void)
{
	asm volatile ("					\
	r1 = *(u64*)(r1 + %[__sk_buff_sk]);		\
	if r1 != 0 goto l0_%=;				\
	r0 = 0;						\
	exit;						\
l0_%=:	call %[bpf_tcp_sock];				\
	r0 = *(u32*)(r0 + %[bpf_tcp_sock_snd_cwnd]);	\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_tcp_sock),
	  __imm_const(__sk_buff_sk, offsetof(struct __sk_buff, sk)),
	  __imm_const(bpf_tcp_sock_snd_cwnd, offsetof(struct bpf_tcp_sock, snd_cwnd))
	: __clobber_all);
}

SEC("cgroup/skb")
__description("bpf_tcp_sock(skb->sk): tp->snd_cwnd")
__success __success_unpriv __retval(0)
__naked void skb_sk_tp_snd_cwnd_1(void)
{
	asm volatile ("					\
	r1 = *(u64*)(r1 + %[__sk_buff_sk]);		\
	if r1 != 0 goto l0_%=;				\
	r0 = 0;						\
	exit;						\
l0_%=:	call %[bpf_tcp_sock];				\
	if r0 != 0 goto l1_%=;				\
	exit;						\
l1_%=:	r0 = *(u32*)(r0 + %[bpf_tcp_sock_snd_cwnd]);	\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_tcp_sock),
	  __imm_const(__sk_buff_sk, offsetof(struct __sk_buff, sk)),
	  __imm_const(bpf_tcp_sock_snd_cwnd, offsetof(struct bpf_tcp_sock, snd_cwnd))
	: __clobber_all);
}

SEC("cgroup/skb")
__description("bpf_tcp_sock(skb->sk): tp->bytes_acked")
__success __success_unpriv __retval(0)
__naked void skb_sk_tp_bytes_acked(void)
{
	asm volatile ("					\
	r1 = *(u64*)(r1 + %[__sk_buff_sk]);		\
	if r1 != 0 goto l0_%=;				\
	r0 = 0;						\
	exit;						\
l0_%=:	call %[bpf_tcp_sock];				\
	if r0 != 0 goto l1_%=;				\
	exit;						\
l1_%=:	r0 = *(u64*)(r0 + %[bpf_tcp_sock_bytes_acked]);	\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_tcp_sock),
	  __imm_const(__sk_buff_sk, offsetof(struct __sk_buff, sk)),
	  __imm_const(bpf_tcp_sock_bytes_acked, offsetof(struct bpf_tcp_sock, bytes_acked))
	: __clobber_all);
}

SEC("cgroup/skb")
__description("bpf_tcp_sock(skb->sk): beyond last field")
__failure __msg("invalid tcp_sock access")
__failure_unpriv
__naked void skb_sk_beyond_last_field_2(void)
{
	asm volatile ("					\
	r1 = *(u64*)(r1 + %[__sk_buff_sk]);		\
	if r1 != 0 goto l0_%=;				\
	r0 = 0;						\
	exit;						\
l0_%=:	call %[bpf_tcp_sock];				\
	if r0 != 0 goto l1_%=;				\
	exit;						\
l1_%=:	r0 = *(u64*)(r0 + %[bpf_tcp_sock_bytes_acked__end]);\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_tcp_sock),
	  __imm_const(__sk_buff_sk, offsetof(struct __sk_buff, sk)),
	  __imm_const(bpf_tcp_sock_bytes_acked__end, offsetofend(struct bpf_tcp_sock, bytes_acked))
	: __clobber_all);
}

SEC("cgroup/skb")
__description("bpf_tcp_sock(bpf_sk_fullsock(skb->sk)): tp->snd_cwnd")
__success __success_unpriv __retval(0)
__naked void skb_sk_tp_snd_cwnd_2(void)
{
	asm volatile ("					\
	r1 = *(u64*)(r1 + %[__sk_buff_sk]);		\
	if r1 != 0 goto l0_%=;				\
	r0 = 0;						\
	exit;						\
l0_%=:	call %[bpf_sk_fullsock];			\
	if r0 != 0 goto l1_%=;				\
	exit;						\
l1_%=:	r1 = r0;					\
	call %[bpf_tcp_sock];				\
	if r0 != 0 goto l2_%=;				\
	exit;						\
l2_%=:	r0 = *(u32*)(r0 + %[bpf_tcp_sock_snd_cwnd]);	\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_sk_fullsock),
	  __imm(bpf_tcp_sock),
	  __imm_const(__sk_buff_sk, offsetof(struct __sk_buff, sk)),
	  __imm_const(bpf_tcp_sock_snd_cwnd, offsetof(struct bpf_tcp_sock, snd_cwnd))
	: __clobber_all);
}

SEC("tc")
__description("bpf_sk_release(skb->sk)")
__failure __msg("R1 must be referenced when passed to release function")
__naked void bpf_sk_release_skb_sk(void)
{
	asm volatile ("					\
	r1 = *(u64*)(r1 + %[__sk_buff_sk]);		\
	if r1 == 0 goto l0_%=;				\
	call %[bpf_sk_release];				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_sk_release),
	  __imm_const(__sk_buff_sk, offsetof(struct __sk_buff, sk))
	: __clobber_all);
}

SEC("tc")
__description("bpf_sk_release(bpf_sk_fullsock(skb->sk))")
__failure __msg("R1 must be referenced when passed to release function")
__naked void bpf_sk_fullsock_skb_sk(void)
{
	asm volatile ("					\
	r1 = *(u64*)(r1 + %[__sk_buff_sk]);		\
	if r1 != 0 goto l0_%=;				\
	r0 = 0;						\
	exit;						\
l0_%=:	call %[bpf_sk_fullsock];			\
	if r0 != 0 goto l1_%=;				\
	exit;						\
l1_%=:	r1 = r0;					\
	call %[bpf_sk_release];				\
	r0 = 1;						\
	exit;						\
"	:
	: __imm(bpf_sk_fullsock),
	  __imm(bpf_sk_release),
	  __imm_const(__sk_buff_sk, offsetof(struct __sk_buff, sk))
	: __clobber_all);
}

SEC("tc")
__description("bpf_sk_release(bpf_tcp_sock(skb->sk))")
__failure __msg("R1 must be referenced when passed to release function")
__naked void bpf_tcp_sock_skb_sk(void)
{
	asm volatile ("					\
	r1 = *(u64*)(r1 + %[__sk_buff_sk]);		\
	if r1 != 0 goto l0_%=;				\
	r0 = 0;						\
	exit;						\
l0_%=:	call %[bpf_tcp_sock];				\
	if r0 != 0 goto l1_%=;				\
	exit;						\
l1_%=:	r1 = r0;					\
	call %[bpf_sk_release];				\
	r0 = 1;						\
	exit;						\
"	:
	: __imm(bpf_sk_release),
	  __imm(bpf_tcp_sock),
	  __imm_const(__sk_buff_sk, offsetof(struct __sk_buff, sk))
	: __clobber_all);
}

SEC("tc")
__description("sk_storage_get(map, skb->sk, NULL, 0): value == NULL")
__success __retval(0)
__naked void sk_null_0_value_null(void)
{
	asm volatile ("					\
	r1 = *(u64*)(r1 + %[__sk_buff_sk]);		\
	if r1 != 0 goto l0_%=;				\
	r0 = 0;						\
	exit;						\
l0_%=:	call %[bpf_sk_fullsock];			\
	if r0 != 0 goto l1_%=;				\
	r0 = 0;						\
	exit;						\
l1_%=:	r4 = 0;						\
	r3 = 0;						\
	r2 = r0;					\
	r1 = %[sk_storage_map] ll;			\
	call %[bpf_sk_storage_get];			\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_sk_fullsock),
	  __imm(bpf_sk_storage_get),
	  __imm_addr(sk_storage_map),
	  __imm_const(__sk_buff_sk, offsetof(struct __sk_buff, sk))
	: __clobber_all);
}

SEC("tc")
__description("sk_storage_get(map, skb->sk, 1, 1): value == 1")
__failure __msg("R3 type=scalar expected=fp")
__naked void sk_1_1_value_1(void)
{
	asm volatile ("					\
	r1 = *(u64*)(r1 + %[__sk_buff_sk]);		\
	if r1 != 0 goto l0_%=;				\
	r0 = 0;						\
	exit;						\
l0_%=:	call %[bpf_sk_fullsock];			\
	if r0 != 0 goto l1_%=;				\
	r0 = 0;						\
	exit;						\
l1_%=:	r4 = 1;						\
	r3 = 1;						\
	r2 = r0;					\
	r1 = %[sk_storage_map] ll;			\
	call %[bpf_sk_storage_get];			\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_sk_fullsock),
	  __imm(bpf_sk_storage_get),
	  __imm_addr(sk_storage_map),
	  __imm_const(__sk_buff_sk, offsetof(struct __sk_buff, sk))
	: __clobber_all);
}

SEC("tc")
__description("sk_storage_get(map, skb->sk, &stack_value, 1): stack_value")
__success __retval(0)
__naked void stack_value_1_stack_value(void)
{
	asm volatile ("					\
	r2 = 0;						\
	*(u64*)(r10 - 8) = r2;				\
	r1 = *(u64*)(r1 + %[__sk_buff_sk]);		\
	if r1 != 0 goto l0_%=;				\
	r0 = 0;						\
	exit;						\
l0_%=:	call %[bpf_sk_fullsock];			\
	if r0 != 0 goto l1_%=;				\
	r0 = 0;						\
	exit;						\
l1_%=:	r4 = 1;						\
	r3 = r10;					\
	r3 += -8;					\
	r2 = r0;					\
	r1 = %[sk_storage_map] ll;			\
	call %[bpf_sk_storage_get];			\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_sk_fullsock),
	  __imm(bpf_sk_storage_get),
	  __imm_addr(sk_storage_map),
	  __imm_const(__sk_buff_sk, offsetof(struct __sk_buff, sk))
	: __clobber_all);
}

SEC("tc")
__description("bpf_map_lookup_elem(smap, &key)")
__failure __msg("cannot pass map_type 24 into func bpf_map_lookup_elem")
__naked void map_lookup_elem_smap_key(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u32*)(r10 - 4) = r1;				\
	r2 = r10;					\
	r2 += -4;					\
	r1 = %[sk_storage_map] ll;			\
	call %[bpf_map_lookup_elem];			\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(sk_storage_map)
	: __clobber_all);
}

SEC("xdp")
__description("bpf_map_lookup_elem(xskmap, &key); xs->queue_id")
__success __retval(0)
__naked void xskmap_key_xs_queue_id(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u32*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_xskmap] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 != 0 goto l0_%=;				\
	exit;						\
l0_%=:	r0 = *(u32*)(r0 + %[bpf_xdp_sock_queue_id]);	\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_xskmap),
	  __imm_const(bpf_xdp_sock_queue_id, offsetof(struct bpf_xdp_sock, queue_id))
	: __clobber_all);
}

SEC("sk_skb")
__description("bpf_map_lookup_elem(sockmap, &key)")
__failure __msg("Unreleased reference id=2 alloc_insn=6")
__naked void map_lookup_elem_sockmap_key(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u32*)(r10 - 4) = r1;				\
	r2 = r10;					\
	r2 += -4;					\
	r1 = %[map_sockmap] ll;				\
	call %[bpf_map_lookup_elem];			\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_sockmap)
	: __clobber_all);
}

SEC("sk_skb")
__description("bpf_map_lookup_elem(sockhash, &key)")
__failure __msg("Unreleased reference id=2 alloc_insn=6")
__naked void map_lookup_elem_sockhash_key(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u32*)(r10 - 4) = r1;				\
	r2 = r10;					\
	r2 += -4;					\
	r1 = %[map_sockhash] ll;			\
	call %[bpf_map_lookup_elem];			\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_sockhash)
	: __clobber_all);
}

SEC("sk_skb")
__description("bpf_map_lookup_elem(sockmap, &key); sk->type [fullsock field]; bpf_sk_release(sk)")
__success
__naked void field_bpf_sk_release_sk_1(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u32*)(r10 - 4) = r1;				\
	r2 = r10;					\
	r2 += -4;					\
	r1 = %[map_sockmap] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 != 0 goto l0_%=;				\
	exit;						\
l0_%=:	r1 = r0;					\
	r0 = *(u32*)(r0 + %[bpf_sock_type]);		\
	call %[bpf_sk_release];				\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm(bpf_sk_release),
	  __imm_addr(map_sockmap),
	  __imm_const(bpf_sock_type, offsetof(struct bpf_sock, type))
	: __clobber_all);
}

SEC("sk_skb")
__description("bpf_map_lookup_elem(sockhash, &key); sk->type [fullsock field]; bpf_sk_release(sk)")
__success
__naked void field_bpf_sk_release_sk_2(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u32*)(r10 - 4) = r1;				\
	r2 = r10;					\
	r2 += -4;					\
	r1 = %[map_sockhash] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 != 0 goto l0_%=;				\
	exit;						\
l0_%=:	r1 = r0;					\
	r0 = *(u32*)(r0 + %[bpf_sock_type]);		\
	call %[bpf_sk_release];				\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm(bpf_sk_release),
	  __imm_addr(map_sockhash),
	  __imm_const(bpf_sock_type, offsetof(struct bpf_sock, type))
	: __clobber_all);
}

SEC("sk_reuseport")
__description("bpf_sk_select_reuseport(ctx, reuseport_array, &key, flags)")
__success
__naked void ctx_reuseport_array_key_flags(void)
{
	asm volatile ("					\
	r4 = 0;						\
	r2 = 0;						\
	*(u32*)(r10 - 4) = r2;				\
	r3 = r10;					\
	r3 += -4;					\
	r2 = %[map_reuseport_array] ll;			\
	call %[bpf_sk_select_reuseport];		\
	exit;						\
"	:
	: __imm(bpf_sk_select_reuseport),
	  __imm_addr(map_reuseport_array)
	: __clobber_all);
}

SEC("sk_reuseport")
__description("bpf_sk_select_reuseport(ctx, sockmap, &key, flags)")
__success
__naked void reuseport_ctx_sockmap_key_flags(void)
{
	asm volatile ("					\
	r4 = 0;						\
	r2 = 0;						\
	*(u32*)(r10 - 4) = r2;				\
	r3 = r10;					\
	r3 += -4;					\
	r2 = %[map_sockmap] ll;				\
	call %[bpf_sk_select_reuseport];		\
	exit;						\
"	:
	: __imm(bpf_sk_select_reuseport),
	  __imm_addr(map_sockmap)
	: __clobber_all);
}

SEC("sk_reuseport")
__description("bpf_sk_select_reuseport(ctx, sockhash, &key, flags)")
__success
__naked void reuseport_ctx_sockhash_key_flags(void)
{
	asm volatile ("					\
	r4 = 0;						\
	r2 = 0;						\
	*(u32*)(r10 - 4) = r2;				\
	r3 = r10;					\
	r3 += -4;					\
	r2 = %[map_sockmap] ll;				\
	call %[bpf_sk_select_reuseport];		\
	exit;						\
"	:
	: __imm(bpf_sk_select_reuseport),
	  __imm_addr(map_sockmap)
	: __clobber_all);
}

SEC("tc")
__description("mark null check on return value of bpf_skc_to helpers")
__failure __msg("invalid mem access")
__naked void of_bpf_skc_to_helpers(void)
{
	asm volatile ("					\
	r1 = *(u64*)(r1 + %[__sk_buff_sk]);		\
	if r1 != 0 goto l0_%=;				\
	r0 = 0;						\
	exit;						\
l0_%=:	r6 = r1;					\
	call %[bpf_skc_to_tcp_sock];			\
	r7 = r0;					\
	r1 = r6;					\
	call %[bpf_skc_to_tcp_request_sock];		\
	r8 = r0;					\
	if r8 != 0 goto l1_%=;				\
	r0 = 0;						\
	exit;						\
l1_%=:	r0 = *(u8*)(r7 + 0);				\
	exit;						\
"	:
	: __imm(bpf_skc_to_tcp_request_sock),
	  __imm(bpf_skc_to_tcp_sock),
	  __imm_const(__sk_buff_sk, offsetof(struct __sk_buff, sk))
	: __clobber_all);
}

SEC("cgroup/post_bind4")
__description("sk->src_ip6[0] [load 1st byte]")
__failure __msg("invalid bpf_context access off=28 size=2")
__naked void post_bind4_read_src_ip6(void)
{
	asm volatile ("					\
	r6 = r1;					\
	r7 = *(u16*)(r6 + %[bpf_sock_src_ip6_0]);	\
	r0 = 1;						\
	exit;						\
"	:
	: __imm_const(bpf_sock_src_ip6_0, offsetof(struct bpf_sock, src_ip6[0]))
	: __clobber_all);
}

SEC("cgroup/post_bind4")
__description("sk->mark [load mark]")
__failure __msg("invalid bpf_context access off=16 size=2")
__naked void post_bind4_read_mark(void)
{
	asm volatile ("					\
	r6 = r1;					\
	r7 = *(u16*)(r6 + %[bpf_sock_mark]);		\
	r0 = 1;						\
	exit;						\
"	:
	: __imm_const(bpf_sock_mark, offsetof(struct bpf_sock, mark))
	: __clobber_all);
}

SEC("cgroup/post_bind6")
__description("sk->src_ip4 [load src_ip4]")
__failure __msg("invalid bpf_context access off=24 size=2")
__naked void post_bind6_read_src_ip4(void)
{
	asm volatile ("					\
	r6 = r1;					\
	r7 = *(u16*)(r6 + %[bpf_sock_src_ip4]);		\
	r0 = 1;						\
	exit;						\
"	:
	: __imm_const(bpf_sock_src_ip4, offsetof(struct bpf_sock, src_ip4))
	: __clobber_all);
}

SEC("cgroup/sock_create")
__description("sk->src_port [word load]")
__failure __msg("invalid bpf_context access off=44 size=2")
__naked void sock_create_read_src_port(void)
{
	asm volatile ("					\
	r6 = r1;					\
	r7 = *(u16*)(r6 + %[bpf_sock_src_port]);	\
	r0 = 1;						\
	exit;						\
"	:
	: __imm_const(bpf_sock_src_port, offsetof(struct bpf_sock, src_port))
	: __clobber_all);
}

__noinline
long skb_pull_data2(struct __sk_buff *sk, __u32 len)
{
	return bpf_skb_pull_data(sk, len);
}

__noinline
long skb_pull_data1(struct __sk_buff *sk, __u32 len)
{
	return skb_pull_data2(sk, len);
}

/* global function calls bpf_skb_pull_data(), which invalidates packet
 * pointers established before global function call.
 */
SEC("tc")
__failure __msg("invalid mem access")
int invalidate_pkt_pointers_from_global_func(struct __sk_buff *sk)
{
	int *p = (void *)(long)sk->data;

	if ((void *)(p + 1) > (void *)(long)sk->data_end)
		return TCX_DROP;
	skb_pull_data1(sk, 0);
	*p = 42; /* this is unsafe */
	return TCX_PASS;
}

__noinline
long xdp_pull_data2(struct xdp_md *x, __u32 len)
{
	return bpf_xdp_pull_data(x, len);
}

__noinline
long xdp_pull_data1(struct xdp_md *x, __u32 len)
{
	return xdp_pull_data2(x, len);
}

/* global function calls bpf_xdp_pull_data(), which invalidates packet
 * pointers established before global function call.
 */
SEC("xdp")
__failure __msg("invalid mem access")
int invalidate_xdp_pkt_pointers_from_global_func(struct xdp_md *x)
{
	int *p = (void *)(long)x->data;

	if ((void *)(p + 1) > (void *)(long)x->data_end)
		return XDP_DROP;
	xdp_pull_data1(x, 0);
	*p = 42; /* this is unsafe */
	return XDP_PASS;
}

/* XDP packet changing kfunc calls invalidate packet pointers */
SEC("xdp")
__failure __msg("invalid mem access")
int invalidate_xdp_pkt_pointers(struct xdp_md *x)
{
	int *p = (void *)(long)x->data;

	if ((void *)(p + 1) > (void *)(long)x->data_end)
		return XDP_DROP;
	bpf_xdp_pull_data(x, 0);
	*p = 42; /* this is unsafe */
	return XDP_PASS;
}

__noinline
int tail_call(struct __sk_buff *sk)
{
	bpf_tail_call_static(sk, &jmp_table, 0);
	return 0;
}

/* Tail calls invalidate packet pointers. */
SEC("tc")
__failure __msg("invalid mem access")
int invalidate_pkt_pointers_by_tail_call(struct __sk_buff *sk)
{
	int *p = (void *)(long)sk->data;

	if ((void *)(p + 1) > (void *)(long)sk->data_end)
		return TCX_DROP;
	tail_call(sk);
	*p = 42; /* this is unsafe */
	return TCX_PASS;
}

char _license[] SEC("license") = "GPL";
