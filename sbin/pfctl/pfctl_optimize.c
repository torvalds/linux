/*	$OpenBSD: pfctl_optimize.c,v 1.17 2008/05/06 03:45:21 mpf Exp $ */

/*
 * Copyright (c) 2004 Mike Frantzen <frantzen@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/pfvar.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pfctl_parser.h"
#include "pfctl.h"

/* The size at which a table becomes faster than individual rules */
#define TABLE_THRESHOLD		6


/* #define OPT_DEBUG	1 */
#ifdef OPT_DEBUG
# define DEBUG(str, v...) \
	printf("%s: " str "\n", __FUNCTION__ , ## v)
#else
# define DEBUG(str, v...) ((void)0)
#endif


/*
 * A container that lets us sort a superblock to optimize the skip step jumps
 */
struct pf_skip_step {
	int				ps_count;	/* number of items */
	TAILQ_HEAD( , pf_opt_rule)	ps_rules;
	TAILQ_ENTRY(pf_skip_step)	ps_entry;
};


/*
 * A superblock is a block of adjacent rules of similar action.  If there
 * are five PASS rules in a row, they all become members of a superblock.
 * Once we have a superblock, we are free to re-order any rules within it
 * in order to improve performance; if a packet is passed, it doesn't matter
 * who passed it.
 */
struct superblock {
	TAILQ_HEAD( , pf_opt_rule)		 sb_rules;
	TAILQ_ENTRY(superblock)			 sb_entry;
	struct superblock			*sb_profiled_block;
	TAILQ_HEAD(skiplist, pf_skip_step)	 sb_skipsteps[PF_SKIP_COUNT];
};
TAILQ_HEAD(superblocks, superblock);


/*
 * Description of the PF rule structure.
 */
enum {
    BARRIER,	/* the presence of the field puts the rule in its own block */
    BREAK,	/* the field may not differ between rules in a superblock */
    NOMERGE,	/* the field may not differ between rules when combined */
    COMBINED,	/* the field may itself be combined with other rules */
    DC,		/* we just don't care about the field */
    NEVER};	/* we should never see this field set?!? */
static struct pf_rule_field {
	const char	*prf_name;
	int		 prf_type;
	size_t		 prf_offset;
	size_t		 prf_size;
} pf_rule_desc[] = {
#define PF_RULE_FIELD(field, ty)	\
    {#field,				\
    ty,					\
    offsetof(struct pf_rule, field),	\
    sizeof(((struct pf_rule *)0)->field)}


    /*
     * The presence of these fields in a rule put the rule in its own
     * superblock.  Thus it will not be optimized.  It also prevents the
     * rule from being re-ordered at all.
     */
    PF_RULE_FIELD(label,		BARRIER),
    PF_RULE_FIELD(prob,			BARRIER),
    PF_RULE_FIELD(max_states,		BARRIER),
    PF_RULE_FIELD(max_src_nodes,	BARRIER),
    PF_RULE_FIELD(max_src_states,	BARRIER),
    PF_RULE_FIELD(max_src_conn,		BARRIER),
    PF_RULE_FIELD(max_src_conn_rate,	BARRIER),
    PF_RULE_FIELD(anchor,		BARRIER),	/* for now */

    /*
     * These fields must be the same between all rules in the same superblock.
     * These rules are allowed to be re-ordered but only among like rules.
     * For instance we can re-order all 'tag "foo"' rules because they have the
     * same tag.  But we can not re-order between a 'tag "foo"' and a
     * 'tag "bar"' since that would change the meaning of the ruleset.
     */
    PF_RULE_FIELD(tagname,		BREAK),
    PF_RULE_FIELD(keep_state,		BREAK),
    PF_RULE_FIELD(qname,		BREAK),
    PF_RULE_FIELD(pqname,		BREAK),
    PF_RULE_FIELD(rt,			BREAK),
    PF_RULE_FIELD(allow_opts,		BREAK),
    PF_RULE_FIELD(rule_flag,		BREAK),
    PF_RULE_FIELD(action,		BREAK),
    PF_RULE_FIELD(log,			BREAK),
    PF_RULE_FIELD(quick,		BREAK),
    PF_RULE_FIELD(return_ttl,		BREAK),
    PF_RULE_FIELD(overload_tblname,	BREAK),
    PF_RULE_FIELD(flush,		BREAK),
    PF_RULE_FIELD(rpool,		BREAK),
    PF_RULE_FIELD(logif,		BREAK),

    /*
     * Any fields not listed in this structure act as BREAK fields
     */


    /*
     * These fields must not differ when we merge two rules together but
     * their difference isn't enough to put the rules in different superblocks.
     * There are no problems re-ordering any rules with these fields.
     */
    PF_RULE_FIELD(af,			NOMERGE),
    PF_RULE_FIELD(ifnot,		NOMERGE),
    PF_RULE_FIELD(ifname,		NOMERGE),	/* hack for IF groups */
    PF_RULE_FIELD(match_tag_not,	NOMERGE),
    PF_RULE_FIELD(match_tagname,	NOMERGE),
    PF_RULE_FIELD(os_fingerprint,	NOMERGE),
    PF_RULE_FIELD(timeout,		NOMERGE),
    PF_RULE_FIELD(return_icmp,		NOMERGE),
    PF_RULE_FIELD(return_icmp6,		NOMERGE),
    PF_RULE_FIELD(uid,			NOMERGE),
    PF_RULE_FIELD(gid,			NOMERGE),
    PF_RULE_FIELD(direction,		NOMERGE),
    PF_RULE_FIELD(proto,		NOMERGE),
    PF_RULE_FIELD(type,			NOMERGE),
    PF_RULE_FIELD(code,			NOMERGE),
    PF_RULE_FIELD(flags,		NOMERGE),
    PF_RULE_FIELD(flagset,		NOMERGE),
    PF_RULE_FIELD(tos,			NOMERGE),
    PF_RULE_FIELD(src.port,		NOMERGE),
    PF_RULE_FIELD(dst.port,		NOMERGE),
    PF_RULE_FIELD(src.port_op,		NOMERGE),
    PF_RULE_FIELD(dst.port_op,		NOMERGE),
    PF_RULE_FIELD(src.neg,		NOMERGE),
    PF_RULE_FIELD(dst.neg,		NOMERGE),

    /* These fields can be merged */
    PF_RULE_FIELD(src.addr,		COMBINED),
    PF_RULE_FIELD(dst.addr,		COMBINED),

    /* We just don't care about these fields.  They're set by the kernel */
    PF_RULE_FIELD(skip,			DC),
    PF_RULE_FIELD(evaluations,		DC),
    PF_RULE_FIELD(packets,		DC),
    PF_RULE_FIELD(bytes,		DC),
    PF_RULE_FIELD(kif,			DC),
    PF_RULE_FIELD(states_cur,		DC),
    PF_RULE_FIELD(states_tot,		DC),
    PF_RULE_FIELD(src_nodes,		DC),
    PF_RULE_FIELD(nr,			DC),
    PF_RULE_FIELD(entries,		DC),
    PF_RULE_FIELD(qid,			DC),
    PF_RULE_FIELD(pqid,			DC),
    PF_RULE_FIELD(anchor_relative,	DC),
    PF_RULE_FIELD(anchor_wildcard,	DC),
    PF_RULE_FIELD(tag,			DC),
    PF_RULE_FIELD(match_tag,		DC),
    PF_RULE_FIELD(overload_tbl,		DC),

    /* These fields should never be set in a PASS/BLOCK rule */
    PF_RULE_FIELD(natpass,		NEVER),
    PF_RULE_FIELD(max_mss,		NEVER),
    PF_RULE_FIELD(min_ttl,		NEVER),
    PF_RULE_FIELD(set_tos,		NEVER),
};



int	add_opt_table(struct pfctl *, struct pf_opt_tbl **, sa_family_t,
	    struct pf_rule_addr *);
int	addrs_combineable(struct pf_rule_addr *, struct pf_rule_addr *);
int	addrs_equal(struct pf_rule_addr *, struct pf_rule_addr *);
int	block_feedback(struct pfctl *, struct superblock *);
int	combine_rules(struct pfctl *, struct superblock *);
void	comparable_rule(struct pf_rule *, const struct pf_rule *, int);
int	construct_superblocks(struct pfctl *, struct pf_opt_queue *,
	    struct superblocks *);
void	exclude_supersets(struct pf_rule *, struct pf_rule *);
int	interface_group(const char *);
int	load_feedback_profile(struct pfctl *, struct superblocks *);
int	optimize_superblock(struct pfctl *, struct superblock *);
int	pf_opt_create_table(struct pfctl *, struct pf_opt_tbl *);
void	remove_from_skipsteps(struct skiplist *, struct superblock *,
	    struct pf_opt_rule *, struct pf_skip_step *);
int	remove_identical_rules(struct pfctl *, struct superblock *);
int	reorder_rules(struct pfctl *, struct superblock *, int);
int	rules_combineable(struct pf_rule *, struct pf_rule *);
void	skip_append(struct superblock *, int, struct pf_skip_step *,
	    struct pf_opt_rule *);
int	skip_compare(int, struct pf_skip_step *, struct pf_opt_rule *);
void	skip_init(void);
int	skip_cmp_af(struct pf_rule *, struct pf_rule *);
int	skip_cmp_dir(struct pf_rule *, struct pf_rule *);
int	skip_cmp_dst_addr(struct pf_rule *, struct pf_rule *);
int	skip_cmp_dst_port(struct pf_rule *, struct pf_rule *);
int	skip_cmp_ifp(struct pf_rule *, struct pf_rule *);
int	skip_cmp_proto(struct pf_rule *, struct pf_rule *);
int	skip_cmp_src_addr(struct pf_rule *, struct pf_rule *);
int	skip_cmp_src_port(struct pf_rule *, struct pf_rule *);
int	superblock_inclusive(struct superblock *, struct pf_opt_rule *);
void	superblock_free(struct pfctl *, struct superblock *);


static int (*skip_comparitors[PF_SKIP_COUNT])(struct pf_rule *,
    struct pf_rule *);
static const char *skip_comparitors_names[PF_SKIP_COUNT];
#define PF_SKIP_COMPARITORS {				\
    { "ifp", PF_SKIP_IFP, skip_cmp_ifp },		\
    { "dir", PF_SKIP_DIR, skip_cmp_dir },		\
    { "af", PF_SKIP_AF, skip_cmp_af },			\
    { "proto", PF_SKIP_PROTO, skip_cmp_proto },		\
    { "saddr", PF_SKIP_SRC_ADDR, skip_cmp_src_addr },	\
    { "sport", PF_SKIP_SRC_PORT, skip_cmp_src_port },	\
    { "daddr", PF_SKIP_DST_ADDR, skip_cmp_dst_addr },	\
    { "dport", PF_SKIP_DST_PORT, skip_cmp_dst_port }	\
}

static struct pfr_buffer table_buffer;
static int table_identifier;


int
pfctl_optimize_ruleset(struct pfctl *pf, struct pf_ruleset *rs)
{
	struct superblocks superblocks;
	struct pf_opt_queue opt_queue;
	struct superblock *block;
	struct pf_opt_rule *por;
	struct pf_rule *r;
	struct pf_rulequeue *old_rules;

	DEBUG("optimizing ruleset");
	memset(&table_buffer, 0, sizeof(table_buffer));
	skip_init();
	TAILQ_INIT(&opt_queue);

	old_rules = rs->rules[PF_RULESET_FILTER].active.ptr;
	rs->rules[PF_RULESET_FILTER].active.ptr =
	    rs->rules[PF_RULESET_FILTER].inactive.ptr;
	rs->rules[PF_RULESET_FILTER].inactive.ptr = old_rules;

	/*
	 * XXX expanding the pf_opt_rule format throughout pfctl might allow
	 * us to avoid all this copying.
	 */
	while ((r = TAILQ_FIRST(rs->rules[PF_RULESET_FILTER].inactive.ptr))
	    != NULL) {
		TAILQ_REMOVE(rs->rules[PF_RULESET_FILTER].inactive.ptr, r,
		    entries);
		if ((por = calloc(1, sizeof(*por))) == NULL)
			err(1, "calloc");
		memcpy(&por->por_rule, r, sizeof(*r));
		if (TAILQ_FIRST(&r->rpool.list) != NULL) {
			TAILQ_INIT(&por->por_rule.rpool.list);
			pfctl_move_pool(&r->rpool, &por->por_rule.rpool);
		} else
			bzero(&por->por_rule.rpool,
			    sizeof(por->por_rule.rpool));


		TAILQ_INSERT_TAIL(&opt_queue, por, por_entry);
	}

	TAILQ_INIT(&superblocks);
	if (construct_superblocks(pf, &opt_queue, &superblocks))
		goto error;

	if (pf->optimize & PF_OPTIMIZE_PROFILE) {
		if (load_feedback_profile(pf, &superblocks))
			goto error;
	}

	TAILQ_FOREACH(block, &superblocks, sb_entry) {
		if (optimize_superblock(pf, block))
			goto error;
	}

	rs->anchor->refcnt = 0;
	while ((block = TAILQ_FIRST(&superblocks))) {
		TAILQ_REMOVE(&superblocks, block, sb_entry);

		while ((por = TAILQ_FIRST(&block->sb_rules))) {
			TAILQ_REMOVE(&block->sb_rules, por, por_entry);
			por->por_rule.nr = rs->anchor->refcnt++;
			if ((r = calloc(1, sizeof(*r))) == NULL)
				err(1, "calloc");
			memcpy(r, &por->por_rule, sizeof(*r));
			TAILQ_INIT(&r->rpool.list);
			pfctl_move_pool(&por->por_rule.rpool, &r->rpool);
			TAILQ_INSERT_TAIL(
			    rs->rules[PF_RULESET_FILTER].active.ptr,
			    r, entries);
			free(por);
		}
		free(block);
	}

	return (0);

error:
	while ((por = TAILQ_FIRST(&opt_queue))) {
		TAILQ_REMOVE(&opt_queue, por, por_entry);
		if (por->por_src_tbl) {
			pfr_buf_clear(por->por_src_tbl->pt_buf);
			free(por->por_src_tbl->pt_buf);
			free(por->por_src_tbl);
		}
		if (por->por_dst_tbl) {
			pfr_buf_clear(por->por_dst_tbl->pt_buf);
			free(por->por_dst_tbl->pt_buf);
			free(por->por_dst_tbl);
		}
		free(por);
	}
	while ((block = TAILQ_FIRST(&superblocks))) {
		TAILQ_REMOVE(&superblocks, block, sb_entry);
		superblock_free(pf, block);
	}
	return (1);
}


/*
 * Go ahead and optimize a superblock
 */
int
optimize_superblock(struct pfctl *pf, struct superblock *block)
{
#ifdef OPT_DEBUG
	struct pf_opt_rule *por;
#endif /* OPT_DEBUG */

	/* We have a few optimization passes:
	 *   1) remove duplicate rules or rules that are a subset of other
	 *      rules
	 *   2) combine otherwise identical rules with different IP addresses
	 *      into a single rule and put the addresses in a table.
	 *   3) re-order the rules to improve kernel skip steps
	 *   4) re-order the 'quick' rules based on feedback from the
	 *      active ruleset statistics
	 *
	 * XXX combine_rules() doesn't combine v4 and v6 rules.  would just
	 *     have to keep af in the table container, make af 'COMBINE' and
	 *     twiddle the af on the merged rule
	 * XXX maybe add a weighting to the metric on skipsteps when doing
	 *     reordering.  sometimes two sequential tables will be better
	 *     that four consecutive interfaces.
	 * XXX need to adjust the skipstep count of everything after PROTO,
	 *     since they aren't actually checked on a proto mismatch in
	 *     pf_test_{tcp, udp, icmp}()
	 * XXX should i treat proto=0, af=0 or dir=0 special in skepstep
	 *     calculation since they are a DC?
	 * XXX keep last skiplist of last superblock to influence this
	 *     superblock.  '5 inet6 log' should make '3 inet6' come before '4
	 *     inet' in the next superblock.
	 * XXX would be useful to add tables for ports
	 * XXX we can also re-order some mutually exclusive superblocks to
	 *     try merging superblocks before any of these optimization passes.
	 *     for instance a single 'log in' rule in the middle of non-logging
	 *     out rules.
	 */

	/* shortcut.  there will be a lot of 1-rule superblocks */
	if (!TAILQ_NEXT(TAILQ_FIRST(&block->sb_rules), por_entry))
		return (0);

#ifdef OPT_DEBUG
	printf("--- Superblock ---\n");
	TAILQ_FOREACH(por, &block->sb_rules, por_entry) {
		printf("  ");
		print_rule(&por->por_rule, por->por_rule.anchor ?
		    por->por_rule.anchor->name : "", 1, 0);
	}
#endif /* OPT_DEBUG */


	if (remove_identical_rules(pf, block))
		return (1);
	if (combine_rules(pf, block))
		return (1);
	if ((pf->optimize & PF_OPTIMIZE_PROFILE) &&
	    TAILQ_FIRST(&block->sb_rules)->por_rule.quick &&
	    block->sb_profiled_block) {
		if (block_feedback(pf, block))
			return (1);
	} else if (reorder_rules(pf, block, 0)) {
		return (1);
	}

	/*
	 * Don't add any optimization passes below reorder_rules().  It will
	 * have divided superblocks into smaller blocks for further refinement
	 * and doesn't put them back together again.  What once was a true
	 * superblock might have been split into multiple superblocks.
	 */

#ifdef OPT_DEBUG
	printf("--- END Superblock ---\n");
#endif /* OPT_DEBUG */
	return (0);
}


/*
 * Optimization pass #1: remove identical rules
 */
int
remove_identical_rules(struct pfctl *pf, struct superblock *block)
{
	struct pf_opt_rule *por1, *por2, *por_next, *por2_next;
	struct pf_rule a, a2, b, b2;

	for (por1 = TAILQ_FIRST(&block->sb_rules); por1; por1 = por_next) {
		por_next = TAILQ_NEXT(por1, por_entry);
		for (por2 = por_next; por2; por2 = por2_next) {
			por2_next = TAILQ_NEXT(por2, por_entry);
			comparable_rule(&a, &por1->por_rule, DC);
			comparable_rule(&b, &por2->por_rule, DC);
			memcpy(&a2, &a, sizeof(a2));
			memcpy(&b2, &b, sizeof(b2));

			exclude_supersets(&a, &b);
			exclude_supersets(&b2, &a2);
			if (memcmp(&a, &b, sizeof(a)) == 0) {
				DEBUG("removing identical rule  nr%d = *nr%d*",
				    por1->por_rule.nr, por2->por_rule.nr);
				TAILQ_REMOVE(&block->sb_rules, por2, por_entry);
				if (por_next == por2)
					por_next = TAILQ_NEXT(por1, por_entry);
				free(por2);
			} else if (memcmp(&a2, &b2, sizeof(a2)) == 0) {
				DEBUG("removing identical rule  *nr%d* = nr%d",
				    por1->por_rule.nr, por2->por_rule.nr);
				TAILQ_REMOVE(&block->sb_rules, por1, por_entry);
				free(por1);
				break;
			}
		}
	}

	return (0);
}


/*
 * Optimization pass #2: combine similar rules with different addresses
 * into a single rule and a table
 */
int
combine_rules(struct pfctl *pf, struct superblock *block)
{
	struct pf_opt_rule *p1, *p2, *por_next;
	int src_eq, dst_eq;

	if ((pf->loadopt & PFCTL_FLAG_TABLE) == 0) {
		warnx("Must enable table loading for optimizations");
		return (1);
	}

	/* First we make a pass to combine the rules.  O(n log n) */
	TAILQ_FOREACH(p1, &block->sb_rules, por_entry) {
		for (p2 = TAILQ_NEXT(p1, por_entry); p2; p2 = por_next) {
			por_next = TAILQ_NEXT(p2, por_entry);

			src_eq = addrs_equal(&p1->por_rule.src,
			    &p2->por_rule.src);
			dst_eq = addrs_equal(&p1->por_rule.dst,
			    &p2->por_rule.dst);

			if (src_eq && !dst_eq && p1->por_src_tbl == NULL &&
			    p2->por_dst_tbl == NULL &&
			    p2->por_src_tbl == NULL &&
			    rules_combineable(&p1->por_rule, &p2->por_rule) &&
			    addrs_combineable(&p1->por_rule.dst,
			    &p2->por_rule.dst)) {
				DEBUG("can combine rules  nr%d = nr%d",
				    p1->por_rule.nr, p2->por_rule.nr);
				if (p1->por_dst_tbl == NULL &&
				    add_opt_table(pf, &p1->por_dst_tbl,
				    p1->por_rule.af, &p1->por_rule.dst))
					return (1);
				if (add_opt_table(pf, &p1->por_dst_tbl,
				    p1->por_rule.af, &p2->por_rule.dst))
					return (1);
				p2->por_dst_tbl = p1->por_dst_tbl;
				if (p1->por_dst_tbl->pt_rulecount >=
				    TABLE_THRESHOLD) {
					TAILQ_REMOVE(&block->sb_rules, p2,
					    por_entry);
					free(p2);
				}
			} else if (!src_eq && dst_eq && p1->por_dst_tbl == NULL
			    && p2->por_src_tbl == NULL &&
			    p2->por_dst_tbl == NULL &&
			    rules_combineable(&p1->por_rule, &p2->por_rule) &&
			    addrs_combineable(&p1->por_rule.src,
			    &p2->por_rule.src)) {
				DEBUG("can combine rules  nr%d = nr%d",
				    p1->por_rule.nr, p2->por_rule.nr);
				if (p1->por_src_tbl == NULL &&
				    add_opt_table(pf, &p1->por_src_tbl,
				    p1->por_rule.af, &p1->por_rule.src))
					return (1);
				if (add_opt_table(pf, &p1->por_src_tbl,
				    p1->por_rule.af, &p2->por_rule.src))
					return (1);
				p2->por_src_tbl = p1->por_src_tbl;
				if (p1->por_src_tbl->pt_rulecount >=
				    TABLE_THRESHOLD) {
					TAILQ_REMOVE(&block->sb_rules, p2,
					    por_entry);
					free(p2);
				}
			}
		}
	}


	/*
	 * Then we make a final pass to create a valid table name and
	 * insert the name into the rules.
	 */
	for (p1 = TAILQ_FIRST(&block->sb_rules); p1; p1 = por_next) {
		por_next = TAILQ_NEXT(p1, por_entry);
		assert(p1->por_src_tbl == NULL || p1->por_dst_tbl == NULL);

		if (p1->por_src_tbl && p1->por_src_tbl->pt_rulecount >=
		    TABLE_THRESHOLD) {
			if (p1->por_src_tbl->pt_generated) {
				/* This rule is included in a table */
				TAILQ_REMOVE(&block->sb_rules, p1, por_entry);
				free(p1);
				continue;
			}
			p1->por_src_tbl->pt_generated = 1;

			if ((pf->opts & PF_OPT_NOACTION) == 0 &&
			    pf_opt_create_table(pf, p1->por_src_tbl))
				return (1);

			pf->tdirty = 1;

			if (pf->opts & PF_OPT_VERBOSE)
				print_tabledef(p1->por_src_tbl->pt_name,
				    PFR_TFLAG_CONST, 1,
				    &p1->por_src_tbl->pt_nodes);

			memset(&p1->por_rule.src.addr, 0,
			    sizeof(p1->por_rule.src.addr));
			p1->por_rule.src.addr.type = PF_ADDR_TABLE;
			strlcpy(p1->por_rule.src.addr.v.tblname,
			    p1->por_src_tbl->pt_name,
			    sizeof(p1->por_rule.src.addr.v.tblname));

			pfr_buf_clear(p1->por_src_tbl->pt_buf);
			free(p1->por_src_tbl->pt_buf);
			p1->por_src_tbl->pt_buf = NULL;
		}
		if (p1->por_dst_tbl && p1->por_dst_tbl->pt_rulecount >=
		    TABLE_THRESHOLD) {
			if (p1->por_dst_tbl->pt_generated) {
				/* This rule is included in a table */
				TAILQ_REMOVE(&block->sb_rules, p1, por_entry);
				free(p1);
				continue;
			}
			p1->por_dst_tbl->pt_generated = 1;

			if ((pf->opts & PF_OPT_NOACTION) == 0 &&
			    pf_opt_create_table(pf, p1->por_dst_tbl))
				return (1);
			pf->tdirty = 1;

			if (pf->opts & PF_OPT_VERBOSE)
				print_tabledef(p1->por_dst_tbl->pt_name,
				    PFR_TFLAG_CONST, 1,
				    &p1->por_dst_tbl->pt_nodes);

			memset(&p1->por_rule.dst.addr, 0,
			    sizeof(p1->por_rule.dst.addr));
			p1->por_rule.dst.addr.type = PF_ADDR_TABLE;
			strlcpy(p1->por_rule.dst.addr.v.tblname,
			    p1->por_dst_tbl->pt_name,
			    sizeof(p1->por_rule.dst.addr.v.tblname));

			pfr_buf_clear(p1->por_dst_tbl->pt_buf);
			free(p1->por_dst_tbl->pt_buf);
			p1->por_dst_tbl->pt_buf = NULL;
		}
	}

	return (0);
}


/*
 * Optimization pass #3: re-order rules to improve skip steps
 */
int
reorder_rules(struct pfctl *pf, struct superblock *block, int depth)
{
	struct superblock *newblock;
	struct pf_skip_step *skiplist;
	struct pf_opt_rule *por;
	int i, largest, largest_list, rule_count = 0;
	TAILQ_HEAD( , pf_opt_rule) head;

	/*
	 * Calculate the best-case skip steps.  We put each rule in a list
	 * of other rules with common fields
	 */
	for (i = 0; i < PF_SKIP_COUNT; i++) {
		TAILQ_FOREACH(por, &block->sb_rules, por_entry) {
			TAILQ_FOREACH(skiplist, &block->sb_skipsteps[i],
			    ps_entry) {
				if (skip_compare(i, skiplist, por) == 0)
					break;
			}
			if (skiplist == NULL) {
				if ((skiplist = calloc(1, sizeof(*skiplist))) ==
				    NULL)
					err(1, "calloc");
				TAILQ_INIT(&skiplist->ps_rules);
				TAILQ_INSERT_TAIL(&block->sb_skipsteps[i],
				    skiplist, ps_entry);
			}
			skip_append(block, i, skiplist, por);
		}
	}

	TAILQ_FOREACH(por, &block->sb_rules, por_entry)
		rule_count++;

	/*
	 * Now we're going to ignore any fields that are identical between
	 * all of the rules in the superblock and those fields which differ
	 * between every rule in the superblock.
	 */
	largest = 0;
	for (i = 0; i < PF_SKIP_COUNT; i++) {
		skiplist = TAILQ_FIRST(&block->sb_skipsteps[i]);
		if (skiplist->ps_count == rule_count) {
			DEBUG("(%d) original skipstep '%s' is all rules",
			    depth, skip_comparitors_names[i]);
			skiplist->ps_count = 0;
		} else if (skiplist->ps_count == 1) {
			skiplist->ps_count = 0;
		} else {
			DEBUG("(%d) original skipstep '%s' largest jump is %d",
			    depth, skip_comparitors_names[i],
			    skiplist->ps_count);
			if (skiplist->ps_count > largest)
				largest = skiplist->ps_count;
		}
	}
	if (largest == 0) {
		/* Ugh.  There is NO commonality in the superblock on which
		 * optimize the skipsteps optimization.
		 */
		goto done;
	}

	/*
	 * Now we're going to empty the superblock rule list and re-create
	 * it based on a more optimal skipstep order.
	 */
	TAILQ_INIT(&head);
	while ((por = TAILQ_FIRST(&block->sb_rules))) {
		TAILQ_REMOVE(&block->sb_rules, por, por_entry);
		TAILQ_INSERT_TAIL(&head, por, por_entry);
	}


	while (!TAILQ_EMPTY(&head)) {
		largest = 1;

		/*
		 * Find the most useful skip steps remaining
		 */
		for (i = 0; i < PF_SKIP_COUNT; i++) {
			skiplist = TAILQ_FIRST(&block->sb_skipsteps[i]);
			if (skiplist->ps_count > largest) {
				largest = skiplist->ps_count;
				largest_list = i;
			}
		}

		if (largest <= 1) {
			/*
			 * Nothing useful left.  Leave remaining rules in order.
			 */
			DEBUG("(%d) no more commonality for skip steps", depth);
			while ((por = TAILQ_FIRST(&head))) {
				TAILQ_REMOVE(&head, por, por_entry);
				TAILQ_INSERT_TAIL(&block->sb_rules, por,
				    por_entry);
			}
		} else {
			/*
			 * There is commonality.  Extract those common rules
			 * and place them in the ruleset adjacent to each
			 * other.
			 */
			skiplist = TAILQ_FIRST(&block->sb_skipsteps[
			    largest_list]);
			DEBUG("(%d) skipstep '%s' largest jump is %d @ #%d",
			    depth, skip_comparitors_names[largest_list],
			    largest, TAILQ_FIRST(&TAILQ_FIRST(&block->
			    sb_skipsteps [largest_list])->ps_rules)->
			    por_rule.nr);
			TAILQ_REMOVE(&block->sb_skipsteps[largest_list],
			    skiplist, ps_entry);


			/*
			 * There may be further commonality inside these
			 * rules.  So we'll split them off into they're own
			 * superblock and pass it back into the optimizer.
			 */
			if (skiplist->ps_count > 2) {
				if ((newblock = calloc(1, sizeof(*newblock)))
				    == NULL) {
					warn("calloc");
					return (1);
				}
				TAILQ_INIT(&newblock->sb_rules);
				for (i = 0; i < PF_SKIP_COUNT; i++)
					TAILQ_INIT(&newblock->sb_skipsteps[i]);
				TAILQ_INSERT_BEFORE(block, newblock, sb_entry);
				DEBUG("(%d) splitting off %d rules from superblock @ #%d",
				    depth, skiplist->ps_count,
				    TAILQ_FIRST(&skiplist->ps_rules)->
				    por_rule.nr);
			} else {
				newblock = block;
			}

			while ((por = TAILQ_FIRST(&skiplist->ps_rules))) {
				TAILQ_REMOVE(&head, por, por_entry);
				TAILQ_REMOVE(&skiplist->ps_rules, por,
				    por_skip_entry[largest_list]);
				TAILQ_INSERT_TAIL(&newblock->sb_rules, por,
				    por_entry);

				/* Remove this rule from all other skiplists */
				remove_from_skipsteps(&block->sb_skipsteps[
				    largest_list], block, por, skiplist);
			}
			free(skiplist);
			if (newblock != block)
				if (reorder_rules(pf, newblock, depth + 1))
					return (1);
		}
	}

done:
	for (i = 0; i < PF_SKIP_COUNT; i++) {
		while ((skiplist = TAILQ_FIRST(&block->sb_skipsteps[i]))) {
			TAILQ_REMOVE(&block->sb_skipsteps[i], skiplist,
			    ps_entry);
			free(skiplist);
		}
	}

	return (0);
}


/*
 * Optimization pass #4: re-order 'quick' rules based on feedback from the
 * currently running ruleset
 */
int
block_feedback(struct pfctl *pf, struct superblock *block)
{
	TAILQ_HEAD( , pf_opt_rule) queue;
	struct pf_opt_rule *por1, *por2;
	u_int64_t total_count = 0;
	struct pf_rule a, b;


	/*
	 * Walk through all of the profiled superblock's rules and copy
	 * the counters onto our rules.
	 */
	TAILQ_FOREACH(por1, &block->sb_profiled_block->sb_rules, por_entry) {
		comparable_rule(&a, &por1->por_rule, DC);
		total_count += por1->por_rule.packets[0] +
		    por1->por_rule.packets[1];
		TAILQ_FOREACH(por2, &block->sb_rules, por_entry) {
			if (por2->por_profile_count)
				continue;
			comparable_rule(&b, &por2->por_rule, DC);
			if (memcmp(&a, &b, sizeof(a)) == 0) {
				por2->por_profile_count =
				    por1->por_rule.packets[0] +
				    por1->por_rule.packets[1];
				break;
			}
		}
	}
	superblock_free(pf, block->sb_profiled_block);
	block->sb_profiled_block = NULL;

	/*
	 * Now we pull all of the rules off the superblock and re-insert them
	 * in sorted order.
	 */

	TAILQ_INIT(&queue);
	while ((por1 = TAILQ_FIRST(&block->sb_rules)) != NULL) {
		TAILQ_REMOVE(&block->sb_rules, por1, por_entry);
		TAILQ_INSERT_TAIL(&queue, por1, por_entry);
	}

	while ((por1 = TAILQ_FIRST(&queue)) != NULL) {
		TAILQ_REMOVE(&queue, por1, por_entry);
/* XXX I should sort all of the unused rules based on skip steps */
		TAILQ_FOREACH(por2, &block->sb_rules, por_entry) {
			if (por1->por_profile_count > por2->por_profile_count) {
				TAILQ_INSERT_BEFORE(por2, por1, por_entry);
				break;
			}
		}
#ifdef __FreeBSD__
		if (por2 == NULL)
#else
		if (por2 == TAILQ_END(&block->sb_rules))
#endif
			TAILQ_INSERT_TAIL(&block->sb_rules, por1, por_entry);
	}

	return (0);
}


/*
 * Load the current ruleset from the kernel and try to associate them with
 * the ruleset we're optimizing.
 */
int
load_feedback_profile(struct pfctl *pf, struct superblocks *superblocks)
{
	struct superblock *block, *blockcur;
	struct superblocks prof_superblocks;
	struct pf_opt_rule *por;
	struct pf_opt_queue queue;
	struct pfioc_rule pr;
	struct pf_rule a, b;
	int nr, mnr;

	TAILQ_INIT(&queue);
	TAILQ_INIT(&prof_superblocks);

	memset(&pr, 0, sizeof(pr));
	pr.rule.action = PF_PASS;
	if (ioctl(pf->dev, DIOCGETRULES, &pr)) {
		warn("DIOCGETRULES");
		return (1);
	}
	mnr = pr.nr;

	DEBUG("Loading %d active rules for a feedback profile", mnr);
	for (nr = 0; nr < mnr; ++nr) {
		struct pf_ruleset *rs;
		if ((por = calloc(1, sizeof(*por))) == NULL) {
			warn("calloc");
			return (1);
		}
		pr.nr = nr;
		if (ioctl(pf->dev, DIOCGETRULE, &pr)) {
			warn("DIOCGETRULES");
			return (1);
		}
		memcpy(&por->por_rule, &pr.rule, sizeof(por->por_rule));
		rs = pf_find_or_create_ruleset(pr.anchor_call);
		por->por_rule.anchor = rs->anchor;
		if (TAILQ_EMPTY(&por->por_rule.rpool.list))
			memset(&por->por_rule.rpool, 0,
			    sizeof(por->por_rule.rpool));
		TAILQ_INSERT_TAIL(&queue, por, por_entry);

		/* XXX pfctl_get_pool(pf->dev, &pr.rule.rpool, nr, pr.ticket,
		 *         PF_PASS, pf->anchor) ???
		 * ... pfctl_clear_pool(&pr.rule.rpool)
		 */
	}

	if (construct_superblocks(pf, &queue, &prof_superblocks))
		return (1);


	/*
	 * Now we try to associate the active ruleset's superblocks with
	 * the superblocks we're compiling.
	 */
	block = TAILQ_FIRST(superblocks);
	blockcur = TAILQ_FIRST(&prof_superblocks);
	while (block && blockcur) {
		comparable_rule(&a, &TAILQ_FIRST(&block->sb_rules)->por_rule,
		    BREAK);
		comparable_rule(&b, &TAILQ_FIRST(&blockcur->sb_rules)->por_rule,
		    BREAK);
		if (memcmp(&a, &b, sizeof(a)) == 0) {
			/* The two superblocks lined up */
			block->sb_profiled_block = blockcur;
		} else {
			DEBUG("superblocks don't line up between #%d and #%d",
			    TAILQ_FIRST(&block->sb_rules)->por_rule.nr,
			    TAILQ_FIRST(&blockcur->sb_rules)->por_rule.nr);
			break;
		}
		block = TAILQ_NEXT(block, sb_entry);
		blockcur = TAILQ_NEXT(blockcur, sb_entry);
	}



	/* Free any superblocks we couldn't link */
	while (blockcur) {
		block = TAILQ_NEXT(blockcur, sb_entry);
		superblock_free(pf, blockcur);
		blockcur = block;
	}
	return (0);
}


/*
 * Compare a rule to a skiplist to see if the rule is a member
 */
int
skip_compare(int skipnum, struct pf_skip_step *skiplist,
    struct pf_opt_rule *por)
{
	struct pf_rule *a, *b;
	if (skipnum >= PF_SKIP_COUNT || skipnum < 0)
		errx(1, "skip_compare() out of bounds");
	a = &por->por_rule;
	b = &TAILQ_FIRST(&skiplist->ps_rules)->por_rule;

	return ((skip_comparitors[skipnum])(a, b));
}


/*
 * Add a rule to a skiplist
 */
void
skip_append(struct superblock *superblock, int skipnum,
    struct pf_skip_step *skiplist, struct pf_opt_rule *por)
{
	struct pf_skip_step *prev;

	skiplist->ps_count++;
	TAILQ_INSERT_TAIL(&skiplist->ps_rules, por, por_skip_entry[skipnum]);

	/* Keep the list of skiplists sorted by whichever is larger */
	while ((prev = TAILQ_PREV(skiplist, skiplist, ps_entry)) &&
	    prev->ps_count < skiplist->ps_count) {
		TAILQ_REMOVE(&superblock->sb_skipsteps[skipnum],
		    skiplist, ps_entry);
		TAILQ_INSERT_BEFORE(prev, skiplist, ps_entry);
	}
}


/*
 * Remove a rule from the other skiplist calculations.
 */
void
remove_from_skipsteps(struct skiplist *head, struct superblock *block,
    struct pf_opt_rule *por, struct pf_skip_step *active_list)
{
	struct pf_skip_step *sk, *next;
	struct pf_opt_rule *p2;
	int i, found;

	for (i = 0; i < PF_SKIP_COUNT; i++) {
		sk = TAILQ_FIRST(&block->sb_skipsteps[i]);
		if (sk == NULL || sk == active_list || sk->ps_count <= 1)
			continue;
		found = 0;
		do {
			TAILQ_FOREACH(p2, &sk->ps_rules, por_skip_entry[i])
				if (p2 == por) {
					TAILQ_REMOVE(&sk->ps_rules, p2,
					    por_skip_entry[i]);
					found = 1;
					sk->ps_count--;
					break;
				}
		} while (!found && (sk = TAILQ_NEXT(sk, ps_entry)));
		if (found && sk) {
			/* Does this change the sorting order? */
			while ((next = TAILQ_NEXT(sk, ps_entry)) &&
			    next->ps_count > sk->ps_count) {
				TAILQ_REMOVE(head, sk, ps_entry);
				TAILQ_INSERT_AFTER(head, next, sk, ps_entry);
			}
#ifdef OPT_DEBUG
			next = TAILQ_NEXT(sk, ps_entry);
			assert(next == NULL || next->ps_count <= sk->ps_count);
#endif /* OPT_DEBUG */
		}
	}
}


/* Compare two rules AF field for skiplist construction */
int
skip_cmp_af(struct pf_rule *a, struct pf_rule *b)
{
	if (a->af != b->af || a->af == 0)
		return (1);
	return (0);
}

/* Compare two rules DIRECTION field for skiplist construction */
int
skip_cmp_dir(struct pf_rule *a, struct pf_rule *b)
{
	if (a->direction == 0 || a->direction != b->direction)
		return (1);
	return (0);
}

/* Compare two rules DST Address field for skiplist construction */
int
skip_cmp_dst_addr(struct pf_rule *a, struct pf_rule *b)
{
	if (a->dst.neg != b->dst.neg ||
	    a->dst.addr.type != b->dst.addr.type)
		return (1);
	/* XXX if (a->proto != b->proto && a->proto != 0 && b->proto != 0
	 *    && (a->proto == IPPROTO_TCP || a->proto == IPPROTO_UDP ||
	 *    a->proto == IPPROTO_ICMP
	 *	return (1);
	 */
	switch (a->dst.addr.type) {
	case PF_ADDR_ADDRMASK:
		if (memcmp(&a->dst.addr.v.a.addr, &b->dst.addr.v.a.addr,
		    sizeof(a->dst.addr.v.a.addr)) ||
		    memcmp(&a->dst.addr.v.a.mask, &b->dst.addr.v.a.mask,
		    sizeof(a->dst.addr.v.a.mask)) ||
		    (a->dst.addr.v.a.addr.addr32[0] == 0 &&
		    a->dst.addr.v.a.addr.addr32[1] == 0 &&
		    a->dst.addr.v.a.addr.addr32[2] == 0 &&
		    a->dst.addr.v.a.addr.addr32[3] == 0))
			return (1);
		return (0);
	case PF_ADDR_DYNIFTL:
		if (strcmp(a->dst.addr.v.ifname, b->dst.addr.v.ifname) != 0 ||
		    a->dst.addr.iflags != b->dst.addr.iflags ||
		    memcmp(&a->dst.addr.v.a.mask, &b->dst.addr.v.a.mask,
		    sizeof(a->dst.addr.v.a.mask)))
			return (1);
		return (0);
	case PF_ADDR_NOROUTE:
	case PF_ADDR_URPFFAILED:
		return (0);
	case PF_ADDR_TABLE:
		return (strcmp(a->dst.addr.v.tblname, b->dst.addr.v.tblname));
	}
	return (1);
}

/* Compare two rules DST port field for skiplist construction */
int
skip_cmp_dst_port(struct pf_rule *a, struct pf_rule *b)
{
	/* XXX if (a->proto != b->proto && a->proto != 0 && b->proto != 0
	 *    && (a->proto == IPPROTO_TCP || a->proto == IPPROTO_UDP ||
	 *    a->proto == IPPROTO_ICMP
	 *	return (1);
	 */
	if (a->dst.port_op == PF_OP_NONE || a->dst.port_op != b->dst.port_op ||
	    a->dst.port[0] != b->dst.port[0] ||
	    a->dst.port[1] != b->dst.port[1])
		return (1);
	return (0);
}

/* Compare two rules IFP field for skiplist construction */
int
skip_cmp_ifp(struct pf_rule *a, struct pf_rule *b)
{
	if (strcmp(a->ifname, b->ifname) || a->ifname[0] == '\0')
		return (1);
	return (a->ifnot != b->ifnot);
}

/* Compare two rules PROTO field for skiplist construction */
int
skip_cmp_proto(struct pf_rule *a, struct pf_rule *b)
{
	return (a->proto != b->proto || a->proto == 0);
}

/* Compare two rules SRC addr field for skiplist construction */
int
skip_cmp_src_addr(struct pf_rule *a, struct pf_rule *b)
{
	if (a->src.neg != b->src.neg ||
	    a->src.addr.type != b->src.addr.type)
		return (1);
	/* XXX if (a->proto != b->proto && a->proto != 0 && b->proto != 0
	 *    && (a->proto == IPPROTO_TCP || a->proto == IPPROTO_UDP ||
	 *    a->proto == IPPROTO_ICMP
	 *	return (1);
	 */
	switch (a->src.addr.type) {
	case PF_ADDR_ADDRMASK:
		if (memcmp(&a->src.addr.v.a.addr, &b->src.addr.v.a.addr,
		    sizeof(a->src.addr.v.a.addr)) ||
		    memcmp(&a->src.addr.v.a.mask, &b->src.addr.v.a.mask,
		    sizeof(a->src.addr.v.a.mask)) ||
		    (a->src.addr.v.a.addr.addr32[0] == 0 &&
		    a->src.addr.v.a.addr.addr32[1] == 0 &&
		    a->src.addr.v.a.addr.addr32[2] == 0 &&
		    a->src.addr.v.a.addr.addr32[3] == 0))
			return (1);
		return (0);
	case PF_ADDR_DYNIFTL:
		if (strcmp(a->src.addr.v.ifname, b->src.addr.v.ifname) != 0 ||
		    a->src.addr.iflags != b->src.addr.iflags ||
		    memcmp(&a->src.addr.v.a.mask, &b->src.addr.v.a.mask,
		    sizeof(a->src.addr.v.a.mask)))
			return (1);
		return (0);
	case PF_ADDR_NOROUTE:
	case PF_ADDR_URPFFAILED:
		return (0);
	case PF_ADDR_TABLE:
		return (strcmp(a->src.addr.v.tblname, b->src.addr.v.tblname));
	}
	return (1);
}

/* Compare two rules SRC port field for skiplist construction */
int
skip_cmp_src_port(struct pf_rule *a, struct pf_rule *b)
{
	if (a->src.port_op == PF_OP_NONE || a->src.port_op != b->src.port_op ||
	    a->src.port[0] != b->src.port[0] ||
	    a->src.port[1] != b->src.port[1])
		return (1);
	/* XXX if (a->proto != b->proto && a->proto != 0 && b->proto != 0
	 *    && (a->proto == IPPROTO_TCP || a->proto == IPPROTO_UDP ||
	 *    a->proto == IPPROTO_ICMP
	 *	return (1);
	 */
	return (0);
}


void
skip_init(void)
{
	struct {
		char *name;
		int skipnum;
		int (*func)(struct pf_rule *, struct pf_rule *);
	} comps[] = PF_SKIP_COMPARITORS;
	int skipnum, i;

	for (skipnum = 0; skipnum < PF_SKIP_COUNT; skipnum++) {
		for (i = 0; i < sizeof(comps)/sizeof(*comps); i++)
			if (comps[i].skipnum == skipnum) {
				skip_comparitors[skipnum] = comps[i].func;
				skip_comparitors_names[skipnum] = comps[i].name;
			}
	}
	for (skipnum = 0; skipnum < PF_SKIP_COUNT; skipnum++)
		if (skip_comparitors[skipnum] == NULL)
			errx(1, "Need to add skip step comparitor to pfctl?!");
}

/*
 * Add a host/netmask to a table
 */
int
add_opt_table(struct pfctl *pf, struct pf_opt_tbl **tbl, sa_family_t af,
    struct pf_rule_addr *addr)
{
#ifdef OPT_DEBUG
	char buf[128];
#endif /* OPT_DEBUG */
	static int tablenum = 0;
	struct node_host node_host;

	if (*tbl == NULL) {
		if ((*tbl = calloc(1, sizeof(**tbl))) == NULL ||
		    ((*tbl)->pt_buf = calloc(1, sizeof(*(*tbl)->pt_buf))) ==
		    NULL)
			err(1, "calloc");
		(*tbl)->pt_buf->pfrb_type = PFRB_ADDRS;
		SIMPLEQ_INIT(&(*tbl)->pt_nodes);

		/* This is just a temporary table name */
		snprintf((*tbl)->pt_name, sizeof((*tbl)->pt_name), "%s%d",
		    PF_OPT_TABLE_PREFIX, tablenum++);
		DEBUG("creating table <%s>", (*tbl)->pt_name);
	}

	memset(&node_host, 0, sizeof(node_host));
	node_host.af = af;
	node_host.addr = addr->addr;

#ifdef OPT_DEBUG
	DEBUG("<%s> adding %s/%d", (*tbl)->pt_name, inet_ntop(af,
	    &node_host.addr.v.a.addr, buf, sizeof(buf)),
	    unmask(&node_host.addr.v.a.mask, af));
#endif /* OPT_DEBUG */

	if (append_addr_host((*tbl)->pt_buf, &node_host, 0, 0)) {
		warn("failed to add host");
		return (1);
	}
	if (pf->opts & PF_OPT_VERBOSE) {
		struct node_tinit *ti;

		if ((ti = calloc(1, sizeof(*ti))) == NULL)
			err(1, "malloc");
		if ((ti->host = malloc(sizeof(*ti->host))) == NULL)
			err(1, "malloc");
		memcpy(ti->host, &node_host, sizeof(*ti->host));
		SIMPLEQ_INSERT_TAIL(&(*tbl)->pt_nodes, ti, entries);
	}

	(*tbl)->pt_rulecount++;
	if ((*tbl)->pt_rulecount == TABLE_THRESHOLD)
		DEBUG("table <%s> now faster than skip steps", (*tbl)->pt_name);

	return (0);
}


/*
 * Do the dirty work of choosing an unused table name and creating it.
 * (be careful with the table name, it might already be used in another anchor)
 */
int
pf_opt_create_table(struct pfctl *pf, struct pf_opt_tbl *tbl)
{
	static int tablenum;
	struct pfr_table *t;

	if (table_buffer.pfrb_type == 0) {
		/* Initialize the list of tables */
		table_buffer.pfrb_type = PFRB_TABLES;
		for (;;) {
			pfr_buf_grow(&table_buffer, table_buffer.pfrb_size);
			table_buffer.pfrb_size = table_buffer.pfrb_msize;
			if (pfr_get_tables(NULL, table_buffer.pfrb_caddr,
			    &table_buffer.pfrb_size, PFR_FLAG_ALLRSETS))
				err(1, "pfr_get_tables");
			if (table_buffer.pfrb_size <= table_buffer.pfrb_msize)
				break;
		}
		table_identifier = arc4random();
	}

	/* XXX would be *really* nice to avoid duplicating identical tables */

	/* Now we have to pick a table name that isn't used */
again:
	DEBUG("translating temporary table <%s> to <%s%x_%d>", tbl->pt_name,
	    PF_OPT_TABLE_PREFIX, table_identifier, tablenum);
	snprintf(tbl->pt_name, sizeof(tbl->pt_name), "%s%x_%d",
	    PF_OPT_TABLE_PREFIX, table_identifier, tablenum);
	PFRB_FOREACH(t, &table_buffer) {
		if (strcasecmp(t->pfrt_name, tbl->pt_name) == 0) {
			/* Collision.  Try again */
			DEBUG("wow, table <%s> in use.  trying again",
			    tbl->pt_name);
			table_identifier = arc4random();
			goto again;
		}
	}
	tablenum++;


	if (pfctl_define_table(tbl->pt_name, PFR_TFLAG_CONST, 1,
	    pf->astack[0]->name, tbl->pt_buf, pf->astack[0]->ruleset.tticket)) {
		warn("failed to create table %s in %s",
		    tbl->pt_name, pf->astack[0]->name);
		return (1);
	}
	return (0);
}

/*
 * Partition the flat ruleset into a list of distinct superblocks
 */
int
construct_superblocks(struct pfctl *pf, struct pf_opt_queue *opt_queue,
    struct superblocks *superblocks)
{
	struct superblock *block = NULL;
	struct pf_opt_rule *por;
	int i;

	while (!TAILQ_EMPTY(opt_queue)) {
		por = TAILQ_FIRST(opt_queue);
		TAILQ_REMOVE(opt_queue, por, por_entry);
		if (block == NULL || !superblock_inclusive(block, por)) {
			if ((block = calloc(1, sizeof(*block))) == NULL) {
				warn("calloc");
				return (1);
			}
			TAILQ_INIT(&block->sb_rules);
			for (i = 0; i < PF_SKIP_COUNT; i++)
				TAILQ_INIT(&block->sb_skipsteps[i]);
			TAILQ_INSERT_TAIL(superblocks, block, sb_entry);
		}
		TAILQ_INSERT_TAIL(&block->sb_rules, por, por_entry);
	}

	return (0);
}


/*
 * Compare two rule addresses
 */
int
addrs_equal(struct pf_rule_addr *a, struct pf_rule_addr *b)
{
	if (a->neg != b->neg)
		return (0);
	return (memcmp(&a->addr, &b->addr, sizeof(a->addr)) == 0);
}


/*
 * The addresses are not equal, but can we combine them into one table?
 */
int
addrs_combineable(struct pf_rule_addr *a, struct pf_rule_addr *b)
{
	if (a->addr.type != PF_ADDR_ADDRMASK ||
	    b->addr.type != PF_ADDR_ADDRMASK)
		return (0);
	if (a->neg != b->neg || a->port_op != b->port_op ||
	    a->port[0] != b->port[0] || a->port[1] != b->port[1])
		return (0);
	return (1);
}


/*
 * Are we allowed to combine these two rules
 */
int
rules_combineable(struct pf_rule *p1, struct pf_rule *p2)
{
	struct pf_rule a, b;

	comparable_rule(&a, p1, COMBINED);
	comparable_rule(&b, p2, COMBINED);
	return (memcmp(&a, &b, sizeof(a)) == 0);
}


/*
 * Can a rule be included inside a superblock
 */
int
superblock_inclusive(struct superblock *block, struct pf_opt_rule *por)
{
	struct pf_rule a, b;
	int i, j;

	/* First check for hard breaks */
	for (i = 0; i < sizeof(pf_rule_desc)/sizeof(*pf_rule_desc); i++) {
		if (pf_rule_desc[i].prf_type == BARRIER) {
			for (j = 0; j < pf_rule_desc[i].prf_size; j++)
				if (((char *)&por->por_rule)[j +
				    pf_rule_desc[i].prf_offset] != 0)
					return (0);
		}
	}

	/* per-rule src-track is also a hard break */
	if (por->por_rule.rule_flag & PFRULE_RULESRCTRACK)
		return (0);

	/*
	 * Have to handle interface groups separately.  Consider the following
	 * rules:
	 *	block on EXTIFS to any port 22
	 *	pass  on em0 to any port 22
	 * (where EXTIFS is an arbitrary interface group)
	 * The optimizer may decide to re-order the pass rule in front of the
	 * block rule.  But what if EXTIFS includes em0???  Such a reordering
	 * would change the meaning of the ruleset.
	 * We can't just lookup the EXTIFS group and check if em0 is a member
	 * because the user is allowed to add interfaces to a group during
	 * runtime.
	 * Ergo interface groups become a defacto superblock break :-(
	 */
	if (interface_group(por->por_rule.ifname) ||
	    interface_group(TAILQ_FIRST(&block->sb_rules)->por_rule.ifname)) {
		if (strcasecmp(por->por_rule.ifname,
		    TAILQ_FIRST(&block->sb_rules)->por_rule.ifname) != 0)
			return (0);
	}

	comparable_rule(&a, &TAILQ_FIRST(&block->sb_rules)->por_rule, NOMERGE);
	comparable_rule(&b, &por->por_rule, NOMERGE);
	if (memcmp(&a, &b, sizeof(a)) == 0)
		return (1);

#ifdef OPT_DEBUG
	for (i = 0; i < sizeof(por->por_rule); i++) {
		int closest = -1;
		if (((u_int8_t *)&a)[i] != ((u_int8_t *)&b)[i]) {
			for (j = 0; j < sizeof(pf_rule_desc) /
			    sizeof(*pf_rule_desc); j++) {
				if (i >= pf_rule_desc[j].prf_offset &&
				    i < pf_rule_desc[j].prf_offset +
				    pf_rule_desc[j].prf_size) {
					DEBUG("superblock break @ %d due to %s",
					    por->por_rule.nr,
					    pf_rule_desc[j].prf_name);
					return (0);
				}
				if (i > pf_rule_desc[j].prf_offset) {
					if (closest == -1 ||
					    i-pf_rule_desc[j].prf_offset <
					    i-pf_rule_desc[closest].prf_offset)
						closest = j;
				}
			}

			if (closest >= 0)
				DEBUG("superblock break @ %d on %s+%xh",
				    por->por_rule.nr,
				    pf_rule_desc[closest].prf_name,
				    i - pf_rule_desc[closest].prf_offset -
				    pf_rule_desc[closest].prf_size);
			else
				DEBUG("superblock break @ %d on field @ %d",
				    por->por_rule.nr, i);
			return (0);
		}
	}
#endif /* OPT_DEBUG */

	return (0);
}


/*
 * Figure out if an interface name is an actual interface or actually a
 * group of interfaces.
 */
int
interface_group(const char *ifname)
{
	if (ifname == NULL || !ifname[0])
		return (0);

	/* Real interfaces must end in a number, interface groups do not */
	if (isdigit(ifname[strlen(ifname) - 1]))
		return (0);
	else
		return (1);
}


/*
 * Make a rule that can directly compared by memcmp()
 */
void
comparable_rule(struct pf_rule *dst, const struct pf_rule *src, int type)
{
	int i;
	/*
	 * To simplify the comparison, we just zero out the fields that are
	 * allowed to be different and then do a simple memcmp()
	 */
	memcpy(dst, src, sizeof(*dst));
	for (i = 0; i < sizeof(pf_rule_desc)/sizeof(*pf_rule_desc); i++)
		if (pf_rule_desc[i].prf_type >= type) {
#ifdef OPT_DEBUG
			assert(pf_rule_desc[i].prf_type != NEVER ||
			    *(((char *)dst) + pf_rule_desc[i].prf_offset) == 0);
#endif /* OPT_DEBUG */
			memset(((char *)dst) + pf_rule_desc[i].prf_offset, 0,
			    pf_rule_desc[i].prf_size);
		}
}


/*
 * Remove superset information from two rules so we can directly compare them
 * with memcmp()
 */
void
exclude_supersets(struct pf_rule *super, struct pf_rule *sub)
{
	if (super->ifname[0] == '\0')
		memset(sub->ifname, 0, sizeof(sub->ifname));
	if (super->direction == PF_INOUT)
		sub->direction = PF_INOUT;
	if ((super->proto == 0 || super->proto == sub->proto) &&
	    super->flags == 0 && super->flagset == 0 && (sub->flags ||
	    sub->flagset)) {
		sub->flags = super->flags;
		sub->flagset = super->flagset;
	}
	if (super->proto == 0)
		sub->proto = 0;

	if (super->src.port_op == 0) {
		sub->src.port_op = 0;
		sub->src.port[0] = 0;
		sub->src.port[1] = 0;
	}
	if (super->dst.port_op == 0) {
		sub->dst.port_op = 0;
		sub->dst.port[0] = 0;
		sub->dst.port[1] = 0;
	}

	if (super->src.addr.type == PF_ADDR_ADDRMASK && !super->src.neg &&
	    !sub->src.neg && super->src.addr.v.a.mask.addr32[0] == 0 &&
	    super->src.addr.v.a.mask.addr32[1] == 0 &&
	    super->src.addr.v.a.mask.addr32[2] == 0 &&
	    super->src.addr.v.a.mask.addr32[3] == 0)
		memset(&sub->src.addr, 0, sizeof(sub->src.addr));
	else if (super->src.addr.type == PF_ADDR_ADDRMASK &&
	    sub->src.addr.type == PF_ADDR_ADDRMASK &&
	    super->src.neg == sub->src.neg &&
	    super->af == sub->af &&
	    unmask(&super->src.addr.v.a.mask, super->af) <
	    unmask(&sub->src.addr.v.a.mask, sub->af) &&
	    super->src.addr.v.a.addr.addr32[0] ==
	    (sub->src.addr.v.a.addr.addr32[0] &
	    super->src.addr.v.a.mask.addr32[0]) &&
	    super->src.addr.v.a.addr.addr32[1] ==
	    (sub->src.addr.v.a.addr.addr32[1] &
	    super->src.addr.v.a.mask.addr32[1]) &&
	    super->src.addr.v.a.addr.addr32[2] ==
	    (sub->src.addr.v.a.addr.addr32[2] &
	    super->src.addr.v.a.mask.addr32[2]) &&
	    super->src.addr.v.a.addr.addr32[3] ==
	    (sub->src.addr.v.a.addr.addr32[3] &
	    super->src.addr.v.a.mask.addr32[3])) {
		/* sub->src.addr is a subset of super->src.addr/mask */
		memcpy(&sub->src.addr, &super->src.addr, sizeof(sub->src.addr));
	}

	if (super->dst.addr.type == PF_ADDR_ADDRMASK && !super->dst.neg &&
	    !sub->dst.neg && super->dst.addr.v.a.mask.addr32[0] == 0 &&
	    super->dst.addr.v.a.mask.addr32[1] == 0 &&
	    super->dst.addr.v.a.mask.addr32[2] == 0 &&
	    super->dst.addr.v.a.mask.addr32[3] == 0)
		memset(&sub->dst.addr, 0, sizeof(sub->dst.addr));
	else if (super->dst.addr.type == PF_ADDR_ADDRMASK &&
	    sub->dst.addr.type == PF_ADDR_ADDRMASK &&
	    super->dst.neg == sub->dst.neg &&
	    super->af == sub->af &&
	    unmask(&super->dst.addr.v.a.mask, super->af) <
	    unmask(&sub->dst.addr.v.a.mask, sub->af) &&
	    super->dst.addr.v.a.addr.addr32[0] ==
	    (sub->dst.addr.v.a.addr.addr32[0] &
	    super->dst.addr.v.a.mask.addr32[0]) &&
	    super->dst.addr.v.a.addr.addr32[1] ==
	    (sub->dst.addr.v.a.addr.addr32[1] &
	    super->dst.addr.v.a.mask.addr32[1]) &&
	    super->dst.addr.v.a.addr.addr32[2] ==
	    (sub->dst.addr.v.a.addr.addr32[2] &
	    super->dst.addr.v.a.mask.addr32[2]) &&
	    super->dst.addr.v.a.addr.addr32[3] ==
	    (sub->dst.addr.v.a.addr.addr32[3] &
	    super->dst.addr.v.a.mask.addr32[3])) {
		/* sub->dst.addr is a subset of super->dst.addr/mask */
		memcpy(&sub->dst.addr, &super->dst.addr, sizeof(sub->dst.addr));
	}

	if (super->af == 0)
		sub->af = 0;
}


void
superblock_free(struct pfctl *pf, struct superblock *block)
{
	struct pf_opt_rule *por;
	while ((por = TAILQ_FIRST(&block->sb_rules))) {
		TAILQ_REMOVE(&block->sb_rules, por, por_entry);
		if (por->por_src_tbl) {
			if (por->por_src_tbl->pt_buf) {
				pfr_buf_clear(por->por_src_tbl->pt_buf);
				free(por->por_src_tbl->pt_buf);
			}
			free(por->por_src_tbl);
		}
		if (por->por_dst_tbl) {
			if (por->por_dst_tbl->pt_buf) {
				pfr_buf_clear(por->por_dst_tbl->pt_buf);
				free(por->por_dst_tbl->pt_buf);
			}
			free(por->por_dst_tbl);
		}
		free(por);
	}
	if (block->sb_profiled_block)
		superblock_free(pf, block->sb_profiled_block);
	free(block);
}

