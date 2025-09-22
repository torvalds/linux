/* Public domain. */

#ifndef _LINUX_CLK_H
#define _LINUX_CLK_H

struct clk {
	uint32_t freq;
};

unsigned long clk_get_rate(struct clk *);
struct clk *devm_clk_get(struct device *, const char *);
#define devm_clk_put(a, b)

#endif
