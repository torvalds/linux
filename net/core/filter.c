/*
 * Linux Socket Filter - Kernel level socket filtering
 *
 * Author:
 *     Jay Schulist <jschlst@samba.org>
 *
 * Based on the design of:
 *     - The Berkeley Packet Filter
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * Andi Kleen - Fix a few bad bugs and races.
 * Kris Katterjohn - Added many additional checks in sk_chk_filter()
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

/* No hurry in this branch
 *
 * Exported for the bpf jit load helper.
 */
void *bpf_internal_load_pointer_neg_helper(const struct sk_buff *skb, int k, unsigned int size)
{
	u8 *ptr = NULL;

	if (k >= SKF_NET_OFF)
		ptr = skb_network_header(skb) + k - SKF_NET_OFF;
	else if (k >= SKF_LL_OFF)
		ptr = skb_mac_header(skb) + k - SKF_LL_OFF;

	if (ptr >= skb->head && ptr + size <= skb_tail_pointer(skb))
		return ptr;
	return NULL;
}

static inline void *load_pointer(const struct sk_buff *skb, int k,
				 unsigned int size, void *buffer)
{
	if (k >= 0)
		return skb_header_pointer(skb, k, size, buffer);
	return bpf_internal_load_pointer_neg_helper(skb, k, size);
}

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

/**
 *	sk_run_filter - run a filter on a socket
 *	@skb: buffer to run the filter on
 *	@fentry: filter to apply
 *
 * Decode and apply filter instructions to the skb->data.
 * Return length to keep, 0 for none. @skb is the data we are
 * filtering, @filter is the array of filter instructions.
 * Because all jumps are guaranteed to be before last instruction,
 * and last instruction guaranteed to be a RET, we dont need to check
 * flen. (We used to pass to this function the length of filter)
 */
unsigned int sk_run_filter(const struct sk_buff *skb,
			   const struct sock_filter *fentry)
{
	void *ptr;
	u32 A = 0;			/* Accumulator */
	u32 X = 0;			/* Index Register */
	u32 mem[BPF_MEMWORDS];		/* Scratch Memory Store */
	u32 tmp;
	int k;

	/*
	 * Process array of filter instructions.
	 */
	for (;; fentry++) {
#if defined(CONFIG_X86_32)
#define	K (fentry->k)
#else
		const u32 K = fentry->k;
#endif

		switch (fentry->code) {
		case BPF_S_ALU_ADD_X:
			A += X;
			continue;
		case BPF_S_ALU_ADD_K:
			A += K;
			continue;
		case BPF_S_ALU_SUB_X:
			A -= X;
			continue;
		case BPF_S_ALU_SUB_K:
			A -= K;
			continue;
		case BPF_S_ALU_MUL_X:
			A *= X;
			continue;
		case BPF_S_ALU_MUL_K:
			A *= K;
			continue;
		case BPF_S_ALU_DIV_X:
			if (X == 0)
				return 0;
			A /= X;
			continue;
		case BPF_S_ALU_DIV_K:
			A /= K;
			continue;
		case BPF_S_ALU_MOD_X:
			if (X == 0)
				return 0;
			A %= X;
			continue;
		case BPF_S_ALU_MOD_K:
			A %= K;
			continue;
		case BPF_S_ALU_AND_X:
			A &= X;
			continue;
		case BPF_S_ALU_AND_K:
			A &= K;
			continue;
		case BPF_S_ALU_OR_X:
			A |= X;
			continue;
		case BPF_S_ALU_OR_K:
			A |= K;
			continue;
		case BPF_S_ANC_ALU_XOR_X:
		case BPF_S_ALU_XOR_X:
			A ^= X;
			continue;
		case BPF_S_ALU_XOR_K:
			A ^= K;
			continue;
		case BPF_S_ALU_LSH_X:
			A <<= X;
			continue;
		case BPF_S_ALU_LSH_K:
			A <<= K;
			continue;
		case BPF_S_ALU_RSH_X:
			A >>= X;
			continue;
		case BPF_S_ALU_RSH_K:
			A >>= K;
			continue;
		case BPF_S_ALU_NEG:
			A = -A;
			continue;
		case BPF_S_JMP_JA:
			fentry += K;
			continue;
		case BPF_S_JMP_JGT_K:
			fentry += (A > K) ? fentry->jt : fentry->jf;
			continue;
		case BPF_S_JMP_JGE_K:
			fentry += (A >= K) ? fentry->jt : fentry->jf;
			continue;
		case BPF_S_JMP_JEQ_K:
			fentry += (A == K) ? fentry->jt : fentry->jf;
			continue;
		case BPF_S_JMP_JSET_K:
			fentry += (A & K) ? fentry->jt : fentry->jf;
			continue;
		case BPF_S_JMP_JGT_X:
			fentry += (A > X) ? fentry->jt : fentry->jf;
			continue;
		case BPF_S_JMP_JGE_X:
			fentry += (A >= X) ? fentry->jt : fentry->jf;
			continue;
		case BPF_S_JMP_JEQ_X:
			fentry += (A == X) ? fentry->jt : fentry->jf;
			continue;
		case BPF_S_JMP_JSET_X:
			fentry += (A & X) ? fentry->jt : fentry->jf;
			continue;
		case BPF_S_LD_W_ABS:
			k = K;
load_w:
			ptr = load_pointer(skb, k, 4, &tmp);
			if (ptr != NULL) {
				A = get_unaligned_be32(ptr);
				continue;
			}
			return 0;
		case BPF_S_LD_H_ABS:
			k = K;
load_h:
			ptr = load_pointer(skb, k, 2, &tmp);
			if (ptr != NULL) {
				A = get_unaligned_be16(ptr);
				continue;
			}
			return 0;
		case BPF_S_LD_B_ABS:
			k = K;
load_b:
			ptr = load_pointer(skb, k, 1, &tmp);
			if (ptr != NULL) {
				A = *(u8 *)ptr;
				continue;
			}
			return 0;
		case BPF_S_LD_W_LEN:
			A = skb->len;
			continue;
		case BPF_S_LDX_W_LEN:
			X = skb->len;
			continue;
		case BPF_S_LD_W_IND:
			k = X + K;
			goto load_w;
		case BPF_S_LD_H_IND:
			k = X + K;
			goto load_h;
		case BPF_S_LD_B_IND:
			k = X + K;
			goto load_b;
		case BPF_S_LDX_B_MSH:
			ptr = load_pointer(skb, K, 1, &tmp);
			if (ptr != NULL) {
				X = (*(u8 *)ptr & 0xf) << 2;
				continue;
			}
			return 0;
		case BPF_S_LD_IMM:
			A = K;
			continue;
		case BPF_S_LDX_IMM:
			X = K;
			continue;
		case BPF_S_LD_MEM:
			A = mem[K];
			continue;
		case BPF_S_LDX_MEM:
			X = mem[K];
			continue;
		case BPF_S_MISC_TAX:
			X = A;
			continue;
		case BPF_S_MISC_TXA:
			A = X;
			continue;
		case BPF_S_RET_K:
			return K;
		case BPF_S_RET_A:
			return A;
		case BPF_S_ST:
			mem[K] = A;
			continue;
		case BPF_S_STX:
			mem[K] = X;
			continue;
		case BPF_S_ANC_PROTOCOL:
			A = ntohs(skb->protocol);
			continue;
		case BPF_S_ANC_PKTTYPE:
			A = skb->pkt_type;
			continue;
		case BPF_S_ANC_IFINDEX:
			if (!skb->dev)
				return 0;
			A = skb->dev->ifindex;
			continue;
		case BPF_S_ANC_MARK:
			A = skb->mark;
			continue;
		case BPF_S_ANC_QUEUE:
			A = skb->queue_mapping;
			continue;
		case BPF_S_ANC_HATYPE:
			if (!skb->dev)
				return 0;
			A = skb->dev->type;
			continue;
		case BPF_S_ANC_RXHASH:
			A = skb->rxhash;
			continue;
		case BPF_S_ANC_CPU:
			A = raw_smp_processor_id();
			continue;
		case BPF_S_ANC_VLAN_TAG:
			A = vlan_tx_tag_get(skb);
			continue;
		case BPF_S_ANC_VLAN_TAG_PRESENT:
			A = !!vlan_tx_tag_present(skb);
			continue;
		case BPF_S_ANC_PAY_OFFSET:
			A = __skb_get_poff(skb);
			continue;
		case BPF_S_ANC_NLATTR: {
			struct nlattr *nla;

			if (skb_is_nonlinear(skb))
				return 0;
			if (skb->len < sizeof(struct nlattr))
				return 0;
			if (A > skb->len - sizeof(struct nlattr))
				return 0;

			nla = nla_find((struct nlattr *)&skb->data[A],
				       skb->len - A, X);
			if (nla)
				A = (void *)nla - (void *)skb->data;
			else
				A = 0;
			continue;
		}
		case BPF_S_ANC_NLATTR_NEST: {
			struct nlattr *nla;

			if (skb_is_nonlinear(skb))
				return 0;
			if (skb->len < sizeof(struct nlattr))
				return 0;
			if (A > skb->len - sizeof(struct nlattr))
				return 0;

			nla = (struct nlattr *)&skb->data[A];
			if (nla->nla_len > skb->len - A)
				return 0;

			nla = nla_find_nested(nla, X);
			if (nla)
				A = (void *)nla - (void *)skb->data;
			else
				A = 0;
			continue;
		}
#ifdef CONFIG_SECCOMP_FILTER
		case BPF_S_ANC_SECCOMP_LD_W:
			A = seccomp_bpf_load(fentry->k);
			continue;
#endif
		default:
			WARN_RATELIMIT(1, "Unknown code:%u jt:%u tf:%u k:%u\n",
				       fentry->code, fentry->jt,
				       fentry->jf, fentry->k);
			return 0;
		}
	}

	return 0;
}
EXPORT_SYMBOL(sk_run_filter);

/*
 * Security :
 * A BPF program is able to use 16 cells of memory to store intermediate
 * values (check u32 mem[BPF_MEMWORDS] in sk_run_filter())
 * As we dont want to clear mem[] array for each packet going through
 * sk_run_filter(), we check that filter loaded by user never try to read
 * a cell if not previously written, and we check all branches to be sure
 * a malicious user doesn't try to abuse us.
 */
static int check_load_and_stores(struct sock_filter *filter, int flen)
{
	u16 *masks, memvalid = 0; /* one bit per cell, 16 cells */
	int pc, ret = 0;

	BUILD_BUG_ON(BPF_MEMWORDS > 16);
	masks = kmalloc(flen * sizeof(*masks), GFP_KERNEL);
	if (!masks)
		return -ENOMEM;
	memset(masks, 0xff, flen * sizeof(*masks));

	for (pc = 0; pc < flen; pc++) {
		memvalid &= masks[pc];

		switch (filter[pc].code) {
		case BPF_S_ST:
		case BPF_S_STX:
			memvalid |= (1 << filter[pc].k);
			break;
		case BPF_S_LD_MEM:
		case BPF_S_LDX_MEM:
			if (!(memvalid & (1 << filter[pc].k))) {
				ret = -EINVAL;
				goto error;
			}
			break;
		case BPF_S_JMP_JA:
			/* a jump must set masks on target */
			masks[pc + 1 + filter[pc].k] &= memvalid;
			memvalid = ~0;
			break;
		case BPF_S_JMP_JEQ_K:
		case BPF_S_JMP_JEQ_X:
		case BPF_S_JMP_JGE_K:
		case BPF_S_JMP_JGE_X:
		case BPF_S_JMP_JGT_K:
		case BPF_S_JMP_JGT_X:
		case BPF_S_JMP_JSET_X:
		case BPF_S_JMP_JSET_K:
			/* a jump must set masks on targets */
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

/**
 *	sk_chk_filter - verify socket filter code
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
int sk_chk_filter(struct sock_filter *filter, unsigned int flen)
{
	/*
	 * Valid instructions are initialized to non-0.
	 * Invalid instructions are initialized to 0.
	 */
	static const u8 codes[] = {
		[BPF_ALU|BPF_ADD|BPF_K]  = BPF_S_ALU_ADD_K,
		[BPF_ALU|BPF_ADD|BPF_X]  = BPF_S_ALU_ADD_X,
		[BPF_ALU|BPF_SUB|BPF_K]  = BPF_S_ALU_SUB_K,
		[BPF_ALU|BPF_SUB|BPF_X]  = BPF_S_ALU_SUB_X,
		[BPF_ALU|BPF_MUL|BPF_K]  = BPF_S_ALU_MUL_K,
		[BPF_ALU|BPF_MUL|BPF_X]  = BPF_S_ALU_MUL_X,
		[BPF_ALU|BPF_DIV|BPF_X]  = BPF_S_ALU_DIV_X,
		[BPF_ALU|BPF_MOD|BPF_K]  = BPF_S_ALU_MOD_K,
		[BPF_ALU|BPF_MOD|BPF_X]  = BPF_S_ALU_MOD_X,
		[BPF_ALU|BPF_AND|BPF_K]  = BPF_S_ALU_AND_K,
		[BPF_ALU|BPF_AND|BPF_X]  = BPF_S_ALU_AND_X,
		[BPF_ALU|BPF_OR|BPF_K]   = BPF_S_ALU_OR_K,
		[BPF_ALU|BPF_OR|BPF_X]   = BPF_S_ALU_OR_X,
		[BPF_ALU|BPF_XOR|BPF_K]  = BPF_S_ALU_XOR_K,
		[BPF_ALU|BPF_XOR|BPF_X]  = BPF_S_ALU_XOR_X,
		[BPF_ALU|BPF_LSH|BPF_K]  = BPF_S_ALU_LSH_K,
		[BPF_ALU|BPF_LSH|BPF_X]  = BPF_S_ALU_LSH_X,
		[BPF_ALU|BPF_RSH|BPF_K]  = BPF_S_ALU_RSH_K,
		[BPF_ALU|BPF_RSH|BPF_X]  = BPF_S_ALU_RSH_X,
		[BPF_ALU|BPF_NEG]        = BPF_S_ALU_NEG,
		[BPF_LD|BPF_W|BPF_ABS]   = BPF_S_LD_W_ABS,
		[BPF_LD|BPF_H|BPF_ABS]   = BPF_S_LD_H_ABS,
		[BPF_LD|BPF_B|BPF_ABS]   = BPF_S_LD_B_ABS,
		[BPF_LD|BPF_W|BPF_LEN]   = BPF_S_LD_W_LEN,
		[BPF_LD|BPF_W|BPF_IND]   = BPF_S_LD_W_IND,
		[BPF_LD|BPF_H|BPF_IND]   = BPF_S_LD_H_IND,
		[BPF_LD|BPF_B|BPF_IND]   = BPF_S_LD_B_IND,
		[BPF_LD|BPF_IMM]         = BPF_S_LD_IMM,
		[BPF_LDX|BPF_W|BPF_LEN]  = BPF_S_LDX_W_LEN,
		[BPF_LDX|BPF_B|BPF_MSH]  = BPF_S_LDX_B_MSH,
		[BPF_LDX|BPF_IMM]        = BPF_S_LDX_IMM,
		[BPF_MISC|BPF_TAX]       = BPF_S_MISC_TAX,
		[BPF_MISC|BPF_TXA]       = BPF_S_MISC_TXA,
		[BPF_RET|BPF_K]          = BPF_S_RET_K,
		[BPF_RET|BPF_A]          = BPF_S_RET_A,
		[BPF_ALU|BPF_DIV|BPF_K]  = BPF_S_ALU_DIV_K,
		[BPF_LD|BPF_MEM]         = BPF_S_LD_MEM,
		[BPF_LDX|BPF_MEM]        = BPF_S_LDX_MEM,
		[BPF_ST]                 = BPF_S_ST,
		[BPF_STX]                = BPF_S_STX,
		[BPF_JMP|BPF_JA]         = BPF_S_JMP_JA,
		[BPF_JMP|BPF_JEQ|BPF_K]  = BPF_S_JMP_JEQ_K,
		[BPF_JMP|BPF_JEQ|BPF_X]  = BPF_S_JMP_JEQ_X,
		[BPF_JMP|BPF_JGE|BPF_K]  = BPF_S_JMP_JGE_K,
		[BPF_JMP|BPF_JGE|BPF_X]  = BPF_S_JMP_JGE_X,
		[BPF_JMP|BPF_JGT|BPF_K]  = BPF_S_JMP_JGT_K,
		[BPF_JMP|BPF_JGT|BPF_X]  = BPF_S_JMP_JGT_X,
		[BPF_JMP|BPF_JSET|BPF_K] = BPF_S_JMP_JSET_K,
		[BPF_JMP|BPF_JSET|BPF_X] = BPF_S_JMP_JSET_X,
	};
	int pc;
	bool anc_found;

	if (flen == 0 || flen > BPF_MAXINSNS)
		return -EINVAL;

	/* check the filter code now */
	for (pc = 0; pc < flen; pc++) {
		struct sock_filter *ftest = &filter[pc];
		u16 code = ftest->code;

		if (code >= ARRAY_SIZE(codes))
			return -EINVAL;
		code = codes[code];
		if (!code)
			return -EINVAL;
		/* Some instructions need special checks */
		switch (code) {
		case BPF_S_ALU_DIV_K:
		case BPF_S_ALU_MOD_K:
			/* check for division by zero */
			if (ftest->k == 0)
				return -EINVAL;
			break;
		case BPF_S_LD_MEM:
		case BPF_S_LDX_MEM:
		case BPF_S_ST:
		case BPF_S_STX:
			/* check for invalid memory addresses */
			if (ftest->k >= BPF_MEMWORDS)
				return -EINVAL;
			break;
		case BPF_S_JMP_JA:
			/*
			 * Note, the large ftest->k might cause loops.
			 * Compare this with conditional jumps below,
			 * where offsets are limited. --ANK (981016)
			 */
			if (ftest->k >= (unsigned int)(flen-pc-1))
				return -EINVAL;
			break;
		case BPF_S_JMP_JEQ_K:
		case BPF_S_JMP_JEQ_X:
		case BPF_S_JMP_JGE_K:
		case BPF_S_JMP_JGE_X:
		case BPF_S_JMP_JGT_K:
		case BPF_S_JMP_JGT_X:
		case BPF_S_JMP_JSET_X:
		case BPF_S_JMP_JSET_K:
			/* for conditionals both must be safe */
			if (pc + ftest->jt + 1 >= flen ||
			    pc + ftest->jf + 1 >= flen)
				return -EINVAL;
			break;
		case BPF_S_LD_W_ABS:
		case BPF_S_LD_H_ABS:
		case BPF_S_LD_B_ABS:
			anc_found = false;
#define ANCILLARY(CODE) case SKF_AD_OFF + SKF_AD_##CODE:	\
				code = BPF_S_ANC_##CODE;	\
				anc_found = true;		\
				break
			switch (ftest->k) {
			ANCILLARY(PROTOCOL);
			ANCILLARY(PKTTYPE);
			ANCILLARY(IFINDEX);
			ANCILLARY(NLATTR);
			ANCILLARY(NLATTR_NEST);
			ANCILLARY(MARK);
			ANCILLARY(QUEUE);
			ANCILLARY(HATYPE);
			ANCILLARY(RXHASH);
			ANCILLARY(CPU);
			ANCILLARY(ALU_XOR_X);
			ANCILLARY(VLAN_TAG);
			ANCILLARY(VLAN_TAG_PRESENT);
			ANCILLARY(PAY_OFFSET);
			}

			/* ancillary operation unknown or unsupported */
			if (anc_found == false && ftest->k >= SKF_AD_OFF)
				return -EINVAL;
		}
		ftest->code = code;
	}

	/* last instruction must be a RET code */
	switch (filter[flen - 1].code) {
	case BPF_S_RET_K:
	case BPF_S_RET_A:
		return check_load_and_stores(filter, flen);
	}
	return -EINVAL;
}
EXPORT_SYMBOL(sk_chk_filter);

/**
 * 	sk_filter_release_rcu - Release a socket filter by rcu_head
 *	@rcu: rcu_head that contains the sk_filter to free
 */
void sk_filter_release_rcu(struct rcu_head *rcu)
{
	struct sk_filter *fp = container_of(rcu, struct sk_filter, rcu);

	bpf_jit_free(fp);
	kfree(fp);
}
EXPORT_SYMBOL(sk_filter_release_rcu);

static int __sk_prepare_filter(struct sk_filter *fp)
{
	int err;

	fp->bpf_func = sk_run_filter;

	err = sk_chk_filter(fp->insns, fp->len);
	if (err)
		return err;

	bpf_jit_compile(fp);
	return 0;
}

/**
 *	sk_unattached_filter_create - create an unattached filter
 *	@fprog: the filter program
 *	@pfp: the unattached filter that is created
 *
 * Create a filter independent of any socket. We first run some
 * sanity checks on it to make sure it does not explode on us later.
 * If an error occurs or there is insufficient memory for the filter
 * a negative errno code is returned. On success the return is zero.
 */
int sk_unattached_filter_create(struct sk_filter **pfp,
				struct sock_fprog *fprog)
{
	struct sk_filter *fp;
	unsigned int fsize = sizeof(struct sock_filter) * fprog->len;
	int err;

	/* Make sure new filter is there and in the right amounts. */
	if (fprog->filter == NULL)
		return -EINVAL;

	fp = kmalloc(fsize + sizeof(*fp), GFP_KERNEL);
	if (!fp)
		return -ENOMEM;
	memcpy(fp->insns, fprog->filter, fsize);

	atomic_set(&fp->refcnt, 1);
	fp->len = fprog->len;

	err = __sk_prepare_filter(fp);
	if (err)
		goto free_mem;

	*pfp = fp;
	return 0;
free_mem:
	kfree(fp);
	return err;
}
EXPORT_SYMBOL_GPL(sk_unattached_filter_create);

void sk_unattached_filter_destroy(struct sk_filter *fp)
{
	sk_filter_release(fp);
}
EXPORT_SYMBOL_GPL(sk_unattached_filter_destroy);

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
	unsigned int fsize = sizeof(struct sock_filter) * fprog->len;
	int err;

	if (sock_flag(sk, SOCK_FILTER_LOCKED))
		return -EPERM;

	/* Make sure new filter is there and in the right amounts. */
	if (fprog->filter == NULL)
		return -EINVAL;

	fp = sock_kmalloc(sk, fsize+sizeof(*fp), GFP_KERNEL);
	if (!fp)
		return -ENOMEM;
	if (copy_from_user(fp->insns, fprog->filter, fsize)) {
		sock_kfree_s(sk, fp, fsize+sizeof(*fp));
		return -EFAULT;
	}

	atomic_set(&fp->refcnt, 1);
	fp->len = fprog->len;

	err = __sk_prepare_filter(fp);
	if (err) {
		sk_filter_uncharge(sk, fp);
		return err;
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

void sk_decode_filter(struct sock_filter *filt, struct sock_filter *to)
{
	static const u16 decodes[] = {
		[BPF_S_ALU_ADD_K]	= BPF_ALU|BPF_ADD|BPF_K,
		[BPF_S_ALU_ADD_X]	= BPF_ALU|BPF_ADD|BPF_X,
		[BPF_S_ALU_SUB_K]	= BPF_ALU|BPF_SUB|BPF_K,
		[BPF_S_ALU_SUB_X]	= BPF_ALU|BPF_SUB|BPF_X,
		[BPF_S_ALU_MUL_K]	= BPF_ALU|BPF_MUL|BPF_K,
		[BPF_S_ALU_MUL_X]	= BPF_ALU|BPF_MUL|BPF_X,
		[BPF_S_ALU_DIV_X]	= BPF_ALU|BPF_DIV|BPF_X,
		[BPF_S_ALU_MOD_K]	= BPF_ALU|BPF_MOD|BPF_K,
		[BPF_S_ALU_MOD_X]	= BPF_ALU|BPF_MOD|BPF_X,
		[BPF_S_ALU_AND_K]	= BPF_ALU|BPF_AND|BPF_K,
		[BPF_S_ALU_AND_X]	= BPF_ALU|BPF_AND|BPF_X,
		[BPF_S_ALU_OR_K]	= BPF_ALU|BPF_OR|BPF_K,
		[BPF_S_ALU_OR_X]	= BPF_ALU|BPF_OR|BPF_X,
		[BPF_S_ALU_XOR_K]	= BPF_ALU|BPF_XOR|BPF_K,
		[BPF_S_ALU_XOR_X]	= BPF_ALU|BPF_XOR|BPF_X,
		[BPF_S_ALU_LSH_K]	= BPF_ALU|BPF_LSH|BPF_K,
		[BPF_S_ALU_LSH_X]	= BPF_ALU|BPF_LSH|BPF_X,
		[BPF_S_ALU_RSH_K]	= BPF_ALU|BPF_RSH|BPF_K,
		[BPF_S_ALU_RSH_X]	= BPF_ALU|BPF_RSH|BPF_X,
		[BPF_S_ALU_NEG]		= BPF_ALU|BPF_NEG,
		[BPF_S_LD_W_ABS]	= BPF_LD|BPF_W|BPF_ABS,
		[BPF_S_LD_H_ABS]	= BPF_LD|BPF_H|BPF_ABS,
		[BPF_S_LD_B_ABS]	= BPF_LD|BPF_B|BPF_ABS,
		[BPF_S_ANC_PROTOCOL]	= BPF_LD|BPF_B|BPF_ABS,
		[BPF_S_ANC_PKTTYPE]	= BPF_LD|BPF_B|BPF_ABS,
		[BPF_S_ANC_IFINDEX]	= BPF_LD|BPF_B|BPF_ABS,
		[BPF_S_ANC_NLATTR]	= BPF_LD|BPF_B|BPF_ABS,
		[BPF_S_ANC_NLATTR_NEST]	= BPF_LD|BPF_B|BPF_ABS,
		[BPF_S_ANC_MARK]	= BPF_LD|BPF_B|BPF_ABS,
		[BPF_S_ANC_QUEUE]	= BPF_LD|BPF_B|BPF_ABS,
		[BPF_S_ANC_HATYPE]	= BPF_LD|BPF_B|BPF_ABS,
		[BPF_S_ANC_RXHASH]	= BPF_LD|BPF_B|BPF_ABS,
		[BPF_S_ANC_CPU]		= BPF_LD|BPF_B|BPF_ABS,
		[BPF_S_ANC_ALU_XOR_X]	= BPF_LD|BPF_B|BPF_ABS,
		[BPF_S_ANC_SECCOMP_LD_W] = BPF_LD|BPF_B|BPF_ABS,
		[BPF_S_ANC_VLAN_TAG]	= BPF_LD|BPF_B|BPF_ABS,
		[BPF_S_ANC_VLAN_TAG_PRESENT] = BPF_LD|BPF_B|BPF_ABS,
		[BPF_S_ANC_PAY_OFFSET]	= BPF_LD|BPF_B|BPF_ABS,
		[BPF_S_LD_W_LEN]	= BPF_LD|BPF_W|BPF_LEN,
		[BPF_S_LD_W_IND]	= BPF_LD|BPF_W|BPF_IND,
		[BPF_S_LD_H_IND]	= BPF_LD|BPF_H|BPF_IND,
		[BPF_S_LD_B_IND]	= BPF_LD|BPF_B|BPF_IND,
		[BPF_S_LD_IMM]		= BPF_LD|BPF_IMM,
		[BPF_S_LDX_W_LEN]	= BPF_LDX|BPF_W|BPF_LEN,
		[BPF_S_LDX_B_MSH]	= BPF_LDX|BPF_B|BPF_MSH,
		[BPF_S_LDX_IMM]		= BPF_LDX|BPF_IMM,
		[BPF_S_MISC_TAX]	= BPF_MISC|BPF_TAX,
		[BPF_S_MISC_TXA]	= BPF_MISC|BPF_TXA,
		[BPF_S_RET_K]		= BPF_RET|BPF_K,
		[BPF_S_RET_A]		= BPF_RET|BPF_A,
		[BPF_S_ALU_DIV_K]	= BPF_ALU|BPF_DIV|BPF_K,
		[BPF_S_LD_MEM]		= BPF_LD|BPF_MEM,
		[BPF_S_LDX_MEM]		= BPF_LDX|BPF_MEM,
		[BPF_S_ST]		= BPF_ST,
		[BPF_S_STX]		= BPF_STX,
		[BPF_S_JMP_JA]		= BPF_JMP|BPF_JA,
		[BPF_S_JMP_JEQ_K]	= BPF_JMP|BPF_JEQ|BPF_K,
		[BPF_S_JMP_JEQ_X]	= BPF_JMP|BPF_JEQ|BPF_X,
		[BPF_S_JMP_JGE_K]	= BPF_JMP|BPF_JGE|BPF_K,
		[BPF_S_JMP_JGE_X]	= BPF_JMP|BPF_JGE|BPF_X,
		[BPF_S_JMP_JGT_K]	= BPF_JMP|BPF_JGT|BPF_K,
		[BPF_S_JMP_JGT_X]	= BPF_JMP|BPF_JGT|BPF_X,
		[BPF_S_JMP_JSET_K]	= BPF_JMP|BPF_JSET|BPF_K,
		[BPF_S_JMP_JSET_X]	= BPF_JMP|BPF_JSET|BPF_X,
	};
	u16 code;

	code = filt->code;

	to->code = decodes[code];
	to->jt = filt->jt;
	to->jf = filt->jf;
	to->k = filt->k;
}

int sk_get_filter(struct sock *sk, struct sock_filter __user *ubuf, unsigned int len)
{
	struct sk_filter *filter;
	int i, ret;

	lock_sock(sk);
	filter = rcu_dereference_protected(sk->sk_filter,
			sock_owned_by_user(sk));
	ret = 0;
	if (!filter)
		goto out;
	ret = filter->len;
	if (!len)
		goto out;
	ret = -EINVAL;
	if (len < filter->len)
		goto out;

	ret = -EFAULT;
	for (i = 0; i < filter->len; i++) {
		struct sock_filter fb;

		sk_decode_filter(&filter->insns[i], &fb);
		if (copy_to_user(&ubuf[i], &fb, sizeof(fb)))
			goto out;
	}

	ret = filter->len;
out:
	release_sock(sk);
	return ret;
}
