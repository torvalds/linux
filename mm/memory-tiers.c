// SPDX-License-Identifier: GPL-2.0
#include <linux/types.h>
#include <linux/nodemask.h>
#include <linux/slab.h>
#include <linux/lockdep.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/memory-tiers.h>

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
};

struct memory_dev_type {
	/* list of memory types that are part of same tier as this type */
	struct list_head tier_sibiling;
	/* abstract distance for this specific memory type */
	int adistance;
	/* Nodes of same abstract distance */
	nodemask_t nodes;
	struct memory_tier *memtier;
};

static DEFINE_MUTEX(memory_tier_lock);
static LIST_HEAD(memory_tiers);
static struct memory_dev_type *node_memory_types[MAX_NUMNODES];
/*
 * For now we can have 4 faster memory tiers with smaller adistance
 * than default DRAM tier.
 */
static struct memory_dev_type default_dram_type  = {
	.adistance = MEMTIER_ADISTANCE_DRAM,
	.tier_sibiling = LIST_HEAD_INIT(default_dram_type.tier_sibiling),
};

static struct memory_tier *find_create_memory_tier(struct memory_dev_type *memtype)
{
	bool found_slot = false;
	struct memory_tier *memtier, *new_memtier;
	int adistance = memtype->adistance;
	unsigned int memtier_adistance_chunk_size = MEMTIER_CHUNK_SIZE;

	lockdep_assert_held_once(&memory_tier_lock);

	/*
	 * If the memtype is already part of a memory tier,
	 * just return that.
	 */
	if (memtype->memtier)
		return memtype->memtier;

	adistance = round_down(adistance, memtier_adistance_chunk_size);
	list_for_each_entry(memtier, &memory_tiers, list) {
		if (adistance == memtier->adistance_start) {
			memtype->memtier = memtier;
			list_add(&memtype->tier_sibiling, &memtier->memory_types);
			return memtier;
		} else if (adistance < memtier->adistance_start) {
			found_slot = true;
			break;
		}
	}

	new_memtier = kmalloc(sizeof(struct memory_tier), GFP_KERNEL);
	if (!new_memtier)
		return ERR_PTR(-ENOMEM);

	new_memtier->adistance_start = adistance;
	INIT_LIST_HEAD(&new_memtier->list);
	INIT_LIST_HEAD(&new_memtier->memory_types);
	if (found_slot)
		list_add_tail(&new_memtier->list, &memtier->list);
	else
		list_add_tail(&new_memtier->list, &memory_tiers);
	memtype->memtier = new_memtier;
	list_add(&memtype->tier_sibiling, &new_memtier->memory_types);
	return new_memtier;
}

static struct memory_tier *set_node_memory_tier(int node)
{
	struct memory_tier *memtier;
	struct memory_dev_type *memtype;

	lockdep_assert_held_once(&memory_tier_lock);

	if (!node_state(node, N_MEMORY))
		return ERR_PTR(-EINVAL);

	if (!node_memory_types[node])
		node_memory_types[node] = &default_dram_type;

	memtype = node_memory_types[node];
	node_set(node, memtype->nodes);
	memtier = find_create_memory_tier(memtype);
	return memtier;
}

static int __init memory_tier_init(void)
{
	int node;
	struct memory_tier *memtier;

	mutex_lock(&memory_tier_lock);
	/*
	 * Look at all the existing N_MEMORY nodes and add them to
	 * default memory tier or to a tier if we already have memory
	 * types assigned.
	 */
	for_each_node_state(node, N_MEMORY) {
		memtier = set_node_memory_tier(node);
		if (IS_ERR(memtier))
			/*
			 * Continue with memtiers we are able to setup
			 */
			break;
	}
	mutex_unlock(&memory_tier_lock);

	return 0;
}
subsys_initcall(memory_tier_init);

bool numa_demotion_enabled = false;

#ifdef CONFIG_MIGRATION
#ifdef CONFIG_SYSFS
static ssize_t numa_demotion_enabled_show(struct kobject *kobj,
					  struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%s\n",
			  numa_demotion_enabled ? "true" : "false");
}

static ssize_t numa_demotion_enabled_store(struct kobject *kobj,
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
	__ATTR(demotion_enabled, 0644, numa_demotion_enabled_show,
	       numa_demotion_enabled_store);

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
		return -ENOMEM;
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
