/*
 * starfive.c -- Hardware interface for audio DSP on Starfive
 *
 * Copyright (C) 2023 StarFive Technology Co., Ltd.
 *
 * Author: Carter Li <carter.li@starfivetech.com>
 */

#include <linux/clk.h>
#include <linux/firmware.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/pm_domain.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <sound/sof.h>
#include <sound/sof/xtensa.h>
#include "../ops.h"
#include "starfive-common.h"
#include "dsp.h"
#include "../sof-audio.h"

/* DSP memories */
#define JH7110_IRAM_OFFSET      0x10000
#define JH7110_IRAM_SIZE        (2 * 1024)
#define JH7110_DRAM0_OFFSET     0x0
#define JH7110_DRAM0_SIZE       (32 * 1024)
#define JH7110_DRAM1_OFFSET     0x8000
#define JH7110_DRAM1_SIZE       (32 * 1024)
#define JH7110_SYSRAM_OFFSET    0x18000
#define JH7110_SYSRAM_SIZE      (256 * 1024)
#define JH7110_SYSROM_OFFSET    0x58000
#define JH7110_SYSROM_SIZE      (192 * 1024)

#define JH7110_MBOX_OFFSET      0x2001000
#define JH7110_MBOX_SIZE        0x1000
/* DSP control */
#define JH7110_RESET_VECTOR_VADDR                           0x69c00000
#define JH7110_STG_RUNSTALLADDR_OFFSET                      0x38U
#define JH7110_STG_STATVECTORSELADDR_OFFSET                 0x44U
#define JH7110_STG_ALTRESETVECADDR_OFFSET                   0x2CU
#define JH7110_U0_HIFI4_STATVECTORSEL_SHIFT                 0xCU
#define JH7110_U0_HIFI4_ALTRESETVEC_SHIFT                   0x0U
#define JH7110_U0_HIFI4_RUNSTALL_SHIFT                      0x12U
#define JH7110_U0_HIFI4_STATVECTORSEL_MASK                  0x1000U
#define JH7110_U0_HIFI4_ALTRESETVEC_MASK                    0xFFFFFFFFU
#define JH7110_U0_HIFI4_RUNSTALL_MASK                       0x40000U
#define JH7110_HIFI4_CORE_CLK_FREQ_HZ                       594000000

struct jh7110_hw {
	phys_addr_t crg_regs_phys;
	void __iomem *crg_regs;
	phys_addr_t syscon_regs_phys;
	void __iomem *syscon_regs;
	struct clk *core_clk;
	struct reset_control *core_rst;
	struct reset_control *axi_rst;
	struct regmap *syscon_regmap;
};

struct jh7110_priv {
	struct device *dev;
	struct snd_sof_dev *sdev;

	/* DSP IPC handler */
	struct jh7110_dsp_ipc *dsp_ipc;
	struct platform_device *ipc_dev;

	/* System Controller IPC handler */
	struct jh7110_sc_ipc *sc_ipc;

	/* Power domain handling */
	int num_domains;
	struct device **pd_dev;
	struct device_link **link;

	struct jh7110_hw hw;
};

static void jh7110_get_reply(struct snd_sof_dev *sdev)
{
	struct snd_sof_ipc_msg *msg = sdev->msg;
	struct sof_ipc_reply reply;
	int ret = 0;

	if (!msg) {
		dev_warn(sdev->dev, "unexpected ipc interrupt\n");
		return;
	}

	/* get reply */
	sof_mailbox_read(sdev, sdev->host_box.offset, &reply, sizeof(reply));

	if (unlikely(reply.error < 0)) {
		memcpy(msg->reply_data, &reply, sizeof(reply));
		ret = reply.error;
	} else {
		/* reply has correct size? */
		if (unlikely(reply.hdr.size != msg->reply_size)) {
			dev_err(sdev->dev, "error: reply expected %zu got %u bytes\n",
				msg->reply_size, reply.hdr.size);
			ret = -EINVAL;
		}

		/* read the message */
		if (likely(msg->reply_size > 0))
			sof_mailbox_read(sdev, sdev->host_box.offset,
					 msg->reply_data, msg->reply_size);
	}

	msg->reply_error = ret;
}

static int jh7110_get_mailbox_offset(struct snd_sof_dev *sdev)
{
	return JH7110_MBOX_OFFSET;
}

static int jh7110_get_window_offset(struct snd_sof_dev *sdev, u32 id)
{
	return JH7110_MBOX_OFFSET;
}

static void jh7110_dsp_handle_reply(struct jh7110_dsp_ipc *ipc)
{
	struct jh7110_priv *priv = jh7110_dsp_get_data(ipc);
	unsigned long flags;

	spin_lock_irqsave(&priv->sdev->ipc_lock, flags);
	jh7110_get_reply(priv->sdev);
	snd_sof_ipc_reply(priv->sdev, 0);
	spin_unlock_irqrestore(&priv->sdev->ipc_lock, flags);
}

static void jh7110_dsp_handle_request(struct jh7110_dsp_ipc *ipc)
{
	struct jh7110_priv *priv = jh7110_dsp_get_data(ipc);
	u32 p; /* panic code */

	/* Read the message from the debug box. */
	sof_mailbox_read(priv->sdev, priv->sdev->debug_box.offset + 4, &p, sizeof(p));

	/* Check to see if the message is a panic code (0x0dead***) */
	if (unlikely((p & SOF_IPC_PANIC_MAGIC_MASK) == SOF_IPC_PANIC_MAGIC))
		snd_sof_dsp_panic(priv->sdev, p);
	else
		snd_sof_ipc_msgs_rx(priv->sdev);
}

static struct jh7110_dsp_ops dsp_ops = {
	.handle_reply		= jh7110_dsp_handle_reply,
	.handle_request		= jh7110_dsp_handle_request,
};

static int jh7110_send_msg(struct snd_sof_dev *sdev, struct snd_sof_ipc_msg *msg)
{
	struct jh7110_priv *priv = sdev->pdata->hw_pdata;

	sof_mailbox_write(sdev, sdev->host_box.offset, msg->msg_data,
			  msg->msg_size);
	jh7110_dsp_ring_doorbell(priv->dsp_ipc, 0);

	return 0;
}

/*
 * DSP control.
 */
static int jh7110_dsp_reset(struct jh7110_hw *hw)
{
	int ret;

	if (NULL == hw)
		return -EINVAL;

	regmap_update_bits(hw->syscon_regmap, JH7110_STG_STATVECTORSELADDR_OFFSET,
						JH7110_U0_HIFI4_STATVECTORSEL_MASK, (1 << JH7110_U0_HIFI4_STATVECTORSEL_SHIFT));
	regmap_update_bits(hw->syscon_regmap, JH7110_STG_ALTRESETVECADDR_OFFSET,
						JH7110_U0_HIFI4_ALTRESETVEC_MASK, JH7110_RESET_VECTOR_VADDR);

	clk_prepare_enable(hw->core_clk);

	clk_set_rate(hw->core_clk, JH7110_HIFI4_CORE_CLK_FREQ_HZ);

	ret = reset_control_assert(hw->axi_rst);
	if (ret) {
		pr_err("failed to assert dsp axi rst.\n");
		goto err_reset;
	}

	ret = reset_control_assert(hw->core_rst);
	if (ret) {
		pr_err("failed to assert dsp core rst.\n");
		goto err_reset;
	}

	ret = reset_control_deassert(hw->axi_rst);
	if (ret) {
		pr_err("failed to deassert dsp axi rst.\n");
		goto err_reset;
	}

	ret = reset_control_deassert(hw->core_rst);
	if (ret) {
		pr_err("failed to deassert dsp core rst.\n");
		goto err_reset;
	}

	pr_debug("jh7110 dsp reset.\n");

	return 0;

err_reset:
	clk_disable_unprepare(hw->core_clk);

	return ret;
}

static int jh7110_dsp_enable(struct jh7110_hw *hw)
{
	int ret;

	if (NULL == hw)
		return -EINVAL;

	clk_prepare_enable(hw->core_clk);
	clk_set_rate(hw->core_clk, JH7110_HIFI4_CORE_CLK_FREQ_HZ);

	ret = reset_control_deassert(hw->axi_rst);
	if (ret) {
		pr_err("failed to deassert dsp axi rst.\n");
		goto err_reset;
	}

	ret = reset_control_deassert(hw->core_rst);
	if (ret) {
		pr_err("failed to deassert dsp core rst.\n");
		goto err_reset;
	}

	pr_debug("jh7110 dsp enable ...\n");

	return 0;

err_reset:
	clk_disable_unprepare(hw->core_clk);

	return ret;
}

static int jh7110_dsp_disable(struct jh7110_hw *hw)
{
	int ret;

	reset_control_assert(hw->core_rst);
	if (ret) {
		pr_err("failed to deassert dsp core rst.\n");
		goto err_reset;
	}

	reset_control_assert(hw->axi_rst);
	if (ret) {
		pr_err("failed to deassert dsp axi rst.\n");
		goto err_reset;
	}

	clk_disable_unprepare(hw->core_clk);

	pr_debug("jh7110 dsp disable.\n");

	return 0;

err_reset:
	clk_disable_unprepare(hw->core_clk);

	return ret;
}

int jh7110_dsp_halt(struct jh7110_hw *hw)
{
	if (NULL == hw)
		return -EINVAL;

	regmap_update_bits(hw->syscon_regmap, JH7110_STG_RUNSTALLADDR_OFFSET,
						JH7110_U0_HIFI4_RUNSTALL_MASK, (1 << JH7110_U0_HIFI4_RUNSTALL_SHIFT));
	pr_debug("jh7110 dsp halt.\n");

	return 0;
}

int jh7110_dsp_release(struct jh7110_hw *hw)
{
	if (NULL == hw)
		return -EINVAL;

	regmap_update_bits(hw->syscon_regmap, JH7110_STG_RUNSTALLADDR_OFFSET,
						JH7110_U0_HIFI4_RUNSTALL_MASK, (0 << JH7110_U0_HIFI4_RUNSTALL_SHIFT));
	pr_debug("jh7110 dsp begin run.\n");

	return 0;
}

static int jh7110_dsp_hw_init(struct platform_device *pdev, struct jh7110_hw *hw)
{
	hw->syscon_regmap = syscon_regmap_lookup_by_phandle(pdev->dev.of_node, "starfive,stg-syscon");
	if (IS_ERR(hw->syscon_regmap)) {
		dev_err(&pdev->dev, "can't get starfive,stg-syscon.\n");
		return PTR_ERR(hw->syscon_regmap);
	}

	hw->core_clk = devm_clk_get(&pdev->dev, "core_clk");
	if (IS_ERR(hw->core_clk))
		return -ENODEV;

	hw->core_rst = devm_reset_control_get(&pdev->dev, "rst_core");
	if (IS_ERR(hw->core_rst))
		return -ENODEV;

	hw->axi_rst = devm_reset_control_get(&pdev->dev, "rst_axi");
	if (IS_ERR(hw->axi_rst))
		return -ENODEV;

	dev_dbg(&pdev->dev, "get rst handle ok.\n");
	return 0;
}


static int jh7110_run(struct snd_sof_dev *sdev)
{
	struct jh7110_priv *priv = sdev->pdata->hw_pdata;
	struct jh7110_hw *hw = &priv->hw;
	int ret;

	ret = jh7110_dsp_halt(hw);
	if (ret) {
		dev_err(sdev->dev, "dsp halt err\n");
		return ret;
	}

	ret = jh7110_dsp_reset(hw);
	if (ret) {
		dev_err(sdev->dev, "dsp reset err\n");
		return ret;
	}

	ret = jh7110_dsp_release(hw);
	if (ret) {
		dev_err(sdev->dev, "dsp release err\n");
		return ret;
	}

	dev_info(sdev->dev, "jh7110 dsp run success\n");

	return 0;
}

static int jh7110_probe(struct snd_sof_dev *sdev)
{
	struct platform_device *pdev =
		container_of(sdev->dev, struct platform_device, dev);
	struct device_node *np = pdev->dev.of_node;
	struct device_node *res_node;
	struct resource *mmio;
	struct jh7110_priv *priv;
	struct resource res;
	u32 base, size;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	ret = jh7110_dsp_hw_init(pdev, &priv->hw);
	if (ret) {
		dev_err(&pdev->dev, "jh7110_dsp_hw_init failed\n");
		return ret;
	} else {
		dev_dbg(&pdev->dev, "jh7110_dsp_hw_init success\n");
	}

	sdev->pdata->hw_pdata = priv;
	priv->dev = sdev->dev;
	priv->sdev = sdev;

	/* set jh7110-dsp(provide low-levlel ipc function: ring doorbell handle rx msg) */
	/* handle rx msg need handle_reply and handle_reply from jh7110 */
	priv->ipc_dev = platform_device_register_data(sdev->dev, "jh7110-dsp",
						      PLATFORM_DEVID_NONE,
						      pdev, sizeof(*pdev));
	if (IS_ERR(priv->ipc_dev)) {
		ret = PTR_ERR(priv->ipc_dev);
		dev_err(sdev->dev, "Failed to get platform_device_register_data\n");
	}

	priv->dsp_ipc = dev_get_drvdata(&priv->ipc_dev->dev);
	if (!priv->dsp_ipc) {
		/* DSP IPC driver not probed yet, try later */
		ret = -EPROBE_DEFER;
		dev_err(sdev->dev, "Failed to get drvdata\n");
		goto exit_pdev_unregister;
	}

	jh7110_dsp_set_data(priv->dsp_ipc, priv);
	priv->dsp_ipc->ops = &dsp_ops;

	/* DSP base */
	mmio = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (mmio) {
		base = mmio->start;
		size = resource_size(mmio);
	} else {
		dev_err(sdev->dev, "error: failed to get DSP base at idx 0\n");
		ret = -EINVAL;
		goto exit_pdev_unregister;
	}

	sdev->bar[SOF_FW_BLK_TYPE_IRAM] = devm_ioremap(sdev->dev, base, size);
	if (!sdev->bar[SOF_FW_BLK_TYPE_IRAM]) {
		dev_err(sdev->dev, "failed to ioremap base 0x%x size 0x%x\n",
			base, size);
		ret = -ENODEV;
		goto exit_pdev_unregister;
	}
	sdev->mmio_bar = SOF_FW_BLK_TYPE_IRAM;

	res_node = of_parse_phandle(np, "memory-region", 0);
	if (!res_node) {
		dev_err(&pdev->dev, "failed to get memory region node\n");
		ret = -ENODEV;
		goto exit_pdev_unregister;
	}

	ret = of_address_to_resource(res_node, 0, &res);
	of_node_put(res_node);
	if (ret) {
		dev_err(&pdev->dev, "failed to get reserved region address\n");
		goto exit_pdev_unregister;
	}

	sdev->bar[SOF_FW_BLK_TYPE_SRAM] = devm_ioremap_wc(sdev->dev, res.start,
							  resource_size(&res));
	if (!sdev->bar[SOF_FW_BLK_TYPE_SRAM]) {
		dev_err(sdev->dev, "failed to ioremap mem 0x%x size 0x%x\n",
			base, size);
		ret = -ENOMEM;
		goto exit_pdev_unregister;
	}
	sdev->mailbox_bar = SOF_FW_BLK_TYPE_SRAM;

	/* set default mailbox offset for FW ready message */
	sdev->dsp_box.offset = JH7110_MBOX_OFFSET;

	dev_dbg(&pdev->dev, "dsp_box.offset: 0x%x, res.start: 0x%llx, ",
		sdev->dsp_box.offset,
		res.start);
	dev_dbg(&pdev->dev, "res_size: 0x%llx, sram bar: 0x%p, base:0x%x ",
		resource_size(&res),
		sdev->bar[SOF_FW_BLK_TYPE_SRAM], base);

	return 0;

exit_pdev_unregister:
	platform_device_unregister(priv->ipc_dev);

	return ret;
}

static int jh7110_remove(struct snd_sof_dev *sdev)
{
	struct jh7110_priv *priv = sdev->pdata->hw_pdata;
	int i;

	platform_device_unregister(priv->ipc_dev);

	for (i = 0; i < priv->num_domains; i++) {
		device_link_del(priv->link[i]);
		dev_pm_domain_detach(priv->pd_dev[i], false);
	}

	return 0;
}

/* there is 1 to 1 match between type and BAR idx */
static int jh7110_get_bar_index(struct snd_sof_dev *sdev, u32 type)
{
	/* Only IRAM and SRAM bars are valid */
	switch (type) {
	case SOF_FW_BLK_TYPE_IRAM:
	case SOF_FW_BLK_TYPE_SRAM:
		return type;
	default:
		return -EINVAL;
	}
}

static void jh7110_ipc_msg_data(struct snd_sof_dev *sdev,
			      struct snd_pcm_substream *substream,
			      void *p, size_t sz)
{
	sof_mailbox_read(sdev, sdev->dsp_box.offset, p, sz);
}

static int jh7110_ipc_pcm_params(struct snd_sof_dev *sdev,
			       struct snd_pcm_substream *substream,
			       const struct sof_ipc_pcm_params_reply *reply)
{
	return 0;
}

static void jh7110_machine_select(struct snd_sof_dev *sdev)
{
	struct platform_device *pdev =
		container_of(sdev->dev, struct platform_device, dev);
	struct snd_sof_pdata *sof_pdata = sdev->pdata;
	struct snd_soc_acpi_mach *mach;
	struct device_node *np = pdev->dev.of_node;
	int ret;

	mach = devm_kzalloc(sdev->dev, sizeof(*mach), GFP_KERNEL);
	if (!mach) {
		dev_err(sdev->dev, "failed to alloc mem for machine\n");
		return;
	}

	ret = of_property_read_string(np, "machine-drv-name", &mach->drv_name);
	if (ret)
		dev_dbg(sdev->dev, "machine-drv-name empty in device-tree\n");

	ret = of_property_read_string(np, "firmware-name", &mach->sof_fw_filename);
	if (ret) {
		dev_dbg(sdev->dev, "firmware-name parse error in device-tree: %d\n", ret);
		mach->sof_fw_filename = sof_pdata->fw_filename;
	}

	ret = of_property_read_string(np, "tplg-name", &mach->sof_tplg_filename);
	if (ret) {
		dev_err(sdev->dev, "tplg-name parse error in device-tree: %d\n", ret);
		devm_kfree(sdev->dev, mach);
		return;
	}

	sof_pdata->machine = mach;
}

static void jh7110_machine_parm_set(const struct snd_soc_acpi_mach *mach,
				struct snd_sof_dev *sdev)
{
	struct snd_sof_pdata *sof_pdata = sdev->pdata;
	const struct sof_dev_desc *desc = sof_pdata->desc;
	struct snd_soc_acpi_mach_params *mach_params;

	if (!strcmp(mach->drv_name, "sof-nocodec")) {
		return;
	} else if (!!mach) {
		sof_pdata->fw_filename = mach->sof_fw_filename;
		sof_pdata->tplg_filename = mach->sof_tplg_filename;
	} else
		dev_err(sdev->dev, "empty machine happened!\n");

	mach_params = (struct snd_soc_acpi_mach_params *)&mach->mach_params;
	mach_params->platform = dev_name(sdev->dev);
	mach_params->num_dai_drivers = desc->ops->num_drv;
	mach_params->dai_drivers = desc->ops->drv;
}

static struct snd_soc_dai_driver jh7110_dai[] = {
{
	.name = "ssp0-xxx",
	.playback = {
		.channels_min = 1,
		.channels_max = 8,
	},
	.capture = {
		.channels_min = 1,
		.channels_max = 8,
	},
}
};

/* jh7110 ops */
struct snd_sof_dsp_ops sof_jh7110_ops = {
	/* probe and remove */
	.probe		= jh7110_probe,
	.remove		= jh7110_remove,
	/* DSP core boot */
	.run		= jh7110_run,

	/* Block IO */
	.block_read	= sof_block_read,
	.block_write = sof_block_write,

	/* Module IO */
	.read64	= sof_io_read64,

	/* ipc */
	.send_msg	= jh7110_send_msg,
	.fw_ready	= sof_fw_ready,
	.get_mailbox_offset	= jh7110_get_mailbox_offset,
	.get_window_offset	= jh7110_get_window_offset,

	.ipc_msg_data	= jh7110_ipc_msg_data,
	.ipc_pcm_params	= jh7110_ipc_pcm_params,

	/* module loading */
	.load_module	= snd_sof_parse_module_memcpy,
	.get_bar_index	= jh7110_get_bar_index,
	/* firmware loading */
	.load_firmware	= snd_sof_load_firmware_memcpy,
	/* machine setting */
	.machine_select = jh7110_machine_select,
	.set_mach_params = jh7110_machine_parm_set,

	/* machine driver */
	.machine_register = sof_machine_register,
	.machine_unregister = sof_machine_unregister,

	/* Debug information */
	.dbg_dump = jh7110_dump,

	/* Firmware ops */
	.arch_ops = &sof_xtensa_arch_ops,

	/* DAI drivers */
	.drv = jh7110_dai,
	.num_drv = ARRAY_SIZE(jh7110_dai),

	/* ALSA HW info flags */
	.hw_info =	SNDRV_PCM_INFO_MMAP |
			SNDRV_PCM_INFO_MMAP_VALID |
			SNDRV_PCM_INFO_INTERLEAVED |
			SNDRV_PCM_INFO_PAUSE |
			SNDRV_PCM_INFO_NO_PERIOD_WAKEUP,
};
EXPORT_SYMBOL(sof_jh7110_ops);

MODULE_AUTHOR("Carter Li <carter.li@starfivetech.com>");
MODULE_IMPORT_NS(SND_SOC_SOF_XTENSA);
MODULE_LICENSE("Dual BSD/GPL");
