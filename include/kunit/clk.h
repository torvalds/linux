/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _CLK_KUNIT_H
#define _CLK_KUNIT_H

struct clk;
struct clk_hw;
struct device;
struct device_node;
struct of_phandle_args;
struct kunit;

struct clk *
clk_get_kunit(struct kunit *test, struct device *dev, const char *con_id);
struct clk *
of_clk_get_kunit(struct kunit *test, struct device_node *np, int index);

struct clk *
clk_hw_get_clk_kunit(struct kunit *test, struct clk_hw *hw, const char *con_id);
struct clk *
clk_hw_get_clk_prepared_enabled_kunit(struct kunit *test, struct clk_hw *hw,
				      const char *con_id);

int clk_prepare_enable_kunit(struct kunit *test, struct clk *clk);

int clk_hw_register_kunit(struct kunit *test, struct device *dev, struct clk_hw *hw);
int of_clk_hw_register_kunit(struct kunit *test, struct device_node *node,
			     struct clk_hw *hw);

int of_clk_add_hw_provider_kunit(struct kunit *test, struct device_node *np,
				 struct clk_hw *(*get)(struct of_phandle_args *clkspec, void *data),
				 void *data);

#endif
