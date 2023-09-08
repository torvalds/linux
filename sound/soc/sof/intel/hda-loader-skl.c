// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018-2022 Intel Corporation. All rights reserved.
//

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/firmware.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <sound/hdaudio_ext.h>
#include <sound/sof.h>
#include <sound/pcm_params.h>

#include "../sof-priv.h"
#include "../ops.h"
#include "hda.h"

#define HDA_SKL_WAIT_TIMEOUT		500	/* 500 msec */
#define HDA_SKL_CLDMA_MAX_BUFFER_SIZE	(32 * PAGE_SIZE)

/* Stream Reset */
#define HDA_CL_SD_CTL_SRST_SHIFT	0
#define HDA_CL_SD_CTL_SRST(x)		(((x) & 0x1) << \
					HDA_CL_SD_CTL_SRST_SHIFT)

/* Stream Run */
#define HDA_CL_SD_CTL_RUN_SHIFT		1
#define HDA_CL_SD_CTL_RUN(x)		(((x) & 0x1) << \
					HDA_CL_SD_CTL_RUN_SHIFT)

/* Interrupt On Completion Enable */
#define HDA_CL_SD_CTL_IOCE_SHIFT	2
#define HDA_CL_SD_CTL_IOCE(x)		(((x) & 0x1) << \
					HDA_CL_SD_CTL_IOCE_SHIFT)

/* FIFO Error Interrupt Enable */
#define HDA_CL_SD_CTL_FEIE_SHIFT	3
#define HDA_CL_SD_CTL_FEIE(x)		(((x) & 0x1) << \
					HDA_CL_SD_CTL_FEIE_SHIFT)

/* Descriptor Error Interrupt Enable */
#define HDA_CL_SD_CTL_DEIE_SHIFT	4
#define HDA_CL_SD_CTL_DEIE(x)		(((x) & 0x1) << \
					HDA_CL_SD_CTL_DEIE_SHIFT)

/* FIFO Limit Change */
#define HDA_CL_SD_CTL_FIFOLC_SHIFT	5
#define HDA_CL_SD_CTL_FIFOLC(x)		(((x) & 0x1) << \
					HDA_CL_SD_CTL_FIFOLC_SHIFT)

/* Stripe Control */
#define HDA_CL_SD_CTL_STRIPE_SHIFT	16
#define HDA_CL_SD_CTL_STRIPE(x)		(((x) & 0x3) << \
					HDA_CL_SD_CTL_STRIPE_SHIFT)

/* Traffic Priority */
#define HDA_CL_SD_CTL_TP_SHIFT		18
#define HDA_CL_SD_CTL_TP(x)		(((x) & 0x1) << \
					HDA_CL_SD_CTL_TP_SHIFT)

/* Bidirectional Direction Control */
#define HDA_CL_SD_CTL_DIR_SHIFT		19
#define HDA_CL_SD_CTL_DIR(x)		(((x) & 0x1) << \
					HDA_CL_SD_CTL_DIR_SHIFT)

/* Stream Number */
#define HDA_CL_SD_CTL_STRM_SHIFT	20
#define HDA_CL_SD_CTL_STRM(x)		(((x) & 0xf) << \
					HDA_CL_SD_CTL_STRM_SHIFT)

#define HDA_CL_SD_CTL_INT(x)	\
		(HDA_CL_SD_CTL_IOCE(x) | \
		HDA_CL_SD_CTL_FEIE(x) | \
		HDA_CL_SD_CTL_DEIE(x))

#define HDA_CL_SD_CTL_INT_MASK	\
		(HDA_CL_SD_CTL_IOCE(1) | \
		HDA_CL_SD_CTL_FEIE(1) | \
		HDA_CL_SD_CTL_DEIE(1))

#define DMA_ADDRESS_128_BITS_ALIGNMENT	7
#define BDL_ALIGN(x)			((x) >> DMA_ADDRESS_128_BITS_ALIGNMENT)

/* Buffer Descriptor List Lower Base Address */
#define HDA_CL_SD_BDLPLBA_SHIFT		7
#define HDA_CL_SD_BDLPLBA_MASK		GENMASK(31, 7)
#define HDA_CL_SD_BDLPLBA(x)		\
	((BDL_ALIGN(lower_32_bits(x)) << HDA_CL_SD_BDLPLBA_SHIFT) & \
	 HDA_CL_SD_BDLPLBA_MASK)

/* Buffer Descriptor List Upper Base Address */
#define HDA_CL_SD_BDLPUBA(x)		\
			(upper_32_bits(x))

/* Software Position in Buffer Enable */
#define HDA_CL_SPBFIFO_SPBFCCTL_SPIBE_SHIFT	0
#define HDA_CL_SPBFIFO_SPBFCCTL_SPIBE_MASK	\
			(1 << HDA_CL_SPBFIFO_SPBFCCTL_SPIBE_SHIFT)

#define HDA_CL_SPBFIFO_SPBFCCTL_SPIBE(x)	\
			(((x) << HDA_CL_SPBFIFO_SPBFCCTL_SPIBE_SHIFT) & \
			 HDA_CL_SPBFIFO_SPBFCCTL_SPIBE_MASK)

#define HDA_CL_DMA_SD_INT_COMPLETE		0x4

static int cl_skl_cldma_setup_bdle(struct snd_sof_dev *sdev,
				   struct snd_dma_buffer *dmab_data,
				   __le32 **bdlp, int size, int with_ioc)
{
	phys_addr_t addr = virt_to_phys(dmab_data->area);
	__le32 *bdl = *bdlp;

	/*
	 * This code is simplified by using one fragment of physical memory and assuming
	 * all the code fits. This could be improved with scatter-gather but the firmware
	 * size is limited by DSP memory anyways
	 */
	bdl[0] = cpu_to_le32(lower_32_bits(addr));
	bdl[1] = cpu_to_le32(upper_32_bits(addr));
	bdl[2] = cpu_to_le32(size);
	bdl[3] = (!with_ioc) ? 0 : cpu_to_le32(0x01);

	return 1; /* one fragment */
}

static void cl_skl_cldma_stream_run(struct snd_sof_dev *sdev, bool enable)
{
	int sd_offset = SOF_HDA_ADSP_LOADER_BASE;
	unsigned char val;
	int retries;
	u32 run = enable ? 0x1 : 0;

	snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR,
				sd_offset + SOF_HDA_ADSP_REG_SD_CTL,
				HDA_CL_SD_CTL_RUN(1), HDA_CL_SD_CTL_RUN(run));

	retries = 300;
	do {
		udelay(3);

		/* waiting for hardware to report the stream Run bit set */
		val = snd_sof_dsp_read(sdev, HDA_DSP_BAR,
				       sd_offset + SOF_HDA_ADSP_REG_SD_CTL);
		val &= HDA_CL_SD_CTL_RUN(1);
		if (enable && val)
			break;
		else if (!enable && !val)
			break;
	} while (--retries);

	if (retries == 0)
		dev_err(sdev->dev, "%s: failed to set Run bit=%d enable=%d\n",
			__func__, val, enable);
}

static void cl_skl_cldma_stream_clear(struct snd_sof_dev *sdev)
{
	int sd_offset = SOF_HDA_ADSP_LOADER_BASE;

	/* make sure Run bit is cleared before setting stream register */
	cl_skl_cldma_stream_run(sdev, 0);

	/* Disable the Interrupt On Completion, FIFO Error Interrupt,
	 * Descriptor Error Interrupt and set the cldma stream number to 0.
	 */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR,
				sd_offset + SOF_HDA_ADSP_REG_SD_CTL,
				HDA_CL_SD_CTL_INT_MASK, HDA_CL_SD_CTL_INT(0));
	snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR,
				sd_offset + SOF_HDA_ADSP_REG_SD_CTL,
				HDA_CL_SD_CTL_STRM(0xf), HDA_CL_SD_CTL_STRM(0));

	snd_sof_dsp_write(sdev, HDA_DSP_BAR,
			  sd_offset + SOF_HDA_ADSP_REG_SD_BDLPL, HDA_CL_SD_BDLPLBA(0));
	snd_sof_dsp_write(sdev, HDA_DSP_BAR,
			  sd_offset + SOF_HDA_ADSP_REG_SD_BDLPU, 0);

	/* Set the Cyclic Buffer Length to 0. */
	snd_sof_dsp_write(sdev, HDA_DSP_BAR,
			  sd_offset + SOF_HDA_ADSP_REG_SD_CBL, 0);
	/* Set the Last Valid Index. */
	snd_sof_dsp_write(sdev, HDA_DSP_BAR,
			  sd_offset + SOF_HDA_ADSP_REG_SD_LVI, 0);
}

static void cl_skl_cldma_setup_spb(struct snd_sof_dev *sdev,
				   unsigned int size, bool enable)
{
	int sd_offset = SOF_DSP_REG_CL_SPBFIFO;

	if (enable)
		snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR,
					sd_offset + SOF_HDA_ADSP_REG_CL_SPBFIFO_SPBFCCTL,
					HDA_CL_SPBFIFO_SPBFCCTL_SPIBE_MASK,
					HDA_CL_SPBFIFO_SPBFCCTL_SPIBE(1));

	snd_sof_dsp_write(sdev, HDA_DSP_BAR,
			  sd_offset + SOF_HDA_ADSP_REG_CL_SPBFIFO_SPIB, size);
}

static void cl_skl_cldma_set_intr(struct snd_sof_dev *sdev, bool enable)
{
	u32 val = enable ? HDA_DSP_ADSPIC_CL_DMA : 0;

	snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR, HDA_DSP_REG_ADSPIC,
				HDA_DSP_ADSPIC_CL_DMA, val);
}

static void cl_skl_cldma_cleanup_spb(struct snd_sof_dev *sdev)
{
	int sd_offset = SOF_DSP_REG_CL_SPBFIFO;

	snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR,
				sd_offset + SOF_HDA_ADSP_REG_CL_SPBFIFO_SPBFCCTL,
				HDA_CL_SPBFIFO_SPBFCCTL_SPIBE_MASK,
				HDA_CL_SPBFIFO_SPBFCCTL_SPIBE(0));

	snd_sof_dsp_write(sdev, HDA_DSP_BAR,
			  sd_offset + SOF_HDA_ADSP_REG_CL_SPBFIFO_SPIB, 0);
}

static void cl_skl_cldma_setup_controller(struct snd_sof_dev *sdev,
					  struct snd_dma_buffer *dmab_bdl,
					  unsigned int max_size, u32 count)
{
	int sd_offset = SOF_HDA_ADSP_LOADER_BASE;

	/* Clear the stream first and then set it. */
	cl_skl_cldma_stream_clear(sdev);

	/* setting the stream register */
	snd_sof_dsp_write(sdev, HDA_DSP_BAR,
			  sd_offset + SOF_HDA_ADSP_REG_SD_BDLPL,
			  HDA_CL_SD_BDLPLBA(dmab_bdl->addr));
	snd_sof_dsp_write(sdev, HDA_DSP_BAR,
			  sd_offset + SOF_HDA_ADSP_REG_SD_BDLPU,
			  HDA_CL_SD_BDLPUBA(dmab_bdl->addr));

	/* Set the Cyclic Buffer Length. */
	snd_sof_dsp_write(sdev, HDA_DSP_BAR,
			  sd_offset + SOF_HDA_ADSP_REG_SD_CBL, max_size);
	/* Set the Last Valid Index. */
	snd_sof_dsp_write(sdev, HDA_DSP_BAR,
			  sd_offset + SOF_HDA_ADSP_REG_SD_LVI, count - 1);

	/* Set the Interrupt On Completion, FIFO Error Interrupt,
	 * Descriptor Error Interrupt and the cldma stream number.
	 */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR,
				sd_offset + SOF_HDA_ADSP_REG_SD_CTL,
				HDA_CL_SD_CTL_INT_MASK, HDA_CL_SD_CTL_INT(1));
	snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR,
				sd_offset + SOF_HDA_ADSP_REG_SD_CTL,
				HDA_CL_SD_CTL_STRM(0xf),
				HDA_CL_SD_CTL_STRM(1));
}

static int cl_stream_prepare_skl(struct snd_sof_dev *sdev,
				 struct snd_dma_buffer *dmab,
				 struct snd_dma_buffer *dmab_bdl)

{
	unsigned int bufsize = HDA_SKL_CLDMA_MAX_BUFFER_SIZE;
	__le32 *bdl;
	int frags;
	int ret;

	ret = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, sdev->dev, bufsize, dmab);
	if (ret < 0) {
		dev_err(sdev->dev, "%s: failed to alloc fw buffer: %x\n", __func__, ret);
		return ret;
	}

	ret = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, sdev->dev, bufsize, dmab_bdl);
	if (ret < 0) {
		dev_err(sdev->dev, "%s: failed to alloc blde: %x\n", __func__, ret);
		snd_dma_free_pages(dmab);
		return ret;
	}

	bdl = (__le32 *)dmab_bdl->area;
	frags = cl_skl_cldma_setup_bdle(sdev, dmab, &bdl, bufsize, 1);
	cl_skl_cldma_setup_controller(sdev, dmab_bdl, bufsize, frags);

	return ret;
}

static void cl_cleanup_skl(struct snd_sof_dev *sdev,
			   struct snd_dma_buffer *dmab,
			   struct snd_dma_buffer *dmab_bdl)
{
	cl_skl_cldma_cleanup_spb(sdev);
	cl_skl_cldma_stream_clear(sdev);
	snd_dma_free_pages(dmab);
	snd_dma_free_pages(dmab_bdl);
}

static int cl_dsp_init_skl(struct snd_sof_dev *sdev,
			   struct snd_dma_buffer *dmab,
			   struct snd_dma_buffer *dmab_bdl)
{
	struct sof_intel_hda_dev *hda = sdev->pdata->hw_pdata;
	const struct sof_intel_dsp_desc *chip = hda->desc;
	unsigned int status;
	u32 flags;
	int ret;

	/* check if the init_core is already enabled, if yes, reset and make it run,
	 * if not, powerdown and enable it again.
	 */
	if (hda_dsp_core_is_enabled(sdev, chip->init_core_mask)) {
		/* if enabled, reset it, and run the init_core. */
		ret = hda_dsp_core_stall_reset(sdev, chip->init_core_mask);
		if (ret < 0)
			goto err;

		ret = hda_dsp_core_run(sdev, chip->init_core_mask);
		if (ret < 0) {
			dev_err(sdev->dev, "%s: dsp core start failed %d\n", __func__, ret);
			goto err;
		}
	} else {
		/* if not enabled, power down it first and then powerup and run
		 * the init_core.
		 */
		ret = hda_dsp_core_reset_power_down(sdev, chip->init_core_mask);
		if (ret < 0) {
			dev_err(sdev->dev, "%s: dsp core0 disable fail: %d\n", __func__, ret);
			goto err;
		}
		ret = hda_dsp_enable_core(sdev, chip->init_core_mask);
		if (ret < 0) {
			dev_err(sdev->dev, "%s: dsp core0 enable fail: %d\n", __func__, ret);
			goto err;
		}
	}

	/* prepare DMA for code loader stream */
	ret = cl_stream_prepare_skl(sdev, dmab, dmab_bdl);
	if (ret < 0) {
		dev_err(sdev->dev, "%s: dma prepare fw loading err: %x\n", __func__, ret);
		return ret;
	}

	/* enable the interrupt */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR, HDA_DSP_REG_ADSPIC,
				HDA_DSP_ADSPIC_IPC, HDA_DSP_ADSPIC_IPC);

	/* enable IPC DONE interrupt */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR, chip->ipc_ctl,
				HDA_DSP_REG_HIPCCTL_DONE,
				HDA_DSP_REG_HIPCCTL_DONE);

	/* enable IPC BUSY interrupt */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR, chip->ipc_ctl,
				HDA_DSP_REG_HIPCCTL_BUSY,
				HDA_DSP_REG_HIPCCTL_BUSY);

	/* polling the ROM init status information. */
	ret = snd_sof_dsp_read_poll_timeout(sdev, HDA_DSP_BAR,
					    chip->rom_status_reg, status,
					    (FSR_TO_STATE_CODE(status)
					     == FSR_STATE_INIT_DONE),
					    HDA_DSP_REG_POLL_INTERVAL_US,
					    chip->rom_init_timeout *
					    USEC_PER_MSEC);
	if (ret < 0)
		goto err;

	return ret;

err:
	flags = SOF_DBG_DUMP_PCI | SOF_DBG_DUMP_MBOX;

	snd_sof_dsp_dbg_dump(sdev, "Boot failed\n", flags);
	cl_cleanup_skl(sdev, dmab, dmab_bdl);
	hda_dsp_core_reset_power_down(sdev, chip->init_core_mask);
	return ret;
}

static void cl_skl_cldma_fill_buffer(struct snd_sof_dev *sdev,
				     struct snd_dma_buffer *dmab,
				     unsigned int bufsize,
				     unsigned int copysize,
				     const void *curr_pos,
				     bool intr_enable)
{
	struct sof_intel_hda_dev *hda = sdev->pdata->hw_pdata;

	/* copy the image into the buffer with the maximum buffer size. */
	unsigned int size = (bufsize == copysize) ? bufsize : copysize;

	memcpy(dmab->area, curr_pos, size);

	/* Set the wait condition for every load. */
	hda->code_loading = 1;

	/* Set the interrupt. */
	if (intr_enable)
		cl_skl_cldma_set_intr(sdev, true);

	/* Set the SPB. */
	cl_skl_cldma_setup_spb(sdev, size, true);

	/* Trigger the code loading stream. */
	cl_skl_cldma_stream_run(sdev, true);
}

static int cl_skl_cldma_wait_interruptible(struct snd_sof_dev *sdev,
					   bool intr_wait)
{
	struct sof_intel_hda_dev *hda = sdev->pdata->hw_pdata;
	const struct sof_intel_dsp_desc *chip = hda->desc;
	int sd_offset = SOF_HDA_ADSP_LOADER_BASE;
	u8 cl_dma_intr_status;

	/*
	 * Wait for CLDMA interrupt to inform the binary segment transfer is
	 * complete.
	 */
	if (!wait_event_timeout(hda->waitq, !hda->code_loading,
				msecs_to_jiffies(HDA_SKL_WAIT_TIMEOUT))) {
		dev_err(sdev->dev, "cldma copy timeout\n");
		dev_err(sdev->dev, "ROM code=%#x: FW status=%#x\n",
			snd_sof_dsp_read(sdev, HDA_DSP_BAR, HDA_DSP_SRAM_REG_ROM_ERROR),
			snd_sof_dsp_read(sdev, HDA_DSP_BAR, chip->rom_status_reg));
		return -EIO;
	}

	/* now check DMA interrupt status */
	cl_dma_intr_status = snd_sof_dsp_read(sdev, HDA_DSP_BAR,
					      sd_offset + SOF_HDA_ADSP_REG_SD_STS);

	if (!(cl_dma_intr_status & HDA_CL_DMA_SD_INT_COMPLETE)) {
		dev_err(sdev->dev, "cldma copy failed\n");
		return -EIO;
	}

	dev_dbg(sdev->dev, "cldma buffer copy complete\n");
	return 0;
}

static int
cl_skl_cldma_copy_to_buf(struct snd_sof_dev *sdev,
			 struct snd_dma_buffer *dmab,
			 const void *bin,
			 u32 total_size, u32 bufsize)
{
	unsigned int bytes_left = total_size;
	const void *curr_pos = bin;
	int ret;

	if (total_size <= 0)
		return -EINVAL;

	while (bytes_left > 0) {
		if (bytes_left > bufsize) {
			dev_dbg(sdev->dev, "cldma copy %#x bytes\n", bufsize);

			cl_skl_cldma_fill_buffer(sdev, dmab, bufsize, bufsize, curr_pos, true);

			ret = cl_skl_cldma_wait_interruptible(sdev, false);
			if (ret < 0) {
				dev_err(sdev->dev, "%s: fw failed to load. %#x bytes remaining\n",
					__func__, bytes_left);
				return ret;
			}

			bytes_left -= bufsize;
			curr_pos += bufsize;
		} else {
			dev_dbg(sdev->dev, "cldma copy %#x bytes\n", bytes_left);

			cl_skl_cldma_set_intr(sdev, false);
			cl_skl_cldma_fill_buffer(sdev, dmab, bufsize, bytes_left, curr_pos, false);
			return 0;
		}
	}

	return bytes_left;
}

static int cl_copy_fw_skl(struct snd_sof_dev *sdev,
			  struct snd_dma_buffer *dmab)

{
	const struct firmware *fw =  sdev->basefw.fw;
	struct firmware stripped_firmware;
	unsigned int bufsize = HDA_SKL_CLDMA_MAX_BUFFER_SIZE;
	int ret;

	stripped_firmware.data = fw->data + sdev->basefw.payload_offset;
	stripped_firmware.size = fw->size - sdev->basefw.payload_offset;

	dev_dbg(sdev->dev, "firmware size: %#zx buffer size %#x\n", fw->size, bufsize);

	ret = cl_skl_cldma_copy_to_buf(sdev, dmab, stripped_firmware.data,
				       stripped_firmware.size, bufsize);
	if (ret < 0)
		dev_err(sdev->dev, "%s: fw copy failed %d\n", __func__, ret);

	return ret;
}

int hda_dsp_cl_boot_firmware_skl(struct snd_sof_dev *sdev)
{
	struct sof_intel_hda_dev *hda = sdev->pdata->hw_pdata;
	const struct sof_intel_dsp_desc *chip = hda->desc;
	struct snd_dma_buffer dmab_bdl;
	struct snd_dma_buffer dmab;
	unsigned int reg;
	u32 flags;
	int ret;

	ret = cl_dsp_init_skl(sdev, &dmab, &dmab_bdl);

	/* retry enabling core and ROM load. seemed to help */
	if (ret < 0) {
		ret = cl_dsp_init_skl(sdev, &dmab, &dmab_bdl);
		if (ret < 0) {
			dev_err(sdev->dev, "Error code=%#x: FW status=%#x\n",
				snd_sof_dsp_read(sdev, HDA_DSP_BAR, HDA_DSP_SRAM_REG_ROM_ERROR),
				snd_sof_dsp_read(sdev, HDA_DSP_BAR, chip->rom_status_reg));
			dev_err(sdev->dev, "Core En/ROM load fail:%d\n", ret);
			return ret;
		}
	}

	dev_dbg(sdev->dev, "ROM init successful\n");

	/* at this point DSP ROM has been initialized and should be ready for
	 * code loading and firmware boot
	 */
	ret = cl_copy_fw_skl(sdev, &dmab);
	if (ret < 0) {
		dev_err(sdev->dev, "%s: load firmware failed : %d\n", __func__, ret);
		goto err;
	}

	ret = snd_sof_dsp_read_poll_timeout(sdev, HDA_DSP_BAR,
					    chip->rom_status_reg, reg,
					    (FSR_TO_STATE_CODE(reg)
					     == FSR_STATE_ROM_BASEFW_ENTERED),
					    HDA_DSP_REG_POLL_INTERVAL_US,
					    HDA_DSP_BASEFW_TIMEOUT_US);

	dev_dbg(sdev->dev, "Firmware download successful, booting...\n");

	cl_skl_cldma_stream_run(sdev, false);
	cl_cleanup_skl(sdev, &dmab, &dmab_bdl);

	if (!ret)
		return chip->init_core_mask;

	return ret;

err:
	flags = SOF_DBG_DUMP_PCI | SOF_DBG_DUMP_MBOX;

	snd_sof_dsp_dbg_dump(sdev, "Boot failed\n", flags);

	/* power down DSP */
	hda_dsp_core_reset_power_down(sdev, chip->init_core_mask);
	cl_skl_cldma_stream_run(sdev, false);
	cl_cleanup_skl(sdev, &dmab, &dmab_bdl);

	dev_err(sdev->dev, "%s: load fw failed err: %d\n", __func__, ret);
	return ret;
}
