/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __PXA_REGS_H
#define __PXA_REGS_H

#include <linux/types.h>

void pxa_smemc_set_pcmcia_timing(int sock, u32 mcmem, u32 mcatt, u32 mcio);
void pxa_smemc_set_pcmcia_socket(int nr);
int pxa2xx_smemc_get_sdram_rows(void);
unsigned int pxa3xx_smemc_get_memclkdiv(void);
void __iomem *pxa_smemc_get_mdrefr(void);

/*
 * Once fully converted to the clock framework, all these functions should be
 * removed, and replaced with a clk_get(NULL, "core").
 */
#ifdef CONFIG_PXA25x
extern unsigned pxa25x_get_clk_frequency_khz(int);
#else
#define pxa25x_get_clk_frequency_khz(x)		(0)
#endif

#ifdef CONFIG_PXA27x
extern unsigned pxa27x_get_clk_frequency_khz(int);
#else
#define pxa27x_get_clk_frequency_khz(x)		(0)
#endif

#endif
