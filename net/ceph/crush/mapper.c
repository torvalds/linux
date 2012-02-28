
#ifdef __KERNEL__
# include <linux/string.h>
# include <linux/slab.h>
# include <linux/bug.h>
# include <linux/kernel.h>
# ifndef dprintk
#  define dprintk(args...)
# endif
#else
# include <string.h>
# include <stdio.h>
# include <stdlib.h>
# include <assert.h>
# define BUG_ON(x) assert(!(x))
# define dprintk(args...) /* printf(args) */
# define kmalloc(x, f) malloc(x)
# define kfree(x) free(x)
#endif

#include <linux/crush/crush.h>
#include <linux/crush/hash.h>

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
int crush_find_rule(struct crush_map *map, int ruleset, int type, int size)
{
	int i;

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
static int bucket_perm_choose(struct crush_bucket *bucket,
			      int x, int r)
{
	unsigned pr = r % bucket->size;
	unsigned i, s;

	/* start a new permutation if @x has changed */
	if (bucket->perm_x != x || bucket->perm_n == 0) {
		dprintk("bucket %d new x=%d\n", bucket->id, x);
		bucket->perm_x = x;

		/* optimize common r=0 case */
		if (pr == 0) {
			s = crush_hash32_3(bucket->hash, x, bucket->id, 0) %
				bucket->size;
			bucket->perm[0] = s;
			bucket->perm_n = 0xffff;   /* magic value, see below */
			goto out;
		}

		for (i = 0; i < bucket->size; i++)
			bucket->perm[i] = i;
		bucket->perm_n = 0;
	} else if (bucket->perm_n == 0xffff) {
		/* clean up after the r=0 case above */
		for (i = 1; i < bucket->size; i++)
			bucket->perm[i] = i;
		bucket->perm[bucket->perm[0]] = 0;
		bucket->perm_n = 1;
	}

	/* calculate permutation up to pr */
	for (i = 0; i < bucket->perm_n; i++)
		dprintk(" perm_choose have %d: %d\n", i, bucket->perm[i]);
	while (bucket->perm_n <= pr) {
		unsigned p = bucket->perm_n;
		/* no point in swapping the final entry */
		if (p < bucket->size - 1) {
			i = crush_hash32_3(bucket->hash, x, bucket->id, p) %
				(bucket->size - p);
			if (i) {
				unsigned t = bucket->perm[p + i];
				bucket->perm[p + i] = bucket->perm[p];
				bucket->perm[p] = t;
			}
			dprintk(" perm_choose swap %d with %d\n", p, p+i);
		}
		bucket->perm_n++;
	}
	for (i = 0; i < bucket->size; i++)
		dprintk(" perm_choose  %d: %d\n", i, bucket->perm[i]);

	s = bucket->perm[pr];
out:
	dprintk(" perm_choose %d sz=%d x=%d r=%d (%d) s=%d\n", bucket->id,
		bucket->size, x, r, pr, s);
	return bucket->items[s];
}

/* uniform */
static int bucket_uniform_choose(struct crush_bucket_uniform *bucket,
				 int x, int r)
{
	return bucket_perm_choose(&bucket->h, x, r);
}

/* list */
static int bucket_list_choose(struct crush_bucket_list *bucket,
			      int x, int r)
{
	int i;

	for (i = bucket->h.size-1; i >= 0; i--) {
		__u64 w = crush_hash32_4(bucket->h.hash,x, bucket->h.items[i],
					 r, bucket->h.id);
		w &= 0xffff;
		dprintk("list_choose i=%d x=%d r=%d item %d weight %x "
			"sw %x rand %llx",
			i, x, r, bucket->h.items[i], bucket->item_weights[i],
			bucket->sum_weights[i], w);
		w *= bucket->sum_weights[i];
		w = w >> 16;
		/*dprintk(" scaled %llx\n", w);*/
		if (w < bucket->item_weights[i])
			return bucket->h.items[i];
	}

	BUG_ON(1);
	return 0;
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

static int bucket_tree_choose(struct crush_bucket_tree *bucket,
			      int x, int r)
{
	int n, l;
	__u32 w;
	__u64 t;

	/* start at root */
	n = bucket->num_nodes >> 1;

	while (!terminal(n)) {
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

static int bucket_straw_choose(struct crush_bucket_straw *bucket,
			       int x, int r)
{
	int i;
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

static int crush_bucket_choose(struct crush_bucket *in, int x, int r)
{
	dprintk(" crush_bucket_choose %d x=%d r=%d\n", in->id, x, r);
	switch (in->alg) {
	case CRUSH_BUCKET_UNIFORM:
		return bucket_uniform_choose((struct crush_bucket_uniform *)in,
					  x, r);
	case CRUSH_BUCKET_LIST:
		return bucket_list_choose((struct crush_bucket_list *)in,
					  x, r);
	case CRUSH_BUCKET_TREE:
		return bucket_tree_choose((struct crush_bucket_tree *)in,
					  x, r);
	case CRUSH_BUCKET_STRAW:
		return bucket_straw_choose((struct crush_bucket_straw *)in,
					   x, r);
	default:
		BUG_ON(1);
		return in->items[0];
	}
}

/*
 * true if device is marked "out" (failed, fully offloaded)
 * of the cluster
 */
static int is_out(struct crush_map *map, __u32 *weight, int item, int x)
{
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
 * crush_choose - choose numrep distinct items of given type
 * @map: the crush_map
 * @bucket: the bucket we are choose an item from
 * @x: crush input value
 * @numrep: the number of items to choose
 * @type: the type of item to choose
 * @out: pointer to output vector
 * @outpos: our position in that vector
 * @firstn: true if choosing "first n" items, false if choosing "indep"
 * @recurse_to_leaf: true if we want one device under each item of given type
 * @out2: second output vector for leaf items (if @recurse_to_leaf)
 */
static int crush_choose(struct crush_map *map,
			struct crush_bucket *bucket,
			__u32 *weight,
			int x, int numrep, int type,
			int *out, int outpos,
			int firstn, int recurse_to_leaf,
			int *out2)
{
	int rep;
	int ftotal, flocal;
	int retry_descent, retry_bucket, skip_rep;
	struct crush_bucket *in = bucket;
	int r;
	int i;
	int item = 0;
	int itemtype;
	int collide, reject;
	const int orig_tries = 5; /* attempts before we fall back to search */

	dprintk("CHOOSE%s bucket %d x %d outpos %d numrep %d\n", recurse_to_leaf ? "_LEAF" : "",
		bucket->id, x, outpos, numrep);

	for (rep = outpos; rep < numrep; rep++) {
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
				r = rep;
				if (in->alg == CRUSH_BUCKET_UNIFORM) {
					/* be careful */
					if (firstn || numrep >= in->size)
						/* r' = r + f_total */
						r += ftotal;
					else if (in->size % numrep == 0)
						/* r'=r+(n+1)*f_local */
						r += (numrep+1) *
							(flocal+ftotal);
					else
						/* r' = r + n*f_local */
						r += numrep * (flocal+ftotal);
				} else {
					if (firstn)
						/* r' = r + f_total */
						r += ftotal;
					else
						/* r' = r + n*f_local */
						r += numrep * (flocal+ftotal);
				}

				/* bucket choose */
				if (in->size == 0) {
					reject = 1;
					goto reject;
				}
				if (flocal >= (in->size>>1) &&
				    flocal > orig_tries)
					item = bucket_perm_choose(in, x, r);
				else
					item = crush_bucket_choose(in, x, r);
				BUG_ON(item >= map->max_devices);

				/* desired type? */
				if (item < 0)
					itemtype = map->buckets[-1-item]->type;
				else
					itemtype = 0;
				dprintk("  item %d type %d\n", item, itemtype);

				/* keep going? */
				if (itemtype != type) {
					BUG_ON(item >= 0 ||
					       (-1-item) >= map->max_buckets);
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
				if (recurse_to_leaf) {
					if (item < 0) {
						if (crush_choose(map,
							 map->buckets[-1-item],
							 weight,
							 x, outpos+1, 0,
							 out2, outpos,
							 firstn, 0,
							 NULL) <= outpos)
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
								item, x);
					else
						reject = 0;
				}

reject:
				if (reject || collide) {
					ftotal++;
					flocal++;

					if (collide && flocal < 3)
						/* retry locally a few times */
						retry_bucket = 1;
					else if (flocal < in->size + orig_tries)
						/* exhaustive bucket search */
						retry_bucket = 1;
					else if (ftotal < 20)
						/* then retry descent */
						retry_descent = 1;
					else
						/* else give up */
						skip_rep = 1;
					dprintk("  reject %d  collide %d  "
						"ftotal %d  flocal %d\n",
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
	}

	dprintk("CHOOSE returns %d\n", outpos);
	return outpos;
}


/**
 * crush_do_rule - calculate a mapping with the given input and rule
 * @map: the crush_map
 * @ruleno: the rule id
 * @x: hash input
 * @result: pointer to result vector
 * @result_max: maximum result size
 * @force: force initial replica choice; -1 for none
 */
int crush_do_rule(struct crush_map *map,
		  int ruleno, int x, int *result, int result_max,
		  int force, __u32 *weight)
{
	int result_len;
	int force_context[CRUSH_MAX_DEPTH];
	int force_pos = -1;
	int a[CRUSH_MAX_SET];
	int b[CRUSH_MAX_SET];
	int c[CRUSH_MAX_SET];
	int recurse_to_leaf;
	int *w;
	int wsize = 0;
	int *o;
	int osize;
	int *tmp;
	struct crush_rule *rule;
	int step;
	int i, j;
	int numrep;
	int firstn;

	BUG_ON(ruleno >= map->max_rules);

	rule = map->rules[ruleno];
	result_len = 0;
	w = a;
	o = b;

	/*
	 * determine hierarchical context of force, if any.  note
	 * that this may or may not correspond to the specific types
	 * referenced by the crush rule.
	 */
	if (force >= 0 &&
	    force < map->max_devices &&
	    map->device_parents[force] != 0 &&
	    !is_out(map, weight, force, x)) {
		while (1) {
			force_context[++force_pos] = force;
			if (force >= 0)
				force = map->device_parents[force];
			else
				force = map->bucket_parents[-1-force];
			if (force == 0)
				break;
		}
	}

	for (step = 0; step < rule->len; step++) {
		firstn = 0;
		switch (rule->steps[step].op) {
		case CRUSH_RULE_TAKE:
			w[0] = rule->steps[step].arg1;

			/* find position in force_context/hierarchy */
			while (force_pos >= 0 &&
			       force_context[force_pos] != w[0])
				force_pos--;
			/* and move past it */
			if (force_pos >= 0)
				force_pos--;

			wsize = 1;
			break;

		case CRUSH_RULE_CHOOSE_LEAF_FIRSTN:
		case CRUSH_RULE_CHOOSE_FIRSTN:
			firstn = 1;
		case CRUSH_RULE_CHOOSE_LEAF_INDEP:
		case CRUSH_RULE_CHOOSE_INDEP:
			BUG_ON(wsize == 0);

			recurse_to_leaf =
				rule->steps[step].op ==
				 CRUSH_RULE_CHOOSE_LEAF_FIRSTN ||
				rule->steps[step].op ==
				CRUSH_RULE_CHOOSE_LEAF_INDEP;

			/* reset output */
			osize = 0;

			for (i = 0; i < wsize; i++) {
				/*
				 * see CRUSH_N, CRUSH_N_MINUS macros.
				 * basically, numrep <= 0 means relative to
				 * the provided result_max
				 */
				numrep = rule->steps[step].arg1;
				if (numrep <= 0) {
					numrep += result_max;
					if (numrep <= 0)
						continue;
				}
				j = 0;
				if (osize == 0 && force_pos >= 0) {
					/* skip any intermediate types */
					while (force_pos &&
					       force_context[force_pos] < 0 &&
					       rule->steps[step].arg2 !=
					       map->buckets[-1 -
					       force_context[force_pos]]->type)
						force_pos--;
					o[osize] = force_context[force_pos];
					if (recurse_to_leaf)
						c[osize] = force_context[0];
					j++;
					force_pos--;
				}
				osize += crush_choose(map,
						      map->buckets[-1-w[i]],
						      weight,
						      x, numrep,
						      rule->steps[step].arg2,
						      o+osize, j,
						      firstn,
						      recurse_to_leaf, c+osize);
			}

			if (recurse_to_leaf)
				/* copy final _leaf_ values to output set */
				memcpy(o, c, osize*sizeof(*o));

			/* swap t and w arrays */
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
			BUG_ON(1);
		}
	}
	return result_len;
}


