#include <linux/module.h>
#include <linux/prio_tree.h>
#include <linux/random.h>
#include <asm/timex.h>

#define NODES        100
#define PERF_LOOPS   100000
#define SEARCHES     100
#define SEARCH_LOOPS 10000

static struct prio_tree_root root;
static struct prio_tree_node nodes[NODES];
static u32 queries[SEARCHES];

static struct rnd_state rnd;

static inline unsigned long
search(unsigned long query, struct prio_tree_root *root)
{
	struct prio_tree_iter iter;
	unsigned long results = 0;

	prio_tree_iter_init(&iter, root, query, query);
	while (prio_tree_next(&iter))
		results++;
	return results;
}

static void init(void)
{
	int i;
	for (i = 0; i < NODES; i++) {
		u32 a = prandom32(&rnd), b = prandom32(&rnd);
		if (a <= b) {
			nodes[i].start = a;
			nodes[i].last = b;
		} else {
			nodes[i].start = b;
			nodes[i].last = a;
		}
	}
	for (i = 0; i < SEARCHES; i++)
		queries[i] = prandom32(&rnd);
}

static int prio_tree_test_init(void)
{
	int i, j;
	unsigned long results;
	cycles_t time1, time2, time;

	printk(KERN_ALERT "prio tree insert/remove");

	prandom32_seed(&rnd, 3141592653589793238ULL);
	INIT_PRIO_TREE_ROOT(&root);
	init();

	time1 = get_cycles();

	for (i = 0; i < PERF_LOOPS; i++) {
		for (j = 0; j < NODES; j++)
			prio_tree_insert(&root, nodes + j);
		for (j = 0; j < NODES; j++)
			prio_tree_remove(&root, nodes + j);
	}

	time2 = get_cycles();
	time = time2 - time1;

	time = div_u64(time, PERF_LOOPS);
	printk(" -> %llu cycles\n", (unsigned long long)time);

	printk(KERN_ALERT "prio tree search");

	for (j = 0; j < NODES; j++)
		prio_tree_insert(&root, nodes + j);

	time1 = get_cycles();

	results = 0;
	for (i = 0; i < SEARCH_LOOPS; i++)
		for (j = 0; j < SEARCHES; j++)
			results += search(queries[j], &root);

	time2 = get_cycles();
	time = time2 - time1;

	time = div_u64(time, SEARCH_LOOPS);
	results = div_u64(results, SEARCH_LOOPS);
	printk(" -> %llu cycles (%lu results)\n",
	       (unsigned long long)time, results);

	return -EAGAIN; /* Fail will directly unload the module */
}

static void prio_tree_test_exit(void)
{
	printk(KERN_ALERT "test exit\n");
}

module_init(prio_tree_test_init)
module_exit(prio_tree_test_exit)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Michel Lespinasse");
MODULE_DESCRIPTION("Prio Tree test");
