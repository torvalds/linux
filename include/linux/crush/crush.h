#ifndef CEPH_CRUSH_CRUSH_H
#define CEPH_CRUSH_CRUSH_H

#include <linux/types.h>

/*
 * CRUSH is a pseudo-random data distribution algorithm that
 * efficiently distributes input values (typically, data objects)
 * across a heterogeneous, structured storage cluster.
 *
 * The algorithm was originally described in detail in this paper
 * (although the algorithm has evolved somewhat since then):
 *
 *     http://www.ssrc.ucsc.edu/Papers/weil-sc06.pdf
 *
 * LGPL2
 */


#define CRUSH_MAGIC 0x00010000ul   /* for detecting algorithm revisions */

#define CRUSH_MAX_DEPTH 10  /* max crush hierarchy depth */


#define CRUSH_ITEM_UNDEF  0x7ffffffe  /* undefined result (internal use only) */
#define CRUSH_ITEM_NONE   0x7fffffff  /* no result */

/*
 * CRUSH uses user-defined "rules" to describe how inputs should be
 * mapped to devices.  A rule consists of sequence of steps to perform
 * to generate the set of output devices.
 */
struct crush_rule_step {
	__u32 op;
	__s32 arg1;
	__s32 arg2;
};

/* step op codes */
enum {
	CRUSH_RULE_NOOP = 0,
	CRUSH_RULE_TAKE = 1,          /* arg1 = value to start with */
	CRUSH_RULE_CHOOSE_FIRSTN = 2, /* arg1 = num items to pick */
				      /* arg2 = type */
	CRUSH_RULE_CHOOSE_INDEP = 3,  /* same */
	CRUSH_RULE_EMIT = 4,          /* no args */
	CRUSH_RULE_CHOOSELEAF_FIRSTN = 6,
	CRUSH_RULE_CHOOSELEAF_INDEP = 7,

	CRUSH_RULE_SET_CHOOSE_TRIES = 8, /* override choose_total_tries */
	CRUSH_RULE_SET_CHOOSELEAF_TRIES = 9, /* override chooseleaf_descend_once */
	CRUSH_RULE_SET_CHOOSE_LOCAL_TRIES = 10,
	CRUSH_RULE_SET_CHOOSE_LOCAL_FALLBACK_TRIES = 11,
};

/*
 * for specifying choose num (arg1) relative to the max parameter
 * passed to do_rule
 */
#define CRUSH_CHOOSE_N            0
#define CRUSH_CHOOSE_N_MINUS(x)   (-(x))

/*
 * The rule mask is used to describe what the rule is intended for.
 * Given a ruleset and size of output set, we search through the
 * rule list for a matching rule_mask.
 */
struct crush_rule_mask {
	__u8 ruleset;
	__u8 type;
	__u8 min_size;
	__u8 max_size;
};

struct crush_rule {
	__u32 len;
	struct crush_rule_mask mask;
	struct crush_rule_step steps[0];
};

#define crush_rule_size(len) (sizeof(struct crush_rule) + \
			      (len)*sizeof(struct crush_rule_step))



/*
 * A bucket is a named container of other items (either devices or
 * other buckets).  Items within a bucket are chosen using one of a
 * few different algorithms.  The table summarizes how the speed of
 * each option measures up against mapping stability when items are
 * added or removed.
 *
 *  Bucket Alg     Speed       Additions    Removals
 *  ------------------------------------------------
 *  uniform         O(1)       poor         poor
 *  list            O(n)       optimal      poor
 *  tree            O(log n)   good         good
 *  straw           O(n)       optimal      optimal
 */
enum {
	CRUSH_BUCKET_UNIFORM = 1,
	CRUSH_BUCKET_LIST = 2,
	CRUSH_BUCKET_TREE = 3,
	CRUSH_BUCKET_STRAW = 4
};
extern const char *crush_bucket_alg_name(int alg);

struct crush_bucket {
	__s32 id;        /* this'll be negative */
	__u16 type;      /* non-zero; type=0 is reserved for devices */
	__u8 alg;        /* one of CRUSH_BUCKET_* */
	__u8 hash;       /* which hash function to use, CRUSH_HASH_* */
	__u32 weight;    /* 16-bit fixed point */
	__u32 size;      /* num items */
	__s32 *items;

	/*
	 * cached random permutation: used for uniform bucket and for
	 * the linear search fallback for the other bucket types.
	 */
	__u32 perm_x;  /* @x for which *perm is defined */
	__u32 perm_n;  /* num elements of *perm that are permuted/defined */
	__u32 *perm;
};

struct crush_bucket_uniform {
	struct crush_bucket h;
	__u32 item_weight;  /* 16-bit fixed point; all items equally weighted */
};

struct crush_bucket_list {
	struct crush_bucket h;
	__u32 *item_weights;  /* 16-bit fixed point */
	__u32 *sum_weights;   /* 16-bit fixed point.  element i is sum
				 of weights 0..i, inclusive */
};

struct crush_bucket_tree {
	struct crush_bucket h;  /* note: h.size is _tree_ size, not number of
				   actual items */
	__u8 num_nodes;
	__u32 *node_weights;
};

struct crush_bucket_straw {
	struct crush_bucket h;
	__u32 *item_weights;   /* 16-bit fixed point */
	__u32 *straws;         /* 16-bit fixed point */
};



/*
 * CRUSH map includes all buckets, rules, etc.
 */
struct crush_map {
	struct crush_bucket **buckets;
	struct crush_rule **rules;

	__s32 max_buckets;
	__u32 max_rules;
	__s32 max_devices;

	/* choose local retries before re-descent */
	__u32 choose_local_tries;
	/* choose local attempts using a fallback permutation before
	 * re-descent */
	__u32 choose_local_fallback_tries;
	/* choose attempts before giving up */ 
	__u32 choose_total_tries;
	/* attempt chooseleaf inner descent once for firstn mode; on
	 * reject retry outer descent.  Note that this does *not*
	 * apply to a collision: in that case we will retry as we used
	 * to. */
	__u32 chooseleaf_descend_once;
};


/* crush.c */
extern int crush_get_bucket_item_weight(const struct crush_bucket *b, int pos);
extern void crush_destroy_bucket_uniform(struct crush_bucket_uniform *b);
extern void crush_destroy_bucket_list(struct crush_bucket_list *b);
extern void crush_destroy_bucket_tree(struct crush_bucket_tree *b);
extern void crush_destroy_bucket_straw(struct crush_bucket_straw *b);
extern void crush_destroy_bucket(struct crush_bucket *b);
extern void crush_destroy_rule(struct crush_rule *r);
extern void crush_destroy(struct crush_map *map);

static inline int crush_calc_tree_node(int i)
{
	return ((i+1) << 1)-1;
}

#endif
