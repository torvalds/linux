// SPDX-License-Identifier: GPL-2.0

#include <linux/clk.h>

/*
 * The "inline" implementation of below helpers are only available when
 * CONFIG_HAVE_CLK or CONFIG_HAVE_CLK_PREPARE aren't set.
 */
#ifndef CONFIG_HAVE_CLK
struct clk *rust_helper_clk_get(struct device *dev, const char *id)
{
	return clk_get(dev, id);
}

void rust_helper_clk_put(struct clk *clk)
{
	clk_put(clk);
}

int rust_helper_clk_enable(struct clk *clk)
{
	return clk_enable(clk);
}

void rust_helper_clk_disable(struct clk *clk)
{
	clk_disable(clk);
}

unsigned long rust_helper_clk_get_rate(struct clk *clk)
{
	return clk_get_rate(clk);
}

int rust_helper_clk_set_rate(struct clk *clk, unsigned long rate)
{
	return clk_set_rate(clk, rate);
}
#endif

#ifndef CONFIG_HAVE_CLK_PREPARE
int rust_helper_clk_prepare(struct clk *clk)
{
	return clk_prepare(clk);
}

void rust_helper_clk_unprepare(struct clk *clk)
{
	clk_unprepare(clk);
}
#endif

struct clk *rust_helper_clk_get_optional(struct device *dev, const char *id)
{
	return clk_get_optional(dev, id);
}

int rust_helper_clk_prepare_enable(struct clk *clk)
{
	return clk_prepare_enable(clk);
}

void rust_helper_clk_disable_unprepare(struct clk *clk)
{
	clk_disable_unprepare(clk);
}
