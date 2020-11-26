/* SPDX-License-Identifier: GPL-2.0 */
#ifndef CEPH_CRUSH_CRUSH_H
#define CEPH_CRUSH_CRUSH_H

#ifdef __KERNEL__
# include <linux/rbtree.h>
# include <linux/types.h>
#else
# include "crush_compat.h"
#endif

/*
 * CRUSH is a pseudo-random data distribution algorithm that
 * efficiently distributes input values (typically, data objects)
 * across a heterogeneous, structured storage cluster.
 *
 * The algorithm was originally described in detail in this paper
 * (although the algorithm has evolved somewhat since then):
 *
 *     https://www.ssrc.ucsc.edu/Papers/weil-sc06.pdf
 *
 * LGPL2
 */


#define CRUSH_MAGIC 0x00010000ul   /* for detecting algorithm revisions */

#define CRUSH_MAX_DEPTH 10  /* max crush hierarchy depth */
#define CRUSH_MAX_RULESET (1<<8)  /* max crush ruleset number */
#define CRUSH_MAX_RULES CRUSH_MAX_RULESET  /* should be the same as max rulesets */

#define CRUSH_MAX_DEVICE_WEIGHT (100u * 0x10000u)
#define CRUSH_MAX_BUCKET_WEIGHT (65535u * 0x10000u)

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
	CRUSH_RULE_SET_CHOOSELEAF_VARY_R = 12,
	CRUSH_RULE_SET_CHOOSELEAF_STABLE = 13
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
	struct crush_rule_step steps[];
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
 *  straw           O(n)       better       better
 *  straw2          O(n)       optimal      optimal
 */
enum {
	CRUSH_BUCKET_UNIFORM = 1,
	CRUSH_BUCKET_LIST = 2,
	CRUSH_BUCKET_TREE = 3,
	CRUSH_BUCKET_STRAW = 4,
	CRUSH_BUCKET_STRAW2 = 5,
};
extern const char *crush_bucket_alg_name(int alg);

/*
 * although tree was a legacy algorithm, it has been buggy, so
 * exclude it.
 */
#define CRUSH_LEGACY_ALLOWED_BUCKET_ALGS (	\
		(1 << CRUSH_BUCKET_UNIFORM) |	\
		(1 << CRUSH_BUCKET_LIST) |	\
		(1 << CRUSH_BUCKET_STRAW))

struct crush_bucket {
	__s32 id;        /* this'll be negative */
	__u16 type;      /* non-zero; type=0 is reserved for devices */
	__u8 alg;        /* one of CRUSH_BUCKET_* */
	__u8 hash;       /* which hash function to use, CRUSH_HASH_* */
	__u32 weight;    /* 16-bit fixed point */
	__u32 size;      /* num items */
	__s32 *items;

};

/** @ingroup API
 *
 * Replacement weights for each item in a bucket. The size of the
 * array must be exactly the size of the straw2 bucket, just as the
 * item_weights array.
 *
 */
struct crush_weight_set {
	__u32 *weights; /*!< 16.16 fixed point weights
                             in the same order as items */
	__u32 size;     /*!< size of the __weights__ array */
};

/** @ingroup API
 *
 * Replacement weights and ids for a given straw2 bucket, for
 * placement purposes.
 *
 * When crush_do_rule() chooses the Nth item from a straw2 bucket, the
 * replacement weights found at __weight_set[N]__ are used instead of
 * the weights from __item_weights__. If __N__ is greater than
 * __weight_set_size__, the weights found at __weight_set_size-1__ are
 * used instead. For instance if __weight_set__ is:
 *
 *    [ [ 0x10000, 0x20000 ],   // position 0
 *      [ 0x20000, 0x40000 ] ]  // position 1
 *
 * choosing the 0th item will use position 0 weights [ 0x10000, 0x20000 ]
 * choosing the 1th item will use position 1 weights [ 0x20000, 0x40000 ]
 * choosing the 2th item will use position 1 weights [ 0x20000, 0x40000 ]
 * etc.
 *
 */
struct crush_choose_arg {
	__s32 *ids;            /*!< values to use instead of items */
	__u32 ids_size;        /*!< size of the __ids__ array */
	struct crush_weight_set *weight_set; /*!< weight replacements for
                                                  a given position */
	__u32 weight_set_size; /*!< size of the __weight_set__ array */
};

/** @ingroup API
 *
 * Replacement weights and ids for each bucket in the crushmap. The
 * __size__ of the __args__ array must be exactly the same as the
 * __map->max_buckets__.
 *
 * The __crush_choose_arg__ at index N will be used when choosing
 * an item from the bucket __map->buckets[N]__ bucket, provided it
 * is a straw2 bucket.
 *
 */
struct crush_choose_arg_map {
#ifdef __KERNEL__
	struct rb_node node;
	s64 choose_args_index;
#endif
	struct crush_choose_arg *args; /*!< replacement for each bucket
                                            in the crushmap */
	__u32 size;                    /*!< size of the __args__ array */
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

struct crush_bucket_straw2 {
	struct crush_bucket h;
	__u32 *item_weights;   /* 16-bit fixed point */
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

	/* if non-zero, feed r into chooseleaf, bit-shifted right by (r-1)
	 * bits.  a value of 1 is best for new clusters.  for legacy clusters
	 * that want to limit reshuffling, a value of 3 or 4 will make the
	 * mappings line up a bit better with previous mappings. */
	__u8 chooseleaf_vary_r;

	/* if true, it makes chooseleaf firstn to return stable results (if
	 * no local retry) so that data migrations would be optimal when some
	 * device fails. */
	__u8 chooseleaf_stable;

	/*
	 * This value is calculated after decode or construction by
	 * the builder. It is exposed here (rather than having a
	 * 'build CRUSH working space' function) so that callers can
	 * reserve a static buffer, allocate space on the stack, or
	 * otherwise avoid calling into the heap allocator if they
	 * want to. The size of the working space depends on the map,
	 * while the size of the scratch vector passed to the mapper
	 * depends on the size of the desired result set.
	 *
	 * Nothing stops the caller from allocating both in one swell
	 * foop and passing in two points, though.
	 */
	size_t working_size;

#ifndef __KERNEL__
	/*
	 * version 0 (original) of straw_calc has various flaws.  version 1
	 * fixes a few of them.
	 */
	__u8 straw_calc_version;

	/*
	 * allowed bucket algs is a bitmask, here the bit positions
	 * are CRUSH_BUCKET_*.  note that these are *bits* and
	 * CRUSH_BUCKET_* values are not, so we need to or together (1
	 * << CRUSH_BUCKET_WHATEVER).  The 0th bit is not used to
	 * minimize confusion (bucket type values start at 1).
	 */
	__u32 allowed_bucket_algs;

	__u32 *choose_tries;
#else
	/* device/bucket type id -> type name (CrushWrapper::type_map) */
	struct rb_root type_names;

	/* device/bucket id -> name (CrushWrapper::name_map) */
	struct rb_root names;

	/* CrushWrapper::choose_args */
	struct rb_root choose_args;
#endif
};


/* crush.c */
extern int crush_get_bucket_item_weight(const struct crush_bucket *b, int pos);
extern void crush_destroy_bucket_uniform(struct crush_bucket_uniform *b);
extern void crush_destroy_bucket_list(struct crush_bucket_list *b);
extern void crush_destroy_bucket_tree(struct crush_bucket_tree *b);
extern void crush_destroy_bucket_straw(struct crush_bucket_straw *b);
extern void crush_destroy_bucket_straw2(struct crush_bucket_straw2 *b);
extern void crush_destroy_bucket(struct crush_bucket *b);
extern void crush_destroy_rule(struct crush_rule *r);
extern void crush_destroy(struct crush_map *map);

static inline int crush_calc_tree_node(int i)
{
	return ((i+1) << 1)-1;
}

/*
 * These data structures are private to the CRUSH implementation. They
 * are exposed in this header file because builder needs their
 * definitions to calculate the total working size.
 *
 * Moving this out of the crush map allow us to treat the CRUSH map as
 * immutable within the mapper and removes the requirement for a CRUSH
 * map lock.
 */
struct crush_work_bucket {
	__u32 perm_x; /* @x for which *perm is defined */
	__u32 perm_n; /* num elements of *perm that are permuted/defined */
	__u32 *perm;  /* Permutation of the bucket's items */
};

struct crush_work {
	struct crush_work_bucket **work; /* Per-bucket working store */
#ifdef __KERNEL__
	struct list_head item;
#endif
};

#ifdef __KERNEL__
/* osdmap.c */
void clear_crush_names(struct rb_root *root);
void clear_choose_args(struct crush_map *c);
#endif

#endif
