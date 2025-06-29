// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2021-2022 Intel Corporation
//
// Authors: Cezary Rojewski <cezary.rojewski@intel.com>
//          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
//

#include <linux/acpi.h>
#include <linux/module.h>
#include <linux/dmi.h>
#include <linux/pci.h>
#include <acpi/nhlt.h>
#include <linux/platform_device.h>
#include <sound/hda_codec.h>
#include <sound/hda_register.h>
#include <sound/soc-acpi.h>
#include <sound/soc-component.h>
#include "avs.h"
#include "utils.h"

static char *i2s_test;
module_param(i2s_test, charp, 0444);
MODULE_PARM_DESC(i2s_test, "Use I2S test-board instead of ACPI, i2s_test=ssp0tdm,ssp1tdm,... 0 to ignore port");

bool obsolete_card_names = IS_ENABLED(CONFIG_SND_SOC_INTEL_AVS_CARDNAME_OBSOLETE);
module_param_named(obsolete_card_names, obsolete_card_names, bool, 0444);
MODULE_PARM_DESC(obsolete_card_names, "Use obsolete card names 0=no, 1=yes");

static const struct dmi_system_id kbl_dmi_table[] = {
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Intel Corporation"),
			DMI_MATCH(DMI_BOARD_NAME, "Skylake Y LPDDR3 RVP3"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Intel Corporation"),
			DMI_MATCH(DMI_BOARD_NAME, "AmberLake Y"),
		},
	},
	{}
};

static const struct dmi_system_id kblr_dmi_table[] = {
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Intel Corporation"),
			DMI_MATCH(DMI_BOARD_NAME, "Kabylake R DDR4 RVP"),
		},
	},
	{}
};

static struct snd_soc_acpi_mach *dmi_match_quirk(void *arg)
{
	struct snd_soc_acpi_mach *mach = arg;
	const struct dmi_system_id *dmi_id;
	struct dmi_system_id *dmi_table;

	if (mach->quirk_data == NULL)
		return mach;

	dmi_table = (struct dmi_system_id *)mach->quirk_data;

	dmi_id = dmi_first_match(dmi_table);
	if (!dmi_id)
		return NULL;

	return mach;
}

#define AVS_SSP(x)		(BIT(x))
#define AVS_SSP_RANGE(a, b)	(GENMASK(b, a))

/* supported I2S board codec configurations */
static struct snd_soc_acpi_mach avs_skl_i2s_machines[] = {
	{
		.id = "INT343A",
		.drv_name = "avs_rt286",
		.mach_params = {
			.i2s_link_mask = AVS_SSP(0),
		},
		.tplg_filename = "rt286-tplg.bin",
	},
	{
		.id = "10508825",
		.drv_name = "avs_nau8825",
		.mach_params = {
			.i2s_link_mask = AVS_SSP(1),
		},
		.tplg_filename = "nau8825-tplg.bin",
	},
	{
		.id = "INT343B",
		.drv_name = "avs_ssm4567",
		.mach_params = {
			.i2s_link_mask = AVS_SSP(0),
		},
		.tplg_filename = "ssm4567-tplg.bin",
	},
	{
		.id = "MX98357A",
		.drv_name = "avs_max98357a",
		.mach_params = {
			.i2s_link_mask = AVS_SSP(0),
		},
		.tplg_filename = "max98357a-tplg.bin",
	},
	{},
};

static struct snd_soc_acpi_mach avs_kbl_i2s_machines[] = {
	{
		.id = "INT343A",
		.drv_name = "avs_rt286",
		.mach_params = {
			.i2s_link_mask = AVS_SSP(0),
		},
		.quirk_data = &kbl_dmi_table,
		.machine_quirk = dmi_match_quirk,
		.tplg_filename = "rt286-tplg.bin",
	},
	{
		.id = "INT343A",
		.drv_name = "avs_rt298",
		.mach_params = {
			.i2s_link_mask = AVS_SSP(0),
		},
		.quirk_data = &kblr_dmi_table,
		.machine_quirk = dmi_match_quirk,
		.tplg_filename = "rt298-tplg.bin",
	},
	{
		.id = "MX98927",
		.drv_name = "avs_max98927",
		.mach_params = {
			.i2s_link_mask = AVS_SSP(0),
		},
		.tplg_filename = "max98927-tplg.bin",
	},
	{
		.id = "10EC5514",
		.drv_name = "avs_rt5514",
		.mach_params = {
			.i2s_link_mask = AVS_SSP(0),
		},
		.pdata = (struct avs_mach_pdata[]){ { .tdms = (unsigned long[]){ 0x2 } } },
		.tplg_filename = "rt5514-tplg.bin",
	},
	{
		.id = "10EC5663",
		.drv_name = "avs_rt5663",
		.mach_params = {
			.i2s_link_mask = AVS_SSP(1),
		},
		.tplg_filename = "rt5663-tplg.bin",
	},
	{
		.id = "MX98373",
		.drv_name = "avs_max98373",
		.mach_params = {
			.i2s_link_mask = AVS_SSP(0),
		},
		.tplg_filename = "max98373-tplg.bin",
	},
	{
		.id = "MX98357A",
		.drv_name = "avs_max98357a",
		.mach_params = {
			.i2s_link_mask = AVS_SSP(0),
		},
		.tplg_filename = "max98357a-tplg.bin",
	},
	{
		.id = "DLGS7219",
		.drv_name = "avs_da7219",
		.mach_params = {
			.i2s_link_mask = AVS_SSP(1),
		},
		.tplg_filename = "da7219-tplg.bin",
	},
	{
		.id = "ESSX8336",
		.drv_name = "avs_es8336",
		.mach_params = {
			.i2s_link_mask = AVS_SSP(0),
		},
		.tplg_filename = "es8336-tplg.bin",
	},
	{},
};

static struct snd_soc_acpi_mach avs_apl_i2s_machines[] = {
	{
		.id = "INT343A",
		.drv_name = "avs_rt298",
		.mach_params = {
			.i2s_link_mask = AVS_SSP(5),
		},
		.tplg_filename = "rt298-tplg.bin",
	},
	{
		.id = "INT34C3",
		.drv_name = "avs_tdf8532",
		.mach_params = {
			.i2s_link_mask = AVS_SSP_RANGE(0, 5),
		},
		.pdata = (struct avs_mach_pdata[]){ {
			.tdms = (unsigned long[]){ 0x1, 0x1, 0x14, 0x1, 0x1, 0x1 }
		} },
		.tplg_filename = "tdf8532-tplg.bin",
	},
	{
		.id = "MX98357A",
		.drv_name = "avs_max98357a",
		.mach_params = {
			.i2s_link_mask = AVS_SSP(5),
		},
		.tplg_filename = "max98357a-tplg.bin",
	},
	{
		.id = "DLGS7219",
		.drv_name = "avs_da7219",
		.mach_params = {
			.i2s_link_mask = AVS_SSP(1),
		},
		.tplg_filename = "da7219-tplg.bin",
	},
	{},
};

static struct snd_soc_acpi_mach avs_gml_i2s_machines[] = {
	{
		.id = "INT343A",
		.drv_name = "avs_rt298",
		.mach_params = {
			.i2s_link_mask = AVS_SSP(2),
		},
		.tplg_filename = "rt298-tplg.bin",
	},
	{},
};

static struct snd_soc_acpi_mach avs_cnl_i2s_machines[] = {
	{
		.id = "INT34C2",
		.drv_name = "avs_rt274",
		.mach_params = {
			.i2s_link_mask = AVS_SSP(0),
		},
		.tplg_filename = "rt274-tplg.bin",
	},
	{
		.id = "10EC5682",
		.drv_name = "avs_rt5682",
		.mach_params = {
			.i2s_link_mask = AVS_SSP(1),
		},
		.tplg_filename = "rt5682-tplg.bin",
	},
	{},
};

static struct snd_soc_acpi_mach avs_icl_i2s_machines[] = {
	{
		.id = "INT343A",
		.drv_name = "avs_rt298",
		.mach_params = {
			.i2s_link_mask = AVS_SSP(0),
		},
		.tplg_filename = "rt298-tplg.bin",
	},
	{
		.id = "INT34C2",
		.drv_name = "avs_rt274",
		.mach_params = {
			.i2s_link_mask = AVS_SSP(0),
		},
		.tplg_filename = "rt274-tplg.bin",
	},
	{},
};

static struct snd_soc_acpi_mach avs_tgl_i2s_machines[] = {
	{
		.id = "INT34C2",
		.drv_name = "avs_rt274",
		.mach_params = {
			.i2s_link_mask = AVS_SSP(0),
		},
		.tplg_filename = "rt274-tplg.bin",
	},
	{
		.id = "10EC0298",
		.drv_name = "avs_rt298",
		.mach_params = {
			.i2s_link_mask = AVS_SSP(0),
		},
		.tplg_filename = "rt298-tplg.bin",
	},
	{
		.id = "10EC1308",
		.drv_name = "avs_rt1308",
		.mach_params = {
			.i2s_link_mask = AVS_SSP(1),
		},
		.tplg_filename = "rt1308-tplg.bin",
	},
	{
		.id = "ESSX8336",
		.drv_name = "avs_es8336",
		.mach_params = {
			.i2s_link_mask = AVS_SSP(0),
		},
		.tplg_filename = "es8336-tplg.bin",
	},
	{},
};

static struct snd_soc_acpi_mach avs_mbl_i2s_machines[] = {
	{
		.id = "PCM3168A",
		.drv_name = "avs_pcm3168a",
		.mach_params = {
			.i2s_link_mask = AVS_SSP(0) | AVS_SSP(2),
		},
		.tplg_filename = "pcm3168a-tplg.bin",
	},
	{}
};

struct avs_acpi_boards {
	int id;
	struct snd_soc_acpi_mach *machs;
};

#define AVS_MACH_ENTRY(_id, _mach) \
	{ .id = PCI_DEVICE_ID_INTEL_##_id, .machs = (_mach), }

/* supported I2S boards per platform */
static const struct avs_acpi_boards i2s_boards[] = {
	AVS_MACH_ENTRY(HDA_SKL_LP, avs_skl_i2s_machines),
	AVS_MACH_ENTRY(HDA_KBL_LP, avs_kbl_i2s_machines),
	AVS_MACH_ENTRY(HDA_APL, avs_apl_i2s_machines),
	AVS_MACH_ENTRY(HDA_GML, avs_gml_i2s_machines),
	AVS_MACH_ENTRY(HDA_CNL_LP,	avs_cnl_i2s_machines),
	AVS_MACH_ENTRY(HDA_CNL_H,	avs_cnl_i2s_machines),
	AVS_MACH_ENTRY(HDA_CML_LP,	avs_cnl_i2s_machines),
	AVS_MACH_ENTRY(HDA_ICL_LP,	avs_icl_i2s_machines),
	AVS_MACH_ENTRY(HDA_TGL_LP,	avs_tgl_i2s_machines),
	AVS_MACH_ENTRY(HDA_EHL_0,	avs_tgl_i2s_machines),
	AVS_MACH_ENTRY(HDA_ADL_N,	avs_mbl_i2s_machines),
	AVS_MACH_ENTRY(HDA_ADL_P,	avs_tgl_i2s_machines),
	AVS_MACH_ENTRY(HDA_RPL_P_0,	avs_tgl_i2s_machines),
	AVS_MACH_ENTRY(HDA_RPL_M,	avs_mbl_i2s_machines),
	AVS_MACH_ENTRY(HDA_FCL,		avs_tgl_i2s_machines),
	{ },
};

static const struct avs_acpi_boards *avs_get_i2s_boards(struct avs_dev *adev)
{
	int id, i;

	id = adev->base.pci->device;
	for (i = 0; i < ARRAY_SIZE(i2s_boards); i++)
		if (i2s_boards[i].id == id)
			return &i2s_boards[i];
	return NULL;
}

/* platform devices owned by AVS audio are removed with this hook */
static void board_pdev_unregister(void *data)
{
	platform_device_unregister(data);
}

static int __maybe_unused avs_register_probe_board(struct avs_dev *adev)
{
	struct platform_device *board;
	struct snd_soc_acpi_mach mach = {{0}};
	int ret;

	ret = avs_probe_platform_register(adev, "probe-platform");
	if (ret < 0)
		return ret;

	mach.mach_params.platform = "probe-platform";

	board = platform_device_register_data(NULL, "avs_probe_mb", PLATFORM_DEVID_NONE,
					      (const void *)&mach, sizeof(mach));
	if (IS_ERR(board)) {
		dev_err(adev->dev, "probe board register failed\n");
		return PTR_ERR(board);
	}

	ret = devm_add_action(adev->dev, board_pdev_unregister, board);
	if (ret < 0) {
		platform_device_unregister(board);
		return ret;
	}
	return 0;
}

static int avs_register_dmic_board(struct avs_dev *adev)
{
	struct platform_device *codec, *board;
	struct snd_soc_acpi_mach mach = {{0}};
	struct avs_mach_pdata *pdata;
	int ret;

	if (!acpi_nhlt_find_endpoint(ACPI_NHLT_LINKTYPE_PDM, -1, -1, -1)) {
		dev_dbg(adev->dev, "no DMIC endpoints present\n");
		return 0;
	}

	codec = platform_device_register_simple("dmic-codec", PLATFORM_DEVID_NONE, NULL, 0);
	if (IS_ERR(codec)) {
		dev_err(adev->dev, "dmic codec register failed\n");
		return PTR_ERR(codec);
	}

	ret = devm_add_action(adev->dev, board_pdev_unregister, codec);
	if (ret < 0) {
		platform_device_unregister(codec);
		return ret;
	}

	ret = avs_dmic_platform_register(adev, "dmic-platform");
	if (ret < 0)
		return ret;

	pdata = devm_kzalloc(adev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;
	pdata->obsolete_card_names = obsolete_card_names;
	mach.pdata = pdata;
	mach.tplg_filename = "dmic-tplg.bin";
	mach.mach_params.platform = "dmic-platform";

	board = platform_device_register_data(NULL, "avs_dmic", PLATFORM_DEVID_NONE,
					(const void *)&mach, sizeof(mach));
	if (IS_ERR(board)) {
		dev_err(adev->dev, "dmic board register failed\n");
		return PTR_ERR(board);
	}

	ret = devm_add_action(adev->dev, board_pdev_unregister, board);
	if (ret < 0) {
		platform_device_unregister(board);
		return ret;
	}

	return 0;
}

static int avs_register_i2s_board(struct avs_dev *adev, struct snd_soc_acpi_mach *mach)
{
	struct platform_device *board;
	struct avs_mach_pdata *pdata;
	int num_ssps;
	char *name;
	int ret;
	int uid;

	num_ssps = adev->hw_cfg.i2s_caps.ctrl_count;
	if (fls(mach->mach_params.i2s_link_mask) > num_ssps) {
		dev_err(adev->dev, "Platform supports %d SSPs but board %s requires SSP%ld\n",
			num_ssps, mach->drv_name,
			(unsigned long)__fls(mach->mach_params.i2s_link_mask));
		return -ENODEV;
	}

	pdata = mach->pdata;
	if (!pdata)
		pdata = devm_kzalloc(adev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;
	pdata->obsolete_card_names = obsolete_card_names;
	mach->pdata = pdata;

	uid = mach->mach_params.i2s_link_mask;
	if (avs_mach_singular_ssp(mach))
		uid = (uid << AVS_CHANNELS_MAX) + avs_mach_ssp_tdm(mach, avs_mach_ssp_port(mach));

	name = devm_kasprintf(adev->dev, GFP_KERNEL, "%s.%d-platform", mach->drv_name, uid);
	if (!name)
		return -ENOMEM;

	ret = avs_i2s_platform_register(adev, name, mach->mach_params.i2s_link_mask, pdata->tdms);
	if (ret < 0)
		return ret;

	mach->mach_params.platform = name;

	board = platform_device_register_data(NULL, mach->drv_name, uid,
					      (const void *)mach, sizeof(*mach));
	if (IS_ERR(board)) {
		dev_err(adev->dev, "ssp board register failed\n");
		return PTR_ERR(board);
	}

	ret = devm_add_action(adev->dev, board_pdev_unregister, board);
	if (ret < 0) {
		platform_device_unregister(board);
		return ret;
	}

	return 0;
}

static int avs_register_i2s_test_board(struct avs_dev *adev, int ssp_port, int tdm_slot)
{
	struct snd_soc_acpi_mach *mach;
	int tdm_mask = BIT(tdm_slot);
	unsigned long *tdm_cfg;
	char *tplg_name;
	int ret;

	mach = devm_kzalloc(adev->dev, sizeof(*mach), GFP_KERNEL);
	tdm_cfg = devm_kcalloc(adev->dev, ssp_port + 1, sizeof(unsigned long), GFP_KERNEL);
	tplg_name = devm_kasprintf(adev->dev, GFP_KERNEL, AVS_STRING_FMT("i2s", "-test-tplg.bin",
				   ssp_port, tdm_slot));
	if (!mach || !tdm_cfg || !tplg_name)
		return -ENOMEM;

	mach->drv_name = "avs_i2s_test";
	mach->mach_params.i2s_link_mask = AVS_SSP(ssp_port);
	tdm_cfg[ssp_port] = tdm_mask;
	mach->pdata = tdm_cfg;
	mach->tplg_filename = tplg_name;

	ret = avs_register_i2s_board(adev, mach);
	if (ret < 0) {
		dev_warn(adev->dev, "register i2s %s failed: %d\n", mach->drv_name, ret);
		return ret;
	}

	return 0;
}

static int avs_register_i2s_test_boards(struct avs_dev *adev)
{
	int max_ssps = adev->hw_cfg.i2s_caps.ctrl_count;
	int ssp_port, tdm_slot, ret;
	unsigned long tdm_slots;
	u32 *array, num_elems;

	ret = parse_int_array(i2s_test, strlen(i2s_test), (int **)&array);
	if (ret) {
		dev_err(adev->dev, "failed to parse i2s_test parameter\n");
		return ret;
	}

	num_elems = *array;
	if (num_elems > max_ssps) {
		dev_err(adev->dev, "board supports only %d SSP, %d specified\n",
			max_ssps, num_elems);
		return -EINVAL;
	}

	for (ssp_port = 0; ssp_port < num_elems; ssp_port++) {
		tdm_slots = array[1 + ssp_port];
		for_each_set_bit(tdm_slot, &tdm_slots, 16) {
			ret = avs_register_i2s_test_board(adev, ssp_port, tdm_slot);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static int avs_register_i2s_boards(struct avs_dev *adev)
{
	const struct avs_acpi_boards *boards;
	struct snd_soc_acpi_mach *mach;
	int ret;

	if (!acpi_nhlt_find_endpoint(ACPI_NHLT_LINKTYPE_SSP, -1, -1, -1)) {
		dev_dbg(adev->dev, "no I2S endpoints present\n");
		return 0;
	}

	if (i2s_test)
		return avs_register_i2s_test_boards(adev);

	boards = avs_get_i2s_boards(adev);
	if (!boards) {
		dev_dbg(adev->dev, "no I2S endpoints supported\n");
		return 0;
	}

	for (mach = boards->machs; mach->id[0]; mach++) {
		if (!acpi_dev_present(mach->id, mach->uid, -1))
			continue;

		if (mach->machine_quirk)
			if (!mach->machine_quirk(mach))
				continue;

		ret = avs_register_i2s_board(adev, mach);
		if (ret < 0)
			dev_warn(adev->dev, "register i2s %s failed: %d\n", mach->drv_name, ret);
	}

	return 0;
}

static int avs_register_hda_board(struct avs_dev *adev, struct hda_codec *codec)
{
	struct snd_soc_acpi_mach mach = {{0}};
	struct platform_device *board;
	struct avs_mach_pdata *pdata;
	struct hdac_device *hdev = &codec->core;
	char *pname;
	int ret, id;

	pname = devm_kasprintf(adev->dev, GFP_KERNEL, "%s-platform", dev_name(&hdev->dev));
	if (!pname)
		return -ENOMEM;

	pdata = devm_kzalloc(adev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;
	pdata->obsolete_card_names = obsolete_card_names;
	pdata->codec = codec;

	ret = avs_hda_platform_register(adev, pname);
	if (ret < 0)
		return ret;

	mach.pdata = pdata;
	mach.mach_params.platform = pname;
	mach.tplg_filename = devm_kasprintf(adev->dev, GFP_KERNEL, "hda-%08x-tplg.bin",
					    hdev->vendor_id);
	if (!mach.tplg_filename)
		return -ENOMEM;

	id = adev->base.core.idx * HDA_MAX_CODECS + hdev->addr;
	board = platform_device_register_data(NULL, "avs_hdaudio", id, (const void *)&mach,
					      sizeof(mach));
	if (IS_ERR(board)) {
		dev_err(adev->dev, "hda board register failed\n");
		return PTR_ERR(board);
	}

	ret = devm_add_action(adev->dev, board_pdev_unregister, board);
	if (ret < 0) {
		platform_device_unregister(board);
		return ret;
	}

	return 0;
}

static int avs_register_hda_boards(struct avs_dev *adev)
{
	struct hdac_bus *bus = &adev->base.core;
	struct hdac_device *hdev;
	int ret;

	if (!bus->num_codecs) {
		dev_dbg(adev->dev, "no HDA endpoints present\n");
		return 0;
	}

	list_for_each_entry(hdev, &bus->codec_list, list) {
		struct hda_codec *codec;

		codec = dev_to_hda_codec(&hdev->dev);

		ret = avs_register_hda_board(adev, codec);
		if (ret < 0)
			dev_warn(adev->dev, "register hda-%08x failed: %d\n",
				 codec->core.vendor_id, ret);
	}

	return 0;
}

int avs_register_all_boards(struct avs_dev *adev)
{
	int ret;

#ifdef CONFIG_DEBUG_FS
	ret = avs_register_probe_board(adev);
	if (ret < 0)
		dev_warn(adev->dev, "enumerate PROBE endpoints failed: %d\n", ret);
#endif

	ret = avs_register_dmic_board(adev);
	if (ret < 0)
		dev_warn(adev->dev, "enumerate DMIC endpoints failed: %d\n",
			 ret);

	ret = avs_register_i2s_boards(adev);
	if (ret < 0)
		dev_warn(adev->dev, "enumerate I2S endpoints failed: %d\n",
			 ret);

	ret = avs_register_hda_boards(adev);
	if (ret < 0)
		dev_warn(adev->dev, "enumerate HDA endpoints failed: %d\n",
			 ret);

	return 0;
}

void avs_unregister_all_boards(struct avs_dev *adev)
{
	snd_soc_unregister_component(adev->dev);
}
