/*
 * wm_adsp.c  --  Wolfson ADSP support
 *
 * Copyright 2012 Wolfson Microelectronics plc
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/list.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>
#include <linux/debugfs.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/jack.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include <linux/mfd/arizona/registers.h>

#include "arizona.h"
#include "wm_adsp.h"

#define adsp_crit(_dsp, fmt, ...) \
	dev_crit(_dsp->dev, "DSP%d: " fmt, _dsp->num, ##__VA_ARGS__)
#define adsp_err(_dsp, fmt, ...) \
	dev_err(_dsp->dev, "DSP%d: " fmt, _dsp->num, ##__VA_ARGS__)
#define adsp_warn(_dsp, fmt, ...) \
	dev_warn(_dsp->dev, "DSP%d: " fmt, _dsp->num, ##__VA_ARGS__)
#define adsp_info(_dsp, fmt, ...) \
	dev_info(_dsp->dev, "DSP%d: " fmt, _dsp->num, ##__VA_ARGS__)
#define adsp_dbg(_dsp, fmt, ...) \
	dev_dbg(_dsp->dev, "DSP%d: " fmt, _dsp->num, ##__VA_ARGS__)

#define ADSP1_CONTROL_1                   0x00
#define ADSP1_CONTROL_2                   0x02
#define ADSP1_CONTROL_3                   0x03
#define ADSP1_CONTROL_4                   0x04
#define ADSP1_CONTROL_5                   0x06
#define ADSP1_CONTROL_6                   0x07
#define ADSP1_CONTROL_7                   0x08
#define ADSP1_CONTROL_8                   0x09
#define ADSP1_CONTROL_9                   0x0A
#define ADSP1_CONTROL_10                  0x0B
#define ADSP1_CONTROL_11                  0x0C
#define ADSP1_CONTROL_12                  0x0D
#define ADSP1_CONTROL_13                  0x0F
#define ADSP1_CONTROL_14                  0x10
#define ADSP1_CONTROL_15                  0x11
#define ADSP1_CONTROL_16                  0x12
#define ADSP1_CONTROL_17                  0x13
#define ADSP1_CONTROL_18                  0x14
#define ADSP1_CONTROL_19                  0x16
#define ADSP1_CONTROL_20                  0x17
#define ADSP1_CONTROL_21                  0x18
#define ADSP1_CONTROL_22                  0x1A
#define ADSP1_CONTROL_23                  0x1B
#define ADSP1_CONTROL_24                  0x1C
#define ADSP1_CONTROL_25                  0x1E
#define ADSP1_CONTROL_26                  0x20
#define ADSP1_CONTROL_27                  0x21
#define ADSP1_CONTROL_28                  0x22
#define ADSP1_CONTROL_29                  0x23
#define ADSP1_CONTROL_30                  0x24
#define ADSP1_CONTROL_31                  0x26

/*
 * ADSP1 Control 19
 */
#define ADSP1_WDMA_BUFFER_LENGTH_MASK     0x00FF  /* DSP1_WDMA_BUFFER_LENGTH - [7:0] */
#define ADSP1_WDMA_BUFFER_LENGTH_SHIFT         0  /* DSP1_WDMA_BUFFER_LENGTH - [7:0] */
#define ADSP1_WDMA_BUFFER_LENGTH_WIDTH         8  /* DSP1_WDMA_BUFFER_LENGTH - [7:0] */


/*
 * ADSP1 Control 30
 */
#define ADSP1_DBG_CLK_ENA                 0x0008  /* DSP1_DBG_CLK_ENA */
#define ADSP1_DBG_CLK_ENA_MASK            0x0008  /* DSP1_DBG_CLK_ENA */
#define ADSP1_DBG_CLK_ENA_SHIFT                3  /* DSP1_DBG_CLK_ENA */
#define ADSP1_DBG_CLK_ENA_WIDTH                1  /* DSP1_DBG_CLK_ENA */
#define ADSP1_SYS_ENA                     0x0004  /* DSP1_SYS_ENA */
#define ADSP1_SYS_ENA_MASK                0x0004  /* DSP1_SYS_ENA */
#define ADSP1_SYS_ENA_SHIFT                    2  /* DSP1_SYS_ENA */
#define ADSP1_SYS_ENA_WIDTH                    1  /* DSP1_SYS_ENA */
#define ADSP1_CORE_ENA                    0x0002  /* DSP1_CORE_ENA */
#define ADSP1_CORE_ENA_MASK               0x0002  /* DSP1_CORE_ENA */
#define ADSP1_CORE_ENA_SHIFT                   1  /* DSP1_CORE_ENA */
#define ADSP1_CORE_ENA_WIDTH                   1  /* DSP1_CORE_ENA */
#define ADSP1_START                       0x0001  /* DSP1_START */
#define ADSP1_START_MASK                  0x0001  /* DSP1_START */
#define ADSP1_START_SHIFT                      0  /* DSP1_START */
#define ADSP1_START_WIDTH                      1  /* DSP1_START */

/*
 * ADSP1 Control 31
 */
#define ADSP1_CLK_SEL_MASK                0x0007  /* CLK_SEL_ENA */
#define ADSP1_CLK_SEL_SHIFT                    0  /* CLK_SEL_ENA */
#define ADSP1_CLK_SEL_WIDTH                    3  /* CLK_SEL_ENA */

#define ADSP2_CONTROL        0x0
#define ADSP2_CLOCKING       0x1
#define ADSP2_STATUS1        0x4
#define ADSP2_WDMA_CONFIG_1 0x30
#define ADSP2_WDMA_CONFIG_2 0x31
#define ADSP2_RDMA_CONFIG_1 0x34

#define ADSP2_SCRATCH0        0x40
#define ADSP2_SCRATCH1        0x41
#define ADSP2_SCRATCH2        0x42
#define ADSP2_SCRATCH3        0x43

/*
 * ADSP2 Control
 */

#define ADSP2_MEM_ENA                     0x0010  /* DSP1_MEM_ENA */
#define ADSP2_MEM_ENA_MASK                0x0010  /* DSP1_MEM_ENA */
#define ADSP2_MEM_ENA_SHIFT                    4  /* DSP1_MEM_ENA */
#define ADSP2_MEM_ENA_WIDTH                    1  /* DSP1_MEM_ENA */
#define ADSP2_SYS_ENA                     0x0004  /* DSP1_SYS_ENA */
#define ADSP2_SYS_ENA_MASK                0x0004  /* DSP1_SYS_ENA */
#define ADSP2_SYS_ENA_SHIFT                    2  /* DSP1_SYS_ENA */
#define ADSP2_SYS_ENA_WIDTH                    1  /* DSP1_SYS_ENA */
#define ADSP2_CORE_ENA                    0x0002  /* DSP1_CORE_ENA */
#define ADSP2_CORE_ENA_MASK               0x0002  /* DSP1_CORE_ENA */
#define ADSP2_CORE_ENA_SHIFT                   1  /* DSP1_CORE_ENA */
#define ADSP2_CORE_ENA_WIDTH                   1  /* DSP1_CORE_ENA */
#define ADSP2_START                       0x0001  /* DSP1_START */
#define ADSP2_START_MASK                  0x0001  /* DSP1_START */
#define ADSP2_START_SHIFT                      0  /* DSP1_START */
#define ADSP2_START_WIDTH                      1  /* DSP1_START */

/*
 * ADSP2 clocking
 */
#define ADSP2_CLK_SEL_MASK                0x0007  /* CLK_SEL_ENA */
#define ADSP2_CLK_SEL_SHIFT                    0  /* CLK_SEL_ENA */
#define ADSP2_CLK_SEL_WIDTH                    3  /* CLK_SEL_ENA */

/*
 * ADSP2 Status 1
 */
#define ADSP2_RAM_RDY                     0x0001
#define ADSP2_RAM_RDY_MASK                0x0001
#define ADSP2_RAM_RDY_SHIFT                    0
#define ADSP2_RAM_RDY_WIDTH                    1

struct wm_adsp_buf {
	struct list_head list;
	void *buf;
};

static struct wm_adsp_buf *wm_adsp_buf_alloc(const void *src, size_t len,
					     struct list_head *list)
{
	struct wm_adsp_buf *buf = kzalloc(sizeof(*buf), GFP_KERNEL);

	if (buf == NULL)
		return NULL;

	buf->buf = vmalloc(len);
	if (!buf->buf) {
		vfree(buf);
		return NULL;
	}
	memcpy(buf->buf, src, len);

	if (list)
		list_add_tail(&buf->list, list);

	return buf;
}

static void wm_adsp_buf_free(struct list_head *list)
{
	while (!list_empty(list)) {
		struct wm_adsp_buf *buf = list_first_entry(list,
							   struct wm_adsp_buf,
							   list);
		list_del(&buf->list);
		vfree(buf->buf);
		kfree(buf);
	}
}

#define WM_ADSP_NUM_FW 4

#define WM_ADSP_FW_MBC_VSS 0
#define WM_ADSP_FW_TX      1
#define WM_ADSP_FW_TX_SPK  2
#define WM_ADSP_FW_RX_ANC  3

static const char *wm_adsp_fw_text[WM_ADSP_NUM_FW] = {
	[WM_ADSP_FW_MBC_VSS] = "MBC/VSS",
	[WM_ADSP_FW_TX] =      "Tx",
	[WM_ADSP_FW_TX_SPK] =  "Tx Speaker",
	[WM_ADSP_FW_RX_ANC] =  "Rx ANC",
};

static struct {
	const char *file;
} wm_adsp_fw[WM_ADSP_NUM_FW] = {
	[WM_ADSP_FW_MBC_VSS] = { .file = "mbc-vss" },
	[WM_ADSP_FW_TX] =      { .file = "tx" },
	[WM_ADSP_FW_TX_SPK] =  { .file = "tx-spk" },
	[WM_ADSP_FW_RX_ANC] =  { .file = "rx-anc" },
};

struct wm_coeff_ctl_ops {
	int (*xget)(struct snd_kcontrol *kcontrol,
		    struct snd_ctl_elem_value *ucontrol);
	int (*xput)(struct snd_kcontrol *kcontrol,
		    struct snd_ctl_elem_value *ucontrol);
	int (*xinfo)(struct snd_kcontrol *kcontrol,
		     struct snd_ctl_elem_info *uinfo);
};

struct wm_coeff_ctl {
	const char *name;
	const char *fw_name;
	struct wm_adsp_alg_region alg_region;
	struct wm_coeff_ctl_ops ops;
	struct wm_adsp *dsp;
	unsigned int enabled:1;
	struct list_head list;
	void *cache;
	unsigned int offset;
	size_t len;
	unsigned int set:1;
	struct snd_kcontrol *kcontrol;
	unsigned int flags;
};

#ifdef CONFIG_DEBUG_FS
static void wm_adsp_debugfs_save_wmfwname(struct wm_adsp *dsp, const char *s)
{
	char *tmp = kasprintf(GFP_KERNEL, "%s\n", s);

	mutex_lock(&dsp->debugfs_lock);
	kfree(dsp->wmfw_file_name);
	dsp->wmfw_file_name = tmp;
	mutex_unlock(&dsp->debugfs_lock);
}

static void wm_adsp_debugfs_save_binname(struct wm_adsp *dsp, const char *s)
{
	char *tmp = kasprintf(GFP_KERNEL, "%s\n", s);

	mutex_lock(&dsp->debugfs_lock);
	kfree(dsp->bin_file_name);
	dsp->bin_file_name = tmp;
	mutex_unlock(&dsp->debugfs_lock);
}

static void wm_adsp_debugfs_clear(struct wm_adsp *dsp)
{
	mutex_lock(&dsp->debugfs_lock);
	kfree(dsp->wmfw_file_name);
	kfree(dsp->bin_file_name);
	dsp->wmfw_file_name = NULL;
	dsp->bin_file_name = NULL;
	mutex_unlock(&dsp->debugfs_lock);
}

static ssize_t wm_adsp_debugfs_wmfw_read(struct file *file,
					 char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	struct wm_adsp *dsp = file->private_data;
	ssize_t ret;

	mutex_lock(&dsp->debugfs_lock);

	if (!dsp->wmfw_file_name || !dsp->running)
		ret = 0;
	else
		ret = simple_read_from_buffer(user_buf, count, ppos,
					      dsp->wmfw_file_name,
					      strlen(dsp->wmfw_file_name));

	mutex_unlock(&dsp->debugfs_lock);
	return ret;
}

static ssize_t wm_adsp_debugfs_bin_read(struct file *file,
					char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct wm_adsp *dsp = file->private_data;
	ssize_t ret;

	mutex_lock(&dsp->debugfs_lock);

	if (!dsp->bin_file_name || !dsp->running)
		ret = 0;
	else
		ret = simple_read_from_buffer(user_buf, count, ppos,
					      dsp->bin_file_name,
					      strlen(dsp->bin_file_name));

	mutex_unlock(&dsp->debugfs_lock);
	return ret;
}

static const struct {
	const char *name;
	const struct file_operations fops;
} wm_adsp_debugfs_fops[] = {
	{
		.name = "wmfw_file_name",
		.fops = {
			.open = simple_open,
			.read = wm_adsp_debugfs_wmfw_read,
		},
	},
	{
		.name = "bin_file_name",
		.fops = {
			.open = simple_open,
			.read = wm_adsp_debugfs_bin_read,
		},
	},
};

static void wm_adsp2_init_debugfs(struct wm_adsp *dsp,
				  struct snd_soc_codec *codec)
{
	struct dentry *root = NULL;
	char *root_name;
	int i;

	if (!codec->component.debugfs_root) {
		adsp_err(dsp, "No codec debugfs root\n");
		goto err;
	}

	root_name = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!root_name)
		goto err;

	snprintf(root_name, PAGE_SIZE, "dsp%d", dsp->num);
	root = debugfs_create_dir(root_name, codec->component.debugfs_root);
	kfree(root_name);

	if (!root)
		goto err;

	if (!debugfs_create_bool("running", S_IRUGO, root, &dsp->running))
		goto err;

	if (!debugfs_create_x32("fw_id", S_IRUGO, root, &dsp->fw_id))
		goto err;

	if (!debugfs_create_x32("fw_version", S_IRUGO, root,
				&dsp->fw_id_version))
		goto err;

	for (i = 0; i < ARRAY_SIZE(wm_adsp_debugfs_fops); ++i) {
		if (!debugfs_create_file(wm_adsp_debugfs_fops[i].name,
					 S_IRUGO, root, dsp,
					 &wm_adsp_debugfs_fops[i].fops))
			goto err;
	}

	dsp->debugfs_root = root;
	return;

err:
	debugfs_remove_recursive(root);
	adsp_err(dsp, "Failed to create debugfs\n");
}

static void wm_adsp2_cleanup_debugfs(struct wm_adsp *dsp)
{
	wm_adsp_debugfs_clear(dsp);
	debugfs_remove_recursive(dsp->debugfs_root);
}
#else
static inline void wm_adsp2_init_debugfs(struct wm_adsp *dsp,
					 struct snd_soc_codec *codec)
{
}

static inline void wm_adsp2_cleanup_debugfs(struct wm_adsp *dsp)
{
}

static inline void wm_adsp_debugfs_save_wmfwname(struct wm_adsp *dsp,
						 const char *s)
{
}

static inline void wm_adsp_debugfs_save_binname(struct wm_adsp *dsp,
						const char *s)
{
}

static inline void wm_adsp_debugfs_clear(struct wm_adsp *dsp)
{
}
#endif

static int wm_adsp_fw_get(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	struct wm_adsp *dsp = snd_soc_codec_get_drvdata(codec);

	ucontrol->value.integer.value[0] = dsp[e->shift_l].fw;

	return 0;
}

static int wm_adsp_fw_put(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	struct wm_adsp *dsp = snd_soc_codec_get_drvdata(codec);

	if (ucontrol->value.integer.value[0] == dsp[e->shift_l].fw)
		return 0;

	if (ucontrol->value.integer.value[0] >= WM_ADSP_NUM_FW)
		return -EINVAL;

	if (dsp[e->shift_l].running)
		return -EBUSY;

	dsp[e->shift_l].fw = ucontrol->value.integer.value[0];

	return 0;
}

static const struct soc_enum wm_adsp_fw_enum[] = {
	SOC_ENUM_SINGLE(0, 0, ARRAY_SIZE(wm_adsp_fw_text), wm_adsp_fw_text),
	SOC_ENUM_SINGLE(0, 1, ARRAY_SIZE(wm_adsp_fw_text), wm_adsp_fw_text),
	SOC_ENUM_SINGLE(0, 2, ARRAY_SIZE(wm_adsp_fw_text), wm_adsp_fw_text),
	SOC_ENUM_SINGLE(0, 3, ARRAY_SIZE(wm_adsp_fw_text), wm_adsp_fw_text),
};

const struct snd_kcontrol_new wm_adsp_fw_controls[] = {
	SOC_ENUM_EXT("DSP1 Firmware", wm_adsp_fw_enum[0],
		     wm_adsp_fw_get, wm_adsp_fw_put),
	SOC_ENUM_EXT("DSP2 Firmware", wm_adsp_fw_enum[1],
		     wm_adsp_fw_get, wm_adsp_fw_put),
	SOC_ENUM_EXT("DSP3 Firmware", wm_adsp_fw_enum[2],
		     wm_adsp_fw_get, wm_adsp_fw_put),
	SOC_ENUM_EXT("DSP4 Firmware", wm_adsp_fw_enum[3],
		     wm_adsp_fw_get, wm_adsp_fw_put),
};
EXPORT_SYMBOL_GPL(wm_adsp_fw_controls);

static struct wm_adsp_region const *wm_adsp_find_region(struct wm_adsp *dsp,
							int type)
{
	int i;

	for (i = 0; i < dsp->num_mems; i++)
		if (dsp->mem[i].type == type)
			return &dsp->mem[i];

	return NULL;
}

static unsigned int wm_adsp_region_to_reg(struct wm_adsp_region const *mem,
					  unsigned int offset)
{
	if (WARN_ON(!mem))
		return offset;
	switch (mem->type) {
	case WMFW_ADSP1_PM:
		return mem->base + (offset * 3);
	case WMFW_ADSP1_DM:
		return mem->base + (offset * 2);
	case WMFW_ADSP2_XM:
		return mem->base + (offset * 2);
	case WMFW_ADSP2_YM:
		return mem->base + (offset * 2);
	case WMFW_ADSP1_ZM:
		return mem->base + (offset * 2);
	default:
		WARN(1, "Unknown memory region type");
		return offset;
	}
}

static void wm_adsp2_show_fw_status(struct wm_adsp *dsp)
{
	u16 scratch[4];
	int ret;

	ret = regmap_raw_read(dsp->regmap, dsp->base + ADSP2_SCRATCH0,
				scratch, sizeof(scratch));
	if (ret) {
		adsp_err(dsp, "Failed to read SCRATCH regs: %d\n", ret);
		return;
	}

	adsp_dbg(dsp, "FW SCRATCH 0:0x%x 1:0x%x 2:0x%x 3:0x%x\n",
		 be16_to_cpu(scratch[0]),
		 be16_to_cpu(scratch[1]),
		 be16_to_cpu(scratch[2]),
		 be16_to_cpu(scratch[3]));
}

static int wm_coeff_info(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_info *uinfo)
{
	struct wm_coeff_ctl *ctl = (struct wm_coeff_ctl *)kcontrol->private_value;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_BYTES;
	uinfo->count = ctl->len;
	return 0;
}

static int wm_coeff_write_control(struct wm_coeff_ctl *ctl,
				  const void *buf, size_t len)
{
	struct wm_adsp_alg_region *alg_region = &ctl->alg_region;
	const struct wm_adsp_region *mem;
	struct wm_adsp *dsp = ctl->dsp;
	void *scratch;
	int ret;
	unsigned int reg;

	mem = wm_adsp_find_region(dsp, alg_region->type);
	if (!mem) {
		adsp_err(dsp, "No base for region %x\n",
			 alg_region->type);
		return -EINVAL;
	}

	reg = ctl->alg_region.base + ctl->offset;
	reg = wm_adsp_region_to_reg(mem, reg);

	scratch = kmemdup(buf, ctl->len, GFP_KERNEL | GFP_DMA);
	if (!scratch)
		return -ENOMEM;

	ret = regmap_raw_write(dsp->regmap, reg, scratch,
			       ctl->len);
	if (ret) {
		adsp_err(dsp, "Failed to write %zu bytes to %x: %d\n",
			 ctl->len, reg, ret);
		kfree(scratch);
		return ret;
	}
	adsp_dbg(dsp, "Wrote %zu bytes to %x\n", ctl->len, reg);

	kfree(scratch);

	return 0;
}

static int wm_coeff_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct wm_coeff_ctl *ctl = (struct wm_coeff_ctl *)kcontrol->private_value;
	char *p = ucontrol->value.bytes.data;

	memcpy(ctl->cache, p, ctl->len);

	ctl->set = 1;
	if (!ctl->enabled)
		return 0;

	return wm_coeff_write_control(ctl, p, ctl->len);
}

static int wm_coeff_read_control(struct wm_coeff_ctl *ctl,
				 void *buf, size_t len)
{
	struct wm_adsp_alg_region *alg_region = &ctl->alg_region;
	const struct wm_adsp_region *mem;
	struct wm_adsp *dsp = ctl->dsp;
	void *scratch;
	int ret;
	unsigned int reg;

	mem = wm_adsp_find_region(dsp, alg_region->type);
	if (!mem) {
		adsp_err(dsp, "No base for region %x\n",
			 alg_region->type);
		return -EINVAL;
	}

	reg = ctl->alg_region.base + ctl->offset;
	reg = wm_adsp_region_to_reg(mem, reg);

	scratch = kmalloc(ctl->len, GFP_KERNEL | GFP_DMA);
	if (!scratch)
		return -ENOMEM;

	ret = regmap_raw_read(dsp->regmap, reg, scratch, ctl->len);
	if (ret) {
		adsp_err(dsp, "Failed to read %zu bytes from %x: %d\n",
			 ctl->len, reg, ret);
		kfree(scratch);
		return ret;
	}
	adsp_dbg(dsp, "Read %zu bytes from %x\n", ctl->len, reg);

	memcpy(buf, scratch, ctl->len);
	kfree(scratch);

	return 0;
}

static int wm_coeff_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct wm_coeff_ctl *ctl = (struct wm_coeff_ctl *)kcontrol->private_value;
	char *p = ucontrol->value.bytes.data;

	if (ctl->flags & WMFW_CTL_FLAG_VOLATILE) {
		if (ctl->enabled)
			return wm_coeff_read_control(ctl, p, ctl->len);
		else
			return -EPERM;
	}

	memcpy(p, ctl->cache, ctl->len);

	return 0;
}

struct wmfw_ctl_work {
	struct wm_adsp *dsp;
	struct wm_coeff_ctl *ctl;
	struct work_struct work;
};

static int wmfw_add_ctl(struct wm_adsp *dsp, struct wm_coeff_ctl *ctl)
{
	struct snd_kcontrol_new *kcontrol;
	int ret;

	if (!ctl || !ctl->name)
		return -EINVAL;

	kcontrol = kzalloc(sizeof(*kcontrol), GFP_KERNEL);
	if (!kcontrol)
		return -ENOMEM;
	kcontrol->iface = SNDRV_CTL_ELEM_IFACE_MIXER;

	kcontrol->name = ctl->name;
	kcontrol->info = wm_coeff_info;
	kcontrol->get = wm_coeff_get;
	kcontrol->put = wm_coeff_put;
	kcontrol->private_value = (unsigned long)ctl;

	if (ctl->flags) {
		if (ctl->flags & WMFW_CTL_FLAG_WRITEABLE)
			kcontrol->access |= SNDRV_CTL_ELEM_ACCESS_WRITE;
		if (ctl->flags & WMFW_CTL_FLAG_READABLE)
			kcontrol->access |= SNDRV_CTL_ELEM_ACCESS_READ;
		if (ctl->flags & WMFW_CTL_FLAG_VOLATILE)
			kcontrol->access |= SNDRV_CTL_ELEM_ACCESS_VOLATILE;
	}

	ret = snd_soc_add_card_controls(dsp->card,
					kcontrol, 1);
	if (ret < 0)
		goto err_kcontrol;

	kfree(kcontrol);

	ctl->kcontrol = snd_soc_card_get_kcontrol(dsp->card,
						  ctl->name);

	return 0;

err_kcontrol:
	kfree(kcontrol);
	return ret;
}

static int wm_coeff_init_control_caches(struct wm_adsp *dsp)
{
	struct wm_coeff_ctl *ctl;
	int ret;

	list_for_each_entry(ctl, &dsp->ctl_list, list) {
		if (!ctl->enabled || ctl->set)
			continue;
		if (ctl->flags & WMFW_CTL_FLAG_VOLATILE)
			continue;

		ret = wm_coeff_read_control(ctl,
					    ctl->cache,
					    ctl->len);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int wm_coeff_sync_controls(struct wm_adsp *dsp)
{
	struct wm_coeff_ctl *ctl;
	int ret;

	list_for_each_entry(ctl, &dsp->ctl_list, list) {
		if (!ctl->enabled)
			continue;
		if (ctl->set && !(ctl->flags & WMFW_CTL_FLAG_VOLATILE)) {
			ret = wm_coeff_write_control(ctl,
						     ctl->cache,
						     ctl->len);
			if (ret < 0)
				return ret;
		}
	}

	return 0;
}

static void wm_adsp_ctl_work(struct work_struct *work)
{
	struct wmfw_ctl_work *ctl_work = container_of(work,
						      struct wmfw_ctl_work,
						      work);

	wmfw_add_ctl(ctl_work->dsp, ctl_work->ctl);
	kfree(ctl_work);
}

static int wm_adsp_create_control(struct wm_adsp *dsp,
				  const struct wm_adsp_alg_region *alg_region,
				  unsigned int offset, unsigned int len,
				  const char *subname, unsigned int subname_len,
				  unsigned int flags)
{
	struct wm_coeff_ctl *ctl;
	struct wmfw_ctl_work *ctl_work;
	char name[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
	char *region_name;
	int ret;

	if (flags & WMFW_CTL_FLAG_SYS)
		return 0;

	switch (alg_region->type) {
	case WMFW_ADSP1_PM:
		region_name = "PM";
		break;
	case WMFW_ADSP1_DM:
		region_name = "DM";
		break;
	case WMFW_ADSP2_XM:
		region_name = "XM";
		break;
	case WMFW_ADSP2_YM:
		region_name = "YM";
		break;
	case WMFW_ADSP1_ZM:
		region_name = "ZM";
		break;
	default:
		adsp_err(dsp, "Unknown region type: %d\n", alg_region->type);
		return -EINVAL;
	}

	switch (dsp->fw_ver) {
	case 0:
	case 1:
		snprintf(name, SNDRV_CTL_ELEM_ID_NAME_MAXLEN, "DSP%d %s %x",
			 dsp->num, region_name, alg_region->alg);
		break;
	default:
		ret = snprintf(name, SNDRV_CTL_ELEM_ID_NAME_MAXLEN,
				"DSP%d%c %.12s %x", dsp->num, *region_name,
				wm_adsp_fw_text[dsp->fw], alg_region->alg);

		/* Truncate the subname from the start if it is too long */
		if (subname) {
			int avail = SNDRV_CTL_ELEM_ID_NAME_MAXLEN - ret - 2;
			int skip = 0;

			if (subname_len > avail)
				skip = subname_len - avail;

			snprintf(name + ret,
				 SNDRV_CTL_ELEM_ID_NAME_MAXLEN - ret, " %.*s",
				 subname_len - skip, subname + skip);
		}
		break;
	}

	list_for_each_entry(ctl, &dsp->ctl_list,
			    list) {
		if (!strcmp(ctl->name, name)) {
			if (!ctl->enabled)
				ctl->enabled = 1;
			return 0;
		}
	}

	ctl = kzalloc(sizeof(*ctl), GFP_KERNEL);
	if (!ctl)
		return -ENOMEM;
	ctl->fw_name = wm_adsp_fw_text[dsp->fw];
	ctl->alg_region = *alg_region;
	ctl->name = kmemdup(name, strlen(name) + 1, GFP_KERNEL);
	if (!ctl->name) {
		ret = -ENOMEM;
		goto err_ctl;
	}
	ctl->enabled = 1;
	ctl->set = 0;
	ctl->ops.xget = wm_coeff_get;
	ctl->ops.xput = wm_coeff_put;
	ctl->dsp = dsp;

	ctl->flags = flags;
	ctl->offset = offset;
	if (len > 512) {
		adsp_warn(dsp, "Truncating control %s from %d\n",
			  ctl->name, len);
		len = 512;
	}
	ctl->len = len;
	ctl->cache = kzalloc(ctl->len, GFP_KERNEL);
	if (!ctl->cache) {
		ret = -ENOMEM;
		goto err_ctl_name;
	}

	list_add(&ctl->list, &dsp->ctl_list);

	ctl_work = kzalloc(sizeof(*ctl_work), GFP_KERNEL);
	if (!ctl_work) {
		ret = -ENOMEM;
		goto err_ctl_cache;
	}

	ctl_work->dsp = dsp;
	ctl_work->ctl = ctl;
	INIT_WORK(&ctl_work->work, wm_adsp_ctl_work);
	schedule_work(&ctl_work->work);

	return 0;

err_ctl_cache:
	kfree(ctl->cache);
err_ctl_name:
	kfree(ctl->name);
err_ctl:
	kfree(ctl);

	return ret;
}

struct wm_coeff_parsed_alg {
	int id;
	const u8 *name;
	int name_len;
	int ncoeff;
};

struct wm_coeff_parsed_coeff {
	int offset;
	int mem_type;
	const u8 *name;
	int name_len;
	int ctl_type;
	int flags;
	int len;
};

static int wm_coeff_parse_string(int bytes, const u8 **pos, const u8 **str)
{
	int length;

	switch (bytes) {
	case 1:
		length = **pos;
		break;
	case 2:
		length = le16_to_cpu(*((__le16 *)*pos));
		break;
	default:
		return 0;
	}

	if (str)
		*str = *pos + bytes;

	*pos += ((length + bytes) + 3) & ~0x03;

	return length;
}

static int wm_coeff_parse_int(int bytes, const u8 **pos)
{
	int val = 0;

	switch (bytes) {
	case 2:
		val = le16_to_cpu(*((__le16 *)*pos));
		break;
	case 4:
		val = le32_to_cpu(*((__le32 *)*pos));
		break;
	default:
		break;
	}

	*pos += bytes;

	return val;
}

static inline void wm_coeff_parse_alg(struct wm_adsp *dsp, const u8 **data,
				      struct wm_coeff_parsed_alg *blk)
{
	const struct wmfw_adsp_alg_data *raw;

	switch (dsp->fw_ver) {
	case 0:
	case 1:
		raw = (const struct wmfw_adsp_alg_data *)*data;
		*data = raw->data;

		blk->id = le32_to_cpu(raw->id);
		blk->name = raw->name;
		blk->name_len = strlen(raw->name);
		blk->ncoeff = le32_to_cpu(raw->ncoeff);
		break;
	default:
		blk->id = wm_coeff_parse_int(sizeof(raw->id), data);
		blk->name_len = wm_coeff_parse_string(sizeof(u8), data,
						      &blk->name);
		wm_coeff_parse_string(sizeof(u16), data, NULL);
		blk->ncoeff = wm_coeff_parse_int(sizeof(raw->ncoeff), data);
		break;
	}

	adsp_dbg(dsp, "Algorithm ID: %#x\n", blk->id);
	adsp_dbg(dsp, "Algorithm name: %.*s\n", blk->name_len, blk->name);
	adsp_dbg(dsp, "# of coefficient descriptors: %#x\n", blk->ncoeff);
}

static inline void wm_coeff_parse_coeff(struct wm_adsp *dsp, const u8 **data,
					struct wm_coeff_parsed_coeff *blk)
{
	const struct wmfw_adsp_coeff_data *raw;
	const u8 *tmp;
	int length;

	switch (dsp->fw_ver) {
	case 0:
	case 1:
		raw = (const struct wmfw_adsp_coeff_data *)*data;
		*data = *data + sizeof(raw->hdr) + le32_to_cpu(raw->hdr.size);

		blk->offset = le16_to_cpu(raw->hdr.offset);
		blk->mem_type = le16_to_cpu(raw->hdr.type);
		blk->name = raw->name;
		blk->name_len = strlen(raw->name);
		blk->ctl_type = le16_to_cpu(raw->ctl_type);
		blk->flags = le16_to_cpu(raw->flags);
		blk->len = le32_to_cpu(raw->len);
		break;
	default:
		tmp = *data;
		blk->offset = wm_coeff_parse_int(sizeof(raw->hdr.offset), &tmp);
		blk->mem_type = wm_coeff_parse_int(sizeof(raw->hdr.type), &tmp);
		length = wm_coeff_parse_int(sizeof(raw->hdr.size), &tmp);
		blk->name_len = wm_coeff_parse_string(sizeof(u8), &tmp,
						      &blk->name);
		wm_coeff_parse_string(sizeof(u8), &tmp, NULL);
		wm_coeff_parse_string(sizeof(u16), &tmp, NULL);
		blk->ctl_type = wm_coeff_parse_int(sizeof(raw->ctl_type), &tmp);
		blk->flags = wm_coeff_parse_int(sizeof(raw->flags), &tmp);
		blk->len = wm_coeff_parse_int(sizeof(raw->len), &tmp);

		*data = *data + sizeof(raw->hdr) + length;
		break;
	}

	adsp_dbg(dsp, "\tCoefficient type: %#x\n", blk->mem_type);
	adsp_dbg(dsp, "\tCoefficient offset: %#x\n", blk->offset);
	adsp_dbg(dsp, "\tCoefficient name: %.*s\n", blk->name_len, blk->name);
	adsp_dbg(dsp, "\tCoefficient flags: %#x\n", blk->flags);
	adsp_dbg(dsp, "\tALSA control type: %#x\n", blk->ctl_type);
	adsp_dbg(dsp, "\tALSA control len: %#x\n", blk->len);
}

static int wm_adsp_parse_coeff(struct wm_adsp *dsp,
			       const struct wmfw_region *region)
{
	struct wm_adsp_alg_region alg_region = {};
	struct wm_coeff_parsed_alg alg_blk;
	struct wm_coeff_parsed_coeff coeff_blk;
	const u8 *data = region->data;
	int i, ret;

	wm_coeff_parse_alg(dsp, &data, &alg_blk);
	for (i = 0; i < alg_blk.ncoeff; i++) {
		wm_coeff_parse_coeff(dsp, &data, &coeff_blk);

		switch (coeff_blk.ctl_type) {
		case SNDRV_CTL_ELEM_TYPE_BYTES:
			break;
		default:
			adsp_err(dsp, "Unknown control type: %d\n",
				 coeff_blk.ctl_type);
			return -EINVAL;
		}

		alg_region.type = coeff_blk.mem_type;
		alg_region.alg = alg_blk.id;

		ret = wm_adsp_create_control(dsp, &alg_region,
					     coeff_blk.offset,
					     coeff_blk.len,
					     coeff_blk.name,
					     coeff_blk.name_len,
					     coeff_blk.flags);
		if (ret < 0)
			adsp_err(dsp, "Failed to create control: %.*s, %d\n",
				 coeff_blk.name_len, coeff_blk.name, ret);
	}

	return 0;
}

static int wm_adsp_load(struct wm_adsp *dsp)
{
	LIST_HEAD(buf_list);
	const struct firmware *firmware;
	struct regmap *regmap = dsp->regmap;
	unsigned int pos = 0;
	const struct wmfw_header *header;
	const struct wmfw_adsp1_sizes *adsp1_sizes;
	const struct wmfw_adsp2_sizes *adsp2_sizes;
	const struct wmfw_footer *footer;
	const struct wmfw_region *region;
	const struct wm_adsp_region *mem;
	const char *region_name;
	char *file, *text;
	struct wm_adsp_buf *buf;
	unsigned int reg;
	int regions = 0;
	int ret, offset, type, sizes;

	file = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (file == NULL)
		return -ENOMEM;

	snprintf(file, PAGE_SIZE, "%s-dsp%d-%s.wmfw", dsp->part, dsp->num,
		 wm_adsp_fw[dsp->fw].file);
	file[PAGE_SIZE - 1] = '\0';

	ret = request_firmware(&firmware, file, dsp->dev);
	if (ret != 0) {
		adsp_err(dsp, "Failed to request '%s'\n", file);
		goto out;
	}
	ret = -EINVAL;

	pos = sizeof(*header) + sizeof(*adsp1_sizes) + sizeof(*footer);
	if (pos >= firmware->size) {
		adsp_err(dsp, "%s: file too short, %zu bytes\n",
			 file, firmware->size);
		goto out_fw;
	}

	header = (void*)&firmware->data[0];

	if (memcmp(&header->magic[0], "WMFW", 4) != 0) {
		adsp_err(dsp, "%s: invalid magic\n", file);
		goto out_fw;
	}

	switch (header->ver) {
	case 0:
		adsp_warn(dsp, "%s: Depreciated file format %d\n",
			  file, header->ver);
		break;
	case 1:
	case 2:
		break;
	default:
		adsp_err(dsp, "%s: unknown file format %d\n",
			 file, header->ver);
		goto out_fw;
	}

	adsp_info(dsp, "Firmware version: %d\n", header->ver);
	dsp->fw_ver = header->ver;

	if (header->core != dsp->type) {
		adsp_err(dsp, "%s: invalid core %d != %d\n",
			 file, header->core, dsp->type);
		goto out_fw;
	}

	switch (dsp->type) {
	case WMFW_ADSP1:
		pos = sizeof(*header) + sizeof(*adsp1_sizes) + sizeof(*footer);
		adsp1_sizes = (void *)&(header[1]);
		footer = (void *)&(adsp1_sizes[1]);
		sizes = sizeof(*adsp1_sizes);

		adsp_dbg(dsp, "%s: %d DM, %d PM, %d ZM\n",
			 file, le32_to_cpu(adsp1_sizes->dm),
			 le32_to_cpu(adsp1_sizes->pm),
			 le32_to_cpu(adsp1_sizes->zm));
		break;

	case WMFW_ADSP2:
		pos = sizeof(*header) + sizeof(*adsp2_sizes) + sizeof(*footer);
		adsp2_sizes = (void *)&(header[1]);
		footer = (void *)&(adsp2_sizes[1]);
		sizes = sizeof(*adsp2_sizes);

		adsp_dbg(dsp, "%s: %d XM, %d YM %d PM, %d ZM\n",
			 file, le32_to_cpu(adsp2_sizes->xm),
			 le32_to_cpu(adsp2_sizes->ym),
			 le32_to_cpu(adsp2_sizes->pm),
			 le32_to_cpu(adsp2_sizes->zm));
		break;

	default:
		WARN(1, "Unknown DSP type");
		goto out_fw;
	}

	if (le32_to_cpu(header->len) != sizeof(*header) +
	    sizes + sizeof(*footer)) {
		adsp_err(dsp, "%s: unexpected header length %d\n",
			 file, le32_to_cpu(header->len));
		goto out_fw;
	}

	adsp_dbg(dsp, "%s: timestamp %llu\n", file,
		 le64_to_cpu(footer->timestamp));

	while (pos < firmware->size &&
	       pos - firmware->size > sizeof(*region)) {
		region = (void *)&(firmware->data[pos]);
		region_name = "Unknown";
		reg = 0;
		text = NULL;
		offset = le32_to_cpu(region->offset) & 0xffffff;
		type = be32_to_cpu(region->type) & 0xff;
		mem = wm_adsp_find_region(dsp, type);
		
		switch (type) {
		case WMFW_NAME_TEXT:
			region_name = "Firmware name";
			text = kzalloc(le32_to_cpu(region->len) + 1,
				       GFP_KERNEL);
			break;
		case WMFW_ALGORITHM_DATA:
			region_name = "Algorithm";
			ret = wm_adsp_parse_coeff(dsp, region);
			if (ret != 0)
				goto out_fw;
			break;
		case WMFW_INFO_TEXT:
			region_name = "Information";
			text = kzalloc(le32_to_cpu(region->len) + 1,
				       GFP_KERNEL);
			break;
		case WMFW_ABSOLUTE:
			region_name = "Absolute";
			reg = offset;
			break;
		case WMFW_ADSP1_PM:
			region_name = "PM";
			reg = wm_adsp_region_to_reg(mem, offset);
			break;
		case WMFW_ADSP1_DM:
			region_name = "DM";
			reg = wm_adsp_region_to_reg(mem, offset);
			break;
		case WMFW_ADSP2_XM:
			region_name = "XM";
			reg = wm_adsp_region_to_reg(mem, offset);
			break;
		case WMFW_ADSP2_YM:
			region_name = "YM";
			reg = wm_adsp_region_to_reg(mem, offset);
			break;
		case WMFW_ADSP1_ZM:
			region_name = "ZM";
			reg = wm_adsp_region_to_reg(mem, offset);
			break;
		default:
			adsp_warn(dsp,
				  "%s.%d: Unknown region type %x at %d(%x)\n",
				  file, regions, type, pos, pos);
			break;
		}

		adsp_dbg(dsp, "%s.%d: %d bytes at %d in %s\n", file,
			 regions, le32_to_cpu(region->len), offset,
			 region_name);

		if (text) {
			memcpy(text, region->data, le32_to_cpu(region->len));
			adsp_info(dsp, "%s: %s\n", file, text);
			kfree(text);
		}

		if (reg) {
			buf = wm_adsp_buf_alloc(region->data,
						le32_to_cpu(region->len),
						&buf_list);
			if (!buf) {
				adsp_err(dsp, "Out of memory\n");
				ret = -ENOMEM;
				goto out_fw;
			}

			ret = regmap_raw_write_async(regmap, reg, buf->buf,
						     le32_to_cpu(region->len));
			if (ret != 0) {
				adsp_err(dsp,
					"%s.%d: Failed to write %d bytes at %d in %s: %d\n",
					file, regions,
					le32_to_cpu(region->len), offset,
					region_name, ret);
				goto out_fw;
			}
		}

		pos += le32_to_cpu(region->len) + sizeof(*region);
		regions++;
	}

	ret = regmap_async_complete(regmap);
	if (ret != 0) {
		adsp_err(dsp, "Failed to complete async write: %d\n", ret);
		goto out_fw;
	}

	if (pos > firmware->size)
		adsp_warn(dsp, "%s.%d: %zu bytes at end of file\n",
			  file, regions, pos - firmware->size);

	wm_adsp_debugfs_save_wmfwname(dsp, file);

out_fw:
	regmap_async_complete(regmap);
	wm_adsp_buf_free(&buf_list);
	release_firmware(firmware);
out:
	kfree(file);

	return ret;
}

static void wm_adsp_ctl_fixup_base(struct wm_adsp *dsp,
				  const struct wm_adsp_alg_region *alg_region)
{
	struct wm_coeff_ctl *ctl;

	list_for_each_entry(ctl, &dsp->ctl_list, list) {
		if (ctl->fw_name == wm_adsp_fw_text[dsp->fw] &&
		    alg_region->alg == ctl->alg_region.alg &&
		    alg_region->type == ctl->alg_region.type) {
			ctl->alg_region.base = alg_region->base;
		}
	}
}

static void *wm_adsp_read_algs(struct wm_adsp *dsp, size_t n_algs,
			       unsigned int pos, unsigned int len)
{
	void *alg;
	int ret;
	__be32 val;

	if (n_algs == 0) {
		adsp_err(dsp, "No algorithms\n");
		return ERR_PTR(-EINVAL);
	}

	if (n_algs > 1024) {
		adsp_err(dsp, "Algorithm count %zx excessive\n", n_algs);
		return ERR_PTR(-EINVAL);
	}

	/* Read the terminator first to validate the length */
	ret = regmap_raw_read(dsp->regmap, pos + len, &val, sizeof(val));
	if (ret != 0) {
		adsp_err(dsp, "Failed to read algorithm list end: %d\n",
			ret);
		return ERR_PTR(ret);
	}

	if (be32_to_cpu(val) != 0xbedead)
		adsp_warn(dsp, "Algorithm list end %x 0x%x != 0xbeadead\n",
			  pos + len, be32_to_cpu(val));

	alg = kzalloc(len * 2, GFP_KERNEL | GFP_DMA);
	if (!alg)
		return ERR_PTR(-ENOMEM);

	ret = regmap_raw_read(dsp->regmap, pos, alg, len * 2);
	if (ret != 0) {
		adsp_err(dsp, "Failed to read algorithm list: %d\n",
			ret);
		kfree(alg);
		return ERR_PTR(ret);
	}

	return alg;
}

static struct wm_adsp_alg_region *wm_adsp_create_region(struct wm_adsp *dsp,
							int type, __be32 id,
							__be32 base)
{
	struct wm_adsp_alg_region *alg_region;

	alg_region = kzalloc(sizeof(*alg_region), GFP_KERNEL);
	if (!alg_region)
		return ERR_PTR(-ENOMEM);

	alg_region->type = type;
	alg_region->alg = be32_to_cpu(id);
	alg_region->base = be32_to_cpu(base);

	list_add_tail(&alg_region->list, &dsp->alg_regions);

	if (dsp->fw_ver > 0)
		wm_adsp_ctl_fixup_base(dsp, alg_region);

	return alg_region;
}

static int wm_adsp1_setup_algs(struct wm_adsp *dsp)
{
	struct wmfw_adsp1_id_hdr adsp1_id;
	struct wmfw_adsp1_alg_hdr *adsp1_alg;
	struct wm_adsp_alg_region *alg_region;
	const struct wm_adsp_region *mem;
	unsigned int pos, len;
	size_t n_algs;
	int i, ret;

	mem = wm_adsp_find_region(dsp, WMFW_ADSP1_DM);
	if (WARN_ON(!mem))
		return -EINVAL;

	ret = regmap_raw_read(dsp->regmap, mem->base, &adsp1_id,
			      sizeof(adsp1_id));
	if (ret != 0) {
		adsp_err(dsp, "Failed to read algorithm info: %d\n",
			 ret);
		return ret;
	}

	n_algs = be32_to_cpu(adsp1_id.n_algs);
	dsp->fw_id = be32_to_cpu(adsp1_id.fw.id);
	adsp_info(dsp, "Firmware: %x v%d.%d.%d, %zu algorithms\n",
		  dsp->fw_id,
		  (be32_to_cpu(adsp1_id.fw.ver) & 0xff0000) >> 16,
		  (be32_to_cpu(adsp1_id.fw.ver) & 0xff00) >> 8,
		  be32_to_cpu(adsp1_id.fw.ver) & 0xff,
		  n_algs);

	alg_region = wm_adsp_create_region(dsp, WMFW_ADSP1_ZM,
					   adsp1_id.fw.id, adsp1_id.zm);
	if (IS_ERR(alg_region))
		return PTR_ERR(alg_region);

	alg_region = wm_adsp_create_region(dsp, WMFW_ADSP1_DM,
					   adsp1_id.fw.id, adsp1_id.dm);
	if (IS_ERR(alg_region))
		return PTR_ERR(alg_region);

	pos = sizeof(adsp1_id) / 2;
	len = (sizeof(*adsp1_alg) * n_algs) / 2;

	adsp1_alg = wm_adsp_read_algs(dsp, n_algs, mem->base + pos, len);
	if (IS_ERR(adsp1_alg))
		return PTR_ERR(adsp1_alg);

	for (i = 0; i < n_algs; i++) {
		adsp_info(dsp, "%d: ID %x v%d.%d.%d DM@%x ZM@%x\n",
			  i, be32_to_cpu(adsp1_alg[i].alg.id),
			  (be32_to_cpu(adsp1_alg[i].alg.ver) & 0xff0000) >> 16,
			  (be32_to_cpu(adsp1_alg[i].alg.ver) & 0xff00) >> 8,
			  be32_to_cpu(adsp1_alg[i].alg.ver) & 0xff,
			  be32_to_cpu(adsp1_alg[i].dm),
			  be32_to_cpu(adsp1_alg[i].zm));

		alg_region = wm_adsp_create_region(dsp, WMFW_ADSP1_DM,
						   adsp1_alg[i].alg.id,
						   adsp1_alg[i].dm);
		if (IS_ERR(alg_region)) {
			ret = PTR_ERR(alg_region);
			goto out;
		}
		if (dsp->fw_ver == 0) {
			if (i + 1 < n_algs) {
				len = be32_to_cpu(adsp1_alg[i + 1].dm);
				len -= be32_to_cpu(adsp1_alg[i].dm);
				len *= 4;
				wm_adsp_create_control(dsp, alg_region, 0,
						       len, NULL, 0, 0);
			} else {
				adsp_warn(dsp, "Missing length info for region DM with ID %x\n",
					  be32_to_cpu(adsp1_alg[i].alg.id));
			}
		}

		alg_region = wm_adsp_create_region(dsp, WMFW_ADSP1_ZM,
						   adsp1_alg[i].alg.id,
						   adsp1_alg[i].zm);
		if (IS_ERR(alg_region)) {
			ret = PTR_ERR(alg_region);
			goto out;
		}
		if (dsp->fw_ver == 0) {
			if (i + 1 < n_algs) {
				len = be32_to_cpu(adsp1_alg[i + 1].zm);
				len -= be32_to_cpu(adsp1_alg[i].zm);
				len *= 4;
				wm_adsp_create_control(dsp, alg_region, 0,
						       len, NULL, 0, 0);
			} else {
				adsp_warn(dsp, "Missing length info for region ZM with ID %x\n",
					  be32_to_cpu(adsp1_alg[i].alg.id));
			}
		}
	}

out:
	kfree(adsp1_alg);
	return ret;
}

static int wm_adsp2_setup_algs(struct wm_adsp *dsp)
{
	struct wmfw_adsp2_id_hdr adsp2_id;
	struct wmfw_adsp2_alg_hdr *adsp2_alg;
	struct wm_adsp_alg_region *alg_region;
	const struct wm_adsp_region *mem;
	unsigned int pos, len;
	size_t n_algs;
	int i, ret;

	mem = wm_adsp_find_region(dsp, WMFW_ADSP2_XM);
	if (WARN_ON(!mem))
		return -EINVAL;

	ret = regmap_raw_read(dsp->regmap, mem->base, &adsp2_id,
			      sizeof(adsp2_id));
	if (ret != 0) {
		adsp_err(dsp, "Failed to read algorithm info: %d\n",
			 ret);
		return ret;
	}

	n_algs = be32_to_cpu(adsp2_id.n_algs);
	dsp->fw_id = be32_to_cpu(adsp2_id.fw.id);
	dsp->fw_id_version = be32_to_cpu(adsp2_id.fw.ver);
	adsp_info(dsp, "Firmware: %x v%d.%d.%d, %zu algorithms\n",
		  dsp->fw_id,
		  (dsp->fw_id_version & 0xff0000) >> 16,
		  (dsp->fw_id_version & 0xff00) >> 8,
		  dsp->fw_id_version & 0xff,
		  n_algs);

	alg_region = wm_adsp_create_region(dsp, WMFW_ADSP2_XM,
					   adsp2_id.fw.id, adsp2_id.xm);
	if (IS_ERR(alg_region))
		return PTR_ERR(alg_region);

	alg_region = wm_adsp_create_region(dsp, WMFW_ADSP2_YM,
					   adsp2_id.fw.id, adsp2_id.ym);
	if (IS_ERR(alg_region))
		return PTR_ERR(alg_region);

	alg_region = wm_adsp_create_region(dsp, WMFW_ADSP2_ZM,
					   adsp2_id.fw.id, adsp2_id.zm);
	if (IS_ERR(alg_region))
		return PTR_ERR(alg_region);

	pos = sizeof(adsp2_id) / 2;
	len = (sizeof(*adsp2_alg) * n_algs) / 2;

	adsp2_alg = wm_adsp_read_algs(dsp, n_algs, mem->base + pos, len);
	if (IS_ERR(adsp2_alg))
		return PTR_ERR(adsp2_alg);

	for (i = 0; i < n_algs; i++) {
		adsp_info(dsp,
			  "%d: ID %x v%d.%d.%d XM@%x YM@%x ZM@%x\n",
			  i, be32_to_cpu(adsp2_alg[i].alg.id),
			  (be32_to_cpu(adsp2_alg[i].alg.ver) & 0xff0000) >> 16,
			  (be32_to_cpu(adsp2_alg[i].alg.ver) & 0xff00) >> 8,
			  be32_to_cpu(adsp2_alg[i].alg.ver) & 0xff,
			  be32_to_cpu(adsp2_alg[i].xm),
			  be32_to_cpu(adsp2_alg[i].ym),
			  be32_to_cpu(adsp2_alg[i].zm));

		alg_region = wm_adsp_create_region(dsp, WMFW_ADSP2_XM,
						   adsp2_alg[i].alg.id,
						   adsp2_alg[i].xm);
		if (IS_ERR(alg_region)) {
			ret = PTR_ERR(alg_region);
			goto out;
		}
		if (dsp->fw_ver == 0) {
			if (i + 1 < n_algs) {
				len = be32_to_cpu(adsp2_alg[i + 1].xm);
				len -= be32_to_cpu(adsp2_alg[i].xm);
				len *= 4;
				wm_adsp_create_control(dsp, alg_region, 0,
						       len, NULL, 0, 0);
			} else {
				adsp_warn(dsp, "Missing length info for region XM with ID %x\n",
					  be32_to_cpu(adsp2_alg[i].alg.id));
			}
		}

		alg_region = wm_adsp_create_region(dsp, WMFW_ADSP2_YM,
						   adsp2_alg[i].alg.id,
						   adsp2_alg[i].ym);
		if (IS_ERR(alg_region)) {
			ret = PTR_ERR(alg_region);
			goto out;
		}
		if (dsp->fw_ver == 0) {
			if (i + 1 < n_algs) {
				len = be32_to_cpu(adsp2_alg[i + 1].ym);
				len -= be32_to_cpu(adsp2_alg[i].ym);
				len *= 4;
				wm_adsp_create_control(dsp, alg_region, 0,
						       len, NULL, 0, 0);
			} else {
				adsp_warn(dsp, "Missing length info for region YM with ID %x\n",
					  be32_to_cpu(adsp2_alg[i].alg.id));
			}
		}

		alg_region = wm_adsp_create_region(dsp, WMFW_ADSP2_ZM,
						   adsp2_alg[i].alg.id,
						   adsp2_alg[i].zm);
		if (IS_ERR(alg_region)) {
			ret = PTR_ERR(alg_region);
			goto out;
		}
		if (dsp->fw_ver == 0) {
			if (i + 1 < n_algs) {
				len = be32_to_cpu(adsp2_alg[i + 1].zm);
				len -= be32_to_cpu(adsp2_alg[i].zm);
				len *= 4;
				wm_adsp_create_control(dsp, alg_region, 0,
						       len, NULL, 0, 0);
			} else {
				adsp_warn(dsp, "Missing length info for region ZM with ID %x\n",
					  be32_to_cpu(adsp2_alg[i].alg.id));
			}
		}
	}

out:
	kfree(adsp2_alg);
	return ret;
}

static int wm_adsp_load_coeff(struct wm_adsp *dsp)
{
	LIST_HEAD(buf_list);
	struct regmap *regmap = dsp->regmap;
	struct wmfw_coeff_hdr *hdr;
	struct wmfw_coeff_item *blk;
	const struct firmware *firmware;
	const struct wm_adsp_region *mem;
	struct wm_adsp_alg_region *alg_region;
	const char *region_name;
	int ret, pos, blocks, type, offset, reg;
	char *file;
	struct wm_adsp_buf *buf;

	file = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (file == NULL)
		return -ENOMEM;

	snprintf(file, PAGE_SIZE, "%s-dsp%d-%s.bin", dsp->part, dsp->num,
		 wm_adsp_fw[dsp->fw].file);
	file[PAGE_SIZE - 1] = '\0';

	ret = request_firmware(&firmware, file, dsp->dev);
	if (ret != 0) {
		adsp_warn(dsp, "Failed to request '%s'\n", file);
		ret = 0;
		goto out;
	}
	ret = -EINVAL;

	if (sizeof(*hdr) >= firmware->size) {
		adsp_err(dsp, "%s: file too short, %zu bytes\n",
			file, firmware->size);
		goto out_fw;
	}

	hdr = (void*)&firmware->data[0];
	if (memcmp(hdr->magic, "WMDR", 4) != 0) {
		adsp_err(dsp, "%s: invalid magic\n", file);
		goto out_fw;
	}

	switch (be32_to_cpu(hdr->rev) & 0xff) {
	case 1:
		break;
	default:
		adsp_err(dsp, "%s: Unsupported coefficient file format %d\n",
			 file, be32_to_cpu(hdr->rev) & 0xff);
		ret = -EINVAL;
		goto out_fw;
	}

	adsp_dbg(dsp, "%s: v%d.%d.%d\n", file,
		(le32_to_cpu(hdr->ver) >> 16) & 0xff,
		(le32_to_cpu(hdr->ver) >>  8) & 0xff,
		le32_to_cpu(hdr->ver) & 0xff);

	pos = le32_to_cpu(hdr->len);

	blocks = 0;
	while (pos < firmware->size &&
	       pos - firmware->size > sizeof(*blk)) {
		blk = (void*)(&firmware->data[pos]);

		type = le16_to_cpu(blk->type);
		offset = le16_to_cpu(blk->offset);

		adsp_dbg(dsp, "%s.%d: %x v%d.%d.%d\n",
			 file, blocks, le32_to_cpu(blk->id),
			 (le32_to_cpu(blk->ver) >> 16) & 0xff,
			 (le32_to_cpu(blk->ver) >>  8) & 0xff,
			 le32_to_cpu(blk->ver) & 0xff);
		adsp_dbg(dsp, "%s.%d: %d bytes at 0x%x in %x\n",
			 file, blocks, le32_to_cpu(blk->len), offset, type);

		reg = 0;
		region_name = "Unknown";
		switch (type) {
		case (WMFW_NAME_TEXT << 8):
		case (WMFW_INFO_TEXT << 8):
			break;
		case (WMFW_ABSOLUTE << 8):
			/*
			 * Old files may use this for global
			 * coefficients.
			 */
			if (le32_to_cpu(blk->id) == dsp->fw_id &&
			    offset == 0) {
				region_name = "global coefficients";
				mem = wm_adsp_find_region(dsp, type);
				if (!mem) {
					adsp_err(dsp, "No ZM\n");
					break;
				}
				reg = wm_adsp_region_to_reg(mem, 0);

			} else {
				region_name = "register";
				reg = offset;
			}
			break;

		case WMFW_ADSP1_DM:
		case WMFW_ADSP1_ZM:
		case WMFW_ADSP2_XM:
		case WMFW_ADSP2_YM:
			adsp_dbg(dsp, "%s.%d: %d bytes in %x for %x\n",
				 file, blocks, le32_to_cpu(blk->len),
				 type, le32_to_cpu(blk->id));

			mem = wm_adsp_find_region(dsp, type);
			if (!mem) {
				adsp_err(dsp, "No base for region %x\n", type);
				break;
			}

			reg = 0;
			list_for_each_entry(alg_region,
					    &dsp->alg_regions, list) {
				if (le32_to_cpu(blk->id) == alg_region->alg &&
				    type == alg_region->type) {
					reg = alg_region->base;
					reg = wm_adsp_region_to_reg(mem,
								    reg);
					reg += offset;
					break;
				}
			}

			if (reg == 0)
				adsp_err(dsp, "No %x for algorithm %x\n",
					 type, le32_to_cpu(blk->id));
			break;

		default:
			adsp_err(dsp, "%s.%d: Unknown region type %x at %d\n",
				 file, blocks, type, pos);
			break;
		}

		if (reg) {
			buf = wm_adsp_buf_alloc(blk->data,
						le32_to_cpu(blk->len),
						&buf_list);
			if (!buf) {
				adsp_err(dsp, "Out of memory\n");
				ret = -ENOMEM;
				goto out_fw;
			}

			adsp_dbg(dsp, "%s.%d: Writing %d bytes at %x\n",
				 file, blocks, le32_to_cpu(blk->len),
				 reg);
			ret = regmap_raw_write_async(regmap, reg, buf->buf,
						     le32_to_cpu(blk->len));
			if (ret != 0) {
				adsp_err(dsp,
					"%s.%d: Failed to write to %x in %s: %d\n",
					file, blocks, reg, region_name, ret);
			}
		}

		pos += (le32_to_cpu(blk->len) + sizeof(*blk) + 3) & ~0x03;
		blocks++;
	}

	ret = regmap_async_complete(regmap);
	if (ret != 0)
		adsp_err(dsp, "Failed to complete async write: %d\n", ret);

	if (pos > firmware->size)
		adsp_warn(dsp, "%s.%d: %zu bytes at end of file\n",
			  file, blocks, pos - firmware->size);

	wm_adsp_debugfs_save_binname(dsp, file);

out_fw:
	regmap_async_complete(regmap);
	release_firmware(firmware);
	wm_adsp_buf_free(&buf_list);
out:
	kfree(file);
	return ret;
}

int wm_adsp1_init(struct wm_adsp *dsp)
{
	INIT_LIST_HEAD(&dsp->alg_regions);

#ifdef CONFIG_DEBUG_FS
	mutex_init(&dsp->debugfs_lock);
#endif
	return 0;
}
EXPORT_SYMBOL_GPL(wm_adsp1_init);

int wm_adsp1_event(struct snd_soc_dapm_widget *w,
		   struct snd_kcontrol *kcontrol,
		   int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct wm_adsp *dsps = snd_soc_codec_get_drvdata(codec);
	struct wm_adsp *dsp = &dsps[w->shift];
	struct wm_adsp_alg_region *alg_region;
	struct wm_coeff_ctl *ctl;
	int ret;
	int val;

	dsp->card = codec->component.card;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_update_bits(dsp->regmap, dsp->base + ADSP1_CONTROL_30,
				   ADSP1_SYS_ENA, ADSP1_SYS_ENA);

		/*
		 * For simplicity set the DSP clock rate to be the
		 * SYSCLK rate rather than making it configurable.
		 */
		if(dsp->sysclk_reg) {
			ret = regmap_read(dsp->regmap, dsp->sysclk_reg, &val);
			if (ret != 0) {
				adsp_err(dsp, "Failed to read SYSCLK state: %d\n",
				ret);
				return ret;
			}

			val = (val & dsp->sysclk_mask)
				>> dsp->sysclk_shift;

			ret = regmap_update_bits(dsp->regmap,
						 dsp->base + ADSP1_CONTROL_31,
						 ADSP1_CLK_SEL_MASK, val);
			if (ret != 0) {
				adsp_err(dsp, "Failed to set clock rate: %d\n",
					 ret);
				return ret;
			}
		}

		ret = wm_adsp_load(dsp);
		if (ret != 0)
			goto err;

		ret = wm_adsp1_setup_algs(dsp);
		if (ret != 0)
			goto err;

		ret = wm_adsp_load_coeff(dsp);
		if (ret != 0)
			goto err;

		/* Initialize caches for enabled and unset controls */
		ret = wm_coeff_init_control_caches(dsp);
		if (ret != 0)
			goto err;

		/* Sync set controls */
		ret = wm_coeff_sync_controls(dsp);
		if (ret != 0)
			goto err;

		/* Start the core running */
		regmap_update_bits(dsp->regmap, dsp->base + ADSP1_CONTROL_30,
				   ADSP1_CORE_ENA | ADSP1_START,
				   ADSP1_CORE_ENA | ADSP1_START);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		/* Halt the core */
		regmap_update_bits(dsp->regmap, dsp->base + ADSP1_CONTROL_30,
				   ADSP1_CORE_ENA | ADSP1_START, 0);

		regmap_update_bits(dsp->regmap, dsp->base + ADSP1_CONTROL_19,
				   ADSP1_WDMA_BUFFER_LENGTH_MASK, 0);

		regmap_update_bits(dsp->regmap, dsp->base + ADSP1_CONTROL_30,
				   ADSP1_SYS_ENA, 0);

		list_for_each_entry(ctl, &dsp->ctl_list, list)
			ctl->enabled = 0;

		while (!list_empty(&dsp->alg_regions)) {
			alg_region = list_first_entry(&dsp->alg_regions,
						      struct wm_adsp_alg_region,
						      list);
			list_del(&alg_region->list);
			kfree(alg_region);
		}
		break;

	default:
		break;
	}

	return 0;

err:
	regmap_update_bits(dsp->regmap, dsp->base + ADSP1_CONTROL_30,
			   ADSP1_SYS_ENA, 0);
	return ret;
}
EXPORT_SYMBOL_GPL(wm_adsp1_event);

static int wm_adsp2_ena(struct wm_adsp *dsp)
{
	unsigned int val;
	int ret, count;

	ret = regmap_update_bits_async(dsp->regmap, dsp->base + ADSP2_CONTROL,
				       ADSP2_SYS_ENA, ADSP2_SYS_ENA);
	if (ret != 0)
		return ret;

	/* Wait for the RAM to start, should be near instantaneous */
	for (count = 0; count < 10; ++count) {
		ret = regmap_read(dsp->regmap, dsp->base + ADSP2_STATUS1,
				  &val);
		if (ret != 0)
			return ret;

		if (val & ADSP2_RAM_RDY)
			break;

		msleep(1);
	}

	if (!(val & ADSP2_RAM_RDY)) {
		adsp_err(dsp, "Failed to start DSP RAM\n");
		return -EBUSY;
	}

	adsp_dbg(dsp, "RAM ready after %d polls\n", count);

	return 0;
}

static void wm_adsp2_boot_work(struct work_struct *work)
{
	struct wm_adsp *dsp = container_of(work,
					   struct wm_adsp,
					   boot_work);
	int ret;
	unsigned int val;

	/*
	 * For simplicity set the DSP clock rate to be the
	 * SYSCLK rate rather than making it configurable.
	 */
	ret = regmap_read(dsp->regmap, ARIZONA_SYSTEM_CLOCK_1, &val);
	if (ret != 0) {
		adsp_err(dsp, "Failed to read SYSCLK state: %d\n", ret);
		return;
	}
	val = (val & ARIZONA_SYSCLK_FREQ_MASK)
		>> ARIZONA_SYSCLK_FREQ_SHIFT;

	ret = regmap_update_bits_async(dsp->regmap,
				       dsp->base + ADSP2_CLOCKING,
				       ADSP2_CLK_SEL_MASK, val);
	if (ret != 0) {
		adsp_err(dsp, "Failed to set clock rate: %d\n", ret);
		return;
	}

	ret = wm_adsp2_ena(dsp);
	if (ret != 0)
		return;

	ret = wm_adsp_load(dsp);
	if (ret != 0)
		goto err;

	ret = wm_adsp2_setup_algs(dsp);
	if (ret != 0)
		goto err;

	ret = wm_adsp_load_coeff(dsp);
	if (ret != 0)
		goto err;

	/* Initialize caches for enabled and unset controls */
	ret = wm_coeff_init_control_caches(dsp);
	if (ret != 0)
		goto err;

	/* Sync set controls */
	ret = wm_coeff_sync_controls(dsp);
	if (ret != 0)
		goto err;

	dsp->running = true;

	return;

err:
	regmap_update_bits(dsp->regmap, dsp->base + ADSP2_CONTROL,
			   ADSP2_SYS_ENA | ADSP2_CORE_ENA | ADSP2_START, 0);
}

int wm_adsp2_early_event(struct snd_soc_dapm_widget *w,
		   struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct wm_adsp *dsps = snd_soc_codec_get_drvdata(codec);
	struct wm_adsp *dsp = &dsps[w->shift];

	dsp->card = codec->component.card;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		queue_work(system_unbound_wq, &dsp->boot_work);
		break;
	default:
		break;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(wm_adsp2_early_event);

int wm_adsp2_event(struct snd_soc_dapm_widget *w,
		   struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct wm_adsp *dsps = snd_soc_codec_get_drvdata(codec);
	struct wm_adsp *dsp = &dsps[w->shift];
	struct wm_adsp_alg_region *alg_region;
	struct wm_coeff_ctl *ctl;
	int ret;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		flush_work(&dsp->boot_work);

		if (!dsp->running)
			return -EIO;

		ret = regmap_update_bits(dsp->regmap,
					 dsp->base + ADSP2_CONTROL,
					 ADSP2_CORE_ENA | ADSP2_START,
					 ADSP2_CORE_ENA | ADSP2_START);
		if (ret != 0)
			goto err;
		break;

	case SND_SOC_DAPM_PRE_PMD:
		/* Log firmware state, it can be useful for analysis */
		wm_adsp2_show_fw_status(dsp);

		wm_adsp_debugfs_clear(dsp);

		dsp->fw_id = 0;
		dsp->fw_id_version = 0;
		dsp->running = false;

		regmap_update_bits(dsp->regmap, dsp->base + ADSP2_CONTROL,
				   ADSP2_SYS_ENA | ADSP2_CORE_ENA |
				   ADSP2_START, 0);

		/* Make sure DMAs are quiesced */
		regmap_write(dsp->regmap, dsp->base + ADSP2_WDMA_CONFIG_1, 0);
		regmap_write(dsp->regmap, dsp->base + ADSP2_WDMA_CONFIG_2, 0);
		regmap_write(dsp->regmap, dsp->base + ADSP2_RDMA_CONFIG_1, 0);

		list_for_each_entry(ctl, &dsp->ctl_list, list)
			ctl->enabled = 0;

		while (!list_empty(&dsp->alg_regions)) {
			alg_region = list_first_entry(&dsp->alg_regions,
						      struct wm_adsp_alg_region,
						      list);
			list_del(&alg_region->list);
			kfree(alg_region);
		}

		adsp_dbg(dsp, "Shutdown complete\n");
		break;

	default:
		break;
	}

	return 0;
err:
	regmap_update_bits(dsp->regmap, dsp->base + ADSP2_CONTROL,
			   ADSP2_SYS_ENA | ADSP2_CORE_ENA | ADSP2_START, 0);
	return ret;
}
EXPORT_SYMBOL_GPL(wm_adsp2_event);

int wm_adsp2_codec_probe(struct wm_adsp *dsp, struct snd_soc_codec *codec)
{
	wm_adsp2_init_debugfs(dsp, codec);

	return snd_soc_add_codec_controls(codec,
					  &wm_adsp_fw_controls[dsp->num - 1],
					  1);
}
EXPORT_SYMBOL_GPL(wm_adsp2_codec_probe);

int wm_adsp2_codec_remove(struct wm_adsp *dsp, struct snd_soc_codec *codec)
{
	wm_adsp2_cleanup_debugfs(dsp);

	return 0;
}
EXPORT_SYMBOL_GPL(wm_adsp2_codec_remove);

int wm_adsp2_init(struct wm_adsp *dsp)
{
	int ret;

	/*
	 * Disable the DSP memory by default when in reset for a small
	 * power saving.
	 */
	ret = regmap_update_bits(dsp->regmap, dsp->base + ADSP2_CONTROL,
				 ADSP2_MEM_ENA, 0);
	if (ret != 0) {
		adsp_err(dsp, "Failed to clear memory retention: %d\n", ret);
		return ret;
	}

	INIT_LIST_HEAD(&dsp->alg_regions);
	INIT_LIST_HEAD(&dsp->ctl_list);
	INIT_WORK(&dsp->boot_work, wm_adsp2_boot_work);

#ifdef CONFIG_DEBUG_FS
	mutex_init(&dsp->debugfs_lock);
#endif
	return 0;
}
EXPORT_SYMBOL_GPL(wm_adsp2_init);

MODULE_LICENSE("GPL v2");
