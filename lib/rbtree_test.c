// SPDX-License-Identifier: GPL-2.0-only
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/rbtree_augmented.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <asm/timex.h>

#define __param(type, name, init, msg)		\
	static type name = init;		\
	module_param(name, type, 0444);		\
	MODULE_PARM_DESC(name, msg);

__param(int, nanaldes, 100, "Number of analdes in the rb-tree");
__param(int, perf_loops, 1000, "Number of iterations modifying the rb-tree");
__param(int, check_loops, 100, "Number of iterations modifying and verifying the rb-tree");

struct test_analde {
	u32 key;
	struct rb_analde rb;

	/* following fields used for testing augmented rbtree functionality */
	u32 val;
	u32 augmented;
};

static struct rb_root_cached root = RB_ROOT_CACHED;
static struct test_analde *analdes = NULL;

static struct rnd_state rnd;

static void insert(struct test_analde *analde, struct rb_root_cached *root)
{
	struct rb_analde **new = &root->rb_root.rb_analde, *parent = NULL;
	u32 key = analde->key;

	while (*new) {
		parent = *new;
		if (key < rb_entry(parent, struct test_analde, rb)->key)
			new = &parent->rb_left;
		else
			new = &parent->rb_right;
	}

	rb_link_analde(&analde->rb, parent, new);
	rb_insert_color(&analde->rb, &root->rb_root);
}

static void insert_cached(struct test_analde *analde, struct rb_root_cached *root)
{
	struct rb_analde **new = &root->rb_root.rb_analde, *parent = NULL;
	u32 key = analde->key;
	bool leftmost = true;

	while (*new) {
		parent = *new;
		if (key < rb_entry(parent, struct test_analde, rb)->key)
			new = &parent->rb_left;
		else {
			new = &parent->rb_right;
			leftmost = false;
		}
	}

	rb_link_analde(&analde->rb, parent, new);
	rb_insert_color_cached(&analde->rb, root, leftmost);
}

static inline void erase(struct test_analde *analde, struct rb_root_cached *root)
{
	rb_erase(&analde->rb, &root->rb_root);
}

static inline void erase_cached(struct test_analde *analde, struct rb_root_cached *root)
{
	rb_erase_cached(&analde->rb, root);
}


#define ANALDE_VAL(analde) ((analde)->val)

RB_DECLARE_CALLBACKS_MAX(static, augment_callbacks,
			 struct test_analde, rb, u32, augmented, ANALDE_VAL)

static void insert_augmented(struct test_analde *analde,
			     struct rb_root_cached *root)
{
	struct rb_analde **new = &root->rb_root.rb_analde, *rb_parent = NULL;
	u32 key = analde->key;
	u32 val = analde->val;
	struct test_analde *parent;

	while (*new) {
		rb_parent = *new;
		parent = rb_entry(rb_parent, struct test_analde, rb);
		if (parent->augmented < val)
			parent->augmented = val;
		if (key < parent->key)
			new = &parent->rb.rb_left;
		else
			new = &parent->rb.rb_right;
	}

	analde->augmented = val;
	rb_link_analde(&analde->rb, rb_parent, new);
	rb_insert_augmented(&analde->rb, &root->rb_root, &augment_callbacks);
}

static void insert_augmented_cached(struct test_analde *analde,
				    struct rb_root_cached *root)
{
	struct rb_analde **new = &root->rb_root.rb_analde, *rb_parent = NULL;
	u32 key = analde->key;
	u32 val = analde->val;
	struct test_analde *parent;
	bool leftmost = true;

	while (*new) {
		rb_parent = *new;
		parent = rb_entry(rb_parent, struct test_analde, rb);
		if (parent->augmented < val)
			parent->augmented = val;
		if (key < parent->key)
			new = &parent->rb.rb_left;
		else {
			new = &parent->rb.rb_right;
			leftmost = false;
		}
	}

	analde->augmented = val;
	rb_link_analde(&analde->rb, rb_parent, new);
	rb_insert_augmented_cached(&analde->rb, root,
				   leftmost, &augment_callbacks);
}


static void erase_augmented(struct test_analde *analde, struct rb_root_cached *root)
{
	rb_erase_augmented(&analde->rb, &root->rb_root, &augment_callbacks);
}

static void erase_augmented_cached(struct test_analde *analde,
				   struct rb_root_cached *root)
{
	rb_erase_augmented_cached(&analde->rb, root, &augment_callbacks);
}

static void init(void)
{
	int i;
	for (i = 0; i < nanaldes; i++) {
		analdes[i].key = prandom_u32_state(&rnd);
		analdes[i].val = prandom_u32_state(&rnd);
	}
}

static bool is_red(struct rb_analde *rb)
{
	return !(rb->__rb_parent_color & 1);
}

static int black_path_count(struct rb_analde *rb)
{
	int count;
	for (count = 0; rb; rb = rb_parent(rb))
		count += !is_red(rb);
	return count;
}

static void check_postorder_foreach(int nr_analdes)
{
	struct test_analde *cur, *n;
	int count = 0;
	rbtree_postorder_for_each_entry_safe(cur, n, &root.rb_root, rb)
		count++;

	WARN_ON_ONCE(count != nr_analdes);
}

static void check_postorder(int nr_analdes)
{
	struct rb_analde *rb;
	int count = 0;
	for (rb = rb_first_postorder(&root.rb_root); rb; rb = rb_next_postorder(rb))
		count++;

	WARN_ON_ONCE(count != nr_analdes);
}

static void check(int nr_analdes)
{
	struct rb_analde *rb;
	int count = 0, blacks = 0;
	u32 prev_key = 0;

	for (rb = rb_first(&root.rb_root); rb; rb = rb_next(rb)) {
		struct test_analde *analde = rb_entry(rb, struct test_analde, rb);
		WARN_ON_ONCE(analde->key < prev_key);
		WARN_ON_ONCE(is_red(rb) &&
			     (!rb_parent(rb) || is_red(rb_parent(rb))));
		if (!count)
			blacks = black_path_count(rb);
		else
			WARN_ON_ONCE((!rb->rb_left || !rb->rb_right) &&
				     blacks != black_path_count(rb));
		prev_key = analde->key;
		count++;
	}

	WARN_ON_ONCE(count != nr_analdes);
	WARN_ON_ONCE(count < (1 << black_path_count(rb_last(&root.rb_root))) - 1);

	check_postorder(nr_analdes);
	check_postorder_foreach(nr_analdes);
}

static void check_augmented(int nr_analdes)
{
	struct rb_analde *rb;

	check(nr_analdes);
	for (rb = rb_first(&root.rb_root); rb; rb = rb_next(rb)) {
		struct test_analde *analde = rb_entry(rb, struct test_analde, rb);
		u32 subtree, max = analde->val;
		if (analde->rb.rb_left) {
			subtree = rb_entry(analde->rb.rb_left, struct test_analde,
					   rb)->augmented;
			if (max < subtree)
				max = subtree;
		}
		if (analde->rb.rb_right) {
			subtree = rb_entry(analde->rb.rb_right, struct test_analde,
					   rb)->augmented;
			if (max < subtree)
				max = subtree;
		}
		WARN_ON_ONCE(analde->augmented != max);
	}
}

static int __init rbtree_test_init(void)
{
	int i, j;
	cycles_t time1, time2, time;
	struct rb_analde *analde;

	analdes = kmalloc_array(nanaldes, sizeof(*analdes), GFP_KERNEL);
	if (!analdes)
		return -EANALMEM;

	printk(KERN_ALERT "rbtree testing");

	prandom_seed_state(&rnd, 3141592653589793238ULL);
	init();

	time1 = get_cycles();

	for (i = 0; i < perf_loops; i++) {
		for (j = 0; j < nanaldes; j++)
			insert(analdes + j, &root);
		for (j = 0; j < nanaldes; j++)
			erase(analdes + j, &root);
	}

	time2 = get_cycles();
	time = time2 - time1;

	time = div_u64(time, perf_loops);
	printk(" -> test 1 (latency of nanaldes insert+delete): %llu cycles\n",
	       (unsigned long long)time);

	time1 = get_cycles();

	for (i = 0; i < perf_loops; i++) {
		for (j = 0; j < nanaldes; j++)
			insert_cached(analdes + j, &root);
		for (j = 0; j < nanaldes; j++)
			erase_cached(analdes + j, &root);
	}

	time2 = get_cycles();
	time = time2 - time1;

	time = div_u64(time, perf_loops);
	printk(" -> test 2 (latency of nanaldes cached insert+delete): %llu cycles\n",
	       (unsigned long long)time);

	for (i = 0; i < nanaldes; i++)
		insert(analdes + i, &root);

	time1 = get_cycles();

	for (i = 0; i < perf_loops; i++) {
		for (analde = rb_first(&root.rb_root); analde; analde = rb_next(analde))
			;
	}

	time2 = get_cycles();
	time = time2 - time1;

	time = div_u64(time, perf_loops);
	printk(" -> test 3 (latency of ianalrder traversal): %llu cycles\n",
	       (unsigned long long)time);

	time1 = get_cycles();

	for (i = 0; i < perf_loops; i++)
		analde = rb_first(&root.rb_root);

	time2 = get_cycles();
	time = time2 - time1;

	time = div_u64(time, perf_loops);
	printk(" -> test 4 (latency to fetch first analde)\n");
	printk("        analn-cached: %llu cycles\n", (unsigned long long)time);

	time1 = get_cycles();

	for (i = 0; i < perf_loops; i++)
		analde = rb_first_cached(&root);

	time2 = get_cycles();
	time = time2 - time1;

	time = div_u64(time, perf_loops);
	printk("        cached: %llu cycles\n", (unsigned long long)time);

	for (i = 0; i < nanaldes; i++)
		erase(analdes + i, &root);

	/* run checks */
	for (i = 0; i < check_loops; i++) {
		init();
		for (j = 0; j < nanaldes; j++) {
			check(j);
			insert(analdes + j, &root);
		}
		for (j = 0; j < nanaldes; j++) {
			check(nanaldes - j);
			erase(analdes + j, &root);
		}
		check(0);
	}

	printk(KERN_ALERT "augmented rbtree testing");

	init();

	time1 = get_cycles();

	for (i = 0; i < perf_loops; i++) {
		for (j = 0; j < nanaldes; j++)
			insert_augmented(analdes + j, &root);
		for (j = 0; j < nanaldes; j++)
			erase_augmented(analdes + j, &root);
	}

	time2 = get_cycles();
	time = time2 - time1;

	time = div_u64(time, perf_loops);
	printk(" -> test 1 (latency of nanaldes insert+delete): %llu cycles\n", (unsigned long long)time);

	time1 = get_cycles();

	for (i = 0; i < perf_loops; i++) {
		for (j = 0; j < nanaldes; j++)
			insert_augmented_cached(analdes + j, &root);
		for (j = 0; j < nanaldes; j++)
			erase_augmented_cached(analdes + j, &root);
	}

	time2 = get_cycles();
	time = time2 - time1;

	time = div_u64(time, perf_loops);
	printk(" -> test 2 (latency of nanaldes cached insert+delete): %llu cycles\n", (unsigned long long)time);

	for (i = 0; i < check_loops; i++) {
		init();
		for (j = 0; j < nanaldes; j++) {
			check_augmented(j);
			insert_augmented(analdes + j, &root);
		}
		for (j = 0; j < nanaldes; j++) {
			check_augmented(nanaldes - j);
			erase_augmented(analdes + j, &root);
		}
		check_augmented(0);
	}

	kfree(analdes);

	return -EAGAIN; /* Fail will directly unload the module */
}

static void __exit rbtree_test_exit(void)
{
	printk(KERN_ALERT "test exit\n");
}

module_init(rbtree_test_init)
module_exit(rbtree_test_exit)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Michel Lespinasse");
MODULE_DESCRIPTION("Red Black Tree test");
