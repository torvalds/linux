// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation
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

#include <linux/acpi.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_intel.h>
#include <sound/intel-dsp-config.h>
#include <sound/intel-nhlt.h>
#include <sound/soc-acpi-intel-ssp-common.h>
#include <sound/sof.h>
#include <sound/sof/xtensa.h>
#include <sound/hda-mlink.h>
#include "../sof-audio.h"
#include "../sof-pci-dev.h"
#include "../ops.h"
#include "../ipc4-topology.h"
#include "hda.h"

#include <trace/events/sof_intel.h>

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)
#include <sound/soc-acpi-intel-match.h>
#endif

/* platform specific devices */
#include "shim.h"

#if IS_ENABLED(CONFIG_SND_SOC_SOF_INTEL_SOUNDWIRE)

/*
 * The default for SoundWire clock stop quirks is to power gate the IP
 * and do a Bus Reset, this will need to be modified when the DSP
 * needs to remain in D0i3 so that the Master does not lose context
 * and enumeration is not required on clock restart
 */
static int sdw_clock_stop_quirks = SDW_INTEL_CLK_STOP_BUS_RESET;
module_param(sdw_clock_stop_quirks, int, 0444);
MODULE_PARM_DESC(sdw_clock_stop_quirks, "SOF SoundWire clock stop quirks");

static int sdw_params_stream(struct device *dev,
			     struct sdw_intel_stream_params_data *params_data)
{
	struct snd_soc_dai *d = params_data->dai;
	struct snd_soc_dapm_widget *w = snd_soc_dai_get_widget(d, params_data->substream->stream);
	struct snd_sof_dai_config_data data = { 0 };

	data.dai_index = (params_data->link_id << 8) | d->id;
	data.dai_data = params_data->alh_stream_id;
	data.dai_node_id = data.dai_data;

	return hda_dai_config(w, SOF_DAI_CONFIG_FLAGS_HW_PARAMS, &data);
}

static int sdw_params_free(struct device *dev, struct sdw_intel_stream_free_data *free_data)
{
	struct snd_soc_dai *d = free_data->dai;
	struct snd_soc_dapm_widget *w = snd_soc_dai_get_widget(d, free_data->substream->stream);
	struct snd_sof_dev *sdev = widget_to_sdev(w);

	if (sdev->pdata->ipc_type == SOF_IPC_TYPE_4) {
		struct snd_sof_widget *swidget = w->dobj.private;
		struct snd_sof_dai *dai = swidget->private;
		struct sof_ipc4_copier_data *copier_data;
		struct sof_ipc4_copier *ipc4_copier;

		ipc4_copier = dai->private;
		ipc4_copier->dai_index = 0;
		copier_data = &ipc4_copier->data;

		/* clear the node ID */
		copier_data->gtw_cfg.node_id &= ~SOF_IPC4_NODE_INDEX_MASK;
	}

	return 0;
}

struct sdw_intel_ops sdw_callback = {
	.params_stream = sdw_params_stream,
	.free_stream = sdw_params_free,
};

static int sdw_ace2x_params_stream(struct device *dev,
				   struct sdw_intel_stream_params_data *params_data)
{
	return sdw_hda_dai_hw_params(params_data->substream,
				     params_data->hw_params,
				     params_data->dai,
				     params_data->link_id,
				     params_data->alh_stream_id);
}

static int sdw_ace2x_free_stream(struct device *dev,
				 struct sdw_intel_stream_free_data *free_data)
{
	return sdw_hda_dai_hw_free(free_data->substream,
				   free_data->dai,
				   free_data->link_id);
}

static int sdw_ace2x_trigger(struct snd_pcm_substream *substream, int cmd, struct snd_soc_dai *dai)
{
	return sdw_hda_dai_trigger(substream, cmd, dai);
}

static struct sdw_intel_ops sdw_ace2x_callback = {
	.params_stream = sdw_ace2x_params_stream,
	.free_stream = sdw_ace2x_free_stream,
	.trigger = sdw_ace2x_trigger,
};

static int hda_sdw_acpi_scan(struct snd_sof_dev *sdev)
{
	u32 interface_mask = hda_get_interface_mask(sdev);
	struct sof_intel_hda_dev *hdev;
	acpi_handle handle;
	int ret;

	if (!(interface_mask & BIT(SOF_DAI_INTEL_ALH)))
		return -EINVAL;

	handle = ACPI_HANDLE(sdev->dev);

	/* save ACPI info for the probe step */
	hdev = sdev->pdata->hw_pdata;

	ret = sdw_intel_acpi_scan(handle, &hdev->info);
	if (ret < 0)
		return -EINVAL;

	return 0;
}

static int hda_sdw_probe(struct snd_sof_dev *sdev)
{
	const struct sof_intel_dsp_desc *chip;
	struct sof_intel_hda_dev *hdev;
	struct sdw_intel_res res;
	void *sdw;

	hdev = sdev->pdata->hw_pdata;

	memset(&res, 0, sizeof(res));

	chip = get_chip_info(sdev->pdata);
	if (chip->hw_ip_version < SOF_INTEL_ACE_2_0) {
		res.mmio_base = sdev->bar[HDA_DSP_BAR];
		res.hw_ops = &sdw_intel_cnl_hw_ops;
		res.shim_base = hdev->desc->sdw_shim_base;
		res.alh_base = hdev->desc->sdw_alh_base;
		res.ext = false;
		res.ops = &sdw_callback;
	} else {
		/*
		 * retrieve eml_lock needed to protect shared registers
		 * in the HDaudio multi-link areas
		 */
		res.eml_lock = hdac_bus_eml_get_mutex(sof_to_bus(sdev), true,
						      AZX_REG_ML_LEPTR_ID_SDW);
		if (!res.eml_lock)
			return -ENODEV;

		res.mmio_base = sdev->bar[HDA_DSP_HDA_BAR];
		/*
		 * the SHIM and SoundWire register offsets are link-specific
		 * and will be determined when adding auxiliary devices
		 */
		res.hw_ops = &sdw_intel_lnl_hw_ops;
		res.ext = true;
		res.ops = &sdw_ace2x_callback;

	}
	res.irq = sdev->ipc_irq;
	res.handle = hdev->info.handle;
	res.parent = sdev->dev;

	res.dev = sdev->dev;
	res.clock_stop_quirks = sdw_clock_stop_quirks;
	res.hbus = sof_to_bus(sdev);

	/*
	 * ops and arg fields are not populated for now,
	 * they will be needed when the DAI callbacks are
	 * provided
	 */

	/* we could filter links here if needed, e.g for quirks */
	res.count = hdev->info.count;
	res.link_mask = hdev->info.link_mask;

	sdw = sdw_intel_probe(&res);
	if (!sdw) {
		dev_err(sdev->dev, "error: SoundWire probe failed\n");
		return -EINVAL;
	}

	/* save context */
	hdev->sdw = sdw;

	return 0;
}

int hda_sdw_startup(struct snd_sof_dev *sdev)
{
	struct sof_intel_hda_dev *hdev;
	struct snd_sof_pdata *pdata = sdev->pdata;
	int ret;

	hdev = sdev->pdata->hw_pdata;

	if (!hdev->sdw)
		return 0;

	if (pdata->machine && !pdata->machine->mach_params.link_mask)
		return 0;

	ret = hda_sdw_check_lcount(sdev);
	if (ret < 0)
		return ret;

	return sdw_intel_startup(hdev->sdw);
}
EXPORT_SYMBOL_NS(hda_sdw_startup, SND_SOC_SOF_INTEL_HDA_GENERIC);

static int hda_sdw_exit(struct snd_sof_dev *sdev)
{
	struct sof_intel_hda_dev *hdev;

	hdev = sdev->pdata->hw_pdata;

	if (hdev->sdw)
		sdw_intel_exit(hdev->sdw);
	hdev->sdw = NULL;

	hda_sdw_int_enable(sdev, false);

	return 0;
}

bool hda_common_check_sdw_irq(struct snd_sof_dev *sdev)
{
	struct sof_intel_hda_dev *hdev;
	bool ret = false;
	u32 irq_status;

	hdev = sdev->pdata->hw_pdata;

	if (!hdev->sdw)
		return ret;

	/* store status */
	irq_status = snd_sof_dsp_read(sdev, HDA_DSP_BAR, HDA_DSP_REG_ADSPIS2);

	/* invalid message ? */
	if (irq_status == 0xffffffff)
		goto out;

	/* SDW message ? */
	if (irq_status & HDA_DSP_REG_ADSPIS2_SNDW)
		ret = true;

out:
	return ret;
}
EXPORT_SYMBOL_NS(hda_common_check_sdw_irq, SND_SOC_SOF_INTEL_HDA_GENERIC);

static bool hda_dsp_check_sdw_irq(struct snd_sof_dev *sdev)
{
	u32 interface_mask = hda_get_interface_mask(sdev);
	const struct sof_intel_dsp_desc *chip;

	if (!(interface_mask & BIT(SOF_DAI_INTEL_ALH)))
		return false;

	chip = get_chip_info(sdev->pdata);
	if (chip && chip->check_sdw_irq)
		return chip->check_sdw_irq(sdev);

	return false;
}

static irqreturn_t hda_dsp_sdw_thread(int irq, void *context)
{
	return sdw_intel_thread(irq, context);
}

bool hda_sdw_check_wakeen_irq_common(struct snd_sof_dev *sdev)
{
	struct sof_intel_hda_dev *hdev;

	hdev = sdev->pdata->hw_pdata;
	if (hdev->sdw &&
	    snd_sof_dsp_read(sdev, HDA_DSP_BAR,
			     hdev->desc->sdw_shim_base + SDW_SHIM_WAKESTS))
		return true;

	return false;
}
EXPORT_SYMBOL_NS(hda_sdw_check_wakeen_irq_common, SND_SOC_SOF_INTEL_HDA_GENERIC);

static bool hda_sdw_check_wakeen_irq(struct snd_sof_dev *sdev)
{
	u32 interface_mask = hda_get_interface_mask(sdev);
	const struct sof_intel_dsp_desc *chip;

	if (!(interface_mask & BIT(SOF_DAI_INTEL_ALH)))
		return false;

	chip = get_chip_info(sdev->pdata);
	if (chip && chip->check_sdw_wakeen_irq)
		return chip->check_sdw_wakeen_irq(sdev);

	return false;
}

void hda_sdw_process_wakeen_common(struct snd_sof_dev *sdev)
{
	u32 interface_mask = hda_get_interface_mask(sdev);
	struct sof_intel_hda_dev *hdev;

	if (!(interface_mask & BIT(SOF_DAI_INTEL_ALH)))
		return;

	hdev = sdev->pdata->hw_pdata;
	if (!hdev->sdw)
		return;

	sdw_intel_process_wakeen_event(hdev->sdw);
}
EXPORT_SYMBOL_NS(hda_sdw_process_wakeen_common, SND_SOC_SOF_INTEL_HDA_GENERIC);

#else /* IS_ENABLED(CONFIG_SND_SOC_SOF_INTEL_SOUNDWIRE) */
static inline int hda_sdw_acpi_scan(struct snd_sof_dev *sdev)
{
	return 0;
}

static inline int hda_sdw_probe(struct snd_sof_dev *sdev)
{
	return 0;
}

static inline int hda_sdw_exit(struct snd_sof_dev *sdev)
{
	return 0;
}

static inline bool hda_dsp_check_sdw_irq(struct snd_sof_dev *sdev)
{
	return false;
}

static inline irqreturn_t hda_dsp_sdw_thread(int irq, void *context)
{
	return IRQ_HANDLED;
}

static inline bool hda_sdw_check_wakeen_irq(struct snd_sof_dev *sdev)
{
	return false;
}

#endif /* IS_ENABLED(CONFIG_SND_SOC_SOF_INTEL_SOUNDWIRE) */

/* pre fw run operations */
int hda_dsp_pre_fw_run(struct snd_sof_dev *sdev)
{
	/* disable clock gating and power gating */
	return hda_dsp_ctrl_clock_power_gating(sdev, false);
}

/* post fw run operations */
int hda_dsp_post_fw_run(struct snd_sof_dev *sdev)
{
	int ret;

	if (sdev->first_boot) {
		struct sof_intel_hda_dev *hdev = sdev->pdata->hw_pdata;

		ret = hda_sdw_startup(sdev);
		if (ret < 0) {
			dev_err(sdev->dev,
				"error: could not startup SoundWire links\n");
			return ret;
		}

		/* Check if IMR boot is usable */
		if (!sof_debug_check_flag(SOF_DBG_IGNORE_D3_PERSISTENT) &&
		    (sdev->fw_ready.flags & SOF_IPC_INFO_D3_PERSISTENT ||
		     sdev->pdata->ipc_type == SOF_IPC_TYPE_4)) {
			hdev->imrboot_supported = true;
			debugfs_create_bool("skip_imr_boot",
					    0644, sdev->debugfs_root,
					    &hdev->skip_imr_boot);
		}
	}

	hda_sdw_int_enable(sdev, true);

	/* re-enable clock gating and power gating */
	return hda_dsp_ctrl_clock_power_gating(sdev, true);
}
EXPORT_SYMBOL_NS(hda_dsp_post_fw_run, SND_SOC_SOF_INTEL_HDA_GENERIC);

/*
 * Debug
 */

#if IS_ENABLED(CONFIG_SND_SOC_SOF_DEBUG)
static bool hda_use_msi = true;
module_param_named(use_msi, hda_use_msi, bool, 0444);
MODULE_PARM_DESC(use_msi, "SOF HDA use PCI MSI mode");
#else
#define hda_use_msi	(1)
#endif

static char *hda_model;
module_param(hda_model, charp, 0444);
MODULE_PARM_DESC(hda_model, "Use the given HDA board model.");

static int dmic_num_override = -1;
module_param_named(dmic_num, dmic_num_override, int, 0444);
MODULE_PARM_DESC(dmic_num, "SOF HDA DMIC number");

static int mclk_id_override = -1;
module_param_named(mclk_id, mclk_id_override, int, 0444);
MODULE_PARM_DESC(mclk_id, "SOF SSP mclk_id");

static int bt_link_mask_override;
module_param_named(bt_link_mask, bt_link_mask_override, int, 0444);
MODULE_PARM_DESC(bt_link_mask, "SOF BT offload link mask");

static int hda_init(struct snd_sof_dev *sdev)
{
	struct hda_bus *hbus;
	struct hdac_bus *bus;
	struct pci_dev *pci = to_pci_dev(sdev->dev);
	int ret;

	hbus = sof_to_hbus(sdev);
	bus = sof_to_bus(sdev);

	/* HDA bus init */
	sof_hda_bus_init(sdev, &pci->dev);

	if (sof_hda_position_quirk == SOF_HDA_POSITION_QUIRK_USE_DPIB_REGISTERS)
		bus->use_posbuf = 0;
	else
		bus->use_posbuf = 1;
	bus->bdl_pos_adj = 0;
	bus->sync_write = 1;

	mutex_init(&hbus->prepare_mutex);
	hbus->pci = pci;
	hbus->mixer_assigned = -1;
	hbus->modelname = hda_model;

	/* initialise hdac bus */
	bus->addr = pci_resource_start(pci, 0);
	bus->remap_addr = pci_ioremap_bar(pci, 0);
	if (!bus->remap_addr) {
		dev_err(bus->dev, "error: ioremap error\n");
		return -ENXIO;
	}

	/* HDA base */
	sdev->bar[HDA_DSP_HDA_BAR] = bus->remap_addr;

	/* init i915 and HDMI codecs */
	ret = hda_codec_i915_init(sdev);
	if (ret < 0 && ret != -ENODEV) {
		dev_err_probe(sdev->dev, ret, "init of i915 and HDMI codec failed\n");
		goto out;
	}

	/* get controller capabilities */
	ret = hda_dsp_ctrl_get_caps(sdev);
	if (ret < 0) {
		dev_err(sdev->dev, "error: get caps error\n");
		hda_codec_i915_exit(sdev);
	}

out:
	if (ret < 0)
		iounmap(sof_to_bus(sdev)->remap_addr);

	return ret;
}

static int check_dmic_num(struct snd_sof_dev *sdev)
{
	struct sof_intel_hda_dev *hdev = sdev->pdata->hw_pdata;
	struct nhlt_acpi_table *nhlt;
	int dmic_num = 0;

	nhlt = hdev->nhlt;
	if (nhlt)
		dmic_num = intel_nhlt_get_dmic_geo(sdev->dev, nhlt);

	dev_info(sdev->dev, "DMICs detected in NHLT tables: %d\n", dmic_num);

	/* allow for module parameter override */
	if (dmic_num_override != -1) {
		dev_dbg(sdev->dev,
			"overriding DMICs detected in NHLT tables %d by kernel param %d\n",
			dmic_num, dmic_num_override);
		dmic_num = dmic_num_override;
	}

	if (dmic_num < 0 || dmic_num > 4) {
		dev_dbg(sdev->dev, "invalid dmic_number %d\n", dmic_num);
		dmic_num = 0;
	}

	return dmic_num;
}

static int check_nhlt_ssp_mask(struct snd_sof_dev *sdev, u8 device_type)
{
	struct sof_intel_hda_dev *hdev = sdev->pdata->hw_pdata;
	struct nhlt_acpi_table *nhlt;
	int ssp_mask = 0;

	nhlt = hdev->nhlt;
	if (!nhlt)
		return ssp_mask;

	if (intel_nhlt_has_endpoint_type(nhlt, NHLT_LINK_SSP)) {
		ssp_mask = intel_nhlt_ssp_endpoint_mask(nhlt, device_type);
		if (ssp_mask)
			dev_info(sdev->dev, "NHLT device %s(%d) detected, ssp_mask %#x\n",
				 device_type == NHLT_DEVICE_BT ? "BT" : "I2S",
				 device_type, ssp_mask);
	}

	return ssp_mask;
}

static int check_nhlt_ssp_mclk_mask(struct snd_sof_dev *sdev, int ssp_num)
{
	struct sof_intel_hda_dev *hdev = sdev->pdata->hw_pdata;
	struct nhlt_acpi_table *nhlt;

	nhlt = hdev->nhlt;
	if (!nhlt)
		return 0;

	return intel_nhlt_ssp_mclk_mask(nhlt, ssp_num);
}

static int hda_init_caps(struct snd_sof_dev *sdev)
{
	u32 interface_mask = hda_get_interface_mask(sdev);
	struct hdac_bus *bus = sof_to_bus(sdev);
	struct snd_sof_pdata *pdata = sdev->pdata;
	struct sof_intel_hda_dev *hdev = pdata->hw_pdata;
	u32 link_mask;
	int ret = 0;

	/* check if dsp is there */
	if (bus->ppcap)
		dev_dbg(sdev->dev, "PP capability, will probe DSP later.\n");

	/* Init HDA controller after i915 init */
	ret = hda_dsp_ctrl_init_chip(sdev);
	if (ret < 0) {
		dev_err(bus->dev, "error: init chip failed with ret: %d\n",
			ret);
		return ret;
	}

	hda_bus_ml_init(bus);

	/* Skip SoundWire if it is not supported */
	if (!(interface_mask & BIT(SOF_DAI_INTEL_ALH)))
		goto skip_soundwire;

	/* scan SoundWire capabilities exposed by DSDT */
	ret = hda_sdw_acpi_scan(sdev);
	if (ret < 0) {
		dev_dbg(sdev->dev, "skipping SoundWire, not detected with ACPI scan\n");
		goto skip_soundwire;
	}

	link_mask = hdev->info.link_mask;
	if (!link_mask) {
		dev_dbg(sdev->dev, "skipping SoundWire, no links enabled\n");
		goto skip_soundwire;
	}

	/*
	 * probe/allocate SoundWire resources.
	 * The hardware configuration takes place in hda_sdw_startup
	 * after power rails are enabled.
	 * It's entirely possible to have a mix of I2S/DMIC/SoundWire
	 * devices, so we allocate the resources in all cases.
	 */
	ret = hda_sdw_probe(sdev);
	if (ret < 0) {
		dev_err(sdev->dev, "error: SoundWire probe error\n");
		return ret;
	}

skip_soundwire:

	/* create codec instances */
	hda_codec_probe_bus(sdev);

	if (!HDA_IDISP_CODEC(bus->codec_mask))
		hda_codec_i915_display_power(sdev, false);

	hda_bus_ml_put_all(bus);

	return 0;
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
	struct sof_intel_hda_dev *hdev = sdev->pdata->hw_pdata;

	/* deal with streams and controller first */
	if (hda_dsp_check_stream_irq(sdev)) {
		trace_sof_intel_hda_irq(sdev, "stream");
		hda_dsp_stream_threaded_handler(irq, sdev);
	}

	if (hda_check_ipc_irq(sdev)) {
		trace_sof_intel_hda_irq(sdev, "ipc");
		sof_ops(sdev)->irq_thread(irq, sdev);
	}

	if (hda_dsp_check_sdw_irq(sdev)) {
		trace_sof_intel_hda_irq(sdev, "sdw");
		hda_dsp_sdw_thread(irq, hdev->sdw);
	}

	if (hda_sdw_check_wakeen_irq(sdev)) {
		trace_sof_intel_hda_irq(sdev, "wakeen");
		hda_sdw_process_wakeen(sdev);
	}

	hda_codec_check_for_state_change(sdev);

	/* enable GIE interrupt */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_HDA_BAR,
				SOF_HDA_INTCTL,
				SOF_HDA_INT_GLOBAL_EN,
				SOF_HDA_INT_GLOBAL_EN);

	return IRQ_HANDLED;
}

int hda_dsp_probe_early(struct snd_sof_dev *sdev)
{
	struct pci_dev *pci = to_pci_dev(sdev->dev);
	struct sof_intel_hda_dev *hdev;
	const struct sof_intel_dsp_desc *chip;
	int ret = 0;

	if (!sdev->dspless_mode_selected) {
		/*
		 * detect DSP by checking class/subclass/prog-id information
		 * class=04 subclass 03 prog-if 00: no DSP, legacy driver is required
		 * class=04 subclass 01 prog-if 00: DSP is present
		 *   (and may be required e.g. for DMIC or SSP support)
		 * class=04 subclass 03 prog-if 80: either of DSP or legacy mode works
		 */
		if (pci->class == 0x040300) {
			dev_err(sdev->dev, "the DSP is not enabled on this platform, aborting probe\n");
			return -ENODEV;
		} else if (pci->class != 0x040100 && pci->class != 0x040380) {
			dev_err(sdev->dev, "unknown PCI class/subclass/prog-if 0x%06x found, aborting probe\n",
				pci->class);
			return -ENODEV;
		}
		dev_info_once(sdev->dev, "DSP detected with PCI class/subclass/prog-if 0x%06x\n",
			      pci->class);
	}

	chip = get_chip_info(sdev->pdata);
	if (!chip) {
		dev_err(sdev->dev, "error: no such device supported, chip id:%x\n",
			pci->device);
		ret = -EIO;
		goto err;
	}

	sdev->num_cores = chip->cores_num;

	hdev = devm_kzalloc(sdev->dev, sizeof(*hdev), GFP_KERNEL);
	if (!hdev)
		return -ENOMEM;
	sdev->pdata->hw_pdata = hdev;
	hdev->desc = chip;
	ret = hda_init(sdev);

err:
	return ret;
}
EXPORT_SYMBOL_NS(hda_dsp_probe_early, SND_SOC_SOF_INTEL_HDA_GENERIC);

int hda_dsp_probe(struct snd_sof_dev *sdev)
{
	struct pci_dev *pci = to_pci_dev(sdev->dev);
	struct sof_intel_hda_dev *hdev = sdev->pdata->hw_pdata;
	const struct sof_intel_dsp_desc *chip;
	int ret = 0;

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

	if (sdev->dspless_mode_selected)
		hdev->no_ipc_position = 1;

	if (sdev->dspless_mode_selected)
		goto skip_dsp_setup;

	/* DSP base */
	sdev->bar[HDA_DSP_BAR] = pci_ioremap_bar(pci, HDA_DSP_BAR);
	if (!sdev->bar[HDA_DSP_BAR]) {
		dev_err(sdev->dev, "error: ioremap error\n");
		ret = -ENXIO;
		goto hdac_bus_unmap;
	}

	sdev->mmio_bar = HDA_DSP_BAR;
	sdev->mailbox_bar = HDA_DSP_BAR;
skip_dsp_setup:

	/* allow 64bit DMA address if supported by H/W */
	if (dma_set_mask_and_coherent(&pci->dev, DMA_BIT_MASK(64))) {
		dev_dbg(sdev->dev, "DMA mask is 32 bit\n");
		dma_set_mask_and_coherent(&pci->dev, DMA_BIT_MASK(32));
	}
	dma_set_max_seg_size(&pci->dev, UINT_MAX);

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

	if (!sdev->dspless_mode_selected) {
		/* enable ppcap interrupt */
		hda_dsp_ctrl_ppcap_enable(sdev, true);
		hda_dsp_ctrl_ppcap_int_enable(sdev, true);

		/* set default mailbox offset for FW ready message */
		sdev->dsp_box.offset = HDA_DSP_MBOX_UPLINK_OFFSET;

		INIT_DELAYED_WORK(&hdev->d0i3_work, hda_dsp_d0i3_work);
	}

	chip = get_chip_info(sdev->pdata);
	if (chip && chip->hw_ip_version >= SOF_INTEL_ACE_2_0) {
		ret = hda_sdw_startup(sdev);
		if (ret < 0) {
			dev_err(sdev->dev, "could not startup SoundWire links\n");
			goto disable_pp_cap;
		}

		hda_sdw_int_enable(sdev, true);
	}

	init_waitqueue_head(&hdev->waitq);

	hdev->nhlt = intel_nhlt_init(sdev->dev);

	return 0;

disable_pp_cap:
	if (!sdev->dspless_mode_selected) {
		hda_dsp_ctrl_ppcap_int_enable(sdev, false);
		hda_dsp_ctrl_ppcap_enable(sdev, false);
	}
free_ipc_irq:
	free_irq(sdev->ipc_irq, sdev);
free_irq_vector:
	if (sdev->msi_enabled)
		pci_free_irq_vectors(pci);
free_streams:
	hda_dsp_stream_free(sdev);
/* dsp_unmap: not currently used */
	if (!sdev->dspless_mode_selected)
		iounmap(sdev->bar[HDA_DSP_BAR]);
hdac_bus_unmap:
	platform_device_unregister(hdev->dmic_dev);

	return ret;
}
EXPORT_SYMBOL_NS(hda_dsp_probe, SND_SOC_SOF_INTEL_HDA_GENERIC);

void hda_dsp_remove(struct snd_sof_dev *sdev)
{
	struct sof_intel_hda_dev *hda = sdev->pdata->hw_pdata;
	const struct sof_intel_dsp_desc *chip = hda->desc;
	struct pci_dev *pci = to_pci_dev(sdev->dev);
	struct nhlt_acpi_table *nhlt = hda->nhlt;

	if (nhlt)
		intel_nhlt_free(nhlt);

	if (!sdev->dspless_mode_selected)
		/* cancel any attempt for DSP D0I3 */
		cancel_delayed_work_sync(&hda->d0i3_work);

	hda_codec_device_remove(sdev);

	hda_sdw_exit(sdev);

	if (!IS_ERR_OR_NULL(hda->dmic_dev))
		platform_device_unregister(hda->dmic_dev);

	if (!sdev->dspless_mode_selected) {
		/* disable DSP IRQ */
		hda_dsp_ctrl_ppcap_int_enable(sdev, false);
	}

	/* disable CIE and GIE interrupts */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_HDA_BAR, SOF_HDA_INTCTL,
				SOF_HDA_INT_CTRL_EN | SOF_HDA_INT_GLOBAL_EN, 0);

	if (sdev->dspless_mode_selected)
		goto skip_disable_dsp;

	/* no need to check for error as the DSP will be disabled anyway */
	if (chip && chip->power_down_dsp)
		chip->power_down_dsp(sdev);

	/* disable DSP */
	hda_dsp_ctrl_ppcap_enable(sdev, false);

skip_disable_dsp:
	free_irq(sdev->ipc_irq, sdev);
	if (sdev->msi_enabled)
		pci_free_irq_vectors(pci);

	hda_dsp_stream_free(sdev);

	hda_bus_ml_free(sof_to_bus(sdev));

	if (!sdev->dspless_mode_selected)
		iounmap(sdev->bar[HDA_DSP_BAR]);
}
EXPORT_SYMBOL_NS(hda_dsp_remove, SND_SOC_SOF_INTEL_HDA_GENERIC);

void hda_dsp_remove_late(struct snd_sof_dev *sdev)
{
	iounmap(sof_to_bus(sdev)->remap_addr);
	sof_hda_bus_exit(sdev);
	hda_codec_i915_exit(sdev);
}

int hda_power_down_dsp(struct snd_sof_dev *sdev)
{
	struct sof_intel_hda_dev *hda = sdev->pdata->hw_pdata;
	const struct sof_intel_dsp_desc *chip = hda->desc;

	return hda_dsp_core_reset_power_down(sdev, chip->host_managed_cores_mask);
}
EXPORT_SYMBOL_NS(hda_power_down_dsp, SND_SOC_SOF_INTEL_HDA_GENERIC);

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA_AUDIO_CODEC)
static void hda_generic_machine_select(struct snd_sof_dev *sdev,
				       struct snd_soc_acpi_mach **mach)
{
	struct hdac_bus *bus = sof_to_bus(sdev);
	struct snd_soc_acpi_mach_params *mach_params;
	struct snd_soc_acpi_mach *hda_mach;
	struct snd_sof_pdata *pdata = sdev->pdata;
	const char *tplg_filename;
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
		if (!*mach && codec_num <= 2) {
			bool tplg_fixup = false;

			hda_mach = snd_soc_acpi_intel_hda_machines;

			dev_info(bus->dev, "using HDA machine driver %s now\n",
				 hda_mach->drv_name);

			/*
			 * topology: use the info from hda_machines since tplg file name
			 * is not overwritten
			 */
			if (!pdata->tplg_filename)
				tplg_fixup = true;

			if (tplg_fixup &&
			    codec_num == 1 && HDA_IDISP_CODEC(bus->codec_mask)) {
				tplg_filename = devm_kasprintf(sdev->dev, GFP_KERNEL,
							       "%s-idisp",
							       hda_mach->sof_tplg_filename);
				if (!tplg_filename)
					return;

				hda_mach->sof_tplg_filename = tplg_filename;
			}

			if (codec_num == 2 ||
			    (codec_num == 1 && !HDA_IDISP_CODEC(bus->codec_mask))) {
				/*
				 * Prevent SoundWire links from starting when an external
				 * HDaudio codec is used
				 */
				hda_mach->mach_params.link_mask = 0;
			} else {
				/*
				 * Allow SoundWire links to start when no external HDaudio codec
				 * was detected. This will not create a SoundWire card but
				 * will help detect if any SoundWire codec reports as ATTACHED.
				 */
				struct sof_intel_hda_dev *hdev = sdev->pdata->hw_pdata;

				hda_mach->mach_params.link_mask = hdev->info.link_mask;
			}

			*mach = hda_mach;
		}
	}

	/* used by hda machine driver to create dai links */
	if (*mach) {
		mach_params = &(*mach)->mach_params;
		mach_params->codec_mask = bus->codec_mask;
	}
}
#else
static void hda_generic_machine_select(struct snd_sof_dev *sdev,
				       struct snd_soc_acpi_mach **mach)
{
}
#endif

#if IS_ENABLED(CONFIG_SND_SOC_SOF_INTEL_SOUNDWIRE)

static struct snd_soc_acpi_mach *hda_sdw_machine_select(struct snd_sof_dev *sdev)
{
	struct snd_sof_pdata *pdata = sdev->pdata;
	const struct snd_soc_acpi_link_adr *link;
	struct sdw_extended_slave_id *ids;
	struct snd_soc_acpi_mach *mach;
	struct sof_intel_hda_dev *hdev;
	u32 link_mask;
	int i;

	hdev = pdata->hw_pdata;
	link_mask = hdev->info.link_mask;

	if (!link_mask) {
		dev_info(sdev->dev, "SoundWire links not enabled\n");
		return NULL;
	}

	if (!hdev->sdw) {
		dev_dbg(sdev->dev, "SoundWire context not allocated\n");
		return NULL;
	}

	if (!hdev->sdw->num_slaves) {
		dev_warn(sdev->dev, "No SoundWire peripheral detected in ACPI tables\n");
		return NULL;
	}

	/*
	 * Select SoundWire machine driver if needed using the
	 * alternate tables. This case deals with SoundWire-only
	 * machines, for mixed cases with I2C/I2S the detection relies
	 * on the HID list.
	 */
	for (mach = pdata->desc->alt_machines;
	     mach && mach->link_mask; mach++) {
		/*
		 * On some platforms such as Up Extreme all links
		 * are enabled but only one link can be used by
		 * external codec. Instead of exact match of two masks,
		 * first check whether link_mask of mach is subset of
		 * link_mask supported by hw and then go on searching
		 * link_adr
		 */
		if (~link_mask & mach->link_mask)
			continue;

		/* No need to match adr if there is no links defined */
		if (!mach->links)
			break;

		link = mach->links;
		for (i = 0; i < hdev->info.count && link->num_adr;
		     i++, link++) {
			/*
			 * Try next machine if any expected Slaves
			 * are not found on this link.
			 */
			if (!snd_soc_acpi_sdw_link_slaves_found(sdev->dev, link,
								hdev->sdw->ids,
								hdev->sdw->num_slaves))
				break;
		}
		/* Found if all Slaves are checked */
		if (i == hdev->info.count || !link->num_adr)
			break;
	}
	if (mach && mach->link_mask) {
		mach->mach_params.links = mach->links;
		mach->mach_params.link_mask = mach->link_mask;
		mach->mach_params.platform = dev_name(sdev->dev);

		return mach;
	}

	dev_info(sdev->dev, "No SoundWire machine driver found for the ACPI-reported configuration:\n");
	ids = hdev->sdw->ids;
	for (i = 0; i < hdev->sdw->num_slaves; i++)
		dev_info(sdev->dev, "link %d mfg_id 0x%04x part_id 0x%04x version %#x\n",
			 ids[i].link_id, ids[i].id.mfg_id, ids[i].id.part_id, ids[i].id.sdw_version);

	return NULL;
}
#else
static struct snd_soc_acpi_mach *hda_sdw_machine_select(struct snd_sof_dev *sdev)
{
	return NULL;
}
#endif

void hda_set_mach_params(struct snd_soc_acpi_mach *mach,
			 struct snd_sof_dev *sdev)
{
	struct snd_sof_pdata *pdata = sdev->pdata;
	const struct sof_dev_desc *desc = pdata->desc;
	struct snd_soc_acpi_mach_params *mach_params;

	mach_params = &mach->mach_params;
	mach_params->platform = dev_name(sdev->dev);
	if (IS_ENABLED(CONFIG_SND_SOC_SOF_NOCODEC_DEBUG_SUPPORT) &&
	    sof_debug_check_flag(SOF_DBG_FORCE_NOCODEC))
		mach_params->num_dai_drivers = SOF_SKL_NUM_DAIS_NOCODEC;
	else
		mach_params->num_dai_drivers = desc->ops->num_drv;
	mach_params->dai_drivers = desc->ops->drv;
}

static int check_tplg_quirk_mask(struct snd_soc_acpi_mach *mach)
{
	u32 dmic_ssp_quirk;
	u32 codec_amp_name_quirk;

	/*
	 * In current implementation dmic and ssp quirks are designed for es8336
	 * machine driver and could not be mixed with codec name and amp name
	 * quirks.
	 */
	dmic_ssp_quirk = mach->tplg_quirk_mask &
			 (SND_SOC_ACPI_TPLG_INTEL_DMIC_NUMBER | SND_SOC_ACPI_TPLG_INTEL_SSP_NUMBER);
	codec_amp_name_quirk = mach->tplg_quirk_mask &
			 (SND_SOC_ACPI_TPLG_INTEL_AMP_NAME | SND_SOC_ACPI_TPLG_INTEL_CODEC_NAME);

	if (dmic_ssp_quirk && codec_amp_name_quirk)
		return -EINVAL;

	return 0;
}

static char *remove_file_ext(const char *tplg_filename)
{
	char *filename, *tmp;

	filename = kstrdup(tplg_filename, GFP_KERNEL);
	if (!filename)
		return NULL;

	/* remove file extension if exist */
	tmp = filename;
	return strsep(&tmp, ".");
}

struct snd_soc_acpi_mach *hda_machine_select(struct snd_sof_dev *sdev)
{
	u32 interface_mask = hda_get_interface_mask(sdev);
	struct snd_sof_pdata *sof_pdata = sdev->pdata;
	const struct sof_dev_desc *desc = sof_pdata->desc;
	struct hdac_bus *bus = sof_to_bus(sdev);
	struct snd_soc_acpi_mach *mach = NULL;
	enum snd_soc_acpi_intel_codec codec_type, amp_type;
	const char *tplg_filename;
	const char *tplg_suffix;
	bool amp_name_valid;
	bool i2s_mach_found = false;
	bool sdw_mach_found = false;

	/* Try I2S or DMIC if it is supported */
	if (interface_mask & (BIT(SOF_DAI_INTEL_SSP) | BIT(SOF_DAI_INTEL_DMIC))) {
		mach = snd_soc_acpi_find_machine(desc->machines);
		if (mach)
			i2s_mach_found = true;
	}

	/*
	 * If I2S fails and no external HDaudio codec is detected,
	 * try SoundWire if it is supported
	 */
	if (!mach && !HDA_EXT_CODEC(bus->codec_mask) &&
	    (interface_mask & BIT(SOF_DAI_INTEL_ALH))) {
		mach = hda_sdw_machine_select(sdev);
		if (mach)
			sdw_mach_found = true;
	}

	/*
	 * Choose HDA generic machine driver if mach is NULL.
	 * Otherwise, set certain mach params.
	 */
	hda_generic_machine_select(sdev, &mach);
	if (!mach) {
		dev_warn(sdev->dev, "warning: No matching ASoC machine driver found\n");
		return NULL;
	}

	/* report BT offload link mask to machine driver */
	mach->mach_params.bt_link_mask = check_nhlt_ssp_mask(sdev, NHLT_DEVICE_BT);

	dev_info(sdev->dev, "BT link detected in NHLT tables: %#x\n",
		 mach->mach_params.bt_link_mask);

	/* allow for module parameter override */
	if (bt_link_mask_override) {
		dev_dbg(sdev->dev, "overriding BT link detected in NHLT tables %#x by kernel param %#x\n",
			mach->mach_params.bt_link_mask, bt_link_mask_override);
		mach->mach_params.bt_link_mask = bt_link_mask_override;
	}

	if (hweight_long(mach->mach_params.bt_link_mask) > 1) {
		dev_warn(sdev->dev, "invalid BT link mask %#x found, reset the mask\n",
			mach->mach_params.bt_link_mask);
		mach->mach_params.bt_link_mask = 0;
	}

	/*
	 * Fixup tplg file name by appending dmic num, ssp num, codec/amplifier
	 * name string if quirk flag is set.
	 */
	if (mach) {
		bool tplg_fixup = false;
		bool dmic_fixup = false;

		/*
		 * If tplg file name is overridden, use it instead of
		 * the one set in mach table
		 */
		if (!sof_pdata->tplg_filename) {
			/* remove file extension if it exists */
			tplg_filename = remove_file_ext(mach->sof_tplg_filename);
			if (!tplg_filename)
				return NULL;

			sof_pdata->tplg_filename = tplg_filename;
			tplg_fixup = true;
		}

		/*
		 * Checking quirk mask integrity; some quirk flags could not be
		 * set concurrently.
		 */
		if (tplg_fixup &&
		    check_tplg_quirk_mask(mach)) {
			dev_err(sdev->dev, "Invalid tplg quirk mask 0x%x\n",
				mach->tplg_quirk_mask);
			return NULL;
		}

		/* report to machine driver if any DMICs are found */
		mach->mach_params.dmic_num = check_dmic_num(sdev);

		if (sdw_mach_found) {
			/*
			 * DMICs use up to 4 pins and are typically pin-muxed with SoundWire
			 * link 2 and 3, or link 1 and 2, thus we only try to enable dmics
			 * if all conditions are true:
			 * a) 2 or fewer links are used by SoundWire
			 * b) the NHLT table reports the presence of microphones
			 */
			if (hweight_long(mach->link_mask) <= 2)
				dmic_fixup = true;
			else
				mach->mach_params.dmic_num = 0;
		} else {
			if (mach->tplg_quirk_mask & SND_SOC_ACPI_TPLG_INTEL_DMIC_NUMBER)
				dmic_fixup = true;
		}

		if (tplg_fixup &&
		    dmic_fixup &&
		    mach->mach_params.dmic_num) {
			tplg_filename = devm_kasprintf(sdev->dev, GFP_KERNEL,
						       "%s%s%d%s",
						       sof_pdata->tplg_filename,
						       i2s_mach_found ? "-dmic" : "-",
						       mach->mach_params.dmic_num,
						       "ch");
			if (!tplg_filename)
				return NULL;

			sof_pdata->tplg_filename = tplg_filename;
		}

		if (mach->link_mask) {
			mach->mach_params.links = mach->links;
			mach->mach_params.link_mask = mach->link_mask;
		}

		/* report SSP link mask to machine driver */
		mach->mach_params.i2s_link_mask = check_nhlt_ssp_mask(sdev, NHLT_DEVICE_I2S);

		if (tplg_fixup &&
		    mach->tplg_quirk_mask & SND_SOC_ACPI_TPLG_INTEL_SSP_NUMBER &&
		    mach->mach_params.i2s_link_mask) {
			const struct sof_intel_dsp_desc *chip = get_chip_info(sdev->pdata);
			int ssp_num;
			int mclk_mask;

			if (hweight_long(mach->mach_params.i2s_link_mask) > 1 &&
			    !(mach->tplg_quirk_mask & SND_SOC_ACPI_TPLG_INTEL_SSP_MSB))
				dev_warn(sdev->dev, "More than one SSP exposed by NHLT, choosing MSB\n");

			/* fls returns 1-based results, SSPs indices are 0-based */
			ssp_num = fls(mach->mach_params.i2s_link_mask) - 1;

			if (ssp_num >= chip->ssp_count) {
				dev_err(sdev->dev, "Invalid SSP %d, max on this platform is %d\n",
					ssp_num, chip->ssp_count);
				return NULL;
			}

			tplg_filename = devm_kasprintf(sdev->dev, GFP_KERNEL,
						       "%s%s%d",
						       sof_pdata->tplg_filename,
						       "-ssp",
						       ssp_num);
			if (!tplg_filename)
				return NULL;

			sof_pdata->tplg_filename = tplg_filename;

			mclk_mask = check_nhlt_ssp_mclk_mask(sdev, ssp_num);

			if (mclk_mask < 0) {
				dev_err(sdev->dev, "Invalid MCLK configuration\n");
				return NULL;
			}

			dev_dbg(sdev->dev, "MCLK mask %#x found in NHLT\n", mclk_mask);

			if (mclk_mask) {
				dev_info(sdev->dev, "Overriding topology with MCLK mask %#x from NHLT\n", mclk_mask);
				sdev->mclk_id_override = true;
				sdev->mclk_id_quirk = (mclk_mask & BIT(0)) ? 0 : 1;
			}
		}

		amp_type = snd_soc_acpi_intel_detect_amp_type(sdev->dev);
		codec_type = snd_soc_acpi_intel_detect_codec_type(sdev->dev);
		amp_name_valid = amp_type != CODEC_NONE && amp_type != codec_type;

		if (tplg_fixup && amp_name_valid &&
		    mach->tplg_quirk_mask & SND_SOC_ACPI_TPLG_INTEL_AMP_NAME) {
			tplg_suffix = snd_soc_acpi_intel_get_amp_tplg_suffix(amp_type);
			if (!tplg_suffix) {
				dev_err(sdev->dev, "no tplg suffix found, amp %d\n",
					amp_type);
				return NULL;
			}

			tplg_filename = devm_kasprintf(sdev->dev, GFP_KERNEL,
						       "%s-%s",
						       sof_pdata->tplg_filename,
						       tplg_suffix);
			if (!tplg_filename)
				return NULL;

			sof_pdata->tplg_filename = tplg_filename;
		}


		if (tplg_fixup &&
		    mach->tplg_quirk_mask & SND_SOC_ACPI_TPLG_INTEL_CODEC_NAME &&
		    codec_type != CODEC_NONE) {
			tplg_suffix = snd_soc_acpi_intel_get_codec_tplg_suffix(codec_type);
			if (!tplg_suffix) {
				dev_err(sdev->dev, "no tplg suffix found, codec %d\n",
					codec_type);
				return NULL;
			}

			tplg_filename = devm_kasprintf(sdev->dev, GFP_KERNEL,
						       "%s-%s",
						       sof_pdata->tplg_filename,
						       tplg_suffix);
			if (!tplg_filename)
				return NULL;

			sof_pdata->tplg_filename = tplg_filename;
		}

		if (tplg_fixup) {
			tplg_filename = devm_kasprintf(sdev->dev, GFP_KERNEL,
						       "%s%s",
						       sof_pdata->tplg_filename,
						       ".tplg");
			if (!tplg_filename)
				return NULL;

			sof_pdata->tplg_filename = tplg_filename;
		}

		/* check if mclk_id should be modified from topology defaults */
		if (mclk_id_override >= 0) {
			dev_info(sdev->dev, "Overriding topology with MCLK %d from kernel_parameter\n", mclk_id_override);
			sdev->mclk_id_override = true;
			sdev->mclk_id_quirk = mclk_id_override;
		}
	}

	return mach;
}

int hda_pci_intel_probe(struct pci_dev *pci, const struct pci_device_id *pci_id)
{
	int ret;

	ret = snd_intel_dsp_driver_probe(pci);
	if (ret != SND_INTEL_DSP_DRIVER_ANY && ret != SND_INTEL_DSP_DRIVER_SOF) {
		dev_dbg(&pci->dev, "SOF PCI driver not selected, aborting probe\n");
		return -ENODEV;
	}

	return sof_pci_probe(pci, pci_id);
}
EXPORT_SYMBOL_NS(hda_pci_intel_probe, SND_SOC_SOF_INTEL_HDA_GENERIC);

int hda_register_clients(struct snd_sof_dev *sdev)
{
	return hda_probes_register(sdev);
}

void hda_unregister_clients(struct snd_sof_dev *sdev)
{
	hda_probes_unregister(sdev);
}

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("SOF support for HDaudio platforms");
MODULE_IMPORT_NS(SND_SOC_SOF_PCI_DEV);
MODULE_IMPORT_NS(SND_SOC_SOF_HDA_AUDIO_CODEC);
MODULE_IMPORT_NS(SND_SOC_SOF_HDA_AUDIO_CODEC_I915);
MODULE_IMPORT_NS(SND_SOC_SOF_XTENSA);
MODULE_IMPORT_NS(SND_INTEL_SOUNDWIRE_ACPI);
MODULE_IMPORT_NS(SOUNDWIRE_INTEL_INIT);
MODULE_IMPORT_NS(SOUNDWIRE_INTEL);
MODULE_IMPORT_NS(SND_SOC_SOF_HDA_MLINK);
MODULE_IMPORT_NS(SND_SOC_SOF_INTEL_HDA_COMMON);
MODULE_IMPORT_NS(SND_SOC_ACPI_INTEL_MATCH);
