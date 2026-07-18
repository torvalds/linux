/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright (c) 2021, Microsoft Corporation. */

#ifndef _SHM_CHANNEL_H
#define _SHM_CHANNEL_H

#define SMC_APERTURE_BITS 256
#define SMC_BASIC_UNIT (sizeof(u32))
#define SMC_APERTURE_DWORDS (SMC_APERTURE_BITS / (SMC_BASIC_UNIT * 8))
#define SMC_LAST_DWORD (SMC_APERTURE_DWORDS - 1)
#define SMC_APERTURE_SIZE  (SMC_APERTURE_BITS / 8)

struct shm_channel {
	struct device *dev;
	void __iomem *base;
};

void mana_smc_init(struct shm_channel *sc, struct device *dev,
		   void __iomem *base);

int mana_smc_setup_hwc(struct shm_channel *sc, bool reset_vf, u64 eq_addr,
		       u64 cq_addr, u64 rq_addr, u64 sq_addr,
		       u32 eq_msix_index);

int mana_smc_teardown_hwc(struct shm_channel *sc, bool reset_vf);

#endif /* _SHM_CHANNEL_H */
