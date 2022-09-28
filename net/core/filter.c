// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Linux Socket Filter - Kernel level socket filtering
 *
 * Based on the design of the Berkeley Packet Filter. The new
 * internal format has been designed by PLUMgrid:
 *
 *	Copyright (c) 2011 - 2014 PLUMgrid, http://plumgrid.com
 *
 * Authors:
 *
 *	Jay Schulist <jschlst@samba.org>
 *	Alexei Starovoitov <ast@plumgrid.com>
 *	Daniel Borkmann <dborkman@redhat.com>
 *
 * Andi Kleen - Fix a few bad bugs and races.
 * Kris Katterjohn - Added many additional checks in bpf_check_classic()
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/fcntl.h>
#include <linux/socket.h>
#include <linux/sock_diag.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/if_packet.h>
#include <linux/if_arp.h>
#include <linux/gfp.h>
#include <net/inet_common.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <net/netlink.h>
#include <linux/skbuff.h>
#include <linux/skmsg.h>
#include <net/sock.h>
#include <net/flow_dissector.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <linux/uaccess.h>
#include <asm/unaligned.h>
#include <asm/cmpxchg.h>
#include <linux/filter.h>
#include <linux/ratelimit.h>
#include <linux/seccomp.h>
#include <linux/if_vlan.h>
#include <linux/bpf.h>
#include <linux/btf.h>
#include <net/sch_generic.h>
#include <net/cls_cgroup.h>
#include <net/dst_metadata.h>
#include <net/dst.h>
#include <net/sock_reuseport.h>
#include <net/busy_poll.h>
#include <net/tcp.h>
#include <net/xfrm.h>
#include <net/udp.h>
#include <linux/bpf_trace.h>
#include <net/xdp_sock.h>
#include <linux/inetdevice.h>
#include <net/inet_hashtables.h>
#include <net/inet6_hashtables.h>
#include <net/ip_fib.h>
#include <net/nexthop.h>
#include <net/flow.h>
#include <net/arp.h>
#include <net/ipv6.h>
#include <net/net_namespace.h>
#include <linux/seg6_local.h>
#include <net/seg6.h>
#include <net/seg6_local.h>
#include <net/lwtunnel.h>
#include <net/ipv6_stubs.h>
#include <net/bpf_sk_storage.h>
#include <net/transp_v6.h>
#include <linux/btf_ids.h>
#include <net/tls.h>

static const struct bpf_func_proto *
bpf_sk_base_func_proto(enum bpf_func_id func_id);

int copy_bpf_fprog_from_user(struct sock_fprog *dst, sockptr_t src, int len)
{
	if (in_compat_syscall()) {
		struct compat_sock_fprog f32;

		if (len != sizeof(f32))
			return -EINVAL;
		if (copy_from_sockptr(&f32, src, sizeof(f32)))
			return -EFAULT;
		memset(dst, 0, sizeof(*dst));
		dst->len = f32.len;
		dst->filter = compat_ptr(f32.filter);
	} else {
		if (len != sizeof(*dst))
			return -EINVAL;
		if (copy_from_sockptr(dst, src, sizeof(*dst)))
			return -EFAULT;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(copy_bpf_fprog_from_user);

/**
 *	sk_filter_trim_cap - run a packet through a socket filter
 *	@sk: sock associated with &sk_buff
 *	@skb: buffer to filter
 *	@cap: limit on how short the eBPF program may trim the packet
 *
 * Run the eBPF program and then cut skb->data to correct size returned by
 * the program. If pkt_len is 0 we toss packet. If skb->len is smaller
 * than pkt_len we keep whole skb->data. This is the socket level
 * wrapper to BPF_PROG_RUN. It returns 0 if the packet should
 * be accepted or -EPERM if the packet should be tossed.
 *
 */
int sk_filter_trim_cap(struct sock *sk, struct sk_buff *skb, unsigned int cap)
{
	int err;
	struct sk_filter *filter;

	/*
	 * If the skb was allocated from pfmemalloc reserves, only
	 * allow SOCK_MEMALLOC sockets to use it as this socket is
	 * helping free memory
	 */
	if (skb_pfmemalloc(skb) && !sock_flag(sk, SOCK_MEMALLOC)) {
		NET_INC_STATS(sock_net(sk), LINUX_MIB_PFMEMALLOCDROP);
		return -ENOMEM;
	}
	err = BPF_CGROUP_RUN_PROG_INET_INGRESS(sk, skb);
	if (err)
		return err;

	err = security_sock_rcv_skb(sk, skb);
	if (err)
		return err;

	rcu_read_lock();
	filter = rcu_dereference(sk->sk_filter);
	if (filter) {
		struct sock *save_sk = skb->sk;
		unsigned int pkt_len;

		skb->sk = sk;
		pkt_len = bpf_prog_run_save_cb(filter->prog, skb);
		skb->sk = save_sk;
		err = pkt_len ? pskb_trim(skb, max(cap, pkt_len)) : -EPERM;
	}
	rcu_read_unlock();

	return err;
}
EXPORT_SYMBOL(sk_filter_trim_cap);

BPF_CALL_1(bpf_skb_get_pay_offset, struct sk_buff *, skb)
{
	return skb_get_poff(skb);
}

BPF_CALL_3(bpf_skb_get_nlattr, struct sk_buff *, skb, u32, a, u32, x)
{
	struct nlattr *nla;

	if (skb_is_nonlinear(skb))
		return 0;

	if (skb->len < sizeof(struct nlattr))
		return 0;

	if (a > skb->len - sizeof(struct nlattr))
		return 0;

	nla = nla_find((struct nlattr *) &skb->data[a], skb->len - a, x);
	if (nla)
		return (void *) nla - (void *) skb->data;

	return 0;
}

BPF_CALL_3(bpf_skb_get_nlattr_nest, struct sk_buff *, skb, u32, a, u32, x)
{
	struct nlattr *nla;

	if (skb_is_nonlinear(skb))
		return 0;

	if (skb->len < sizeof(struct nlattr))
		return 0;

	if (a > skb->len - sizeof(struct nlattr))
		return 0;

	nla = (struct nlattr *) &skb->data[a];
	if (nla->nla_len > skb->len - a)
		return 0;

	nla = nla_find_nested(nla, x);
	if (nla)
		return (void *) nla - (void *) skb->data;

	return 0;
}

BPF_CALL_4(bpf_skb_load_helper_8, const struct sk_buff *, skb, const void *,
	   data, int, headlen, int, offset)
{
	u8 tmp, *ptr;
	const int len = sizeof(tmp);

	if (offset >= 0) {
		if (headlen - offset >= len)
			return *(u8 *)(data + offset);
		if (!skb_copy_bits(skb, offset, &tmp, sizeof(tmp)))
			return tmp;
	} else {
		ptr = bpf_internal_load_pointer_neg_helper(skb, offset, len);
		if (likely(ptr))
			return *(u8 *)ptr;
	}

	return -EFAULT;
}

BPF_CALL_2(bpf_skb_load_helper_8_no_cache, const struct sk_buff *, skb,
	   int, offset)
{
	return ____bpf_skb_load_helper_8(skb, skb->data, skb->len - skb->data_len,
					 offset);
}

BPF_CALL_4(bpf_skb_load_helper_16, const struct sk_buff *, skb, const void *,
	   data, int, headlen, int, offset)
{
	u16 tmp, *ptr;
	const int len = sizeof(tmp);

	if (offset >= 0) {
		if (headlen - offset >= len)
			return get_unaligned_be16(data + offset);
		if (!skb_copy_bits(skb, offset, &tmp, sizeof(tmp)))
			return be16_to_cpu(tmp);
	} else {
		ptr = bpf_internal_load_pointer_neg_helper(skb, offset, len);
		if (likely(ptr))
			return get_unaligned_be16(ptr);
	}

	return -EFAULT;
}

BPF_CALL_2(bpf_skb_load_helper_16_no_cache, const struct sk_buff *, skb,
	   int, offset)
{
	return ____bpf_skb_load_helper_16(skb, skb->data, skb->len - skb->data_len,
					  offset);
}

BPF_CALL_4(bpf_skb_load_helper_32, const struct sk_buff *, skb, const void *,
	   data, int, headlen, int, offset)
{
	u32 tmp, *ptr;
	const int len = sizeof(tmp);

	if (likely(offset >= 0)) {
		if (headlen - offset >= len)
			return get_unaligned_be32(data + offset);
		if (!skb_copy_bits(skb, offset, &tmp, sizeof(tmp)))
			return be32_to_cpu(tmp);
	} else {
		ptr = bpf_internal_load_pointer_neg_helper(skb, offset, len);
		if (likely(ptr))
			return get_unaligned_be32(ptr);
	}

	return -EFAULT;
}

BPF_CALL_2(bpf_skb_load_helper_32_no_cache, const struct sk_buff *, skb,
	   int, offset)
{
	return ____bpf_skb_load_helper_32(skb, skb->data, skb->len - skb->data_len,
					  offset);
}

static u32 convert_skb_access(int skb_field, int dst_reg, int src_reg,
			      struct bpf_insn *insn_buf)
{
	struct bpf_insn *insn = insn_buf;

	switch (skb_field) {
	case SKF_AD_MARK:
		BUILD_BUG_ON(sizeof_field(struct sk_buff, mark) != 4);

		*insn++ = BPF_LDX_MEM(BPF_W, dst_reg, src_reg,
				      offsetof(struct sk_buff, mark));
		break;

	case SKF_AD_PKTTYPE:
		*insn++ = BPF_LDX_MEM(BPF_B, dst_reg, src_reg, PKT_TYPE_OFFSET());
		*insn++ = BPF_ALU32_IMM(BPF_AND, dst_reg, PKT_TYPE_MAX);
#ifdef __BIG_ENDIAN_BITFIELD
		*insn++ = BPF_ALU32_IMM(BPF_RSH, dst_reg, 5);
#endif
		break;

	case SKF_AD_QUEUE:
		BUILD_BUG_ON(sizeof_field(struct sk_buff, queue_mapping) != 2);

		*insn++ = BPF_LDX_MEM(BPF_H, dst_reg, src_reg,
				      offsetof(struct sk_buff, queue_mapping));
		break;

	case SKF_AD_VLAN_TAG:
		BUILD_BUG_ON(sizeof_field(struct sk_buff, vlan_tci) != 2);

		/* dst_reg = *(u16 *) (src_reg + offsetof(vlan_tci)) */
		*insn++ = BPF_LDX_MEM(BPF_H, dst_reg, src_reg,
				      offsetof(struct sk_buff, vlan_tci));
		break;
	case SKF_AD_VLAN_TAG_PRESENT:
		*insn++ = BPF_LDX_MEM(BPF_B, dst_reg, src_reg, PKT_VLAN_PRESENT_OFFSET());
		if (PKT_VLAN_PRESENT_BIT)
			*insn++ = BPF_ALU32_IMM(BPF_RSH, dst_reg, PKT_VLAN_PRESENT_BIT);
		if (PKT_VLAN_PRESENT_BIT < 7)
			*insn++ = BPF_ALU32_IMM(BPF_AND, dst_reg, 1);
		break;
	}

	return insn - insn_buf;
}

static bool convert_bpf_extensions(struct sock_filter *fp,
				   struct bpf_insn **insnp)
{
	struct bpf_insn *insn = *insnp;
	u32 cnt;

	switch (fp->k) {
	case SKF_AD_OFF + SKF_AD_PROTOCOL:
		BUILD_BUG_ON(sizeof_field(struct sk_buff, protocol) != 2);

		/* A = *(u16 *) (CTX + offsetof(protocol)) */
		*insn++ = BPF_LDX_MEM(BPF_H, BPF_REG_A, BPF_REG_CTX,
				      offsetof(struct sk_buff, protocol));
		/* A = ntohs(A) [emitting a nop or swap16] */
		*insn = BPF_ENDIAN(BPF_FROM_BE, BPF_REG_A, 16);
		break;

	case SKF_AD_OFF + SKF_AD_PKTTYPE:
		cnt = convert_skb_access(SKF_AD_PKTTYPE, BPF_REG_A, BPF_REG_CTX, insn);
		insn += cnt - 1;
		break;

	case SKF_AD_OFF + SKF_AD_IFINDEX:
	case SKF_AD_OFF + SKF_AD_HATYPE:
		BUILD_BUG_ON(sizeof_field(struct net_device, ifindex) != 4);
		BUILD_BUG_ON(sizeof_field(struct net_device, type) != 2);

		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct sk_buff, dev),
				      BPF_REG_TMP, BPF_REG_CTX,
				      offsetof(struct sk_buff, dev));
		/* if (tmp != 0) goto pc + 1 */
		*insn++ = BPF_JMP_IMM(BPF_JNE, BPF_REG_TMP, 0, 1);
		*insn++ = BPF_EXIT_INSN();
		if (fp->k == SKF_AD_OFF + SKF_AD_IFINDEX)
			*insn = BPF_LDX_MEM(BPF_W, BPF_REG_A, BPF_REG_TMP,
					    offsetof(struct net_device, ifindex));
		else
			*insn = BPF_LDX_MEM(BPF_H, BPF_REG_A, BPF_REG_TMP,
					    offsetof(struct net_device, type));
		break;

	case SKF_AD_OFF + SKF_AD_MARK:
		cnt = convert_skb_access(SKF_AD_MARK, BPF_REG_A, BPF_REG_CTX, insn);
		insn += cnt - 1;
		break;

	case SKF_AD_OFF + SKF_AD_RXHASH:
		BUILD_BUG_ON(sizeof_field(struct sk_buff, hash) != 4);

		*insn = BPF_LDX_MEM(BPF_W, BPF_REG_A, BPF_REG_CTX,
				    offsetof(struct sk_buff, hash));
		break;

	case SKF_AD_OFF + SKF_AD_QUEUE:
		cnt = convert_skb_access(SKF_AD_QUEUE, BPF_REG_A, BPF_REG_CTX, insn);
		insn += cnt - 1;
		break;

	case SKF_AD_OFF + SKF_AD_VLAN_TAG:
		cnt = convert_skb_access(SKF_AD_VLAN_TAG,
					 BPF_REG_A, BPF_REG_CTX, insn);
		insn += cnt - 1;
		break;

	case SKF_AD_OFF + SKF_AD_VLAN_TAG_PRESENT:
		cnt = convert_skb_access(SKF_AD_VLAN_TAG_PRESENT,
					 BPF_REG_A, BPF_REG_CTX, insn);
		insn += cnt - 1;
		break;

	case SKF_AD_OFF + SKF_AD_VLAN_TPID:
		BUILD_BUG_ON(sizeof_field(struct sk_buff, vlan_proto) != 2);

		/* A = *(u16 *) (CTX + offsetof(vlan_proto)) */
		*insn++ = BPF_LDX_MEM(BPF_H, BPF_REG_A, BPF_REG_CTX,
				      offsetof(struct sk_buff, vlan_proto));
		/* A = ntohs(A) [emitting a nop or swap16] */
		*insn = BPF_ENDIAN(BPF_FROM_BE, BPF_REG_A, 16);
		break;

	case SKF_AD_OFF + SKF_AD_PAY_OFFSET:
	case SKF_AD_OFF + SKF_AD_NLATTR:
	case SKF_AD_OFF + SKF_AD_NLATTR_NEST:
	case SKF_AD_OFF + SKF_AD_CPU:
	case SKF_AD_OFF + SKF_AD_RANDOM:
		/* arg1 = CTX */
		*insn++ = BPF_MOV64_REG(BPF_REG_ARG1, BPF_REG_CTX);
		/* arg2 = A */
		*insn++ = BPF_MOV64_REG(BPF_REG_ARG2, BPF_REG_A);
		/* arg3 = X */
		*insn++ = BPF_MOV64_REG(BPF_REG_ARG3, BPF_REG_X);
		/* Emit call(arg1=CTX, arg2=A, arg3=X) */
		switch (fp->k) {
		case SKF_AD_OFF + SKF_AD_PAY_OFFSET:
			*insn = BPF_EMIT_CALL(bpf_skb_get_pay_offset);
			break;
		case SKF_AD_OFF + SKF_AD_NLATTR:
			*insn = BPF_EMIT_CALL(bpf_skb_get_nlattr);
			break;
		case SKF_AD_OFF + SKF_AD_NLATTR_NEST:
			*insn = BPF_EMIT_CALL(bpf_skb_get_nlattr_nest);
			break;
		case SKF_AD_OFF + SKF_AD_CPU:
			*insn = BPF_EMIT_CALL(bpf_get_raw_cpu_id);
			break;
		case SKF_AD_OFF + SKF_AD_RANDOM:
			*insn = BPF_EMIT_CALL(bpf_user_rnd_u32);
			bpf_user_rnd_init_once();
			break;
		}
		break;

	case SKF_AD_OFF + SKF_AD_ALU_XOR_X:
		/* A ^= X */
		*insn = BPF_ALU32_REG(BPF_XOR, BPF_REG_A, BPF_REG_X);
		break;

	default:
		/* This is just a dummy call to avoid letting the compiler
		 * evict __bpf_call_base() as an optimization. Placed here
		 * where no-one bothers.
		 */
		BUG_ON(__bpf_call_base(0, 0, 0, 0, 0) != 0);
		return false;
	}

	*insnp = insn;
	return true;
}

static bool convert_bpf_ld_abs(struct sock_filter *fp, struct bpf_insn **insnp)
{
	const bool unaligned_ok = IS_BUILTIN(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS);
	int size = bpf_size_to_bytes(BPF_SIZE(fp->code));
	bool endian = BPF_SIZE(fp->code) == BPF_H ||
		      BPF_SIZE(fp->code) == BPF_W;
	bool indirect = BPF_MODE(fp->code) == BPF_IND;
	const int ip_align = NET_IP_ALIGN;
	struct bpf_insn *insn = *insnp;
	int offset = fp->k;

	if (!indirect &&
	    ((unaligned_ok && offset >= 0) ||
	     (!unaligned_ok && offset >= 0 &&
	      offset + ip_align >= 0 &&
	      offset + ip_align % size == 0))) {
		bool ldx_off_ok = offset <= S16_MAX;

		*insn++ = BPF_MOV64_REG(BPF_REG_TMP, BPF_REG_H);
		if (offset)
			*insn++ = BPF_ALU64_IMM(BPF_SUB, BPF_REG_TMP, offset);
		*insn++ = BPF_JMP_IMM(BPF_JSLT, BPF_REG_TMP,
				      size, 2 + endian + (!ldx_off_ok * 2));
		if (ldx_off_ok) {
			*insn++ = BPF_LDX_MEM(BPF_SIZE(fp->code), BPF_REG_A,
					      BPF_REG_D, offset);
		} else {
			*insn++ = BPF_MOV64_REG(BPF_REG_TMP, BPF_REG_D);
			*insn++ = BPF_ALU64_IMM(BPF_ADD, BPF_REG_TMP, offset);
			*insn++ = BPF_LDX_MEM(BPF_SIZE(fp->code), BPF_REG_A,
					      BPF_REG_TMP, 0);
		}
		if (endian)
			*insn++ = BPF_ENDIAN(BPF_FROM_BE, BPF_REG_A, size * 8);
		*insn++ = BPF_JMP_A(8);
	}

	*insn++ = BPF_MOV64_REG(BPF_REG_ARG1, BPF_REG_CTX);
	*insn++ = BPF_MOV64_REG(BPF_REG_ARG2, BPF_REG_D);
	*insn++ = BPF_MOV64_REG(BPF_REG_ARG3, BPF_REG_H);
	if (!indirect) {
		*insn++ = BPF_MOV64_IMM(BPF_REG_ARG4, offset);
	} else {
		*insn++ = BPF_MOV64_REG(BPF_REG_ARG4, BPF_REG_X);
		if (fp->k)
			*insn++ = BPF_ALU64_IMM(BPF_ADD, BPF_REG_ARG4, offset);
	}

	switch (BPF_SIZE(fp->code)) {
	case BPF_B:
		*insn++ = BPF_EMIT_CALL(bpf_skb_load_helper_8);
		break;
	case BPF_H:
		*insn++ = BPF_EMIT_CALL(bpf_skb_load_helper_16);
		break;
	case BPF_W:
		*insn++ = BPF_EMIT_CALL(bpf_skb_load_helper_32);
		break;
	default:
		return false;
	}

	*insn++ = BPF_JMP_IMM(BPF_JSGE, BPF_REG_A, 0, 2);
	*insn++ = BPF_ALU32_REG(BPF_XOR, BPF_REG_A, BPF_REG_A);
	*insn   = BPF_EXIT_INSN();

	*insnp = insn;
	return true;
}

/**
 *	bpf_convert_filter - convert filter program
 *	@prog: the user passed filter program
 *	@len: the length of the user passed filter program
 *	@new_prog: allocated 'struct bpf_prog' or NULL
 *	@new_len: pointer to store length of converted program
 *	@seen_ld_abs: bool whether we've seen ld_abs/ind
 *
 * Remap 'sock_filter' style classic BPF (cBPF) instruction set to 'bpf_insn'
 * style extended BPF (eBPF).
 * Conversion workflow:
 *
 * 1) First pass for calculating the new program length:
 *   bpf_convert_filter(old_prog, old_len, NULL, &new_len, &seen_ld_abs)
 *
 * 2) 2nd pass to remap in two passes: 1st pass finds new
 *    jump offsets, 2nd pass remapping:
 *   bpf_convert_filter(old_prog, old_len, new_prog, &new_len, &seen_ld_abs)
 */
static int bpf_convert_filter(struct sock_filter *prog, int len,
			      struct bpf_prog *new_prog, int *new_len,
			      bool *seen_ld_abs)
{
	int new_flen = 0, pass = 0, target, i, stack_off;
	struct bpf_insn *new_insn, *first_insn = NULL;
	struct sock_filter *fp;
	int *addrs = NULL;
	u8 bpf_src;

	BUILD_BUG_ON(BPF_MEMWORDS * sizeof(u32) > MAX_BPF_STACK);
	BUILD_BUG_ON(BPF_REG_FP + 1 != MAX_BPF_REG);

	if (len <= 0 || len > BPF_MAXINSNS)
		return -EINVAL;

	if (new_prog) {
		first_insn = new_prog->insnsi;
		addrs = kcalloc(len, sizeof(*addrs),
				GFP_KERNEL | __GFP_NOWARN);
		if (!addrs)
			return -ENOMEM;
	}

do_pass:
	new_insn = first_insn;
	fp = prog;

	/* Classic BPF related prologue emission. */
	if (new_prog) {
		/* Classic BPF expects A and X to be reset first. These need
		 * to be guaranteed to be the first two instructions.
		 */
		*new_insn++ = BPF_ALU32_REG(BPF_XOR, BPF_REG_A, BPF_REG_A);
		*new_insn++ = BPF_ALU32_REG(BPF_XOR, BPF_REG_X, BPF_REG_X);

		/* All programs must keep CTX in callee saved BPF_REG_CTX.
		 * In eBPF case it's done by the compiler, here we need to
		 * do this ourself. Initial CTX is present in BPF_REG_ARG1.
		 */
		*new_insn++ = BPF_MOV64_REG(BPF_REG_CTX, BPF_REG_ARG1);
		if (*seen_ld_abs) {
			/* For packet access in classic BPF, cache skb->data
			 * in callee-saved BPF R8 and skb->len - skb->data_len
			 * (headlen) in BPF R9. Since classic BPF is read-only
			 * on CTX, we only need to cache it once.
			 */
			*new_insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct sk_buff, data),
						  BPF_REG_D, BPF_REG_CTX,
						  offsetof(struct sk_buff, data));
			*new_insn++ = BPF_LDX_MEM(BPF_W, BPF_REG_H, BPF_REG_CTX,
						  offsetof(struct sk_buff, len));
			*new_insn++ = BPF_LDX_MEM(BPF_W, BPF_REG_TMP, BPF_REG_CTX,
						  offsetof(struct sk_buff, data_len));
			*new_insn++ = BPF_ALU32_REG(BPF_SUB, BPF_REG_H, BPF_REG_TMP);
		}
	} else {
		new_insn += 3;
	}

	for (i = 0; i < len; fp++, i++) {
		struct bpf_insn tmp_insns[32] = { };
		struct bpf_insn *insn = tmp_insns;

		if (addrs)
			addrs[i] = new_insn - first_insn;

		switch (fp->code) {
		/* All arithmetic insns and skb loads map as-is. */
		case BPF_ALU | BPF_ADD | BPF_X:
		case BPF_ALU | BPF_ADD | BPF_K:
		case BPF_ALU | BPF_SUB | BPF_X:
		case BPF_ALU | BPF_SUB | BPF_K:
		case BPF_ALU | BPF_AND | BPF_X:
		case BPF_ALU | BPF_AND | BPF_K:
		case BPF_ALU | BPF_OR | BPF_X:
		case BPF_ALU | BPF_OR | BPF_K:
		case BPF_ALU | BPF_LSH | BPF_X:
		case BPF_ALU | BPF_LSH | BPF_K:
		case BPF_ALU | BPF_RSH | BPF_X:
		case BPF_ALU | BPF_RSH | BPF_K:
		case BPF_ALU | BPF_XOR | BPF_X:
		case BPF_ALU | BPF_XOR | BPF_K:
		case BPF_ALU | BPF_MUL | BPF_X:
		case BPF_ALU | BPF_MUL | BPF_K:
		case BPF_ALU | BPF_DIV | BPF_X:
		case BPF_ALU | BPF_DIV | BPF_K:
		case BPF_ALU | BPF_MOD | BPF_X:
		case BPF_ALU | BPF_MOD | BPF_K:
		case BPF_ALU | BPF_NEG:
		case BPF_LD | BPF_ABS | BPF_W:
		case BPF_LD | BPF_ABS | BPF_H:
		case BPF_LD | BPF_ABS | BPF_B:
		case BPF_LD | BPF_IND | BPF_W:
		case BPF_LD | BPF_IND | BPF_H:
		case BPF_LD | BPF_IND | BPF_B:
			/* Check for overloaded BPF extension and
			 * directly convert it if found, otherwise
			 * just move on with mapping.
			 */
			if (BPF_CLASS(fp->code) == BPF_LD &&
			    BPF_MODE(fp->code) == BPF_ABS &&
			    convert_bpf_extensions(fp, &insn))
				break;
			if (BPF_CLASS(fp->code) == BPF_LD &&
			    convert_bpf_ld_abs(fp, &insn)) {
				*seen_ld_abs = true;
				break;
			}

			if (fp->code == (BPF_ALU | BPF_DIV | BPF_X) ||
			    fp->code == (BPF_ALU | BPF_MOD | BPF_X)) {
				*insn++ = BPF_MOV32_REG(BPF_REG_X, BPF_REG_X);
				/* Error with exception code on div/mod by 0.
				 * For cBPF programs, this was always return 0.
				 */
				*insn++ = BPF_JMP_IMM(BPF_JNE, BPF_REG_X, 0, 2);
				*insn++ = BPF_ALU32_REG(BPF_XOR, BPF_REG_A, BPF_REG_A);
				*insn++ = BPF_EXIT_INSN();
			}

			*insn = BPF_RAW_INSN(fp->code, BPF_REG_A, BPF_REG_X, 0, fp->k);
			break;

		/* Jump transformation cannot use BPF block macros
		 * everywhere as offset calculation and target updates
		 * require a bit more work than the rest, i.e. jump
		 * opcodes map as-is, but offsets need adjustment.
		 */

#define BPF_EMIT_JMP							\
	do {								\
		const s32 off_min = S16_MIN, off_max = S16_MAX;		\
		s32 off;						\
									\
		if (target >= len || target < 0)			\
			goto err;					\
		off = addrs ? addrs[target] - addrs[i] - 1 : 0;		\
		/* Adjust pc relative offset for 2nd or 3rd insn. */	\
		off -= insn - tmp_insns;				\
		/* Reject anything not fitting into insn->off. */	\
		if (off < off_min || off > off_max)			\
			goto err;					\
		insn->off = off;					\
	} while (0)

		case BPF_JMP | BPF_JA:
			target = i + fp->k + 1;
			insn->code = fp->code;
			BPF_EMIT_JMP;
			break;

		case BPF_JMP | BPF_JEQ | BPF_K:
		case BPF_JMP | BPF_JEQ | BPF_X:
		case BPF_JMP | BPF_JSET | BPF_K:
		case BPF_JMP | BPF_JSET | BPF_X:
		case BPF_JMP | BPF_JGT | BPF_K:
		case BPF_JMP | BPF_JGT | BPF_X:
		case BPF_JMP | BPF_JGE | BPF_K:
		case BPF_JMP | BPF_JGE | BPF_X:
			if (BPF_SRC(fp->code) == BPF_K && (int) fp->k < 0) {
				/* BPF immediates are signed, zero extend
				 * immediate into tmp register and use it
				 * in compare insn.
				 */
				*insn++ = BPF_MOV32_IMM(BPF_REG_TMP, fp->k);

				insn->dst_reg = BPF_REG_A;
				insn->src_reg = BPF_REG_TMP;
				bpf_src = BPF_X;
			} else {
				insn->dst_reg = BPF_REG_A;
				insn->imm = fp->k;
				bpf_src = BPF_SRC(fp->code);
				insn->src_reg = bpf_src == BPF_X ? BPF_REG_X : 0;
			}

			/* Common case where 'jump_false' is next insn. */
			if (fp->jf == 0) {
				insn->code = BPF_JMP | BPF_OP(fp->code) | bpf_src;
				target = i + fp->jt + 1;
				BPF_EMIT_JMP;
				break;
			}

			/* Convert some jumps when 'jump_true' is next insn. */
			if (fp->jt == 0) {
				switch (BPF_OP(fp->code)) {
				case BPF_JEQ:
					insn->code = BPF_JMP | BPF_JNE | bpf_src;
					break;
				case BPF_JGT:
					insn->code = BPF_JMP | BPF_JLE | bpf_src;
					break;
				case BPF_JGE:
					insn->code = BPF_JMP | BPF_JLT | bpf_src;
					break;
				default:
					goto jmp_rest;
				}

				target = i + fp->jf + 1;
				BPF_EMIT_JMP;
				break;
			}
jmp_rest:
			/* Other jumps are mapped into two insns: Jxx and JA. */
			target = i + fp->jt + 1;
			insn->code = BPF_JMP | BPF_OP(fp->code) | bpf_src;
			BPF_EMIT_JMP;
			insn++;

			insn->code = BPF_JMP | BPF_JA;
			target = i + fp->jf + 1;
			BPF_EMIT_JMP;
			break;

		/* ldxb 4 * ([14] & 0xf) is remaped into 6 insns. */
		case BPF_LDX | BPF_MSH | BPF_B: {
			struct sock_filter tmp = {
				.code	= BPF_LD | BPF_ABS | BPF_B,
				.k	= fp->k,
			};

			*seen_ld_abs = true;

			/* X = A */
			*insn++ = BPF_MOV64_REG(BPF_REG_X, BPF_REG_A);
			/* A = BPF_R0 = *(u8 *) (skb->data + K) */
			convert_bpf_ld_abs(&tmp, &insn);
			insn++;
			/* A &= 0xf */
			*insn++ = BPF_ALU32_IMM(BPF_AND, BPF_REG_A, 0xf);
			/* A <<= 2 */
			*insn++ = BPF_ALU32_IMM(BPF_LSH, BPF_REG_A, 2);
			/* tmp = X */
			*insn++ = BPF_MOV64_REG(BPF_REG_TMP, BPF_REG_X);
			/* X = A */
			*insn++ = BPF_MOV64_REG(BPF_REG_X, BPF_REG_A);
			/* A = tmp */
			*insn = BPF_MOV64_REG(BPF_REG_A, BPF_REG_TMP);
			break;
		}
		/* RET_K is remaped into 2 insns. RET_A case doesn't need an
		 * extra mov as BPF_REG_0 is already mapped into BPF_REG_A.
		 */
		case BPF_RET | BPF_A:
		case BPF_RET | BPF_K:
			if (BPF_RVAL(fp->code) == BPF_K)
				*insn++ = BPF_MOV32_RAW(BPF_K, BPF_REG_0,
							0, fp->k);
			*insn = BPF_EXIT_INSN();
			break;

		/* Store to stack. */
		case BPF_ST:
		case BPF_STX:
			stack_off = fp->k * 4  + 4;
			*insn = BPF_STX_MEM(BPF_W, BPF_REG_FP, BPF_CLASS(fp->code) ==
					    BPF_ST ? BPF_REG_A : BPF_REG_X,
					    -stack_off);
			/* check_load_and_stores() verifies that classic BPF can
			 * load from stack only after write, so tracking
			 * stack_depth for ST|STX insns is enough
			 */
			if (new_prog && new_prog->aux->stack_depth < stack_off)
				new_prog->aux->stack_depth = stack_off;
			break;

		/* Load from stack. */
		case BPF_LD | BPF_MEM:
		case BPF_LDX | BPF_MEM:
			stack_off = fp->k * 4  + 4;
			*insn = BPF_LDX_MEM(BPF_W, BPF_CLASS(fp->code) == BPF_LD  ?
					    BPF_REG_A : BPF_REG_X, BPF_REG_FP,
					    -stack_off);
			break;

		/* A = K or X = K */
		case BPF_LD | BPF_IMM:
		case BPF_LDX | BPF_IMM:
			*insn = BPF_MOV32_IMM(BPF_CLASS(fp->code) == BPF_LD ?
					      BPF_REG_A : BPF_REG_X, fp->k);
			break;

		/* X = A */
		case BPF_MISC | BPF_TAX:
			*insn = BPF_MOV64_REG(BPF_REG_X, BPF_REG_A);
			break;

		/* A = X */
		case BPF_MISC | BPF_TXA:
			*insn = BPF_MOV64_REG(BPF_REG_A, BPF_REG_X);
			break;

		/* A = skb->len or X = skb->len */
		case BPF_LD | BPF_W | BPF_LEN:
		case BPF_LDX | BPF_W | BPF_LEN:
			*insn = BPF_LDX_MEM(BPF_W, BPF_CLASS(fp->code) == BPF_LD ?
					    BPF_REG_A : BPF_REG_X, BPF_REG_CTX,
					    offsetof(struct sk_buff, len));
			break;

		/* Access seccomp_data fields. */
		case BPF_LDX | BPF_ABS | BPF_W:
			/* A = *(u32 *) (ctx + K) */
			*insn = BPF_LDX_MEM(BPF_W, BPF_REG_A, BPF_REG_CTX, fp->k);
			break;

		/* Unknown instruction. */
		default:
			goto err;
		}

		insn++;
		if (new_prog)
			memcpy(new_insn, tmp_insns,
			       sizeof(*insn) * (insn - tmp_insns));
		new_insn += insn - tmp_insns;
	}

	if (!new_prog) {
		/* Only calculating new length. */
		*new_len = new_insn - first_insn;
		if (*seen_ld_abs)
			*new_len += 4; /* Prologue bits. */
		return 0;
	}

	pass++;
	if (new_flen != new_insn - first_insn) {
		new_flen = new_insn - first_insn;
		if (pass > 2)
			goto err;
		goto do_pass;
	}

	kfree(addrs);
	BUG_ON(*new_len != new_flen);
	return 0;
err:
	kfree(addrs);
	return -EINVAL;
}

/* Security:
 *
 * As we dont want to clear mem[] array for each packet going through
 * __bpf_prog_run(), we check that filter loaded by user never try to read
 * a cell if not previously written, and we check all branches to be sure
 * a malicious user doesn't try to abuse us.
 */
static int check_load_and_stores(const struct sock_filter *filter, int flen)
{
	u16 *masks, memvalid = 0; /* One bit per cell, 16 cells */
	int pc, ret = 0;

	BUILD_BUG_ON(BPF_MEMWORDS > 16);

	masks = kmalloc_array(flen, sizeof(*masks), GFP_KERNEL);
	if (!masks)
		return -ENOMEM;

	memset(masks, 0xff, flen * sizeof(*masks));

	for (pc = 0; pc < flen; pc++) {
		memvalid &= masks[pc];

		switch (filter[pc].code) {
		case BPF_ST:
		case BPF_STX:
			memvalid |= (1 << filter[pc].k);
			break;
		case BPF_LD | BPF_MEM:
		case BPF_LDX | BPF_MEM:
			if (!(memvalid & (1 << filter[pc].k))) {
				ret = -EINVAL;
				goto error;
			}
			break;
		case BPF_JMP | BPF_JA:
			/* A jump must set masks on target */
			masks[pc + 1 + filter[pc].k] &= memvalid;
			memvalid = ~0;
			break;
		case BPF_JMP | BPF_JEQ | BPF_K:
		case BPF_JMP | BPF_JEQ | BPF_X:
		case BPF_JMP | BPF_JGE | BPF_K:
		case BPF_JMP | BPF_JGE | BPF_X:
		case BPF_JMP | BPF_JGT | BPF_K:
		case BPF_JMP | BPF_JGT | BPF_X:
		case BPF_JMP | BPF_JSET | BPF_K:
		case BPF_JMP | BPF_JSET | BPF_X:
			/* A jump must set masks on targets */
			masks[pc + 1 + filter[pc].jt] &= memvalid;
			masks[pc + 1 + filter[pc].jf] &= memvalid;
			memvalid = ~0;
			break;
		}
	}
error:
	kfree(masks);
	return ret;
}

static bool chk_code_allowed(u16 code_to_probe)
{
	static const bool codes[] = {
		/* 32 bit ALU operations */
		[BPF_ALU | BPF_ADD | BPF_K] = true,
		[BPF_ALU | BPF_ADD | BPF_X] = true,
		[BPF_ALU | BPF_SUB | BPF_K] = true,
		[BPF_ALU | BPF_SUB | BPF_X] = true,
		[BPF_ALU | BPF_MUL | BPF_K] = true,
		[BPF_ALU | BPF_MUL | BPF_X] = true,
		[BPF_ALU | BPF_DIV | BPF_K] = true,
		[BPF_ALU | BPF_DIV | BPF_X] = true,
		[BPF_ALU | BPF_MOD | BPF_K] = true,
		[BPF_ALU | BPF_MOD | BPF_X] = true,
		[BPF_ALU | BPF_AND | BPF_K] = true,
		[BPF_ALU | BPF_AND | BPF_X] = true,
		[BPF_ALU | BPF_OR | BPF_K] = true,
		[BPF_ALU | BPF_OR | BPF_X] = true,
		[BPF_ALU | BPF_XOR | BPF_K] = true,
		[BPF_ALU | BPF_XOR | BPF_X] = true,
		[BPF_ALU | BPF_LSH | BPF_K] = true,
		[BPF_ALU | BPF_LSH | BPF_X] = true,
		[BPF_ALU | BPF_RSH | BPF_K] = true,
		[BPF_ALU | BPF_RSH | BPF_X] = true,
		[BPF_ALU | BPF_NEG] = true,
		/* Load instructions */
		[BPF_LD | BPF_W | BPF_ABS] = true,
		[BPF_LD | BPF_H | BPF_ABS] = true,
		[BPF_LD | BPF_B | BPF_ABS] = true,
		[BPF_LD | BPF_W | BPF_LEN] = true,
		[BPF_LD | BPF_W | BPF_IND] = true,
		[BPF_LD | BPF_H | BPF_IND] = true,
		[BPF_LD | BPF_B | BPF_IND] = true,
		[BPF_LD | BPF_IMM] = true,
		[BPF_LD | BPF_MEM] = true,
		[BPF_LDX | BPF_W | BPF_LEN] = true,
		[BPF_LDX | BPF_B | BPF_MSH] = true,
		[BPF_LDX | BPF_IMM] = true,
		[BPF_LDX | BPF_MEM] = true,
		/* Store instructions */
		[BPF_ST] = true,
		[BPF_STX] = true,
		/* Misc instructions */
		[BPF_MISC | BPF_TAX] = true,
		[BPF_MISC | BPF_TXA] = true,
		/* Return instructions */
		[BPF_RET | BPF_K] = true,
		[BPF_RET | BPF_A] = true,
		/* Jump instructions */
		[BPF_JMP | BPF_JA] = true,
		[BPF_JMP | BPF_JEQ | BPF_K] = true,
		[BPF_JMP | BPF_JEQ | BPF_X] = true,
		[BPF_JMP | BPF_JGE | BPF_K] = true,
		[BPF_JMP | BPF_JGE | BPF_X] = true,
		[BPF_JMP | BPF_JGT | BPF_K] = true,
		[BPF_JMP | BPF_JGT | BPF_X] = true,
		[BPF_JMP | BPF_JSET | BPF_K] = true,
		[BPF_JMP | BPF_JSET | BPF_X] = true,
	};

	if (code_to_probe >= ARRAY_SIZE(codes))
		return false;

	return codes[code_to_probe];
}

static bool bpf_check_basics_ok(const struct sock_filter *filter,
				unsigned int flen)
{
	if (filter == NULL)
		return false;
	if (flen == 0 || flen > BPF_MAXINSNS)
		return false;

	return true;
}

/**
 *	bpf_check_classic - verify socket filter code
 *	@filter: filter to verify
 *	@flen: length of filter
 *
 * Check the user's filter code. If we let some ugly
 * filter code slip through kaboom! The filter must contain
 * no references or jumps that are out of range, no illegal
 * instructions, and must end with a RET instruction.
 *
 * All jumps are forward as they are not signed.
 *
 * Returns 0 if the rule set is legal or -EINVAL if not.
 */
static int bpf_check_classic(const struct sock_filter *filter,
			     unsigned int flen)
{
	bool anc_found;
	int pc;

	/* Check the filter code now */
	for (pc = 0; pc < flen; pc++) {
		const struct sock_filter *ftest = &filter[pc];

		/* May we actually operate on this code? */
		if (!chk_code_allowed(ftest->code))
			return -EINVAL;

		/* Some instructions need special checks */
		switch (ftest->code) {
		case BPF_ALU | BPF_DIV | BPF_K:
		case BPF_ALU | BPF_MOD | BPF_K:
			/* Check for division by zero */
			if (ftest->k == 0)
				return -EINVAL;
			break;
		case BPF_ALU | BPF_LSH | BPF_K:
		case BPF_ALU | BPF_RSH | BPF_K:
			if (ftest->k >= 32)
				return -EINVAL;
			break;
		case BPF_LD | BPF_MEM:
		case BPF_LDX | BPF_MEM:
		case BPF_ST:
		case BPF_STX:
			/* Check for invalid memory addresses */
			if (ftest->k >= BPF_MEMWORDS)
				return -EINVAL;
			break;
		case BPF_JMP | BPF_JA:
			/* Note, the large ftest->k might cause loops.
			 * Compare this with conditional jumps below,
			 * where offsets are limited. --ANK (981016)
			 */
			if (ftest->k >= (unsigned int)(flen - pc - 1))
				return -EINVAL;
			break;
		case BPF_JMP | BPF_JEQ | BPF_K:
		case BPF_JMP | BPF_JEQ | BPF_X:
		case BPF_JMP | BPF_JGE | BPF_K:
		case BPF_JMP | BPF_JGE | BPF_X:
		case BPF_JMP | BPF_JGT | BPF_K:
		case BPF_JMP | BPF_JGT | BPF_X:
		case BPF_JMP | BPF_JSET | BPF_K:
		case BPF_JMP | BPF_JSET | BPF_X:
			/* Both conditionals must be safe */
			if (pc + ftest->jt + 1 >= flen ||
			    pc + ftest->jf + 1 >= flen)
				return -EINVAL;
			break;
		case BPF_LD | BPF_W | BPF_ABS:
		case BPF_LD | BPF_H | BPF_ABS:
		case BPF_LD | BPF_B | BPF_ABS:
			anc_found = false;
			if (bpf_anc_helper(ftest) & BPF_ANC)
				anc_found = true;
			/* Ancillary operation unknown or unsupported */
			if (anc_found == false && ftest->k >= SKF_AD_OFF)
				return -EINVAL;
		}
	}

	/* Last instruction must be a RET code */
	switch (filter[flen - 1].code) {
	case BPF_RET | BPF_K:
	case BPF_RET | BPF_A:
		return check_load_and_stores(filter, flen);
	}

	return -EINVAL;
}

static int bpf_prog_store_orig_filter(struct bpf_prog *fp,
				      const struct sock_fprog *fprog)
{
	unsigned int fsize = bpf_classic_proglen(fprog);
	struct sock_fprog_kern *fkprog;

	fp->orig_prog = kmalloc(sizeof(*fkprog), GFP_KERNEL);
	if (!fp->orig_prog)
		return -ENOMEM;

	fkprog = fp->orig_prog;
	fkprog->len = fprog->len;

	fkprog->filter = kmemdup(fp->insns, fsize,
				 GFP_KERNEL | __GFP_NOWARN);
	if (!fkprog->filter) {
		kfree(fp->orig_prog);
		return -ENOMEM;
	}

	return 0;
}

static void bpf_release_orig_filter(struct bpf_prog *fp)
{
	struct sock_fprog_kern *fprog = fp->orig_prog;

	if (fprog) {
		kfree(fprog->filter);
		kfree(fprog);
	}
}

static void __bpf_prog_release(struct bpf_prog *prog)
{
	if (prog->type == BPF_PROG_TYPE_SOCKET_FILTER) {
		bpf_prog_put(prog);
	} else {
		bpf_release_orig_filter(prog);
		bpf_prog_free(prog);
	}
}

static void __sk_filter_release(struct sk_filter *fp)
{
	__bpf_prog_release(fp->prog);
	kfree(fp);
}

/**
 * 	sk_filter_release_rcu - Release a socket filter by rcu_head
 *	@rcu: rcu_head that contains the sk_filter to free
 */
static void sk_filter_release_rcu(struct rcu_head *rcu)
{
	struct sk_filter *fp = container_of(rcu, struct sk_filter, rcu);

	__sk_filter_release(fp);
}

/**
 *	sk_filter_release - release a socket filter
 *	@fp: filter to remove
 *
 *	Remove a filter from a socket and release its resources.
 */
static void sk_filter_release(struct sk_filter *fp)
{
	if (refcount_dec_and_test(&fp->refcnt))
		call_rcu(&fp->rcu, sk_filter_release_rcu);
}

void sk_filter_uncharge(struct sock *sk, struct sk_filter *fp)
{
	u32 filter_size = bpf_prog_size(fp->prog->len);

	atomic_sub(filter_size, &sk->sk_omem_alloc);
	sk_filter_release(fp);
}

/* try to charge the socket memory if there is space available
 * return true on success
 */
static bool __sk_filter_charge(struct sock *sk, struct sk_filter *fp)
{
	u32 filter_size = bpf_prog_size(fp->prog->len);

	/* same check as in sock_kmalloc() */
	if (filter_size <= sysctl_optmem_max &&
	    atomic_read(&sk->sk_omem_alloc) + filter_size < sysctl_optmem_max) {
		atomic_add(filter_size, &sk->sk_omem_alloc);
		return true;
	}
	return false;
}

bool sk_filter_charge(struct sock *sk, struct sk_filter *fp)
{
	if (!refcount_inc_not_zero(&fp->refcnt))
		return false;

	if (!__sk_filter_charge(sk, fp)) {
		sk_filter_release(fp);
		return false;
	}
	return true;
}

static struct bpf_prog *bpf_migrate_filter(struct bpf_prog *fp)
{
	struct sock_filter *old_prog;
	struct bpf_prog *old_fp;
	int err, new_len, old_len = fp->len;
	bool seen_ld_abs = false;

	/* We are free to overwrite insns et al right here as it
	 * won't be used at this point in time anymore internally
	 * after the migration to the internal BPF instruction
	 * representation.
	 */
	BUILD_BUG_ON(sizeof(struct sock_filter) !=
		     sizeof(struct bpf_insn));

	/* Conversion cannot happen on overlapping memory areas,
	 * so we need to keep the user BPF around until the 2nd
	 * pass. At this time, the user BPF is stored in fp->insns.
	 */
	old_prog = kmemdup(fp->insns, old_len * sizeof(struct sock_filter),
			   GFP_KERNEL | __GFP_NOWARN);
	if (!old_prog) {
		err = -ENOMEM;
		goto out_err;
	}

	/* 1st pass: calculate the new program length. */
	err = bpf_convert_filter(old_prog, old_len, NULL, &new_len,
				 &seen_ld_abs);
	if (err)
		goto out_err_free;

	/* Expand fp for appending the new filter representation. */
	old_fp = fp;
	fp = bpf_prog_realloc(old_fp, bpf_prog_size(new_len), 0);
	if (!fp) {
		/* The old_fp is still around in case we couldn't
		 * allocate new memory, so uncharge on that one.
		 */
		fp = old_fp;
		err = -ENOMEM;
		goto out_err_free;
	}

	fp->len = new_len;

	/* 2nd pass: remap sock_filter insns into bpf_insn insns. */
	err = bpf_convert_filter(old_prog, old_len, fp, &new_len,
				 &seen_ld_abs);
	if (err)
		/* 2nd bpf_convert_filter() can fail only if it fails
		 * to allocate memory, remapping must succeed. Note,
		 * that at this time old_fp has already been released
		 * by krealloc().
		 */
		goto out_err_free;

	fp = bpf_prog_select_runtime(fp, &err);
	if (err)
		goto out_err_free;

	kfree(old_prog);
	return fp;

out_err_free:
	kfree(old_prog);
out_err:
	__bpf_prog_release(fp);
	return ERR_PTR(err);
}

static struct bpf_prog *bpf_prepare_filter(struct bpf_prog *fp,
					   bpf_aux_classic_check_t trans)
{
	int err;

	fp->bpf_func = NULL;
	fp->jited = 0;

	err = bpf_check_classic(fp->insns, fp->len);
	if (err) {
		__bpf_prog_release(fp);
		return ERR_PTR(err);
	}

	/* There might be additional checks and transformations
	 * needed on classic filters, f.e. in case of seccomp.
	 */
	if (trans) {
		err = trans(fp->insns, fp->len);
		if (err) {
			__bpf_prog_release(fp);
			return ERR_PTR(err);
		}
	}

	/* Probe if we can JIT compile the filter and if so, do
	 * the compilation of the filter.
	 */
	bpf_jit_compile(fp);

	/* JIT compiler couldn't process this filter, so do the
	 * internal BPF translation for the optimized interpreter.
	 */
	if (!fp->jited)
		fp = bpf_migrate_filter(fp);

	return fp;
}

/**
 *	bpf_prog_create - create an unattached filter
 *	@pfp: the unattached filter that is created
 *	@fprog: the filter program
 *
 * Create a filter independent of any socket. We first run some
 * sanity checks on it to make sure it does not explode on us later.
 * If an error occurs or there is insufficient memory for the filter
 * a negative errno code is returned. On success the return is zero.
 */
int bpf_prog_create(struct bpf_prog **pfp, struct sock_fprog_kern *fprog)
{
	unsigned int fsize = bpf_classic_proglen(fprog);
	struct bpf_prog *fp;

	/* Make sure new filter is there and in the right amounts. */
	if (!bpf_check_basics_ok(fprog->filter, fprog->len))
		return -EINVAL;

	fp = bpf_prog_alloc(bpf_prog_size(fprog->len), 0);
	if (!fp)
		return -ENOMEM;

	memcpy(fp->insns, fprog->filter, fsize);

	fp->len = fprog->len;
	/* Since unattached filters are not copied back to user
	 * space through sk_get_filter(), we do not need to hold
	 * a copy here, and can spare us the work.
	 */
	fp->orig_prog = NULL;

	/* bpf_prepare_filter() already takes care of freeing
	 * memory in case something goes wrong.
	 */
	fp = bpf_prepare_filter(fp, NULL);
	if (IS_ERR(fp))
		return PTR_ERR(fp);

	*pfp = fp;
	return 0;
}
EXPORT_SYMBOL_GPL(bpf_prog_create);

/**
 *	bpf_prog_create_from_user - create an unattached filter from user buffer
 *	@pfp: the unattached filter that is created
 *	@fprog: the filter program
 *	@trans: post-classic verifier transformation handler
 *	@save_orig: save classic BPF program
 *
 * This function effectively does the same as bpf_prog_create(), only
 * that it builds up its insns buffer from user space provided buffer.
 * It also allows for passing a bpf_aux_classic_check_t handler.
 */
int bpf_prog_create_from_user(struct bpf_prog **pfp, struct sock_fprog *fprog,
			      bpf_aux_classic_check_t trans, bool save_orig)
{
	unsigned int fsize = bpf_classic_proglen(fprog);
	struct bpf_prog *fp;
	int err;

	/* Make sure new filter is there and in the right amounts. */
	if (!bpf_check_basics_ok(fprog->filter, fprog->len))
		return -EINVAL;

	fp = bpf_prog_alloc(bpf_prog_size(fprog->len), 0);
	if (!fp)
		return -ENOMEM;

	if (copy_from_user(fp->insns, fprog->filter, fsize)) {
		__bpf_prog_free(fp);
		return -EFAULT;
	}

	fp->len = fprog->len;
	fp->orig_prog = NULL;

	if (save_orig) {
		err = bpf_prog_store_orig_filter(fp, fprog);
		if (err) {
			__bpf_prog_free(fp);
			return -ENOMEM;
		}
	}

	/* bpf_prepare_filter() already takes care of freeing
	 * memory in case something goes wrong.
	 */
	fp = bpf_prepare_filter(fp, trans);
	if (IS_ERR(fp))
		return PTR_ERR(fp);

	*pfp = fp;
	return 0;
}
EXPORT_SYMBOL_GPL(bpf_prog_create_from_user);

void bpf_prog_destroy(struct bpf_prog *fp)
{
	__bpf_prog_release(fp);
}
EXPORT_SYMBOL_GPL(bpf_prog_destroy);

static int __sk_attach_prog(struct bpf_prog *prog, struct sock *sk)
{
	struct sk_filter *fp, *old_fp;

	fp = kmalloc(sizeof(*fp), GFP_KERNEL);
	if (!fp)
		return -ENOMEM;

	fp->prog = prog;

	if (!__sk_filter_charge(sk, fp)) {
		kfree(fp);
		return -ENOMEM;
	}
	refcount_set(&fp->refcnt, 1);

	old_fp = rcu_dereference_protected(sk->sk_filter,
					   lockdep_sock_is_held(sk));
	rcu_assign_pointer(sk->sk_filter, fp);

	if (old_fp)
		sk_filter_uncharge(sk, old_fp);

	return 0;
}

static
struct bpf_prog *__get_filter(struct sock_fprog *fprog, struct sock *sk)
{
	unsigned int fsize = bpf_classic_proglen(fprog);
	struct bpf_prog *prog;
	int err;

	if (sock_flag(sk, SOCK_FILTER_LOCKED))
		return ERR_PTR(-EPERM);

	/* Make sure new filter is there and in the right amounts. */
	if (!bpf_check_basics_ok(fprog->filter, fprog->len))
		return ERR_PTR(-EINVAL);

	prog = bpf_prog_alloc(bpf_prog_size(fprog->len), 0);
	if (!prog)
		return ERR_PTR(-ENOMEM);

	if (copy_from_user(prog->insns, fprog->filter, fsize)) {
		__bpf_prog_free(prog);
		return ERR_PTR(-EFAULT);
	}

	prog->len = fprog->len;

	err = bpf_prog_store_orig_filter(prog, fprog);
	if (err) {
		__bpf_prog_free(prog);
		return ERR_PTR(-ENOMEM);
	}

	/* bpf_prepare_filter() already takes care of freeing
	 * memory in case something goes wrong.
	 */
	return bpf_prepare_filter(prog, NULL);
}

/**
 *	sk_attach_filter - attach a socket filter
 *	@fprog: the filter program
 *	@sk: the socket to use
 *
 * Attach the user's filter code. We first run some sanity checks on
 * it to make sure it does not explode on us later. If an error
 * occurs or there is insufficient memory for the filter a negative
 * errno code is returned. On success the return is zero.
 */
int sk_attach_filter(struct sock_fprog *fprog, struct sock *sk)
{
	struct bpf_prog *prog = __get_filter(fprog, sk);
	int err;

	if (IS_ERR(prog))
		return PTR_ERR(prog);

	err = __sk_attach_prog(prog, sk);
	if (err < 0) {
		__bpf_prog_release(prog);
		return err;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(sk_attach_filter);

int sk_reuseport_attach_filter(struct sock_fprog *fprog, struct sock *sk)
{
	struct bpf_prog *prog = __get_filter(fprog, sk);
	int err;

	if (IS_ERR(prog))
		return PTR_ERR(prog);

	if (bpf_prog_size(prog->len) > sysctl_optmem_max)
		err = -ENOMEM;
	else
		err = reuseport_attach_prog(sk, prog);

	if (err)
		__bpf_prog_release(prog);

	return err;
}

static struct bpf_prog *__get_bpf(u32 ufd, struct sock *sk)
{
	if (sock_flag(sk, SOCK_FILTER_LOCKED))
		return ERR_PTR(-EPERM);

	return bpf_prog_get_type(ufd, BPF_PROG_TYPE_SOCKET_FILTER);
}

int sk_attach_bpf(u32 ufd, struct sock *sk)
{
	struct bpf_prog *prog = __get_bpf(ufd, sk);
	int err;

	if (IS_ERR(prog))
		return PTR_ERR(prog);

	err = __sk_attach_prog(prog, sk);
	if (err < 0) {
		bpf_prog_put(prog);
		return err;
	}

	return 0;
}

int sk_reuseport_attach_bpf(u32 ufd, struct sock *sk)
{
	struct bpf_prog *prog;
	int err;

	if (sock_flag(sk, SOCK_FILTER_LOCKED))
		return -EPERM;

	prog = bpf_prog_get_type(ufd, BPF_PROG_TYPE_SOCKET_FILTER);
	if (PTR_ERR(prog) == -EINVAL)
		prog = bpf_prog_get_type(ufd, BPF_PROG_TYPE_SK_REUSEPORT);
	if (IS_ERR(prog))
		return PTR_ERR(prog);

	if (prog->type == BPF_PROG_TYPE_SK_REUSEPORT) {
		/* Like other non BPF_PROG_TYPE_SOCKET_FILTER
		 * bpf prog (e.g. sockmap).  It depends on the
		 * limitation imposed by bpf_prog_load().
		 * Hence, sysctl_optmem_max is not checked.
		 */
		if ((sk->sk_type != SOCK_STREAM &&
		     sk->sk_type != SOCK_DGRAM) ||
		    (sk->sk_protocol != IPPROTO_UDP &&
		     sk->sk_protocol != IPPROTO_TCP) ||
		    (sk->sk_family != AF_INET &&
		     sk->sk_family != AF_INET6)) {
			err = -ENOTSUPP;
			goto err_prog_put;
		}
	} else {
		/* BPF_PROG_TYPE_SOCKET_FILTER */
		if (bpf_prog_size(prog->len) > sysctl_optmem_max) {
			err = -ENOMEM;
			goto err_prog_put;
		}
	}

	err = reuseport_attach_prog(sk, prog);
err_prog_put:
	if (err)
		bpf_prog_put(prog);

	return err;
}

void sk_reuseport_prog_free(struct bpf_prog *prog)
{
	if (!prog)
		return;

	if (prog->type == BPF_PROG_TYPE_SK_REUSEPORT)
		bpf_prog_put(prog);
	else
		bpf_prog_destroy(prog);
}

struct bpf_scratchpad {
	union {
		__be32 diff[MAX_BPF_STACK / sizeof(__be32)];
		u8     buff[MAX_BPF_STACK];
	};
};

static DEFINE_PER_CPU(struct bpf_scratchpad, bpf_sp);

static inline int __bpf_try_make_writable(struct sk_buff *skb,
					  unsigned int write_len)
{
	return skb_ensure_writable(skb, write_len);
}

static inline int bpf_try_make_writable(struct sk_buff *skb,
					unsigned int write_len)
{
	int err = __bpf_try_make_writable(skb, write_len);

	bpf_compute_data_pointers(skb);
	return err;
}

static int bpf_try_make_head_writable(struct sk_buff *skb)
{
	return bpf_try_make_writable(skb, skb_headlen(skb));
}

static inline void bpf_push_mac_rcsum(struct sk_buff *skb)
{
	if (skb_at_tc_ingress(skb))
		skb_postpush_rcsum(skb, skb_mac_header(skb), skb->mac_len);
}

static inline void bpf_pull_mac_rcsum(struct sk_buff *skb)
{
	if (skb_at_tc_ingress(skb))
		skb_postpull_rcsum(skb, skb_mac_header(skb), skb->mac_len);
}

BPF_CALL_5(bpf_skb_store_bytes, struct sk_buff *, skb, u32, offset,
	   const void *, from, u32, len, u64, flags)
{
	void *ptr;

	if (unlikely(flags & ~(BPF_F_RECOMPUTE_CSUM | BPF_F_INVALIDATE_HASH)))
		return -EINVAL;
	if (unlikely(offset > INT_MAX))
		return -EFAULT;
	if (unlikely(bpf_try_make_writable(skb, offset + len)))
		return -EFAULT;

	ptr = skb->data + offset;
	if (flags & BPF_F_RECOMPUTE_CSUM)
		__skb_postpull_rcsum(skb, ptr, len, offset);

	memcpy(ptr, from, len);

	if (flags & BPF_F_RECOMPUTE_CSUM)
		__skb_postpush_rcsum(skb, ptr, len, offset);
	if (flags & BPF_F_INVALIDATE_HASH)
		skb_clear_hash(skb);

	return 0;
}

static const struct bpf_func_proto bpf_skb_store_bytes_proto = {
	.func		= bpf_skb_store_bytes,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
	.arg3_type	= ARG_PTR_TO_MEM,
	.arg4_type	= ARG_CONST_SIZE,
	.arg5_type	= ARG_ANYTHING,
};

BPF_CALL_4(bpf_skb_load_bytes, const struct sk_buff *, skb, u32, offset,
	   void *, to, u32, len)
{
	void *ptr;

	if (unlikely(offset > INT_MAX))
		goto err_clear;

	ptr = skb_header_pointer(skb, offset, len, to);
	if (unlikely(!ptr))
		goto err_clear;
	if (ptr != to)
		memcpy(to, ptr, len);

	return 0;
err_clear:
	memset(to, 0, len);
	return -EFAULT;
}

static const struct bpf_func_proto bpf_skb_load_bytes_proto = {
	.func		= bpf_skb_load_bytes,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
	.arg3_type	= ARG_PTR_TO_UNINIT_MEM,
	.arg4_type	= ARG_CONST_SIZE,
};

BPF_CALL_4(bpf_flow_dissector_load_bytes,
	   const struct bpf_flow_dissector *, ctx, u32, offset,
	   void *, to, u32, len)
{
	void *ptr;

	if (unlikely(offset > 0xffff))
		goto err_clear;

	if (unlikely(!ctx->skb))
		goto err_clear;

	ptr = skb_header_pointer(ctx->skb, offset, len, to);
	if (unlikely(!ptr))
		goto err_clear;
	if (ptr != to)
		memcpy(to, ptr, len);

	return 0;
err_clear:
	memset(to, 0, len);
	return -EFAULT;
}

static const struct bpf_func_proto bpf_flow_dissector_load_bytes_proto = {
	.func		= bpf_flow_dissector_load_bytes,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
	.arg3_type	= ARG_PTR_TO_UNINIT_MEM,
	.arg4_type	= ARG_CONST_SIZE,
};

BPF_CALL_5(bpf_skb_load_bytes_relative, const struct sk_buff *, skb,
	   u32, offset, void *, to, u32, len, u32, start_header)
{
	u8 *end = skb_tail_pointer(skb);
	u8 *start, *ptr;

	if (unlikely(offset > 0xffff))
		goto err_clear;

	switch (start_header) {
	case BPF_HDR_START_MAC:
		if (unlikely(!skb_mac_header_was_set(skb)))
			goto err_clear;
		start = skb_mac_header(skb);
		break;
	case BPF_HDR_START_NET:
		start = skb_network_header(skb);
		break;
	default:
		goto err_clear;
	}

	ptr = start + offset;

	if (likely(ptr + len <= end)) {
		memcpy(to, ptr, len);
		return 0;
	}

err_clear:
	memset(to, 0, len);
	return -EFAULT;
}

static const struct bpf_func_proto bpf_skb_load_bytes_relative_proto = {
	.func		= bpf_skb_load_bytes_relative,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
	.arg3_type	= ARG_PTR_TO_UNINIT_MEM,
	.arg4_type	= ARG_CONST_SIZE,
	.arg5_type	= ARG_ANYTHING,
};

BPF_CALL_2(bpf_skb_pull_data, struct sk_buff *, skb, u32, len)
{
	/* Idea is the following: should the needed direct read/write
	 * test fail during runtime, we can pull in more data and redo
	 * again, since implicitly, we invalidate previous checks here.
	 *
	 * Or, since we know how much we need to make read/writeable,
	 * this can be done once at the program beginning for direct
	 * access case. By this we overcome limitations of only current
	 * headroom being accessible.
	 */
	return bpf_try_make_writable(skb, len ? : skb_headlen(skb));
}

static const struct bpf_func_proto bpf_skb_pull_data_proto = {
	.func		= bpf_skb_pull_data,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
};

BPF_CALL_1(bpf_sk_fullsock, struct sock *, sk)
{
	return sk_fullsock(sk) ? (unsigned long)sk : (unsigned long)NULL;
}

static const struct bpf_func_proto bpf_sk_fullsock_proto = {
	.func		= bpf_sk_fullsock,
	.gpl_only	= false,
	.ret_type	= RET_PTR_TO_SOCKET_OR_NULL,
	.arg1_type	= ARG_PTR_TO_SOCK_COMMON,
};

static inline int sk_skb_try_make_writable(struct sk_buff *skb,
					   unsigned int write_len)
{
	int err = __bpf_try_make_writable(skb, write_len);

	bpf_compute_data_end_sk_skb(skb);
	return err;
}

BPF_CALL_2(sk_skb_pull_data, struct sk_buff *, skb, u32, len)
{
	/* Idea is the following: should the needed direct read/write
	 * test fail during runtime, we can pull in more data and redo
	 * again, since implicitly, we invalidate previous checks here.
	 *
	 * Or, since we know how much we need to make read/writeable,
	 * this can be done once at the program beginning for direct
	 * access case. By this we overcome limitations of only current
	 * headroom being accessible.
	 */
	return sk_skb_try_make_writable(skb, len ? : skb_headlen(skb));
}

static const struct bpf_func_proto sk_skb_pull_data_proto = {
	.func		= sk_skb_pull_data,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
};

BPF_CALL_5(bpf_l3_csum_replace, struct sk_buff *, skb, u32, offset,
	   u64, from, u64, to, u64, flags)
{
	__sum16 *ptr;

	if (unlikely(flags & ~(BPF_F_HDR_FIELD_MASK)))
		return -EINVAL;
	if (unlikely(offset > 0xffff || offset & 1))
		return -EFAULT;
	if (unlikely(bpf_try_make_writable(skb, offset + sizeof(*ptr))))
		return -EFAULT;

	ptr = (__sum16 *)(skb->data + offset);
	switch (flags & BPF_F_HDR_FIELD_MASK) {
	case 0:
		if (unlikely(from != 0))
			return -EINVAL;

		csum_replace_by_diff(ptr, to);
		break;
	case 2:
		csum_replace2(ptr, from, to);
		break;
	case 4:
		csum_replace4(ptr, from, to);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct bpf_func_proto bpf_l3_csum_replace_proto = {
	.func		= bpf_l3_csum_replace,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
	.arg3_type	= ARG_ANYTHING,
	.arg4_type	= ARG_ANYTHING,
	.arg5_type	= ARG_ANYTHING,
};

BPF_CALL_5(bpf_l4_csum_replace, struct sk_buff *, skb, u32, offset,
	   u64, from, u64, to, u64, flags)
{
	bool is_pseudo = flags & BPF_F_PSEUDO_HDR;
	bool is_mmzero = flags & BPF_F_MARK_MANGLED_0;
	bool do_mforce = flags & BPF_F_MARK_ENFORCE;
	__sum16 *ptr;

	if (unlikely(flags & ~(BPF_F_MARK_MANGLED_0 | BPF_F_MARK_ENFORCE |
			       BPF_F_PSEUDO_HDR | BPF_F_HDR_FIELD_MASK)))
		return -EINVAL;
	if (unlikely(offset > 0xffff || offset & 1))
		return -EFAULT;
	if (unlikely(bpf_try_make_writable(skb, offset + sizeof(*ptr))))
		return -EFAULT;

	ptr = (__sum16 *)(skb->data + offset);
	if (is_mmzero && !do_mforce && !*ptr)
		return 0;

	switch (flags & BPF_F_HDR_FIELD_MASK) {
	case 0:
		if (unlikely(from != 0))
			return -EINVAL;

		inet_proto_csum_replace_by_diff(ptr, skb, to, is_pseudo);
		break;
	case 2:
		inet_proto_csum_replace2(ptr, skb, from, to, is_pseudo);
		break;
	case 4:
		inet_proto_csum_replace4(ptr, skb, from, to, is_pseudo);
		break;
	default:
		return -EINVAL;
	}

	if (is_mmzero && !*ptr)
		*ptr = CSUM_MANGLED_0;
	return 0;
}

static const struct bpf_func_proto bpf_l4_csum_replace_proto = {
	.func		= bpf_l4_csum_replace,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
	.arg3_type	= ARG_ANYTHING,
	.arg4_type	= ARG_ANYTHING,
	.arg5_type	= ARG_ANYTHING,
};

BPF_CALL_5(bpf_csum_diff, __be32 *, from, u32, from_size,
	   __be32 *, to, u32, to_size, __wsum, seed)
{
	struct bpf_scratchpad *sp = this_cpu_ptr(&bpf_sp);
	u32 diff_size = from_size + to_size;
	int i, j = 0;

	/* This is quite flexible, some examples:
	 *
	 * from_size == 0, to_size > 0,  seed := csum --> pushing data
	 * from_size > 0,  to_size == 0, seed := csum --> pulling data
	 * from_size > 0,  to_size > 0,  seed := 0    --> diffing data
	 *
	 * Even for diffing, from_size and to_size don't need to be equal.
	 */
	if (unlikely(((from_size | to_size) & (sizeof(__be32) - 1)) ||
		     diff_size > sizeof(sp->diff)))
		return -EINVAL;

	for (i = 0; i < from_size / sizeof(__be32); i++, j++)
		sp->diff[j] = ~from[i];
	for (i = 0; i <   to_size / sizeof(__be32); i++, j++)
		sp->diff[j] = to[i];

	return csum_partial(sp->diff, diff_size, seed);
}

static const struct bpf_func_proto bpf_csum_diff_proto = {
	.func		= bpf_csum_diff,
	.gpl_only	= false,
	.pkt_access	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_MEM_OR_NULL,
	.arg2_type	= ARG_CONST_SIZE_OR_ZERO,
	.arg3_type	= ARG_PTR_TO_MEM_OR_NULL,
	.arg4_type	= ARG_CONST_SIZE_OR_ZERO,
	.arg5_type	= ARG_ANYTHING,
};

BPF_CALL_2(bpf_csum_update, struct sk_buff *, skb, __wsum, csum)
{
	/* The interface is to be used in combination with bpf_csum_diff()
	 * for direct packet writes. csum rotation for alignment as well
	 * as emulating csum_sub() can be done from the eBPF program.
	 */
	if (skb->ip_summed == CHECKSUM_COMPLETE)
		return (skb->csum = csum_add(skb->csum, csum));

	return -ENOTSUPP;
}

static const struct bpf_func_proto bpf_csum_update_proto = {
	.func		= bpf_csum_update,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
};

BPF_CALL_2(bpf_csum_level, struct sk_buff *, skb, u64, level)
{
	/* The interface is to be used in combination with bpf_skb_adjust_room()
	 * for encap/decap of packet headers when BPF_F_ADJ_ROOM_NO_CSUM_RESET
	 * is passed as flags, for example.
	 */
	switch (level) {
	case BPF_CSUM_LEVEL_INC:
		__skb_incr_checksum_unnecessary(skb);
		break;
	case BPF_CSUM_LEVEL_DEC:
		__skb_decr_checksum_unnecessary(skb);
		break;
	case BPF_CSUM_LEVEL_RESET:
		__skb_reset_checksum_unnecessary(skb);
		break;
	case BPF_CSUM_LEVEL_QUERY:
		return skb->ip_summed == CHECKSUM_UNNECESSARY ?
		       skb->csum_level : -EACCES;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct bpf_func_proto bpf_csum_level_proto = {
	.func		= bpf_csum_level,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
};

static inline int __bpf_rx_skb(struct net_device *dev, struct sk_buff *skb)
{
	return dev_forward_skb(dev, skb);
}

static inline int __bpf_rx_skb_no_mac(struct net_device *dev,
				      struct sk_buff *skb)
{
	int ret = ____dev_forward_skb(dev, skb);

	if (likely(!ret)) {
		skb->dev = dev;
		ret = netif_rx(skb);
	}

	return ret;
}

static inline int __bpf_tx_skb(struct net_device *dev, struct sk_buff *skb)
{
	int ret;

	if (dev_xmit_recursion()) {
		net_crit_ratelimited("bpf: recursion limit reached on datapath, buggy bpf program?\n");
		kfree_skb(skb);
		return -ENETDOWN;
	}

	skb->dev = dev;
	skb->tstamp = 0;

	dev_xmit_recursion_inc();
	ret = dev_queue_xmit(skb);
	dev_xmit_recursion_dec();

	return ret;
}

static int __bpf_redirect_no_mac(struct sk_buff *skb, struct net_device *dev,
				 u32 flags)
{
	unsigned int mlen = skb_network_offset(skb);

	if (mlen) {
		__skb_pull(skb, mlen);

		/* At ingress, the mac header has already been pulled once.
		 * At egress, skb_pospull_rcsum has to be done in case that
		 * the skb is originated from ingress (i.e. a forwarded skb)
		 * to ensure that rcsum starts at net header.
		 */
		if (!skb_at_tc_ingress(skb))
			skb_postpull_rcsum(skb, skb_mac_header(skb), mlen);
	}
	skb_pop_mac_header(skb);
	skb_reset_mac_len(skb);
	return flags & BPF_F_INGRESS ?
	       __bpf_rx_skb_no_mac(dev, skb) : __bpf_tx_skb(dev, skb);
}

static int __bpf_redirect_common(struct sk_buff *skb, struct net_device *dev,
				 u32 flags)
{
	/* Verify that a link layer header is carried */
	if (unlikely(skb->mac_header >= skb->network_header)) {
		kfree_skb(skb);
		return -ERANGE;
	}

	bpf_push_mac_rcsum(skb);
	return flags & BPF_F_INGRESS ?
	       __bpf_rx_skb(dev, skb) : __bpf_tx_skb(dev, skb);
}

static int __bpf_redirect(struct sk_buff *skb, struct net_device *dev,
			  u32 flags)
{
	if (dev_is_mac_header_xmit(dev))
		return __bpf_redirect_common(skb, dev, flags);
	else
		return __bpf_redirect_no_mac(skb, dev, flags);
}

#if IS_ENABLED(CONFIG_IPV6)
static int bpf_out_neigh_v6(struct net *net, struct sk_buff *skb,
			    struct net_device *dev, struct bpf_nh_params *nh)
{
	u32 hh_len = LL_RESERVED_SPACE(dev);
	const struct in6_addr *nexthop;
	struct dst_entry *dst = NULL;
	struct neighbour *neigh;

	if (dev_xmit_recursion()) {
		net_crit_ratelimited("bpf: recursion limit reached on datapath, buggy bpf program?\n");
		goto out_drop;
	}

	skb->dev = dev;
	skb->tstamp = 0;

	if (unlikely(skb_headroom(skb) < hh_len && dev->header_ops)) {
		struct sk_buff *skb2;

		skb2 = skb_realloc_headroom(skb, hh_len);
		if (unlikely(!skb2)) {
			kfree_skb(skb);
			return -ENOMEM;
		}
		if (skb->sk)
			skb_set_owner_w(skb2, skb->sk);
		consume_skb(skb);
		skb = skb2;
	}

	rcu_read_lock_bh();
	if (!nh) {
		dst = skb_dst(skb);
		nexthop = rt6_nexthop(container_of(dst, struct rt6_info, dst),
				      &ipv6_hdr(skb)->daddr);
	} else {
		nexthop = &nh->ipv6_nh;
	}
	neigh = ip_neigh_gw6(dev, nexthop);
	if (likely(!IS_ERR(neigh))) {
		int ret;

		sock_confirm_neigh(skb, neigh);
		dev_xmit_recursion_inc();
		ret = neigh_output(neigh, skb, false);
		dev_xmit_recursion_dec();
		rcu_read_unlock_bh();
		return ret;
	}
	rcu_read_unlock_bh();
	if (dst)
		IP6_INC_STATS(dev_net(dst->dev),
			      ip6_dst_idev(dst), IPSTATS_MIB_OUTNOROUTES);
out_drop:
	kfree_skb(skb);
	return -ENETDOWN;
}

static int __bpf_redirect_neigh_v6(struct sk_buff *skb, struct net_device *dev,
				   struct bpf_nh_params *nh)
{
	const struct ipv6hdr *ip6h = ipv6_hdr(skb);
	struct net *net = dev_net(dev);
	int err, ret = NET_XMIT_DROP;

	if (!nh) {
		struct dst_entry *dst;
		struct flowi6 fl6 = {
			.flowi6_flags = FLOWI_FLAG_ANYSRC,
			.flowi6_mark  = skb->mark,
			.flowlabel    = ip6_flowinfo(ip6h),
			.flowi6_oif   = dev->ifindex,
			.flowi6_proto = ip6h->nexthdr,
			.daddr	      = ip6h->daddr,
			.saddr	      = ip6h->saddr,
		};

		dst = ipv6_stub->ipv6_dst_lookup_flow(net, NULL, &fl6, NULL);
		if (IS_ERR(dst))
			goto out_drop;

		skb_dst_set(skb, dst);
	} else if (nh->nh_family != AF_INET6) {
		goto out_drop;
	}

	err = bpf_out_neigh_v6(net, skb, dev, nh);
	if (unlikely(net_xmit_eval(err)))
		dev->stats.tx_errors++;
	else
		ret = NET_XMIT_SUCCESS;
	goto out_xmit;
out_drop:
	dev->stats.tx_errors++;
	kfree_skb(skb);
out_xmit:
	return ret;
}
#else
static int __bpf_redirect_neigh_v6(struct sk_buff *skb, struct net_device *dev,
				   struct bpf_nh_params *nh)
{
	kfree_skb(skb);
	return NET_XMIT_DROP;
}
#endif /* CONFIG_IPV6 */

#if IS_ENABLED(CONFIG_INET)
static int bpf_out_neigh_v4(struct net *net, struct sk_buff *skb,
			    struct net_device *dev, struct bpf_nh_params *nh)
{
	u32 hh_len = LL_RESERVED_SPACE(dev);
	struct neighbour *neigh;
	bool is_v6gw = false;

	if (dev_xmit_recursion()) {
		net_crit_ratelimited("bpf: recursion limit reached on datapath, buggy bpf program?\n");
		goto out_drop;
	}

	skb->dev = dev;
	skb->tstamp = 0;

	if (unlikely(skb_headroom(skb) < hh_len && dev->header_ops)) {
		struct sk_buff *skb2;

		skb2 = skb_realloc_headroom(skb, hh_len);
		if (unlikely(!skb2)) {
			kfree_skb(skb);
			return -ENOMEM;
		}
		if (skb->sk)
			skb_set_owner_w(skb2, skb->sk);
		consume_skb(skb);
		skb = skb2;
	}

	rcu_read_lock_bh();
	if (!nh) {
		struct dst_entry *dst = skb_dst(skb);
		struct rtable *rt = container_of(dst, struct rtable, dst);

		neigh = ip_neigh_for_gw(rt, skb, &is_v6gw);
	} else if (nh->nh_family == AF_INET6) {
		neigh = ip_neigh_gw6(dev, &nh->ipv6_nh);
		is_v6gw = true;
	} else if (nh->nh_family == AF_INET) {
		neigh = ip_neigh_gw4(dev, nh->ipv4_nh);
	} else {
		rcu_read_unlock_bh();
		goto out_drop;
	}

	if (likely(!IS_ERR(neigh))) {
		int ret;

		sock_confirm_neigh(skb, neigh);
		dev_xmit_recursion_inc();
		ret = neigh_output(neigh, skb, is_v6gw);
		dev_xmit_recursion_dec();
		rcu_read_unlock_bh();
		return ret;
	}
	rcu_read_unlock_bh();
out_drop:
	kfree_skb(skb);
	return -ENETDOWN;
}

static int __bpf_redirect_neigh_v4(struct sk_buff *skb, struct net_device *dev,
				   struct bpf_nh_params *nh)
{
	const struct iphdr *ip4h = ip_hdr(skb);
	struct net *net = dev_net(dev);
	int err, ret = NET_XMIT_DROP;

	if (!nh) {
		struct flowi4 fl4 = {
			.flowi4_flags = FLOWI_FLAG_ANYSRC,
			.flowi4_mark  = skb->mark,
			.flowi4_tos   = RT_TOS(ip4h->tos),
			.flowi4_oif   = dev->ifindex,
			.flowi4_proto = ip4h->protocol,
			.daddr	      = ip4h->daddr,
			.saddr	      = ip4h->saddr,
		};
		struct rtable *rt;

		rt = ip_route_output_flow(net, &fl4, NULL);
		if (IS_ERR(rt))
			goto out_drop;
		if (rt->rt_type != RTN_UNICAST && rt->rt_type != RTN_LOCAL) {
			ip_rt_put(rt);
			goto out_drop;
		}

		skb_dst_set(skb, &rt->dst);
	}

	err = bpf_out_neigh_v4(net, skb, dev, nh);
	if (unlikely(net_xmit_eval(err)))
		dev->stats.tx_errors++;
	else
		ret = NET_XMIT_SUCCESS;
	goto out_xmit;
out_drop:
	dev->stats.tx_errors++;
	kfree_skb(skb);
out_xmit:
	return ret;
}
#else
static int __bpf_redirect_neigh_v4(struct sk_buff *skb, struct net_device *dev,
				   struct bpf_nh_params *nh)
{
	kfree_skb(skb);
	return NET_XMIT_DROP;
}
#endif /* CONFIG_INET */

static int __bpf_redirect_neigh(struct sk_buff *skb, struct net_device *dev,
				struct bpf_nh_params *nh)
{
	struct ethhdr *ethh = eth_hdr(skb);

	if (unlikely(skb->mac_header >= skb->network_header))
		goto out;
	bpf_push_mac_rcsum(skb);
	if (is_multicast_ether_addr(ethh->h_dest))
		goto out;

	skb_pull(skb, sizeof(*ethh));
	skb_unset_mac_header(skb);
	skb_reset_network_header(skb);

	if (skb->protocol == htons(ETH_P_IP))
		return __bpf_redirect_neigh_v4(skb, dev, nh);
	else if (skb->protocol == htons(ETH_P_IPV6))
		return __bpf_redirect_neigh_v6(skb, dev, nh);
out:
	kfree_skb(skb);
	return -ENOTSUPP;
}

/* Internal, non-exposed redirect flags. */
enum {
	BPF_F_NEIGH	= (1ULL << 1),
	BPF_F_PEER	= (1ULL << 2),
	BPF_F_NEXTHOP	= (1ULL << 3),
#define BPF_F_REDIRECT_INTERNAL	(BPF_F_NEIGH | BPF_F_PEER | BPF_F_NEXTHOP)
};

BPF_CALL_3(bpf_clone_redirect, struct sk_buff *, skb, u32, ifindex, u64, flags)
{
	struct net_device *dev;
	struct sk_buff *clone;
	int ret;

	if (unlikely(flags & (~(BPF_F_INGRESS) | BPF_F_REDIRECT_INTERNAL)))
		return -EINVAL;

	dev = dev_get_by_index_rcu(dev_net(skb->dev), ifindex);
	if (unlikely(!dev))
		return -EINVAL;

	clone = skb_clone(skb, GFP_ATOMIC);
	if (unlikely(!clone))
		return -ENOMEM;

	/* For direct write, we need to keep the invariant that the skbs
	 * we're dealing with need to be uncloned. Should uncloning fail
	 * here, we need to free the just generated clone to unclone once
	 * again.
	 */
	ret = bpf_try_make_head_writable(skb);
	if (unlikely(ret)) {
		kfree_skb(clone);
		return -ENOMEM;
	}

	return __bpf_redirect(clone, dev, flags);
}

static const struct bpf_func_proto bpf_clone_redirect_proto = {
	.func           = bpf_clone_redirect,
	.gpl_only       = false,
	.ret_type       = RET_INTEGER,
	.arg1_type      = ARG_PTR_TO_CTX,
	.arg2_type      = ARG_ANYTHING,
	.arg3_type      = ARG_ANYTHING,
};

DEFINE_PER_CPU(struct bpf_redirect_info, bpf_redirect_info);
EXPORT_PER_CPU_SYMBOL_GPL(bpf_redirect_info);

int skb_do_redirect(struct sk_buff *skb)
{
	struct bpf_redirect_info *ri = this_cpu_ptr(&bpf_redirect_info);
	struct net *net = dev_net(skb->dev);
	struct net_device *dev;
	u32 flags = ri->flags;

	dev = dev_get_by_index_rcu(net, ri->tgt_index);
	ri->tgt_index = 0;
	ri->flags = 0;
	if (unlikely(!dev))
		goto out_drop;
	if (flags & BPF_F_PEER) {
		const struct net_device_ops *ops = dev->netdev_ops;

		if (unlikely(!ops->ndo_get_peer_dev ||
			     !skb_at_tc_ingress(skb)))
			goto out_drop;
		dev = ops->ndo_get_peer_dev(dev);
		if (unlikely(!dev ||
			     !is_skb_forwardable(dev, skb) ||
			     net_eq(net, dev_net(dev))))
			goto out_drop;
		skb->dev = dev;
		return -EAGAIN;
	}
	return flags & BPF_F_NEIGH ?
	       __bpf_redirect_neigh(skb, dev, flags & BPF_F_NEXTHOP ?
				    &ri->nh : NULL) :
	       __bpf_redirect(skb, dev, flags);
out_drop:
	kfree_skb(skb);
	return -EINVAL;
}

BPF_CALL_2(bpf_redirect, u32, ifindex, u64, flags)
{
	struct bpf_redirect_info *ri = this_cpu_ptr(&bpf_redirect_info);

	if (unlikely(flags & (~(BPF_F_INGRESS) | BPF_F_REDIRECT_INTERNAL)))
		return TC_ACT_SHOT;

	ri->flags = flags;
	ri->tgt_index = ifindex;

	return TC_ACT_REDIRECT;
}

static const struct bpf_func_proto bpf_redirect_proto = {
	.func           = bpf_redirect,
	.gpl_only       = false,
	.ret_type       = RET_INTEGER,
	.arg1_type      = ARG_ANYTHING,
	.arg2_type      = ARG_ANYTHING,
};

BPF_CALL_2(bpf_redirect_peer, u32, ifindex, u64, flags)
{
	struct bpf_redirect_info *ri = this_cpu_ptr(&bpf_redirect_info);

	if (unlikely(flags))
		return TC_ACT_SHOT;

	ri->flags = BPF_F_PEER;
	ri->tgt_index = ifindex;

	return TC_ACT_REDIRECT;
}

static const struct bpf_func_proto bpf_redirect_peer_proto = {
	.func           = bpf_redirect_peer,
	.gpl_only       = false,
	.ret_type       = RET_INTEGER,
	.arg1_type      = ARG_ANYTHING,
	.arg2_type      = ARG_ANYTHING,
};

BPF_CALL_4(bpf_redirect_neigh, u32, ifindex, struct bpf_redir_neigh *, params,
	   int, plen, u64, flags)
{
	struct bpf_redirect_info *ri = this_cpu_ptr(&bpf_redirect_info);

	if (unlikely((plen && plen < sizeof(*params)) || flags))
		return TC_ACT_SHOT;

	ri->flags = BPF_F_NEIGH | (plen ? BPF_F_NEXTHOP : 0);
	ri->tgt_index = ifindex;

	BUILD_BUG_ON(sizeof(struct bpf_redir_neigh) != sizeof(struct bpf_nh_params));
	if (plen)
		memcpy(&ri->nh, params, sizeof(ri->nh));

	return TC_ACT_REDIRECT;
}

static const struct bpf_func_proto bpf_redirect_neigh_proto = {
	.func		= bpf_redirect_neigh,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_ANYTHING,
	.arg2_type      = ARG_PTR_TO_MEM_OR_NULL,
	.arg3_type      = ARG_CONST_SIZE_OR_ZERO,
	.arg4_type	= ARG_ANYTHING,
};

BPF_CALL_2(bpf_msg_apply_bytes, struct sk_msg *, msg, u32, bytes)
{
	msg->apply_bytes = bytes;
	return 0;
}

static const struct bpf_func_proto bpf_msg_apply_bytes_proto = {
	.func           = bpf_msg_apply_bytes,
	.gpl_only       = false,
	.ret_type       = RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type      = ARG_ANYTHING,
};

BPF_CALL_2(bpf_msg_cork_bytes, struct sk_msg *, msg, u32, bytes)
{
	msg->cork_bytes = bytes;
	return 0;
}

static const struct bpf_func_proto bpf_msg_cork_bytes_proto = {
	.func           = bpf_msg_cork_bytes,
	.gpl_only       = false,
	.ret_type       = RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type      = ARG_ANYTHING,
};

BPF_CALL_4(bpf_msg_pull_data, struct sk_msg *, msg, u32, start,
	   u32, end, u64, flags)
{
	u32 len = 0, offset = 0, copy = 0, poffset = 0, bytes = end - start;
	u32 first_sge, last_sge, i, shift, bytes_sg_total;
	struct scatterlist *sge;
	u8 *raw, *to, *from;
	struct page *page;

	if (unlikely(flags || end <= start))
		return -EINVAL;

	/* First find the starting scatterlist element */
	i = msg->sg.start;
	do {
		offset += len;
		len = sk_msg_elem(msg, i)->length;
		if (start < offset + len)
			break;
		sk_msg_iter_var_next(i);
	} while (i != msg->sg.end);

	if (unlikely(start >= offset + len))
		return -EINVAL;

	first_sge = i;
	/* The start may point into the sg element so we need to also
	 * account for the headroom.
	 */
	bytes_sg_total = start - offset + bytes;
	if (!test_bit(i, &msg->sg.copy) && bytes_sg_total <= len)
		goto out;

	/* At this point we need to linearize multiple scatterlist
	 * elements or a single shared page. Either way we need to
	 * copy into a linear buffer exclusively owned by BPF. Then
	 * place the buffer in the scatterlist and fixup the original
	 * entries by removing the entries now in the linear buffer
	 * and shifting the remaining entries. For now we do not try
	 * to copy partial entries to avoid complexity of running out
	 * of sg_entry slots. The downside is reading a single byte
	 * will copy the entire sg entry.
	 */
	do {
		copy += sk_msg_elem(msg, i)->length;
		sk_msg_iter_var_next(i);
		if (bytes_sg_total <= copy)
			break;
	} while (i != msg->sg.end);
	last_sge = i;

	if (unlikely(bytes_sg_total > copy))
		return -EINVAL;

	page = alloc_pages(__GFP_NOWARN | GFP_ATOMIC | __GFP_COMP,
			   get_order(copy));
	if (unlikely(!page))
		return -ENOMEM;

	raw = page_address(page);
	i = first_sge;
	do {
		sge = sk_msg_elem(msg, i);
		from = sg_virt(sge);
		len = sge->length;
		to = raw + poffset;

		memcpy(to, from, len);
		poffset += len;
		sge->length = 0;
		put_page(sg_page(sge));

		sk_msg_iter_var_next(i);
	} while (i != last_sge);

	sg_set_page(&msg->sg.data[first_sge], page, copy, 0);

	/* To repair sg ring we need to shift entries. If we only
	 * had a single entry though we can just replace it and
	 * be done. Otherwise walk the ring and shift the entries.
	 */
	WARN_ON_ONCE(last_sge == first_sge);
	shift = last_sge > first_sge ?
		last_sge - first_sge - 1 :
		NR_MSG_FRAG_IDS - first_sge + last_sge - 1;
	if (!shift)
		goto out;

	i = first_sge;
	sk_msg_iter_var_next(i);
	do {
		u32 move_from;

		if (i + shift >= NR_MSG_FRAG_IDS)
			move_from = i + shift - NR_MSG_FRAG_IDS;
		else
			move_from = i + shift;
		if (move_from == msg->sg.end)
			break;

		msg->sg.data[i] = msg->sg.data[move_from];
		msg->sg.data[move_from].length = 0;
		msg->sg.data[move_from].page_link = 0;
		msg->sg.data[move_from].offset = 0;
		sk_msg_iter_var_next(i);
	} while (1);

	msg->sg.end = msg->sg.end - shift > msg->sg.end ?
		      msg->sg.end - shift + NR_MSG_FRAG_IDS :
		      msg->sg.end - shift;
out:
	msg->data = sg_virt(&msg->sg.data[first_sge]) + start - offset;
	msg->data_end = msg->data + bytes;
	return 0;
}

static const struct bpf_func_proto bpf_msg_pull_data_proto = {
	.func		= bpf_msg_pull_data,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
	.arg3_type	= ARG_ANYTHING,
	.arg4_type	= ARG_ANYTHING,
};

BPF_CALL_4(bpf_msg_push_data, struct sk_msg *, msg, u32, start,
	   u32, len, u64, flags)
{
	struct scatterlist sge, nsge, nnsge, rsge = {0}, *psge;
	u32 new, i = 0, l = 0, space, copy = 0, offset = 0;
	u8 *raw, *to, *from;
	struct page *page;

	if (unlikely(flags))
		return -EINVAL;

	if (unlikely(len == 0))
		return 0;

	/* First find the starting scatterlist element */
	i = msg->sg.start;
	do {
		offset += l;
		l = sk_msg_elem(msg, i)->length;

		if (start < offset + l)
			break;
		sk_msg_iter_var_next(i);
	} while (i != msg->sg.end);

	if (start >= offset + l)
		return -EINVAL;

	space = MAX_MSG_FRAGS - sk_msg_elem_used(msg);

	/* If no space available will fallback to copy, we need at
	 * least one scatterlist elem available to push data into
	 * when start aligns to the beginning of an element or two
	 * when it falls inside an element. We handle the start equals
	 * offset case because its the common case for inserting a
	 * header.
	 */
	if (!space || (space == 1 && start != offset))
		copy = msg->sg.data[i].length;

	page = alloc_pages(__GFP_NOWARN | GFP_ATOMIC | __GFP_COMP,
			   get_order(copy + len));
	if (unlikely(!page))
		return -ENOMEM;

	if (copy) {
		int front, back;

		raw = page_address(page);

		psge = sk_msg_elem(msg, i);
		front = start - offset;
		back = psge->length - front;
		from = sg_virt(psge);

		if (front)
			memcpy(raw, from, front);

		if (back) {
			from += front;
			to = raw + front + len;

			memcpy(to, from, back);
		}

		put_page(sg_page(psge));
	} else if (start - offset) {
		psge = sk_msg_elem(msg, i);
		rsge = sk_msg_elem_cpy(msg, i);

		psge->length = start - offset;
		rsge.length -= psge->length;
		rsge.offset += start;

		sk_msg_iter_var_next(i);
		sg_unmark_end(psge);
		sg_unmark_end(&rsge);
		sk_msg_iter_next(msg, end);
	}

	/* Slot(s) to place newly allocated data */
	new = i;

	/* Shift one or two slots as needed */
	if (!copy) {
		sge = sk_msg_elem_cpy(msg, i);

		sk_msg_iter_var_next(i);
		sg_unmark_end(&sge);
		sk_msg_iter_next(msg, end);

		nsge = sk_msg_elem_cpy(msg, i);
		if (rsge.length) {
			sk_msg_iter_var_next(i);
			nnsge = sk_msg_elem_cpy(msg, i);
		}

		while (i != msg->sg.end) {
			msg->sg.data[i] = sge;
			sge = nsge;
			sk_msg_iter_var_next(i);
			if (rsge.length) {
				nsge = nnsge;
				nnsge = sk_msg_elem_cpy(msg, i);
			} else {
				nsge = sk_msg_elem_cpy(msg, i);
			}
		}
	}

	/* Place newly allocated data buffer */
	sk_mem_charge(msg->sk, len);
	msg->sg.size += len;
	__clear_bit(new, &msg->sg.copy);
	sg_set_page(&msg->sg.data[new], page, len + copy, 0);
	if (rsge.length) {
		get_page(sg_page(&rsge));
		sk_msg_iter_var_next(new);
		msg->sg.data[new] = rsge;
	}

	sk_msg_compute_data_pointers(msg);
	return 0;
}

static const struct bpf_func_proto bpf_msg_push_data_proto = {
	.func		= bpf_msg_push_data,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
	.arg3_type	= ARG_ANYTHING,
	.arg4_type	= ARG_ANYTHING,
};

static void sk_msg_shift_left(struct sk_msg *msg, int i)
{
	int prev;

	do {
		prev = i;
		sk_msg_iter_var_next(i);
		msg->sg.data[prev] = msg->sg.data[i];
	} while (i != msg->sg.end);

	sk_msg_iter_prev(msg, end);
}

static void sk_msg_shift_right(struct sk_msg *msg, int i)
{
	struct scatterlist tmp, sge;

	sk_msg_iter_next(msg, end);
	sge = sk_msg_elem_cpy(msg, i);
	sk_msg_iter_var_next(i);
	tmp = sk_msg_elem_cpy(msg, i);

	while (i != msg->sg.end) {
		msg->sg.data[i] = sge;
		sk_msg_iter_var_next(i);
		sge = tmp;
		tmp = sk_msg_elem_cpy(msg, i);
	}
}

BPF_CALL_4(bpf_msg_pop_data, struct sk_msg *, msg, u32, start,
	   u32, len, u64, flags)
{
	u32 i = 0, l = 0, space, offset = 0;
	u64 last = start + len;
	int pop;

	if (unlikely(flags))
		return -EINVAL;

	/* First find the starting scatterlist element */
	i = msg->sg.start;
	do {
		offset += l;
		l = sk_msg_elem(msg, i)->length;

		if (start < offset + l)
			break;
		sk_msg_iter_var_next(i);
	} while (i != msg->sg.end);

	/* Bounds checks: start and pop must be inside message */
	if (start >= offset + l || last >= msg->sg.size)
		return -EINVAL;

	space = MAX_MSG_FRAGS - sk_msg_elem_used(msg);

	pop = len;
	/* --------------| offset
	 * -| start      |-------- len -------|
	 *
	 *  |----- a ----|-------- pop -------|----- b ----|
	 *  |______________________________________________| length
	 *
	 *
	 * a:   region at front of scatter element to save
	 * b:   region at back of scatter element to save when length > A + pop
	 * pop: region to pop from element, same as input 'pop' here will be
	 *      decremented below per iteration.
	 *
	 * Two top-level cases to handle when start != offset, first B is non
	 * zero and second B is zero corresponding to when a pop includes more
	 * than one element.
	 *
	 * Then if B is non-zero AND there is no space allocate space and
	 * compact A, B regions into page. If there is space shift ring to
	 * the rigth free'ing the next element in ring to place B, leaving
	 * A untouched except to reduce length.
	 */
	if (start != offset) {
		struct scatterlist *nsge, *sge = sk_msg_elem(msg, i);
		int a = start;
		int b = sge->length - pop - a;

		sk_msg_iter_var_next(i);

		if (pop < sge->length - a) {
			if (space) {
				sge->length = a;
				sk_msg_shift_right(msg, i);
				nsge = sk_msg_elem(msg, i);
				get_page(sg_page(sge));
				sg_set_page(nsge,
					    sg_page(sge),
					    b, sge->offset + pop + a);
			} else {
				struct page *page, *orig;
				u8 *to, *from;

				page = alloc_pages(__GFP_NOWARN |
						   __GFP_COMP   | GFP_ATOMIC,
						   get_order(a + b));
				if (unlikely(!page))
					return -ENOMEM;

				sge->length = a;
				orig = sg_page(sge);
				from = sg_virt(sge);
				to = page_address(page);
				memcpy(to, from, a);
				memcpy(to + a, from + a + pop, b);
				sg_set_page(sge, page, a + b, 0);
				put_page(orig);
			}
			pop = 0;
		} else if (pop >= sge->length - a) {
			pop -= (sge->length - a);
			sge->length = a;
		}
	}

	/* From above the current layout _must_ be as follows,
	 *
	 * -| offset
	 * -| start
	 *
	 *  |---- pop ---|---------------- b ------------|
	 *  |____________________________________________| length
	 *
	 * Offset and start of the current msg elem are equal because in the
	 * previous case we handled offset != start and either consumed the
	 * entire element and advanced to the next element OR pop == 0.
	 *
	 * Two cases to handle here are first pop is less than the length
	 * leaving some remainder b above. Simply adjust the element's layout
	 * in this case. Or pop >= length of the element so that b = 0. In this
	 * case advance to next element decrementing pop.
	 */
	while (pop) {
		struct scatterlist *sge = sk_msg_elem(msg, i);

		if (pop < sge->length) {
			sge->length -= pop;
			sge->offset += pop;
			pop = 0;
		} else {
			pop -= sge->length;
			sk_msg_shift_left(msg, i);
		}
		sk_msg_iter_var_next(i);
	}

	sk_mem_uncharge(msg->sk, len - pop);
	msg->sg.size -= (len - pop);
	sk_msg_compute_data_pointers(msg);
	return 0;
}

static const struct bpf_func_proto bpf_msg_pop_data_proto = {
	.func		= bpf_msg_pop_data,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
	.arg3_type	= ARG_ANYTHING,
	.arg4_type	= ARG_ANYTHING,
};

#ifdef CONFIG_CGROUP_NET_CLASSID
BPF_CALL_0(bpf_get_cgroup_classid_curr)
{
	return __task_get_classid(current);
}

static const struct bpf_func_proto bpf_get_cgroup_classid_curr_proto = {
	.func		= bpf_get_cgroup_classid_curr,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
};

BPF_CALL_1(bpf_skb_cgroup_classid, const struct sk_buff *, skb)
{
	struct sock *sk = skb_to_full_sk(skb);

	if (!sk || !sk_fullsock(sk))
		return 0;

	return sock_cgroup_classid(&sk->sk_cgrp_data);
}

static const struct bpf_func_proto bpf_skb_cgroup_classid_proto = {
	.func		= bpf_skb_cgroup_classid,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
};
#endif

BPF_CALL_1(bpf_get_cgroup_classid, const struct sk_buff *, skb)
{
	return task_get_classid(skb);
}

static const struct bpf_func_proto bpf_get_cgroup_classid_proto = {
	.func           = bpf_get_cgroup_classid,
	.gpl_only       = false,
	.ret_type       = RET_INTEGER,
	.arg1_type      = ARG_PTR_TO_CTX,
};

BPF_CALL_1(bpf_get_route_realm, const struct sk_buff *, skb)
{
	return dst_tclassid(skb);
}

static const struct bpf_func_proto bpf_get_route_realm_proto = {
	.func           = bpf_get_route_realm,
	.gpl_only       = false,
	.ret_type       = RET_INTEGER,
	.arg1_type      = ARG_PTR_TO_CTX,
};

BPF_CALL_1(bpf_get_hash_recalc, struct sk_buff *, skb)
{
	/* If skb_clear_hash() was called due to mangling, we can
	 * trigger SW recalculation here. Later access to hash
	 * can then use the inline skb->hash via context directly
	 * instead of calling this helper again.
	 */
	return skb_get_hash(skb);
}

static const struct bpf_func_proto bpf_get_hash_recalc_proto = {
	.func		= bpf_get_hash_recalc,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
};

BPF_CALL_1(bpf_set_hash_invalid, struct sk_buff *, skb)
{
	/* After all direct packet write, this can be used once for
	 * triggering a lazy recalc on next skb_get_hash() invocation.
	 */
	skb_clear_hash(skb);
	return 0;
}

static const struct bpf_func_proto bpf_set_hash_invalid_proto = {
	.func		= bpf_set_hash_invalid,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
};

BPF_CALL_2(bpf_set_hash, struct sk_buff *, skb, u32, hash)
{
	/* Set user specified hash as L4(+), so that it gets returned
	 * on skb_get_hash() call unless BPF prog later on triggers a
	 * skb_clear_hash().
	 */
	__skb_set_sw_hash(skb, hash, true);
	return 0;
}

static const struct bpf_func_proto bpf_set_hash_proto = {
	.func		= bpf_set_hash,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
};

BPF_CALL_3(bpf_skb_vlan_push, struct sk_buff *, skb, __be16, vlan_proto,
	   u16, vlan_tci)
{
	int ret;

	if (unlikely(vlan_proto != htons(ETH_P_8021Q) &&
		     vlan_proto != htons(ETH_P_8021AD)))
		vlan_proto = htons(ETH_P_8021Q);

	bpf_push_mac_rcsum(skb);
	ret = skb_vlan_push(skb, vlan_proto, vlan_tci);
	bpf_pull_mac_rcsum(skb);

	bpf_compute_data_pointers(skb);
	return ret;
}

static const struct bpf_func_proto bpf_skb_vlan_push_proto = {
	.func           = bpf_skb_vlan_push,
	.gpl_only       = false,
	.ret_type       = RET_INTEGER,
	.arg1_type      = ARG_PTR_TO_CTX,
	.arg2_type      = ARG_ANYTHING,
	.arg3_type      = ARG_ANYTHING,
};

BPF_CALL_1(bpf_skb_vlan_pop, struct sk_buff *, skb)
{
	int ret;

	bpf_push_mac_rcsum(skb);
	ret = skb_vlan_pop(skb);
	bpf_pull_mac_rcsum(skb);

	bpf_compute_data_pointers(skb);
	return ret;
}

static const struct bpf_func_proto bpf_skb_vlan_pop_proto = {
	.func           = bpf_skb_vlan_pop,
	.gpl_only       = false,
	.ret_type       = RET_INTEGER,
	.arg1_type      = ARG_PTR_TO_CTX,
};

static int bpf_skb_generic_push(struct sk_buff *skb, u32 off, u32 len)
{
	/* Caller already did skb_cow() with len as headroom,
	 * so no need to do it here.
	 */
	skb_push(skb, len);
	memmove(skb->data, skb->data + len, off);
	memset(skb->data + off, 0, len);

	/* No skb_postpush_rcsum(skb, skb->data + off, len)
	 * needed here as it does not change the skb->csum
	 * result for checksum complete when summing over
	 * zeroed blocks.
	 */
	return 0;
}

static int bpf_skb_generic_pop(struct sk_buff *skb, u32 off, u32 len)
{
	/* skb_ensure_writable() is not needed here, as we're
	 * already working on an uncloned skb.
	 */
	if (unlikely(!pskb_may_pull(skb, off + len)))
		return -ENOMEM;

	skb_postpull_rcsum(skb, skb->data + off, len);
	memmove(skb->data + len, skb->data, off);
	__skb_pull(skb, len);

	return 0;
}

static int bpf_skb_net_hdr_push(struct sk_buff *skb, u32 off, u32 len)
{
	bool trans_same = skb->transport_header == skb->network_header;
	int ret;

	/* There's no need for __skb_push()/__skb_pull() pair to
	 * get to the start of the mac header as we're guaranteed
	 * to always start from here under eBPF.
	 */
	ret = bpf_skb_generic_push(skb, off, len);
	if (likely(!ret)) {
		skb->mac_header -= len;
		skb->network_header -= len;
		if (trans_same)
			skb->transport_header = skb->network_header;
	}

	return ret;
}

static int bpf_skb_net_hdr_pop(struct sk_buff *skb, u32 off, u32 len)
{
	bool trans_same = skb->transport_header == skb->network_header;
	int ret;

	/* Same here, __skb_push()/__skb_pull() pair not needed. */
	ret = bpf_skb_generic_pop(skb, off, len);
	if (likely(!ret)) {
		skb->mac_header += len;
		skb->network_header += len;
		if (trans_same)
			skb->transport_header = skb->network_header;
	}

	return ret;
}

static int bpf_skb_proto_4_to_6(struct sk_buff *skb)
{
	const u32 len_diff = sizeof(struct ipv6hdr) - sizeof(struct iphdr);
	u32 off = skb_mac_header_len(skb);
	int ret;

	ret = skb_cow(skb, len_diff);
	if (unlikely(ret < 0))
		return ret;

	ret = bpf_skb_net_hdr_push(skb, off, len_diff);
	if (unlikely(ret < 0))
		return ret;

	if (skb_is_gso(skb)) {
		struct skb_shared_info *shinfo = skb_shinfo(skb);

		/* SKB_GSO_TCPV4 needs to be changed into SKB_GSO_TCPV6. */
		if (shinfo->gso_type & SKB_GSO_TCPV4) {
			shinfo->gso_type &= ~SKB_GSO_TCPV4;
			shinfo->gso_type |=  SKB_GSO_TCPV6;
		}
	}

	skb->protocol = htons(ETH_P_IPV6);
	skb_clear_hash(skb);

	return 0;
}

static int bpf_skb_proto_6_to_4(struct sk_buff *skb)
{
	const u32 len_diff = sizeof(struct ipv6hdr) - sizeof(struct iphdr);
	u32 off = skb_mac_header_len(skb);
	int ret;

	ret = skb_unclone(skb, GFP_ATOMIC);
	if (unlikely(ret < 0))
		return ret;

	ret = bpf_skb_net_hdr_pop(skb, off, len_diff);
	if (unlikely(ret < 0))
		return ret;

	if (skb_is_gso(skb)) {
		struct skb_shared_info *shinfo = skb_shinfo(skb);

		/* SKB_GSO_TCPV6 needs to be changed into SKB_GSO_TCPV4. */
		if (shinfo->gso_type & SKB_GSO_TCPV6) {
			shinfo->gso_type &= ~SKB_GSO_TCPV6;
			shinfo->gso_type |=  SKB_GSO_TCPV4;
		}
	}

	skb->protocol = htons(ETH_P_IP);
	skb_clear_hash(skb);

	return 0;
}

static int bpf_skb_proto_xlat(struct sk_buff *skb, __be16 to_proto)
{
	__be16 from_proto = skb->protocol;

	if (from_proto == htons(ETH_P_IP) &&
	      to_proto == htons(ETH_P_IPV6))
		return bpf_skb_proto_4_to_6(skb);

	if (from_proto == htons(ETH_P_IPV6) &&
	      to_proto == htons(ETH_P_IP))
		return bpf_skb_proto_6_to_4(skb);

	return -ENOTSUPP;
}

BPF_CALL_3(bpf_skb_change_proto, struct sk_buff *, skb, __be16, proto,
	   u64, flags)
{
	int ret;

	if (unlikely(flags))
		return -EINVAL;

	/* General idea is that this helper does the basic groundwork
	 * needed for changing the protocol, and eBPF program fills the
	 * rest through bpf_skb_store_bytes(), bpf_lX_csum_replace()
	 * and other helpers, rather than passing a raw buffer here.
	 *
	 * The rationale is to keep this minimal and without a need to
	 * deal with raw packet data. F.e. even if we would pass buffers
	 * here, the program still needs to call the bpf_lX_csum_replace()
	 * helpers anyway. Plus, this way we keep also separation of
	 * concerns, since f.e. bpf_skb_store_bytes() should only take
	 * care of stores.
	 *
	 * Currently, additional options and extension header space are
	 * not supported, but flags register is reserved so we can adapt
	 * that. For offloads, we mark packet as dodgy, so that headers
	 * need to be verified first.
	 */
	ret = bpf_skb_proto_xlat(skb, proto);
	bpf_compute_data_pointers(skb);
	return ret;
}

static const struct bpf_func_proto bpf_skb_change_proto_proto = {
	.func		= bpf_skb_change_proto,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
	.arg3_type	= ARG_ANYTHING,
};

BPF_CALL_2(bpf_skb_change_type, struct sk_buff *, skb, u32, pkt_type)
{
	/* We only allow a restricted subset to be changed for now. */
	if (unlikely(!skb_pkt_type_ok(skb->pkt_type) ||
		     !skb_pkt_type_ok(pkt_type)))
		return -EINVAL;

	skb->pkt_type = pkt_type;
	return 0;
}

static const struct bpf_func_proto bpf_skb_change_type_proto = {
	.func		= bpf_skb_change_type,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
};

static u32 bpf_skb_net_base_len(const struct sk_buff *skb)
{
	switch (skb->protocol) {
	case htons(ETH_P_IP):
		return sizeof(struct iphdr);
	case htons(ETH_P_IPV6):
		return sizeof(struct ipv6hdr);
	default:
		return ~0U;
	}
}

#define BPF_F_ADJ_ROOM_ENCAP_L3_MASK	(BPF_F_ADJ_ROOM_ENCAP_L3_IPV4 | \
					 BPF_F_ADJ_ROOM_ENCAP_L3_IPV6)

#define BPF_F_ADJ_ROOM_MASK		(BPF_F_ADJ_ROOM_FIXED_GSO | \
					 BPF_F_ADJ_ROOM_ENCAP_L3_MASK | \
					 BPF_F_ADJ_ROOM_ENCAP_L4_GRE | \
					 BPF_F_ADJ_ROOM_ENCAP_L4_UDP | \
					 BPF_F_ADJ_ROOM_ENCAP_L2( \
					  BPF_ADJ_ROOM_ENCAP_L2_MASK))

static int bpf_skb_net_grow(struct sk_buff *skb, u32 off, u32 len_diff,
			    u64 flags)
{
	u8 inner_mac_len = flags >> BPF_ADJ_ROOM_ENCAP_L2_SHIFT;
	bool encap = flags & BPF_F_ADJ_ROOM_ENCAP_L3_MASK;
	u16 mac_len = 0, inner_net = 0, inner_trans = 0;
	unsigned int gso_type = SKB_GSO_DODGY;
	int ret;

	if (skb_is_gso(skb) && !skb_is_gso_tcp(skb)) {
		/* udp gso_size delineates datagrams, only allow if fixed */
		if (!(skb_shinfo(skb)->gso_type & SKB_GSO_UDP_L4) ||
		    !(flags & BPF_F_ADJ_ROOM_FIXED_GSO))
			return -ENOTSUPP;
	}

	ret = skb_cow_head(skb, len_diff);
	if (unlikely(ret < 0))
		return ret;

	if (encap) {
		if (skb->protocol != htons(ETH_P_IP) &&
		    skb->protocol != htons(ETH_P_IPV6))
			return -ENOTSUPP;

		if (flags & BPF_F_ADJ_ROOM_ENCAP_L3_IPV4 &&
		    flags & BPF_F_ADJ_ROOM_ENCAP_L3_IPV6)
			return -EINVAL;

		if (flags & BPF_F_ADJ_ROOM_ENCAP_L4_GRE &&
		    flags & BPF_F_ADJ_ROOM_ENCAP_L4_UDP)
			return -EINVAL;

		if (skb->encapsulation)
			return -EALREADY;

		mac_len = skb->network_header - skb->mac_header;
		inner_net = skb->network_header;
		if (inner_mac_len > len_diff)
			return -EINVAL;
		inner_trans = skb->transport_header;
	}

	ret = bpf_skb_net_hdr_push(skb, off, len_diff);
	if (unlikely(ret < 0))
		return ret;

	if (encap) {
		skb->inner_mac_header = inner_net - inner_mac_len;
		skb->inner_network_header = inner_net;
		skb->inner_transport_header = inner_trans;
		skb_set_inner_protocol(skb, skb->protocol);

		skb->encapsulation = 1;
		skb_set_network_header(skb, mac_len);

		if (flags & BPF_F_ADJ_ROOM_ENCAP_L4_UDP)
			gso_type |= SKB_GSO_UDP_TUNNEL;
		else if (flags & BPF_F_ADJ_ROOM_ENCAP_L4_GRE)
			gso_type |= SKB_GSO_GRE;
		else if (flags & BPF_F_ADJ_ROOM_ENCAP_L3_IPV6)
			gso_type |= SKB_GSO_IPXIP6;
		else if (flags & BPF_F_ADJ_ROOM_ENCAP_L3_IPV4)
			gso_type |= SKB_GSO_IPXIP4;

		if (flags & BPF_F_ADJ_ROOM_ENCAP_L4_GRE ||
		    flags & BPF_F_ADJ_ROOM_ENCAP_L4_UDP) {
			int nh_len = flags & BPF_F_ADJ_ROOM_ENCAP_L3_IPV6 ?
					sizeof(struct ipv6hdr) :
					sizeof(struct iphdr);

			skb_set_transport_header(skb, mac_len + nh_len);
		}

		/* Match skb->protocol to new outer l3 protocol */
		if (skb->protocol == htons(ETH_P_IP) &&
		    flags & BPF_F_ADJ_ROOM_ENCAP_L3_IPV6)
			skb->protocol = htons(ETH_P_IPV6);
		else if (skb->protocol == htons(ETH_P_IPV6) &&
			 flags & BPF_F_ADJ_ROOM_ENCAP_L3_IPV4)
			skb->protocol = htons(ETH_P_IP);
	}

	if (skb_is_gso(skb)) {
		struct skb_shared_info *shinfo = skb_shinfo(skb);

		/* Due to header grow, MSS needs to be downgraded. */
		if (!(flags & BPF_F_ADJ_ROOM_FIXED_GSO))
			skb_decrease_gso_size(shinfo, len_diff);

		/* Header must be checked, and gso_segs recomputed. */
		shinfo->gso_type |= gso_type;
		shinfo->gso_segs = 0;
	}

	return 0;
}

static int bpf_skb_net_shrink(struct sk_buff *skb, u32 off, u32 len_diff,
			      u64 flags)
{
	int ret;

	if (unlikely(flags & ~(BPF_F_ADJ_ROOM_FIXED_GSO |
			       BPF_F_ADJ_ROOM_NO_CSUM_RESET)))
		return -EINVAL;

	if (skb_is_gso(skb) && !skb_is_gso_tcp(skb)) {
		/* udp gso_size delineates datagrams, only allow if fixed */
		if (!(skb_shinfo(skb)->gso_type & SKB_GSO_UDP_L4) ||
		    !(flags & BPF_F_ADJ_ROOM_FIXED_GSO))
			return -ENOTSUPP;
	}

	ret = skb_unclone(skb, GFP_ATOMIC);
	if (unlikely(ret < 0))
		return ret;

	ret = bpf_skb_net_hdr_pop(skb, off, len_diff);
	if (unlikely(ret < 0))
		return ret;

	if (skb_is_gso(skb)) {
		struct skb_shared_info *shinfo = skb_shinfo(skb);

		/* Due to header shrink, MSS can be upgraded. */
		if (!(flags & BPF_F_ADJ_ROOM_FIXED_GSO))
			skb_increase_gso_size(shinfo, len_diff);

		/* Header must be checked, and gso_segs recomputed. */
		shinfo->gso_type |= SKB_GSO_DODGY;
		shinfo->gso_segs = 0;
	}

	return 0;
}

#define BPF_SKB_MAX_LEN SKB_MAX_ALLOC

BPF_CALL_4(sk_skb_adjust_room, struct sk_buff *, skb, s32, len_diff,
	   u32, mode, u64, flags)
{
	u32 len_diff_abs = abs(len_diff);
	bool shrink = len_diff < 0;
	int ret = 0;

	if (unlikely(flags || mode))
		return -EINVAL;
	if (unlikely(len_diff_abs > 0xfffU))
		return -EFAULT;

	if (!shrink) {
		ret = skb_cow(skb, len_diff);
		if (unlikely(ret < 0))
			return ret;
		__skb_push(skb, len_diff_abs);
		memset(skb->data, 0, len_diff_abs);
	} else {
		if (unlikely(!pskb_may_pull(skb, len_diff_abs)))
			return -ENOMEM;
		__skb_pull(skb, len_diff_abs);
	}
	bpf_compute_data_end_sk_skb(skb);
	if (tls_sw_has_ctx_rx(skb->sk)) {
		struct strp_msg *rxm = strp_msg(skb);

		rxm->full_len += len_diff;
	}
	return ret;
}

static const struct bpf_func_proto sk_skb_adjust_room_proto = {
	.func		= sk_skb_adjust_room,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
	.arg3_type	= ARG_ANYTHING,
	.arg4_type	= ARG_ANYTHING,
};

BPF_CALL_4(bpf_skb_adjust_room, struct sk_buff *, skb, s32, len_diff,
	   u32, mode, u64, flags)
{
	u32 len_cur, len_diff_abs = abs(len_diff);
	u32 len_min = bpf_skb_net_base_len(skb);
	u32 len_max = BPF_SKB_MAX_LEN;
	__be16 proto = skb->protocol;
	bool shrink = len_diff < 0;
	u32 off;
	int ret;

	if (unlikely(flags & ~(BPF_F_ADJ_ROOM_MASK |
			       BPF_F_ADJ_ROOM_NO_CSUM_RESET)))
		return -EINVAL;
	if (unlikely(len_diff_abs > 0xfffU))
		return -EFAULT;
	if (unlikely(proto != htons(ETH_P_IP) &&
		     proto != htons(ETH_P_IPV6)))
		return -ENOTSUPP;

	off = skb_mac_header_len(skb);
	switch (mode) {
	case BPF_ADJ_ROOM_NET:
		off += bpf_skb_net_base_len(skb);
		break;
	case BPF_ADJ_ROOM_MAC:
		break;
	default:
		return -ENOTSUPP;
	}

	len_cur = skb->len - skb_network_offset(skb);
	if ((shrink && (len_diff_abs >= len_cur ||
			len_cur - len_diff_abs < len_min)) ||
	    (!shrink && (skb->len + len_diff_abs > len_max &&
			 !skb_is_gso(skb))))
		return -ENOTSUPP;

	ret = shrink ? bpf_skb_net_shrink(skb, off, len_diff_abs, flags) :
		       bpf_skb_net_grow(skb, off, len_diff_abs, flags);
	if (!ret && !(flags & BPF_F_ADJ_ROOM_NO_CSUM_RESET))
		__skb_reset_checksum_unnecessary(skb);

	bpf_compute_data_pointers(skb);
	return ret;
}

static const struct bpf_func_proto bpf_skb_adjust_room_proto = {
	.func		= bpf_skb_adjust_room,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
	.arg3_type	= ARG_ANYTHING,
	.arg4_type	= ARG_ANYTHING,
};

static u32 __bpf_skb_min_len(const struct sk_buff *skb)
{
	u32 min_len = skb_network_offset(skb);

	if (skb_transport_header_was_set(skb))
		min_len = skb_transport_offset(skb);
	if (skb->ip_summed == CHECKSUM_PARTIAL)
		min_len = skb_checksum_start_offset(skb) +
			  skb->csum_offset + sizeof(__sum16);
	return min_len;
}

static int bpf_skb_grow_rcsum(struct sk_buff *skb, unsigned int new_len)
{
	unsigned int old_len = skb->len;
	int ret;

	ret = __skb_grow_rcsum(skb, new_len);
	if (!ret)
		memset(skb->data + old_len, 0, new_len - old_len);
	return ret;
}

static int bpf_skb_trim_rcsum(struct sk_buff *skb, unsigned int new_len)
{
	return __skb_trim_rcsum(skb, new_len);
}

static inline int __bpf_skb_change_tail(struct sk_buff *skb, u32 new_len,
					u64 flags)
{
	u32 max_len = BPF_SKB_MAX_LEN;
	u32 min_len = __bpf_skb_min_len(skb);
	int ret;

	if (unlikely(flags || new_len > max_len || new_len < min_len))
		return -EINVAL;
	if (skb->encapsulation)
		return -ENOTSUPP;

	/* The basic idea of this helper is that it's performing the
	 * needed work to either grow or trim an skb, and eBPF program
	 * rewrites the rest via helpers like bpf_skb_store_bytes(),
	 * bpf_lX_csum_replace() and others rather than passing a raw
	 * buffer here. This one is a slow path helper and intended
	 * for replies with control messages.
	 *
	 * Like in bpf_skb_change_proto(), we want to keep this rather
	 * minimal and without protocol specifics so that we are able
	 * to separate concerns as in bpf_skb_store_bytes() should only
	 * be the one responsible for writing buffers.
	 *
	 * It's really expected to be a slow path operation here for
	 * control message replies, so we're implicitly linearizing,
	 * uncloning and drop offloads from the skb by this.
	 */
	ret = __bpf_try_make_writable(skb, skb->len);
	if (!ret) {
		if (new_len > skb->len)
			ret = bpf_skb_grow_rcsum(skb, new_len);
		else if (new_len < skb->len)
			ret = bpf_skb_trim_rcsum(skb, new_len);
		if (!ret && skb_is_gso(skb))
			skb_gso_reset(skb);
	}
	return ret;
}

BPF_CALL_3(bpf_skb_change_tail, struct sk_buff *, skb, u32, new_len,
	   u64, flags)
{
	int ret = __bpf_skb_change_tail(skb, new_len, flags);

	bpf_compute_data_pointers(skb);
	return ret;
}

static const struct bpf_func_proto bpf_skb_change_tail_proto = {
	.func		= bpf_skb_change_tail,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
	.arg3_type	= ARG_ANYTHING,
};

BPF_CALL_3(sk_skb_change_tail, struct sk_buff *, skb, u32, new_len,
	   u64, flags)
{
	int ret = __bpf_skb_change_tail(skb, new_len, flags);

	bpf_compute_data_end_sk_skb(skb);
	return ret;
}

static const struct bpf_func_proto sk_skb_change_tail_proto = {
	.func		= sk_skb_change_tail,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
	.arg3_type	= ARG_ANYTHING,
};

static inline int __bpf_skb_change_head(struct sk_buff *skb, u32 head_room,
					u64 flags)
{
	u32 max_len = BPF_SKB_MAX_LEN;
	u32 new_len = skb->len + head_room;
	int ret;

	if (unlikely(flags || (!skb_is_gso(skb) && new_len > max_len) ||
		     new_len < skb->len))
		return -EINVAL;

	ret = skb_cow(skb, head_room);
	if (likely(!ret)) {
		/* Idea for this helper is that we currently only
		 * allow to expand on mac header. This means that
		 * skb->protocol network header, etc, stay as is.
		 * Compared to bpf_skb_change_tail(), we're more
		 * flexible due to not needing to linearize or
		 * reset GSO. Intention for this helper is to be
		 * used by an L3 skb that needs to push mac header
		 * for redirection into L2 device.
		 */
		__skb_push(skb, head_room);
		memset(skb->data, 0, head_room);
		skb_reset_mac_header(skb);
		skb_reset_mac_len(skb);
	}

	return ret;
}

BPF_CALL_3(bpf_skb_change_head, struct sk_buff *, skb, u32, head_room,
	   u64, flags)
{
	int ret = __bpf_skb_change_head(skb, head_room, flags);

	bpf_compute_data_pointers(skb);
	return ret;
}

static const struct bpf_func_proto bpf_skb_change_head_proto = {
	.func		= bpf_skb_change_head,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
	.arg3_type	= ARG_ANYTHING,
};

BPF_CALL_3(sk_skb_change_head, struct sk_buff *, skb, u32, head_room,
	   u64, flags)
{
	int ret = __bpf_skb_change_head(skb, head_room, flags);

	bpf_compute_data_end_sk_skb(skb);
	return ret;
}

static const struct bpf_func_proto sk_skb_change_head_proto = {
	.func		= sk_skb_change_head,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
	.arg3_type	= ARG_ANYTHING,
};
static unsigned long xdp_get_metalen(const struct xdp_buff *xdp)
{
	return xdp_data_meta_unsupported(xdp) ? 0 :
	       xdp->data - xdp->data_meta;
}

BPF_CALL_2(bpf_xdp_adjust_head, struct xdp_buff *, xdp, int, offset)
{
	void *xdp_frame_end = xdp->data_hard_start + sizeof(struct xdp_frame);
	unsigned long metalen = xdp_get_metalen(xdp);
	void *data_start = xdp_frame_end + metalen;
	void *data = xdp->data + offset;

	if (unlikely(data < data_start ||
		     data > xdp->data_end - ETH_HLEN))
		return -EINVAL;

	if (metalen)
		memmove(xdp->data_meta + offset,
			xdp->data_meta, metalen);
	xdp->data_meta += offset;
	xdp->data = data;

	return 0;
}

static const struct bpf_func_proto bpf_xdp_adjust_head_proto = {
	.func		= bpf_xdp_adjust_head,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
};

BPF_CALL_2(bpf_xdp_adjust_tail, struct xdp_buff *, xdp, int, offset)
{
	void *data_hard_end = xdp_data_hard_end(xdp); /* use xdp->frame_sz */
	void *data_end = xdp->data_end + offset;

	/* Notice that xdp_data_hard_end have reserved some tailroom */
	if (unlikely(data_end > data_hard_end))
		return -EINVAL;

	/* ALL drivers MUST init xdp->frame_sz, chicken check below */
	if (unlikely(xdp->frame_sz > PAGE_SIZE)) {
		WARN_ONCE(1, "Too BIG xdp->frame_sz = %d\n", xdp->frame_sz);
		return -EINVAL;
	}

	if (unlikely(data_end < xdp->data + ETH_HLEN))
		return -EINVAL;

	/* Clear memory area on grow, can contain uninit kernel memory */
	if (offset > 0)
		memset(xdp->data_end, 0, offset);

	xdp->data_end = data_end;

	return 0;
}

static const struct bpf_func_proto bpf_xdp_adjust_tail_proto = {
	.func		= bpf_xdp_adjust_tail,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
};

BPF_CALL_2(bpf_xdp_adjust_meta, struct xdp_buff *, xdp, int, offset)
{
	void *xdp_frame_end = xdp->data_hard_start + sizeof(struct xdp_frame);
	void *meta = xdp->data_meta + offset;
	unsigned long metalen = xdp->data - meta;

	if (xdp_data_meta_unsupported(xdp))
		return -ENOTSUPP;
	if (unlikely(meta < xdp_frame_end ||
		     meta > xdp->data))
		return -EINVAL;
	if (unlikely((metalen & (sizeof(__u32) - 1)) ||
		     (metalen > 32)))
		return -EACCES;

	xdp->data_meta = meta;

	return 0;
}

static const struct bpf_func_proto bpf_xdp_adjust_meta_proto = {
	.func		= bpf_xdp_adjust_meta,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
};

static int __bpf_tx_xdp_map(struct net_device *dev_rx, void *fwd,
			    struct bpf_map *map, struct xdp_buff *xdp)
{
	switch (map->map_type) {
	case BPF_MAP_TYPE_DEVMAP:
	case BPF_MAP_TYPE_DEVMAP_HASH:
		return dev_map_enqueue(fwd, xdp, dev_rx);
	case BPF_MAP_TYPE_CPUMAP:
		return cpu_map_enqueue(fwd, xdp, dev_rx);
	case BPF_MAP_TYPE_XSKMAP:
		return __xsk_map_redirect(fwd, xdp);
	default:
		return -EBADRQC;
	}
	return 0;
}

void xdp_do_flush(void)
{
	__dev_flush();
	__cpu_map_flush();
	__xsk_map_flush();
}
EXPORT_SYMBOL_GPL(xdp_do_flush);

static inline void *__xdp_map_lookup_elem(struct bpf_map *map, u32 index)
{
	switch (map->map_type) {
	case BPF_MAP_TYPE_DEVMAP:
		return __dev_map_lookup_elem(map, index);
	case BPF_MAP_TYPE_DEVMAP_HASH:
		return __dev_map_hash_lookup_elem(map, index);
	case BPF_MAP_TYPE_CPUMAP:
		return __cpu_map_lookup_elem(map, index);
	case BPF_MAP_TYPE_XSKMAP:
		return __xsk_map_lookup_elem(map, index);
	default:
		return NULL;
	}
}

void bpf_clear_redirect_map(struct bpf_map *map)
{
	struct bpf_redirect_info *ri;
	int cpu;

	for_each_possible_cpu(cpu) {
		ri = per_cpu_ptr(&bpf_redirect_info, cpu);
		/* Avoid polluting remote cacheline due to writes if
		 * not needed. Once we pass this test, we need the
		 * cmpxchg() to make sure it hasn't been changed in
		 * the meantime by remote CPU.
		 */
		if (unlikely(READ_ONCE(ri->map) == map))
			cmpxchg(&ri->map, map, NULL);
	}
}

int xdp_do_redirect(struct net_device *dev, struct xdp_buff *xdp,
		    struct bpf_prog *xdp_prog)
{
	struct bpf_redirect_info *ri = this_cpu_ptr(&bpf_redirect_info);
	struct bpf_map *map = READ_ONCE(ri->map);
	u32 index = ri->tgt_index;
	void *fwd = ri->tgt_value;
	int err;

	ri->tgt_index = 0;
	ri->tgt_value = NULL;
	WRITE_ONCE(ri->map, NULL);

	if (unlikely(!map)) {
		fwd = dev_get_by_index_rcu(dev_net(dev), index);
		if (unlikely(!fwd)) {
			err = -EINVAL;
			goto err;
		}

		err = dev_xdp_enqueue(fwd, xdp, dev);
	} else {
		err = __bpf_tx_xdp_map(dev, fwd, map, xdp);
	}

	if (unlikely(err))
		goto err;

	_trace_xdp_redirect_map(dev, xdp_prog, fwd, map, index);
	return 0;
err:
	_trace_xdp_redirect_map_err(dev, xdp_prog, fwd, map, index, err);
	return err;
}
EXPORT_SYMBOL_GPL(xdp_do_redirect);

static int xdp_do_generic_redirect_map(struct net_device *dev,
				       struct sk_buff *skb,
				       struct xdp_buff *xdp,
				       struct bpf_prog *xdp_prog,
				       struct bpf_map *map)
{
	struct bpf_redirect_info *ri = this_cpu_ptr(&bpf_redirect_info);
	u32 index = ri->tgt_index;
	void *fwd = ri->tgt_value;
	int err = 0;

	ri->tgt_index = 0;
	ri->tgt_value = NULL;
	WRITE_ONCE(ri->map, NULL);

	if (map->map_type == BPF_MAP_TYPE_DEVMAP ||
	    map->map_type == BPF_MAP_TYPE_DEVMAP_HASH) {
		struct bpf_dtab_netdev *dst = fwd;

		err = dev_map_generic_redirect(dst, skb, xdp_prog);
		if (unlikely(err))
			goto err;
	} else if (map->map_type == BPF_MAP_TYPE_XSKMAP) {
		struct xdp_sock *xs = fwd;

		err = xsk_generic_rcv(xs, xdp);
		if (err)
			goto err;
		consume_skb(skb);
	} else {
		/* TODO: Handle BPF_MAP_TYPE_CPUMAP */
		err = -EBADRQC;
		goto err;
	}

	_trace_xdp_redirect_map(dev, xdp_prog, fwd, map, index);
	return 0;
err:
	_trace_xdp_redirect_map_err(dev, xdp_prog, fwd, map, index, err);
	return err;
}

int xdp_do_generic_redirect(struct net_device *dev, struct sk_buff *skb,
			    struct xdp_buff *xdp, struct bpf_prog *xdp_prog)
{
	struct bpf_redirect_info *ri = this_cpu_ptr(&bpf_redirect_info);
	struct bpf_map *map = READ_ONCE(ri->map);
	u32 index = ri->tgt_index;
	struct net_device *fwd;
	int err = 0;

	if (map)
		return xdp_do_generic_redirect_map(dev, skb, xdp, xdp_prog,
						   map);
	ri->tgt_index = 0;
	fwd = dev_get_by_index_rcu(dev_net(dev), index);
	if (unlikely(!fwd)) {
		err = -EINVAL;
		goto err;
	}

	err = xdp_ok_fwd_dev(fwd, skb->len);
	if (unlikely(err))
		goto err;

	skb->dev = fwd;
	_trace_xdp_redirect(dev, xdp_prog, index);
	generic_xdp_tx(skb, xdp_prog);
	return 0;
err:
	_trace_xdp_redirect_err(dev, xdp_prog, index, err);
	return err;
}

BPF_CALL_2(bpf_xdp_redirect, u32, ifindex, u64, flags)
{
	struct bpf_redirect_info *ri = this_cpu_ptr(&bpf_redirect_info);

	if (unlikely(flags))
		return XDP_ABORTED;

	ri->flags = flags;
	ri->tgt_index = ifindex;
	ri->tgt_value = NULL;
	WRITE_ONCE(ri->map, NULL);

	return XDP_REDIRECT;
}

static const struct bpf_func_proto bpf_xdp_redirect_proto = {
	.func           = bpf_xdp_redirect,
	.gpl_only       = false,
	.ret_type       = RET_INTEGER,
	.arg1_type      = ARG_ANYTHING,
	.arg2_type      = ARG_ANYTHING,
};

BPF_CALL_3(bpf_xdp_redirect_map, struct bpf_map *, map, u32, ifindex,
	   u64, flags)
{
	struct bpf_redirect_info *ri = this_cpu_ptr(&bpf_redirect_info);

	/* Lower bits of the flags are used as return code on lookup failure */
	if (unlikely(flags > XDP_TX))
		return XDP_ABORTED;

	ri->tgt_value = __xdp_map_lookup_elem(map, ifindex);
	if (unlikely(!ri->tgt_value)) {
		/* If the lookup fails we want to clear out the state in the
		 * redirect_info struct completely, so that if an eBPF program
		 * performs multiple lookups, the last one always takes
		 * precedence.
		 */
		WRITE_ONCE(ri->map, NULL);
		return flags;
	}

	ri->flags = flags;
	ri->tgt_index = ifindex;
	WRITE_ONCE(ri->map, map);

	return XDP_REDIRECT;
}

static const struct bpf_func_proto bpf_xdp_redirect_map_proto = {
	.func           = bpf_xdp_redirect_map,
	.gpl_only       = false,
	.ret_type       = RET_INTEGER,
	.arg1_type      = ARG_CONST_MAP_PTR,
	.arg2_type      = ARG_ANYTHING,
	.arg3_type      = ARG_ANYTHING,
};

static unsigned long bpf_skb_copy(void *dst_buff, const void *skb,
				  unsigned long off, unsigned long len)
{
	void *ptr = skb_header_pointer(skb, off, len, dst_buff);

	if (unlikely(!ptr))
		return len;
	if (ptr != dst_buff)
		memcpy(dst_buff, ptr, len);

	return 0;
}

BPF_CALL_5(bpf_skb_event_output, struct sk_buff *, skb, struct bpf_map *, map,
	   u64, flags, void *, meta, u64, meta_size)
{
	u64 skb_size = (flags & BPF_F_CTXLEN_MASK) >> 32;

	if (unlikely(flags & ~(BPF_F_CTXLEN_MASK | BPF_F_INDEX_MASK)))
		return -EINVAL;
	if (unlikely(!skb || skb_size > skb->len))
		return -EFAULT;

	return bpf_event_output(map, flags, meta, meta_size, skb, skb_size,
				bpf_skb_copy);
}

static const struct bpf_func_proto bpf_skb_event_output_proto = {
	.func		= bpf_skb_event_output,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_CONST_MAP_PTR,
	.arg3_type	= ARG_ANYTHING,
	.arg4_type	= ARG_PTR_TO_MEM,
	.arg5_type	= ARG_CONST_SIZE_OR_ZERO,
};

BTF_ID_LIST_SINGLE(bpf_skb_output_btf_ids, struct, sk_buff)

const struct bpf_func_proto bpf_skb_output_proto = {
	.func		= bpf_skb_event_output,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_BTF_ID,
	.arg1_btf_id	= &bpf_skb_output_btf_ids[0],
	.arg2_type	= ARG_CONST_MAP_PTR,
	.arg3_type	= ARG_ANYTHING,
	.arg4_type	= ARG_PTR_TO_MEM,
	.arg5_type	= ARG_CONST_SIZE_OR_ZERO,
};

static unsigned short bpf_tunnel_key_af(u64 flags)
{
	return flags & BPF_F_TUNINFO_IPV6 ? AF_INET6 : AF_INET;
}

BPF_CALL_4(bpf_skb_get_tunnel_key, struct sk_buff *, skb, struct bpf_tunnel_key *, to,
	   u32, size, u64, flags)
{
	const struct ip_tunnel_info *info = skb_tunnel_info(skb);
	u8 compat[sizeof(struct bpf_tunnel_key)];
	void *to_orig = to;
	int err;

	if (unlikely(!info || (flags & ~(BPF_F_TUNINFO_IPV6)))) {
		err = -EINVAL;
		goto err_clear;
	}
	if (ip_tunnel_info_af(info) != bpf_tunnel_key_af(flags)) {
		err = -EPROTO;
		goto err_clear;
	}
	if (unlikely(size != sizeof(struct bpf_tunnel_key))) {
		err = -EINVAL;
		switch (size) {
		case offsetof(struct bpf_tunnel_key, tunnel_label):
		case offsetof(struct bpf_tunnel_key, tunnel_ext):
			goto set_compat;
		case offsetof(struct bpf_tunnel_key, remote_ipv6[1]):
			/* Fixup deprecated structure layouts here, so we have
			 * a common path later on.
			 */
			if (ip_tunnel_info_af(info) != AF_INET)
				goto err_clear;
set_compat:
			to = (struct bpf_tunnel_key *)compat;
			break;
		default:
			goto err_clear;
		}
	}

	to->tunnel_id = be64_to_cpu(info->key.tun_id);
	to->tunnel_tos = info->key.tos;
	to->tunnel_ttl = info->key.ttl;
	to->tunnel_ext = 0;

	if (flags & BPF_F_TUNINFO_IPV6) {
		memcpy(to->remote_ipv6, &info->key.u.ipv6.src,
		       sizeof(to->remote_ipv6));
		to->tunnel_label = be32_to_cpu(info->key.label);
	} else {
		to->remote_ipv4 = be32_to_cpu(info->key.u.ipv4.src);
		memset(&to->remote_ipv6[1], 0, sizeof(__u32) * 3);
		to->tunnel_label = 0;
	}

	if (unlikely(size != sizeof(struct bpf_tunnel_key)))
		memcpy(to_orig, to, size);

	return 0;
err_clear:
	memset(to_orig, 0, size);
	return err;
}

static const struct bpf_func_proto bpf_skb_get_tunnel_key_proto = {
	.func		= bpf_skb_get_tunnel_key,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_PTR_TO_UNINIT_MEM,
	.arg3_type	= ARG_CONST_SIZE,
	.arg4_type	= ARG_ANYTHING,
};

BPF_CALL_3(bpf_skb_get_tunnel_opt, struct sk_buff *, skb, u8 *, to, u32, size)
{
	const struct ip_tunnel_info *info = skb_tunnel_info(skb);
	int err;

	if (unlikely(!info ||
		     !(info->key.tun_flags & TUNNEL_OPTIONS_PRESENT))) {
		err = -ENOENT;
		goto err_clear;
	}
	if (unlikely(size < info->options_len)) {
		err = -ENOMEM;
		goto err_clear;
	}

	ip_tunnel_info_opts_get(to, info);
	if (size > info->options_len)
		memset(to + info->options_len, 0, size - info->options_len);

	return info->options_len;
err_clear:
	memset(to, 0, size);
	return err;
}

static const struct bpf_func_proto bpf_skb_get_tunnel_opt_proto = {
	.func		= bpf_skb_get_tunnel_opt,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_PTR_TO_UNINIT_MEM,
	.arg3_type	= ARG_CONST_SIZE,
};

static struct metadata_dst __percpu *md_dst;

BPF_CALL_4(bpf_skb_set_tunnel_key, struct sk_buff *, skb,
	   const struct bpf_tunnel_key *, from, u32, size, u64, flags)
{
	struct metadata_dst *md = this_cpu_ptr(md_dst);
	u8 compat[sizeof(struct bpf_tunnel_key)];
	struct ip_tunnel_info *info;

	if (unlikely(flags & ~(BPF_F_TUNINFO_IPV6 | BPF_F_ZERO_CSUM_TX |
			       BPF_F_DONT_FRAGMENT | BPF_F_SEQ_NUMBER)))
		return -EINVAL;
	if (unlikely(size != sizeof(struct bpf_tunnel_key))) {
		switch (size) {
		case offsetof(struct bpf_tunnel_key, tunnel_label):
		case offsetof(struct bpf_tunnel_key, tunnel_ext):
		case offsetof(struct bpf_tunnel_key, remote_ipv6[1]):
			/* Fixup deprecated structure layouts here, so we have
			 * a common path later on.
			 */
			memcpy(compat, from, size);
			memset(compat + size, 0, sizeof(compat) - size);
			from = (const struct bpf_tunnel_key *) compat;
			break;
		default:
			return -EINVAL;
		}
	}
	if (unlikely((!(flags & BPF_F_TUNINFO_IPV6) && from->tunnel_label) ||
		     from->tunnel_ext))
		return -EINVAL;

	skb_dst_drop(skb);
	dst_hold((struct dst_entry *) md);
	skb_dst_set(skb, (struct dst_entry *) md);

	info = &md->u.tun_info;
	memset(info, 0, sizeof(*info));
	info->mode = IP_TUNNEL_INFO_TX;

	info->key.tun_flags = TUNNEL_KEY | TUNNEL_CSUM | TUNNEL_NOCACHE;
	if (flags & BPF_F_DONT_FRAGMENT)
		info->key.tun_flags |= TUNNEL_DONT_FRAGMENT;
	if (flags & BPF_F_ZERO_CSUM_TX)
		info->key.tun_flags &= ~TUNNEL_CSUM;
	if (flags & BPF_F_SEQ_NUMBER)
		info->key.tun_flags |= TUNNEL_SEQ;

	info->key.tun_id = cpu_to_be64(from->tunnel_id);
	info->key.tos = from->tunnel_tos;
	info->key.ttl = from->tunnel_ttl;

	if (flags & BPF_F_TUNINFO_IPV6) {
		info->mode |= IP_TUNNEL_INFO_IPV6;
		memcpy(&info->key.u.ipv6.dst, from->remote_ipv6,
		       sizeof(from->remote_ipv6));
		info->key.label = cpu_to_be32(from->tunnel_label) &
				  IPV6_FLOWLABEL_MASK;
	} else {
		info->key.u.ipv4.dst = cpu_to_be32(from->remote_ipv4);
	}

	return 0;
}

static const struct bpf_func_proto bpf_skb_set_tunnel_key_proto = {
	.func		= bpf_skb_set_tunnel_key,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_PTR_TO_MEM,
	.arg3_type	= ARG_CONST_SIZE,
	.arg4_type	= ARG_ANYTHING,
};

BPF_CALL_3(bpf_skb_set_tunnel_opt, struct sk_buff *, skb,
	   const u8 *, from, u32, size)
{
	struct ip_tunnel_info *info = skb_tunnel_info(skb);
	const struct metadata_dst *md = this_cpu_ptr(md_dst);

	if (unlikely(info != &md->u.tun_info || (size & (sizeof(u32) - 1))))
		return -EINVAL;
	if (unlikely(size > IP_TUNNEL_OPTS_MAX))
		return -ENOMEM;

	ip_tunnel_info_opts_set(info, from, size, TUNNEL_OPTIONS_PRESENT);

	return 0;
}

static const struct bpf_func_proto bpf_skb_set_tunnel_opt_proto = {
	.func		= bpf_skb_set_tunnel_opt,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_PTR_TO_MEM,
	.arg3_type	= ARG_CONST_SIZE,
};

static const struct bpf_func_proto *
bpf_get_skb_set_tunnel_proto(enum bpf_func_id which)
{
	if (!md_dst) {
		struct metadata_dst __percpu *tmp;

		tmp = metadata_dst_alloc_percpu(IP_TUNNEL_OPTS_MAX,
						METADATA_IP_TUNNEL,
						GFP_KERNEL);
		if (!tmp)
			return NULL;
		if (cmpxchg(&md_dst, NULL, tmp))
			metadata_dst_free_percpu(tmp);
	}

	switch (which) {
	case BPF_FUNC_skb_set_tunnel_key:
		return &bpf_skb_set_tunnel_key_proto;
	case BPF_FUNC_skb_set_tunnel_opt:
		return &bpf_skb_set_tunnel_opt_proto;
	default:
		return NULL;
	}
}

BPF_CALL_3(bpf_skb_under_cgroup, struct sk_buff *, skb, struct bpf_map *, map,
	   u32, idx)
{
	struct bpf_array *array = container_of(map, struct bpf_array, map);
	struct cgroup *cgrp;
	struct sock *sk;

	sk = skb_to_full_sk(skb);
	if (!sk || !sk_fullsock(sk))
		return -ENOENT;
	if (unlikely(idx >= array->map.max_entries))
		return -E2BIG;

	cgrp = READ_ONCE(array->ptrs[idx]);
	if (unlikely(!cgrp))
		return -EAGAIN;

	return sk_under_cgroup_hierarchy(sk, cgrp);
}

static const struct bpf_func_proto bpf_skb_under_cgroup_proto = {
	.func		= bpf_skb_under_cgroup,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_CONST_MAP_PTR,
	.arg3_type	= ARG_ANYTHING,
};

#ifdef CONFIG_SOCK_CGROUP_DATA
static inline u64 __bpf_sk_cgroup_id(struct sock *sk)
{
	struct cgroup *cgrp;

	sk = sk_to_full_sk(sk);
	if (!sk || !sk_fullsock(sk))
		return 0;

	cgrp = sock_cgroup_ptr(&sk->sk_cgrp_data);
	return cgroup_id(cgrp);
}

BPF_CALL_1(bpf_skb_cgroup_id, const struct sk_buff *, skb)
{
	return __bpf_sk_cgroup_id(skb->sk);
}

static const struct bpf_func_proto bpf_skb_cgroup_id_proto = {
	.func           = bpf_skb_cgroup_id,
	.gpl_only       = false,
	.ret_type       = RET_INTEGER,
	.arg1_type      = ARG_PTR_TO_CTX,
};

static inline u64 __bpf_sk_ancestor_cgroup_id(struct sock *sk,
					      int ancestor_level)
{
	struct cgroup *ancestor;
	struct cgroup *cgrp;

	sk = sk_to_full_sk(sk);
	if (!sk || !sk_fullsock(sk))
		return 0;

	cgrp = sock_cgroup_ptr(&sk->sk_cgrp_data);
	ancestor = cgroup_ancestor(cgrp, ancestor_level);
	if (!ancestor)
		return 0;

	return cgroup_id(ancestor);
}

BPF_CALL_2(bpf_skb_ancestor_cgroup_id, const struct sk_buff *, skb, int,
	   ancestor_level)
{
	return __bpf_sk_ancestor_cgroup_id(skb->sk, ancestor_level);
}

static const struct bpf_func_proto bpf_skb_ancestor_cgroup_id_proto = {
	.func           = bpf_skb_ancestor_cgroup_id,
	.gpl_only       = false,
	.ret_type       = RET_INTEGER,
	.arg1_type      = ARG_PTR_TO_CTX,
	.arg2_type      = ARG_ANYTHING,
};

BPF_CALL_1(bpf_sk_cgroup_id, struct sock *, sk)
{
	return __bpf_sk_cgroup_id(sk);
}

static const struct bpf_func_proto bpf_sk_cgroup_id_proto = {
	.func           = bpf_sk_cgroup_id,
	.gpl_only       = false,
	.ret_type       = RET_INTEGER,
	.arg1_type      = ARG_PTR_TO_BTF_ID_SOCK_COMMON,
};

BPF_CALL_2(bpf_sk_ancestor_cgroup_id, struct sock *, sk, int, ancestor_level)
{
	return __bpf_sk_ancestor_cgroup_id(sk, ancestor_level);
}

static const struct bpf_func_proto bpf_sk_ancestor_cgroup_id_proto = {
	.func           = bpf_sk_ancestor_cgroup_id,
	.gpl_only       = false,
	.ret_type       = RET_INTEGER,
	.arg1_type      = ARG_PTR_TO_BTF_ID_SOCK_COMMON,
	.arg2_type      = ARG_ANYTHING,
};
#endif

static unsigned long bpf_xdp_copy(void *dst_buff, const void *src_buff,
				  unsigned long off, unsigned long len)
{
	memcpy(dst_buff, src_buff + off, len);
	return 0;
}

BPF_CALL_5(bpf_xdp_event_output, struct xdp_buff *, xdp, struct bpf_map *, map,
	   u64, flags, void *, meta, u64, meta_size)
{
	u64 xdp_size = (flags & BPF_F_CTXLEN_MASK) >> 32;

	if (unlikely(flags & ~(BPF_F_CTXLEN_MASK | BPF_F_INDEX_MASK)))
		return -EINVAL;
	if (unlikely(!xdp ||
		     xdp_size > (unsigned long)(xdp->data_end - xdp->data)))
		return -EFAULT;

	return bpf_event_output(map, flags, meta, meta_size, xdp->data,
				xdp_size, bpf_xdp_copy);
}

static const struct bpf_func_proto bpf_xdp_event_output_proto = {
	.func		= bpf_xdp_event_output,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_CONST_MAP_PTR,
	.arg3_type	= ARG_ANYTHING,
	.arg4_type	= ARG_PTR_TO_MEM,
	.arg5_type	= ARG_CONST_SIZE_OR_ZERO,
};

BTF_ID_LIST_SINGLE(bpf_xdp_output_btf_ids, struct, xdp_buff)

const struct bpf_func_proto bpf_xdp_output_proto = {
	.func		= bpf_xdp_event_output,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_BTF_ID,
	.arg1_btf_id	= &bpf_xdp_output_btf_ids[0],
	.arg2_type	= ARG_CONST_MAP_PTR,
	.arg3_type	= ARG_ANYTHING,
	.arg4_type	= ARG_PTR_TO_MEM,
	.arg5_type	= ARG_CONST_SIZE_OR_ZERO,
};

BPF_CALL_1(bpf_get_socket_cookie, struct sk_buff *, skb)
{
	return skb->sk ? __sock_gen_cookie(skb->sk) : 0;
}

static const struct bpf_func_proto bpf_get_socket_cookie_proto = {
	.func           = bpf_get_socket_cookie,
	.gpl_only       = false,
	.ret_type       = RET_INTEGER,
	.arg1_type      = ARG_PTR_TO_CTX,
};

BPF_CALL_1(bpf_get_socket_cookie_sock_addr, struct bpf_sock_addr_kern *, ctx)
{
	return __sock_gen_cookie(ctx->sk);
}

static const struct bpf_func_proto bpf_get_socket_cookie_sock_addr_proto = {
	.func		= bpf_get_socket_cookie_sock_addr,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
};

BPF_CALL_1(bpf_get_socket_cookie_sock, struct sock *, ctx)
{
	return __sock_gen_cookie(ctx);
}

static const struct bpf_func_proto bpf_get_socket_cookie_sock_proto = {
	.func		= bpf_get_socket_cookie_sock,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
};

BPF_CALL_1(bpf_get_socket_cookie_sock_ops, struct bpf_sock_ops_kern *, ctx)
{
	return __sock_gen_cookie(ctx->sk);
}

static const struct bpf_func_proto bpf_get_socket_cookie_sock_ops_proto = {
	.func		= bpf_get_socket_cookie_sock_ops,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
};

static u64 __bpf_get_netns_cookie(struct sock *sk)
{
#ifdef CONFIG_NET_NS
	return __net_gen_cookie(sk ? sk->sk_net.net : &init_net);
#else
	return 0;
#endif
}

BPF_CALL_1(bpf_get_netns_cookie_sock, struct sock *, ctx)
{
	return __bpf_get_netns_cookie(ctx);
}

static const struct bpf_func_proto bpf_get_netns_cookie_sock_proto = {
	.func		= bpf_get_netns_cookie_sock,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX_OR_NULL,
};

BPF_CALL_1(bpf_get_netns_cookie_sock_addr, struct bpf_sock_addr_kern *, ctx)
{
	return __bpf_get_netns_cookie(ctx ? ctx->sk : NULL);
}

static const struct bpf_func_proto bpf_get_netns_cookie_sock_addr_proto = {
	.func		= bpf_get_netns_cookie_sock_addr,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX_OR_NULL,
};

BPF_CALL_1(bpf_get_socket_uid, struct sk_buff *, skb)
{
	struct sock *sk = sk_to_full_sk(skb->sk);
	kuid_t kuid;

	if (!sk || !sk_fullsock(sk))
		return overflowuid;
	kuid = sock_net_uid(sock_net(sk), sk);
	return from_kuid_munged(sock_net(sk)->user_ns, kuid);
}

static const struct bpf_func_proto bpf_get_socket_uid_proto = {
	.func           = bpf_get_socket_uid,
	.gpl_only       = false,
	.ret_type       = RET_INTEGER,
	.arg1_type      = ARG_PTR_TO_CTX,
};

static int _bpf_setsockopt(struct sock *sk, int level, int optname,
			   char *optval, int optlen)
{
	char devname[IFNAMSIZ];
	int val, valbool;
	struct net *net;
	int ifindex;
	int ret = 0;

	if (!sk_fullsock(sk))
		return -EINVAL;

	sock_owned_by_me(sk);

	if (level == SOL_SOCKET) {
		if (optlen != sizeof(int) && optname != SO_BINDTODEVICE)
			return -EINVAL;
		val = *((int *)optval);
		valbool = val ? 1 : 0;

		/* Only some socketops are supported */
		switch (optname) {
		case SO_RCVBUF:
			val = min_t(u32, val, sysctl_rmem_max);
			val = min_t(int, val, INT_MAX / 2);
			sk->sk_userlocks |= SOCK_RCVBUF_LOCK;
			WRITE_ONCE(sk->sk_rcvbuf,
				   max_t(int, val * 2, SOCK_MIN_RCVBUF));
			break;
		case SO_SNDBUF:
			val = min_t(u32, val, sysctl_wmem_max);
			val = min_t(int, val, INT_MAX / 2);
			sk->sk_userlocks |= SOCK_SNDBUF_LOCK;
			WRITE_ONCE(sk->sk_sndbuf,
				   max_t(int, val * 2, SOCK_MIN_SNDBUF));
			break;
		case SO_MAX_PACING_RATE: /* 32bit version */
			if (val != ~0U)
				cmpxchg(&sk->sk_pacing_status,
					SK_PACING_NONE,
					SK_PACING_NEEDED);
			sk->sk_max_pacing_rate = (val == ~0U) ?
						 ~0UL : (unsigned int)val;
			sk->sk_pacing_rate = min(sk->sk_pacing_rate,
						 sk->sk_max_pacing_rate);
			break;
		case SO_PRIORITY:
			sk->sk_priority = val;
			break;
		case SO_RCVLOWAT:
			if (val < 0)
				val = INT_MAX;
			WRITE_ONCE(sk->sk_rcvlowat, val ? : 1);
			break;
		case SO_MARK:
			if (sk->sk_mark != val) {
				sk->sk_mark = val;
				sk_dst_reset(sk);
			}
			break;
		case SO_BINDTODEVICE:
			optlen = min_t(long, optlen, IFNAMSIZ - 1);
			strncpy(devname, optval, optlen);
			devname[optlen] = 0;

			ifindex = 0;
			if (devname[0] != '\0') {
				struct net_device *dev;

				ret = -ENODEV;

				net = sock_net(sk);
				dev = dev_get_by_name(net, devname);
				if (!dev)
					break;
				ifindex = dev->ifindex;
				dev_put(dev);
			}
			ret = sock_bindtoindex(sk, ifindex, false);
			break;
		case SO_KEEPALIVE:
			if (sk->sk_prot->keepalive)
				sk->sk_prot->keepalive(sk, valbool);
			sock_valbool_flag(sk, SOCK_KEEPOPEN, valbool);
			break;
		default:
			ret = -EINVAL;
		}
#ifdef CONFIG_INET
	} else if (level == SOL_IP) {
		if (optlen != sizeof(int) || sk->sk_family != AF_INET)
			return -EINVAL;

		val = *((int *)optval);
		/* Only some options are supported */
		switch (optname) {
		case IP_TOS:
			if (val < -1 || val > 0xff) {
				ret = -EINVAL;
			} else {
				struct inet_sock *inet = inet_sk(sk);

				if (val == -1)
					val = 0;
				inet->tos = val;
			}
			break;
		default:
			ret = -EINVAL;
		}
#if IS_ENABLED(CONFIG_IPV6)
	} else if (level == SOL_IPV6) {
		if (optlen != sizeof(int) || sk->sk_family != AF_INET6)
			return -EINVAL;

		val = *((int *)optval);
		/* Only some options are supported */
		switch (optname) {
		case IPV6_TCLASS:
			if (val < -1 || val > 0xff) {
				ret = -EINVAL;
			} else {
				struct ipv6_pinfo *np = inet6_sk(sk);

				if (val == -1)
					val = 0;
				np->tclass = val;
			}
			break;
		default:
			ret = -EINVAL;
		}
#endif
	} else if (level == SOL_TCP &&
		   sk->sk_prot->setsockopt == tcp_setsockopt) {
		if (optname == TCP_CONGESTION) {
			char name[TCP_CA_NAME_MAX];

			strncpy(name, optval, min_t(long, optlen,
						    TCP_CA_NAME_MAX-1));
			name[TCP_CA_NAME_MAX-1] = 0;
			ret = tcp_set_congestion_control(sk, name, false, true);
		} else {
			struct inet_connection_sock *icsk = inet_csk(sk);
			struct tcp_sock *tp = tcp_sk(sk);
			unsigned long timeout;

			if (optlen != sizeof(int))
				return -EINVAL;

			val = *((int *)optval);
			/* Only some options are supported */
			switch (optname) {
			case TCP_BPF_IW:
				if (val <= 0 || tp->data_segs_out > tp->syn_data)
					ret = -EINVAL;
				else
					tp->snd_cwnd = val;
				break;
			case TCP_BPF_SNDCWND_CLAMP:
				if (val <= 0) {
					ret = -EINVAL;
				} else {
					tp->snd_cwnd_clamp = val;
					tp->snd_ssthresh = val;
				}
				break;
			case TCP_BPF_DELACK_MAX:
				timeout = usecs_to_jiffies(val);
				if (timeout > TCP_DELACK_MAX ||
				    timeout < TCP_TIMEOUT_MIN)
					return -EINVAL;
				inet_csk(sk)->icsk_delack_max = timeout;
				break;
			case TCP_BPF_RTO_MIN:
				timeout = usecs_to_jiffies(val);
				if (timeout > TCP_RTO_MIN ||
				    timeout < TCP_TIMEOUT_MIN)
					return -EINVAL;
				inet_csk(sk)->icsk_rto_min = timeout;
				break;
			case TCP_SAVE_SYN:
				if (val < 0 || val > 1)
					ret = -EINVAL;
				else
					tp->save_syn = val;
				break;
			case TCP_KEEPIDLE:
				ret = tcp_sock_set_keepidle_locked(sk, val);
				break;
			case TCP_KEEPINTVL:
				if (val < 1 || val > MAX_TCP_KEEPINTVL)
					ret = -EINVAL;
				else
					tp->keepalive_intvl = val * HZ;
				break;
			case TCP_KEEPCNT:
				if (val < 1 || val > MAX_TCP_KEEPCNT)
					ret = -EINVAL;
				else
					tp->keepalive_probes = val;
				break;
			case TCP_SYNCNT:
				if (val < 1 || val > MAX_TCP_SYNCNT)
					ret = -EINVAL;
				else
					icsk->icsk_syn_retries = val;
				break;
			case TCP_USER_TIMEOUT:
				if (val < 0)
					ret = -EINVAL;
				else
					icsk->icsk_user_timeout = val;
				break;
			case TCP_NOTSENT_LOWAT:
				tp->notsent_lowat = val;
				sk->sk_write_space(sk);
				break;
			default:
				ret = -EINVAL;
			}
		}
#endif
	} else {
		ret = -EINVAL;
	}
	return ret;
}

static int _bpf_getsockopt(struct sock *sk, int level, int optname,
			   char *optval, int optlen)
{
	if (!sk_fullsock(sk))
		goto err_clear;

	sock_owned_by_me(sk);

#ifdef CONFIG_INET
	if (level == SOL_TCP && sk->sk_prot->getsockopt == tcp_getsockopt) {
		struct inet_connection_sock *icsk;
		struct tcp_sock *tp;

		switch (optname) {
		case TCP_CONGESTION:
			icsk = inet_csk(sk);

			if (!icsk->icsk_ca_ops || optlen <= 1)
				goto err_clear;
			strncpy(optval, icsk->icsk_ca_ops->name, optlen);
			optval[optlen - 1] = 0;
			break;
		case TCP_SAVED_SYN:
			tp = tcp_sk(sk);

			if (optlen <= 0 || !tp->saved_syn ||
			    optlen > tcp_saved_syn_len(tp->saved_syn))
				goto err_clear;
			memcpy(optval, tp->saved_syn->data, optlen);
			break;
		default:
			goto err_clear;
		}
	} else if (level == SOL_IP) {
		struct inet_sock *inet = inet_sk(sk);

		if (optlen != sizeof(int) || sk->sk_family != AF_INET)
			goto err_clear;

		/* Only some options are supported */
		switch (optname) {
		case IP_TOS:
			*((int *)optval) = (int)inet->tos;
			break;
		default:
			goto err_clear;
		}
#if IS_ENABLED(CONFIG_IPV6)
	} else if (level == SOL_IPV6) {
		struct ipv6_pinfo *np = inet6_sk(sk);

		if (optlen != sizeof(int) || sk->sk_family != AF_INET6)
			goto err_clear;

		/* Only some options are supported */
		switch (optname) {
		case IPV6_TCLASS:
			*((int *)optval) = (int)np->tclass;
			break;
		default:
			goto err_clear;
		}
#endif
	} else {
		goto err_clear;
	}
	return 0;
#endif
err_clear:
	memset(optval, 0, optlen);
	return -EINVAL;
}

BPF_CALL_5(bpf_sock_addr_setsockopt, struct bpf_sock_addr_kern *, ctx,
	   int, level, int, optname, char *, optval, int, optlen)
{
	return _bpf_setsockopt(ctx->sk, level, optname, optval, optlen);
}

static const struct bpf_func_proto bpf_sock_addr_setsockopt_proto = {
	.func		= bpf_sock_addr_setsockopt,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
	.arg3_type	= ARG_ANYTHING,
	.arg4_type	= ARG_PTR_TO_MEM,
	.arg5_type	= ARG_CONST_SIZE,
};

BPF_CALL_5(bpf_sock_addr_getsockopt, struct bpf_sock_addr_kern *, ctx,
	   int, level, int, optname, char *, optval, int, optlen)
{
	return _bpf_getsockopt(ctx->sk, level, optname, optval, optlen);
}

static const struct bpf_func_proto bpf_sock_addr_getsockopt_proto = {
	.func		= bpf_sock_addr_getsockopt,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
	.arg3_type	= ARG_ANYTHING,
	.arg4_type	= ARG_PTR_TO_UNINIT_MEM,
	.arg5_type	= ARG_CONST_SIZE,
};

BPF_CALL_5(bpf_sock_ops_setsockopt, struct bpf_sock_ops_kern *, bpf_sock,
	   int, level, int, optname, char *, optval, int, optlen)
{
	return _bpf_setsockopt(bpf_sock->sk, level, optname, optval, optlen);
}

static const struct bpf_func_proto bpf_sock_ops_setsockopt_proto = {
	.func		= bpf_sock_ops_setsockopt,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
	.arg3_type	= ARG_ANYTHING,
	.arg4_type	= ARG_PTR_TO_MEM,
	.arg5_type	= ARG_CONST_SIZE,
};

static int bpf_sock_ops_get_syn(struct bpf_sock_ops_kern *bpf_sock,
				int optname, const u8 **start)
{
	struct sk_buff *syn_skb = bpf_sock->syn_skb;
	const u8 *hdr_start;
	int ret;

	if (syn_skb) {
		/* sk is a request_sock here */

		if (optname == TCP_BPF_SYN) {
			hdr_start = syn_skb->data;
			ret = tcp_hdrlen(syn_skb);
		} else if (optname == TCP_BPF_SYN_IP) {
			hdr_start = skb_network_header(syn_skb);
			ret = skb_network_header_len(syn_skb) +
				tcp_hdrlen(syn_skb);
		} else {
			/* optname == TCP_BPF_SYN_MAC */
			hdr_start = skb_mac_header(syn_skb);
			ret = skb_mac_header_len(syn_skb) +
				skb_network_header_len(syn_skb) +
				tcp_hdrlen(syn_skb);
		}
	} else {
		struct sock *sk = bpf_sock->sk;
		struct saved_syn *saved_syn;

		if (sk->sk_state == TCP_NEW_SYN_RECV)
			/* synack retransmit. bpf_sock->syn_skb will
			 * not be available.  It has to resort to
			 * saved_syn (if it is saved).
			 */
			saved_syn = inet_reqsk(sk)->saved_syn;
		else
			saved_syn = tcp_sk(sk)->saved_syn;

		if (!saved_syn)
			return -ENOENT;

		if (optname == TCP_BPF_SYN) {
			hdr_start = saved_syn->data +
				saved_syn->mac_hdrlen +
				saved_syn->network_hdrlen;
			ret = saved_syn->tcp_hdrlen;
		} else if (optname == TCP_BPF_SYN_IP) {
			hdr_start = saved_syn->data +
				saved_syn->mac_hdrlen;
			ret = saved_syn->network_hdrlen +
				saved_syn->tcp_hdrlen;
		} else {
			/* optname == TCP_BPF_SYN_MAC */

			/* TCP_SAVE_SYN may not have saved the mac hdr */
			if (!saved_syn->mac_hdrlen)
				return -ENOENT;

			hdr_start = saved_syn->data;
			ret = saved_syn->mac_hdrlen +
				saved_syn->network_hdrlen +
				saved_syn->tcp_hdrlen;
		}
	}

	*start = hdr_start;
	return ret;
}

BPF_CALL_5(bpf_sock_ops_getsockopt, struct bpf_sock_ops_kern *, bpf_sock,
	   int, level, int, optname, char *, optval, int, optlen)
{
	if (IS_ENABLED(CONFIG_INET) && level == SOL_TCP &&
	    optname >= TCP_BPF_SYN && optname <= TCP_BPF_SYN_MAC) {
		int ret, copy_len = 0;
		const u8 *start;

		ret = bpf_sock_ops_get_syn(bpf_sock, optname, &start);
		if (ret > 0) {
			copy_len = ret;
			if (optlen < copy_len) {
				copy_len = optlen;
				ret = -ENOSPC;
			}

			memcpy(optval, start, copy_len);
		}

		/* Zero out unused buffer at the end */
		memset(optval + copy_len, 0, optlen - copy_len);

		return ret;
	}

	return _bpf_getsockopt(bpf_sock->sk, level, optname, optval, optlen);
}

static const struct bpf_func_proto bpf_sock_ops_getsockopt_proto = {
	.func		= bpf_sock_ops_getsockopt,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
	.arg3_type	= ARG_ANYTHING,
	.arg4_type	= ARG_PTR_TO_UNINIT_MEM,
	.arg5_type	= ARG_CONST_SIZE,
};

BPF_CALL_2(bpf_sock_ops_cb_flags_set, struct bpf_sock_ops_kern *, bpf_sock,
	   int, argval)
{
	struct sock *sk = bpf_sock->sk;
	int val = argval & BPF_SOCK_OPS_ALL_CB_FLAGS;

	if (!IS_ENABLED(CONFIG_INET) || !sk_fullsock(sk))
		return -EINVAL;

	tcp_sk(sk)->bpf_sock_ops_cb_flags = val;

	return argval & (~BPF_SOCK_OPS_ALL_CB_FLAGS);
}

static const struct bpf_func_proto bpf_sock_ops_cb_flags_set_proto = {
	.func		= bpf_sock_ops_cb_flags_set,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
};

const struct ipv6_bpf_stub *ipv6_bpf_stub __read_mostly;
EXPORT_SYMBOL_GPL(ipv6_bpf_stub);

BPF_CALL_3(bpf_bind, struct bpf_sock_addr_kern *, ctx, struct sockaddr *, addr,
	   int, addr_len)
{
#ifdef CONFIG_INET
	struct sock *sk = ctx->sk;
	u32 flags = BIND_FROM_BPF;
	int err;

	err = -EINVAL;
	if (addr_len < offsetofend(struct sockaddr, sa_family))
		return err;
	if (addr->sa_family == AF_INET) {
		if (addr_len < sizeof(struct sockaddr_in))
			return err;
		if (((struct sockaddr_in *)addr)->sin_port == htons(0))
			flags |= BIND_FORCE_ADDRESS_NO_PORT;
		return __inet_bind(sk, addr, addr_len, flags);
#if IS_ENABLED(CONFIG_IPV6)
	} else if (addr->sa_family == AF_INET6) {
		if (addr_len < SIN6_LEN_RFC2133)
			return err;
		if (((struct sockaddr_in6 *)addr)->sin6_port == htons(0))
			flags |= BIND_FORCE_ADDRESS_NO_PORT;
		/* ipv6_bpf_stub cannot be NULL, since it's called from
		 * bpf_cgroup_inet6_connect hook and ipv6 is already loaded
		 */
		return ipv6_bpf_stub->inet6_bind(sk, addr, addr_len, flags);
#endif /* CONFIG_IPV6 */
	}
#endif /* CONFIG_INET */

	return -EAFNOSUPPORT;
}

static const struct bpf_func_proto bpf_bind_proto = {
	.func		= bpf_bind,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_PTR_TO_MEM,
	.arg3_type	= ARG_CONST_SIZE,
};

#ifdef CONFIG_XFRM
BPF_CALL_5(bpf_skb_get_xfrm_state, struct sk_buff *, skb, u32, index,
	   struct bpf_xfrm_state *, to, u32, size, u64, flags)
{
	const struct sec_path *sp = skb_sec_path(skb);
	const struct xfrm_state *x;

	if (!sp || unlikely(index >= sp->len || flags))
		goto err_clear;

	x = sp->xvec[index];

	if (unlikely(size != sizeof(struct bpf_xfrm_state)))
		goto err_clear;

	to->reqid = x->props.reqid;
	to->spi = x->id.spi;
	to->family = x->props.family;
	to->ext = 0;

	if (to->family == AF_INET6) {
		memcpy(to->remote_ipv6, x->props.saddr.a6,
		       sizeof(to->remote_ipv6));
	} else {
		to->remote_ipv4 = x->props.saddr.a4;
		memset(&to->remote_ipv6[1], 0, sizeof(__u32) * 3);
	}

	return 0;
err_clear:
	memset(to, 0, size);
	return -EINVAL;
}

static const struct bpf_func_proto bpf_skb_get_xfrm_state_proto = {
	.func		= bpf_skb_get_xfrm_state,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
	.arg3_type	= ARG_PTR_TO_UNINIT_MEM,
	.arg4_type	= ARG_CONST_SIZE,
	.arg5_type	= ARG_ANYTHING,
};
#endif

#if IS_ENABLED(CONFIG_INET) || IS_ENABLED(CONFIG_IPV6)
static int bpf_fib_set_fwd_params(struct bpf_fib_lookup *params,
				  const struct neighbour *neigh,
				  const struct net_device *dev)
{
	memcpy(params->dmac, neigh->ha, ETH_ALEN);
	memcpy(params->smac, dev->dev_addr, ETH_ALEN);
	params->h_vlan_TCI = 0;
	params->h_vlan_proto = 0;

	return 0;
}
#endif

#if IS_ENABLED(CONFIG_INET)
static int bpf_ipv4_fib_lookup(struct net *net, struct bpf_fib_lookup *params,
			       u32 flags, bool check_mtu)
{
	struct fib_nh_common *nhc;
	struct in_device *in_dev;
	struct neighbour *neigh;
	struct net_device *dev;
	struct fib_result res;
	struct flowi4 fl4;
	int err;
	u32 mtu;

	dev = dev_get_by_index_rcu(net, params->ifindex);
	if (unlikely(!dev))
		return -ENODEV;

	/* verify forwarding is enabled on this interface */
	in_dev = __in_dev_get_rcu(dev);
	if (unlikely(!in_dev || !IN_DEV_FORWARD(in_dev)))
		return BPF_FIB_LKUP_RET_FWD_DISABLED;

	if (flags & BPF_FIB_LOOKUP_OUTPUT) {
		fl4.flowi4_iif = 1;
		fl4.flowi4_oif = params->ifindex;
	} else {
		fl4.flowi4_iif = params->ifindex;
		fl4.flowi4_oif = 0;
	}
	fl4.flowi4_tos = params->tos & IPTOS_RT_MASK;
	fl4.flowi4_scope = RT_SCOPE_UNIVERSE;
	fl4.flowi4_flags = 0;

	fl4.flowi4_proto = params->l4_protocol;
	fl4.daddr = params->ipv4_dst;
	fl4.saddr = params->ipv4_src;
	fl4.fl4_sport = params->sport;
	fl4.fl4_dport = params->dport;
	fl4.flowi4_multipath_hash = 0;

	if (flags & BPF_FIB_LOOKUP_DIRECT) {
		u32 tbid = l3mdev_fib_table_rcu(dev) ? : RT_TABLE_MAIN;
		struct fib_table *tb;

		tb = fib_get_table(net, tbid);
		if (unlikely(!tb))
			return BPF_FIB_LKUP_RET_NOT_FWDED;

		err = fib_table_lookup(tb, &fl4, &res, FIB_LOOKUP_NOREF);
	} else {
		fl4.flowi4_mark = 0;
		fl4.flowi4_secid = 0;
		fl4.flowi4_tun_key.tun_id = 0;
		fl4.flowi4_uid = sock_net_uid(net, NULL);

		err = fib_lookup(net, &fl4, &res, FIB_LOOKUP_NOREF);
	}

	if (err) {
		/* map fib lookup errors to RTN_ type */
		if (err == -EINVAL)
			return BPF_FIB_LKUP_RET_BLACKHOLE;
		if (err == -EHOSTUNREACH)
			return BPF_FIB_LKUP_RET_UNREACHABLE;
		if (err == -EACCES)
			return BPF_FIB_LKUP_RET_PROHIBIT;

		return BPF_FIB_LKUP_RET_NOT_FWDED;
	}

	if (res.type != RTN_UNICAST)
		return BPF_FIB_LKUP_RET_NOT_FWDED;

	if (fib_info_num_path(res.fi) > 1)
		fib_select_path(net, &res, &fl4, NULL);

	if (check_mtu) {
		mtu = ip_mtu_from_fib_result(&res, params->ipv4_dst);
		if (params->tot_len > mtu)
			return BPF_FIB_LKUP_RET_FRAG_NEEDED;
	}

	nhc = res.nhc;

	/* do not handle lwt encaps right now */
	if (nhc->nhc_lwtstate)
		return BPF_FIB_LKUP_RET_UNSUPP_LWT;

	dev = nhc->nhc_dev;

	params->rt_metric = res.fi->fib_priority;
	params->ifindex = dev->ifindex;

	/* xdp and cls_bpf programs are run in RCU-bh so
	 * rcu_read_lock_bh is not needed here
	 */
	if (likely(nhc->nhc_gw_family != AF_INET6)) {
		if (nhc->nhc_gw_family)
			params->ipv4_dst = nhc->nhc_gw.ipv4;

		neigh = __ipv4_neigh_lookup_noref(dev,
						 (__force u32)params->ipv4_dst);
	} else {
		struct in6_addr *dst = (struct in6_addr *)params->ipv6_dst;

		params->family = AF_INET6;
		*dst = nhc->nhc_gw.ipv6;
		neigh = __ipv6_neigh_lookup_noref_stub(dev, dst);
	}

	if (!neigh)
		return BPF_FIB_LKUP_RET_NO_NEIGH;

	return bpf_fib_set_fwd_params(params, neigh, dev);
}
#endif

#if IS_ENABLED(CONFIG_IPV6)
static int bpf_ipv6_fib_lookup(struct net *net, struct bpf_fib_lookup *params,
			       u32 flags, bool check_mtu)
{
	struct in6_addr *src = (struct in6_addr *) params->ipv6_src;
	struct in6_addr *dst = (struct in6_addr *) params->ipv6_dst;
	struct fib6_result res = {};
	struct neighbour *neigh;
	struct net_device *dev;
	struct inet6_dev *idev;
	struct flowi6 fl6;
	int strict = 0;
	int oif, err;
	u32 mtu;

	/* link local addresses are never forwarded */
	if (rt6_need_strict(dst) || rt6_need_strict(src))
		return BPF_FIB_LKUP_RET_NOT_FWDED;

	dev = dev_get_by_index_rcu(net, params->ifindex);
	if (unlikely(!dev))
		return -ENODEV;

	idev = __in6_dev_get_safely(dev);
	if (unlikely(!idev || !idev->cnf.forwarding))
		return BPF_FIB_LKUP_RET_FWD_DISABLED;

	if (flags & BPF_FIB_LOOKUP_OUTPUT) {
		fl6.flowi6_iif = 1;
		oif = fl6.flowi6_oif = params->ifindex;
	} else {
		oif = fl6.flowi6_iif = params->ifindex;
		fl6.flowi6_oif = 0;
		strict = RT6_LOOKUP_F_HAS_SADDR;
	}
	fl6.flowlabel = params->flowinfo;
	fl6.flowi6_scope = 0;
	fl6.flowi6_flags = 0;
	fl6.mp_hash = 0;

	fl6.flowi6_proto = params->l4_protocol;
	fl6.daddr = *dst;
	fl6.saddr = *src;
	fl6.fl6_sport = params->sport;
	fl6.fl6_dport = params->dport;

	if (flags & BPF_FIB_LOOKUP_DIRECT) {
		u32 tbid = l3mdev_fib_table_rcu(dev) ? : RT_TABLE_MAIN;
		struct fib6_table *tb;

		tb = ipv6_stub->fib6_get_table(net, tbid);
		if (unlikely(!tb))
			return BPF_FIB_LKUP_RET_NOT_FWDED;

		err = ipv6_stub->fib6_table_lookup(net, tb, oif, &fl6, &res,
						   strict);
	} else {
		fl6.flowi6_mark = 0;
		fl6.flowi6_secid = 0;
		fl6.flowi6_tun_key.tun_id = 0;
		fl6.flowi6_uid = sock_net_uid(net, NULL);

		err = ipv6_stub->fib6_lookup(net, oif, &fl6, &res, strict);
	}

	if (unlikely(err || IS_ERR_OR_NULL(res.f6i) ||
		     res.f6i == net->ipv6.fib6_null_entry))
		return BPF_FIB_LKUP_RET_NOT_FWDED;

	switch (res.fib6_type) {
	/* only unicast is forwarded */
	case RTN_UNICAST:
		break;
	case RTN_BLACKHOLE:
		return BPF_FIB_LKUP_RET_BLACKHOLE;
	case RTN_UNREACHABLE:
		return BPF_FIB_LKUP_RET_UNREACHABLE;
	case RTN_PROHIBIT:
		return BPF_FIB_LKUP_RET_PROHIBIT;
	default:
		return BPF_FIB_LKUP_RET_NOT_FWDED;
	}

	ipv6_stub->fib6_select_path(net, &res, &fl6, fl6.flowi6_oif,
				    fl6.flowi6_oif != 0, NULL, strict);

	if (check_mtu) {
		mtu = ipv6_stub->ip6_mtu_from_fib6(&res, dst, src);
		if (params->tot_len > mtu)
			return BPF_FIB_LKUP_RET_FRAG_NEEDED;
	}

	if (res.nh->fib_nh_lws)
		return BPF_FIB_LKUP_RET_UNSUPP_LWT;

	if (res.nh->fib_nh_gw_family)
		*dst = res.nh->fib_nh_gw6;

	dev = res.nh->fib_nh_dev;
	params->rt_metric = res.f6i->fib6_metric;
	params->ifindex = dev->ifindex;

	/* xdp and cls_bpf programs are run in RCU-bh so rcu_read_lock_bh is
	 * not needed here.
	 */
	neigh = __ipv6_neigh_lookup_noref_stub(dev, dst);
	if (!neigh)
		return BPF_FIB_LKUP_RET_NO_NEIGH;

	return bpf_fib_set_fwd_params(params, neigh, dev);
}
#endif

BPF_CALL_4(bpf_xdp_fib_lookup, struct xdp_buff *, ctx,
	   struct bpf_fib_lookup *, params, int, plen, u32, flags)
{
	if (plen < sizeof(*params))
		return -EINVAL;

	if (flags & ~(BPF_FIB_LOOKUP_DIRECT | BPF_FIB_LOOKUP_OUTPUT))
		return -EINVAL;

	switch (params->family) {
#if IS_ENABLED(CONFIG_INET)
	case AF_INET:
		return bpf_ipv4_fib_lookup(dev_net(ctx->rxq->dev), params,
					   flags, true);
#endif
#if IS_ENABLED(CONFIG_IPV6)
	case AF_INET6:
		return bpf_ipv6_fib_lookup(dev_net(ctx->rxq->dev), params,
					   flags, true);
#endif
	}
	return -EAFNOSUPPORT;
}

static const struct bpf_func_proto bpf_xdp_fib_lookup_proto = {
	.func		= bpf_xdp_fib_lookup,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type      = ARG_PTR_TO_CTX,
	.arg2_type      = ARG_PTR_TO_MEM,
	.arg3_type      = ARG_CONST_SIZE,
	.arg4_type	= ARG_ANYTHING,
};

BPF_CALL_4(bpf_skb_fib_lookup, struct sk_buff *, skb,
	   struct bpf_fib_lookup *, params, int, plen, u32, flags)
{
	struct net *net = dev_net(skb->dev);
	int rc = -EAFNOSUPPORT;
	bool check_mtu = false;

	if (plen < sizeof(*params))
		return -EINVAL;

	if (flags & ~(BPF_FIB_LOOKUP_DIRECT | BPF_FIB_LOOKUP_OUTPUT))
		return -EINVAL;

	if (params->tot_len)
		check_mtu = true;

	switch (params->family) {
#if IS_ENABLED(CONFIG_INET)
	case AF_INET:
		rc = bpf_ipv4_fib_lookup(net, params, flags, check_mtu);
		break;
#endif
#if IS_ENABLED(CONFIG_IPV6)
	case AF_INET6:
		rc = bpf_ipv6_fib_lookup(net, params, flags, check_mtu);
		break;
#endif
	}

	if (rc == BPF_FIB_LKUP_RET_SUCCESS && !check_mtu) {
		struct net_device *dev;

		/* When tot_len isn't provided by user, check skb
		 * against MTU of FIB lookup resulting net_device
		 */
		dev = dev_get_by_index_rcu(net, params->ifindex);
		if (!is_skb_forwardable(dev, skb))
			rc = BPF_FIB_LKUP_RET_FRAG_NEEDED;
	}

	return rc;
}

static const struct bpf_func_proto bpf_skb_fib_lookup_proto = {
	.func		= bpf_skb_fib_lookup,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type      = ARG_PTR_TO_CTX,
	.arg2_type      = ARG_PTR_TO_MEM,
	.arg3_type      = ARG_CONST_SIZE,
	.arg4_type	= ARG_ANYTHING,
};

#if IS_ENABLED(CONFIG_IPV6_SEG6_BPF)
static int bpf_push_seg6_encap(struct sk_buff *skb, u32 type, void *hdr, u32 len)
{
	int err;
	struct ipv6_sr_hdr *srh = (struct ipv6_sr_hdr *)hdr;

	if (!seg6_validate_srh(srh, len, false))
		return -EINVAL;

	switch (type) {
	case BPF_LWT_ENCAP_SEG6_INLINE:
		if (skb->protocol != htons(ETH_P_IPV6))
			return -EBADMSG;

		err = seg6_do_srh_inline(skb, srh);
		break;
	case BPF_LWT_ENCAP_SEG6:
		skb_reset_inner_headers(skb);
		skb->encapsulation = 1;
		err = seg6_do_srh_encap(skb, srh, IPPROTO_IPV6);
		break;
	default:
		return -EINVAL;
	}

	bpf_compute_data_pointers(skb);
	if (err)
		return err;

	skb_set_transport_header(skb, sizeof(struct ipv6hdr));

	return seg6_lookup_nexthop(skb, NULL, 0);
}
#endif /* CONFIG_IPV6_SEG6_BPF */

#if IS_ENABLED(CONFIG_LWTUNNEL_BPF)
static int bpf_push_ip_encap(struct sk_buff *skb, void *hdr, u32 len,
			     bool ingress)
{
	return bpf_lwt_push_ip_encap(skb, hdr, len, ingress);
}
#endif

BPF_CALL_4(bpf_lwt_in_push_encap, struct sk_buff *, skb, u32, type, void *, hdr,
	   u32, len)
{
	switch (type) {
#if IS_ENABLED(CONFIG_IPV6_SEG6_BPF)
	case BPF_LWT_ENCAP_SEG6:
	case BPF_LWT_ENCAP_SEG6_INLINE:
		return bpf_push_seg6_encap(skb, type, hdr, len);
#endif
#if IS_ENABLED(CONFIG_LWTUNNEL_BPF)
	case BPF_LWT_ENCAP_IP:
		return bpf_push_ip_encap(skb, hdr, len, true /* ingress */);
#endif
	default:
		return -EINVAL;
	}
}

BPF_CALL_4(bpf_lwt_xmit_push_encap, struct sk_buff *, skb, u32, type,
	   void *, hdr, u32, len)
{
	switch (type) {
#if IS_ENABLED(CONFIG_LWTUNNEL_BPF)
	case BPF_LWT_ENCAP_IP:
		return bpf_push_ip_encap(skb, hdr, len, false /* egress */);
#endif
	default:
		return -EINVAL;
	}
}

static const struct bpf_func_proto bpf_lwt_in_push_encap_proto = {
	.func		= bpf_lwt_in_push_encap,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
	.arg3_type	= ARG_PTR_TO_MEM,
	.arg4_type	= ARG_CONST_SIZE
};

static const struct bpf_func_proto bpf_lwt_xmit_push_encap_proto = {
	.func		= bpf_lwt_xmit_push_encap,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
	.arg3_type	= ARG_PTR_TO_MEM,
	.arg4_type	= ARG_CONST_SIZE
};

#if IS_ENABLED(CONFIG_IPV6_SEG6_BPF)
BPF_CALL_4(bpf_lwt_seg6_store_bytes, struct sk_buff *, skb, u32, offset,
	   const void *, from, u32, len)
{
	struct seg6_bpf_srh_state *srh_state =
		this_cpu_ptr(&seg6_bpf_srh_states);
	struct ipv6_sr_hdr *srh = srh_state->srh;
	void *srh_tlvs, *srh_end, *ptr;
	int srhoff = 0;

	if (srh == NULL)
		return -EINVAL;

	srh_tlvs = (void *)((char *)srh + ((srh->first_segment + 1) << 4));
	srh_end = (void *)((char *)srh + sizeof(*srh) + srh_state->hdrlen);

	ptr = skb->data + offset;
	if (ptr >= srh_tlvs && ptr + len <= srh_end)
		srh_state->valid = false;
	else if (ptr < (void *)&srh->flags ||
		 ptr + len > (void *)&srh->segments)
		return -EFAULT;

	if (unlikely(bpf_try_make_writable(skb, offset + len)))
		return -EFAULT;
	if (ipv6_find_hdr(skb, &srhoff, IPPROTO_ROUTING, NULL, NULL) < 0)
		return -EINVAL;
	srh_state->srh = (struct ipv6_sr_hdr *)(skb->data + srhoff);

	memcpy(skb->data + offset, from, len);
	return 0;
}

static const struct bpf_func_proto bpf_lwt_seg6_store_bytes_proto = {
	.func		= bpf_lwt_seg6_store_bytes,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
	.arg3_type	= ARG_PTR_TO_MEM,
	.arg4_type	= ARG_CONST_SIZE
};

static void bpf_update_srh_state(struct sk_buff *skb)
{
	struct seg6_bpf_srh_state *srh_state =
		this_cpu_ptr(&seg6_bpf_srh_states);
	int srhoff = 0;

	if (ipv6_find_hdr(skb, &srhoff, IPPROTO_ROUTING, NULL, NULL) < 0) {
		srh_state->srh = NULL;
	} else {
		srh_state->srh = (struct ipv6_sr_hdr *)(skb->data + srhoff);
		srh_state->hdrlen = srh_state->srh->hdrlen << 3;
		srh_state->valid = true;
	}
}

BPF_CALL_4(bpf_lwt_seg6_action, struct sk_buff *, skb,
	   u32, action, void *, param, u32, param_len)
{
	struct seg6_bpf_srh_state *srh_state =
		this_cpu_ptr(&seg6_bpf_srh_states);
	int hdroff = 0;
	int err;

	switch (action) {
	case SEG6_LOCAL_ACTION_END_X:
		if (!seg6_bpf_has_valid_srh(skb))
			return -EBADMSG;
		if (param_len != sizeof(struct in6_addr))
			return -EINVAL;
		return seg6_lookup_nexthop(skb, (struct in6_addr *)param, 0);
	case SEG6_LOCAL_ACTION_END_T:
		if (!seg6_bpf_has_valid_srh(skb))
			return -EBADMSG;
		if (param_len != sizeof(int))
			return -EINVAL;
		return seg6_lookup_nexthop(skb, NULL, *(int *)param);
	case SEG6_LOCAL_ACTION_END_DT6:
		if (!seg6_bpf_has_valid_srh(skb))
			return -EBADMSG;
		if (param_len != sizeof(int))
			return -EINVAL;

		if (ipv6_find_hdr(skb, &hdroff, IPPROTO_IPV6, NULL, NULL) < 0)
			return -EBADMSG;
		if (!pskb_pull(skb, hdroff))
			return -EBADMSG;

		skb_postpull_rcsum(skb, skb_network_header(skb), hdroff);
		skb_reset_network_header(skb);
		skb_reset_transport_header(skb);
		skb->encapsulation = 0;

		bpf_compute_data_pointers(skb);
		bpf_update_srh_state(skb);
		return seg6_lookup_nexthop(skb, NULL, *(int *)param);
	case SEG6_LOCAL_ACTION_END_B6:
		if (srh_state->srh && !seg6_bpf_has_valid_srh(skb))
			return -EBADMSG;
		err = bpf_push_seg6_encap(skb, BPF_LWT_ENCAP_SEG6_INLINE,
					  param, param_len);
		if (!err)
			bpf_update_srh_state(skb);

		return err;
	case SEG6_LOCAL_ACTION_END_B6_ENCAP:
		if (srh_state->srh && !seg6_bpf_has_valid_srh(skb))
			return -EBADMSG;
		err = bpf_push_seg6_encap(skb, BPF_LWT_ENCAP_SEG6,
					  param, param_len);
		if (!err)
			bpf_update_srh_state(skb);

		return err;
	default:
		return -EINVAL;
	}
}

static const struct bpf_func_proto bpf_lwt_seg6_action_proto = {
	.func		= bpf_lwt_seg6_action,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
	.arg3_type	= ARG_PTR_TO_MEM,
	.arg4_type	= ARG_CONST_SIZE
};

BPF_CALL_3(bpf_lwt_seg6_adjust_srh, struct sk_buff *, skb, u32, offset,
	   s32, len)
{
	struct seg6_bpf_srh_state *srh_state =
		this_cpu_ptr(&seg6_bpf_srh_states);
	struct ipv6_sr_hdr *srh = srh_state->srh;
	void *srh_end, *srh_tlvs, *ptr;
	struct ipv6hdr *hdr;
	int srhoff = 0;
	int ret;

	if (unlikely(srh == NULL))
		return -EINVAL;

	srh_tlvs = (void *)((unsigned char *)srh + sizeof(*srh) +
			((srh->first_segment + 1) << 4));
	srh_end = (void *)((unsigned char *)srh + sizeof(*srh) +
			srh_state->hdrlen);
	ptr = skb->data + offset;

	if (unlikely(ptr < srh_tlvs || ptr > srh_end))
		return -EFAULT;
	if (unlikely(len < 0 && (void *)((char *)ptr - len) > srh_end))
		return -EFAULT;

	if (len > 0) {
		ret = skb_cow_head(skb, len);
		if (unlikely(ret < 0))
			return ret;

		ret = bpf_skb_net_hdr_push(skb, offset, len);
	} else {
		ret = bpf_skb_net_hdr_pop(skb, offset, -1 * len);
	}

	bpf_compute_data_pointers(skb);
	if (unlikely(ret < 0))
		return ret;

	hdr = (struct ipv6hdr *)skb->data;
	hdr->payload_len = htons(skb->len - sizeof(struct ipv6hdr));

	if (ipv6_find_hdr(skb, &srhoff, IPPROTO_ROUTING, NULL, NULL) < 0)
		return -EINVAL;
	srh_state->srh = (struct ipv6_sr_hdr *)(skb->data + srhoff);
	srh_state->hdrlen += len;
	srh_state->valid = false;
	return 0;
}

static const struct bpf_func_proto bpf_lwt_seg6_adjust_srh_proto = {
	.func		= bpf_lwt_seg6_adjust_srh,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
	.arg3_type	= ARG_ANYTHING,
};
#endif /* CONFIG_IPV6_SEG6_BPF */

#ifdef CONFIG_INET
static struct sock *sk_lookup(struct net *net, struct bpf_sock_tuple *tuple,
			      int dif, int sdif, u8 family, u8 proto)
{
	bool refcounted = false;
	struct sock *sk = NULL;

	if (family == AF_INET) {
		__be32 src4 = tuple->ipv4.saddr;
		__be32 dst4 = tuple->ipv4.daddr;

		if (proto == IPPROTO_TCP)
			sk = __inet_lookup(net, &tcp_hashinfo, NULL, 0,
					   src4, tuple->ipv4.sport,
					   dst4, tuple->ipv4.dport,
					   dif, sdif, &refcounted);
		else
			sk = __udp4_lib_lookup(net, src4, tuple->ipv4.sport,
					       dst4, tuple->ipv4.dport,
					       dif, sdif, &udp_table, NULL);
#if IS_ENABLED(CONFIG_IPV6)
	} else {
		struct in6_addr *src6 = (struct in6_addr *)&tuple->ipv6.saddr;
		struct in6_addr *dst6 = (struct in6_addr *)&tuple->ipv6.daddr;

		if (proto == IPPROTO_TCP)
			sk = __inet6_lookup(net, &tcp_hashinfo, NULL, 0,
					    src6, tuple->ipv6.sport,
					    dst6, ntohs(tuple->ipv6.dport),
					    dif, sdif, &refcounted);
		else if (likely(ipv6_bpf_stub))
			sk = ipv6_bpf_stub->udp6_lib_lookup(net,
							    src6, tuple->ipv6.sport,
							    dst6, tuple->ipv6.dport,
							    dif, sdif,
							    &udp_table, NULL);
#endif
	}

	if (unlikely(sk && !refcounted && !sock_flag(sk, SOCK_RCU_FREE))) {
		WARN_ONCE(1, "Found non-RCU, unreferenced socket!");
		sk = NULL;
	}
	return sk;
}

/* bpf_skc_lookup performs the core lookup for different types of sockets,
 * taking a reference on the socket if it doesn't have the flag SOCK_RCU_FREE.
 * Returns the socket as an 'unsigned long' to simplify the casting in the
 * callers to satisfy BPF_CALL declarations.
 */
static struct sock *
__bpf_skc_lookup(struct sk_buff *skb, struct bpf_sock_tuple *tuple, u32 len,
		 struct net *caller_net, u32 ifindex, u8 proto, u64 netns_id,
		 u64 flags)
{
	struct sock *sk = NULL;
	u8 family = AF_UNSPEC;
	struct net *net;
	int sdif;

	if (len == sizeof(tuple->ipv4))
		family = AF_INET;
	else if (len == sizeof(tuple->ipv6))
		family = AF_INET6;
	else
		return NULL;

	if (unlikely(family == AF_UNSPEC || flags ||
		     !((s32)netns_id < 0 || netns_id <= S32_MAX)))
		goto out;

	if (family == AF_INET)
		sdif = inet_sdif(skb);
	else
		sdif = inet6_sdif(skb);

	if ((s32)netns_id < 0) {
		net = caller_net;
		sk = sk_lookup(net, tuple, ifindex, sdif, family, proto);
	} else {
		net = get_net_ns_by_id(caller_net, netns_id);
		if (unlikely(!net))
			goto out;
		sk = sk_lookup(net, tuple, ifindex, sdif, family, proto);
		put_net(net);
	}

out:
	return sk;
}

static struct sock *
__bpf_sk_lookup(struct sk_buff *skb, struct bpf_sock_tuple *tuple, u32 len,
		struct net *caller_net, u32 ifindex, u8 proto, u64 netns_id,
		u64 flags)
{
	struct sock *sk = __bpf_skc_lookup(skb, tuple, len, caller_net,
					   ifindex, proto, netns_id, flags);

	if (sk) {
		struct sock *sk2 = sk_to_full_sk(sk);

		/* sk_to_full_sk() may return (sk)->rsk_listener, so make sure the original sk
		 * sock refcnt is decremented to prevent a request_sock leak.
		 */
		if (!sk_fullsock(sk2))
			sk2 = NULL;
		if (sk2 != sk) {
			sock_gen_put(sk);
			/* Ensure there is no need to bump sk2 refcnt */
			if (unlikely(sk2 && !sock_flag(sk2, SOCK_RCU_FREE))) {
				WARN_ONCE(1, "Found non-RCU, unreferenced socket!");
				return NULL;
			}
			sk = sk2;
		}
	}

	return sk;
}

static struct sock *
bpf_skc_lookup(struct sk_buff *skb, struct bpf_sock_tuple *tuple, u32 len,
	       u8 proto, u64 netns_id, u64 flags)
{
	struct net *caller_net;
	int ifindex;

	if (skb->dev) {
		caller_net = dev_net(skb->dev);
		ifindex = skb->dev->ifindex;
	} else {
		caller_net = sock_net(skb->sk);
		ifindex = 0;
	}

	return __bpf_skc_lookup(skb, tuple, len, caller_net, ifindex, proto,
				netns_id, flags);
}

static struct sock *
bpf_sk_lookup(struct sk_buff *skb, struct bpf_sock_tuple *tuple, u32 len,
	      u8 proto, u64 netns_id, u64 flags)
{
	struct sock *sk = bpf_skc_lookup(skb, tuple, len, proto, netns_id,
					 flags);

	if (sk) {
		struct sock *sk2 = sk_to_full_sk(sk);

		/* sk_to_full_sk() may return (sk)->rsk_listener, so make sure the original sk
		 * sock refcnt is decremented to prevent a request_sock leak.
		 */
		if (!sk_fullsock(sk2))
			sk2 = NULL;
		if (sk2 != sk) {
			sock_gen_put(sk);
			/* Ensure there is no need to bump sk2 refcnt */
			if (unlikely(sk2 && !sock_flag(sk2, SOCK_RCU_FREE))) {
				WARN_ONCE(1, "Found non-RCU, unreferenced socket!");
				return NULL;
			}
			sk = sk2;
		}
	}

	return sk;
}

BPF_CALL_5(bpf_skc_lookup_tcp, struct sk_buff *, skb,
	   struct bpf_sock_tuple *, tuple, u32, len, u64, netns_id, u64, flags)
{
	return (unsigned long)bpf_skc_lookup(skb, tuple, len, IPPROTO_TCP,
					     netns_id, flags);
}

static const struct bpf_func_proto bpf_skc_lookup_tcp_proto = {
	.func		= bpf_skc_lookup_tcp,
	.gpl_only	= false,
	.pkt_access	= true,
	.ret_type	= RET_PTR_TO_SOCK_COMMON_OR_NULL,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_PTR_TO_MEM,
	.arg3_type	= ARG_CONST_SIZE,
	.arg4_type	= ARG_ANYTHING,
	.arg5_type	= ARG_ANYTHING,
};

BPF_CALL_5(bpf_sk_lookup_tcp, struct sk_buff *, skb,
	   struct bpf_sock_tuple *, tuple, u32, len, u64, netns_id, u64, flags)
{
	return (unsigned long)bpf_sk_lookup(skb, tuple, len, IPPROTO_TCP,
					    netns_id, flags);
}

static const struct bpf_func_proto bpf_sk_lookup_tcp_proto = {
	.func		= bpf_sk_lookup_tcp,
	.gpl_only	= false,
	.pkt_access	= true,
	.ret_type	= RET_PTR_TO_SOCKET_OR_NULL,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_PTR_TO_MEM,
	.arg3_type	= ARG_CONST_SIZE,
	.arg4_type	= ARG_ANYTHING,
	.arg5_type	= ARG_ANYTHING,
};

BPF_CALL_5(bpf_sk_lookup_udp, struct sk_buff *, skb,
	   struct bpf_sock_tuple *, tuple, u32, len, u64, netns_id, u64, flags)
{
	return (unsigned long)bpf_sk_lookup(skb, tuple, len, IPPROTO_UDP,
					    netns_id, flags);
}

static const struct bpf_func_proto bpf_sk_lookup_udp_proto = {
	.func		= bpf_sk_lookup_udp,
	.gpl_only	= false,
	.pkt_access	= true,
	.ret_type	= RET_PTR_TO_SOCKET_OR_NULL,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_PTR_TO_MEM,
	.arg3_type	= ARG_CONST_SIZE,
	.arg4_type	= ARG_ANYTHING,
	.arg5_type	= ARG_ANYTHING,
};

BPF_CALL_1(bpf_sk_release, struct sock *, sk)
{
	if (sk && sk_is_refcounted(sk))
		sock_gen_put(sk);
	return 0;
}

static const struct bpf_func_proto bpf_sk_release_proto = {
	.func		= bpf_sk_release,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_BTF_ID_SOCK_COMMON,
};

BPF_CALL_5(bpf_xdp_sk_lookup_udp, struct xdp_buff *, ctx,
	   struct bpf_sock_tuple *, tuple, u32, len, u32, netns_id, u64, flags)
{
	struct net *caller_net = dev_net(ctx->rxq->dev);
	int ifindex = ctx->rxq->dev->ifindex;

	return (unsigned long)__bpf_sk_lookup(NULL, tuple, len, caller_net,
					      ifindex, IPPROTO_UDP, netns_id,
					      flags);
}

static const struct bpf_func_proto bpf_xdp_sk_lookup_udp_proto = {
	.func           = bpf_xdp_sk_lookup_udp,
	.gpl_only       = false,
	.pkt_access     = true,
	.ret_type       = RET_PTR_TO_SOCKET_OR_NULL,
	.arg1_type      = ARG_PTR_TO_CTX,
	.arg2_type      = ARG_PTR_TO_MEM,
	.arg3_type      = ARG_CONST_SIZE,
	.arg4_type      = ARG_ANYTHING,
	.arg5_type      = ARG_ANYTHING,
};

BPF_CALL_5(bpf_xdp_skc_lookup_tcp, struct xdp_buff *, ctx,
	   struct bpf_sock_tuple *, tuple, u32, len, u32, netns_id, u64, flags)
{
	struct net *caller_net = dev_net(ctx->rxq->dev);
	int ifindex = ctx->rxq->dev->ifindex;

	return (unsigned long)__bpf_skc_lookup(NULL, tuple, len, caller_net,
					       ifindex, IPPROTO_TCP, netns_id,
					       flags);
}

static const struct bpf_func_proto bpf_xdp_skc_lookup_tcp_proto = {
	.func           = bpf_xdp_skc_lookup_tcp,
	.gpl_only       = false,
	.pkt_access     = true,
	.ret_type       = RET_PTR_TO_SOCK_COMMON_OR_NULL,
	.arg1_type      = ARG_PTR_TO_CTX,
	.arg2_type      = ARG_PTR_TO_MEM,
	.arg3_type      = ARG_CONST_SIZE,
	.arg4_type      = ARG_ANYTHING,
	.arg5_type      = ARG_ANYTHING,
};

BPF_CALL_5(bpf_xdp_sk_lookup_tcp, struct xdp_buff *, ctx,
	   struct bpf_sock_tuple *, tuple, u32, len, u32, netns_id, u64, flags)
{
	struct net *caller_net = dev_net(ctx->rxq->dev);
	int ifindex = ctx->rxq->dev->ifindex;

	return (unsigned long)__bpf_sk_lookup(NULL, tuple, len, caller_net,
					      ifindex, IPPROTO_TCP, netns_id,
					      flags);
}

static const struct bpf_func_proto bpf_xdp_sk_lookup_tcp_proto = {
	.func           = bpf_xdp_sk_lookup_tcp,
	.gpl_only       = false,
	.pkt_access     = true,
	.ret_type       = RET_PTR_TO_SOCKET_OR_NULL,
	.arg1_type      = ARG_PTR_TO_CTX,
	.arg2_type      = ARG_PTR_TO_MEM,
	.arg3_type      = ARG_CONST_SIZE,
	.arg4_type      = ARG_ANYTHING,
	.arg5_type      = ARG_ANYTHING,
};

BPF_CALL_5(bpf_sock_addr_skc_lookup_tcp, struct bpf_sock_addr_kern *, ctx,
	   struct bpf_sock_tuple *, tuple, u32, len, u64, netns_id, u64, flags)
{
	return (unsigned long)__bpf_skc_lookup(NULL, tuple, len,
					       sock_net(ctx->sk), 0,
					       IPPROTO_TCP, netns_id, flags);
}

static const struct bpf_func_proto bpf_sock_addr_skc_lookup_tcp_proto = {
	.func		= bpf_sock_addr_skc_lookup_tcp,
	.gpl_only	= false,
	.ret_type	= RET_PTR_TO_SOCK_COMMON_OR_NULL,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_PTR_TO_MEM,
	.arg3_type	= ARG_CONST_SIZE,
	.arg4_type	= ARG_ANYTHING,
	.arg5_type	= ARG_ANYTHING,
};

BPF_CALL_5(bpf_sock_addr_sk_lookup_tcp, struct bpf_sock_addr_kern *, ctx,
	   struct bpf_sock_tuple *, tuple, u32, len, u64, netns_id, u64, flags)
{
	return (unsigned long)__bpf_sk_lookup(NULL, tuple, len,
					      sock_net(ctx->sk), 0, IPPROTO_TCP,
					      netns_id, flags);
}

static const struct bpf_func_proto bpf_sock_addr_sk_lookup_tcp_proto = {
	.func		= bpf_sock_addr_sk_lookup_tcp,
	.gpl_only	= false,
	.ret_type	= RET_PTR_TO_SOCKET_OR_NULL,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_PTR_TO_MEM,
	.arg3_type	= ARG_CONST_SIZE,
	.arg4_type	= ARG_ANYTHING,
	.arg5_type	= ARG_ANYTHING,
};

BPF_CALL_5(bpf_sock_addr_sk_lookup_udp, struct bpf_sock_addr_kern *, ctx,
	   struct bpf_sock_tuple *, tuple, u32, len, u64, netns_id, u64, flags)
{
	return (unsigned long)__bpf_sk_lookup(NULL, tuple, len,
					      sock_net(ctx->sk), 0, IPPROTO_UDP,
					      netns_id, flags);
}

static const struct bpf_func_proto bpf_sock_addr_sk_lookup_udp_proto = {
	.func		= bpf_sock_addr_sk_lookup_udp,
	.gpl_only	= false,
	.ret_type	= RET_PTR_TO_SOCKET_OR_NULL,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_PTR_TO_MEM,
	.arg3_type	= ARG_CONST_SIZE,
	.arg4_type	= ARG_ANYTHING,
	.arg5_type	= ARG_ANYTHING,
};

bool bpf_tcp_sock_is_valid_access(int off, int size, enum bpf_access_type type,
				  struct bpf_insn_access_aux *info)
{
	if (off < 0 || off >= offsetofend(struct bpf_tcp_sock,
					  icsk_retransmits))
		return false;

	if (off % size != 0)
		return false;

	switch (off) {
	case offsetof(struct bpf_tcp_sock, bytes_received):
	case offsetof(struct bpf_tcp_sock, bytes_acked):
		return size == sizeof(__u64);
	default:
		return size == sizeof(__u32);
	}
}

u32 bpf_tcp_sock_convert_ctx_access(enum bpf_access_type type,
				    const struct bpf_insn *si,
				    struct bpf_insn *insn_buf,
				    struct bpf_prog *prog, u32 *target_size)
{
	struct bpf_insn *insn = insn_buf;

#define BPF_TCP_SOCK_GET_COMMON(FIELD)					\
	do {								\
		BUILD_BUG_ON(sizeof_field(struct tcp_sock, FIELD) >	\
			     sizeof_field(struct bpf_tcp_sock, FIELD));	\
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct tcp_sock, FIELD),\
				      si->dst_reg, si->src_reg,		\
				      offsetof(struct tcp_sock, FIELD)); \
	} while (0)

#define BPF_INET_SOCK_GET_COMMON(FIELD)					\
	do {								\
		BUILD_BUG_ON(sizeof_field(struct inet_connection_sock,	\
					  FIELD) >			\
			     sizeof_field(struct bpf_tcp_sock, FIELD));	\
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(			\
					struct inet_connection_sock,	\
					FIELD),				\
				      si->dst_reg, si->src_reg,		\
				      offsetof(				\
					struct inet_connection_sock,	\
					FIELD));			\
	} while (0)

	if (insn > insn_buf)
		return insn - insn_buf;

	switch (si->off) {
	case offsetof(struct bpf_tcp_sock, rtt_min):
		BUILD_BUG_ON(sizeof_field(struct tcp_sock, rtt_min) !=
			     sizeof(struct minmax));
		BUILD_BUG_ON(sizeof(struct minmax) <
			     sizeof(struct minmax_sample));

		*insn++ = BPF_LDX_MEM(BPF_W, si->dst_reg, si->src_reg,
				      offsetof(struct tcp_sock, rtt_min) +
				      offsetof(struct minmax_sample, v));
		break;
	case offsetof(struct bpf_tcp_sock, snd_cwnd):
		BPF_TCP_SOCK_GET_COMMON(snd_cwnd);
		break;
	case offsetof(struct bpf_tcp_sock, srtt_us):
		BPF_TCP_SOCK_GET_COMMON(srtt_us);
		break;
	case offsetof(struct bpf_tcp_sock, snd_ssthresh):
		BPF_TCP_SOCK_GET_COMMON(snd_ssthresh);
		break;
	case offsetof(struct bpf_tcp_sock, rcv_nxt):
		BPF_TCP_SOCK_GET_COMMON(rcv_nxt);
		break;
	case offsetof(struct bpf_tcp_sock, snd_nxt):
		BPF_TCP_SOCK_GET_COMMON(snd_nxt);
		break;
	case offsetof(struct bpf_tcp_sock, snd_una):
		BPF_TCP_SOCK_GET_COMMON(snd_una);
		break;
	case offsetof(struct bpf_tcp_sock, mss_cache):
		BPF_TCP_SOCK_GET_COMMON(mss_cache);
		break;
	case offsetof(struct bpf_tcp_sock, ecn_flags):
		BPF_TCP_SOCK_GET_COMMON(ecn_flags);
		break;
	case offsetof(struct bpf_tcp_sock, rate_delivered):
		BPF_TCP_SOCK_GET_COMMON(rate_delivered);
		break;
	case offsetof(struct bpf_tcp_sock, rate_interval_us):
		BPF_TCP_SOCK_GET_COMMON(rate_interval_us);
		break;
	case offsetof(struct bpf_tcp_sock, packets_out):
		BPF_TCP_SOCK_GET_COMMON(packets_out);
		break;
	case offsetof(struct bpf_tcp_sock, retrans_out):
		BPF_TCP_SOCK_GET_COMMON(retrans_out);
		break;
	case offsetof(struct bpf_tcp_sock, total_retrans):
		BPF_TCP_SOCK_GET_COMMON(total_retrans);
		break;
	case offsetof(struct bpf_tcp_sock, segs_in):
		BPF_TCP_SOCK_GET_COMMON(segs_in);
		break;
	case offsetof(struct bpf_tcp_sock, data_segs_in):
		BPF_TCP_SOCK_GET_COMMON(data_segs_in);
		break;
	case offsetof(struct bpf_tcp_sock, segs_out):
		BPF_TCP_SOCK_GET_COMMON(segs_out);
		break;
	case offsetof(struct bpf_tcp_sock, data_segs_out):
		BPF_TCP_SOCK_GET_COMMON(data_segs_out);
		break;
	case offsetof(struct bpf_tcp_sock, lost_out):
		BPF_TCP_SOCK_GET_COMMON(lost_out);
		break;
	case offsetof(struct bpf_tcp_sock, sacked_out):
		BPF_TCP_SOCK_GET_COMMON(sacked_out);
		break;
	case offsetof(struct bpf_tcp_sock, bytes_received):
		BPF_TCP_SOCK_GET_COMMON(bytes_received);
		break;
	case offsetof(struct bpf_tcp_sock, bytes_acked):
		BPF_TCP_SOCK_GET_COMMON(bytes_acked);
		break;
	case offsetof(struct bpf_tcp_sock, dsack_dups):
		BPF_TCP_SOCK_GET_COMMON(dsack_dups);
		break;
	case offsetof(struct bpf_tcp_sock, delivered):
		BPF_TCP_SOCK_GET_COMMON(delivered);
		break;
	case offsetof(struct bpf_tcp_sock, delivered_ce):
		BPF_TCP_SOCK_GET_COMMON(delivered_ce);
		break;
	case offsetof(struct bpf_tcp_sock, icsk_retransmits):
		BPF_INET_SOCK_GET_COMMON(icsk_retransmits);
		break;
	}

	return insn - insn_buf;
}

BPF_CALL_1(bpf_tcp_sock, struct sock *, sk)
{
	if (sk_fullsock(sk) && sk->sk_protocol == IPPROTO_TCP)
		return (unsigned long)sk;

	return (unsigned long)NULL;
}

const struct bpf_func_proto bpf_tcp_sock_proto = {
	.func		= bpf_tcp_sock,
	.gpl_only	= false,
	.ret_type	= RET_PTR_TO_TCP_SOCK_OR_NULL,
	.arg1_type	= ARG_PTR_TO_SOCK_COMMON,
};

BPF_CALL_1(bpf_get_listener_sock, struct sock *, sk)
{
	sk = sk_to_full_sk(sk);

	if (sk->sk_state == TCP_LISTEN && sock_flag(sk, SOCK_RCU_FREE))
		return (unsigned long)sk;

	return (unsigned long)NULL;
}

static const struct bpf_func_proto bpf_get_listener_sock_proto = {
	.func		= bpf_get_listener_sock,
	.gpl_only	= false,
	.ret_type	= RET_PTR_TO_SOCKET_OR_NULL,
	.arg1_type	= ARG_PTR_TO_SOCK_COMMON,
};

BPF_CALL_1(bpf_skb_ecn_set_ce, struct sk_buff *, skb)
{
	unsigned int iphdr_len;

	switch (skb_protocol(skb, true)) {
	case cpu_to_be16(ETH_P_IP):
		iphdr_len = sizeof(struct iphdr);
		break;
	case cpu_to_be16(ETH_P_IPV6):
		iphdr_len = sizeof(struct ipv6hdr);
		break;
	default:
		return 0;
	}

	if (skb_headlen(skb) < iphdr_len)
		return 0;

	if (skb_cloned(skb) && !skb_clone_writable(skb, iphdr_len))
		return 0;

	return INET_ECN_set_ce(skb);
}

bool bpf_xdp_sock_is_valid_access(int off, int size, enum bpf_access_type type,
				  struct bpf_insn_access_aux *info)
{
	if (off < 0 || off >= offsetofend(struct bpf_xdp_sock, queue_id))
		return false;

	if (off % size != 0)
		return false;

	switch (off) {
	default:
		return size == sizeof(__u32);
	}
}

u32 bpf_xdp_sock_convert_ctx_access(enum bpf_access_type type,
				    const struct bpf_insn *si,
				    struct bpf_insn *insn_buf,
				    struct bpf_prog *prog, u32 *target_size)
{
	struct bpf_insn *insn = insn_buf;

#define BPF_XDP_SOCK_GET(FIELD)						\
	do {								\
		BUILD_BUG_ON(sizeof_field(struct xdp_sock, FIELD) >	\
			     sizeof_field(struct bpf_xdp_sock, FIELD));	\
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct xdp_sock, FIELD),\
				      si->dst_reg, si->src_reg,		\
				      offsetof(struct xdp_sock, FIELD)); \
	} while (0)

	switch (si->off) {
	case offsetof(struct bpf_xdp_sock, queue_id):
		BPF_XDP_SOCK_GET(queue_id);
		break;
	}

	return insn - insn_buf;
}

static const struct bpf_func_proto bpf_skb_ecn_set_ce_proto = {
	.func           = bpf_skb_ecn_set_ce,
	.gpl_only       = false,
	.ret_type       = RET_INTEGER,
	.arg1_type      = ARG_PTR_TO_CTX,
};

BPF_CALL_5(bpf_tcp_check_syncookie, struct sock *, sk, void *, iph, u32, iph_len,
	   struct tcphdr *, th, u32, th_len)
{
#ifdef CONFIG_SYN_COOKIES
	u32 cookie;
	int ret;

	if (unlikely(!sk || th_len < sizeof(*th)))
		return -EINVAL;

	/* sk_listener() allows TCP_NEW_SYN_RECV, which makes no sense here. */
	if (sk->sk_protocol != IPPROTO_TCP || sk->sk_state != TCP_LISTEN)
		return -EINVAL;

	if (!READ_ONCE(sock_net(sk)->ipv4.sysctl_tcp_syncookies))
		return -EINVAL;

	if (!th->ack || th->rst || th->syn)
		return -ENOENT;

	if (unlikely(iph_len < sizeof(struct iphdr)))
		return -EINVAL;

	if (tcp_synq_no_recent_overflow(sk))
		return -ENOENT;

	cookie = ntohl(th->ack_seq) - 1;

	/* Both struct iphdr and struct ipv6hdr have the version field at the
	 * same offset so we can cast to the shorter header (struct iphdr).
	 */
	switch (((struct iphdr *)iph)->version) {
	case 4:
		if (sk->sk_family == AF_INET6 && ipv6_only_sock(sk))
			return -EINVAL;

		ret = __cookie_v4_check((struct iphdr *)iph, th, cookie);
		break;

#if IS_BUILTIN(CONFIG_IPV6)
	case 6:
		if (unlikely(iph_len < sizeof(struct ipv6hdr)))
			return -EINVAL;

		if (sk->sk_family != AF_INET6)
			return -EINVAL;

		ret = __cookie_v6_check((struct ipv6hdr *)iph, th, cookie);
		break;
#endif /* CONFIG_IPV6 */

	default:
		return -EPROTONOSUPPORT;
	}

	if (ret > 0)
		return 0;

	return -ENOENT;
#else
	return -ENOTSUPP;
#endif
}

static const struct bpf_func_proto bpf_tcp_check_syncookie_proto = {
	.func		= bpf_tcp_check_syncookie,
	.gpl_only	= true,
	.pkt_access	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_BTF_ID_SOCK_COMMON,
	.arg2_type	= ARG_PTR_TO_MEM,
	.arg3_type	= ARG_CONST_SIZE,
	.arg4_type	= ARG_PTR_TO_MEM,
	.arg5_type	= ARG_CONST_SIZE,
};

BPF_CALL_5(bpf_tcp_gen_syncookie, struct sock *, sk, void *, iph, u32, iph_len,
	   struct tcphdr *, th, u32, th_len)
{
#ifdef CONFIG_SYN_COOKIES
	u32 cookie;
	u16 mss;

	if (unlikely(!sk || th_len < sizeof(*th) || th_len != th->doff * 4))
		return -EINVAL;

	if (sk->sk_protocol != IPPROTO_TCP || sk->sk_state != TCP_LISTEN)
		return -EINVAL;

	if (!READ_ONCE(sock_net(sk)->ipv4.sysctl_tcp_syncookies))
		return -ENOENT;

	if (!th->syn || th->ack || th->fin || th->rst)
		return -EINVAL;

	if (unlikely(iph_len < sizeof(struct iphdr)))
		return -EINVAL;

	/* Both struct iphdr and struct ipv6hdr have the version field at the
	 * same offset so we can cast to the shorter header (struct iphdr).
	 */
	switch (((struct iphdr *)iph)->version) {
	case 4:
		if (sk->sk_family == AF_INET6 && sk->sk_ipv6only)
			return -EINVAL;

		mss = tcp_v4_get_syncookie(sk, iph, th, &cookie);
		break;

#if IS_BUILTIN(CONFIG_IPV6)
	case 6:
		if (unlikely(iph_len < sizeof(struct ipv6hdr)))
			return -EINVAL;

		if (sk->sk_family != AF_INET6)
			return -EINVAL;

		mss = tcp_v6_get_syncookie(sk, iph, th, &cookie);
		break;
#endif /* CONFIG_IPV6 */

	default:
		return -EPROTONOSUPPORT;
	}
	if (mss == 0)
		return -ENOENT;

	return cookie | ((u64)mss << 32);
#else
	return -EOPNOTSUPP;
#endif /* CONFIG_SYN_COOKIES */
}

static const struct bpf_func_proto bpf_tcp_gen_syncookie_proto = {
	.func		= bpf_tcp_gen_syncookie,
	.gpl_only	= true, /* __cookie_v*_init_sequence() is GPL */
	.pkt_access	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_BTF_ID_SOCK_COMMON,
	.arg2_type	= ARG_PTR_TO_MEM,
	.arg3_type	= ARG_CONST_SIZE,
	.arg4_type	= ARG_PTR_TO_MEM,
	.arg5_type	= ARG_CONST_SIZE,
};

BPF_CALL_3(bpf_sk_assign, struct sk_buff *, skb, struct sock *, sk, u64, flags)
{
	if (!sk || flags != 0)
		return -EINVAL;
	if (!skb_at_tc_ingress(skb))
		return -EOPNOTSUPP;
	if (unlikely(dev_net(skb->dev) != sock_net(sk)))
		return -ENETUNREACH;
	if (unlikely(sk_fullsock(sk) && sk->sk_reuseport))
		return -ESOCKTNOSUPPORT;
	if (sk_is_refcounted(sk) &&
	    unlikely(!refcount_inc_not_zero(&sk->sk_refcnt)))
		return -ENOENT;

	skb_orphan(skb);
	skb->sk = sk;
	skb->destructor = sock_pfree;

	return 0;
}

static const struct bpf_func_proto bpf_sk_assign_proto = {
	.func		= bpf_sk_assign,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type      = ARG_PTR_TO_CTX,
	.arg2_type      = ARG_PTR_TO_BTF_ID_SOCK_COMMON,
	.arg3_type	= ARG_ANYTHING,
};

static const u8 *bpf_search_tcp_opt(const u8 *op, const u8 *opend,
				    u8 search_kind, const u8 *magic,
				    u8 magic_len, bool *eol)
{
	u8 kind, kind_len;

	*eol = false;

	while (op < opend) {
		kind = op[0];

		if (kind == TCPOPT_EOL) {
			*eol = true;
			return ERR_PTR(-ENOMSG);
		} else if (kind == TCPOPT_NOP) {
			op++;
			continue;
		}

		if (opend - op < 2 || opend - op < op[1] || op[1] < 2)
			/* Something is wrong in the received header.
			 * Follow the TCP stack's tcp_parse_options()
			 * and just bail here.
			 */
			return ERR_PTR(-EFAULT);

		kind_len = op[1];
		if (search_kind == kind) {
			if (!magic_len)
				return op;

			if (magic_len > kind_len - 2)
				return ERR_PTR(-ENOMSG);

			if (!memcmp(&op[2], magic, magic_len))
				return op;
		}

		op += kind_len;
	}

	return ERR_PTR(-ENOMSG);
}

BPF_CALL_4(bpf_sock_ops_load_hdr_opt, struct bpf_sock_ops_kern *, bpf_sock,
	   void *, search_res, u32, len, u64, flags)
{
	bool eol, load_syn = flags & BPF_LOAD_HDR_OPT_TCP_SYN;
	const u8 *op, *opend, *magic, *search = search_res;
	u8 search_kind, search_len, copy_len, magic_len;
	int ret;

	/* 2 byte is the minimal option len except TCPOPT_NOP and
	 * TCPOPT_EOL which are useless for the bpf prog to learn
	 * and this helper disallow loading them also.
	 */
	if (len < 2 || flags & ~BPF_LOAD_HDR_OPT_TCP_SYN)
		return -EINVAL;

	search_kind = search[0];
	search_len = search[1];

	if (search_len > len || search_kind == TCPOPT_NOP ||
	    search_kind == TCPOPT_EOL)
		return -EINVAL;

	if (search_kind == TCPOPT_EXP || search_kind == 253) {
		/* 16 or 32 bit magic.  +2 for kind and kind length */
		if (search_len != 4 && search_len != 6)
			return -EINVAL;
		magic = &search[2];
		magic_len = search_len - 2;
	} else {
		if (search_len)
			return -EINVAL;
		magic = NULL;
		magic_len = 0;
	}

	if (load_syn) {
		ret = bpf_sock_ops_get_syn(bpf_sock, TCP_BPF_SYN, &op);
		if (ret < 0)
			return ret;

		opend = op + ret;
		op += sizeof(struct tcphdr);
	} else {
		if (!bpf_sock->skb ||
		    bpf_sock->op == BPF_SOCK_OPS_HDR_OPT_LEN_CB)
			/* This bpf_sock->op cannot call this helper */
			return -EPERM;

		opend = bpf_sock->skb_data_end;
		op = bpf_sock->skb->data + sizeof(struct tcphdr);
	}

	op = bpf_search_tcp_opt(op, opend, search_kind, magic, magic_len,
				&eol);
	if (IS_ERR(op))
		return PTR_ERR(op);

	copy_len = op[1];
	ret = copy_len;
	if (copy_len > len) {
		ret = -ENOSPC;
		copy_len = len;
	}

	memcpy(search_res, op, copy_len);
	return ret;
}

static const struct bpf_func_proto bpf_sock_ops_load_hdr_opt_proto = {
	.func		= bpf_sock_ops_load_hdr_opt,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_PTR_TO_MEM,
	.arg3_type	= ARG_CONST_SIZE,
	.arg4_type	= ARG_ANYTHING,
};

BPF_CALL_4(bpf_sock_ops_store_hdr_opt, struct bpf_sock_ops_kern *, bpf_sock,
	   const void *, from, u32, len, u64, flags)
{
	u8 new_kind, new_kind_len, magic_len = 0, *opend;
	const u8 *op, *new_op, *magic = NULL;
	struct sk_buff *skb;
	bool eol;

	if (bpf_sock->op != BPF_SOCK_OPS_WRITE_HDR_OPT_CB)
		return -EPERM;

	if (len < 2 || flags)
		return -EINVAL;

	new_op = from;
	new_kind = new_op[0];
	new_kind_len = new_op[1];

	if (new_kind_len > len || new_kind == TCPOPT_NOP ||
	    new_kind == TCPOPT_EOL)
		return -EINVAL;

	if (new_kind_len > bpf_sock->remaining_opt_len)
		return -ENOSPC;

	/* 253 is another experimental kind */
	if (new_kind == TCPOPT_EXP || new_kind == 253)  {
		if (new_kind_len < 4)
			return -EINVAL;
		/* Match for the 2 byte magic also.
		 * RFC 6994: the magic could be 2 or 4 bytes.
		 * Hence, matching by 2 byte only is on the
		 * conservative side but it is the right
		 * thing to do for the 'search-for-duplication'
		 * purpose.
		 */
		magic = &new_op[2];
		magic_len = 2;
	}

	/* Check for duplication */
	skb = bpf_sock->skb;
	op = skb->data + sizeof(struct tcphdr);
	opend = bpf_sock->skb_data_end;

	op = bpf_search_tcp_opt(op, opend, new_kind, magic, magic_len,
				&eol);
	if (!IS_ERR(op))
		return -EEXIST;

	if (PTR_ERR(op) != -ENOMSG)
		return PTR_ERR(op);

	if (eol)
		/* The option has been ended.  Treat it as no more
		 * header option can be written.
		 */
		return -ENOSPC;

	/* No duplication found.  Store the header option. */
	memcpy(opend, from, new_kind_len);

	bpf_sock->remaining_opt_len -= new_kind_len;
	bpf_sock->skb_data_end += new_kind_len;

	return 0;
}

static const struct bpf_func_proto bpf_sock_ops_store_hdr_opt_proto = {
	.func		= bpf_sock_ops_store_hdr_opt,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_PTR_TO_MEM,
	.arg3_type	= ARG_CONST_SIZE,
	.arg4_type	= ARG_ANYTHING,
};

BPF_CALL_3(bpf_sock_ops_reserve_hdr_opt, struct bpf_sock_ops_kern *, bpf_sock,
	   u32, len, u64, flags)
{
	if (bpf_sock->op != BPF_SOCK_OPS_HDR_OPT_LEN_CB)
		return -EPERM;

	if (flags || len < 2)
		return -EINVAL;

	if (len > bpf_sock->remaining_opt_len)
		return -ENOSPC;

	bpf_sock->remaining_opt_len -= len;

	return 0;
}

static const struct bpf_func_proto bpf_sock_ops_reserve_hdr_opt_proto = {
	.func		= bpf_sock_ops_reserve_hdr_opt,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
	.arg3_type	= ARG_ANYTHING,
};

#endif /* CONFIG_INET */

bool bpf_helper_changes_pkt_data(void *func)
{
	if (func == bpf_skb_vlan_push ||
	    func == bpf_skb_vlan_pop ||
	    func == bpf_skb_store_bytes ||
	    func == bpf_skb_change_proto ||
	    func == bpf_skb_change_head ||
	    func == sk_skb_change_head ||
	    func == bpf_skb_change_tail ||
	    func == sk_skb_change_tail ||
	    func == bpf_skb_adjust_room ||
	    func == sk_skb_adjust_room ||
	    func == bpf_skb_pull_data ||
	    func == sk_skb_pull_data ||
	    func == bpf_clone_redirect ||
	    func == bpf_l3_csum_replace ||
	    func == bpf_l4_csum_replace ||
	    func == bpf_xdp_adjust_head ||
	    func == bpf_xdp_adjust_meta ||
	    func == bpf_msg_pull_data ||
	    func == bpf_msg_push_data ||
	    func == bpf_msg_pop_data ||
	    func == bpf_xdp_adjust_tail ||
#if IS_ENABLED(CONFIG_IPV6_SEG6_BPF)
	    func == bpf_lwt_seg6_store_bytes ||
	    func == bpf_lwt_seg6_adjust_srh ||
	    func == bpf_lwt_seg6_action ||
#endif
#ifdef CONFIG_INET
	    func == bpf_sock_ops_store_hdr_opt ||
#endif
	    func == bpf_lwt_in_push_encap ||
	    func == bpf_lwt_xmit_push_encap)
		return true;

	return false;
}

const struct bpf_func_proto bpf_event_output_data_proto __weak;
const struct bpf_func_proto bpf_sk_storage_get_cg_sock_proto __weak;

static const struct bpf_func_proto *
sock_filter_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	switch (func_id) {
	/* inet and inet6 sockets are created in a process
	 * context so there is always a valid uid/gid
	 */
	case BPF_FUNC_get_current_uid_gid:
		return &bpf_get_current_uid_gid_proto;
	case BPF_FUNC_get_local_storage:
		return &bpf_get_local_storage_proto;
	case BPF_FUNC_get_socket_cookie:
		return &bpf_get_socket_cookie_sock_proto;
	case BPF_FUNC_get_netns_cookie:
		return &bpf_get_netns_cookie_sock_proto;
	case BPF_FUNC_perf_event_output:
		return &bpf_event_output_data_proto;
	case BPF_FUNC_get_current_pid_tgid:
		return &bpf_get_current_pid_tgid_proto;
	case BPF_FUNC_get_current_comm:
		return &bpf_get_current_comm_proto;
#ifdef CONFIG_CGROUPS
	case BPF_FUNC_get_current_cgroup_id:
		return &bpf_get_current_cgroup_id_proto;
	case BPF_FUNC_get_current_ancestor_cgroup_id:
		return &bpf_get_current_ancestor_cgroup_id_proto;
#endif
#ifdef CONFIG_CGROUP_NET_CLASSID
	case BPF_FUNC_get_cgroup_classid:
		return &bpf_get_cgroup_classid_curr_proto;
#endif
	case BPF_FUNC_sk_storage_get:
		return &bpf_sk_storage_get_cg_sock_proto;
	default:
		return bpf_base_func_proto(func_id);
	}
}

static const struct bpf_func_proto *
sock_addr_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	switch (func_id) {
	/* inet and inet6 sockets are created in a process
	 * context so there is always a valid uid/gid
	 */
	case BPF_FUNC_get_current_uid_gid:
		return &bpf_get_current_uid_gid_proto;
	case BPF_FUNC_bind:
		switch (prog->expected_attach_type) {
		case BPF_CGROUP_INET4_CONNECT:
		case BPF_CGROUP_INET6_CONNECT:
			return &bpf_bind_proto;
		default:
			return NULL;
		}
	case BPF_FUNC_get_socket_cookie:
		return &bpf_get_socket_cookie_sock_addr_proto;
	case BPF_FUNC_get_netns_cookie:
		return &bpf_get_netns_cookie_sock_addr_proto;
	case BPF_FUNC_get_local_storage:
		return &bpf_get_local_storage_proto;
	case BPF_FUNC_perf_event_output:
		return &bpf_event_output_data_proto;
	case BPF_FUNC_get_current_pid_tgid:
		return &bpf_get_current_pid_tgid_proto;
	case BPF_FUNC_get_current_comm:
		return &bpf_get_current_comm_proto;
#ifdef CONFIG_CGROUPS
	case BPF_FUNC_get_current_cgroup_id:
		return &bpf_get_current_cgroup_id_proto;
	case BPF_FUNC_get_current_ancestor_cgroup_id:
		return &bpf_get_current_ancestor_cgroup_id_proto;
#endif
#ifdef CONFIG_CGROUP_NET_CLASSID
	case BPF_FUNC_get_cgroup_classid:
		return &bpf_get_cgroup_classid_curr_proto;
#endif
#ifdef CONFIG_INET
	case BPF_FUNC_sk_lookup_tcp:
		return &bpf_sock_addr_sk_lookup_tcp_proto;
	case BPF_FUNC_sk_lookup_udp:
		return &bpf_sock_addr_sk_lookup_udp_proto;
	case BPF_FUNC_sk_release:
		return &bpf_sk_release_proto;
	case BPF_FUNC_skc_lookup_tcp:
		return &bpf_sock_addr_skc_lookup_tcp_proto;
#endif /* CONFIG_INET */
	case BPF_FUNC_sk_storage_get:
		return &bpf_sk_storage_get_proto;
	case BPF_FUNC_sk_storage_delete:
		return &bpf_sk_storage_delete_proto;
	case BPF_FUNC_setsockopt:
		switch (prog->expected_attach_type) {
		case BPF_CGROUP_INET4_CONNECT:
		case BPF_CGROUP_INET6_CONNECT:
			return &bpf_sock_addr_setsockopt_proto;
		default:
			return NULL;
		}
	case BPF_FUNC_getsockopt:
		switch (prog->expected_attach_type) {
		case BPF_CGROUP_INET4_CONNECT:
		case BPF_CGROUP_INET6_CONNECT:
			return &bpf_sock_addr_getsockopt_proto;
		default:
			return NULL;
		}
	default:
		return bpf_sk_base_func_proto(func_id);
	}
}

static const struct bpf_func_proto *
sk_filter_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	switch (func_id) {
	case BPF_FUNC_skb_load_bytes:
		return &bpf_skb_load_bytes_proto;
	case BPF_FUNC_skb_load_bytes_relative:
		return &bpf_skb_load_bytes_relative_proto;
	case BPF_FUNC_get_socket_cookie:
		return &bpf_get_socket_cookie_proto;
	case BPF_FUNC_get_socket_uid:
		return &bpf_get_socket_uid_proto;
	case BPF_FUNC_perf_event_output:
		return &bpf_skb_event_output_proto;
	default:
		return bpf_sk_base_func_proto(func_id);
	}
}

const struct bpf_func_proto bpf_sk_storage_get_proto __weak;
const struct bpf_func_proto bpf_sk_storage_delete_proto __weak;

static const struct bpf_func_proto *
cg_skb_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	switch (func_id) {
	case BPF_FUNC_get_local_storage:
		return &bpf_get_local_storage_proto;
	case BPF_FUNC_sk_fullsock:
		return &bpf_sk_fullsock_proto;
	case BPF_FUNC_sk_storage_get:
		return &bpf_sk_storage_get_proto;
	case BPF_FUNC_sk_storage_delete:
		return &bpf_sk_storage_delete_proto;
	case BPF_FUNC_perf_event_output:
		return &bpf_skb_event_output_proto;
#ifdef CONFIG_SOCK_CGROUP_DATA
	case BPF_FUNC_skb_cgroup_id:
		return &bpf_skb_cgroup_id_proto;
	case BPF_FUNC_skb_ancestor_cgroup_id:
		return &bpf_skb_ancestor_cgroup_id_proto;
	case BPF_FUNC_sk_cgroup_id:
		return &bpf_sk_cgroup_id_proto;
	case BPF_FUNC_sk_ancestor_cgroup_id:
		return &bpf_sk_ancestor_cgroup_id_proto;
#endif
#ifdef CONFIG_INET
	case BPF_FUNC_sk_lookup_tcp:
		return &bpf_sk_lookup_tcp_proto;
	case BPF_FUNC_sk_lookup_udp:
		return &bpf_sk_lookup_udp_proto;
	case BPF_FUNC_sk_release:
		return &bpf_sk_release_proto;
	case BPF_FUNC_skc_lookup_tcp:
		return &bpf_skc_lookup_tcp_proto;
	case BPF_FUNC_tcp_sock:
		return &bpf_tcp_sock_proto;
	case BPF_FUNC_get_listener_sock:
		return &bpf_get_listener_sock_proto;
	case BPF_FUNC_skb_ecn_set_ce:
		return &bpf_skb_ecn_set_ce_proto;
#endif
	default:
		return sk_filter_func_proto(func_id, prog);
	}
}

static const struct bpf_func_proto *
tc_cls_act_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	switch (func_id) {
	case BPF_FUNC_skb_store_bytes:
		return &bpf_skb_store_bytes_proto;
	case BPF_FUNC_skb_load_bytes:
		return &bpf_skb_load_bytes_proto;
	case BPF_FUNC_skb_load_bytes_relative:
		return &bpf_skb_load_bytes_relative_proto;
	case BPF_FUNC_skb_pull_data:
		return &bpf_skb_pull_data_proto;
	case BPF_FUNC_csum_diff:
		return &bpf_csum_diff_proto;
	case BPF_FUNC_csum_update:
		return &bpf_csum_update_proto;
	case BPF_FUNC_csum_level:
		return &bpf_csum_level_proto;
	case BPF_FUNC_l3_csum_replace:
		return &bpf_l3_csum_replace_proto;
	case BPF_FUNC_l4_csum_replace:
		return &bpf_l4_csum_replace_proto;
	case BPF_FUNC_clone_redirect:
		return &bpf_clone_redirect_proto;
	case BPF_FUNC_get_cgroup_classid:
		return &bpf_get_cgroup_classid_proto;
	case BPF_FUNC_skb_vlan_push:
		return &bpf_skb_vlan_push_proto;
	case BPF_FUNC_skb_vlan_pop:
		return &bpf_skb_vlan_pop_proto;
	case BPF_FUNC_skb_change_proto:
		return &bpf_skb_change_proto_proto;
	case BPF_FUNC_skb_change_type:
		return &bpf_skb_change_type_proto;
	case BPF_FUNC_skb_adjust_room:
		return &bpf_skb_adjust_room_proto;
	case BPF_FUNC_skb_change_tail:
		return &bpf_skb_change_tail_proto;
	case BPF_FUNC_skb_change_head:
		return &bpf_skb_change_head_proto;
	case BPF_FUNC_skb_get_tunnel_key:
		return &bpf_skb_get_tunnel_key_proto;
	case BPF_FUNC_skb_set_tunnel_key:
		return bpf_get_skb_set_tunnel_proto(func_id);
	case BPF_FUNC_skb_get_tunnel_opt:
		return &bpf_skb_get_tunnel_opt_proto;
	case BPF_FUNC_skb_set_tunnel_opt:
		return bpf_get_skb_set_tunnel_proto(func_id);
	case BPF_FUNC_redirect:
		return &bpf_redirect_proto;
	case BPF_FUNC_redirect_neigh:
		return &bpf_redirect_neigh_proto;
	case BPF_FUNC_redirect_peer:
		return &bpf_redirect_peer_proto;
	case BPF_FUNC_get_route_realm:
		return &bpf_get_route_realm_proto;
	case BPF_FUNC_get_hash_recalc:
		return &bpf_get_hash_recalc_proto;
	case BPF_FUNC_set_hash_invalid:
		return &bpf_set_hash_invalid_proto;
	case BPF_FUNC_set_hash:
		return &bpf_set_hash_proto;
	case BPF_FUNC_perf_event_output:
		return &bpf_skb_event_output_proto;
	case BPF_FUNC_get_smp_processor_id:
		return &bpf_get_smp_processor_id_proto;
	case BPF_FUNC_skb_under_cgroup:
		return &bpf_skb_under_cgroup_proto;
	case BPF_FUNC_get_socket_cookie:
		return &bpf_get_socket_cookie_proto;
	case BPF_FUNC_get_socket_uid:
		return &bpf_get_socket_uid_proto;
	case BPF_FUNC_fib_lookup:
		return &bpf_skb_fib_lookup_proto;
	case BPF_FUNC_sk_fullsock:
		return &bpf_sk_fullsock_proto;
	case BPF_FUNC_sk_storage_get:
		return &bpf_sk_storage_get_proto;
	case BPF_FUNC_sk_storage_delete:
		return &bpf_sk_storage_delete_proto;
#ifdef CONFIG_XFRM
	case BPF_FUNC_skb_get_xfrm_state:
		return &bpf_skb_get_xfrm_state_proto;
#endif
#ifdef CONFIG_CGROUP_NET_CLASSID
	case BPF_FUNC_skb_cgroup_classid:
		return &bpf_skb_cgroup_classid_proto;
#endif
#ifdef CONFIG_SOCK_CGROUP_DATA
	case BPF_FUNC_skb_cgroup_id:
		return &bpf_skb_cgroup_id_proto;
	case BPF_FUNC_skb_ancestor_cgroup_id:
		return &bpf_skb_ancestor_cgroup_id_proto;
#endif
#ifdef CONFIG_INET
	case BPF_FUNC_sk_lookup_tcp:
		return &bpf_sk_lookup_tcp_proto;
	case BPF_FUNC_sk_lookup_udp:
		return &bpf_sk_lookup_udp_proto;
	case BPF_FUNC_sk_release:
		return &bpf_sk_release_proto;
	case BPF_FUNC_tcp_sock:
		return &bpf_tcp_sock_proto;
	case BPF_FUNC_get_listener_sock:
		return &bpf_get_listener_sock_proto;
	case BPF_FUNC_skc_lookup_tcp:
		return &bpf_skc_lookup_tcp_proto;
	case BPF_FUNC_tcp_check_syncookie:
		return &bpf_tcp_check_syncookie_proto;
	case BPF_FUNC_skb_ecn_set_ce:
		return &bpf_skb_ecn_set_ce_proto;
	case BPF_FUNC_tcp_gen_syncookie:
		return &bpf_tcp_gen_syncookie_proto;
	case BPF_FUNC_sk_assign:
		return &bpf_sk_assign_proto;
#endif
	default:
		return bpf_sk_base_func_proto(func_id);
	}
}

static const struct bpf_func_proto *
xdp_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	switch (func_id) {
	case BPF_FUNC_perf_event_output:
		return &bpf_xdp_event_output_proto;
	case BPF_FUNC_get_smp_processor_id:
		return &bpf_get_smp_processor_id_proto;
	case BPF_FUNC_csum_diff:
		return &bpf_csum_diff_proto;
	case BPF_FUNC_xdp_adjust_head:
		return &bpf_xdp_adjust_head_proto;
	case BPF_FUNC_xdp_adjust_meta:
		return &bpf_xdp_adjust_meta_proto;
	case BPF_FUNC_redirect:
		return &bpf_xdp_redirect_proto;
	case BPF_FUNC_redirect_map:
		return &bpf_xdp_redirect_map_proto;
	case BPF_FUNC_xdp_adjust_tail:
		return &bpf_xdp_adjust_tail_proto;
	case BPF_FUNC_fib_lookup:
		return &bpf_xdp_fib_lookup_proto;
#ifdef CONFIG_INET
	case BPF_FUNC_sk_lookup_udp:
		return &bpf_xdp_sk_lookup_udp_proto;
	case BPF_FUNC_sk_lookup_tcp:
		return &bpf_xdp_sk_lookup_tcp_proto;
	case BPF_FUNC_sk_release:
		return &bpf_sk_release_proto;
	case BPF_FUNC_skc_lookup_tcp:
		return &bpf_xdp_skc_lookup_tcp_proto;
	case BPF_FUNC_tcp_check_syncookie:
		return &bpf_tcp_check_syncookie_proto;
	case BPF_FUNC_tcp_gen_syncookie:
		return &bpf_tcp_gen_syncookie_proto;
#endif
	default:
		return bpf_sk_base_func_proto(func_id);
	}
}

const struct bpf_func_proto bpf_sock_map_update_proto __weak;
const struct bpf_func_proto bpf_sock_hash_update_proto __weak;

static const struct bpf_func_proto *
sock_ops_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	switch (func_id) {
	case BPF_FUNC_setsockopt:
		return &bpf_sock_ops_setsockopt_proto;
	case BPF_FUNC_getsockopt:
		return &bpf_sock_ops_getsockopt_proto;
	case BPF_FUNC_sock_ops_cb_flags_set:
		return &bpf_sock_ops_cb_flags_set_proto;
	case BPF_FUNC_sock_map_update:
		return &bpf_sock_map_update_proto;
	case BPF_FUNC_sock_hash_update:
		return &bpf_sock_hash_update_proto;
	case BPF_FUNC_get_socket_cookie:
		return &bpf_get_socket_cookie_sock_ops_proto;
	case BPF_FUNC_get_local_storage:
		return &bpf_get_local_storage_proto;
	case BPF_FUNC_perf_event_output:
		return &bpf_event_output_data_proto;
	case BPF_FUNC_sk_storage_get:
		return &bpf_sk_storage_get_proto;
	case BPF_FUNC_sk_storage_delete:
		return &bpf_sk_storage_delete_proto;
#ifdef CONFIG_INET
	case BPF_FUNC_load_hdr_opt:
		return &bpf_sock_ops_load_hdr_opt_proto;
	case BPF_FUNC_store_hdr_opt:
		return &bpf_sock_ops_store_hdr_opt_proto;
	case BPF_FUNC_reserve_hdr_opt:
		return &bpf_sock_ops_reserve_hdr_opt_proto;
	case BPF_FUNC_tcp_sock:
		return &bpf_tcp_sock_proto;
#endif /* CONFIG_INET */
	default:
		return bpf_sk_base_func_proto(func_id);
	}
}

const struct bpf_func_proto bpf_msg_redirect_map_proto __weak;
const struct bpf_func_proto bpf_msg_redirect_hash_proto __weak;

static const struct bpf_func_proto *
sk_msg_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	switch (func_id) {
	case BPF_FUNC_msg_redirect_map:
		return &bpf_msg_redirect_map_proto;
	case BPF_FUNC_msg_redirect_hash:
		return &bpf_msg_redirect_hash_proto;
	case BPF_FUNC_msg_apply_bytes:
		return &bpf_msg_apply_bytes_proto;
	case BPF_FUNC_msg_cork_bytes:
		return &bpf_msg_cork_bytes_proto;
	case BPF_FUNC_msg_pull_data:
		return &bpf_msg_pull_data_proto;
	case BPF_FUNC_msg_push_data:
		return &bpf_msg_push_data_proto;
	case BPF_FUNC_msg_pop_data:
		return &bpf_msg_pop_data_proto;
	case BPF_FUNC_perf_event_output:
		return &bpf_event_output_data_proto;
	case BPF_FUNC_get_current_uid_gid:
		return &bpf_get_current_uid_gid_proto;
	case BPF_FUNC_get_current_pid_tgid:
		return &bpf_get_current_pid_tgid_proto;
	case BPF_FUNC_sk_storage_get:
		return &bpf_sk_storage_get_proto;
	case BPF_FUNC_sk_storage_delete:
		return &bpf_sk_storage_delete_proto;
#ifdef CONFIG_CGROUPS
	case BPF_FUNC_get_current_cgroup_id:
		return &bpf_get_current_cgroup_id_proto;
	case BPF_FUNC_get_current_ancestor_cgroup_id:
		return &bpf_get_current_ancestor_cgroup_id_proto;
#endif
#ifdef CONFIG_CGROUP_NET_CLASSID
	case BPF_FUNC_get_cgroup_classid:
		return &bpf_get_cgroup_classid_curr_proto;
#endif
	default:
		return bpf_sk_base_func_proto(func_id);
	}
}

const struct bpf_func_proto bpf_sk_redirect_map_proto __weak;
const struct bpf_func_proto bpf_sk_redirect_hash_proto __weak;

static const struct bpf_func_proto *
sk_skb_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	switch (func_id) {
	case BPF_FUNC_skb_store_bytes:
		return &bpf_skb_store_bytes_proto;
	case BPF_FUNC_skb_load_bytes:
		return &bpf_skb_load_bytes_proto;
	case BPF_FUNC_skb_pull_data:
		return &sk_skb_pull_data_proto;
	case BPF_FUNC_skb_change_tail:
		return &sk_skb_change_tail_proto;
	case BPF_FUNC_skb_change_head:
		return &sk_skb_change_head_proto;
	case BPF_FUNC_skb_adjust_room:
		return &sk_skb_adjust_room_proto;
	case BPF_FUNC_get_socket_cookie:
		return &bpf_get_socket_cookie_proto;
	case BPF_FUNC_get_socket_uid:
		return &bpf_get_socket_uid_proto;
	case BPF_FUNC_sk_redirect_map:
		return &bpf_sk_redirect_map_proto;
	case BPF_FUNC_sk_redirect_hash:
		return &bpf_sk_redirect_hash_proto;
	case BPF_FUNC_perf_event_output:
		return &bpf_skb_event_output_proto;
#ifdef CONFIG_INET
	case BPF_FUNC_sk_lookup_tcp:
		return &bpf_sk_lookup_tcp_proto;
	case BPF_FUNC_sk_lookup_udp:
		return &bpf_sk_lookup_udp_proto;
	case BPF_FUNC_sk_release:
		return &bpf_sk_release_proto;
	case BPF_FUNC_skc_lookup_tcp:
		return &bpf_skc_lookup_tcp_proto;
#endif
	default:
		return bpf_sk_base_func_proto(func_id);
	}
}

static const struct bpf_func_proto *
flow_dissector_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	switch (func_id) {
	case BPF_FUNC_skb_load_bytes:
		return &bpf_flow_dissector_load_bytes_proto;
	default:
		return bpf_sk_base_func_proto(func_id);
	}
}

static const struct bpf_func_proto *
lwt_out_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	switch (func_id) {
	case BPF_FUNC_skb_load_bytes:
		return &bpf_skb_load_bytes_proto;
	case BPF_FUNC_skb_pull_data:
		return &bpf_skb_pull_data_proto;
	case BPF_FUNC_csum_diff:
		return &bpf_csum_diff_proto;
	case BPF_FUNC_get_cgroup_classid:
		return &bpf_get_cgroup_classid_proto;
	case BPF_FUNC_get_route_realm:
		return &bpf_get_route_realm_proto;
	case BPF_FUNC_get_hash_recalc:
		return &bpf_get_hash_recalc_proto;
	case BPF_FUNC_perf_event_output:
		return &bpf_skb_event_output_proto;
	case BPF_FUNC_get_smp_processor_id:
		return &bpf_get_smp_processor_id_proto;
	case BPF_FUNC_skb_under_cgroup:
		return &bpf_skb_under_cgroup_proto;
	default:
		return bpf_sk_base_func_proto(func_id);
	}
}

static const struct bpf_func_proto *
lwt_in_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	switch (func_id) {
	case BPF_FUNC_lwt_push_encap:
		return &bpf_lwt_in_push_encap_proto;
	default:
		return lwt_out_func_proto(func_id, prog);
	}
}

static const struct bpf_func_proto *
lwt_xmit_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	switch (func_id) {
	case BPF_FUNC_skb_get_tunnel_key:
		return &bpf_skb_get_tunnel_key_proto;
	case BPF_FUNC_skb_set_tunnel_key:
		return bpf_get_skb_set_tunnel_proto(func_id);
	case BPF_FUNC_skb_get_tunnel_opt:
		return &bpf_skb_get_tunnel_opt_proto;
	case BPF_FUNC_skb_set_tunnel_opt:
		return bpf_get_skb_set_tunnel_proto(func_id);
	case BPF_FUNC_redirect:
		return &bpf_redirect_proto;
	case BPF_FUNC_clone_redirect:
		return &bpf_clone_redirect_proto;
	case BPF_FUNC_skb_change_tail:
		return &bpf_skb_change_tail_proto;
	case BPF_FUNC_skb_change_head:
		return &bpf_skb_change_head_proto;
	case BPF_FUNC_skb_store_bytes:
		return &bpf_skb_store_bytes_proto;
	case BPF_FUNC_csum_update:
		return &bpf_csum_update_proto;
	case BPF_FUNC_csum_level:
		return &bpf_csum_level_proto;
	case BPF_FUNC_l3_csum_replace:
		return &bpf_l3_csum_replace_proto;
	case BPF_FUNC_l4_csum_replace:
		return &bpf_l4_csum_replace_proto;
	case BPF_FUNC_set_hash_invalid:
		return &bpf_set_hash_invalid_proto;
	case BPF_FUNC_lwt_push_encap:
		return &bpf_lwt_xmit_push_encap_proto;
	default:
		return lwt_out_func_proto(func_id, prog);
	}
}

static const struct bpf_func_proto *
lwt_seg6local_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	switch (func_id) {
#if IS_ENABLED(CONFIG_IPV6_SEG6_BPF)
	case BPF_FUNC_lwt_seg6_store_bytes:
		return &bpf_lwt_seg6_store_bytes_proto;
	case BPF_FUNC_lwt_seg6_action:
		return &bpf_lwt_seg6_action_proto;
	case BPF_FUNC_lwt_seg6_adjust_srh:
		return &bpf_lwt_seg6_adjust_srh_proto;
#endif
	default:
		return lwt_out_func_proto(func_id, prog);
	}
}

static bool bpf_skb_is_valid_access(int off, int size, enum bpf_access_type type,
				    const struct bpf_prog *prog,
				    struct bpf_insn_access_aux *info)
{
	const int size_default = sizeof(__u32);

	if (off < 0 || off >= sizeof(struct __sk_buff))
		return false;

	/* The verifier guarantees that size > 0. */
	if (off % size != 0)
		return false;

	switch (off) {
	case bpf_ctx_range_till(struct __sk_buff, cb[0], cb[4]):
		if (off + size > offsetofend(struct __sk_buff, cb[4]))
			return false;
		break;
	case bpf_ctx_range_till(struct __sk_buff, remote_ip6[0], remote_ip6[3]):
	case bpf_ctx_range_till(struct __sk_buff, local_ip6[0], local_ip6[3]):
	case bpf_ctx_range_till(struct __sk_buff, remote_ip4, remote_ip4):
	case bpf_ctx_range_till(struct __sk_buff, local_ip4, local_ip4):
	case bpf_ctx_range(struct __sk_buff, data):
	case bpf_ctx_range(struct __sk_buff, data_meta):
	case bpf_ctx_range(struct __sk_buff, data_end):
		if (size != size_default)
			return false;
		break;
	case bpf_ctx_range_ptr(struct __sk_buff, flow_keys):
		return false;
	case bpf_ctx_range(struct __sk_buff, tstamp):
		if (size != sizeof(__u64))
			return false;
		break;
	case offsetof(struct __sk_buff, sk):
		if (type == BPF_WRITE || size != sizeof(__u64))
			return false;
		info->reg_type = PTR_TO_SOCK_COMMON_OR_NULL;
		break;
	default:
		/* Only narrow read access allowed for now. */
		if (type == BPF_WRITE) {
			if (size != size_default)
				return false;
		} else {
			bpf_ctx_record_field_size(info, size_default);
			if (!bpf_ctx_narrow_access_ok(off, size, size_default))
				return false;
		}
	}

	return true;
}

static bool sk_filter_is_valid_access(int off, int size,
				      enum bpf_access_type type,
				      const struct bpf_prog *prog,
				      struct bpf_insn_access_aux *info)
{
	switch (off) {
	case bpf_ctx_range(struct __sk_buff, tc_classid):
	case bpf_ctx_range(struct __sk_buff, data):
	case bpf_ctx_range(struct __sk_buff, data_meta):
	case bpf_ctx_range(struct __sk_buff, data_end):
	case bpf_ctx_range_till(struct __sk_buff, family, local_port):
	case bpf_ctx_range(struct __sk_buff, tstamp):
	case bpf_ctx_range(struct __sk_buff, wire_len):
		return false;
	}

	if (type == BPF_WRITE) {
		switch (off) {
		case bpf_ctx_range_till(struct __sk_buff, cb[0], cb[4]):
			break;
		default:
			return false;
		}
	}

	return bpf_skb_is_valid_access(off, size, type, prog, info);
}

static bool cg_skb_is_valid_access(int off, int size,
				   enum bpf_access_type type,
				   const struct bpf_prog *prog,
				   struct bpf_insn_access_aux *info)
{
	switch (off) {
	case bpf_ctx_range(struct __sk_buff, tc_classid):
	case bpf_ctx_range(struct __sk_buff, data_meta):
	case bpf_ctx_range(struct __sk_buff, wire_len):
		return false;
	case bpf_ctx_range(struct __sk_buff, data):
	case bpf_ctx_range(struct __sk_buff, data_end):
		if (!bpf_capable())
			return false;
		break;
	}

	if (type == BPF_WRITE) {
		switch (off) {
		case bpf_ctx_range(struct __sk_buff, mark):
		case bpf_ctx_range(struct __sk_buff, priority):
		case bpf_ctx_range_till(struct __sk_buff, cb[0], cb[4]):
			break;
		case bpf_ctx_range(struct __sk_buff, tstamp):
			if (!bpf_capable())
				return false;
			break;
		default:
			return false;
		}
	}

	switch (off) {
	case bpf_ctx_range(struct __sk_buff, data):
		info->reg_type = PTR_TO_PACKET;
		break;
	case bpf_ctx_range(struct __sk_buff, data_end):
		info->reg_type = PTR_TO_PACKET_END;
		break;
	}

	return bpf_skb_is_valid_access(off, size, type, prog, info);
}

static bool lwt_is_valid_access(int off, int size,
				enum bpf_access_type type,
				const struct bpf_prog *prog,
				struct bpf_insn_access_aux *info)
{
	switch (off) {
	case bpf_ctx_range(struct __sk_buff, tc_classid):
	case bpf_ctx_range_till(struct __sk_buff, family, local_port):
	case bpf_ctx_range(struct __sk_buff, data_meta):
	case bpf_ctx_range(struct __sk_buff, tstamp):
	case bpf_ctx_range(struct __sk_buff, wire_len):
		return false;
	}

	if (type == BPF_WRITE) {
		switch (off) {
		case bpf_ctx_range(struct __sk_buff, mark):
		case bpf_ctx_range(struct __sk_buff, priority):
		case bpf_ctx_range_till(struct __sk_buff, cb[0], cb[4]):
			break;
		default:
			return false;
		}
	}

	switch (off) {
	case bpf_ctx_range(struct __sk_buff, data):
		info->reg_type = PTR_TO_PACKET;
		break;
	case bpf_ctx_range(struct __sk_buff, data_end):
		info->reg_type = PTR_TO_PACKET_END;
		break;
	}

	return bpf_skb_is_valid_access(off, size, type, prog, info);
}

/* Attach type specific accesses */
static bool __sock_filter_check_attach_type(int off,
					    enum bpf_access_type access_type,
					    enum bpf_attach_type attach_type)
{
	switch (off) {
	case offsetof(struct bpf_sock, bound_dev_if):
	case offsetof(struct bpf_sock, mark):
	case offsetof(struct bpf_sock, priority):
		switch (attach_type) {
		case BPF_CGROUP_INET_SOCK_CREATE:
		case BPF_CGROUP_INET_SOCK_RELEASE:
			goto full_access;
		default:
			return false;
		}
	case bpf_ctx_range(struct bpf_sock, src_ip4):
		switch (attach_type) {
		case BPF_CGROUP_INET4_POST_BIND:
			goto read_only;
		default:
			return false;
		}
	case bpf_ctx_range_till(struct bpf_sock, src_ip6[0], src_ip6[3]):
		switch (attach_type) {
		case BPF_CGROUP_INET6_POST_BIND:
			goto read_only;
		default:
			return false;
		}
	case bpf_ctx_range(struct bpf_sock, src_port):
		switch (attach_type) {
		case BPF_CGROUP_INET4_POST_BIND:
		case BPF_CGROUP_INET6_POST_BIND:
			goto read_only;
		default:
			return false;
		}
	}
read_only:
	return access_type == BPF_READ;
full_access:
	return true;
}

bool bpf_sock_common_is_valid_access(int off, int size,
				     enum bpf_access_type type,
				     struct bpf_insn_access_aux *info)
{
	switch (off) {
	case bpf_ctx_range_till(struct bpf_sock, type, priority):
		return false;
	default:
		return bpf_sock_is_valid_access(off, size, type, info);
	}
}

bool bpf_sock_is_valid_access(int off, int size, enum bpf_access_type type,
			      struct bpf_insn_access_aux *info)
{
	const int size_default = sizeof(__u32);
	int field_size;

	if (off < 0 || off >= sizeof(struct bpf_sock))
		return false;
	if (off % size != 0)
		return false;

	switch (off) {
	case offsetof(struct bpf_sock, state):
	case offsetof(struct bpf_sock, family):
	case offsetof(struct bpf_sock, type):
	case offsetof(struct bpf_sock, protocol):
	case offsetof(struct bpf_sock, src_port):
	case offsetof(struct bpf_sock, rx_queue_mapping):
	case bpf_ctx_range(struct bpf_sock, src_ip4):
	case bpf_ctx_range_till(struct bpf_sock, src_ip6[0], src_ip6[3]):
	case bpf_ctx_range(struct bpf_sock, dst_ip4):
	case bpf_ctx_range_till(struct bpf_sock, dst_ip6[0], dst_ip6[3]):
		bpf_ctx_record_field_size(info, size_default);
		return bpf_ctx_narrow_access_ok(off, size, size_default);
	case bpf_ctx_range(struct bpf_sock, dst_port):
		field_size = size == size_default ?
			size_default : sizeof_field(struct bpf_sock, dst_port);
		bpf_ctx_record_field_size(info, field_size);
		return bpf_ctx_narrow_access_ok(off, size, field_size);
	case offsetofend(struct bpf_sock, dst_port) ...
	     offsetof(struct bpf_sock, dst_ip4) - 1:
		return false;
	}

	return size == size_default;
}

static bool sock_filter_is_valid_access(int off, int size,
					enum bpf_access_type type,
					const struct bpf_prog *prog,
					struct bpf_insn_access_aux *info)
{
	if (!bpf_sock_is_valid_access(off, size, type, info))
		return false;
	return __sock_filter_check_attach_type(off, type,
					       prog->expected_attach_type);
}

static int bpf_noop_prologue(struct bpf_insn *insn_buf, bool direct_write,
			     const struct bpf_prog *prog)
{
	/* Neither direct read nor direct write requires any preliminary
	 * action.
	 */
	return 0;
}

static int bpf_unclone_prologue(struct bpf_insn *insn_buf, bool direct_write,
				const struct bpf_prog *prog, int drop_verdict)
{
	struct bpf_insn *insn = insn_buf;

	if (!direct_write)
		return 0;

	/* if (!skb->cloned)
	 *       goto start;
	 *
	 * (Fast-path, otherwise approximation that we might be
	 *  a clone, do the rest in helper.)
	 */
	*insn++ = BPF_LDX_MEM(BPF_B, BPF_REG_6, BPF_REG_1, CLONED_OFFSET());
	*insn++ = BPF_ALU32_IMM(BPF_AND, BPF_REG_6, CLONED_MASK);
	*insn++ = BPF_JMP_IMM(BPF_JEQ, BPF_REG_6, 0, 7);

	/* ret = bpf_skb_pull_data(skb, 0); */
	*insn++ = BPF_MOV64_REG(BPF_REG_6, BPF_REG_1);
	*insn++ = BPF_ALU64_REG(BPF_XOR, BPF_REG_2, BPF_REG_2);
	*insn++ = BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
			       BPF_FUNC_skb_pull_data);
	/* if (!ret)
	 *      goto restore;
	 * return TC_ACT_SHOT;
	 */
	*insn++ = BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 2);
	*insn++ = BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, drop_verdict);
	*insn++ = BPF_EXIT_INSN();

	/* restore: */
	*insn++ = BPF_MOV64_REG(BPF_REG_1, BPF_REG_6);
	/* start: */
	*insn++ = prog->insnsi[0];

	return insn - insn_buf;
}

static int bpf_gen_ld_abs(const struct bpf_insn *orig,
			  struct bpf_insn *insn_buf)
{
	bool indirect = BPF_MODE(orig->code) == BPF_IND;
	struct bpf_insn *insn = insn_buf;

	if (!indirect) {
		*insn++ = BPF_MOV64_IMM(BPF_REG_2, orig->imm);
	} else {
		*insn++ = BPF_MOV64_REG(BPF_REG_2, orig->src_reg);
		if (orig->imm)
			*insn++ = BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, orig->imm);
	}
	/* We're guaranteed here that CTX is in R6. */
	*insn++ = BPF_MOV64_REG(BPF_REG_1, BPF_REG_CTX);

	switch (BPF_SIZE(orig->code)) {
	case BPF_B:
		*insn++ = BPF_EMIT_CALL(bpf_skb_load_helper_8_no_cache);
		break;
	case BPF_H:
		*insn++ = BPF_EMIT_CALL(bpf_skb_load_helper_16_no_cache);
		break;
	case BPF_W:
		*insn++ = BPF_EMIT_CALL(bpf_skb_load_helper_32_no_cache);
		break;
	}

	*insn++ = BPF_JMP_IMM(BPF_JSGE, BPF_REG_0, 0, 2);
	*insn++ = BPF_ALU32_REG(BPF_XOR, BPF_REG_0, BPF_REG_0);
	*insn++ = BPF_EXIT_INSN();

	return insn - insn_buf;
}

static int tc_cls_act_prologue(struct bpf_insn *insn_buf, bool direct_write,
			       const struct bpf_prog *prog)
{
	return bpf_unclone_prologue(insn_buf, direct_write, prog, TC_ACT_SHOT);
}

static bool tc_cls_act_is_valid_access(int off, int size,
				       enum bpf_access_type type,
				       const struct bpf_prog *prog,
				       struct bpf_insn_access_aux *info)
{
	if (type == BPF_WRITE) {
		switch (off) {
		case bpf_ctx_range(struct __sk_buff, mark):
		case bpf_ctx_range(struct __sk_buff, tc_index):
		case bpf_ctx_range(struct __sk_buff, priority):
		case bpf_ctx_range(struct __sk_buff, tc_classid):
		case bpf_ctx_range_till(struct __sk_buff, cb[0], cb[4]):
		case bpf_ctx_range(struct __sk_buff, tstamp):
		case bpf_ctx_range(struct __sk_buff, queue_mapping):
			break;
		default:
			return false;
		}
	}

	switch (off) {
	case bpf_ctx_range(struct __sk_buff, data):
		info->reg_type = PTR_TO_PACKET;
		break;
	case bpf_ctx_range(struct __sk_buff, data_meta):
		info->reg_type = PTR_TO_PACKET_META;
		break;
	case bpf_ctx_range(struct __sk_buff, data_end):
		info->reg_type = PTR_TO_PACKET_END;
		break;
	case bpf_ctx_range_till(struct __sk_buff, family, local_port):
		return false;
	}

	return bpf_skb_is_valid_access(off, size, type, prog, info);
}

static bool __is_valid_xdp_access(int off, int size)
{
	if (off < 0 || off >= sizeof(struct xdp_md))
		return false;
	if (off % size != 0)
		return false;
	if (size != sizeof(__u32))
		return false;

	return true;
}

static bool xdp_is_valid_access(int off, int size,
				enum bpf_access_type type,
				const struct bpf_prog *prog,
				struct bpf_insn_access_aux *info)
{
	if (prog->expected_attach_type != BPF_XDP_DEVMAP) {
		switch (off) {
		case offsetof(struct xdp_md, egress_ifindex):
			return false;
		}
	}

	if (type == BPF_WRITE) {
		if (bpf_prog_is_dev_bound(prog->aux)) {
			switch (off) {
			case offsetof(struct xdp_md, rx_queue_index):
				return __is_valid_xdp_access(off, size);
			}
		}
		return false;
	}

	switch (off) {
	case offsetof(struct xdp_md, data):
		info->reg_type = PTR_TO_PACKET;
		break;
	case offsetof(struct xdp_md, data_meta):
		info->reg_type = PTR_TO_PACKET_META;
		break;
	case offsetof(struct xdp_md, data_end):
		info->reg_type = PTR_TO_PACKET_END;
		break;
	}

	return __is_valid_xdp_access(off, size);
}

void bpf_warn_invalid_xdp_action(u32 act)
{
	const u32 act_max = XDP_REDIRECT;

	pr_warn_once("%s XDP return value %u, expect packet loss!\n",
		     act > act_max ? "Illegal" : "Driver unsupported",
		     act);
}
EXPORT_SYMBOL_GPL(bpf_warn_invalid_xdp_action);

static bool sock_addr_is_valid_access(int off, int size,
				      enum bpf_access_type type,
				      const struct bpf_prog *prog,
				      struct bpf_insn_access_aux *info)
{
	const int size_default = sizeof(__u32);

	if (off < 0 || off >= sizeof(struct bpf_sock_addr))
		return false;
	if (off % size != 0)
		return false;

	/* Disallow access to IPv6 fields from IPv4 contex and vise
	 * versa.
	 */
	switch (off) {
	case bpf_ctx_range(struct bpf_sock_addr, user_ip4):
		switch (prog->expected_attach_type) {
		case BPF_CGROUP_INET4_BIND:
		case BPF_CGROUP_INET4_CONNECT:
		case BPF_CGROUP_INET4_GETPEERNAME:
		case BPF_CGROUP_INET4_GETSOCKNAME:
		case BPF_CGROUP_UDP4_SENDMSG:
		case BPF_CGROUP_UDP4_RECVMSG:
			break;
		default:
			return false;
		}
		break;
	case bpf_ctx_range_till(struct bpf_sock_addr, user_ip6[0], user_ip6[3]):
		switch (prog->expected_attach_type) {
		case BPF_CGROUP_INET6_BIND:
		case BPF_CGROUP_INET6_CONNECT:
		case BPF_CGROUP_INET6_GETPEERNAME:
		case BPF_CGROUP_INET6_GETSOCKNAME:
		case BPF_CGROUP_UDP6_SENDMSG:
		case BPF_CGROUP_UDP6_RECVMSG:
			break;
		default:
			return false;
		}
		break;
	case bpf_ctx_range(struct bpf_sock_addr, msg_src_ip4):
		switch (prog->expected_attach_type) {
		case BPF_CGROUP_UDP4_SENDMSG:
			break;
		default:
			return false;
		}
		break;
	case bpf_ctx_range_till(struct bpf_sock_addr, msg_src_ip6[0],
				msg_src_ip6[3]):
		switch (prog->expected_attach_type) {
		case BPF_CGROUP_UDP6_SENDMSG:
			break;
		default:
			return false;
		}
		break;
	}

	switch (off) {
	case bpf_ctx_range(struct bpf_sock_addr, user_ip4):
	case bpf_ctx_range_till(struct bpf_sock_addr, user_ip6[0], user_ip6[3]):
	case bpf_ctx_range(struct bpf_sock_addr, msg_src_ip4):
	case bpf_ctx_range_till(struct bpf_sock_addr, msg_src_ip6[0],
				msg_src_ip6[3]):
	case bpf_ctx_range(struct bpf_sock_addr, user_port):
		if (type == BPF_READ) {
			bpf_ctx_record_field_size(info, size_default);

			if (bpf_ctx_wide_access_ok(off, size,
						   struct bpf_sock_addr,
						   user_ip6))
				return true;

			if (bpf_ctx_wide_access_ok(off, size,
						   struct bpf_sock_addr,
						   msg_src_ip6))
				return true;

			if (!bpf_ctx_narrow_access_ok(off, size, size_default))
				return false;
		} else {
			if (bpf_ctx_wide_access_ok(off, size,
						   struct bpf_sock_addr,
						   user_ip6))
				return true;

			if (bpf_ctx_wide_access_ok(off, size,
						   struct bpf_sock_addr,
						   msg_src_ip6))
				return true;

			if (size != size_default)
				return false;
		}
		break;
	case offsetof(struct bpf_sock_addr, sk):
		if (type != BPF_READ)
			return false;
		if (size != sizeof(__u64))
			return false;
		info->reg_type = PTR_TO_SOCKET;
		break;
	default:
		if (type == BPF_READ) {
			if (size != size_default)
				return false;
		} else {
			return false;
		}
	}

	return true;
}

static bool sock_ops_is_valid_access(int off, int size,
				     enum bpf_access_type type,
				     const struct bpf_prog *prog,
				     struct bpf_insn_access_aux *info)
{
	const int size_default = sizeof(__u32);

	if (off < 0 || off >= sizeof(struct bpf_sock_ops))
		return false;

	/* The verifier guarantees that size > 0. */
	if (off % size != 0)
		return false;

	if (type == BPF_WRITE) {
		switch (off) {
		case offsetof(struct bpf_sock_ops, reply):
		case offsetof(struct bpf_sock_ops, sk_txhash):
			if (size != size_default)
				return false;
			break;
		default:
			return false;
		}
	} else {
		switch (off) {
		case bpf_ctx_range_till(struct bpf_sock_ops, bytes_received,
					bytes_acked):
			if (size != sizeof(__u64))
				return false;
			break;
		case offsetof(struct bpf_sock_ops, sk):
			if (size != sizeof(__u64))
				return false;
			info->reg_type = PTR_TO_SOCKET_OR_NULL;
			break;
		case offsetof(struct bpf_sock_ops, skb_data):
			if (size != sizeof(__u64))
				return false;
			info->reg_type = PTR_TO_PACKET;
			break;
		case offsetof(struct bpf_sock_ops, skb_data_end):
			if (size != sizeof(__u64))
				return false;
			info->reg_type = PTR_TO_PACKET_END;
			break;
		case offsetof(struct bpf_sock_ops, skb_tcp_flags):
			bpf_ctx_record_field_size(info, size_default);
			return bpf_ctx_narrow_access_ok(off, size,
							size_default);
		default:
			if (size != size_default)
				return false;
			break;
		}
	}

	return true;
}

static int sk_skb_prologue(struct bpf_insn *insn_buf, bool direct_write,
			   const struct bpf_prog *prog)
{
	return bpf_unclone_prologue(insn_buf, direct_write, prog, SK_DROP);
}

static bool sk_skb_is_valid_access(int off, int size,
				   enum bpf_access_type type,
				   const struct bpf_prog *prog,
				   struct bpf_insn_access_aux *info)
{
	switch (off) {
	case bpf_ctx_range(struct __sk_buff, tc_classid):
	case bpf_ctx_range(struct __sk_buff, data_meta):
	case bpf_ctx_range(struct __sk_buff, tstamp):
	case bpf_ctx_range(struct __sk_buff, wire_len):
		return false;
	}

	if (type == BPF_WRITE) {
		switch (off) {
		case bpf_ctx_range(struct __sk_buff, tc_index):
		case bpf_ctx_range(struct __sk_buff, priority):
			break;
		default:
			return false;
		}
	}

	switch (off) {
	case bpf_ctx_range(struct __sk_buff, mark):
		return false;
	case bpf_ctx_range(struct __sk_buff, data):
		info->reg_type = PTR_TO_PACKET;
		break;
	case bpf_ctx_range(struct __sk_buff, data_end):
		info->reg_type = PTR_TO_PACKET_END;
		break;
	}

	return bpf_skb_is_valid_access(off, size, type, prog, info);
}

static bool sk_msg_is_valid_access(int off, int size,
				   enum bpf_access_type type,
				   const struct bpf_prog *prog,
				   struct bpf_insn_access_aux *info)
{
	if (type == BPF_WRITE)
		return false;

	if (off % size != 0)
		return false;

	switch (off) {
	case offsetof(struct sk_msg_md, data):
		info->reg_type = PTR_TO_PACKET;
		if (size != sizeof(__u64))
			return false;
		break;
	case offsetof(struct sk_msg_md, data_end):
		info->reg_type = PTR_TO_PACKET_END;
		if (size != sizeof(__u64))
			return false;
		break;
	case offsetof(struct sk_msg_md, sk):
		if (size != sizeof(__u64))
			return false;
		info->reg_type = PTR_TO_SOCKET;
		break;
	case bpf_ctx_range(struct sk_msg_md, family):
	case bpf_ctx_range(struct sk_msg_md, remote_ip4):
	case bpf_ctx_range(struct sk_msg_md, local_ip4):
	case bpf_ctx_range_till(struct sk_msg_md, remote_ip6[0], remote_ip6[3]):
	case bpf_ctx_range_till(struct sk_msg_md, local_ip6[0], local_ip6[3]):
	case bpf_ctx_range(struct sk_msg_md, remote_port):
	case bpf_ctx_range(struct sk_msg_md, local_port):
	case bpf_ctx_range(struct sk_msg_md, size):
		if (size != sizeof(__u32))
			return false;
		break;
	default:
		return false;
	}
	return true;
}

static bool flow_dissector_is_valid_access(int off, int size,
					   enum bpf_access_type type,
					   const struct bpf_prog *prog,
					   struct bpf_insn_access_aux *info)
{
	const int size_default = sizeof(__u32);

	if (off < 0 || off >= sizeof(struct __sk_buff))
		return false;

	if (type == BPF_WRITE)
		return false;

	switch (off) {
	case bpf_ctx_range(struct __sk_buff, data):
		if (size != size_default)
			return false;
		info->reg_type = PTR_TO_PACKET;
		return true;
	case bpf_ctx_range(struct __sk_buff, data_end):
		if (size != size_default)
			return false;
		info->reg_type = PTR_TO_PACKET_END;
		return true;
	case bpf_ctx_range_ptr(struct __sk_buff, flow_keys):
		if (size != sizeof(__u64))
			return false;
		info->reg_type = PTR_TO_FLOW_KEYS;
		return true;
	default:
		return false;
	}
}

static u32 flow_dissector_convert_ctx_access(enum bpf_access_type type,
					     const struct bpf_insn *si,
					     struct bpf_insn *insn_buf,
					     struct bpf_prog *prog,
					     u32 *target_size)

{
	struct bpf_insn *insn = insn_buf;

	switch (si->off) {
	case offsetof(struct __sk_buff, data):
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct bpf_flow_dissector, data),
				      si->dst_reg, si->src_reg,
				      offsetof(struct bpf_flow_dissector, data));
		break;

	case offsetof(struct __sk_buff, data_end):
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct bpf_flow_dissector, data_end),
				      si->dst_reg, si->src_reg,
				      offsetof(struct bpf_flow_dissector, data_end));
		break;

	case offsetof(struct __sk_buff, flow_keys):
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct bpf_flow_dissector, flow_keys),
				      si->dst_reg, si->src_reg,
				      offsetof(struct bpf_flow_dissector, flow_keys));
		break;
	}

	return insn - insn_buf;
}

static struct bpf_insn *bpf_convert_shinfo_access(const struct bpf_insn *si,
						  struct bpf_insn *insn)
{
	/* si->dst_reg = skb_shinfo(SKB); */
#ifdef NET_SKBUFF_DATA_USES_OFFSET
	*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct sk_buff, end),
			      BPF_REG_AX, si->src_reg,
			      offsetof(struct sk_buff, end));
	*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct sk_buff, head),
			      si->dst_reg, si->src_reg,
			      offsetof(struct sk_buff, head));
	*insn++ = BPF_ALU64_REG(BPF_ADD, si->dst_reg, BPF_REG_AX);
#else
	*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct sk_buff, end),
			      si->dst_reg, si->src_reg,
			      offsetof(struct sk_buff, end));
#endif

	return insn;
}

static u32 bpf_convert_ctx_access(enum bpf_access_type type,
				  const struct bpf_insn *si,
				  struct bpf_insn *insn_buf,
				  struct bpf_prog *prog, u32 *target_size)
{
	struct bpf_insn *insn = insn_buf;
	int off;

	switch (si->off) {
	case offsetof(struct __sk_buff, len):
		*insn++ = BPF_LDX_MEM(BPF_W, si->dst_reg, si->src_reg,
				      bpf_target_off(struct sk_buff, len, 4,
						     target_size));
		break;

	case offsetof(struct __sk_buff, protocol):
		*insn++ = BPF_LDX_MEM(BPF_H, si->dst_reg, si->src_reg,
				      bpf_target_off(struct sk_buff, protocol, 2,
						     target_size));
		break;

	case offsetof(struct __sk_buff, vlan_proto):
		*insn++ = BPF_LDX_MEM(BPF_H, si->dst_reg, si->src_reg,
				      bpf_target_off(struct sk_buff, vlan_proto, 2,
						     target_size));
		break;

	case offsetof(struct __sk_buff, priority):
		if (type == BPF_WRITE)
			*insn++ = BPF_STX_MEM(BPF_W, si->dst_reg, si->src_reg,
					      bpf_target_off(struct sk_buff, priority, 4,
							     target_size));
		else
			*insn++ = BPF_LDX_MEM(BPF_W, si->dst_reg, si->src_reg,
					      bpf_target_off(struct sk_buff, priority, 4,
							     target_size));
		break;

	case offsetof(struct __sk_buff, ingress_ifindex):
		*insn++ = BPF_LDX_MEM(BPF_W, si->dst_reg, si->src_reg,
				      bpf_target_off(struct sk_buff, skb_iif, 4,
						     target_size));
		break;

	case offsetof(struct __sk_buff, ifindex):
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct sk_buff, dev),
				      si->dst_reg, si->src_reg,
				      offsetof(struct sk_buff, dev));
		*insn++ = BPF_JMP_IMM(BPF_JEQ, si->dst_reg, 0, 1);
		*insn++ = BPF_LDX_MEM(BPF_W, si->dst_reg, si->dst_reg,
				      bpf_target_off(struct net_device, ifindex, 4,
						     target_size));
		break;

	case offsetof(struct __sk_buff, hash):
		*insn++ = BPF_LDX_MEM(BPF_W, si->dst_reg, si->src_reg,
				      bpf_target_off(struct sk_buff, hash, 4,
						     target_size));
		break;

	case offsetof(struct __sk_buff, mark):
		if (type == BPF_WRITE)
			*insn++ = BPF_STX_MEM(BPF_W, si->dst_reg, si->src_reg,
					      bpf_target_off(struct sk_buff, mark, 4,
							     target_size));
		else
			*insn++ = BPF_LDX_MEM(BPF_W, si->dst_reg, si->src_reg,
					      bpf_target_off(struct sk_buff, mark, 4,
							     target_size));
		break;

	case offsetof(struct __sk_buff, pkt_type):
		*target_size = 1;
		*insn++ = BPF_LDX_MEM(BPF_B, si->dst_reg, si->src_reg,
				      PKT_TYPE_OFFSET());
		*insn++ = BPF_ALU32_IMM(BPF_AND, si->dst_reg, PKT_TYPE_MAX);
#ifdef __BIG_ENDIAN_BITFIELD
		*insn++ = BPF_ALU32_IMM(BPF_RSH, si->dst_reg, 5);
#endif
		break;

	case offsetof(struct __sk_buff, queue_mapping):
		if (type == BPF_WRITE) {
			*insn++ = BPF_JMP_IMM(BPF_JGE, si->src_reg, NO_QUEUE_MAPPING, 1);
			*insn++ = BPF_STX_MEM(BPF_H, si->dst_reg, si->src_reg,
					      bpf_target_off(struct sk_buff,
							     queue_mapping,
							     2, target_size));
		} else {
			*insn++ = BPF_LDX_MEM(BPF_H, si->dst_reg, si->src_reg,
					      bpf_target_off(struct sk_buff,
							     queue_mapping,
							     2, target_size));
		}
		break;

	case offsetof(struct __sk_buff, vlan_present):
		*target_size = 1;
		*insn++ = BPF_LDX_MEM(BPF_B, si->dst_reg, si->src_reg,
				      PKT_VLAN_PRESENT_OFFSET());
		if (PKT_VLAN_PRESENT_BIT)
			*insn++ = BPF_ALU32_IMM(BPF_RSH, si->dst_reg, PKT_VLAN_PRESENT_BIT);
		if (PKT_VLAN_PRESENT_BIT < 7)
			*insn++ = BPF_ALU32_IMM(BPF_AND, si->dst_reg, 1);
		break;

	case offsetof(struct __sk_buff, vlan_tci):
		*insn++ = BPF_LDX_MEM(BPF_H, si->dst_reg, si->src_reg,
				      bpf_target_off(struct sk_buff, vlan_tci, 2,
						     target_size));
		break;

	case offsetof(struct __sk_buff, cb[0]) ...
	     offsetofend(struct __sk_buff, cb[4]) - 1:
		BUILD_BUG_ON(sizeof_field(struct qdisc_skb_cb, data) < 20);
		BUILD_BUG_ON((offsetof(struct sk_buff, cb) +
			      offsetof(struct qdisc_skb_cb, data)) %
			     sizeof(__u64));

		prog->cb_access = 1;
		off  = si->off;
		off -= offsetof(struct __sk_buff, cb[0]);
		off += offsetof(struct sk_buff, cb);
		off += offsetof(struct qdisc_skb_cb, data);
		if (type == BPF_WRITE)
			*insn++ = BPF_STX_MEM(BPF_SIZE(si->code), si->dst_reg,
					      si->src_reg, off);
		else
			*insn++ = BPF_LDX_MEM(BPF_SIZE(si->code), si->dst_reg,
					      si->src_reg, off);
		break;

	case offsetof(struct __sk_buff, tc_classid):
		BUILD_BUG_ON(sizeof_field(struct qdisc_skb_cb, tc_classid) != 2);

		off  = si->off;
		off -= offsetof(struct __sk_buff, tc_classid);
		off += offsetof(struct sk_buff, cb);
		off += offsetof(struct qdisc_skb_cb, tc_classid);
		*target_size = 2;
		if (type == BPF_WRITE)
			*insn++ = BPF_STX_MEM(BPF_H, si->dst_reg,
					      si->src_reg, off);
		else
			*insn++ = BPF_LDX_MEM(BPF_H, si->dst_reg,
					      si->src_reg, off);
		break;

	case offsetof(struct __sk_buff, data):
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct sk_buff, data),
				      si->dst_reg, si->src_reg,
				      offsetof(struct sk_buff, data));
		break;

	case offsetof(struct __sk_buff, data_meta):
		off  = si->off;
		off -= offsetof(struct __sk_buff, data_meta);
		off += offsetof(struct sk_buff, cb);
		off += offsetof(struct bpf_skb_data_end, data_meta);
		*insn++ = BPF_LDX_MEM(BPF_SIZEOF(void *), si->dst_reg,
				      si->src_reg, off);
		break;

	case offsetof(struct __sk_buff, data_end):
		off  = si->off;
		off -= offsetof(struct __sk_buff, data_end);
		off += offsetof(struct sk_buff, cb);
		off += offsetof(struct bpf_skb_data_end, data_end);
		*insn++ = BPF_LDX_MEM(BPF_SIZEOF(void *), si->dst_reg,
				      si->src_reg, off);
		break;

	case offsetof(struct __sk_buff, tc_index):
#ifdef CONFIG_NET_SCHED
		if (type == BPF_WRITE)
			*insn++ = BPF_STX_MEM(BPF_H, si->dst_reg, si->src_reg,
					      bpf_target_off(struct sk_buff, tc_index, 2,
							     target_size));
		else
			*insn++ = BPF_LDX_MEM(BPF_H, si->dst_reg, si->src_reg,
					      bpf_target_off(struct sk_buff, tc_index, 2,
							     target_size));
#else
		*target_size = 2;
		if (type == BPF_WRITE)
			*insn++ = BPF_MOV64_REG(si->dst_reg, si->dst_reg);
		else
			*insn++ = BPF_MOV64_IMM(si->dst_reg, 0);
#endif
		break;

	case offsetof(struct __sk_buff, napi_id):
#if defined(CONFIG_NET_RX_BUSY_POLL)
		*insn++ = BPF_LDX_MEM(BPF_W, si->dst_reg, si->src_reg,
				      bpf_target_off(struct sk_buff, napi_id, 4,
						     target_size));
		*insn++ = BPF_JMP_IMM(BPF_JGE, si->dst_reg, MIN_NAPI_ID, 1);
		*insn++ = BPF_MOV64_IMM(si->dst_reg, 0);
#else
		*target_size = 4;
		*insn++ = BPF_MOV64_IMM(si->dst_reg, 0);
#endif
		break;
	case offsetof(struct __sk_buff, family):
		BUILD_BUG_ON(sizeof_field(struct sock_common, skc_family) != 2);

		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct sk_buff, sk),
				      si->dst_reg, si->src_reg,
				      offsetof(struct sk_buff, sk));
		*insn++ = BPF_LDX_MEM(BPF_H, si->dst_reg, si->dst_reg,
				      bpf_target_off(struct sock_common,
						     skc_family,
						     2, target_size));
		break;
	case offsetof(struct __sk_buff, remote_ip4):
		BUILD_BUG_ON(sizeof_field(struct sock_common, skc_daddr) != 4);

		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct sk_buff, sk),
				      si->dst_reg, si->src_reg,
				      offsetof(struct sk_buff, sk));
		*insn++ = BPF_LDX_MEM(BPF_W, si->dst_reg, si->dst_reg,
				      bpf_target_off(struct sock_common,
						     skc_daddr,
						     4, target_size));
		break;
	case offsetof(struct __sk_buff, local_ip4):
		BUILD_BUG_ON(sizeof_field(struct sock_common,
					  skc_rcv_saddr) != 4);

		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct sk_buff, sk),
				      si->dst_reg, si->src_reg,
				      offsetof(struct sk_buff, sk));
		*insn++ = BPF_LDX_MEM(BPF_W, si->dst_reg, si->dst_reg,
				      bpf_target_off(struct sock_common,
						     skc_rcv_saddr,
						     4, target_size));
		break;
	case offsetof(struct __sk_buff, remote_ip6[0]) ...
	     offsetof(struct __sk_buff, remote_ip6[3]):
#if IS_ENABLED(CONFIG_IPV6)
		BUILD_BUG_ON(sizeof_field(struct sock_common,
					  skc_v6_daddr.s6_addr32[0]) != 4);

		off = si->off;
		off -= offsetof(struct __sk_buff, remote_ip6[0]);

		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct sk_buff, sk),
				      si->dst_reg, si->src_reg,
				      offsetof(struct sk_buff, sk));
		*insn++ = BPF_LDX_MEM(BPF_W, si->dst_reg, si->dst_reg,
				      offsetof(struct sock_common,
					       skc_v6_daddr.s6_addr32[0]) +
				      off);
#else
		*insn++ = BPF_MOV32_IMM(si->dst_reg, 0);
#endif
		break;
	case offsetof(struct __sk_buff, local_ip6[0]) ...
	     offsetof(struct __sk_buff, local_ip6[3]):
#if IS_ENABLED(CONFIG_IPV6)
		BUILD_BUG_ON(sizeof_field(struct sock_common,
					  skc_v6_rcv_saddr.s6_addr32[0]) != 4);

		off = si->off;
		off -= offsetof(struct __sk_buff, local_ip6[0]);

		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct sk_buff, sk),
				      si->dst_reg, si->src_reg,
				      offsetof(struct sk_buff, sk));
		*insn++ = BPF_LDX_MEM(BPF_W, si->dst_reg, si->dst_reg,
				      offsetof(struct sock_common,
					       skc_v6_rcv_saddr.s6_addr32[0]) +
				      off);
#else
		*insn++ = BPF_MOV32_IMM(si->dst_reg, 0);
#endif
		break;

	case offsetof(struct __sk_buff, remote_port):
		BUILD_BUG_ON(sizeof_field(struct sock_common, skc_dport) != 2);

		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct sk_buff, sk),
				      si->dst_reg, si->src_reg,
				      offsetof(struct sk_buff, sk));
		*insn++ = BPF_LDX_MEM(BPF_H, si->dst_reg, si->dst_reg,
				      bpf_target_off(struct sock_common,
						     skc_dport,
						     2, target_size));
#ifndef __BIG_ENDIAN_BITFIELD
		*insn++ = BPF_ALU32_IMM(BPF_LSH, si->dst_reg, 16);
#endif
		break;

	case offsetof(struct __sk_buff, local_port):
		BUILD_BUG_ON(sizeof_field(struct sock_common, skc_num) != 2);

		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct sk_buff, sk),
				      si->dst_reg, si->src_reg,
				      offsetof(struct sk_buff, sk));
		*insn++ = BPF_LDX_MEM(BPF_H, si->dst_reg, si->dst_reg,
				      bpf_target_off(struct sock_common,
						     skc_num, 2, target_size));
		break;

	case offsetof(struct __sk_buff, tstamp):
		BUILD_BUG_ON(sizeof_field(struct sk_buff, tstamp) != 8);

		if (type == BPF_WRITE)
			*insn++ = BPF_STX_MEM(BPF_DW,
					      si->dst_reg, si->src_reg,
					      bpf_target_off(struct sk_buff,
							     tstamp, 8,
							     target_size));
		else
			*insn++ = BPF_LDX_MEM(BPF_DW,
					      si->dst_reg, si->src_reg,
					      bpf_target_off(struct sk_buff,
							     tstamp, 8,
							     target_size));
		break;

	case offsetof(struct __sk_buff, gso_segs):
		insn = bpf_convert_shinfo_access(si, insn);
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct skb_shared_info, gso_segs),
				      si->dst_reg, si->dst_reg,
				      bpf_target_off(struct skb_shared_info,
						     gso_segs, 2,
						     target_size));
		break;
	case offsetof(struct __sk_buff, gso_size):
		insn = bpf_convert_shinfo_access(si, insn);
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct skb_shared_info, gso_size),
				      si->dst_reg, si->dst_reg,
				      bpf_target_off(struct skb_shared_info,
						     gso_size, 2,
						     target_size));
		break;
	case offsetof(struct __sk_buff, wire_len):
		BUILD_BUG_ON(sizeof_field(struct qdisc_skb_cb, pkt_len) != 4);

		off = si->off;
		off -= offsetof(struct __sk_buff, wire_len);
		off += offsetof(struct sk_buff, cb);
		off += offsetof(struct qdisc_skb_cb, pkt_len);
		*target_size = 4;
		*insn++ = BPF_LDX_MEM(BPF_W, si->dst_reg, si->src_reg, off);
		break;

	case offsetof(struct __sk_buff, sk):
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct sk_buff, sk),
				      si->dst_reg, si->src_reg,
				      offsetof(struct sk_buff, sk));
		break;
	}

	return insn - insn_buf;
}

u32 bpf_sock_convert_ctx_access(enum bpf_access_type type,
				const struct bpf_insn *si,
				struct bpf_insn *insn_buf,
				struct bpf_prog *prog, u32 *target_size)
{
	struct bpf_insn *insn = insn_buf;
	int off;

	switch (si->off) {
	case offsetof(struct bpf_sock, bound_dev_if):
		BUILD_BUG_ON(sizeof_field(struct sock, sk_bound_dev_if) != 4);

		if (type == BPF_WRITE)
			*insn++ = BPF_STX_MEM(BPF_W, si->dst_reg, si->src_reg,
					offsetof(struct sock, sk_bound_dev_if));
		else
			*insn++ = BPF_LDX_MEM(BPF_W, si->dst_reg, si->src_reg,
				      offsetof(struct sock, sk_bound_dev_if));
		break;

	case offsetof(struct bpf_sock, mark):
		BUILD_BUG_ON(sizeof_field(struct sock, sk_mark) != 4);

		if (type == BPF_WRITE)
			*insn++ = BPF_STX_MEM(BPF_W, si->dst_reg, si->src_reg,
					offsetof(struct sock, sk_mark));
		else
			*insn++ = BPF_LDX_MEM(BPF_W, si->dst_reg, si->src_reg,
				      offsetof(struct sock, sk_mark));
		break;

	case offsetof(struct bpf_sock, priority):
		BUILD_BUG_ON(sizeof_field(struct sock, sk_priority) != 4);

		if (type == BPF_WRITE)
			*insn++ = BPF_STX_MEM(BPF_W, si->dst_reg, si->src_reg,
					offsetof(struct sock, sk_priority));
		else
			*insn++ = BPF_LDX_MEM(BPF_W, si->dst_reg, si->src_reg,
				      offsetof(struct sock, sk_priority));
		break;

	case offsetof(struct bpf_sock, family):
		*insn++ = BPF_LDX_MEM(
			BPF_FIELD_SIZEOF(struct sock_common, skc_family),
			si->dst_reg, si->src_reg,
			bpf_target_off(struct sock_common,
				       skc_family,
				       sizeof_field(struct sock_common,
						    skc_family),
				       target_size));
		break;

	case offsetof(struct bpf_sock, type):
		*insn++ = BPF_LDX_MEM(
			BPF_FIELD_SIZEOF(struct sock, sk_type),
			si->dst_reg, si->src_reg,
			bpf_target_off(struct sock, sk_type,
				       sizeof_field(struct sock, sk_type),
				       target_size));
		break;

	case offsetof(struct bpf_sock, protocol):
		*insn++ = BPF_LDX_MEM(
			BPF_FIELD_SIZEOF(struct sock, sk_protocol),
			si->dst_reg, si->src_reg,
			bpf_target_off(struct sock, sk_protocol,
				       sizeof_field(struct sock, sk_protocol),
				       target_size));
		break;

	case offsetof(struct bpf_sock, src_ip4):
		*insn++ = BPF_LDX_MEM(
			BPF_SIZE(si->code), si->dst_reg, si->src_reg,
			bpf_target_off(struct sock_common, skc_rcv_saddr,
				       sizeof_field(struct sock_common,
						    skc_rcv_saddr),
				       target_size));
		break;

	case offsetof(struct bpf_sock, dst_ip4):
		*insn++ = BPF_LDX_MEM(
			BPF_SIZE(si->code), si->dst_reg, si->src_reg,
			bpf_target_off(struct sock_common, skc_daddr,
				       sizeof_field(struct sock_common,
						    skc_daddr),
				       target_size));
		break;

	case bpf_ctx_range_till(struct bpf_sock, src_ip6[0], src_ip6[3]):
#if IS_ENABLED(CONFIG_IPV6)
		off = si->off;
		off -= offsetof(struct bpf_sock, src_ip6[0]);
		*insn++ = BPF_LDX_MEM(
			BPF_SIZE(si->code), si->dst_reg, si->src_reg,
			bpf_target_off(
				struct sock_common,
				skc_v6_rcv_saddr.s6_addr32[0],
				sizeof_field(struct sock_common,
					     skc_v6_rcv_saddr.s6_addr32[0]),
				target_size) + off);
#else
		(void)off;
		*insn++ = BPF_MOV32_IMM(si->dst_reg, 0);
#endif
		break;

	case bpf_ctx_range_till(struct bpf_sock, dst_ip6[0], dst_ip6[3]):
#if IS_ENABLED(CONFIG_IPV6)
		off = si->off;
		off -= offsetof(struct bpf_sock, dst_ip6[0]);
		*insn++ = BPF_LDX_MEM(
			BPF_SIZE(si->code), si->dst_reg, si->src_reg,
			bpf_target_off(struct sock_common,
				       skc_v6_daddr.s6_addr32[0],
				       sizeof_field(struct sock_common,
						    skc_v6_daddr.s6_addr32[0]),
				       target_size) + off);
#else
		*insn++ = BPF_MOV32_IMM(si->dst_reg, 0);
		*target_size = 4;
#endif
		break;

	case offsetof(struct bpf_sock, src_port):
		*insn++ = BPF_LDX_MEM(
			BPF_FIELD_SIZEOF(struct sock_common, skc_num),
			si->dst_reg, si->src_reg,
			bpf_target_off(struct sock_common, skc_num,
				       sizeof_field(struct sock_common,
						    skc_num),
				       target_size));
		break;

	case offsetof(struct bpf_sock, dst_port):
		*insn++ = BPF_LDX_MEM(
			BPF_FIELD_SIZEOF(struct sock_common, skc_dport),
			si->dst_reg, si->src_reg,
			bpf_target_off(struct sock_common, skc_dport,
				       sizeof_field(struct sock_common,
						    skc_dport),
				       target_size));
		break;

	case offsetof(struct bpf_sock, state):
		*insn++ = BPF_LDX_MEM(
			BPF_FIELD_SIZEOF(struct sock_common, skc_state),
			si->dst_reg, si->src_reg,
			bpf_target_off(struct sock_common, skc_state,
				       sizeof_field(struct sock_common,
						    skc_state),
				       target_size));
		break;
	case offsetof(struct bpf_sock, rx_queue_mapping):
#ifdef CONFIG_XPS
		*insn++ = BPF_LDX_MEM(
			BPF_FIELD_SIZEOF(struct sock, sk_rx_queue_mapping),
			si->dst_reg, si->src_reg,
			bpf_target_off(struct sock, sk_rx_queue_mapping,
				       sizeof_field(struct sock,
						    sk_rx_queue_mapping),
				       target_size));
		*insn++ = BPF_JMP_IMM(BPF_JNE, si->dst_reg, NO_QUEUE_MAPPING,
				      1);
		*insn++ = BPF_MOV64_IMM(si->dst_reg, -1);
#else
		*insn++ = BPF_MOV64_IMM(si->dst_reg, -1);
		*target_size = 2;
#endif
		break;
	}

	return insn - insn_buf;
}

static u32 tc_cls_act_convert_ctx_access(enum bpf_access_type type,
					 const struct bpf_insn *si,
					 struct bpf_insn *insn_buf,
					 struct bpf_prog *prog, u32 *target_size)
{
	struct bpf_insn *insn = insn_buf;

	switch (si->off) {
	case offsetof(struct __sk_buff, ifindex):
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct sk_buff, dev),
				      si->dst_reg, si->src_reg,
				      offsetof(struct sk_buff, dev));
		*insn++ = BPF_LDX_MEM(BPF_W, si->dst_reg, si->dst_reg,
				      bpf_target_off(struct net_device, ifindex, 4,
						     target_size));
		break;
	default:
		return bpf_convert_ctx_access(type, si, insn_buf, prog,
					      target_size);
	}

	return insn - insn_buf;
}

static u32 xdp_convert_ctx_access(enum bpf_access_type type,
				  const struct bpf_insn *si,
				  struct bpf_insn *insn_buf,
				  struct bpf_prog *prog, u32 *target_size)
{
	struct bpf_insn *insn = insn_buf;

	switch (si->off) {
	case offsetof(struct xdp_md, data):
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct xdp_buff, data),
				      si->dst_reg, si->src_reg,
				      offsetof(struct xdp_buff, data));
		break;
	case offsetof(struct xdp_md, data_meta):
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct xdp_buff, data_meta),
				      si->dst_reg, si->src_reg,
				      offsetof(struct xdp_buff, data_meta));
		break;
	case offsetof(struct xdp_md, data_end):
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct xdp_buff, data_end),
				      si->dst_reg, si->src_reg,
				      offsetof(struct xdp_buff, data_end));
		break;
	case offsetof(struct xdp_md, ingress_ifindex):
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct xdp_buff, rxq),
				      si->dst_reg, si->src_reg,
				      offsetof(struct xdp_buff, rxq));
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct xdp_rxq_info, dev),
				      si->dst_reg, si->dst_reg,
				      offsetof(struct xdp_rxq_info, dev));
		*insn++ = BPF_LDX_MEM(BPF_W, si->dst_reg, si->dst_reg,
				      offsetof(struct net_device, ifindex));
		break;
	case offsetof(struct xdp_md, rx_queue_index):
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct xdp_buff, rxq),
				      si->dst_reg, si->src_reg,
				      offsetof(struct xdp_buff, rxq));
		*insn++ = BPF_LDX_MEM(BPF_W, si->dst_reg, si->dst_reg,
				      offsetof(struct xdp_rxq_info,
					       queue_index));
		break;
	case offsetof(struct xdp_md, egress_ifindex):
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct xdp_buff, txq),
				      si->dst_reg, si->src_reg,
				      offsetof(struct xdp_buff, txq));
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct xdp_txq_info, dev),
				      si->dst_reg, si->dst_reg,
				      offsetof(struct xdp_txq_info, dev));
		*insn++ = BPF_LDX_MEM(BPF_W, si->dst_reg, si->dst_reg,
				      offsetof(struct net_device, ifindex));
		break;
	}

	return insn - insn_buf;
}

/* SOCK_ADDR_LOAD_NESTED_FIELD() loads Nested Field S.F.NF where S is type of
 * context Structure, F is Field in context structure that contains a pointer
 * to Nested Structure of type NS that has the field NF.
 *
 * SIZE encodes the load size (BPF_B, BPF_H, etc). It's up to caller to make
 * sure that SIZE is not greater than actual size of S.F.NF.
 *
 * If offset OFF is provided, the load happens from that offset relative to
 * offset of NF.
 */
#define SOCK_ADDR_LOAD_NESTED_FIELD_SIZE_OFF(S, NS, F, NF, SIZE, OFF)	       \
	do {								       \
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(S, F), si->dst_reg,     \
				      si->src_reg, offsetof(S, F));	       \
		*insn++ = BPF_LDX_MEM(					       \
			SIZE, si->dst_reg, si->dst_reg,			       \
			bpf_target_off(NS, NF, sizeof_field(NS, NF),	       \
				       target_size)			       \
				+ OFF);					       \
	} while (0)

#define SOCK_ADDR_LOAD_NESTED_FIELD(S, NS, F, NF)			       \
	SOCK_ADDR_LOAD_NESTED_FIELD_SIZE_OFF(S, NS, F, NF,		       \
					     BPF_FIELD_SIZEOF(NS, NF), 0)

/* SOCK_ADDR_STORE_NESTED_FIELD_OFF() has semantic similar to
 * SOCK_ADDR_LOAD_NESTED_FIELD_SIZE_OFF() but for store operation.
 *
 * In addition it uses Temporary Field TF (member of struct S) as the 3rd
 * "register" since two registers available in convert_ctx_access are not
 * enough: we can't override neither SRC, since it contains value to store, nor
 * DST since it contains pointer to context that may be used by later
 * instructions. But we need a temporary place to save pointer to nested
 * structure whose field we want to store to.
 */
#define SOCK_ADDR_STORE_NESTED_FIELD_OFF(S, NS, F, NF, SIZE, OFF, TF)	       \
	do {								       \
		int tmp_reg = BPF_REG_9;				       \
		if (si->src_reg == tmp_reg || si->dst_reg == tmp_reg)	       \
			--tmp_reg;					       \
		if (si->src_reg == tmp_reg || si->dst_reg == tmp_reg)	       \
			--tmp_reg;					       \
		*insn++ = BPF_STX_MEM(BPF_DW, si->dst_reg, tmp_reg,	       \
				      offsetof(S, TF));			       \
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(S, F), tmp_reg,	       \
				      si->dst_reg, offsetof(S, F));	       \
		*insn++ = BPF_STX_MEM(SIZE, tmp_reg, si->src_reg,	       \
			bpf_target_off(NS, NF, sizeof_field(NS, NF),	       \
				       target_size)			       \
				+ OFF);					       \
		*insn++ = BPF_LDX_MEM(BPF_DW, tmp_reg, si->dst_reg,	       \
				      offsetof(S, TF));			       \
	} while (0)

#define SOCK_ADDR_LOAD_OR_STORE_NESTED_FIELD_SIZE_OFF(S, NS, F, NF, SIZE, OFF, \
						      TF)		       \
	do {								       \
		if (type == BPF_WRITE) {				       \
			SOCK_ADDR_STORE_NESTED_FIELD_OFF(S, NS, F, NF, SIZE,   \
							 OFF, TF);	       \
		} else {						       \
			SOCK_ADDR_LOAD_NESTED_FIELD_SIZE_OFF(		       \
				S, NS, F, NF, SIZE, OFF);  \
		}							       \
	} while (0)

#define SOCK_ADDR_LOAD_OR_STORE_NESTED_FIELD(S, NS, F, NF, TF)		       \
	SOCK_ADDR_LOAD_OR_STORE_NESTED_FIELD_SIZE_OFF(			       \
		S, NS, F, NF, BPF_FIELD_SIZEOF(NS, NF), 0, TF)

static u32 sock_addr_convert_ctx_access(enum bpf_access_type type,
					const struct bpf_insn *si,
					struct bpf_insn *insn_buf,
					struct bpf_prog *prog, u32 *target_size)
{
	int off, port_size = sizeof_field(struct sockaddr_in6, sin6_port);
	struct bpf_insn *insn = insn_buf;

	switch (si->off) {
	case offsetof(struct bpf_sock_addr, user_family):
		SOCK_ADDR_LOAD_NESTED_FIELD(struct bpf_sock_addr_kern,
					    struct sockaddr, uaddr, sa_family);
		break;

	case offsetof(struct bpf_sock_addr, user_ip4):
		SOCK_ADDR_LOAD_OR_STORE_NESTED_FIELD_SIZE_OFF(
			struct bpf_sock_addr_kern, struct sockaddr_in, uaddr,
			sin_addr, BPF_SIZE(si->code), 0, tmp_reg);
		break;

	case bpf_ctx_range_till(struct bpf_sock_addr, user_ip6[0], user_ip6[3]):
		off = si->off;
		off -= offsetof(struct bpf_sock_addr, user_ip6[0]);
		SOCK_ADDR_LOAD_OR_STORE_NESTED_FIELD_SIZE_OFF(
			struct bpf_sock_addr_kern, struct sockaddr_in6, uaddr,
			sin6_addr.s6_addr32[0], BPF_SIZE(si->code), off,
			tmp_reg);
		break;

	case offsetof(struct bpf_sock_addr, user_port):
		/* To get port we need to know sa_family first and then treat
		 * sockaddr as either sockaddr_in or sockaddr_in6.
		 * Though we can simplify since port field has same offset and
		 * size in both structures.
		 * Here we check this invariant and use just one of the
		 * structures if it's true.
		 */
		BUILD_BUG_ON(offsetof(struct sockaddr_in, sin_port) !=
			     offsetof(struct sockaddr_in6, sin6_port));
		BUILD_BUG_ON(sizeof_field(struct sockaddr_in, sin_port) !=
			     sizeof_field(struct sockaddr_in6, sin6_port));
		/* Account for sin6_port being smaller than user_port. */
		port_size = min(port_size, BPF_LDST_BYTES(si));
		SOCK_ADDR_LOAD_OR_STORE_NESTED_FIELD_SIZE_OFF(
			struct bpf_sock_addr_kern, struct sockaddr_in6, uaddr,
			sin6_port, bytes_to_bpf_size(port_size), 0, tmp_reg);
		break;

	case offsetof(struct bpf_sock_addr, family):
		SOCK_ADDR_LOAD_NESTED_FIELD(struct bpf_sock_addr_kern,
					    struct sock, sk, sk_family);
		break;

	case offsetof(struct bpf_sock_addr, type):
		SOCK_ADDR_LOAD_NESTED_FIELD(struct bpf_sock_addr_kern,
					    struct sock, sk, sk_type);
		break;

	case offsetof(struct bpf_sock_addr, protocol):
		SOCK_ADDR_LOAD_NESTED_FIELD(struct bpf_sock_addr_kern,
					    struct sock, sk, sk_protocol);
		break;

	case offsetof(struct bpf_sock_addr, msg_src_ip4):
		/* Treat t_ctx as struct in_addr for msg_src_ip4. */
		SOCK_ADDR_LOAD_OR_STORE_NESTED_FIELD_SIZE_OFF(
			struct bpf_sock_addr_kern, struct in_addr, t_ctx,
			s_addr, BPF_SIZE(si->code), 0, tmp_reg);
		break;

	case bpf_ctx_range_till(struct bpf_sock_addr, msg_src_ip6[0],
				msg_src_ip6[3]):
		off = si->off;
		off -= offsetof(struct bpf_sock_addr, msg_src_ip6[0]);
		/* Treat t_ctx as struct in6_addr for msg_src_ip6. */
		SOCK_ADDR_LOAD_OR_STORE_NESTED_FIELD_SIZE_OFF(
			struct bpf_sock_addr_kern, struct in6_addr, t_ctx,
			s6_addr32[0], BPF_SIZE(si->code), off, tmp_reg);
		break;
	case offsetof(struct bpf_sock_addr, sk):
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct bpf_sock_addr_kern, sk),
				      si->dst_reg, si->src_reg,
				      offsetof(struct bpf_sock_addr_kern, sk));
		break;
	}

	return insn - insn_buf;
}

static u32 sock_ops_convert_ctx_access(enum bpf_access_type type,
				       const struct bpf_insn *si,
				       struct bpf_insn *insn_buf,
				       struct bpf_prog *prog,
				       u32 *target_size)
{
	struct bpf_insn *insn = insn_buf;
	int off;

/* Helper macro for adding read access to tcp_sock or sock fields. */
#define SOCK_OPS_GET_FIELD(BPF_FIELD, OBJ_FIELD, OBJ)			      \
	do {								      \
		int fullsock_reg = si->dst_reg, reg = BPF_REG_9, jmp = 2;     \
		BUILD_BUG_ON(sizeof_field(OBJ, OBJ_FIELD) >		      \
			     sizeof_field(struct bpf_sock_ops, BPF_FIELD));   \
		if (si->dst_reg == reg || si->src_reg == reg)		      \
			reg--;						      \
		if (si->dst_reg == reg || si->src_reg == reg)		      \
			reg--;						      \
		if (si->dst_reg == si->src_reg) {			      \
			*insn++ = BPF_STX_MEM(BPF_DW, si->src_reg, reg,	      \
					  offsetof(struct bpf_sock_ops_kern,  \
					  temp));			      \
			fullsock_reg = reg;				      \
			jmp += 2;					      \
		}							      \
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(			      \
						struct bpf_sock_ops_kern,     \
						is_fullsock),		      \
				      fullsock_reg, si->src_reg,	      \
				      offsetof(struct bpf_sock_ops_kern,      \
					       is_fullsock));		      \
		*insn++ = BPF_JMP_IMM(BPF_JEQ, fullsock_reg, 0, jmp);	      \
		if (si->dst_reg == si->src_reg)				      \
			*insn++ = BPF_LDX_MEM(BPF_DW, reg, si->src_reg,	      \
				      offsetof(struct bpf_sock_ops_kern,      \
				      temp));				      \
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(			      \
						struct bpf_sock_ops_kern, sk),\
				      si->dst_reg, si->src_reg,		      \
				      offsetof(struct bpf_sock_ops_kern, sk));\
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(OBJ,		      \
						       OBJ_FIELD),	      \
				      si->dst_reg, si->dst_reg,		      \
				      offsetof(OBJ, OBJ_FIELD));	      \
		if (si->dst_reg == si->src_reg)	{			      \
			*insn++ = BPF_JMP_A(1);				      \
			*insn++ = BPF_LDX_MEM(BPF_DW, reg, si->src_reg,	      \
				      offsetof(struct bpf_sock_ops_kern,      \
				      temp));				      \
		}							      \
	} while (0)

#define SOCK_OPS_GET_SK()							      \
	do {								      \
		int fullsock_reg = si->dst_reg, reg = BPF_REG_9, jmp = 1;     \
		if (si->dst_reg == reg || si->src_reg == reg)		      \
			reg--;						      \
		if (si->dst_reg == reg || si->src_reg == reg)		      \
			reg--;						      \
		if (si->dst_reg == si->src_reg) {			      \
			*insn++ = BPF_STX_MEM(BPF_DW, si->src_reg, reg,	      \
					  offsetof(struct bpf_sock_ops_kern,  \
					  temp));			      \
			fullsock_reg = reg;				      \
			jmp += 2;					      \
		}							      \
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(			      \
						struct bpf_sock_ops_kern,     \
						is_fullsock),		      \
				      fullsock_reg, si->src_reg,	      \
				      offsetof(struct bpf_sock_ops_kern,      \
					       is_fullsock));		      \
		*insn++ = BPF_JMP_IMM(BPF_JEQ, fullsock_reg, 0, jmp);	      \
		if (si->dst_reg == si->src_reg)				      \
			*insn++ = BPF_LDX_MEM(BPF_DW, reg, si->src_reg,	      \
				      offsetof(struct bpf_sock_ops_kern,      \
				      temp));				      \
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(			      \
						struct bpf_sock_ops_kern, sk),\
				      si->dst_reg, si->src_reg,		      \
				      offsetof(struct bpf_sock_ops_kern, sk));\
		if (si->dst_reg == si->src_reg)	{			      \
			*insn++ = BPF_JMP_A(1);				      \
			*insn++ = BPF_LDX_MEM(BPF_DW, reg, si->src_reg,	      \
				      offsetof(struct bpf_sock_ops_kern,      \
				      temp));				      \
		}							      \
	} while (0)

#define SOCK_OPS_GET_TCP_SOCK_FIELD(FIELD) \
		SOCK_OPS_GET_FIELD(FIELD, FIELD, struct tcp_sock)

/* Helper macro for adding write access to tcp_sock or sock fields.
 * The macro is called with two registers, dst_reg which contains a pointer
 * to ctx (context) and src_reg which contains the value that should be
 * stored. However, we need an additional register since we cannot overwrite
 * dst_reg because it may be used later in the program.
 * Instead we "borrow" one of the other register. We first save its value
 * into a new (temp) field in bpf_sock_ops_kern, use it, and then restore
 * it at the end of the macro.
 */
#define SOCK_OPS_SET_FIELD(BPF_FIELD, OBJ_FIELD, OBJ)			      \
	do {								      \
		int reg = BPF_REG_9;					      \
		BUILD_BUG_ON(sizeof_field(OBJ, OBJ_FIELD) >		      \
			     sizeof_field(struct bpf_sock_ops, BPF_FIELD));   \
		if (si->dst_reg == reg || si->src_reg == reg)		      \
			reg--;						      \
		if (si->dst_reg == reg || si->src_reg == reg)		      \
			reg--;						      \
		*insn++ = BPF_STX_MEM(BPF_DW, si->dst_reg, reg,		      \
				      offsetof(struct bpf_sock_ops_kern,      \
					       temp));			      \
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(			      \
						struct bpf_sock_ops_kern,     \
						is_fullsock),		      \
				      reg, si->dst_reg,			      \
				      offsetof(struct bpf_sock_ops_kern,      \
					       is_fullsock));		      \
		*insn++ = BPF_JMP_IMM(BPF_JEQ, reg, 0, 2);		      \
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(			      \
						struct bpf_sock_ops_kern, sk),\
				      reg, si->dst_reg,			      \
				      offsetof(struct bpf_sock_ops_kern, sk));\
		*insn++ = BPF_STX_MEM(BPF_FIELD_SIZEOF(OBJ, OBJ_FIELD),	      \
				      reg, si->src_reg,			      \
				      offsetof(OBJ, OBJ_FIELD));	      \
		*insn++ = BPF_LDX_MEM(BPF_DW, reg, si->dst_reg,		      \
				      offsetof(struct bpf_sock_ops_kern,      \
					       temp));			      \
	} while (0)

#define SOCK_OPS_GET_OR_SET_FIELD(BPF_FIELD, OBJ_FIELD, OBJ, TYPE)	      \
	do {								      \
		if (TYPE == BPF_WRITE)					      \
			SOCK_OPS_SET_FIELD(BPF_FIELD, OBJ_FIELD, OBJ);	      \
		else							      \
			SOCK_OPS_GET_FIELD(BPF_FIELD, OBJ_FIELD, OBJ);	      \
	} while (0)

	if (insn > insn_buf)
		return insn - insn_buf;

	switch (si->off) {
	case offsetof(struct bpf_sock_ops, op):
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct bpf_sock_ops_kern,
						       op),
				      si->dst_reg, si->src_reg,
				      offsetof(struct bpf_sock_ops_kern, op));
		break;

	case offsetof(struct bpf_sock_ops, replylong[0]) ...
	     offsetof(struct bpf_sock_ops, replylong[3]):
		BUILD_BUG_ON(sizeof_field(struct bpf_sock_ops, reply) !=
			     sizeof_field(struct bpf_sock_ops_kern, reply));
		BUILD_BUG_ON(sizeof_field(struct bpf_sock_ops, replylong) !=
			     sizeof_field(struct bpf_sock_ops_kern, replylong));
		off = si->off;
		off -= offsetof(struct bpf_sock_ops, replylong[0]);
		off += offsetof(struct bpf_sock_ops_kern, replylong[0]);
		if (type == BPF_WRITE)
			*insn++ = BPF_STX_MEM(BPF_W, si->dst_reg, si->src_reg,
					      off);
		else
			*insn++ = BPF_LDX_MEM(BPF_W, si->dst_reg, si->src_reg,
					      off);
		break;

	case offsetof(struct bpf_sock_ops, family):
		BUILD_BUG_ON(sizeof_field(struct sock_common, skc_family) != 2);

		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(
					      struct bpf_sock_ops_kern, sk),
				      si->dst_reg, si->src_reg,
				      offsetof(struct bpf_sock_ops_kern, sk));
		*insn++ = BPF_LDX_MEM(BPF_H, si->dst_reg, si->dst_reg,
				      offsetof(struct sock_common, skc_family));
		break;

	case offsetof(struct bpf_sock_ops, remote_ip4):
		BUILD_BUG_ON(sizeof_field(struct sock_common, skc_daddr) != 4);

		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(
						struct bpf_sock_ops_kern, sk),
				      si->dst_reg, si->src_reg,
				      offsetof(struct bpf_sock_ops_kern, sk));
		*insn++ = BPF_LDX_MEM(BPF_W, si->dst_reg, si->dst_reg,
				      offsetof(struct sock_common, skc_daddr));
		break;

	case offsetof(struct bpf_sock_ops, local_ip4):
		BUILD_BUG_ON(sizeof_field(struct sock_common,
					  skc_rcv_saddr) != 4);

		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(
					      struct bpf_sock_ops_kern, sk),
				      si->dst_reg, si->src_reg,
				      offsetof(struct bpf_sock_ops_kern, sk));
		*insn++ = BPF_LDX_MEM(BPF_W, si->dst_reg, si->dst_reg,
				      offsetof(struct sock_common,
					       skc_rcv_saddr));
		break;

	case offsetof(struct bpf_sock_ops, remote_ip6[0]) ...
	     offsetof(struct bpf_sock_ops, remote_ip6[3]):
#if IS_ENABLED(CONFIG_IPV6)
		BUILD_BUG_ON(sizeof_field(struct sock_common,
					  skc_v6_daddr.s6_addr32[0]) != 4);

		off = si->off;
		off -= offsetof(struct bpf_sock_ops, remote_ip6[0]);
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(
						struct bpf_sock_ops_kern, sk),
				      si->dst_reg, si->src_reg,
				      offsetof(struct bpf_sock_ops_kern, sk));
		*insn++ = BPF_LDX_MEM(BPF_W, si->dst_reg, si->dst_reg,
				      offsetof(struct sock_common,
					       skc_v6_daddr.s6_addr32[0]) +
				      off);
#else
		*insn++ = BPF_MOV32_IMM(si->dst_reg, 0);
#endif
		break;

	case offsetof(struct bpf_sock_ops, local_ip6[0]) ...
	     offsetof(struct bpf_sock_ops, local_ip6[3]):
#if IS_ENABLED(CONFIG_IPV6)
		BUILD_BUG_ON(sizeof_field(struct sock_common,
					  skc_v6_rcv_saddr.s6_addr32[0]) != 4);

		off = si->off;
		off -= offsetof(struct bpf_sock_ops, local_ip6[0]);
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(
						struct bpf_sock_ops_kern, sk),
				      si->dst_reg, si->src_reg,
				      offsetof(struct bpf_sock_ops_kern, sk));
		*insn++ = BPF_LDX_MEM(BPF_W, si->dst_reg, si->dst_reg,
				      offsetof(struct sock_common,
					       skc_v6_rcv_saddr.s6_addr32[0]) +
				      off);
#else
		*insn++ = BPF_MOV32_IMM(si->dst_reg, 0);
#endif
		break;

	case offsetof(struct bpf_sock_ops, remote_port):
		BUILD_BUG_ON(sizeof_field(struct sock_common, skc_dport) != 2);

		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(
						struct bpf_sock_ops_kern, sk),
				      si->dst_reg, si->src_reg,
				      offsetof(struct bpf_sock_ops_kern, sk));
		*insn++ = BPF_LDX_MEM(BPF_H, si->dst_reg, si->dst_reg,
				      offsetof(struct sock_common, skc_dport));
#ifndef __BIG_ENDIAN_BITFIELD
		*insn++ = BPF_ALU32_IMM(BPF_LSH, si->dst_reg, 16);
#endif
		break;

	case offsetof(struct bpf_sock_ops, local_port):
		BUILD_BUG_ON(sizeof_field(struct sock_common, skc_num) != 2);

		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(
						struct bpf_sock_ops_kern, sk),
				      si->dst_reg, si->src_reg,
				      offsetof(struct bpf_sock_ops_kern, sk));
		*insn++ = BPF_LDX_MEM(BPF_H, si->dst_reg, si->dst_reg,
				      offsetof(struct sock_common, skc_num));
		break;

	case offsetof(struct bpf_sock_ops, is_fullsock):
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(
						struct bpf_sock_ops_kern,
						is_fullsock),
				      si->dst_reg, si->src_reg,
				      offsetof(struct bpf_sock_ops_kern,
					       is_fullsock));
		break;

	case offsetof(struct bpf_sock_ops, state):
		BUILD_BUG_ON(sizeof_field(struct sock_common, skc_state) != 1);

		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(
						struct bpf_sock_ops_kern, sk),
				      si->dst_reg, si->src_reg,
				      offsetof(struct bpf_sock_ops_kern, sk));
		*insn++ = BPF_LDX_MEM(BPF_B, si->dst_reg, si->dst_reg,
				      offsetof(struct sock_common, skc_state));
		break;

	case offsetof(struct bpf_sock_ops, rtt_min):
		BUILD_BUG_ON(sizeof_field(struct tcp_sock, rtt_min) !=
			     sizeof(struct minmax));
		BUILD_BUG_ON(sizeof(struct minmax) <
			     sizeof(struct minmax_sample));

		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(
						struct bpf_sock_ops_kern, sk),
				      si->dst_reg, si->src_reg,
				      offsetof(struct bpf_sock_ops_kern, sk));
		*insn++ = BPF_LDX_MEM(BPF_W, si->dst_reg, si->dst_reg,
				      offsetof(struct tcp_sock, rtt_min) +
				      sizeof_field(struct minmax_sample, t));
		break;

	case offsetof(struct bpf_sock_ops, bpf_sock_ops_cb_flags):
		SOCK_OPS_GET_FIELD(bpf_sock_ops_cb_flags, bpf_sock_ops_cb_flags,
				   struct tcp_sock);
		break;

	case offsetof(struct bpf_sock_ops, sk_txhash):
		SOCK_OPS_GET_OR_SET_FIELD(sk_txhash, sk_txhash,
					  struct sock, type);
		break;
	case offsetof(struct bpf_sock_ops, snd_cwnd):
		SOCK_OPS_GET_TCP_SOCK_FIELD(snd_cwnd);
		break;
	case offsetof(struct bpf_sock_ops, srtt_us):
		SOCK_OPS_GET_TCP_SOCK_FIELD(srtt_us);
		break;
	case offsetof(struct bpf_sock_ops, snd_ssthresh):
		SOCK_OPS_GET_TCP_SOCK_FIELD(snd_ssthresh);
		break;
	case offsetof(struct bpf_sock_ops, rcv_nxt):
		SOCK_OPS_GET_TCP_SOCK_FIELD(rcv_nxt);
		break;
	case offsetof(struct bpf_sock_ops, snd_nxt):
		SOCK_OPS_GET_TCP_SOCK_FIELD(snd_nxt);
		break;
	case offsetof(struct bpf_sock_ops, snd_una):
		SOCK_OPS_GET_TCP_SOCK_FIELD(snd_una);
		break;
	case offsetof(struct bpf_sock_ops, mss_cache):
		SOCK_OPS_GET_TCP_SOCK_FIELD(mss_cache);
		break;
	case offsetof(struct bpf_sock_ops, ecn_flags):
		SOCK_OPS_GET_TCP_SOCK_FIELD(ecn_flags);
		break;
	case offsetof(struct bpf_sock_ops, rate_delivered):
		SOCK_OPS_GET_TCP_SOCK_FIELD(rate_delivered);
		break;
	case offsetof(struct bpf_sock_ops, rate_interval_us):
		SOCK_OPS_GET_TCP_SOCK_FIELD(rate_interval_us);
		break;
	case offsetof(struct bpf_sock_ops, packets_out):
		SOCK_OPS_GET_TCP_SOCK_FIELD(packets_out);
		break;
	case offsetof(struct bpf_sock_ops, retrans_out):
		SOCK_OPS_GET_TCP_SOCK_FIELD(retrans_out);
		break;
	case offsetof(struct bpf_sock_ops, total_retrans):
		SOCK_OPS_GET_TCP_SOCK_FIELD(total_retrans);
		break;
	case offsetof(struct bpf_sock_ops, segs_in):
		SOCK_OPS_GET_TCP_SOCK_FIELD(segs_in);
		break;
	case offsetof(struct bpf_sock_ops, data_segs_in):
		SOCK_OPS_GET_TCP_SOCK_FIELD(data_segs_in);
		break;
	case offsetof(struct bpf_sock_ops, segs_out):
		SOCK_OPS_GET_TCP_SOCK_FIELD(segs_out);
		break;
	case offsetof(struct bpf_sock_ops, data_segs_out):
		SOCK_OPS_GET_TCP_SOCK_FIELD(data_segs_out);
		break;
	case offsetof(struct bpf_sock_ops, lost_out):
		SOCK_OPS_GET_TCP_SOCK_FIELD(lost_out);
		break;
	case offsetof(struct bpf_sock_ops, sacked_out):
		SOCK_OPS_GET_TCP_SOCK_FIELD(sacked_out);
		break;
	case offsetof(struct bpf_sock_ops, bytes_received):
		SOCK_OPS_GET_TCP_SOCK_FIELD(bytes_received);
		break;
	case offsetof(struct bpf_sock_ops, bytes_acked):
		SOCK_OPS_GET_TCP_SOCK_FIELD(bytes_acked);
		break;
	case offsetof(struct bpf_sock_ops, sk):
		SOCK_OPS_GET_SK();
		break;
	case offsetof(struct bpf_sock_ops, skb_data_end):
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct bpf_sock_ops_kern,
						       skb_data_end),
				      si->dst_reg, si->src_reg,
				      offsetof(struct bpf_sock_ops_kern,
					       skb_data_end));
		break;
	case offsetof(struct bpf_sock_ops, skb_data):
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct bpf_sock_ops_kern,
						       skb),
				      si->dst_reg, si->src_reg,
				      offsetof(struct bpf_sock_ops_kern,
					       skb));
		*insn++ = BPF_JMP_IMM(BPF_JEQ, si->dst_reg, 0, 1);
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct sk_buff, data),
				      si->dst_reg, si->dst_reg,
				      offsetof(struct sk_buff, data));
		break;
	case offsetof(struct bpf_sock_ops, skb_len):
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct bpf_sock_ops_kern,
						       skb),
				      si->dst_reg, si->src_reg,
				      offsetof(struct bpf_sock_ops_kern,
					       skb));
		*insn++ = BPF_JMP_IMM(BPF_JEQ, si->dst_reg, 0, 1);
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct sk_buff, len),
				      si->dst_reg, si->dst_reg,
				      offsetof(struct sk_buff, len));
		break;
	case offsetof(struct bpf_sock_ops, skb_tcp_flags):
		off = offsetof(struct sk_buff, cb);
		off += offsetof(struct tcp_skb_cb, tcp_flags);
		*target_size = sizeof_field(struct tcp_skb_cb, tcp_flags);
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct bpf_sock_ops_kern,
						       skb),
				      si->dst_reg, si->src_reg,
				      offsetof(struct bpf_sock_ops_kern,
					       skb));
		*insn++ = BPF_JMP_IMM(BPF_JEQ, si->dst_reg, 0, 1);
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct tcp_skb_cb,
						       tcp_flags),
				      si->dst_reg, si->dst_reg, off);
		break;
	}
	return insn - insn_buf;
}

static u32 sk_skb_convert_ctx_access(enum bpf_access_type type,
				     const struct bpf_insn *si,
				     struct bpf_insn *insn_buf,
				     struct bpf_prog *prog, u32 *target_size)
{
	struct bpf_insn *insn = insn_buf;
	int off;

	switch (si->off) {
	case offsetof(struct __sk_buff, data_end):
		off  = si->off;
		off -= offsetof(struct __sk_buff, data_end);
		off += offsetof(struct sk_buff, cb);
		off += offsetof(struct tcp_skb_cb, bpf.data_end);
		*insn++ = BPF_LDX_MEM(BPF_SIZEOF(void *), si->dst_reg,
				      si->src_reg, off);
		break;
	case offsetof(struct __sk_buff, cb[0]) ...
	     offsetofend(struct __sk_buff, cb[4]) - 1:
		BUILD_BUG_ON(sizeof_field(struct sk_skb_cb, data) < 20);
		BUILD_BUG_ON((offsetof(struct sk_buff, cb) +
			      offsetof(struct sk_skb_cb, data)) %
			     sizeof(__u64));

		prog->cb_access = 1;
		off  = si->off;
		off -= offsetof(struct __sk_buff, cb[0]);
		off += offsetof(struct sk_buff, cb);
		off += offsetof(struct sk_skb_cb, data);
		if (type == BPF_WRITE)
			*insn++ = BPF_STX_MEM(BPF_SIZE(si->code), si->dst_reg,
					      si->src_reg, off);
		else
			*insn++ = BPF_LDX_MEM(BPF_SIZE(si->code), si->dst_reg,
					      si->src_reg, off);
		break;


	default:
		return bpf_convert_ctx_access(type, si, insn_buf, prog,
					      target_size);
	}

	return insn - insn_buf;
}

static u32 sk_msg_convert_ctx_access(enum bpf_access_type type,
				     const struct bpf_insn *si,
				     struct bpf_insn *insn_buf,
				     struct bpf_prog *prog, u32 *target_size)
{
	struct bpf_insn *insn = insn_buf;
#if IS_ENABLED(CONFIG_IPV6)
	int off;
#endif

	/* convert ctx uses the fact sg element is first in struct */
	BUILD_BUG_ON(offsetof(struct sk_msg, sg) != 0);

	switch (si->off) {
	case offsetof(struct sk_msg_md, data):
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct sk_msg, data),
				      si->dst_reg, si->src_reg,
				      offsetof(struct sk_msg, data));
		break;
	case offsetof(struct sk_msg_md, data_end):
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct sk_msg, data_end),
				      si->dst_reg, si->src_reg,
				      offsetof(struct sk_msg, data_end));
		break;
	case offsetof(struct sk_msg_md, family):
		BUILD_BUG_ON(sizeof_field(struct sock_common, skc_family) != 2);

		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(
					      struct sk_msg, sk),
				      si->dst_reg, si->src_reg,
				      offsetof(struct sk_msg, sk));
		*insn++ = BPF_LDX_MEM(BPF_H, si->dst_reg, si->dst_reg,
				      offsetof(struct sock_common, skc_family));
		break;

	case offsetof(struct sk_msg_md, remote_ip4):
		BUILD_BUG_ON(sizeof_field(struct sock_common, skc_daddr) != 4);

		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(
						struct sk_msg, sk),
				      si->dst_reg, si->src_reg,
				      offsetof(struct sk_msg, sk));
		*insn++ = BPF_LDX_MEM(BPF_W, si->dst_reg, si->dst_reg,
				      offsetof(struct sock_common, skc_daddr));
		break;

	case offsetof(struct sk_msg_md, local_ip4):
		BUILD_BUG_ON(sizeof_field(struct sock_common,
					  skc_rcv_saddr) != 4);

		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(
					      struct sk_msg, sk),
				      si->dst_reg, si->src_reg,
				      offsetof(struct sk_msg, sk));
		*insn++ = BPF_LDX_MEM(BPF_W, si->dst_reg, si->dst_reg,
				      offsetof(struct sock_common,
					       skc_rcv_saddr));
		break;

	case offsetof(struct sk_msg_md, remote_ip6[0]) ...
	     offsetof(struct sk_msg_md, remote_ip6[3]):
#if IS_ENABLED(CONFIG_IPV6)
		BUILD_BUG_ON(sizeof_field(struct sock_common,
					  skc_v6_daddr.s6_addr32[0]) != 4);

		off = si->off;
		off -= offsetof(struct sk_msg_md, remote_ip6[0]);
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(
						struct sk_msg, sk),
				      si->dst_reg, si->src_reg,
				      offsetof(struct sk_msg, sk));
		*insn++ = BPF_LDX_MEM(BPF_W, si->dst_reg, si->dst_reg,
				      offsetof(struct sock_common,
					       skc_v6_daddr.s6_addr32[0]) +
				      off);
#else
		*insn++ = BPF_MOV32_IMM(si->dst_reg, 0);
#endif
		break;

	case offsetof(struct sk_msg_md, local_ip6[0]) ...
	     offsetof(struct sk_msg_md, local_ip6[3]):
#if IS_ENABLED(CONFIG_IPV6)
		BUILD_BUG_ON(sizeof_field(struct sock_common,
					  skc_v6_rcv_saddr.s6_addr32[0]) != 4);

		off = si->off;
		off -= offsetof(struct sk_msg_md, local_ip6[0]);
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(
						struct sk_msg, sk),
				      si->dst_reg, si->src_reg,
				      offsetof(struct sk_msg, sk));
		*insn++ = BPF_LDX_MEM(BPF_W, si->dst_reg, si->dst_reg,
				      offsetof(struct sock_common,
					       skc_v6_rcv_saddr.s6_addr32[0]) +
				      off);
#else
		*insn++ = BPF_MOV32_IMM(si->dst_reg, 0);
#endif
		break;

	case offsetof(struct sk_msg_md, remote_port):
		BUILD_BUG_ON(sizeof_field(struct sock_common, skc_dport) != 2);

		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(
						struct sk_msg, sk),
				      si->dst_reg, si->src_reg,
				      offsetof(struct sk_msg, sk));
		*insn++ = BPF_LDX_MEM(BPF_H, si->dst_reg, si->dst_reg,
				      offsetof(struct sock_common, skc_dport));
#ifndef __BIG_ENDIAN_BITFIELD
		*insn++ = BPF_ALU32_IMM(BPF_LSH, si->dst_reg, 16);
#endif
		break;

	case offsetof(struct sk_msg_md, local_port):
		BUILD_BUG_ON(sizeof_field(struct sock_common, skc_num) != 2);

		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(
						struct sk_msg, sk),
				      si->dst_reg, si->src_reg,
				      offsetof(struct sk_msg, sk));
		*insn++ = BPF_LDX_MEM(BPF_H, si->dst_reg, si->dst_reg,
				      offsetof(struct sock_common, skc_num));
		break;

	case offsetof(struct sk_msg_md, size):
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct sk_msg_sg, size),
				      si->dst_reg, si->src_reg,
				      offsetof(struct sk_msg_sg, size));
		break;

	case offsetof(struct sk_msg_md, sk):
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct sk_msg, sk),
				      si->dst_reg, si->src_reg,
				      offsetof(struct sk_msg, sk));
		break;
	}

	return insn - insn_buf;
}

const struct bpf_verifier_ops sk_filter_verifier_ops = {
	.get_func_proto		= sk_filter_func_proto,
	.is_valid_access	= sk_filter_is_valid_access,
	.convert_ctx_access	= bpf_convert_ctx_access,
	.gen_ld_abs		= bpf_gen_ld_abs,
};

const struct bpf_prog_ops sk_filter_prog_ops = {
	.test_run		= bpf_prog_test_run_skb,
};

const struct bpf_verifier_ops tc_cls_act_verifier_ops = {
	.get_func_proto		= tc_cls_act_func_proto,
	.is_valid_access	= tc_cls_act_is_valid_access,
	.convert_ctx_access	= tc_cls_act_convert_ctx_access,
	.gen_prologue		= tc_cls_act_prologue,
	.gen_ld_abs		= bpf_gen_ld_abs,
};

const struct bpf_prog_ops tc_cls_act_prog_ops = {
	.test_run		= bpf_prog_test_run_skb,
};

const struct bpf_verifier_ops xdp_verifier_ops = {
	.get_func_proto		= xdp_func_proto,
	.is_valid_access	= xdp_is_valid_access,
	.convert_ctx_access	= xdp_convert_ctx_access,
	.gen_prologue		= bpf_noop_prologue,
};

const struct bpf_prog_ops xdp_prog_ops = {
	.test_run		= bpf_prog_test_run_xdp,
};

const struct bpf_verifier_ops cg_skb_verifier_ops = {
	.get_func_proto		= cg_skb_func_proto,
	.is_valid_access	= cg_skb_is_valid_access,
	.convert_ctx_access	= bpf_convert_ctx_access,
};

const struct bpf_prog_ops cg_skb_prog_ops = {
	.test_run		= bpf_prog_test_run_skb,
};

const struct bpf_verifier_ops lwt_in_verifier_ops = {
	.get_func_proto		= lwt_in_func_proto,
	.is_valid_access	= lwt_is_valid_access,
	.convert_ctx_access	= bpf_convert_ctx_access,
};

const struct bpf_prog_ops lwt_in_prog_ops = {
	.test_run		= bpf_prog_test_run_skb,
};

const struct bpf_verifier_ops lwt_out_verifier_ops = {
	.get_func_proto		= lwt_out_func_proto,
	.is_valid_access	= lwt_is_valid_access,
	.convert_ctx_access	= bpf_convert_ctx_access,
};

const struct bpf_prog_ops lwt_out_prog_ops = {
	.test_run		= bpf_prog_test_run_skb,
};

const struct bpf_verifier_ops lwt_xmit_verifier_ops = {
	.get_func_proto		= lwt_xmit_func_proto,
	.is_valid_access	= lwt_is_valid_access,
	.convert_ctx_access	= bpf_convert_ctx_access,
	.gen_prologue		= tc_cls_act_prologue,
};

const struct bpf_prog_ops lwt_xmit_prog_ops = {
	.test_run		= bpf_prog_test_run_skb,
};

const struct bpf_verifier_ops lwt_seg6local_verifier_ops = {
	.get_func_proto		= lwt_seg6local_func_proto,
	.is_valid_access	= lwt_is_valid_access,
	.convert_ctx_access	= bpf_convert_ctx_access,
};

const struct bpf_prog_ops lwt_seg6local_prog_ops = {
	.test_run		= bpf_prog_test_run_skb,
};

const struct bpf_verifier_ops cg_sock_verifier_ops = {
	.get_func_proto		= sock_filter_func_proto,
	.is_valid_access	= sock_filter_is_valid_access,
	.convert_ctx_access	= bpf_sock_convert_ctx_access,
};

const struct bpf_prog_ops cg_sock_prog_ops = {
};

const struct bpf_verifier_ops cg_sock_addr_verifier_ops = {
	.get_func_proto		= sock_addr_func_proto,
	.is_valid_access	= sock_addr_is_valid_access,
	.convert_ctx_access	= sock_addr_convert_ctx_access,
};

const struct bpf_prog_ops cg_sock_addr_prog_ops = {
};

const struct bpf_verifier_ops sock_ops_verifier_ops = {
	.get_func_proto		= sock_ops_func_proto,
	.is_valid_access	= sock_ops_is_valid_access,
	.convert_ctx_access	= sock_ops_convert_ctx_access,
};

const struct bpf_prog_ops sock_ops_prog_ops = {
};

const struct bpf_verifier_ops sk_skb_verifier_ops = {
	.get_func_proto		= sk_skb_func_proto,
	.is_valid_access	= sk_skb_is_valid_access,
	.convert_ctx_access	= sk_skb_convert_ctx_access,
	.gen_prologue		= sk_skb_prologue,
};

const struct bpf_prog_ops sk_skb_prog_ops = {
};

const struct bpf_verifier_ops sk_msg_verifier_ops = {
	.get_func_proto		= sk_msg_func_proto,
	.is_valid_access	= sk_msg_is_valid_access,
	.convert_ctx_access	= sk_msg_convert_ctx_access,
	.gen_prologue		= bpf_noop_prologue,
};

const struct bpf_prog_ops sk_msg_prog_ops = {
};

const struct bpf_verifier_ops flow_dissector_verifier_ops = {
	.get_func_proto		= flow_dissector_func_proto,
	.is_valid_access	= flow_dissector_is_valid_access,
	.convert_ctx_access	= flow_dissector_convert_ctx_access,
};

const struct bpf_prog_ops flow_dissector_prog_ops = {
	.test_run		= bpf_prog_test_run_flow_dissector,
};

int sk_detach_filter(struct sock *sk)
{
	int ret = -ENOENT;
	struct sk_filter *filter;

	if (sock_flag(sk, SOCK_FILTER_LOCKED))
		return -EPERM;

	filter = rcu_dereference_protected(sk->sk_filter,
					   lockdep_sock_is_held(sk));
	if (filter) {
		RCU_INIT_POINTER(sk->sk_filter, NULL);
		sk_filter_uncharge(sk, filter);
		ret = 0;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(sk_detach_filter);

int sk_get_filter(struct sock *sk, struct sock_filter __user *ubuf,
		  unsigned int len)
{
	struct sock_fprog_kern *fprog;
	struct sk_filter *filter;
	int ret = 0;

	lock_sock(sk);
	filter = rcu_dereference_protected(sk->sk_filter,
					   lockdep_sock_is_held(sk));
	if (!filter)
		goto out;

	/* We're copying the filter that has been originally attached,
	 * so no conversion/decode needed anymore. eBPF programs that
	 * have no original program cannot be dumped through this.
	 */
	ret = -EACCES;
	fprog = filter->prog->orig_prog;
	if (!fprog)
		goto out;

	ret = fprog->len;
	if (!len)
		/* User space only enquires number of filter blocks. */
		goto out;

	ret = -EINVAL;
	if (len < fprog->len)
		goto out;

	ret = -EFAULT;
	if (copy_to_user(ubuf, fprog->filter, bpf_classic_proglen(fprog)))
		goto out;

	/* Instead of bytes, the API requests to return the number
	 * of filter blocks.
	 */
	ret = fprog->len;
out:
	release_sock(sk);
	return ret;
}

#ifdef CONFIG_INET
static void bpf_init_reuseport_kern(struct sk_reuseport_kern *reuse_kern,
				    struct sock_reuseport *reuse,
				    struct sock *sk, struct sk_buff *skb,
				    u32 hash)
{
	reuse_kern->skb = skb;
	reuse_kern->sk = sk;
	reuse_kern->selected_sk = NULL;
	reuse_kern->data_end = skb->data + skb_headlen(skb);
	reuse_kern->hash = hash;
	reuse_kern->reuseport_id = reuse->reuseport_id;
	reuse_kern->bind_inany = reuse->bind_inany;
}

struct sock *bpf_run_sk_reuseport(struct sock_reuseport *reuse, struct sock *sk,
				  struct bpf_prog *prog, struct sk_buff *skb,
				  u32 hash)
{
	struct sk_reuseport_kern reuse_kern;
	enum sk_action action;

	bpf_init_reuseport_kern(&reuse_kern, reuse, sk, skb, hash);
	action = BPF_PROG_RUN(prog, &reuse_kern);

	if (action == SK_PASS)
		return reuse_kern.selected_sk;
	else
		return ERR_PTR(-ECONNREFUSED);
}

BPF_CALL_4(sk_select_reuseport, struct sk_reuseport_kern *, reuse_kern,
	   struct bpf_map *, map, void *, key, u32, flags)
{
	bool is_sockarray = map->map_type == BPF_MAP_TYPE_REUSEPORT_SOCKARRAY;
	struct sock_reuseport *reuse;
	struct sock *selected_sk;

	selected_sk = map->ops->map_lookup_elem(map, key);
	if (!selected_sk)
		return -ENOENT;

	reuse = rcu_dereference(selected_sk->sk_reuseport_cb);
	if (!reuse) {
		/* Lookup in sock_map can return TCP ESTABLISHED sockets. */
		if (sk_is_refcounted(selected_sk))
			sock_put(selected_sk);

		/* reuseport_array has only sk with non NULL sk_reuseport_cb.
		 * The only (!reuse) case here is - the sk has already been
		 * unhashed (e.g. by close()), so treat it as -ENOENT.
		 *
		 * Other maps (e.g. sock_map) do not provide this guarantee and
		 * the sk may never be in the reuseport group to begin with.
		 */
		return is_sockarray ? -ENOENT : -EINVAL;
	}

	if (unlikely(reuse->reuseport_id != reuse_kern->reuseport_id)) {
		struct sock *sk = reuse_kern->sk;

		if (sk->sk_protocol != selected_sk->sk_protocol)
			return -EPROTOTYPE;
		else if (sk->sk_family != selected_sk->sk_family)
			return -EAFNOSUPPORT;

		/* Catch all. Likely bound to a different sockaddr. */
		return -EBADFD;
	}

	reuse_kern->selected_sk = selected_sk;

	return 0;
}

static const struct bpf_func_proto sk_select_reuseport_proto = {
	.func           = sk_select_reuseport,
	.gpl_only       = false,
	.ret_type       = RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type      = ARG_CONST_MAP_PTR,
	.arg3_type      = ARG_PTR_TO_MAP_KEY,
	.arg4_type	= ARG_ANYTHING,
};

BPF_CALL_4(sk_reuseport_load_bytes,
	   const struct sk_reuseport_kern *, reuse_kern, u32, offset,
	   void *, to, u32, len)
{
	return ____bpf_skb_load_bytes(reuse_kern->skb, offset, to, len);
}

static const struct bpf_func_proto sk_reuseport_load_bytes_proto = {
	.func		= sk_reuseport_load_bytes,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
	.arg3_type	= ARG_PTR_TO_UNINIT_MEM,
	.arg4_type	= ARG_CONST_SIZE,
};

BPF_CALL_5(sk_reuseport_load_bytes_relative,
	   const struct sk_reuseport_kern *, reuse_kern, u32, offset,
	   void *, to, u32, len, u32, start_header)
{
	return ____bpf_skb_load_bytes_relative(reuse_kern->skb, offset, to,
					       len, start_header);
}

static const struct bpf_func_proto sk_reuseport_load_bytes_relative_proto = {
	.func		= sk_reuseport_load_bytes_relative,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
	.arg3_type	= ARG_PTR_TO_UNINIT_MEM,
	.arg4_type	= ARG_CONST_SIZE,
	.arg5_type	= ARG_ANYTHING,
};

static const struct bpf_func_proto *
sk_reuseport_func_proto(enum bpf_func_id func_id,
			const struct bpf_prog *prog)
{
	switch (func_id) {
	case BPF_FUNC_sk_select_reuseport:
		return &sk_select_reuseport_proto;
	case BPF_FUNC_skb_load_bytes:
		return &sk_reuseport_load_bytes_proto;
	case BPF_FUNC_skb_load_bytes_relative:
		return &sk_reuseport_load_bytes_relative_proto;
	default:
		return bpf_base_func_proto(func_id);
	}
}

static bool
sk_reuseport_is_valid_access(int off, int size,
			     enum bpf_access_type type,
			     const struct bpf_prog *prog,
			     struct bpf_insn_access_aux *info)
{
	const u32 size_default = sizeof(__u32);

	if (off < 0 || off >= sizeof(struct sk_reuseport_md) ||
	    off % size || type != BPF_READ)
		return false;

	switch (off) {
	case offsetof(struct sk_reuseport_md, data):
		info->reg_type = PTR_TO_PACKET;
		return size == sizeof(__u64);

	case offsetof(struct sk_reuseport_md, data_end):
		info->reg_type = PTR_TO_PACKET_END;
		return size == sizeof(__u64);

	case offsetof(struct sk_reuseport_md, hash):
		return size == size_default;

	/* Fields that allow narrowing */
	case bpf_ctx_range(struct sk_reuseport_md, eth_protocol):
		if (size < sizeof_field(struct sk_buff, protocol))
			return false;
		fallthrough;
	case bpf_ctx_range(struct sk_reuseport_md, ip_protocol):
	case bpf_ctx_range(struct sk_reuseport_md, bind_inany):
	case bpf_ctx_range(struct sk_reuseport_md, len):
		bpf_ctx_record_field_size(info, size_default);
		return bpf_ctx_narrow_access_ok(off, size, size_default);

	default:
		return false;
	}
}

#define SK_REUSEPORT_LOAD_FIELD(F) ({					\
	*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct sk_reuseport_kern, F), \
			      si->dst_reg, si->src_reg,			\
			      bpf_target_off(struct sk_reuseport_kern, F, \
					     sizeof_field(struct sk_reuseport_kern, F), \
					     target_size));		\
	})

#define SK_REUSEPORT_LOAD_SKB_FIELD(SKB_FIELD)				\
	SOCK_ADDR_LOAD_NESTED_FIELD(struct sk_reuseport_kern,		\
				    struct sk_buff,			\
				    skb,				\
				    SKB_FIELD)

#define SK_REUSEPORT_LOAD_SK_FIELD(SK_FIELD)				\
	SOCK_ADDR_LOAD_NESTED_FIELD(struct sk_reuseport_kern,		\
				    struct sock,			\
				    sk,					\
				    SK_FIELD)

static u32 sk_reuseport_convert_ctx_access(enum bpf_access_type type,
					   const struct bpf_insn *si,
					   struct bpf_insn *insn_buf,
					   struct bpf_prog *prog,
					   u32 *target_size)
{
	struct bpf_insn *insn = insn_buf;

	switch (si->off) {
	case offsetof(struct sk_reuseport_md, data):
		SK_REUSEPORT_LOAD_SKB_FIELD(data);
		break;

	case offsetof(struct sk_reuseport_md, len):
		SK_REUSEPORT_LOAD_SKB_FIELD(len);
		break;

	case offsetof(struct sk_reuseport_md, eth_protocol):
		SK_REUSEPORT_LOAD_SKB_FIELD(protocol);
		break;

	case offsetof(struct sk_reuseport_md, ip_protocol):
		SK_REUSEPORT_LOAD_SK_FIELD(sk_protocol);
		break;

	case offsetof(struct sk_reuseport_md, data_end):
		SK_REUSEPORT_LOAD_FIELD(data_end);
		break;

	case offsetof(struct sk_reuseport_md, hash):
		SK_REUSEPORT_LOAD_FIELD(hash);
		break;

	case offsetof(struct sk_reuseport_md, bind_inany):
		SK_REUSEPORT_LOAD_FIELD(bind_inany);
		break;
	}

	return insn - insn_buf;
}

const struct bpf_verifier_ops sk_reuseport_verifier_ops = {
	.get_func_proto		= sk_reuseport_func_proto,
	.is_valid_access	= sk_reuseport_is_valid_access,
	.convert_ctx_access	= sk_reuseport_convert_ctx_access,
};

const struct bpf_prog_ops sk_reuseport_prog_ops = {
};

DEFINE_STATIC_KEY_FALSE(bpf_sk_lookup_enabled);
EXPORT_SYMBOL(bpf_sk_lookup_enabled);

BPF_CALL_3(bpf_sk_lookup_assign, struct bpf_sk_lookup_kern *, ctx,
	   struct sock *, sk, u64, flags)
{
	if (unlikely(flags & ~(BPF_SK_LOOKUP_F_REPLACE |
			       BPF_SK_LOOKUP_F_NO_REUSEPORT)))
		return -EINVAL;
	if (unlikely(sk && sk_is_refcounted(sk)))
		return -ESOCKTNOSUPPORT; /* reject non-RCU freed sockets */
	if (unlikely(sk && sk->sk_state == TCP_ESTABLISHED))
		return -ESOCKTNOSUPPORT; /* reject connected sockets */

	/* Check if socket is suitable for packet L3/L4 protocol */
	if (sk && sk->sk_protocol != ctx->protocol)
		return -EPROTOTYPE;
	if (sk && sk->sk_family != ctx->family &&
	    (sk->sk_family == AF_INET || ipv6_only_sock(sk)))
		return -EAFNOSUPPORT;

	if (ctx->selected_sk && !(flags & BPF_SK_LOOKUP_F_REPLACE))
		return -EEXIST;

	/* Select socket as lookup result */
	ctx->selected_sk = sk;
	ctx->no_reuseport = flags & BPF_SK_LOOKUP_F_NO_REUSEPORT;
	return 0;
}

static const struct bpf_func_proto bpf_sk_lookup_assign_proto = {
	.func		= bpf_sk_lookup_assign,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_PTR_TO_SOCKET_OR_NULL,
	.arg3_type	= ARG_ANYTHING,
};

static const struct bpf_func_proto *
sk_lookup_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	switch (func_id) {
	case BPF_FUNC_perf_event_output:
		return &bpf_event_output_data_proto;
	case BPF_FUNC_sk_assign:
		return &bpf_sk_lookup_assign_proto;
	case BPF_FUNC_sk_release:
		return &bpf_sk_release_proto;
	default:
		return bpf_sk_base_func_proto(func_id);
	}
}

static bool sk_lookup_is_valid_access(int off, int size,
				      enum bpf_access_type type,
				      const struct bpf_prog *prog,
				      struct bpf_insn_access_aux *info)
{
	if (off < 0 || off >= sizeof(struct bpf_sk_lookup))
		return false;
	if (off % size != 0)
		return false;
	if (type != BPF_READ)
		return false;

	switch (off) {
	case offsetof(struct bpf_sk_lookup, sk):
		info->reg_type = PTR_TO_SOCKET_OR_NULL;
		return size == sizeof(__u64);

	case bpf_ctx_range(struct bpf_sk_lookup, family):
	case bpf_ctx_range(struct bpf_sk_lookup, protocol):
	case bpf_ctx_range(struct bpf_sk_lookup, remote_ip4):
	case bpf_ctx_range(struct bpf_sk_lookup, local_ip4):
	case bpf_ctx_range_till(struct bpf_sk_lookup, remote_ip6[0], remote_ip6[3]):
	case bpf_ctx_range_till(struct bpf_sk_lookup, local_ip6[0], local_ip6[3]):
	case bpf_ctx_range(struct bpf_sk_lookup, remote_port):
	case bpf_ctx_range(struct bpf_sk_lookup, local_port):
		bpf_ctx_record_field_size(info, sizeof(__u32));
		return bpf_ctx_narrow_access_ok(off, size, sizeof(__u32));

	default:
		return false;
	}
}

static u32 sk_lookup_convert_ctx_access(enum bpf_access_type type,
					const struct bpf_insn *si,
					struct bpf_insn *insn_buf,
					struct bpf_prog *prog,
					u32 *target_size)
{
	struct bpf_insn *insn = insn_buf;

	switch (si->off) {
	case offsetof(struct bpf_sk_lookup, sk):
		*insn++ = BPF_LDX_MEM(BPF_SIZEOF(void *), si->dst_reg, si->src_reg,
				      offsetof(struct bpf_sk_lookup_kern, selected_sk));
		break;

	case offsetof(struct bpf_sk_lookup, family):
		*insn++ = BPF_LDX_MEM(BPF_H, si->dst_reg, si->src_reg,
				      bpf_target_off(struct bpf_sk_lookup_kern,
						     family, 2, target_size));
		break;

	case offsetof(struct bpf_sk_lookup, protocol):
		*insn++ = BPF_LDX_MEM(BPF_H, si->dst_reg, si->src_reg,
				      bpf_target_off(struct bpf_sk_lookup_kern,
						     protocol, 2, target_size));
		break;

	case offsetof(struct bpf_sk_lookup, remote_ip4):
		*insn++ = BPF_LDX_MEM(BPF_W, si->dst_reg, si->src_reg,
				      bpf_target_off(struct bpf_sk_lookup_kern,
						     v4.saddr, 4, target_size));
		break;

	case offsetof(struct bpf_sk_lookup, local_ip4):
		*insn++ = BPF_LDX_MEM(BPF_W, si->dst_reg, si->src_reg,
				      bpf_target_off(struct bpf_sk_lookup_kern,
						     v4.daddr, 4, target_size));
		break;

	case bpf_ctx_range_till(struct bpf_sk_lookup,
				remote_ip6[0], remote_ip6[3]): {
#if IS_ENABLED(CONFIG_IPV6)
		int off = si->off;

		off -= offsetof(struct bpf_sk_lookup, remote_ip6[0]);
		off += bpf_target_off(struct in6_addr, s6_addr32[0], 4, target_size);
		*insn++ = BPF_LDX_MEM(BPF_SIZEOF(void *), si->dst_reg, si->src_reg,
				      offsetof(struct bpf_sk_lookup_kern, v6.saddr));
		*insn++ = BPF_JMP_IMM(BPF_JEQ, si->dst_reg, 0, 1);
		*insn++ = BPF_LDX_MEM(BPF_W, si->dst_reg, si->dst_reg, off);
#else
		*insn++ = BPF_MOV32_IMM(si->dst_reg, 0);
#endif
		break;
	}
	case bpf_ctx_range_till(struct bpf_sk_lookup,
				local_ip6[0], local_ip6[3]): {
#if IS_ENABLED(CONFIG_IPV6)
		int off = si->off;

		off -= offsetof(struct bpf_sk_lookup, local_ip6[0]);
		off += bpf_target_off(struct in6_addr, s6_addr32[0], 4, target_size);
		*insn++ = BPF_LDX_MEM(BPF_SIZEOF(void *), si->dst_reg, si->src_reg,
				      offsetof(struct bpf_sk_lookup_kern, v6.daddr));
		*insn++ = BPF_JMP_IMM(BPF_JEQ, si->dst_reg, 0, 1);
		*insn++ = BPF_LDX_MEM(BPF_W, si->dst_reg, si->dst_reg, off);
#else
		*insn++ = BPF_MOV32_IMM(si->dst_reg, 0);
#endif
		break;
	}
	case offsetof(struct bpf_sk_lookup, remote_port):
		*insn++ = BPF_LDX_MEM(BPF_H, si->dst_reg, si->src_reg,
				      bpf_target_off(struct bpf_sk_lookup_kern,
						     sport, 2, target_size));
		break;

	case offsetof(struct bpf_sk_lookup, local_port):
		*insn++ = BPF_LDX_MEM(BPF_H, si->dst_reg, si->src_reg,
				      bpf_target_off(struct bpf_sk_lookup_kern,
						     dport, 2, target_size));
		break;
	}

	return insn - insn_buf;
}

const struct bpf_prog_ops sk_lookup_prog_ops = {
	.test_run = bpf_prog_test_run_sk_lookup,
};

const struct bpf_verifier_ops sk_lookup_verifier_ops = {
	.get_func_proto		= sk_lookup_func_proto,
	.is_valid_access	= sk_lookup_is_valid_access,
	.convert_ctx_access	= sk_lookup_convert_ctx_access,
};

#endif /* CONFIG_INET */

DEFINE_BPF_DISPATCHER(xdp)

void bpf_prog_change_xdp(struct bpf_prog *prev_prog, struct bpf_prog *prog)
{
	bpf_dispatcher_change_prog(BPF_DISPATCHER_PTR(xdp), prev_prog, prog);
}

#ifdef CONFIG_DEBUG_INFO_BTF
BTF_ID_LIST_GLOBAL(btf_sock_ids)
#define BTF_SOCK_TYPE(name, type) BTF_ID(struct, type)
BTF_SOCK_TYPE_xxx
#undef BTF_SOCK_TYPE
#else
u32 btf_sock_ids[MAX_BTF_SOCK_TYPE];
#endif

BPF_CALL_1(bpf_skc_to_tcp6_sock, struct sock *, sk)
{
	/* tcp6_sock type is not generated in dwarf and hence btf,
	 * trigger an explicit type generation here.
	 */
	BTF_TYPE_EMIT(struct tcp6_sock);
	if (sk && sk_fullsock(sk) && sk->sk_protocol == IPPROTO_TCP &&
	    sk->sk_family == AF_INET6)
		return (unsigned long)sk;

	return (unsigned long)NULL;
}

const struct bpf_func_proto bpf_skc_to_tcp6_sock_proto = {
	.func			= bpf_skc_to_tcp6_sock,
	.gpl_only		= false,
	.ret_type		= RET_PTR_TO_BTF_ID_OR_NULL,
	.arg1_type		= ARG_PTR_TO_BTF_ID_SOCK_COMMON,
	.ret_btf_id		= &btf_sock_ids[BTF_SOCK_TYPE_TCP6],
};

BPF_CALL_1(bpf_skc_to_tcp_sock, struct sock *, sk)
{
	if (sk && sk_fullsock(sk) && sk->sk_protocol == IPPROTO_TCP)
		return (unsigned long)sk;

	return (unsigned long)NULL;
}

const struct bpf_func_proto bpf_skc_to_tcp_sock_proto = {
	.func			= bpf_skc_to_tcp_sock,
	.gpl_only		= false,
	.ret_type		= RET_PTR_TO_BTF_ID_OR_NULL,
	.arg1_type		= ARG_PTR_TO_BTF_ID_SOCK_COMMON,
	.ret_btf_id		= &btf_sock_ids[BTF_SOCK_TYPE_TCP],
};

BPF_CALL_1(bpf_skc_to_tcp_timewait_sock, struct sock *, sk)
{
	/* BTF types for tcp_timewait_sock and inet_timewait_sock are not
	 * generated if CONFIG_INET=n. Trigger an explicit generation here.
	 */
	BTF_TYPE_EMIT(struct inet_timewait_sock);
	BTF_TYPE_EMIT(struct tcp_timewait_sock);

#ifdef CONFIG_INET
	if (sk && sk->sk_prot == &tcp_prot && sk->sk_state == TCP_TIME_WAIT)
		return (unsigned long)sk;
#endif

#if IS_BUILTIN(CONFIG_IPV6)
	if (sk && sk->sk_prot == &tcpv6_prot && sk->sk_state == TCP_TIME_WAIT)
		return (unsigned long)sk;
#endif

	return (unsigned long)NULL;
}

const struct bpf_func_proto bpf_skc_to_tcp_timewait_sock_proto = {
	.func			= bpf_skc_to_tcp_timewait_sock,
	.gpl_only		= false,
	.ret_type		= RET_PTR_TO_BTF_ID_OR_NULL,
	.arg1_type		= ARG_PTR_TO_BTF_ID_SOCK_COMMON,
	.ret_btf_id		= &btf_sock_ids[BTF_SOCK_TYPE_TCP_TW],
};

BPF_CALL_1(bpf_skc_to_tcp_request_sock, struct sock *, sk)
{
#ifdef CONFIG_INET
	if (sk && sk->sk_prot == &tcp_prot && sk->sk_state == TCP_NEW_SYN_RECV)
		return (unsigned long)sk;
#endif

#if IS_BUILTIN(CONFIG_IPV6)
	if (sk && sk->sk_prot == &tcpv6_prot && sk->sk_state == TCP_NEW_SYN_RECV)
		return (unsigned long)sk;
#endif

	return (unsigned long)NULL;
}

const struct bpf_func_proto bpf_skc_to_tcp_request_sock_proto = {
	.func			= bpf_skc_to_tcp_request_sock,
	.gpl_only		= false,
	.ret_type		= RET_PTR_TO_BTF_ID_OR_NULL,
	.arg1_type		= ARG_PTR_TO_BTF_ID_SOCK_COMMON,
	.ret_btf_id		= &btf_sock_ids[BTF_SOCK_TYPE_TCP_REQ],
};

BPF_CALL_1(bpf_skc_to_udp6_sock, struct sock *, sk)
{
	/* udp6_sock type is not generated in dwarf and hence btf,
	 * trigger an explicit type generation here.
	 */
	BTF_TYPE_EMIT(struct udp6_sock);
	if (sk && sk_fullsock(sk) && sk->sk_protocol == IPPROTO_UDP &&
	    sk->sk_type == SOCK_DGRAM && sk->sk_family == AF_INET6)
		return (unsigned long)sk;

	return (unsigned long)NULL;
}

const struct bpf_func_proto bpf_skc_to_udp6_sock_proto = {
	.func			= bpf_skc_to_udp6_sock,
	.gpl_only		= false,
	.ret_type		= RET_PTR_TO_BTF_ID_OR_NULL,
	.arg1_type		= ARG_PTR_TO_BTF_ID_SOCK_COMMON,
	.ret_btf_id		= &btf_sock_ids[BTF_SOCK_TYPE_UDP6],
};

static const struct bpf_func_proto *
bpf_sk_base_func_proto(enum bpf_func_id func_id)
{
	const struct bpf_func_proto *func;

	switch (func_id) {
	case BPF_FUNC_skc_to_tcp6_sock:
		func = &bpf_skc_to_tcp6_sock_proto;
		break;
	case BPF_FUNC_skc_to_tcp_sock:
		func = &bpf_skc_to_tcp_sock_proto;
		break;
	case BPF_FUNC_skc_to_tcp_timewait_sock:
		func = &bpf_skc_to_tcp_timewait_sock_proto;
		break;
	case BPF_FUNC_skc_to_tcp_request_sock:
		func = &bpf_skc_to_tcp_request_sock_proto;
		break;
	case BPF_FUNC_skc_to_udp6_sock:
		func = &bpf_skc_to_udp6_sock_proto;
		break;
	default:
		return bpf_base_func_proto(func_id);
	}

	if (!perfmon_capable())
		return NULL;

	return func;
}
