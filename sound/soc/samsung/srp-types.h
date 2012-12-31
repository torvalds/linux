/* linux/sound/soc/samsung/srp-type.h
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * EXYNOS - SRP Type definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __SND_SOC_SAMSUNG_SRP_TYPE_H
#define __SND_SOC_SAMSUNG_SRP_TYPE_H

#if defined(CONFIG_SND_SAMSUNG_RP)
#include "srp_ulp/srp_reg.h"
#elif defined(CONFIG_SND_SAMSUNG_ALP)
#include "srp_alp/srp_alp_reg.h"
#endif

enum {
	IS_RUNNING,
	IS_OPENED,
};

extern int srp_get_status(int cmd);

#endif /* __SND_SOC_SAMSUNG_SRP_TYPE_H */
