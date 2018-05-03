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
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
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
#include <net/sch_generic.h>
#include <net/cls_cgroup.h>
#include <net/dst_metadata.h>
#include <net/dst.h>
#include <net/sock_reuseport.h>
#include <net/busy_poll.h>
#include <net/tcp.h>
#include <net/xfrm.h>
#include <linux/bpf_trace.h>
#include <net/xdp_sock.h>

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

BPF_CALL_0(bpf_get_raw_cpu_id)
{
	return raw_smp_processor_id();
}

static const struct bpf_func_proto bpf_get_raw_smp_processor_id_proto = {
	.func		= bpf_get_raw_cpu_id,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
};

static u32 convert_skb_access(int skb_field, int dst_reg, int src_reg,
			      struct bpf_insn *insn_buf)
{
	struct bpf_insn *insn = insn_buf;

	switch (skb_field) {
	case SKF_AD_MARK:
		BUILD_BUG_ON(FIELD_SIZEOF(struct sk_buff, mark) != 4);

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
		BUILD_BUG_ON(FIELD_SIZEOF(struct sk_buff, queue_mapping) != 2);

		*insn++ = BPF_LDX_MEM(BPF_H, dst_reg, src_reg,
				      offsetof(struct sk_buff, queue_mapping));
		break;

	case SKF_AD_VLAN_TAG:
	case SKF_AD_VLAN_TAG_PRESENT:
		BUILD_BUG_ON(FIELD_SIZEOF(struct sk_buff, vlan_tci) != 2);
		BUILD_BUG_ON(VLAN_TAG_PRESENT != 0x1000);

		/* dst_reg = *(u16 *) (src_reg + offsetof(vlan_tci)) */
		*insn++ = BPF_LDX_MEM(BPF_H, dst_reg, src_reg,
				      offsetof(struct sk_buff, vlan_tci));
		if (skb_field == SKF_AD_VLAN_TAG) {
			*insn++ = BPF_ALU32_IMM(BPF_AND, dst_reg,
						~VLAN_TAG_PRESENT);
		} else {
			/* dst_reg >>= 12 */
			*insn++ = BPF_ALU32_IMM(BPF_RSH, dst_reg, 12);
			/* dst_reg &= 1 */
			*insn++ = BPF_ALU32_IMM(BPF_AND, dst_reg, 1);
		}
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
		BUILD_BUG_ON(FIELD_SIZEOF(struct sk_buff, protocol) != 2);

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
		BUILD_BUG_ON(FIELD_SIZEOF(struct net_device, ifindex) != 4);
		BUILD_BUG_ON(FIELD_SIZEOF(struct net_device, type) != 2);

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
		BUILD_BUG_ON(FIELD_SIZEOF(struct sk_buff, hash) != 4);

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
		BUILD_BUG_ON(FIELD_SIZEOF(struct sk_buff, vlan_proto) != 2);

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
		*insn++ = BPF_MOV64_REG(BPF_REG_TMP, BPF_REG_H);
		*insn++ = BPF_ALU64_IMM(BPF_SUB, BPF_REG_TMP, offset);
		*insn++ = BPF_JMP_IMM(BPF_JSLT, BPF_REG_TMP, size, 2 + endian);
		*insn++ = BPF_LDX_MEM(BPF_SIZE(fp->code), BPF_REG_A, BPF_REG_D,
				      offset);
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
		if (target >= len || target < 0)			\
			goto err;					\
		insn->off = addrs ? addrs[target] - addrs[i] - 1 : 0;	\
		/* Adjust pc relative offset for 2nd or 3rd insn. */	\
		insn->off -= insn - tmp_insns;				\
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

static int __reuseport_attach_prog(struct bpf_prog *prog, struct sock *sk)
{
	struct bpf_prog *old_prog;
	int err;

	if (bpf_prog_size(prog->len) > sysctl_optmem_max)
		return -ENOMEM;

	if (sk_unhashed(sk) && sk->sk_reuseport) {
		err = reuseport_alloc(sk);
		if (err)
			return err;
	} else if (!rcu_access_pointer(sk->sk_reuseport_cb)) {
		/* The socket wasn't bound with SO_REUSEPORT */
		return -EINVAL;
	}

	old_prog = reuseport_attach_prog(sk, prog);
	if (old_prog)
		bpf_prog_destroy(old_prog);

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

	err = __reuseport_attach_prog(prog, sk);
	if (err < 0) {
		__bpf_prog_release(prog);
		return err;
	}

	return 0;
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
	struct bpf_prog *prog = __get_bpf(ufd, sk);
	int err;

	if (IS_ERR(prog))
		return PTR_ERR(prog);

	err = __reuseport_attach_prog(prog, sk);
	if (err < 0) {
		bpf_prog_put(prog);
		return err;
	}

	return 0;
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
	if (unlikely(offset > 0xffff))
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

	if (unlikely(offset > 0xffff))
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

	if (unlikely(__this_cpu_read(xmit_recursion) > XMIT_RECURSION_LIMIT)) {
		net_crit_ratelimited("bpf: recursion limit reached on datapath, buggy bpf program?\n");
		kfree_skb(skb);
		return -ENETDOWN;
	}

	skb->dev = dev;

	__this_cpu_inc(xmit_recursion);
	ret = dev_queue_xmit(skb);
	__this_cpu_dec(xmit_recursion);

	return ret;
}

static int __bpf_redirect_no_mac(struct sk_buff *skb, struct net_device *dev,
				 u32 flags)
{
	/* skb->mac_len is not set on normal egress */
	unsigned int mlen = skb->network_header - skb->mac_header;

	__skb_pull(skb, mlen);

	/* At ingress, the mac header has already been pulled once.
	 * At egress, skb_pospull_rcsum has to be done in case that
	 * the skb is originated from ingress (i.e. a forwarded skb)
	 * to ensure that rcsum starts at net header.
	 */
	if (!skb_at_tc_ingress(skb))
		skb_postpull_rcsum(skb, skb_mac_header(skb), mlen);
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

BPF_CALL_3(bpf_clone_redirect, struct sk_buff *, skb, u32, ifindex, u64, flags)
{
	struct net_device *dev;
	struct sk_buff *clone;
	int ret;

	if (unlikely(flags & ~(BPF_F_INGRESS)))
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

struct redirect_info {
	u32 ifindex;
	u32 flags;
	struct bpf_map *map;
	struct bpf_map *map_to_flush;
	unsigned long   map_owner;
};

static DEFINE_PER_CPU(struct redirect_info, redirect_info);

BPF_CALL_2(bpf_redirect, u32, ifindex, u64, flags)
{
	struct redirect_info *ri = this_cpu_ptr(&redirect_info);

	if (unlikely(flags & ~(BPF_F_INGRESS)))
		return TC_ACT_SHOT;

	ri->ifindex = ifindex;
	ri->flags = flags;

	return TC_ACT_REDIRECT;
}

int skb_do_redirect(struct sk_buff *skb)
{
	struct redirect_info *ri = this_cpu_ptr(&redirect_info);
	struct net_device *dev;

	dev = dev_get_by_index_rcu(dev_net(skb->dev), ri->ifindex);
	ri->ifindex = 0;
	if (unlikely(!dev)) {
		kfree_skb(skb);
		return -EINVAL;
	}

	return __bpf_redirect(skb, dev, ri->flags);
}

static const struct bpf_func_proto bpf_redirect_proto = {
	.func           = bpf_redirect,
	.gpl_only       = false,
	.ret_type       = RET_INTEGER,
	.arg1_type      = ARG_ANYTHING,
	.arg2_type      = ARG_ANYTHING,
};

BPF_CALL_4(bpf_sk_redirect_map, struct sk_buff *, skb,
	   struct bpf_map *, map, u32, key, u64, flags)
{
	struct tcp_skb_cb *tcb = TCP_SKB_CB(skb);

	/* If user passes invalid input drop the packet. */
	if (unlikely(flags & ~(BPF_F_INGRESS)))
		return SK_DROP;

	tcb->bpf.key = key;
	tcb->bpf.flags = flags;
	tcb->bpf.map = map;

	return SK_PASS;
}

struct sock *do_sk_redirect_map(struct sk_buff *skb)
{
	struct tcp_skb_cb *tcb = TCP_SKB_CB(skb);
	struct sock *sk = NULL;

	if (tcb->bpf.map) {
		sk = __sock_map_lookup_elem(tcb->bpf.map, tcb->bpf.key);

		tcb->bpf.key = 0;
		tcb->bpf.map = NULL;
	}

	return sk;
}

static const struct bpf_func_proto bpf_sk_redirect_map_proto = {
	.func           = bpf_sk_redirect_map,
	.gpl_only       = false,
	.ret_type       = RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type      = ARG_CONST_MAP_PTR,
	.arg3_type      = ARG_ANYTHING,
	.arg4_type      = ARG_ANYTHING,
};

BPF_CALL_4(bpf_msg_redirect_map, struct sk_msg_buff *, msg,
	   struct bpf_map *, map, u32, key, u64, flags)
{
	/* If user passes invalid input drop the packet. */
	if (unlikely(flags & ~(BPF_F_INGRESS)))
		return SK_DROP;

	msg->key = key;
	msg->flags = flags;
	msg->map = map;

	return SK_PASS;
}

struct sock *do_msg_redirect_map(struct sk_msg_buff *msg)
{
	struct sock *sk = NULL;

	if (msg->map) {
		sk = __sock_map_lookup_elem(msg->map, msg->key);

		msg->key = 0;
		msg->map = NULL;
	}

	return sk;
}

static const struct bpf_func_proto bpf_msg_redirect_map_proto = {
	.func           = bpf_msg_redirect_map,
	.gpl_only       = false,
	.ret_type       = RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type      = ARG_CONST_MAP_PTR,
	.arg3_type      = ARG_ANYTHING,
	.arg4_type      = ARG_ANYTHING,
};

BPF_CALL_2(bpf_msg_apply_bytes, struct sk_msg_buff *, msg, u32, bytes)
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

BPF_CALL_2(bpf_msg_cork_bytes, struct sk_msg_buff *, msg, u32, bytes)
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

BPF_CALL_4(bpf_msg_pull_data,
	   struct sk_msg_buff *, msg, u32, start, u32, end, u64, flags)
{
	unsigned int len = 0, offset = 0, copy = 0;
	struct scatterlist *sg = msg->sg_data;
	int first_sg, last_sg, i, shift;
	unsigned char *p, *to, *from;
	int bytes = end - start;
	struct page *page;

	if (unlikely(flags || end <= start))
		return -EINVAL;

	/* First find the starting scatterlist element */
	i = msg->sg_start;
	do {
		len = sg[i].length;
		offset += len;
		if (start < offset + len)
			break;
		i++;
		if (i == MAX_SKB_FRAGS)
			i = 0;
	} while (i != msg->sg_end);

	if (unlikely(start >= offset + len))
		return -EINVAL;

	if (!msg->sg_copy[i] && bytes <= len)
		goto out;

	first_sg = i;

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
		copy += sg[i].length;
		i++;
		if (i == MAX_SKB_FRAGS)
			i = 0;
		if (bytes < copy)
			break;
	} while (i != msg->sg_end);
	last_sg = i;

	if (unlikely(copy < end - start))
		return -EINVAL;

	page = alloc_pages(__GFP_NOWARN | GFP_ATOMIC, get_order(copy));
	if (unlikely(!page))
		return -ENOMEM;
	p = page_address(page);
	offset = 0;

	i = first_sg;
	do {
		from = sg_virt(&sg[i]);
		len = sg[i].length;
		to = p + offset;

		memcpy(to, from, len);
		offset += len;
		sg[i].length = 0;
		put_page(sg_page(&sg[i]));

		i++;
		if (i == MAX_SKB_FRAGS)
			i = 0;
	} while (i != last_sg);

	sg[first_sg].length = copy;
	sg_set_page(&sg[first_sg], page, copy, 0);

	/* To repair sg ring we need to shift entries. If we only
	 * had a single entry though we can just replace it and
	 * be done. Otherwise walk the ring and shift the entries.
	 */
	shift = last_sg - first_sg - 1;
	if (!shift)
		goto out;

	i = first_sg + 1;
	do {
		int move_from;

		if (i + shift >= MAX_SKB_FRAGS)
			move_from = i + shift - MAX_SKB_FRAGS;
		else
			move_from = i + shift;

		if (move_from == msg->sg_end)
			break;

		sg[i] = sg[move_from];
		sg[move_from].length = 0;
		sg[move_from].page_link = 0;
		sg[move_from].offset = 0;

		i++;
		if (i == MAX_SKB_FRAGS)
			i = 0;
	} while (1);
	msg->sg_end -= shift;
	if (msg->sg_end < 0)
		msg->sg_end += MAX_SKB_FRAGS;
out:
	msg->data = sg_virt(&sg[i]) + start - offset;
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

	/* SCTP uses GSO_BY_FRAGS, thus cannot adjust it. */
	if (skb_is_gso(skb) && unlikely(skb_is_gso_sctp(skb)))
		return -ENOTSUPP;

	ret = skb_cow(skb, len_diff);
	if (unlikely(ret < 0))
		return ret;

	ret = bpf_skb_net_hdr_push(skb, off, len_diff);
	if (unlikely(ret < 0))
		return ret;

	if (skb_is_gso(skb)) {
		struct skb_shared_info *shinfo = skb_shinfo(skb);

		/* SKB_GSO_TCPV4 needs to be changed into
		 * SKB_GSO_TCPV6.
		 */
		if (shinfo->gso_type & SKB_GSO_TCPV4) {
			shinfo->gso_type &= ~SKB_GSO_TCPV4;
			shinfo->gso_type |=  SKB_GSO_TCPV6;
		}

		/* Due to IPv6 header, MSS needs to be downgraded. */
		skb_decrease_gso_size(shinfo, len_diff);
		/* Header must be checked, and gso_segs recomputed. */
		shinfo->gso_type |= SKB_GSO_DODGY;
		shinfo->gso_segs = 0;
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

	/* SCTP uses GSO_BY_FRAGS, thus cannot adjust it. */
	if (skb_is_gso(skb) && unlikely(skb_is_gso_sctp(skb)))
		return -ENOTSUPP;

	ret = skb_unclone(skb, GFP_ATOMIC);
	if (unlikely(ret < 0))
		return ret;

	ret = bpf_skb_net_hdr_pop(skb, off, len_diff);
	if (unlikely(ret < 0))
		return ret;

	if (skb_is_gso(skb)) {
		struct skb_shared_info *shinfo = skb_shinfo(skb);

		/* SKB_GSO_TCPV6 needs to be changed into
		 * SKB_GSO_TCPV4.
		 */
		if (shinfo->gso_type & SKB_GSO_TCPV6) {
			shinfo->gso_type &= ~SKB_GSO_TCPV6;
			shinfo->gso_type |=  SKB_GSO_TCPV4;
		}

		/* Due to IPv4 header, MSS can be upgraded. */
		skb_increase_gso_size(shinfo, len_diff);
		/* Header must be checked, and gso_segs recomputed. */
		shinfo->gso_type |= SKB_GSO_DODGY;
		shinfo->gso_segs = 0;
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

static int bpf_skb_net_grow(struct sk_buff *skb, u32 len_diff)
{
	u32 off = skb_mac_header_len(skb) + bpf_skb_net_base_len(skb);
	int ret;

	/* SCTP uses GSO_BY_FRAGS, thus cannot adjust it. */
	if (skb_is_gso(skb) && unlikely(skb_is_gso_sctp(skb)))
		return -ENOTSUPP;

	ret = skb_cow(skb, len_diff);
	if (unlikely(ret < 0))
		return ret;

	ret = bpf_skb_net_hdr_push(skb, off, len_diff);
	if (unlikely(ret < 0))
		return ret;

	if (skb_is_gso(skb)) {
		struct skb_shared_info *shinfo = skb_shinfo(skb);

		/* Due to header grow, MSS needs to be downgraded. */
		skb_decrease_gso_size(shinfo, len_diff);
		/* Header must be checked, and gso_segs recomputed. */
		shinfo->gso_type |= SKB_GSO_DODGY;
		shinfo->gso_segs = 0;
	}

	return 0;
}

static int bpf_skb_net_shrink(struct sk_buff *skb, u32 len_diff)
{
	u32 off = skb_mac_header_len(skb) + bpf_skb_net_base_len(skb);
	int ret;

	/* SCTP uses GSO_BY_FRAGS, thus cannot adjust it. */
	if (skb_is_gso(skb) && unlikely(skb_is_gso_sctp(skb)))
		return -ENOTSUPP;

	ret = skb_unclone(skb, GFP_ATOMIC);
	if (unlikely(ret < 0))
		return ret;

	ret = bpf_skb_net_hdr_pop(skb, off, len_diff);
	if (unlikely(ret < 0))
		return ret;

	if (skb_is_gso(skb)) {
		struct skb_shared_info *shinfo = skb_shinfo(skb);

		/* Due to header shrink, MSS can be upgraded. */
		skb_increase_gso_size(shinfo, len_diff);
		/* Header must be checked, and gso_segs recomputed. */
		shinfo->gso_type |= SKB_GSO_DODGY;
		shinfo->gso_segs = 0;
	}

	return 0;
}

static u32 __bpf_skb_max_len(const struct sk_buff *skb)
{
	return skb->dev->mtu + skb->dev->hard_header_len;
}

static int bpf_skb_adjust_net(struct sk_buff *skb, s32 len_diff)
{
	bool trans_same = skb->transport_header == skb->network_header;
	u32 len_cur, len_diff_abs = abs(len_diff);
	u32 len_min = bpf_skb_net_base_len(skb);
	u32 len_max = __bpf_skb_max_len(skb);
	__be16 proto = skb->protocol;
	bool shrink = len_diff < 0;
	int ret;

	if (unlikely(len_diff_abs > 0xfffU))
		return -EFAULT;
	if (unlikely(proto != htons(ETH_P_IP) &&
		     proto != htons(ETH_P_IPV6)))
		return -ENOTSUPP;

	len_cur = skb->len - skb_network_offset(skb);
	if (skb_transport_header_was_set(skb) && !trans_same)
		len_cur = skb_network_header_len(skb);
	if ((shrink && (len_diff_abs >= len_cur ||
			len_cur - len_diff_abs < len_min)) ||
	    (!shrink && (skb->len + len_diff_abs > len_max &&
			 !skb_is_gso(skb))))
		return -ENOTSUPP;

	ret = shrink ? bpf_skb_net_shrink(skb, len_diff_abs) :
		       bpf_skb_net_grow(skb, len_diff_abs);

	bpf_compute_data_pointers(skb);
	return ret;
}

BPF_CALL_4(bpf_skb_adjust_room, struct sk_buff *, skb, s32, len_diff,
	   u32, mode, u64, flags)
{
	if (unlikely(flags))
		return -EINVAL;
	if (likely(mode == BPF_ADJ_ROOM_NET))
		return bpf_skb_adjust_net(skb, len_diff);

	return -ENOTSUPP;
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

BPF_CALL_3(bpf_skb_change_tail, struct sk_buff *, skb, u32, new_len,
	   u64, flags)
{
	u32 max_len = __bpf_skb_max_len(skb);
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

BPF_CALL_3(bpf_skb_change_head, struct sk_buff *, skb, u32, head_room,
	   u64, flags)
{
	u32 max_len = __bpf_skb_max_len(skb);
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
	}

	bpf_compute_data_pointers(skb);
	return 0;
}

static const struct bpf_func_proto bpf_skb_change_head_proto = {
	.func		= bpf_skb_change_head,
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
	void *data_end = xdp->data_end + offset;

	/* only shrinking is allowed for now. */
	if (unlikely(offset >= 0))
		return -EINVAL;

	if (unlikely(data_end < xdp->data + ETH_HLEN))
		return -EINVAL;

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

static int __bpf_tx_xdp(struct net_device *dev,
			struct bpf_map *map,
			struct xdp_buff *xdp,
			u32 index)
{
	struct xdp_frame *xdpf;
	int err;

	if (!dev->netdev_ops->ndo_xdp_xmit) {
		return -EOPNOTSUPP;
	}

	xdpf = convert_to_xdp_frame(xdp);
	if (unlikely(!xdpf))
		return -EOVERFLOW;

	err = dev->netdev_ops->ndo_xdp_xmit(dev, xdpf);
	if (err)
		return err;
	dev->netdev_ops->ndo_xdp_flush(dev);
	return 0;
}

static int __bpf_tx_xdp_map(struct net_device *dev_rx, void *fwd,
			    struct bpf_map *map,
			    struct xdp_buff *xdp,
			    u32 index)
{
	int err;

	switch (map->map_type) {
	case BPF_MAP_TYPE_DEVMAP: {
		struct net_device *dev = fwd;
		struct xdp_frame *xdpf;

		if (!dev->netdev_ops->ndo_xdp_xmit)
			return -EOPNOTSUPP;

		xdpf = convert_to_xdp_frame(xdp);
		if (unlikely(!xdpf))
			return -EOVERFLOW;

		/* TODO: move to inside map code instead, for bulk support
		 * err = dev_map_enqueue(dev, xdp);
		 */
		err = dev->netdev_ops->ndo_xdp_xmit(dev, xdpf);
		if (err)
			return err;
		__dev_map_insert_ctx(map, index);
		break;
	}
	case BPF_MAP_TYPE_CPUMAP: {
		struct bpf_cpu_map_entry *rcpu = fwd;

		err = cpu_map_enqueue(rcpu, xdp, dev_rx);
		if (err)
			return err;
		__cpu_map_insert_ctx(map, index);
		break;
	}
	case BPF_MAP_TYPE_XSKMAP: {
		struct xdp_sock *xs = fwd;

		err = __xsk_map_redirect(map, xdp, xs);
		return err;
	}
	default:
		break;
	}
	return 0;
}

void xdp_do_flush_map(void)
{
	struct redirect_info *ri = this_cpu_ptr(&redirect_info);
	struct bpf_map *map = ri->map_to_flush;

	ri->map_to_flush = NULL;
	if (map) {
		switch (map->map_type) {
		case BPF_MAP_TYPE_DEVMAP:
			__dev_map_flush(map);
			break;
		case BPF_MAP_TYPE_CPUMAP:
			__cpu_map_flush(map);
			break;
		case BPF_MAP_TYPE_XSKMAP:
			__xsk_map_flush(map);
			break;
		default:
			break;
		}
	}
}
EXPORT_SYMBOL_GPL(xdp_do_flush_map);

static void *__xdp_map_lookup_elem(struct bpf_map *map, u32 index)
{
	switch (map->map_type) {
	case BPF_MAP_TYPE_DEVMAP:
		return __dev_map_lookup_elem(map, index);
	case BPF_MAP_TYPE_CPUMAP:
		return __cpu_map_lookup_elem(map, index);
	case BPF_MAP_TYPE_XSKMAP:
		return __xsk_map_lookup_elem(map, index);
	default:
		return NULL;
	}
}

static inline bool xdp_map_invalid(const struct bpf_prog *xdp_prog,
				   unsigned long aux)
{
	return (unsigned long)xdp_prog->aux != aux;
}

static int xdp_do_redirect_map(struct net_device *dev, struct xdp_buff *xdp,
			       struct bpf_prog *xdp_prog)
{
	struct redirect_info *ri = this_cpu_ptr(&redirect_info);
	unsigned long map_owner = ri->map_owner;
	struct bpf_map *map = ri->map;
	u32 index = ri->ifindex;
	void *fwd = NULL;
	int err;

	ri->ifindex = 0;
	ri->map = NULL;
	ri->map_owner = 0;

	if (unlikely(xdp_map_invalid(xdp_prog, map_owner))) {
		err = -EFAULT;
		map = NULL;
		goto err;
	}

	fwd = __xdp_map_lookup_elem(map, index);
	if (!fwd) {
		err = -EINVAL;
		goto err;
	}
	if (ri->map_to_flush && ri->map_to_flush != map)
		xdp_do_flush_map();

	err = __bpf_tx_xdp_map(dev, fwd, map, xdp, index);
	if (unlikely(err))
		goto err;

	ri->map_to_flush = map;
	_trace_xdp_redirect_map(dev, xdp_prog, fwd, map, index);
	return 0;
err:
	_trace_xdp_redirect_map_err(dev, xdp_prog, fwd, map, index, err);
	return err;
}

int xdp_do_redirect(struct net_device *dev, struct xdp_buff *xdp,
		    struct bpf_prog *xdp_prog)
{
	struct redirect_info *ri = this_cpu_ptr(&redirect_info);
	struct net_device *fwd;
	u32 index = ri->ifindex;
	int err;

	if (ri->map)
		return xdp_do_redirect_map(dev, xdp, xdp_prog);

	fwd = dev_get_by_index_rcu(dev_net(dev), index);
	ri->ifindex = 0;
	if (unlikely(!fwd)) {
		err = -EINVAL;
		goto err;
	}

	err = __bpf_tx_xdp(fwd, NULL, xdp, 0);
	if (unlikely(err))
		goto err;

	_trace_xdp_redirect(dev, xdp_prog, index);
	return 0;
err:
	_trace_xdp_redirect_err(dev, xdp_prog, index, err);
	return err;
}
EXPORT_SYMBOL_GPL(xdp_do_redirect);

static int __xdp_generic_ok_fwd_dev(struct sk_buff *skb, struct net_device *fwd)
{
	unsigned int len;

	if (unlikely(!(fwd->flags & IFF_UP)))
		return -ENETDOWN;

	len = fwd->mtu + fwd->hard_header_len + VLAN_HLEN;
	if (skb->len > len)
		return -EMSGSIZE;

	return 0;
}

static int xdp_do_generic_redirect_map(struct net_device *dev,
				       struct sk_buff *skb,
				       struct xdp_buff *xdp,
				       struct bpf_prog *xdp_prog)
{
	struct redirect_info *ri = this_cpu_ptr(&redirect_info);
	unsigned long map_owner = ri->map_owner;
	struct bpf_map *map = ri->map;
	u32 index = ri->ifindex;
	void *fwd = NULL;
	int err = 0;

	ri->ifindex = 0;
	ri->map = NULL;
	ri->map_owner = 0;

	if (unlikely(xdp_map_invalid(xdp_prog, map_owner))) {
		err = -EFAULT;
		map = NULL;
		goto err;
	}
	fwd = __xdp_map_lookup_elem(map, index);
	if (unlikely(!fwd)) {
		err = -EINVAL;
		goto err;
	}

	if (map->map_type == BPF_MAP_TYPE_DEVMAP) {
		if (unlikely((err = __xdp_generic_ok_fwd_dev(skb, fwd))))
			goto err;
		skb->dev = fwd;
		generic_xdp_tx(skb, xdp_prog);
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
	struct redirect_info *ri = this_cpu_ptr(&redirect_info);
	u32 index = ri->ifindex;
	struct net_device *fwd;
	int err = 0;

	if (ri->map)
		return xdp_do_generic_redirect_map(dev, skb, xdp, xdp_prog);

	ri->ifindex = 0;
	fwd = dev_get_by_index_rcu(dev_net(dev), index);
	if (unlikely(!fwd)) {
		err = -EINVAL;
		goto err;
	}

	if (unlikely((err = __xdp_generic_ok_fwd_dev(skb, fwd))))
		goto err;

	skb->dev = fwd;
	_trace_xdp_redirect(dev, xdp_prog, index);
	generic_xdp_tx(skb, xdp_prog);
	return 0;
err:
	_trace_xdp_redirect_err(dev, xdp_prog, index, err);
	return err;
}
EXPORT_SYMBOL_GPL(xdp_do_generic_redirect);

BPF_CALL_2(bpf_xdp_redirect, u32, ifindex, u64, flags)
{
	struct redirect_info *ri = this_cpu_ptr(&redirect_info);

	if (unlikely(flags))
		return XDP_ABORTED;

	ri->ifindex = ifindex;
	ri->flags = flags;
	ri->map = NULL;
	ri->map_owner = 0;

	return XDP_REDIRECT;
}

static const struct bpf_func_proto bpf_xdp_redirect_proto = {
	.func           = bpf_xdp_redirect,
	.gpl_only       = false,
	.ret_type       = RET_INTEGER,
	.arg1_type      = ARG_ANYTHING,
	.arg2_type      = ARG_ANYTHING,
};

BPF_CALL_4(bpf_xdp_redirect_map, struct bpf_map *, map, u32, ifindex, u64, flags,
	   unsigned long, map_owner)
{
	struct redirect_info *ri = this_cpu_ptr(&redirect_info);

	if (unlikely(flags))
		return XDP_ABORTED;

	ri->ifindex = ifindex;
	ri->flags = flags;
	ri->map = map;
	ri->map_owner = map_owner;

	return XDP_REDIRECT;
}

/* Note, arg4 is hidden from users and populated by the verifier
 * with the right pointer.
 */
static const struct bpf_func_proto bpf_xdp_redirect_map_proto = {
	.func           = bpf_xdp_redirect_map,
	.gpl_only       = false,
	.ret_type       = RET_INTEGER,
	.arg1_type      = ARG_CONST_MAP_PTR,
	.arg2_type      = ARG_ANYTHING,
	.arg3_type      = ARG_ANYTHING,
};

bool bpf_helper_changes_pkt_data(void *func)
{
	if (func == bpf_skb_vlan_push ||
	    func == bpf_skb_vlan_pop ||
	    func == bpf_skb_store_bytes ||
	    func == bpf_skb_change_proto ||
	    func == bpf_skb_change_head ||
	    func == bpf_skb_change_tail ||
	    func == bpf_skb_adjust_room ||
	    func == bpf_skb_pull_data ||
	    func == bpf_clone_redirect ||
	    func == bpf_l3_csum_replace ||
	    func == bpf_l4_csum_replace ||
	    func == bpf_xdp_adjust_head ||
	    func == bpf_xdp_adjust_meta ||
	    func == bpf_msg_pull_data ||
	    func == bpf_xdp_adjust_tail)
		return true;

	return false;
}

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
	if (unlikely(skb_size > skb->len))
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

	if (flags & BPF_F_TUNINFO_IPV6) {
		memcpy(to->remote_ipv6, &info->key.u.ipv6.src,
		       sizeof(to->remote_ipv6));
		to->tunnel_label = be32_to_cpu(info->key.label);
	} else {
		to->remote_ipv4 = be32_to_cpu(info->key.u.ipv4.src);
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

	ip_tunnel_info_opts_set(info, from, size);

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
	if (unlikely(xdp_size > (unsigned long)(xdp->data_end - xdp->data)))
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

BPF_CALL_1(bpf_get_socket_cookie, struct sk_buff *, skb)
{
	return skb->sk ? sock_gen_cookie(skb->sk) : 0;
}

static const struct bpf_func_proto bpf_get_socket_cookie_proto = {
	.func           = bpf_get_socket_cookie,
	.gpl_only       = false,
	.ret_type       = RET_INTEGER,
	.arg1_type      = ARG_PTR_TO_CTX,
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

BPF_CALL_5(bpf_setsockopt, struct bpf_sock_ops_kern *, bpf_sock,
	   int, level, int, optname, char *, optval, int, optlen)
{
	struct sock *sk = bpf_sock->sk;
	int ret = 0;
	int val;

	if (!sk_fullsock(sk))
		return -EINVAL;

	if (level == SOL_SOCKET) {
		if (optlen != sizeof(int))
			return -EINVAL;
		val = *((int *)optval);

		/* Only some socketops are supported */
		switch (optname) {
		case SO_RCVBUF:
			sk->sk_userlocks |= SOCK_RCVBUF_LOCK;
			sk->sk_rcvbuf = max_t(int, val * 2, SOCK_MIN_RCVBUF);
			break;
		case SO_SNDBUF:
			sk->sk_userlocks |= SOCK_SNDBUF_LOCK;
			sk->sk_sndbuf = max_t(int, val * 2, SOCK_MIN_SNDBUF);
			break;
		case SO_MAX_PACING_RATE:
			sk->sk_max_pacing_rate = val;
			sk->sk_pacing_rate = min(sk->sk_pacing_rate,
						 sk->sk_max_pacing_rate);
			break;
		case SO_PRIORITY:
			sk->sk_priority = val;
			break;
		case SO_RCVLOWAT:
			if (val < 0)
				val = INT_MAX;
			sk->sk_rcvlowat = val ? : 1;
			break;
		case SO_MARK:
			sk->sk_mark = val;
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
			bool reinit = bpf_sock->op > BPF_SOCK_OPS_NEEDS_ECN;

			strncpy(name, optval, min_t(long, optlen,
						    TCP_CA_NAME_MAX-1));
			name[TCP_CA_NAME_MAX-1] = 0;
			ret = tcp_set_congestion_control(sk, name, false,
							 reinit);
		} else {
			struct tcp_sock *tp = tcp_sk(sk);

			if (optlen != sizeof(int))
				return -EINVAL;

			val = *((int *)optval);
			/* Only some options are supported */
			switch (optname) {
			case TCP_BPF_IW:
				if (val <= 0 || tp->data_segs_out > 0)
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

static const struct bpf_func_proto bpf_setsockopt_proto = {
	.func		= bpf_setsockopt,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
	.arg3_type	= ARG_ANYTHING,
	.arg4_type	= ARG_PTR_TO_MEM,
	.arg5_type	= ARG_CONST_SIZE,
};

BPF_CALL_5(bpf_getsockopt, struct bpf_sock_ops_kern *, bpf_sock,
	   int, level, int, optname, char *, optval, int, optlen)
{
	struct sock *sk = bpf_sock->sk;

	if (!sk_fullsock(sk))
		goto err_clear;

#ifdef CONFIG_INET
	if (level == SOL_TCP && sk->sk_prot->getsockopt == tcp_getsockopt) {
		if (optname == TCP_CONGESTION) {
			struct inet_connection_sock *icsk = inet_csk(sk);

			if (!icsk->icsk_ca_ops || optlen <= 1)
				goto err_clear;
			strncpy(optval, icsk->icsk_ca_ops->name, optlen);
			optval[optlen - 1] = 0;
		} else {
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

static const struct bpf_func_proto bpf_getsockopt_proto = {
	.func		= bpf_getsockopt,
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

	if (val)
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
	int err;

	/* Binding to port can be expensive so it's prohibited in the helper.
	 * Only binding to IP is supported.
	 */
	err = -EINVAL;
	if (addr->sa_family == AF_INET) {
		if (addr_len < sizeof(struct sockaddr_in))
			return err;
		if (((struct sockaddr_in *)addr)->sin_port != htons(0))
			return err;
		return __inet_bind(sk, addr, addr_len, true, false);
#if IS_ENABLED(CONFIG_IPV6)
	} else if (addr->sa_family == AF_INET6) {
		if (addr_len < SIN6_LEN_RFC2133)
			return err;
		if (((struct sockaddr_in6 *)addr)->sin6_port != htons(0))
			return err;
		/* ipv6_bpf_stub cannot be NULL, since it's called from
		 * bpf_cgroup_inet6_connect hook and ipv6 is already loaded
		 */
		return ipv6_bpf_stub->inet6_bind(sk, addr, addr_len, true, false);
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
	if (to->family == AF_INET6) {
		memcpy(to->remote_ipv6, x->props.saddr.a6,
		       sizeof(to->remote_ipv6));
	} else {
		to->remote_ipv4 = x->props.saddr.a4;
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

static const struct bpf_func_proto *
bpf_base_func_proto(enum bpf_func_id func_id)
{
	switch (func_id) {
	case BPF_FUNC_map_lookup_elem:
		return &bpf_map_lookup_elem_proto;
	case BPF_FUNC_map_update_elem:
		return &bpf_map_update_elem_proto;
	case BPF_FUNC_map_delete_elem:
		return &bpf_map_delete_elem_proto;
	case BPF_FUNC_get_prandom_u32:
		return &bpf_get_prandom_u32_proto;
	case BPF_FUNC_get_smp_processor_id:
		return &bpf_get_raw_smp_processor_id_proto;
	case BPF_FUNC_get_numa_node_id:
		return &bpf_get_numa_node_id_proto;
	case BPF_FUNC_tail_call:
		return &bpf_tail_call_proto;
	case BPF_FUNC_ktime_get_ns:
		return &bpf_ktime_get_ns_proto;
	case BPF_FUNC_trace_printk:
		if (capable(CAP_SYS_ADMIN))
			return bpf_get_trace_printk_proto();
	default:
		return NULL;
	}
}

static const struct bpf_func_proto *
sock_filter_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	switch (func_id) {
	/* inet and inet6 sockets are created in a process
	 * context so there is always a valid uid/gid
	 */
	case BPF_FUNC_get_current_uid_gid:
		return &bpf_get_current_uid_gid_proto;
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
	default:
		return bpf_base_func_proto(func_id);
	}
}

static const struct bpf_func_proto *
sk_filter_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	switch (func_id) {
	case BPF_FUNC_skb_load_bytes:
		return &bpf_skb_load_bytes_proto;
	case BPF_FUNC_get_socket_cookie:
		return &bpf_get_socket_cookie_proto;
	case BPF_FUNC_get_socket_uid:
		return &bpf_get_socket_uid_proto;
	default:
		return bpf_base_func_proto(func_id);
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
	case BPF_FUNC_skb_pull_data:
		return &bpf_skb_pull_data_proto;
	case BPF_FUNC_csum_diff:
		return &bpf_csum_diff_proto;
	case BPF_FUNC_csum_update:
		return &bpf_csum_update_proto;
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
#ifdef CONFIG_XFRM
	case BPF_FUNC_skb_get_xfrm_state:
		return &bpf_skb_get_xfrm_state_proto;
#endif
	default:
		return bpf_base_func_proto(func_id);
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
	default:
		return bpf_base_func_proto(func_id);
	}
}

static const struct bpf_func_proto *
lwt_inout_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
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
		return bpf_base_func_proto(func_id);
	}
}

static const struct bpf_func_proto *
sock_ops_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	switch (func_id) {
	case BPF_FUNC_setsockopt:
		return &bpf_setsockopt_proto;
	case BPF_FUNC_getsockopt:
		return &bpf_getsockopt_proto;
	case BPF_FUNC_sock_ops_cb_flags_set:
		return &bpf_sock_ops_cb_flags_set_proto;
	case BPF_FUNC_sock_map_update:
		return &bpf_sock_map_update_proto;
	default:
		return bpf_base_func_proto(func_id);
	}
}

static const struct bpf_func_proto *
sk_msg_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	switch (func_id) {
	case BPF_FUNC_msg_redirect_map:
		return &bpf_msg_redirect_map_proto;
	case BPF_FUNC_msg_apply_bytes:
		return &bpf_msg_apply_bytes_proto;
	case BPF_FUNC_msg_cork_bytes:
		return &bpf_msg_cork_bytes_proto;
	case BPF_FUNC_msg_pull_data:
		return &bpf_msg_pull_data_proto;
	default:
		return bpf_base_func_proto(func_id);
	}
}

static const struct bpf_func_proto *
sk_skb_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	switch (func_id) {
	case BPF_FUNC_skb_store_bytes:
		return &bpf_skb_store_bytes_proto;
	case BPF_FUNC_skb_load_bytes:
		return &bpf_skb_load_bytes_proto;
	case BPF_FUNC_skb_pull_data:
		return &bpf_skb_pull_data_proto;
	case BPF_FUNC_skb_change_tail:
		return &bpf_skb_change_tail_proto;
	case BPF_FUNC_skb_change_head:
		return &bpf_skb_change_head_proto;
	case BPF_FUNC_get_socket_cookie:
		return &bpf_get_socket_cookie_proto;
	case BPF_FUNC_get_socket_uid:
		return &bpf_get_socket_uid_proto;
	case BPF_FUNC_sk_redirect_map:
		return &bpf_sk_redirect_map_proto;
	default:
		return bpf_base_func_proto(func_id);
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
	case BPF_FUNC_l3_csum_replace:
		return &bpf_l3_csum_replace_proto;
	case BPF_FUNC_l4_csum_replace:
		return &bpf_l4_csum_replace_proto;
	case BPF_FUNC_set_hash_invalid:
		return &bpf_set_hash_invalid_proto;
	default:
		return lwt_inout_func_proto(func_id, prog);
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

static bool lwt_is_valid_access(int off, int size,
				enum bpf_access_type type,
				const struct bpf_prog *prog,
				struct bpf_insn_access_aux *info)
{
	switch (off) {
	case bpf_ctx_range(struct __sk_buff, tc_classid):
	case bpf_ctx_range_till(struct __sk_buff, family, local_port):
	case bpf_ctx_range(struct __sk_buff, data_meta):
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

static bool __sock_filter_check_size(int off, int size,
				     struct bpf_insn_access_aux *info)
{
	const int size_default = sizeof(__u32);

	switch (off) {
	case bpf_ctx_range(struct bpf_sock, src_ip4):
	case bpf_ctx_range_till(struct bpf_sock, src_ip6[0], src_ip6[3]):
		bpf_ctx_record_field_size(info, size_default);
		return bpf_ctx_narrow_access_ok(off, size, size_default);
	}

	return size == size_default;
}

static bool sock_filter_is_valid_access(int off, int size,
					enum bpf_access_type type,
					const struct bpf_prog *prog,
					struct bpf_insn_access_aux *info)
{
	if (off < 0 || off >= sizeof(struct bpf_sock))
		return false;
	if (off % size != 0)
		return false;
	if (!__sock_filter_check_attach_type(off, type,
					     prog->expected_attach_type))
		return false;
	if (!__sock_filter_check_size(off, size, info))
		return false;
	return true;
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

	/* We're guaranteed here that CTX is in R6. */
	*insn++ = BPF_MOV64_REG(BPF_REG_1, BPF_REG_CTX);
	if (!indirect) {
		*insn++ = BPF_MOV64_IMM(BPF_REG_2, orig->imm);
	} else {
		*insn++ = BPF_MOV64_REG(BPF_REG_2, orig->src_reg);
		if (orig->imm)
			*insn++ = BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, orig->imm);
	}

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
	if (type == BPF_WRITE)
		return false;

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

	WARN_ONCE(1, "%s XDP return value %u, expect packet loss!\n",
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
			break;
		default:
			return false;
		}
		break;
	case bpf_ctx_range_till(struct bpf_sock_addr, user_ip6[0], user_ip6[3]):
		switch (prog->expected_attach_type) {
		case BPF_CGROUP_INET6_BIND:
		case BPF_CGROUP_INET6_CONNECT:
			break;
		default:
			return false;
		}
		break;
	}

	switch (off) {
	case bpf_ctx_range(struct bpf_sock_addr, user_ip4):
	case bpf_ctx_range_till(struct bpf_sock_addr, user_ip6[0], user_ip6[3]):
		/* Only narrow read access allowed for now. */
		if (type == BPF_READ) {
			bpf_ctx_record_field_size(info, size_default);
			if (!bpf_ctx_narrow_access_ok(off, size, size_default))
				return false;
		} else {
			if (size != size_default)
				return false;
		}
		break;
	case bpf_ctx_range(struct bpf_sock_addr, user_port):
		if (size != size_default)
			return false;
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

	switch (off) {
	case offsetof(struct sk_msg_md, data):
		info->reg_type = PTR_TO_PACKET;
		break;
	case offsetof(struct sk_msg_md, data_end):
		info->reg_type = PTR_TO_PACKET_END;
		break;
	}

	if (off < 0 || off >= sizeof(struct sk_msg_md))
		return false;
	if (off % size != 0)
		return false;
	if (size != sizeof(__u64))
		return false;

	return true;
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
		*insn++ = BPF_LDX_MEM(BPF_H, si->dst_reg, si->src_reg,
				      bpf_target_off(struct sk_buff, queue_mapping, 2,
						     target_size));
		break;

	case offsetof(struct __sk_buff, vlan_present):
	case offsetof(struct __sk_buff, vlan_tci):
		BUILD_BUG_ON(VLAN_TAG_PRESENT != 0x1000);

		*insn++ = BPF_LDX_MEM(BPF_H, si->dst_reg, si->src_reg,
				      bpf_target_off(struct sk_buff, vlan_tci, 2,
						     target_size));
		if (si->off == offsetof(struct __sk_buff, vlan_tci)) {
			*insn++ = BPF_ALU32_IMM(BPF_AND, si->dst_reg,
						~VLAN_TAG_PRESENT);
		} else {
			*insn++ = BPF_ALU32_IMM(BPF_RSH, si->dst_reg, 12);
			*insn++ = BPF_ALU32_IMM(BPF_AND, si->dst_reg, 1);
		}
		break;

	case offsetof(struct __sk_buff, cb[0]) ...
	     offsetofend(struct __sk_buff, cb[4]) - 1:
		BUILD_BUG_ON(FIELD_SIZEOF(struct qdisc_skb_cb, data) < 20);
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
		BUILD_BUG_ON(FIELD_SIZEOF(struct qdisc_skb_cb, tc_classid) != 2);

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
		BUILD_BUG_ON(FIELD_SIZEOF(struct sock_common, skc_family) != 2);

		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct sk_buff, sk),
				      si->dst_reg, si->src_reg,
				      offsetof(struct sk_buff, sk));
		*insn++ = BPF_LDX_MEM(BPF_H, si->dst_reg, si->dst_reg,
				      bpf_target_off(struct sock_common,
						     skc_family,
						     2, target_size));
		break;
	case offsetof(struct __sk_buff, remote_ip4):
		BUILD_BUG_ON(FIELD_SIZEOF(struct sock_common, skc_daddr) != 4);

		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct sk_buff, sk),
				      si->dst_reg, si->src_reg,
				      offsetof(struct sk_buff, sk));
		*insn++ = BPF_LDX_MEM(BPF_W, si->dst_reg, si->dst_reg,
				      bpf_target_off(struct sock_common,
						     skc_daddr,
						     4, target_size));
		break;
	case offsetof(struct __sk_buff, local_ip4):
		BUILD_BUG_ON(FIELD_SIZEOF(struct sock_common,
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
		BUILD_BUG_ON(FIELD_SIZEOF(struct sock_common,
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
		BUILD_BUG_ON(FIELD_SIZEOF(struct sock_common,
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
		BUILD_BUG_ON(FIELD_SIZEOF(struct sock_common, skc_dport) != 2);

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
		BUILD_BUG_ON(FIELD_SIZEOF(struct sock_common, skc_num) != 2);

		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct sk_buff, sk),
				      si->dst_reg, si->src_reg,
				      offsetof(struct sk_buff, sk));
		*insn++ = BPF_LDX_MEM(BPF_H, si->dst_reg, si->dst_reg,
				      bpf_target_off(struct sock_common,
						     skc_num, 2, target_size));
		break;
	}

	return insn - insn_buf;
}

static u32 sock_filter_convert_ctx_access(enum bpf_access_type type,
					  const struct bpf_insn *si,
					  struct bpf_insn *insn_buf,
					  struct bpf_prog *prog, u32 *target_size)
{
	struct bpf_insn *insn = insn_buf;
	int off;

	switch (si->off) {
	case offsetof(struct bpf_sock, bound_dev_if):
		BUILD_BUG_ON(FIELD_SIZEOF(struct sock, sk_bound_dev_if) != 4);

		if (type == BPF_WRITE)
			*insn++ = BPF_STX_MEM(BPF_W, si->dst_reg, si->src_reg,
					offsetof(struct sock, sk_bound_dev_if));
		else
			*insn++ = BPF_LDX_MEM(BPF_W, si->dst_reg, si->src_reg,
				      offsetof(struct sock, sk_bound_dev_if));
		break;

	case offsetof(struct bpf_sock, mark):
		BUILD_BUG_ON(FIELD_SIZEOF(struct sock, sk_mark) != 4);

		if (type == BPF_WRITE)
			*insn++ = BPF_STX_MEM(BPF_W, si->dst_reg, si->src_reg,
					offsetof(struct sock, sk_mark));
		else
			*insn++ = BPF_LDX_MEM(BPF_W, si->dst_reg, si->src_reg,
				      offsetof(struct sock, sk_mark));
		break;

	case offsetof(struct bpf_sock, priority):
		BUILD_BUG_ON(FIELD_SIZEOF(struct sock, sk_priority) != 4);

		if (type == BPF_WRITE)
			*insn++ = BPF_STX_MEM(BPF_W, si->dst_reg, si->src_reg,
					offsetof(struct sock, sk_priority));
		else
			*insn++ = BPF_LDX_MEM(BPF_W, si->dst_reg, si->src_reg,
				      offsetof(struct sock, sk_priority));
		break;

	case offsetof(struct bpf_sock, family):
		BUILD_BUG_ON(FIELD_SIZEOF(struct sock, sk_family) != 2);

		*insn++ = BPF_LDX_MEM(BPF_H, si->dst_reg, si->src_reg,
				      offsetof(struct sock, sk_family));
		break;

	case offsetof(struct bpf_sock, type):
		*insn++ = BPF_LDX_MEM(BPF_W, si->dst_reg, si->src_reg,
				      offsetof(struct sock, __sk_flags_offset));
		*insn++ = BPF_ALU32_IMM(BPF_AND, si->dst_reg, SK_FL_TYPE_MASK);
		*insn++ = BPF_ALU32_IMM(BPF_RSH, si->dst_reg, SK_FL_TYPE_SHIFT);
		break;

	case offsetof(struct bpf_sock, protocol):
		*insn++ = BPF_LDX_MEM(BPF_W, si->dst_reg, si->src_reg,
				      offsetof(struct sock, __sk_flags_offset));
		*insn++ = BPF_ALU32_IMM(BPF_AND, si->dst_reg, SK_FL_PROTO_MASK);
		*insn++ = BPF_ALU32_IMM(BPF_RSH, si->dst_reg, SK_FL_PROTO_SHIFT);
		break;

	case offsetof(struct bpf_sock, src_ip4):
		*insn++ = BPF_LDX_MEM(
			BPF_SIZE(si->code), si->dst_reg, si->src_reg,
			bpf_target_off(struct sock_common, skc_rcv_saddr,
				       FIELD_SIZEOF(struct sock_common,
						    skc_rcv_saddr),
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
				FIELD_SIZEOF(struct sock_common,
					     skc_v6_rcv_saddr.s6_addr32[0]),
				target_size) + off);
#else
		(void)off;
		*insn++ = BPF_MOV32_IMM(si->dst_reg, 0);
#endif
		break;

	case offsetof(struct bpf_sock, src_port):
		*insn++ = BPF_LDX_MEM(
			BPF_FIELD_SIZEOF(struct sock_common, skc_num),
			si->dst_reg, si->src_reg,
			bpf_target_off(struct sock_common, skc_num,
				       FIELD_SIZEOF(struct sock_common,
						    skc_num),
				       target_size));
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
			bpf_target_off(NS, NF, FIELD_SIZEOF(NS, NF),	       \
				       target_size)			       \
				+ OFF);					       \
	} while (0)

#define SOCK_ADDR_LOAD_NESTED_FIELD(S, NS, F, NF)			       \
	SOCK_ADDR_LOAD_NESTED_FIELD_SIZE_OFF(S, NS, F, NF,		       \
					     BPF_FIELD_SIZEOF(NS, NF), 0)

/* SOCK_ADDR_STORE_NESTED_FIELD_OFF() has semantic similar to
 * SOCK_ADDR_LOAD_NESTED_FIELD_SIZE_OFF() but for store operation.
 *
 * It doesn't support SIZE argument though since narrow stores are not
 * supported for now.
 *
 * In addition it uses Temporary Field TF (member of struct S) as the 3rd
 * "register" since two registers available in convert_ctx_access are not
 * enough: we can't override neither SRC, since it contains value to store, nor
 * DST since it contains pointer to context that may be used by later
 * instructions. But we need a temporary place to save pointer to nested
 * structure whose field we want to store to.
 */
#define SOCK_ADDR_STORE_NESTED_FIELD_OFF(S, NS, F, NF, OFF, TF)		       \
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
		*insn++ = BPF_STX_MEM(					       \
			BPF_FIELD_SIZEOF(NS, NF), tmp_reg, si->src_reg,	       \
			bpf_target_off(NS, NF, FIELD_SIZEOF(NS, NF),	       \
				       target_size)			       \
				+ OFF);					       \
		*insn++ = BPF_LDX_MEM(BPF_DW, tmp_reg, si->dst_reg,	       \
				      offsetof(S, TF));			       \
	} while (0)

#define SOCK_ADDR_LOAD_OR_STORE_NESTED_FIELD_SIZE_OFF(S, NS, F, NF, SIZE, OFF, \
						      TF)		       \
	do {								       \
		if (type == BPF_WRITE) {				       \
			SOCK_ADDR_STORE_NESTED_FIELD_OFF(S, NS, F, NF, OFF,    \
							 TF);		       \
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
	struct bpf_insn *insn = insn_buf;
	int off;

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
		BUILD_BUG_ON(FIELD_SIZEOF(struct sockaddr_in, sin_port) !=
			     FIELD_SIZEOF(struct sockaddr_in6, sin6_port));
		SOCK_ADDR_LOAD_OR_STORE_NESTED_FIELD(struct bpf_sock_addr_kern,
						     struct sockaddr_in6, uaddr,
						     sin6_port, tmp_reg);
		break;

	case offsetof(struct bpf_sock_addr, family):
		SOCK_ADDR_LOAD_NESTED_FIELD(struct bpf_sock_addr_kern,
					    struct sock, sk, sk_family);
		break;

	case offsetof(struct bpf_sock_addr, type):
		SOCK_ADDR_LOAD_NESTED_FIELD_SIZE_OFF(
			struct bpf_sock_addr_kern, struct sock, sk,
			__sk_flags_offset, BPF_W, 0);
		*insn++ = BPF_ALU32_IMM(BPF_AND, si->dst_reg, SK_FL_TYPE_MASK);
		*insn++ = BPF_ALU32_IMM(BPF_RSH, si->dst_reg, SK_FL_TYPE_SHIFT);
		break;

	case offsetof(struct bpf_sock_addr, protocol):
		SOCK_ADDR_LOAD_NESTED_FIELD_SIZE_OFF(
			struct bpf_sock_addr_kern, struct sock, sk,
			__sk_flags_offset, BPF_W, 0);
		*insn++ = BPF_ALU32_IMM(BPF_AND, si->dst_reg, SK_FL_PROTO_MASK);
		*insn++ = BPF_ALU32_IMM(BPF_RSH, si->dst_reg,
					SK_FL_PROTO_SHIFT);
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

	switch (si->off) {
	case offsetof(struct bpf_sock_ops, op) ...
	     offsetof(struct bpf_sock_ops, replylong[3]):
		BUILD_BUG_ON(FIELD_SIZEOF(struct bpf_sock_ops, op) !=
			     FIELD_SIZEOF(struct bpf_sock_ops_kern, op));
		BUILD_BUG_ON(FIELD_SIZEOF(struct bpf_sock_ops, reply) !=
			     FIELD_SIZEOF(struct bpf_sock_ops_kern, reply));
		BUILD_BUG_ON(FIELD_SIZEOF(struct bpf_sock_ops, replylong) !=
			     FIELD_SIZEOF(struct bpf_sock_ops_kern, replylong));
		off = si->off;
		off -= offsetof(struct bpf_sock_ops, op);
		off += offsetof(struct bpf_sock_ops_kern, op);
		if (type == BPF_WRITE)
			*insn++ = BPF_STX_MEM(BPF_W, si->dst_reg, si->src_reg,
					      off);
		else
			*insn++ = BPF_LDX_MEM(BPF_W, si->dst_reg, si->src_reg,
					      off);
		break;

	case offsetof(struct bpf_sock_ops, family):
		BUILD_BUG_ON(FIELD_SIZEOF(struct sock_common, skc_family) != 2);

		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(
					      struct bpf_sock_ops_kern, sk),
				      si->dst_reg, si->src_reg,
				      offsetof(struct bpf_sock_ops_kern, sk));
		*insn++ = BPF_LDX_MEM(BPF_H, si->dst_reg, si->dst_reg,
				      offsetof(struct sock_common, skc_family));
		break;

	case offsetof(struct bpf_sock_ops, remote_ip4):
		BUILD_BUG_ON(FIELD_SIZEOF(struct sock_common, skc_daddr) != 4);

		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(
						struct bpf_sock_ops_kern, sk),
				      si->dst_reg, si->src_reg,
				      offsetof(struct bpf_sock_ops_kern, sk));
		*insn++ = BPF_LDX_MEM(BPF_W, si->dst_reg, si->dst_reg,
				      offsetof(struct sock_common, skc_daddr));
		break;

	case offsetof(struct bpf_sock_ops, local_ip4):
		BUILD_BUG_ON(FIELD_SIZEOF(struct sock_common, skc_rcv_saddr) != 4);

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
		BUILD_BUG_ON(FIELD_SIZEOF(struct sock_common,
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
		BUILD_BUG_ON(FIELD_SIZEOF(struct sock_common,
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
		BUILD_BUG_ON(FIELD_SIZEOF(struct sock_common, skc_dport) != 2);

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
		BUILD_BUG_ON(FIELD_SIZEOF(struct sock_common, skc_num) != 2);

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
		BUILD_BUG_ON(FIELD_SIZEOF(struct sock_common, skc_state) != 1);

		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(
						struct bpf_sock_ops_kern, sk),
				      si->dst_reg, si->src_reg,
				      offsetof(struct bpf_sock_ops_kern, sk));
		*insn++ = BPF_LDX_MEM(BPF_B, si->dst_reg, si->dst_reg,
				      offsetof(struct sock_common, skc_state));
		break;

	case offsetof(struct bpf_sock_ops, rtt_min):
		BUILD_BUG_ON(FIELD_SIZEOF(struct tcp_sock, rtt_min) !=
			     sizeof(struct minmax));
		BUILD_BUG_ON(sizeof(struct minmax) <
			     sizeof(struct minmax_sample));

		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(
						struct bpf_sock_ops_kern, sk),
				      si->dst_reg, si->src_reg,
				      offsetof(struct bpf_sock_ops_kern, sk));
		*insn++ = BPF_LDX_MEM(BPF_W, si->dst_reg, si->dst_reg,
				      offsetof(struct tcp_sock, rtt_min) +
				      FIELD_SIZEOF(struct minmax_sample, t));
		break;

/* Helper macro for adding read access to tcp_sock or sock fields. */
#define SOCK_OPS_GET_FIELD(BPF_FIELD, OBJ_FIELD, OBJ)			      \
	do {								      \
		BUILD_BUG_ON(FIELD_SIZEOF(OBJ, OBJ_FIELD) >		      \
			     FIELD_SIZEOF(struct bpf_sock_ops, BPF_FIELD));   \
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(			      \
						struct bpf_sock_ops_kern,     \
						is_fullsock),		      \
				      si->dst_reg, si->src_reg,		      \
				      offsetof(struct bpf_sock_ops_kern,      \
					       is_fullsock));		      \
		*insn++ = BPF_JMP_IMM(BPF_JEQ, si->dst_reg, 0, 2);	      \
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(			      \
						struct bpf_sock_ops_kern, sk),\
				      si->dst_reg, si->src_reg,		      \
				      offsetof(struct bpf_sock_ops_kern, sk));\
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(OBJ,		      \
						       OBJ_FIELD),	      \
				      si->dst_reg, si->dst_reg,		      \
				      offsetof(OBJ, OBJ_FIELD));	      \
	} while (0)

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
		BUILD_BUG_ON(FIELD_SIZEOF(OBJ, OBJ_FIELD) >		      \
			     FIELD_SIZEOF(struct bpf_sock_ops, BPF_FIELD));   \
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

	case offsetof(struct bpf_sock_ops, snd_cwnd):
		SOCK_OPS_GET_FIELD(snd_cwnd, snd_cwnd, struct tcp_sock);
		break;

	case offsetof(struct bpf_sock_ops, srtt_us):
		SOCK_OPS_GET_FIELD(srtt_us, srtt_us, struct tcp_sock);
		break;

	case offsetof(struct bpf_sock_ops, bpf_sock_ops_cb_flags):
		SOCK_OPS_GET_FIELD(bpf_sock_ops_cb_flags, bpf_sock_ops_cb_flags,
				   struct tcp_sock);
		break;

	case offsetof(struct bpf_sock_ops, snd_ssthresh):
		SOCK_OPS_GET_FIELD(snd_ssthresh, snd_ssthresh, struct tcp_sock);
		break;

	case offsetof(struct bpf_sock_ops, rcv_nxt):
		SOCK_OPS_GET_FIELD(rcv_nxt, rcv_nxt, struct tcp_sock);
		break;

	case offsetof(struct bpf_sock_ops, snd_nxt):
		SOCK_OPS_GET_FIELD(snd_nxt, snd_nxt, struct tcp_sock);
		break;

	case offsetof(struct bpf_sock_ops, snd_una):
		SOCK_OPS_GET_FIELD(snd_una, snd_una, struct tcp_sock);
		break;

	case offsetof(struct bpf_sock_ops, mss_cache):
		SOCK_OPS_GET_FIELD(mss_cache, mss_cache, struct tcp_sock);
		break;

	case offsetof(struct bpf_sock_ops, ecn_flags):
		SOCK_OPS_GET_FIELD(ecn_flags, ecn_flags, struct tcp_sock);
		break;

	case offsetof(struct bpf_sock_ops, rate_delivered):
		SOCK_OPS_GET_FIELD(rate_delivered, rate_delivered,
				   struct tcp_sock);
		break;

	case offsetof(struct bpf_sock_ops, rate_interval_us):
		SOCK_OPS_GET_FIELD(rate_interval_us, rate_interval_us,
				   struct tcp_sock);
		break;

	case offsetof(struct bpf_sock_ops, packets_out):
		SOCK_OPS_GET_FIELD(packets_out, packets_out, struct tcp_sock);
		break;

	case offsetof(struct bpf_sock_ops, retrans_out):
		SOCK_OPS_GET_FIELD(retrans_out, retrans_out, struct tcp_sock);
		break;

	case offsetof(struct bpf_sock_ops, total_retrans):
		SOCK_OPS_GET_FIELD(total_retrans, total_retrans,
				   struct tcp_sock);
		break;

	case offsetof(struct bpf_sock_ops, segs_in):
		SOCK_OPS_GET_FIELD(segs_in, segs_in, struct tcp_sock);
		break;

	case offsetof(struct bpf_sock_ops, data_segs_in):
		SOCK_OPS_GET_FIELD(data_segs_in, data_segs_in, struct tcp_sock);
		break;

	case offsetof(struct bpf_sock_ops, segs_out):
		SOCK_OPS_GET_FIELD(segs_out, segs_out, struct tcp_sock);
		break;

	case offsetof(struct bpf_sock_ops, data_segs_out):
		SOCK_OPS_GET_FIELD(data_segs_out, data_segs_out,
				   struct tcp_sock);
		break;

	case offsetof(struct bpf_sock_ops, lost_out):
		SOCK_OPS_GET_FIELD(lost_out, lost_out, struct tcp_sock);
		break;

	case offsetof(struct bpf_sock_ops, sacked_out):
		SOCK_OPS_GET_FIELD(sacked_out, sacked_out, struct tcp_sock);
		break;

	case offsetof(struct bpf_sock_ops, sk_txhash):
		SOCK_OPS_GET_OR_SET_FIELD(sk_txhash, sk_txhash,
					  struct sock, type);
		break;

	case offsetof(struct bpf_sock_ops, bytes_received):
		SOCK_OPS_GET_FIELD(bytes_received, bytes_received,
				   struct tcp_sock);
		break;

	case offsetof(struct bpf_sock_ops, bytes_acked):
		SOCK_OPS_GET_FIELD(bytes_acked, bytes_acked, struct tcp_sock);
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

	switch (si->off) {
	case offsetof(struct sk_msg_md, data):
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct sk_msg_buff, data),
				      si->dst_reg, si->src_reg,
				      offsetof(struct sk_msg_buff, data));
		break;
	case offsetof(struct sk_msg_md, data_end):
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct sk_msg_buff, data_end),
				      si->dst_reg, si->src_reg,
				      offsetof(struct sk_msg_buff, data_end));
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
};

const struct bpf_prog_ops xdp_prog_ops = {
	.test_run		= bpf_prog_test_run_xdp,
};

const struct bpf_verifier_ops cg_skb_verifier_ops = {
	.get_func_proto		= sk_filter_func_proto,
	.is_valid_access	= sk_filter_is_valid_access,
	.convert_ctx_access	= bpf_convert_ctx_access,
};

const struct bpf_prog_ops cg_skb_prog_ops = {
	.test_run		= bpf_prog_test_run_skb,
};

const struct bpf_verifier_ops lwt_inout_verifier_ops = {
	.get_func_proto		= lwt_inout_func_proto,
	.is_valid_access	= lwt_is_valid_access,
	.convert_ctx_access	= bpf_convert_ctx_access,
};

const struct bpf_prog_ops lwt_inout_prog_ops = {
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

const struct bpf_verifier_ops cg_sock_verifier_ops = {
	.get_func_proto		= sock_filter_func_proto,
	.is_valid_access	= sock_filter_is_valid_access,
	.convert_ctx_access	= sock_filter_convert_ctx_access,
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
};

const struct bpf_prog_ops sk_msg_prog_ops = {
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
