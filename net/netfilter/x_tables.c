/*
 * x_tables core - Backend for {ip,ip6,arp}_tables
 *
 * Copyright (C) 2006-2006 Harald Welte <laforge@netfilter.org>
 * Copyright (C) 2006-2012 Patrick McHardy <kaber@trash.net>
 *
 * Based on existing ip_tables code which is
 *   Copyright (C) 1999 Paul `Rusty' Russell & Michael J. Neuling
 *   Copyright (C) 2000-2005 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/socket.h>
#include <linux/net.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/mutex.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/audit.h>
#include <linux/user_namespace.h>
#include <net/net_namespace.h>

#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_arp.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv6/ip6_tables.h>
#include <linux/netfilter_arp/arp_tables.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Harald Welte <laforge@netfilter.org>");
MODULE_DESCRIPTION("{ip,ip6,arp,eb}_tables backend module");

#define SMP_ALIGN(x) (((x) + SMP_CACHE_BYTES-1) & ~(SMP_CACHE_BYTES-1))

struct compat_delta {
	unsigned int offset; /* offset in kernel */
	int delta; /* delta in 32bit user land */
};

struct xt_af {
	struct mutex mutex;
	struct list_head match;
	struct list_head target;
#ifdef CONFIG_COMPAT
	struct mutex compat_mutex;
	struct compat_delta *compat_tab;
	unsigned int number; /* number of slots in compat_tab[] */
	unsigned int cur; /* number of used slots in compat_tab[] */
#endif
};

static struct xt_af *xt;

static const char *const xt_prefix[NFPROTO_NUMPROTO] = {
	[NFPROTO_UNSPEC] = "x",
	[NFPROTO_IPV4]   = "ip",
	[NFPROTO_ARP]    = "arp",
	[NFPROTO_BRIDGE] = "eb",
	[NFPROTO_IPV6]   = "ip6",
};

/* Registration hooks for targets. */
int xt_register_target(struct xt_target *target)
{
	u_int8_t af = target->family;

	mutex_lock(&xt[af].mutex);
	list_add(&target->list, &xt[af].target);
	mutex_unlock(&xt[af].mutex);
	return 0;
}
EXPORT_SYMBOL(xt_register_target);

void
xt_unregister_target(struct xt_target *target)
{
	u_int8_t af = target->family;

	mutex_lock(&xt[af].mutex);
	list_del(&target->list);
	mutex_unlock(&xt[af].mutex);
}
EXPORT_SYMBOL(xt_unregister_target);

int
xt_register_targets(struct xt_target *target, unsigned int n)
{
	unsigned int i;
	int err = 0;

	for (i = 0; i < n; i++) {
		err = xt_register_target(&target[i]);
		if (err)
			goto err;
	}
	return err;

err:
	if (i > 0)
		xt_unregister_targets(target, i);
	return err;
}
EXPORT_SYMBOL(xt_register_targets);

void
xt_unregister_targets(struct xt_target *target, unsigned int n)
{
	while (n-- > 0)
		xt_unregister_target(&target[n]);
}
EXPORT_SYMBOL(xt_unregister_targets);

int xt_register_match(struct xt_match *match)
{
	u_int8_t af = match->family;

	mutex_lock(&xt[af].mutex);
	list_add(&match->list, &xt[af].match);
	mutex_unlock(&xt[af].mutex);
	return 0;
}
EXPORT_SYMBOL(xt_register_match);

void
xt_unregister_match(struct xt_match *match)
{
	u_int8_t af = match->family;

	mutex_lock(&xt[af].mutex);
	list_del(&match->list);
	mutex_unlock(&xt[af].mutex);
}
EXPORT_SYMBOL(xt_unregister_match);

int
xt_register_matches(struct xt_match *match, unsigned int n)
{
	unsigned int i;
	int err = 0;

	for (i = 0; i < n; i++) {
		err = xt_register_match(&match[i]);
		if (err)
			goto err;
	}
	return err;

err:
	if (i > 0)
		xt_unregister_matches(match, i);
	return err;
}
EXPORT_SYMBOL(xt_register_matches);

void
xt_unregister_matches(struct xt_match *match, unsigned int n)
{
	while (n-- > 0)
		xt_unregister_match(&match[n]);
}
EXPORT_SYMBOL(xt_unregister_matches);


/*
 * These are weird, but module loading must not be done with mutex
 * held (since they will register), and we have to have a single
 * function to use.
 */

/* Find match, grabs ref.  Returns ERR_PTR() on error. */
struct xt_match *xt_find_match(u8 af, const char *name, u8 revision)
{
	struct xt_match *m;
	int err = -ENOENT;

	mutex_lock(&xt[af].mutex);
	list_for_each_entry(m, &xt[af].match, list) {
		if (strcmp(m->name, name) == 0) {
			if (m->revision == revision) {
				if (try_module_get(m->me)) {
					mutex_unlock(&xt[af].mutex);
					return m;
				}
			} else
				err = -EPROTOTYPE; /* Found something. */
		}
	}
	mutex_unlock(&xt[af].mutex);

	if (af != NFPROTO_UNSPEC)
		/* Try searching again in the family-independent list */
		return xt_find_match(NFPROTO_UNSPEC, name, revision);

	return ERR_PTR(err);
}
EXPORT_SYMBOL(xt_find_match);

struct xt_match *
xt_request_find_match(uint8_t nfproto, const char *name, uint8_t revision)
{
	struct xt_match *match;

	match = xt_find_match(nfproto, name, revision);
	if (IS_ERR(match)) {
		request_module("%st_%s", xt_prefix[nfproto], name);
		match = xt_find_match(nfproto, name, revision);
	}

	return match;
}
EXPORT_SYMBOL_GPL(xt_request_find_match);

/* Find target, grabs ref.  Returns ERR_PTR() on error. */
struct xt_target *xt_find_target(u8 af, const char *name, u8 revision)
{
	struct xt_target *t;
	int err = -ENOENT;

	mutex_lock(&xt[af].mutex);
	list_for_each_entry(t, &xt[af].target, list) {
		if (strcmp(t->name, name) == 0) {
			if (t->revision == revision) {
				if (try_module_get(t->me)) {
					mutex_unlock(&xt[af].mutex);
					return t;
				}
			} else
				err = -EPROTOTYPE; /* Found something. */
		}
	}
	mutex_unlock(&xt[af].mutex);

	if (af != NFPROTO_UNSPEC)
		/* Try searching again in the family-independent list */
		return xt_find_target(NFPROTO_UNSPEC, name, revision);

	return ERR_PTR(err);
}
EXPORT_SYMBOL(xt_find_target);

struct xt_target *xt_request_find_target(u8 af, const char *name, u8 revision)
{
	struct xt_target *target;

	target = xt_find_target(af, name, revision);
	if (IS_ERR(target)) {
		request_module("%st_%s", xt_prefix[af], name);
		target = xt_find_target(af, name, revision);
	}

	return target;
}
EXPORT_SYMBOL_GPL(xt_request_find_target);

static int match_revfn(u8 af, const char *name, u8 revision, int *bestp)
{
	const struct xt_match *m;
	int have_rev = 0;

	list_for_each_entry(m, &xt[af].match, list) {
		if (strcmp(m->name, name) == 0) {
			if (m->revision > *bestp)
				*bestp = m->revision;
			if (m->revision == revision)
				have_rev = 1;
		}
	}

	if (af != NFPROTO_UNSPEC && !have_rev)
		return match_revfn(NFPROTO_UNSPEC, name, revision, bestp);

	return have_rev;
}

static int target_revfn(u8 af, const char *name, u8 revision, int *bestp)
{
	const struct xt_target *t;
	int have_rev = 0;

	list_for_each_entry(t, &xt[af].target, list) {
		if (strcmp(t->name, name) == 0) {
			if (t->revision > *bestp)
				*bestp = t->revision;
			if (t->revision == revision)
				have_rev = 1;
		}
	}

	if (af != NFPROTO_UNSPEC && !have_rev)
		return target_revfn(NFPROTO_UNSPEC, name, revision, bestp);

	return have_rev;
}

/* Returns true or false (if no such extension at all) */
int xt_find_revision(u8 af, const char *name, u8 revision, int target,
		     int *err)
{
	int have_rev, best = -1;

	mutex_lock(&xt[af].mutex);
	if (target == 1)
		have_rev = target_revfn(af, name, revision, &best);
	else
		have_rev = match_revfn(af, name, revision, &best);
	mutex_unlock(&xt[af].mutex);

	/* Nothing at all?  Return 0 to try loading module. */
	if (best == -1) {
		*err = -ENOENT;
		return 0;
	}

	*err = best;
	if (!have_rev)
		*err = -EPROTONOSUPPORT;
	return 1;
}
EXPORT_SYMBOL_GPL(xt_find_revision);

static char *
textify_hooks(char *buf, size_t size, unsigned int mask, uint8_t nfproto)
{
	static const char *const inetbr_names[] = {
		"PREROUTING", "INPUT", "FORWARD",
		"OUTPUT", "POSTROUTING", "BROUTING",
	};
	static const char *const arp_names[] = {
		"INPUT", "FORWARD", "OUTPUT",
	};
	const char *const *names;
	unsigned int i, max;
	char *p = buf;
	bool np = false;
	int res;

	names = (nfproto == NFPROTO_ARP) ? arp_names : inetbr_names;
	max   = (nfproto == NFPROTO_ARP) ? ARRAY_SIZE(arp_names) :
	                                   ARRAY_SIZE(inetbr_names);
	*p = '\0';
	for (i = 0; i < max; ++i) {
		if (!(mask & (1 << i)))
			continue;
		res = snprintf(p, size, "%s%s", np ? "/" : "", names[i]);
		if (res > 0) {
			size -= res;
			p += res;
		}
		np = true;
	}

	return buf;
}

int xt_check_match(struct xt_mtchk_param *par,
		   unsigned int size, u_int8_t proto, bool inv_proto)
{
	int ret;

	if (XT_ALIGN(par->match->matchsize) != size &&
	    par->match->matchsize != -1) {
		/*
		 * ebt_among is exempt from centralized matchsize checking
		 * because it uses a dynamic-size data set.
		 */
		pr_err("%s_tables: %s.%u match: invalid size "
		       "%u (kernel) != (user) %u\n",
		       xt_prefix[par->family], par->match->name,
		       par->match->revision,
		       XT_ALIGN(par->match->matchsize), size);
		return -EINVAL;
	}
	if (par->match->table != NULL &&
	    strcmp(par->match->table, par->table) != 0) {
		pr_err("%s_tables: %s match: only valid in %s table, not %s\n",
		       xt_prefix[par->family], par->match->name,
		       par->match->table, par->table);
		return -EINVAL;
	}
	if (par->match->hooks && (par->hook_mask & ~par->match->hooks) != 0) {
		char used[64], allow[64];

		pr_err("%s_tables: %s match: used from hooks %s, but only "
		       "valid from %s\n",
		       xt_prefix[par->family], par->match->name,
		       textify_hooks(used, sizeof(used), par->hook_mask,
		                     par->family),
		       textify_hooks(allow, sizeof(allow), par->match->hooks,
		                     par->family));
		return -EINVAL;
	}
	if (par->match->proto && (par->match->proto != proto || inv_proto)) {
		pr_err("%s_tables: %s match: only valid for protocol %u\n",
		       xt_prefix[par->family], par->match->name,
		       par->match->proto);
		return -EINVAL;
	}
	if (par->match->checkentry != NULL) {
		ret = par->match->checkentry(par);
		if (ret < 0)
			return ret;
		else if (ret > 0)
			/* Flag up potential errors. */
			return -EIO;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(xt_check_match);

/** xt_check_entry_match - check that matches end before start of target
 *
 * @match: beginning of xt_entry_match
 * @target: beginning of this rules target (alleged end of matches)
 * @alignment: alignment requirement of match structures
 *
 * Validates that all matches add up to the beginning of the target,
 * and that each match covers at least the base structure size.
 *
 * Return: 0 on success, negative errno on failure.
 */
static int xt_check_entry_match(const char *match, const char *target,
				const size_t alignment)
{
	const struct xt_entry_match *pos;
	int length = target - match;

	if (length == 0) /* no matches */
		return 0;

	pos = (struct xt_entry_match *)match;
	do {
		if ((unsigned long)pos % alignment)
			return -EINVAL;

		if (length < (int)sizeof(struct xt_entry_match))
			return -EINVAL;

		if (pos->u.match_size < sizeof(struct xt_entry_match))
			return -EINVAL;

		if (pos->u.match_size > length)
			return -EINVAL;

		length -= pos->u.match_size;
		pos = ((void *)((char *)(pos) + (pos)->u.match_size));
	} while (length > 0);

	return 0;
}

#ifdef CONFIG_COMPAT
int xt_compat_add_offset(u_int8_t af, unsigned int offset, int delta)
{
	struct xt_af *xp = &xt[af];

	if (!xp->compat_tab) {
		if (!xp->number)
			return -EINVAL;
		xp->compat_tab = vmalloc(sizeof(struct compat_delta) * xp->number);
		if (!xp->compat_tab)
			return -ENOMEM;
		xp->cur = 0;
	}

	if (xp->cur >= xp->number)
		return -EINVAL;

	if (xp->cur)
		delta += xp->compat_tab[xp->cur - 1].delta;
	xp->compat_tab[xp->cur].offset = offset;
	xp->compat_tab[xp->cur].delta = delta;
	xp->cur++;
	return 0;
}
EXPORT_SYMBOL_GPL(xt_compat_add_offset);

void xt_compat_flush_offsets(u_int8_t af)
{
	if (xt[af].compat_tab) {
		vfree(xt[af].compat_tab);
		xt[af].compat_tab = NULL;
		xt[af].number = 0;
		xt[af].cur = 0;
	}
}
EXPORT_SYMBOL_GPL(xt_compat_flush_offsets);

int xt_compat_calc_jump(u_int8_t af, unsigned int offset)
{
	struct compat_delta *tmp = xt[af].compat_tab;
	int mid, left = 0, right = xt[af].cur - 1;

	while (left <= right) {
		mid = (left + right) >> 1;
		if (offset > tmp[mid].offset)
			left = mid + 1;
		else if (offset < tmp[mid].offset)
			right = mid - 1;
		else
			return mid ? tmp[mid - 1].delta : 0;
	}
	return left ? tmp[left - 1].delta : 0;
}
EXPORT_SYMBOL_GPL(xt_compat_calc_jump);

void xt_compat_init_offsets(u_int8_t af, unsigned int number)
{
	xt[af].number = number;
	xt[af].cur = 0;
}
EXPORT_SYMBOL(xt_compat_init_offsets);

int xt_compat_match_offset(const struct xt_match *match)
{
	u_int16_t csize = match->compatsize ? : match->matchsize;
	return XT_ALIGN(match->matchsize) - COMPAT_XT_ALIGN(csize);
}
EXPORT_SYMBOL_GPL(xt_compat_match_offset);

void xt_compat_match_from_user(struct xt_entry_match *m, void **dstptr,
			       unsigned int *size)
{
	const struct xt_match *match = m->u.kernel.match;
	struct compat_xt_entry_match *cm = (struct compat_xt_entry_match *)m;
	int pad, off = xt_compat_match_offset(match);
	u_int16_t msize = cm->u.user.match_size;
	char name[sizeof(m->u.user.name)];

	m = *dstptr;
	memcpy(m, cm, sizeof(*cm));
	if (match->compat_from_user)
		match->compat_from_user(m->data, cm->data);
	else
		memcpy(m->data, cm->data, msize - sizeof(*cm));
	pad = XT_ALIGN(match->matchsize) - match->matchsize;
	if (pad > 0)
		memset(m->data + match->matchsize, 0, pad);

	msize += off;
	m->u.user.match_size = msize;
	strlcpy(name, match->name, sizeof(name));
	module_put(match->me);
	strncpy(m->u.user.name, name, sizeof(m->u.user.name));

	*size += off;
	*dstptr += msize;
}
EXPORT_SYMBOL_GPL(xt_compat_match_from_user);

int xt_compat_match_to_user(const struct xt_entry_match *m,
			    void __user **dstptr, unsigned int *size)
{
	const struct xt_match *match = m->u.kernel.match;
	struct compat_xt_entry_match __user *cm = *dstptr;
	int off = xt_compat_match_offset(match);
	u_int16_t msize = m->u.user.match_size - off;

	if (copy_to_user(cm, m, sizeof(*cm)) ||
	    put_user(msize, &cm->u.user.match_size) ||
	    copy_to_user(cm->u.user.name, m->u.kernel.match->name,
			 strlen(m->u.kernel.match->name) + 1))
		return -EFAULT;

	if (match->compat_to_user) {
		if (match->compat_to_user((void __user *)cm->data, m->data))
			return -EFAULT;
	} else {
		if (copy_to_user(cm->data, m->data, msize - sizeof(*cm)))
			return -EFAULT;
	}

	*size -= off;
	*dstptr += msize;
	return 0;
}
EXPORT_SYMBOL_GPL(xt_compat_match_to_user);

/* non-compat version may have padding after verdict */
struct compat_xt_standard_target {
	struct compat_xt_entry_target t;
	compat_uint_t verdict;
};

int xt_compat_check_entry_offsets(const void *base, const char *elems,
				  unsigned int target_offset,
				  unsigned int next_offset)
{
	long size_of_base_struct = elems - (const char *)base;
	const struct compat_xt_entry_target *t;
	const char *e = base;

	if (target_offset < size_of_base_struct)
		return -EINVAL;

	if (target_offset + sizeof(*t) > next_offset)
		return -EINVAL;

	t = (void *)(e + target_offset);
	if (t->u.target_size < sizeof(*t))
		return -EINVAL;

	if (target_offset + t->u.target_size > next_offset)
		return -EINVAL;

	if (strcmp(t->u.user.name, XT_STANDARD_TARGET) == 0 &&
	    COMPAT_XT_ALIGN(target_offset + sizeof(struct compat_xt_standard_target)) != next_offset)
		return -EINVAL;

	/* compat_xt_entry match has less strict aligment requirements,
	 * otherwise they are identical.  In case of padding differences
	 * we need to add compat version of xt_check_entry_match.
	 */
	BUILD_BUG_ON(sizeof(struct compat_xt_entry_match) != sizeof(struct xt_entry_match));

	return xt_check_entry_match(elems, base + target_offset,
				    __alignof__(struct compat_xt_entry_match));
}
EXPORT_SYMBOL(xt_compat_check_entry_offsets);
#endif /* CONFIG_COMPAT */

/**
 * xt_check_entry_offsets - validate arp/ip/ip6t_entry
 *
 * @base: pointer to arp/ip/ip6t_entry
 * @elems: pointer to first xt_entry_match, i.e. ip(6)t_entry->elems
 * @target_offset: the arp/ip/ip6_t->target_offset
 * @next_offset: the arp/ip/ip6_t->next_offset
 *
 * validates that target_offset and next_offset are sane and that all
 * match sizes (if any) align with the target offset.
 *
 * This function does not validate the targets or matches themselves, it
 * only tests that all the offsets and sizes are correct, that all
 * match structures are aligned, and that the last structure ends where
 * the target structure begins.
 *
 * Also see xt_compat_check_entry_offsets for CONFIG_COMPAT version.
 *
 * The arp/ip/ip6t_entry structure @base must have passed following tests:
 * - it must point to a valid memory location
 * - base to base + next_offset must be accessible, i.e. not exceed allocated
 *   length.
 *
 * A well-formed entry looks like this:
 *
 * ip(6)t_entry   match [mtdata]  match [mtdata] target [tgdata] ip(6)t_entry
 * e->elems[]-----'                              |               |
 *                matchsize                      |               |
 *                                matchsize      |               |
 *                                               |               |
 * target_offset---------------------------------'               |
 * next_offset---------------------------------------------------'
 *
 * elems[]: flexible array member at end of ip(6)/arpt_entry struct.
 *          This is where matches (if any) and the target reside.
 * target_offset: beginning of target.
 * next_offset: start of the next rule; also: size of this rule.
 * Since targets have a minimum size, target_offset + minlen <= next_offset.
 *
 * Every match stores its size, sum of sizes must not exceed target_offset.
 *
 * Return: 0 on success, negative errno on failure.
 */
int xt_check_entry_offsets(const void *base,
			   const char *elems,
			   unsigned int target_offset,
			   unsigned int next_offset)
{
	long size_of_base_struct = elems - (const char *)base;
	const struct xt_entry_target *t;
	const char *e = base;

	/* target start is within the ip/ip6/arpt_entry struct */
	if (target_offset < size_of_base_struct)
		return -EINVAL;

	if (target_offset + sizeof(*t) > next_offset)
		return -EINVAL;

	t = (void *)(e + target_offset);
	if (t->u.target_size < sizeof(*t))
		return -EINVAL;

	if (target_offset + t->u.target_size > next_offset)
		return -EINVAL;

	if (strcmp(t->u.user.name, XT_STANDARD_TARGET) == 0 &&
	    XT_ALIGN(target_offset + sizeof(struct xt_standard_target)) != next_offset)
		return -EINVAL;

	return xt_check_entry_match(elems, base + target_offset,
				    __alignof__(struct xt_entry_match));
}
EXPORT_SYMBOL(xt_check_entry_offsets);

/**
 * xt_alloc_entry_offsets - allocate array to store rule head offsets
 *
 * @size: number of entries
 *
 * Return: NULL or kmalloc'd or vmalloc'd array
 */
unsigned int *xt_alloc_entry_offsets(unsigned int size)
{
	unsigned int *off;

	off = kcalloc(size, sizeof(unsigned int), GFP_KERNEL | __GFP_NOWARN);

	if (off)
		return off;

	if (size < (SIZE_MAX / sizeof(unsigned int)))
		off = vmalloc(size * sizeof(unsigned int));

	return off;
}
EXPORT_SYMBOL(xt_alloc_entry_offsets);

/**
 * xt_find_jump_offset - check if target is a valid jump offset
 *
 * @offsets: array containing all valid rule start offsets of a rule blob
 * @target: the jump target to search for
 * @size: entries in @offset
 */
bool xt_find_jump_offset(const unsigned int *offsets,
			 unsigned int target, unsigned int size)
{
	int m, low = 0, hi = size;

	while (hi > low) {
		m = (low + hi) / 2u;

		if (offsets[m] > target)
			hi = m;
		else if (offsets[m] < target)
			low = m + 1;
		else
			return true;
	}

	return false;
}
EXPORT_SYMBOL(xt_find_jump_offset);

int xt_check_target(struct xt_tgchk_param *par,
		    unsigned int size, u_int8_t proto, bool inv_proto)
{
	int ret;

	if (XT_ALIGN(par->target->targetsize) != size) {
		pr_err("%s_tables: %s.%u target: invalid size "
		       "%u (kernel) != (user) %u\n",
		       xt_prefix[par->family], par->target->name,
		       par->target->revision,
		       XT_ALIGN(par->target->targetsize), size);
		return -EINVAL;
	}
	if (par->target->table != NULL &&
	    strcmp(par->target->table, par->table) != 0) {
		pr_err("%s_tables: %s target: only valid in %s table, not %s\n",
		       xt_prefix[par->family], par->target->name,
		       par->target->table, par->table);
		return -EINVAL;
	}
	if (par->target->hooks && (par->hook_mask & ~par->target->hooks) != 0) {
		char used[64], allow[64];

		pr_err("%s_tables: %s target: used from hooks %s, but only "
		       "usable from %s\n",
		       xt_prefix[par->family], par->target->name,
		       textify_hooks(used, sizeof(used), par->hook_mask,
		                     par->family),
		       textify_hooks(allow, sizeof(allow), par->target->hooks,
		                     par->family));
		return -EINVAL;
	}
	if (par->target->proto && (par->target->proto != proto || inv_proto)) {
		pr_err("%s_tables: %s target: only valid for protocol %u\n",
		       xt_prefix[par->family], par->target->name,
		       par->target->proto);
		return -EINVAL;
	}
	if (par->target->checkentry != NULL) {
		ret = par->target->checkentry(par);
		if (ret < 0)
			return ret;
		else if (ret > 0)
			/* Flag up potential errors. */
			return -EIO;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(xt_check_target);

/**
 * xt_copy_counters_from_user - copy counters and metadata from userspace
 *
 * @user: src pointer to userspace memory
 * @len: alleged size of userspace memory
 * @info: where to store the xt_counters_info metadata
 * @compat: true if we setsockopt call is done by 32bit task on 64bit kernel
 *
 * Copies counter meta data from @user and stores it in @info.
 *
 * vmallocs memory to hold the counters, then copies the counter data
 * from @user to the new memory and returns a pointer to it.
 *
 * If @compat is true, @info gets converted automatically to the 64bit
 * representation.
 *
 * The metadata associated with the counters is stored in @info.
 *
 * Return: returns pointer that caller has to test via IS_ERR().
 * If IS_ERR is false, caller has to vfree the pointer.
 */
void *xt_copy_counters_from_user(const void __user *user, unsigned int len,
				 struct xt_counters_info *info, bool compat)
{
	void *mem;
	u64 size;

#ifdef CONFIG_COMPAT
	if (compat) {
		/* structures only differ in size due to alignment */
		struct compat_xt_counters_info compat_tmp;

		if (len <= sizeof(compat_tmp))
			return ERR_PTR(-EINVAL);

		len -= sizeof(compat_tmp);
		if (copy_from_user(&compat_tmp, user, sizeof(compat_tmp)) != 0)
			return ERR_PTR(-EFAULT);

		strlcpy(info->name, compat_tmp.name, sizeof(info->name));
		info->num_counters = compat_tmp.num_counters;
		user += sizeof(compat_tmp);
	} else
#endif
	{
		if (len <= sizeof(*info))
			return ERR_PTR(-EINVAL);

		len -= sizeof(*info);
		if (copy_from_user(info, user, sizeof(*info)) != 0)
			return ERR_PTR(-EFAULT);

		info->name[sizeof(info->name) - 1] = '\0';
		user += sizeof(*info);
	}

	size = sizeof(struct xt_counters);
	size *= info->num_counters;

	if (size != (u64)len)
		return ERR_PTR(-EINVAL);

	mem = vmalloc(len);
	if (!mem)
		return ERR_PTR(-ENOMEM);

	if (copy_from_user(mem, user, len) == 0)
		return mem;

	vfree(mem);
	return ERR_PTR(-EFAULT);
}
EXPORT_SYMBOL_GPL(xt_copy_counters_from_user);

#ifdef CONFIG_COMPAT
int xt_compat_target_offset(const struct xt_target *target)
{
	u_int16_t csize = target->compatsize ? : target->targetsize;
	return XT_ALIGN(target->targetsize) - COMPAT_XT_ALIGN(csize);
}
EXPORT_SYMBOL_GPL(xt_compat_target_offset);

void xt_compat_target_from_user(struct xt_entry_target *t, void **dstptr,
				unsigned int *size)
{
	const struct xt_target *target = t->u.kernel.target;
	struct compat_xt_entry_target *ct = (struct compat_xt_entry_target *)t;
	int pad, off = xt_compat_target_offset(target);
	u_int16_t tsize = ct->u.user.target_size;
	char name[sizeof(t->u.user.name)];

	t = *dstptr;
	memcpy(t, ct, sizeof(*ct));
	if (target->compat_from_user)
		target->compat_from_user(t->data, ct->data);
	else
		memcpy(t->data, ct->data, tsize - sizeof(*ct));
	pad = XT_ALIGN(target->targetsize) - target->targetsize;
	if (pad > 0)
		memset(t->data + target->targetsize, 0, pad);

	tsize += off;
	t->u.user.target_size = tsize;
	strlcpy(name, target->name, sizeof(name));
	module_put(target->me);
	strncpy(t->u.user.name, name, sizeof(t->u.user.name));

	*size += off;
	*dstptr += tsize;
}
EXPORT_SYMBOL_GPL(xt_compat_target_from_user);

int xt_compat_target_to_user(const struct xt_entry_target *t,
			     void __user **dstptr, unsigned int *size)
{
	const struct xt_target *target = t->u.kernel.target;
	struct compat_xt_entry_target __user *ct = *dstptr;
	int off = xt_compat_target_offset(target);
	u_int16_t tsize = t->u.user.target_size - off;

	if (copy_to_user(ct, t, sizeof(*ct)) ||
	    put_user(tsize, &ct->u.user.target_size) ||
	    copy_to_user(ct->u.user.name, t->u.kernel.target->name,
			 strlen(t->u.kernel.target->name) + 1))
		return -EFAULT;

	if (target->compat_to_user) {
		if (target->compat_to_user((void __user *)ct->data, t->data))
			return -EFAULT;
	} else {
		if (copy_to_user(ct->data, t->data, tsize - sizeof(*ct)))
			return -EFAULT;
	}

	*size -= off;
	*dstptr += tsize;
	return 0;
}
EXPORT_SYMBOL_GPL(xt_compat_target_to_user);
#endif

struct xt_table_info *xt_alloc_table_info(unsigned int size)
{
	struct xt_table_info *info = NULL;
	size_t sz = sizeof(*info) + size;

	if (sz < sizeof(*info))
		return NULL;

	/* Pedantry: prevent them from hitting BUG() in vmalloc.c --RR */
	if ((SMP_ALIGN(size) >> PAGE_SHIFT) + 2 > totalram_pages)
		return NULL;

	if (sz <= (PAGE_SIZE << PAGE_ALLOC_COSTLY_ORDER))
		info = kmalloc(sz, GFP_KERNEL | __GFP_NOWARN | __GFP_NORETRY);
	if (!info) {
		info = vmalloc(sz);
		if (!info)
			return NULL;
	}
	memset(info, 0, sizeof(*info));
	info->size = size;
	return info;
}
EXPORT_SYMBOL(xt_alloc_table_info);

void xt_free_table_info(struct xt_table_info *info)
{
	int cpu;

	if (info->jumpstack != NULL) {
		for_each_possible_cpu(cpu)
			kvfree(info->jumpstack[cpu]);
		kvfree(info->jumpstack);
	}

	kvfree(info);
}
EXPORT_SYMBOL(xt_free_table_info);

/* Find table by name, grabs mutex & ref.  Returns ERR_PTR() on error. */
struct xt_table *xt_find_table_lock(struct net *net, u_int8_t af,
				    const char *name)
{
	struct xt_table *t, *found = NULL;

	mutex_lock(&xt[af].mutex);
	list_for_each_entry(t, &net->xt.tables[af], list)
		if (strcmp(t->name, name) == 0 && try_module_get(t->me))
			return t;

	if (net == &init_net)
		goto out;

	/* Table doesn't exist in this netns, re-try init */
	list_for_each_entry(t, &init_net.xt.tables[af], list) {
		if (strcmp(t->name, name))
			continue;
		if (!try_module_get(t->me))
			return NULL;

		mutex_unlock(&xt[af].mutex);
		if (t->table_init(net) != 0) {
			module_put(t->me);
			return NULL;
		}

		found = t;

		mutex_lock(&xt[af].mutex);
		break;
	}

	if (!found)
		goto out;

	/* and once again: */
	list_for_each_entry(t, &net->xt.tables[af], list)
		if (strcmp(t->name, name) == 0)
			return t;

	module_put(found->me);
 out:
	mutex_unlock(&xt[af].mutex);
	return NULL;
}
EXPORT_SYMBOL_GPL(xt_find_table_lock);

void xt_table_unlock(struct xt_table *table)
{
	mutex_unlock(&xt[table->af].mutex);
}
EXPORT_SYMBOL_GPL(xt_table_unlock);

#ifdef CONFIG_COMPAT
void xt_compat_lock(u_int8_t af)
{
	mutex_lock(&xt[af].compat_mutex);
}
EXPORT_SYMBOL_GPL(xt_compat_lock);

void xt_compat_unlock(u_int8_t af)
{
	mutex_unlock(&xt[af].compat_mutex);
}
EXPORT_SYMBOL_GPL(xt_compat_unlock);
#endif

DEFINE_PER_CPU(seqcount_t, xt_recseq);
EXPORT_PER_CPU_SYMBOL_GPL(xt_recseq);

struct static_key xt_tee_enabled __read_mostly;
EXPORT_SYMBOL_GPL(xt_tee_enabled);

static int xt_jumpstack_alloc(struct xt_table_info *i)
{
	unsigned int size;
	int cpu;

	size = sizeof(void **) * nr_cpu_ids;
	if (size > PAGE_SIZE)
		i->jumpstack = vzalloc(size);
	else
		i->jumpstack = kzalloc(size, GFP_KERNEL);
	if (i->jumpstack == NULL)
		return -ENOMEM;

	/* ruleset without jumps -- no stack needed */
	if (i->stacksize == 0)
		return 0;

	/* Jumpstack needs to be able to record two full callchains, one
	 * from the first rule set traversal, plus one table reentrancy
	 * via -j TEE without clobbering the callchain that brought us to
	 * TEE target.
	 *
	 * This is done by allocating two jumpstacks per cpu, on reentry
	 * the upper half of the stack is used.
	 *
	 * see the jumpstack setup in ipt_do_table() for more details.
	 */
	size = sizeof(void *) * i->stacksize * 2u;
	for_each_possible_cpu(cpu) {
		if (size > PAGE_SIZE)
			i->jumpstack[cpu] = vmalloc_node(size,
				cpu_to_node(cpu));
		else
			i->jumpstack[cpu] = kmalloc_node(size,
				GFP_KERNEL, cpu_to_node(cpu));
		if (i->jumpstack[cpu] == NULL)
			/*
			 * Freeing will be done later on by the callers. The
			 * chain is: xt_replace_table -> __do_replace ->
			 * do_replace -> xt_free_table_info.
			 */
			return -ENOMEM;
	}

	return 0;
}

struct xt_table_info *
xt_replace_table(struct xt_table *table,
	      unsigned int num_counters,
	      struct xt_table_info *newinfo,
	      int *error)
{
	struct xt_table_info *private;
	int ret;

	ret = xt_jumpstack_alloc(newinfo);
	if (ret < 0) {
		*error = ret;
		return NULL;
	}

	/* Do the substitution. */
	local_bh_disable();
	private = table->private;

	/* Check inside lock: is the old number correct? */
	if (num_counters != private->number) {
		pr_debug("num_counters != table->private->number (%u/%u)\n",
			 num_counters, private->number);
		local_bh_enable();
		*error = -EAGAIN;
		return NULL;
	}

	newinfo->initial_entries = private->initial_entries;
	/*
	 * Ensure contents of newinfo are visible before assigning to
	 * private.
	 */
	smp_wmb();
	table->private = newinfo;

	/*
	 * Even though table entries have now been swapped, other CPU's
	 * may still be using the old entries. This is okay, because
	 * resynchronization happens because of the locking done
	 * during the get_counters() routine.
	 */
	local_bh_enable();

#ifdef CONFIG_AUDIT
	if (audit_enabled) {
		struct audit_buffer *ab;

		ab = audit_log_start(current->audit_context, GFP_KERNEL,
				     AUDIT_NETFILTER_CFG);
		if (ab) {
			audit_log_format(ab, "table=%s family=%u entries=%u",
					 table->name, table->af,
					 private->number);
			audit_log_end(ab);
		}
	}
#endif

	return private;
}
EXPORT_SYMBOL_GPL(xt_replace_table);

struct xt_table *xt_register_table(struct net *net,
				   const struct xt_table *input_table,
				   struct xt_table_info *bootstrap,
				   struct xt_table_info *newinfo)
{
	int ret;
	struct xt_table_info *private;
	struct xt_table *t, *table;

	/* Don't add one object to multiple lists. */
	table = kmemdup(input_table, sizeof(struct xt_table), GFP_KERNEL);
	if (!table) {
		ret = -ENOMEM;
		goto out;
	}

	mutex_lock(&xt[table->af].mutex);
	/* Don't autoload: we'd eat our tail... */
	list_for_each_entry(t, &net->xt.tables[table->af], list) {
		if (strcmp(t->name, table->name) == 0) {
			ret = -EEXIST;
			goto unlock;
		}
	}

	/* Simplifies replace_table code. */
	table->private = bootstrap;

	if (!xt_replace_table(table, 0, newinfo, &ret))
		goto unlock;

	private = table->private;
	pr_debug("table->private->number = %u\n", private->number);

	/* save number of initial entries */
	private->initial_entries = private->number;

	list_add(&table->list, &net->xt.tables[table->af]);
	mutex_unlock(&xt[table->af].mutex);
	return table;

unlock:
	mutex_unlock(&xt[table->af].mutex);
	kfree(table);
out:
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(xt_register_table);

void *xt_unregister_table(struct xt_table *table)
{
	struct xt_table_info *private;

	mutex_lock(&xt[table->af].mutex);
	private = table->private;
	list_del(&table->list);
	mutex_unlock(&xt[table->af].mutex);
	kfree(table);

	return private;
}
EXPORT_SYMBOL_GPL(xt_unregister_table);

#ifdef CONFIG_PROC_FS
struct xt_names_priv {
	struct seq_net_private p;
	u_int8_t af;
};
static void *xt_table_seq_start(struct seq_file *seq, loff_t *pos)
{
	struct xt_names_priv *priv = seq->private;
	struct net *net = seq_file_net(seq);
	u_int8_t af = priv->af;

	mutex_lock(&xt[af].mutex);
	return seq_list_start(&net->xt.tables[af], *pos);
}

static void *xt_table_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct xt_names_priv *priv = seq->private;
	struct net *net = seq_file_net(seq);
	u_int8_t af = priv->af;

	return seq_list_next(v, &net->xt.tables[af], pos);
}

static void xt_table_seq_stop(struct seq_file *seq, void *v)
{
	struct xt_names_priv *priv = seq->private;
	u_int8_t af = priv->af;

	mutex_unlock(&xt[af].mutex);
}

static int xt_table_seq_show(struct seq_file *seq, void *v)
{
	struct xt_table *table = list_entry(v, struct xt_table, list);

	if (*table->name)
		seq_printf(seq, "%s\n", table->name);
	return 0;
}

static const struct seq_operations xt_table_seq_ops = {
	.start	= xt_table_seq_start,
	.next	= xt_table_seq_next,
	.stop	= xt_table_seq_stop,
	.show	= xt_table_seq_show,
};

static int xt_table_open(struct inode *inode, struct file *file)
{
	int ret;
	struct xt_names_priv *priv;

	ret = seq_open_net(inode, file, &xt_table_seq_ops,
			   sizeof(struct xt_names_priv));
	if (!ret) {
		priv = ((struct seq_file *)file->private_data)->private;
		priv->af = (unsigned long)PDE_DATA(inode);
	}
	return ret;
}

static const struct file_operations xt_table_ops = {
	.owner	 = THIS_MODULE,
	.open	 = xt_table_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = seq_release_net,
};

/*
 * Traverse state for ip{,6}_{tables,matches} for helping crossing
 * the multi-AF mutexes.
 */
struct nf_mttg_trav {
	struct list_head *head, *curr;
	uint8_t class, nfproto;
};

enum {
	MTTG_TRAV_INIT,
	MTTG_TRAV_NFP_UNSPEC,
	MTTG_TRAV_NFP_SPEC,
	MTTG_TRAV_DONE,
};

static void *xt_mttg_seq_next(struct seq_file *seq, void *v, loff_t *ppos,
    bool is_target)
{
	static const uint8_t next_class[] = {
		[MTTG_TRAV_NFP_UNSPEC] = MTTG_TRAV_NFP_SPEC,
		[MTTG_TRAV_NFP_SPEC]   = MTTG_TRAV_DONE,
	};
	struct nf_mttg_trav *trav = seq->private;

	switch (trav->class) {
	case MTTG_TRAV_INIT:
		trav->class = MTTG_TRAV_NFP_UNSPEC;
		mutex_lock(&xt[NFPROTO_UNSPEC].mutex);
		trav->head = trav->curr = is_target ?
			&xt[NFPROTO_UNSPEC].target : &xt[NFPROTO_UNSPEC].match;
 		break;
	case MTTG_TRAV_NFP_UNSPEC:
		trav->curr = trav->curr->next;
		if (trav->curr != trav->head)
			break;
		mutex_unlock(&xt[NFPROTO_UNSPEC].mutex);
		mutex_lock(&xt[trav->nfproto].mutex);
		trav->head = trav->curr = is_target ?
			&xt[trav->nfproto].target : &xt[trav->nfproto].match;
		trav->class = next_class[trav->class];
		break;
	case MTTG_TRAV_NFP_SPEC:
		trav->curr = trav->curr->next;
		if (trav->curr != trav->head)
			break;
		/* fallthru, _stop will unlock */
	default:
		return NULL;
	}

	if (ppos != NULL)
		++*ppos;
	return trav;
}

static void *xt_mttg_seq_start(struct seq_file *seq, loff_t *pos,
    bool is_target)
{
	struct nf_mttg_trav *trav = seq->private;
	unsigned int j;

	trav->class = MTTG_TRAV_INIT;
	for (j = 0; j < *pos; ++j)
		if (xt_mttg_seq_next(seq, NULL, NULL, is_target) == NULL)
			return NULL;
	return trav;
}

static void xt_mttg_seq_stop(struct seq_file *seq, void *v)
{
	struct nf_mttg_trav *trav = seq->private;

	switch (trav->class) {
	case MTTG_TRAV_NFP_UNSPEC:
		mutex_unlock(&xt[NFPROTO_UNSPEC].mutex);
		break;
	case MTTG_TRAV_NFP_SPEC:
		mutex_unlock(&xt[trav->nfproto].mutex);
		break;
	}
}

static void *xt_match_seq_start(struct seq_file *seq, loff_t *pos)
{
	return xt_mttg_seq_start(seq, pos, false);
}

static void *xt_match_seq_next(struct seq_file *seq, void *v, loff_t *ppos)
{
	return xt_mttg_seq_next(seq, v, ppos, false);
}

static int xt_match_seq_show(struct seq_file *seq, void *v)
{
	const struct nf_mttg_trav *trav = seq->private;
	const struct xt_match *match;

	switch (trav->class) {
	case MTTG_TRAV_NFP_UNSPEC:
	case MTTG_TRAV_NFP_SPEC:
		if (trav->curr == trav->head)
			return 0;
		match = list_entry(trav->curr, struct xt_match, list);
		if (*match->name)
			seq_printf(seq, "%s\n", match->name);
	}
	return 0;
}

static const struct seq_operations xt_match_seq_ops = {
	.start	= xt_match_seq_start,
	.next	= xt_match_seq_next,
	.stop	= xt_mttg_seq_stop,
	.show	= xt_match_seq_show,
};

static int xt_match_open(struct inode *inode, struct file *file)
{
	struct nf_mttg_trav *trav;
	trav = __seq_open_private(file, &xt_match_seq_ops, sizeof(*trav));
	if (!trav)
		return -ENOMEM;

	trav->nfproto = (unsigned long)PDE_DATA(inode);
	return 0;
}

static const struct file_operations xt_match_ops = {
	.owner	 = THIS_MODULE,
	.open	 = xt_match_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = seq_release_private,
};

static void *xt_target_seq_start(struct seq_file *seq, loff_t *pos)
{
	return xt_mttg_seq_start(seq, pos, true);
}

static void *xt_target_seq_next(struct seq_file *seq, void *v, loff_t *ppos)
{
	return xt_mttg_seq_next(seq, v, ppos, true);
}

static int xt_target_seq_show(struct seq_file *seq, void *v)
{
	const struct nf_mttg_trav *trav = seq->private;
	const struct xt_target *target;

	switch (trav->class) {
	case MTTG_TRAV_NFP_UNSPEC:
	case MTTG_TRAV_NFP_SPEC:
		if (trav->curr == trav->head)
			return 0;
		target = list_entry(trav->curr, struct xt_target, list);
		if (*target->name)
			seq_printf(seq, "%s\n", target->name);
	}
	return 0;
}

static const struct seq_operations xt_target_seq_ops = {
	.start	= xt_target_seq_start,
	.next	= xt_target_seq_next,
	.stop	= xt_mttg_seq_stop,
	.show	= xt_target_seq_show,
};

static int xt_target_open(struct inode *inode, struct file *file)
{
	struct nf_mttg_trav *trav;
	trav = __seq_open_private(file, &xt_target_seq_ops, sizeof(*trav));
	if (!trav)
		return -ENOMEM;

	trav->nfproto = (unsigned long)PDE_DATA(inode);
	return 0;
}

static const struct file_operations xt_target_ops = {
	.owner	 = THIS_MODULE,
	.open	 = xt_target_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = seq_release_private,
};

#define FORMAT_TABLES	"_tables_names"
#define	FORMAT_MATCHES	"_tables_matches"
#define FORMAT_TARGETS 	"_tables_targets"

#endif /* CONFIG_PROC_FS */

/**
 * xt_hook_ops_alloc - set up hooks for a new table
 * @table:	table with metadata needed to set up hooks
 * @fn:		Hook function
 *
 * This function will create the nf_hook_ops that the x_table needs
 * to hand to xt_hook_link_net().
 */
struct nf_hook_ops *
xt_hook_ops_alloc(const struct xt_table *table, nf_hookfn *fn)
{
	unsigned int hook_mask = table->valid_hooks;
	uint8_t i, num_hooks = hweight32(hook_mask);
	uint8_t hooknum;
	struct nf_hook_ops *ops;

	if (!num_hooks)
		return ERR_PTR(-EINVAL);

	ops = kcalloc(num_hooks, sizeof(*ops), GFP_KERNEL);
	if (ops == NULL)
		return ERR_PTR(-ENOMEM);

	for (i = 0, hooknum = 0; i < num_hooks && hook_mask != 0;
	     hook_mask >>= 1, ++hooknum) {
		if (!(hook_mask & 1))
			continue;
		ops[i].hook     = fn;
		ops[i].pf       = table->af;
		ops[i].hooknum  = hooknum;
		ops[i].priority = table->priority;
		++i;
	}

	return ops;
}
EXPORT_SYMBOL_GPL(xt_hook_ops_alloc);

int xt_proto_init(struct net *net, u_int8_t af)
{
#ifdef CONFIG_PROC_FS
	char buf[XT_FUNCTION_MAXNAMELEN];
	struct proc_dir_entry *proc;
	kuid_t root_uid;
	kgid_t root_gid;
#endif

	if (af >= ARRAY_SIZE(xt_prefix))
		return -EINVAL;


#ifdef CONFIG_PROC_FS
	root_uid = make_kuid(net->user_ns, 0);
	root_gid = make_kgid(net->user_ns, 0);

	strlcpy(buf, xt_prefix[af], sizeof(buf));
	strlcat(buf, FORMAT_TABLES, sizeof(buf));
	proc = proc_create_data(buf, 0440, net->proc_net, &xt_table_ops,
				(void *)(unsigned long)af);
	if (!proc)
		goto out;
	if (uid_valid(root_uid) && gid_valid(root_gid))
		proc_set_user(proc, root_uid, root_gid);

	strlcpy(buf, xt_prefix[af], sizeof(buf));
	strlcat(buf, FORMAT_MATCHES, sizeof(buf));
	proc = proc_create_data(buf, 0440, net->proc_net, &xt_match_ops,
				(void *)(unsigned long)af);
	if (!proc)
		goto out_remove_tables;
	if (uid_valid(root_uid) && gid_valid(root_gid))
		proc_set_user(proc, root_uid, root_gid);

	strlcpy(buf, xt_prefix[af], sizeof(buf));
	strlcat(buf, FORMAT_TARGETS, sizeof(buf));
	proc = proc_create_data(buf, 0440, net->proc_net, &xt_target_ops,
				(void *)(unsigned long)af);
	if (!proc)
		goto out_remove_matches;
	if (uid_valid(root_uid) && gid_valid(root_gid))
		proc_set_user(proc, root_uid, root_gid);
#endif

	return 0;

#ifdef CONFIG_PROC_FS
out_remove_matches:
	strlcpy(buf, xt_prefix[af], sizeof(buf));
	strlcat(buf, FORMAT_MATCHES, sizeof(buf));
	remove_proc_entry(buf, net->proc_net);

out_remove_tables:
	strlcpy(buf, xt_prefix[af], sizeof(buf));
	strlcat(buf, FORMAT_TABLES, sizeof(buf));
	remove_proc_entry(buf, net->proc_net);
out:
	return -1;
#endif
}
EXPORT_SYMBOL_GPL(xt_proto_init);

void xt_proto_fini(struct net *net, u_int8_t af)
{
#ifdef CONFIG_PROC_FS
	char buf[XT_FUNCTION_MAXNAMELEN];

	strlcpy(buf, xt_prefix[af], sizeof(buf));
	strlcat(buf, FORMAT_TABLES, sizeof(buf));
	remove_proc_entry(buf, net->proc_net);

	strlcpy(buf, xt_prefix[af], sizeof(buf));
	strlcat(buf, FORMAT_TARGETS, sizeof(buf));
	remove_proc_entry(buf, net->proc_net);

	strlcpy(buf, xt_prefix[af], sizeof(buf));
	strlcat(buf, FORMAT_MATCHES, sizeof(buf));
	remove_proc_entry(buf, net->proc_net);
#endif /*CONFIG_PROC_FS*/
}
EXPORT_SYMBOL_GPL(xt_proto_fini);

static int __net_init xt_net_init(struct net *net)
{
	int i;

	for (i = 0; i < NFPROTO_NUMPROTO; i++)
		INIT_LIST_HEAD(&net->xt.tables[i]);
	return 0;
}

static struct pernet_operations xt_net_ops = {
	.init = xt_net_init,
};

static int __init xt_init(void)
{
	unsigned int i;
	int rv;

	for_each_possible_cpu(i) {
		seqcount_init(&per_cpu(xt_recseq, i));
	}

	xt = kmalloc(sizeof(struct xt_af) * NFPROTO_NUMPROTO, GFP_KERNEL);
	if (!xt)
		return -ENOMEM;

	for (i = 0; i < NFPROTO_NUMPROTO; i++) {
		mutex_init(&xt[i].mutex);
#ifdef CONFIG_COMPAT
		mutex_init(&xt[i].compat_mutex);
		xt[i].compat_tab = NULL;
#endif
		INIT_LIST_HEAD(&xt[i].target);
		INIT_LIST_HEAD(&xt[i].match);
	}
	rv = register_pernet_subsys(&xt_net_ops);
	if (rv < 0)
		kfree(xt);
	return rv;
}

static void __exit xt_fini(void)
{
	unregister_pernet_subsys(&xt_net_ops);
	kfree(xt);
}

module_init(xt_init);
module_exit(xt_fini);

