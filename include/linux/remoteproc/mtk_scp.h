/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#ifndef _MTK_SCP_H
#define _MTK_SCP_H

#include <linux/platform_device.h>

typedef void (*scp_ipi_handler_t) (void *data,
				   unsigned int len,
				   void *priv);
struct mtk_scp;

/**
 * enum ipi_id - the id of inter-processor interrupt
 *
 * @SCP_IPI_INIT:	 The interrupt from scp is to notfiy kernel
 *			 SCP initialization completed.
 *			 IPI_SCP_INIT is sent from SCP when firmware is
 *			 loaded. AP doesn't need to send IPI_SCP_INIT
 *			 command to SCP.
 *			 For other IPI below, AP should send the request
 *			 to SCP to trigger the interrupt.
 * @SCP_IPI_MAX:	 The maximum IPI number
 */

enum scp_ipi_id {
	SCP_IPI_INIT = 0,
	SCP_IPI_VDEC_H264,
	SCP_IPI_VDEC_VP8,
	SCP_IPI_VDEC_VP9,
	SCP_IPI_VENC_H264,
	SCP_IPI_VENC_VP8,
	SCP_IPI_MDP_INIT,
	SCP_IPI_MDP_DEINIT,
	SCP_IPI_MDP_FRAME,
	SCP_IPI_DIP,
	SCP_IPI_ISP_CMD,
	SCP_IPI_ISP_FRAME,
	SCP_IPI_FD_CMD,
	SCP_IPI_CROS_HOST_CMD,
	SCP_IPI_VDEC_LAT,
	SCP_IPI_VDEC_CORE,
	SCP_IPI_IMGSYS_CMD,
	SCP_IPI_NS_SERVICE = 0xFF,
	SCP_IPI_MAX = 0x100,
};

struct mtk_scp *scp_get(struct platform_device *pdev);
void scp_put(struct mtk_scp *scp);

struct device *scp_get_device(struct mtk_scp *scp);
struct rproc *scp_get_rproc(struct mtk_scp *scp);

int scp_ipi_register(struct mtk_scp *scp, u32 id, scp_ipi_handler_t handler,
		     void *priv);
void scp_ipi_unregister(struct mtk_scp *scp, u32 id);

int scp_ipi_send(struct mtk_scp *scp, u32 id, void *buf, unsigned int len,
		 unsigned int wait);

unsigned int scp_get_vdec_hw_capa(struct mtk_scp *scp);
unsigned int scp_get_venc_hw_capa(struct mtk_scp *scp);

void *scp_mapping_dm_addr(struct mtk_scp *scp, u32 mem_addr);

#endif /* _MTK_SCP_H */
