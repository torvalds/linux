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
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/unaligned.h>
#include <linux/filter.h>

/* No hurry in this branch */
static void *__load_pointer(struct sk_buff *skb, int k)
{
	u8 *ptr = NULL;

	if (k >= SKF_NET_OFF)
		ptr = skb_network_header(skb) + k - SKF_NET_OFF;
	else if (k >= SKF_LL_OFF)
		ptr = skb_mac_header(skb) + k - SKF_LL_OFF;

	if (ptr >= skb->head && ptr < skb_tail_pointer(skb))
		return ptr;
	return NULL;
}

static inline void *load_pointer(struct sk_buff *skb, int k,
				 unsigned int size, void *buffer)
{
	if (k >= 0)
		return skb_header_pointer(skb, k, size, buffer);
	else {
		if (k >= SKF_AD_OFF)
			return NULL;
		return __load_pointer(skb, k);
	}
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

	err = security_sock_rcv_skb(sk, skb);
	if (err)
		return err;

	rcu_read_lock_bh();
	filter = rcu_dereference_bh(sk->sk_filter);
	if (filter) {
		unsigned int pkt_len = sk_run_filter(skb, filter->insns,
				filter->len);
		err = pkt_len ? pskb_trim(skb, pkt_len) : -EPERM;
	}
	rcu_read_unlock_bh();

	return err;
}
EXPORT_SYMBOL(sk_filter);

/**
 *	sk_run_filter - run a filter on a socket
 *	@skb: buffer to run the filter on
 *	@filter: filter to apply
 *	@flen: length of filter
 *
 * Decode and apply filter instructions to the skb->data.
 * Return length to keep, 0 for none. skb is the data we are
 * filtering, filter is the array of filter instructions, and
 * len is the number of filter blocks in the array.
 */
unsigned int sk_run_filter(struct sk_buff *skb, struct sock_filter *filter, int flen)
{
	struct sock_filter *fentry;	/* We walk down these */
	void *ptr;
	u32 A = 0;			/* Accumulator */
	u32 X = 0;			/* Index Register */
	u32 mem[BPF_MEMWORDS];		/* Scratch Memory Store */
	u32 tmp;
	int k;
	int pc;

	/*
	 * Process array of filter instructions.
	 */
	for (pc = 0; pc < flen; pc++) {
		fentry = &filter[pc];

		switch (fentry->code) {
		case BPF_S_ALU_ADD_X:
			A += X;
			continue;
		case BPF_S_ALU_ADD_K:
			A += fentry->k;
			continue;
		case BPF_S_ALU_SUB_X:
			A -= X;
			continue;
		case BPF_S_ALU_SUB_K:
			A -= fentry->k;
			continue;
		case BPF_S_ALU_MUL_X:
			A *= X;
			continue;
		case BPF_S_ALU_MUL_K:
			A *= fentry->k;
			continue;
		case BPF_S_ALU_DIV_X:
			if (X == 0)
				return 0;
			A /= X;
			continue;
		case BPF_S_ALU_DIV_K:
			A /= fentry->k;
			continue;
		case BPF_S_ALU_AND_X:
			A &= X;
			continue;
		case BPF_S_ALU_AND_K:
			A &= fentry->k;
			continue;
		case BPF_S_ALU_OR_X:
			A |= X;
			continue;
		case BPF_S_ALU_OR_K:
			A |= fentry->k;
			continue;
		case BPF_S_ALU_LSH_X:
			A <<= X;
			continue;
		case BPF_S_ALU_LSH_K:
			A <<= fentry->k;
			continue;
		case BPF_S_ALU_RSH_X:
			A >>= X;
			continue;
		case BPF_S_ALU_RSH_K:
			A >>= fentry->k;
			continue;
		case BPF_S_ALU_NEG:
			A = -A;
			continue;
		case BPF_S_JMP_JA:
			pc += fentry->k;
			continue;
		case BPF_S_JMP_JGT_K:
			pc += (A > fentry->k) ? fentry->jt : fentry->jf;
			continue;
		case BPF_S_JMP_JGE_K:
			pc += (A >= fentry->k) ? fentry->jt : fentry->jf;
			continue;
		case BPF_S_JMP_JEQ_K:
			pc += (A == fentry->k) ? fentry->jt : fentry->jf;
			continue;
		case BPF_S_JMP_JSET_K:
			pc += (A & fentry->k) ? fentry->jt : fentry->jf;
			continue;
		case BPF_S_JMP_JGT_X:
			pc += (A > X) ? fentry->jt : fentry->jf;
			continue;
		case BPF_S_JMP_JGE_X:
			pc += (A >= X) ? fentry->jt : fentry->jf;
			continue;
		case BPF_S_JMP_JEQ_X:
			pc += (A == X) ? fentry->jt : fentry->jf;
			continue;
		case BPF_S_JMP_JSET_X:
			pc += (A & X) ? fentry->jt : fentry->jf;
			continue;
		case BPF_S_LD_W_ABS:
			k = fentry->k;
load_w:
			ptr = load_pointer(skb, k, 4, &tmp);
			if (ptr != NULL) {
				A = get_unaligned_be32(ptr);
				continue;
			}
			break;
		case BPF_S_LD_H_ABS:
			k = fentry->k;
load_h:
			ptr = load_pointer(skb, k, 2, &tmp);
			if (ptr != NULL) {
				A = get_unaligned_be16(ptr);
				continue;
			}
			break;
		case BPF_S_LD_B_ABS:
			k = fentry->k;
load_b:
			ptr = load_pointer(skb, k, 1, &tmp);
			if (ptr != NULL) {
				A = *(u8 *)ptr;
				continue;
			}
			break;
		case BPF_S_LD_W_LEN:
			A = skb->len;
			continue;
		case BPF_S_LDX_W_LEN:
			X = skb->len;
			continue;
		case BPF_S_LD_W_IND:
			k = X + fentry->k;
			goto load_w;
		case BPF_S_LD_H_IND:
			k = X + fentry->k;
			goto load_h;
		case BPF_S_LD_B_IND:
			k = X + fentry->k;
			goto load_b;
		case BPF_S_LDX_B_MSH:
			ptr = load_pointer(skb, fentry->k, 1, &tmp);
			if (ptr != NULL) {
				X = (*(u8 *)ptr & 0xf) << 2;
				continue;
			}
			return 0;
		case BPF_S_LD_IMM:
			A = fentry->k;
			continue;
		case BPF_S_LDX_IMM:
			X = fentry->k;
			continue;
		case BPF_S_LD_MEM:
			A = mem[fentry->k];
			continue;
		case BPF_S_LDX_MEM:
			X = mem[fentry->k];
			continue;
		case BPF_S_MISC_TAX:
			X = A;
			continue;
		case BPF_S_MISC_TXA:
			A = X;
			continue;
		case BPF_S_RET_K:
			return fentry->k;
		case BPF_S_RET_A:
			return A;
		case BPF_S_ST:
			mem[fentry->k] = A;
			continue;
		case BPF_S_STX:
			mem[fentry->k] = X;
			continue;
		default:
			WARN_ON(1);
			return 0;
		}

		/*
		 * Handle ancillary data, which are impossible
		 * (or very difficult) to get parsing packet contents.
		 */
		switch (k-SKF_AD_OFF) {
		case SKF_AD_PROTOCOL:
			A = ntohs(skb->protocol);
			continue;
		case SKF_AD_PKTTYPE:
			A = skb->pkt_type;
			continue;
		case SKF_AD_IFINDEX:
			if (!skb->dev)
				return 0;
			A = skb->dev->ifindex;
			continue;
		case SKF_AD_MARK:
			A = skb->mark;
			continue;
		case SKF_AD_QUEUE:
			A = skb->queue_mapping;
			continue;
		case SKF_AD_HATYPE:
			if (!skb->dev)
				return 0;
			A = skb->dev->type;
			continue;
		case SKF_AD_NLATTR: {
			struct nlattr *nla;

			if (skb_is_nonlinear(skb))
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
		case SKF_AD_NLATTR_NEST: {
			struct nlattr *nla;

			if (skb_is_nonlinear(skb))
				return 0;
			if (A > skb->len - sizeof(struct nlattr))
				return 0;

			nla = (struct nlattr *)&skb->data[A];
			if (nla->nla_len > A - skb->len)
				return 0;

			nla = nla_find_nested(nla, X);
			if (nla)
				A = (void *)nla - (void *)skb->data;
			else
				A = 0;
			continue;
		}
		default:
			return 0;
		}
	}

	return 0;
}
EXPORT_SYMBOL(sk_run_filter);

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
int sk_chk_filter(struct sock_filter *filter, int flen)
{
	struct sock_filter *ftest;
	int pc;

	if (flen == 0 || flen > BPF_MAXINSNS)
		return -EINVAL;

	/* check the filter code now */
	for (pc = 0; pc < flen; pc++) {
		ftest = &filter[pc];

		/* Only allow valid instructions */
		switch (ftest->code) {
		case BPF_ALU|BPF_ADD|BPF_K:
			ftest->code = BPF_S_ALU_ADD_K;
			break;
		case BPF_ALU|BPF_ADD|BPF_X:
			ftest->code = BPF_S_ALU_ADD_X;
			break;
		case BPF_ALU|BPF_SUB|BPF_K:
			ftest->code = BPF_S_ALU_SUB_K;
			break;
		case BPF_ALU|BPF_SUB|BPF_X:
			ftest->code = BPF_S_ALU_SUB_X;
			break;
		case BPF_ALU|BPF_MUL|BPF_K:
			ftest->code = BPF_S_ALU_MUL_K;
			break;
		case BPF_ALU|BPF_MUL|BPF_X:
			ftest->code = BPF_S_ALU_MUL_X;
			break;
		case BPF_ALU|BPF_DIV|BPF_X:
			ftest->code = BPF_S_ALU_DIV_X;
			break;
		case BPF_ALU|BPF_AND|BPF_K:
			ftest->code = BPF_S_ALU_AND_K;
			break;
		case BPF_ALU|BPF_AND|BPF_X:
			ftest->code = BPF_S_ALU_AND_X;
			break;
		case BPF_ALU|BPF_OR|BPF_K:
			ftest->code = BPF_S_ALU_OR_K;
			break;
		case BPF_ALU|BPF_OR|BPF_X:
			ftest->code = BPF_S_ALU_OR_X;
			break;
		case BPF_ALU|BPF_LSH|BPF_K:
			ftest->code = BPF_S_ALU_LSH_K;
			break;
		case BPF_ALU|BPF_LSH|BPF_X:
			ftest->code = BPF_S_ALU_LSH_X;
			break;
		case BPF_ALU|BPF_RSH|BPF_K:
			ftest->code = BPF_S_ALU_RSH_K;
			break;
		case BPF_ALU|BPF_RSH|BPF_X:
			ftest->code = BPF_S_ALU_RSH_X;
			break;
		case BPF_ALU|BPF_NEG:
			ftest->code = BPF_S_ALU_NEG;
			break;
		case BPF_LD|BPF_W|BPF_ABS:
			ftest->code = BPF_S_LD_W_ABS;
			break;
		case BPF_LD|BPF_H|BPF_ABS:
			ftest->code = BPF_S_LD_H_ABS;
			break;
		case BPF_LD|BPF_B|BPF_ABS:
			ftest->code = BPF_S_LD_B_ABS;
			break;
		case BPF_LD|BPF_W|BPF_LEN:
			ftest->code = BPF_S_LD_W_LEN;
			break;
		case BPF_LD|BPF_W|BPF_IND:
			ftest->code = BPF_S_LD_W_IND;
			break;
		case BPF_LD|BPF_H|BPF_IND:
			ftest->code = BPF_S_LD_H_IND;
			break;
		case BPF_LD|BPF_B|BPF_IND:
			ftest->code = BPF_S_LD_B_IND;
			break;
		case BPF_LD|BPF_IMM:
			ftest->code = BPF_S_LD_IMM;
			break;
		case BPF_LDX|BPF_W|BPF_LEN:
			ftest->code = BPF_S_LDX_W_LEN;
			break;
		case BPF_LDX|BPF_B|BPF_MSH:
			ftest->code = BPF_S_LDX_B_MSH;
			break;
		case BPF_LDX|BPF_IMM:
			ftest->code = BPF_S_LDX_IMM;
			break;
		case BPF_MISC|BPF_TAX:
			ftest->code = BPF_S_MISC_TAX;
			break;
		case BPF_MISC|BPF_TXA:
			ftest->code = BPF_S_MISC_TXA;
			break;
		case BPF_RET|BPF_K:
			ftest->code = BPF_S_RET_K;
			break;
		case BPF_RET|BPF_A:
			ftest->code = BPF_S_RET_A;
			break;

		/* Some instructions need special checks */

			/* check for division by zero */
		case BPF_ALU|BPF_DIV|BPF_K:
			if (ftest->k == 0)
				return -EINVAL;
			ftest->code = BPF_S_ALU_DIV_K;
			break;

		/* check for invalid memory addresses */
		case BPF_LD|BPF_MEM:
			if (ftest->k >= BPF_MEMWORDS)
				return -EINVAL;
			ftest->code = BPF_S_LD_MEM;
			break;
		case BPF_LDX|BPF_MEM:
			if (ftest->k >= BPF_MEMWORDS)
				return -EINVAL;
			ftest->code = BPF_S_LDX_MEM;
			break;
		case BPF_ST:
			if (ftest->k >= BPF_MEMWORDS)
				return -EINVAL;
			ftest->code = BPF_S_ST;
			break;
		case BPF_STX:
			if (ftest->k >= BPF_MEMWORDS)
				return -EINVAL;
			ftest->code = BPF_S_STX;
			break;

		case BPF_JMP|BPF_JA:
			/*
			 * Note, the large ftest->k might cause loops.
			 * Compare this with conditional jumps below,
			 * where offsets are limited. --ANK (981016)
			 */
			if (ftest->k >= (unsigned)(flen-pc-1))
				return -EINVAL;
			ftest->code = BPF_S_JMP_JA;
			break;

		case BPF_JMP|BPF_JEQ|BPF_K:
			ftest->code = BPF_S_JMP_JEQ_K;
			break;
		case BPF_JMP|BPF_JEQ|BPF_X:
			ftest->code = BPF_S_JMP_JEQ_X;
			break;
		case BPF_JMP|BPF_JGE|BPF_K:
			ftest->code = BPF_S_JMP_JGE_K;
			break;
		case BPF_JMP|BPF_JGE|BPF_X:
			ftest->code = BPF_S_JMP_JGE_X;
			break;
		case BPF_JMP|BPF_JGT|BPF_K:
			ftest->code = BPF_S_JMP_JGT_K;
			break;
		case BPF_JMP|BPF_JGT|BPF_X:
			ftest->code = BPF_S_JMP_JGT_X;
			break;
		case BPF_JMP|BPF_JSET|BPF_K:
			ftest->code = BPF_S_JMP_JSET_K;
			break;
		case BPF_JMP|BPF_JSET|BPF_X:
			ftest->code = BPF_S_JMP_JSET_X;
			break;

		default:
			return -EINVAL;
		}

			/* for conditionals both must be safe */
		switch (ftest->code) {
		case BPF_S_JMP_JEQ_K:
		case BPF_S_JMP_JEQ_X:
		case BPF_S_JMP_JGE_K:
		case BPF_S_JMP_JGE_X:
		case BPF_S_JMP_JGT_K:
		case BPF_S_JMP_JGT_X:
		case BPF_S_JMP_JSET_X:
		case BPF_S_JMP_JSET_K:
			if (pc + ftest->jt + 1 >= flen ||
			    pc + ftest->jf + 1 >= flen)
				return -EINVAL;
		}
	}

	/* last instruction must be a RET code */
	switch (filter[flen - 1].code) {
	case BPF_S_RET_K:
	case BPF_S_RET_A:
		return 0;
		break;
		default:
			return -EINVAL;
		}
}
EXPORT_SYMBOL(sk_chk_filter);

/**
 * 	sk_filter_rcu_release: Release a socket filter by rcu_head
 *	@rcu: rcu_head that contains the sk_filter to free
 */
static void sk_filter_rcu_release(struct rcu_head *rcu)
{
	struct sk_filter *fp = container_of(rcu, struct sk_filter, rcu);

	sk_filter_release(fp);
}

static void sk_filter_delayed_uncharge(struct sock *sk, struct sk_filter *fp)
{
	unsigned int size = sk_filter_len(fp);

	atomic_sub(size, &sk->sk_omem_alloc);
	call_rcu_bh(&fp->rcu, sk_filter_rcu_release);
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
	struct sk_filter *fp, *old_fp;
	unsigned int fsize = sizeof(struct sock_filter) * fprog->len;
	int err;

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

	err = sk_chk_filter(fp->insns, fp->len);
	if (err) {
		sk_filter_uncharge(sk, fp);
		return err;
	}

	old_fp = rcu_dereference_protected(sk->sk_filter,
					   sock_owned_by_user(sk));
	rcu_assign_pointer(sk->sk_filter, fp);

	if (old_fp)
		sk_filter_delayed_uncharge(sk, old_fp);
	return 0;
}
EXPORT_SYMBOL_GPL(sk_attach_filter);

int sk_detach_filter(struct sock *sk)
{
	int ret = -ENOENT;
	struct sk_filter *filter;

	filter = rcu_dereference_protected(sk->sk_filter,
					   sock_owned_by_user(sk));
	if (filter) {
		rcu_assign_pointer(sk->sk_filter, NULL);
		sk_filter_delayed_uncharge(sk, filter);
		ret = 0;
	}
	return ret;
}
EXPORT_SYMBOL_GPL(sk_detach_filter);
