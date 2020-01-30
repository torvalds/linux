// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//

/*
 * Hardware interface for audio DSP on Baytrail, Braswell and Cherrytrail.
 */

#include <linux/module.h>
#include <sound/sof.h>
#include <sound/sof/xtensa.h>
#include "../ops.h"
#include "shim.h"

/* DSP memories */
#define IRAM_OFFSET		0x0C0000
#define IRAM_SIZE		(80 * 1024)
#define DRAM_OFFSET		0x100000
#define DRAM_SIZE		(160 * 1024)
#define SHIM_OFFSET		0x140000
#define SHIM_SIZE		0x100
#define MBOX_OFFSET		0x144000
#define MBOX_SIZE		0x1000
#define EXCEPT_OFFSET		0x800
#define EXCEPT_MAX_HDR_SIZE	0x400

/* DSP peripherals */
#define DMAC0_OFFSET		0x098000
#define DMAC1_OFFSET		0x09c000
#define DMAC2_OFFSET		0x094000
#define DMAC_SIZE		0x420
#define SSP0_OFFSET		0x0a0000
#define SSP1_OFFSET		0x0a1000
#define SSP2_OFFSET		0x0a2000
#define SSP3_OFFSET		0x0a4000
#define SSP4_OFFSET		0x0a5000
#define SSP5_OFFSET		0x0a6000
#define SSP_SIZE		0x100

#define BYT_STACK_DUMP_SIZE	32

#define BYT_PCI_BAR_SIZE	0x200000

#define BYT_PANIC_OFFSET(x)	(((x) & GENMASK_ULL(47, 32)) >> 32)

/*
 * Debug
 */

#define MBOX_DUMP_SIZE	0x30

/* BARs */
#define BYT_DSP_BAR		0
#define BYT_PCI_BAR		1
#define BYT_IMR_BAR		2

static const struct snd_sof_debugfs_map byt_debugfs[] = {
	{"dmac0", BYT_DSP_BAR, DMAC0_OFFSET, DMAC_SIZE,
	 SOF_DEBUGFS_ACCESS_ALWAYS},
	{"dmac1", BYT_DSP_BAR,  DMAC1_OFFSET, DMAC_SIZE,
	 SOF_DEBUGFS_ACCESS_ALWAYS},
	{"ssp0",  BYT_DSP_BAR, SSP0_OFFSET, SSP_SIZE,
	 SOF_DEBUGFS_ACCESS_ALWAYS},
	{"ssp1", BYT_DSP_BAR, SSP1_OFFSET, SSP_SIZE,
	 SOF_DEBUGFS_ACCESS_ALWAYS},
	{"ssp2", BYT_DSP_BAR, SSP2_OFFSET, SSP_SIZE,
	 SOF_DEBUGFS_ACCESS_ALWAYS},
	{"iram", BYT_DSP_BAR, IRAM_OFFSET, IRAM_SIZE,
	 SOF_DEBUGFS_ACCESS_D0_ONLY},
	{"dram", BYT_DSP_BAR, DRAM_OFFSET, DRAM_SIZE,
	 SOF_DEBUGFS_ACCESS_D0_ONLY},
	{"shim", BYT_DSP_BAR, SHIM_OFFSET, SHIM_SIZE,
	 SOF_DEBUGFS_ACCESS_ALWAYS},
};

static const struct snd_sof_debugfs_map cht_debugfs[] = {
	{"dmac0", BYT_DSP_BAR, DMAC0_OFFSET, DMAC_SIZE,
	 SOF_DEBUGFS_ACCESS_ALWAYS},
	{"dmac1", BYT_DSP_BAR,  DMAC1_OFFSET, DMAC_SIZE,
	 SOF_DEBUGFS_ACCESS_ALWAYS},
	{"dmac2", BYT_DSP_BAR,  DMAC2_OFFSET, DMAC_SIZE,
	 SOF_DEBUGFS_ACCESS_ALWAYS},
	{"ssp0",  BYT_DSP_BAR, SSP0_OFFSET, SSP_SIZE,
	 SOF_DEBUGFS_ACCESS_ALWAYS},
	{"ssp1", BYT_DSP_BAR, SSP1_OFFSET, SSP_SIZE,
	 SOF_DEBUGFS_ACCESS_ALWAYS},
	{"ssp2", BYT_DSP_BAR, SSP2_OFFSET, SSP_SIZE,
	 SOF_DEBUGFS_ACCESS_ALWAYS},
	{"ssp3", BYT_DSP_BAR, SSP3_OFFSET, SSP_SIZE,
	 SOF_DEBUGFS_ACCESS_ALWAYS},
	{"ssp4", BYT_DSP_BAR, SSP4_OFFSET, SSP_SIZE,
	 SOF_DEBUGFS_ACCESS_ALWAYS},
	{"ssp5", BYT_DSP_BAR, SSP5_OFFSET, SSP_SIZE,
	 SOF_DEBUGFS_ACCESS_ALWAYS},
	{"iram", BYT_DSP_BAR, IRAM_OFFSET, IRAM_SIZE,
	 SOF_DEBUGFS_ACCESS_D0_ONLY},
	{"dram", BYT_DSP_BAR, DRAM_OFFSET, DRAM_SIZE,
	 SOF_DEBUGFS_ACCESS_D0_ONLY},
	{"shim", BYT_DSP_BAR, SHIM_OFFSET, SHIM_SIZE,
	 SOF_DEBUGFS_ACCESS_ALWAYS},
};

static void byt_host_done(struct snd_sof_dev *sdev);
static void byt_dsp_done(struct snd_sof_dev *sdev);
static void byt_get_reply(struct snd_sof_dev *sdev);

/*
 * Debug
 */

static void byt_get_registers(struct snd_sof_dev *sdev,
			      struct sof_ipc_dsp_oops_xtensa *xoops,
			      struct sof_ipc_panic_info *panic_info,
			      u32 *stack, size_t stack_words)
{
	u32 offset = sdev->dsp_oops_offset;

	/* first read regsisters */
	sof_mailbox_read(sdev, offset, xoops, sizeof(*xoops));

	/* note: variable AR register array is not read */

	/* then get panic info */
	if (xoops->arch_hdr.totalsize > EXCEPT_MAX_HDR_SIZE) {
		dev_err(sdev->dev, "invalid header size 0x%x. FW oops is bogus\n",
			xoops->arch_hdr.totalsize);
		return;
	}
	offset += xoops->arch_hdr.totalsize;
	sof_mailbox_read(sdev, offset, panic_info, sizeof(*panic_info));

	/* then get the stack */
	offset += sizeof(*panic_info);
	sof_mailbox_read(sdev, offset, stack, stack_words * sizeof(u32));
}

static void byt_dump(struct snd_sof_dev *sdev, u32 flags)
{
	struct sof_ipc_dsp_oops_xtensa xoops;
	struct sof_ipc_panic_info panic_info;
	u32 stack[BYT_STACK_DUMP_SIZE];
	u32 status, panic;

	/* now try generic SOF status messages */
	status = snd_sof_dsp_read(sdev, BYT_DSP_BAR, SHIM_IPCD);
	panic = snd_sof_dsp_read(sdev, BYT_DSP_BAR, SHIM_IPCX);
	byt_get_registers(sdev, &xoops, &panic_info, stack,
			  BYT_STACK_DUMP_SIZE);
	snd_sof_get_status(sdev, status, panic, &xoops, &panic_info, stack,
			   BYT_STACK_DUMP_SIZE);
}

/*
 * IPC Doorbell IRQ handler and thread.
 */

static irqreturn_t byt_irq_handler(int irq, void *context)
{
	struct snd_sof_dev *sdev = context;
	u64 isr;
	int ret = IRQ_NONE;

	/* Interrupt arrived, check src */
	isr = snd_sof_dsp_read64(sdev, BYT_DSP_BAR, SHIM_ISRX);
	if (isr & (SHIM_ISRX_DONE | SHIM_ISRX_BUSY))
		ret = IRQ_WAKE_THREAD;

	return ret;
}

static irqreturn_t byt_irq_thread(int irq, void *context)
{
	struct snd_sof_dev *sdev = context;
	u64 ipcx, ipcd;
	u64 imrx;

	imrx = snd_sof_dsp_read64(sdev, BYT_DSP_BAR, SHIM_IMRX);
	ipcx = snd_sof_dsp_read64(sdev, BYT_DSP_BAR, SHIM_IPCX);

	/* reply message from DSP */
	if (ipcx & SHIM_BYT_IPCX_DONE &&
	    !(imrx & SHIM_IMRX_DONE)) {
		/* Mask Done interrupt before first */
		snd_sof_dsp_update_bits64_unlocked(sdev, BYT_DSP_BAR,
						   SHIM_IMRX,
						   SHIM_IMRX_DONE,
						   SHIM_IMRX_DONE);

		spin_lock_irq(&sdev->ipc_lock);

		/*
		 * handle immediate reply from DSP core. If the msg is
		 * found, set done bit in cmd_done which is called at the
		 * end of message processing function, else set it here
		 * because the done bit can't be set in cmd_done function
		 * which is triggered by msg
		 */
		byt_get_reply(sdev);
		snd_sof_ipc_reply(sdev, ipcx);

		byt_dsp_done(sdev);

		spin_unlock_irq(&sdev->ipc_lock);
	}

	/* new message from DSP */
	ipcd = snd_sof_dsp_read64(sdev, BYT_DSP_BAR, SHIM_IPCD);
	if (ipcd & SHIM_BYT_IPCD_BUSY &&
	    !(imrx & SHIM_IMRX_BUSY)) {
		/* Mask Busy interrupt before return */
		snd_sof_dsp_update_bits64_unlocked(sdev, BYT_DSP_BAR,
						   SHIM_IMRX,
						   SHIM_IMRX_BUSY,
						   SHIM_IMRX_BUSY);

		/* Handle messages from DSP Core */
		if ((ipcd & SOF_IPC_PANIC_MAGIC_MASK) == SOF_IPC_PANIC_MAGIC) {
			snd_sof_dsp_panic(sdev, BYT_PANIC_OFFSET(ipcd) +
					  MBOX_OFFSET);
		} else {
			snd_sof_ipc_msgs_rx(sdev);
		}

		byt_host_done(sdev);
	}

	return IRQ_HANDLED;
}

static int byt_send_msg(struct snd_sof_dev *sdev, struct snd_sof_ipc_msg *msg)
{
	/* send the message */
	sof_mailbox_write(sdev, sdev->host_box.offset, msg->msg_data,
			  msg->msg_size);
	snd_sof_dsp_write64(sdev, BYT_DSP_BAR, SHIM_IPCX, SHIM_BYT_IPCX_BUSY);

	return 0;
}

static void byt_get_reply(struct snd_sof_dev *sdev)
{
	struct snd_sof_ipc_msg *msg = sdev->msg;
	struct sof_ipc_reply reply;
	int ret = 0;

	/*
	 * Sometimes, there is unexpected reply ipc arriving. The reply
	 * ipc belongs to none of the ipcs sent from driver.
	 * In this case, the driver must ignore the ipc.
	 */
	if (!msg) {
		dev_warn(sdev->dev, "unexpected ipc interrupt raised!\n");
		return;
	}

	/* get reply */
	sof_mailbox_read(sdev, sdev->host_box.offset, &reply, sizeof(reply));

	if (reply.error < 0) {
		memcpy(msg->reply_data, &reply, sizeof(reply));
		ret = reply.error;
	} else {
		/* reply correct size ? */
		if (reply.hdr.size != msg->reply_size) {
			dev_err(sdev->dev, "error: reply expected %zu got %u bytes\n",
				msg->reply_size, reply.hdr.size);
			ret = -EINVAL;
		}

		/* read the message */
		if (msg->reply_size > 0)
			sof_mailbox_read(sdev, sdev->host_box.offset,
					 msg->reply_data, msg->reply_size);
	}

	msg->reply_error = ret;
}

static int byt_get_mailbox_offset(struct snd_sof_dev *sdev)
{
	return MBOX_OFFSET;
}

static int byt_get_window_offset(struct snd_sof_dev *sdev, u32 id)
{
	return MBOX_OFFSET;
}

static void byt_host_done(struct snd_sof_dev *sdev)
{
	/* clear BUSY bit and set DONE bit - accept new messages */
	snd_sof_dsp_update_bits64_unlocked(sdev, BYT_DSP_BAR, SHIM_IPCD,
					   SHIM_BYT_IPCD_BUSY |
					   SHIM_BYT_IPCD_DONE,
					   SHIM_BYT_IPCD_DONE);

	/* unmask busy interrupt */
	snd_sof_dsp_update_bits64_unlocked(sdev, BYT_DSP_BAR, SHIM_IMRX,
					   SHIM_IMRX_BUSY, 0);
}

static void byt_dsp_done(struct snd_sof_dev *sdev)
{
	/* clear DONE bit - tell DSP we have completed */
	snd_sof_dsp_update_bits64_unlocked(sdev, BYT_DSP_BAR, SHIM_IPCX,
					   SHIM_BYT_IPCX_DONE, 0);

	/* unmask Done interrupt */
	snd_sof_dsp_update_bits64_unlocked(sdev, BYT_DSP_BAR, SHIM_IMRX,
					   SHIM_IMRX_DONE, 0);
}

/*
 * DSP control.
 */

static int byt_run(struct snd_sof_dev *sdev)
{
	int tries = 10;

	/* release stall and wait to unstall */
	snd_sof_dsp_update_bits64(sdev, BYT_DSP_BAR, SHIM_CSR,
				  SHIM_BYT_CSR_STALL, 0x0);
	while (tries--) {
		if (!(snd_sof_dsp_read64(sdev, BYT_DSP_BAR, SHIM_CSR) &
		      SHIM_BYT_CSR_PWAITMODE))
			break;
		msleep(100);
	}
	if (tries < 0) {
		dev_err(sdev->dev, "error:  unable to run DSP firmware\n");
		byt_dump(sdev, SOF_DBG_REGS | SOF_DBG_MBOX);
		return -ENODEV;
	}

	/* return init core mask */
	return 1;
}

static int byt_reset(struct snd_sof_dev *sdev)
{
	/* put DSP into reset, set reset vector and stall */
	snd_sof_dsp_update_bits64(sdev, BYT_DSP_BAR, SHIM_CSR,
				  SHIM_BYT_CSR_RST | SHIM_BYT_CSR_VECTOR_SEL |
				  SHIM_BYT_CSR_STALL,
				  SHIM_BYT_CSR_RST | SHIM_BYT_CSR_VECTOR_SEL |
				  SHIM_BYT_CSR_STALL);

	usleep_range(10, 15);

	/* take DSP out of reset and keep stalled for FW loading */
	snd_sof_dsp_update_bits64(sdev, BYT_DSP_BAR, SHIM_CSR,
				  SHIM_BYT_CSR_RST, 0);

	return 0;
}

/* Baytrail DAIs */
static struct snd_soc_dai_driver byt_dai[] = {
{
	.name = "ssp0-port",
},
{
	.name = "ssp1-port",
},
{
	.name = "ssp2-port",
},
{
	.name = "ssp3-port",
},
{
	.name = "ssp4-port",
},
{
	.name = "ssp5-port",
},
};

/*
 * Probe and remove.
 */

#if IS_ENABLED(CONFIG_SND_SOC_SOF_MERRIFIELD)

static int tangier_pci_probe(struct snd_sof_dev *sdev)
{
	struct snd_sof_pdata *pdata = sdev->pdata;
	const struct sof_dev_desc *desc = pdata->desc;
	struct pci_dev *pci = to_pci_dev(sdev->dev);
	u32 base, size;
	int ret;

	/* DSP DMA can only access low 31 bits of host memory */
	ret = dma_coerce_mask_and_coherent(&pci->dev, DMA_BIT_MASK(31));
	if (ret < 0) {
		dev_err(sdev->dev, "error: failed to set DMA mask %d\n", ret);
		return ret;
	}

	/* LPE base */
	base = pci_resource_start(pci, desc->resindex_lpe_base) - IRAM_OFFSET;
	size = BYT_PCI_BAR_SIZE;

	dev_dbg(sdev->dev, "LPE PHY base at 0x%x size 0x%x", base, size);
	sdev->bar[BYT_DSP_BAR] = devm_ioremap(sdev->dev, base, size);
	if (!sdev->bar[BYT_DSP_BAR]) {
		dev_err(sdev->dev, "error: failed to ioremap LPE base 0x%x size 0x%x\n",
			base, size);
		return -ENODEV;
	}
	dev_dbg(sdev->dev, "LPE VADDR %p\n", sdev->bar[BYT_DSP_BAR]);

	/* IMR base - optional */
	if (desc->resindex_imr_base == -1)
		goto irq;

	base = pci_resource_start(pci, desc->resindex_imr_base);
	size = pci_resource_len(pci, desc->resindex_imr_base);

	/* some BIOSes don't map IMR */
	if (base == 0x55aa55aa || base == 0x0) {
		dev_info(sdev->dev, "IMR not set by BIOS. Ignoring\n");
		goto irq;
	}

	dev_dbg(sdev->dev, "IMR base at 0x%x size 0x%x", base, size);
	sdev->bar[BYT_IMR_BAR] = devm_ioremap(sdev->dev, base, size);
	if (!sdev->bar[BYT_IMR_BAR]) {
		dev_err(sdev->dev, "error: failed to ioremap IMR base 0x%x size 0x%x\n",
			base, size);
		return -ENODEV;
	}
	dev_dbg(sdev->dev, "IMR VADDR %p\n", sdev->bar[BYT_IMR_BAR]);

irq:
	/* register our IRQ */
	sdev->ipc_irq = pci->irq;
	dev_dbg(sdev->dev, "using IRQ %d\n", sdev->ipc_irq);
	ret = devm_request_threaded_irq(sdev->dev, sdev->ipc_irq,
					byt_irq_handler, byt_irq_thread,
					0, "AudioDSP", sdev);
	if (ret < 0) {
		dev_err(sdev->dev, "error: failed to register IRQ %d\n",
			sdev->ipc_irq);
		return ret;
	}

	/* enable Interrupt from both sides */
	snd_sof_dsp_update_bits64(sdev, BYT_DSP_BAR, SHIM_IMRX, 0x3, 0x0);
	snd_sof_dsp_update_bits64(sdev, BYT_DSP_BAR, SHIM_IMRD, 0x3, 0x0);

	/* set default mailbox offset for FW ready message */
	sdev->dsp_box.offset = MBOX_OFFSET;

	return ret;
}

const struct snd_sof_dsp_ops sof_tng_ops = {
	/* device init */
	.probe		= tangier_pci_probe,

	/* DSP core boot / reset */
	.run		= byt_run,
	.reset		= byt_reset,

	/* Register IO */
	.write		= sof_io_write,
	.read		= sof_io_read,
	.write64	= sof_io_write64,
	.read64		= sof_io_read64,

	/* Block IO */
	.block_read	= sof_block_read,
	.block_write	= sof_block_write,

	/* doorbell */
	.irq_handler	= byt_irq_handler,
	.irq_thread	= byt_irq_thread,

	/* ipc */
	.send_msg	= byt_send_msg,
	.fw_ready	= sof_fw_ready,
	.get_mailbox_offset = byt_get_mailbox_offset,
	.get_window_offset = byt_get_window_offset,

	.ipc_msg_data	= intel_ipc_msg_data,
	.ipc_pcm_params	= intel_ipc_pcm_params,

	/* debug */
	.debug_map	= byt_debugfs,
	.debug_map_count	= ARRAY_SIZE(byt_debugfs),
	.dbg_dump	= byt_dump,

	/* stream callbacks */
	.pcm_open	= intel_pcm_open,
	.pcm_close	= intel_pcm_close,

	/* module loading */
	.load_module	= snd_sof_parse_module_memcpy,

	/*Firmware loading */
	.load_firmware	= snd_sof_load_firmware_memcpy,

	/* DAI drivers */
	.drv = byt_dai,
	.num_drv = 3, /* we have only 3 SSPs on byt*/
};
EXPORT_SYMBOL(sof_tng_ops);

const struct sof_intel_dsp_desc tng_chip_info = {
	.cores_num = 1,
	.cores_mask = 1,
};
EXPORT_SYMBOL(tng_chip_info);

#endif /* CONFIG_SND_SOC_SOF_MERRIFIELD */

#if IS_ENABLED(CONFIG_SND_SOC_SOF_BAYTRAIL)

static int byt_acpi_probe(struct snd_sof_dev *sdev)
{
	struct snd_sof_pdata *pdata = sdev->pdata;
	const struct sof_dev_desc *desc = pdata->desc;
	struct platform_device *pdev =
		container_of(sdev->dev, struct platform_device, dev);
	struct resource *mmio;
	u32 base, size;
	int ret;

	/* DSP DMA can only access low 31 bits of host memory */
	ret = dma_coerce_mask_and_coherent(sdev->dev, DMA_BIT_MASK(31));
	if (ret < 0) {
		dev_err(sdev->dev, "error: failed to set DMA mask %d\n", ret);
		return ret;
	}

	/* LPE base */
	mmio = platform_get_resource(pdev, IORESOURCE_MEM,
				     desc->resindex_lpe_base);
	if (mmio) {
		base = mmio->start;
		size = resource_size(mmio);
	} else {
		dev_err(sdev->dev, "error: failed to get LPE base at idx %d\n",
			desc->resindex_lpe_base);
		return -EINVAL;
	}

	dev_dbg(sdev->dev, "LPE PHY base at 0x%x size 0x%x", base, size);
	sdev->bar[BYT_DSP_BAR] = devm_ioremap(sdev->dev, base, size);
	if (!sdev->bar[BYT_DSP_BAR]) {
		dev_err(sdev->dev, "error: failed to ioremap LPE base 0x%x size 0x%x\n",
			base, size);
		return -ENODEV;
	}
	dev_dbg(sdev->dev, "LPE VADDR %p\n", sdev->bar[BYT_DSP_BAR]);

	/* TODO: add offsets */
	sdev->mmio_bar = BYT_DSP_BAR;
	sdev->mailbox_bar = BYT_DSP_BAR;

	/* IMR base - optional */
	if (desc->resindex_imr_base == -1)
		goto irq;

	mmio = platform_get_resource(pdev, IORESOURCE_MEM,
				     desc->resindex_imr_base);
	if (mmio) {
		base = mmio->start;
		size = resource_size(mmio);
	} else {
		dev_err(sdev->dev, "error: failed to get IMR base at idx %d\n",
			desc->resindex_imr_base);
		return -ENODEV;
	}

	/* some BIOSes don't map IMR */
	if (base == 0x55aa55aa || base == 0x0) {
		dev_info(sdev->dev, "IMR not set by BIOS. Ignoring\n");
		goto irq;
	}

	dev_dbg(sdev->dev, "IMR base at 0x%x size 0x%x", base, size);
	sdev->bar[BYT_IMR_BAR] = devm_ioremap(sdev->dev, base, size);
	if (!sdev->bar[BYT_IMR_BAR]) {
		dev_err(sdev->dev, "error: failed to ioremap IMR base 0x%x size 0x%x\n",
			base, size);
		return -ENODEV;
	}
	dev_dbg(sdev->dev, "IMR VADDR %p\n", sdev->bar[BYT_IMR_BAR]);

irq:
	/* register our IRQ */
	sdev->ipc_irq = platform_get_irq(pdev, desc->irqindex_host_ipc);
	if (sdev->ipc_irq < 0)
		return sdev->ipc_irq;

	dev_dbg(sdev->dev, "using IRQ %d\n", sdev->ipc_irq);
	ret = devm_request_threaded_irq(sdev->dev, sdev->ipc_irq,
					byt_irq_handler, byt_irq_thread,
					IRQF_SHARED, "AudioDSP", sdev);
	if (ret < 0) {
		dev_err(sdev->dev, "error: failed to register IRQ %d\n",
			sdev->ipc_irq);
		return ret;
	}

	/* enable Interrupt from both sides */
	snd_sof_dsp_update_bits64(sdev, BYT_DSP_BAR, SHIM_IMRX, 0x3, 0x0);
	snd_sof_dsp_update_bits64(sdev, BYT_DSP_BAR, SHIM_IMRD, 0x3, 0x0);

	/* set default mailbox offset for FW ready message */
	sdev->dsp_box.offset = MBOX_OFFSET;

	return ret;
}

/* baytrail ops */
const struct snd_sof_dsp_ops sof_byt_ops = {
	/* device init */
	.probe		= byt_acpi_probe,

	/* DSP core boot / reset */
	.run		= byt_run,
	.reset		= byt_reset,

	/* Register IO */
	.write		= sof_io_write,
	.read		= sof_io_read,
	.write64	= sof_io_write64,
	.read64		= sof_io_read64,

	/* Block IO */
	.block_read	= sof_block_read,
	.block_write	= sof_block_write,

	/* doorbell */
	.irq_handler	= byt_irq_handler,
	.irq_thread	= byt_irq_thread,

	/* ipc */
	.send_msg	= byt_send_msg,
	.fw_ready	= sof_fw_ready,
	.get_mailbox_offset = byt_get_mailbox_offset,
	.get_window_offset = byt_get_window_offset,

	.ipc_msg_data	= intel_ipc_msg_data,
	.ipc_pcm_params	= intel_ipc_pcm_params,

	/* debug */
	.debug_map	= byt_debugfs,
	.debug_map_count	= ARRAY_SIZE(byt_debugfs),
	.dbg_dump	= byt_dump,

	/* stream callbacks */
	.pcm_open	= intel_pcm_open,
	.pcm_close	= intel_pcm_close,

	/* module loading */
	.load_module	= snd_sof_parse_module_memcpy,

	/*Firmware loading */
	.load_firmware	= snd_sof_load_firmware_memcpy,

	/* DAI drivers */
	.drv = byt_dai,
	.num_drv = 3, /* we have only 3 SSPs on byt*/
};
EXPORT_SYMBOL(sof_byt_ops);

const struct sof_intel_dsp_desc byt_chip_info = {
	.cores_num = 1,
	.cores_mask = 1,
};
EXPORT_SYMBOL(byt_chip_info);

/* cherrytrail and braswell ops */
const struct snd_sof_dsp_ops sof_cht_ops = {
	/* device init */
	.probe		= byt_acpi_probe,

	/* DSP core boot / reset */
	.run		= byt_run,
	.reset		= byt_reset,

	/* Register IO */
	.write		= sof_io_write,
	.read		= sof_io_read,
	.write64	= sof_io_write64,
	.read64		= sof_io_read64,

	/* Block IO */
	.block_read	= sof_block_read,
	.block_write	= sof_block_write,

	/* doorbell */
	.irq_handler	= byt_irq_handler,
	.irq_thread	= byt_irq_thread,

	/* ipc */
	.send_msg	= byt_send_msg,
	.fw_ready	= sof_fw_ready,
	.get_mailbox_offset = byt_get_mailbox_offset,
	.get_window_offset = byt_get_window_offset,

	.ipc_msg_data	= intel_ipc_msg_data,
	.ipc_pcm_params	= intel_ipc_pcm_params,

	/* debug */
	.debug_map	= cht_debugfs,
	.debug_map_count	= ARRAY_SIZE(cht_debugfs),
	.dbg_dump	= byt_dump,

	/* stream callbacks */
	.pcm_open	= intel_pcm_open,
	.pcm_close	= intel_pcm_close,

	/* module loading */
	.load_module	= snd_sof_parse_module_memcpy,

	/*Firmware loading */
	.load_firmware	= snd_sof_load_firmware_memcpy,

	/* DAI drivers */
	.drv = byt_dai,
	/* all 6 SSPs may be available for cherrytrail */
	.num_drv = ARRAY_SIZE(byt_dai),
};
EXPORT_SYMBOL(sof_cht_ops);

const struct sof_intel_dsp_desc cht_chip_info = {
	.cores_num = 1,
	.cores_mask = 1,
};
EXPORT_SYMBOL(cht_chip_info);

#endif /* CONFIG_SND_SOC_SOF_BAYTRAIL */

MODULE_LICENSE("Dual BSD/GPL");
