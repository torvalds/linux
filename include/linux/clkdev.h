/*
 *  include/linux/clkdev.h
 *
 *  Copyright (C) 2008 Russell King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Helper for the clk API to assist looking up a struct clk.
 */
#ifndef __CLKDEV_H
#define __CLKDEV_H

#include <asm/clkdev.h>

struct clk;
struct device;

struct clk_lookup {
	struct list_head	node;
	const char		*dev_id;
	const char		*con_id;
	struct clk		*clk;
};

struct clk_lookup *clkdev_alloc(struct clk *clk, const char *con_id,
	const char *dev_fmt, ...);

void clkdev_add(struct clk_lookup *cl);
void clkdev_drop(struct clk_lookup *cl);

void clkdev_add_table(struct clk_lookup *, size_t);
int clk_add_alias(const char *, const char *, char *, struct device *);

#endif
