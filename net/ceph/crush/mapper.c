/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2015 Intel Corporation All Rights Reserved
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#ifdef __KERNEL__
# include <linux/string.h>
# include <linux/slab.h>
# include <linux/bug.h>
# include <linux/kernel.h>
# include <linux/crush/crush.h>
# include <linux/crush/hash.h>
# include <linux/crush/mapper.h>
#else
# include "crush_compat.h"
# include "crush.h"
# include "hash.h"
# include "mapper.h"
#endif
#include "crush_ln_table.h"

#define dprintk(args...) /* printf(args) */

/*
 * Implement the core CRUSH mapping algorithm.
 */

/**
 * crush_find_rule - find a crush_rule id for a given ruleset, type, and size.
 * @map: the crush_map
 * @ruleset: the storage ruleset id (user defined)
 * @type: storage ruleset type (user defined)
 * @size: output set size
 */
int crush_find_rule(const struct crush_map *map, int ruleset, int type, int size)
{
	__u32 i;

	for (i = 0; i < map->max_rules; i++) {
		if (map->rules[i] &&
		    map->rules[i]->mask.ruleset == ruleset &&
		    map->rules[i]->mask.type == type &&
		    map->rules[i]->mask.min_size <= size &&
		    map->rules[i]->mask.max_size >= size)
			return i;
	}
	return -1;
}

/*
 * bucket choose methods
 *
 * For each bucket algorithm, we have a "choose" method that, given a
 * crush input @x and replica position (usually, position in output set) @r,
 * will produce an item in the bucket.
 */

/*
 * Choose based on a random permutation of the bucket.
 *
 * We used to use some prime number arithmetic to do this, but it
 * wasn't very random, and had some other bad behaviors.  Instead, we
 * calculate an actual random permutation of the bucket members.
 * Since this is expensive, we optimize for the r=0 case, which
 * captures the vast majority of calls.
 */
static int bucket_perm_choose(const struct crush_bucket *bucket,
			      struct crush_work_bucket *work,
			      int x, int r)
{
	unsigned int pr = r % bucket->size;
	unsigned int i, s;

	/* start a new permutation if @x has changed */
	if (work->perm_x != (__u32)x || work->perm_n == 0) {
		dprintk("bucket %d new x=%d\n", bucket->id, x);
		work->perm_x = x;

		/* optimize common r=0 case */
		if (pr == 0) {
			s = crush_hash32_3(bucket->hash, x, bucket->id, 0) %
				bucket->size;
			work->perm[0] = s;
			work->perm_n = 0xffff;   /* magic value, see below */
			goto out;
		}

		for (i = 0; i < bucket->size; i++)
			work->perm[i] = i;
		work->perm_n = 0;
	} else if (work->perm_n == 0xffff) {
		/* clean up after the r=0 case above */
		for (i = 1; i < bucket->size; i++)
			work->perm[i] = i;
		work->perm[work->perm[0]] = 0;
		work->perm_n = 1;
	}

	/* calculate permutation up to pr */
	for (i = 0; i < work->perm_n; i++)
		dprintk(" perm_choose have %d: %d\n", i, bucket->perm[i]);
	while (work->perm_n <= pr) {
		unsigned int p = work->perm_n;
		/* no point in swapping the final entry */
		if (p < bucket->size - 1) {
			i = crush_hash32_3(bucket->hash, x, bucket->id, p) %
				(bucket->size - p);
			if (i) {
				unsigned int t = work->perm[p + i];
				work->perm[p + i] = work->perm[p];
				work->perm[p] = t;
			}
			dprintk(" perm_choose swap %d with %d\n", p, p+i);
		}
		work->perm_n++;
	}
	for (i = 0; i < bucket->size; i++)
		dprintk(" perm_choose  %d: %d\n", i, bucket->perm[i]);

	s = work->perm[pr];
out:
	dprintk(" perm_choose %d sz=%d x=%d r=%d (%d) s=%d\n", bucket->id,
		bucket->size, x, r, pr, s);
	return bucket->items[s];
}

/* uniform */
static int bucket_uniform_choose(const struct crush_bucket_uniform *bucket,
				 struct crush_work_bucket *work, int x, int r)
{
	return bucket_perm_choose(&bucket->h, work, x, r);
}

/* list */
static int bucket_list_choose(const struct crush_bucket_list *bucket,
			      int x, int r)
{
	int i;

	for (i = bucket->h.size-1; i >= 0; i--) {
		__u64 w = crush_hash32_4(bucket->h.hash, x, bucket->h.items[i],
					 r, bucket->h.id);
		w &= 0xffff;
		dprintk("list_choose i=%d x=%d r=%d item %d weight %x "
			"sw %x rand %llx",
			i, x, r, bucket->h.items[i], bucket->item_weights[i],
			bucket->sum_weights[i], w);
		w *= bucket->sum_weights[i];
		w = w >> 16;
		/*dprintk(" scaled %llx\n", w);*/
		if (w < bucket->item_weights[i]) {
			return bucket->h.items[i];
		}
	}

	dprintk("bad list sums for bucket %d\n", bucket->h.id);
	return bucket->h.items[0];
}


/* (binary) tree */
static int height(int n)
{
	int h = 0;
	while ((n & 1) == 0) {
		h++;
		n = n >> 1;
	}
	return h;
}

static int left(int x)
{
	int h = height(x);
	return x - (1 << (h-1));
}

static int right(int x)
{
	int h = height(x);
	return x + (1 << (h-1));
}

static int terminal(int x)
{
	return x & 1;
}

static int bucket_tree_choose(const struct crush_bucket_tree *bucket,
			      int x, int r)
{
	int n;
	__u32 w;
	__u64 t;

	/* start at root */
	n = bucket->num_nodes >> 1;

	while (!terminal(n)) {
		int l;
		/* pick point in [0, w) */
		w = bucket->node_weights[n];
		t = (__u64)crush_hash32_4(bucket->h.hash, x, n, r,
					  bucket->h.id) * (__u64)w;
		t = t >> 32;

		/* descend to the left or right? */
		l = left(n);
		if (t < bucket->node_weights[l])
			n = l;
		else
			n = right(n);
	}

	return bucket->h.items[n >> 1];
}


/* straw */

static int bucket_straw_choose(const struct crush_bucket_straw *bucket,
			       int x, int r)
{
	__u32 i;
	int high = 0;
	__u64 high_draw = 0;
	__u64 draw;

	for (i = 0; i < bucket->h.size; i++) {
		draw = crush_hash32_3(bucket->h.hash, x, bucket->h.items[i], r);
		draw &= 0xffff;
		draw *= bucket->straws[i];
		if (i == 0 || draw > high_draw) {
			high = i;
			high_draw = draw;
		}
	}
	return bucket->h.items[high];
}

/* compute 2^44*log2(input+1) */
static __u64 crush_ln(unsigned int xin)
{
	unsigned int x = xin;
	int iexpon, index1, index2;
	__u64 RH, LH, LL, xl64, result;

	x++;

	/* normalize input */
	iexpon = 15;

	/*
	 * figure out number of bits we need to shift and
	 * do it in one step instead of iteratively
	 */
	if (!(x & 0x18000)) {
		int bits = __builtin_clz(x & 0x1FFFF) - 16;
		x <<= bits;
		iexpon = 15 - bits;
	}

	index1 = (x >> 8) << 1;
	/* RH ~ 2^56/index1 */
	RH = __RH_LH_tbl[index1 - 256];
	/* LH ~ 2^48 * log2(index1/256) */
	LH = __RH_LH_tbl[index1 + 1 - 256];

	/* RH*x ~ 2^48 * (2^15 + xf), xf<2^8 */
	xl64 = (__s64)x * RH;
	xl64 >>= 48;

	result = iexpon;
	result <<= (12 + 32);

	index2 = xl64 & 0xff;
	/* LL ~ 2^48*log2(1.0+index2/2^15) */
	LL = __LL_tbl[index2];

	LH = LH + LL;

	LH >>= (48 - 12 - 32);
	result += LH;

	return result;
}


/*
 * straw2
 *
 * for reference, see:
 *
 * http://en.wikipedia.org/wiki/Exponential_distribution#Distribution_of_the_minimum_of_exponential_random_variables
 *
 */

static int bucket_straw2_choose(const struct crush_bucket_straw2 *bucket,
				int x, int r)
{
	unsigned int i, high = 0;
	unsigned int u;
	unsigned int w;
	__s64 ln, draw, high_draw = 0;

	for (i = 0; i < bucket->h.size; i++) {
		w = bucket->item_weights[i];
		if (w) {
			u = crush_hash32_3(bucket->h.hash, x,
					   bucket->h.items[i], r);
			u &= 0xffff;

			/*
			 * for some reason slightly less than 0x10000 produces
			 * a slightly more accurate distribution... probably a
			 * rounding effect.
			 *
			 * the natural log lookup table maps [0,0xffff]
			 * (corresponding to real numbers [1/0x10000, 1] to
			 * [0, 0xffffffffffff] (corresponding to real numbers
			 * [-11.090355,0]).
			 */
			ln = crush_ln(u) - 0x1000000000000ll;

			/*
			 * divide by 16.16 fixed-point weight.  note
			 * that the ln value is negative, so a larger
			 * weight means a larger (less negative) value
			 * for draw.
			 */
			draw = div64_s64(ln, w);
		} else {
			draw = S64_MIN;
		}

		if (i == 0 || draw > high_draw) {
			high = i;
			high_draw = draw;
		}
	}

	return bucket->h.items[high];
}


static int crush_bucket_choose(const struct crush_bucket *in,
			       struct crush_work_bucket *work,
			       int x, int r)
{
	dprintk(" crush_bucket_choose %d x=%d r=%d\n", in->id, x, r);
	BUG_ON(in->size == 0);
	switch (in->alg) {
	case CRUSH_BUCKET_UNIFORM:
		return bucket_uniform_choose(
			(const struct crush_bucket_uniform *)in,
			work, x, r);
	case CRUSH_BUCKET_LIST:
		return bucket_list_choose((const struct crush_bucket_list *)in,
					  x, r);
	case CRUSH_BUCKET_TREE:
		return bucket_tree_choose((const struct crush_bucket_tree *)in,
					  x, r);
	case CRUSH_BUCKET_STRAW:
		return bucket_straw_choose(
			(const struct crush_bucket_straw *)in,
			x, r);
	case CRUSH_BUCKET_STRAW2:
		return bucket_straw2_choose(
			(const struct crush_bucket_straw2 *)in,
			x, r);
	default:
		dprintk("unknown bucket %d alg %d\n", in->id, in->alg);
		return in->items[0];
	}
}

/*
 * true if device is marked "out" (failed, fully offloaded)
 * of the cluster
 */
static int is_out(const struct crush_map *map,
		  const __u32 *weight, int weight_max,
		  int item, int x)
{
	if (item >= weight_max)
		return 1;
	if (weight[item] >= 0x10000)
		return 0;
	if (weight[item] == 0)
		return 1;
	if ((crush_hash32_2(CRUSH_HASH_RJENKINS1, x, item) & 0xffff)
	    < weight[item])
		return 0;
	return 1;
}

/**
 * crush_choose_firstn - choose numrep distinct items of given type
 * @map: the crush_map
 * @bucket: the bucket we are choose an item from
 * @x: crush input value
 * @numrep: the number of items to choose
 * @type: the type of item to choose
 * @out: pointer to output vector
 * @outpos: our position in that vector
 * @out_size: size of the out vector
 * @tries: number of attempts to make
 * @recurse_tries: number of attempts to have recursive chooseleaf make
 * @local_retries: localized retries
 * @local_fallback_retries: localized fallback retries
 * @recurse_to_leaf: true if we want one device under each item of given type (chooseleaf instead of choose)
 * @stable: stable mode starts rep=0 in the recursive call for all replicas
 * @vary_r: pass r to recursive calls
 * @out2: second output vector for leaf items (if @recurse_to_leaf)
 * @parent_r: r value passed from the parent
 */
static int crush_choose_firstn(const struct crush_map *map,
			       struct crush_work *work,
			       const struct crush_bucket *bucket,
			       const __u32 *weight, int weight_max,
			       int x, int numrep, int type,
			       int *out, int outpos,
			       int out_size,
			       unsigned int tries,
			       unsigned int recurse_tries,
			       unsigned int local_retries,
			       unsigned int local_fallback_retries,
			       int recurse_to_leaf,
			       unsigned int vary_r,
			       unsigned int stable,
			       int *out2,
			       int parent_r)
{
	int rep;
	unsigned int ftotal, flocal;
	int retry_descent, retry_bucket, skip_rep;
	const struct crush_bucket *in = bucket;
	int r;
	int i;
	int item = 0;
	int itemtype;
	int collide, reject;
	int count = out_size;

	dprintk("CHOOSE%s bucket %d x %d outpos %d numrep %d tries %d recurse_tries %d local_retries %d local_fallback_retries %d parent_r %d stable %d\n",
		recurse_to_leaf ? "_LEAF" : "",
		bucket->id, x, outpos, numrep,
		tries, recurse_tries, local_retries, local_fallback_retries,
		parent_r, stable);

	for (rep = stable ? 0 : outpos; rep < numrep && count > 0 ; rep++) {
		/* keep trying until we get a non-out, non-colliding item */
		ftotal = 0;
		skip_rep = 0;
		do {
			retry_descent = 0;
			in = bucket;               /* initial bucket */

			/* choose through intervening buckets */
			flocal = 0;
			do {
				collide = 0;
				retry_bucket = 0;
				r = rep + parent_r;
				/* r' = r + f_total */
				r += ftotal;

				/* bucket choose */
				if (in->size == 0) {
					reject = 1;
					goto reject;
				}
				if (local_fallback_retries > 0 &&
				    flocal >= (in->size>>1) &&
				    flocal > local_fallback_retries)
					item = bucket_perm_choose(
						in, work->work[-1-in->id],
						x, r);
				else
					item = crush_bucket_choose(
						in, work->work[-1-in->id],
						x, r);
				if (item >= map->max_devices) {
					dprintk("   bad item %d\n", item);
					skip_rep = 1;
					break;
				}

				/* desired type? */
				if (item < 0)
					itemtype = map->buckets[-1-item]->type;
				else
					itemtype = 0;
				dprintk("  item %d type %d\n", item, itemtype);

				/* keep going? */
				if (itemtype != type) {
					if (item >= 0 ||
					    (-1-item) >= map->max_buckets) {
						dprintk("   bad item type %d\n", type);
						skip_rep = 1;
						break;
					}
					in = map->buckets[-1-item];
					retry_bucket = 1;
					continue;
				}

				/* collision? */
				for (i = 0; i < outpos; i++) {
					if (out[i] == item) {
						collide = 1;
						break;
					}
				}

				reject = 0;
				if (!collide && recurse_to_leaf) {
					if (item < 0) {
						int sub_r;
						if (vary_r)
							sub_r = r >> (vary_r-1);
						else
							sub_r = 0;
						if (crush_choose_firstn(
							    map,
							    work,
							    map->buckets[-1-item],
							    weight, weight_max,
							    x, stable ? 1 : outpos+1, 0,
							    out2, outpos, count,
							    recurse_tries, 0,
							    local_retries,
							    local_fallback_retries,
							    0,
							    vary_r,
							    stable,
							    NULL,
							    sub_r) <= outpos)
							/* didn't get leaf */
							reject = 1;
					} else {
						/* we already have a leaf! */
						out2[outpos] = item;
					}
				}

				if (!reject) {
					/* out? */
					if (itemtype == 0)
						reject = is_out(map, weight,
								weight_max,
								item, x);
					else
						reject = 0;
				}

reject:
				if (reject || collide) {
					ftotal++;
					flocal++;

					if (collide && flocal <= local_retries)
						/* retry locally a few times */
						retry_bucket = 1;
					else if (local_fallback_retries > 0 &&
						 flocal <= in->size + local_fallback_retries)
						/* exhaustive bucket search */
						retry_bucket = 1;
					else if (ftotal < tries)
						/* then retry descent */
						retry_descent = 1;
					else
						/* else give up */
						skip_rep = 1;
					dprintk("  reject %d  collide %d  "
						"ftotal %u  flocal %u\n",
						reject, collide, ftotal,
						flocal);
				}
			} while (retry_bucket);
		} while (retry_descent);

		if (skip_rep) {
			dprintk("skip rep\n");
			continue;
		}

		dprintk("CHOOSE got %d\n", item);
		out[outpos] = item;
		outpos++;
		count--;
#ifndef __KERNEL__
		if (map->choose_tries && ftotal <= map->choose_total_tries)
			map->choose_tries[ftotal]++;
#endif
	}

	dprintk("CHOOSE returns %d\n", outpos);
	return outpos;
}


/**
 * crush_choose_indep: alternative breadth-first positionally stable mapping
 *
 */
static void crush_choose_indep(const struct crush_map *map,
			       struct crush_work *work,
			       const struct crush_bucket *bucket,
			       const __u32 *weight, int weight_max,
			       int x, int left, int numrep, int type,
			       int *out, int outpos,
			       unsigned int tries,
			       unsigned int recurse_tries,
			       int recurse_to_leaf,
			       int *out2,
			       int parent_r)
{
	const struct crush_bucket *in = bucket;
	int endpos = outpos + left;
	int rep;
	unsigned int ftotal;
	int r;
	int i;
	int item = 0;
	int itemtype;
	int collide;

	dprintk("CHOOSE%s INDEP bucket %d x %d outpos %d numrep %d\n", recurse_to_leaf ? "_LEAF" : "",
		bucket->id, x, outpos, numrep);

	/* initially my result is undefined */
	for (rep = outpos; rep < endpos; rep++) {
		out[rep] = CRUSH_ITEM_UNDEF;
		if (out2)
			out2[rep] = CRUSH_ITEM_UNDEF;
	}

	for (ftotal = 0; left > 0 && ftotal < tries; ftotal++) {
#ifdef DEBUG_INDEP
		if (out2 && ftotal) {
			dprintk("%u %d a: ", ftotal, left);
			for (rep = outpos; rep < endpos; rep++) {
				dprintk(" %d", out[rep]);
			}
			dprintk("\n");
			dprintk("%u %d b: ", ftotal, left);
			for (rep = outpos; rep < endpos; rep++) {
				dprintk(" %d", out2[rep]);
			}
			dprintk("\n");
		}
#endif
		for (rep = outpos; rep < endpos; rep++) {
			if (out[rep] != CRUSH_ITEM_UNDEF)
				continue;

			in = bucket;  /* initial bucket */

			/* choose through intervening buckets */
			for (;;) {
				/* note: we base the choice on the position
				 * even in the nested call.  that means that
				 * if the first layer chooses the same bucket
				 * in a different position, we will tend to
				 * choose a different item in that bucket.
				 * this will involve more devices in data
				 * movement and tend to distribute the load.
				 */
				r = rep + parent_r;

				/* be careful */
				if (in->alg == CRUSH_BUCKET_UNIFORM &&
				    in->size % numrep == 0)
					/* r'=r+(n+1)*f_total */
					r += (numrep+1) * ftotal;
				else
					/* r' = r + n*f_total */
					r += numrep * ftotal;

				/* bucket choose */
				if (in->size == 0) {
					dprintk("   empty bucket\n");
					break;
				}

				item = crush_bucket_choose(
					in, work->work[-1-in->id],
					x, r);
				if (item >= map->max_devices) {
					dprintk("   bad item %d\n", item);
					out[rep] = CRUSH_ITEM_NONE;
					if (out2)
						out2[rep] = CRUSH_ITEM_NONE;
					left--;
					break;
				}

				/* desired type? */
				if (item < 0)
					itemtype = map->buckets[-1-item]->type;
				else
					itemtype = 0;
				dprintk("  item %d type %d\n", item, itemtype);

				/* keep going? */
				if (itemtype != type) {
					if (item >= 0 ||
					    (-1-item) >= map->max_buckets) {
						dprintk("   bad item type %d\n", type);
						out[rep] = CRUSH_ITEM_NONE;
						if (out2)
							out2[rep] =
								CRUSH_ITEM_NONE;
						left--;
						break;
					}
					in = map->buckets[-1-item];
					continue;
				}

				/* collision? */
				collide = 0;
				for (i = outpos; i < endpos; i++) {
					if (out[i] == item) {
						collide = 1;
						break;
					}
				}
				if (collide)
					break;

				if (recurse_to_leaf) {
					if (item < 0) {
						crush_choose_indep(
							map,
							work,
							map->buckets[-1-item],
							weight, weight_max,
							x, 1, numrep, 0,
							out2, rep,
							recurse_tries, 0,
							0, NULL, r);
						if (out2[rep] == CRUSH_ITEM_NONE) {
							/* placed nothing; no leaf */
							break;
						}
					} else {
						/* we already have a leaf! */
						out2[rep] = item;
					}
				}

				/* out? */
				if (itemtype == 0 &&
				    is_out(map, weight, weight_max, item, x))
					break;

				/* yay! */
				out[rep] = item;
				left--;
				break;
			}
		}
	}
	for (rep = outpos; rep < endpos; rep++) {
		if (out[rep] == CRUSH_ITEM_UNDEF) {
			out[rep] = CRUSH_ITEM_NONE;
		}
		if (out2 && out2[rep] == CRUSH_ITEM_UNDEF) {
			out2[rep] = CRUSH_ITEM_NONE;
		}
	}
#ifndef __KERNEL__
	if (map->choose_tries && ftotal <= map->choose_total_tries)
		map->choose_tries[ftotal]++;
#endif
#ifdef DEBUG_INDEP
	if (out2) {
		dprintk("%u %d a: ", ftotal, left);
		for (rep = outpos; rep < endpos; rep++) {
			dprintk(" %d", out[rep]);
		}
		dprintk("\n");
		dprintk("%u %d b: ", ftotal, left);
		for (rep = outpos; rep < endpos; rep++) {
			dprintk(" %d", out2[rep]);
		}
		dprintk("\n");
	}
#endif
}


/*
 * This takes a chunk of memory and sets it up to be a shiny new
 * working area for a CRUSH placement computation. It must be called
 * on any newly allocated memory before passing it in to
 * crush_do_rule. It may be used repeatedly after that, so long as the
 * map has not changed. If the map /has/ changed, you must make sure
 * the working size is no smaller than what was allocated and re-run
 * crush_init_workspace.
 *
 * If you do retain the working space between calls to crush, make it
 * thread-local.
 */
void crush_init_workspace(const struct crush_map *map, void *v)
{
	struct crush_work *w = v;
	__s32 b;

	/*
	 * We work by moving through the available space and setting
	 * values and pointers as we go.
	 *
	 * It's a bit like Forth's use of the 'allot' word since we
	 * set the pointer first and then reserve the space for it to
	 * point to by incrementing the point.
	 */
	v += sizeof(struct crush_work *);
	w->work = v;
	v += map->max_buckets * sizeof(struct crush_work_bucket *);
	for (b = 0; b < map->max_buckets; ++b) {
		if (!map->buckets[b])
			continue;

		w->work[b] = v;
		switch (map->buckets[b]->alg) {
		default:
			v += sizeof(struct crush_work_bucket);
			break;
		}
		w->work[b]->perm_x = 0;
		w->work[b]->perm_n = 0;
		w->work[b]->perm = v;
		v += map->buckets[b]->size * sizeof(__u32);
	}
	BUG_ON(v - (void *)w != map->working_size);
}

/**
 * crush_do_rule - calculate a mapping with the given input and rule
 * @map: the crush_map
 * @ruleno: the rule id
 * @x: hash input
 * @result: pointer to result vector
 * @result_max: maximum result size
 * @weight: weight vector (for map leaves)
 * @weight_max: size of weight vector
 * @cwin: pointer to at least crush_work_size() bytes of memory
 */
int crush_do_rule(const struct crush_map *map,
		  int ruleno, int x, int *result, int result_max,
		  const __u32 *weight, int weight_max,
		  void *cwin)
{
	int result_len;
	struct crush_work *cw = cwin;
	int *a = cwin + map->working_size;
	int *b = a + result_max;
	int *c = b + result_max;
	int *w = a;
	int *o = b;
	int recurse_to_leaf;
	int wsize = 0;
	int osize;
	int *tmp;
	const struct crush_rule *rule;
	__u32 step;
	int i, j;
	int numrep;
	int out_size;
	/*
	 * the original choose_total_tries value was off by one (it
	 * counted "retries" and not "tries").  add one.
	 */
	int choose_tries = map->choose_total_tries + 1;
	int choose_leaf_tries = 0;
	/*
	 * the local tries values were counted as "retries", though,
	 * and need no adjustment
	 */
	int choose_local_retries = map->choose_local_tries;
	int choose_local_fallback_retries = map->choose_local_fallback_tries;

	int vary_r = map->chooseleaf_vary_r;
	int stable = map->chooseleaf_stable;

	if ((__u32)ruleno >= map->max_rules) {
		dprintk(" bad ruleno %d\n", ruleno);
		return 0;
	}

	rule = map->rules[ruleno];
	result_len = 0;

	for (step = 0; step < rule->len; step++) {
		int firstn = 0;
		const struct crush_rule_step *curstep = &rule->steps[step];

		switch (curstep->op) {
		case CRUSH_RULE_TAKE:
			if ((curstep->arg1 >= 0 &&
			     curstep->arg1 < map->max_devices) ||
			    (-1-curstep->arg1 >= 0 &&
			     -1-curstep->arg1 < map->max_buckets &&
			     map->buckets[-1-curstep->arg1])) {
				w[0] = curstep->arg1;
				wsize = 1;
			} else {
				dprintk(" bad take value %d\n", curstep->arg1);
			}
			break;

		case CRUSH_RULE_SET_CHOOSE_TRIES:
			if (curstep->arg1 > 0)
				choose_tries = curstep->arg1;
			break;

		case CRUSH_RULE_SET_CHOOSELEAF_TRIES:
			if (curstep->arg1 > 0)
				choose_leaf_tries = curstep->arg1;
			break;

		case CRUSH_RULE_SET_CHOOSE_LOCAL_TRIES:
			if (curstep->arg1 >= 0)
				choose_local_retries = curstep->arg1;
			break;

		case CRUSH_RULE_SET_CHOOSE_LOCAL_FALLBACK_TRIES:
			if (curstep->arg1 >= 0)
				choose_local_fallback_retries = curstep->arg1;
			break;

		case CRUSH_RULE_SET_CHOOSELEAF_VARY_R:
			if (curstep->arg1 >= 0)
				vary_r = curstep->arg1;
			break;

		case CRUSH_RULE_SET_CHOOSELEAF_STABLE:
			if (curstep->arg1 >= 0)
				stable = curstep->arg1;
			break;

		case CRUSH_RULE_CHOOSELEAF_FIRSTN:
		case CRUSH_RULE_CHOOSE_FIRSTN:
			firstn = 1;
			/* fall through */
		case CRUSH_RULE_CHOOSELEAF_INDEP:
		case CRUSH_RULE_CHOOSE_INDEP:
			if (wsize == 0)
				break;

			recurse_to_leaf =
				curstep->op ==
				 CRUSH_RULE_CHOOSELEAF_FIRSTN ||
				curstep->op ==
				CRUSH_RULE_CHOOSELEAF_INDEP;

			/* reset output */
			osize = 0;

			for (i = 0; i < wsize; i++) {
				int bno;
				/*
				 * see CRUSH_N, CRUSH_N_MINUS macros.
				 * basically, numrep <= 0 means relative to
				 * the provided result_max
				 */
				numrep = curstep->arg1;
				if (numrep <= 0) {
					numrep += result_max;
					if (numrep <= 0)
						continue;
				}
				j = 0;
				/* make sure bucket id is valid */
				bno = -1 - w[i];
				if (bno < 0 || bno >= map->max_buckets) {
					/* w[i] is probably CRUSH_ITEM_NONE */
					dprintk("  bad w[i] %d\n", w[i]);
					continue;
				}
				if (firstn) {
					int recurse_tries;
					if (choose_leaf_tries)
						recurse_tries =
							choose_leaf_tries;
					else if (map->chooseleaf_descend_once)
						recurse_tries = 1;
					else
						recurse_tries = choose_tries;
					osize += crush_choose_firstn(
						map,
						cw,
						map->buckets[bno],
						weight, weight_max,
						x, numrep,
						curstep->arg2,
						o+osize, j,
						result_max-osize,
						choose_tries,
						recurse_tries,
						choose_local_retries,
						choose_local_fallback_retries,
						recurse_to_leaf,
						vary_r,
						stable,
						c+osize,
						0);
				} else {
					out_size = ((numrep < (result_max-osize)) ?
						    numrep : (result_max-osize));
					crush_choose_indep(
						map,
						cw,
						map->buckets[bno],
						weight, weight_max,
						x, out_size, numrep,
						curstep->arg2,
						o+osize, j,
						choose_tries,
						choose_leaf_tries ?
						   choose_leaf_tries : 1,
						recurse_to_leaf,
						c+osize,
						0);
					osize += out_size;
				}
			}

			if (recurse_to_leaf)
				/* copy final _leaf_ values to output set */
				memcpy(o, c, osize*sizeof(*o));

			/* swap o and w arrays */
			tmp = o;
			o = w;
			w = tmp;
			wsize = osize;
			break;


		case CRUSH_RULE_EMIT:
			for (i = 0; i < wsize && result_len < result_max; i++) {
				result[result_len] = w[i];
				result_len++;
			}
			wsize = 0;
			break;

		default:
			dprintk(" unknown op %d at step %d\n",
				curstep->op, step);
			break;
		}
	}

	return result_len;
}
