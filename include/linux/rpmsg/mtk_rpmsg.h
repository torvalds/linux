/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2019 Google LLC.
 */

#ifndef __LINUX_RPMSG_MTK_RPMSG_H
#define __LINUX_RPMSG_MTK_RPMSG_H

#include <linux/platform_device.h>
#include <linux/remoteproc.h>

typedef void (*ipi_handler_t)(void *data, unsigned int len, void *priv);

/*
 * struct mtk_rpmsg_info - IPI functions tied to the rpmsg device.
 * @register_ipi: register IPI handler for an IPI id.
 * @unregister_ipi: unregister IPI handler for a registered IPI id.
 * @send_ipi: send IPI to an IPI id. wait is the timeout (in msecs) to wait
 *            until response, or 0 if there's no timeout.
 * @ns_ipi_id: the IPI id used for name service, or -1 if name service isn't
 *             supported.
 */
struct mtk_rpmsg_info {
	int (*register_ipi)(struct platform_device *pdev, u32 id,
			    ipi_handler_t handler, void *priv);
	void (*unregister_ipi)(struct platform_device *pdev, u32 id);
	int (*send_ipi)(struct platform_device *pdev, u32 id,
			void *buf, unsigned int len, unsigned int wait);
	int ns_ipi_id;
};

struct rproc_subdev *
mtk_rpmsg_create_rproc_subdev(struct platform_device *pdev,
			      struct mtk_rpmsg_info *info);

void mtk_rpmsg_destroy_rproc_subdev(struct rproc_subdev *subdev);

#endif
