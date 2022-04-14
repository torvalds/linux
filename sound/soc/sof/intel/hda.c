// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
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

#include <linux/acpi.h>
#include <linux/module.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_intel.h>
#include <sound/intel-dsp-config.h>
#include <sound/intel-nhlt.h>
#include <sound/sof.h>
#include <sound/sof/xtensa.h>
#include "../sof-audio.h"
#include "../sof-pci-dev.h"
#include "../ops.h"
#include "hda.h"

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)
#include <sound/soc-acpi-intel-match.h>
#endif

/* platform specific devices */
#include "shim.h"

#define EXCEPT_MAX_HDR_SIZE	0x400
#define HDA_EXT_ROM_STATUS_SIZE 8

int hda_ctrl_dai_widget_setup(struct snd_soc_dapm_widget *w, unsigned int quirk_flags,
			      struct snd_sof_dai_config_data *data)
{
	struct snd_sof_widget *swidget = w->dobj.private;
	struct snd_soc_component *component = swidget->scomp;
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	const struct sof_ipc_tplg_ops *tplg_ops = sdev->ipc->ops->tplg;
	struct snd_sof_dai *sof_dai = swidget->private;
	int ret;

	if (!sof_dai) {
		dev_err(sdev->dev, "%s: No DAI for DAI widget %s\n", __func__, w->name);
		return -EINVAL;
	}

	if (tplg_ops->dai_config) {
		unsigned int flags;

		/* set HW_PARAMS flag along with quirks */
		flags = SOF_DAI_CONFIG_FLAGS_HW_PARAMS |
			quirk_flags << SOF_DAI_CONFIG_FLAGS_QUIRK_SHIFT;

		ret = tplg_ops->dai_config(sdev, swidget, flags, data);
		if (ret < 0) {
			dev_err(sdev->dev, "%s: DAI config failed for widget %s\n", __func__,
				w->name);
			return ret;
		}
	}

	return 0;
}

int hda_ctrl_dai_widget_free(struct snd_soc_dapm_widget *w, unsigned int quirk_flags,
			     struct snd_sof_dai_config_data *data)
{
	struct snd_sof_widget *swidget = w->dobj.private;
	struct snd_soc_component *component = swidget->scomp;
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	const struct sof_ipc_tplg_ops *tplg_ops = sdev->ipc->ops->tplg;
	struct snd_sof_dai *sof_dai = swidget->private;

	if (!sof_dai) {
		dev_err(sdev->dev, "%s: No DAI for BE DAI widget %s\n", __func__, w->name);
		return -EINVAL;
	}

	if (tplg_ops->dai_config) {
		unsigned int flags;
		int ret;

		/* set HW_FREE flag along with any quirks */
		flags = SOF_DAI_CONFIG_FLAGS_HW_FREE |
			quirk_flags << SOF_DAI_CONFIG_FLAGS_QUIRK_SHIFT;

		ret = tplg_ops->dai_config(sdev, swidget, flags, data);
		if (ret < 0)
			dev_err(sdev->dev, "%s: DAI config failed for widget '%s'\n", __func__,
				w->name);
	}

	return 0;
}

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
	struct snd_sof_dai_config_data data;
	struct snd_soc_dapm_widget *w;

	w = snd_soc_dai_get_widget(d, params_data->stream);
	data.dai_index = (params_data->link_id << 8) | d->id;
	data.dai_data = params_data->alh_stream_id;

	return hda_ctrl_dai_widget_setup(w, SOF_DAI_CONFIG_FLAGS_NONE, &data);
}

static int sdw_free_stream(struct device *dev,
			   struct sdw_intel_stream_free_data *free_data)
{
	struct snd_soc_dai *d = free_data->dai;
	struct snd_sof_dai_config_data data;
	struct snd_soc_dapm_widget *w;

	w = snd_soc_dai_get_widget(d, free_data->stream);
	data.dai_index = (free_data->link_id << 8) | d->id;

	/* send invalid stream_id */
	data.dai_data = 0xFFFF;

	return hda_ctrl_dai_widget_free(w, SOF_DAI_CONFIG_FLAGS_NONE, &data);
}

static const struct sdw_intel_ops sdw_callback = {
	.params_stream = sdw_params_stream,
	.free_stream = sdw_free_stream,
};

void hda_sdw_int_enable(struct snd_sof_dev *sdev, bool enable)
{
	sdw_intel_enable_irq(sdev->bar[HDA_DSP_BAR], enable);
}

static int hda_sdw_acpi_scan(struct snd_sof_dev *sdev)
{
	struct sof_intel_hda_dev *hdev;
	acpi_handle handle;
	int ret;

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
	struct sof_intel_hda_dev *hdev;
	struct sdw_intel_res res;
	void *sdw;

	hdev = sdev->pdata->hw_pdata;

	memset(&res, 0, sizeof(res));

	res.mmio_base = sdev->bar[HDA_DSP_BAR];
	res.shim_base = hdev->desc->sdw_shim_base;
	res.alh_base = hdev->desc->sdw_alh_base;
	res.irq = sdev->ipc_irq;
	res.handle = hdev->info.handle;
	res.parent = sdev->dev;
	res.ops = &sdw_callback;
	res.dev = sdev->dev;
	res.clock_stop_quirks = sdw_clock_stop_quirks;

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

	hdev = sdev->pdata->hw_pdata;

	if (!hdev->sdw)
		return 0;

	if (pdata->machine && !pdata->machine->mach_params.link_mask)
		return 0;

	return sdw_intel_startup(hdev->sdw);
}

static int hda_sdw_exit(struct snd_sof_dev *sdev)
{
	struct sof_intel_hda_dev *hdev;

	hdev = sdev->pdata->hw_pdata;

	hda_sdw_int_enable(sdev, false);

	if (hdev->sdw)
		sdw_intel_exit(hdev->sdw);
	hdev->sdw = NULL;

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

static bool hda_dsp_check_sdw_irq(struct snd_sof_dev *sdev)
{
	const struct sof_intel_dsp_desc *chip;

	chip = get_chip_info(sdev->pdata);
	if (chip && chip->check_sdw_irq)
		return chip->check_sdw_irq(sdev);

	return false;
}

static irqreturn_t hda_dsp_sdw_thread(int irq, void *context)
{
	return sdw_intel_thread(irq, context);
}

static bool hda_sdw_check_wakeen_irq(struct snd_sof_dev *sdev)
{
	struct sof_intel_hda_dev *hdev;

	hdev = sdev->pdata->hw_pdata;
	if (hdev->sdw &&
	    snd_sof_dsp_read(sdev, HDA_DSP_BAR,
			     hdev->desc->sdw_shim_base + SDW_SHIM_WAKESTS))
		return true;

	return false;
}

void hda_sdw_process_wakeen(struct snd_sof_dev *sdev)
{
	struct sof_intel_hda_dev *hdev;

	hdev = sdev->pdata->hw_pdata;
	if (!hdev->sdw)
		return;

	sdw_intel_process_wakeen_event(hdev->sdw);
}

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

/*
 * Debug
 */

struct hda_dsp_msg_code {
	u32 code;
	const char *msg;
};

#if IS_ENABLED(CONFIG_SND_SOC_SOF_DEBUG)
static bool hda_use_msi = true;
module_param_named(use_msi, hda_use_msi, bool, 0444);
MODULE_PARM_DESC(use_msi, "SOF HDA use PCI MSI mode");
#else
#define hda_use_msi	(1)
#endif

int sof_hda_position_quirk = SOF_HDA_POSITION_QUIRK_USE_DPIB_REGISTERS;
module_param_named(position_quirk, sof_hda_position_quirk, int, 0444);
MODULE_PARM_DESC(position_quirk, "SOF HDaudio position quirk");

static char *hda_model;
module_param(hda_model, charp, 0444);
MODULE_PARM_DESC(hda_model, "Use the given HDA board model.");

static int dmic_num_override = -1;
module_param_named(dmic_num, dmic_num_override, int, 0444);
MODULE_PARM_DESC(dmic_num, "SOF HDA DMIC number");

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)
static bool hda_codec_use_common_hdmi = IS_ENABLED(CONFIG_SND_HDA_CODEC_HDMI);
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

static void hda_dsp_get_status(struct snd_sof_dev *sdev, const char *level)
{
	u32 status;
	int i;

	status = snd_sof_dsp_read(sdev, HDA_DSP_BAR,
				  HDA_DSP_SRAM_REG_ROM_STATUS);

	for (i = 0; i < ARRAY_SIZE(hda_dsp_rom_msg); i++) {
		if (status == hda_dsp_rom_msg[i].code) {
			dev_printk(level, sdev->dev, "%s - code %8.8x\n",
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

/* dump the first 8 dwords representing the extended ROM status */
static void hda_dsp_dump_ext_rom_status(struct snd_sof_dev *sdev, const char *level,
					u32 flags)
{
	char msg[128];
	int len = 0;
	u32 value;
	int i;

	for (i = 0; i < HDA_EXT_ROM_STATUS_SIZE; i++) {
		value = snd_sof_dsp_read(sdev, HDA_DSP_BAR, HDA_DSP_SRAM_REG_ROM_STATUS + i * 0x4);
		len += snprintf(msg + len, sizeof(msg) - len, " 0x%x", value);
	}

	dev_printk(level, sdev->dev, "extended rom status: %s", msg);

}

void hda_dsp_dump(struct snd_sof_dev *sdev, u32 flags)
{
	char *level = (flags & SOF_DBG_DUMP_OPTIONAL) ? KERN_DEBUG : KERN_ERR;
	struct sof_ipc_dsp_oops_xtensa xoops;
	struct sof_ipc_panic_info panic_info;
	u32 stack[HDA_DSP_STACK_DUMP_SIZE];

	/* print ROM/FW status */
	hda_dsp_get_status(sdev, level);

	if (flags & SOF_DBG_DUMP_REGS) {
		u32 status = snd_sof_dsp_read(sdev, HDA_DSP_BAR, HDA_DSP_SRAM_REG_FW_STATUS);
		u32 panic = snd_sof_dsp_read(sdev, HDA_DSP_BAR, HDA_DSP_SRAM_REG_FW_TRACEP);

		hda_dsp_get_registers(sdev, &xoops, &panic_info, stack,
				      HDA_DSP_STACK_DUMP_SIZE);
		sof_print_oops_and_stack(sdev, level, status, panic, &xoops,
					 &panic_info, stack, HDA_DSP_STACK_DUMP_SIZE);
	} else {
		hda_dsp_dump_ext_rom_status(sdev, level, flags);
	}
}

static bool hda_check_ipc_irq(struct snd_sof_dev *sdev)
{
	const struct sof_intel_dsp_desc *chip;

	chip = get_chip_info(sdev->pdata);
	if (chip && chip->check_ipc_irq)
		return chip->check_ipc_irq(sdev);

	return false;
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

	dev_err(sdev->dev, "hda irq intsts 0x%8.8x intlctl 0x%8.8x rirb %2.2x\n",
		intsts, intctl, rirbsts);
	dev_err(sdev->dev, "dsp irq ppsts 0x%8.8x adspis 0x%8.8x\n", ppsts, adspis);
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
	dev_err(sdev->dev, "host status 0x%8.8x dsp status 0x%8.8x mask 0x%8.8x\n",
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
	if (ret < 0)
		dev_warn(sdev->dev, "init of i915 and HDMI codec failed\n");

	/* get controller capabilities */
	ret = hda_dsp_ctrl_get_caps(sdev);
	if (ret < 0)
		dev_err(sdev->dev, "error: get caps error\n");

	return ret;
}

static int check_dmic_num(struct snd_sof_dev *sdev)
{
	struct nhlt_acpi_table *nhlt;
	int dmic_num = 0;

	nhlt = intel_nhlt_init(sdev->dev);
	if (nhlt) {
		dmic_num = intel_nhlt_get_dmic_geo(sdev->dev, nhlt);
		intel_nhlt_free(nhlt);
	}

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

static int check_nhlt_ssp_mask(struct snd_sof_dev *sdev)
{
	struct nhlt_acpi_table *nhlt;
	int ssp_mask = 0;

	nhlt = intel_nhlt_init(sdev->dev);
	if (!nhlt)
		return ssp_mask;

	if (intel_nhlt_has_endpoint_type(nhlt, NHLT_LINK_SSP)) {
		ssp_mask = intel_nhlt_ssp_endpoint_mask(nhlt, NHLT_DEVICE_I2S);
		if (ssp_mask)
			dev_info(sdev->dev, "NHLT_DEVICE_I2S detected, ssp_mask %#x\n", ssp_mask);
	}
	intel_nhlt_free(nhlt);

	return ssp_mask;
}

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA) || IS_ENABLED(CONFIG_SND_SOC_SOF_INTEL_SOUNDWIRE)

static const char *fixup_tplg_name(struct snd_sof_dev *sdev,
				   const char *sof_tplg_filename,
				   const char *idisp_str,
				   const char *dmic_str)
{
	const char *tplg_filename = NULL;
	char *filename, *tmp;
	const char *split_ext;

	filename = kstrdup(sof_tplg_filename, GFP_KERNEL);
	if (!filename)
		return NULL;

	/* this assumes a .tplg extension */
	tmp = filename;
	split_ext = strsep(&tmp, ".");
	if (split_ext)
		tplg_filename = devm_kasprintf(sdev->dev, GFP_KERNEL,
					       "%s%s%s.tplg",
					       split_ext, idisp_str, dmic_str);
	kfree(filename);

	return tplg_filename;
}

static int dmic_topology_fixup(struct snd_sof_dev *sdev,
			       const char **tplg_filename,
			       const char *idisp_str,
			       int *dmic_found)
{
	const char *default_tplg_filename = *tplg_filename;
	const char *fixed_tplg_filename;
	const char *dmic_str;
	int dmic_num;

	/* first check for DMICs (using NHLT or module parameter) */
	dmic_num = check_dmic_num(sdev);

	switch (dmic_num) {
	case 1:
		dmic_str = "-1ch";
		break;
	case 2:
		dmic_str = "-2ch";
		break;
	case 3:
		dmic_str = "-3ch";
		break;
	case 4:
		dmic_str = "-4ch";
		break;
	default:
		dmic_num = 0;
		dmic_str = "";
		break;
	}

	fixed_tplg_filename = fixup_tplg_name(sdev, default_tplg_filename,
					      idisp_str, dmic_str);
	if (!fixed_tplg_filename)
		return -ENOMEM;

	dev_info(sdev->dev, "DMICs detected in NHLT tables: %d\n", dmic_num);
	*dmic_found = dmic_num;
	*tplg_filename = fixed_tplg_filename;

	return 0;
}
#endif

static int hda_init_caps(struct snd_sof_dev *sdev)
{
	struct hdac_bus *bus = sof_to_bus(sdev);
	struct snd_sof_pdata *pdata = sdev->pdata;
#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)
	struct hdac_ext_link *hlink;
#endif
	struct sof_intel_hda_dev *hdev = pdata->hw_pdata;
	u32 link_mask;
	int ret = 0;

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

static void hda_check_for_state_change(struct snd_sof_dev *sdev)
{
#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)
	struct hdac_bus *bus = sof_to_bus(sdev);
	unsigned int codec_mask;

	codec_mask = snd_hdac_chip_readw(bus, STATESTS);
	if (codec_mask) {
		hda_codec_jack_check(sdev);
		snd_hdac_chip_writew(bus, STATESTS, codec_mask);
	}
#endif
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
	if (hda_dsp_check_stream_irq(sdev))
		hda_dsp_stream_threaded_handler(irq, sdev);

	if (hda_check_ipc_irq(sdev))
		sof_ops(sdev)->irq_thread(irq, sdev);

	if (hda_dsp_check_sdw_irq(sdev))
		hda_dsp_sdw_thread(irq, hdev->sdw);

	if (hda_sdw_check_wakeen_irq(sdev))
		hda_sdw_process_wakeen(sdev);

	hda_check_for_state_change(sdev);

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

	sdev->num_cores = chip->cores_num;

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
	sdev->bar[HDA_DSP_BAR] = pci_ioremap_bar(pci, HDA_DSP_BAR);
	if (!sdev->bar[HDA_DSP_BAR]) {
		dev_err(sdev->dev, "error: ioremap error\n");
		ret = -ENXIO;
		goto hdac_bus_unmap;
	}

	sdev->mmio_bar = HDA_DSP_BAR;
	sdev->mailbox_bar = HDA_DSP_BAR;

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

	/* enable ppcap interrupt */
	hda_dsp_ctrl_ppcap_enable(sdev, true);
	hda_dsp_ctrl_ppcap_int_enable(sdev, true);

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
	platform_device_unregister(hdev->dmic_dev);
	iounmap(bus->remap_addr);
	hda_codec_i915_exit(sdev);
err:
	return ret;
}

int hda_dsp_remove(struct snd_sof_dev *sdev)
{
	struct sof_intel_hda_dev *hda = sdev->pdata->hw_pdata;
	const struct sof_intel_dsp_desc *chip = hda->desc;
	struct hdac_bus *bus = sof_to_bus(sdev);
	struct pci_dev *pci = to_pci_dev(sdev->dev);

	/* cancel any attempt for DSP D0I3 */
	cancel_delayed_work_sync(&hda->d0i3_work);

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)
	/* codec removal, invoke bus_device_remove */
	snd_hdac_ext_bus_device_remove(bus);
#endif

	hda_sdw_exit(sdev);

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
		hda_dsp_core_reset_power_down(sdev, chip->host_managed_cores_mask);

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
static void hda_generic_machine_select(struct snd_sof_dev *sdev,
				       struct snd_soc_acpi_mach **mach)
{
	struct hdac_bus *bus = sof_to_bus(sdev);
	struct snd_soc_acpi_mach_params *mach_params;
	struct snd_soc_acpi_mach *hda_mach;
	struct snd_sof_pdata *pdata = sdev->pdata;
	const char *tplg_filename;
	const char *idisp_str;
	int dmic_num = 0;
	int codec_num = 0;
	int ret;
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
			hda_mach = snd_soc_acpi_intel_hda_machines;

			dev_info(bus->dev, "using HDA machine driver %s now\n",
				 hda_mach->drv_name);

			if (codec_num == 1 && HDA_IDISP_CODEC(bus->codec_mask))
				idisp_str = "-idisp";
			else
				idisp_str = "";

			/* topology: use the info from hda_machines */
			tplg_filename = hda_mach->sof_tplg_filename;
			ret = dmic_topology_fixup(sdev, &tplg_filename, idisp_str, &dmic_num);
			if (ret < 0)
				return;

			hda_mach->mach_params.dmic_num = dmic_num;
			pdata->tplg_filename = tplg_filename;

			if (codec_num == 2) {
				/*
				 * Prevent SoundWire links from starting when an external
				 * HDaudio codec is used
				 */
				hda_mach->mach_params.link_mask = 0;
			}

			*mach = hda_mach;
		}
	}

	/* used by hda machine driver to create dai links */
	if (*mach) {
		mach_params = &(*mach)->mach_params;
		mach_params->codec_mask = bus->codec_mask;
		mach_params->common_hdmi_codec_drv = hda_codec_use_common_hdmi;
	}
}
#else
static void hda_generic_machine_select(struct snd_sof_dev *sdev,
				       struct snd_soc_acpi_mach **mach)
{
}
#endif

#if IS_ENABLED(CONFIG_SND_SOC_SOF_INTEL_SOUNDWIRE)

#define SDW_CODEC_ADR_MASK(_adr) ((_adr) & (SDW_DISCO_LINK_ID_MASK | SDW_VERSION_MASK | \
				  SDW_MFG_ID_MASK | SDW_PART_ID_MASK))

/* Check if all Slaves defined on the link can be found */
static bool link_slaves_found(struct snd_sof_dev *sdev,
			      const struct snd_soc_acpi_link_adr *link,
			      struct sdw_intel_ctx *sdw)
{
	struct hdac_bus *bus = sof_to_bus(sdev);
	struct sdw_intel_slave_id *ids = sdw->ids;
	int num_slaves = sdw->num_slaves;
	unsigned int part_id, link_id, unique_id, mfg_id, version;
	int i, j, k;

	for (i = 0; i < link->num_adr; i++) {
		u64 adr = link->adr_d[i].adr;
		int reported_part_count = 0;

		mfg_id = SDW_MFG_ID(adr);
		part_id = SDW_PART_ID(adr);
		link_id = SDW_DISCO_LINK_ID(adr);
		version = SDW_VERSION(adr);

		for (j = 0; j < num_slaves; j++) {
			/* find out how many identical parts were reported on that link */
			if (ids[j].link_id == link_id &&
			    ids[j].id.part_id == part_id &&
			    ids[j].id.mfg_id == mfg_id &&
			    ids[j].id.sdw_version == version)
				reported_part_count++;
		}

		for (j = 0; j < num_slaves; j++) {
			int expected_part_count = 0;

			if (ids[j].link_id != link_id ||
			    ids[j].id.part_id != part_id ||
			    ids[j].id.mfg_id != mfg_id ||
			    ids[j].id.sdw_version != version)
				continue;

			/* find out how many identical parts are expected */
			for (k = 0; k < link->num_adr; k++) {
				u64 adr2 = link->adr_d[k].adr;

				if (SDW_CODEC_ADR_MASK(adr2) == SDW_CODEC_ADR_MASK(adr))
					expected_part_count++;
			}

			if (reported_part_count == expected_part_count) {
				/*
				 * we have to check unique id
				 * if there is more than one
				 * Slave on the link
				 */
				unique_id = SDW_UNIQUE_ID(adr);
				if (reported_part_count == 1 ||
				    ids[j].id.unique_id == unique_id) {
					dev_dbg(bus->dev, "found %x at link %d\n",
						part_id, link_id);
					break;
				}
			} else {
				dev_dbg(bus->dev, "part %x reported %d expected %d on link %d, skipping\n",
					part_id, reported_part_count, expected_part_count, link_id);
			}
		}
		if (j == num_slaves) {
			dev_dbg(bus->dev,
				"Slave %x not found\n",
				part_id);
			return false;
		}
	}
	return true;
}

static struct snd_soc_acpi_mach *hda_sdw_machine_select(struct snd_sof_dev *sdev)
{
	struct snd_sof_pdata *pdata = sdev->pdata;
	const struct snd_soc_acpi_link_adr *link;
	struct snd_soc_acpi_mach *mach;
	struct sof_intel_hda_dev *hdev;
	u32 link_mask;
	int i;

	hdev = pdata->hw_pdata;
	link_mask = hdev->info.link_mask;

	/*
	 * Select SoundWire machine driver if needed using the
	 * alternate tables. This case deals with SoundWire-only
	 * machines, for mixed cases with I2C/I2S the detection relies
	 * on the HID list.
	 */
	if (link_mask) {
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
				if (!link_slaves_found(sdev, link, hdev->sdw))
					break;
			}
			/* Found if all Slaves are checked */
			if (i == hdev->info.count || !link->num_adr)
				break;
		}
		if (mach && mach->link_mask) {
			int dmic_num = 0;

			mach->mach_params.links = mach->links;
			mach->mach_params.link_mask = mach->link_mask;
			mach->mach_params.platform = dev_name(sdev->dev);
			pdata->fw_filename = pdata->desc->default_fw_filename[pdata->ipc_type];
			pdata->tplg_filename = mach->sof_tplg_filename;

			/*
			 * DMICs use up to 4 pins and are typically pin-muxed with SoundWire
			 * link 2 and 3, thus we only try to enable dmics if all conditions
			 * are true:
			 * a) link 2 and 3 are not used by SoundWire
			 * b) the NHLT table reports the presence of microphones
			 */
			if (!(mach->link_mask & GENMASK(3, 2))) {
				const char *tplg_filename = mach->sof_tplg_filename;
				int ret;

				ret = dmic_topology_fixup(sdev, &tplg_filename, "", &dmic_num);
				if (ret < 0)
					return NULL;

				pdata->tplg_filename = tplg_filename;
			}
			mach->mach_params.dmic_num = dmic_num;

			dev_dbg(sdev->dev,
				"SoundWire machine driver %s topology %s\n",
				mach->drv_name,
				pdata->tplg_filename);

			return mach;
		}

		dev_info(sdev->dev, "No SoundWire machine driver found\n");
	}

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
	mach_params->num_dai_drivers = desc->ops->num_drv;
	mach_params->dai_drivers = desc->ops->drv;
}

struct snd_soc_acpi_mach *hda_machine_select(struct snd_sof_dev *sdev)
{
	struct snd_sof_pdata *sof_pdata = sdev->pdata;
	const struct sof_dev_desc *desc = sof_pdata->desc;
	struct snd_soc_acpi_mach *mach;
	const char *tplg_filename;

	mach = snd_soc_acpi_find_machine(desc->machines);
	if (mach) {
		bool add_extension = false;

		/*
		 * If tplg file name is overridden, use it instead of
		 * the one set in mach table
		 */
		if (!sof_pdata->tplg_filename)
			sof_pdata->tplg_filename = mach->sof_tplg_filename;

		/* report to machine driver if any DMICs are found */
		mach->mach_params.dmic_num = check_dmic_num(sdev);

		if (mach->tplg_quirk_mask & SND_SOC_ACPI_TPLG_INTEL_DMIC_NUMBER &&
		    mach->mach_params.dmic_num) {
			tplg_filename = devm_kasprintf(sdev->dev, GFP_KERNEL,
						       "%s%s%d%s",
						       sof_pdata->tplg_filename,
						       "-dmic",
						       mach->mach_params.dmic_num,
						       "ch");
			if (!tplg_filename)
				return NULL;

			sof_pdata->tplg_filename = tplg_filename;
			add_extension = true;
		}

		if (mach->link_mask) {
			mach->mach_params.links = mach->links;
			mach->mach_params.link_mask = mach->link_mask;
		}

		/* report SSP link mask to machine driver */
		mach->mach_params.i2s_link_mask = check_nhlt_ssp_mask(sdev);

		if (mach->tplg_quirk_mask & SND_SOC_ACPI_TPLG_INTEL_SSP_NUMBER &&
		    mach->mach_params.i2s_link_mask) {
			int ssp_num;

			if (hweight_long(mach->mach_params.i2s_link_mask) > 1 &&
			    !(mach->tplg_quirk_mask & SND_SOC_ACPI_TPLG_INTEL_SSP_MSB))
				dev_warn(sdev->dev, "More than one SSP exposed by NHLT, choosing MSB\n");

			/* fls returns 1-based results, SSPs indices are 0-based */
			ssp_num = fls(mach->mach_params.i2s_link_mask) - 1;

			tplg_filename = devm_kasprintf(sdev->dev, GFP_KERNEL,
						       "%s%s%d",
						       sof_pdata->tplg_filename,
						       "-ssp",
						       ssp_num);
			if (!tplg_filename)
				return NULL;

			sof_pdata->tplg_filename = tplg_filename;
			add_extension = true;
		}

		if (add_extension) {
			tplg_filename = devm_kasprintf(sdev->dev, GFP_KERNEL,
						       "%s%s",
						       sof_pdata->tplg_filename,
						       ".tplg");
			if (!tplg_filename)
				return NULL;

			sof_pdata->tplg_filename = tplg_filename;
		}
	}

	/*
	 * If I2S fails, try SoundWire
	 */
	if (!mach)
		mach = hda_sdw_machine_select(sdev);

	/*
	 * Choose HDA generic machine driver if mach is NULL.
	 * Otherwise, set certain mach params.
	 */
	hda_generic_machine_select(sdev, &mach);
	if (!mach)
		dev_warn(sdev->dev, "warning: No matching ASoC machine driver found\n");

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
EXPORT_SYMBOL_NS(hda_pci_intel_probe, SND_SOC_SOF_INTEL_HDA_COMMON);

int hda_register_clients(struct snd_sof_dev *sdev)
{
	return hda_probes_register(sdev);
}

void hda_unregister_clients(struct snd_sof_dev *sdev)
{
	hda_probes_unregister(sdev);
}

MODULE_LICENSE("Dual BSD/GPL");
MODULE_IMPORT_NS(SND_SOC_SOF_PCI_DEV);
MODULE_IMPORT_NS(SND_SOC_SOF_HDA_AUDIO_CODEC);
MODULE_IMPORT_NS(SND_SOC_SOF_HDA_AUDIO_CODEC_I915);
MODULE_IMPORT_NS(SND_SOC_SOF_XTENSA);
MODULE_IMPORT_NS(SND_INTEL_SOUNDWIRE_ACPI);
MODULE_IMPORT_NS(SOUNDWIRE_INTEL_INIT);
