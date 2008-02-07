/* memcontrol.c - Memory Controller
 *
 * Copyright IBM Corporation, 2007
 * Author Balbir Singh <balbir@linux.vnet.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/res_counter.h>
#include <linux/memcontrol.h>
#include <linux/cgroup.h>

struct cgroup_subsys mem_cgroup_subsys;

/*
 * The memory controller data structure. The memory controller controls both
 * page cache and RSS per cgroup. We would eventually like to provide
 * statistics based on the statistics developed by Rik Van Riel for clock-pro,
 * to help the administrator determine what knobs to tune.
 *
 * TODO: Add a water mark for the memory controller. Reclaim will begin when
 * we hit the water mark.
 */
struct mem_cgroup {
	struct cgroup_subsys_state css;
	/*
	 * the counter to account for memory usage
	 */
	struct res_counter res;
};

/*
 * A page_cgroup page is associated with every page descriptor. The
 * page_cgroup helps us identify information about the cgroup
 */
struct page_cgroup {
	struct list_head lru;		/* per cgroup LRU list */
	struct page *page;
	struct mem_cgroup *mem_cgroup;
};


static inline
struct mem_cgroup *mem_cgroup_from_cont(struct cgroup *cont)
{
	return container_of(cgroup_subsys_state(cont,
				mem_cgroup_subsys_id), struct mem_cgroup,
				css);
}

static ssize_t mem_cgroup_read(struct cgroup *cont, struct cftype *cft,
			struct file *file, char __user *userbuf, size_t nbytes,
			loff_t *ppos)
{
	return res_counter_read(&mem_cgroup_from_cont(cont)->res,
				cft->private, userbuf, nbytes, ppos);
}

static ssize_t mem_cgroup_write(struct cgroup *cont, struct cftype *cft,
				struct file *file, const char __user *userbuf,
				size_t nbytes, loff_t *ppos)
{
	return res_counter_write(&mem_cgroup_from_cont(cont)->res,
				cft->private, userbuf, nbytes, ppos);
}

static struct cftype mem_cgroup_files[] = {
	{
		.name = "usage",
		.private = RES_USAGE,
		.read = mem_cgroup_read,
	},
	{
		.name = "limit",
		.private = RES_LIMIT,
		.write = mem_cgroup_write,
		.read = mem_cgroup_read,
	},
	{
		.name = "failcnt",
		.private = RES_FAILCNT,
		.read = mem_cgroup_read,
	},
};

static struct cgroup_subsys_state *
mem_cgroup_create(struct cgroup_subsys *ss, struct cgroup *cont)
{
	struct mem_cgroup *mem;

	mem = kzalloc(sizeof(struct mem_cgroup), GFP_KERNEL);
	if (!mem)
		return -ENOMEM;

	res_counter_init(&mem->res);
	return &mem->css;
}

static void mem_cgroup_destroy(struct cgroup_subsys *ss,
				struct cgroup *cont)
{
	kfree(mem_cgroup_from_cont(cont));
}

static int mem_cgroup_populate(struct cgroup_subsys *ss,
				struct cgroup *cont)
{
	return cgroup_add_files(cont, ss, mem_cgroup_files,
					ARRAY_SIZE(mem_cgroup_files));
}

struct cgroup_subsys mem_cgroup_subsys = {
	.name = "memory",
	.subsys_id = mem_cgroup_subsys_id,
	.create = mem_cgroup_create,
	.destroy = mem_cgroup_destroy,
	.populate = mem_cgroup_populate,
	.early_init = 0,
};
