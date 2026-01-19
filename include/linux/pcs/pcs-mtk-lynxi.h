/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_PCS_MTK_LYNXI_H
#define __LINUX_PCS_MTK_LYNXI_H

#include <linux/phylink.h>
#include <linux/regmap.h>

struct phylink_pcs *mtk_pcs_lynxi_create(struct device *dev,
					 struct fwnode_handle *fwnode,
					 struct regmap *regmap, u32 ana_rgc3);
void mtk_pcs_lynxi_destroy(struct phylink_pcs *pcs);
#endif
