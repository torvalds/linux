// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2021-2022 Intel Corporation. All rights reserved.
//
// Authors: Cezary Rojewski <cezary.rojewski@intel.com>
//          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
//
// Special thanks to:
//    Krzysztof Hejmowski <krzysztof.hejmowski@intel.com>
//    Michal Sienkiewicz <michal.sienkiewicz@intel.com>
//    Filip Proborszcz
//
// for sharing Intel AudioDSP expertise and helping shape the very
// foundation of this driver
//

#include <linux/module.h>
#include <linux/pci.h>
#include <sound/hda_codec.h>
#include <sound/hda_i915.h>
#include <sound/hda_register.h>
#include <sound/hdaudio.h>
#include <sound/hdaudio_ext.h>
#include <sound/intel-dsp-config.h>
#include <sound/intel-nhlt.h>
#include "../../codecs/hda.h"
#include "avs.h"
#include "cldma.h"

static void
avs_hda_update_config_dword(struct hdac_bus *bus, u32 reg, u32 mask, u32 value)
{
	struct pci_dev *pci = to_pci_dev(bus->dev);
	u32 data;

	pci_read_config_dword(pci, reg, &data);
	data &= ~mask;
	data |= (value & mask);
	pci_write_config_dword(pci, reg, data);
}

void avs_hda_power_gating_enable(struct avs_dev *adev, bool enable)
{
	u32 value;

	value = enable ? 0 : AZX_PGCTL_LSRMD_MASK;
	avs_hda_update_config_dword(&adev->base.core, AZX_PCIREG_PGCTL,
				    AZX_PGCTL_LSRMD_MASK, value);
}

static void avs_hdac_clock_gating_enable(struct hdac_bus *bus, bool enable)
{
	u32 value;

	value = enable ? AZX_CGCTL_MISCBDCGE_MASK : 0;
	avs_hda_update_config_dword(bus, AZX_PCIREG_CGCTL, AZX_CGCTL_MISCBDCGE_MASK, value);
}

void avs_hda_clock_gating_enable(struct avs_dev *adev, bool enable)
{
	avs_hdac_clock_gating_enable(&adev->base.core, enable);
}

void avs_hda_l1sen_enable(struct avs_dev *adev, bool enable)
{
	u32 value;

	value = enable ? AZX_VS_EM2_L1SEN : 0;
	snd_hdac_chip_updatel(&adev->base.core, VS_EM2, AZX_VS_EM2_L1SEN, value);
}

static int avs_hdac_bus_init_streams(struct hdac_bus *bus)
{
	unsigned int cp_streams, pb_streams;
	unsigned int gcap;

	gcap = snd_hdac_chip_readw(bus, GCAP);
	cp_streams = (gcap >> 8) & 0x0F;
	pb_streams = (gcap >> 12) & 0x0F;
	bus->num_streams = cp_streams + pb_streams;

	snd_hdac_ext_stream_init_all(bus, 0, cp_streams, SNDRV_PCM_STREAM_CAPTURE);
	snd_hdac_ext_stream_init_all(bus, cp_streams, pb_streams, SNDRV_PCM_STREAM_PLAYBACK);

	return snd_hdac_bus_alloc_stream_pages(bus);
}

static bool avs_hdac_bus_init_chip(struct hdac_bus *bus, bool full_reset)
{
	struct hdac_ext_link *hlink;
	bool ret;

	avs_hdac_clock_gating_enable(bus, false);
	ret = snd_hdac_bus_init_chip(bus, full_reset);

	/* Reset stream-to-link mapping */
	list_for_each_entry(hlink, &bus->hlink_list, list)
		writel(0, hlink->ml_addr + AZX_REG_ML_LOSIDV);

	avs_hdac_clock_gating_enable(bus, true);

	/* Set DUM bit to address incorrect position reporting for capture
	 * streams. In order to do so, CTRL needs to be out of reset state
	 */
	snd_hdac_chip_updatel(bus, VS_EM2, AZX_VS_EM2_DUM, AZX_VS_EM2_DUM);

	return ret;
}

static int probe_codec(struct hdac_bus *bus, int addr)
{
	struct hda_codec *codec;
	unsigned int cmd = (addr << 28) | (AC_NODE_ROOT << 20) |
			   (AC_VERB_PARAMETERS << 8) | AC_PAR_VENDOR_ID;
	unsigned int res = -1;
	int ret;

	mutex_lock(&bus->cmd_mutex);
	snd_hdac_bus_send_cmd(bus, cmd);
	snd_hdac_bus_get_response(bus, addr, &res);
	mutex_unlock(&bus->cmd_mutex);
	if (res == -1)
		return -EIO;

	dev_dbg(bus->dev, "codec #%d probed OK: 0x%x\n", addr, res);

	codec = snd_hda_codec_device_init(to_hda_bus(bus), addr, "hdaudioB%dD%d", bus->idx, addr);
	if (IS_ERR(codec)) {
		dev_err(bus->dev, "init codec failed: %ld\n", PTR_ERR(codec));
		return PTR_ERR(codec);
	}
	/*
	 * Allow avs_core suspend by forcing suspended state on all
	 * of its codec child devices. Component interested in
	 * dealing with hda codecs directly takes pm responsibilities
	 */
	pm_runtime_set_suspended(hda_codec_dev(codec));

	/* configure effectively creates new ASoC component */
	ret = snd_hda_codec_configure(codec);
	if (ret < 0) {
		dev_err(bus->dev, "failed to config codec %d\n", ret);
		return ret;
	}

	return 0;
}

static void avs_hdac_bus_probe_codecs(struct hdac_bus *bus)
{
	int c;

	/* First try to probe all given codec slots */
	for (c = 0; c < HDA_MAX_CODECS; c++) {
		if (!(bus->codec_mask & BIT(c)))
			continue;

		if (!probe_codec(bus, c))
			/* success, continue probing */
			continue;

		/*
		 * Some BIOSen give you wrong codec addresses
		 * that don't exist
		 */
		dev_warn(bus->dev, "Codec #%d probe error; disabling it...\n", c);
		bus->codec_mask &= ~BIT(c);
		/*
		 * More badly, accessing to a non-existing
		 * codec often screws up the controller bus,
		 * and disturbs the further communications.
		 * Thus if an error occurs during probing,
		 * better to reset the controller bus to get
		 * back to the sanity state.
		 */
		snd_hdac_bus_stop_chip(bus);
		avs_hdac_bus_init_chip(bus, true);
	}
}

static void avs_hda_probe_work(struct work_struct *work)
{
	struct avs_dev *adev = container_of(work, struct avs_dev, probe_work);
	struct hdac_bus *bus = &adev->base.core;
	struct hdac_ext_link *hlink;
	int ret;

	pm_runtime_set_active(bus->dev); /* clear runtime_error flag */

	ret = snd_hdac_i915_init(bus);
	if (ret < 0)
		dev_info(bus->dev, "i915 init unsuccessful: %d\n", ret);

	snd_hdac_display_power(bus, HDA_CODEC_IDX_CONTROLLER, true);
	avs_hdac_bus_init_chip(bus, true);
	avs_hdac_bus_probe_codecs(bus);
	snd_hdac_display_power(bus, HDA_CODEC_IDX_CONTROLLER, false);

	/* with all codecs probed, links can be powered down */
	list_for_each_entry(hlink, &bus->hlink_list, list)
		snd_hdac_ext_bus_link_put(bus, hlink);

	snd_hdac_ext_bus_ppcap_enable(bus, true);
	snd_hdac_ext_bus_ppcap_int_enable(bus, true);

	ret = avs_dsp_first_boot_firmware(adev);
	if (ret < 0)
		return;

	adev->nhlt = intel_nhlt_init(adev->dev);
	if (!adev->nhlt)
		dev_info(bus->dev, "platform has no NHLT\n");

	avs_register_all_boards(adev);

	/* configure PM */
	pm_runtime_set_autosuspend_delay(bus->dev, 2000);
	pm_runtime_use_autosuspend(bus->dev);
	pm_runtime_mark_last_busy(bus->dev);
	pm_runtime_put_autosuspend(bus->dev);
	pm_runtime_allow(bus->dev);
}

static void hdac_stream_update_pos(struct hdac_stream *stream, u64 buffer_size)
{
	u64 prev_pos, pos, num_bytes;

	div64_u64_rem(stream->curr_pos, buffer_size, &prev_pos);
	pos = snd_hdac_stream_get_pos_posbuf(stream);

	if (pos < prev_pos)
		num_bytes = (buffer_size - prev_pos) +  pos;
	else
		num_bytes = pos - prev_pos;

	stream->curr_pos += num_bytes;
}

/* called from IRQ */
static void hdac_update_stream(struct hdac_bus *bus, struct hdac_stream *stream)
{
	if (stream->substream) {
		snd_pcm_period_elapsed(stream->substream);
	} else if (stream->cstream) {
		u64 buffer_size = stream->cstream->runtime->buffer_size;

		hdac_stream_update_pos(stream, buffer_size);
		snd_compr_fragment_elapsed(stream->cstream);
	}
}

static irqreturn_t hdac_bus_irq_handler(int irq, void *context)
{
	struct hdac_bus *bus = context;
	u32 mask, int_enable;
	u32 status;
	int ret = IRQ_NONE;

	if (!pm_runtime_active(bus->dev))
		return ret;

	spin_lock(&bus->reg_lock);

	status = snd_hdac_chip_readl(bus, INTSTS);
	if (status == 0 || status == UINT_MAX) {
		spin_unlock(&bus->reg_lock);
		return ret;
	}

	/* clear rirb int */
	status = snd_hdac_chip_readb(bus, RIRBSTS);
	if (status & RIRB_INT_MASK) {
		if (status & RIRB_INT_RESPONSE)
			snd_hdac_bus_update_rirb(bus);
		snd_hdac_chip_writeb(bus, RIRBSTS, RIRB_INT_MASK);
	}

	mask = (0x1 << bus->num_streams) - 1;

	status = snd_hdac_chip_readl(bus, INTSTS);
	status &= mask;
	if (status) {
		/* Disable stream interrupts; Re-enable in bottom half */
		int_enable = snd_hdac_chip_readl(bus, INTCTL);
		snd_hdac_chip_writel(bus, INTCTL, (int_enable & (~mask)));
		ret = IRQ_WAKE_THREAD;
	} else {
		ret = IRQ_HANDLED;
	}

	spin_unlock(&bus->reg_lock);
	return ret;
}

static irqreturn_t hdac_bus_irq_thread(int irq, void *context)
{
	struct hdac_bus *bus = context;
	u32 status;
	u32 int_enable;
	u32 mask;
	unsigned long flags;

	status = snd_hdac_chip_readl(bus, INTSTS);

	snd_hdac_bus_handle_stream_irq(bus, status, hdac_update_stream);

	/* Re-enable stream interrupts */
	mask = (0x1 << bus->num_streams) - 1;
	spin_lock_irqsave(&bus->reg_lock, flags);
	int_enable = snd_hdac_chip_readl(bus, INTCTL);
	snd_hdac_chip_writel(bus, INTCTL, (int_enable | mask));
	spin_unlock_irqrestore(&bus->reg_lock, flags);

	return IRQ_HANDLED;
}

static int avs_hdac_acquire_irq(struct avs_dev *adev)
{
	struct hdac_bus *bus = &adev->base.core;
	struct pci_dev *pci = to_pci_dev(bus->dev);
	int ret;

	/* request one and check that we only got one interrupt */
	ret = pci_alloc_irq_vectors(pci, 1, 1, PCI_IRQ_MSI | PCI_IRQ_LEGACY);
	if (ret != 1) {
		dev_err(adev->dev, "Failed to allocate IRQ vector: %d\n", ret);
		return ret;
	}

	ret = pci_request_irq(pci, 0, hdac_bus_irq_handler, hdac_bus_irq_thread, bus,
			      KBUILD_MODNAME);
	if (ret < 0) {
		dev_err(adev->dev, "Failed to request stream IRQ handler: %d\n", ret);
		goto free_vector;
	}

	ret = pci_request_irq(pci, 0, avs_dsp_irq_handler, avs_dsp_irq_thread, adev,
			      KBUILD_MODNAME);
	if (ret < 0) {
		dev_err(adev->dev, "Failed to request IPC IRQ handler: %d\n", ret);
		goto free_stream_irq;
	}

	return 0;

free_stream_irq:
	pci_free_irq(pci, 0, bus);
free_vector:
	pci_free_irq_vectors(pci);
	return ret;
}

static int avs_bus_init(struct avs_dev *adev, struct pci_dev *pci, const struct pci_device_id *id)
{
	struct hda_bus *bus = &adev->base;
	struct avs_ipc *ipc;
	struct device *dev = &pci->dev;
	int ret;

	ret = snd_hdac_ext_bus_init(&bus->core, dev, NULL, &soc_hda_ext_bus_ops);
	if (ret < 0)
		return ret;

	bus->core.use_posbuf = 1;
	bus->core.bdl_pos_adj = 0;
	bus->core.sync_write = 1;
	bus->pci = pci;
	bus->mixer_assigned = -1;
	mutex_init(&bus->prepare_mutex);

	ipc = devm_kzalloc(dev, sizeof(*ipc), GFP_KERNEL);
	if (!ipc)
		return -ENOMEM;
	ret = avs_ipc_init(ipc, dev);
	if (ret < 0)
		return ret;

	adev->dev = dev;
	adev->spec = (const struct avs_spec *)id->driver_data;
	adev->ipc = ipc;
	adev->hw_cfg.dsp_cores = hweight_long(AVS_MAIN_CORE_MASK);
	INIT_WORK(&adev->probe_work, avs_hda_probe_work);
	INIT_LIST_HEAD(&adev->comp_list);
	INIT_LIST_HEAD(&adev->path_list);
	INIT_LIST_HEAD(&adev->fw_list);
	init_completion(&adev->fw_ready);
	spin_lock_init(&adev->path_list_lock);
	mutex_init(&adev->modres_mutex);
	mutex_init(&adev->comp_list_mutex);
	mutex_init(&adev->path_mutex);

	return 0;
}

static int avs_pci_probe(struct pci_dev *pci, const struct pci_device_id *id)
{
	struct hdac_bus *bus;
	struct avs_dev *adev;
	struct device *dev = &pci->dev;
	int ret;

	ret = snd_intel_dsp_driver_probe(pci);
	if (ret != SND_INTEL_DSP_DRIVER_ANY && ret != SND_INTEL_DSP_DRIVER_AVS)
		return -ENODEV;

	ret = pcim_enable_device(pci);
	if (ret < 0)
		return ret;

	adev = devm_kzalloc(dev, sizeof(*adev), GFP_KERNEL);
	if (!adev)
		return -ENOMEM;
	ret = avs_bus_init(adev, pci, id);
	if (ret < 0) {
		dev_err(dev, "failed to init avs bus: %d\n", ret);
		return ret;
	}

	ret = pci_request_regions(pci, "AVS HDAudio");
	if (ret < 0)
		return ret;

	bus = &adev->base.core;
	bus->addr = pci_resource_start(pci, 0);
	bus->remap_addr = pci_ioremap_bar(pci, 0);
	if (!bus->remap_addr) {
		dev_err(bus->dev, "ioremap error\n");
		ret = -ENXIO;
		goto err_remap_bar0;
	}

	adev->dsp_ba = pci_ioremap_bar(pci, 4);
	if (!adev->dsp_ba) {
		dev_err(bus->dev, "ioremap error\n");
		ret = -ENXIO;
		goto err_remap_bar4;
	}

	snd_hdac_bus_parse_capabilities(bus);
	if (bus->mlcap)
		snd_hdac_ext_bus_get_ml_capabilities(bus);

	if (dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64)))
		dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	dma_set_max_seg_size(dev, UINT_MAX);

	ret = avs_hdac_bus_init_streams(bus);
	if (ret < 0) {
		dev_err(dev, "failed to init streams: %d\n", ret);
		goto err_init_streams;
	}

	ret = avs_hdac_acquire_irq(adev);
	if (ret < 0) {
		dev_err(bus->dev, "failed to acquire irq: %d\n", ret);
		goto err_acquire_irq;
	}

	pci_set_master(pci);
	pci_set_drvdata(pci, bus);
	device_disable_async_suspend(dev);

	schedule_work(&adev->probe_work);

	return 0;

err_acquire_irq:
	snd_hdac_bus_free_stream_pages(bus);
	snd_hdac_ext_stream_free_all(bus);
err_init_streams:
	iounmap(adev->dsp_ba);
err_remap_bar4:
	iounmap(bus->remap_addr);
err_remap_bar0:
	pci_release_regions(pci);
	return ret;
}

static void avs_pci_shutdown(struct pci_dev *pci)
{
	struct hdac_bus *bus = pci_get_drvdata(pci);
	struct avs_dev *adev = hdac_to_avs(bus);

	cancel_work_sync(&adev->probe_work);
	avs_ipc_block(adev->ipc);

	snd_hdac_stop_streams(bus);
	avs_dsp_op(adev, int_control, false);
	snd_hdac_ext_bus_ppcap_int_enable(bus, false);
	snd_hdac_ext_bus_link_power_down_all(bus);

	snd_hdac_bus_stop_chip(bus);
	snd_hdac_display_power(bus, HDA_CODEC_IDX_CONTROLLER, false);

	if (avs_platattr_test(adev, CLDMA))
		pci_free_irq(pci, 0, &code_loader);
	pci_free_irq(pci, 0, adev);
	pci_free_irq(pci, 0, bus);
	pci_free_irq_vectors(pci);
}

static void avs_pci_remove(struct pci_dev *pci)
{
	struct hdac_device *hdev, *save;
	struct hdac_bus *bus = pci_get_drvdata(pci);
	struct avs_dev *adev = hdac_to_avs(bus);

	cancel_work_sync(&adev->probe_work);
	avs_ipc_block(adev->ipc);

	avs_unregister_all_boards(adev);

	if (adev->nhlt)
		intel_nhlt_free(adev->nhlt);

	if (avs_platattr_test(adev, CLDMA))
		hda_cldma_free(&code_loader);

	snd_hdac_stop_streams_and_chip(bus);
	avs_dsp_op(adev, int_control, false);
	snd_hdac_ext_bus_ppcap_int_enable(bus, false);

	/* it is safe to remove all codecs from the system now */
	list_for_each_entry_safe(hdev, save, &bus->codec_list, list)
		snd_hda_codec_unregister(hdac_to_hda_codec(hdev));

	snd_hdac_bus_free_stream_pages(bus);
	snd_hdac_ext_stream_free_all(bus);
	/* reverse ml_capabilities */
	snd_hdac_link_free_all(bus);
	snd_hdac_ext_bus_exit(bus);

	avs_dsp_core_disable(adev, GENMASK(adev->hw_cfg.dsp_cores - 1, 0));
	snd_hdac_ext_bus_ppcap_enable(bus, false);

	/* snd_hdac_stop_streams_and_chip does that already? */
	snd_hdac_bus_stop_chip(bus);
	snd_hdac_display_power(bus, HDA_CODEC_IDX_CONTROLLER, false);
	if (bus->audio_component)
		snd_hdac_i915_exit(bus);

	avs_module_info_free(adev);
	pci_free_irq(pci, 0, adev);
	pci_free_irq(pci, 0, bus);
	pci_free_irq_vectors(pci);
	iounmap(bus->remap_addr);
	iounmap(adev->dsp_ba);
	pci_release_regions(pci);

	/* Firmware is not needed anymore */
	avs_release_firmwares(adev);

	/* pm_runtime_forbid() can rpm_resume() which we do not want */
	pm_runtime_disable(&pci->dev);
	pm_runtime_forbid(&pci->dev);
	pm_runtime_enable(&pci->dev);
	pm_runtime_get_noresume(&pci->dev);
}

static int __maybe_unused avs_suspend_common(struct avs_dev *adev)
{
	struct hdac_bus *bus = &adev->base.core;
	int ret;

	flush_work(&adev->probe_work);

	snd_hdac_ext_bus_link_power_down_all(bus);

	ret = avs_ipc_set_dx(adev, AVS_MAIN_CORE_MASK, false);
	/*
	 * pm_runtime is blocked on DSP failure but system-wide suspend is not.
	 * Do not block entire system from suspending if that's the case.
	 */
	if (ret && ret != -EPERM) {
		dev_err(adev->dev, "set dx failed: %d\n", ret);
		return AVS_IPC_RET(ret);
	}

	avs_ipc_block(adev->ipc);
	avs_dsp_op(adev, int_control, false);
	snd_hdac_ext_bus_ppcap_int_enable(bus, false);

	ret = avs_dsp_core_disable(adev, AVS_MAIN_CORE_MASK);
	if (ret < 0) {
		dev_err(adev->dev, "core_mask %ld disable failed: %d\n", AVS_MAIN_CORE_MASK, ret);
		return ret;
	}

	snd_hdac_ext_bus_ppcap_enable(bus, false);
	/* disable LP SRAM retention */
	avs_hda_power_gating_enable(adev, false);
	snd_hdac_bus_stop_chip(bus);
	/* disable CG when putting controller to reset */
	avs_hdac_clock_gating_enable(bus, false);
	snd_hdac_bus_enter_link_reset(bus);
	avs_hdac_clock_gating_enable(bus, true);

	snd_hdac_display_power(bus, HDA_CODEC_IDX_CONTROLLER, false);

	return 0;
}

static int __maybe_unused avs_resume_common(struct avs_dev *adev, bool purge)
{
	struct hdac_bus *bus = &adev->base.core;
	struct hdac_ext_link *hlink;
	int ret;

	snd_hdac_display_power(bus, HDA_CODEC_IDX_CONTROLLER, true);
	avs_hdac_bus_init_chip(bus, true);

	snd_hdac_ext_bus_ppcap_enable(bus, true);
	snd_hdac_ext_bus_ppcap_int_enable(bus, true);

	ret = avs_dsp_boot_firmware(adev, purge);
	if (ret < 0) {
		dev_err(adev->dev, "firmware boot failed: %d\n", ret);
		return ret;
	}

	/* turn off the links that were off before suspend */
	list_for_each_entry(hlink, &bus->hlink_list, list) {
		if (!hlink->ref_count)
			snd_hdac_ext_bus_link_power_down(hlink);
	}

	/* check dma status and clean up CORB/RIRB buffers */
	if (!bus->cmd_dma_state)
		snd_hdac_bus_stop_cmd_io(bus);

	return 0;
}

static int __maybe_unused avs_suspend(struct device *dev)
{
	return avs_suspend_common(to_avs_dev(dev));
}

static int __maybe_unused avs_resume(struct device *dev)
{
	return avs_resume_common(to_avs_dev(dev), true);
}

static int __maybe_unused avs_runtime_suspend(struct device *dev)
{
	return avs_suspend_common(to_avs_dev(dev));
}

static int __maybe_unused avs_runtime_resume(struct device *dev)
{
	return avs_resume_common(to_avs_dev(dev), true);
}

static const struct dev_pm_ops avs_dev_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(avs_suspend, avs_resume)
	SET_RUNTIME_PM_OPS(avs_runtime_suspend, avs_runtime_resume, NULL)
};

static const struct avs_spec skl_desc = {
	.name = "skl",
	.min_fw_version = {
		.major = 9,
		.minor = 21,
		.hotfix = 0,
		.build = 4732,
	},
	.dsp_ops = &skl_dsp_ops,
	.core_init_mask = 1,
	.attributes = AVS_PLATATTR_CLDMA,
	.sram_base_offset = SKL_ADSP_SRAM_BASE_OFFSET,
	.sram_window_size = SKL_ADSP_SRAM_WINDOW_SIZE,
	.rom_status = SKL_ADSP_SRAM_BASE_OFFSET,
};

static const struct avs_spec apl_desc = {
	.name = "apl",
	.min_fw_version = {
		.major = 9,
		.minor = 22,
		.hotfix = 1,
		.build = 4323,
	},
	.dsp_ops = &apl_dsp_ops,
	.core_init_mask = 3,
	.attributes = AVS_PLATATTR_IMR,
	.sram_base_offset = APL_ADSP_SRAM_BASE_OFFSET,
	.sram_window_size = APL_ADSP_SRAM_WINDOW_SIZE,
	.rom_status = APL_ADSP_SRAM_BASE_OFFSET,
};

static const struct pci_device_id avs_ids[] = {
	{ PCI_VDEVICE(INTEL, 0x9d70), (unsigned long)&skl_desc }, /* SKL */
	{ PCI_VDEVICE(INTEL, 0x9d71), (unsigned long)&skl_desc }, /* KBL */
	{ PCI_VDEVICE(INTEL, 0x5a98), (unsigned long)&apl_desc }, /* APL */
	{ PCI_VDEVICE(INTEL, 0x3198), (unsigned long)&apl_desc }, /* GML */
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, avs_ids);

static struct pci_driver avs_pci_driver = {
	.name = KBUILD_MODNAME,
	.id_table = avs_ids,
	.probe = avs_pci_probe,
	.remove = avs_pci_remove,
	.shutdown = avs_pci_shutdown,
	.driver = {
		.pm = &avs_dev_pm,
	},
};
module_pci_driver(avs_pci_driver);

MODULE_AUTHOR("Cezary Rojewski <cezary.rojewski@intel.com>");
MODULE_AUTHOR("Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>");
MODULE_DESCRIPTION("Intel cAVS sound driver");
MODULE_LICENSE("GPL");
