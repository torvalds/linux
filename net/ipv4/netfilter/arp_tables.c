/*
 * Packet matching code for ARP packets.
 *
 * Based heavily, if not almost entirely, upon ip_tables.c framework.
 *
 * Some ARP specific bits are:
 *
 * Copyright (C) 2002 David S. Miller (davem@redhat.com)
 *
 */

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
#include <asm/uaccess.h>

#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_arp/arp_tables.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("David S. Miller <davem@redhat.com>");
MODULE_DESCRIPTION("arptables core");

/*#define DEBUG_ARP_TABLES*/
/*#define DEBUG_ARP_TABLES_USER*/

#ifdef DEBUG_ARP_TABLES
#define dprintf(format, args...)  printk(format , ## args)
#else
#define dprintf(format, args...)
#endif

#ifdef DEBUG_ARP_TABLES_USER
#define duprintf(format, args...) printk(format , ## args)
#else
#define duprintf(format, args...)
#endif

#ifdef CONFIG_NETFILTER_DEBUG
#define ARP_NF_ASSERT(x)					\
do {								\
	if (!(x))						\
		printk("ARP_NF_ASSERT: %s:%s:%u\n",		\
		       __func__, __FILE__, __LINE__);	\
} while(0)
#else
#define ARP_NF_ASSERT(x)
#endif

static inline int arp_devaddr_compare(const struct arpt_devaddr_info *ap,
				      const char *hdr_addr, int len)
{
	int i, ret;

	if (len > ARPT_DEV_ADDR_LEN_MAX)
		len = ARPT_DEV_ADDR_LEN_MAX;

	ret = 0;
	for (i = 0; i < len; i++)
		ret |= (hdr_addr[i] ^ ap->addr[i]) & ap->mask[i];

	return (ret != 0);
}

/*
 * Unfortunatly, _b and _mask are not aligned to an int (or long int)
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

#define FWINV(bool, invflg) ((bool) ^ !!(arpinfo->invflags & (invflg)))

	if (FWINV((arphdr->ar_op & arpinfo->arpop_mask) != arpinfo->arpop,
		  ARPT_INV_ARPOP)) {
		dprintf("ARP operation field mismatch.\n");
		dprintf("ar_op: %04x info->arpop: %04x info->arpop_mask: %04x\n",
			arphdr->ar_op, arpinfo->arpop, arpinfo->arpop_mask);
		return 0;
	}

	if (FWINV((arphdr->ar_hrd & arpinfo->arhrd_mask) != arpinfo->arhrd,
		  ARPT_INV_ARPHRD)) {
		dprintf("ARP hardware address format mismatch.\n");
		dprintf("ar_hrd: %04x info->arhrd: %04x info->arhrd_mask: %04x\n",
			arphdr->ar_hrd, arpinfo->arhrd, arpinfo->arhrd_mask);
		return 0;
	}

	if (FWINV((arphdr->ar_pro & arpinfo->arpro_mask) != arpinfo->arpro,
		  ARPT_INV_ARPPRO)) {
		dprintf("ARP protocol address format mismatch.\n");
		dprintf("ar_pro: %04x info->arpro: %04x info->arpro_mask: %04x\n",
			arphdr->ar_pro, arpinfo->arpro, arpinfo->arpro_mask);
		return 0;
	}

	if (FWINV((arphdr->ar_hln & arpinfo->arhln_mask) != arpinfo->arhln,
		  ARPT_INV_ARPHLN)) {
		dprintf("ARP hardware address length mismatch.\n");
		dprintf("ar_hln: %02x info->arhln: %02x info->arhln_mask: %02x\n",
			arphdr->ar_hln, arpinfo->arhln, arpinfo->arhln_mask);
		return 0;
	}

	src_devaddr = arpptr;
	arpptr += dev->addr_len;
	memcpy(&src_ipaddr, arpptr, sizeof(u32));
	arpptr += sizeof(u32);
	tgt_devaddr = arpptr;
	arpptr += dev->addr_len;
	memcpy(&tgt_ipaddr, arpptr, sizeof(u32));

	if (FWINV(arp_devaddr_compare(&arpinfo->src_devaddr, src_devaddr, dev->addr_len),
		  ARPT_INV_SRCDEVADDR) ||
	    FWINV(arp_devaddr_compare(&arpinfo->tgt_devaddr, tgt_devaddr, dev->addr_len),
		  ARPT_INV_TGTDEVADDR)) {
		dprintf("Source or target device address mismatch.\n");

		return 0;
	}

	if (FWINV((src_ipaddr & arpinfo->smsk.s_addr) != arpinfo->src.s_addr,
		  ARPT_INV_SRCIP) ||
	    FWINV(((tgt_ipaddr & arpinfo->tmsk.s_addr) != arpinfo->tgt.s_addr),
		  ARPT_INV_TGTIP)) {
		dprintf("Source or target IP address mismatch.\n");

		dprintf("SRC: %pI4. Mask: %pI4. Target: %pI4.%s\n",
			&src_ipaddr,
			&arpinfo->smsk.s_addr,
			&arpinfo->src.s_addr,
			arpinfo->invflags & ARPT_INV_SRCIP ? " (INV)" : "");
		dprintf("TGT: %pI4 Mask: %pI4 Target: %pI4.%s\n",
			&tgt_ipaddr,
			&arpinfo->tmsk.s_addr,
			&arpinfo->tgt.s_addr,
			arpinfo->invflags & ARPT_INV_TGTIP ? " (INV)" : "");
		return 0;
	}

	/* Look for ifname matches.  */
	ret = ifname_compare(indev, arpinfo->iniface, arpinfo->iniface_mask);

	if (FWINV(ret != 0, ARPT_INV_VIA_IN)) {
		dprintf("VIA in mismatch (%s vs %s).%s\n",
			indev, arpinfo->iniface,
			arpinfo->invflags&ARPT_INV_VIA_IN ?" (INV)":"");
		return 0;
	}

	ret = ifname_compare(outdev, arpinfo->outiface, arpinfo->outiface_mask);

	if (FWINV(ret != 0, ARPT_INV_VIA_OUT)) {
		dprintf("VIA out mismatch (%s vs %s).%s\n",
			outdev, arpinfo->outiface,
			arpinfo->invflags&ARPT_INV_VIA_OUT ?" (INV)":"");
		return 0;
	}

	return 1;
#undef FWINV
}

static inline int arp_checkentry(const struct arpt_arp *arp)
{
	if (arp->flags & ~ARPT_F_MASK) {
		duprintf("Unknown flag bits set: %08X\n",
			 arp->flags & ~ARPT_F_MASK);
		return 0;
	}
	if (arp->invflags & ~ARPT_INV_MASK) {
		duprintf("Unknown invflag bits set: %08X\n",
			 arp->invflags & ~ARPT_INV_MASK);
		return 0;
	}

	return 1;
}

static unsigned int
arpt_error(struct sk_buff *skb, const struct xt_target_param *par)
{
	if (net_ratelimit())
		printk("arp_tables: error: '%s'\n",
		       (const char *)par->targinfo);

	return NF_DROP;
}

static inline struct arpt_entry *get_entry(void *base, unsigned int offset)
{
	return (struct arpt_entry *)(base + offset);
}

unsigned int arpt_do_table(struct sk_buff *skb,
			   unsigned int hook,
			   const struct net_device *in,
			   const struct net_device *out,
			   struct xt_table *table)
{
	static const char nulldevname[IFNAMSIZ] __attribute__((aligned(sizeof(long))));
	unsigned int verdict = NF_DROP;
	const struct arphdr *arp;
	bool hotdrop = false;
	struct arpt_entry *e, *back;
	const char *indev, *outdev;
	void *table_base;
	const struct xt_table_info *private;
	struct xt_target_param tgpar;

	if (!pskb_may_pull(skb, arp_hdr_len(skb->dev)))
		return NF_DROP;

	indev = in ? in->name : nulldevname;
	outdev = out ? out->name : nulldevname;

	xt_info_rdlock_bh();
	private = table->private;
	table_base = private->entries[smp_processor_id()];

	e = get_entry(table_base, private->hook_entry[hook]);
	back = get_entry(table_base, private->underflow[hook]);

	tgpar.in      = in;
	tgpar.out     = out;
	tgpar.hooknum = hook;
	tgpar.family  = NFPROTO_ARP;

	arp = arp_hdr(skb);
	do {
		if (arp_packet_match(arp, skb->dev, indev, outdev, &e->arp)) {
			struct arpt_entry_target *t;
			int hdr_len;

			hdr_len = sizeof(*arp) + (2 * sizeof(struct in_addr)) +
				(2 * skb->dev->addr_len);

			ADD_COUNTER(e->counters, hdr_len, 1);

			t = arpt_get_target(e);

			/* Standard target? */
			if (!t->u.kernel.target->target) {
				int v;

				v = ((struct arpt_standard_target *)t)->verdict;
				if (v < 0) {
					/* Pop from stack? */
					if (v != ARPT_RETURN) {
						verdict = (unsigned)(-v) - 1;
						break;
					}
					e = back;
					back = get_entry(table_base,
							 back->comefrom);
					continue;
				}
				if (table_base + v
				    != (void *)e + e->next_offset) {
					/* Save old back ptr in next entry */
					struct arpt_entry *next
						= (void *)e + e->next_offset;
					next->comefrom =
						(void *)back - table_base;

					/* set back pointer to next entry */
					back = next;
				}

				e = get_entry(table_base, v);
			} else {
				/* Targets which reenter must return
				 * abs. verdicts
				 */
				tgpar.target   = t->u.kernel.target;
				tgpar.targinfo = t->data;
				verdict = t->u.kernel.target->target(skb,
								     &tgpar);

				/* Target might have changed stuff. */
				arp = arp_hdr(skb);

				if (verdict == ARPT_CONTINUE)
					e = (void *)e + e->next_offset;
				else
					/* Verdict */
					break;
			}
		} else {
			e = (void *)e + e->next_offset;
		}
	} while (!hotdrop);
	xt_info_rdunlock_bh();

	if (hotdrop)
		return NF_DROP;
	else
		return verdict;
}

/* All zeroes == unconditional rule. */
static inline int unconditional(const struct arpt_arp *arp)
{
	unsigned int i;

	for (i = 0; i < sizeof(*arp)/sizeof(__u32); i++)
		if (((__u32 *)arp)[i])
			return 0;

	return 1;
}

/* Figures out from what hook each rule can be called: returns 0 if
 * there are loops.  Puts hook bitmask in comefrom.
 */
static int mark_source_chains(struct xt_table_info *newinfo,
			      unsigned int valid_hooks, void *entry0)
{
	unsigned int hook;

	/* No recursion; use packet counter to save back ptrs (reset
	 * to 0 as we leave), and comefrom to save source hook bitmask.
	 */
	for (hook = 0; hook < NF_ARP_NUMHOOKS; hook++) {
		unsigned int pos = newinfo->hook_entry[hook];
		struct arpt_entry *e
			= (struct arpt_entry *)(entry0 + pos);

		if (!(valid_hooks & (1 << hook)))
			continue;

		/* Set initial back pointer. */
		e->counters.pcnt = pos;

		for (;;) {
			const struct arpt_standard_target *t
				= (void *)arpt_get_target(e);
			int visited = e->comefrom & (1 << hook);

			if (e->comefrom & (1 << NF_ARP_NUMHOOKS)) {
				printk("arptables: loop hook %u pos %u %08X.\n",
				       hook, pos, e->comefrom);
				return 0;
			}
			e->comefrom
				|= ((1 << hook) | (1 << NF_ARP_NUMHOOKS));

			/* Unconditional return/END. */
			if ((e->target_offset == sizeof(struct arpt_entry)
			    && (strcmp(t->target.u.user.name,
				       ARPT_STANDARD_TARGET) == 0)
			    && t->verdict < 0
			    && unconditional(&e->arp)) || visited) {
				unsigned int oldpos, size;

				if ((strcmp(t->target.u.user.name,
					    ARPT_STANDARD_TARGET) == 0) &&
				    t->verdict < -NF_MAX_VERDICT - 1) {
					duprintf("mark_source_chains: bad "
						"negative verdict (%i)\n",
								t->verdict);
					return 0;
				}

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

					e = (struct arpt_entry *)
						(entry0 + pos);
				} while (oldpos == pos + e->next_offset);

				/* Move along one */
				size = e->next_offset;
				e = (struct arpt_entry *)
					(entry0 + pos + size);
				e->counters.pcnt = pos;
				pos += size;
			} else {
				int newpos = t->verdict;

				if (strcmp(t->target.u.user.name,
					   ARPT_STANDARD_TARGET) == 0
				    && newpos >= 0) {
					if (newpos > newinfo->size -
						sizeof(struct arpt_entry)) {
						duprintf("mark_source_chains: "
							"bad verdict (%i)\n",
								newpos);
						return 0;
					}

					/* This a jump; chase it. */
					duprintf("Jump rule %u -> %u\n",
						 pos, newpos);
				} else {
					/* ... this is a fallthru */
					newpos = pos + e->next_offset;
				}
				e = (struct arpt_entry *)
					(entry0 + newpos);
				e->counters.pcnt = pos;
				pos = newpos;
			}
		}
		next:
		duprintf("Finished chain %u\n", hook);
	}
	return 1;
}

static inline int check_entry(struct arpt_entry *e, const char *name)
{
	const struct arpt_entry_target *t;

	if (!arp_checkentry(&e->arp)) {
		duprintf("arp_tables: arp check failed %p %s.\n", e, name);
		return -EINVAL;
	}

	if (e->target_offset + sizeof(struct arpt_entry_target) > e->next_offset)
		return -EINVAL;

	t = arpt_get_target(e);
	if (e->target_offset + t->u.target_size > e->next_offset)
		return -EINVAL;

	return 0;
}

static inline int check_target(struct arpt_entry *e, const char *name)
{
	struct arpt_entry_target *t = arpt_get_target(e);
	int ret;
	struct xt_tgchk_param par = {
		.table     = name,
		.entryinfo = e,
		.target    = t->u.kernel.target,
		.targinfo  = t->data,
		.hook_mask = e->comefrom,
		.family    = NFPROTO_ARP,
	};

	ret = xt_check_target(&par, t->u.target_size - sizeof(*t), 0, false);
	if (ret < 0) {
		duprintf("arp_tables: check failed for `%s'.\n",
			 t->u.kernel.target->name);
		return ret;
	}
	return 0;
}

static inline int
find_check_entry(struct arpt_entry *e, const char *name, unsigned int size,
		 unsigned int *i)
{
	struct arpt_entry_target *t;
	struct xt_target *target;
	int ret;

	ret = check_entry(e, name);
	if (ret)
		return ret;

	t = arpt_get_target(e);
	target = try_then_request_module(xt_find_target(NFPROTO_ARP,
							t->u.user.name,
							t->u.user.revision),
					 "arpt_%s", t->u.user.name);
	if (IS_ERR(target) || !target) {
		duprintf("find_check_entry: `%s' not found\n", t->u.user.name);
		ret = target ? PTR_ERR(target) : -ENOENT;
		goto out;
	}
	t->u.kernel.target = target;

	ret = check_target(e, name);
	if (ret)
		goto err;

	(*i)++;
	return 0;
err:
	module_put(t->u.kernel.target->me);
out:
	return ret;
}

static inline int check_entry_size_and_hooks(struct arpt_entry *e,
					     struct xt_table_info *newinfo,
					     unsigned char *base,
					     unsigned char *limit,
					     const unsigned int *hook_entries,
					     const unsigned int *underflows,
					     unsigned int *i)
{
	unsigned int h;

	if ((unsigned long)e % __alignof__(struct arpt_entry) != 0
	    || (unsigned char *)e + sizeof(struct arpt_entry) >= limit) {
		duprintf("Bad offset %p\n", e);
		return -EINVAL;
	}

	if (e->next_offset
	    < sizeof(struct arpt_entry) + sizeof(struct arpt_entry_target)) {
		duprintf("checking: element %p size %u\n",
			 e, e->next_offset);
		return -EINVAL;
	}

	/* Check hooks & underflows */
	for (h = 0; h < NF_ARP_NUMHOOKS; h++) {
		if ((unsigned char *)e - base == hook_entries[h])
			newinfo->hook_entry[h] = hook_entries[h];
		if ((unsigned char *)e - base == underflows[h])
			newinfo->underflow[h] = underflows[h];
	}

	/* FIXME: underflows must be unconditional, standard verdicts
	   < 0 (not ARPT_RETURN). --RR */

	/* Clear counters and comefrom */
	e->counters = ((struct xt_counters) { 0, 0 });
	e->comefrom = 0;

	(*i)++;
	return 0;
}

static inline int cleanup_entry(struct arpt_entry *e, unsigned int *i)
{
	struct xt_tgdtor_param par;
	struct arpt_entry_target *t;

	if (i && (*i)-- == 0)
		return 1;

	t = arpt_get_target(e);
	par.target   = t->u.kernel.target;
	par.targinfo = t->data;
	par.family   = NFPROTO_ARP;
	if (par.target->destroy != NULL)
		par.target->destroy(&par);
	module_put(par.target->me);
	return 0;
}

/* Checks and translates the user-supplied table segment (held in
 * newinfo).
 */
static int translate_table(const char *name,
			   unsigned int valid_hooks,
			   struct xt_table_info *newinfo,
			   void *entry0,
			   unsigned int size,
			   unsigned int number,
			   const unsigned int *hook_entries,
			   const unsigned int *underflows)
{
	unsigned int i;
	int ret;

	newinfo->size = size;
	newinfo->number = number;

	/* Init all hooks to impossible value. */
	for (i = 0; i < NF_ARP_NUMHOOKS; i++) {
		newinfo->hook_entry[i] = 0xFFFFFFFF;
		newinfo->underflow[i] = 0xFFFFFFFF;
	}

	duprintf("translate_table: size %u\n", newinfo->size);
	i = 0;

	/* Walk through entries, checking offsets. */
	ret = ARPT_ENTRY_ITERATE(entry0, newinfo->size,
				 check_entry_size_and_hooks,
				 newinfo,
				 entry0,
				 entry0 + size,
				 hook_entries, underflows, &i);
	duprintf("translate_table: ARPT_ENTRY_ITERATE gives %d\n", ret);
	if (ret != 0)
		return ret;

	if (i != number) {
		duprintf("translate_table: %u not %u entries\n",
			 i, number);
		return -EINVAL;
	}

	/* Check hooks all assigned */
	for (i = 0; i < NF_ARP_NUMHOOKS; i++) {
		/* Only hooks which are valid */
		if (!(valid_hooks & (1 << i)))
			continue;
		if (newinfo->hook_entry[i] == 0xFFFFFFFF) {
			duprintf("Invalid hook entry %u %u\n",
				 i, hook_entries[i]);
			return -EINVAL;
		}
		if (newinfo->underflow[i] == 0xFFFFFFFF) {
			duprintf("Invalid underflow %u %u\n",
				 i, underflows[i]);
			return -EINVAL;
		}
	}

	if (!mark_source_chains(newinfo, valid_hooks, entry0)) {
		duprintf("Looping hook\n");
		return -ELOOP;
	}

	/* Finally, each sanity check must pass */
	i = 0;
	ret = ARPT_ENTRY_ITERATE(entry0, newinfo->size,
				 find_check_entry, name, size, &i);

	if (ret != 0) {
		ARPT_ENTRY_ITERATE(entry0, newinfo->size,
				cleanup_entry, &i);
		return ret;
	}

	/* And one copy for every other CPU */
	for_each_possible_cpu(i) {
		if (newinfo->entries[i] && newinfo->entries[i] != entry0)
			memcpy(newinfo->entries[i], entry0, newinfo->size);
	}

	return ret;
}

/* Gets counters. */
static inline int add_entry_to_counter(const struct arpt_entry *e,
				       struct xt_counters total[],
				       unsigned int *i)
{
	ADD_COUNTER(total[*i], e->counters.bcnt, e->counters.pcnt);

	(*i)++;
	return 0;
}

static inline int set_entry_to_counter(const struct arpt_entry *e,
				       struct xt_counters total[],
				       unsigned int *i)
{
	SET_COUNTER(total[*i], e->counters.bcnt, e->counters.pcnt);

	(*i)++;
	return 0;
}

static void get_counters(const struct xt_table_info *t,
			 struct xt_counters counters[])
{
	unsigned int cpu;
	unsigned int i;
	unsigned int curcpu;

	/* Instead of clearing (by a previous call to memset())
	 * the counters and using adds, we set the counters
	 * with data used by 'current' CPU
	 *
	 * Bottom half has to be disabled to prevent deadlock
	 * if new softirq were to run and call ipt_do_table
	 */
	local_bh_disable();
	curcpu = smp_processor_id();

	i = 0;
	ARPT_ENTRY_ITERATE(t->entries[curcpu],
			   t->size,
			   set_entry_to_counter,
			   counters,
			   &i);

	for_each_possible_cpu(cpu) {
		if (cpu == curcpu)
			continue;
		i = 0;
		xt_info_wrlock(cpu);
		ARPT_ENTRY_ITERATE(t->entries[cpu],
				   t->size,
				   add_entry_to_counter,
				   counters,
				   &i);
		xt_info_wrunlock(cpu);
	}
	local_bh_enable();
}

static struct xt_counters *alloc_counters(struct xt_table *table)
{
	unsigned int countersize;
	struct xt_counters *counters;
	struct xt_table_info *private = table->private;

	/* We need atomic snapshot of counters: rest doesn't change
	 * (other than comefrom, which userspace doesn't care
	 * about).
	 */
	countersize = sizeof(struct xt_counters) * private->number;
	counters = vmalloc_node(countersize, numa_node_id());

	if (counters == NULL)
		return ERR_PTR(-ENOMEM);

	get_counters(private, counters);

	return counters;
}

static int copy_entries_to_user(unsigned int total_size,
				struct xt_table *table,
				void __user *userptr)
{
	unsigned int off, num;
	struct arpt_entry *e;
	struct xt_counters *counters;
	struct xt_table_info *private = table->private;
	int ret = 0;
	void *loc_cpu_entry;

	counters = alloc_counters(table);
	if (IS_ERR(counters))
		return PTR_ERR(counters);

	loc_cpu_entry = private->entries[raw_smp_processor_id()];
	/* ... then copy entire thing ... */
	if (copy_to_user(userptr, loc_cpu_entry, total_size) != 0) {
		ret = -EFAULT;
		goto free_counters;
	}

	/* FIXME: use iterator macros --RR */
	/* ... then go back and fix counters and names */
	for (off = 0, num = 0; off < total_size; off += e->next_offset, num++){
		struct arpt_entry_target *t;

		e = (struct arpt_entry *)(loc_cpu_entry + off);
		if (copy_to_user(userptr + off
				 + offsetof(struct arpt_entry, counters),
				 &counters[num],
				 sizeof(counters[num])) != 0) {
			ret = -EFAULT;
			goto free_counters;
		}

		t = arpt_get_target(e);
		if (copy_to_user(userptr + off + e->target_offset
				 + offsetof(struct arpt_entry_target,
					    u.user.name),
				 t->u.kernel.target->name,
				 strlen(t->u.kernel.target->name)+1) != 0) {
			ret = -EFAULT;
			goto free_counters;
		}
	}

 free_counters:
	vfree(counters);
	return ret;
}

#ifdef CONFIG_COMPAT
static void compat_standard_from_user(void *dst, void *src)
{
	int v = *(compat_int_t *)src;

	if (v > 0)
		v += xt_compat_calc_jump(NFPROTO_ARP, v);
	memcpy(dst, &v, sizeof(v));
}

static int compat_standard_to_user(void __user *dst, void *src)
{
	compat_int_t cv = *(int *)src;

	if (cv > 0)
		cv -= xt_compat_calc_jump(NFPROTO_ARP, cv);
	return copy_to_user(dst, &cv, sizeof(cv)) ? -EFAULT : 0;
}

static int compat_calc_entry(struct arpt_entry *e,
			     const struct xt_table_info *info,
			     void *base, struct xt_table_info *newinfo)
{
	struct arpt_entry_target *t;
	unsigned int entry_offset;
	int off, i, ret;

	off = sizeof(struct arpt_entry) - sizeof(struct compat_arpt_entry);
	entry_offset = (void *)e - base;

	t = arpt_get_target(e);
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
	void *loc_cpu_entry;

	if (!newinfo || !info)
		return -EINVAL;

	/* we dont care about newinfo->entries[] */
	memcpy(newinfo, info, offsetof(struct xt_table_info, entries));
	newinfo->initial_entries = 0;
	loc_cpu_entry = info->entries[raw_smp_processor_id()];
	return ARPT_ENTRY_ITERATE(loc_cpu_entry, info->size,
				  compat_calc_entry, info, loc_cpu_entry,
				  newinfo);
}
#endif

static int get_info(struct net *net, void __user *user, int *len, int compat)
{
	char name[ARPT_TABLE_MAXNAMELEN];
	struct xt_table *t;
	int ret;

	if (*len != sizeof(struct arpt_getinfo)) {
		duprintf("length %u != %Zu\n", *len,
			 sizeof(struct arpt_getinfo));
		return -EINVAL;
	}

	if (copy_from_user(name, user, sizeof(name)) != 0)
		return -EFAULT;

	name[ARPT_TABLE_MAXNAMELEN-1] = '\0';
#ifdef CONFIG_COMPAT
	if (compat)
		xt_compat_lock(NFPROTO_ARP);
#endif
	t = try_then_request_module(xt_find_table_lock(net, NFPROTO_ARP, name),
				    "arptable_%s", name);
	if (t && !IS_ERR(t)) {
		struct arpt_getinfo info;
		const struct xt_table_info *private = t->private;

#ifdef CONFIG_COMPAT
		if (compat) {
			struct xt_table_info tmp;
			ret = compat_table_info(private, &tmp);
			xt_compat_flush_offsets(NFPROTO_ARP);
			private = &tmp;
		}
#endif
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
		ret = t ? PTR_ERR(t) : -ENOENT;
#ifdef CONFIG_COMPAT
	if (compat)
		xt_compat_unlock(NFPROTO_ARP);
#endif
	return ret;
}

static int get_entries(struct net *net, struct arpt_get_entries __user *uptr,
		       int *len)
{
	int ret;
	struct arpt_get_entries get;
	struct xt_table *t;

	if (*len < sizeof(get)) {
		duprintf("get_entries: %u < %Zu\n", *len, sizeof(get));
		return -EINVAL;
	}
	if (copy_from_user(&get, uptr, sizeof(get)) != 0)
		return -EFAULT;
	if (*len != sizeof(struct arpt_get_entries) + get.size) {
		duprintf("get_entries: %u != %Zu\n", *len,
			 sizeof(struct arpt_get_entries) + get.size);
		return -EINVAL;
	}

	t = xt_find_table_lock(net, NFPROTO_ARP, get.name);
	if (t && !IS_ERR(t)) {
		const struct xt_table_info *private = t->private;

		duprintf("t->private->number = %u\n",
			 private->number);
		if (get.size == private->size)
			ret = copy_entries_to_user(private->size,
						   t, uptr->entrytable);
		else {
			duprintf("get_entries: I've got %u not %u!\n",
				 private->size, get.size);
			ret = -EAGAIN;
		}
		module_put(t->me);
		xt_table_unlock(t);
	} else
		ret = t ? PTR_ERR(t) : -ENOENT;

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

	ret = 0;
	counters = vmalloc_node(num_counters * sizeof(struct xt_counters),
				numa_node_id());
	if (!counters) {
		ret = -ENOMEM;
		goto out;
	}

	t = try_then_request_module(xt_find_table_lock(net, NFPROTO_ARP, name),
				    "arptable_%s", name);
	if (!t || IS_ERR(t)) {
		ret = t ? PTR_ERR(t) : -ENOENT;
		goto free_newinfo_counters_untrans;
	}

	/* You lied! */
	if (valid_hooks != t->valid_hooks) {
		duprintf("Valid hook crap: %08X vs %08X\n",
			 valid_hooks, t->valid_hooks);
		ret = -EINVAL;
		goto put_module;
	}

	oldinfo = xt_replace_table(t, num_counters, newinfo, &ret);
	if (!oldinfo)
		goto put_module;

	/* Update module usage count based on number of rules */
	duprintf("do_replace: oldnum=%u, initnum=%u, newnum=%u\n",
		oldinfo->number, oldinfo->initial_entries, newinfo->number);
	if ((oldinfo->number > oldinfo->initial_entries) ||
	    (newinfo->number <= oldinfo->initial_entries))
		module_put(t->me);
	if ((oldinfo->number > oldinfo->initial_entries) &&
	    (newinfo->number <= oldinfo->initial_entries))
		module_put(t->me);

	/* Get the old counters, and synchronize with replace */
	get_counters(oldinfo, counters);

	/* Decrease module usage counts and free resource */
	loc_cpu_old_entry = oldinfo->entries[raw_smp_processor_id()];
	ARPT_ENTRY_ITERATE(loc_cpu_old_entry, oldinfo->size, cleanup_entry,
			   NULL);

	xt_free_table_info(oldinfo);
	if (copy_to_user(counters_ptr, counters,
			 sizeof(struct xt_counters) * num_counters) != 0)
		ret = -EFAULT;
	vfree(counters);
	xt_table_unlock(t);
	return ret;

 put_module:
	module_put(t->me);
	xt_table_unlock(t);
 free_newinfo_counters_untrans:
	vfree(counters);
 out:
	return ret;
}

static int do_replace(struct net *net, void __user *user, unsigned int len)
{
	int ret;
	struct arpt_replace tmp;
	struct xt_table_info *newinfo;
	void *loc_cpu_entry;

	if (copy_from_user(&tmp, user, sizeof(tmp)) != 0)
		return -EFAULT;

	/* overflow check */
	if (tmp.num_counters >= INT_MAX / sizeof(struct xt_counters))
		return -ENOMEM;

	newinfo = xt_alloc_table_info(tmp.size);
	if (!newinfo)
		return -ENOMEM;

	/* choose the copy that is on our node/cpu */
	loc_cpu_entry = newinfo->entries[raw_smp_processor_id()];
	if (copy_from_user(loc_cpu_entry, user + sizeof(tmp),
			   tmp.size) != 0) {
		ret = -EFAULT;
		goto free_newinfo;
	}

	ret = translate_table(tmp.name, tmp.valid_hooks,
			      newinfo, loc_cpu_entry, tmp.size, tmp.num_entries,
			      tmp.hook_entry, tmp.underflow);
	if (ret != 0)
		goto free_newinfo;

	duprintf("arp_tables: Translated table\n");

	ret = __do_replace(net, tmp.name, tmp.valid_hooks, newinfo,
			   tmp.num_counters, tmp.counters);
	if (ret)
		goto free_newinfo_untrans;
	return 0;

 free_newinfo_untrans:
	ARPT_ENTRY_ITERATE(loc_cpu_entry, newinfo->size, cleanup_entry, NULL);
 free_newinfo:
	xt_free_table_info(newinfo);
	return ret;
}

/* We're lazy, and add to the first CPU; overflow works its fey magic
 * and everything is OK. */
static int
add_counter_to_entry(struct arpt_entry *e,
		     const struct xt_counters addme[],
		     unsigned int *i)
{
	ADD_COUNTER(e->counters, addme[*i].bcnt, addme[*i].pcnt);

	(*i)++;
	return 0;
}

static int do_add_counters(struct net *net, void __user *user, unsigned int len,
			   int compat)
{
	unsigned int i, curcpu;
	struct xt_counters_info tmp;
	struct xt_counters *paddc;
	unsigned int num_counters;
	const char *name;
	int size;
	void *ptmp;
	struct xt_table *t;
	const struct xt_table_info *private;
	int ret = 0;
	void *loc_cpu_entry;
#ifdef CONFIG_COMPAT
	struct compat_xt_counters_info compat_tmp;

	if (compat) {
		ptmp = &compat_tmp;
		size = sizeof(struct compat_xt_counters_info);
	} else
#endif
	{
		ptmp = &tmp;
		size = sizeof(struct xt_counters_info);
	}

	if (copy_from_user(ptmp, user, size) != 0)
		return -EFAULT;

#ifdef CONFIG_COMPAT
	if (compat) {
		num_counters = compat_tmp.num_counters;
		name = compat_tmp.name;
	} else
#endif
	{
		num_counters = tmp.num_counters;
		name = tmp.name;
	}

	if (len != size + num_counters * sizeof(struct xt_counters))
		return -EINVAL;

	paddc = vmalloc_node(len - size, numa_node_id());
	if (!paddc)
		return -ENOMEM;

	if (copy_from_user(paddc, user + size, len - size) != 0) {
		ret = -EFAULT;
		goto free;
	}

	t = xt_find_table_lock(net, NFPROTO_ARP, name);
	if (!t || IS_ERR(t)) {
		ret = t ? PTR_ERR(t) : -ENOENT;
		goto free;
	}

	local_bh_disable();
	private = t->private;
	if (private->number != num_counters) {
		ret = -EINVAL;
		goto unlock_up_free;
	}

	i = 0;
	/* Choose the copy that is on our node */
	curcpu = smp_processor_id();
	loc_cpu_entry = private->entries[curcpu];
	xt_info_wrlock(curcpu);
	ARPT_ENTRY_ITERATE(loc_cpu_entry,
			   private->size,
			   add_counter_to_entry,
			   paddc,
			   &i);
	xt_info_wrunlock(curcpu);
 unlock_up_free:
	local_bh_enable();
	xt_table_unlock(t);
	module_put(t->me);
 free:
	vfree(paddc);

	return ret;
}

#ifdef CONFIG_COMPAT
static inline int
compat_release_entry(struct compat_arpt_entry *e, unsigned int *i)
{
	struct arpt_entry_target *t;

	if (i && (*i)-- == 0)
		return 1;

	t = compat_arpt_get_target(e);
	module_put(t->u.kernel.target->me);
	return 0;
}

static inline int
check_compat_entry_size_and_hooks(struct compat_arpt_entry *e,
				  struct xt_table_info *newinfo,
				  unsigned int *size,
				  unsigned char *base,
				  unsigned char *limit,
				  unsigned int *hook_entries,
				  unsigned int *underflows,
				  unsigned int *i,
				  const char *name)
{
	struct arpt_entry_target *t;
	struct xt_target *target;
	unsigned int entry_offset;
	int ret, off, h;

	duprintf("check_compat_entry_size_and_hooks %p\n", e);
	if ((unsigned long)e % __alignof__(struct compat_arpt_entry) != 0
	    || (unsigned char *)e + sizeof(struct compat_arpt_entry) >= limit) {
		duprintf("Bad offset %p, limit = %p\n", e, limit);
		return -EINVAL;
	}

	if (e->next_offset < sizeof(struct compat_arpt_entry) +
			     sizeof(struct compat_xt_entry_target)) {
		duprintf("checking: element %p size %u\n",
			 e, e->next_offset);
		return -EINVAL;
	}

	/* For purposes of check_entry casting the compat entry is fine */
	ret = check_entry((struct arpt_entry *)e, name);
	if (ret)
		return ret;

	off = sizeof(struct arpt_entry) - sizeof(struct compat_arpt_entry);
	entry_offset = (void *)e - (void *)base;

	t = compat_arpt_get_target(e);
	target = try_then_request_module(xt_find_target(NFPROTO_ARP,
							t->u.user.name,
							t->u.user.revision),
					 "arpt_%s", t->u.user.name);
	if (IS_ERR(target) || !target) {
		duprintf("check_compat_entry_size_and_hooks: `%s' not found\n",
			 t->u.user.name);
		ret = target ? PTR_ERR(target) : -ENOENT;
		goto out;
	}
	t->u.kernel.target = target;

	off += xt_compat_target_offset(target);
	*size += off;
	ret = xt_compat_add_offset(NFPROTO_ARP, entry_offset, off);
	if (ret)
		goto release_target;

	/* Check hooks & underflows */
	for (h = 0; h < NF_ARP_NUMHOOKS; h++) {
		if ((unsigned char *)e - base == hook_entries[h])
			newinfo->hook_entry[h] = hook_entries[h];
		if ((unsigned char *)e - base == underflows[h])
			newinfo->underflow[h] = underflows[h];
	}

	/* Clear counters and comefrom */
	memset(&e->counters, 0, sizeof(e->counters));
	e->comefrom = 0;

	(*i)++;
	return 0;

release_target:
	module_put(t->u.kernel.target->me);
out:
	return ret;
}

static int
compat_copy_entry_from_user(struct compat_arpt_entry *e, void **dstptr,
			    unsigned int *size, const char *name,
			    struct xt_table_info *newinfo, unsigned char *base)
{
	struct arpt_entry_target *t;
	struct xt_target *target;
	struct arpt_entry *de;
	unsigned int origsize;
	int ret, h;

	ret = 0;
	origsize = *size;
	de = (struct arpt_entry *)*dstptr;
	memcpy(de, e, sizeof(struct arpt_entry));
	memcpy(&de->counters, &e->counters, sizeof(e->counters));

	*dstptr += sizeof(struct arpt_entry);
	*size += sizeof(struct arpt_entry) - sizeof(struct compat_arpt_entry);

	de->target_offset = e->target_offset - (origsize - *size);
	t = compat_arpt_get_target(e);
	target = t->u.kernel.target;
	xt_compat_target_from_user(t, dstptr, size);

	de->next_offset = e->next_offset - (origsize - *size);
	for (h = 0; h < NF_ARP_NUMHOOKS; h++) {
		if ((unsigned char *)de - base < newinfo->hook_entry[h])
			newinfo->hook_entry[h] -= origsize - *size;
		if ((unsigned char *)de - base < newinfo->underflow[h])
			newinfo->underflow[h] -= origsize - *size;
	}
	return ret;
}

static inline int compat_check_entry(struct arpt_entry *e, const char *name,
				     unsigned int *i)
{
	int ret;

	ret = check_target(e, name);
	if (ret)
		return ret;

	(*i)++;
	return 0;
}

static int translate_compat_table(const char *name,
				  unsigned int valid_hooks,
				  struct xt_table_info **pinfo,
				  void **pentry0,
				  unsigned int total_size,
				  unsigned int number,
				  unsigned int *hook_entries,
				  unsigned int *underflows)
{
	unsigned int i, j;
	struct xt_table_info *newinfo, *info;
	void *pos, *entry0, *entry1;
	unsigned int size;
	int ret;

	info = *pinfo;
	entry0 = *pentry0;
	size = total_size;
	info->number = number;

	/* Init all hooks to impossible value. */
	for (i = 0; i < NF_ARP_NUMHOOKS; i++) {
		info->hook_entry[i] = 0xFFFFFFFF;
		info->underflow[i] = 0xFFFFFFFF;
	}

	duprintf("translate_compat_table: size %u\n", info->size);
	j = 0;
	xt_compat_lock(NFPROTO_ARP);
	/* Walk through entries, checking offsets. */
	ret = COMPAT_ARPT_ENTRY_ITERATE(entry0, total_size,
					check_compat_entry_size_and_hooks,
					info, &size, entry0,
					entry0 + total_size,
					hook_entries, underflows, &j, name);
	if (ret != 0)
		goto out_unlock;

	ret = -EINVAL;
	if (j != number) {
		duprintf("translate_compat_table: %u not %u entries\n",
			 j, number);
		goto out_unlock;
	}

	/* Check hooks all assigned */
	for (i = 0; i < NF_ARP_NUMHOOKS; i++) {
		/* Only hooks which are valid */
		if (!(valid_hooks & (1 << i)))
			continue;
		if (info->hook_entry[i] == 0xFFFFFFFF) {
			duprintf("Invalid hook entry %u %u\n",
				 i, hook_entries[i]);
			goto out_unlock;
		}
		if (info->underflow[i] == 0xFFFFFFFF) {
			duprintf("Invalid underflow %u %u\n",
				 i, underflows[i]);
			goto out_unlock;
		}
	}

	ret = -ENOMEM;
	newinfo = xt_alloc_table_info(size);
	if (!newinfo)
		goto out_unlock;

	newinfo->number = number;
	for (i = 0; i < NF_ARP_NUMHOOKS; i++) {
		newinfo->hook_entry[i] = info->hook_entry[i];
		newinfo->underflow[i] = info->underflow[i];
	}
	entry1 = newinfo->entries[raw_smp_processor_id()];
	pos = entry1;
	size = total_size;
	ret = COMPAT_ARPT_ENTRY_ITERATE(entry0, total_size,
					compat_copy_entry_from_user,
					&pos, &size, name, newinfo, entry1);
	xt_compat_flush_offsets(NFPROTO_ARP);
	xt_compat_unlock(NFPROTO_ARP);
	if (ret)
		goto free_newinfo;

	ret = -ELOOP;
	if (!mark_source_chains(newinfo, valid_hooks, entry1))
		goto free_newinfo;

	i = 0;
	ret = ARPT_ENTRY_ITERATE(entry1, newinfo->size, compat_check_entry,
				 name, &i);
	if (ret) {
		j -= i;
		COMPAT_ARPT_ENTRY_ITERATE_CONTINUE(entry0, newinfo->size, i,
						   compat_release_entry, &j);
		ARPT_ENTRY_ITERATE(entry1, newinfo->size, cleanup_entry, &i);
		xt_free_table_info(newinfo);
		return ret;
	}

	/* And one copy for every other CPU */
	for_each_possible_cpu(i)
		if (newinfo->entries[i] && newinfo->entries[i] != entry1)
			memcpy(newinfo->entries[i], entry1, newinfo->size);

	*pinfo = newinfo;
	*pentry0 = entry1;
	xt_free_table_info(info);
	return 0;

free_newinfo:
	xt_free_table_info(newinfo);
out:
	COMPAT_ARPT_ENTRY_ITERATE(entry0, total_size, compat_release_entry, &j);
	return ret;
out_unlock:
	xt_compat_flush_offsets(NFPROTO_ARP);
	xt_compat_unlock(NFPROTO_ARP);
	goto out;
}

struct compat_arpt_replace {
	char				name[ARPT_TABLE_MAXNAMELEN];
	u32				valid_hooks;
	u32				num_entries;
	u32				size;
	u32				hook_entry[NF_ARP_NUMHOOKS];
	u32				underflow[NF_ARP_NUMHOOKS];
	u32				num_counters;
	compat_uptr_t			counters;
	struct compat_arpt_entry	entries[0];
};

static int compat_do_replace(struct net *net, void __user *user,
			     unsigned int len)
{
	int ret;
	struct compat_arpt_replace tmp;
	struct xt_table_info *newinfo;
	void *loc_cpu_entry;

	if (copy_from_user(&tmp, user, sizeof(tmp)) != 0)
		return -EFAULT;

	/* overflow check */
	if (tmp.size >= INT_MAX / num_possible_cpus())
		return -ENOMEM;
	if (tmp.num_counters >= INT_MAX / sizeof(struct xt_counters))
		return -ENOMEM;

	newinfo = xt_alloc_table_info(tmp.size);
	if (!newinfo)
		return -ENOMEM;

	/* choose the copy that is on our node/cpu */
	loc_cpu_entry = newinfo->entries[raw_smp_processor_id()];
	if (copy_from_user(loc_cpu_entry, user + sizeof(tmp), tmp.size) != 0) {
		ret = -EFAULT;
		goto free_newinfo;
	}

	ret = translate_compat_table(tmp.name, tmp.valid_hooks,
				     &newinfo, &loc_cpu_entry, tmp.size,
				     tmp.num_entries, tmp.hook_entry,
				     tmp.underflow);
	if (ret != 0)
		goto free_newinfo;

	duprintf("compat_do_replace: Translated table\n");

	ret = __do_replace(net, tmp.name, tmp.valid_hooks, newinfo,
			   tmp.num_counters, compat_ptr(tmp.counters));
	if (ret)
		goto free_newinfo_untrans;
	return 0;

 free_newinfo_untrans:
	ARPT_ENTRY_ITERATE(loc_cpu_entry, newinfo->size, cleanup_entry, NULL);
 free_newinfo:
	xt_free_table_info(newinfo);
	return ret;
}

static int compat_do_arpt_set_ctl(struct sock *sk, int cmd, void __user *user,
				  unsigned int len)
{
	int ret;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	switch (cmd) {
	case ARPT_SO_SET_REPLACE:
		ret = compat_do_replace(sock_net(sk), user, len);
		break;

	case ARPT_SO_SET_ADD_COUNTERS:
		ret = do_add_counters(sock_net(sk), user, len, 1);
		break;

	default:
		duprintf("do_arpt_set_ctl:  unknown request %i\n", cmd);
		ret = -EINVAL;
	}

	return ret;
}

static int compat_copy_entry_to_user(struct arpt_entry *e, void __user **dstptr,
				     compat_uint_t *size,
				     struct xt_counters *counters,
				     unsigned int *i)
{
	struct arpt_entry_target *t;
	struct compat_arpt_entry __user *ce;
	u_int16_t target_offset, next_offset;
	compat_uint_t origsize;
	int ret;

	ret = -EFAULT;
	origsize = *size;
	ce = (struct compat_arpt_entry __user *)*dstptr;
	if (copy_to_user(ce, e, sizeof(struct arpt_entry)))
		goto out;

	if (copy_to_user(&ce->counters, &counters[*i], sizeof(counters[*i])))
		goto out;

	*dstptr += sizeof(struct compat_arpt_entry);
	*size -= sizeof(struct arpt_entry) - sizeof(struct compat_arpt_entry);

	target_offset = e->target_offset - (origsize - *size);

	t = arpt_get_target(e);
	ret = xt_compat_target_to_user(t, dstptr, size);
	if (ret)
		goto out;
	ret = -EFAULT;
	next_offset = e->next_offset - (origsize - *size);
	if (put_user(target_offset, &ce->target_offset))
		goto out;
	if (put_user(next_offset, &ce->next_offset))
		goto out;

	(*i)++;
	return 0;
out:
	return ret;
}

static int compat_copy_entries_to_user(unsigned int total_size,
				       struct xt_table *table,
				       void __user *userptr)
{
	struct xt_counters *counters;
	const struct xt_table_info *private = table->private;
	void __user *pos;
	unsigned int size;
	int ret = 0;
	void *loc_cpu_entry;
	unsigned int i = 0;

	counters = alloc_counters(table);
	if (IS_ERR(counters))
		return PTR_ERR(counters);

	/* choose the copy on our node/cpu */
	loc_cpu_entry = private->entries[raw_smp_processor_id()];
	pos = userptr;
	size = total_size;
	ret = ARPT_ENTRY_ITERATE(loc_cpu_entry, total_size,
				 compat_copy_entry_to_user,
				 &pos, &size, counters, &i);
	vfree(counters);
	return ret;
}

struct compat_arpt_get_entries {
	char name[ARPT_TABLE_MAXNAMELEN];
	compat_uint_t size;
	struct compat_arpt_entry entrytable[0];
};

static int compat_get_entries(struct net *net,
			      struct compat_arpt_get_entries __user *uptr,
			      int *len)
{
	int ret;
	struct compat_arpt_get_entries get;
	struct xt_table *t;

	if (*len < sizeof(get)) {
		duprintf("compat_get_entries: %u < %zu\n", *len, sizeof(get));
		return -EINVAL;
	}
	if (copy_from_user(&get, uptr, sizeof(get)) != 0)
		return -EFAULT;
	if (*len != sizeof(struct compat_arpt_get_entries) + get.size) {
		duprintf("compat_get_entries: %u != %zu\n",
			 *len, sizeof(get) + get.size);
		return -EINVAL;
	}

	xt_compat_lock(NFPROTO_ARP);
	t = xt_find_table_lock(net, NFPROTO_ARP, get.name);
	if (t && !IS_ERR(t)) {
		const struct xt_table_info *private = t->private;
		struct xt_table_info info;

		duprintf("t->private->number = %u\n", private->number);
		ret = compat_table_info(private, &info);
		if (!ret && get.size == info.size) {
			ret = compat_copy_entries_to_user(private->size,
							  t, uptr->entrytable);
		} else if (!ret) {
			duprintf("compat_get_entries: I've got %u not %u!\n",
				 private->size, get.size);
			ret = -EAGAIN;
		}
		xt_compat_flush_offsets(NFPROTO_ARP);
		module_put(t->me);
		xt_table_unlock(t);
	} else
		ret = t ? PTR_ERR(t) : -ENOENT;

	xt_compat_unlock(NFPROTO_ARP);
	return ret;
}

static int do_arpt_get_ctl(struct sock *, int, void __user *, int *);

static int compat_do_arpt_get_ctl(struct sock *sk, int cmd, void __user *user,
				  int *len)
{
	int ret;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	switch (cmd) {
	case ARPT_SO_GET_INFO:
		ret = get_info(sock_net(sk), user, len, 1);
		break;
	case ARPT_SO_GET_ENTRIES:
		ret = compat_get_entries(sock_net(sk), user, len);
		break;
	default:
		ret = do_arpt_get_ctl(sk, cmd, user, len);
	}
	return ret;
}
#endif

static int do_arpt_set_ctl(struct sock *sk, int cmd, void __user *user, unsigned int len)
{
	int ret;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	switch (cmd) {
	case ARPT_SO_SET_REPLACE:
		ret = do_replace(sock_net(sk), user, len);
		break;

	case ARPT_SO_SET_ADD_COUNTERS:
		ret = do_add_counters(sock_net(sk), user, len, 0);
		break;

	default:
		duprintf("do_arpt_set_ctl:  unknown request %i\n", cmd);
		ret = -EINVAL;
	}

	return ret;
}

static int do_arpt_get_ctl(struct sock *sk, int cmd, void __user *user, int *len)
{
	int ret;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	switch (cmd) {
	case ARPT_SO_GET_INFO:
		ret = get_info(sock_net(sk), user, len, 0);
		break;

	case ARPT_SO_GET_ENTRIES:
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

		try_then_request_module(xt_find_revision(NFPROTO_ARP, rev.name,
							 rev.revision, 1, &ret),
					"arpt_%s", rev.name);
		break;
	}

	default:
		duprintf("do_arpt_get_ctl: unknown request %i\n", cmd);
		ret = -EINVAL;
	}

	return ret;
}

struct xt_table *arpt_register_table(struct net *net, struct xt_table *table,
				     const struct arpt_replace *repl)
{
	int ret;
	struct xt_table_info *newinfo;
	struct xt_table_info bootstrap
		= { 0, 0, 0, { 0 }, { 0 }, { } };
	void *loc_cpu_entry;
	struct xt_table *new_table;

	newinfo = xt_alloc_table_info(repl->size);
	if (!newinfo) {
		ret = -ENOMEM;
		goto out;
	}

	/* choose the copy on our node/cpu */
	loc_cpu_entry = newinfo->entries[raw_smp_processor_id()];
	memcpy(loc_cpu_entry, repl->entries, repl->size);

	ret = translate_table(table->name, table->valid_hooks,
			      newinfo, loc_cpu_entry, repl->size,
			      repl->num_entries,
			      repl->hook_entry,
			      repl->underflow);

	duprintf("arpt_register_table: translate table gives %d\n", ret);
	if (ret != 0)
		goto out_free;

	new_table = xt_register_table(net, table, &bootstrap, newinfo);
	if (IS_ERR(new_table)) {
		ret = PTR_ERR(new_table);
		goto out_free;
	}
	return new_table;

out_free:
	xt_free_table_info(newinfo);
out:
	return ERR_PTR(ret);
}

void arpt_unregister_table(struct xt_table *table)
{
	struct xt_table_info *private;
	void *loc_cpu_entry;
	struct module *table_owner = table->me;

	private = xt_unregister_table(table);

	/* Decrease module usage counts and free resources */
	loc_cpu_entry = private->entries[raw_smp_processor_id()];
	ARPT_ENTRY_ITERATE(loc_cpu_entry, private->size,
			   cleanup_entry, NULL);
	if (private->number > private->initial_entries)
		module_put(table_owner);
	xt_free_table_info(private);
}

/* The built-in targets: standard (NULL) and error. */
static struct xt_target arpt_standard_target __read_mostly = {
	.name		= ARPT_STANDARD_TARGET,
	.targetsize	= sizeof(int),
	.family		= NFPROTO_ARP,
#ifdef CONFIG_COMPAT
	.compatsize	= sizeof(compat_int_t),
	.compat_from_user = compat_standard_from_user,
	.compat_to_user	= compat_standard_to_user,
#endif
};

static struct xt_target arpt_error_target __read_mostly = {
	.name		= ARPT_ERROR_TARGET,
	.target		= arpt_error,
	.targetsize	= ARPT_FUNCTION_MAXNAMELEN,
	.family		= NFPROTO_ARP,
};

static struct nf_sockopt_ops arpt_sockopts = {
	.pf		= PF_INET,
	.set_optmin	= ARPT_BASE_CTL,
	.set_optmax	= ARPT_SO_SET_MAX+1,
	.set		= do_arpt_set_ctl,
#ifdef CONFIG_COMPAT
	.compat_set	= compat_do_arpt_set_ctl,
#endif
	.get_optmin	= ARPT_BASE_CTL,
	.get_optmax	= ARPT_SO_GET_MAX+1,
	.get		= do_arpt_get_ctl,
#ifdef CONFIG_COMPAT
	.compat_get	= compat_do_arpt_get_ctl,
#endif
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

	/* Noone else will be downing sem now, so we won't sleep */
	ret = xt_register_target(&arpt_standard_target);
	if (ret < 0)
		goto err2;
	ret = xt_register_target(&arpt_error_target);
	if (ret < 0)
		goto err3;

	/* Register setsockopt */
	ret = nf_register_sockopt(&arpt_sockopts);
	if (ret < 0)
		goto err4;

	printk(KERN_INFO "arp_tables: (C) 2002 David S. Miller\n");
	return 0;

err4:
	xt_unregister_target(&arpt_error_target);
err3:
	xt_unregister_target(&arpt_standard_target);
err2:
	unregister_pernet_subsys(&arp_tables_net_ops);
err1:
	return ret;
}

static void __exit arp_tables_fini(void)
{
	nf_unregister_sockopt(&arpt_sockopts);
	xt_unregister_target(&arpt_error_target);
	xt_unregister_target(&arpt_standard_target);
	unregister_pernet_subsys(&arp_tables_net_ops);
}

EXPORT_SYMBOL(arpt_register_table);
EXPORT_SYMBOL(arpt_unregister_table);
EXPORT_SYMBOL(arpt_do_table);

module_init(arp_tables_init);
module_exit(arp_tables_fini);
