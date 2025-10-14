// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2021-2022 Intel Corporation
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

#include <linux/acpi.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <acpi/nhlt.h>
#include <sound/hda_codec.h>
#include <sound/hda_i915.h>
#include <sound/hda_register.h>
#include <sound/hdaudio.h>
#include <sound/hdaudio_ext.h>
#include <sound/intel-dsp-config.h>
#include "../../codecs/hda.h"
#include "avs.h"
#include "cldma.h"
#include "debug.h"
#include "messages.h"
#include "pcm.h"

static u32 pgctl_mask = AZX_PGCTL_LSRMD_MASK;
module_param(pgctl_mask, uint, 0444);
MODULE_PARM_DESC(pgctl_mask, "PCI PGCTL policy override");

static u32 cgctl_mask = AZX_CGCTL_MISCBDCGE_MASK;
module_param(cgctl_mask, uint, 0444);
MODULE_PARM_DESC(cgctl_mask, "PCI CGCTL policy override");

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
	u32 value = enable ? 0 : pgctl_mask;

	if (!avs_platattr_test(adev, ACE))
		avs_hda_update_config_dword(&adev->base.core, AZX_PCIREG_PGCTL, pgctl_mask, value);
}

static void avs_hdac_clock_gating_enable(struct hdac_bus *bus, bool enable)
{
	struct avs_dev *adev = hdac_to_avs(bus);
	u32 value = enable ? cgctl_mask : 0;

	if (!avs_platattr_test(adev, ACE))
		avs_hda_update_config_dword(bus, AZX_PCIREG_CGCTL, cgctl_mask, value);
}

void avs_hda_clock_gating_enable(struct avs_dev *adev, bool enable)
{
	avs_hdac_clock_gating_enable(&adev->base.core, enable);
}

void avs_hda_l1sen_enable(struct avs_dev *adev, bool enable)
{
	if (avs_platattr_test(adev, ACE))
		return;
	if (enable) {
		if (atomic_inc_and_test(&adev->l1sen_counter))
			snd_hdac_chip_updatel(&adev->base.core, VS_EM2, AZX_VS_EM2_L1SEN,
					      AZX_VS_EM2_L1SEN);
	} else {
		if (atomic_dec_return(&adev->l1sen_counter) == -1)
			snd_hdac_chip_updatel(&adev->base.core, VS_EM2, AZX_VS_EM2_L1SEN, 0);
	}
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
	struct avs_dev *adev = hdac_to_avs(bus);
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
	if (!avs_platattr_test(adev, ACE))
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
		dev_warn(bus->dev, "failed to config codec #%d: %d\n", addr, ret);
		return ret;
	}

	return 0;
}

static void avs_hdac_bus_probe_codecs(struct hdac_bus *bus)
{
	int ret, c;

	/* First try to probe all given codec slots */
	for (c = 0; c < HDA_MAX_CODECS; c++) {
		if (!(bus->codec_mask & BIT(c)))
			continue;

		ret = probe_codec(bus, c);
		/* Ignore codecs with no supporting driver. */
		if (!ret || ret == -ENODEV)
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

	snd_hdac_display_power(bus, HDA_CODEC_IDX_CONTROLLER, true);
	avs_hdac_bus_init_chip(bus, true);
	avs_hdac_bus_probe_codecs(bus);
	snd_hdac_display_power(bus, HDA_CODEC_IDX_CONTROLLER, false);

	/* with all codecs probed, links can be powered down */
	list_for_each_entry(hlink, &bus->hlink_list, list)
		snd_hdac_ext_bus_link_put(bus, hlink);

	snd_hdac_ext_bus_ppcap_enable(bus, true);
	snd_hdac_ext_bus_ppcap_int_enable(bus, true);
	avs_debugfs_init(adev);

	ret = avs_dsp_first_boot_firmware(adev);
	if (ret < 0)
		return;

	acpi_nhlt_get_gbl_table();

	avs_register_all_boards(adev);

	/* configure PM */
	pm_runtime_set_autosuspend_delay(bus->dev, 2000);
	pm_runtime_use_autosuspend(bus->dev);
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
		avs_period_elapsed(stream->substream);
	} else if (stream->cstream) {
		u64 buffer_size = stream->cstream->runtime->buffer_size;

		hdac_stream_update_pos(stream, buffer_size);
		snd_compr_fragment_elapsed(stream->cstream);
	}
}

static irqreturn_t avs_hda_interrupt(struct hdac_bus *bus)
{
	irqreturn_t ret = IRQ_NONE;
	u32 status;

	status = snd_hdac_chip_readl(bus, INTSTS);
	if (snd_hdac_bus_handle_stream_irq(bus, status, hdac_update_stream))
		ret = IRQ_HANDLED;

	spin_lock_irq(&bus->reg_lock);
	/* Clear RIRB interrupt. */
	status = snd_hdac_chip_readb(bus, RIRBSTS);
	if (status & RIRB_INT_MASK) {
		if (status & RIRB_INT_RESPONSE)
			snd_hdac_bus_update_rirb(bus);
		snd_hdac_chip_writeb(bus, RIRBSTS, RIRB_INT_MASK);
		ret = IRQ_HANDLED;
	}

	spin_unlock_irq(&bus->reg_lock);
	return ret;
}

static irqreturn_t avs_hda_irq_handler(int irq, void *dev_id)
{
	struct hdac_bus *bus = dev_id;
	u32 intsts;

	intsts = snd_hdac_chip_readl(bus, INTSTS);
	if (intsts == UINT_MAX || !(intsts & AZX_INT_GLOBAL_EN))
		return IRQ_NONE;

	/* Mask GIE, unmasked in irq_thread(). */
	snd_hdac_chip_updatel(bus, INTCTL, AZX_INT_GLOBAL_EN, 0);

	return IRQ_WAKE_THREAD;
}

static irqreturn_t avs_hda_irq_thread(int irq, void *dev_id)
{
	struct hdac_bus *bus = dev_id;
	u32 status;

	status = snd_hdac_chip_readl(bus, INTSTS);
	if (status & ~AZX_INT_GLOBAL_EN)
		avs_hda_interrupt(bus);

	/* Unmask GIE, masked in irq_handler(). */
	snd_hdac_chip_updatel(bus, INTCTL, AZX_INT_GLOBAL_EN, AZX_INT_GLOBAL_EN);

	return IRQ_HANDLED;
}

static irqreturn_t avs_dsp_irq_handler(int irq, void *dev_id)
{
	struct avs_dev *adev = dev_id;

	return avs_hda_irq_handler(irq, &adev->base.core);
}

static irqreturn_t avs_dsp_irq_thread(int irq, void *dev_id)
{
	struct avs_dev *adev = dev_id;
	struct hdac_bus *bus = &adev->base.core;
	u32 status;

	status = readl(bus->ppcap + AZX_REG_PP_PPSTS);
	if (status & AZX_PPCTL_PIE)
		avs_dsp_op(adev, dsp_interrupt);

	/* Unmask GIE, masked in irq_handler(). */
	snd_hdac_chip_updatel(bus, INTCTL, AZX_INT_GLOBAL_EN, AZX_INT_GLOBAL_EN);

	return IRQ_HANDLED;
}

static int avs_hdac_acquire_irq(struct avs_dev *adev)
{
	struct hdac_bus *bus = &adev->base.core;
	struct pci_dev *pci = to_pci_dev(bus->dev);
	int ret;

	/* request one and check that we only got one interrupt */
	ret = pci_alloc_irq_vectors(pci, 1, 1, PCI_IRQ_MSI | PCI_IRQ_INTX);
	if (ret != 1) {
		dev_err(adev->dev, "Failed to allocate IRQ vector: %d\n", ret);
		return ret;
	}

	ret = pci_request_irq(pci, 0, avs_hda_irq_handler, avs_hda_irq_thread, bus,
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

	adev->modcfg_buf = devm_kzalloc(dev, AVS_MAILBOX_SIZE, GFP_KERNEL);
	if (!adev->modcfg_buf)
		return -ENOMEM;

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
	switch (ret) {
	case SND_INTEL_DSP_DRIVER_ANY:
	case SND_INTEL_DSP_DRIVER_SST:
	case SND_INTEL_DSP_DRIVER_AVS:
		break;
	default:
		return -ENODEV;
	}

	ret = pcim_enable_device(pci);
	if (ret < 0)
		return ret;

	adev = devm_kzalloc(dev, sizeof(*adev), GFP_KERNEL);
	if (!adev)
		return -ENOMEM;
	bus = &adev->base.core;

	ret = avs_bus_init(adev, pci, id);
	if (ret < 0) {
		dev_err(dev, "failed to init avs bus: %d\n", ret);
		return ret;
	}

	ret = pcim_request_all_regions(pci, "AVS HDAudio");
	if (ret < 0)
		return ret;

	bus->addr = pci_resource_start(pci, 0);
	bus->remap_addr = pci_ioremap_bar(pci, 0);
	if (!bus->remap_addr) {
		dev_err(bus->dev, "ioremap error\n");
		return -ENXIO;
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

	ret = snd_hdac_i915_init(bus);
	if (ret == -EPROBE_DEFER)
		goto err_i915_init;
	else if (ret < 0)
		dev_info(bus->dev, "i915 init unsuccessful: %d\n", ret);

	schedule_work(&adev->probe_work);

	return 0;

err_i915_init:
	pci_free_irq(pci, 0, adev);
	pci_free_irq(pci, 0, bus);
	pci_free_irq_vectors(pci);
	pci_clear_master(pci);
	pci_set_drvdata(pci, NULL);
err_acquire_irq:
	snd_hdac_bus_free_stream_pages(bus);
	snd_hdac_ext_stream_free_all(bus);
err_init_streams:
	iounmap(adev->dsp_ba);
err_remap_bar4:
	iounmap(bus->remap_addr);
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

	acpi_nhlt_put_gbl_table();
	avs_debugfs_exit(adev);

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
	snd_hdac_ext_link_free_all(bus);
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

	/* Firmware is not needed anymore */
	avs_release_firmwares(adev);

	/* pm_runtime_forbid() can rpm_resume() which we do not want */
	pm_runtime_disable(&pci->dev);
	pm_runtime_forbid(&pci->dev);
	pm_runtime_enable(&pci->dev);
	pm_runtime_get_noresume(&pci->dev);
}

static int avs_suspend_standby(struct avs_dev *adev)
{
	struct hdac_bus *bus = &adev->base.core;
	struct pci_dev *pci = adev->base.pci;

	if (bus->cmd_dma_state)
		snd_hdac_bus_stop_cmd_io(bus);

	snd_hdac_ext_bus_link_power_down_all(bus);

	enable_irq_wake(pci->irq);
	pci_save_state(pci);

	return 0;
}

static int avs_suspend_common(struct avs_dev *adev, bool low_power)
{
	struct hdac_bus *bus = &adev->base.core;
	int ret;

	flush_work(&adev->probe_work);
	if (low_power && adev->num_lp_paths)
		return avs_suspend_standby(adev);

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

static int avs_resume_standby(struct avs_dev *adev)
{
	struct hdac_bus *bus = &adev->base.core;
	struct pci_dev *pci = adev->base.pci;

	pci_restore_state(pci);
	disable_irq_wake(pci->irq);

	snd_hdac_ext_bus_link_power_up_all(bus);

	if (bus->cmd_dma_state)
		snd_hdac_bus_init_cmd_io(bus);

	return 0;
}

static int avs_resume_common(struct avs_dev *adev, bool low_power, bool purge)
{
	struct hdac_bus *bus = &adev->base.core;
	int ret;

	if (low_power && adev->num_lp_paths)
		return avs_resume_standby(adev);

	snd_hdac_display_power(bus, HDA_CODEC_IDX_CONTROLLER, true);
	avs_hdac_bus_init_chip(bus, true);

	snd_hdac_ext_bus_ppcap_enable(bus, true);
	snd_hdac_ext_bus_ppcap_int_enable(bus, true);

	ret = avs_dsp_boot_firmware(adev, purge);
	if (ret < 0) {
		dev_err(adev->dev, "firmware boot failed: %d\n", ret);
		return ret;
	}

	return 0;
}

static int avs_suspend(struct device *dev)
{
	return avs_suspend_common(to_avs_dev(dev), true);
}

static int avs_resume(struct device *dev)
{
	return avs_resume_common(to_avs_dev(dev), true, true);
}

static int avs_runtime_suspend(struct device *dev)
{
	return avs_suspend_common(to_avs_dev(dev), true);
}

static int avs_runtime_resume(struct device *dev)
{
	return avs_resume_common(to_avs_dev(dev), true, false);
}

static int avs_freeze(struct device *dev)
{
	return avs_suspend_common(to_avs_dev(dev), false);
}
static int avs_thaw(struct device *dev)
{
	return avs_resume_common(to_avs_dev(dev), false, true);
}

static int avs_poweroff(struct device *dev)
{
	return avs_suspend_common(to_avs_dev(dev), false);
}

static int avs_restore(struct device *dev)
{
	return avs_resume_common(to_avs_dev(dev), false, true);
}

static const struct dev_pm_ops avs_dev_pm = {
	.suspend = avs_suspend,
	.resume = avs_resume,
	.freeze = avs_freeze,
	.thaw = avs_thaw,
	.poweroff = avs_poweroff,
	.restore = avs_restore,
	RUNTIME_PM_OPS(avs_runtime_suspend, avs_runtime_resume, NULL)
};

static const struct avs_sram_spec skl_sram_spec = {
	.base_offset = SKL_ADSP_SRAM_BASE_OFFSET,
	.window_size = SKL_ADSP_SRAM_WINDOW_SIZE,
};

static const struct avs_sram_spec apl_sram_spec = {
	.base_offset = APL_ADSP_SRAM_BASE_OFFSET,
	.window_size = APL_ADSP_SRAM_WINDOW_SIZE,
};

static const struct avs_sram_spec mtl_sram_spec = {
	.base_offset = MTL_ADSP_SRAM_BASE_OFFSET,
	.window_size = MTL_ADSP_SRAM_WINDOW_SIZE,
};

static const struct avs_hipc_spec skl_hipc_spec = {
	.req_offset = SKL_ADSP_REG_HIPCI,
	.req_ext_offset = SKL_ADSP_REG_HIPCIE,
	.req_busy_mask = SKL_ADSP_HIPCI_BUSY,
	.ack_offset = SKL_ADSP_REG_HIPCIE,
	.ack_done_mask = SKL_ADSP_HIPCIE_DONE,
	.rsp_offset = SKL_ADSP_REG_HIPCT,
	.rsp_busy_mask = SKL_ADSP_HIPCT_BUSY,
	.ctl_offset = SKL_ADSP_REG_HIPCCTL,
	.sts_offset = SKL_ADSP_SRAM_BASE_OFFSET,
};

static const struct avs_hipc_spec apl_hipc_spec = {
	.req_offset = SKL_ADSP_REG_HIPCI,
	.req_ext_offset = SKL_ADSP_REG_HIPCIE,
	.req_busy_mask = SKL_ADSP_HIPCI_BUSY,
	.ack_offset = SKL_ADSP_REG_HIPCIE,
	.ack_done_mask = SKL_ADSP_HIPCIE_DONE,
	.rsp_offset = SKL_ADSP_REG_HIPCT,
	.rsp_busy_mask = SKL_ADSP_HIPCT_BUSY,
	.ctl_offset = SKL_ADSP_REG_HIPCCTL,
	.sts_offset = APL_ADSP_SRAM_BASE_OFFSET,
};

static const struct avs_hipc_spec cnl_hipc_spec = {
	.req_offset = CNL_ADSP_REG_HIPCIDR,
	.req_ext_offset = CNL_ADSP_REG_HIPCIDD,
	.req_busy_mask = CNL_ADSP_HIPCIDR_BUSY,
	.ack_offset = CNL_ADSP_REG_HIPCIDA,
	.ack_done_mask = CNL_ADSP_HIPCIDA_DONE,
	.rsp_offset = CNL_ADSP_REG_HIPCTDR,
	.rsp_busy_mask = CNL_ADSP_HIPCTDR_BUSY,
	.ctl_offset = CNL_ADSP_REG_HIPCCTL,
	.sts_offset = APL_ADSP_SRAM_BASE_OFFSET,
};

static const struct avs_hipc_spec lnl_hipc_spec = {
	.req_offset = MTL_REG_HfIPCxIDR,
	.req_ext_offset = MTL_REG_HfIPCxIDD,
	.req_busy_mask = MTL_HfIPCxIDR_BUSY,
	.ack_offset = MTL_REG_HfIPCxIDA,
	.ack_done_mask = MTL_HfIPCxIDA_DONE,
	.rsp_offset = MTL_REG_HfIPCxTDR,
	.rsp_busy_mask = MTL_HfIPCxTDR_BUSY,
	.ctl_offset = MTL_REG_HfIPCxCTL,
	.sts_offset = LNL_REG_HfDFR(0),
};

static const struct avs_spec skl_desc = {
	.name = "skl",
	.min_fw_version = { 9, 21, 0, 4732 },
	.dsp_ops = &avs_skl_dsp_ops,
	.core_init_mask = 1,
	.attributes = AVS_PLATATTR_CLDMA,
	.sram = &skl_sram_spec,
	.hipc = &skl_hipc_spec,
};

static const struct avs_spec apl_desc = {
	.name = "apl",
	.min_fw_version = { 9, 22, 1, 4323 },
	.dsp_ops = &avs_apl_dsp_ops,
	.core_init_mask = 3,
	.attributes = AVS_PLATATTR_IMR,
	.sram = &apl_sram_spec,
	.hipc = &apl_hipc_spec,
};

static const struct avs_spec cnl_desc = {
	.name = "cnl",
	.min_fw_version = { 10, 23, 0, 5314 },
	.dsp_ops = &avs_cnl_dsp_ops,
	.core_init_mask = 1,
	.attributes = AVS_PLATATTR_IMR,
	.sram = &apl_sram_spec,
	.hipc = &cnl_hipc_spec,
};

static const struct avs_spec icl_desc = {
	.name = "icl",
	.min_fw_version = { 10, 23, 0, 5040 },
	.dsp_ops = &avs_icl_dsp_ops,
	.core_init_mask = 1,
	.attributes = AVS_PLATATTR_IMR,
	.sram = &apl_sram_spec,
	.hipc = &cnl_hipc_spec,
};

static const struct avs_spec jsl_desc = {
	.name = "jsl",
	.min_fw_version = { 10, 26, 0, 5872 },
	.dsp_ops = &avs_icl_dsp_ops,
	.core_init_mask = 1,
	.attributes = AVS_PLATATTR_IMR,
	.sram = &apl_sram_spec,
	.hipc = &cnl_hipc_spec,
};

#define AVS_TGL_BASED_SPEC(sname, min)		\
static const struct avs_spec sname##_desc = {	\
	.name = #sname,				\
	.min_fw_version = { 10,	min, 0, 5646 },	\
	.dsp_ops = &avs_tgl_dsp_ops,		\
	.core_init_mask = 1,			\
	.attributes = AVS_PLATATTR_IMR,		\
	.sram = &apl_sram_spec,			\
	.hipc = &cnl_hipc_spec,			\
}

AVS_TGL_BASED_SPEC(lkf, 28);
AVS_TGL_BASED_SPEC(tgl, 29);
AVS_TGL_BASED_SPEC(ehl, 30);
AVS_TGL_BASED_SPEC(adl, 35);
AVS_TGL_BASED_SPEC(adl_n, 35);

static const struct avs_spec fcl_desc = {
	.name = "fcl",
	.min_fw_version = { 0 },
	.dsp_ops = &avs_ptl_dsp_ops,
	.core_init_mask = 1,
	.attributes = AVS_PLATATTR_IMR | AVS_PLATATTR_ACE | AVS_PLATATTR_ALTHDA,
	.sram = &mtl_sram_spec,
	.hipc = &lnl_hipc_spec,
};

static const struct pci_device_id avs_ids[] = {
	{ PCI_DEVICE_DATA(INTEL, HDA_SKL_LP, &skl_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_SKL, &skl_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_KBL_LP, &skl_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_KBL, &skl_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_KBL_H, &skl_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_CML_S, &skl_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_APL, &apl_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_GML, &apl_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_CNL_LP,	&cnl_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_CNL_H,	&cnl_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_CML_LP,	&cnl_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_CML_H,	&cnl_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_RKL_S,	&cnl_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_ICL_LP,	&icl_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_ICL_N,	&icl_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_ICL_H,	&icl_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_JSL_N,	&jsl_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_LKF,	&lkf_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_TGL_LP,	&tgl_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_TGL_H,	&tgl_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_CML_R,	&tgl_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_EHL_0,	&ehl_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_EHL_3,	&ehl_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_ADL_S,	&adl_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_ADL_P,	&adl_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_ADL_PS,	&adl_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_ADL_M,	&adl_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_ADL_PX,	&adl_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_ADL_N,	&adl_n_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_RPL_S,	&adl_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_RPL_P_0,	&adl_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_RPL_P_1,	&adl_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_RPL_M,	&adl_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_RPL_PX,	&adl_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_FCL,	&fcl_desc) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, avs_ids);

static struct pci_driver avs_pci_driver = {
	.name = KBUILD_MODNAME,
	.id_table = avs_ids,
	.probe = avs_pci_probe,
	.remove = avs_pci_remove,
	.shutdown = avs_pci_shutdown,
	.dev_groups = avs_attr_groups,
	.driver = {
		.pm = pm_ptr(&avs_dev_pm),
	},
};
module_pci_driver(avs_pci_driver);

MODULE_AUTHOR("Cezary Rojewski <cezary.rojewski@intel.com>");
MODULE_AUTHOR("Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>");
MODULE_DESCRIPTION("Intel cAVS sound driver");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE("intel/avs/skl/dsp_basefw.bin");
MODULE_FIRMWARE("intel/avs/apl/dsp_basefw.bin");
MODULE_FIRMWARE("intel/avs/cnl/dsp_basefw.bin");
MODULE_FIRMWARE("intel/avs/icl/dsp_basefw.bin");
MODULE_FIRMWARE("intel/avs/jsl/dsp_basefw.bin");
MODULE_FIRMWARE("intel/avs/lkf/dsp_basefw.bin");
MODULE_FIRMWARE("intel/avs/tgl/dsp_basefw.bin");
MODULE_FIRMWARE("intel/avs/ehl/dsp_basefw.bin");
MODULE_FIRMWARE("intel/avs/adl/dsp_basefw.bin");
MODULE_FIRMWARE("intel/avs/adl_n/dsp_basefw.bin");
MODULE_FIRMWARE("intel/fcl/dsp_basefw.bin");
