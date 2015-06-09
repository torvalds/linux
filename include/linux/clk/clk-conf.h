/*
 * Copyright (C) 2014 Samsung Electronics Co., Ltd.
 * Sylwester Nawrocki <s.nawrocki@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

struct device_node;

#if defined(CONFIG_OF) && defined(CONFIG_COMMON_CLK)
int of_clk_set_defaults(struct device_node *node, bool clk_supplier);
#else
static inline int of_clk_set_defaults(struct device_node *node,
				      bool clk_supplier)
{
	return 0;
}
#endif
