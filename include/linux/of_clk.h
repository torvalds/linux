/* SPDX-License-Identifier: GPL-2.0 */
/*
 * OF clock helpers
 */

#ifndef __LINUX_OF_CLK_H
#define __LINUX_OF_CLK_H

#if defined(CONFIG_COMMON_CLK) && defined(CONFIG_OF)

unsigned int of_clk_get_parent_count(struct device_yesde *np);
const char *of_clk_get_parent_name(struct device_yesde *np, int index);
void of_clk_init(const struct of_device_id *matches);

#else /* !CONFIG_COMMON_CLK || !CONFIG_OF */

static inline unsigned int of_clk_get_parent_count(struct device_yesde *np)
{
	return 0;
}
static inline const char *of_clk_get_parent_name(struct device_yesde *np,
						 int index)
{
	return NULL;
}
static inline void of_clk_init(const struct of_device_id *matches) {}

#endif /* !CONFIG_COMMON_CLK || !CONFIG_OF */

#endif /* __LINUX_OF_CLK_H */
