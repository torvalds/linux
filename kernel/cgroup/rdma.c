// SPDX-License-Identifier: GPL-2.0-only
/*
 * RDMA resource limiting controller for cgroups.
 *
 * Used to allow a cgroup hierarchy to stop processes from consuming
 * additional RDMA resources after a certain limit is reached.
 *
 * Copyright (C) 2016 Parav Pandit <pandit.parav@gmail.com>
 */

#include <linux/bitops.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/cgroup.h>
#include <linux/parser.h>
#include <linux/cgroup_rdma.h>

#define RDMACG_MAX_STR "max"

/*
 * Protects list of resource pools maintained on per cgroup basis
 * and rdma device list.
 */
static DEFINE_MUTEX(rdmacg_mutex);
static LIST_HEAD(rdmacg_devices);

enum rdmacg_file_type {
	RDMACG_RESOURCE_TYPE_MAX,
	RDMACG_RESOURCE_TYPE_STAT,
};

/*
 * resource table definition as to be seen by the user.
 * Need to add entries to it when more resources are
 * added/defined at IB verb/core layer.
 */
static char const *rdmacg_resource_names[] = {
	[RDMACG_RESOURCE_HCA_HANDLE]	= "hca_handle",
	[RDMACG_RESOURCE_HCA_OBJECT]	= "hca_object",
};

/* resource tracker for each resource of rdma cgroup */
struct rdmacg_resource {
	int max;
	int usage;
};

/*
 * resource pool object which represents per cgroup, per device
 * resources. There are multiple instances of this object per cgroup,
 * therefore it cannot be embedded within rdma_cgroup structure. It
 * is maintained as list.
 */
struct rdmacg_resource_pool {
	struct rdmacg_device	*device;
	struct rdmacg_resource	resources[RDMACG_RESOURCE_MAX];

	struct list_head	cg_node;
	struct list_head	dev_node;

	/* count active user tasks of this pool */
	u64			usage_sum;
	/* total number counts which are set to max */
	int			num_max_cnt;
};

static struct rdma_cgroup *css_rdmacg(struct cgroup_subsys_state *css)
{
	return container_of(css, struct rdma_cgroup, css);
}

static struct rdma_cgroup *parent_rdmacg(struct rdma_cgroup *cg)
{
	return css_rdmacg(cg->css.parent);
}

static inline struct rdma_cgroup *get_current_rdmacg(void)
{
	return css_rdmacg(task_get_css(current, rdma_cgrp_id));
}

static void set_resource_limit(struct rdmacg_resource_pool *rpool,
			       int index, int new_max)
{
	if (new_max == S32_MAX) {
		if (rpool->resources[index].max != S32_MAX)
			rpool->num_max_cnt++;
	} else {
		if (rpool->resources[index].max == S32_MAX)
			rpool->num_max_cnt--;
	}
	rpool->resources[index].max = new_max;
}

static void set_all_resource_max_limit(struct rdmacg_resource_pool *rpool)
{
	int i;

	for (i = 0; i < RDMACG_RESOURCE_MAX; i++)
		set_resource_limit(rpool, i, S32_MAX);
}

static void free_cg_rpool_locked(struct rdmacg_resource_pool *rpool)
{
	lockdep_assert_held(&rdmacg_mutex);

	list_del(&rpool->cg_node);
	list_del(&rpool->dev_node);
	kfree(rpool);
}

static struct rdmacg_resource_pool *
find_cg_rpool_locked(struct rdma_cgroup *cg,
		     struct rdmacg_device *device)

{
	struct rdmacg_resource_pool *pool;

	lockdep_assert_held(&rdmacg_mutex);

	list_for_each_entry(pool, &cg->rpools, cg_node)
		if (pool->device == device)
			return pool;

	return NULL;
}

static struct rdmacg_resource_pool *
get_cg_rpool_locked(struct rdma_cgroup *cg, struct rdmacg_device *device)
{
	struct rdmacg_resource_pool *rpool;

	rpool = find_cg_rpool_locked(cg, device);
	if (rpool)
		return rpool;

	rpool = kzalloc(sizeof(*rpool), GFP_KERNEL);
	if (!rpool)
		return ERR_PTR(-ENOMEM);

	rpool->device = device;
	set_all_resource_max_limit(rpool);

	INIT_LIST_HEAD(&rpool->cg_node);
	INIT_LIST_HEAD(&rpool->dev_node);
	list_add_tail(&rpool->cg_node, &cg->rpools);
	list_add_tail(&rpool->dev_node, &device->rpools);
	return rpool;
}

/**
 * uncharge_cg_locked - uncharge resource for rdma cgroup
 * @cg: pointer to cg to uncharge and all parents in hierarchy
 * @device: pointer to rdmacg device
 * @index: index of the resource to uncharge in cg (resource pool)
 *
 * It also frees the resource pool which was created as part of
 * charging operation when there are no resources attached to
 * resource pool.
 */
static void
uncharge_cg_locked(struct rdma_cgroup *cg,
		   struct rdmacg_device *device,
		   enum rdmacg_resource_type index)
{
	struct rdmacg_resource_pool *rpool;

	rpool = find_cg_rpool_locked(cg, device);

	/*
	 * rpool cannot be null at this stage. Let kernel operate in case
	 * if there a bug in IB stack or rdma controller, instead of crashing
	 * the system.
	 */
	if (unlikely(!rpool)) {
		pr_warn("Invalid device %p or rdma cgroup %p\n", cg, device);
		return;
	}

	rpool->resources[index].usage--;

	/*
	 * A negative count (or overflow) is invalid,
	 * it indicates a bug in the rdma controller.
	 */
	WARN_ON_ONCE(rpool->resources[index].usage < 0);
	rpool->usage_sum--;
	if (rpool->usage_sum == 0 &&
	    rpool->num_max_cnt == RDMACG_RESOURCE_MAX) {
		/*
		 * No user of the rpool and all entries are set to max, so
		 * safe to delete this rpool.
		 */
		free_cg_rpool_locked(rpool);
	}
}

/**
 * rdmacg_uncharge_hierarchy - hierarchically uncharge rdma resource count
 * @cg: pointer to cg to uncharge and all parents in hierarchy
 * @device: pointer to rdmacg device
 * @stop_cg: while traversing hirerchy, when meet with stop_cg cgroup
 *           stop uncharging
 * @index: index of the resource to uncharge in cg in given resource pool
 */
static void rdmacg_uncharge_hierarchy(struct rdma_cgroup *cg,
				     struct rdmacg_device *device,
				     struct rdma_cgroup *stop_cg,
				     enum rdmacg_resource_type index)
{
	struct rdma_cgroup *p;

	mutex_lock(&rdmacg_mutex);

	for (p = cg; p != stop_cg; p = parent_rdmacg(p))
		uncharge_cg_locked(p, device, index);

	mutex_unlock(&rdmacg_mutex);

	css_put(&cg->css);
}

/**
 * rdmacg_uncharge - hierarchically uncharge rdma resource count
 * @cg: pointer to cg to uncharge and all parents in hierarchy
 * @device: pointer to rdmacg device
 * @index: index of the resource to uncharge in cgroup in given resource pool
 */
void rdmacg_uncharge(struct rdma_cgroup *cg,
		     struct rdmacg_device *device,
		     enum rdmacg_resource_type index)
{
	if (index >= RDMACG_RESOURCE_MAX)
		return;

	rdmacg_uncharge_hierarchy(cg, device, NULL, index);
}
EXPORT_SYMBOL(rdmacg_uncharge);

/**
 * rdmacg_try_charge - hierarchically try to charge the rdma resource
 * @rdmacg: pointer to rdma cgroup which will own this resource
 * @device: pointer to rdmacg device
 * @index: index of the resource to charge in cgroup (resource pool)
 *
 * This function follows charging resource in hierarchical way.
 * It will fail if the charge would cause the new value to exceed the
 * hierarchical limit.
 * Returns 0 if the charge succeeded, otherwise -EAGAIN, -ENOMEM or -EINVAL.
 * Returns pointer to rdmacg for this resource when charging is successful.
 *
 * Charger needs to account resources on two criteria.
 * (a) per cgroup & (b) per device resource usage.
 * Per cgroup resource usage ensures that tasks of cgroup doesn't cross
 * the configured limits. Per device provides granular configuration
 * in multi device usage. It allocates resource pool in the hierarchy
 * for each parent it come across for first resource. Later on resource
 * pool will be available. Therefore it will be much faster thereon
 * to charge/uncharge.
 */
int rdmacg_try_charge(struct rdma_cgroup **rdmacg,
		      struct rdmacg_device *device,
		      enum rdmacg_resource_type index)
{
	struct rdma_cgroup *cg, *p;
	struct rdmacg_resource_pool *rpool;
	s64 new;
	int ret = 0;

	if (index >= RDMACG_RESOURCE_MAX)
		return -EINVAL;

	/*
	 * hold on to css, as cgroup can be removed but resource
	 * accounting happens on css.
	 */
	cg = get_current_rdmacg();

	mutex_lock(&rdmacg_mutex);
	for (p = cg; p; p = parent_rdmacg(p)) {
		rpool = get_cg_rpool_locked(p, device);
		if (IS_ERR(rpool)) {
			ret = PTR_ERR(rpool);
			goto err;
		} else {
			new = rpool->resources[index].usage + 1;
			if (new > rpool->resources[index].max) {
				ret = -EAGAIN;
				goto err;
			} else {
				rpool->resources[index].usage = new;
				rpool->usage_sum++;
			}
		}
	}
	mutex_unlock(&rdmacg_mutex);

	*rdmacg = cg;
	return 0;

err:
	mutex_unlock(&rdmacg_mutex);
	rdmacg_uncharge_hierarchy(cg, device, p, index);
	return ret;
}
EXPORT_SYMBOL(rdmacg_try_charge);

/**
 * rdmacg_register_device - register rdmacg device to rdma controller.
 * @device: pointer to rdmacg device whose resources need to be accounted.
 *
 * If IB stack wish a device to participate in rdma cgroup resource
 * tracking, it must invoke this API to register with rdma cgroup before
 * any user space application can start using the RDMA resources.
 */
void rdmacg_register_device(struct rdmacg_device *device)
{
	INIT_LIST_HEAD(&device->dev_node);
	INIT_LIST_HEAD(&device->rpools);

	mutex_lock(&rdmacg_mutex);
	list_add_tail(&device->dev_node, &rdmacg_devices);
	mutex_unlock(&rdmacg_mutex);
}
EXPORT_SYMBOL(rdmacg_register_device);

/**
 * rdmacg_unregister_device - unregister rdmacg device from rdma controller.
 * @device: pointer to rdmacg device which was previously registered with rdma
 *          controller using rdmacg_register_device().
 *
 * IB stack must invoke this after all the resources of the IB device
 * are destroyed and after ensuring that no more resources will be created
 * when this API is invoked.
 */
void rdmacg_unregister_device(struct rdmacg_device *device)
{
	struct rdmacg_resource_pool *rpool, *tmp;

	/*
	 * Synchronize with any active resource settings,
	 * usage query happening via configfs.
	 */
	mutex_lock(&rdmacg_mutex);
	list_del_init(&device->dev_node);

	/*
	 * Now that this device is off the cgroup list, its safe to free
	 * all the rpool resources.
	 */
	list_for_each_entry_safe(rpool, tmp, &device->rpools, dev_node)
		free_cg_rpool_locked(rpool);

	mutex_unlock(&rdmacg_mutex);
}
EXPORT_SYMBOL(rdmacg_unregister_device);

static int parse_resource(char *c, int *intval)
{
	substring_t argstr;
	char *name, *value = c;
	size_t len;
	int ret, i;

	name = strsep(&value, "=");
	if (!name || !value)
		return -EINVAL;

	i = match_string(rdmacg_resource_names, RDMACG_RESOURCE_MAX, name);
	if (i < 0)
		return i;

	len = strlen(value);

	argstr.from = value;
	argstr.to = value + len;

	ret = match_int(&argstr, intval);
	if (ret >= 0) {
		if (*intval < 0)
			return -EINVAL;
		return i;
	}
	if (strncmp(value, RDMACG_MAX_STR, len) == 0) {
		*intval = S32_MAX;
		return i;
	}
	return -EINVAL;
}

static int rdmacg_parse_limits(char *options,
			       int *new_limits, unsigned long *enables)
{
	char *c;
	int err = -EINVAL;

	/* parse resource options */
	while ((c = strsep(&options, " ")) != NULL) {
		int index, intval;

		index = parse_resource(c, &intval);
		if (index < 0)
			goto err;

		new_limits[index] = intval;
		*enables |= BIT(index);
	}
	return 0;

err:
	return err;
}

static struct rdmacg_device *rdmacg_get_device_locked(const char *name)
{
	struct rdmacg_device *device;

	lockdep_assert_held(&rdmacg_mutex);

	list_for_each_entry(device, &rdmacg_devices, dev_node)
		if (!strcmp(name, device->name))
			return device;

	return NULL;
}

static ssize_t rdmacg_resource_set_max(struct kernfs_open_file *of,
				       char *buf, size_t nbytes, loff_t off)
{
	struct rdma_cgroup *cg = css_rdmacg(of_css(of));
	const char *dev_name;
	struct rdmacg_resource_pool *rpool;
	struct rdmacg_device *device;
	char *options = strstrip(buf);
	int *new_limits;
	unsigned long enables = 0;
	int i = 0, ret = 0;

	/* extract the device name first */
	dev_name = strsep(&options, " ");
	if (!dev_name) {
		ret = -EINVAL;
		goto err;
	}

	new_limits = kcalloc(RDMACG_RESOURCE_MAX, sizeof(int), GFP_KERNEL);
	if (!new_limits) {
		ret = -ENOMEM;
		goto err;
	}

	ret = rdmacg_parse_limits(options, new_limits, &enables);
	if (ret)
		goto parse_err;

	/* acquire lock to synchronize with hot plug devices */
	mutex_lock(&rdmacg_mutex);

	device = rdmacg_get_device_locked(dev_name);
	if (!device) {
		ret = -ENODEV;
		goto dev_err;
	}

	rpool = get_cg_rpool_locked(cg, device);
	if (IS_ERR(rpool)) {
		ret = PTR_ERR(rpool);
		goto dev_err;
	}

	/* now set the new limits of the rpool */
	for_each_set_bit(i, &enables, RDMACG_RESOURCE_MAX)
		set_resource_limit(rpool, i, new_limits[i]);

	if (rpool->usage_sum == 0 &&
	    rpool->num_max_cnt == RDMACG_RESOURCE_MAX) {
		/*
		 * No user of the rpool and all entries are set to max, so
		 * safe to delete this rpool.
		 */
		free_cg_rpool_locked(rpool);
	}

dev_err:
	mutex_unlock(&rdmacg_mutex);

parse_err:
	kfree(new_limits);

err:
	return ret ?: nbytes;
}

static void print_rpool_values(struct seq_file *sf,
			       struct rdmacg_resource_pool *rpool)
{
	enum rdmacg_file_type sf_type;
	int i;
	u32 value;

	sf_type = seq_cft(sf)->private;

	for (i = 0; i < RDMACG_RESOURCE_MAX; i++) {
		seq_puts(sf, rdmacg_resource_names[i]);
		seq_putc(sf, '=');
		if (sf_type == RDMACG_RESOURCE_TYPE_MAX) {
			if (rpool)
				value = rpool->resources[i].max;
			else
				value = S32_MAX;
		} else {
			if (rpool)
				value = rpool->resources[i].usage;
			else
				value = 0;
		}

		if (value == S32_MAX)
			seq_puts(sf, RDMACG_MAX_STR);
		else
			seq_printf(sf, "%d", value);
		seq_putc(sf, ' ');
	}
}

static int rdmacg_resource_read(struct seq_file *sf, void *v)
{
	struct rdmacg_device *device;
	struct rdmacg_resource_pool *rpool;
	struct rdma_cgroup *cg = css_rdmacg(seq_css(sf));

	mutex_lock(&rdmacg_mutex);

	list_for_each_entry(device, &rdmacg_devices, dev_node) {
		seq_printf(sf, "%s ", device->name);

		rpool = find_cg_rpool_locked(cg, device);
		print_rpool_values(sf, rpool);

		seq_putc(sf, '\n');
	}

	mutex_unlock(&rdmacg_mutex);
	return 0;
}

static struct cftype rdmacg_files[] = {
	{
		.name = "max",
		.write = rdmacg_resource_set_max,
		.seq_show = rdmacg_resource_read,
		.private = RDMACG_RESOURCE_TYPE_MAX,
		.flags = CFTYPE_NOT_ON_ROOT,
	},
	{
		.name = "current",
		.seq_show = rdmacg_resource_read,
		.private = RDMACG_RESOURCE_TYPE_STAT,
		.flags = CFTYPE_NOT_ON_ROOT,
	},
	{ }	/* terminate */
};

static struct cgroup_subsys_state *
rdmacg_css_alloc(struct cgroup_subsys_state *parent)
{
	struct rdma_cgroup *cg;

	cg = kzalloc(sizeof(*cg), GFP_KERNEL);
	if (!cg)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&cg->rpools);
	return &cg->css;
}

static void rdmacg_css_free(struct cgroup_subsys_state *css)
{
	struct rdma_cgroup *cg = css_rdmacg(css);

	kfree(cg);
}

/**
 * rdmacg_css_offline - cgroup css_offline callback
 * @css: css of interest
 *
 * This function is called when @css is about to go away and responsible
 * for shooting down all rdmacg associated with @css. As part of that it
 * marks all the resource pool entries to max value, so that when resources are
 * uncharged, associated resource pool can be freed as well.
 */
static void rdmacg_css_offline(struct cgroup_subsys_state *css)
{
	struct rdma_cgroup *cg = css_rdmacg(css);
	struct rdmacg_resource_pool *rpool;

	mutex_lock(&rdmacg_mutex);

	list_for_each_entry(rpool, &cg->rpools, cg_node)
		set_all_resource_max_limit(rpool);

	mutex_unlock(&rdmacg_mutex);
}

struct cgroup_subsys rdma_cgrp_subsys = {
	.css_alloc	= rdmacg_css_alloc,
	.css_free	= rdmacg_css_free,
	.css_offline	= rdmacg_css_offline,
	.legacy_cftypes	= rdmacg_files,
	.dfl_cftypes	= rdmacg_files,
};
