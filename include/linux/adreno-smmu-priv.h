// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Google, Inc
 */

#ifndef __ADRENO_SMMU_PRIV_H
#define __ADRENO_SMMU_PRIV_H

#include <linux/io-pgtable.h>

/**
 * struct adreno_smmu_priv - private interface between adreno-smmu and GPU
 *
 * @cookie:        An opque token provided by adreno-smmu and passed
 *                 back into the callbacks
 * @get_ttbr1_cfg: Get the TTBR1 config for the GPUs context-bank
 * @set_ttbr0_cfg: Set the TTBR0 config for the GPUs context bank.  A
 *                 NULL config disables TTBR0 translation, otherwise
 *                 TTBR0 translation is enabled with the specified cfg
 *
 * The GPU driver (drm/msm) and adreno-smmu work together for controlling
 * the GPU's SMMU instance.  This is by necessity, as the GPU is directly
 * updating the SMMU for context switches, while on the other hand we do
 * not want to duplicate all of the initial setup logic from arm-smmu.
 *
 * This private interface is used for the two drivers to coordinate.  The
 * cookie and callback functions are populated when the GPU driver attaches
 * it's domain.
 */
struct adreno_smmu_priv {
    const void *cookie;
    const struct io_pgtable_cfg *(*get_ttbr1_cfg)(const void *cookie);
    int (*set_ttbr0_cfg)(const void *cookie, const struct io_pgtable_cfg *cfg);
};

#endif /* __ADRENO_SMMU_PRIV_H */