/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Miscellaneous cgroup controller.
 *
 * Copyright 2020 Google LLC
 * Author: Vipin Sharma <vipinsh@google.com>
 */
#ifndef _MISC_CGROUP_H_
#define _MISC_CGROUP_H_

/**
 * Types of misc cgroup entries supported by the host.
 */
enum misc_res_type {
#ifdef CONFIG_KVM_AMD_SEV
	/* AMD SEV ASIDs resource */
	MISC_CG_RES_SEV,
	/* AMD SEV-ES ASIDs resource */
	MISC_CG_RES_SEV_ES,
#endif
	MISC_CG_RES_TYPES
};

struct misc_cg;

#ifdef CONFIG_CGROUP_MISC

#include <linux/cgroup.h>

/**
 * struct misc_res: Per cgroup per misc type resource
 * @max: Maximum limit on the resource.
 * @usage: Current usage of the resource.
 * @failed: True if charged failed for the resource in a cgroup.
 */
struct misc_res {
	unsigned long max;
	atomic_long_t usage;
	atomic_long_t events;
};

/**
 * struct misc_cg - Miscellaneous controller's cgroup structure.
 * @css: cgroup subsys state object.
 * @res: Array of misc resources usage in the cgroup.
 */
struct misc_cg {
	struct cgroup_subsys_state css;

	/* misc.events */
	struct cgroup_file events_file;

	struct misc_res res[MISC_CG_RES_TYPES];
};

unsigned long misc_cg_res_total_usage(enum misc_res_type type);
int misc_cg_set_capacity(enum misc_res_type type, unsigned long capacity);
int misc_cg_try_charge(enum misc_res_type type, struct misc_cg *cg,
		       unsigned long amount);
void misc_cg_uncharge(enum misc_res_type type, struct misc_cg *cg,
		      unsigned long amount);

/**
 * css_misc() - Get misc cgroup from the css.
 * @css: cgroup subsys state object.
 *
 * Context: Any context.
 * Return:
 * * %NULL - If @css is null.
 * * struct misc_cg* - misc cgroup pointer of the passed css.
 */
static inline struct misc_cg *css_misc(struct cgroup_subsys_state *css)
{
	return css ? container_of(css, struct misc_cg, css) : NULL;
}

/*
 * get_current_misc_cg() - Find and get the misc cgroup of the current task.
 *
 * Returned cgroup has its ref count increased by 1. Caller must call
 * put_misc_cg() to return the reference.
 *
 * Return: Misc cgroup to which the current task belongs to.
 */
static inline struct misc_cg *get_current_misc_cg(void)
{
	return css_misc(task_get_css(current, misc_cgrp_id));
}

/*
 * put_misc_cg() - Put the misc cgroup and reduce its ref count.
 * @cg - cgroup to put.
 */
static inline void put_misc_cg(struct misc_cg *cg)
{
	if (cg)
		css_put(&cg->css);
}

#else /* !CONFIG_CGROUP_MISC */

static inline unsigned long misc_cg_res_total_usage(enum misc_res_type type)
{
	return 0;
}

static inline int misc_cg_set_capacity(enum misc_res_type type,
				       unsigned long capacity)
{
	return 0;
}

static inline int misc_cg_try_charge(enum misc_res_type type,
				     struct misc_cg *cg,
				     unsigned long amount)
{
	return 0;
}

static inline void misc_cg_uncharge(enum misc_res_type type,
				    struct misc_cg *cg,
				    unsigned long amount)
{
}

static inline struct misc_cg *get_current_misc_cg(void)
{
	return NULL;
}

static inline void put_misc_cg(struct misc_cg *cg)
{
}

#endif /* CONFIG_CGROUP_MISC */
#endif /* _MISC_CGROUP_H_ */
