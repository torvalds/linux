// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Authors: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//	    Ranjani Sridharan <ranjani.sridharan@linux.intel.com>
//	    Rander Wang <rander.wang@intel.com>
//          Keyon Jie <yang.jie@linux.intel.com>
//

/*
 * Hardware interface for generic Intel audio DSP HDA IP
 */

#include <sound/hdaudio_ext.h>
#include <sound/hda_register.h>

#include <linux/module.h>
#include <sound/intel-nhlt.h>
#include <sound/sof.h>
#include <sound/sof/xtensa.h>
#include "../ops.h"
#include "hda.h"

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)
#include <sound/soc-acpi-intel-match.h>
#endif

/* platform specific devices */
#include "shim.h"

#define EXCEPT_MAX_HDR_SIZE	0x400

/*
 * Debug
 */

struct hda_dsp_msg_code {
	u32 code;
	const char *msg;
};

static bool hda_use_msi = IS_ENABLED(CONFIG_PCI);
#if IS_ENABLED(CONFIG_SND_SOC_SOF_DEBUG)
module_param_named(use_msi, hda_use_msi, bool, 0444);
MODULE_PARM_DESC(use_msi, "SOF HDA use PCI MSI mode");
#endif

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)
static int hda_dmic_num = -1;
module_param_named(dmic_num, hda_dmic_num, int, 0444);
MODULE_PARM_DESC(dmic_num, "SOF HDA DMIC number");

static bool hda_codec_use_common_hdmi =
	IS_ENABLED(CONFIG_SND_SOC_SOF_HDA_COMMON_HDMI_CODEC);
module_param_named(use_common_hdmi, hda_codec_use_common_hdmi, bool, 0444);
MODULE_PARM_DESC(use_common_hdmi, "SOF HDA use common HDMI codec driver");
#endif

static const struct hda_dsp_msg_code hda_dsp_rom_msg[] = {
	{HDA_DSP_ROM_FW_MANIFEST_LOADED, "status: manifest loaded"},
	{HDA_DSP_ROM_FW_FW_LOADED, "status: fw loaded"},
	{HDA_DSP_ROM_FW_ENTERED, "status: fw entered"},
	{HDA_DSP_ROM_CSE_ERROR, "error: cse error"},
	{HDA_DSP_ROM_CSE_WRONG_RESPONSE, "error: cse wrong response"},
	{HDA_DSP_ROM_IMR_TO_SMALL, "error: IMR too small"},
	{HDA_DSP_ROM_BASE_FW_NOT_FOUND, "error: base fw not found"},
	{HDA_DSP_ROM_CSE_VALIDATION_FAILED, "error: signature verification failed"},
	{HDA_DSP_ROM_IPC_FATAL_ERROR, "error: ipc fatal error"},
	{HDA_DSP_ROM_L2_CACHE_ERROR, "error: L2 cache error"},
	{HDA_DSP_ROM_LOAD_OFFSET_TO_SMALL, "error: load offset too small"},
	{HDA_DSP_ROM_API_PTR_INVALID, "error: API ptr invalid"},
	{HDA_DSP_ROM_BASEFW_INCOMPAT, "error: base fw incompatible"},
	{HDA_DSP_ROM_UNHANDLED_INTERRUPT, "error: unhandled interrupt"},
	{HDA_DSP_ROM_MEMORY_HOLE_ECC, "error: ECC memory hole"},
	{HDA_DSP_ROM_KERNEL_EXCEPTION, "error: kernel exception"},
	{HDA_DSP_ROM_USER_EXCEPTION, "error: user exception"},
	{HDA_DSP_ROM_UNEXPECTED_RESET, "error: unexpected reset"},
	{HDA_DSP_ROM_NULL_FW_ENTRY,	"error: null FW entry point"},
};

static void hda_dsp_get_status_skl(struct snd_sof_dev *sdev)
{
	u32 status;
	int i;

	status = snd_sof_dsp_read(sdev, HDA_DSP_BAR,
				  HDA_ADSP_FW_STATUS_SKL);

	for (i = 0; i < ARRAY_SIZE(hda_dsp_rom_msg); i++) {
		if (status == hda_dsp_rom_msg[i].code) {
			dev_err(sdev->dev, "%s - code %8.8x\n",
				hda_dsp_rom_msg[i].msg, status);
			return;
		}
	}

	/* not for us, must be generic sof message */
	dev_dbg(sdev->dev, "unknown ROM status value %8.8x\n", status);
}

static void hda_dsp_get_status(struct snd_sof_dev *sdev)
{
	u32 status;
	int i;

	status = snd_sof_dsp_read(sdev, HDA_DSP_BAR,
				  HDA_DSP_SRAM_REG_ROM_STATUS);

	for (i = 0; i < ARRAY_SIZE(hda_dsp_rom_msg); i++) {
		if (status == hda_dsp_rom_msg[i].code) {
			dev_err(sdev->dev, "%s - code %8.8x\n",
				hda_dsp_rom_msg[i].msg, status);
			return;
		}
	}

	/* not for us, must be generic sof message */
	dev_dbg(sdev->dev, "unknown ROM status value %8.8x\n", status);
}

static void hda_dsp_get_registers(struct snd_sof_dev *sdev,
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
	sof_block_read(sdev, sdev->mmio_bar, offset,
		       panic_info, sizeof(*panic_info));

	/* then get the stack */
	offset += sizeof(*panic_info);
	sof_block_read(sdev, sdev->mmio_bar, offset, stack,
		       stack_words * sizeof(u32));
}

void hda_dsp_dump_skl(struct snd_sof_dev *sdev, u32 flags)
{
	struct sof_ipc_dsp_oops_xtensa xoops;
	struct sof_ipc_panic_info panic_info;
	u32 stack[HDA_DSP_STACK_DUMP_SIZE];
	u32 status, panic;

	/* try APL specific status message types first */
	hda_dsp_get_status_skl(sdev);

	/* now try generic SOF status messages */
	status = snd_sof_dsp_read(sdev, HDA_DSP_BAR,
				  HDA_ADSP_ERROR_CODE_SKL);

	/*TODO: Check: there is no define in spec, but it is used in the code*/
	panic = snd_sof_dsp_read(sdev, HDA_DSP_BAR,
				 HDA_ADSP_ERROR_CODE_SKL + 0x4);

	if (sdev->fw_state == SOF_FW_BOOT_COMPLETE) {
		hda_dsp_get_registers(sdev, &xoops, &panic_info, stack,
				      HDA_DSP_STACK_DUMP_SIZE);
		snd_sof_get_status(sdev, status, panic, &xoops, &panic_info,
				   stack, HDA_DSP_STACK_DUMP_SIZE);
	} else {
		dev_err(sdev->dev, "error: status = 0x%8.8x panic = 0x%8.8x\n",
			status, panic);
		hda_dsp_get_status_skl(sdev);
	}
}

void hda_dsp_dump(struct snd_sof_dev *sdev, u32 flags)
{
	struct sof_ipc_dsp_oops_xtensa xoops;
	struct sof_ipc_panic_info panic_info;
	u32 stack[HDA_DSP_STACK_DUMP_SIZE];
	u32 status, panic;

	/* try APL specific status message types first */
	hda_dsp_get_status(sdev);

	/* now try generic SOF status messages */
	status = snd_sof_dsp_read(sdev, HDA_DSP_BAR,
				  HDA_DSP_SRAM_REG_FW_STATUS);
	panic = snd_sof_dsp_read(sdev, HDA_DSP_BAR, HDA_DSP_SRAM_REG_FW_TRACEP);

	if (sdev->fw_state == SOF_FW_BOOT_COMPLETE) {
		hda_dsp_get_registers(sdev, &xoops, &panic_info, stack,
				      HDA_DSP_STACK_DUMP_SIZE);
		snd_sof_get_status(sdev, status, panic, &xoops, &panic_info,
				   stack, HDA_DSP_STACK_DUMP_SIZE);
	} else {
		dev_err(sdev->dev, "error: status = 0x%8.8x panic = 0x%8.8x\n",
			status, panic);
		hda_dsp_get_status(sdev);
	}
}

void hda_ipc_irq_dump(struct snd_sof_dev *sdev)
{
	struct hdac_bus *bus = sof_to_bus(sdev);
	u32 adspis;
	u32 intsts;
	u32 intctl;
	u32 ppsts;
	u8 rirbsts;

	/* read key IRQ stats and config registers */
	adspis = snd_sof_dsp_read(sdev, HDA_DSP_BAR, HDA_DSP_REG_ADSPIS);
	intsts = snd_sof_dsp_read(sdev, HDA_DSP_HDA_BAR, SOF_HDA_INTSTS);
	intctl = snd_sof_dsp_read(sdev, HDA_DSP_HDA_BAR, SOF_HDA_INTCTL);
	ppsts = snd_sof_dsp_read(sdev, HDA_DSP_PP_BAR, SOF_HDA_REG_PP_PPSTS);
	rirbsts = snd_hdac_chip_readb(bus, RIRBSTS);

	dev_err(sdev->dev,
		"error: hda irq intsts 0x%8.8x intlctl 0x%8.8x rirb %2.2x\n",
		intsts, intctl, rirbsts);
	dev_err(sdev->dev,
		"error: dsp irq ppsts 0x%8.8x adspis 0x%8.8x\n",
		ppsts, adspis);
}

void hda_ipc_dump(struct snd_sof_dev *sdev)
{
	u32 hipcie;
	u32 hipct;
	u32 hipcctl;

	hda_ipc_irq_dump(sdev);

	/* read IPC status */
	hipcie = snd_sof_dsp_read(sdev, HDA_DSP_BAR, HDA_DSP_REG_HIPCIE);
	hipct = snd_sof_dsp_read(sdev, HDA_DSP_BAR, HDA_DSP_REG_HIPCT);
	hipcctl = snd_sof_dsp_read(sdev, HDA_DSP_BAR, HDA_DSP_REG_HIPCCTL);

	/* dump the IPC regs */
	/* TODO: parse the raw msg */
	dev_err(sdev->dev,
		"error: host status 0x%8.8x dsp status 0x%8.8x mask 0x%8.8x\n",
		hipcie, hipct, hipcctl);
}

static int hda_init(struct snd_sof_dev *sdev)
{
	struct hda_bus *hbus;
	struct hdac_bus *bus;
	struct pci_dev *pci = to_pci_dev(sdev->dev);
	int ret;

	hbus = sof_to_hbus(sdev);
	bus = sof_to_bus(sdev);

	/* HDA bus init */
	sof_hda_bus_init(bus, &pci->dev);

	bus->use_posbuf = 1;
	bus->bdl_pos_adj = 0;
	bus->sync_write = 1;

	mutex_init(&hbus->prepare_mutex);
	hbus->pci = pci;
	hbus->mixer_assigned = -1;
	hbus->modelname = "sofbus";

	/* initialise hdac bus */
	bus->addr = pci_resource_start(pci, 0);
#if IS_ENABLED(CONFIG_PCI)
	bus->remap_addr = pci_ioremap_bar(pci, 0);
#endif
	if (!bus->remap_addr) {
		dev_err(bus->dev, "error: ioremap error\n");
		return -ENXIO;
	}

	/* HDA base */
	sdev->bar[HDA_DSP_HDA_BAR] = bus->remap_addr;

	/* init i915 and HDMI codecs */
	ret = hda_codec_i915_init(sdev);
	if (ret < 0)
		dev_warn(sdev->dev, "init of i915 and HDMI codec failed\n");

	/* get controller capabilities */
	ret = hda_dsp_ctrl_get_caps(sdev);
	if (ret < 0)
		dev_err(sdev->dev, "error: get caps error\n");

	return ret;
}

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)

static int check_nhlt_dmic(struct snd_sof_dev *sdev)
{
	struct nhlt_acpi_table *nhlt;
	int dmic_num;

	nhlt = intel_nhlt_init(sdev->dev);
	if (nhlt) {
		dmic_num = intel_nhlt_get_dmic_geo(sdev->dev, nhlt);
		intel_nhlt_free(nhlt);
		if (dmic_num == 2 || dmic_num == 4)
			return dmic_num;
	}

	return 0;
}

static const char *fixup_tplg_name(struct snd_sof_dev *sdev,
				   const char *sof_tplg_filename,
				   const char *idisp_str,
				   const char *dmic_str)
{
	const char *tplg_filename = NULL;
	char *filename;
	char *split_ext;

	filename = devm_kstrdup(sdev->dev, sof_tplg_filename, GFP_KERNEL);
	if (!filename)
		return NULL;

	/* this assumes a .tplg extension */
	split_ext = strsep(&filename, ".");
	if (split_ext) {
		tplg_filename = devm_kasprintf(sdev->dev, GFP_KERNEL,
					       "%s%s%s.tplg",
					       split_ext, idisp_str, dmic_str);
		if (!tplg_filename)
			return NULL;
	}
	return tplg_filename;
}

#endif

static int hda_init_caps(struct snd_sof_dev *sdev)
{
	struct hdac_bus *bus = sof_to_bus(sdev);
#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)
	struct hdac_ext_link *hlink;
#endif
	int ret = 0;

	device_disable_async_suspend(bus->dev);

	/* check if dsp is there */
	if (bus->ppcap)
		dev_dbg(sdev->dev, "PP capability, will probe DSP later.\n");

	/* Init HDA controller after i915 init */
	ret = hda_dsp_ctrl_init_chip(sdev, true);
	if (ret < 0) {
		dev_err(bus->dev, "error: init chip failed with ret: %d\n",
			ret);
		return ret;
	}

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)
	if (bus->mlcap)
		snd_hdac_ext_bus_get_ml_capabilities(bus);

	/* create codec instances */
	hda_codec_probe_bus(sdev, hda_codec_use_common_hdmi);

	if (!HDA_IDISP_CODEC(bus->codec_mask))
		hda_codec_i915_display_power(sdev, false);

	/*
	 * we are done probing so decrement link counts
	 */
	list_for_each_entry(hlink, &bus->hlink_list, list)
		snd_hdac_ext_bus_link_put(bus, hlink);
#endif
	return 0;
}

static const struct sof_intel_dsp_desc
	*get_chip_info(struct snd_sof_pdata *pdata)
{
	const struct sof_dev_desc *desc = pdata->desc;
	const struct sof_intel_dsp_desc *chip_info;

	chip_info = desc->chip_info;

	return chip_info;
}

static irqreturn_t hda_dsp_interrupt_handler(int irq, void *context)
{
	struct snd_sof_dev *sdev = context;

	/*
	 * Get global interrupt status. It includes all hardware interrupt
	 * sources in the Intel HD Audio controller.
	 */
	if (snd_sof_dsp_read(sdev, HDA_DSP_HDA_BAR, SOF_HDA_INTSTS) &
	    SOF_HDA_INTSTS_GIS) {

		/* disable GIE interrupt */
		snd_sof_dsp_update_bits(sdev, HDA_DSP_HDA_BAR,
					SOF_HDA_INTCTL,
					SOF_HDA_INT_GLOBAL_EN,
					0);

		return IRQ_WAKE_THREAD;
	}

	return IRQ_NONE;
}

static irqreturn_t hda_dsp_interrupt_thread(int irq, void *context)
{
	struct snd_sof_dev *sdev = context;

	/* deal with streams and controller first */
	if (hda_dsp_check_stream_irq(sdev))
		hda_dsp_stream_threaded_handler(irq, sdev);

	if (hda_dsp_check_ipc_irq(sdev))
		sof_ops(sdev)->irq_thread(irq, sdev);

	/* enable GIE interrupt */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_HDA_BAR,
				SOF_HDA_INTCTL,
				SOF_HDA_INT_GLOBAL_EN,
				SOF_HDA_INT_GLOBAL_EN);

	return IRQ_HANDLED;
}

int hda_dsp_probe(struct snd_sof_dev *sdev)
{
	struct pci_dev *pci = to_pci_dev(sdev->dev);
	struct sof_intel_hda_dev *hdev;
	struct hdac_bus *bus;
	const struct sof_intel_dsp_desc *chip;
	int ret = 0;

	/*
	 * detect DSP by checking class/subclass/prog-id information
	 * class=04 subclass 03 prog-if 00: no DSP, legacy driver is required
	 * class=04 subclass 01 prog-if 00: DSP is present
	 *   (and may be required e.g. for DMIC or SSP support)
	 * class=04 subclass 03 prog-if 80: either of DSP or legacy mode works
	 */
	if (pci->class == 0x040300) {
		dev_err(sdev->dev, "error: the DSP is not enabled on this platform, aborting probe\n");
		return -ENODEV;
	} else if (pci->class != 0x040100 && pci->class != 0x040380) {
		dev_err(sdev->dev, "error: unknown PCI class/subclass/prog-if 0x%06x found, aborting probe\n", pci->class);
		return -ENODEV;
	}
	dev_info(sdev->dev, "DSP detected with PCI class/subclass/prog-if 0x%06x\n", pci->class);

	chip = get_chip_info(sdev->pdata);
	if (!chip) {
		dev_err(sdev->dev, "error: no such device supported, chip id:%x\n",
			pci->device);
		ret = -EIO;
		goto err;
	}

	hdev = devm_kzalloc(sdev->dev, sizeof(*hdev), GFP_KERNEL);
	if (!hdev)
		return -ENOMEM;
	sdev->pdata->hw_pdata = hdev;
	hdev->desc = chip;

	hdev->dmic_dev = platform_device_register_data(sdev->dev, "dmic-codec",
						       PLATFORM_DEVID_NONE,
						       NULL, 0);
	if (IS_ERR(hdev->dmic_dev)) {
		dev_err(sdev->dev, "error: failed to create DMIC device\n");
		return PTR_ERR(hdev->dmic_dev);
	}

	/*
	 * use position update IPC if either it is forced
	 * or we don't have other choice
	 */
#if IS_ENABLED(CONFIG_SND_SOC_SOF_DEBUG_FORCE_IPC_POSITION)
	hdev->no_ipc_position = 0;
#else
	hdev->no_ipc_position = sof_ops(sdev)->pcm_pointer ? 1 : 0;
#endif

	/* set up HDA base */
	bus = sof_to_bus(sdev);
	ret = hda_init(sdev);
	if (ret < 0)
		goto hdac_bus_unmap;

	/* DSP base */
#if IS_ENABLED(CONFIG_PCI)
	sdev->bar[HDA_DSP_BAR] = pci_ioremap_bar(pci, HDA_DSP_BAR);
#endif
	if (!sdev->bar[HDA_DSP_BAR]) {
		dev_err(sdev->dev, "error: ioremap error\n");
		ret = -ENXIO;
		goto hdac_bus_unmap;
	}

	sdev->mmio_bar = HDA_DSP_BAR;
	sdev->mailbox_bar = HDA_DSP_BAR;

	/* allow 64bit DMA address if supported by H/W */
	if (!dma_set_mask(&pci->dev, DMA_BIT_MASK(64))) {
		dev_dbg(sdev->dev, "DMA mask is 64 bit\n");
		dma_set_coherent_mask(&pci->dev, DMA_BIT_MASK(64));
	} else {
		dev_dbg(sdev->dev, "DMA mask is 32 bit\n");
		dma_set_mask(&pci->dev, DMA_BIT_MASK(32));
		dma_set_coherent_mask(&pci->dev, DMA_BIT_MASK(32));
	}

	/* init streams */
	ret = hda_dsp_stream_init(sdev);
	if (ret < 0) {
		dev_err(sdev->dev, "error: failed to init streams\n");
		/*
		 * not all errors are due to memory issues, but trying
		 * to free everything does not harm
		 */
		goto free_streams;
	}

	/*
	 * register our IRQ
	 * let's try to enable msi firstly
	 * if it fails, use legacy interrupt mode
	 * TODO: support msi multiple vectors
	 */
	if (hda_use_msi && pci_alloc_irq_vectors(pci, 1, 1, PCI_IRQ_MSI) > 0) {
		dev_info(sdev->dev, "use msi interrupt mode\n");
		sdev->ipc_irq = pci_irq_vector(pci, 0);
		/* initialised to "false" by kzalloc() */
		sdev->msi_enabled = true;
	}

	if (!sdev->msi_enabled) {
		dev_info(sdev->dev, "use legacy interrupt mode\n");
		/*
		 * in IO-APIC mode, hda->irq and ipc_irq are using the same
		 * irq number of pci->irq
		 */
		sdev->ipc_irq = pci->irq;
	}

	dev_dbg(sdev->dev, "using IPC IRQ %d\n", sdev->ipc_irq);
	ret = request_threaded_irq(sdev->ipc_irq, hda_dsp_interrupt_handler,
				   hda_dsp_interrupt_thread,
				   IRQF_SHARED, "AudioDSP", sdev);
	if (ret < 0) {
		dev_err(sdev->dev, "error: failed to register IPC IRQ %d\n",
			sdev->ipc_irq);
		goto free_irq_vector;
	}

	pci_set_master(pci);
	synchronize_irq(pci->irq);

	/*
	 * clear TCSEL to clear playback on some HD Audio
	 * codecs. PCI TCSEL is defined in the Intel manuals.
	 */
	snd_sof_pci_update_bits(sdev, PCI_TCSEL, 0x07, 0);

	/* init HDA capabilities */
	ret = hda_init_caps(sdev);
	if (ret < 0)
		goto free_ipc_irq;

	/* enable ppcap interrupt */
	hda_dsp_ctrl_ppcap_enable(sdev, true);
	hda_dsp_ctrl_ppcap_int_enable(sdev, true);

	/* initialize waitq for code loading */
	init_waitqueue_head(&sdev->waitq);

	/* set default mailbox offset for FW ready message */
	sdev->dsp_box.offset = HDA_DSP_MBOX_UPLINK_OFFSET;

	INIT_DELAYED_WORK(&hdev->d0i3_work, hda_dsp_d0i3_work);

	return 0;

free_ipc_irq:
	free_irq(sdev->ipc_irq, sdev);
free_irq_vector:
	if (sdev->msi_enabled)
		pci_free_irq_vectors(pci);
free_streams:
	hda_dsp_stream_free(sdev);
/* dsp_unmap: not currently used */
	iounmap(sdev->bar[HDA_DSP_BAR]);
hdac_bus_unmap:
	iounmap(bus->remap_addr);
	hda_codec_i915_exit(sdev);
err:
	return ret;
}

int hda_dsp_remove(struct snd_sof_dev *sdev)
{
	struct sof_intel_hda_dev *hda = sdev->pdata->hw_pdata;
	struct hdac_bus *bus = sof_to_bus(sdev);
	struct pci_dev *pci = to_pci_dev(sdev->dev);
	const struct sof_intel_dsp_desc *chip = hda->desc;

	/* cancel any attempt for DSP D0I3 */
	cancel_delayed_work_sync(&hda->d0i3_work);

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)
	/* codec removal, invoke bus_device_remove */
	snd_hdac_ext_bus_device_remove(bus);
#endif

	if (!IS_ERR_OR_NULL(hda->dmic_dev))
		platform_device_unregister(hda->dmic_dev);

	/* disable DSP IRQ */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_PP_BAR, SOF_HDA_REG_PP_PPCTL,
				SOF_HDA_PPCTL_PIE, 0);

	/* disable CIE and GIE interrupts */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_HDA_BAR, SOF_HDA_INTCTL,
				SOF_HDA_INT_CTRL_EN | SOF_HDA_INT_GLOBAL_EN, 0);

	/* disable cores */
	if (chip)
		hda_dsp_core_reset_power_down(sdev, chip->cores_mask);

	/* disable DSP */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_PP_BAR, SOF_HDA_REG_PP_PPCTL,
				SOF_HDA_PPCTL_GPROCEN, 0);

	free_irq(sdev->ipc_irq, sdev);
	if (sdev->msi_enabled)
		pci_free_irq_vectors(pci);

	hda_dsp_stream_free(sdev);
#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)
	snd_hdac_link_free_all(bus);
#endif

	iounmap(sdev->bar[HDA_DSP_BAR]);
	iounmap(bus->remap_addr);

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)
	snd_hdac_ext_bus_exit(bus);
#endif
	hda_codec_i915_exit(sdev);

	return 0;
}

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)
static int hda_generic_machine_select(struct snd_sof_dev *sdev)
{
	struct hdac_bus *bus = sof_to_bus(sdev);
	struct snd_soc_acpi_mach_params *mach_params;
	struct snd_soc_acpi_mach *hda_mach;
	struct snd_sof_pdata *pdata = sdev->pdata;
	const char *tplg_filename;
	const char *idisp_str;
	const char *dmic_str;
	int dmic_num = 0;
	int codec_num = 0;
	int i;

	/* codec detection */
	if (!bus->codec_mask) {
		dev_info(bus->dev, "no hda codecs found!\n");
	} else {
		dev_info(bus->dev, "hda codecs found, mask %lx\n",
			 bus->codec_mask);

		for (i = 0; i < HDA_MAX_CODECS; i++) {
			if (bus->codec_mask & (1 << i))
				codec_num++;
		}

		/*
		 * If no machine driver is found, then:
		 *
		 * generic hda machine driver can handle:
		 *  - one HDMI codec, and/or
		 *  - one external HDAudio codec
		 */
		if (!pdata->machine && codec_num <= 2) {
			hda_mach = snd_soc_acpi_intel_hda_machines;

			/* topology: use the info from hda_machines */
			pdata->tplg_filename =
				hda_mach->sof_tplg_filename;

			dev_info(bus->dev, "using HDA machine driver %s now\n",
				 hda_mach->drv_name);

			if (codec_num == 1 && HDA_IDISP_CODEC(bus->codec_mask))
				idisp_str = "-idisp";
			else
				idisp_str = "";

			/* first check NHLT for DMICs */
			dmic_num = check_nhlt_dmic(sdev);

			/* allow for module parameter override */
			if (hda_dmic_num != -1)
				dmic_num = hda_dmic_num;

			switch (dmic_num) {
			case 2:
				dmic_str = "-2ch";
				break;
			case 4:
				dmic_str = "-4ch";
				break;
			default:
				dmic_num = 0;
				dmic_str = "";
				break;
			}

			tplg_filename = pdata->tplg_filename;
			tplg_filename = fixup_tplg_name(sdev, tplg_filename,
							idisp_str, dmic_str);
			if (!tplg_filename)
				return -EINVAL;

			pdata->machine = hda_mach;
			pdata->tplg_filename = tplg_filename;
		}
	}

	/* used by hda machine driver to create dai links */
	if (pdata->machine) {
		mach_params = (struct snd_soc_acpi_mach_params *)
			&pdata->machine->mach_params;
		mach_params->codec_mask = bus->codec_mask;
		mach_params->common_hdmi_codec_drv = hda_codec_use_common_hdmi;
		mach_params->dmic_num = dmic_num;
	}

	return 0;
}
#else
static int hda_generic_machine_select(struct snd_sof_dev *sdev)
{
	return 0;
}
#endif

void hda_set_mach_params(const struct snd_soc_acpi_mach *mach,
			 struct device *dev)
{
	struct snd_soc_acpi_mach_params *mach_params;

	mach_params = (struct snd_soc_acpi_mach_params *)&mach->mach_params;
	mach_params->platform = dev_name(dev);
}

void hda_machine_select(struct snd_sof_dev *sdev)
{
	struct snd_sof_pdata *sof_pdata = sdev->pdata;
	const struct sof_dev_desc *desc = sof_pdata->desc;
	struct snd_soc_acpi_mach *mach;

	mach = snd_soc_acpi_find_machine(desc->machines);
	if (mach) {
		sof_pdata->tplg_filename = mach->sof_tplg_filename;
		sof_pdata->machine = mach;
	}

	/*
	 * Choose HDA generic machine driver if mach is NULL.
	 * Otherwise, set certain mach params.
	 */
	hda_generic_machine_select(sdev);

	if (!sof_pdata->machine)
		dev_warn(sdev->dev, "warning: No matching ASoC machine driver found\n");
}

MODULE_LICENSE("Dual BSD/GPL");
MODULE_IMPORT_NS(SND_SOC_SOF_HDA_AUDIO_CODEC);
MODULE_IMPORT_NS(SND_SOC_SOF_HDA_AUDIO_CODEC_I915);
MODULE_IMPORT_NS(SND_SOC_SOF_XTENSA);
