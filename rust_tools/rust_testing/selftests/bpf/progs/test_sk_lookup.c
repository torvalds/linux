// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
// Copyright (c) 2020 Cloudflare

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <linux/bpf.h>
#include <linux/in.h>
#include <sys/socket.h>

#include <bpf/bpf_endian.h>
#include <bpf/bpf_helpers.h>

#define IP4(a, b, c, d)					\
	bpf_htonl((((__u32)(a) & 0xffU) << 24) |	\
		  (((__u32)(b) & 0xffU) << 16) |	\
		  (((__u32)(c) & 0xffU) <<  8) |	\
		  (((__u32)(d) & 0xffU) <<  0))
#define IP6(aaaa, bbbb, cccc, dddd)			\
	{ bpf_htonl(aaaa), bpf_htonl(bbbb), bpf_htonl(cccc), bpf_htonl(dddd) }

/* Macros for least-significant byte and word accesses. */
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define LSE_INDEX(index, size) (index)
#else
#define LSE_INDEX(index, size) ((size) - (index) - 1)
#endif
#define LSB(value, index)				\
	(((__u8 *)&(value))[LSE_INDEX((index), sizeof(value))])
#define LSW(value, index)				\
	(((__u16 *)&(value))[LSE_INDEX((index), sizeof(value) / 2)])

#define MAX_SOCKS 32

struct {
	__uint(type, BPF_MAP_TYPE_SOCKMAP);
	__uint(max_entries, MAX_SOCKS);
	__type(key, __u32);
	__type(value, __u64);
} redir_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 2);
	__type(key, int);
	__type(value, int);
} run_map SEC(".maps");

enum {
	PROG1 = 0,
	PROG2,
};

enum {
	SERVER_A = 0,
	SERVER_B,
};

/* Addressable key/value constants for convenience */
static const int KEY_PROG1 = PROG1;
static const int KEY_PROG2 = PROG2;
static const int PROG_DONE = 1;

static const __u32 KEY_SERVER_A = SERVER_A;
static const __u32 KEY_SERVER_B = SERVER_B;

static const __u16 SRC_PORT = bpf_htons(8008);
static const __u32 SRC_IP4 = IP4(127, 0, 0, 2);
static const __u32 SRC_IP6[] = IP6(0xfd000000, 0x0, 0x0, 0x00000002);

static const __u16 DST_PORT = 7007; /* Host byte order */
static const __u32 DST_IP4 = IP4(127, 0, 0, 1);
static const __u32 DST_IP6[] = IP6(0xfd000000, 0x0, 0x0, 0x00000001);

SEC("sk_lookup")
int lookup_pass(struct bpf_sk_lookup *ctx)
{
	return SK_PASS;
}

SEC("sk_lookup")
int lookup_drop(struct bpf_sk_lookup *ctx)
{
	return SK_DROP;
}

SEC("sk_lookup")
int check_ifindex(struct bpf_sk_lookup *ctx)
{
	if (ctx->ingress_ifindex == 1)
		return SK_DROP;
	return SK_PASS;
}

SEC("sk_reuseport")
int reuseport_pass(struct sk_reuseport_md *ctx)
{
	return SK_PASS;
}

SEC("sk_reuseport")
int reuseport_drop(struct sk_reuseport_md *ctx)
{
	return SK_DROP;
}

/* Redirect packets destined for port DST_PORT to socket at redir_map[0]. */
SEC("sk_lookup")
int redir_port(struct bpf_sk_lookup *ctx)
{
	struct bpf_sock *sk;
	int err;

	if (ctx->local_port != DST_PORT)
		return SK_PASS;

	sk = bpf_map_lookup_elem(&redir_map, &KEY_SERVER_A);
	if (!sk)
		return SK_PASS;

	err = bpf_sk_assign(ctx, sk, 0);
	bpf_sk_release(sk);
	return err ? SK_DROP : SK_PASS;
}

/* Redirect packets destined for DST_IP4 address to socket at redir_map[0]. */
SEC("sk_lookup")
int redir_ip4(struct bpf_sk_lookup *ctx)
{
	struct bpf_sock *sk;
	int err;

	if (ctx->family != AF_INET)
		return SK_PASS;
	if (ctx->local_port != DST_PORT)
		return SK_PASS;
	if (ctx->local_ip4 != DST_IP4)
		return SK_PASS;

	sk = bpf_map_lookup_elem(&redir_map, &KEY_SERVER_A);
	if (!sk)
		return SK_PASS;

	err = bpf_sk_assign(ctx, sk, 0);
	bpf_sk_release(sk);
	return err ? SK_DROP : SK_PASS;
}

/* Redirect packets destined for DST_IP6 address to socket at redir_map[0]. */
SEC("sk_lookup")
int redir_ip6(struct bpf_sk_lookup *ctx)
{
	struct bpf_sock *sk;
	int err;

	if (ctx->family != AF_INET6)
		return SK_PASS;
	if (ctx->local_port != DST_PORT)
		return SK_PASS;
	if (ctx->local_ip6[0] != DST_IP6[0] ||
	    ctx->local_ip6[1] != DST_IP6[1] ||
	    ctx->local_ip6[2] != DST_IP6[2] ||
	    ctx->local_ip6[3] != DST_IP6[3])
		return SK_PASS;

	sk = bpf_map_lookup_elem(&redir_map, &KEY_SERVER_A);
	if (!sk)
		return SK_PASS;

	err = bpf_sk_assign(ctx, sk, 0);
	bpf_sk_release(sk);
	return err ? SK_DROP : SK_PASS;
}

SEC("sk_lookup")
int select_sock_a(struct bpf_sk_lookup *ctx)
{
	struct bpf_sock *sk;
	int err;

	sk = bpf_map_lookup_elem(&redir_map, &KEY_SERVER_A);
	if (!sk)
		return SK_PASS;

	err = bpf_sk_assign(ctx, sk, 0);
	bpf_sk_release(sk);
	return err ? SK_DROP : SK_PASS;
}

SEC("sk_lookup")
int select_sock_a_no_reuseport(struct bpf_sk_lookup *ctx)
{
	struct bpf_sock *sk;
	int err;

	sk = bpf_map_lookup_elem(&redir_map, &KEY_SERVER_A);
	if (!sk)
		return SK_DROP;

	err = bpf_sk_assign(ctx, sk, BPF_SK_LOOKUP_F_NO_REUSEPORT);
	bpf_sk_release(sk);
	return err ? SK_DROP : SK_PASS;
}

SEC("sk_reuseport")
int select_sock_b(struct sk_reuseport_md *ctx)
{
	__u32 key = KEY_SERVER_B;
	int err;

	err = bpf_sk_select_reuseport(ctx, &redir_map, &key, 0);
	return err ? SK_DROP : SK_PASS;
}

/* Check that bpf_sk_assign() returns -EEXIST if socket already selected. */
SEC("sk_lookup")
int sk_assign_eexist(struct bpf_sk_lookup *ctx)
{
	struct bpf_sock *sk;
	int err, ret;

	ret = SK_DROP;
	sk = bpf_map_lookup_elem(&redir_map, &KEY_SERVER_B);
	if (!sk)
		goto out;
	err = bpf_sk_assign(ctx, sk, 0);
	if (err)
		goto out;
	bpf_sk_release(sk);

	sk = bpf_map_lookup_elem(&redir_map, &KEY_SERVER_A);
	if (!sk)
		goto out;
	err = bpf_sk_assign(ctx, sk, 0);
	if (err != -EEXIST) {
		bpf_printk("sk_assign returned %d, expected %d\n",
			   err, -EEXIST);
		goto out;
	}

	ret = SK_PASS; /* Success, redirect to KEY_SERVER_B */
out:
	if (sk)
		bpf_sk_release(sk);
	return ret;
}

/* Check that bpf_sk_assign(BPF_SK_LOOKUP_F_REPLACE) can override selection. */
SEC("sk_lookup")
int sk_assign_replace_flag(struct bpf_sk_lookup *ctx)
{
	struct bpf_sock *sk;
	int err, ret;

	ret = SK_DROP;
	sk = bpf_map_lookup_elem(&redir_map, &KEY_SERVER_A);
	if (!sk)
		goto out;
	err = bpf_sk_assign(ctx, sk, 0);
	if (err)
		goto out;
	bpf_sk_release(sk);

	sk = bpf_map_lookup_elem(&redir_map, &KEY_SERVER_B);
	if (!sk)
		goto out;
	err = bpf_sk_assign(ctx, sk, BPF_SK_LOOKUP_F_REPLACE);
	if (err) {
		bpf_printk("sk_assign returned %d, expected 0\n", err);
		goto out;
	}

	ret = SK_PASS; /* Success, redirect to KEY_SERVER_B */
out:
	if (sk)
		bpf_sk_release(sk);
	return ret;
}

/* Check that bpf_sk_assign(sk=NULL) is accepted. */
SEC("sk_lookup")
int sk_assign_null(struct bpf_sk_lookup *ctx)
{
	struct bpf_sock *sk = NULL;
	int err, ret;

	ret = SK_DROP;

	err = bpf_sk_assign(ctx, NULL, 0);
	if (err) {
		bpf_printk("sk_assign returned %d, expected 0\n", err);
		goto out;
	}

	sk = bpf_map_lookup_elem(&redir_map, &KEY_SERVER_B);
	if (!sk)
		goto out;
	err = bpf_sk_assign(ctx, sk, BPF_SK_LOOKUP_F_REPLACE);
	if (err) {
		bpf_printk("sk_assign returned %d, expected 0\n", err);
		goto out;
	}

	if (ctx->sk != sk)
		goto out;
	err = bpf_sk_assign(ctx, NULL, 0);
	if (err != -EEXIST)
		goto out;
	err = bpf_sk_assign(ctx, NULL, BPF_SK_LOOKUP_F_REPLACE);
	if (err)
		goto out;
	err = bpf_sk_assign(ctx, sk, BPF_SK_LOOKUP_F_REPLACE);
	if (err)
		goto out;

	ret = SK_PASS; /* Success, redirect to KEY_SERVER_B */
out:
	if (sk)
		bpf_sk_release(sk);
	return ret;
}

/* Check that selected sk is accessible through context. */
SEC("sk_lookup")
int access_ctx_sk(struct bpf_sk_lookup *ctx)
{
	struct bpf_sock *sk1 = NULL, *sk2 = NULL;
	int err, ret;

	ret = SK_DROP;

	/* Try accessing unassigned (NULL) ctx->sk field */
	if (ctx->sk && ctx->sk->family != AF_INET)
		goto out;

	/* Assign a value to ctx->sk */
	sk1 = bpf_map_lookup_elem(&redir_map, &KEY_SERVER_A);
	if (!sk1)
		goto out;
	err = bpf_sk_assign(ctx, sk1, 0);
	if (err)
		goto out;
	if (ctx->sk != sk1)
		goto out;

	/* Access ctx->sk fields */
	if (ctx->sk->family != AF_INET ||
	    ctx->sk->type != SOCK_STREAM ||
	    ctx->sk->state != BPF_TCP_LISTEN)
		goto out;

	/* Reset selection */
	err = bpf_sk_assign(ctx, NULL, BPF_SK_LOOKUP_F_REPLACE);
	if (err)
		goto out;
	if (ctx->sk)
		goto out;

	/* Assign another socket */
	sk2 = bpf_map_lookup_elem(&redir_map, &KEY_SERVER_B);
	if (!sk2)
		goto out;
	err = bpf_sk_assign(ctx, sk2, BPF_SK_LOOKUP_F_REPLACE);
	if (err)
		goto out;
	if (ctx->sk != sk2)
		goto out;

	/* Access reassigned ctx->sk fields */
	if (ctx->sk->family != AF_INET ||
	    ctx->sk->type != SOCK_STREAM ||
	    ctx->sk->state != BPF_TCP_LISTEN)
		goto out;

	ret = SK_PASS; /* Success, redirect to KEY_SERVER_B */
out:
	if (sk1)
		bpf_sk_release(sk1);
	if (sk2)
		bpf_sk_release(sk2);
	return ret;
}

/* Check narrow loads from ctx fields that support them.
 *
 * Narrow loads of size >= target field size from a non-zero offset
 * are not covered because they give bogus results, that is the
 * verifier ignores the offset.
 */
SEC("sk_lookup")
int ctx_narrow_access(struct bpf_sk_lookup *ctx)
{
	struct bpf_sock *sk;
	__u32 val_u32;
	bool v4;

	v4 = (ctx->family == AF_INET);

	/* Narrow loads from family field */
	if (LSB(ctx->family, 0) != (v4 ? AF_INET : AF_INET6) ||
	    LSB(ctx->family, 1) != 0 || LSB(ctx->family, 2) != 0 || LSB(ctx->family, 3) != 0)
		return SK_DROP;
	if (LSW(ctx->family, 0) != (v4 ? AF_INET : AF_INET6))
		return SK_DROP;

	/* Narrow loads from protocol field */
	if (LSB(ctx->protocol, 0) != IPPROTO_TCP ||
	    LSB(ctx->protocol, 1) != 0 || LSB(ctx->protocol, 2) != 0 || LSB(ctx->protocol, 3) != 0)
		return SK_DROP;
	if (LSW(ctx->protocol, 0) != IPPROTO_TCP)
		return SK_DROP;

	/* Narrow loads from remote_port field. Expect SRC_PORT. */
	if (LSB(ctx->remote_port, 0) != ((SRC_PORT >> 0) & 0xff) ||
	    LSB(ctx->remote_port, 1) != ((SRC_PORT >> 8) & 0xff))
		return SK_DROP;
	if (LSW(ctx->remote_port, 0) != SRC_PORT)
		return SK_DROP;

	/*
	 * NOTE: 4-byte load from bpf_sk_lookup at remote_port offset
	 * is quirky. It gets rewritten by the access converter to a
	 * 2-byte load for backward compatibility. Treating the load
	 * result as a be16 value makes the code portable across
	 * little- and big-endian platforms.
	 */
	val_u32 = *(__u32 *)&ctx->remote_port;
	if (val_u32 != SRC_PORT)
		return SK_DROP;

	/* Narrow loads from local_port field. Expect DST_PORT. */
	if (LSB(ctx->local_port, 0) != ((DST_PORT >> 0) & 0xff) ||
	    LSB(ctx->local_port, 1) != ((DST_PORT >> 8) & 0xff) ||
	    LSB(ctx->local_port, 2) != 0 || LSB(ctx->local_port, 3) != 0)
		return SK_DROP;
	if (LSW(ctx->local_port, 0) != DST_PORT)
		return SK_DROP;

	/* Narrow loads from IPv4 fields */
	if (v4) {
		/* Expect SRC_IP4 in remote_ip4 */
		if (LSB(ctx->remote_ip4, 0) != ((SRC_IP4 >> 0) & 0xff) ||
		    LSB(ctx->remote_ip4, 1) != ((SRC_IP4 >> 8) & 0xff) ||
		    LSB(ctx->remote_ip4, 2) != ((SRC_IP4 >> 16) & 0xff) ||
		    LSB(ctx->remote_ip4, 3) != ((SRC_IP4 >> 24) & 0xff))
			return SK_DROP;
		if (LSW(ctx->remote_ip4, 0) != ((SRC_IP4 >> 0) & 0xffff) ||
		    LSW(ctx->remote_ip4, 1) != ((SRC_IP4 >> 16) & 0xffff))
			return SK_DROP;

		/* Expect DST_IP4 in local_ip4 */
		if (LSB(ctx->local_ip4, 0) != ((DST_IP4 >> 0) & 0xff) ||
		    LSB(ctx->local_ip4, 1) != ((DST_IP4 >> 8) & 0xff) ||
		    LSB(ctx->local_ip4, 2) != ((DST_IP4 >> 16) & 0xff) ||
		    LSB(ctx->local_ip4, 3) != ((DST_IP4 >> 24) & 0xff))
			return SK_DROP;
		if (LSW(ctx->local_ip4, 0) != ((DST_IP4 >> 0) & 0xffff) ||
		    LSW(ctx->local_ip4, 1) != ((DST_IP4 >> 16) & 0xffff))
			return SK_DROP;
	} else {
		/* Expect 0.0.0.0 IPs when family != AF_INET */
		if (LSB(ctx->remote_ip4, 0) != 0 || LSB(ctx->remote_ip4, 1) != 0 ||
		    LSB(ctx->remote_ip4, 2) != 0 || LSB(ctx->remote_ip4, 3) != 0)
			return SK_DROP;
		if (LSW(ctx->remote_ip4, 0) != 0 || LSW(ctx->remote_ip4, 1) != 0)
			return SK_DROP;

		if (LSB(ctx->local_ip4, 0) != 0 || LSB(ctx->local_ip4, 1) != 0 ||
		    LSB(ctx->local_ip4, 2) != 0 || LSB(ctx->local_ip4, 3) != 0)
			return SK_DROP;
		if (LSW(ctx->local_ip4, 0) != 0 || LSW(ctx->local_ip4, 1) != 0)
			return SK_DROP;
	}

	/* Narrow loads from IPv6 fields */
	if (!v4) {
		/* Expect SRC_IP6 in remote_ip6 */
		if (LSB(ctx->remote_ip6[0], 0) != ((SRC_IP6[0] >> 0) & 0xff) ||
		    LSB(ctx->remote_ip6[0], 1) != ((SRC_IP6[0] >> 8) & 0xff) ||
		    LSB(ctx->remote_ip6[0], 2) != ((SRC_IP6[0] >> 16) & 0xff) ||
		    LSB(ctx->remote_ip6[0], 3) != ((SRC_IP6[0] >> 24) & 0xff) ||
		    LSB(ctx->remote_ip6[1], 0) != ((SRC_IP6[1] >> 0) & 0xff) ||
		    LSB(ctx->remote_ip6[1], 1) != ((SRC_IP6[1] >> 8) & 0xff) ||
		    LSB(ctx->remote_ip6[1], 2) != ((SRC_IP6[1] >> 16) & 0xff) ||
		    LSB(ctx->remote_ip6[1], 3) != ((SRC_IP6[1] >> 24) & 0xff) ||
		    LSB(ctx->remote_ip6[2], 0) != ((SRC_IP6[2] >> 0) & 0xff) ||
		    LSB(ctx->remote_ip6[2], 1) != ((SRC_IP6[2] >> 8) & 0xff) ||
		    LSB(ctx->remote_ip6[2], 2) != ((SRC_IP6[2] >> 16) & 0xff) ||
		    LSB(ctx->remote_ip6[2], 3) != ((SRC_IP6[2] >> 24) & 0xff) ||
		    LSB(ctx->remote_ip6[3], 0) != ((SRC_IP6[3] >> 0) & 0xff) ||
		    LSB(ctx->remote_ip6[3], 1) != ((SRC_IP6[3] >> 8) & 0xff) ||
		    LSB(ctx->remote_ip6[3], 2) != ((SRC_IP6[3] >> 16) & 0xff) ||
		    LSB(ctx->remote_ip6[3], 3) != ((SRC_IP6[3] >> 24) & 0xff))
			return SK_DROP;
		if (LSW(ctx->remote_ip6[0], 0) != ((SRC_IP6[0] >> 0) & 0xffff) ||
		    LSW(ctx->remote_ip6[0], 1) != ((SRC_IP6[0] >> 16) & 0xffff) ||
		    LSW(ctx->remote_ip6[1], 0) != ((SRC_IP6[1] >> 0) & 0xffff) ||
		    LSW(ctx->remote_ip6[1], 1) != ((SRC_IP6[1] >> 16) & 0xffff) ||
		    LSW(ctx->remote_ip6[2], 0) != ((SRC_IP6[2] >> 0) & 0xffff) ||
		    LSW(ctx->remote_ip6[2], 1) != ((SRC_IP6[2] >> 16) & 0xffff) ||
		    LSW(ctx->remote_ip6[3], 0) != ((SRC_IP6[3] >> 0) & 0xffff) ||
		    LSW(ctx->remote_ip6[3], 1) != ((SRC_IP6[3] >> 16) & 0xffff))
			return SK_DROP;
		/* Expect DST_IP6 in local_ip6 */
		if (LSB(ctx->local_ip6[0], 0) != ((DST_IP6[0] >> 0) & 0xff) ||
		    LSB(ctx->local_ip6[0], 1) != ((DST_IP6[0] >> 8) & 0xff) ||
		    LSB(ctx->local_ip6[0], 2) != ((DST_IP6[0] >> 16) & 0xff) ||
		    LSB(ctx->local_ip6[0], 3) != ((DST_IP6[0] >> 24) & 0xff) ||
		    LSB(ctx->local_ip6[1], 0) != ((DST_IP6[1] >> 0) & 0xff) ||
		    LSB(ctx->local_ip6[1], 1) != ((DST_IP6[1] >> 8) & 0xff) ||
		    LSB(ctx->local_ip6[1], 2) != ((DST_IP6[1] >> 16) & 0xff) ||
		    LSB(ctx->local_ip6[1], 3) != ((DST_IP6[1] >> 24) & 0xff) ||
		    LSB(ctx->local_ip6[2], 0) != ((DST_IP6[2] >> 0) & 0xff) ||
		    LSB(ctx->local_ip6[2], 1) != ((DST_IP6[2] >> 8) & 0xff) ||
		    LSB(ctx->local_ip6[2], 2) != ((DST_IP6[2] >> 16) & 0xff) ||
		    LSB(ctx->local_ip6[2], 3) != ((DST_IP6[2] >> 24) & 0xff) ||
		    LSB(ctx->local_ip6[3], 0) != ((DST_IP6[3] >> 0) & 0xff) ||
		    LSB(ctx->local_ip6[3], 1) != ((DST_IP6[3] >> 8) & 0xff) ||
		    LSB(ctx->local_ip6[3], 2) != ((DST_IP6[3] >> 16) & 0xff) ||
		    LSB(ctx->local_ip6[3], 3) != ((DST_IP6[3] >> 24) & 0xff))
			return SK_DROP;
		if (LSW(ctx->local_ip6[0], 0) != ((DST_IP6[0] >> 0) & 0xffff) ||
		    LSW(ctx->local_ip6[0], 1) != ((DST_IP6[0] >> 16) & 0xffff) ||
		    LSW(ctx->local_ip6[1], 0) != ((DST_IP6[1] >> 0) & 0xffff) ||
		    LSW(ctx->local_ip6[1], 1) != ((DST_IP6[1] >> 16) & 0xffff) ||
		    LSW(ctx->local_ip6[2], 0) != ((DST_IP6[2] >> 0) & 0xffff) ||
		    LSW(ctx->local_ip6[2], 1) != ((DST_IP6[2] >> 16) & 0xffff) ||
		    LSW(ctx->local_ip6[3], 0) != ((DST_IP6[3] >> 0) & 0xffff) ||
		    LSW(ctx->local_ip6[3], 1) != ((DST_IP6[3] >> 16) & 0xffff))
			return SK_DROP;
	} else {
		/* Expect :: IPs when family != AF_INET6 */
		if (LSB(ctx->remote_ip6[0], 0) != 0 || LSB(ctx->remote_ip6[0], 1) != 0 ||
		    LSB(ctx->remote_ip6[0], 2) != 0 || LSB(ctx->remote_ip6[0], 3) != 0 ||
		    LSB(ctx->remote_ip6[1], 0) != 0 || LSB(ctx->remote_ip6[1], 1) != 0 ||
		    LSB(ctx->remote_ip6[1], 2) != 0 || LSB(ctx->remote_ip6[1], 3) != 0 ||
		    LSB(ctx->remote_ip6[2], 0) != 0 || LSB(ctx->remote_ip6[2], 1) != 0 ||
		    LSB(ctx->remote_ip6[2], 2) != 0 || LSB(ctx->remote_ip6[2], 3) != 0 ||
		    LSB(ctx->remote_ip6[3], 0) != 0 || LSB(ctx->remote_ip6[3], 1) != 0 ||
		    LSB(ctx->remote_ip6[3], 2) != 0 || LSB(ctx->remote_ip6[3], 3) != 0)
			return SK_DROP;
		if (LSW(ctx->remote_ip6[0], 0) != 0 || LSW(ctx->remote_ip6[0], 1) != 0 ||
		    LSW(ctx->remote_ip6[1], 0) != 0 || LSW(ctx->remote_ip6[1], 1) != 0 ||
		    LSW(ctx->remote_ip6[2], 0) != 0 || LSW(ctx->remote_ip6[2], 1) != 0 ||
		    LSW(ctx->remote_ip6[3], 0) != 0 || LSW(ctx->remote_ip6[3], 1) != 0)
			return SK_DROP;

		if (LSB(ctx->local_ip6[0], 0) != 0 || LSB(ctx->local_ip6[0], 1) != 0 ||
		    LSB(ctx->local_ip6[0], 2) != 0 || LSB(ctx->local_ip6[0], 3) != 0 ||
		    LSB(ctx->local_ip6[1], 0) != 0 || LSB(ctx->local_ip6[1], 1) != 0 ||
		    LSB(ctx->local_ip6[1], 2) != 0 || LSB(ctx->local_ip6[1], 3) != 0 ||
		    LSB(ctx->local_ip6[2], 0) != 0 || LSB(ctx->local_ip6[2], 1) != 0 ||
		    LSB(ctx->local_ip6[2], 2) != 0 || LSB(ctx->local_ip6[2], 3) != 0 ||
		    LSB(ctx->local_ip6[3], 0) != 0 || LSB(ctx->local_ip6[3], 1) != 0 ||
		    LSB(ctx->local_ip6[3], 2) != 0 || LSB(ctx->local_ip6[3], 3) != 0)
			return SK_DROP;
		if (LSW(ctx->remote_ip6[0], 0) != 0 || LSW(ctx->remote_ip6[0], 1) != 0 ||
		    LSW(ctx->remote_ip6[1], 0) != 0 || LSW(ctx->remote_ip6[1], 1) != 0 ||
		    LSW(ctx->remote_ip6[2], 0) != 0 || LSW(ctx->remote_ip6[2], 1) != 0 ||
		    LSW(ctx->remote_ip6[3], 0) != 0 || LSW(ctx->remote_ip6[3], 1) != 0)
			return SK_DROP;
	}

	/* Success, redirect to KEY_SERVER_B */
	sk = bpf_map_lookup_elem(&redir_map, &KEY_SERVER_B);
	if (sk) {
		bpf_sk_assign(ctx, sk, 0);
		bpf_sk_release(sk);
	}
	return SK_PASS;
}

/* Check that sk_assign rejects SERVER_A socket with -ESOCKNOSUPPORT */
SEC("sk_lookup")
int sk_assign_esocknosupport(struct bpf_sk_lookup *ctx)
{
	struct bpf_sock *sk;
	int err, ret;

	ret = SK_DROP;
	sk = bpf_map_lookup_elem(&redir_map, &KEY_SERVER_A);
	if (!sk)
		goto out;

	err = bpf_sk_assign(ctx, sk, 0);
	if (err != -ESOCKTNOSUPPORT) {
		bpf_printk("sk_assign returned %d, expected %d\n",
			   err, -ESOCKTNOSUPPORT);
		goto out;
	}

	ret = SK_PASS; /* Success, pass to regular lookup */
out:
	if (sk)
		bpf_sk_release(sk);
	return ret;
}

SEC("sk_lookup")
int multi_prog_pass1(struct bpf_sk_lookup *ctx)
{
	bpf_map_update_elem(&run_map, &KEY_PROG1, &PROG_DONE, BPF_ANY);
	return SK_PASS;
}

SEC("sk_lookup")
int multi_prog_pass2(struct bpf_sk_lookup *ctx)
{
	bpf_map_update_elem(&run_map, &KEY_PROG2, &PROG_DONE, BPF_ANY);
	return SK_PASS;
}

SEC("sk_lookup")
int multi_prog_drop1(struct bpf_sk_lookup *ctx)
{
	bpf_map_update_elem(&run_map, &KEY_PROG1, &PROG_DONE, BPF_ANY);
	return SK_DROP;
}

SEC("sk_lookup")
int multi_prog_drop2(struct bpf_sk_lookup *ctx)
{
	bpf_map_update_elem(&run_map, &KEY_PROG2, &PROG_DONE, BPF_ANY);
	return SK_DROP;
}

static __always_inline int select_server_a(struct bpf_sk_lookup *ctx)
{
	struct bpf_sock *sk;
	int err;

	sk = bpf_map_lookup_elem(&redir_map, &KEY_SERVER_A);
	if (!sk)
		return SK_DROP;

	err = bpf_sk_assign(ctx, sk, 0);
	bpf_sk_release(sk);
	if (err)
		return SK_DROP;

	return SK_PASS;
}

SEC("sk_lookup")
int multi_prog_redir1(struct bpf_sk_lookup *ctx)
{
	(void)select_server_a(ctx);
	bpf_map_update_elem(&run_map, &KEY_PROG1, &PROG_DONE, BPF_ANY);
	return SK_PASS;
}

SEC("sk_lookup")
int multi_prog_redir2(struct bpf_sk_lookup *ctx)
{
	(void)select_server_a(ctx);
	bpf_map_update_elem(&run_map, &KEY_PROG2, &PROG_DONE, BPF_ANY);
	return SK_PASS;
}

char _license[] SEC("license") = "Dual BSD/GPL";
