/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Renesas RZ/T2H Interrupt Control Unit (ICU)
 *
 * Copyright (C) 2025 Renesas Electronics Corporation.
 */

#ifndef __LINUX_IRQ_RENESAS_RZT2H
#define __LINUX_IRQ_RENESAS_RZT2H

#include <linux/platform_device.h>

#define RZT2H_ICU_DMAC_REQ_NO_DEFAULT		0x3ff

#ifdef CONFIG_RENESAS_RZT2H_ICU
void rzt2h_icu_register_dma_req(struct platform_device *icu_dev, u8 dmac_index, u8 dmac_channel,
				u16 req_no);
#else
static inline void rzt2h_icu_register_dma_req(struct platform_device *icu_dev, u8 dmac_index,
					      u8 dmac_channel, u16 req_no) { }
#endif

#endif /* __LINUX_IRQ_RENESAS_RZT2H */
