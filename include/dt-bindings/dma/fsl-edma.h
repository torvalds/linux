/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */

#ifndef _FSL_EDMA_DT_BINDING_H_
#define _FSL_EDMA_DT_BINDING_H_

/* Receive Channel */
#define FSL_EDMA_RX		0x1

/* iMX8 audio remote DMA */
#define FSL_EDMA_REMOTE		0x2

/* FIFO is continue memory region */
#define FSL_EDMA_MULTI_FIFO	0x4

/* Channel need stick to even channel */
#define FSL_EDMA_EVEN_CH	0x8

/* Channel need stick to odd channel */
#define FSL_EDMA_ODD_CH		0x10

#endif
