/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __RK_SDMMC_H
#define __RK_SDMMC_H

#if IS_ENABLED(CONFIG_CPU_RV1106) && IS_REACHABLE(CONFIG_MMC_DW)
void rv1106_sdmmc_get_lock(void);
void rv1106_sdmmc_put_lock(void);
#else
static inline void rv1106_sdmmc_get_lock(void) {}
static inline void rv1106_sdmmc_put_lock(void) {}
#endif

#endif
