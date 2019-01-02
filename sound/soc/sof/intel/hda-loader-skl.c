// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Authors: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//      Zhu Yingjiang <yingjiang.zhu@linux.intel.com>
//

#include <linux/firmware.h>
#include <sound/hdaudio_ext.h>
#include <sound/sof.h>
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
#define HDA_CL_SD_BDLPLBA_MASK		GENMASK(HDA_CL_SD_BDLPLBA_SHIFT + 24,\
						HDA_CL_SD_BDLPLBA_SHIFT)
#define HDA_CL_SD_BDLPLBA(x)		\
	((BDL_ALIGN(lower_32_bits(x)) << HDA_CL_SD_BDLPLBA_SHIFT) & \
	 HDA_CL_SD_BDLPLBA_MASK)

/* Buffer Descriptor List Upper Base Address */
#define HDA_CL_SD_BDLPUBA_SHIFT		0
#define HDA_CL_SD_BDLPUBA_MASK		GENMASK(HDA_CL_SD_BDLPUBA_SHIFT + 31,\
						HDA_CL_SD_BDLPUBA_SHIFT)
#define HDA_CL_SD_BDLPUBA(x)		\
		((upper_32_bits(x) << HDA_CL_SD_BDLPUBA_SHIFT) & \
		 HDA_CL_SD_BDLPUBA_MASK)

/* Software Position in Buffer Enable */
#define HDA_CL_SPBFIFO_SPBFCCTL_SPIBE_SHIFT	0
#define HDA_CL_SPBFIFO_SPBFCCTL_SPIBE_MASK	\
			BIT(HDA_CL_SPBFIFO_SPBFCCTL_SPIBE_SHIFT)

#define HDA_CL_SPBFIFO_SPBFCCTL_SPIBE(x)	\
			(((x) << HDA_CL_SPBFIFO_SPBFCCTL_SPIBE_SHIFT) & \
			 HDA_CL_SPBFIFO_SPBFCCTL_SPIBE_MASK)

static int cl_skl_cldma_setup_bdle(struct snd_sof_dev *sdev,
				   struct snd_dma_buffer *dmab_data,
				   __le32 **bdlp, int size, int with_ioc)
{
	__le32 *bdl = *bdlp;
	int frags = 0;

	while (size > 0) {
		phys_addr_t addr = virt_to_phys(dmab_data->area +
						(frags * size));

		bdl[0] = cpu_to_le32(lower_32_bits(addr));
		bdl[1] = cpu_to_le32(upper_32_bits(addr));

		bdl[2] = cpu_to_le32(size);

		size -= size;
		bdl[3] = (size || !with_ioc) ? 0 : cpu_to_le32(0x01);

		bdl += 4;
		frags++;
	}

	return frags;
}

static void cl_skl_cldma_stream_run(struct snd_sof_dev *sdev, bool enable)
{
	unsigned char val;
	int timeout;
	int sd_offset = SOF_HDA_ADSP_LOADER_BASE;
	u32 run = enable ? 0x1 : 0;

	snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR,
				sd_offset + SOF_HDA_ADSP_REG_CL_SD_CTL,
				HDA_CL_SD_CTL_RUN(1), HDA_CL_SD_CTL_RUN(run));

	udelay(3);
	timeout = 300;
	do {
		/* waiting for hardware to report the stream Run bit set */
		val = snd_sof_dsp_read(sdev, HDA_DSP_BAR,
				       sd_offset + SOF_HDA_ADSP_REG_CL_SD_CTL)
				       & HDA_CL_SD_CTL_RUN(1);
		if (enable && val)
			break;
		else if (!enable && !val)
			break;
		udelay(3);
	} while (--timeout);

	if (timeout == 0)
		dev_err(sdev->dev, "error: failed to set Run bit=%d enable=%d\n",
			val, enable);
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
				sd_offset + SOF_HDA_ADSP_REG_CL_SD_CTL,
				HDA_CL_SD_CTL_INT_MASK, HDA_CL_SD_CTL_INT(0));
	snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR,
				sd_offset + SOF_HDA_ADSP_REG_CL_SD_CTL,
				HDA_CL_SD_CTL_STRM(0xf), HDA_CL_SD_CTL_STRM(0));

	snd_sof_dsp_write(sdev, HDA_DSP_BAR,
			  sd_offset + SOF_HDA_ADSP_REG_CL_SD_BDLPL,
			  HDA_CL_SD_BDLPLBA(0));
	snd_sof_dsp_write(sdev, HDA_DSP_BAR,
			  sd_offset + SOF_HDA_ADSP_REG_CL_SD_BDLPU, 0);

	/* Set the Cyclic Buffer Length to 0. */
	snd_sof_dsp_write(sdev, HDA_DSP_BAR,
			  sd_offset + SOF_HDA_ADSP_REG_CL_SD_CBL, 0);
	/* Set the Last Valid Index. */
	snd_sof_dsp_write(sdev, HDA_DSP_BAR,
			  sd_offset + SOF_HDA_ADSP_REG_CL_SD_LVI, 0);
}

static void cl_skl_cldma_setup_spb(struct snd_sof_dev *sdev,
				   unsigned int size, bool enable)
{
	int sd_offset = SOF_DSP_REG_CL_SPBFIFO;

	if (enable)
		snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR,
					sd_offset +
					SOF_HDA_ADSP_REG_CL_SPBFIFO_SPBFCCTL,
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
				sd_offset +
					SOF_HDA_ADSP_REG_CL_SPBFIFO_SPBFCCTL,
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
			  sd_offset + SOF_HDA_ADSP_REG_CL_SD_BDLPL,
			  HDA_CL_SD_BDLPLBA(dmab_bdl->addr));
	snd_sof_dsp_write(sdev, HDA_DSP_BAR,
			  sd_offset + SOF_HDA_ADSP_REG_CL_SD_BDLPU,
			  HDA_CL_SD_BDLPUBA(dmab_bdl->addr));

	/* Set the Cyclic Buffer Length. */
	snd_sof_dsp_write(sdev, HDA_DSP_BAR,
			  sd_offset + SOF_HDA_ADSP_REG_CL_SD_CBL, max_size);
	/* Set the Last Valid Index. */
	snd_sof_dsp_write(sdev, HDA_DSP_BAR,
			  sd_offset + SOF_HDA_ADSP_REG_CL_SD_LVI, count - 1);

	/* Set the Interrupt On Completion, FIFO Error Interrupt,
	 * Descriptor Error Interrupt and the cldma stream number.
	 */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR,
				sd_offset + SOF_HDA_ADSP_REG_CL_SD_CTL,
				HDA_CL_SD_CTL_INT_MASK, HDA_CL_SD_CTL_INT(1));
	snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR,
				sd_offset + SOF_HDA_ADSP_REG_CL_SD_CTL,
				HDA_CL_SD_CTL_STRM(0xf),
				HDA_CL_SD_CTL_STRM(1));
}

static int cl_stream_prepare_skl(struct snd_sof_dev *sdev)
{
	struct pci_dev *pci = sdev->pci;
	int frags = 0;
	int ret = 0;
	__le32 *bdl;
	unsigned int bufsize = HDA_SKL_CLDMA_MAX_BUFFER_SIZE;

	ret = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, &pci->dev, bufsize,
				  &sdev->dmab);
	if (ret < 0) {
		dev_err(sdev->dev, "error: failed to alloc fw buffer: %x\n",
			ret);
		return ret;
	}

	ret = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, &pci->dev, bufsize,
				  &sdev->dmab_bdl);
	if (ret < 0) {
		dev_err(sdev->dev, "error: failed to alloc blde: %x\n", ret);
		snd_dma_free_pages(&sdev->dmab);
		return ret;
	}

	bdl = (__le32 *)sdev->dmab_bdl.area;
	frags = cl_skl_cldma_setup_bdle(sdev, &sdev->dmab, &bdl, bufsize, 1);
	cl_skl_cldma_setup_controller(sdev, &sdev->dmab_bdl, bufsize, frags);

	return ret;
}

static void cl_cleanup_skl(struct snd_sof_dev *sdev)
{
	cl_skl_cldma_cleanup_spb(sdev);
	cl_skl_cldma_stream_clear(sdev);
	snd_dma_free_pages(&sdev->dmab);
	snd_dma_free_pages(&sdev->dmab_bdl);
	sdev->dmab.area = NULL;
}

static int cl_dsp_init_skl(struct snd_sof_dev *sdev)
{
	int ret;

	/* check if the core is already enabled, if yes, reset and make it run,
	 * if not, powerdown and enable it again.
	 */
	if (hda_dsp_core_is_enabled(sdev, HDA_DSP_CORE_MASK(0))) {

		/* if enabled, reset it, and run the core. */
		ret = hda_dsp_core_stall_reset(sdev, HDA_DSP_CORE_MASK(0));
		if (ret < 0)
			goto err;

		ret = hda_dsp_core_run(sdev, HDA_DSP_CORE_MASK(0));
		if (ret < 0) {
			dev_err(sdev->dev, "error: dsp core start failed %d\n",
				ret);
			goto err;
		}
	} else {
		/* if not enabled, power down it first and then powerup and run
		 * the core.
		 */
		ret = hda_dsp_core_reset_power_down(sdev, HDA_DSP_CORE_MASK(0));
		if (ret < 0) {
			dev_err(sdev->dev, "error: dsp core0 disable fail: %d\n", ret);
			goto err;
		}
		ret = hda_dsp_enable_core(sdev, HDA_DSP_CORE_MASK(0));
		if (ret < 0) {
			dev_err(sdev->dev, "error: dsp core0 enable fail: %d\n", ret);
			goto err;
		}
	}

	/* prepare DMA for code loader stream */
	ret = cl_stream_prepare_skl(sdev);
	if (ret < 0) {
		dev_err(sdev->dev, "error: dma prepare fw loading err: %x\n",
			ret);
		return ret;
	}

	/* enable IPC interrupts */
	hda_dsp_ipc_int_enable(sdev);

	/* polling the ROM init status information. */
	ret = snd_sof_dsp_register_poll(sdev, HDA_DSP_BAR,
					HDA_ADSP_FW_STATUS_SKL,
					HDA_DSP_ROM_STS_MASK, HDA_DSP_ROM_INIT,
					HDA_DSP_INIT_TIMEOUT);
	if (ret < 0)
		goto err;

	return ret;

err:
	hda_dsp_dump_skl(sdev, SOF_DBG_REGS | SOF_DBG_PCI | SOF_DBG_MBOX);
	cl_cleanup_skl(sdev);
	hda_dsp_core_reset_power_down(sdev, HDA_DSP_CORE_MASK(0));
	return ret;
}

static void cl_skl_cldma_fill_buffer(struct snd_sof_dev *sdev,
				     unsigned int bufsize,
				     unsigned int copysize,
				     const void *curr_pos,
				     bool intr_enable, bool trigger)
{
	/* 1. copy the image into the buffer with the maximum buffer size. */
	unsigned int size = (bufsize == copysize) ? bufsize : copysize;

	memcpy(sdev->dmab.area, curr_pos, size);

	/* 2. Set the interrupt. */
	if (intr_enable)
		cl_skl_cldma_set_intr(sdev, true);

	/* 3. Set the SPB. */
	cl_skl_cldma_setup_spb(sdev, size, trigger);

	/* 4. Trigger the code loading stream. */
	if (trigger)
		cl_skl_cldma_stream_run(sdev, true);
}

static int
cl_skl_cldma_copy_to_buf(struct snd_sof_dev *sdev, const void *bin,
			 u32 total_size, u32 bufsize)
{
	int ret = 0;
	unsigned int bytes_left = total_size;
	const void *curr_pos = bin;

	if (total_size <= 0)
		return -EINVAL;

	while (bytes_left > 0) {

		if (bytes_left > bufsize) {

			dev_dbg(sdev->dev, "cldma copy 0x%x bytes\n",
				bufsize);

			cl_skl_cldma_fill_buffer(sdev, bufsize, bufsize,
						 curr_pos, true, true);

			if (ret < 0) {
				dev_err(sdev->dev, "error: fw failed to load. 0x%x bytes remaining\n",
					bytes_left);
				cl_skl_cldma_stream_run(sdev, false);
				return ret;
			}

			bytes_left -= bufsize;
			curr_pos += bufsize;
		} else {

			dev_dbg(sdev->dev, "cldma copy 0x%x bytes\n",
				bytes_left);

			cl_skl_cldma_set_intr(sdev, false);
			cl_skl_cldma_fill_buffer(sdev, bufsize, bytes_left,
						 curr_pos, false, false);
			return 0;
		}
	}

	return bytes_left;
}

static int cl_copy_fw_skl(struct snd_sof_dev *sdev)
{
	struct snd_sof_pdata *plat_data = dev_get_platdata(sdev->dev);
	const struct firmware *fw =  plat_data->fw;
	unsigned int bufsize = HDA_SKL_CLDMA_MAX_BUFFER_SIZE;
	int ret = 0;

	dev_dbg(sdev->dev, "firmware size: 0x%zx buffer size 0x%x\n", fw->size,
		bufsize);

	ret = cl_skl_cldma_copy_to_buf(sdev, fw->data, fw->size, bufsize);
	if (ret < 0) {
		dev_err(sdev->dev, "error: fw copy failed %d\n", ret);
		return ret;
	}

	ret = snd_sof_dsp_register_poll(sdev, HDA_DSP_BAR,
					HDA_ADSP_FW_STATUS_SKL,
					HDA_DSP_ROM_STS_MASK,
					HDA_DSP_ROM_FW_FW_LOADED,
					HDA_DSP_BASEFW_TIMEOUT);
	if (ret < 0)
		dev_err(sdev->dev, "error: firmware transfer timeout!");

	cl_skl_cldma_stream_run(sdev, false);
	cl_cleanup_skl(sdev);

	return ret;
}

int hda_dsp_cl_boot_firmware_skl(struct snd_sof_dev *sdev)
{
	int ret;

	ret = cl_dsp_init_skl(sdev);

	/* retry enabling core and ROM load. seemed to help */
	if (ret < 0) {
		ret = cl_dsp_init_skl(sdev);
		if (ret < 0) {
			dev_err(sdev->dev, "error: Error code=0x%x: FW status=0x%x\n",
				snd_sof_dsp_read(sdev, HDA_DSP_BAR,
						 HDA_ADSP_ERROR_CODE_SKL),
				snd_sof_dsp_read(sdev, HDA_DSP_BAR,
						 HDA_ADSP_FW_STATUS_SKL));
			dev_err(sdev->dev, "error: Core En/ROM load fail:%d\n", ret);
			goto irq_err;
		}
	}

	dev_dbg(sdev->dev, "ROM init successful\n");

	/* init for booting wait */
	init_waitqueue_head(&sdev->boot_wait);
	sdev->boot_complete = false;

	/* at this point DSP ROM has been initialized and should be ready for
	 * code loading and firmware boot
	 */
	ret = cl_copy_fw_skl(sdev);
	if (ret < 0) {
		dev_err(sdev->dev, "error: load firmware failed : %d\n", ret);
		goto irq_err;
	}

	dev_dbg(sdev->dev, "Firmware download successful, booting...\n");

	return ret;

irq_err:
	hda_dsp_dump_skl(sdev, SOF_DBG_REGS | SOF_DBG_PCI | SOF_DBG_MBOX);

	/* power down DSP */
	hda_dsp_core_reset_power_down(sdev, HDA_DSP_CORE_MASK(0));
	cl_cleanup_skl(sdev);

	dev_err(sdev->dev, "error: load fw failed err: %d\n", ret);
	return ret;
}
