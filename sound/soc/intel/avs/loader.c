// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2021-2022 Intel Corporation
//
// Authors: Cezary Rojewski <cezary.rojewski@intel.com>
//          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
//

#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <sound/hdaudio.h>
#include <sound/hdaudio_ext.h>
#include "avs.h"
#include "cldma.h"
#include "messages.h"
#include "registers.h"
#include "topology.h"

#define AVS_ROM_STS_MASK		0xFF
#define AVS_ROM_INIT_DONE		0x1
#define SKL_ROM_BASEFW_ENTERED		0xF
#define APL_ROM_FW_ENTERED		0x5
#define AVS_ROM_INIT_POLLING_US		5
#define SKL_ROM_INIT_TIMEOUT_US		1000000
#define APL_ROM_INIT_TIMEOUT_US		300000
#define APL_ROM_INIT_RETRIES		3

#define AVS_FW_INIT_POLLING_US		500
#define AVS_FW_INIT_TIMEOUT_MS		3000
#define AVS_FW_INIT_TIMEOUT_US		(AVS_FW_INIT_TIMEOUT_MS * 1000)

#define AVS_CLDMA_START_DELAY_MS	100

#define AVS_ROOT_DIR			"intel/avs"
#define AVS_BASEFW_FILENAME		"dsp_basefw.bin"
#define AVS_EXT_MANIFEST_MAGIC		0x31454124
#define SKL_MANIFEST_MAGIC		0x00000006
#define SKL_ADSPFW_OFFSET		0x284
#define APL_MANIFEST_MAGIC		0x44504324
#define APL_ADSPFW_OFFSET		0x2000

/* Occasionally, engineering (release candidate) firmware is provided for testing. */
static bool debug_ignore_fw_version;
module_param_named(ignore_fw_version, debug_ignore_fw_version, bool, 0444);
MODULE_PARM_DESC(ignore_fw_version, "Ignore firmware version check 0=no (default), 1=yes");

#define AVS_LIB_NAME_SIZE	8

struct avs_fw_manifest {
	u32 id;
	u32 len;
	char name[AVS_LIB_NAME_SIZE];
	u32 preload_page_count;
	u32 img_flags;
	u32 feature_mask;
	struct avs_fw_version version;
} __packed;
static_assert(sizeof(struct avs_fw_manifest) == 36);

struct avs_fw_ext_manifest {
	u32 id;
	u32 len;
	u16 version_major;
	u16 version_minor;
	u32 entries;
} __packed;
static_assert(sizeof(struct avs_fw_ext_manifest) == 16);

static int avs_fw_ext_manifest_strip(struct firmware *fw)
{
	struct avs_fw_ext_manifest *man;

	if (fw->size < sizeof(*man))
		return -EINVAL;

	man = (struct avs_fw_ext_manifest *)fw->data;
	if (man->id == AVS_EXT_MANIFEST_MAGIC) {
		fw->data += man->len;
		fw->size -= man->len;
	}

	return 0;
}

static int avs_fw_manifest_offset(struct firmware *fw)
{
	/* Header type found in first DWORD of fw binary. */
	u32 magic = *(u32 *)fw->data;

	switch (magic) {
	case SKL_MANIFEST_MAGIC:
		return SKL_ADSPFW_OFFSET;
	case APL_MANIFEST_MAGIC:
		return APL_ADSPFW_OFFSET;
	default:
		return -EINVAL;
	}
}

static int avs_fw_manifest_strip_verify(struct avs_dev *adev, struct firmware *fw,
					const struct avs_fw_version *min)
{
	struct avs_fw_manifest *man;
	int offset, ret;

	ret = avs_fw_ext_manifest_strip(fw);
	if (ret)
		return ret;

	offset = avs_fw_manifest_offset(fw);
	if (offset < 0)
		return offset;

	if (fw->size < offset + sizeof(*man))
		return -EINVAL;
	if (!min)
		return 0;

	man = (struct avs_fw_manifest *)(fw->data + offset);
	if (man->version.major != min->major ||
	    man->version.minor != min->minor ||
	    man->version.hotfix != min->hotfix ||
	    man->version.build < min->build) {
		dev_warn(adev->dev, "bad FW version %d.%d.%d.%d, expected %d.%d.%d.%d or newer\n",
			 man->version.major, man->version.minor,
			 man->version.hotfix, man->version.build,
			 min->major, min->minor, min->hotfix, min->build);

		if (!debug_ignore_fw_version)
			return -EINVAL;
	}

	return 0;
}

int avs_cldma_load_basefw(struct avs_dev *adev, struct firmware *fw)
{
	struct hda_cldma *cl = &code_loader;
	unsigned int reg;
	int ret;

	ret = avs_dsp_op(adev, power, AVS_MAIN_CORE_MASK, true);
	if (ret < 0)
		return ret;

	ret = avs_dsp_op(adev, reset, AVS_MAIN_CORE_MASK, false);
	if (ret < 0)
		return ret;

	ret = hda_cldma_reset(cl);
	if (ret < 0) {
		dev_err(adev->dev, "cldma reset failed: %d\n", ret);
		return ret;
	}
	hda_cldma_setup(cl);

	ret = avs_dsp_op(adev, stall, AVS_MAIN_CORE_MASK, false);
	if (ret < 0)
		return ret;

	reinit_completion(&adev->fw_ready);
	avs_dsp_op(adev, int_control, true);

	/* await ROM init */
	ret = snd_hdac_adsp_readl_poll(adev, AVS_FW_REG_STATUS(adev), reg,
				       (reg & AVS_ROM_INIT_DONE) == AVS_ROM_INIT_DONE,
				       AVS_ROM_INIT_POLLING_US, SKL_ROM_INIT_TIMEOUT_US);
	if (ret < 0) {
		dev_err(adev->dev, "rom init failed: %d, status: 0x%08x, lec: 0x%08x\n",
			ret, reg, snd_hdac_adsp_readl(adev, AVS_FW_REG_ERROR(adev)));
		avs_dsp_core_disable(adev, AVS_MAIN_CORE_MASK);
		return ret;
	}

	hda_cldma_set_data(cl, (void *)fw->data, fw->size);
	/* transfer firmware */
	hda_cldma_transfer(cl, 0);
	ret = snd_hdac_adsp_readl_poll(adev, AVS_FW_REG_STATUS(adev), reg,
				       (reg & AVS_ROM_STS_MASK) == SKL_ROM_BASEFW_ENTERED,
				       AVS_FW_INIT_POLLING_US, AVS_FW_INIT_TIMEOUT_US);
	hda_cldma_stop(cl);
	if (ret < 0) {
		dev_err(adev->dev, "transfer fw failed: %d, status: 0x%08x, lec: 0x%08x\n",
			ret, reg, snd_hdac_adsp_readl(adev, AVS_FW_REG_ERROR(adev)));
		avs_dsp_core_disable(adev, AVS_MAIN_CORE_MASK);
		return ret;
	}

	return 0;
}

int avs_cldma_load_library(struct avs_dev *adev, struct firmware *lib, u32 id)
{
	struct hda_cldma *cl = &code_loader;
	int ret;

	hda_cldma_set_data(cl, (void *)lib->data, lib->size);
	/* transfer modules manifest */
	hda_cldma_transfer(cl, msecs_to_jiffies(AVS_CLDMA_START_DELAY_MS));

	/* DMA id ignored as there is only ever one code-loader DMA */
	ret = avs_ipc_load_library(adev, 0, id);
	hda_cldma_stop(cl);

	if (ret) {
		ret = AVS_IPC_RET(ret);
		dev_err(adev->dev, "transfer lib %d failed: %d\n", id, ret);
	}

	return ret;
}

static int avs_cldma_load_module(struct avs_dev *adev, struct avs_module_entry *mentry)
{
	struct hda_cldma *cl = &code_loader;
	const struct firmware *mod;
	char *mod_name;
	int ret;

	mod_name = kasprintf(GFP_KERNEL, "%s/%s/dsp_mod_%pUL.bin", AVS_ROOT_DIR,
			     adev->spec->name, mentry->uuid.b);
	if (!mod_name)
		return -ENOMEM;

	ret = avs_request_firmware(adev, &mod, mod_name);
	kfree(mod_name);
	if (ret < 0)
		return ret;

	avs_hda_power_gating_enable(adev, false);
	avs_hda_clock_gating_enable(adev, false);
	avs_hda_l1sen_enable(adev, false);

	hda_cldma_set_data(cl, (void *)mod->data, mod->size);
	hda_cldma_transfer(cl, msecs_to_jiffies(AVS_CLDMA_START_DELAY_MS));
	ret = avs_ipc_load_modules(adev, &mentry->module_id, 1);
	hda_cldma_stop(cl);

	avs_hda_l1sen_enable(adev, true);
	avs_hda_clock_gating_enable(adev, true);
	avs_hda_power_gating_enable(adev, true);

	if (ret) {
		dev_err(adev->dev, "load module %d failed: %d\n", mentry->module_id, ret);
		avs_release_last_firmware(adev);
		return AVS_IPC_RET(ret);
	}

	return 0;
}

int avs_cldma_transfer_modules(struct avs_dev *adev, bool load,
			       struct avs_module_entry *mods, u32 num_mods)
{
	u16 *mod_ids;
	int ret, i;

	/* Either load to DSP or unload them to free space. */
	if (load) {
		for (i = 0; i < num_mods; i++) {
			ret = avs_cldma_load_module(adev, &mods[i]);
			if (ret)
				return ret;
		}

		return 0;
	}

	mod_ids = kcalloc(num_mods, sizeof(u16), GFP_KERNEL);
	if (!mod_ids)
		return -ENOMEM;

	for (i = 0; i < num_mods; i++)
		mod_ids[i] = mods[i].module_id;

	ret = avs_ipc_unload_modules(adev, mod_ids, num_mods);
	kfree(mod_ids);
	if (ret)
		return AVS_IPC_RET(ret);

	return 0;
}

static int
avs_hda_init_rom(struct avs_dev *adev, unsigned int dma_id, bool purge)
{
	const struct avs_spec *const spec = adev->spec;
	unsigned int corex_mask, reg;
	int ret;

	corex_mask = spec->core_init_mask & ~AVS_MAIN_CORE_MASK;

	ret = avs_dsp_op(adev, power, spec->core_init_mask, true);
	if (ret < 0)
		goto err;

	ret = avs_dsp_op(adev, reset, AVS_MAIN_CORE_MASK, false);
	if (ret < 0)
		goto err;

	reinit_completion(&adev->fw_ready);
	avs_dsp_op(adev, int_control, true);

	/* set boot config */
	ret = avs_ipc_set_boot_config(adev, dma_id, purge);
	if (ret) {
		ret = AVS_IPC_RET(ret);
		goto err;
	}

	/* await ROM init */
	ret = snd_hdac_adsp_readl_poll(adev, spec->sram->rom_status_offset, reg,
				       (reg & 0xF) == AVS_ROM_INIT_DONE ||
				       (reg & 0xF) == APL_ROM_FW_ENTERED,
				       AVS_ROM_INIT_POLLING_US, APL_ROM_INIT_TIMEOUT_US);
	if (ret < 0) {
		dev_err(adev->dev, "rom init failed: %d, status: 0x%08x, lec: 0x%08x\n",
			ret, reg, snd_hdac_adsp_readl(adev, AVS_FW_REG_ERROR(adev)));
		goto err;
	}

	/* power down non-main cores */
	if (corex_mask) {
		ret = avs_dsp_op(adev, power, corex_mask, false);
		if (ret < 0)
			goto err;
	}

	return 0;

err:
	avs_dsp_core_disable(adev, spec->core_init_mask);
	return ret;
}

static int avs_imr_load_basefw(struct avs_dev *adev)
{
	int ret;

	/* DMA id ignored when flashing from IMR as no transfer occurs. */
	ret = avs_hda_init_rom(adev, 0, false);
	if (ret < 0)
		return ret;

	ret = wait_for_completion_timeout(&adev->fw_ready,
					  msecs_to_jiffies(AVS_FW_INIT_TIMEOUT_MS));
	if (!ret) {
		dev_err(adev->dev, "firmware ready timeout, status: 0x%08x, lec: 0x%08x\n",
			snd_hdac_adsp_readl(adev, AVS_FW_REG_STATUS(adev)),
			snd_hdac_adsp_readl(adev, AVS_FW_REG_ERROR(adev)));
		avs_dsp_core_disable(adev, AVS_MAIN_CORE_MASK);
		return -ETIMEDOUT;
	}

	return 0;
}

int avs_hda_load_basefw(struct avs_dev *adev, struct firmware *fw)
{
	struct snd_pcm_substream substream;
	struct snd_dma_buffer dmab;
	struct hdac_ext_stream *estream;
	struct hdac_stream *hstream;
	struct hdac_bus *bus = &adev->base.core;
	unsigned int sdfmt, reg;
	int ret, i;

	/* configure hda dma */
	memset(&substream, 0, sizeof(substream));
	substream.stream = SNDRV_PCM_STREAM_PLAYBACK;
	estream = snd_hdac_ext_stream_assign(bus, &substream,
					     HDAC_EXT_STREAM_TYPE_HOST);
	if (!estream)
		return -ENODEV;
	hstream = hdac_stream(estream);

	/* code loading performed with default format */
	sdfmt = snd_hdac_stream_format(1, 32, 48000);
	ret = snd_hdac_dsp_prepare(hstream, sdfmt, fw->size, &dmab);
	if (ret < 0)
		goto release_stream;

	/* enable SPIB for hda stream */
	snd_hdac_stream_spbcap_enable(bus, true, hstream->index);
	ret = snd_hdac_stream_set_spib(bus, hstream, fw->size);
	if (ret)
		goto cleanup_resources;

	memcpy(dmab.area, fw->data, fw->size);

	for (i = 0; i < APL_ROM_INIT_RETRIES; i++) {
		unsigned int dma_id = hstream->stream_tag - 1;

		ret = avs_hda_init_rom(adev, dma_id, true);
		if (!ret)
			break;
		dev_info(adev->dev, "#%d rom init failed: %d\n", i + 1, ret);
	}
	if (ret < 0)
		goto cleanup_resources;

	/* transfer firmware */
	snd_hdac_dsp_trigger(hstream, true);
	ret = snd_hdac_adsp_readl_poll(adev, AVS_FW_REG_STATUS(adev), reg,
				       (reg & AVS_ROM_STS_MASK) == APL_ROM_FW_ENTERED,
				       AVS_FW_INIT_POLLING_US, AVS_FW_INIT_TIMEOUT_US);
	snd_hdac_dsp_trigger(hstream, false);
	if (ret < 0) {
		dev_err(adev->dev, "transfer fw failed: %d, status: 0x%08x, lec: 0x%08x\n",
			ret, reg, snd_hdac_adsp_readl(adev, AVS_FW_REG_ERROR(adev)));
		avs_dsp_core_disable(adev, AVS_MAIN_CORE_MASK);
	}

cleanup_resources:
	/* disable SPIB for hda stream */
	snd_hdac_stream_spbcap_enable(bus, false, hstream->index);
	snd_hdac_stream_set_spib(bus, hstream, 0);

	snd_hdac_dsp_cleanup(hstream, &dmab);
release_stream:
	snd_hdac_ext_stream_release(estream, HDAC_EXT_STREAM_TYPE_HOST);

	return ret;
}

int avs_hda_load_library(struct avs_dev *adev, struct firmware *lib, u32 id)
{
	struct snd_pcm_substream substream;
	struct snd_dma_buffer dmab;
	struct hdac_ext_stream *estream;
	struct hdac_stream *stream;
	struct hdac_bus *bus = &adev->base.core;
	unsigned int sdfmt;
	int ret;

	/* configure hda dma */
	memset(&substream, 0, sizeof(substream));
	substream.stream = SNDRV_PCM_STREAM_PLAYBACK;
	estream = snd_hdac_ext_stream_assign(bus, &substream,
					     HDAC_EXT_STREAM_TYPE_HOST);
	if (!estream)
		return -ENODEV;
	stream = hdac_stream(estream);

	/* code loading performed with default format */
	sdfmt = snd_hdac_stream_format(1, 32, 48000);
	ret = snd_hdac_dsp_prepare(stream, sdfmt, lib->size, &dmab);
	if (ret < 0)
		goto release_stream;

	/* enable SPIB for hda stream */
	snd_hdac_stream_spbcap_enable(bus, true, stream->index);
	snd_hdac_stream_set_spib(bus, stream, lib->size);

	memcpy(dmab.area, lib->data, lib->size);

	/* transfer firmware */
	snd_hdac_dsp_trigger(stream, true);
	ret = avs_ipc_load_library(adev, stream->stream_tag - 1, id);
	snd_hdac_dsp_trigger(stream, false);
	if (ret) {
		dev_err(adev->dev, "transfer lib %d failed: %d\n", id, ret);
		ret = AVS_IPC_RET(ret);
	}

	/* disable SPIB for hda stream */
	snd_hdac_stream_spbcap_enable(bus, false, stream->index);
	snd_hdac_stream_set_spib(bus, stream, 0);

	snd_hdac_dsp_cleanup(stream, &dmab);
release_stream:
	snd_hdac_ext_stream_release(estream, HDAC_EXT_STREAM_TYPE_HOST);

	return ret;
}

int avs_hda_transfer_modules(struct avs_dev *adev, bool load,
			     struct avs_module_entry *mods, u32 num_mods)
{
	/*
	 * All platforms without CLDMA are equipped with IMR,
	 * and thus the module transferring is offloaded to DSP.
	 */
	return 0;
}

int avs_dsp_load_libraries(struct avs_dev *adev, struct avs_tplg_library *libs, u32 num_libs)
{
	int start, id, i = 0;
	int ret;

	/* Calculate the id to assign for the next lib. */
	for (id = 0; id < adev->fw_cfg.max_libs_count; id++)
		if (adev->lib_names[id][0] == '\0')
			break;
	if (id + num_libs >= adev->fw_cfg.max_libs_count)
		return -EINVAL;

	start = id;
	while (i < num_libs) {
		struct avs_fw_manifest *man;
		const struct firmware *fw;
		struct firmware stripped_fw;
		char *filename;
		int j;

		filename = kasprintf(GFP_KERNEL, "%s/%s/%s", AVS_ROOT_DIR, adev->spec->name,
				     libs[i].name);
		if (!filename)
			return -ENOMEM;

		/*
		 * If any call after this one fails, requested firmware is not released with
		 * avs_release_last_firmware() as failing to load code results in need for reload
		 * of entire driver module. And then avs_release_firmwares() is in place already.
		 */
		ret = avs_request_firmware(adev, &fw, filename);
		kfree(filename);
		if (ret < 0)
			return ret;

		stripped_fw = *fw;
		ret = avs_fw_manifest_strip_verify(adev, &stripped_fw, NULL);
		if (ret) {
			dev_err(adev->dev, "invalid library data: %d\n", ret);
			return ret;
		}

		ret = avs_fw_manifest_offset(&stripped_fw);
		if (ret < 0)
			return ret;
		man = (struct avs_fw_manifest *)(stripped_fw.data + ret);

		/* Don't load anything that's already in DSP memory. */
		for (j = 0; j < id; j++)
			if (!strncmp(adev->lib_names[j], man->name, AVS_LIB_NAME_SIZE))
				goto next_lib;

		ret = avs_dsp_op(adev, load_lib, &stripped_fw, id);
		if (ret)
			return ret;

		strscpy(adev->lib_names[id], man->name, AVS_LIB_NAME_SIZE);
		id++;
next_lib:
		i++;
	}

	return start == id ? 1 : 0;
}

static int avs_dsp_load_basefw(struct avs_dev *adev)
{
	const struct avs_fw_version *min_req;
	const struct avs_spec *const spec = adev->spec;
	const struct firmware *fw;
	struct firmware stripped_fw;
	char *filename;
	int ret;

	filename = kasprintf(GFP_KERNEL, "%s/%s/%s", AVS_ROOT_DIR, spec->name, AVS_BASEFW_FILENAME);
	if (!filename)
		return -ENOMEM;

	ret = avs_request_firmware(adev, &fw, filename);
	kfree(filename);
	if (ret < 0) {
		dev_err(adev->dev, "request firmware failed: %d\n", ret);
		return ret;
	}

	stripped_fw = *fw;
	min_req = &adev->spec->min_fw_version;

	ret = avs_fw_manifest_strip_verify(adev, &stripped_fw, min_req);
	if (ret < 0) {
		dev_err(adev->dev, "invalid firmware data: %d\n", ret);
		goto release_fw;
	}

	ret = avs_dsp_op(adev, load_basefw, &stripped_fw);
	if (ret < 0) {
		dev_err(adev->dev, "basefw load failed: %d\n", ret);
		goto release_fw;
	}

	ret = wait_for_completion_timeout(&adev->fw_ready,
					  msecs_to_jiffies(AVS_FW_INIT_TIMEOUT_MS));
	if (!ret) {
		dev_err(adev->dev, "firmware ready timeout, status: 0x%08x, lec: 0x%08x\n",
			snd_hdac_adsp_readl(adev, AVS_FW_REG_STATUS(adev)),
			snd_hdac_adsp_readl(adev, AVS_FW_REG_ERROR(adev)));
		avs_dsp_core_disable(adev, AVS_MAIN_CORE_MASK);
		ret = -ETIMEDOUT;
		goto release_fw;
	}

	return 0;

release_fw:
	avs_release_last_firmware(adev);
	return ret;
}

static int avs_load_firmware(struct avs_dev *adev, bool purge)
{
	struct avs_soc_component *acomp;
	int ret, i;

	/* Forgo full boot if flash from IMR succeeds. */
	if (!purge && avs_platattr_test(adev, IMR)) {
		ret = avs_imr_load_basefw(adev);
		if (!ret)
			return 0;

		dev_dbg(adev->dev, "firmware flash from imr failed: %d\n", ret);
	}

	/* Full boot, clear cached data except for basefw (slot 0). */
	for (i = 1; i < adev->fw_cfg.max_libs_count; i++)
		memset(adev->lib_names[i], 0, AVS_LIB_NAME_SIZE);

	avs_hda_power_gating_enable(adev, false);
	avs_hda_clock_gating_enable(adev, false);
	avs_hda_l1sen_enable(adev, false);

	ret = avs_dsp_load_basefw(adev);
	if (ret)
		goto reenable_gating;

	mutex_lock(&adev->comp_list_mutex);
	list_for_each_entry(acomp, &adev->comp_list, node) {
		struct avs_tplg *tplg = acomp->tplg;

		ret = avs_dsp_load_libraries(adev, tplg->libs, tplg->num_libs);
		if (ret < 0)
			break;
	}
	mutex_unlock(&adev->comp_list_mutex);

reenable_gating:
	avs_hda_l1sen_enable(adev, true);
	avs_hda_clock_gating_enable(adev, true);
	avs_hda_power_gating_enable(adev, true);

	if (ret < 0)
		return ret;

	/* With all code loaded, refresh module information. */
	ret = avs_module_info_init(adev, true);
	if (ret) {
		dev_err(adev->dev, "init module info failed: %d\n", ret);
		return ret;
	}

	return 0;
}

static int avs_config_basefw(struct avs_dev *adev)
{
	int ret;

	if (adev->spec->dsp_ops->config_basefw) {
		ret = avs_dsp_op(adev, config_basefw);
		if (ret)
			return ret;
	}

	return 0;
}

int avs_dsp_boot_firmware(struct avs_dev *adev, bool purge)
{
	int ret;

	ret = avs_load_firmware(adev, purge);
	if (ret)
		return ret;

	return avs_config_basefw(adev);
}

static int avs_dsp_alloc_resources(struct avs_dev *adev)
{
	int ret, i;

	ret = avs_ipc_get_hw_config(adev, &adev->hw_cfg);
	if (ret)
		return AVS_IPC_RET(ret);

	ret = avs_ipc_get_fw_config(adev, &adev->fw_cfg);
	if (ret)
		return AVS_IPC_RET(ret);

	adev->core_refs = devm_kcalloc(adev->dev, adev->hw_cfg.dsp_cores,
				       sizeof(*adev->core_refs), GFP_KERNEL);
	adev->lib_names = devm_kcalloc(adev->dev, adev->fw_cfg.max_libs_count,
				       sizeof(*adev->lib_names), GFP_KERNEL);
	if (!adev->core_refs || !adev->lib_names)
		return -ENOMEM;

	for (i = 0; i < adev->fw_cfg.max_libs_count; i++) {
		adev->lib_names[i] = devm_kzalloc(adev->dev, AVS_LIB_NAME_SIZE, GFP_KERNEL);
		if (!adev->lib_names[i])
			return -ENOMEM;
	}

	/* basefw always occupies slot 0 */
	strscpy(adev->lib_names[0], "BASEFW", AVS_LIB_NAME_SIZE);

	ida_init(&adev->ppl_ida);
	return 0;
}

int avs_dsp_first_boot_firmware(struct avs_dev *adev)
{
	int ret;

	if (avs_platattr_test(adev, CLDMA)) {
		ret = hda_cldma_init(&code_loader, &adev->base.core,
				     adev->dsp_ba, AVS_CL_DEFAULT_BUFFER_SIZE);
		if (ret < 0) {
			dev_err(adev->dev, "cldma init failed: %d\n", ret);
			return ret;
		}
	}

	ret = avs_dsp_core_disable(adev, AVS_MAIN_CORE_MASK);
	if (ret < 0)
		return ret;

	ret = avs_dsp_boot_firmware(adev, true);
	if (ret < 0) {
		dev_err(adev->dev, "firmware boot failed: %d\n", ret);
		return ret;
	}

	return avs_dsp_alloc_resources(adev);
}
