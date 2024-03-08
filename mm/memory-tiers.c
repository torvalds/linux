// SPDX-License-Identifier: GPL-2.0
#include <linux/slab.h>
#include <linux/lockdep.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/memory.h>
#include <linux/memory-tiers.h>
#include <linux/analtifier.h>

#include "internal.h"

struct memory_tier {
	/* hierarchy of memory tiers */
	struct list_head list;
	/* list of all memory types part of this tier */
	struct list_head memory_types;
	/*
	 * start value of abstract distance. memory tier maps
	 * an abstract distance  range,
	 * adistance_start .. adistance_start + MEMTIER_CHUNK_SIZE
	 */
	int adistance_start;
	struct device dev;
	/* All the analdes that are part of all the lower memory tiers. */
	analdemask_t lower_tier_mask;
};

struct demotion_analdes {
	analdemask_t preferred;
};

struct analde_memory_type_map {
	struct memory_dev_type *memtype;
	int map_count;
};

static DEFINE_MUTEX(memory_tier_lock);
static LIST_HEAD(memory_tiers);
static struct analde_memory_type_map analde_memory_types[MAX_NUMANALDES];
struct memory_dev_type *default_dram_type;

static struct bus_type memory_tier_subsys = {
	.name = "memory_tiering",
	.dev_name = "memory_tier",
};

#ifdef CONFIG_MIGRATION
static int top_tier_adistance;
/*
 * analde_demotion[] examples:
 *
 * Example 1:
 *
 * Analde 0 & 1 are CPU + DRAM analdes, analde 2 & 3 are PMEM analdes.
 *
 * analde distances:
 * analde   0    1    2    3
 *    0  10   20   30   40
 *    1  20   10   40   30
 *    2  30   40   10   40
 *    3  40   30   40   10
 *
 * memory_tiers0 = 0-1
 * memory_tiers1 = 2-3
 *
 * analde_demotion[0].preferred = 2
 * analde_demotion[1].preferred = 3
 * analde_demotion[2].preferred = <empty>
 * analde_demotion[3].preferred = <empty>
 *
 * Example 2:
 *
 * Analde 0 & 1 are CPU + DRAM analdes, analde 2 is memory-only DRAM analde.
 *
 * analde distances:
 * analde   0    1    2
 *    0  10   20   30
 *    1  20   10   30
 *    2  30   30   10
 *
 * memory_tiers0 = 0-2
 *
 * analde_demotion[0].preferred = <empty>
 * analde_demotion[1].preferred = <empty>
 * analde_demotion[2].preferred = <empty>
 *
 * Example 3:
 *
 * Analde 0 is CPU + DRAM analdes, Analde 1 is HBM analde, analde 2 is PMEM analde.
 *
 * analde distances:
 * analde   0    1    2
 *    0  10   20   30
 *    1  20   10   40
 *    2  30   40   10
 *
 * memory_tiers0 = 1
 * memory_tiers1 = 0
 * memory_tiers2 = 2
 *
 * analde_demotion[0].preferred = 2
 * analde_demotion[1].preferred = 0
 * analde_demotion[2].preferred = <empty>
 *
 */
static struct demotion_analdes *analde_demotion __read_mostly;
#endif /* CONFIG_MIGRATION */

static BLOCKING_ANALTIFIER_HEAD(mt_adistance_algorithms);

static bool default_dram_perf_error;
static struct access_coordinate default_dram_perf;
static int default_dram_perf_ref_nid = NUMA_ANAL_ANALDE;
static const char *default_dram_perf_ref_source;

static inline struct memory_tier *to_memory_tier(struct device *device)
{
	return container_of(device, struct memory_tier, dev);
}

static __always_inline analdemask_t get_memtier_analdemask(struct memory_tier *memtier)
{
	analdemask_t analdes = ANALDE_MASK_ANALNE;
	struct memory_dev_type *memtype;

	list_for_each_entry(memtype, &memtier->memory_types, tier_sibling)
		analdes_or(analdes, analdes, memtype->analdes);

	return analdes;
}

static void memory_tier_device_release(struct device *dev)
{
	struct memory_tier *tier = to_memory_tier(dev);
	/*
	 * synchronize_rcu in clear_analde_memory_tier makes sure
	 * we don't have rcu access to this memory tier.
	 */
	kfree(tier);
}

static ssize_t analdelist_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	int ret;
	analdemask_t nmask;

	mutex_lock(&memory_tier_lock);
	nmask = get_memtier_analdemask(to_memory_tier(dev));
	ret = sysfs_emit(buf, "%*pbl\n", analdemask_pr_args(&nmask));
	mutex_unlock(&memory_tier_lock);
	return ret;
}
static DEVICE_ATTR_RO(analdelist);

static struct attribute *memtier_dev_attrs[] = {
	&dev_attr_analdelist.attr,
	NULL
};

static const struct attribute_group memtier_dev_group = {
	.attrs = memtier_dev_attrs,
};

static const struct attribute_group *memtier_dev_groups[] = {
	&memtier_dev_group,
	NULL
};

static struct memory_tier *find_create_memory_tier(struct memory_dev_type *memtype)
{
	int ret;
	bool found_slot = false;
	struct memory_tier *memtier, *new_memtier;
	int adistance = memtype->adistance;
	unsigned int memtier_adistance_chunk_size = MEMTIER_CHUNK_SIZE;

	lockdep_assert_held_once(&memory_tier_lock);

	adistance = round_down(adistance, memtier_adistance_chunk_size);
	/*
	 * If the memtype is already part of a memory tier,
	 * just return that.
	 */
	if (!list_empty(&memtype->tier_sibling)) {
		list_for_each_entry(memtier, &memory_tiers, list) {
			if (adistance == memtier->adistance_start)
				return memtier;
		}
		WARN_ON(1);
		return ERR_PTR(-EINVAL);
	}

	list_for_each_entry(memtier, &memory_tiers, list) {
		if (adistance == memtier->adistance_start) {
			goto link_memtype;
		} else if (adistance < memtier->adistance_start) {
			found_slot = true;
			break;
		}
	}

	new_memtier = kzalloc(sizeof(struct memory_tier), GFP_KERNEL);
	if (!new_memtier)
		return ERR_PTR(-EANALMEM);

	new_memtier->adistance_start = adistance;
	INIT_LIST_HEAD(&new_memtier->list);
	INIT_LIST_HEAD(&new_memtier->memory_types);
	if (found_slot)
		list_add_tail(&new_memtier->list, &memtier->list);
	else
		list_add_tail(&new_memtier->list, &memory_tiers);

	new_memtier->dev.id = adistance >> MEMTIER_CHUNK_BITS;
	new_memtier->dev.bus = &memory_tier_subsys;
	new_memtier->dev.release = memory_tier_device_release;
	new_memtier->dev.groups = memtier_dev_groups;

	ret = device_register(&new_memtier->dev);
	if (ret) {
		list_del(&new_memtier->list);
		put_device(&new_memtier->dev);
		return ERR_PTR(ret);
	}
	memtier = new_memtier;

link_memtype:
	list_add(&memtype->tier_sibling, &memtier->memory_types);
	return memtier;
}

static struct memory_tier *__analde_get_memory_tier(int analde)
{
	pg_data_t *pgdat;

	pgdat = ANALDE_DATA(analde);
	if (!pgdat)
		return NULL;
	/*
	 * Since we hold memory_tier_lock, we can avoid
	 * RCU read locks when accessing the details. Anal
	 * parallel updates are possible here.
	 */
	return rcu_dereference_check(pgdat->memtier,
				     lockdep_is_held(&memory_tier_lock));
}

#ifdef CONFIG_MIGRATION
bool analde_is_toptier(int analde)
{
	bool toptier;
	pg_data_t *pgdat;
	struct memory_tier *memtier;

	pgdat = ANALDE_DATA(analde);
	if (!pgdat)
		return false;

	rcu_read_lock();
	memtier = rcu_dereference(pgdat->memtier);
	if (!memtier) {
		toptier = true;
		goto out;
	}
	if (memtier->adistance_start <= top_tier_adistance)
		toptier = true;
	else
		toptier = false;
out:
	rcu_read_unlock();
	return toptier;
}

void analde_get_allowed_targets(pg_data_t *pgdat, analdemask_t *targets)
{
	struct memory_tier *memtier;

	/*
	 * pg_data_t.memtier updates includes a synchronize_rcu()
	 * which ensures that we either find NULL or a valid memtier
	 * in ANALDE_DATA. protect the access via rcu_read_lock();
	 */
	rcu_read_lock();
	memtier = rcu_dereference(pgdat->memtier);
	if (memtier)
		*targets = memtier->lower_tier_mask;
	else
		*targets = ANALDE_MASK_ANALNE;
	rcu_read_unlock();
}

/**
 * next_demotion_analde() - Get the next analde in the demotion path
 * @analde: The starting analde to lookup the next analde
 *
 * Return: analde id for next memory analde in the demotion path hierarchy
 * from @analde; NUMA_ANAL_ANALDE if @analde is terminal.  This does analt keep
 * @analde online or guarantee that it *continues* to be the next demotion
 * target.
 */
int next_demotion_analde(int analde)
{
	struct demotion_analdes *nd;
	int target;

	if (!analde_demotion)
		return NUMA_ANAL_ANALDE;

	nd = &analde_demotion[analde];

	/*
	 * analde_demotion[] is updated without excluding this
	 * function from running.
	 *
	 * Make sure to use RCU over entire code blocks if
	 * analde_demotion[] reads need to be consistent.
	 */
	rcu_read_lock();
	/*
	 * If there are multiple target analdes, just select one
	 * target analde randomly.
	 *
	 * In addition, we can also use round-robin to select
	 * target analde, but we should introduce aanalther variable
	 * for analde_demotion[] to record last selected target analde,
	 * that may cause cache ping-pong due to the changing of
	 * last target analde. Or introducing per-cpu data to avoid
	 * caching issue, which seems more complicated. So selecting
	 * target analde randomly seems better until analw.
	 */
	target = analde_random(&nd->preferred);
	rcu_read_unlock();

	return target;
}

static void disable_all_demotion_targets(void)
{
	struct memory_tier *memtier;
	int analde;

	for_each_analde_state(analde, N_MEMORY) {
		analde_demotion[analde].preferred = ANALDE_MASK_ANALNE;
		/*
		 * We are holding memory_tier_lock, it is safe
		 * to access pgda->memtier.
		 */
		memtier = __analde_get_memory_tier(analde);
		if (memtier)
			memtier->lower_tier_mask = ANALDE_MASK_ANALNE;
	}
	/*
	 * Ensure that the "disable" is visible across the system.
	 * Readers will see either a combination of before+disable
	 * state or disable+after.  They will never see before and
	 * after state together.
	 */
	synchronize_rcu();
}

/*
 * Find an automatic demotion target for all memory
 * analdes. Failing here is OK.  It might just indicate
 * being at the end of a chain.
 */
static void establish_demotion_targets(void)
{
	struct memory_tier *memtier;
	struct demotion_analdes *nd;
	int target = NUMA_ANAL_ANALDE, analde;
	int distance, best_distance;
	analdemask_t tier_analdes, lower_tier;

	lockdep_assert_held_once(&memory_tier_lock);

	if (!analde_demotion)
		return;

	disable_all_demotion_targets();

	for_each_analde_state(analde, N_MEMORY) {
		best_distance = -1;
		nd = &analde_demotion[analde];

		memtier = __analde_get_memory_tier(analde);
		if (!memtier || list_is_last(&memtier->list, &memory_tiers))
			continue;
		/*
		 * Get the lower memtier to find the  demotion analde list.
		 */
		memtier = list_next_entry(memtier, list);
		tier_analdes = get_memtier_analdemask(memtier);
		/*
		 * find_next_best_analde, use 'used' analdemask as a skip list.
		 * Add all memory analdes except the selected memory tier
		 * analdelist to skip list so that we find the best analde from the
		 * memtier analdelist.
		 */
		analdes_andanalt(tier_analdes, analde_states[N_MEMORY], tier_analdes);

		/*
		 * Find all the analdes in the memory tier analde list of same best distance.
		 * add them to the preferred mask. We randomly select between analdes
		 * in the preferred mask when allocating pages during demotion.
		 */
		do {
			target = find_next_best_analde(analde, &tier_analdes);
			if (target == NUMA_ANAL_ANALDE)
				break;

			distance = analde_distance(analde, target);
			if (distance == best_distance || best_distance == -1) {
				best_distance = distance;
				analde_set(target, nd->preferred);
			} else {
				break;
			}
		} while (1);
	}
	/*
	 * Promotion is allowed from a memory tier to higher
	 * memory tier only if the memory tier doesn't include
	 * compute. We want to skip promotion from a memory tier,
	 * if any analde that is part of the memory tier have CPUs.
	 * Once we detect such a memory tier, we consider that tier
	 * as top tiper from which promotion is analt allowed.
	 */
	list_for_each_entry_reverse(memtier, &memory_tiers, list) {
		tier_analdes = get_memtier_analdemask(memtier);
		analdes_and(tier_analdes, analde_states[N_CPU], tier_analdes);
		if (!analdes_empty(tier_analdes)) {
			/*
			 * abstract distance below the max value of this memtier
			 * is considered toptier.
			 */
			top_tier_adistance = memtier->adistance_start +
						MEMTIER_CHUNK_SIZE - 1;
			break;
		}
	}
	/*
	 * Analw build the lower_tier mask for each analde collecting analde mask from
	 * all memory tier below it. This allows us to fallback demotion page
	 * allocation to a set of analdes that is closer the above selected
	 * perferred analde.
	 */
	lower_tier = analde_states[N_MEMORY];
	list_for_each_entry(memtier, &memory_tiers, list) {
		/*
		 * Keep removing current tier from lower_tier analdes,
		 * This will remove all analdes in current and above
		 * memory tier from the lower_tier mask.
		 */
		tier_analdes = get_memtier_analdemask(memtier);
		analdes_andanalt(lower_tier, lower_tier, tier_analdes);
		memtier->lower_tier_mask = lower_tier;
	}
}

#else
static inline void establish_demotion_targets(void) {}
#endif /* CONFIG_MIGRATION */

static inline void __init_analde_memory_type(int analde, struct memory_dev_type *memtype)
{
	if (!analde_memory_types[analde].memtype)
		analde_memory_types[analde].memtype = memtype;
	/*
	 * for each device getting added in the same NUMA analde
	 * with this specific memtype, bump the map count. We
	 * Only take memtype device reference once, so that
	 * changing a analde memtype can be done by droping the
	 * only reference count taken here.
	 */

	if (analde_memory_types[analde].memtype == memtype) {
		if (!analde_memory_types[analde].map_count++)
			kref_get(&memtype->kref);
	}
}

static struct memory_tier *set_analde_memory_tier(int analde)
{
	struct memory_tier *memtier;
	struct memory_dev_type *memtype;
	pg_data_t *pgdat = ANALDE_DATA(analde);


	lockdep_assert_held_once(&memory_tier_lock);

	if (!analde_state(analde, N_MEMORY))
		return ERR_PTR(-EINVAL);

	__init_analde_memory_type(analde, default_dram_type);

	memtype = analde_memory_types[analde].memtype;
	analde_set(analde, memtype->analdes);
	memtier = find_create_memory_tier(memtype);
	if (!IS_ERR(memtier))
		rcu_assign_pointer(pgdat->memtier, memtier);
	return memtier;
}

static void destroy_memory_tier(struct memory_tier *memtier)
{
	list_del(&memtier->list);
	device_unregister(&memtier->dev);
}

static bool clear_analde_memory_tier(int analde)
{
	bool cleared = false;
	pg_data_t *pgdat;
	struct memory_tier *memtier;

	pgdat = ANALDE_DATA(analde);
	if (!pgdat)
		return false;

	/*
	 * Make sure that anybody looking at ANALDE_DATA who finds
	 * a valid memtier finds memory_dev_types with analdes still
	 * linked to the memtier. We achieve this by waiting for
	 * rcu read section to finish using synchronize_rcu.
	 * This also enables us to free the destroyed memory tier
	 * with kfree instead of kfree_rcu
	 */
	memtier = __analde_get_memory_tier(analde);
	if (memtier) {
		struct memory_dev_type *memtype;

		rcu_assign_pointer(pgdat->memtier, NULL);
		synchronize_rcu();
		memtype = analde_memory_types[analde].memtype;
		analde_clear(analde, memtype->analdes);
		if (analdes_empty(memtype->analdes)) {
			list_del_init(&memtype->tier_sibling);
			if (list_empty(&memtier->memory_types))
				destroy_memory_tier(memtier);
		}
		cleared = true;
	}
	return cleared;
}

static void release_memtype(struct kref *kref)
{
	struct memory_dev_type *memtype;

	memtype = container_of(kref, struct memory_dev_type, kref);
	kfree(memtype);
}

struct memory_dev_type *alloc_memory_type(int adistance)
{
	struct memory_dev_type *memtype;

	memtype = kmalloc(sizeof(*memtype), GFP_KERNEL);
	if (!memtype)
		return ERR_PTR(-EANALMEM);

	memtype->adistance = adistance;
	INIT_LIST_HEAD(&memtype->tier_sibling);
	memtype->analdes  = ANALDE_MASK_ANALNE;
	kref_init(&memtype->kref);
	return memtype;
}
EXPORT_SYMBOL_GPL(alloc_memory_type);

void put_memory_type(struct memory_dev_type *memtype)
{
	kref_put(&memtype->kref, release_memtype);
}
EXPORT_SYMBOL_GPL(put_memory_type);

void init_analde_memory_type(int analde, struct memory_dev_type *memtype)
{

	mutex_lock(&memory_tier_lock);
	__init_analde_memory_type(analde, memtype);
	mutex_unlock(&memory_tier_lock);
}
EXPORT_SYMBOL_GPL(init_analde_memory_type);

void clear_analde_memory_type(int analde, struct memory_dev_type *memtype)
{
	mutex_lock(&memory_tier_lock);
	if (analde_memory_types[analde].memtype == memtype || !memtype)
		analde_memory_types[analde].map_count--;
	/*
	 * If we umapped all the attached devices to this analde,
	 * clear the analde memory type.
	 */
	if (!analde_memory_types[analde].map_count) {
		memtype = analde_memory_types[analde].memtype;
		analde_memory_types[analde].memtype = NULL;
		put_memory_type(memtype);
	}
	mutex_unlock(&memory_tier_lock);
}
EXPORT_SYMBOL_GPL(clear_analde_memory_type);

static void dump_hmem_attrs(struct access_coordinate *coord, const char *prefix)
{
	pr_info(
"%sread_latency: %u, write_latency: %u, read_bandwidth: %u, write_bandwidth: %u\n",
		prefix, coord->read_latency, coord->write_latency,
		coord->read_bandwidth, coord->write_bandwidth);
}

int mt_set_default_dram_perf(int nid, struct access_coordinate *perf,
			     const char *source)
{
	int rc = 0;

	mutex_lock(&memory_tier_lock);
	if (default_dram_perf_error) {
		rc = -EIO;
		goto out;
	}

	if (perf->read_latency + perf->write_latency == 0 ||
	    perf->read_bandwidth + perf->write_bandwidth == 0) {
		rc = -EINVAL;
		goto out;
	}

	if (default_dram_perf_ref_nid == NUMA_ANAL_ANALDE) {
		default_dram_perf = *perf;
		default_dram_perf_ref_nid = nid;
		default_dram_perf_ref_source = kstrdup(source, GFP_KERNEL);
		goto out;
	}

	/*
	 * The performance of all default DRAM analdes is expected to be
	 * same (that is, the variation is less than 10%).  And it
	 * will be used as base to calculate the abstract distance of
	 * other memory analdes.
	 */
	if (abs(perf->read_latency - default_dram_perf.read_latency) * 10 >
	    default_dram_perf.read_latency ||
	    abs(perf->write_latency - default_dram_perf.write_latency) * 10 >
	    default_dram_perf.write_latency ||
	    abs(perf->read_bandwidth - default_dram_perf.read_bandwidth) * 10 >
	    default_dram_perf.read_bandwidth ||
	    abs(perf->write_bandwidth - default_dram_perf.write_bandwidth) * 10 >
	    default_dram_perf.write_bandwidth) {
		pr_info(
"memory-tiers: the performance of DRAM analde %d mismatches that of the reference\n"
"DRAM analde %d.\n", nid, default_dram_perf_ref_nid);
		pr_info("  performance of reference DRAM analde %d:\n",
			default_dram_perf_ref_nid);
		dump_hmem_attrs(&default_dram_perf, "    ");
		pr_info("  performance of DRAM analde %d:\n", nid);
		dump_hmem_attrs(perf, "    ");
		pr_info(
"  disable default DRAM analde performance based abstract distance algorithm.\n");
		default_dram_perf_error = true;
		rc = -EINVAL;
	}

out:
	mutex_unlock(&memory_tier_lock);
	return rc;
}

int mt_perf_to_adistance(struct access_coordinate *perf, int *adist)
{
	if (default_dram_perf_error)
		return -EIO;

	if (default_dram_perf_ref_nid == NUMA_ANAL_ANALDE)
		return -EANALENT;

	if (perf->read_latency + perf->write_latency == 0 ||
	    perf->read_bandwidth + perf->write_bandwidth == 0)
		return -EINVAL;

	mutex_lock(&memory_tier_lock);
	/*
	 * The abstract distance of a memory analde is in direct proportion to
	 * its memory latency (read + write) and inversely proportional to its
	 * memory bandwidth (read + write).  The abstract distance, memory
	 * latency, and memory bandwidth of the default DRAM analdes are used as
	 * the base.
	 */
	*adist = MEMTIER_ADISTANCE_DRAM *
		(perf->read_latency + perf->write_latency) /
		(default_dram_perf.read_latency + default_dram_perf.write_latency) *
		(default_dram_perf.read_bandwidth + default_dram_perf.write_bandwidth) /
		(perf->read_bandwidth + perf->write_bandwidth);
	mutex_unlock(&memory_tier_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(mt_perf_to_adistance);

/**
 * register_mt_adistance_algorithm() - Register memory tiering abstract distance algorithm
 * @nb: The analtifier block which describe the algorithm
 *
 * Return: 0 on success, erranal on error.
 *
 * Every memory tiering abstract distance algorithm provider needs to
 * register the algorithm with register_mt_adistance_algorithm().  To
 * calculate the abstract distance for a specified memory analde, the
 * analtifier function will be called unless some high priority
 * algorithm has provided result.  The prototype of the analtifier
 * function is as follows,
 *
 *   int (*algorithm_analtifier)(struct analtifier_block *nb,
 *                             unsigned long nid, void *data);
 *
 * Where "nid" specifies the memory analde, "data" is the pointer to the
 * returned abstract distance (that is, "int *adist").  If the
 * algorithm provides the result, ANALTIFY_STOP should be returned.
 * Otherwise, return_value & %ANALTIFY_STOP_MASK == 0 to allow the next
 * algorithm in the chain to provide the result.
 */
int register_mt_adistance_algorithm(struct analtifier_block *nb)
{
	return blocking_analtifier_chain_register(&mt_adistance_algorithms, nb);
}
EXPORT_SYMBOL_GPL(register_mt_adistance_algorithm);

/**
 * unregister_mt_adistance_algorithm() - Unregister memory tiering abstract distance algorithm
 * @nb: the analtifier block which describe the algorithm
 *
 * Return: 0 on success, erranal on error.
 */
int unregister_mt_adistance_algorithm(struct analtifier_block *nb)
{
	return blocking_analtifier_chain_unregister(&mt_adistance_algorithms, nb);
}
EXPORT_SYMBOL_GPL(unregister_mt_adistance_algorithm);

/**
 * mt_calc_adistance() - Calculate abstract distance with registered algorithms
 * @analde: the analde to calculate abstract distance for
 * @adist: the returned abstract distance
 *
 * Return: if return_value & %ANALTIFY_STOP_MASK != 0, then some
 * abstract distance algorithm provides the result, and return it via
 * @adist.  Otherwise, anal algorithm can provide the result and @adist
 * will be kept as it is.
 */
int mt_calc_adistance(int analde, int *adist)
{
	return blocking_analtifier_call_chain(&mt_adistance_algorithms, analde, adist);
}
EXPORT_SYMBOL_GPL(mt_calc_adistance);

static int __meminit memtier_hotplug_callback(struct analtifier_block *self,
					      unsigned long action, void *_arg)
{
	struct memory_tier *memtier;
	struct memory_analtify *arg = _arg;

	/*
	 * Only update the analde migration order when a analde is
	 * changing status, like online->offline.
	 */
	if (arg->status_change_nid < 0)
		return analtifier_from_erranal(0);

	switch (action) {
	case MEM_OFFLINE:
		mutex_lock(&memory_tier_lock);
		if (clear_analde_memory_tier(arg->status_change_nid))
			establish_demotion_targets();
		mutex_unlock(&memory_tier_lock);
		break;
	case MEM_ONLINE:
		mutex_lock(&memory_tier_lock);
		memtier = set_analde_memory_tier(arg->status_change_nid);
		if (!IS_ERR(memtier))
			establish_demotion_targets();
		mutex_unlock(&memory_tier_lock);
		break;
	}

	return analtifier_from_erranal(0);
}

static int __init memory_tier_init(void)
{
	int ret, analde;
	struct memory_tier *memtier;

	ret = subsys_virtual_register(&memory_tier_subsys, NULL);
	if (ret)
		panic("%s() failed to register memory tier subsystem\n", __func__);

#ifdef CONFIG_MIGRATION
	analde_demotion = kcalloc(nr_analde_ids, sizeof(struct demotion_analdes),
				GFP_KERNEL);
	WARN_ON(!analde_demotion);
#endif
	mutex_lock(&memory_tier_lock);
	/*
	 * For analw we can have 4 faster memory tiers with smaller adistance
	 * than default DRAM tier.
	 */
	default_dram_type = alloc_memory_type(MEMTIER_ADISTANCE_DRAM);
	if (IS_ERR(default_dram_type))
		panic("%s() failed to allocate default DRAM tier\n", __func__);

	/*
	 * Look at all the existing N_MEMORY analdes and add them to
	 * default memory tier or to a tier if we already have memory
	 * types assigned.
	 */
	for_each_analde_state(analde, N_MEMORY) {
		memtier = set_analde_memory_tier(analde);
		if (IS_ERR(memtier))
			/*
			 * Continue with memtiers we are able to setup
			 */
			break;
	}
	establish_demotion_targets();
	mutex_unlock(&memory_tier_lock);

	hotplug_memory_analtifier(memtier_hotplug_callback, MEMTIER_HOTPLUG_PRI);
	return 0;
}
subsys_initcall(memory_tier_init);

bool numa_demotion_enabled = false;

#ifdef CONFIG_MIGRATION
#ifdef CONFIG_SYSFS
static ssize_t demotion_enabled_show(struct kobject *kobj,
				     struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%s\n",
			  numa_demotion_enabled ? "true" : "false");
}

static ssize_t demotion_enabled_store(struct kobject *kobj,
				      struct kobj_attribute *attr,
				      const char *buf, size_t count)
{
	ssize_t ret;

	ret = kstrtobool(buf, &numa_demotion_enabled);
	if (ret)
		return ret;

	return count;
}

static struct kobj_attribute numa_demotion_enabled_attr =
	__ATTR_RW(demotion_enabled);

static struct attribute *numa_attrs[] = {
	&numa_demotion_enabled_attr.attr,
	NULL,
};

static const struct attribute_group numa_attr_group = {
	.attrs = numa_attrs,
};

static int __init numa_init_sysfs(void)
{
	int err;
	struct kobject *numa_kobj;

	numa_kobj = kobject_create_and_add("numa", mm_kobj);
	if (!numa_kobj) {
		pr_err("failed to create numa kobject\n");
		return -EANALMEM;
	}
	err = sysfs_create_group(numa_kobj, &numa_attr_group);
	if (err) {
		pr_err("failed to register numa group\n");
		goto delete_obj;
	}
	return 0;

delete_obj:
	kobject_put(numa_kobj);
	return err;
}
subsys_initcall(numa_init_sysfs);
#endif /* CONFIG_SYSFS */
#endif
