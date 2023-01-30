/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
 *
 * Copyright 2016-2022 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef HABANALABS_H_
#define HABANALABS_H_

#include <linux/types.h>
#include <linux/ioctl.h>

/*
 * Defines that are asic-specific but constitutes as ABI between kernel driver
 * and userspace
 */
#define GOYA_KMD_SRAM_RESERVED_SIZE_FROM_START		0x8000	/* 32KB */
#define GAUDI_DRIVER_SRAM_RESERVED_SIZE_FROM_START	0x80	/* 128 bytes */

/*
 * 128 SOBs reserved for collective wait
 * 16 SOBs reserved for sync stream
 */
#define GAUDI_FIRST_AVAILABLE_W_S_SYNC_OBJECT		144

/*
 * 64 monitors reserved for collective wait
 * 8 monitors reserved for sync stream
 */
#define GAUDI_FIRST_AVAILABLE_W_S_MONITOR		72

/* Max number of elements in timestamps registration buffers */
#define	TS_MAX_ELEMENTS_NUM				(1 << 20) /* 1MB */

/*
 * Goya queue Numbering
 *
 * The external queues (PCI DMA channels) MUST be before the internal queues
 * and each group (PCI DMA channels and internal) must be contiguous inside
 * itself but there can be a gap between the two groups (although not
 * recommended)
 */

enum goya_queue_id {
	GOYA_QUEUE_ID_DMA_0 = 0,
	GOYA_QUEUE_ID_DMA_1 = 1,
	GOYA_QUEUE_ID_DMA_2 = 2,
	GOYA_QUEUE_ID_DMA_3 = 3,
	GOYA_QUEUE_ID_DMA_4 = 4,
	GOYA_QUEUE_ID_CPU_PQ = 5,
	GOYA_QUEUE_ID_MME = 6,	/* Internal queues start here */
	GOYA_QUEUE_ID_TPC0 = 7,
	GOYA_QUEUE_ID_TPC1 = 8,
	GOYA_QUEUE_ID_TPC2 = 9,
	GOYA_QUEUE_ID_TPC3 = 10,
	GOYA_QUEUE_ID_TPC4 = 11,
	GOYA_QUEUE_ID_TPC5 = 12,
	GOYA_QUEUE_ID_TPC6 = 13,
	GOYA_QUEUE_ID_TPC7 = 14,
	GOYA_QUEUE_ID_SIZE
};

/*
 * Gaudi queue Numbering
 * External queues (PCI DMA channels) are DMA_0_*, DMA_1_* and DMA_5_*.
 * Except one CPU queue, all the rest are internal queues.
 */

enum gaudi_queue_id {
	GAUDI_QUEUE_ID_DMA_0_0 = 0,	/* external */
	GAUDI_QUEUE_ID_DMA_0_1 = 1,	/* external */
	GAUDI_QUEUE_ID_DMA_0_2 = 2,	/* external */
	GAUDI_QUEUE_ID_DMA_0_3 = 3,	/* external */
	GAUDI_QUEUE_ID_DMA_1_0 = 4,	/* external */
	GAUDI_QUEUE_ID_DMA_1_1 = 5,	/* external */
	GAUDI_QUEUE_ID_DMA_1_2 = 6,	/* external */
	GAUDI_QUEUE_ID_DMA_1_3 = 7,	/* external */
	GAUDI_QUEUE_ID_CPU_PQ = 8,	/* CPU */
	GAUDI_QUEUE_ID_DMA_2_0 = 9,	/* internal */
	GAUDI_QUEUE_ID_DMA_2_1 = 10,	/* internal */
	GAUDI_QUEUE_ID_DMA_2_2 = 11,	/* internal */
	GAUDI_QUEUE_ID_DMA_2_3 = 12,	/* internal */
	GAUDI_QUEUE_ID_DMA_3_0 = 13,	/* internal */
	GAUDI_QUEUE_ID_DMA_3_1 = 14,	/* internal */
	GAUDI_QUEUE_ID_DMA_3_2 = 15,	/* internal */
	GAUDI_QUEUE_ID_DMA_3_3 = 16,	/* internal */
	GAUDI_QUEUE_ID_DMA_4_0 = 17,	/* internal */
	GAUDI_QUEUE_ID_DMA_4_1 = 18,	/* internal */
	GAUDI_QUEUE_ID_DMA_4_2 = 19,	/* internal */
	GAUDI_QUEUE_ID_DMA_4_3 = 20,	/* internal */
	GAUDI_QUEUE_ID_DMA_5_0 = 21,	/* internal */
	GAUDI_QUEUE_ID_DMA_5_1 = 22,	/* internal */
	GAUDI_QUEUE_ID_DMA_5_2 = 23,	/* internal */
	GAUDI_QUEUE_ID_DMA_5_3 = 24,	/* internal */
	GAUDI_QUEUE_ID_DMA_6_0 = 25,	/* internal */
	GAUDI_QUEUE_ID_DMA_6_1 = 26,	/* internal */
	GAUDI_QUEUE_ID_DMA_6_2 = 27,	/* internal */
	GAUDI_QUEUE_ID_DMA_6_3 = 28,	/* internal */
	GAUDI_QUEUE_ID_DMA_7_0 = 29,	/* internal */
	GAUDI_QUEUE_ID_DMA_7_1 = 30,	/* internal */
	GAUDI_QUEUE_ID_DMA_7_2 = 31,	/* internal */
	GAUDI_QUEUE_ID_DMA_7_3 = 32,	/* internal */
	GAUDI_QUEUE_ID_MME_0_0 = 33,	/* internal */
	GAUDI_QUEUE_ID_MME_0_1 = 34,	/* internal */
	GAUDI_QUEUE_ID_MME_0_2 = 35,	/* internal */
	GAUDI_QUEUE_ID_MME_0_3 = 36,	/* internal */
	GAUDI_QUEUE_ID_MME_1_0 = 37,	/* internal */
	GAUDI_QUEUE_ID_MME_1_1 = 38,	/* internal */
	GAUDI_QUEUE_ID_MME_1_2 = 39,	/* internal */
	GAUDI_QUEUE_ID_MME_1_3 = 40,	/* internal */
	GAUDI_QUEUE_ID_TPC_0_0 = 41,	/* internal */
	GAUDI_QUEUE_ID_TPC_0_1 = 42,	/* internal */
	GAUDI_QUEUE_ID_TPC_0_2 = 43,	/* internal */
	GAUDI_QUEUE_ID_TPC_0_3 = 44,	/* internal */
	GAUDI_QUEUE_ID_TPC_1_0 = 45,	/* internal */
	GAUDI_QUEUE_ID_TPC_1_1 = 46,	/* internal */
	GAUDI_QUEUE_ID_TPC_1_2 = 47,	/* internal */
	GAUDI_QUEUE_ID_TPC_1_3 = 48,	/* internal */
	GAUDI_QUEUE_ID_TPC_2_0 = 49,	/* internal */
	GAUDI_QUEUE_ID_TPC_2_1 = 50,	/* internal */
	GAUDI_QUEUE_ID_TPC_2_2 = 51,	/* internal */
	GAUDI_QUEUE_ID_TPC_2_3 = 52,	/* internal */
	GAUDI_QUEUE_ID_TPC_3_0 = 53,	/* internal */
	GAUDI_QUEUE_ID_TPC_3_1 = 54,	/* internal */
	GAUDI_QUEUE_ID_TPC_3_2 = 55,	/* internal */
	GAUDI_QUEUE_ID_TPC_3_3 = 56,	/* internal */
	GAUDI_QUEUE_ID_TPC_4_0 = 57,	/* internal */
	GAUDI_QUEUE_ID_TPC_4_1 = 58,	/* internal */
	GAUDI_QUEUE_ID_TPC_4_2 = 59,	/* internal */
	GAUDI_QUEUE_ID_TPC_4_3 = 60,	/* internal */
	GAUDI_QUEUE_ID_TPC_5_0 = 61,	/* internal */
	GAUDI_QUEUE_ID_TPC_5_1 = 62,	/* internal */
	GAUDI_QUEUE_ID_TPC_5_2 = 63,	/* internal */
	GAUDI_QUEUE_ID_TPC_5_3 = 64,	/* internal */
	GAUDI_QUEUE_ID_TPC_6_0 = 65,	/* internal */
	GAUDI_QUEUE_ID_TPC_6_1 = 66,	/* internal */
	GAUDI_QUEUE_ID_TPC_6_2 = 67,	/* internal */
	GAUDI_QUEUE_ID_TPC_6_3 = 68,	/* internal */
	GAUDI_QUEUE_ID_TPC_7_0 = 69,	/* internal */
	GAUDI_QUEUE_ID_TPC_7_1 = 70,	/* internal */
	GAUDI_QUEUE_ID_TPC_7_2 = 71,	/* internal */
	GAUDI_QUEUE_ID_TPC_7_3 = 72,	/* internal */
	GAUDI_QUEUE_ID_NIC_0_0 = 73,	/* internal */
	GAUDI_QUEUE_ID_NIC_0_1 = 74,	/* internal */
	GAUDI_QUEUE_ID_NIC_0_2 = 75,	/* internal */
	GAUDI_QUEUE_ID_NIC_0_3 = 76,	/* internal */
	GAUDI_QUEUE_ID_NIC_1_0 = 77,	/* internal */
	GAUDI_QUEUE_ID_NIC_1_1 = 78,	/* internal */
	GAUDI_QUEUE_ID_NIC_1_2 = 79,	/* internal */
	GAUDI_QUEUE_ID_NIC_1_3 = 80,	/* internal */
	GAUDI_QUEUE_ID_NIC_2_0 = 81,	/* internal */
	GAUDI_QUEUE_ID_NIC_2_1 = 82,	/* internal */
	GAUDI_QUEUE_ID_NIC_2_2 = 83,	/* internal */
	GAUDI_QUEUE_ID_NIC_2_3 = 84,	/* internal */
	GAUDI_QUEUE_ID_NIC_3_0 = 85,	/* internal */
	GAUDI_QUEUE_ID_NIC_3_1 = 86,	/* internal */
	GAUDI_QUEUE_ID_NIC_3_2 = 87,	/* internal */
	GAUDI_QUEUE_ID_NIC_3_3 = 88,	/* internal */
	GAUDI_QUEUE_ID_NIC_4_0 = 89,	/* internal */
	GAUDI_QUEUE_ID_NIC_4_1 = 90,	/* internal */
	GAUDI_QUEUE_ID_NIC_4_2 = 91,	/* internal */
	GAUDI_QUEUE_ID_NIC_4_3 = 92,	/* internal */
	GAUDI_QUEUE_ID_NIC_5_0 = 93,	/* internal */
	GAUDI_QUEUE_ID_NIC_5_1 = 94,	/* internal */
	GAUDI_QUEUE_ID_NIC_5_2 = 95,	/* internal */
	GAUDI_QUEUE_ID_NIC_5_3 = 96,	/* internal */
	GAUDI_QUEUE_ID_NIC_6_0 = 97,	/* internal */
	GAUDI_QUEUE_ID_NIC_6_1 = 98,	/* internal */
	GAUDI_QUEUE_ID_NIC_6_2 = 99,	/* internal */
	GAUDI_QUEUE_ID_NIC_6_3 = 100,	/* internal */
	GAUDI_QUEUE_ID_NIC_7_0 = 101,	/* internal */
	GAUDI_QUEUE_ID_NIC_7_1 = 102,	/* internal */
	GAUDI_QUEUE_ID_NIC_7_2 = 103,	/* internal */
	GAUDI_QUEUE_ID_NIC_7_3 = 104,	/* internal */
	GAUDI_QUEUE_ID_NIC_8_0 = 105,	/* internal */
	GAUDI_QUEUE_ID_NIC_8_1 = 106,	/* internal */
	GAUDI_QUEUE_ID_NIC_8_2 = 107,	/* internal */
	GAUDI_QUEUE_ID_NIC_8_3 = 108,	/* internal */
	GAUDI_QUEUE_ID_NIC_9_0 = 109,	/* internal */
	GAUDI_QUEUE_ID_NIC_9_1 = 110,	/* internal */
	GAUDI_QUEUE_ID_NIC_9_2 = 111,	/* internal */
	GAUDI_QUEUE_ID_NIC_9_3 = 112,	/* internal */
	GAUDI_QUEUE_ID_SIZE
};

/*
 * In GAUDI2 we have two modes of operation in regard to queues:
 * 1. Legacy mode, where each QMAN exposes 4 streams to the user
 * 2. F/W mode, where we use F/W to schedule the JOBS to the different queues.
 *
 * When in legacy mode, the user sends the queue id per JOB according to
 * enum gaudi2_queue_id below.
 *
 * When in F/W mode, the user sends a stream id per Command Submission. The
 * stream id is a running number from 0 up to (N-1), where N is the number
 * of streams the F/W exposes and is passed to the user in
 * struct hl_info_hw_ip_info
 */

enum gaudi2_queue_id {
	GAUDI2_QUEUE_ID_PDMA_0_0 = 0,
	GAUDI2_QUEUE_ID_PDMA_0_1 = 1,
	GAUDI2_QUEUE_ID_PDMA_0_2 = 2,
	GAUDI2_QUEUE_ID_PDMA_0_3 = 3,
	GAUDI2_QUEUE_ID_PDMA_1_0 = 4,
	GAUDI2_QUEUE_ID_PDMA_1_1 = 5,
	GAUDI2_QUEUE_ID_PDMA_1_2 = 6,
	GAUDI2_QUEUE_ID_PDMA_1_3 = 7,
	GAUDI2_QUEUE_ID_DCORE0_EDMA_0_0 = 8,
	GAUDI2_QUEUE_ID_DCORE0_EDMA_0_1 = 9,
	GAUDI2_QUEUE_ID_DCORE0_EDMA_0_2 = 10,
	GAUDI2_QUEUE_ID_DCORE0_EDMA_0_3 = 11,
	GAUDI2_QUEUE_ID_DCORE0_EDMA_1_0 = 12,
	GAUDI2_QUEUE_ID_DCORE0_EDMA_1_1 = 13,
	GAUDI2_QUEUE_ID_DCORE0_EDMA_1_2 = 14,
	GAUDI2_QUEUE_ID_DCORE0_EDMA_1_3 = 15,
	GAUDI2_QUEUE_ID_DCORE0_MME_0_0 = 16,
	GAUDI2_QUEUE_ID_DCORE0_MME_0_1 = 17,
	GAUDI2_QUEUE_ID_DCORE0_MME_0_2 = 18,
	GAUDI2_QUEUE_ID_DCORE0_MME_0_3 = 19,
	GAUDI2_QUEUE_ID_DCORE0_TPC_0_0 = 20,
	GAUDI2_QUEUE_ID_DCORE0_TPC_0_1 = 21,
	GAUDI2_QUEUE_ID_DCORE0_TPC_0_2 = 22,
	GAUDI2_QUEUE_ID_DCORE0_TPC_0_3 = 23,
	GAUDI2_QUEUE_ID_DCORE0_TPC_1_0 = 24,
	GAUDI2_QUEUE_ID_DCORE0_TPC_1_1 = 25,
	GAUDI2_QUEUE_ID_DCORE0_TPC_1_2 = 26,
	GAUDI2_QUEUE_ID_DCORE0_TPC_1_3 = 27,
	GAUDI2_QUEUE_ID_DCORE0_TPC_2_0 = 28,
	GAUDI2_QUEUE_ID_DCORE0_TPC_2_1 = 29,
	GAUDI2_QUEUE_ID_DCORE0_TPC_2_2 = 30,
	GAUDI2_QUEUE_ID_DCORE0_TPC_2_3 = 31,
	GAUDI2_QUEUE_ID_DCORE0_TPC_3_0 = 32,
	GAUDI2_QUEUE_ID_DCORE0_TPC_3_1 = 33,
	GAUDI2_QUEUE_ID_DCORE0_TPC_3_2 = 34,
	GAUDI2_QUEUE_ID_DCORE0_TPC_3_3 = 35,
	GAUDI2_QUEUE_ID_DCORE0_TPC_4_0 = 36,
	GAUDI2_QUEUE_ID_DCORE0_TPC_4_1 = 37,
	GAUDI2_QUEUE_ID_DCORE0_TPC_4_2 = 38,
	GAUDI2_QUEUE_ID_DCORE0_TPC_4_3 = 39,
	GAUDI2_QUEUE_ID_DCORE0_TPC_5_0 = 40,
	GAUDI2_QUEUE_ID_DCORE0_TPC_5_1 = 41,
	GAUDI2_QUEUE_ID_DCORE0_TPC_5_2 = 42,
	GAUDI2_QUEUE_ID_DCORE0_TPC_5_3 = 43,
	GAUDI2_QUEUE_ID_DCORE0_TPC_6_0 = 44,
	GAUDI2_QUEUE_ID_DCORE0_TPC_6_1 = 45,
	GAUDI2_QUEUE_ID_DCORE0_TPC_6_2 = 46,
	GAUDI2_QUEUE_ID_DCORE0_TPC_6_3 = 47,
	GAUDI2_QUEUE_ID_DCORE1_EDMA_0_0 = 48,
	GAUDI2_QUEUE_ID_DCORE1_EDMA_0_1 = 49,
	GAUDI2_QUEUE_ID_DCORE1_EDMA_0_2 = 50,
	GAUDI2_QUEUE_ID_DCORE1_EDMA_0_3 = 51,
	GAUDI2_QUEUE_ID_DCORE1_EDMA_1_0 = 52,
	GAUDI2_QUEUE_ID_DCORE1_EDMA_1_1 = 53,
	GAUDI2_QUEUE_ID_DCORE1_EDMA_1_2 = 54,
	GAUDI2_QUEUE_ID_DCORE1_EDMA_1_3 = 55,
	GAUDI2_QUEUE_ID_DCORE1_MME_0_0 = 56,
	GAUDI2_QUEUE_ID_DCORE1_MME_0_1 = 57,
	GAUDI2_QUEUE_ID_DCORE1_MME_0_2 = 58,
	GAUDI2_QUEUE_ID_DCORE1_MME_0_3 = 59,
	GAUDI2_QUEUE_ID_DCORE1_TPC_0_0 = 60,
	GAUDI2_QUEUE_ID_DCORE1_TPC_0_1 = 61,
	GAUDI2_QUEUE_ID_DCORE1_TPC_0_2 = 62,
	GAUDI2_QUEUE_ID_DCORE1_TPC_0_3 = 63,
	GAUDI2_QUEUE_ID_DCORE1_TPC_1_0 = 64,
	GAUDI2_QUEUE_ID_DCORE1_TPC_1_1 = 65,
	GAUDI2_QUEUE_ID_DCORE1_TPC_1_2 = 66,
	GAUDI2_QUEUE_ID_DCORE1_TPC_1_3 = 67,
	GAUDI2_QUEUE_ID_DCORE1_TPC_2_0 = 68,
	GAUDI2_QUEUE_ID_DCORE1_TPC_2_1 = 69,
	GAUDI2_QUEUE_ID_DCORE1_TPC_2_2 = 70,
	GAUDI2_QUEUE_ID_DCORE1_TPC_2_3 = 71,
	GAUDI2_QUEUE_ID_DCORE1_TPC_3_0 = 72,
	GAUDI2_QUEUE_ID_DCORE1_TPC_3_1 = 73,
	GAUDI2_QUEUE_ID_DCORE1_TPC_3_2 = 74,
	GAUDI2_QUEUE_ID_DCORE1_TPC_3_3 = 75,
	GAUDI2_QUEUE_ID_DCORE1_TPC_4_0 = 76,
	GAUDI2_QUEUE_ID_DCORE1_TPC_4_1 = 77,
	GAUDI2_QUEUE_ID_DCORE1_TPC_4_2 = 78,
	GAUDI2_QUEUE_ID_DCORE1_TPC_4_3 = 79,
	GAUDI2_QUEUE_ID_DCORE1_TPC_5_0 = 80,
	GAUDI2_QUEUE_ID_DCORE1_TPC_5_1 = 81,
	GAUDI2_QUEUE_ID_DCORE1_TPC_5_2 = 82,
	GAUDI2_QUEUE_ID_DCORE1_TPC_5_3 = 83,
	GAUDI2_QUEUE_ID_DCORE2_EDMA_0_0 = 84,
	GAUDI2_QUEUE_ID_DCORE2_EDMA_0_1 = 85,
	GAUDI2_QUEUE_ID_DCORE2_EDMA_0_2 = 86,
	GAUDI2_QUEUE_ID_DCORE2_EDMA_0_3 = 87,
	GAUDI2_QUEUE_ID_DCORE2_EDMA_1_0 = 88,
	GAUDI2_QUEUE_ID_DCORE2_EDMA_1_1 = 89,
	GAUDI2_QUEUE_ID_DCORE2_EDMA_1_2 = 90,
	GAUDI2_QUEUE_ID_DCORE2_EDMA_1_3 = 91,
	GAUDI2_QUEUE_ID_DCORE2_MME_0_0 = 92,
	GAUDI2_QUEUE_ID_DCORE2_MME_0_1 = 93,
	GAUDI2_QUEUE_ID_DCORE2_MME_0_2 = 94,
	GAUDI2_QUEUE_ID_DCORE2_MME_0_3 = 95,
	GAUDI2_QUEUE_ID_DCORE2_TPC_0_0 = 96,
	GAUDI2_QUEUE_ID_DCORE2_TPC_0_1 = 97,
	GAUDI2_QUEUE_ID_DCORE2_TPC_0_2 = 98,
	GAUDI2_QUEUE_ID_DCORE2_TPC_0_3 = 99,
	GAUDI2_QUEUE_ID_DCORE2_TPC_1_0 = 100,
	GAUDI2_QUEUE_ID_DCORE2_TPC_1_1 = 101,
	GAUDI2_QUEUE_ID_DCORE2_TPC_1_2 = 102,
	GAUDI2_QUEUE_ID_DCORE2_TPC_1_3 = 103,
	GAUDI2_QUEUE_ID_DCORE2_TPC_2_0 = 104,
	GAUDI2_QUEUE_ID_DCORE2_TPC_2_1 = 105,
	GAUDI2_QUEUE_ID_DCORE2_TPC_2_2 = 106,
	GAUDI2_QUEUE_ID_DCORE2_TPC_2_3 = 107,
	GAUDI2_QUEUE_ID_DCORE2_TPC_3_0 = 108,
	GAUDI2_QUEUE_ID_DCORE2_TPC_3_1 = 109,
	GAUDI2_QUEUE_ID_DCORE2_TPC_3_2 = 110,
	GAUDI2_QUEUE_ID_DCORE2_TPC_3_3 = 111,
	GAUDI2_QUEUE_ID_DCORE2_TPC_4_0 = 112,
	GAUDI2_QUEUE_ID_DCORE2_TPC_4_1 = 113,
	GAUDI2_QUEUE_ID_DCORE2_TPC_4_2 = 114,
	GAUDI2_QUEUE_ID_DCORE2_TPC_4_3 = 115,
	GAUDI2_QUEUE_ID_DCORE2_TPC_5_0 = 116,
	GAUDI2_QUEUE_ID_DCORE2_TPC_5_1 = 117,
	GAUDI2_QUEUE_ID_DCORE2_TPC_5_2 = 118,
	GAUDI2_QUEUE_ID_DCORE2_TPC_5_3 = 119,
	GAUDI2_QUEUE_ID_DCORE3_EDMA_0_0 = 120,
	GAUDI2_QUEUE_ID_DCORE3_EDMA_0_1 = 121,
	GAUDI2_QUEUE_ID_DCORE3_EDMA_0_2 = 122,
	GAUDI2_QUEUE_ID_DCORE3_EDMA_0_3 = 123,
	GAUDI2_QUEUE_ID_DCORE3_EDMA_1_0 = 124,
	GAUDI2_QUEUE_ID_DCORE3_EDMA_1_1 = 125,
	GAUDI2_QUEUE_ID_DCORE3_EDMA_1_2 = 126,
	GAUDI2_QUEUE_ID_DCORE3_EDMA_1_3 = 127,
	GAUDI2_QUEUE_ID_DCORE3_MME_0_0 = 128,
	GAUDI2_QUEUE_ID_DCORE3_MME_0_1 = 129,
	GAUDI2_QUEUE_ID_DCORE3_MME_0_2 = 130,
	GAUDI2_QUEUE_ID_DCORE3_MME_0_3 = 131,
	GAUDI2_QUEUE_ID_DCORE3_TPC_0_0 = 132,
	GAUDI2_QUEUE_ID_DCORE3_TPC_0_1 = 133,
	GAUDI2_QUEUE_ID_DCORE3_TPC_0_2 = 134,
	GAUDI2_QUEUE_ID_DCORE3_TPC_0_3 = 135,
	GAUDI2_QUEUE_ID_DCORE3_TPC_1_0 = 136,
	GAUDI2_QUEUE_ID_DCORE3_TPC_1_1 = 137,
	GAUDI2_QUEUE_ID_DCORE3_TPC_1_2 = 138,
	GAUDI2_QUEUE_ID_DCORE3_TPC_1_3 = 139,
	GAUDI2_QUEUE_ID_DCORE3_TPC_2_0 = 140,
	GAUDI2_QUEUE_ID_DCORE3_TPC_2_1 = 141,
	GAUDI2_QUEUE_ID_DCORE3_TPC_2_2 = 142,
	GAUDI2_QUEUE_ID_DCORE3_TPC_2_3 = 143,
	GAUDI2_QUEUE_ID_DCORE3_TPC_3_0 = 144,
	GAUDI2_QUEUE_ID_DCORE3_TPC_3_1 = 145,
	GAUDI2_QUEUE_ID_DCORE3_TPC_3_2 = 146,
	GAUDI2_QUEUE_ID_DCORE3_TPC_3_3 = 147,
	GAUDI2_QUEUE_ID_DCORE3_TPC_4_0 = 148,
	GAUDI2_QUEUE_ID_DCORE3_TPC_4_1 = 149,
	GAUDI2_QUEUE_ID_DCORE3_TPC_4_2 = 150,
	GAUDI2_QUEUE_ID_DCORE3_TPC_4_3 = 151,
	GAUDI2_QUEUE_ID_DCORE3_TPC_5_0 = 152,
	GAUDI2_QUEUE_ID_DCORE3_TPC_5_1 = 153,
	GAUDI2_QUEUE_ID_DCORE3_TPC_5_2 = 154,
	GAUDI2_QUEUE_ID_DCORE3_TPC_5_3 = 155,
	GAUDI2_QUEUE_ID_NIC_0_0 = 156,
	GAUDI2_QUEUE_ID_NIC_0_1 = 157,
	GAUDI2_QUEUE_ID_NIC_0_2 = 158,
	GAUDI2_QUEUE_ID_NIC_0_3 = 159,
	GAUDI2_QUEUE_ID_NIC_1_0 = 160,
	GAUDI2_QUEUE_ID_NIC_1_1 = 161,
	GAUDI2_QUEUE_ID_NIC_1_2 = 162,
	GAUDI2_QUEUE_ID_NIC_1_3 = 163,
	GAUDI2_QUEUE_ID_NIC_2_0 = 164,
	GAUDI2_QUEUE_ID_NIC_2_1 = 165,
	GAUDI2_QUEUE_ID_NIC_2_2 = 166,
	GAUDI2_QUEUE_ID_NIC_2_3 = 167,
	GAUDI2_QUEUE_ID_NIC_3_0 = 168,
	GAUDI2_QUEUE_ID_NIC_3_1 = 169,
	GAUDI2_QUEUE_ID_NIC_3_2 = 170,
	GAUDI2_QUEUE_ID_NIC_3_3 = 171,
	GAUDI2_QUEUE_ID_NIC_4_0 = 172,
	GAUDI2_QUEUE_ID_NIC_4_1 = 173,
	GAUDI2_QUEUE_ID_NIC_4_2 = 174,
	GAUDI2_QUEUE_ID_NIC_4_3 = 175,
	GAUDI2_QUEUE_ID_NIC_5_0 = 176,
	GAUDI2_QUEUE_ID_NIC_5_1 = 177,
	GAUDI2_QUEUE_ID_NIC_5_2 = 178,
	GAUDI2_QUEUE_ID_NIC_5_3 = 179,
	GAUDI2_QUEUE_ID_NIC_6_0 = 180,
	GAUDI2_QUEUE_ID_NIC_6_1 = 181,
	GAUDI2_QUEUE_ID_NIC_6_2 = 182,
	GAUDI2_QUEUE_ID_NIC_6_3 = 183,
	GAUDI2_QUEUE_ID_NIC_7_0 = 184,
	GAUDI2_QUEUE_ID_NIC_7_1 = 185,
	GAUDI2_QUEUE_ID_NIC_7_2 = 186,
	GAUDI2_QUEUE_ID_NIC_7_3 = 187,
	GAUDI2_QUEUE_ID_NIC_8_0 = 188,
	GAUDI2_QUEUE_ID_NIC_8_1 = 189,
	GAUDI2_QUEUE_ID_NIC_8_2 = 190,
	GAUDI2_QUEUE_ID_NIC_8_3 = 191,
	GAUDI2_QUEUE_ID_NIC_9_0 = 192,
	GAUDI2_QUEUE_ID_NIC_9_1 = 193,
	GAUDI2_QUEUE_ID_NIC_9_2 = 194,
	GAUDI2_QUEUE_ID_NIC_9_3 = 195,
	GAUDI2_QUEUE_ID_NIC_10_0 = 196,
	GAUDI2_QUEUE_ID_NIC_10_1 = 197,
	GAUDI2_QUEUE_ID_NIC_10_2 = 198,
	GAUDI2_QUEUE_ID_NIC_10_3 = 199,
	GAUDI2_QUEUE_ID_NIC_11_0 = 200,
	GAUDI2_QUEUE_ID_NIC_11_1 = 201,
	GAUDI2_QUEUE_ID_NIC_11_2 = 202,
	GAUDI2_QUEUE_ID_NIC_11_3 = 203,
	GAUDI2_QUEUE_ID_NIC_12_0 = 204,
	GAUDI2_QUEUE_ID_NIC_12_1 = 205,
	GAUDI2_QUEUE_ID_NIC_12_2 = 206,
	GAUDI2_QUEUE_ID_NIC_12_3 = 207,
	GAUDI2_QUEUE_ID_NIC_13_0 = 208,
	GAUDI2_QUEUE_ID_NIC_13_1 = 209,
	GAUDI2_QUEUE_ID_NIC_13_2 = 210,
	GAUDI2_QUEUE_ID_NIC_13_3 = 211,
	GAUDI2_QUEUE_ID_NIC_14_0 = 212,
	GAUDI2_QUEUE_ID_NIC_14_1 = 213,
	GAUDI2_QUEUE_ID_NIC_14_2 = 214,
	GAUDI2_QUEUE_ID_NIC_14_3 = 215,
	GAUDI2_QUEUE_ID_NIC_15_0 = 216,
	GAUDI2_QUEUE_ID_NIC_15_1 = 217,
	GAUDI2_QUEUE_ID_NIC_15_2 = 218,
	GAUDI2_QUEUE_ID_NIC_15_3 = 219,
	GAUDI2_QUEUE_ID_NIC_16_0 = 220,
	GAUDI2_QUEUE_ID_NIC_16_1 = 221,
	GAUDI2_QUEUE_ID_NIC_16_2 = 222,
	GAUDI2_QUEUE_ID_NIC_16_3 = 223,
	GAUDI2_QUEUE_ID_NIC_17_0 = 224,
	GAUDI2_QUEUE_ID_NIC_17_1 = 225,
	GAUDI2_QUEUE_ID_NIC_17_2 = 226,
	GAUDI2_QUEUE_ID_NIC_17_3 = 227,
	GAUDI2_QUEUE_ID_NIC_18_0 = 228,
	GAUDI2_QUEUE_ID_NIC_18_1 = 229,
	GAUDI2_QUEUE_ID_NIC_18_2 = 230,
	GAUDI2_QUEUE_ID_NIC_18_3 = 231,
	GAUDI2_QUEUE_ID_NIC_19_0 = 232,
	GAUDI2_QUEUE_ID_NIC_19_1 = 233,
	GAUDI2_QUEUE_ID_NIC_19_2 = 234,
	GAUDI2_QUEUE_ID_NIC_19_3 = 235,
	GAUDI2_QUEUE_ID_NIC_20_0 = 236,
	GAUDI2_QUEUE_ID_NIC_20_1 = 237,
	GAUDI2_QUEUE_ID_NIC_20_2 = 238,
	GAUDI2_QUEUE_ID_NIC_20_3 = 239,
	GAUDI2_QUEUE_ID_NIC_21_0 = 240,
	GAUDI2_QUEUE_ID_NIC_21_1 = 241,
	GAUDI2_QUEUE_ID_NIC_21_2 = 242,
	GAUDI2_QUEUE_ID_NIC_21_3 = 243,
	GAUDI2_QUEUE_ID_NIC_22_0 = 244,
	GAUDI2_QUEUE_ID_NIC_22_1 = 245,
	GAUDI2_QUEUE_ID_NIC_22_2 = 246,
	GAUDI2_QUEUE_ID_NIC_22_3 = 247,
	GAUDI2_QUEUE_ID_NIC_23_0 = 248,
	GAUDI2_QUEUE_ID_NIC_23_1 = 249,
	GAUDI2_QUEUE_ID_NIC_23_2 = 250,
	GAUDI2_QUEUE_ID_NIC_23_3 = 251,
	GAUDI2_QUEUE_ID_ROT_0_0 = 252,
	GAUDI2_QUEUE_ID_ROT_0_1 = 253,
	GAUDI2_QUEUE_ID_ROT_0_2 = 254,
	GAUDI2_QUEUE_ID_ROT_0_3 = 255,
	GAUDI2_QUEUE_ID_ROT_1_0 = 256,
	GAUDI2_QUEUE_ID_ROT_1_1 = 257,
	GAUDI2_QUEUE_ID_ROT_1_2 = 258,
	GAUDI2_QUEUE_ID_ROT_1_3 = 259,
	GAUDI2_QUEUE_ID_CPU_PQ = 260,
	GAUDI2_QUEUE_ID_SIZE
};

/*
 * Engine Numbering
 *
 * Used in the "busy_engines_mask" field in `struct hl_info_hw_idle'
 */

enum goya_engine_id {
	GOYA_ENGINE_ID_DMA_0 = 0,
	GOYA_ENGINE_ID_DMA_1,
	GOYA_ENGINE_ID_DMA_2,
	GOYA_ENGINE_ID_DMA_3,
	GOYA_ENGINE_ID_DMA_4,
	GOYA_ENGINE_ID_MME_0,
	GOYA_ENGINE_ID_TPC_0,
	GOYA_ENGINE_ID_TPC_1,
	GOYA_ENGINE_ID_TPC_2,
	GOYA_ENGINE_ID_TPC_3,
	GOYA_ENGINE_ID_TPC_4,
	GOYA_ENGINE_ID_TPC_5,
	GOYA_ENGINE_ID_TPC_6,
	GOYA_ENGINE_ID_TPC_7,
	GOYA_ENGINE_ID_SIZE
};

enum gaudi_engine_id {
	GAUDI_ENGINE_ID_DMA_0 = 0,
	GAUDI_ENGINE_ID_DMA_1,
	GAUDI_ENGINE_ID_DMA_2,
	GAUDI_ENGINE_ID_DMA_3,
	GAUDI_ENGINE_ID_DMA_4,
	GAUDI_ENGINE_ID_DMA_5,
	GAUDI_ENGINE_ID_DMA_6,
	GAUDI_ENGINE_ID_DMA_7,
	GAUDI_ENGINE_ID_MME_0,
	GAUDI_ENGINE_ID_MME_1,
	GAUDI_ENGINE_ID_MME_2,
	GAUDI_ENGINE_ID_MME_3,
	GAUDI_ENGINE_ID_TPC_0,
	GAUDI_ENGINE_ID_TPC_1,
	GAUDI_ENGINE_ID_TPC_2,
	GAUDI_ENGINE_ID_TPC_3,
	GAUDI_ENGINE_ID_TPC_4,
	GAUDI_ENGINE_ID_TPC_5,
	GAUDI_ENGINE_ID_TPC_6,
	GAUDI_ENGINE_ID_TPC_7,
	GAUDI_ENGINE_ID_NIC_0,
	GAUDI_ENGINE_ID_NIC_1,
	GAUDI_ENGINE_ID_NIC_2,
	GAUDI_ENGINE_ID_NIC_3,
	GAUDI_ENGINE_ID_NIC_4,
	GAUDI_ENGINE_ID_NIC_5,
	GAUDI_ENGINE_ID_NIC_6,
	GAUDI_ENGINE_ID_NIC_7,
	GAUDI_ENGINE_ID_NIC_8,
	GAUDI_ENGINE_ID_NIC_9,
	GAUDI_ENGINE_ID_SIZE
};

enum gaudi2_engine_id {
	GAUDI2_DCORE0_ENGINE_ID_EDMA_0 = 0,
	GAUDI2_DCORE0_ENGINE_ID_EDMA_1,
	GAUDI2_DCORE0_ENGINE_ID_MME,
	GAUDI2_DCORE0_ENGINE_ID_TPC_0,
	GAUDI2_DCORE0_ENGINE_ID_TPC_1,
	GAUDI2_DCORE0_ENGINE_ID_TPC_2,
	GAUDI2_DCORE0_ENGINE_ID_TPC_3,
	GAUDI2_DCORE0_ENGINE_ID_TPC_4,
	GAUDI2_DCORE0_ENGINE_ID_TPC_5,
	GAUDI2_DCORE0_ENGINE_ID_DEC_0,
	GAUDI2_DCORE0_ENGINE_ID_DEC_1,
	GAUDI2_DCORE1_ENGINE_ID_EDMA_0,
	GAUDI2_DCORE1_ENGINE_ID_EDMA_1,
	GAUDI2_DCORE1_ENGINE_ID_MME,
	GAUDI2_DCORE1_ENGINE_ID_TPC_0,
	GAUDI2_DCORE1_ENGINE_ID_TPC_1,
	GAUDI2_DCORE1_ENGINE_ID_TPC_2,
	GAUDI2_DCORE1_ENGINE_ID_TPC_3,
	GAUDI2_DCORE1_ENGINE_ID_TPC_4,
	GAUDI2_DCORE1_ENGINE_ID_TPC_5,
	GAUDI2_DCORE1_ENGINE_ID_DEC_0,
	GAUDI2_DCORE1_ENGINE_ID_DEC_1,
	GAUDI2_DCORE2_ENGINE_ID_EDMA_0,
	GAUDI2_DCORE2_ENGINE_ID_EDMA_1,
	GAUDI2_DCORE2_ENGINE_ID_MME,
	GAUDI2_DCORE2_ENGINE_ID_TPC_0,
	GAUDI2_DCORE2_ENGINE_ID_TPC_1,
	GAUDI2_DCORE2_ENGINE_ID_TPC_2,
	GAUDI2_DCORE2_ENGINE_ID_TPC_3,
	GAUDI2_DCORE2_ENGINE_ID_TPC_4,
	GAUDI2_DCORE2_ENGINE_ID_TPC_5,
	GAUDI2_DCORE2_ENGINE_ID_DEC_0,
	GAUDI2_DCORE2_ENGINE_ID_DEC_1,
	GAUDI2_DCORE3_ENGINE_ID_EDMA_0,
	GAUDI2_DCORE3_ENGINE_ID_EDMA_1,
	GAUDI2_DCORE3_ENGINE_ID_MME,
	GAUDI2_DCORE3_ENGINE_ID_TPC_0,
	GAUDI2_DCORE3_ENGINE_ID_TPC_1,
	GAUDI2_DCORE3_ENGINE_ID_TPC_2,
	GAUDI2_DCORE3_ENGINE_ID_TPC_3,
	GAUDI2_DCORE3_ENGINE_ID_TPC_4,
	GAUDI2_DCORE3_ENGINE_ID_TPC_5,
	GAUDI2_DCORE3_ENGINE_ID_DEC_0,
	GAUDI2_DCORE3_ENGINE_ID_DEC_1,
	GAUDI2_DCORE0_ENGINE_ID_TPC_6,
	GAUDI2_ENGINE_ID_PDMA_0,
	GAUDI2_ENGINE_ID_PDMA_1,
	GAUDI2_ENGINE_ID_ROT_0,
	GAUDI2_ENGINE_ID_ROT_1,
	GAUDI2_PCIE_ENGINE_ID_DEC_0,
	GAUDI2_PCIE_ENGINE_ID_DEC_1,
	GAUDI2_ENGINE_ID_NIC0_0,
	GAUDI2_ENGINE_ID_NIC0_1,
	GAUDI2_ENGINE_ID_NIC1_0,
	GAUDI2_ENGINE_ID_NIC1_1,
	GAUDI2_ENGINE_ID_NIC2_0,
	GAUDI2_ENGINE_ID_NIC2_1,
	GAUDI2_ENGINE_ID_NIC3_0,
	GAUDI2_ENGINE_ID_NIC3_1,
	GAUDI2_ENGINE_ID_NIC4_0,
	GAUDI2_ENGINE_ID_NIC4_1,
	GAUDI2_ENGINE_ID_NIC5_0,
	GAUDI2_ENGINE_ID_NIC5_1,
	GAUDI2_ENGINE_ID_NIC6_0,
	GAUDI2_ENGINE_ID_NIC6_1,
	GAUDI2_ENGINE_ID_NIC7_0,
	GAUDI2_ENGINE_ID_NIC7_1,
	GAUDI2_ENGINE_ID_NIC8_0,
	GAUDI2_ENGINE_ID_NIC8_1,
	GAUDI2_ENGINE_ID_NIC9_0,
	GAUDI2_ENGINE_ID_NIC9_1,
	GAUDI2_ENGINE_ID_NIC10_0,
	GAUDI2_ENGINE_ID_NIC10_1,
	GAUDI2_ENGINE_ID_NIC11_0,
	GAUDI2_ENGINE_ID_NIC11_1,
	GAUDI2_ENGINE_ID_PCIE,
	GAUDI2_ENGINE_ID_PSOC,
	GAUDI2_ENGINE_ID_ARC_FARM,
	GAUDI2_ENGINE_ID_KDMA,
	GAUDI2_ENGINE_ID_SIZE
};

/*
 * ASIC specific PLL index
 *
 * Used to retrieve in frequency info of different IPs via
 * HL_INFO_PLL_FREQUENCY under HL_IOCTL_INFO IOCTL. The enums need to be
 * used as an index in struct hl_pll_frequency_info
 */

enum hl_goya_pll_index {
	HL_GOYA_CPU_PLL = 0,
	HL_GOYA_IC_PLL,
	HL_GOYA_MC_PLL,
	HL_GOYA_MME_PLL,
	HL_GOYA_PCI_PLL,
	HL_GOYA_EMMC_PLL,
	HL_GOYA_TPC_PLL,
	HL_GOYA_PLL_MAX
};

enum hl_gaudi_pll_index {
	HL_GAUDI_CPU_PLL = 0,
	HL_GAUDI_PCI_PLL,
	HL_GAUDI_SRAM_PLL,
	HL_GAUDI_HBM_PLL,
	HL_GAUDI_NIC_PLL,
	HL_GAUDI_DMA_PLL,
	HL_GAUDI_MESH_PLL,
	HL_GAUDI_MME_PLL,
	HL_GAUDI_TPC_PLL,
	HL_GAUDI_IF_PLL,
	HL_GAUDI_PLL_MAX
};

enum hl_gaudi2_pll_index {
	HL_GAUDI2_CPU_PLL = 0,
	HL_GAUDI2_PCI_PLL,
	HL_GAUDI2_SRAM_PLL,
	HL_GAUDI2_HBM_PLL,
	HL_GAUDI2_NIC_PLL,
	HL_GAUDI2_DMA_PLL,
	HL_GAUDI2_MESH_PLL,
	HL_GAUDI2_MME_PLL,
	HL_GAUDI2_TPC_PLL,
	HL_GAUDI2_IF_PLL,
	HL_GAUDI2_VID_PLL,
	HL_GAUDI2_MSS_PLL,
	HL_GAUDI2_PLL_MAX
};

/**
 * enum hl_goya_dma_direction - Direction of DMA operation inside a LIN_DMA packet that is
 *                              submitted to the GOYA's DMA QMAN. This attribute is not relevant
 *                              to the H/W but the kernel driver use it to parse the packet's
 *                              addresses and patch/validate them.
 * @HL_DMA_HOST_TO_DRAM: DMA operation from Host memory to GOYA's DDR.
 * @HL_DMA_HOST_TO_SRAM: DMA operation from Host memory to GOYA's SRAM.
 * @HL_DMA_DRAM_TO_SRAM: DMA operation from GOYA's DDR to GOYA's SRAM.
 * @HL_DMA_SRAM_TO_DRAM: DMA operation from GOYA's SRAM to GOYA's DDR.
 * @HL_DMA_SRAM_TO_HOST: DMA operation from GOYA's SRAM to Host memory.
 * @HL_DMA_DRAM_TO_HOST: DMA operation from GOYA's DDR to Host memory.
 * @HL_DMA_DRAM_TO_DRAM: DMA operation from GOYA's DDR to GOYA's DDR.
 * @HL_DMA_SRAM_TO_SRAM: DMA operation from GOYA's SRAM to GOYA's SRAM.
 * @HL_DMA_ENUM_MAX: number of values in enum
 */
enum hl_goya_dma_direction {
	HL_DMA_HOST_TO_DRAM,
	HL_DMA_HOST_TO_SRAM,
	HL_DMA_DRAM_TO_SRAM,
	HL_DMA_SRAM_TO_DRAM,
	HL_DMA_SRAM_TO_HOST,
	HL_DMA_DRAM_TO_HOST,
	HL_DMA_DRAM_TO_DRAM,
	HL_DMA_SRAM_TO_SRAM,
	HL_DMA_ENUM_MAX
};

/**
 * enum hl_device_status - Device status information.
 * @HL_DEVICE_STATUS_OPERATIONAL: Device is operational.
 * @HL_DEVICE_STATUS_IN_RESET: Device is currently during reset.
 * @HL_DEVICE_STATUS_MALFUNCTION: Device is unusable.
 * @HL_DEVICE_STATUS_NEEDS_RESET: Device needs reset because auto reset was disabled.
 * @HL_DEVICE_STATUS_IN_DEVICE_CREATION: Device is operational but its creation is still in
 *                                       progress.
 * @HL_DEVICE_STATUS_IN_RESET_AFTER_DEVICE_RELEASE: Device is currently during reset that was
 *                                                  triggered because the user released the device
 * @HL_DEVICE_STATUS_LAST: Last status.
 */
enum hl_device_status {
	HL_DEVICE_STATUS_OPERATIONAL,
	HL_DEVICE_STATUS_IN_RESET,
	HL_DEVICE_STATUS_MALFUNCTION,
	HL_DEVICE_STATUS_NEEDS_RESET,
	HL_DEVICE_STATUS_IN_DEVICE_CREATION,
	HL_DEVICE_STATUS_IN_RESET_AFTER_DEVICE_RELEASE,
	HL_DEVICE_STATUS_LAST = HL_DEVICE_STATUS_IN_RESET_AFTER_DEVICE_RELEASE
};

enum hl_server_type {
	HL_SERVER_TYPE_UNKNOWN = 0,
	HL_SERVER_GAUDI_HLS1 = 1,
	HL_SERVER_GAUDI_HLS1H = 2,
	HL_SERVER_GAUDI_TYPE1 = 3,
	HL_SERVER_GAUDI_TYPE2 = 4,
	HL_SERVER_GAUDI2_HLS2 = 5
};

/*
 * Notifier event values - for the notification mechanism and the HL_INFO_GET_EVENTS command
 *
 * HL_NOTIFIER_EVENT_TPC_ASSERT		- Indicates TPC assert event
 * HL_NOTIFIER_EVENT_UNDEFINED_OPCODE	- Indicates undefined operation code
 * HL_NOTIFIER_EVENT_DEVICE_RESET	- Indicates device requires a reset
 * HL_NOTIFIER_EVENT_CS_TIMEOUT		- Indicates CS timeout error
 * HL_NOTIFIER_EVENT_DEVICE_UNAVAILABLE	- Indicates device is unavailable
 * HL_NOTIFIER_EVENT_USER_ENGINE_ERR	- Indicates device engine in error state
 * HL_NOTIFIER_EVENT_GENERAL_HW_ERR     - Indicates device HW error
 * HL_NOTIFIER_EVENT_RAZWI              - Indicates razwi happened
 * HL_NOTIFIER_EVENT_PAGE_FAULT         - Indicates page fault happened
 */
#define HL_NOTIFIER_EVENT_TPC_ASSERT		(1ULL << 0)
#define HL_NOTIFIER_EVENT_UNDEFINED_OPCODE	(1ULL << 1)
#define HL_NOTIFIER_EVENT_DEVICE_RESET		(1ULL << 2)
#define HL_NOTIFIER_EVENT_CS_TIMEOUT		(1ULL << 3)
#define HL_NOTIFIER_EVENT_DEVICE_UNAVAILABLE	(1ULL << 4)
#define HL_NOTIFIER_EVENT_USER_ENGINE_ERR	(1ULL << 5)
#define HL_NOTIFIER_EVENT_GENERAL_HW_ERR	(1ULL << 6)
#define HL_NOTIFIER_EVENT_RAZWI			(1ULL << 7)
#define HL_NOTIFIER_EVENT_PAGE_FAULT		(1ULL << 8)

/* Opcode for management ioctl
 *
 * HW_IP_INFO            - Receive information about different IP blocks in the
 *                         device.
 * HL_INFO_HW_EVENTS     - Receive an array describing how many times each event
 *                         occurred since the last hard reset.
 * HL_INFO_DRAM_USAGE    - Retrieve the dram usage inside the device and of the
 *                         specific context. This is relevant only for devices
 *                         where the dram is managed by the kernel driver
 * HL_INFO_HW_IDLE       - Retrieve information about the idle status of each
 *                         internal engine.
 * HL_INFO_DEVICE_STATUS - Retrieve the device's status. This opcode doesn't
 *                         require an open context.
 * HL_INFO_DEVICE_UTILIZATION  - Retrieve the total utilization of the device
 *                               over the last period specified by the user.
 *                               The period can be between 100ms to 1s, in
 *                               resolution of 100ms. The return value is a
 *                               percentage of the utilization rate.
 * HL_INFO_HW_EVENTS_AGGREGATE - Receive an array describing how many times each
 *                               event occurred since the driver was loaded.
 * HL_INFO_CLK_RATE            - Retrieve the current and maximum clock rate
 *                               of the device in MHz. The maximum clock rate is
 *                               configurable via sysfs parameter
 * HL_INFO_RESET_COUNT   - Retrieve the counts of the soft and hard reset
 *                         operations performed on the device since the last
 *                         time the driver was loaded.
 * HL_INFO_TIME_SYNC     - Retrieve the device's time alongside the host's time
 *                         for synchronization.
 * HL_INFO_CS_COUNTERS   - Retrieve command submission counters
 * HL_INFO_PCI_COUNTERS  - Retrieve PCI counters
 * HL_INFO_CLK_THROTTLE_REASON - Retrieve clock throttling reason
 * HL_INFO_SYNC_MANAGER  - Retrieve sync manager info per dcore
 * HL_INFO_TOTAL_ENERGY  - Retrieve total energy consumption
 * HL_INFO_PLL_FREQUENCY - Retrieve PLL frequency
 * HL_INFO_POWER         - Retrieve power information
 * HL_INFO_OPEN_STATS    - Retrieve info regarding recent device open calls
 * HL_INFO_DRAM_REPLACED_ROWS - Retrieve DRAM replaced rows info
 * HL_INFO_DRAM_PENDING_ROWS - Retrieve DRAM pending rows num
 * HL_INFO_LAST_ERR_OPEN_DEV_TIME - Retrieve timestamp of the last time the device was opened
 *                                  and CS timeout or razwi error occurred.
 * HL_INFO_CS_TIMEOUT_EVENT - Retrieve CS timeout timestamp and its related CS sequence number.
 * HL_INFO_RAZWI_EVENT - Retrieve parameters of razwi:
 *                            Timestamp of razwi.
 *                            The address which accessing it caused the razwi.
 *                            Razwi initiator.
 *                            Razwi cause, was it a page fault or MMU access error.
 * HL_INFO_DEV_MEM_ALLOC_PAGE_SIZES - Retrieve valid page sizes for device memory allocation
 * HL_INFO_SECURED_ATTESTATION - Retrieve attestation report of the boot.
 * HL_INFO_REGISTER_EVENTFD   - Register eventfd for event notifications.
 * HL_INFO_UNREGISTER_EVENTFD - Unregister eventfd
 * HL_INFO_GET_EVENTS         - Retrieve the last occurred events
 * HL_INFO_UNDEFINED_OPCODE_EVENT - Retrieve last undefined opcode error information.
 * HL_INFO_ENGINE_STATUS - Retrieve the status of all the h/w engines in the asic.
 * HL_INFO_PAGE_FAULT_EVENT - Retrieve parameters of captured page fault.
 * HL_INFO_USER_MAPPINGS - Retrieve user mappings, captured after page fault event.
 * HL_INFO_FW_GENERIC_REQ - Send generic request to FW.
 */
#define HL_INFO_HW_IP_INFO			0
#define HL_INFO_HW_EVENTS			1
#define HL_INFO_DRAM_USAGE			2
#define HL_INFO_HW_IDLE				3
#define HL_INFO_DEVICE_STATUS			4
#define HL_INFO_DEVICE_UTILIZATION		6
#define HL_INFO_HW_EVENTS_AGGREGATE		7
#define HL_INFO_CLK_RATE			8
#define HL_INFO_RESET_COUNT			9
#define HL_INFO_TIME_SYNC			10
#define HL_INFO_CS_COUNTERS			11
#define HL_INFO_PCI_COUNTERS			12
#define HL_INFO_CLK_THROTTLE_REASON		13
#define HL_INFO_SYNC_MANAGER			14
#define HL_INFO_TOTAL_ENERGY			15
#define HL_INFO_PLL_FREQUENCY			16
#define HL_INFO_POWER				17
#define HL_INFO_OPEN_STATS			18
#define HL_INFO_DRAM_REPLACED_ROWS		21
#define HL_INFO_DRAM_PENDING_ROWS		22
#define HL_INFO_LAST_ERR_OPEN_DEV_TIME		23
#define HL_INFO_CS_TIMEOUT_EVENT		24
#define HL_INFO_RAZWI_EVENT			25
#define HL_INFO_DEV_MEM_ALLOC_PAGE_SIZES	26
#define HL_INFO_SECURED_ATTESTATION		27
#define HL_INFO_REGISTER_EVENTFD		28
#define HL_INFO_UNREGISTER_EVENTFD		29
#define HL_INFO_GET_EVENTS			30
#define HL_INFO_UNDEFINED_OPCODE_EVENT		31
#define HL_INFO_ENGINE_STATUS			32
#define HL_INFO_PAGE_FAULT_EVENT		33
#define HL_INFO_USER_MAPPINGS			34
#define HL_INFO_FW_GENERIC_REQ			35

#define HL_INFO_VERSION_MAX_LEN			128
#define HL_INFO_CARD_NAME_MAX_LEN		16

/* Maximum buffer size for retrieving engines status */
#define HL_ENGINES_DATA_MAX_SIZE	SZ_1M

/**
 * struct hl_info_hw_ip_info - hardware information on various IPs in the ASIC
 * @sram_base_address: The first SRAM physical base address that is free to be
 *                     used by the user.
 * @dram_base_address: The first DRAM virtual or physical base address that is
 *                     free to be used by the user.
 * @dram_size: The DRAM size that is available to the user.
 * @sram_size: The SRAM size that is available to the user.
 * @num_of_events: The number of events that can be received from the f/w. This
 *                 is needed so the user can what is the size of the h/w events
 *                 array he needs to pass to the kernel when he wants to fetch
 *                 the event counters.
 * @device_id: PCI device ID of the ASIC.
 * @module_id: Module ID of the ASIC for mezzanine cards in servers
 *             (From OCP spec).
 * @decoder_enabled_mask: Bit-mask that represents which decoders are enabled.
 * @first_available_interrupt_id: The first available interrupt ID for the user
 *                                to be used when it works with user interrupts.
 *                                Relevant for Gaudi2 and later.
 * @server_type: Server type that the Gaudi ASIC is currently installed in.
 *               The value is according to enum hl_server_type
 * @cpld_version: CPLD version on the board.
 * @psoc_pci_pll_nr: PCI PLL NR value. Needed by the profiler in some ASICs.
 * @psoc_pci_pll_nf: PCI PLL NF value. Needed by the profiler in some ASICs.
 * @psoc_pci_pll_od: PCI PLL OD value. Needed by the profiler in some ASICs.
 * @psoc_pci_pll_div_factor: PCI PLL DIV factor value. Needed by the profiler
 *                           in some ASICs.
 * @tpc_enabled_mask: Bit-mask that represents which TPCs are enabled. Relevant
 *                    for Goya/Gaudi only.
 * @dram_enabled: Whether the DRAM is enabled.
 * @security_enabled: Whether security is enabled on device.
 * @mme_master_slave_mode: Indicate whether the MME is working in master/slave
 *                         configuration. Relevant for Greco and later.
 * @cpucp_version: The CPUCP f/w version.
 * @card_name: The card name as passed by the f/w.
 * @tpc_enabled_mask_ext: Bit-mask that represents which TPCs are enabled.
 *                        Relevant for Greco and later.
 * @dram_page_size: The DRAM physical page size.
 * @edma_enabled_mask: Bit-mask that represents which EDMAs are enabled.
 *                     Relevant for Gaudi2 and later.
 * @number_of_user_interrupts: The number of interrupts that are available to the userspace
 *                             application to use. Relevant for Gaudi2 and later.
 * @device_mem_alloc_default_page_size: default page size used in device memory allocation.
 * @revision_id: PCI revision ID of the ASIC.
 */
struct hl_info_hw_ip_info {
	__u64 sram_base_address;
	__u64 dram_base_address;
	__u64 dram_size;
	__u32 sram_size;
	__u32 num_of_events;
	__u32 device_id;
	__u32 module_id;
	__u32 decoder_enabled_mask;
	__u16 first_available_interrupt_id;
	__u16 server_type;
	__u32 cpld_version;
	__u32 psoc_pci_pll_nr;
	__u32 psoc_pci_pll_nf;
	__u32 psoc_pci_pll_od;
	__u32 psoc_pci_pll_div_factor;
	__u8 tpc_enabled_mask;
	__u8 dram_enabled;
	__u8 security_enabled;
	__u8 mme_master_slave_mode;
	__u8 cpucp_version[HL_INFO_VERSION_MAX_LEN];
	__u8 card_name[HL_INFO_CARD_NAME_MAX_LEN];
	__u64 tpc_enabled_mask_ext;
	__u64 dram_page_size;
	__u32 edma_enabled_mask;
	__u16 number_of_user_interrupts;
	__u16 pad2;
	__u64 reserved4;
	__u64 device_mem_alloc_default_page_size;
	__u64 reserved5;
	__u64 reserved6;
	__u32 reserved7;
	__u8 reserved8;
	__u8 revision_id;
	__u8 pad[2];
};

struct hl_info_dram_usage {
	__u64 dram_free_mem;
	__u64 ctx_dram_mem;
};

#define HL_BUSY_ENGINES_MASK_EXT_SIZE	4

struct hl_info_hw_idle {
	__u32 is_idle;
	/*
	 * Bitmask of busy engines.
	 * Bits definition is according to `enum <chip>_engine_id'.
	 */
	__u32 busy_engines_mask;

	/*
	 * Extended Bitmask of busy engines.
	 * Bits definition is according to `enum <chip>_engine_id'.
	 */
	__u64 busy_engines_mask_ext[HL_BUSY_ENGINES_MASK_EXT_SIZE];
};

struct hl_info_device_status {
	__u32 status;
	__u32 pad;
};

struct hl_info_device_utilization {
	__u32 utilization;
	__u32 pad;
};

struct hl_info_clk_rate {
	__u32 cur_clk_rate_mhz;
	__u32 max_clk_rate_mhz;
};

struct hl_info_reset_count {
	__u32 hard_reset_cnt;
	__u32 soft_reset_cnt;
};

struct hl_info_time_sync {
	__u64 device_time;
	__u64 host_time;
};

/**
 * struct hl_info_pci_counters - pci counters
 * @rx_throughput: PCI rx throughput KBps
 * @tx_throughput: PCI tx throughput KBps
 * @replay_cnt: PCI replay counter
 */
struct hl_info_pci_counters {
	__u64 rx_throughput;
	__u64 tx_throughput;
	__u64 replay_cnt;
};

enum hl_clk_throttling_type {
	HL_CLK_THROTTLE_TYPE_POWER,
	HL_CLK_THROTTLE_TYPE_THERMAL,
	HL_CLK_THROTTLE_TYPE_MAX
};

/* clk_throttling_reason masks */
#define HL_CLK_THROTTLE_POWER		(1 << HL_CLK_THROTTLE_TYPE_POWER)
#define HL_CLK_THROTTLE_THERMAL		(1 << HL_CLK_THROTTLE_TYPE_THERMAL)

/**
 * struct hl_info_clk_throttle - clock throttling reason
 * @clk_throttling_reason: each bit represents a clk throttling reason
 * @clk_throttling_timestamp_us: represents CPU timestamp in microseconds of the start-event
 * @clk_throttling_duration_ns: the clock throttle time in nanosec
 */
struct hl_info_clk_throttle {
	__u32 clk_throttling_reason;
	__u32 pad;
	__u64 clk_throttling_timestamp_us[HL_CLK_THROTTLE_TYPE_MAX];
	__u64 clk_throttling_duration_ns[HL_CLK_THROTTLE_TYPE_MAX];
};

/**
 * struct hl_info_energy - device energy information
 * @total_energy_consumption: total device energy consumption
 */
struct hl_info_energy {
	__u64 total_energy_consumption;
};

#define HL_PLL_NUM_OUTPUTS 4

struct hl_pll_frequency_info {
	__u16 output[HL_PLL_NUM_OUTPUTS];
};

/**
 * struct hl_open_stats_info - device open statistics information
 * @open_counter: ever growing counter, increased on each successful dev open
 * @last_open_period_ms: duration (ms) device was open last time
 * @is_compute_ctx_active: Whether there is an active compute context executing
 * @compute_ctx_in_release: true if the current compute context is being released
 */
struct hl_open_stats_info {
	__u64 open_counter;
	__u64 last_open_period_ms;
	__u8 is_compute_ctx_active;
	__u8 compute_ctx_in_release;
	__u8 pad[6];
};

/**
 * struct hl_power_info - power information
 * @power: power consumption
 */
struct hl_power_info {
	__u64 power;
};

/**
 * struct hl_info_sync_manager - sync manager information
 * @first_available_sync_object: first available sob
 * @first_available_monitor: first available monitor
 * @first_available_cq: first available cq
 */
struct hl_info_sync_manager {
	__u32 first_available_sync_object;
	__u32 first_available_monitor;
	__u32 first_available_cq;
	__u32 reserved;
};

/**
 * struct hl_info_cs_counters - command submission counters
 * @total_out_of_mem_drop_cnt: total dropped due to memory allocation issue
 * @ctx_out_of_mem_drop_cnt: context dropped due to memory allocation issue
 * @total_parsing_drop_cnt: total dropped due to error in packet parsing
 * @ctx_parsing_drop_cnt: context dropped due to error in packet parsing
 * @total_queue_full_drop_cnt: total dropped due to queue full
 * @ctx_queue_full_drop_cnt: context dropped due to queue full
 * @total_device_in_reset_drop_cnt: total dropped due to device in reset
 * @ctx_device_in_reset_drop_cnt: context dropped due to device in reset
 * @total_max_cs_in_flight_drop_cnt: total dropped due to maximum CS in-flight
 * @ctx_max_cs_in_flight_drop_cnt: context dropped due to maximum CS in-flight
 * @total_validation_drop_cnt: total dropped due to validation error
 * @ctx_validation_drop_cnt: context dropped due to validation error
 */
struct hl_info_cs_counters {
	__u64 total_out_of_mem_drop_cnt;
	__u64 ctx_out_of_mem_drop_cnt;
	__u64 total_parsing_drop_cnt;
	__u64 ctx_parsing_drop_cnt;
	__u64 total_queue_full_drop_cnt;
	__u64 ctx_queue_full_drop_cnt;
	__u64 total_device_in_reset_drop_cnt;
	__u64 ctx_device_in_reset_drop_cnt;
	__u64 total_max_cs_in_flight_drop_cnt;
	__u64 ctx_max_cs_in_flight_drop_cnt;
	__u64 total_validation_drop_cnt;
	__u64 ctx_validation_drop_cnt;
};

/**
 * struct hl_info_last_err_open_dev_time - last error boot information.
 * @timestamp: timestamp of last time the device was opened and error occurred.
 */
struct hl_info_last_err_open_dev_time {
	__s64 timestamp;
};

/**
 * struct hl_info_cs_timeout_event - last CS timeout information.
 * @timestamp: timestamp when last CS timeout event occurred.
 * @seq: sequence number of last CS timeout event.
 */
struct hl_info_cs_timeout_event {
	__s64 timestamp;
	__u64 seq;
};

#define HL_RAZWI_NA_ENG_ID U16_MAX
#define HL_RAZWI_MAX_NUM_OF_ENGINES_PER_RTR 128
#define HL_RAZWI_READ		BIT(0)
#define HL_RAZWI_WRITE		BIT(1)
#define HL_RAZWI_LBW		BIT(2)
#define HL_RAZWI_HBW		BIT(3)
#define HL_RAZWI_RR		BIT(4)
#define HL_RAZWI_ADDR_DEC	BIT(5)

/**
 * struct hl_info_razwi_event - razwi information.
 * @timestamp: timestamp of razwi.
 * @addr: address which accessing it caused razwi.
 * @engine_id: engine id of the razwi initiator, if it was initiated by engine that does not
 *             have engine id it will be set to HL_RAZWI_NA_ENG_ID. If there are several possible
 *             engines which caused the razwi, it will hold all of them.
 * @num_of_possible_engines: contains number of possible engine ids. In some asics, razwi indication
 *                           might be common for several engines and there is no way to get the
 *                           exact engine. In this way, engine_id array will be filled with all
 *                           possible engines caused this razwi. Also, there might be possibility
 *                           in gaudi, where we don't indication on specific engine, in that case
 *                           the value of this parameter will be zero.
 * @flags: bitmask for additional data: HL_RAZWI_READ - razwi caused by read operation
 *                                      HL_RAZWI_WRITE - razwi caused by write operation
 *                                      HL_RAZWI_LBW - razwi caused by lbw fabric transaction
 *                                      HL_RAZWI_HBW - razwi caused by hbw fabric transaction
 *                                      HL_RAZWI_RR - razwi caused by range register
 *                                      HL_RAZWI_ADDR_DEC - razwi caused by address decode error
 *         Note: this data is not supported by all asics, in that case the relevant bits will not
 *               be set.
 */
struct hl_info_razwi_event {
	__s64 timestamp;
	__u64 addr;
	__u16 engine_id[HL_RAZWI_MAX_NUM_OF_ENGINES_PER_RTR];
	__u16 num_of_possible_engines;
	__u8 flags;
	__u8 pad[5];
};

#define MAX_QMAN_STREAMS_INFO		4
#define OPCODE_INFO_MAX_ADDR_SIZE	8
/**
 * struct hl_info_undefined_opcode_event - info about last undefined opcode error
 * @timestamp: timestamp of the undefined opcode error
 * @cb_addr_streams: CB addresses (per stream) that are currently exists in the PQ
 *                   entries. In case all streams array entries are
 *                   filled with values, it means the execution was in Lower-CP.
 * @cq_addr: the address of the current handled command buffer
 * @cq_size: the size of the current handled command buffer
 * @cb_addr_streams_len: num of streams - actual len of cb_addr_streams array.
 *                       should be equal to 1 in case of undefined opcode
 *                       in Upper-CP (specific stream) and equal to 4 incase
 *                       of undefined opcode in Lower-CP.
 * @engine_id: engine-id that the error occurred on
 * @stream_id: the stream id the error occurred on. In case the stream equals to
 *             MAX_QMAN_STREAMS_INFO it means the error occurred on a Lower-CP.
 */
struct hl_info_undefined_opcode_event {
	__s64 timestamp;
	__u64 cb_addr_streams[MAX_QMAN_STREAMS_INFO][OPCODE_INFO_MAX_ADDR_SIZE];
	__u64 cq_addr;
	__u32 cq_size;
	__u32 cb_addr_streams_len;
	__u32 engine_id;
	__u32 stream_id;
};

/**
 * struct hl_info_dev_memalloc_page_sizes - valid page sizes in device mem alloc information.
 * @page_order_bitmask: bitmap in which a set bit represents the order of the supported page size
 *                      (e.g. 0x2100000 means that 1MB and 32MB pages are supported).
 */
struct hl_info_dev_memalloc_page_sizes {
	__u64 page_order_bitmask;
};

#define SEC_PCR_DATA_BUF_SZ	256
#define SEC_PCR_QUOTE_BUF_SZ	510	/* (512 - 2) 2 bytes used for size */
#define SEC_SIGNATURE_BUF_SZ	255	/* (256 - 1) 1 byte used for size */
#define SEC_PUB_DATA_BUF_SZ	510	/* (512 - 2) 2 bytes used for size */
#define SEC_CERTIFICATE_BUF_SZ	2046	/* (2048 - 2) 2 bytes used for size */

/*
 * struct hl_info_sec_attest - attestation report of the boot
 * @nonce: number only used once. random number provided by host. this also passed to the quote
 *         command as a qualifying data.
 * @pcr_quote_len: length of the attestation quote data (bytes)
 * @pub_data_len: length of the public data (bytes)
 * @certificate_len: length of the certificate (bytes)
 * @pcr_num_reg: number of PCR registers in the pcr_data array
 * @pcr_reg_len: length of each PCR register in the pcr_data array (bytes)
 * @quote_sig_len: length of the attestation report signature (bytes)
 * @pcr_data: raw values of the PCR registers
 * @pcr_quote: attestation report data structure
 * @quote_sig: signature structure of the attestation report
 * @public_data: public key for the signed attestation
 *		 (outPublic + name + qualifiedName)
 * @certificate: certificate for the attestation signing key
 */
struct hl_info_sec_attest {
	__u32 nonce;
	__u16 pcr_quote_len;
	__u16 pub_data_len;
	__u16 certificate_len;
	__u8 pcr_num_reg;
	__u8 pcr_reg_len;
	__u8 quote_sig_len;
	__u8 pcr_data[SEC_PCR_DATA_BUF_SZ];
	__u8 pcr_quote[SEC_PCR_QUOTE_BUF_SZ];
	__u8 quote_sig[SEC_SIGNATURE_BUF_SZ];
	__u8 public_data[SEC_PUB_DATA_BUF_SZ];
	__u8 certificate[SEC_CERTIFICATE_BUF_SZ];
	__u8 pad0[2];
};

/**
 * struct hl_page_fault_info - page fault information.
 * @timestamp: timestamp of page fault.
 * @addr: address which accessing it caused page fault.
 * @engine_id: engine id which caused the page fault, supported only in gaudi3.
 */
struct hl_page_fault_info {
	__s64 timestamp;
	__u64 addr;
	__u16 engine_id;
	__u8 pad[6];
};

/**
 * struct hl_user_mapping - user mapping information.
 * @dev_va: device virtual address.
 * @size: virtual address mapping size.
 */
struct hl_user_mapping {
	__u64 dev_va;
	__u64 size;
};

enum gaudi_dcores {
	HL_GAUDI_WS_DCORE,
	HL_GAUDI_WN_DCORE,
	HL_GAUDI_EN_DCORE,
	HL_GAUDI_ES_DCORE
};

/**
 * struct hl_info_args - Main structure to retrieve device related information.
 * @return_pointer: User space address of the relevant structure related to HL_INFO_* operation
 *                  mentioned in @op.
 * @return_size: Size of the structure used in @return_pointer, just like "size" in "snprintf", it
 *               limits how many bytes the kernel can write. For hw_events array, the size should be
 *               hl_info_hw_ip_info.num_of_events * sizeof(__u32).
 * @op: Defines which type of information to be retrieved. Refer HL_INFO_* for details.
 * @dcore_id: DCORE id for which the information is relevant (for Gaudi refer to enum gaudi_dcores).
 * @ctx_id: Context ID of the user. Currently not in use.
 * @period_ms: Period value, in milliseconds, for utilization rate in range 100ms - 1000ms in 100 ms
 *             resolution. Currently not in use.
 * @pll_index: Index as defined in hl_<asic type>_pll_index enumeration.
 * @eventfd: event file descriptor for event notifications.
 * @user_buffer_actual_size: Actual data size which was copied to user allocated buffer by the
 *                           driver. It is possible for the user to allocate buffer larger than
 *                           needed, hence updating this variable so user will know the exact amount
 *                           of bytes copied by the kernel to the buffer.
 * @sec_attest_nonce: Nonce number used for attestation report.
 * @array_size: Number of array members copied to user buffer.
 *              Relevant for HL_INFO_USER_MAPPINGS info ioctl.
 * @fw_sub_opcode: generic requests sub opcodes.
 * @pad: Padding to 64 bit.
 */
struct hl_info_args {
	__u64 return_pointer;
	__u32 return_size;
	__u32 op;

	union {
		__u32 dcore_id;
		__u32 ctx_id;
		__u32 period_ms;
		__u32 pll_index;
		__u32 eventfd;
		__u32 user_buffer_actual_size;
		__u32 sec_attest_nonce;
		__u32 array_size;
		__u32 fw_sub_opcode;
	};

	__u32 pad;
};

/* Opcode to create a new command buffer */
#define HL_CB_OP_CREATE		0
/* Opcode to destroy previously created command buffer */
#define HL_CB_OP_DESTROY	1
/* Opcode to retrieve information about a command buffer */
#define HL_CB_OP_INFO		2

/* 2MB minus 32 bytes for 2xMSG_PROT */
#define HL_MAX_CB_SIZE		(0x200000 - 32)

/* Indicates whether the command buffer should be mapped to the device's MMU */
#define HL_CB_FLAGS_MAP			0x1

/* Used with HL_CB_OP_INFO opcode to get the device va address for kernel mapped CB */
#define HL_CB_FLAGS_GET_DEVICE_VA	0x2

struct hl_cb_in {
	/* Handle of CB or 0 if we want to create one */
	__u64 cb_handle;
	/* HL_CB_OP_* */
	__u32 op;

	/* Size of CB. Maximum size is HL_MAX_CB_SIZE. The minimum size that
	 * will be allocated, regardless of this parameter's value, is PAGE_SIZE
	 */
	__u32 cb_size;

	/* Context ID - Currently not in use */
	__u32 ctx_id;
	/* HL_CB_FLAGS_* */
	__u32 flags;
};

struct hl_cb_out {
	union {
		/* Handle of CB */
		__u64 cb_handle;

		union {
			/* Information about CB */
			struct {
				/* Usage count of CB */
				__u32 usage_cnt;
				__u32 pad;
			};

			/* CB mapped address to device MMU */
			__u64 device_va;
		};
	};
};

union hl_cb_args {
	struct hl_cb_in in;
	struct hl_cb_out out;
};

/* HL_CS_CHUNK_FLAGS_ values
 *
 * HL_CS_CHUNK_FLAGS_USER_ALLOC_CB:
 *      Indicates if the CB was allocated and mapped by userspace
 *      (relevant to greco and above). User allocated CB is a command buffer,
 *      allocated by the user, via malloc (or similar). After allocating the
 *      CB, the user invokes - “memory ioctl” to map the user memory into a
 *      device virtual address. The user provides this address via the
 *      cb_handle field. The interface provides the ability to create a
 *      large CBs, Which aren’t limited to “HL_MAX_CB_SIZE”. Therefore, it
 *      increases the PCI-DMA queues throughput. This CB allocation method
 *      also reduces the use of Linux DMA-able memory pool. Which are limited
 *      and used by other Linux sub-systems.
 */
#define HL_CS_CHUNK_FLAGS_USER_ALLOC_CB 0x1

/*
 * This structure size must always be fixed to 64-bytes for backward
 * compatibility
 */
struct hl_cs_chunk {
	union {
		/* Goya/Gaudi:
		 * For external queue, this represents a Handle of CB on the
		 * Host.
		 * For internal queue in Goya, this represents an SRAM or
		 * a DRAM address of the internal CB. In Gaudi, this might also
		 * represent a mapped host address of the CB.
		 *
		 * Greco onwards:
		 * For H/W queue, this represents either a Handle of CB on the
		 * Host, or an SRAM, a DRAM, or a mapped host address of the CB.
		 *
		 * A mapped host address is in the device address space, after
		 * a host address was mapped by the device MMU.
		 */
		__u64 cb_handle;

		/* Relevant only when HL_CS_FLAGS_WAIT or
		 * HL_CS_FLAGS_COLLECTIVE_WAIT is set
		 * This holds address of array of u64 values that contain
		 * signal CS sequence numbers. The wait described by
		 * this job will listen on all those signals
		 * (wait event per signal)
		 */
		__u64 signal_seq_arr;

		/*
		 * Relevant only when HL_CS_FLAGS_WAIT or
		 * HL_CS_FLAGS_COLLECTIVE_WAIT is set
		 * along with HL_CS_FLAGS_ENCAP_SIGNALS.
		 * This is the CS sequence which has the encapsulated signals.
		 */
		__u64 encaps_signal_seq;
	};

	/* Index of queue to put the CB on */
	__u32 queue_index;

	union {
		/*
		 * Size of command buffer with valid packets
		 * Can be smaller then actual CB size
		 */
		__u32 cb_size;

		/* Relevant only when HL_CS_FLAGS_WAIT or
		 * HL_CS_FLAGS_COLLECTIVE_WAIT is set.
		 * Number of entries in signal_seq_arr
		 */
		__u32 num_signal_seq_arr;

		/* Relevant only when HL_CS_FLAGS_WAIT or
		 * HL_CS_FLAGS_COLLECTIVE_WAIT is set along
		 * with HL_CS_FLAGS_ENCAP_SIGNALS
		 * This set the signals range that the user want to wait for
		 * out of the whole reserved signals range.
		 * e.g if the signals range is 20, and user don't want
		 * to wait for signal 8, so he set this offset to 7, then
		 * he call the API again with 9 and so on till 20.
		 */
		__u32 encaps_signal_offset;
	};

	/* HL_CS_CHUNK_FLAGS_* */
	__u32 cs_chunk_flags;

	/* Relevant only when HL_CS_FLAGS_COLLECTIVE_WAIT is set.
	 * This holds the collective engine ID. The wait described by this job
	 * will sync with this engine and with all NICs before completion.
	 */
	__u32 collective_engine_id;

	/* Align structure to 64 bytes */
	__u32 pad[10];
};

/* SIGNAL/WAIT/COLLECTIVE_WAIT flags are mutually exclusive */
#define HL_CS_FLAGS_FORCE_RESTORE		0x1
#define HL_CS_FLAGS_SIGNAL			0x2
#define HL_CS_FLAGS_WAIT			0x4
#define HL_CS_FLAGS_COLLECTIVE_WAIT		0x8

#define HL_CS_FLAGS_TIMESTAMP			0x20
#define HL_CS_FLAGS_STAGED_SUBMISSION		0x40
#define HL_CS_FLAGS_STAGED_SUBMISSION_FIRST	0x80
#define HL_CS_FLAGS_STAGED_SUBMISSION_LAST	0x100
#define HL_CS_FLAGS_CUSTOM_TIMEOUT		0x200
#define HL_CS_FLAGS_SKIP_RESET_ON_TIMEOUT	0x400

/*
 * The encapsulated signals CS is merged into the existing CS ioctls.
 * In order to use this feature need to follow the below procedure:
 * 1. Reserve signals, set the CS type to HL_CS_FLAGS_RESERVE_SIGNALS_ONLY
 *    the output of this API will be the SOB offset from CFG_BASE.
 *    this address will be used to patch CB cmds to do the signaling for this
 *    SOB by incrementing it's value.
 *    for reverting the reservation use HL_CS_FLAGS_UNRESERVE_SIGNALS_ONLY
 *    CS type, note that this might fail if out-of-sync happened to the SOB
 *    value, in case other signaling request to the same SOB occurred between
 *    reserve-unreserve calls.
 * 2. Use the staged CS to do the encapsulated signaling jobs.
 *    use HL_CS_FLAGS_STAGED_SUBMISSION and HL_CS_FLAGS_STAGED_SUBMISSION_FIRST
 *    along with HL_CS_FLAGS_ENCAP_SIGNALS flag, and set encaps_signal_offset
 *    field. This offset allows app to wait on part of the reserved signals.
 * 3. Use WAIT/COLLECTIVE WAIT CS along with HL_CS_FLAGS_ENCAP_SIGNALS flag
 *    to wait for the encapsulated signals.
 */
#define HL_CS_FLAGS_ENCAP_SIGNALS		0x800
#define HL_CS_FLAGS_RESERVE_SIGNALS_ONLY	0x1000
#define HL_CS_FLAGS_UNRESERVE_SIGNALS_ONLY	0x2000

/*
 * The engine cores CS is merged into the existing CS ioctls.
 * Use it to control the engine cores mode.
 */
#define HL_CS_FLAGS_ENGINE_CORE_COMMAND		0x4000

/*
 * The flush HBW PCI writes is merged into the existing CS ioctls.
 * Used to flush all HBW PCI writes.
 * This is a blocking operation and for this reason the user shall not use
 * the return sequence number (which will be invalid anyway)
 */
#define HL_CS_FLAGS_FLUSH_PCI_HBW_WRITES	0x8000

#define HL_CS_STATUS_SUCCESS		0

#define HL_MAX_JOBS_PER_CS		512

/* HL_ENGINE_CORE_ values
 *
 * HL_ENGINE_CORE_HALT: engine core halt
 * HL_ENGINE_CORE_RUN:  engine core run
 */
#define HL_ENGINE_CORE_HALT	(1 << 0)
#define HL_ENGINE_CORE_RUN	(1 << 1)

struct hl_cs_in {

	union {
		struct {
			/* this holds address of array of hl_cs_chunk for restore phase */
			__u64 chunks_restore;

			/* holds address of array of hl_cs_chunk for execution phase */
			__u64 chunks_execute;
		};

		/* Valid only when HL_CS_FLAGS_ENGINE_CORE_COMMAND is set */
		struct {
			/* this holds address of array of uint32 for engine_cores */
			__u64 engine_cores;

			/* number of engine cores in engine_cores array */
			__u32 num_engine_cores;

			/* the core command to be sent towards engine cores */
			__u32 core_command;
		};
	};

	union {
		/*
		 * Sequence number of a staged submission CS
		 * valid only if HL_CS_FLAGS_STAGED_SUBMISSION is set and
		 * HL_CS_FLAGS_STAGED_SUBMISSION_FIRST is unset.
		 */
		__u64 seq;

		/*
		 * Encapsulated signals handle id
		 * Valid for two flows:
		 * 1. CS with encapsulated signals:
		 *    when HL_CS_FLAGS_STAGED_SUBMISSION and
		 *    HL_CS_FLAGS_STAGED_SUBMISSION_FIRST
		 *    and HL_CS_FLAGS_ENCAP_SIGNALS are set.
		 * 2. unreserve signals:
		 *    valid when HL_CS_FLAGS_UNRESERVE_SIGNALS_ONLY is set.
		 */
		__u32 encaps_sig_handle_id;

		/* Valid only when HL_CS_FLAGS_RESERVE_SIGNALS_ONLY is set */
		struct {
			/* Encapsulated signals number */
			__u32 encaps_signals_count;

			/* Encapsulated signals queue index (stream) */
			__u32 encaps_signals_q_idx;
		};
	};

	/* Number of chunks in restore phase array. Maximum number is
	 * HL_MAX_JOBS_PER_CS
	 */
	__u32 num_chunks_restore;

	/* Number of chunks in execution array. Maximum number is
	 * HL_MAX_JOBS_PER_CS
	 */
	__u32 num_chunks_execute;

	/* timeout in seconds - valid only if HL_CS_FLAGS_CUSTOM_TIMEOUT
	 * is set
	 */
	__u32 timeout;

	/* HL_CS_FLAGS_* */
	__u32 cs_flags;

	/* Context ID - Currently not in use */
	__u32 ctx_id;
	__u8 pad[4];
};

struct hl_cs_out {
	union {
		/*
		 * seq holds the sequence number of the CS to pass to wait
		 * ioctl. All values are valid except for 0 and ULLONG_MAX
		 */
		__u64 seq;

		/* Valid only when HL_CS_FLAGS_RESERVE_SIGNALS_ONLY is set */
		struct {
			/* This is the reserved signal handle id */
			__u32 handle_id;

			/* This is the signals count */
			__u32 count;
		};
	};

	/* HL_CS_STATUS */
	__u32 status;

	/*
	 * SOB base address offset
	 * Valid only when HL_CS_FLAGS_RESERVE_SIGNALS_ONLY or HL_CS_FLAGS_SIGNAL is set
	 */
	__u32 sob_base_addr_offset;

	/*
	 * Count of completed signals in SOB before current signal submission.
	 * Valid only when (HL_CS_FLAGS_ENCAP_SIGNALS & HL_CS_FLAGS_STAGED_SUBMISSION)
	 * or HL_CS_FLAGS_SIGNAL is set
	 */
	__u16 sob_count_before_submission;
	__u16 pad[3];
};

union hl_cs_args {
	struct hl_cs_in in;
	struct hl_cs_out out;
};

#define HL_WAIT_CS_FLAGS_INTERRUPT		0x2
#define HL_WAIT_CS_FLAGS_INTERRUPT_MASK		0xFFF00000
#define HL_WAIT_CS_FLAGS_ANY_CQ_INTERRUPT	0xFFF00000
#define HL_WAIT_CS_FLAGS_ANY_DEC_INTERRUPT	0xFFE00000
#define HL_WAIT_CS_FLAGS_MULTI_CS		0x4
#define HL_WAIT_CS_FLAGS_INTERRUPT_KERNEL_CQ	0x10
#define HL_WAIT_CS_FLAGS_REGISTER_INTERRUPT	0x20

#define HL_WAIT_MULTI_CS_LIST_MAX_LEN	32

struct hl_wait_cs_in {
	union {
		struct {
			/*
			 * In case of wait_cs holds the CS sequence number.
			 * In case of wait for multi CS hold a user pointer to
			 * an array of CS sequence numbers
			 */
			__u64 seq;
			/* Absolute timeout to wait for command submission
			 * in microseconds
			 */
			__u64 timeout_us;
		};

		struct {
			union {
				/* User address for completion comparison.
				 * upon interrupt, driver will compare the value pointed
				 * by this address with the supplied target value.
				 * in order not to perform any comparison, set address
				 * to all 1s.
				 * Relevant only when HL_WAIT_CS_FLAGS_INTERRUPT is set
				 */
				__u64 addr;

				/* cq_counters_handle to a kernel mapped cb which contains
				 * cq counters.
				 * Relevant only when HL_WAIT_CS_FLAGS_INTERRUPT_KERNEL_CQ is set
				 */
				__u64 cq_counters_handle;
			};

			/* Target value for completion comparison */
			__u64 target;
		};
	};

	/* Context ID - Currently not in use */
	__u32 ctx_id;

	/* HL_WAIT_CS_FLAGS_*
	 * If HL_WAIT_CS_FLAGS_INTERRUPT is set, this field should include
	 * interrupt id according to HL_WAIT_CS_FLAGS_INTERRUPT_MASK
	 *
	 * in order to wait for any CQ interrupt, set interrupt value to
	 * HL_WAIT_CS_FLAGS_ANY_CQ_INTERRUPT.
	 *
	 * in order to wait for any decoder interrupt, set interrupt value to
	 * HL_WAIT_CS_FLAGS_ANY_DEC_INTERRUPT.
	 */
	__u32 flags;

	union {
		struct {
			/* Multi CS API info- valid entries in multi-CS array */
			__u8 seq_arr_len;
			__u8 pad[7];
		};

		/* Absolute timeout to wait for an interrupt in microseconds.
		 * Relevant only when HL_WAIT_CS_FLAGS_INTERRUPT is set
		 */
		__u64 interrupt_timeout_us;
	};

	/*
	 * cq counter offset inside the counters cb pointed by cq_counters_handle above.
	 * upon interrupt, driver will compare the value pointed
	 * by this address (cq_counters_handle + cq_counters_offset)
	 * with the supplied target value.
	 * relevant only when HL_WAIT_CS_FLAGS_INTERRUPT_KERNEL_CQ is set
	 */
	__u64 cq_counters_offset;

	/*
	 * Timestamp_handle timestamps buffer handle.
	 * relevant only when HL_WAIT_CS_FLAGS_REGISTER_INTERRUPT is set
	 */
	__u64 timestamp_handle;

	/*
	 * Timestamp_offset is offset inside the timestamp buffer pointed by timestamp_handle above.
	 * upon interrupt, if the cq reached the target value then driver will write
	 * timestamp to this offset.
	 * relevant only when HL_WAIT_CS_FLAGS_REGISTER_INTERRUPT is set
	 */
	__u64 timestamp_offset;
};

#define HL_WAIT_CS_STATUS_COMPLETED	0
#define HL_WAIT_CS_STATUS_BUSY		1
#define HL_WAIT_CS_STATUS_TIMEDOUT	2
#define HL_WAIT_CS_STATUS_ABORTED	3

#define HL_WAIT_CS_STATUS_FLAG_GONE		0x1
#define HL_WAIT_CS_STATUS_FLAG_TIMESTAMP_VLD	0x2

struct hl_wait_cs_out {
	/* HL_WAIT_CS_STATUS_* */
	__u32 status;
	/* HL_WAIT_CS_STATUS_FLAG* */
	__u32 flags;
	/*
	 * valid only if HL_WAIT_CS_STATUS_FLAG_TIMESTAMP_VLD is set
	 * for wait_cs: timestamp of CS completion
	 * for wait_multi_cs: timestamp of FIRST CS completion
	 */
	__s64 timestamp_nsec;
	/* multi CS completion bitmap */
	__u32 cs_completion_map;
	__u32 pad;
};

union hl_wait_cs_args {
	struct hl_wait_cs_in in;
	struct hl_wait_cs_out out;
};

/* Opcode to allocate device memory */
#define HL_MEM_OP_ALLOC			0

/* Opcode to free previously allocated device memory */
#define HL_MEM_OP_FREE			1

/* Opcode to map host and device memory */
#define HL_MEM_OP_MAP			2

/* Opcode to unmap previously mapped host and device memory */
#define HL_MEM_OP_UNMAP			3

/* Opcode to map a hw block */
#define HL_MEM_OP_MAP_BLOCK		4

/* Opcode to create DMA-BUF object for an existing device memory allocation
 * and to export an FD of that DMA-BUF back to the caller
 */
#define HL_MEM_OP_EXPORT_DMABUF_FD	5

/* Opcode to create timestamps pool for user interrupts registration support
 * The memory will be allocated by the kernel driver, A timestamp buffer which the user
 * will get handle to it for mmap, and another internal buffer used by the
 * driver for registration management
 * The memory will be freed when the user closes the file descriptor(ctx close)
 */
#define HL_MEM_OP_TS_ALLOC		6

/* Memory flags */
#define HL_MEM_CONTIGUOUS	0x1
#define HL_MEM_SHARED		0x2
#define HL_MEM_USERPTR		0x4
#define HL_MEM_FORCE_HINT	0x8
#define HL_MEM_PREFETCH		0x40

/**
 * structure hl_mem_in - structure that handle input args for memory IOCTL
 * @union arg: union of structures to be used based on the input operation
 * @op: specify the requested memory operation (one of the HL_MEM_OP_* definitions).
 * @flags: flags for the memory operation (one of the HL_MEM_* definitions).
 *         For the HL_MEM_OP_EXPORT_DMABUF_FD opcode, this field holds the DMA-BUF file/FD flags.
 * @ctx_id: context ID - currently not in use.
 * @num_of_elements: number of timestamp elements used only with HL_MEM_OP_TS_ALLOC opcode.
 */
struct hl_mem_in {
	union {
		/**
		 * structure for device memory allocation (used with the HL_MEM_OP_ALLOC op)
		 * @mem_size: memory size to allocate
		 * @page_size: page size to use on allocation. when the value is 0 the default page
		 *             size will be taken.
		 */
		struct {
			__u64 mem_size;
			__u64 page_size;
		} alloc;

		/**
		 * structure for free-ing device memory (used with the HL_MEM_OP_FREE op)
		 * @handle: handle returned from HL_MEM_OP_ALLOC
		 */
		struct {
			__u64 handle;
		} free;

		/**
		 * structure for mapping device memory (used with the HL_MEM_OP_MAP op)
		 * @hint_addr: requested virtual address of mapped memory.
		 *             the driver will try to map the requested region to this hint
		 *             address, as long as the address is valid and not already mapped.
		 *             the user should check the returned address of the IOCTL to make
		 *             sure he got the hint address.
		 *             passing 0 here means that the driver will choose the address itself.
		 * @handle: handle returned from HL_MEM_OP_ALLOC.
		 */
		struct {
			__u64 hint_addr;
			__u64 handle;
		} map_device;

		/**
		 * structure for mapping host memory (used with the HL_MEM_OP_MAP op)
		 * @host_virt_addr: address of allocated host memory.
		 * @hint_addr: requested virtual address of mapped memory.
		 *             the driver will try to map the requested region to this hint
		 *             address, as long as the address is valid and not already mapped.
		 *             the user should check the returned address of the IOCTL to make
		 *             sure he got the hint address.
		 *             passing 0 here means that the driver will choose the address itself.
		 * @size: size of allocated host memory.
		 */
		struct {
			__u64 host_virt_addr;
			__u64 hint_addr;
			__u64 mem_size;
		} map_host;

		/**
		 * structure for mapping hw block (used with the HL_MEM_OP_MAP_BLOCK op)
		 * @block_addr:HW block address to map, a handle and size will be returned
		 *             to the user and will be used to mmap the relevant block.
		 *             only addresses from configuration space are allowed.
		 */
		struct {
			__u64 block_addr;
		} map_block;

		/**
		 * structure for unmapping host memory (used with the HL_MEM_OP_UNMAP op)
		 * @device_virt_addr: virtual address returned from HL_MEM_OP_MAP
		 */
		struct {
			__u64 device_virt_addr;
		} unmap;

		/**
		 * structure for exporting DMABUF object (used with
		 * the HL_MEM_OP_EXPORT_DMABUF_FD op)
		 * @addr: for Gaudi1, the driver expects a physical address
		 *        inside the device's DRAM. this is because in Gaudi1
		 *        we don't have MMU that covers the device's DRAM.
		 *        for all other ASICs, the driver expects a device
		 *        virtual address that represents the start address of
		 *        a mapped DRAM memory area inside the device.
		 *        the address must be the same as was received from the
		 *        driver during a previous HL_MEM_OP_MAP operation.
		 * @mem_size: size of memory to export.
		 * @offset: for Gaudi1, this value must be 0. For all other ASICs,
		 *          the driver expects an offset inside of the memory area
		 *          describe by addr. the offset represents the start
		 *          address of that the exported dma-buf object describes.
		 */
		struct {
			__u64 addr;
			__u64 mem_size;
			__u64 offset;
		} export_dmabuf_fd;
	};

	__u32 op;
	__u32 flags;
	__u32 ctx_id;
	__u32 num_of_elements;
};

struct hl_mem_out {
	union {
		/*
		 * Used for HL_MEM_OP_MAP as the virtual address that was
		 * assigned in the device VA space.
		 * A value of 0 means the requested operation failed.
		 */
		__u64 device_virt_addr;

		/*
		 * Used in HL_MEM_OP_ALLOC
		 * This is the assigned handle for the allocated memory
		 */
		__u64 handle;

		struct {
			/*
			 * Used in HL_MEM_OP_MAP_BLOCK.
			 * This is the assigned handle for the mapped block
			 */
			__u64 block_handle;

			/*
			 * Used in HL_MEM_OP_MAP_BLOCK
			 * This is the size of the mapped block
			 */
			__u32 block_size;

			__u32 pad;
		};

		/* Returned in HL_MEM_OP_EXPORT_DMABUF_FD. Represents the
		 * DMA-BUF object that was created to describe a memory
		 * allocation on the device's memory space. The FD should be
		 * passed to the importer driver
		 */
		__s32 fd;
	};
};

union hl_mem_args {
	struct hl_mem_in in;
	struct hl_mem_out out;
};

#define HL_DEBUG_MAX_AUX_VALUES		10

struct hl_debug_params_etr {
	/* Address in memory to allocate buffer */
	__u64 buffer_address;

	/* Size of buffer to allocate */
	__u64 buffer_size;

	/* Sink operation mode: SW fifo, HW fifo, Circular buffer */
	__u32 sink_mode;
	__u32 pad;
};

struct hl_debug_params_etf {
	/* Address in memory to allocate buffer */
	__u64 buffer_address;

	/* Size of buffer to allocate */
	__u64 buffer_size;

	/* Sink operation mode: SW fifo, HW fifo, Circular buffer */
	__u32 sink_mode;
	__u32 pad;
};

struct hl_debug_params_stm {
	/* Two bit masks for HW event and Stimulus Port */
	__u64 he_mask;
	__u64 sp_mask;

	/* Trace source ID */
	__u32 id;

	/* Frequency for the timestamp register */
	__u32 frequency;
};

struct hl_debug_params_bmon {
	/* Two address ranges that the user can request to filter */
	__u64 start_addr0;
	__u64 addr_mask0;

	__u64 start_addr1;
	__u64 addr_mask1;

	/* Capture window configuration */
	__u32 bw_win;
	__u32 win_capture;

	/* Trace source ID */
	__u32 id;

	/* Control register */
	__u32 control;

	/* Two more address ranges that the user can request to filter */
	__u64 start_addr2;
	__u64 end_addr2;

	__u64 start_addr3;
	__u64 end_addr3;
};

struct hl_debug_params_spmu {
	/* Event types selection */
	__u64 event_types[HL_DEBUG_MAX_AUX_VALUES];

	/* Number of event types selection */
	__u32 event_types_num;

	/* TRC configuration register values */
	__u32 pmtrc_val;
	__u32 trc_ctrl_host_val;
	__u32 trc_en_host_val;
};

/* Opcode for ETR component */
#define HL_DEBUG_OP_ETR		0
/* Opcode for ETF component */
#define HL_DEBUG_OP_ETF		1
/* Opcode for STM component */
#define HL_DEBUG_OP_STM		2
/* Opcode for FUNNEL component */
#define HL_DEBUG_OP_FUNNEL	3
/* Opcode for BMON component */
#define HL_DEBUG_OP_BMON	4
/* Opcode for SPMU component */
#define HL_DEBUG_OP_SPMU	5
/* Opcode for timestamp (deprecated) */
#define HL_DEBUG_OP_TIMESTAMP	6
/* Opcode for setting the device into or out of debug mode. The enable
 * variable should be 1 for enabling debug mode and 0 for disabling it
 */
#define HL_DEBUG_OP_SET_MODE	7

struct hl_debug_args {
	/*
	 * Pointer to user input structure.
	 * This field is relevant to specific opcodes.
	 */
	__u64 input_ptr;
	/* Pointer to user output structure */
	__u64 output_ptr;
	/* Size of user input structure */
	__u32 input_size;
	/* Size of user output structure */
	__u32 output_size;
	/* HL_DEBUG_OP_* */
	__u32 op;
	/*
	 * Register index in the component, taken from the debug_regs_index enum
	 * in the various ASIC header files
	 */
	__u32 reg_idx;
	/* Enable/disable */
	__u32 enable;
	/* Context ID - Currently not in use */
	__u32 ctx_id;
};

/*
 * Various information operations such as:
 * - H/W IP information
 * - Current dram usage
 *
 * The user calls this IOCTL with an opcode that describes the required
 * information. The user should supply a pointer to a user-allocated memory
 * chunk, which will be filled by the driver with the requested information.
 *
 * The user supplies the maximum amount of size to copy into the user's memory,
 * in order to prevent data corruption in case of differences between the
 * definitions of structures in kernel and userspace, e.g. in case of old
 * userspace and new kernel driver
 */
#define HL_IOCTL_INFO	\
		_IOWR('H', 0x01, struct hl_info_args)

/*
 * Command Buffer
 * - Request a Command Buffer
 * - Destroy a Command Buffer
 *
 * The command buffers are memory blocks that reside in DMA-able address
 * space and are physically contiguous so they can be accessed by the device
 * directly. They are allocated using the coherent DMA API.
 *
 * When creating a new CB, the IOCTL returns a handle of it, and the user-space
 * process needs to use that handle to mmap the buffer so it can access them.
 *
 * In some instances, the device must access the command buffer through the
 * device's MMU, and thus its memory should be mapped. In these cases, user can
 * indicate the driver that such a mapping is required.
 * The resulting device virtual address will be used internally by the driver,
 * and won't be returned to user.
 *
 */
#define HL_IOCTL_CB		\
		_IOWR('H', 0x02, union hl_cb_args)

/*
 * Command Submission
 *
 * To submit work to the device, the user need to call this IOCTL with a set
 * of JOBS. That set of JOBS constitutes a CS object.
 * Each JOB will be enqueued on a specific queue, according to the user's input.
 * There can be more then one JOB per queue.
 *
 * The CS IOCTL will receive two sets of JOBS. One set is for "restore" phase
 * and a second set is for "execution" phase.
 * The JOBS on the "restore" phase are enqueued only after context-switch
 * (or if its the first CS for this context). The user can also order the
 * driver to run the "restore" phase explicitly
 *
 * Goya/Gaudi:
 * There are two types of queues - external and internal. External queues
 * are DMA queues which transfer data from/to the Host. All other queues are
 * internal. The driver will get completion notifications from the device only
 * on JOBS which are enqueued in the external queues.
 *
 * Greco onwards:
 * There is a single type of queue for all types of engines, either DMA engines
 * for transfers from/to the host or inside the device, or compute engines.
 * The driver will get completion notifications from the device for all queues.
 *
 * For jobs on external queues, the user needs to create command buffers
 * through the CB ioctl and give the CB's handle to the CS ioctl. For jobs on
 * internal queues, the user needs to prepare a "command buffer" with packets
 * on either the device SRAM/DRAM or the host, and give the device address of
 * that buffer to the CS ioctl.
 * For jobs on H/W queues both options of command buffers are valid.
 *
 * This IOCTL is asynchronous in regard to the actual execution of the CS. This
 * means it returns immediately after ALL the JOBS were enqueued on their
 * relevant queues. Therefore, the user mustn't assume the CS has been completed
 * or has even started to execute.
 *
 * Upon successful enqueue, the IOCTL returns a sequence number which the user
 * can use with the "Wait for CS" IOCTL to check whether the handle's CS
 * non-internal JOBS have been completed. Note that if the CS has internal JOBS
 * which can execute AFTER the external JOBS have finished, the driver might
 * report that the CS has finished executing BEFORE the internal JOBS have
 * actually finished executing.
 *
 * Even though the sequence number increments per CS, the user can NOT
 * automatically assume that if CS with sequence number N finished, then CS
 * with sequence number N-1 also finished. The user can make this assumption if
 * and only if CS N and CS N-1 are exactly the same (same CBs for the same
 * queues).
 */
#define HL_IOCTL_CS			\
		_IOWR('H', 0x03, union hl_cs_args)

/*
 * Wait for Command Submission
 *
 * The user can call this IOCTL with a handle it received from the CS IOCTL
 * to wait until the handle's CS has finished executing. The user will wait
 * inside the kernel until the CS has finished or until the user-requested
 * timeout has expired.
 *
 * If the timeout value is 0, the driver won't sleep at all. It will check
 * the status of the CS and return immediately
 *
 * The return value of the IOCTL is a standard Linux error code. The possible
 * values are:
 *
 * EINTR     - Kernel waiting has been interrupted, e.g. due to OS signal
 *             that the user process received
 * ETIMEDOUT - The CS has caused a timeout on the device
 * EIO       - The CS was aborted (usually because the device was reset)
 * ENODEV    - The device wants to do hard-reset (so user need to close FD)
 *
 * The driver also returns a custom define in case the IOCTL call returned 0.
 * The define can be one of the following:
 *
 * HL_WAIT_CS_STATUS_COMPLETED   - The CS has been completed successfully (0)
 * HL_WAIT_CS_STATUS_BUSY        - The CS is still executing (0)
 * HL_WAIT_CS_STATUS_TIMEDOUT    - The CS has caused a timeout on the device
 *                                 (ETIMEDOUT)
 * HL_WAIT_CS_STATUS_ABORTED     - The CS was aborted, usually because the
 *                                 device was reset (EIO)
 */

#define HL_IOCTL_WAIT_CS			\
		_IOWR('H', 0x04, union hl_wait_cs_args)

/*
 * Memory
 * - Map host memory to device MMU
 * - Unmap host memory from device MMU
 *
 * This IOCTL allows the user to map host memory to the device MMU
 *
 * For host memory, the IOCTL doesn't allocate memory. The user is supposed
 * to allocate the memory in user-space (malloc/new). The driver pins the
 * physical pages (up to the allowed limit by the OS), assigns a virtual
 * address in the device VA space and initializes the device MMU.
 *
 * There is an option for the user to specify the requested virtual address.
 *
 */
#define HL_IOCTL_MEMORY		\
		_IOWR('H', 0x05, union hl_mem_args)

/*
 * Debug
 * - Enable/disable the ETR/ETF/FUNNEL/STM/BMON/SPMU debug traces
 *
 * This IOCTL allows the user to get debug traces from the chip.
 *
 * Before the user can send configuration requests of the various
 * debug/profile engines, it needs to set the device into debug mode.
 * This is because the debug/profile infrastructure is shared component in the
 * device and we can't allow multiple users to access it at the same time.
 *
 * Once a user set the device into debug mode, the driver won't allow other
 * users to "work" with the device, i.e. open a FD. If there are multiple users
 * opened on the device, the driver won't allow any user to debug the device.
 *
 * For each configuration request, the user needs to provide the register index
 * and essential data such as buffer address and size.
 *
 * Once the user has finished using the debug/profile engines, he should
 * set the device into non-debug mode, i.e. disable debug mode.
 *
 * The driver can decide to "kick out" the user if he abuses this interface.
 *
 */
#define HL_IOCTL_DEBUG		\
		_IOWR('H', 0x06, struct hl_debug_args)

#define HL_COMMAND_START	0x01
#define HL_COMMAND_END		0x07

#endif /* HABANALABS_H_ */
