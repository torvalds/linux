/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  include/linux/clkdev.h
 *
 *  Copyright (C) 2008 Russell King.
 *
 * Helper for the clk API to assist looking up a struct clk.
 */
#ifndef __CLKDEV_H
#define __CLKDEV_H

#include <linux/slab.h>

struct clk;
struct clk_hw;
struct device;

struct clk_lookup {
	struct list_head	node;
	const char		*dev_id;
	const char		*con_id;
	struct clk		*clk;
	struct clk_hw		*clk_hw;
};

#define CLKDEV_INIT(d, n, c)	\
	{			\
		.dev_id = d,	\
		.con_id = n,	\
		.clk = c,	\
	}

void clkdev_add(struct clk_lookup *cl);
void clkdev_drop(struct clk_lookup *cl);

struct clk_lookup *clkdev_create(struct clk *clk, const char *con_id,
	const char *dev_fmt, ...) __printf(3, 4);
struct clk_lookup *clkdev_hw_create(struct clk_hw *hw, const char *con_id,
	const char *dev_fmt, ...) __printf(3, 4);

void clkdev_add_table(struct clk_lookup *, size_t);
int clk_add_alias(const char *, const char *, const char *, struct device *);

int clk_register_clkdev(struct clk *, const char *, const char *);
int clk_hw_register_clkdev(struct clk_hw *, const char *, const char *);

int devm_clk_hw_register_clkdev(struct device *dev, struct clk_hw *hw,
				const char *con_id, const char *dev_id);
void devm_clk_release_clkdev(struct device *dev, const char *con_id,
			     const char *dev_id);
#endif
