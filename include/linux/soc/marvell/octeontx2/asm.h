/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2020 Marvell.
 */

#ifndef __SOC_OTX2_ASM_H
#define __SOC_OTX2_ASM_H

#if defined(CONFIG_ARM64)
/*
 * otx2_lmt_flush is used for LMT store operation.
 * On octeontx2 platform CPT instruction enqueue and
 * NIX packet send are only possible via LMTST
 * operations and it uses LDEOR instruction targeting
 * the coprocessor address.
 */
#define otx2_lmt_flush(ioaddr)                          \
({                                                      \
	u64 result = 0;                                 \
	__asm__ volatile(".cpu  generic+lse\n"          \
			 "ldeor xzr, %x[rf], [%[rs]]"   \
			 : [rf]"=r" (result)            \
			 : [rs]"r" (ioaddr));           \
	(result);                                       \
})
#define cn10k_lmt_flush(val, addr)			\
({							\
	__asm__ volatile(".cpu  generic+lse\n"		\
			 "steor %x[rf],[%[rs]]"		\
			 : [rf]"+r"(val)		\
			 : [rs]"r"(addr));		\
})
#else
#define otx2_lmt_flush(ioaddr)          ({ 0; })
#define cn10k_lmt_flush(val, addr)	({ addr = val; })
#endif

#endif /* __SOC_OTX2_ASM_H */
