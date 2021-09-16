// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//

/*
 * Hardware interface for audio DSP on Broadwell
 */

#include <linux/module.h>
#include <sound/sof.h>
#include <sound/sof/xtensa.h>
#include <sound/soc-acpi.h>
#include <sound/soc-acpi-intel-match.h>
#include <sound/intel-dsp-config.h>
#include "../ops.h"
#include "shim.h"
#include "../sof-acpi-dev.h"
#include "../sof-audio.h"

/* BARs */
#define BDW_DSP_BAR 0
#define BDW_PCI_BAR 1

/*
 * Debug
 */

/* DSP memories for BDW */
#define IRAM_OFFSET     0xA0000
#define BDW_IRAM_SIZE       (10 * 32 * 1024)
#define DRAM_OFFSET     0x00000
#define BDW_DRAM_SIZE       (20 * 32 * 1024)
#define SHIM_OFFSET     0xFB000
#define SHIM_SIZE       0x100
#define MBOX_OFFSET     0x9E000
#define MBOX_SIZE       0x1000
#define MBOX_DUMP_SIZE 0x30
#define EXCEPT_OFFSET	0x800
#define EXCEPT_MAX_HDR_SIZE	0x400

/* DSP peripherals */
#define DMAC0_OFFSET    0xFE000
#define DMAC1_OFFSET    0xFF000
#define DMAC_SIZE       0x420
#define SSP0_OFFSET     0xFC000
#define SSP1_OFFSET     0xFD000
#define SSP_SIZE	0x100

#define BDW_STACK_DUMP_SIZE	32

#define BDW_PANIC_OFFSET(x)	((x) & 0xFFFF)

static const struct snd_sof_debugfs_map bdw_debugfs[] = {
	{"dmac0", BDW_DSP_BAR, DMAC0_OFFSET, DMAC_SIZE,
	 SOF_DEBUGFS_ACCESS_ALWAYS},
	{"dmac1", BDW_DSP_BAR, DMAC1_OFFSET, DMAC_SIZE,
	 SOF_DEBUGFS_ACCESS_ALWAYS},
	{"ssp0", BDW_DSP_BAR, SSP0_OFFSET, SSP_SIZE,
	 SOF_DEBUGFS_ACCESS_ALWAYS},
	{"ssp1", BDW_DSP_BAR, SSP1_OFFSET, SSP_SIZE,
	 SOF_DEBUGFS_ACCESS_ALWAYS},
	{"iram", BDW_DSP_BAR, IRAM_OFFSET, BDW_IRAM_SIZE,
	 SOF_DEBUGFS_ACCESS_D0_ONLY},
	{"dram", BDW_DSP_BAR, DRAM_OFFSET, BDW_DRAM_SIZE,
	 SOF_DEBUGFS_ACCESS_D0_ONLY},
	{"shim", BDW_DSP_BAR, SHIM_OFFSET, SHIM_SIZE,
	 SOF_DEBUGFS_ACCESS_ALWAYS},
};

static void bdw_host_done(struct snd_sof_dev *sdev);
static void bdw_dsp_done(struct snd_sof_dev *sdev);
static void bdw_get_reply(struct snd_sof_dev *sdev);

/*
 * DSP Control.
 */

static int bdw_run(struct snd_sof_dev *sdev)
{
	/* set opportunistic mode on engine 0,1 for all channels */
	snd_sof_dsp_update_bits(sdev, BDW_DSP_BAR, SHIM_HMDC,
				SHIM_HMDC_HDDA_E0_ALLCH |
				SHIM_HMDC_HDDA_E1_ALLCH, 0);

	/* set DSP to RUN */
	snd_sof_dsp_update_bits_unlocked(sdev, BDW_DSP_BAR, SHIM_CSR,
					 SHIM_CSR_STALL, 0x0);

	/* return init core mask */
	return 1;
}

static int bdw_reset(struct snd_sof_dev *sdev)
{
	/* put DSP into reset and stall */
	snd_sof_dsp_update_bits_unlocked(sdev, BDW_DSP_BAR, SHIM_CSR,
					 SHIM_CSR_RST | SHIM_CSR_STALL,
					 SHIM_CSR_RST | SHIM_CSR_STALL);

	/* keep in reset for 10ms */
	mdelay(10);

	/* take DSP out of reset and keep stalled for FW loading */
	snd_sof_dsp_update_bits_unlocked(sdev, BDW_DSP_BAR, SHIM_CSR,
					 SHIM_CSR_RST | SHIM_CSR_STALL,
					 SHIM_CSR_STALL);

	return 0;
}

static int bdw_set_dsp_D0(struct snd_sof_dev *sdev)
{
	int tries = 10;
	u32 reg;

	/* Disable core clock gating (VDRTCTL2.DCLCGE = 0) */
	snd_sof_dsp_update_bits_unlocked(sdev, BDW_PCI_BAR, PCI_VDRTCTL2,
					 PCI_VDRTCL2_DCLCGE |
					 PCI_VDRTCL2_DTCGE, 0);

	/* Disable D3PG (VDRTCTL0.D3PGD = 1) */
	snd_sof_dsp_update_bits_unlocked(sdev, BDW_PCI_BAR, PCI_VDRTCTL0,
					 PCI_VDRTCL0_D3PGD, PCI_VDRTCL0_D3PGD);

	/* Set D0 state */
	snd_sof_dsp_update_bits_unlocked(sdev, BDW_PCI_BAR, PCI_PMCS,
					 PCI_PMCS_PS_MASK, 0);

	/* check that ADSP shim is enabled */
	while (tries--) {
		reg = readl(sdev->bar[BDW_PCI_BAR] + PCI_PMCS)
			& PCI_PMCS_PS_MASK;
		if (reg == 0)
			goto finish;

		msleep(20);
	}

	return -ENODEV;

finish:
	/*
	 * select SSP1 19.2MHz base clock, SSP clock 0,
	 * turn off Low Power Clock
	 */
	snd_sof_dsp_update_bits_unlocked(sdev, BDW_DSP_BAR, SHIM_CSR,
					 SHIM_CSR_S1IOCS | SHIM_CSR_SBCS1 |
					 SHIM_CSR_LPCS, 0x0);

	/* stall DSP core, set clk to 192/96Mhz */
	snd_sof_dsp_update_bits_unlocked(sdev, BDW_DSP_BAR,
					 SHIM_CSR, SHIM_CSR_STALL |
					 SHIM_CSR_DCS_MASK,
					 SHIM_CSR_STALL |
					 SHIM_CSR_DCS(4));

	/* Set 24MHz MCLK, prevent local clock gating, enable SSP0 clock */
	snd_sof_dsp_update_bits_unlocked(sdev, BDW_DSP_BAR, SHIM_CLKCTL,
					 SHIM_CLKCTL_MASK |
					 SHIM_CLKCTL_DCPLCG |
					 SHIM_CLKCTL_SCOE0,
					 SHIM_CLKCTL_MASK |
					 SHIM_CLKCTL_DCPLCG |
					 SHIM_CLKCTL_SCOE0);

	/* Stall and reset core, set CSR */
	bdw_reset(sdev);

	/* Enable core clock gating (VDRTCTL2.DCLCGE = 1), delay 50 us */
	snd_sof_dsp_update_bits_unlocked(sdev, BDW_PCI_BAR, PCI_VDRTCTL2,
					 PCI_VDRTCL2_DCLCGE |
					 PCI_VDRTCL2_DTCGE,
					 PCI_VDRTCL2_DCLCGE |
					 PCI_VDRTCL2_DTCGE);

	usleep_range(50, 55);

	/* switch on audio PLL */
	snd_sof_dsp_update_bits_unlocked(sdev, BDW_PCI_BAR, PCI_VDRTCTL2,
					 PCI_VDRTCL2_APLLSE_MASK, 0);

	/*
	 * set default power gating control, enable power gating control for
	 * all blocks. that is, can't be accessed, please enable each block
	 * before accessing.
	 */
	snd_sof_dsp_update_bits_unlocked(sdev, BDW_PCI_BAR, PCI_VDRTCTL0,
					 0xfffffffC, 0x0);

	/* disable DMA finish function for SSP0 & SSP1 */
	snd_sof_dsp_update_bits_unlocked(sdev, BDW_DSP_BAR,  SHIM_CSR2,
					 SHIM_CSR2_SDFD_SSP1,
					 SHIM_CSR2_SDFD_SSP1);

	/* set on-demond mode on engine 0,1 for all channels */
	snd_sof_dsp_update_bits(sdev, BDW_DSP_BAR, SHIM_HMDC,
				SHIM_HMDC_HDDA_E0_ALLCH |
				SHIM_HMDC_HDDA_E1_ALLCH,
				SHIM_HMDC_HDDA_E0_ALLCH |
				SHIM_HMDC_HDDA_E1_ALLCH);

	/* Enable Interrupt from both sides */
	snd_sof_dsp_update_bits(sdev, BDW_DSP_BAR, SHIM_IMRX,
				(SHIM_IMRX_BUSY | SHIM_IMRX_DONE), 0x0);
	snd_sof_dsp_update_bits(sdev, BDW_DSP_BAR, SHIM_IMRD,
				(SHIM_IMRD_DONE | SHIM_IMRD_BUSY |
				SHIM_IMRD_SSP0 | SHIM_IMRD_DMAC), 0x0);

	/* clear IPC registers */
	snd_sof_dsp_write(sdev, BDW_DSP_BAR, SHIM_IPCX, 0x0);
	snd_sof_dsp_write(sdev, BDW_DSP_BAR, SHIM_IPCD, 0x0);
	snd_sof_dsp_write(sdev, BDW_DSP_BAR, 0x80, 0x6);
	snd_sof_dsp_write(sdev, BDW_DSP_BAR, 0xe0, 0x300a);

	return 0;
}

static void bdw_get_registers(struct snd_sof_dev *sdev,
			      struct sof_ipc_dsp_oops_xtensa *xoops,
			      struct sof_ipc_panic_info *panic_info,
			      u32 *stack, size_t stack_words)
{
	u32 offset = sdev->dsp_oops_offset;

	/* first read registers */
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

static void bdw_dump(struct snd_sof_dev *sdev, u32 flags)
{
	struct sof_ipc_dsp_oops_xtensa xoops;
	struct sof_ipc_panic_info panic_info;
	u32 stack[BDW_STACK_DUMP_SIZE];
	u32 status, panic, imrx, imrd;

	/* now try generic SOF status messages */
	status = snd_sof_dsp_read(sdev, BDW_DSP_BAR, SHIM_IPCD);
	panic = snd_sof_dsp_read(sdev, BDW_DSP_BAR, SHIM_IPCX);
	bdw_get_registers(sdev, &xoops, &panic_info, stack,
			  BDW_STACK_DUMP_SIZE);
	snd_sof_get_status(sdev, status, panic, &xoops, &panic_info, stack,
			   BDW_STACK_DUMP_SIZE);

	/* provide some context for firmware debug */
	imrx = snd_sof_dsp_read(sdev, BDW_DSP_BAR, SHIM_IMRX);
	imrd = snd_sof_dsp_read(sdev, BDW_DSP_BAR, SHIM_IMRD);
	dev_err(sdev->dev,
		"error: ipc host -> DSP: pending %s complete %s raw 0x%8.8x\n",
		(panic & SHIM_IPCX_BUSY) ? "yes" : "no",
		(panic & SHIM_IPCX_DONE) ? "yes" : "no", panic);
	dev_err(sdev->dev,
		"error: mask host: pending %s complete %s raw 0x%8.8x\n",
		(imrx & SHIM_IMRX_BUSY) ? "yes" : "no",
		(imrx & SHIM_IMRX_DONE) ? "yes" : "no", imrx);
	dev_err(sdev->dev,
		"error: ipc DSP -> host: pending %s complete %s raw 0x%8.8x\n",
		(status & SHIM_IPCD_BUSY) ? "yes" : "no",
		(status & SHIM_IPCD_DONE) ? "yes" : "no", status);
	dev_err(sdev->dev,
		"error: mask DSP: pending %s complete %s raw 0x%8.8x\n",
		(imrd & SHIM_IMRD_BUSY) ? "yes" : "no",
		(imrd & SHIM_IMRD_DONE) ? "yes" : "no", imrd);
}

/*
 * IPC Doorbell IRQ handler and thread.
 */

static irqreturn_t bdw_irq_handler(int irq, void *context)
{
	struct snd_sof_dev *sdev = context;
	u32 isr;
	int ret = IRQ_NONE;

	/* Interrupt arrived, check src */
	isr = snd_sof_dsp_read(sdev, BDW_DSP_BAR, SHIM_ISRX);
	if (isr & (SHIM_ISRX_DONE | SHIM_ISRX_BUSY))
		ret = IRQ_WAKE_THREAD;

	return ret;
}

static irqreturn_t bdw_irq_thread(int irq, void *context)
{
	struct snd_sof_dev *sdev = context;
	u32 ipcx, ipcd, imrx;

	imrx = snd_sof_dsp_read64(sdev, BDW_DSP_BAR, SHIM_IMRX);
	ipcx = snd_sof_dsp_read(sdev, BDW_DSP_BAR, SHIM_IPCX);

	/* reply message from DSP */
	if (ipcx & SHIM_IPCX_DONE &&
	    !(imrx & SHIM_IMRX_DONE)) {
		/* Mask Done interrupt before return */
		snd_sof_dsp_update_bits_unlocked(sdev, BDW_DSP_BAR,
						 SHIM_IMRX, SHIM_IMRX_DONE,
						 SHIM_IMRX_DONE);

		spin_lock_irq(&sdev->ipc_lock);

		/*
		 * handle immediate reply from DSP core. If the msg is
		 * found, set done bit in cmd_done which is called at the
		 * end of message processing function, else set it here
		 * because the done bit can't be set in cmd_done function
		 * which is triggered by msg
		 */
		bdw_get_reply(sdev);
		snd_sof_ipc_reply(sdev, ipcx);

		bdw_dsp_done(sdev);

		spin_unlock_irq(&sdev->ipc_lock);
	}

	ipcd = snd_sof_dsp_read(sdev, BDW_DSP_BAR, SHIM_IPCD);

	/* new message from DSP */
	if (ipcd & SHIM_IPCD_BUSY &&
	    !(imrx & SHIM_IMRX_BUSY)) {
		/* Mask Busy interrupt before return */
		snd_sof_dsp_update_bits_unlocked(sdev, BDW_DSP_BAR,
						 SHIM_IMRX, SHIM_IMRX_BUSY,
						 SHIM_IMRX_BUSY);

		/* Handle messages from DSP Core */
		if ((ipcd & SOF_IPC_PANIC_MAGIC_MASK) == SOF_IPC_PANIC_MAGIC) {
			snd_sof_dsp_panic(sdev, BDW_PANIC_OFFSET(ipcx) +
					  MBOX_OFFSET);
		} else {
			snd_sof_ipc_msgs_rx(sdev);
		}

		bdw_host_done(sdev);
	}

	return IRQ_HANDLED;
}

/*
 * IPC Mailbox IO
 */

static int bdw_send_msg(struct snd_sof_dev *sdev, struct snd_sof_ipc_msg *msg)
{
	/* send the message */
	sof_mailbox_write(sdev, sdev->host_box.offset, msg->msg_data,
			  msg->msg_size);
	snd_sof_dsp_write(sdev, BDW_DSP_BAR, SHIM_IPCX, SHIM_IPCX_BUSY);

	return 0;
}

static void bdw_get_reply(struct snd_sof_dev *sdev)
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

static int bdw_get_mailbox_offset(struct snd_sof_dev *sdev)
{
	return MBOX_OFFSET;
}

static int bdw_get_window_offset(struct snd_sof_dev *sdev, u32 id)
{
	return MBOX_OFFSET;
}

static void bdw_host_done(struct snd_sof_dev *sdev)
{
	/* clear BUSY bit and set DONE bit - accept new messages */
	snd_sof_dsp_update_bits_unlocked(sdev, BDW_DSP_BAR, SHIM_IPCD,
					 SHIM_IPCD_BUSY | SHIM_IPCD_DONE,
					 SHIM_IPCD_DONE);

	/* unmask busy interrupt */
	snd_sof_dsp_update_bits_unlocked(sdev, BDW_DSP_BAR, SHIM_IMRX,
					 SHIM_IMRX_BUSY, 0);
}

static void bdw_dsp_done(struct snd_sof_dev *sdev)
{
	/* clear DONE bit - tell DSP we have completed */
	snd_sof_dsp_update_bits_unlocked(sdev, BDW_DSP_BAR, SHIM_IPCX,
					 SHIM_IPCX_DONE, 0);

	/* unmask Done interrupt */
	snd_sof_dsp_update_bits_unlocked(sdev, BDW_DSP_BAR, SHIM_IMRX,
					 SHIM_IMRX_DONE, 0);
}

/*
 * Probe and remove.
 */
static int bdw_probe(struct snd_sof_dev *sdev)
{
	struct snd_sof_pdata *pdata = sdev->pdata;
	const struct sof_dev_desc *desc = pdata->desc;
	struct platform_device *pdev =
		container_of(sdev->dev, struct platform_device, dev);
	struct resource *mmio;
	u32 base, size;
	int ret;

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
	sdev->bar[BDW_DSP_BAR] = devm_ioremap(sdev->dev, base, size);
	if (!sdev->bar[BDW_DSP_BAR]) {
		dev_err(sdev->dev,
			"error: failed to ioremap LPE base 0x%x size 0x%x\n",
			base, size);
		return -ENODEV;
	}
	dev_dbg(sdev->dev, "LPE VADDR %p\n", sdev->bar[BDW_DSP_BAR]);

	/* TODO: add offsets */
	sdev->mmio_bar = BDW_DSP_BAR;
	sdev->mailbox_bar = BDW_DSP_BAR;
	sdev->dsp_oops_offset = MBOX_OFFSET;

	/* PCI base */
	mmio = platform_get_resource(pdev, IORESOURCE_MEM,
				     desc->resindex_pcicfg_base);
	if (mmio) {
		base = mmio->start;
		size = resource_size(mmio);
	} else {
		dev_err(sdev->dev, "error: failed to get PCI base at idx %d\n",
			desc->resindex_pcicfg_base);
		return -ENODEV;
	}

	dev_dbg(sdev->dev, "PCI base at 0x%x size 0x%x", base, size);
	sdev->bar[BDW_PCI_BAR] = devm_ioremap(sdev->dev, base, size);
	if (!sdev->bar[BDW_PCI_BAR]) {
		dev_err(sdev->dev,
			"error: failed to ioremap PCI base 0x%x size 0x%x\n",
			base, size);
		return -ENODEV;
	}
	dev_dbg(sdev->dev, "PCI VADDR %p\n", sdev->bar[BDW_PCI_BAR]);

	/* register our IRQ */
	sdev->ipc_irq = platform_get_irq(pdev, desc->irqindex_host_ipc);
	if (sdev->ipc_irq < 0)
		return sdev->ipc_irq;

	dev_dbg(sdev->dev, "using IRQ %d\n", sdev->ipc_irq);
	ret = devm_request_threaded_irq(sdev->dev, sdev->ipc_irq,
					bdw_irq_handler, bdw_irq_thread,
					IRQF_SHARED, "AudioDSP", sdev);
	if (ret < 0) {
		dev_err(sdev->dev, "error: failed to register IRQ %d\n",
			sdev->ipc_irq);
		return ret;
	}

	/* enable the DSP SHIM */
	ret = bdw_set_dsp_D0(sdev);
	if (ret < 0) {
		dev_err(sdev->dev, "error: failed to set DSP D0\n");
		return ret;
	}

	/* DSP DMA can only access low 31 bits of host memory */
	ret = dma_coerce_mask_and_coherent(sdev->dev, DMA_BIT_MASK(31));
	if (ret < 0) {
		dev_err(sdev->dev, "error: failed to set DMA mask %d\n", ret);
		return ret;
	}

	/* set default mailbox offset for FW ready message */
	sdev->dsp_box.offset = MBOX_OFFSET;

	return ret;
}

static void bdw_machine_select(struct snd_sof_dev *sdev)
{
	struct snd_sof_pdata *sof_pdata = sdev->pdata;
	const struct sof_dev_desc *desc = sof_pdata->desc;
	struct snd_soc_acpi_mach *mach;

	mach = snd_soc_acpi_find_machine(desc->machines);
	if (!mach) {
		dev_warn(sdev->dev, "warning: No matching ASoC machine driver found\n");
		return;
	}

	sof_pdata->tplg_filename = mach->sof_tplg_filename;
	mach->mach_params.acpi_ipc_irq_index = desc->irqindex_host_ipc;
	sof_pdata->machine = mach;
}

static void bdw_set_mach_params(const struct snd_soc_acpi_mach *mach,
				struct snd_sof_dev *sdev)
{
	struct snd_sof_pdata *pdata = sdev->pdata;
	const struct sof_dev_desc *desc = pdata->desc;
	struct snd_soc_acpi_mach_params *mach_params;

	mach_params = (struct snd_soc_acpi_mach_params *)&mach->mach_params;
	mach_params->platform = dev_name(sdev->dev);
	mach_params->num_dai_drivers = desc->ops->num_drv;
	mach_params->dai_drivers = desc->ops->drv;
}

/* Broadwell DAIs */
static struct snd_soc_dai_driver bdw_dai[] = {
{
	.name = "ssp0-port",
	.playback = {
		.channels_min = 1,
		.channels_max = 8,
	},
	.capture = {
		.channels_min = 1,
		.channels_max = 8,
	},
},
{
	.name = "ssp1-port",
	.playback = {
		.channels_min = 1,
		.channels_max = 8,
	},
	.capture = {
		.channels_min = 1,
		.channels_max = 8,
	},
},
};

/* broadwell ops */
static const struct snd_sof_dsp_ops sof_bdw_ops = {
	/*Device init */
	.probe          = bdw_probe,

	/* DSP Core Control */
	.run            = bdw_run,
	.reset          = bdw_reset,

	/* Register IO */
	.write		= sof_io_write,
	.read		= sof_io_read,
	.write64	= sof_io_write64,
	.read64		= sof_io_read64,

	/* Block IO */
	.block_read	= sof_block_read,
	.block_write	= sof_block_write,

	/* ipc */
	.send_msg	= bdw_send_msg,
	.fw_ready	= sof_fw_ready,
	.get_mailbox_offset = bdw_get_mailbox_offset,
	.get_window_offset = bdw_get_window_offset,

	.ipc_msg_data	= intel_ipc_msg_data,
	.ipc_pcm_params	= intel_ipc_pcm_params,

	/* machine driver */
	.machine_select = bdw_machine_select,
	.machine_register = sof_machine_register,
	.machine_unregister = sof_machine_unregister,
	.set_mach_params = bdw_set_mach_params,

	/* debug */
	.debug_map  = bdw_debugfs,
	.debug_map_count    = ARRAY_SIZE(bdw_debugfs),
	.dbg_dump   = bdw_dump,
	.debugfs_add_region_item = snd_sof_debugfs_add_region_item_iomem,

	/* stream callbacks */
	.pcm_open	= intel_pcm_open,
	.pcm_close	= intel_pcm_close,

	/* Module loading */
	.load_module    = snd_sof_parse_module_memcpy,

	/*Firmware loading */
	.load_firmware	= snd_sof_load_firmware_memcpy,

	/* DAI drivers */
	.drv = bdw_dai,
	.num_drv = ARRAY_SIZE(bdw_dai),

	/* ALSA HW info flags */
	.hw_info =	SNDRV_PCM_INFO_MMAP |
			SNDRV_PCM_INFO_MMAP_VALID |
			SNDRV_PCM_INFO_INTERLEAVED |
			SNDRV_PCM_INFO_PAUSE |
			SNDRV_PCM_INFO_BATCH,

	.dsp_arch_ops = &sof_xtensa_arch_ops,
};

static const struct sof_intel_dsp_desc bdw_chip_info = {
	.cores_num = 1,
	.host_managed_cores_mask = 1,
};

static const struct sof_dev_desc sof_acpi_broadwell_desc = {
	.machines = snd_soc_acpi_intel_broadwell_machines,
	.resindex_lpe_base = 0,
	.resindex_pcicfg_base = 1,
	.resindex_imr_base = -1,
	.irqindex_host_ipc = 0,
	.chip_info = &bdw_chip_info,
	.default_fw_path = "intel/sof",
	.default_tplg_path = "intel/sof-tplg",
	.default_fw_filename = "sof-bdw.ri",
	.nocodec_tplg_filename = "sof-bdw-nocodec.tplg",
	.ops = &sof_bdw_ops,
};

static const struct acpi_device_id sof_broadwell_match[] = {
	{ "INT3438", (unsigned long)&sof_acpi_broadwell_desc },
	{ }
};
MODULE_DEVICE_TABLE(acpi, sof_broadwell_match);

static int sof_broadwell_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct acpi_device_id *id;
	const struct sof_dev_desc *desc;
	int ret;

	id = acpi_match_device(dev->driver->acpi_match_table, dev);
	if (!id)
		return -ENODEV;

	ret = snd_intel_acpi_dsp_driver_probe(dev, id->id);
	if (ret != SND_INTEL_DSP_DRIVER_ANY && ret != SND_INTEL_DSP_DRIVER_SOF) {
		dev_dbg(dev, "SOF ACPI driver not selected, aborting probe\n");
		return -ENODEV;
	}

	desc = device_get_match_data(dev);
	if (!desc)
		return -ENODEV;

	return sof_acpi_probe(pdev, device_get_match_data(dev));
}

/* acpi_driver definition */
static struct platform_driver snd_sof_acpi_intel_bdw_driver = {
	.probe = sof_broadwell_probe,
	.remove = sof_acpi_remove,
	.driver = {
		.name = "sof-audio-acpi-intel-bdw",
		.pm = &sof_acpi_pm,
		.acpi_match_table = sof_broadwell_match,
	},
};
module_platform_driver(snd_sof_acpi_intel_bdw_driver);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_IMPORT_NS(SND_SOC_SOF_INTEL_HIFI_EP_IPC);
MODULE_IMPORT_NS(SND_SOC_SOF_XTENSA);
MODULE_IMPORT_NS(SND_SOC_SOF_ACPI_DEV);
