// SPDX-License-Identifier: GPL-2.0+
// Copyright 2017-2020 NXP

#include <linux/module.h>
#include <linux/rpmsg.h>
#include "imx-pcm-rpmsg.h"

/*
 * struct imx_audio_rpmsg: private data
 *
 * @rpmsg_pdev: pointer of platform device
 */
struct imx_audio_rpmsg {
	struct platform_device *rpmsg_pdev;
};

static int imx_audio_rpmsg_cb(struct rpmsg_device *rpdev, void *data, int len,
			      void *priv, u32 src)
{
	struct imx_audio_rpmsg *rpmsg = dev_get_drvdata(&rpdev->dev);
	struct rpmsg_r_msg *r_msg = (struct rpmsg_r_msg *)data;
	struct rpmsg_info *info;
	struct rpmsg_msg *msg;
	unsigned long flags;

	if (!rpmsg->rpmsg_pdev)
		return 0;

	info = platform_get_drvdata(rpmsg->rpmsg_pdev);

	dev_dbg(&rpdev->dev, "get from%d: cmd:%d. %d\n",
		src, r_msg->header.cmd, r_msg->param.resp);

	switch (r_msg->header.type) {
	case MSG_TYPE_C:
		/* TYPE C is notification from M core */
		switch (r_msg->header.cmd) {
		case TX_PERIOD_DONE:
			spin_lock_irqsave(&info->lock[TX], flags);
			msg = &info->msg[TX_PERIOD_DONE + MSG_TYPE_A_NUM];
			msg->r_msg.param.buffer_tail =
						r_msg->param.buffer_tail;
			msg->r_msg.param.buffer_tail %= info->num_period[TX];
			spin_unlock_irqrestore(&info->lock[TX], flags);
			info->callback[TX](info->callback_param[TX]);
			break;
		case RX_PERIOD_DONE:
			spin_lock_irqsave(&info->lock[RX], flags);
			msg = &info->msg[RX_PERIOD_DONE + MSG_TYPE_A_NUM];
			msg->r_msg.param.buffer_tail =
						r_msg->param.buffer_tail;
			msg->r_msg.param.buffer_tail %= info->num_period[1];
			spin_unlock_irqrestore(&info->lock[RX], flags);
			info->callback[RX](info->callback_param[RX]);
			break;
		default:
			dev_warn(&rpdev->dev, "unknown msg command\n");
			break;
		}
		break;
	case MSG_TYPE_B:
		/* TYPE B is response msg */
		memcpy(&info->r_msg, r_msg, sizeof(struct rpmsg_r_msg));
		complete(&info->cmd_complete);
		break;
	default:
		dev_warn(&rpdev->dev, "unknown msg type\n");
		break;
	}

	return 0;
}

static int imx_audio_rpmsg_probe(struct rpmsg_device *rpdev)
{
	struct imx_audio_rpmsg *data;
	int ret = 0;

	dev_info(&rpdev->dev, "new channel: 0x%x -> 0x%x!\n",
		 rpdev->src, rpdev->dst);

	data = devm_kzalloc(&rpdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	dev_set_drvdata(&rpdev->dev, data);

	/* Register platform driver for rpmsg routine */
	data->rpmsg_pdev = platform_device_register_data(&rpdev->dev,
							 IMX_PCM_DRV_NAME,
							 PLATFORM_DEVID_AUTO,
							 NULL, 0);
	if (IS_ERR(data->rpmsg_pdev)) {
		dev_err(&rpdev->dev, "failed to register rpmsg platform.\n");
		ret = PTR_ERR(data->rpmsg_pdev);
	}

	return ret;
}

static void imx_audio_rpmsg_remove(struct rpmsg_device *rpdev)
{
	struct imx_audio_rpmsg *data = dev_get_drvdata(&rpdev->dev);

	if (data->rpmsg_pdev)
		platform_device_unregister(data->rpmsg_pdev);

	dev_info(&rpdev->dev, "audio rpmsg driver is removed\n");
}

static struct rpmsg_device_id imx_audio_rpmsg_id_table[] = {
	{ .name	= "rpmsg-audio-channel" },
	{ .name = "rpmsg-micfil-channel" },
	{ },
};

static struct rpmsg_driver imx_audio_rpmsg_driver = {
	.drv.name	= "imx_audio_rpmsg",
	.drv.owner	= THIS_MODULE,
	.id_table	= imx_audio_rpmsg_id_table,
	.probe		= imx_audio_rpmsg_probe,
	.callback	= imx_audio_rpmsg_cb,
	.remove		= imx_audio_rpmsg_remove,
};

module_rpmsg_driver(imx_audio_rpmsg_driver);

MODULE_DESCRIPTION("Freescale SoC Audio RPMSG interface");
MODULE_AUTHOR("Shengjiu Wang <shengjiu.wang@nxp.com>");
MODULE_ALIAS("platform:imx_audio_rpmsg");
MODULE_LICENSE("GPL v2");
