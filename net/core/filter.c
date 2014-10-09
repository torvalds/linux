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
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/if_packet.h>
#include <linux/gfp.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <net/netlink.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <asm/uaccess.h>
#include <asm/unaligned.h>
#include <linux/filter.h>
#include <linux/ratelimit.h>
#include <linux/seccomp.h>
#include <linux/if_vlan.h>

/**
 *	sk_filter - run a packet through a socket filter
 *	@sk: sock associated with &sk_buff
 *	@skb: buffer to filter
 *
 * Run the filter code and then cut skb->data to correct size returned by
 * sk_run_filter. If pkt_len is 0 we toss packet. If skb->len is smaller
 * than pkt_len we keep whole skb->data. This is the socket level
 * wrapper to sk_run_filter. It returns 0 if the packet should
 * be accepted or -EPERM if the packet should be tossed.
 *
 */
int sk_filter(struct sock *sk, struct sk_buff *skb)
{
	int err;
	struct sk_filter *filter;

	/*
	 * If the skb was allocated from pfmemalloc reserves, only
	 * allow SOCK_MEMALLOC sockets to use it as this socket is
	 * helping free memory
	 */
	if (skb_pfmemalloc(skb) && !sock_flag(sk, SOCK_MEMALLOC))
		return -ENOMEM;

	err = security_sock_rcv_skb(sk, skb);
	if (err)
		return err;

	rcu_read_lock();
	filter = rcu_dereference(sk->sk_filter);
	if (filter) {
		unsigned int pkt_len = SK_RUN_FILTER(filter, skb);

		err = pkt_len ? pskb_trim(skb, pkt_len) : -EPERM;
	}
	rcu_read_unlock();

	return err;
}
EXPORT_SYMBOL(sk_filter);

static u64 __skb_get_pay_offset(u64 ctx, u64 a, u64 x, u64 r4, u64 r5)
{
	return skb_get_poff((struct sk_buff *)(unsigned long) ctx);
}

static u64 __skb_get_nlattr(u64 ctx, u64 a, u64 x, u64 r4, u64 r5)
{
	struct sk_buff *skb = (struct sk_buff *)(unsigned long) ctx;
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

static u64 __skb_get_nlattr_nest(u64 ctx, u64 a, u64 x, u64 r4, u64 r5)
{
	struct sk_buff *skb = (struct sk_buff *)(unsigned long) ctx;
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

static u64 __get_raw_cpu_id(u64 ctx, u64 a, u64 x, u64 r4, u64 r5)
{
	return raw_smp_processor_id();
}

/* note that this only generates 32-bit random numbers */
static u64 __get_random_u32(u64 ctx, u64 a, u64 x, u64 r4, u64 r5)
{
	return prandom_u32();
}

static bool convert_bpf_extensions(struct sock_filter *fp,
				   struct bpf_insn **insnp)
{
	struct bpf_insn *insn = *insnp;

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
		*insn++ = BPF_LDX_MEM(BPF_B, BPF_REG_A, BPF_REG_CTX,
				      PKT_TYPE_OFFSET());
		*insn = BPF_ALU32_IMM(BPF_AND, BPF_REG_A, PKT_TYPE_MAX);
#ifdef __BIG_ENDIAN_BITFIELD
		insn++;
                *insn = BPF_ALU32_IMM(BPF_RSH, BPF_REG_A, 5);
#endif
		break;

	case SKF_AD_OFF + SKF_AD_IFINDEX:
	case SKF_AD_OFF + SKF_AD_HATYPE:
		BUILD_BUG_ON(FIELD_SIZEOF(struct net_device, ifindex) != 4);
		BUILD_BUG_ON(FIELD_SIZEOF(struct net_device, type) != 2);
		BUILD_BUG_ON(bytes_to_bpf_size(FIELD_SIZEOF(struct sk_buff, dev)) < 0);

		*insn++ = BPF_LDX_MEM(bytes_to_bpf_size(FIELD_SIZEOF(struct sk_buff, dev)),
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
		BUILD_BUG_ON(FIELD_SIZEOF(struct sk_buff, mark) != 4);

		*insn = BPF_LDX_MEM(BPF_W, BPF_REG_A, BPF_REG_CTX,
				    offsetof(struct sk_buff, mark));
		break;

	case SKF_AD_OFF + SKF_AD_RXHASH:
		BUILD_BUG_ON(FIELD_SIZEOF(struct sk_buff, hash) != 4);

		*insn = BPF_LDX_MEM(BPF_W, BPF_REG_A, BPF_REG_CTX,
				    offsetof(struct sk_buff, hash));
		break;

	case SKF_AD_OFF + SKF_AD_QUEUE:
		BUILD_BUG_ON(FIELD_SIZEOF(struct sk_buff, queue_mapping) != 2);

		*insn = BPF_LDX_MEM(BPF_H, BPF_REG_A, BPF_REG_CTX,
				    offsetof(struct sk_buff, queue_mapping));
		break;

	case SKF_AD_OFF + SKF_AD_VLAN_TAG:
	case SKF_AD_OFF + SKF_AD_VLAN_TAG_PRESENT:
		BUILD_BUG_ON(FIELD_SIZEOF(struct sk_buff, vlan_tci) != 2);
		BUILD_BUG_ON(VLAN_TAG_PRESENT != 0x1000);

		/* A = *(u16 *) (CTX + offsetof(vlan_tci)) */
		*insn++ = BPF_LDX_MEM(BPF_H, BPF_REG_A, BPF_REG_CTX,
				      offsetof(struct sk_buff, vlan_tci));
		if (fp->k == SKF_AD_OFF + SKF_AD_VLAN_TAG) {
			*insn = BPF_ALU32_IMM(BPF_AND, BPF_REG_A,
					      ~VLAN_TAG_PRESENT);
		} else {
			/* A >>= 12 */
			*insn++ = BPF_ALU32_IMM(BPF_RSH, BPF_REG_A, 12);
			/* A &= 1 */
			*insn = BPF_ALU32_IMM(BPF_AND, BPF_REG_A, 1);
		}
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
			*insn = BPF_EMIT_CALL(__skb_get_pay_offset);
			break;
		case SKF_AD_OFF + SKF_AD_NLATTR:
			*insn = BPF_EMIT_CALL(__skb_get_nlattr);
			break;
		case SKF_AD_OFF + SKF_AD_NLATTR_NEST:
			*insn = BPF_EMIT_CALL(__skb_get_nlattr_nest);
			break;
		case SKF_AD_OFF + SKF_AD_CPU:
			*insn = BPF_EMIT_CALL(__get_raw_cpu_id);
			break;
		case SKF_AD_OFF + SKF_AD_RANDOM:
			*insn = BPF_EMIT_CALL(__get_random_u32);
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

/**
 *	bpf_convert_filter - convert filter program
 *	@prog: the user passed filter program
 *	@len: the length of the user passed filter program
 *	@new_prog: buffer where converted program will be stored
 *	@new_len: pointer to store length of converted program
 *
 * Remap 'sock_filter' style BPF instruction set to 'sock_filter_ext' style.
 * Conversion workflow:
 *
 * 1) First pass for calculating the new program length:
 *   bpf_convert_filter(old_prog, old_len, NULL, &new_len)
 *
 * 2) 2nd pass to remap in two passes: 1st pass finds new
 *    jump offsets, 2nd pass remapping:
 *   new_prog = kmalloc(sizeof(struct bpf_insn) * new_len);
 *   bpf_convert_filter(old_prog, old_len, new_prog, &new_len);
 *
 * User BPF's register A is mapped to our BPF register 6, user BPF
 * register X is mapped to BPF register 7; frame pointer is always
 * register 10; Context 'void *ctx' is stored in register 1, that is,
 * for socket filters: ctx == 'struct sk_buff *', for seccomp:
 * ctx == 'struct seccomp_data *'.
 */
int bpf_convert_filter(struct sock_filter *prog, int len,
		       struct bpf_insn *new_prog, int *new_len)
{
	int new_flen = 0, pass = 0, target, i;
	struct bpf_insn *new_insn;
	struct sock_filter *fp;
	int *addrs = NULL;
	u8 bpf_src;

	BUILD_BUG_ON(BPF_MEMWORDS * sizeof(u32) > MAX_BPF_STACK);
	BUILD_BUG_ON(BPF_REG_FP + 1 != MAX_BPF_REG);

	if (len <= 0 || len > BPF_MAXINSNS)
		return -EINVAL;

	if (new_prog) {
		addrs = kcalloc(len, sizeof(*addrs), GFP_KERNEL);
		if (!addrs)
			return -ENOMEM;
	}

do_pass:
	new_insn = new_prog;
	fp = prog;

	if (new_insn)
		*new_insn = BPF_MOV64_REG(BPF_REG_CTX, BPF_REG_ARG1);
	new_insn++;

	for (i = 0; i < len; fp++, i++) {
		struct bpf_insn tmp_insns[6] = { };
		struct bpf_insn *insn = tmp_insns;

		if (addrs)
			addrs[i] = new_insn - new_prog;

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
				insn->src_reg = BPF_REG_X;
				insn->imm = fp->k;
				bpf_src = BPF_SRC(fp->code);
			}

			/* Common case where 'jump_false' is next insn. */
			if (fp->jf == 0) {
				insn->code = BPF_JMP | BPF_OP(fp->code) | bpf_src;
				target = i + fp->jt + 1;
				BPF_EMIT_JMP;
				break;
			}

			/* Convert JEQ into JNE when 'jump_true' is next insn. */
			if (fp->jt == 0 && BPF_OP(fp->code) == BPF_JEQ) {
				insn->code = BPF_JMP | BPF_JNE | bpf_src;
				target = i + fp->jf + 1;
				BPF_EMIT_JMP;
				break;
			}

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
		case BPF_LDX | BPF_MSH | BPF_B:
			/* tmp = A */
			*insn++ = BPF_MOV64_REG(BPF_REG_TMP, BPF_REG_A);
			/* A = BPF_R0 = *(u8 *) (skb->data + K) */
			*insn++ = BPF_LD_ABS(BPF_B, fp->k);
			/* A &= 0xf */
			*insn++ = BPF_ALU32_IMM(BPF_AND, BPF_REG_A, 0xf);
			/* A <<= 2 */
			*insn++ = BPF_ALU32_IMM(BPF_LSH, BPF_REG_A, 2);
			/* X = A */
			*insn++ = BPF_MOV64_REG(BPF_REG_X, BPF_REG_A);
			/* A = tmp */
			*insn = BPF_MOV64_REG(BPF_REG_A, BPF_REG_TMP);
			break;

		/* RET_K, RET_A are remaped into 2 insns. */
		case BPF_RET | BPF_A:
		case BPF_RET | BPF_K:
			*insn++ = BPF_MOV32_RAW(BPF_RVAL(fp->code) == BPF_K ?
						BPF_K : BPF_X, BPF_REG_0,
						BPF_REG_A, fp->k);
			*insn = BPF_EXIT_INSN();
			break;

		/* Store to stack. */
		case BPF_ST:
		case BPF_STX:
			*insn = BPF_STX_MEM(BPF_W, BPF_REG_FP, BPF_CLASS(fp->code) ==
					    BPF_ST ? BPF_REG_A : BPF_REG_X,
					    -(BPF_MEMWORDS - fp->k) * 4);
			break;

		/* Load from stack. */
		case BPF_LD | BPF_MEM:
		case BPF_LDX | BPF_MEM:
			*insn = BPF_LDX_MEM(BPF_W, BPF_CLASS(fp->code) == BPF_LD  ?
					    BPF_REG_A : BPF_REG_X, BPF_REG_FP,
					    -(BPF_MEMWORDS - fp->k) * 4);
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

		/* Unkown instruction. */
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
		*new_len = new_insn - new_prog;
		return 0;
	}

	pass++;
	if (new_flen != new_insn - new_prog) {
		new_flen = new_insn - new_prog;
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
 * A BPF program is able to use 16 cells of memory to store intermediate
 * values (check u32 mem[BPF_MEMWORDS] in sk_run_filter()).
 *
 * As we dont want to clear mem[] array for each packet going through
 * sk_run_filter(), we check that filter loaded by user never try to read
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
int bpf_check_classic(const struct sock_filter *filter, unsigned int flen)
{
	bool anc_found;
	int pc;

	if (flen == 0 || flen > BPF_MAXINSNS)
		return -EINVAL;

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
EXPORT_SYMBOL(bpf_check_classic);

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
	fkprog->filter = kmemdup(fp->insns, fsize, GFP_KERNEL);
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
	bpf_release_orig_filter(prog);
	bpf_prog_free(prog);
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
	if (atomic_dec_and_test(&fp->refcnt))
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
bool sk_filter_charge(struct sock *sk, struct sk_filter *fp)
{
	u32 filter_size = bpf_prog_size(fp->prog->len);

	/* same check as in sock_kmalloc() */
	if (filter_size <= sysctl_optmem_max &&
	    atomic_read(&sk->sk_omem_alloc) + filter_size < sysctl_optmem_max) {
		atomic_inc(&fp->refcnt);
		atomic_add(filter_size, &sk->sk_omem_alloc);
		return true;
	}
	return false;
}

static struct bpf_prog *bpf_migrate_filter(struct bpf_prog *fp)
{
	struct sock_filter *old_prog;
	struct bpf_prog *old_fp;
	int err, new_len, old_len = fp->len;

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
			   GFP_KERNEL);
	if (!old_prog) {
		err = -ENOMEM;
		goto out_err;
	}

	/* 1st pass: calculate the new program length. */
	err = bpf_convert_filter(old_prog, old_len, NULL, &new_len);
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
	err = bpf_convert_filter(old_prog, old_len, fp->insnsi, &new_len);
	if (err)
		/* 2nd bpf_convert_filter() can fail only if it fails
		 * to allocate memory, remapping must succeed. Note,
		 * that at this time old_fp has already been released
		 * by krealloc().
		 */
		goto out_err_free;

	bpf_prog_select_runtime(fp);

	kfree(old_prog);
	return fp;

out_err_free:
	kfree(old_prog);
out_err:
	__bpf_prog_release(fp);
	return ERR_PTR(err);
}

static struct bpf_prog *bpf_prepare_filter(struct bpf_prog *fp)
{
	int err;

	fp->bpf_func = NULL;
	fp->jited = false;

	err = bpf_check_classic(fp->insns, fp->len);
	if (err) {
		__bpf_prog_release(fp);
		return ERR_PTR(err);
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
	if (fprog->filter == NULL)
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
	fp = bpf_prepare_filter(fp);
	if (IS_ERR(fp))
		return PTR_ERR(fp);

	*pfp = fp;
	return 0;
}
EXPORT_SYMBOL_GPL(bpf_prog_create);

void bpf_prog_destroy(struct bpf_prog *fp)
{
	__bpf_prog_release(fp);
}
EXPORT_SYMBOL_GPL(bpf_prog_destroy);

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
	struct sk_filter *fp, *old_fp;
	unsigned int fsize = bpf_classic_proglen(fprog);
	unsigned int bpf_fsize = bpf_prog_size(fprog->len);
	struct bpf_prog *prog;
	int err;

	if (sock_flag(sk, SOCK_FILTER_LOCKED))
		return -EPERM;

	/* Make sure new filter is there and in the right amounts. */
	if (fprog->filter == NULL)
		return -EINVAL;

	prog = bpf_prog_alloc(bpf_fsize, 0);
	if (!prog)
		return -ENOMEM;

	if (copy_from_user(prog->insns, fprog->filter, fsize)) {
		__bpf_prog_free(prog);
		return -EFAULT;
	}

	prog->len = fprog->len;

	err = bpf_prog_store_orig_filter(prog, fprog);
	if (err) {
		__bpf_prog_free(prog);
		return -ENOMEM;
	}

	/* bpf_prepare_filter() already takes care of freeing
	 * memory in case something goes wrong.
	 */
	prog = bpf_prepare_filter(prog);
	if (IS_ERR(prog))
		return PTR_ERR(prog);

	fp = kmalloc(sizeof(*fp), GFP_KERNEL);
	if (!fp) {
		__bpf_prog_release(prog);
		return -ENOMEM;
	}
	fp->prog = prog;

	atomic_set(&fp->refcnt, 0);

	if (!sk_filter_charge(sk, fp)) {
		__sk_filter_release(fp);
		return -ENOMEM;
	}

	old_fp = rcu_dereference_protected(sk->sk_filter,
					   sock_owned_by_user(sk));
	rcu_assign_pointer(sk->sk_filter, fp);

	if (old_fp)
		sk_filter_uncharge(sk, old_fp);

	return 0;
}
EXPORT_SYMBOL_GPL(sk_attach_filter);

int sk_detach_filter(struct sock *sk)
{
	int ret = -ENOENT;
	struct sk_filter *filter;

	if (sock_flag(sk, SOCK_FILTER_LOCKED))
		return -EPERM;

	filter = rcu_dereference_protected(sk->sk_filter,
					   sock_owned_by_user(sk));
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
					   sock_owned_by_user(sk));
	if (!filter)
		goto out;

	/* We're copying the filter that has been originally attached,
	 * so no conversion/decode needed anymore.
	 */
	fprog = filter->prog->orig_prog;

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
