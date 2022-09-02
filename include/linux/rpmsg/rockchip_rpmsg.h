/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2022 Rockchip Electronics Co., Ltd.
 */
#ifndef ROCKCHIP_RPMSG_H
#define ROCKCHIP_RPMSG_H

/* rpmsg flag bit definition
 * bit 0: Set 1 to indicate remote processor is ready
 * bit 1: Set 1 to use reserved memory region as shared DMA pool
 * bit 2: Set 1 to use cached share memory as vring buffer
 */
#define RPMSG_REMOTE_IS_READY			BIT(0)
#define RPMSG_SHARED_DMA_POOL			BIT(1)
#define RPMSG_CACHED_VRING			BIT(2)

#define RPMSG_VIRTIO_RPMSG_F_NS			BIT(0)

#define RPMSG_BUF_PAYLOAD_SIZE			(496UL)
/* rpmsg buffer size is formed by payload size and struct rpmsg_hdr */
#define RPMSG_BUF_SIZE				(RPMSG_BUF_PAYLOAD_SIZE + 16UL)
/* rpmsg buffer count for each direction */
#define RPMSG_BUF_COUNT				(64UL)
/* rpmsg endpoint size is equal to rpmsg buffer size */
#define RPMSG_EPT_SIZE				RPMSG_BUF_SIZE

#define RPMSG_MAX_INSTANCE_NUM			(12U)
#define RPMSG_MAX_LINK_ID			(0xFFU)

#define RPMSG_MBOX_MAGIC			(0x524D5347U)

/* Linux requires the ALIGN to 0x1000(4KB) */
#define RPMSG_VRING_ALIGN			(0x1000UL)
/* contains pool of descriptors and two circular buffers */
#define RPMSG_VRING_SIZE			(0x8000UL)
/* size of 2 * RPMSG_VRING_SIZE */
#define RPMSG_VRING_OVERHEAD			(2UL * RPMSG_VRING_SIZE)

/* link_id: 4 bit master cpu_id and 4 bit remote_id */
#define RPMSG_GET_M_CPU_ID(link_id)		(((link_id) & 0xF0U) >> 4U)
#define RPMSG_GET_R_CPU_ID(link_id)		((link_id) & 0xFU)

#endif /* ROCKCHIP_RPMSG_H */
