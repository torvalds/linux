/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __PXA_REGS_H
#define __PXA_REGS_H

#include <linux/types.h>

void pxa_smemc_set_pcmcia_timing(int sock, u32 mcmem, u32 mcatt, u32 mcio);
void pxa_smemc_set_pcmcia_socket(int nr);

#endif
