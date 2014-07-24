/*
 * IPMMU VMSA Platform Data
 *
 * Copyright (C) 2014 Renesas Electronics Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#ifndef __IPMMU_VMSA_H__
#define __IPMMU_VMSA_H__

struct ipmmu_vmsa_master {
	const char *name;
	unsigned int utlb;
};

struct ipmmu_vmsa_platform_data {
	const struct ipmmu_vmsa_master *masters;
	unsigned int num_masters;
};

#endif /* __IPMMU_VMSA_H__ */
