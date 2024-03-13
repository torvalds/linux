/* SPDX-License-Identifier: (GPL-2.0 OR MIT) */

#ifndef __DT_BINDINGS_DMA_DW_DMAC_H__
#define __DT_BINDINGS_DMA_DW_DMAC_H__

/*
 * Protection Control bits provide protection against illegal transactions.
 * The protection bits[0:2] are one-to-one mapped to AHB HPROT[3:1] signals.
 */
#define DW_DMAC_HPROT1_PRIVILEGED_MODE	(1 << 0)	/* Privileged Mode */
#define DW_DMAC_HPROT2_BUFFERABLE	(1 << 1)	/* DMA is bufferable */
#define DW_DMAC_HPROT3_CACHEABLE	(1 << 2)	/* DMA is cacheable */

#endif /* __DT_BINDINGS_DMA_DW_DMAC_H__ */
