// SPDX-License-Identifier: GPL-2.0-only
/*
 * Packet matching code for ARP packets.
 *
 * Based heavily, if not almost entirely, upon ip_tables.c framework.
 *
 * Some ARP specific bits are:
 *
 * Copyright (C) 2002 David S. Miller (davem@redhat.com)
 * Copyright (C) 2006-2009 Patrick McHardy <kaber@trash.net>
 *
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/capability.h>
#include <linux/if_arp.h>
#include <linux/kmod.h>
#include <linux/vmalloc.h>
#include <linux/proc_fs.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <net/compat.h>
#include <net/sock.h>
#include <linux/uaccess.h>

#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_arp/arp_tables.h>
#include "../../netfilter/xt_repldata.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("David S. Miller <davem@redhat.com>");
MODULE_DESCRIPTION("arptables core");

void *arpt_alloc_initial_table(const struct xt_table *info)
{
	return xt_alloc_initial_table(arpt, ARPT);
}
EXPORT_SYMBOL_GPL(arpt_alloc_initial_table);

static inline int arp_devaddr_compare(const struct arpt_devaddr_info *ap,
				      const char *hdr_addr, int len)
{
	int i, ret;

	if (len > ARPT_DEV_ADDR_LEN_MAX)
		len = ARPT_DEV_ADDR_LEN_MAX;

	ret = 0;
	for (i = 0; i < len; i++)
		ret |= (hdr_addr[i] ^ ap->addr[i]) & ap->mask[i];

	return ret != 0;
}

/*
 * Unfortunately, _b and _mask are not aligned to an int (or long int)
 * Some arches dont care, unrolling the loop is a win on them.
 * For other arches, we only have a 16bit alignement.
 */
static unsigned long ifname_compare(const char *_a, const char *_b, const char *_mask)
{
#ifdef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
	unsigned long ret = ifname_compare_aligned(_a, _b, _mask);
#else
	unsigned long ret = 0;
	const u16 *a = (const u16 *)_a;
	const u16 *b = (const u16 *)_b;
	const u16 *mask = (const u16 *)_mask;
	int i;

	for (i = 0; i < IFNAMSIZ/sizeof(u16); i++)
		ret |= (a[i] ^ b[i]) & mask[i];
#endif
	return ret;
}

/* Returns whether packet matches rule or not. */
static inline int arp_packet_match(const struct arphdr *arphdr,
				   struct net_device *dev,
				   const char *indev,
				   const char *outdev,
				   const struct arpt_arp *arpinfo)
{
	const char *arpptr = (char *)(arphdr + 1);
	const char *src_devaddr, *tgt_devaddr;
	__be32 src_ipaddr, tgt_ipaddr;
	long ret;

	if (NF_INVF(arpinfo, ARPT_INV_ARPOP,
		    (arphdr->ar_op & arpinfo->arpop_mask) != arpinfo->arpop))
		return 0;

	if (NF_INVF(arpinfo, ARPT_INV_ARPHRD,
		    (arphdr->ar_hrd & arpinfo->arhrd_mask) != arpinfo->arhrd))
		return 0;

	if (NF_INVF(arpinfo, ARPT_INV_ARPPRO,
		    (arphdr->ar_pro & arpinfo->arpro_mask) != arpinfo->arpro))
		return 0;

	if (NF_INVF(arpinfo, ARPT_INV_ARPHLN,
		    (arphdr->ar_hln & arpinfo->arhln_mask) != arpinfo->arhln))
		return 0;

	src_devaddr = arpptr;
	arpptr += dev->addr_len;
	memcpy(&src_ipaddr, arpptr, sizeof(u32));
	arpptr += sizeof(u32);
	tgt_devaddr = arpptr;
	arpptr += dev->addr_len;
	memcpy(&tgt_ipaddr, arpptr, sizeof(u32));

	if (NF_INVF(arpinfo, ARPT_INV_SRCDEVADDR,
		    arp_devaddr_compare(&arpinfo->src_devaddr, src_devaddr,
					dev->addr_len)) ||
	    NF_INVF(arpinfo, ARPT_INV_TGTDEVADDR,
		    arp_devaddr_compare(&arpinfo->tgt_devaddr, tgt_devaddr,
					dev->addr_len)))
		return 0;

	if (NF_INVF(arpinfo, ARPT_INV_SRCIP,
		    (src_ipaddr & arpinfo->smsk.s_addr) != arpinfo->src.s_addr) ||
	    NF_INVF(arpinfo, ARPT_INV_TGTIP,
		    (tgt_ipaddr & arpinfo->tmsk.s_addr) != arpinfo->tgt.s_addr))
		return 0;

	/* Look for ifname matches.  */
	ret = ifname_compare(indev, arpinfo->iniface, arpinfo->iniface_mask);

	if (NF_INVF(arpinfo, ARPT_INV_VIA_IN, ret != 0))
		return 0;

	ret = ifname_compare(outdev, arpinfo->outiface, arpinfo->outiface_mask);

	if (NF_INVF(arpinfo, ARPT_INV_VIA_OUT, ret != 0))
		return 0;

	return 1;
}

static inline int arp_checkentry(const struct arpt_arp *arp)
{
	if (arp->flags & ~ARPT_F_MASK)
		return 0;
	if (arp->invflags & ~ARPT_INV_MASK)
		return 0;

	return 1;
}

static unsigned int
arpt_error(struct sk_buff *skb, const struct xt_action_param *par)
{
	net_err_ratelimited("arp_tables: error: '%s'\n",
			    (const char *)par->targinfo);

	return NF_DROP;
}

static inline const struct xt_entry_target *
arpt_get_target_c(const struct arpt_entry *e)
{
	return arpt_get_target((struct arpt_entry *)e);
}

static inline struct arpt_entry *
get_entry(const void *base, unsigned int offset)
{
	return (struct arpt_entry *)(base + offset);
}

static inline
struct arpt_entry *arpt_next_entry(const struct arpt_entry *entry)
{
	return (void *)entry + entry->next_offset;
}

unsigned int arpt_do_table(struct sk_buff *skb,
			   const struct nf_hook_state *state,
			   struct xt_table *table)
{
	unsigned int hook = state->hook;
	static const char nulldevname[IFNAMSIZ] __attribute__((aligned(sizeof(long))));
	unsigned int verdict = NF_DROP;
	const struct arphdr *arp;
	struct arpt_entry *e, **jumpstack;
	const char *indev, *outdev;
	const void *table_base;
	unsigned int cpu, stackidx = 0;
	const struct xt_table_info *private;
	struct xt_action_param acpar;
	unsigned int addend;

	if (!pskb_may_pull(skb, arp_hdr_len(skb->dev)))
		return NF_DROP;

	indev = state->in ? state->in->name : nulldevname;
	outdev = state->out ? state->out->name : nulldevname;

	local_bh_disable();
	addend = xt_write_recseq_begin();
	private = rcu_access_pointer(table->private);
	cpu     = smp_processor_id();
	table_base = private->entries;
	jumpstack  = (struct arpt_entry **)private->jumpstack[cpu];

	/* No TEE support for arptables, so no need to switch to alternate
	 * stack.  All targets that reenter must return absolute verdicts.
	 */
	e = get_entry(table_base, private->hook_entry[hook]);

	acpar.state   = state;
	acpar.hotdrop = false;

	arp = arp_hdr(skb);
	do {
		const struct xt_entry_target *t;
		struct xt_counters *counter;

		if (!arp_packet_match(arp, skb->dev, indev, outdev, &e->arp)) {
			e = arpt_next_entry(e);
			continue;
		}

		counter = xt_get_this_cpu_counter(&e->counters);
		ADD_COUNTER(*counter, arp_hdr_len(skb->dev), 1);

		t = arpt_get_target_c(e);

		/* Standard target? */
		if (!t->u.kernel.target->target) {
			int v;

			v = ((struct xt_standard_target *)t)->verdict;
			if (v < 0) {
				/* Pop from stack? */
				if (v != XT_RETURN) {
					verdict = (unsigned int)(-v) - 1;
					break;
				}
				if (stackidx == 0) {
					e = get_entry(table_base,
						      private->underflow[hook]);
				} else {
					e = jumpstack[--stackidx];
					e = arpt_next_entry(e);
				}
				continue;
			}
			if (table_base + v
			    != arpt_next_entry(e)) {
				if (unlikely(stackidx >= private->stacksize)) {
					verdict = NF_DROP;
					break;
				}
				jumpstack[stackidx++] = e;
			}

			e = get_entry(table_base, v);
			continue;
		}

		acpar.target   = t->u.kernel.target;
		acpar.targinfo = t->data;
		verdict = t->u.kernel.target->target(skb, &acpar);

		if (verdict == XT_CONTINUE) {
			/* Target might have changed stuff. */
			arp = arp_hdr(skb);
			e = arpt_next_entry(e);
		} else {
			/* Verdict */
			break;
		}
	} while (!acpar.hotdrop);
	xt_write_recseq_end(addend);
	local_bh_enable();

	if (acpar.hotdrop)
		return NF_DROP;
	else
		return verdict;
}

/* All zeroes == unconditional rule. */
static inline bool unconditional(const struct arpt_entry *e)
{
	static const struct arpt_arp uncond;

	return e->target_offset == sizeof(struct arpt_entry) &&
	       memcmp(&e->arp, &uncond, sizeof(uncond)) == 0;
}

/* Figures out from what hook each rule can be called: returns 0 if
 * there are loops.  Puts hook bitmask in comefrom.
 */
static int mark_source_chains(const struct xt_table_info *newinfo,
			      unsigned int valid_hooks, void *entry0,
			      unsigned int *offsets)
{
	unsigned int hook;

	/* No recursion; use packet counter to save back ptrs (reset
	 * to 0 as we leave), and comefrom to save source hook bitmask.
	 */
	for (hook = 0; hook < NF_ARP_NUMHOOKS; hook++) {
		unsigned int pos = newinfo->hook_entry[hook];
		struct arpt_entry *e = entry0 + pos;

		if (!(valid_hooks & (1 << hook)))
			continue;

		/* Set initial back pointer. */
		e->counters.pcnt = pos;

		for (;;) {
			const struct xt_standard_target *t
				= (void *)arpt_get_target_c(e);
			int visited = e->comefrom & (1 << hook);

			if (e->comefrom & (1 << NF_ARP_NUMHOOKS))
				return 0;

			e->comefrom
				|= ((1 << hook) | (1 << NF_ARP_NUMHOOKS));

			/* Unconditional return/END. */
			if ((unconditional(e) &&
			     (strcmp(t->target.u.user.name,
				     XT_STANDARD_TARGET) == 0) &&
			     t->verdict < 0) || visited) {
				unsigned int oldpos, size;

				/* Return: backtrack through the last
				 * big jump.
				 */
				do {
					e->comefrom ^= (1<<NF_ARP_NUMHOOKS);
					oldpos = pos;
					pos = e->counters.pcnt;
					e->counters.pcnt = 0;

					/* We're at the start. */
					if (pos == oldpos)
						goto next;

					e = entry0 + pos;
				} while (oldpos == pos + e->next_offset);

				/* Move along one */
				size = e->next_offset;
				e = entry0 + pos + size;
				if (pos + size >= newinfo->size)
					return 0;
				e->counters.pcnt = pos;
				pos += size;
			} else {
				int newpos = t->verdict;

				if (strcmp(t->target.u.user.name,
					   XT_STANDARD_TARGET) == 0 &&
				    newpos >= 0) {
					/* This a jump; chase it. */
					if (!xt_find_jump_offset(offsets, newpos,
								 newinfo->number))
						return 0;
				} else {
					/* ... this is a fallthru */
					newpos = pos + e->next_offset;
					if (newpos >= newinfo->size)
						return 0;
				}
				e = entry0 + newpos;
				e->counters.pcnt = pos;
				pos = newpos;
			}
		}
next:		;
	}
	return 1;
}

static int check_target(struct arpt_entry *e, struct net *net, const char *name)
{
	struct xt_entry_target *t = arpt_get_target(e);
	struct xt_tgchk_param par = {
		.net       = net,
		.table     = name,
		.entryinfo = e,
		.target    = t->u.kernel.target,
		.targinfo  = t->data,
		.hook_mask = e->comefrom,
		.family    = NFPROTO_ARP,
	};

	return xt_check_target(&par, t->u.target_size - sizeof(*t), 0, false);
}

static int
find_check_entry(struct arpt_entry *e, struct net *net, const char *name,
		 unsigned int size,
		 struct xt_percpu_counter_alloc_state *alloc_state)
{
	struct xt_entry_target *t;
	struct xt_target *target;
	int ret;

	if (!xt_percpu_counter_alloc(alloc_state, &e->counters))
		return -ENOMEM;

	t = arpt_get_target(e);
	target = xt_request_find_target(NFPROTO_ARP, t->u.user.name,
					t->u.user.revision);
	if (IS_ERR(target)) {
		ret = PTR_ERR(target);
		goto out;
	}
	t->u.kernel.target = target;

	ret = check_target(e, net, name);
	if (ret)
		goto err;
	return 0;
err:
	module_put(t->u.kernel.target->me);
out:
	xt_percpu_counter_free(&e->counters);

	return ret;
}

static bool check_underflow(const struct arpt_entry *e)
{
	const struct xt_entry_target *t;
	unsigned int verdict;

	if (!unconditional(e))
		return false;
	t = arpt_get_target_c(e);
	if (strcmp(t->u.user.name, XT_STANDARD_TARGET) != 0)
		return false;
	verdict = ((struct xt_standard_target *)t)->verdict;
	verdict = -verdict - 1;
	return verdict == NF_DROP || verdict == NF_ACCEPT;
}

static inline int check_entry_size_and_hooks(struct arpt_entry *e,
					     struct xt_table_info *newinfo,
					     const unsigned char *base,
					     const unsigned char *limit,
					     const unsigned int *hook_entries,
					     const unsigned int *underflows,
					     unsigned int valid_hooks)
{
	unsigned int h;
	int err;

	if ((unsigned long)e % __alignof__(struct arpt_entry) != 0 ||
	    (unsigned char *)e + sizeof(struct arpt_entry) >= limit ||
	    (unsigned char *)e + e->next_offset > limit)
		return -EINVAL;

	if (e->next_offset
	    < sizeof(struct arpt_entry) + sizeof(struct xt_entry_target))
		return -EINVAL;

	if (!arp_checkentry(&e->arp))
		return -EINVAL;

	err = xt_check_entry_offsets(e, e->elems, e->target_offset,
				     e->next_offset);
	if (err)
		return err;

	/* Check hooks & underflows */
	for (h = 0; h < NF_ARP_NUMHOOKS; h++) {
		if (!(valid_hooks & (1 << h)))
			continue;
		if ((unsigned char *)e - base == hook_entries[h])
			newinfo->hook_entry[h] = hook_entries[h];
		if ((unsigned char *)e - base == underflows[h]) {
			if (!check_underflow(e))
				return -EINVAL;

			newinfo->underflow[h] = underflows[h];
		}
	}

	/* Clear counters and comefrom */
	e->counters = ((struct xt_counters) { 0, 0 });
	e->comefrom = 0;
	return 0;
}

static void cleanup_entry(struct arpt_entry *e, struct net *net)
{
	struct xt_tgdtor_param par;
	struct xt_entry_target *t;

	t = arpt_get_target(e);
	par.net      = net;
	par.target   = t->u.kernel.target;
	par.targinfo = t->data;
	par.family   = NFPROTO_ARP;
	if (par.target->destroy != NULL)
		par.target->destroy(&par);
	module_put(par.target->me);
	xt_percpu_counter_free(&e->counters);
}

/* Checks and translates the user-supplied table segment (held in
 * newinfo).
 */
static int translate_table(struct net *net,
			   struct xt_table_info *newinfo,
			   void *entry0,
			   const struct arpt_replace *repl)
{
	struct xt_percpu_counter_alloc_state alloc_state = { 0 };
	struct arpt_entry *iter;
	unsigned int *offsets;
	unsigned int i;
	int ret = 0;

	newinfo->size = repl->size;
	newinfo->number = repl->num_entries;

	/* Init all hooks to impossible value. */
	for (i = 0; i < NF_ARP_NUMHOOKS; i++) {
		newinfo->hook_entry[i] = 0xFFFFFFFF;
		newinfo->underflow[i] = 0xFFFFFFFF;
	}

	offsets = xt_alloc_entry_offsets(newinfo->number);
	if (!offsets)
		return -ENOMEM;
	i = 0;

	/* Walk through entries, checking offsets. */
	xt_entry_foreach(iter, entry0, newinfo->size) {
		ret = check_entry_size_and_hooks(iter, newinfo, entry0,
						 entry0 + repl->size,
						 repl->hook_entry,
						 repl->underflow,
						 repl->valid_hooks);
		if (ret != 0)
			goto out_free;
		if (i < repl->num_entries)
			offsets[i] = (void *)iter - entry0;
		++i;
		if (strcmp(arpt_get_target(iter)->u.user.name,
		    XT_ERROR_TARGET) == 0)
			++newinfo->stacksize;
	}

	ret = -EINVAL;
	if (i != repl->num_entries)
		goto out_free;

	ret = xt_check_table_hooks(newinfo, repl->valid_hooks);
	if (ret)
		goto out_free;

	if (!mark_source_chains(newinfo, repl->valid_hooks, entry0, offsets)) {
		ret = -ELOOP;
		goto out_free;
	}
	kvfree(offsets);

	/* Finally, each sanity check must pass */
	i = 0;
	xt_entry_foreach(iter, entry0, newinfo->size) {
		ret = find_check_entry(iter, net, repl->name, repl->size,
				       &alloc_state);
		if (ret != 0)
			break;
		++i;
	}

	if (ret != 0) {
		xt_entry_foreach(iter, entry0, newinfo->size) {
			if (i-- == 0)
				break;
			cleanup_entry(iter, net);
		}
		return ret;
	}

	return ret;
 out_free:
	kvfree(offsets);
	return ret;
}

static void get_counters(const struct xt_table_info *t,
			 struct xt_counters counters[])
{
	struct arpt_entry *iter;
	unsigned int cpu;
	unsigned int i;

	for_each_possible_cpu(cpu) {
		seqcount_t *s = &per_cpu(xt_recseq, cpu);

		i = 0;
		xt_entry_foreach(iter, t->entries, t->size) {
			struct xt_counters *tmp;
			u64 bcnt, pcnt;
			unsigned int start;

			tmp = xt_get_per_cpu_counter(&iter->counters, cpu);
			do {
				start = read_seqcount_begin(s);
				bcnt = tmp->bcnt;
				pcnt = tmp->pcnt;
			} while (read_seqcount_retry(s, start));

			ADD_COUNTER(counters[i], bcnt, pcnt);
			++i;
			cond_resched();
		}
	}
}

static void get_old_counters(const struct xt_table_info *t,
			     struct xt_counters counters[])
{
	struct arpt_entry *iter;
	unsigned int cpu, i;

	for_each_possible_cpu(cpu) {
		i = 0;
		xt_entry_foreach(iter, t->entries, t->size) {
			struct xt_counters *tmp;

			tmp = xt_get_per_cpu_counter(&iter->counters, cpu);
			ADD_COUNTER(counters[i], tmp->bcnt, tmp->pcnt);
			++i;
		}
		cond_resched();
	}
}

static struct xt_counters *alloc_counters(const struct xt_table *table)
{
	unsigned int countersize;
	struct xt_counters *counters;
	const struct xt_table_info *private = xt_table_get_private_protected(table);

	/* We need atomic snapshot of counters: rest doesn't change
	 * (other than comefrom, which userspace doesn't care
	 * about).
	 */
	countersize = sizeof(struct xt_counters) * private->number;
	counters = vzalloc(countersize);

	if (counters == NULL)
		return ERR_PTR(-ENOMEM);

	get_counters(private, counters);

	return counters;
}

static int copy_entries_to_user(unsigned int total_size,
				const struct xt_table *table,
				void __user *userptr)
{
	unsigned int off, num;
	const struct arpt_entry *e;
	struct xt_counters *counters;
	struct xt_table_info *private = xt_table_get_private_protected(table);
	int ret = 0;
	void *loc_cpu_entry;

	counters = alloc_counters(table);
	if (IS_ERR(counters))
		return PTR_ERR(counters);

	loc_cpu_entry = private->entries;

	/* FIXME: use iterator macros --RR */
	/* ... then go back and fix counters and names */
	for (off = 0, num = 0; off < total_size; off += e->next_offset, num++){
		const struct xt_entry_target *t;

		e = loc_cpu_entry + off;
		if (copy_to_user(userptr + off, e, sizeof(*e))) {
			ret = -EFAULT;
			goto free_counters;
		}
		if (copy_to_user(userptr + off
				 + offsetof(struct arpt_entry, counters),
				 &counters[num],
				 sizeof(counters[num])) != 0) {
			ret = -EFAULT;
			goto free_counters;
		}

		t = arpt_get_target_c(e);
		if (xt_target_to_user(t, userptr + off + e->target_offset)) {
			ret = -EFAULT;
			goto free_counters;
		}
	}

 free_counters:
	vfree(counters);
	return ret;
}

#ifdef CONFIG_COMPAT
static void compat_standard_from_user(void *dst, const void *src)
{
	int v = *(compat_int_t *)src;

	if (v > 0)
		v += xt_compat_calc_jump(NFPROTO_ARP, v);
	memcpy(dst, &v, sizeof(v));
}

static int compat_standard_to_user(void __user *dst, const void *src)
{
	compat_int_t cv = *(int *)src;

	if (cv > 0)
		cv -= xt_compat_calc_jump(NFPROTO_ARP, cv);
	return copy_to_user(dst, &cv, sizeof(cv)) ? -EFAULT : 0;
}

static int compat_calc_entry(const struct arpt_entry *e,
			     const struct xt_table_info *info,
			     const void *base, struct xt_table_info *newinfo)
{
	const struct xt_entry_target *t;
	unsigned int entry_offset;
	int off, i, ret;

	off = sizeof(struct arpt_entry) - sizeof(struct compat_arpt_entry);
	entry_offset = (void *)e - base;

	t = arpt_get_target_c(e);
	off += xt_compat_target_offset(t->u.kernel.target);
	newinfo->size -= off;
	ret = xt_compat_add_offset(NFPROTO_ARP, entry_offset, off);
	if (ret)
		return ret;

	for (i = 0; i < NF_ARP_NUMHOOKS; i++) {
		if (info->hook_entry[i] &&
		    (e < (struct arpt_entry *)(base + info->hook_entry[i])))
			newinfo->hook_entry[i] -= off;
		if (info->underflow[i] &&
		    (e < (struct arpt_entry *)(base + info->underflow[i])))
			newinfo->underflow[i] -= off;
	}
	return 0;
}

static int compat_table_info(const struct xt_table_info *info,
			     struct xt_table_info *newinfo)
{
	struct arpt_entry *iter;
	const void *loc_cpu_entry;
	int ret;

	if (!newinfo || !info)
		return -EINVAL;

	/* we dont care about newinfo->entries */
	memcpy(newinfo, info, offsetof(struct xt_table_info, entries));
	newinfo->initial_entries = 0;
	loc_cpu_entry = info->entries;
	ret = xt_compat_init_offsets(NFPROTO_ARP, info->number);
	if (ret)
		return ret;
	xt_entry_foreach(iter, loc_cpu_entry, info->size) {
		ret = compat_calc_entry(iter, info, loc_cpu_entry, newinfo);
		if (ret != 0)
			return ret;
	}
	return 0;
}
#endif

static int get_info(struct net *net, void __user *user, const int *len)
{
	char name[XT_TABLE_MAXNAMELEN];
	struct xt_table *t;
	int ret;

	if (*len != sizeof(struct arpt_getinfo))
		return -EINVAL;

	if (copy_from_user(name, user, sizeof(name)) != 0)
		return -EFAULT;

	name[XT_TABLE_MAXNAMELEN-1] = '\0';
#ifdef CONFIG_COMPAT
	if (in_compat_syscall())
		xt_compat_lock(NFPROTO_ARP);
#endif
	t = xt_request_find_table_lock(net, NFPROTO_ARP, name);
	if (!IS_ERR(t)) {
		struct arpt_getinfo info;
		const struct xt_table_info *private = xt_table_get_private_protected(t);
#ifdef CONFIG_COMPAT
		struct xt_table_info tmp;

		if (in_compat_syscall()) {
			ret = compat_table_info(private, &tmp);
			xt_compat_flush_offsets(NFPROTO_ARP);
			private = &tmp;
		}
#endif
		memset(&info, 0, sizeof(info));
		info.valid_hooks = t->valid_hooks;
		memcpy(info.hook_entry, private->hook_entry,
		       sizeof(info.hook_entry));
		memcpy(info.underflow, private->underflow,
		       sizeof(info.underflow));
		info.num_entries = private->number;
		info.size = private->size;
		strcpy(info.name, name);

		if (copy_to_user(user, &info, *len) != 0)
			ret = -EFAULT;
		else
			ret = 0;
		xt_table_unlock(t);
		module_put(t->me);
	} else
		ret = PTR_ERR(t);
#ifdef CONFIG_COMPAT
	if (in_compat_syscall())
		xt_compat_unlock(NFPROTO_ARP);
#endif
	return ret;
}

static int get_entries(struct net *net, struct arpt_get_entries __user *uptr,
		       const int *len)
{
	int ret;
	struct arpt_get_entries get;
	struct xt_table *t;

	if (*len < sizeof(get))
		return -EINVAL;
	if (copy_from_user(&get, uptr, sizeof(get)) != 0)
		return -EFAULT;
	if (*len != sizeof(struct arpt_get_entries) + get.size)
		return -EINVAL;

	get.name[sizeof(get.name) - 1] = '\0';

	t = xt_find_table_lock(net, NFPROTO_ARP, get.name);
	if (!IS_ERR(t)) {
		const struct xt_table_info *private = xt_table_get_private_protected(t);

		if (get.size == private->size)
			ret = copy_entries_to_user(private->size,
						   t, uptr->entrytable);
		else
			ret = -EAGAIN;

		module_put(t->me);
		xt_table_unlock(t);
	} else
		ret = PTR_ERR(t);

	return ret;
}

static int __do_replace(struct net *net, const char *name,
			unsigned int valid_hooks,
			struct xt_table_info *newinfo,
			unsigned int num_counters,
			void __user *counters_ptr)
{
	int ret;
	struct xt_table *t;
	struct xt_table_info *oldinfo;
	struct xt_counters *counters;
	void *loc_cpu_old_entry;
	struct arpt_entry *iter;

	ret = 0;
	counters = xt_counters_alloc(num_counters);
	if (!counters) {
		ret = -ENOMEM;
		goto out;
	}

	t = xt_request_find_table_lock(net, NFPROTO_ARP, name);
	if (IS_ERR(t)) {
		ret = PTR_ERR(t);
		goto free_newinfo_counters_untrans;
	}

	/* You lied! */
	if (valid_hooks != t->valid_hooks) {
		ret = -EINVAL;
		goto put_module;
	}

	oldinfo = xt_replace_table(t, num_counters, newinfo, &ret);
	if (!oldinfo)
		goto put_module;

	/* Update module usage count based on number of rules */
	if ((oldinfo->number > oldinfo->initial_entries) ||
	    (newinfo->number <= oldinfo->initial_entries))
		module_put(t->me);
	if ((oldinfo->number > oldinfo->initial_entries) &&
	    (newinfo->number <= oldinfo->initial_entries))
		module_put(t->me);

	xt_table_unlock(t);

	get_old_counters(oldinfo, counters);

	/* Decrease module usage counts and free resource */
	loc_cpu_old_entry = oldinfo->entries;
	xt_entry_foreach(iter, loc_cpu_old_entry, oldinfo->size)
		cleanup_entry(iter, net);

	xt_free_table_info(oldinfo);
	if (copy_to_user(counters_ptr, counters,
			 sizeof(struct xt_counters) * num_counters) != 0) {
		/* Silent error, can't fail, new table is already in place */
		net_warn_ratelimited("arptables: counters copy to user failed while replacing table\n");
	}
	vfree(counters);
	return ret;

 put_module:
	module_put(t->me);
	xt_table_unlock(t);
 free_newinfo_counters_untrans:
	vfree(counters);
 out:
	return ret;
}

static int do_replace(struct net *net, sockptr_t arg, unsigned int len)
{
	int ret;
	struct arpt_replace tmp;
	struct xt_table_info *newinfo;
	void *loc_cpu_entry;
	struct arpt_entry *iter;

	if (copy_from_sockptr(&tmp, arg, sizeof(tmp)) != 0)
		return -EFAULT;

	/* overflow check */
	if (tmp.num_counters >= INT_MAX / sizeof(struct xt_counters))
		return -ENOMEM;
	if (tmp.num_counters == 0)
		return -EINVAL;

	tmp.name[sizeof(tmp.name)-1] = 0;

	newinfo = xt_alloc_table_info(tmp.size);
	if (!newinfo)
		return -ENOMEM;

	loc_cpu_entry = newinfo->entries;
	if (copy_from_sockptr_offset(loc_cpu_entry, arg, sizeof(tmp),
			tmp.size) != 0) {
		ret = -EFAULT;
		goto free_newinfo;
	}

	ret = translate_table(net, newinfo, loc_cpu_entry, &tmp);
	if (ret != 0)
		goto free_newinfo;

	ret = __do_replace(net, tmp.name, tmp.valid_hooks, newinfo,
			   tmp.num_counters, tmp.counters);
	if (ret)
		goto free_newinfo_untrans;
	return 0;

 free_newinfo_untrans:
	xt_entry_foreach(iter, loc_cpu_entry, newinfo->size)
		cleanup_entry(iter, net);
 free_newinfo:
	xt_free_table_info(newinfo);
	return ret;
}

static int do_add_counters(struct net *net, sockptr_t arg, unsigned int len)
{
	unsigned int i;
	struct xt_counters_info tmp;
	struct xt_counters *paddc;
	struct xt_table *t;
	const struct xt_table_info *private;
	int ret = 0;
	struct arpt_entry *iter;
	unsigned int addend;

	paddc = xt_copy_counters(arg, len, &tmp);
	if (IS_ERR(paddc))
		return PTR_ERR(paddc);

	t = xt_find_table_lock(net, NFPROTO_ARP, tmp.name);
	if (IS_ERR(t)) {
		ret = PTR_ERR(t);
		goto free;
	}

	local_bh_disable();
	private = xt_table_get_private_protected(t);
	if (private->number != tmp.num_counters) {
		ret = -EINVAL;
		goto unlock_up_free;
	}

	i = 0;

	addend = xt_write_recseq_begin();
	xt_entry_foreach(iter,  private->entries, private->size) {
		struct xt_counters *tmp;

		tmp = xt_get_this_cpu_counter(&iter->counters);
		ADD_COUNTER(*tmp, paddc[i].bcnt, paddc[i].pcnt);
		++i;
	}
	xt_write_recseq_end(addend);
 unlock_up_free:
	local_bh_enable();
	xt_table_unlock(t);
	module_put(t->me);
 free:
	vfree(paddc);

	return ret;
}

#ifdef CONFIG_COMPAT
struct compat_arpt_replace {
	char				name[XT_TABLE_MAXNAMELEN];
	u32				valid_hooks;
	u32				num_entries;
	u32				size;
	u32				hook_entry[NF_ARP_NUMHOOKS];
	u32				underflow[NF_ARP_NUMHOOKS];
	u32				num_counters;
	compat_uptr_t			counters;
	struct compat_arpt_entry	entries[];
};

static inline void compat_release_entry(struct compat_arpt_entry *e)
{
	struct xt_entry_target *t;

	t = compat_arpt_get_target(e);
	module_put(t->u.kernel.target->me);
}

static int
check_compat_entry_size_and_hooks(struct compat_arpt_entry *e,
				  struct xt_table_info *newinfo,
				  unsigned int *size,
				  const unsigned char *base,
				  const unsigned char *limit)
{
	struct xt_entry_target *t;
	struct xt_target *target;
	unsigned int entry_offset;
	int ret, off;

	if ((unsigned long)e % __alignof__(struct compat_arpt_entry) != 0 ||
	    (unsigned char *)e + sizeof(struct compat_arpt_entry) >= limit ||
	    (unsigned char *)e + e->next_offset > limit)
		return -EINVAL;

	if (e->next_offset < sizeof(struct compat_arpt_entry) +
			     sizeof(struct compat_xt_entry_target))
		return -EINVAL;

	if (!arp_checkentry(&e->arp))
		return -EINVAL;

	ret = xt_compat_check_entry_offsets(e, e->elems, e->target_offset,
					    e->next_offset);
	if (ret)
		return ret;

	off = sizeof(struct arpt_entry) - sizeof(struct compat_arpt_entry);
	entry_offset = (void *)e - (void *)base;

	t = compat_arpt_get_target(e);
	target = xt_request_find_target(NFPROTO_ARP, t->u.user.name,
					t->u.user.revision);
	if (IS_ERR(target)) {
		ret = PTR_ERR(target);
		goto out;
	}
	t->u.kernel.target = target;

	off += xt_compat_target_offset(target);
	*size += off;
	ret = xt_compat_add_offset(NFPROTO_ARP, entry_offset, off);
	if (ret)
		goto release_target;

	return 0;

release_target:
	module_put(t->u.kernel.target->me);
out:
	return ret;
}

static void
compat_copy_entry_from_user(struct compat_arpt_entry *e, void **dstptr,
			    unsigned int *size,
			    struct xt_table_info *newinfo, unsigned char *base)
{
	struct xt_entry_target *t;
	struct arpt_entry *de;
	unsigned int origsize;
	int h;

	origsize = *size;
	de = *dstptr;
	memcpy(de, e, sizeof(struct arpt_entry));
	memcpy(&de->counters, &e->counters, sizeof(e->counters));

	*dstptr += sizeof(struct arpt_entry);
	*size += sizeof(struct arpt_entry) - sizeof(struct compat_arpt_entry);

	de->target_offset = e->target_offset - (origsize - *size);
	t = compat_arpt_get_target(e);
	xt_compat_target_from_user(t, dstptr, size);

	de->next_offset = e->next_offset - (origsize - *size);
	for (h = 0; h < NF_ARP_NUMHOOKS; h++) {
		if ((unsigned char *)de - base < newinfo->hook_entry[h])
			newinfo->hook_entry[h] -= origsize - *size;
		if ((unsigned char *)de - base < newinfo->underflow[h])
			newinfo->underflow[h] -= origsize - *size;
	}
}

static int translate_compat_table(struct net *net,
				  struct xt_table_info **pinfo,
				  void **pentry0,
				  const struct compat_arpt_replace *compatr)
{
	unsigned int i, j;
	struct xt_table_info *newinfo, *info;
	void *pos, *entry0, *entry1;
	struct compat_arpt_entry *iter0;
	struct arpt_replace repl;
	unsigned int size;
	int ret;

	info = *pinfo;
	entry0 = *pentry0;
	size = compatr->size;
	info->number = compatr->num_entries;

	j = 0;
	xt_compat_lock(NFPROTO_ARP);
	ret = xt_compat_init_offsets(NFPROTO_ARP, compatr->num_entries);
	if (ret)
		goto out_unlock;
	/* Walk through entries, checking offsets. */
	xt_entry_foreach(iter0, entry0, compatr->size) {
		ret = check_compat_entry_size_and_hooks(iter0, info, &size,
							entry0,
							entry0 + compatr->size);
		if (ret != 0)
			goto out_unlock;
		++j;
	}

	ret = -EINVAL;
	if (j != compatr->num_entries)
		goto out_unlock;

	ret = -ENOMEM;
	newinfo = xt_alloc_table_info(size);
	if (!newinfo)
		goto out_unlock;

	newinfo->number = compatr->num_entries;
	for (i = 0; i < NF_ARP_NUMHOOKS; i++) {
		newinfo->hook_entry[i] = compatr->hook_entry[i];
		newinfo->underflow[i] = compatr->underflow[i];
	}
	entry1 = newinfo->entries;
	pos = entry1;
	size = compatr->size;
	xt_entry_foreach(iter0, entry0, compatr->size)
		compat_copy_entry_from_user(iter0, &pos, &size,
					    newinfo, entry1);

	/* all module references in entry0 are now gone */

	xt_compat_flush_offsets(NFPROTO_ARP);
	xt_compat_unlock(NFPROTO_ARP);

	memcpy(&repl, compatr, sizeof(*compatr));

	for (i = 0; i < NF_ARP_NUMHOOKS; i++) {
		repl.hook_entry[i] = newinfo->hook_entry[i];
		repl.underflow[i] = newinfo->underflow[i];
	}

	repl.num_counters = 0;
	repl.counters = NULL;
	repl.size = newinfo->size;
	ret = translate_table(net, newinfo, entry1, &repl);
	if (ret)
		goto free_newinfo;

	*pinfo = newinfo;
	*pentry0 = entry1;
	xt_free_table_info(info);
	return 0;

free_newinfo:
	xt_free_table_info(newinfo);
	return ret;
out_unlock:
	xt_compat_flush_offsets(NFPROTO_ARP);
	xt_compat_unlock(NFPROTO_ARP);
	xt_entry_foreach(iter0, entry0, compatr->size) {
		if (j-- == 0)
			break;
		compat_release_entry(iter0);
	}
	return ret;
}

static int compat_do_replace(struct net *net, sockptr_t arg, unsigned int len)
{
	int ret;
	struct compat_arpt_replace tmp;
	struct xt_table_info *newinfo;
	void *loc_cpu_entry;
	struct arpt_entry *iter;

	if (copy_from_sockptr(&tmp, arg, sizeof(tmp)) != 0)
		return -EFAULT;

	/* overflow check */
	if (tmp.num_counters >= INT_MAX / sizeof(struct xt_counters))
		return -ENOMEM;
	if (tmp.num_counters == 0)
		return -EINVAL;

	tmp.name[sizeof(tmp.name)-1] = 0;

	newinfo = xt_alloc_table_info(tmp.size);
	if (!newinfo)
		return -ENOMEM;

	loc_cpu_entry = newinfo->entries;
	if (copy_from_sockptr_offset(loc_cpu_entry, arg, sizeof(tmp),
			tmp.size) != 0) {
		ret = -EFAULT;
		goto free_newinfo;
	}

	ret = translate_compat_table(net, &newinfo, &loc_cpu_entry, &tmp);
	if (ret != 0)
		goto free_newinfo;

	ret = __do_replace(net, tmp.name, tmp.valid_hooks, newinfo,
			   tmp.num_counters, compat_ptr(tmp.counters));
	if (ret)
		goto free_newinfo_untrans;
	return 0;

 free_newinfo_untrans:
	xt_entry_foreach(iter, loc_cpu_entry, newinfo->size)
		cleanup_entry(iter, net);
 free_newinfo:
	xt_free_table_info(newinfo);
	return ret;
}

static int compat_copy_entry_to_user(struct arpt_entry *e, void __user **dstptr,
				     compat_uint_t *size,
				     struct xt_counters *counters,
				     unsigned int i)
{
	struct xt_entry_target *t;
	struct compat_arpt_entry __user *ce;
	u_int16_t target_offset, next_offset;
	compat_uint_t origsize;
	int ret;

	origsize = *size;
	ce = *dstptr;
	if (copy_to_user(ce, e, sizeof(struct arpt_entry)) != 0 ||
	    copy_to_user(&ce->counters, &counters[i],
	    sizeof(counters[i])) != 0)
		return -EFAULT;

	*dstptr += sizeof(struct compat_arpt_entry);
	*size -= sizeof(struct arpt_entry) - sizeof(struct compat_arpt_entry);

	target_offset = e->target_offset - (origsize - *size);

	t = arpt_get_target(e);
	ret = xt_compat_target_to_user(t, dstptr, size);
	if (ret)
		return ret;
	next_offset = e->next_offset - (origsize - *size);
	if (put_user(target_offset, &ce->target_offset) != 0 ||
	    put_user(next_offset, &ce->next_offset) != 0)
		return -EFAULT;
	return 0;
}

static int compat_copy_entries_to_user(unsigned int total_size,
				       struct xt_table *table,
				       void __user *userptr)
{
	struct xt_counters *counters;
	const struct xt_table_info *private = xt_table_get_private_protected(table);
	void __user *pos;
	unsigned int size;
	int ret = 0;
	unsigned int i = 0;
	struct arpt_entry *iter;

	counters = alloc_counters(table);
	if (IS_ERR(counters))
		return PTR_ERR(counters);

	pos = userptr;
	size = total_size;
	xt_entry_foreach(iter, private->entries, total_size) {
		ret = compat_copy_entry_to_user(iter, &pos,
						&size, counters, i++);
		if (ret != 0)
			break;
	}
	vfree(counters);
	return ret;
}

struct compat_arpt_get_entries {
	char name[XT_TABLE_MAXNAMELEN];
	compat_uint_t size;
	struct compat_arpt_entry entrytable[];
};

static int compat_get_entries(struct net *net,
			      struct compat_arpt_get_entries __user *uptr,
			      int *len)
{
	int ret;
	struct compat_arpt_get_entries get;
	struct xt_table *t;

	if (*len < sizeof(get))
		return -EINVAL;
	if (copy_from_user(&get, uptr, sizeof(get)) != 0)
		return -EFAULT;
	if (*len != sizeof(struct compat_arpt_get_entries) + get.size)
		return -EINVAL;

	get.name[sizeof(get.name) - 1] = '\0';

	xt_compat_lock(NFPROTO_ARP);
	t = xt_find_table_lock(net, NFPROTO_ARP, get.name);
	if (!IS_ERR(t)) {
		const struct xt_table_info *private = xt_table_get_private_protected(t);
		struct xt_table_info info;

		ret = compat_table_info(private, &info);
		if (!ret && get.size == info.size) {
			ret = compat_copy_entries_to_user(private->size,
							  t, uptr->entrytable);
		} else if (!ret)
			ret = -EAGAIN;

		xt_compat_flush_offsets(NFPROTO_ARP);
		module_put(t->me);
		xt_table_unlock(t);
	} else
		ret = PTR_ERR(t);

	xt_compat_unlock(NFPROTO_ARP);
	return ret;
}
#endif

static int do_arpt_set_ctl(struct sock *sk, int cmd, sockptr_t arg,
		unsigned int len)
{
	int ret;

	if (!ns_capable(sock_net(sk)->user_ns, CAP_NET_ADMIN))
		return -EPERM;

	switch (cmd) {
	case ARPT_SO_SET_REPLACE:
#ifdef CONFIG_COMPAT
		if (in_compat_syscall())
			ret = compat_do_replace(sock_net(sk), arg, len);
		else
#endif
			ret = do_replace(sock_net(sk), arg, len);
		break;

	case ARPT_SO_SET_ADD_COUNTERS:
		ret = do_add_counters(sock_net(sk), arg, len);
		break;

	default:
		ret = -EINVAL;
	}

	return ret;
}

static int do_arpt_get_ctl(struct sock *sk, int cmd, void __user *user, int *len)
{
	int ret;

	if (!ns_capable(sock_net(sk)->user_ns, CAP_NET_ADMIN))
		return -EPERM;

	switch (cmd) {
	case ARPT_SO_GET_INFO:
		ret = get_info(sock_net(sk), user, len);
		break;

	case ARPT_SO_GET_ENTRIES:
#ifdef CONFIG_COMPAT
		if (in_compat_syscall())
			ret = compat_get_entries(sock_net(sk), user, len);
		else
#endif
			ret = get_entries(sock_net(sk), user, len);
		break;

	case ARPT_SO_GET_REVISION_TARGET: {
		struct xt_get_revision rev;

		if (*len != sizeof(rev)) {
			ret = -EINVAL;
			break;
		}
		if (copy_from_user(&rev, user, sizeof(rev)) != 0) {
			ret = -EFAULT;
			break;
		}
		rev.name[sizeof(rev.name)-1] = 0;

		try_then_request_module(xt_find_revision(NFPROTO_ARP, rev.name,
							 rev.revision, 1, &ret),
					"arpt_%s", rev.name);
		break;
	}

	default:
		ret = -EINVAL;
	}

	return ret;
}

static void __arpt_unregister_table(struct net *net, struct xt_table *table)
{
	struct xt_table_info *private;
	void *loc_cpu_entry;
	struct module *table_owner = table->me;
	struct arpt_entry *iter;

	private = xt_unregister_table(table);

	/* Decrease module usage counts and free resources */
	loc_cpu_entry = private->entries;
	xt_entry_foreach(iter, loc_cpu_entry, private->size)
		cleanup_entry(iter, net);
	if (private->number > private->initial_entries)
		module_put(table_owner);
	xt_free_table_info(private);
}

int arpt_register_table(struct net *net,
			const struct xt_table *table,
			const struct arpt_replace *repl,
			const struct nf_hook_ops *ops,
			struct xt_table **res)
{
	int ret;
	struct xt_table_info *newinfo;
	struct xt_table_info bootstrap = {0};
	void *loc_cpu_entry;
	struct xt_table *new_table;

	newinfo = xt_alloc_table_info(repl->size);
	if (!newinfo)
		return -ENOMEM;

	loc_cpu_entry = newinfo->entries;
	memcpy(loc_cpu_entry, repl->entries, repl->size);

	ret = translate_table(net, newinfo, loc_cpu_entry, repl);
	if (ret != 0)
		goto out_free;

	new_table = xt_register_table(net, table, &bootstrap, newinfo);
	if (IS_ERR(new_table)) {
		ret = PTR_ERR(new_table);
		goto out_free;
	}

	/* set res now, will see skbs right after nf_register_net_hooks */
	WRITE_ONCE(*res, new_table);

	ret = nf_register_net_hooks(net, ops, hweight32(table->valid_hooks));
	if (ret != 0) {
		__arpt_unregister_table(net, new_table);
		*res = NULL;
	}

	return ret;

out_free:
	xt_free_table_info(newinfo);
	return ret;
}

void arpt_unregister_table(struct net *net, struct xt_table *table,
			   const struct nf_hook_ops *ops)
{
	nf_unregister_net_hooks(net, ops, hweight32(table->valid_hooks));
	__arpt_unregister_table(net, table);
}

/* The built-in targets: standard (NULL) and error. */
static struct xt_target arpt_builtin_tg[] __read_mostly = {
	{
		.name             = XT_STANDARD_TARGET,
		.targetsize       = sizeof(int),
		.family           = NFPROTO_ARP,
#ifdef CONFIG_COMPAT
		.compatsize       = sizeof(compat_int_t),
		.compat_from_user = compat_standard_from_user,
		.compat_to_user   = compat_standard_to_user,
#endif
	},
	{
		.name             = XT_ERROR_TARGET,
		.target           = arpt_error,
		.targetsize       = XT_FUNCTION_MAXNAMELEN,
		.family           = NFPROTO_ARP,
	},
};

static struct nf_sockopt_ops arpt_sockopts = {
	.pf		= PF_INET,
	.set_optmin	= ARPT_BASE_CTL,
	.set_optmax	= ARPT_SO_SET_MAX+1,
	.set		= do_arpt_set_ctl,
	.get_optmin	= ARPT_BASE_CTL,
	.get_optmax	= ARPT_SO_GET_MAX+1,
	.get		= do_arpt_get_ctl,
	.owner		= THIS_MODULE,
};

static int __net_init arp_tables_net_init(struct net *net)
{
	return xt_proto_init(net, NFPROTO_ARP);
}

static void __net_exit arp_tables_net_exit(struct net *net)
{
	xt_proto_fini(net, NFPROTO_ARP);
}

static struct pernet_operations arp_tables_net_ops = {
	.init = arp_tables_net_init,
	.exit = arp_tables_net_exit,
};

static int __init arp_tables_init(void)
{
	int ret;

	ret = register_pernet_subsys(&arp_tables_net_ops);
	if (ret < 0)
		goto err1;

	/* No one else will be downing sem now, so we won't sleep */
	ret = xt_register_targets(arpt_builtin_tg, ARRAY_SIZE(arpt_builtin_tg));
	if (ret < 0)
		goto err2;

	/* Register setsockopt */
	ret = nf_register_sockopt(&arpt_sockopts);
	if (ret < 0)
		goto err4;

	return 0;

err4:
	xt_unregister_targets(arpt_builtin_tg, ARRAY_SIZE(arpt_builtin_tg));
err2:
	unregister_pernet_subsys(&arp_tables_net_ops);
err1:
	return ret;
}

static void __exit arp_tables_fini(void)
{
	nf_unregister_sockopt(&arpt_sockopts);
	xt_unregister_targets(arpt_builtin_tg, ARRAY_SIZE(arpt_builtin_tg));
	unregister_pernet_subsys(&arp_tables_net_ops);
}

EXPORT_SYMBOL(arpt_register_table);
EXPORT_SYMBOL(arpt_unregister_table);
EXPORT_SYMBOL(arpt_do_table);

module_init(arp_tables_init);
module_exit(arp_tables_fini);
