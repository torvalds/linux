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

#include <asm/uaccess.h>
#include <linux/mutex.h>

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
		       __FUNCTION__, __FILE__, __LINE__);	\
} while(0)
#else
#define ARP_NF_ASSERT(x)
#endif

static inline int arp_devaddr_compare(const struct arpt_devaddr_info *ap,
				      char *hdr_addr, int len)
{
	int i, ret;

	if (len > ARPT_DEV_ADDR_LEN_MAX)
		len = ARPT_DEV_ADDR_LEN_MAX;

	ret = 0;
	for (i = 0; i < len; i++)
		ret |= (hdr_addr[i] ^ ap->addr[i]) & ap->mask[i];

	return (ret != 0);
}

/* Returns whether packet matches rule or not. */
static inline int arp_packet_match(const struct arphdr *arphdr,
				   struct net_device *dev,
				   const char *indev,
				   const char *outdev,
				   const struct arpt_arp *arpinfo)
{
	char *arpptr = (char *)(arphdr + 1);
	char *src_devaddr, *tgt_devaddr;
	__be32 src_ipaddr, tgt_ipaddr;
	int i, ret;

#define FWINV(bool,invflg) ((bool) ^ !!(arpinfo->invflags & invflg))

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

		dprintf("SRC: %u.%u.%u.%u. Mask: %u.%u.%u.%u. Target: %u.%u.%u.%u.%s\n",
			NIPQUAD(src_ipaddr),
			NIPQUAD(arpinfo->smsk.s_addr),
			NIPQUAD(arpinfo->src.s_addr),
			arpinfo->invflags & ARPT_INV_SRCIP ? " (INV)" : "");
		dprintf("TGT: %u.%u.%u.%u Mask: %u.%u.%u.%u Target: %u.%u.%u.%u.%s\n",
			NIPQUAD(tgt_ipaddr),
			NIPQUAD(arpinfo->tmsk.s_addr),
			NIPQUAD(arpinfo->tgt.s_addr),
			arpinfo->invflags & ARPT_INV_TGTIP ? " (INV)" : "");
		return 0;
	}

	/* Look for ifname matches.  */
	for (i = 0, ret = 0; i < IFNAMSIZ; i++) {
		ret |= (indev[i] ^ arpinfo->iniface[i])
			& arpinfo->iniface_mask[i];
	}

	if (FWINV(ret != 0, ARPT_INV_VIA_IN)) {
		dprintf("VIA in mismatch (%s vs %s).%s\n",
			indev, arpinfo->iniface,
			arpinfo->invflags&ARPT_INV_VIA_IN ?" (INV)":"");
		return 0;
	}

	for (i = 0, ret = 0; i < IFNAMSIZ; i++) {
		ret |= (outdev[i] ^ arpinfo->outiface[i])
			& arpinfo->outiface_mask[i];
	}

	if (FWINV(ret != 0, ARPT_INV_VIA_OUT)) {
		dprintf("VIA out mismatch (%s vs %s).%s\n",
			outdev, arpinfo->outiface,
			arpinfo->invflags&ARPT_INV_VIA_OUT ?" (INV)":"");
		return 0;
	}

	return 1;
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

static unsigned int arpt_error(struct sk_buff *skb,
			       const struct net_device *in,
			       const struct net_device *out,
			       unsigned int hooknum,
			       const struct xt_target *target,
			       const void *targinfo)
{
	if (net_ratelimit())
		printk("arp_tables: error: '%s'\n", (char *)targinfo);

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
			   struct arpt_table *table)
{
	static const char nulldevname[IFNAMSIZ];
	unsigned int verdict = NF_DROP;
	struct arphdr *arp;
	bool hotdrop = false;
	struct arpt_entry *e, *back;
	const char *indev, *outdev;
	void *table_base;
	struct xt_table_info *private;

	/* ARP header, plus 2 device addresses, plus 2 IP addresses.  */
	if (!pskb_may_pull(skb, (sizeof(struct arphdr) +
				 (2 * skb->dev->addr_len) +
				 (2 * sizeof(u32)))))
		return NF_DROP;

	indev = in ? in->name : nulldevname;
	outdev = out ? out->name : nulldevname;

	read_lock_bh(&table->lock);
	private = table->private;
	table_base = (void *)private->entries[smp_processor_id()];
	e = get_entry(table_base, private->hook_entry[hook]);
	back = get_entry(table_base, private->underflow[hook]);

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
				verdict = t->u.kernel.target->target(skb,
								     in, out,
								     hook,
								     t->u.kernel.target,
								     t->data);

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
	read_unlock_bh(&table->lock);

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
			struct arpt_standard_target *t
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

				if (t->verdict < -NF_MAX_VERDICT - 1) {
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

static inline int standard_check(const struct arpt_entry_target *t,
				 unsigned int max_offset)
{
	/* Check standard info. */
	if (t->u.target_size
	    != ARPT_ALIGN(sizeof(struct arpt_standard_target))) {
		duprintf("arpt_standard_check: target size %u != %Zu\n",
			 t->u.target_size,
			 ARPT_ALIGN(sizeof(struct arpt_standard_target)));
		return 0;
	}

	return 1;
}

static struct arpt_target arpt_standard_target;

static inline int check_entry(struct arpt_entry *e, const char *name, unsigned int size,
			      unsigned int *i)
{
	struct arpt_entry_target *t;
	struct arpt_target *target;
	int ret;

	if (!arp_checkentry(&e->arp)) {
		duprintf("arp_tables: arp check failed %p %s.\n", e, name);
		return -EINVAL;
	}

	if (e->target_offset + sizeof(struct arpt_entry_target) > e->next_offset)
		return -EINVAL;

	t = arpt_get_target(e);
	if (e->target_offset + t->u.target_size > e->next_offset)
		return -EINVAL;

	target = try_then_request_module(xt_find_target(NF_ARP, t->u.user.name,
							t->u.user.revision),
					 "arpt_%s", t->u.user.name);
	if (IS_ERR(target) || !target) {
		duprintf("check_entry: `%s' not found\n", t->u.user.name);
		ret = target ? PTR_ERR(target) : -ENOENT;
		goto out;
	}
	t->u.kernel.target = target;

	ret = xt_check_target(target, NF_ARP, t->u.target_size - sizeof(*t),
			      name, e->comefrom, 0, 0);
	if (ret)
		goto err;

	if (t->u.kernel.target == &arpt_standard_target) {
		if (!standard_check(t, size)) {
			ret = -EINVAL;
			goto err;
		}
	} else if (t->u.kernel.target->checkentry
		   && !t->u.kernel.target->checkentry(name, e, target, t->data,
						      e->comefrom)) {
		duprintf("arp_tables: check failed for `%s'.\n",
			 t->u.kernel.target->name);
		ret = -EINVAL;
		goto err;
	}

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
	struct arpt_entry_target *t;

	if (i && (*i)-- == 0)
		return 1;

	t = arpt_get_target(e);
	if (t->u.kernel.target->destroy)
		t->u.kernel.target->destroy(t->u.kernel.target, t->data);
	module_put(t->u.kernel.target->me);
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
				 check_entry, name, size, &i);

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
	 * We dont care about preemption here.
	 */
	curcpu = raw_smp_processor_id();

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
		ARPT_ENTRY_ITERATE(t->entries[cpu],
				   t->size,
				   add_entry_to_counter,
				   counters,
				   &i);
	}
}

static int copy_entries_to_user(unsigned int total_size,
				struct arpt_table *table,
				void __user *userptr)
{
	unsigned int off, num, countersize;
	struct arpt_entry *e;
	struct xt_counters *counters;
	struct xt_table_info *private = table->private;
	int ret = 0;
	void *loc_cpu_entry;

	/* We need atomic snapshot of counters: rest doesn't change
	 * (other than comefrom, which userspace doesn't care
	 * about).
	 */
	countersize = sizeof(struct xt_counters) * private->number;
	counters = vmalloc_node(countersize, numa_node_id());

	if (counters == NULL)
		return -ENOMEM;

	/* First, sum counters... */
	write_lock_bh(&table->lock);
	get_counters(private, counters);
	write_unlock_bh(&table->lock);

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

static int get_entries(const struct arpt_get_entries *entries,
		       struct arpt_get_entries __user *uptr)
{
	int ret;
	struct arpt_table *t;

	t = xt_find_table_lock(NF_ARP, entries->name);
	if (t && !IS_ERR(t)) {
		struct xt_table_info *private = t->private;
		duprintf("t->private->number = %u\n",
			 private->number);
		if (entries->size == private->size)
			ret = copy_entries_to_user(private->size,
						   t, uptr->entrytable);
		else {
			duprintf("get_entries: I've got %u not %u!\n",
				 private->size, entries->size);
			ret = -EINVAL;
		}
		module_put(t->me);
		xt_table_unlock(t);
	} else
		ret = t ? PTR_ERR(t) : -ENOENT;

	return ret;
}

static int do_replace(void __user *user, unsigned int len)
{
	int ret;
	struct arpt_replace tmp;
	struct arpt_table *t;
	struct xt_table_info *newinfo, *oldinfo;
	struct xt_counters *counters;
	void *loc_cpu_entry, *loc_cpu_old_entry;

	if (copy_from_user(&tmp, user, sizeof(tmp)) != 0)
		return -EFAULT;

	/* Hack: Causes ipchains to give correct error msg --RR */
	if (len != sizeof(tmp) + tmp.size)
		return -ENOPROTOOPT;

	/* overflow check */
	if (tmp.size >= (INT_MAX - sizeof(struct xt_table_info)) / NR_CPUS -
			SMP_CACHE_BYTES)
		return -ENOMEM;
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

	counters = vmalloc(tmp.num_counters * sizeof(struct xt_counters));
	if (!counters) {
		ret = -ENOMEM;
		goto free_newinfo;
	}

	ret = translate_table(tmp.name, tmp.valid_hooks,
			      newinfo, loc_cpu_entry, tmp.size, tmp.num_entries,
			      tmp.hook_entry, tmp.underflow);
	if (ret != 0)
		goto free_newinfo_counters;

	duprintf("arp_tables: Translated table\n");

	t = try_then_request_module(xt_find_table_lock(NF_ARP, tmp.name),
				    "arptable_%s", tmp.name);
	if (!t || IS_ERR(t)) {
		ret = t ? PTR_ERR(t) : -ENOENT;
		goto free_newinfo_counters_untrans;
	}

	/* You lied! */
	if (tmp.valid_hooks != t->valid_hooks) {
		duprintf("Valid hook crap: %08X vs %08X\n",
			 tmp.valid_hooks, t->valid_hooks);
		ret = -EINVAL;
		goto put_module;
	}

	oldinfo = xt_replace_table(t, tmp.num_counters, newinfo, &ret);
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

	/* Get the old counters. */
	get_counters(oldinfo, counters);
	/* Decrease module usage counts and free resource */
	loc_cpu_old_entry = oldinfo->entries[raw_smp_processor_id()];
	ARPT_ENTRY_ITERATE(loc_cpu_old_entry, oldinfo->size, cleanup_entry,NULL);

	xt_free_table_info(oldinfo);
	if (copy_to_user(tmp.counters, counters,
			 sizeof(struct xt_counters) * tmp.num_counters) != 0)
		ret = -EFAULT;
	vfree(counters);
	xt_table_unlock(t);
	return ret;

 put_module:
	module_put(t->me);
	xt_table_unlock(t);
 free_newinfo_counters_untrans:
	ARPT_ENTRY_ITERATE(loc_cpu_entry, newinfo->size, cleanup_entry, NULL);
 free_newinfo_counters:
	vfree(counters);
 free_newinfo:
	xt_free_table_info(newinfo);
	return ret;
}

/* We're lazy, and add to the first CPU; overflow works its fey magic
 * and everything is OK.
 */
static inline int add_counter_to_entry(struct arpt_entry *e,
				       const struct xt_counters addme[],
				       unsigned int *i)
{

	ADD_COUNTER(e->counters, addme[*i].bcnt, addme[*i].pcnt);

	(*i)++;
	return 0;
}

static int do_add_counters(void __user *user, unsigned int len)
{
	unsigned int i;
	struct xt_counters_info tmp, *paddc;
	struct arpt_table *t;
	struct xt_table_info *private;
	int ret = 0;
	void *loc_cpu_entry;

	if (copy_from_user(&tmp, user, sizeof(tmp)) != 0)
		return -EFAULT;

	if (len != sizeof(tmp) + tmp.num_counters*sizeof(struct xt_counters))
		return -EINVAL;

	paddc = vmalloc(len);
	if (!paddc)
		return -ENOMEM;

	if (copy_from_user(paddc, user, len) != 0) {
		ret = -EFAULT;
		goto free;
	}

	t = xt_find_table_lock(NF_ARP, tmp.name);
	if (!t || IS_ERR(t)) {
		ret = t ? PTR_ERR(t) : -ENOENT;
		goto free;
	}

	write_lock_bh(&t->lock);
	private = t->private;
	if (private->number != tmp.num_counters) {
		ret = -EINVAL;
		goto unlock_up_free;
	}

	i = 0;
	/* Choose the copy that is on our node */
	loc_cpu_entry = private->entries[smp_processor_id()];
	ARPT_ENTRY_ITERATE(loc_cpu_entry,
			   private->size,
			   add_counter_to_entry,
			   paddc->counters,
			   &i);
 unlock_up_free:
	write_unlock_bh(&t->lock);
	xt_table_unlock(t);
	module_put(t->me);
 free:
	vfree(paddc);

	return ret;
}

static int do_arpt_set_ctl(struct sock *sk, int cmd, void __user *user, unsigned int len)
{
	int ret;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	switch (cmd) {
	case ARPT_SO_SET_REPLACE:
		ret = do_replace(user, len);
		break;

	case ARPT_SO_SET_ADD_COUNTERS:
		ret = do_add_counters(user, len);
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
	case ARPT_SO_GET_INFO: {
		char name[ARPT_TABLE_MAXNAMELEN];
		struct arpt_table *t;

		if (*len != sizeof(struct arpt_getinfo)) {
			duprintf("length %u != %Zu\n", *len,
				 sizeof(struct arpt_getinfo));
			ret = -EINVAL;
			break;
		}

		if (copy_from_user(name, user, sizeof(name)) != 0) {
			ret = -EFAULT;
			break;
		}
		name[ARPT_TABLE_MAXNAMELEN-1] = '\0';

		t = try_then_request_module(xt_find_table_lock(NF_ARP, name),
					    "arptable_%s", name);
		if (t && !IS_ERR(t)) {
			struct arpt_getinfo info;
			struct xt_table_info *private = t->private;

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
	}
	break;

	case ARPT_SO_GET_ENTRIES: {
		struct arpt_get_entries get;

		if (*len < sizeof(get)) {
			duprintf("get_entries: %u < %Zu\n", *len, sizeof(get));
			ret = -EINVAL;
		} else if (copy_from_user(&get, user, sizeof(get)) != 0) {
			ret = -EFAULT;
		} else if (*len != sizeof(struct arpt_get_entries) + get.size) {
			duprintf("get_entries: %u != %Zu\n", *len,
				 sizeof(struct arpt_get_entries) + get.size);
			ret = -EINVAL;
		} else
			ret = get_entries(&get, user);
		break;
	}

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

		try_then_request_module(xt_find_revision(NF_ARP, rev.name,
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

int arpt_register_table(struct arpt_table *table,
			const struct arpt_replace *repl)
{
	int ret;
	struct xt_table_info *newinfo;
	static struct xt_table_info bootstrap
		= { 0, 0, 0, { 0 }, { 0 }, { } };
	void *loc_cpu_entry;

	newinfo = xt_alloc_table_info(repl->size);
	if (!newinfo) {
		ret = -ENOMEM;
		return ret;
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
	if (ret != 0) {
		xt_free_table_info(newinfo);
		return ret;
	}

	ret = xt_register_table(table, &bootstrap, newinfo);
	if (ret != 0) {
		xt_free_table_info(newinfo);
		return ret;
	}

	return 0;
}

void arpt_unregister_table(struct arpt_table *table)
{
	struct xt_table_info *private;
	void *loc_cpu_entry;

	private = xt_unregister_table(table);

	/* Decrease module usage counts and free resources */
	loc_cpu_entry = private->entries[raw_smp_processor_id()];
	ARPT_ENTRY_ITERATE(loc_cpu_entry, private->size,
			   cleanup_entry, NULL);
	xt_free_table_info(private);
}

/* The built-in targets: standard (NULL) and error. */
static struct arpt_target arpt_standard_target __read_mostly = {
	.name		= ARPT_STANDARD_TARGET,
	.targetsize	= sizeof(int),
	.family		= NF_ARP,
};

static struct arpt_target arpt_error_target __read_mostly = {
	.name		= ARPT_ERROR_TARGET,
	.target		= arpt_error,
	.targetsize	= ARPT_FUNCTION_MAXNAMELEN,
	.family		= NF_ARP,
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

static int __init arp_tables_init(void)
{
	int ret;

	ret = xt_proto_init(NF_ARP);
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
	xt_proto_fini(NF_ARP);
err1:
	return ret;
}

static void __exit arp_tables_fini(void)
{
	nf_unregister_sockopt(&arpt_sockopts);
	xt_unregister_target(&arpt_error_target);
	xt_unregister_target(&arpt_standard_target);
	xt_proto_fini(NF_ARP);
}

EXPORT_SYMBOL(arpt_register_table);
EXPORT_SYMBOL(arpt_unregister_table);
EXPORT_SYMBOL(arpt_do_table);

module_init(arp_tables_init);
module_exit(arp_tables_fini);
