/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __SOC_SPACEMIT_CCU_H__
#define __SOC_SPACEMIT_CCU_H__

#include <linux/auxiliary_bus.h>
#include <linux/regmap.h>

/* Auxiliary device used to represent a CCU reset controller */
struct spacemit_ccu_adev {
	struct auxiliary_device adev;
	struct regmap *regmap;
};

static inline struct spacemit_ccu_adev *
to_spacemit_ccu_adev(struct auxiliary_device *adev)
{
	return container_of(adev, struct spacemit_ccu_adev, adev);
}

#endif /* __SOC_SPACEMIT_CCU_H__ */
