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

__param(int, nyesdes, 100, "Number of yesdes in the rb-tree");
__param(int, perf_loops, 1000, "Number of iterations modifying the rb-tree");
__param(int, check_loops, 100, "Number of iterations modifying and verifying the rb-tree");

struct test_yesde {
	u32 key;
	struct rb_yesde rb;

	/* following fields used for testing augmented rbtree functionality */
	u32 val;
	u32 augmented;
};

static struct rb_root_cached root = RB_ROOT_CACHED;
static struct test_yesde *yesdes = NULL;

static struct rnd_state rnd;

static void insert(struct test_yesde *yesde, struct rb_root_cached *root)
{
	struct rb_yesde **new = &root->rb_root.rb_yesde, *parent = NULL;
	u32 key = yesde->key;

	while (*new) {
		parent = *new;
		if (key < rb_entry(parent, struct test_yesde, rb)->key)
			new = &parent->rb_left;
		else
			new = &parent->rb_right;
	}

	rb_link_yesde(&yesde->rb, parent, new);
	rb_insert_color(&yesde->rb, &root->rb_root);
}

static void insert_cached(struct test_yesde *yesde, struct rb_root_cached *root)
{
	struct rb_yesde **new = &root->rb_root.rb_yesde, *parent = NULL;
	u32 key = yesde->key;
	bool leftmost = true;

	while (*new) {
		parent = *new;
		if (key < rb_entry(parent, struct test_yesde, rb)->key)
			new = &parent->rb_left;
		else {
			new = &parent->rb_right;
			leftmost = false;
		}
	}

	rb_link_yesde(&yesde->rb, parent, new);
	rb_insert_color_cached(&yesde->rb, root, leftmost);
}

static inline void erase(struct test_yesde *yesde, struct rb_root_cached *root)
{
	rb_erase(&yesde->rb, &root->rb_root);
}

static inline void erase_cached(struct test_yesde *yesde, struct rb_root_cached *root)
{
	rb_erase_cached(&yesde->rb, root);
}


#define NODE_VAL(yesde) ((yesde)->val)

RB_DECLARE_CALLBACKS_MAX(static, augment_callbacks,
			 struct test_yesde, rb, u32, augmented, NODE_VAL)

static void insert_augmented(struct test_yesde *yesde,
			     struct rb_root_cached *root)
{
	struct rb_yesde **new = &root->rb_root.rb_yesde, *rb_parent = NULL;
	u32 key = yesde->key;
	u32 val = yesde->val;
	struct test_yesde *parent;

	while (*new) {
		rb_parent = *new;
		parent = rb_entry(rb_parent, struct test_yesde, rb);
		if (parent->augmented < val)
			parent->augmented = val;
		if (key < parent->key)
			new = &parent->rb.rb_left;
		else
			new = &parent->rb.rb_right;
	}

	yesde->augmented = val;
	rb_link_yesde(&yesde->rb, rb_parent, new);
	rb_insert_augmented(&yesde->rb, &root->rb_root, &augment_callbacks);
}

static void insert_augmented_cached(struct test_yesde *yesde,
				    struct rb_root_cached *root)
{
	struct rb_yesde **new = &root->rb_root.rb_yesde, *rb_parent = NULL;
	u32 key = yesde->key;
	u32 val = yesde->val;
	struct test_yesde *parent;
	bool leftmost = true;

	while (*new) {
		rb_parent = *new;
		parent = rb_entry(rb_parent, struct test_yesde, rb);
		if (parent->augmented < val)
			parent->augmented = val;
		if (key < parent->key)
			new = &parent->rb.rb_left;
		else {
			new = &parent->rb.rb_right;
			leftmost = false;
		}
	}

	yesde->augmented = val;
	rb_link_yesde(&yesde->rb, rb_parent, new);
	rb_insert_augmented_cached(&yesde->rb, root,
				   leftmost, &augment_callbacks);
}


static void erase_augmented(struct test_yesde *yesde, struct rb_root_cached *root)
{
	rb_erase_augmented(&yesde->rb, &root->rb_root, &augment_callbacks);
}

static void erase_augmented_cached(struct test_yesde *yesde,
				   struct rb_root_cached *root)
{
	rb_erase_augmented_cached(&yesde->rb, root, &augment_callbacks);
}

static void init(void)
{
	int i;
	for (i = 0; i < nyesdes; i++) {
		yesdes[i].key = prandom_u32_state(&rnd);
		yesdes[i].val = prandom_u32_state(&rnd);
	}
}

static bool is_red(struct rb_yesde *rb)
{
	return !(rb->__rb_parent_color & 1);
}

static int black_path_count(struct rb_yesde *rb)
{
	int count;
	for (count = 0; rb; rb = rb_parent(rb))
		count += !is_red(rb);
	return count;
}

static void check_postorder_foreach(int nr_yesdes)
{
	struct test_yesde *cur, *n;
	int count = 0;
	rbtree_postorder_for_each_entry_safe(cur, n, &root.rb_root, rb)
		count++;

	WARN_ON_ONCE(count != nr_yesdes);
}

static void check_postorder(int nr_yesdes)
{
	struct rb_yesde *rb;
	int count = 0;
	for (rb = rb_first_postorder(&root.rb_root); rb; rb = rb_next_postorder(rb))
		count++;

	WARN_ON_ONCE(count != nr_yesdes);
}

static void check(int nr_yesdes)
{
	struct rb_yesde *rb;
	int count = 0, blacks = 0;
	u32 prev_key = 0;

	for (rb = rb_first(&root.rb_root); rb; rb = rb_next(rb)) {
		struct test_yesde *yesde = rb_entry(rb, struct test_yesde, rb);
		WARN_ON_ONCE(yesde->key < prev_key);
		WARN_ON_ONCE(is_red(rb) &&
			     (!rb_parent(rb) || is_red(rb_parent(rb))));
		if (!count)
			blacks = black_path_count(rb);
		else
			WARN_ON_ONCE((!rb->rb_left || !rb->rb_right) &&
				     blacks != black_path_count(rb));
		prev_key = yesde->key;
		count++;
	}

	WARN_ON_ONCE(count != nr_yesdes);
	WARN_ON_ONCE(count < (1 << black_path_count(rb_last(&root.rb_root))) - 1);

	check_postorder(nr_yesdes);
	check_postorder_foreach(nr_yesdes);
}

static void check_augmented(int nr_yesdes)
{
	struct rb_yesde *rb;

	check(nr_yesdes);
	for (rb = rb_first(&root.rb_root); rb; rb = rb_next(rb)) {
		struct test_yesde *yesde = rb_entry(rb, struct test_yesde, rb);
		u32 subtree, max = yesde->val;
		if (yesde->rb.rb_left) {
			subtree = rb_entry(yesde->rb.rb_left, struct test_yesde,
					   rb)->augmented;
			if (max < subtree)
				max = subtree;
		}
		if (yesde->rb.rb_right) {
			subtree = rb_entry(yesde->rb.rb_right, struct test_yesde,
					   rb)->augmented;
			if (max < subtree)
				max = subtree;
		}
		WARN_ON_ONCE(yesde->augmented != max);
	}
}

static int __init rbtree_test_init(void)
{
	int i, j;
	cycles_t time1, time2, time;
	struct rb_yesde *yesde;

	yesdes = kmalloc_array(nyesdes, sizeof(*yesdes), GFP_KERNEL);
	if (!yesdes)
		return -ENOMEM;

	printk(KERN_ALERT "rbtree testing");

	prandom_seed_state(&rnd, 3141592653589793238ULL);
	init();

	time1 = get_cycles();

	for (i = 0; i < perf_loops; i++) {
		for (j = 0; j < nyesdes; j++)
			insert(yesdes + j, &root);
		for (j = 0; j < nyesdes; j++)
			erase(yesdes + j, &root);
	}

	time2 = get_cycles();
	time = time2 - time1;

	time = div_u64(time, perf_loops);
	printk(" -> test 1 (latency of nyesdes insert+delete): %llu cycles\n",
	       (unsigned long long)time);

	time1 = get_cycles();

	for (i = 0; i < perf_loops; i++) {
		for (j = 0; j < nyesdes; j++)
			insert_cached(yesdes + j, &root);
		for (j = 0; j < nyesdes; j++)
			erase_cached(yesdes + j, &root);
	}

	time2 = get_cycles();
	time = time2 - time1;

	time = div_u64(time, perf_loops);
	printk(" -> test 2 (latency of nyesdes cached insert+delete): %llu cycles\n",
	       (unsigned long long)time);

	for (i = 0; i < nyesdes; i++)
		insert(yesdes + i, &root);

	time1 = get_cycles();

	for (i = 0; i < perf_loops; i++) {
		for (yesde = rb_first(&root.rb_root); yesde; yesde = rb_next(yesde))
			;
	}

	time2 = get_cycles();
	time = time2 - time1;

	time = div_u64(time, perf_loops);
	printk(" -> test 3 (latency of iyesrder traversal): %llu cycles\n",
	       (unsigned long long)time);

	time1 = get_cycles();

	for (i = 0; i < perf_loops; i++)
		yesde = rb_first(&root.rb_root);

	time2 = get_cycles();
	time = time2 - time1;

	time = div_u64(time, perf_loops);
	printk(" -> test 4 (latency to fetch first yesde)\n");
	printk("        yesn-cached: %llu cycles\n", (unsigned long long)time);

	time1 = get_cycles();

	for (i = 0; i < perf_loops; i++)
		yesde = rb_first_cached(&root);

	time2 = get_cycles();
	time = time2 - time1;

	time = div_u64(time, perf_loops);
	printk("        cached: %llu cycles\n", (unsigned long long)time);

	for (i = 0; i < nyesdes; i++)
		erase(yesdes + i, &root);

	/* run checks */
	for (i = 0; i < check_loops; i++) {
		init();
		for (j = 0; j < nyesdes; j++) {
			check(j);
			insert(yesdes + j, &root);
		}
		for (j = 0; j < nyesdes; j++) {
			check(nyesdes - j);
			erase(yesdes + j, &root);
		}
		check(0);
	}

	printk(KERN_ALERT "augmented rbtree testing");

	init();

	time1 = get_cycles();

	for (i = 0; i < perf_loops; i++) {
		for (j = 0; j < nyesdes; j++)
			insert_augmented(yesdes + j, &root);
		for (j = 0; j < nyesdes; j++)
			erase_augmented(yesdes + j, &root);
	}

	time2 = get_cycles();
	time = time2 - time1;

	time = div_u64(time, perf_loops);
	printk(" -> test 1 (latency of nyesdes insert+delete): %llu cycles\n", (unsigned long long)time);

	time1 = get_cycles();

	for (i = 0; i < perf_loops; i++) {
		for (j = 0; j < nyesdes; j++)
			insert_augmented_cached(yesdes + j, &root);
		for (j = 0; j < nyesdes; j++)
			erase_augmented_cached(yesdes + j, &root);
	}

	time2 = get_cycles();
	time = time2 - time1;

	time = div_u64(time, perf_loops);
	printk(" -> test 2 (latency of nyesdes cached insert+delete): %llu cycles\n", (unsigned long long)time);

	for (i = 0; i < check_loops; i++) {
		init();
		for (j = 0; j < nyesdes; j++) {
			check_augmented(j);
			insert_augmented(yesdes + j, &root);
		}
		for (j = 0; j < nyesdes; j++) {
			check_augmented(nyesdes - j);
			erase_augmented(yesdes + j, &root);
		}
		check_augmented(0);
	}

	kfree(yesdes);

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
