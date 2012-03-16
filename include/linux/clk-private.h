/*
 *  linux/include/linux/clk-private.h
 *
 *  Copyright (c) 2010-2011 Jeremy Kerr <jeremy.kerr@canonical.com>
 *  Copyright (C) 2011-2012 Linaro Ltd <mturquette@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __LINUX_CLK_PRIVATE_H
#define __LINUX_CLK_PRIVATE_H

#include <linux/clk-provider.h>
#include <linux/list.h>

/*
 * WARNING: Do not include clk-private.h from any file that implements struct
 * clk_ops.  Doing so is a layering violation!
 *
 * This header exists only to allow for statically initialized clock data.  Any
 * static clock data must be defined in a separate file from the logic that
 * implements the clock operations for that same data.
 */

#ifdef CONFIG_COMMON_CLK

struct clk {
	const char		*name;
	const struct clk_ops	*ops;
	struct clk_hw		*hw;
	struct clk		*parent;
	char			**parent_names;
	struct clk		**parents;
	u8			num_parents;
	unsigned long		rate;
	unsigned long		new_rate;
	unsigned long		flags;
	unsigned int		enable_count;
	unsigned int		prepare_count;
	struct hlist_head	children;
	struct hlist_node	child_node;
	unsigned int		notifier_count;
#ifdef CONFIG_COMMON_CLK_DEBUG
	struct dentry		*dentry;
#endif
};

/**
 * __clk_init - initialize the data structures in a struct clk
 * @dev:	device initializing this clk, placeholder for now
 * @clk:	clk being initialized
 *
 * Initializes the lists in struct clk, queries the hardware for the
 * parent and rate and sets them both.
 *
 * Any struct clk passed into __clk_init must have the following members
 * populated:
 * 	.name
 * 	.ops
 * 	.hw
 * 	.parent_names
 * 	.num_parents
 * 	.flags
 *
 * It is not necessary to call clk_register if __clk_init is used directly with
 * statically initialized clock data.
 */
void __clk_init(struct device *dev, struct clk *clk);

#endif /* CONFIG_COMMON_CLK */
#endif /* CLK_PRIVATE_H */
