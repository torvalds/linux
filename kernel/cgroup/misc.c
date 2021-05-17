// SPDX-License-Identifier: GPL-2.0
/*
 * Miscellaneous cgroup controller
 *
 * Copyright 2020 Google LLC
 * Author: Vipin Sharma <vipinsh@google.com>
 */

#include <linux/limits.h>
#include <linux/cgroup.h>
#include <linux/errno.h>
#include <linux/atomic.h>
#include <linux/slab.h>
#include <linux/misc_cgroup.h>

#define MAX_STR "max"
#define MAX_NUM ULONG_MAX

/* Miscellaneous res name, keep it in sync with enum misc_res_type */
static const char *const misc_res_name[] = {
#ifdef CONFIG_KVM_AMD_SEV
	/* AMD SEV ASIDs resource */
	"sev",
	/* AMD SEV-ES ASIDs resource */
	"sev_es",
#endif
};

/* Root misc cgroup */
static struct misc_cg root_cg;

/*
 * Miscellaneous resources capacity for the entire machine. 0 capacity means
 * resource is not initialized or not present in the host.
 *
 * root_cg.max and capacity are independent of each other. root_cg.max can be
 * more than the actual capacity. We are using Limits resource distribution
 * model of cgroup for miscellaneous controller.
 */
static unsigned long misc_res_capacity[MISC_CG_RES_TYPES];

/**
 * parent_misc() - Get the parent of the passed misc cgroup.
 * @cgroup: cgroup whose parent needs to be fetched.
 *
 * Context: Any context.
 * Return:
 * * struct misc_cg* - Parent of the @cgroup.
 * * %NULL - If @cgroup is null or the passed cgroup does not have a parent.
 */
static struct misc_cg *parent_misc(struct misc_cg *cgroup)
{
	return cgroup ? css_misc(cgroup->css.parent) : NULL;
}

/**
 * valid_type() - Check if @type is valid or not.
 * @type: misc res type.
 *
 * Context: Any context.
 * Return:
 * * true - If valid type.
 * * false - If not valid type.
 */
static inline bool valid_type(enum misc_res_type type)
{
	return type >= 0 && type < MISC_CG_RES_TYPES;
}

/**
 * misc_cg_res_total_usage() - Get the current total usage of the resource.
 * @type: misc res type.
 *
 * Context: Any context.
 * Return: Current total usage of the resource.
 */
unsigned long misc_cg_res_total_usage(enum misc_res_type type)
{
	if (valid_type(type))
		return atomic_long_read(&root_cg.res[type].usage);

	return 0;
}
EXPORT_SYMBOL_GPL(misc_cg_res_total_usage);

/**
 * misc_cg_set_capacity() - Set the capacity of the misc cgroup res.
 * @type: Type of the misc res.
 * @capacity: Supported capacity of the misc res on the host.
 *
 * If capacity is 0 then the charging a misc cgroup fails for that type.
 *
 * Context: Any context.
 * Return:
 * * %0 - Successfully registered the capacity.
 * * %-EINVAL - If @type is invalid.
 */
int misc_cg_set_capacity(enum misc_res_type type, unsigned long capacity)
{
	if (!valid_type(type))
		return -EINVAL;

	WRITE_ONCE(misc_res_capacity[type], capacity);
	return 0;
}
EXPORT_SYMBOL_GPL(misc_cg_set_capacity);

/**
 * misc_cg_cancel_charge() - Cancel the charge from the misc cgroup.
 * @type: Misc res type in misc cg to cancel the charge from.
 * @cg: Misc cgroup to cancel charge from.
 * @amount: Amount to cancel.
 *
 * Context: Any context.
 */
static void misc_cg_cancel_charge(enum misc_res_type type, struct misc_cg *cg,
				  unsigned long amount)
{
	WARN_ONCE(atomic_long_add_negative(-amount, &cg->res[type].usage),
		  "misc cgroup resource %s became less than 0",
		  misc_res_name[type]);
}

/**
 * misc_cg_try_charge() - Try charging the misc cgroup.
 * @type: Misc res type to charge.
 * @cg: Misc cgroup which will be charged.
 * @amount: Amount to charge.
 *
 * Charge @amount to the misc cgroup. Caller must use the same cgroup during
 * the uncharge call.
 *
 * Context: Any context.
 * Return:
 * * %0 - If successfully charged.
 * * -EINVAL - If @type is invalid or misc res has 0 capacity.
 * * -EBUSY - If max limit will be crossed or total usage will be more than the
 *	      capacity.
 */
int misc_cg_try_charge(enum misc_res_type type, struct misc_cg *cg,
		       unsigned long amount)
{
	struct misc_cg *i, *j;
	int ret;
	struct misc_res *res;
	int new_usage;

	if (!(valid_type(type) && cg && READ_ONCE(misc_res_capacity[type])))
		return -EINVAL;

	if (!amount)
		return 0;

	for (i = cg; i; i = parent_misc(i)) {
		res = &i->res[type];

		new_usage = atomic_long_add_return(amount, &res->usage);
		if (new_usage > READ_ONCE(res->max) ||
		    new_usage > READ_ONCE(misc_res_capacity[type])) {
			if (!res->failed) {
				pr_info("cgroup: charge rejected by the misc controller for %s resource in ",
					misc_res_name[type]);
				pr_cont_cgroup_path(i->css.cgroup);
				pr_cont("\n");
				res->failed = true;
			}
			ret = -EBUSY;
			goto err_charge;
		}
	}
	return 0;

err_charge:
	for (j = cg; j != i; j = parent_misc(j))
		misc_cg_cancel_charge(type, j, amount);
	misc_cg_cancel_charge(type, i, amount);
	return ret;
}
EXPORT_SYMBOL_GPL(misc_cg_try_charge);

/**
 * misc_cg_uncharge() - Uncharge the misc cgroup.
 * @type: Misc res type which was charged.
 * @cg: Misc cgroup which will be uncharged.
 * @amount: Charged amount.
 *
 * Context: Any context.
 */
void misc_cg_uncharge(enum misc_res_type type, struct misc_cg *cg,
		      unsigned long amount)
{
	struct misc_cg *i;

	if (!(amount && valid_type(type) && cg))
		return;

	for (i = cg; i; i = parent_misc(i))
		misc_cg_cancel_charge(type, i, amount);
}
EXPORT_SYMBOL_GPL(misc_cg_uncharge);

/**
 * misc_cg_max_show() - Show the misc cgroup max limit.
 * @sf: Interface file
 * @v: Arguments passed
 *
 * Context: Any context.
 * Return: 0 to denote successful print.
 */
static int misc_cg_max_show(struct seq_file *sf, void *v)
{
	int i;
	struct misc_cg *cg = css_misc(seq_css(sf));
	unsigned long max;

	for (i = 0; i < MISC_CG_RES_TYPES; i++) {
		if (READ_ONCE(misc_res_capacity[i])) {
			max = READ_ONCE(cg->res[i].max);
			if (max == MAX_NUM)
				seq_printf(sf, "%s max\n", misc_res_name[i]);
			else
				seq_printf(sf, "%s %lu\n", misc_res_name[i],
					   max);
		}
	}

	return 0;
}

/**
 * misc_cg_max_write() - Update the maximum limit of the cgroup.
 * @of: Handler for the file.
 * @buf: Data from the user. It should be either "max", 0, or a positive
 *	 integer.
 * @nbytes: Number of bytes of the data.
 * @off: Offset in the file.
 *
 * User can pass data like:
 * echo sev 23 > misc.max, OR
 * echo sev max > misc.max
 *
 * Context: Any context.
 * Return:
 * * >= 0 - Number of bytes processed in the input.
 * * -EINVAL - If buf is not valid.
 * * -ERANGE - If number is bigger than the unsigned long capacity.
 */
static ssize_t misc_cg_max_write(struct kernfs_open_file *of, char *buf,
				 size_t nbytes, loff_t off)
{
	struct misc_cg *cg;
	unsigned long max;
	int ret = 0, i;
	enum misc_res_type type = MISC_CG_RES_TYPES;
	char *token;

	buf = strstrip(buf);
	token = strsep(&buf, " ");

	if (!token || !buf)
		return -EINVAL;

	for (i = 0; i < MISC_CG_RES_TYPES; i++) {
		if (!strcmp(misc_res_name[i], token)) {
			type = i;
			break;
		}
	}

	if (type == MISC_CG_RES_TYPES)
		return -EINVAL;

	if (!strcmp(MAX_STR, buf)) {
		max = MAX_NUM;
	} else {
		ret = kstrtoul(buf, 0, &max);
		if (ret)
			return ret;
	}

	cg = css_misc(of_css(of));

	if (READ_ONCE(misc_res_capacity[type]))
		WRITE_ONCE(cg->res[type].max, max);
	else
		ret = -EINVAL;

	return ret ? ret : nbytes;
}

/**
 * misc_cg_current_show() - Show the current usage of the misc cgroup.
 * @sf: Interface file
 * @v: Arguments passed
 *
 * Context: Any context.
 * Return: 0 to denote successful print.
 */
static int misc_cg_current_show(struct seq_file *sf, void *v)
{
	int i;
	unsigned long usage;
	struct misc_cg *cg = css_misc(seq_css(sf));

	for (i = 0; i < MISC_CG_RES_TYPES; i++) {
		usage = atomic_long_read(&cg->res[i].usage);
		if (READ_ONCE(misc_res_capacity[i]) || usage)
			seq_printf(sf, "%s %lu\n", misc_res_name[i], usage);
	}

	return 0;
}

/**
 * misc_cg_capacity_show() - Show the total capacity of misc res on the host.
 * @sf: Interface file
 * @v: Arguments passed
 *
 * Only present in the root cgroup directory.
 *
 * Context: Any context.
 * Return: 0 to denote successful print.
 */
static int misc_cg_capacity_show(struct seq_file *sf, void *v)
{
	int i;
	unsigned long cap;

	for (i = 0; i < MISC_CG_RES_TYPES; i++) {
		cap = READ_ONCE(misc_res_capacity[i]);
		if (cap)
			seq_printf(sf, "%s %lu\n", misc_res_name[i], cap);
	}

	return 0;
}

/* Misc cgroup interface files */
static struct cftype misc_cg_files[] = {
	{
		.name = "max",
		.write = misc_cg_max_write,
		.seq_show = misc_cg_max_show,
		.flags = CFTYPE_NOT_ON_ROOT,
	},
	{
		.name = "current",
		.seq_show = misc_cg_current_show,
		.flags = CFTYPE_NOT_ON_ROOT,
	},
	{
		.name = "capacity",
		.seq_show = misc_cg_capacity_show,
		.flags = CFTYPE_ONLY_ON_ROOT,
	},
	{}
};

/**
 * misc_cg_alloc() - Allocate misc cgroup.
 * @parent_css: Parent cgroup.
 *
 * Context: Process context.
 * Return:
 * * struct cgroup_subsys_state* - css of the allocated cgroup.
 * * ERR_PTR(-ENOMEM) - No memory available to allocate.
 */
static struct cgroup_subsys_state *
misc_cg_alloc(struct cgroup_subsys_state *parent_css)
{
	enum misc_res_type i;
	struct misc_cg *cg;

	if (!parent_css) {
		cg = &root_cg;
	} else {
		cg = kzalloc(sizeof(*cg), GFP_KERNEL);
		if (!cg)
			return ERR_PTR(-ENOMEM);
	}

	for (i = 0; i < MISC_CG_RES_TYPES; i++) {
		WRITE_ONCE(cg->res[i].max, MAX_NUM);
		atomic_long_set(&cg->res[i].usage, 0);
	}

	return &cg->css;
}

/**
 * misc_cg_free() - Free the misc cgroup.
 * @css: cgroup subsys object.
 *
 * Context: Any context.
 */
static void misc_cg_free(struct cgroup_subsys_state *css)
{
	kfree(css_misc(css));
}

/* Cgroup controller callbacks */
struct cgroup_subsys misc_cgrp_subsys = {
	.css_alloc = misc_cg_alloc,
	.css_free = misc_cg_free,
	.legacy_cftypes = misc_cg_files,
	.dfl_cftypes = misc_cg_files,
};
