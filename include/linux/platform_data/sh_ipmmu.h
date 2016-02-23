/* sh_ipmmu.h
 *
 * Copyright (C) 2012  Hideki EIRAKU
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#ifndef __SH_IPMMU_H__
#define __SH_IPMMU_H__

struct shmobile_ipmmu_platform_data {
	const char * const *dev_names;
	unsigned int num_dev_names;
};

#endif /* __SH_IPMMU_H__ */
