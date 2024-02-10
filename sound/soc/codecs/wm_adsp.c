// SPDX-License-Identifier: GPL-2.0-only
/*
 * wm_adsp.c  --  Wolfson ADSP support
 *
 * Copyright 2012 Wolfson Microelectronics plc
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 */

#include <linux/ctype.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/list.h>
#include <linux/pm.h>
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

#include "wm_adsp.h"

#define adsp_crit(_dsp, fmt, ...) \
	dev_crit(_dsp->cs_dsp.dev, "%s: " fmt, _dsp->cs_dsp.name, ##__VA_ARGS__)
#define adsp_err(_dsp, fmt, ...) \
	dev_err(_dsp->cs_dsp.dev, "%s: " fmt, _dsp->cs_dsp.name, ##__VA_ARGS__)
#define adsp_warn(_dsp, fmt, ...) \
	dev_warn(_dsp->cs_dsp.dev, "%s: " fmt, _dsp->cs_dsp.name, ##__VA_ARGS__)
#define adsp_info(_dsp, fmt, ...) \
	dev_info(_dsp->cs_dsp.dev, "%s: " fmt, _dsp->cs_dsp.name, ##__VA_ARGS__)
#define adsp_dbg(_dsp, fmt, ...) \
	dev_dbg(_dsp->cs_dsp.dev, "%s: " fmt, _dsp->cs_dsp.name, ##__VA_ARGS__)

#define compr_err(_obj, fmt, ...) \
	adsp_err(_obj->dsp, "%s: " fmt, _obj->name ? _obj->name : "legacy", \
		 ##__VA_ARGS__)
#define compr_dbg(_obj, fmt, ...) \
	adsp_dbg(_obj->dsp, "%s: " fmt, _obj->name ? _obj->name : "legacy", \
		 ##__VA_ARGS__)

#define ADSP_MAX_STD_CTRL_SIZE               512

static const struct cs_dsp_client_ops wm_adsp1_client_ops;
static const struct cs_dsp_client_ops wm_adsp2_client_ops;

#define WM_ADSP_FW_MBC_VSS  0
#define WM_ADSP_FW_HIFI     1
#define WM_ADSP_FW_TX       2
#define WM_ADSP_FW_TX_SPK   3
#define WM_ADSP_FW_RX       4
#define WM_ADSP_FW_RX_ANC   5
#define WM_ADSP_FW_CTRL     6
#define WM_ADSP_FW_ASR      7
#define WM_ADSP_FW_TRACE    8
#define WM_ADSP_FW_SPK_PROT 9
#define WM_ADSP_FW_SPK_CALI 10
#define WM_ADSP_FW_SPK_DIAG 11
#define WM_ADSP_FW_MISC     12

#define WM_ADSP_NUM_FW      13

static const char *wm_adsp_fw_text[WM_ADSP_NUM_FW] = {
	[WM_ADSP_FW_MBC_VSS] =  "MBC/VSS",
	[WM_ADSP_FW_HIFI] =     "MasterHiFi",
	[WM_ADSP_FW_TX] =       "Tx",
	[WM_ADSP_FW_TX_SPK] =   "Tx Speaker",
	[WM_ADSP_FW_RX] =       "Rx",
	[WM_ADSP_FW_RX_ANC] =   "Rx ANC",
	[WM_ADSP_FW_CTRL] =     "Voice Ctrl",
	[WM_ADSP_FW_ASR] =      "ASR Assist",
	[WM_ADSP_FW_TRACE] =    "Dbg Trace",
	[WM_ADSP_FW_SPK_PROT] = "Protection",
	[WM_ADSP_FW_SPK_CALI] = "Calibration",
	[WM_ADSP_FW_SPK_DIAG] = "Diagnostic",
	[WM_ADSP_FW_MISC] =     "Misc",
};

struct wm_adsp_system_config_xm_hdr {
	__be32 sys_enable;
	__be32 fw_id;
	__be32 fw_rev;
	__be32 boot_status;
	__be32 watchdog;
	__be32 dma_buffer_size;
	__be32 rdma[6];
	__be32 wdma[8];
	__be32 build_job_name[3];
	__be32 build_job_number;
} __packed;

struct wm_halo_system_config_xm_hdr {
	__be32 halo_heartbeat;
	__be32 build_job_name[3];
	__be32 build_job_number;
} __packed;

struct wm_adsp_alg_xm_struct {
	__be32 magic;
	__be32 smoothing;
	__be32 threshold;
	__be32 host_buf_ptr;
	__be32 start_seq;
	__be32 high_water_mark;
	__be32 low_water_mark;
	__be64 smoothed_power;
} __packed;

struct wm_adsp_host_buf_coeff_v1 {
	__be32 host_buf_ptr;		/* Host buffer pointer */
	__be32 versions;		/* Version numbers */
	__be32 name[4];			/* The buffer name */
} __packed;

struct wm_adsp_buffer {
	__be32 buf1_base;		/* Base addr of first buffer area */
	__be32 buf1_size;		/* Size of buf1 area in DSP words */
	__be32 buf2_base;		/* Base addr of 2nd buffer area */
	__be32 buf1_buf2_size;		/* Size of buf1+buf2 in DSP words */
	__be32 buf3_base;		/* Base addr of buf3 area */
	__be32 buf_total_size;		/* Size of buf1+buf2+buf3 in DSP words */
	__be32 high_water_mark;		/* Point at which IRQ is asserted */
	__be32 irq_count;		/* bits 1-31 count IRQ assertions */
	__be32 irq_ack;			/* acked IRQ count, bit 0 enables IRQ */
	__be32 next_write_index;	/* word index of next write */
	__be32 next_read_index;		/* word index of next read */
	__be32 error;			/* error if any */
	__be32 oldest_block_index;	/* word index of oldest surviving */
	__be32 requested_rewind;	/* how many blocks rewind was done */
	__be32 reserved_space;		/* internal */
	__be32 min_free;		/* min free space since stream start */
	__be32 blocks_written[2];	/* total blocks written (64 bit) */
	__be32 words_written[2];	/* total words written (64 bit) */
} __packed;

struct wm_adsp_compr;

struct wm_adsp_compr_buf {
	struct list_head list;
	struct wm_adsp *dsp;
	struct wm_adsp_compr *compr;

	struct wm_adsp_buffer_region *regions;
	u32 host_buf_ptr;

	u32 error;
	u32 irq_count;
	int read_index;
	int avail;
	int host_buf_mem_type;

	char *name;
};

struct wm_adsp_compr {
	struct list_head list;
	struct wm_adsp *dsp;
	struct wm_adsp_compr_buf *buf;

	struct snd_compr_stream *stream;
	struct snd_compressed_buffer size;

	u32 *raw_buf;
	unsigned int copied_total;

	unsigned int sample_rate;

	const char *name;
};

#define WM_ADSP_MIN_FRAGMENTS          1
#define WM_ADSP_MAX_FRAGMENTS          256
#define WM_ADSP_MIN_FRAGMENT_SIZE      (16 * CS_DSP_DATA_WORD_SIZE)
#define WM_ADSP_MAX_FRAGMENT_SIZE      (4096 * CS_DSP_DATA_WORD_SIZE)

#define WM_ADSP_ALG_XM_STRUCT_MAGIC    0x49aec7

#define HOST_BUFFER_FIELD(field) \
	(offsetof(struct wm_adsp_buffer, field) / sizeof(__be32))

#define ALG_XM_FIELD(field) \
	(offsetof(struct wm_adsp_alg_xm_struct, field) / sizeof(__be32))

#define HOST_BUF_COEFF_SUPPORTED_COMPAT_VER	1

#define HOST_BUF_COEFF_COMPAT_VER_MASK		0xFF00
#define HOST_BUF_COEFF_COMPAT_VER_SHIFT		8

static int wm_adsp_buffer_init(struct wm_adsp *dsp);
static int wm_adsp_buffer_free(struct wm_adsp *dsp);

struct wm_adsp_buffer_region {
	unsigned int offset;
	unsigned int cumulative_size;
	unsigned int mem_type;
	unsigned int base_addr;
};

struct wm_adsp_buffer_region_def {
	unsigned int mem_type;
	unsigned int base_offset;
	unsigned int size_offset;
};

static const struct wm_adsp_buffer_region_def default_regions[] = {
	{
		.mem_type = WMFW_ADSP2_XM,
		.base_offset = HOST_BUFFER_FIELD(buf1_base),
		.size_offset = HOST_BUFFER_FIELD(buf1_size),
	},
	{
		.mem_type = WMFW_ADSP2_XM,
		.base_offset = HOST_BUFFER_FIELD(buf2_base),
		.size_offset = HOST_BUFFER_FIELD(buf1_buf2_size),
	},
	{
		.mem_type = WMFW_ADSP2_YM,
		.base_offset = HOST_BUFFER_FIELD(buf3_base),
		.size_offset = HOST_BUFFER_FIELD(buf_total_size),
	},
};

struct wm_adsp_fw_caps {
	u32 id;
	struct snd_codec_desc desc;
	int num_regions;
	const struct wm_adsp_buffer_region_def *region_defs;
};

static const struct wm_adsp_fw_caps ctrl_caps[] = {
	{
		.id = SND_AUDIOCODEC_BESPOKE,
		.desc = {
			.max_ch = 8,
			.sample_rates = { 16000 },
			.num_sample_rates = 1,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
		.num_regions = ARRAY_SIZE(default_regions),
		.region_defs = default_regions,
	},
};

static const struct wm_adsp_fw_caps trace_caps[] = {
	{
		.id = SND_AUDIOCODEC_BESPOKE,
		.desc = {
			.max_ch = 8,
			.sample_rates = {
				4000, 8000, 11025, 12000, 16000, 22050,
				24000, 32000, 44100, 48000, 64000, 88200,
				96000, 176400, 192000
			},
			.num_sample_rates = 15,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
		.num_regions = ARRAY_SIZE(default_regions),
		.region_defs = default_regions,
	},
};

static const struct {
	const char *file;
	int compr_direction;
	int num_caps;
	const struct wm_adsp_fw_caps *caps;
	bool voice_trigger;
} wm_adsp_fw[WM_ADSP_NUM_FW] = {
	[WM_ADSP_FW_MBC_VSS] =  { .file = "mbc-vss" },
	[WM_ADSP_FW_HIFI] =     { .file = "hifi" },
	[WM_ADSP_FW_TX] =       { .file = "tx" },
	[WM_ADSP_FW_TX_SPK] =   { .file = "tx-spk" },
	[WM_ADSP_FW_RX] =       { .file = "rx" },
	[WM_ADSP_FW_RX_ANC] =   { .file = "rx-anc" },
	[WM_ADSP_FW_CTRL] =     {
		.file = "ctrl",
		.compr_direction = SND_COMPRESS_CAPTURE,
		.num_caps = ARRAY_SIZE(ctrl_caps),
		.caps = ctrl_caps,
		.voice_trigger = true,
	},
	[WM_ADSP_FW_ASR] =      { .file = "asr" },
	[WM_ADSP_FW_TRACE] =    {
		.file = "trace",
		.compr_direction = SND_COMPRESS_CAPTURE,
		.num_caps = ARRAY_SIZE(trace_caps),
		.caps = trace_caps,
	},
	[WM_ADSP_FW_SPK_PROT] = {
		.file = "spk-prot",
		.compr_direction = SND_COMPRESS_CAPTURE,
		.num_caps = ARRAY_SIZE(trace_caps),
		.caps = trace_caps,
	},
	[WM_ADSP_FW_SPK_CALI] = { .file = "spk-cali" },
	[WM_ADSP_FW_SPK_DIAG] = { .file = "spk-diag" },
	[WM_ADSP_FW_MISC] =     { .file = "misc" },
};

struct wm_coeff_ctl {
	const char *name;
	struct cs_dsp_coeff_ctl *cs_ctl;
	struct soc_bytes_ext bytes_ext;
	struct work_struct work;
};

int wm_adsp_fw_get(struct snd_kcontrol *kcontrol,
		   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	struct wm_adsp *dsp = snd_soc_component_get_drvdata(component);

	ucontrol->value.enumerated.item[0] = dsp[e->shift_l].fw;

	return 0;
}
EXPORT_SYMBOL_GPL(wm_adsp_fw_get);

int wm_adsp_fw_put(struct snd_kcontrol *kcontrol,
		   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	struct wm_adsp *dsp = snd_soc_component_get_drvdata(component);
	int ret = 1;

	if (ucontrol->value.enumerated.item[0] == dsp[e->shift_l].fw)
		return 0;

	if (ucontrol->value.enumerated.item[0] >= WM_ADSP_NUM_FW)
		return -EINVAL;

	mutex_lock(&dsp[e->shift_l].cs_dsp.pwr_lock);

	if (dsp[e->shift_l].cs_dsp.booted || !list_empty(&dsp[e->shift_l].compr_list))
		ret = -EBUSY;
	else
		dsp[e->shift_l].fw = ucontrol->value.enumerated.item[0];

	mutex_unlock(&dsp[e->shift_l].cs_dsp.pwr_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(wm_adsp_fw_put);

const struct soc_enum wm_adsp_fw_enum[] = {
	SOC_ENUM_SINGLE(0, 0, ARRAY_SIZE(wm_adsp_fw_text), wm_adsp_fw_text),
	SOC_ENUM_SINGLE(0, 1, ARRAY_SIZE(wm_adsp_fw_text), wm_adsp_fw_text),
	SOC_ENUM_SINGLE(0, 2, ARRAY_SIZE(wm_adsp_fw_text), wm_adsp_fw_text),
	SOC_ENUM_SINGLE(0, 3, ARRAY_SIZE(wm_adsp_fw_text), wm_adsp_fw_text),
	SOC_ENUM_SINGLE(0, 4, ARRAY_SIZE(wm_adsp_fw_text), wm_adsp_fw_text),
	SOC_ENUM_SINGLE(0, 5, ARRAY_SIZE(wm_adsp_fw_text), wm_adsp_fw_text),
	SOC_ENUM_SINGLE(0, 6, ARRAY_SIZE(wm_adsp_fw_text), wm_adsp_fw_text),
};
EXPORT_SYMBOL_GPL(wm_adsp_fw_enum);

static inline struct wm_coeff_ctl *bytes_ext_to_ctl(struct soc_bytes_ext *ext)
{
	return container_of(ext, struct wm_coeff_ctl, bytes_ext);
}

static int wm_coeff_info(struct snd_kcontrol *kctl,
			 struct snd_ctl_elem_info *uinfo)
{
	struct soc_bytes_ext *bytes_ext =
		(struct soc_bytes_ext *)kctl->private_value;
	struct wm_coeff_ctl *ctl = bytes_ext_to_ctl(bytes_ext);
	struct cs_dsp_coeff_ctl *cs_ctl = ctl->cs_ctl;

	switch (cs_ctl->type) {
	case WMFW_CTL_TYPE_ACKED:
		uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
		uinfo->value.integer.min = CS_DSP_ACKED_CTL_MIN_VALUE;
		uinfo->value.integer.max = CS_DSP_ACKED_CTL_MAX_VALUE;
		uinfo->value.integer.step = 1;
		uinfo->count = 1;
		break;
	default:
		uinfo->type = SNDRV_CTL_ELEM_TYPE_BYTES;
		uinfo->count = cs_ctl->len;
		break;
	}

	return 0;
}

static int wm_coeff_put(struct snd_kcontrol *kctl,
			struct snd_ctl_elem_value *ucontrol)
{
	struct soc_bytes_ext *bytes_ext =
		(struct soc_bytes_ext *)kctl->private_value;
	struct wm_coeff_ctl *ctl = bytes_ext_to_ctl(bytes_ext);
	struct cs_dsp_coeff_ctl *cs_ctl = ctl->cs_ctl;
	char *p = ucontrol->value.bytes.data;
	int ret = 0;

	mutex_lock(&cs_ctl->dsp->pwr_lock);
	ret = cs_dsp_coeff_write_ctrl(cs_ctl, 0, p, cs_ctl->len);
	mutex_unlock(&cs_ctl->dsp->pwr_lock);

	return ret;
}

static int wm_coeff_tlv_put(struct snd_kcontrol *kctl,
			    const unsigned int __user *bytes, unsigned int size)
{
	struct soc_bytes_ext *bytes_ext =
		(struct soc_bytes_ext *)kctl->private_value;
	struct wm_coeff_ctl *ctl = bytes_ext_to_ctl(bytes_ext);
	struct cs_dsp_coeff_ctl *cs_ctl = ctl->cs_ctl;
	void *scratch;
	int ret = 0;

	scratch = vmalloc(size);
	if (!scratch)
		return -ENOMEM;

	if (copy_from_user(scratch, bytes, size)) {
		ret = -EFAULT;
	} else {
		mutex_lock(&cs_ctl->dsp->pwr_lock);
		ret = cs_dsp_coeff_write_ctrl(cs_ctl, 0, scratch, size);
		mutex_unlock(&cs_ctl->dsp->pwr_lock);
	}
	vfree(scratch);

	return ret;
}

static int wm_coeff_put_acked(struct snd_kcontrol *kctl,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct soc_bytes_ext *bytes_ext =
		(struct soc_bytes_ext *)kctl->private_value;
	struct wm_coeff_ctl *ctl = bytes_ext_to_ctl(bytes_ext);
	struct cs_dsp_coeff_ctl *cs_ctl = ctl->cs_ctl;
	unsigned int val = ucontrol->value.integer.value[0];
	int ret;

	if (val == 0)
		return 0;	/* 0 means no event */

	mutex_lock(&cs_ctl->dsp->pwr_lock);

	if (cs_ctl->enabled)
		ret = cs_dsp_coeff_write_acked_control(cs_ctl, val);
	else
		ret = -EPERM;

	mutex_unlock(&cs_ctl->dsp->pwr_lock);

	if (ret < 0)
		return ret;

	return 1;
}

static int wm_coeff_get(struct snd_kcontrol *kctl,
			struct snd_ctl_elem_value *ucontrol)
{
	struct soc_bytes_ext *bytes_ext =
		(struct soc_bytes_ext *)kctl->private_value;
	struct wm_coeff_ctl *ctl = bytes_ext_to_ctl(bytes_ext);
	struct cs_dsp_coeff_ctl *cs_ctl = ctl->cs_ctl;
	char *p = ucontrol->value.bytes.data;
	int ret;

	mutex_lock(&cs_ctl->dsp->pwr_lock);
	ret = cs_dsp_coeff_read_ctrl(cs_ctl, 0, p, cs_ctl->len);
	mutex_unlock(&cs_ctl->dsp->pwr_lock);

	return ret;
}

static int wm_coeff_tlv_get(struct snd_kcontrol *kctl,
			    unsigned int __user *bytes, unsigned int size)
{
	struct soc_bytes_ext *bytes_ext =
		(struct soc_bytes_ext *)kctl->private_value;
	struct wm_coeff_ctl *ctl = bytes_ext_to_ctl(bytes_ext);
	struct cs_dsp_coeff_ctl *cs_ctl = ctl->cs_ctl;
	int ret = 0;

	mutex_lock(&cs_ctl->dsp->pwr_lock);

	ret = cs_dsp_coeff_read_ctrl(cs_ctl, 0, cs_ctl->cache, size);

	if (!ret && copy_to_user(bytes, cs_ctl->cache, size))
		ret = -EFAULT;

	mutex_unlock(&cs_ctl->dsp->pwr_lock);

	return ret;
}

static int wm_coeff_get_acked(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	/*
	 * Although it's not useful to read an acked control, we must satisfy
	 * user-side assumptions that all controls are readable and that a
	 * write of the same value should be filtered out (it's valid to send
	 * the same event number again to the firmware). We therefore return 0,
	 * meaning "no event" so valid event numbers will always be a change
	 */
	ucontrol->value.integer.value[0] = 0;

	return 0;
}

static unsigned int wmfw_convert_flags(unsigned int in, unsigned int len)
{
	unsigned int out, rd, wr, vol;

	if (len > ADSP_MAX_STD_CTRL_SIZE) {
		rd = SNDRV_CTL_ELEM_ACCESS_TLV_READ;
		wr = SNDRV_CTL_ELEM_ACCESS_TLV_WRITE;
		vol = SNDRV_CTL_ELEM_ACCESS_VOLATILE;

		out = SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK;
	} else {
		rd = SNDRV_CTL_ELEM_ACCESS_READ;
		wr = SNDRV_CTL_ELEM_ACCESS_WRITE;
		vol = SNDRV_CTL_ELEM_ACCESS_VOLATILE;

		out = 0;
	}

	if (in) {
		out |= rd;
		if (in & WMFW_CTL_FLAG_WRITEABLE)
			out |= wr;
		if (in & WMFW_CTL_FLAG_VOLATILE)
			out |= vol;
	} else {
		out |= rd | wr | vol;
	}

	return out;
}

static void wm_adsp_ctl_work(struct work_struct *work)
{
	struct wm_coeff_ctl *ctl = container_of(work,
						struct wm_coeff_ctl,
						work);
	struct cs_dsp_coeff_ctl *cs_ctl = ctl->cs_ctl;
	struct wm_adsp *dsp = container_of(cs_ctl->dsp,
					   struct wm_adsp,
					   cs_dsp);
	struct snd_kcontrol_new *kcontrol;

	kcontrol = kzalloc(sizeof(*kcontrol), GFP_KERNEL);
	if (!kcontrol)
		return;

	kcontrol->name = ctl->name;
	kcontrol->info = wm_coeff_info;
	kcontrol->iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	kcontrol->tlv.c = snd_soc_bytes_tlv_callback;
	kcontrol->private_value = (unsigned long)&ctl->bytes_ext;
	kcontrol->access = wmfw_convert_flags(cs_ctl->flags, cs_ctl->len);

	switch (cs_ctl->type) {
	case WMFW_CTL_TYPE_ACKED:
		kcontrol->get = wm_coeff_get_acked;
		kcontrol->put = wm_coeff_put_acked;
		break;
	default:
		if (kcontrol->access & SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK) {
			ctl->bytes_ext.max = cs_ctl->len;
			ctl->bytes_ext.get = wm_coeff_tlv_get;
			ctl->bytes_ext.put = wm_coeff_tlv_put;
		} else {
			kcontrol->get = wm_coeff_get;
			kcontrol->put = wm_coeff_put;
		}
		break;
	}

	snd_soc_add_component_controls(dsp->component, kcontrol, 1);

	kfree(kcontrol);
}

static int wm_adsp_control_add(struct cs_dsp_coeff_ctl *cs_ctl)
{
	struct wm_adsp *dsp = container_of(cs_ctl->dsp, struct wm_adsp, cs_dsp);
	struct cs_dsp *cs_dsp = &dsp->cs_dsp;
	struct wm_coeff_ctl *ctl;
	char name[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
	const char *region_name;
	int ret;

	if (cs_ctl->flags & WMFW_CTL_FLAG_SYS)
		return 0;

	region_name = cs_dsp_mem_region_name(cs_ctl->alg_region.type);
	if (!region_name) {
		adsp_err(dsp, "Unknown region type: %d\n", cs_ctl->alg_region.type);
		return -EINVAL;
	}

	switch (cs_dsp->fw_ver) {
	case 0:
	case 1:
		ret = scnprintf(name, SNDRV_CTL_ELEM_ID_NAME_MAXLEN,
				"%s %s %x", cs_dsp->name, region_name,
				cs_ctl->alg_region.alg);
		break;
	case 2:
		ret = scnprintf(name, SNDRV_CTL_ELEM_ID_NAME_MAXLEN,
				"%s%c %.12s %x", cs_dsp->name, *region_name,
				wm_adsp_fw_text[dsp->fw], cs_ctl->alg_region.alg);
		break;
	default:
		ret = scnprintf(name, SNDRV_CTL_ELEM_ID_NAME_MAXLEN,
				"%s %.12s %x", cs_dsp->name,
				wm_adsp_fw_text[dsp->fw], cs_ctl->alg_region.alg);
		break;
	}

	if (cs_ctl->subname) {
		int avail = SNDRV_CTL_ELEM_ID_NAME_MAXLEN - ret - 2;
		int skip = 0;

		if (dsp->component->name_prefix)
			avail -= strlen(dsp->component->name_prefix) + 1;

		/* Truncate the subname from the start if it is too long */
		if (cs_ctl->subname_len > avail)
			skip = cs_ctl->subname_len - avail;

		snprintf(name + ret, SNDRV_CTL_ELEM_ID_NAME_MAXLEN - ret,
			 " %.*s", cs_ctl->subname_len - skip, cs_ctl->subname + skip);
	}

	ctl = kzalloc(sizeof(*ctl), GFP_KERNEL);
	if (!ctl)
		return -ENOMEM;
	ctl->cs_ctl = cs_ctl;

	ctl->name = kmemdup(name, strlen(name) + 1, GFP_KERNEL);
	if (!ctl->name) {
		ret = -ENOMEM;
		goto err_ctl;
	}

	cs_ctl->priv = ctl;

	INIT_WORK(&ctl->work, wm_adsp_ctl_work);
	schedule_work(&ctl->work);

	return 0;

err_ctl:
	kfree(ctl);

	return ret;
}

static void wm_adsp_control_remove(struct cs_dsp_coeff_ctl *cs_ctl)
{
	struct wm_coeff_ctl *ctl = cs_ctl->priv;

	cancel_work_sync(&ctl->work);

	kfree(ctl->name);
	kfree(ctl);
}

int wm_adsp_write_ctl(struct wm_adsp *dsp, const char *name, int type,
		      unsigned int alg, void *buf, size_t len)
{
	struct cs_dsp_coeff_ctl *cs_ctl = cs_dsp_get_ctl(&dsp->cs_dsp, name, type, alg);
	struct wm_coeff_ctl *ctl;
	int ret;

	mutex_lock(&dsp->cs_dsp.pwr_lock);
	ret = cs_dsp_coeff_write_ctrl(cs_ctl, 0, buf, len);
	mutex_unlock(&dsp->cs_dsp.pwr_lock);

	if (ret < 0)
		return ret;

	if (ret == 0 || (cs_ctl->flags & WMFW_CTL_FLAG_SYS))
		return 0;

	ctl = cs_ctl->priv;

	return snd_soc_component_notify_control(dsp->component, ctl->name);
}
EXPORT_SYMBOL_GPL(wm_adsp_write_ctl);

int wm_adsp_read_ctl(struct wm_adsp *dsp, const char *name, int type,
		     unsigned int alg, void *buf, size_t len)
{
	int ret;

	mutex_lock(&dsp->cs_dsp.pwr_lock);
	ret = cs_dsp_coeff_read_ctrl(cs_dsp_get_ctl(&dsp->cs_dsp, name, type, alg),
				     0, buf, len);
	mutex_unlock(&dsp->cs_dsp.pwr_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(wm_adsp_read_ctl);

static void wm_adsp_release_firmware_files(struct wm_adsp *dsp,
					   const struct firmware *wmfw_firmware,
					   char *wmfw_filename,
					   const struct firmware *coeff_firmware,
					   char *coeff_filename)
{
	if (wmfw_firmware)
		release_firmware(wmfw_firmware);
	kfree(wmfw_filename);

	if (coeff_firmware)
		release_firmware(coeff_firmware);
	kfree(coeff_filename);
}

static int wm_adsp_request_firmware_file(struct wm_adsp *dsp,
					 const struct firmware **firmware, char **filename,
					 const char *dir, const char *system_name,
					 const char *asoc_component_prefix,
					 const char *filetype)
{
	struct cs_dsp *cs_dsp = &dsp->cs_dsp;
	char *s, c;
	int ret = 0;

	if (system_name && asoc_component_prefix)
		*filename = kasprintf(GFP_KERNEL, "%s%s-%s-%s-%s-%s.%s", dir, dsp->part,
				      dsp->fwf_name, wm_adsp_fw[dsp->fw].file, system_name,
				      asoc_component_prefix, filetype);
	else if (system_name)
		*filename = kasprintf(GFP_KERNEL, "%s%s-%s-%s-%s.%s", dir, dsp->part,
				      dsp->fwf_name, wm_adsp_fw[dsp->fw].file, system_name,
				      filetype);
	else
		*filename = kasprintf(GFP_KERNEL, "%s%s-%s-%s.%s", dir, dsp->part, dsp->fwf_name,
				      wm_adsp_fw[dsp->fw].file, filetype);

	if (*filename == NULL)
		return -ENOMEM;

	/*
	 * Make sure that filename is lower-case and any non alpha-numeric
	 * characters except full stop and forward slash are replaced with
	 * hyphens.
	 */
	s = *filename;
	while (*s) {
		c = *s;
		if (isalnum(c))
			*s = tolower(c);
		else if ((c != '.') && (c != '/'))
			*s = '-';
		s++;
	}

	ret = firmware_request_nowarn(firmware, *filename, cs_dsp->dev);
	if (ret != 0) {
		adsp_dbg(dsp, "Failed to request '%s'\n", *filename);
		kfree(*filename);
		*filename = NULL;
	} else {
		adsp_dbg(dsp, "Found '%s'\n", *filename);
	}

	return ret;
}

static const char *cirrus_dir = "cirrus/";
static int wm_adsp_request_firmware_files(struct wm_adsp *dsp,
					  const struct firmware **wmfw_firmware,
					  char **wmfw_filename,
					  const struct firmware **coeff_firmware,
					  char **coeff_filename)
{
	const char *system_name = dsp->system_name;
	const char *asoc_component_prefix = dsp->component->name_prefix;
	int ret = 0;

	if (system_name && asoc_component_prefix) {
		if (!wm_adsp_request_firmware_file(dsp, wmfw_firmware, wmfw_filename,
						   cirrus_dir, system_name,
						   asoc_component_prefix, "wmfw")) {
			wm_adsp_request_firmware_file(dsp, coeff_firmware, coeff_filename,
						      cirrus_dir, system_name,
						      asoc_component_prefix, "bin");
			return 0;
		}
	}

	if (system_name) {
		if (!wm_adsp_request_firmware_file(dsp, wmfw_firmware, wmfw_filename,
						   cirrus_dir, system_name,
						   NULL, "wmfw")) {
			if (asoc_component_prefix)
				wm_adsp_request_firmware_file(dsp, coeff_firmware, coeff_filename,
							      cirrus_dir, system_name,
							      asoc_component_prefix, "bin");

			if (!*coeff_firmware)
				wm_adsp_request_firmware_file(dsp, coeff_firmware, coeff_filename,
							      cirrus_dir, system_name,
							      NULL, "bin");
			return 0;
		}
	}

	if (!wm_adsp_request_firmware_file(dsp, wmfw_firmware, wmfw_filename,
					   "", NULL, NULL, "wmfw")) {
		wm_adsp_request_firmware_file(dsp, coeff_firmware, coeff_filename,
					      "", NULL, NULL, "bin");
		return 0;
	}

	ret = wm_adsp_request_firmware_file(dsp, wmfw_firmware, wmfw_filename,
					    cirrus_dir, NULL, NULL, "wmfw");
	if (!ret) {
		wm_adsp_request_firmware_file(dsp, coeff_firmware, coeff_filename,
					      cirrus_dir, NULL, NULL, "bin");
		return 0;
	}

	if (dsp->wmfw_optional) {
		if (system_name) {
			if (asoc_component_prefix)
				wm_adsp_request_firmware_file(dsp, coeff_firmware, coeff_filename,
							      cirrus_dir, system_name,
							      asoc_component_prefix, "bin");

			if (!*coeff_firmware)
				wm_adsp_request_firmware_file(dsp, coeff_firmware, coeff_filename,
							      cirrus_dir, system_name,
							      NULL, "bin");
		}

		if (!*coeff_firmware)
			wm_adsp_request_firmware_file(dsp, coeff_firmware, coeff_filename,
						      "", NULL, NULL, "bin");

		if (!*coeff_firmware)
			wm_adsp_request_firmware_file(dsp, coeff_firmware, coeff_filename,
						      cirrus_dir, NULL, NULL, "bin");

		return 0;
	}

	adsp_err(dsp, "Failed to request firmware <%s>%s-%s-%s<-%s<%s>>.wmfw\n",
		 cirrus_dir, dsp->part, dsp->fwf_name, wm_adsp_fw[dsp->fw].file,
		 system_name, asoc_component_prefix);

	return -ENOENT;
}

static int wm_adsp_common_init(struct wm_adsp *dsp)
{
	char *p;

	INIT_LIST_HEAD(&dsp->compr_list);
	INIT_LIST_HEAD(&dsp->buffer_list);

	if (!dsp->fwf_name) {
		p = devm_kstrdup(dsp->cs_dsp.dev, dsp->cs_dsp.name, GFP_KERNEL);
		if (!p)
			return -ENOMEM;

		dsp->fwf_name = p;
		for (; *p != 0; ++p)
			*p = tolower(*p);
	}

	return 0;
}

int wm_adsp1_init(struct wm_adsp *dsp)
{
	int ret;

	dsp->cs_dsp.client_ops = &wm_adsp1_client_ops;

	ret = cs_dsp_adsp1_init(&dsp->cs_dsp);
	if (ret)
		return ret;

	return wm_adsp_common_init(dsp);
}
EXPORT_SYMBOL_GPL(wm_adsp1_init);

int wm_adsp1_event(struct snd_soc_dapm_widget *w,
		   struct snd_kcontrol *kcontrol,
		   int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct wm_adsp *dsps = snd_soc_component_get_drvdata(component);
	struct wm_adsp *dsp = &dsps[w->shift];
	int ret = 0;
	char *wmfw_filename = NULL;
	const struct firmware *wmfw_firmware = NULL;
	char *coeff_filename = NULL;
	const struct firmware *coeff_firmware = NULL;

	dsp->component = component;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		ret = wm_adsp_request_firmware_files(dsp,
						     &wmfw_firmware, &wmfw_filename,
						     &coeff_firmware, &coeff_filename);
		if (ret)
			break;

		ret = cs_dsp_adsp1_power_up(&dsp->cs_dsp,
					    wmfw_firmware, wmfw_filename,
					    coeff_firmware, coeff_filename,
					    wm_adsp_fw_text[dsp->fw]);

		wm_adsp_release_firmware_files(dsp,
					       wmfw_firmware, wmfw_filename,
					       coeff_firmware, coeff_filename);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		cs_dsp_adsp1_power_down(&dsp->cs_dsp);
		break;
	default:
		break;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(wm_adsp1_event);

int wm_adsp2_set_dspclk(struct snd_soc_dapm_widget *w, unsigned int freq)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct wm_adsp *dsps = snd_soc_component_get_drvdata(component);
	struct wm_adsp *dsp = &dsps[w->shift];

	return cs_dsp_set_dspclk(&dsp->cs_dsp, freq);
}
EXPORT_SYMBOL_GPL(wm_adsp2_set_dspclk);

int wm_adsp2_preloader_get(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wm_adsp *dsps = snd_soc_component_get_drvdata(component);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct wm_adsp *dsp = &dsps[mc->shift - 1];

	ucontrol->value.integer.value[0] = dsp->preloaded;

	return 0;
}
EXPORT_SYMBOL_GPL(wm_adsp2_preloader_get);

int wm_adsp2_preloader_put(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wm_adsp *dsps = snd_soc_component_get_drvdata(component);
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct wm_adsp *dsp = &dsps[mc->shift - 1];
	char preload[32];

	if (dsp->preloaded == ucontrol->value.integer.value[0])
		return 0;

	snprintf(preload, ARRAY_SIZE(preload), "%s Preload", dsp->cs_dsp.name);

	if (ucontrol->value.integer.value[0] || dsp->toggle_preload)
		snd_soc_component_force_enable_pin(component, preload);
	else
		snd_soc_component_disable_pin(component, preload);

	snd_soc_dapm_sync(dapm);

	flush_work(&dsp->boot_work);

	dsp->preloaded = ucontrol->value.integer.value[0];

	if (dsp->toggle_preload) {
		snd_soc_component_disable_pin(component, preload);
		snd_soc_dapm_sync(dapm);
	}

	return 1;
}
EXPORT_SYMBOL_GPL(wm_adsp2_preloader_put);

int wm_adsp_power_up(struct wm_adsp *dsp, bool load_firmware)
{
	int ret = 0;
	char *wmfw_filename = NULL;
	const struct firmware *wmfw_firmware = NULL;
	char *coeff_filename = NULL;
	const struct firmware *coeff_firmware = NULL;

	if (load_firmware) {
		ret = wm_adsp_request_firmware_files(dsp,
						     &wmfw_firmware, &wmfw_filename,
						     &coeff_firmware, &coeff_filename);
		if (ret)
			return ret;
	}

	ret = cs_dsp_power_up(&dsp->cs_dsp,
			      wmfw_firmware, wmfw_filename,
			      coeff_firmware, coeff_filename,
			      wm_adsp_fw_text[dsp->fw]);

	wm_adsp_release_firmware_files(dsp,
				       wmfw_firmware, wmfw_filename,
				       coeff_firmware, coeff_filename);

	return ret;
}
EXPORT_SYMBOL_GPL(wm_adsp_power_up);

void wm_adsp_power_down(struct wm_adsp *dsp)
{
	cs_dsp_power_down(&dsp->cs_dsp);
}
EXPORT_SYMBOL_GPL(wm_adsp_power_down);

static void wm_adsp_boot_work(struct work_struct *work)
{
	struct wm_adsp *dsp = container_of(work,
					   struct wm_adsp,
					   boot_work);

	wm_adsp_power_up(dsp, true);
}

int wm_adsp_early_event(struct snd_soc_dapm_widget *w,
			struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct wm_adsp *dsps = snd_soc_component_get_drvdata(component);
	struct wm_adsp *dsp = &dsps[w->shift];

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		queue_work(system_unbound_wq, &dsp->boot_work);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		wm_adsp_power_down(dsp);
		break;
	default:
		break;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(wm_adsp_early_event);

static int wm_adsp_pre_run(struct cs_dsp *cs_dsp)
{
	struct wm_adsp *dsp = container_of(cs_dsp, struct wm_adsp, cs_dsp);

	if (!dsp->pre_run)
		return 0;

	return (*dsp->pre_run)(dsp);
}

static int wm_adsp_event_post_run(struct cs_dsp *cs_dsp)
{
	struct wm_adsp *dsp = container_of(cs_dsp, struct wm_adsp, cs_dsp);

	if (wm_adsp_fw[dsp->fw].num_caps != 0)
		return wm_adsp_buffer_init(dsp);

	return 0;
}

static void wm_adsp_event_post_stop(struct cs_dsp *cs_dsp)
{
	struct wm_adsp *dsp = container_of(cs_dsp, struct wm_adsp, cs_dsp);

	if (wm_adsp_fw[dsp->fw].num_caps != 0)
		wm_adsp_buffer_free(dsp);

	dsp->fatal_error = false;
}

int wm_adsp_event(struct snd_soc_dapm_widget *w,
		  struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct wm_adsp *dsps = snd_soc_component_get_drvdata(component);
	struct wm_adsp *dsp = &dsps[w->shift];
	int ret = 0;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		flush_work(&dsp->boot_work);
		ret = cs_dsp_run(&dsp->cs_dsp);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		cs_dsp_stop(&dsp->cs_dsp);
		break;
	default:
		break;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(wm_adsp_event);

int wm_adsp2_component_probe(struct wm_adsp *dsp, struct snd_soc_component *component)
{
	char preload[32];

	if (!dsp->cs_dsp.no_core_startstop) {
		snprintf(preload, ARRAY_SIZE(preload), "%s Preload", dsp->cs_dsp.name);
		snd_soc_component_disable_pin(component, preload);
	}

	cs_dsp_init_debugfs(&dsp->cs_dsp, component->debugfs_root);

	dsp->component = component;

	return 0;
}
EXPORT_SYMBOL_GPL(wm_adsp2_component_probe);

int wm_adsp2_component_remove(struct wm_adsp *dsp, struct snd_soc_component *component)
{
	cs_dsp_cleanup_debugfs(&dsp->cs_dsp);

	return 0;
}
EXPORT_SYMBOL_GPL(wm_adsp2_component_remove);

int wm_adsp2_init(struct wm_adsp *dsp)
{
	int ret;

	INIT_WORK(&dsp->boot_work, wm_adsp_boot_work);

	dsp->sys_config_size = sizeof(struct wm_adsp_system_config_xm_hdr);
	dsp->cs_dsp.client_ops = &wm_adsp2_client_ops;

	ret = cs_dsp_adsp2_init(&dsp->cs_dsp);
	if (ret)
		return ret;

	return wm_adsp_common_init(dsp);
}
EXPORT_SYMBOL_GPL(wm_adsp2_init);

int wm_halo_init(struct wm_adsp *dsp)
{
	int ret;

	INIT_WORK(&dsp->boot_work, wm_adsp_boot_work);

	dsp->sys_config_size = sizeof(struct wm_halo_system_config_xm_hdr);
	dsp->cs_dsp.client_ops = &wm_adsp2_client_ops;

	ret = cs_dsp_halo_init(&dsp->cs_dsp);
	if (ret)
		return ret;

	return wm_adsp_common_init(dsp);
}
EXPORT_SYMBOL_GPL(wm_halo_init);

void wm_adsp2_remove(struct wm_adsp *dsp)
{
	cs_dsp_remove(&dsp->cs_dsp);
}
EXPORT_SYMBOL_GPL(wm_adsp2_remove);

static inline int wm_adsp_compr_attached(struct wm_adsp_compr *compr)
{
	return compr->buf != NULL;
}

static int wm_adsp_compr_attach(struct wm_adsp_compr *compr)
{
	struct wm_adsp_compr_buf *buf = NULL, *tmp;

	if (compr->dsp->fatal_error)
		return -EINVAL;

	list_for_each_entry(tmp, &compr->dsp->buffer_list, list) {
		if (!tmp->name || !strcmp(compr->name, tmp->name)) {
			buf = tmp;
			break;
		}
	}

	if (!buf)
		return -EINVAL;

	compr->buf = buf;
	buf->compr = compr;

	return 0;
}

static void wm_adsp_compr_detach(struct wm_adsp_compr *compr)
{
	if (!compr)
		return;

	/* Wake the poll so it can see buffer is no longer attached */
	if (compr->stream)
		snd_compr_fragment_elapsed(compr->stream);

	if (wm_adsp_compr_attached(compr)) {
		compr->buf->compr = NULL;
		compr->buf = NULL;
	}
}

int wm_adsp_compr_open(struct wm_adsp *dsp, struct snd_compr_stream *stream)
{
	struct wm_adsp_compr *compr, *tmp;
	struct snd_soc_pcm_runtime *rtd = stream->private_data;
	int ret = 0;

	mutex_lock(&dsp->cs_dsp.pwr_lock);

	if (wm_adsp_fw[dsp->fw].num_caps == 0) {
		adsp_err(dsp, "%s: Firmware does not support compressed API\n",
			 asoc_rtd_to_codec(rtd, 0)->name);
		ret = -ENXIO;
		goto out;
	}

	if (wm_adsp_fw[dsp->fw].compr_direction != stream->direction) {
		adsp_err(dsp, "%s: Firmware does not support stream direction\n",
			 asoc_rtd_to_codec(rtd, 0)->name);
		ret = -EINVAL;
		goto out;
	}

	list_for_each_entry(tmp, &dsp->compr_list, list) {
		if (!strcmp(tmp->name, asoc_rtd_to_codec(rtd, 0)->name)) {
			adsp_err(dsp, "%s: Only a single stream supported per dai\n",
				 asoc_rtd_to_codec(rtd, 0)->name);
			ret = -EBUSY;
			goto out;
		}
	}

	compr = kzalloc(sizeof(*compr), GFP_KERNEL);
	if (!compr) {
		ret = -ENOMEM;
		goto out;
	}

	compr->dsp = dsp;
	compr->stream = stream;
	compr->name = asoc_rtd_to_codec(rtd, 0)->name;

	list_add_tail(&compr->list, &dsp->compr_list);

	stream->runtime->private_data = compr;

out:
	mutex_unlock(&dsp->cs_dsp.pwr_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(wm_adsp_compr_open);

int wm_adsp_compr_free(struct snd_soc_component *component,
		       struct snd_compr_stream *stream)
{
	struct wm_adsp_compr *compr = stream->runtime->private_data;
	struct wm_adsp *dsp = compr->dsp;

	mutex_lock(&dsp->cs_dsp.pwr_lock);

	wm_adsp_compr_detach(compr);
	list_del(&compr->list);

	kfree(compr->raw_buf);
	kfree(compr);

	mutex_unlock(&dsp->cs_dsp.pwr_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(wm_adsp_compr_free);

static int wm_adsp_compr_check_params(struct snd_compr_stream *stream,
				      struct snd_compr_params *params)
{
	struct wm_adsp_compr *compr = stream->runtime->private_data;
	struct wm_adsp *dsp = compr->dsp;
	const struct wm_adsp_fw_caps *caps;
	const struct snd_codec_desc *desc;
	int i, j;

	if (params->buffer.fragment_size < WM_ADSP_MIN_FRAGMENT_SIZE ||
	    params->buffer.fragment_size > WM_ADSP_MAX_FRAGMENT_SIZE ||
	    params->buffer.fragments < WM_ADSP_MIN_FRAGMENTS ||
	    params->buffer.fragments > WM_ADSP_MAX_FRAGMENTS ||
	    params->buffer.fragment_size % CS_DSP_DATA_WORD_SIZE) {
		compr_err(compr, "Invalid buffer fragsize=%d fragments=%d\n",
			  params->buffer.fragment_size,
			  params->buffer.fragments);

		return -EINVAL;
	}

	for (i = 0; i < wm_adsp_fw[dsp->fw].num_caps; i++) {
		caps = &wm_adsp_fw[dsp->fw].caps[i];
		desc = &caps->desc;

		if (caps->id != params->codec.id)
			continue;

		if (stream->direction == SND_COMPRESS_PLAYBACK) {
			if (desc->max_ch < params->codec.ch_out)
				continue;
		} else {
			if (desc->max_ch < params->codec.ch_in)
				continue;
		}

		if (!(desc->formats & (1 << params->codec.format)))
			continue;

		for (j = 0; j < desc->num_sample_rates; ++j)
			if (desc->sample_rates[j] == params->codec.sample_rate)
				return 0;
	}

	compr_err(compr, "Invalid params id=%u ch=%u,%u rate=%u fmt=%u\n",
		  params->codec.id, params->codec.ch_in, params->codec.ch_out,
		  params->codec.sample_rate, params->codec.format);
	return -EINVAL;
}

static inline unsigned int wm_adsp_compr_frag_words(struct wm_adsp_compr *compr)
{
	return compr->size.fragment_size / CS_DSP_DATA_WORD_SIZE;
}

int wm_adsp_compr_set_params(struct snd_soc_component *component,
			     struct snd_compr_stream *stream,
			     struct snd_compr_params *params)
{
	struct wm_adsp_compr *compr = stream->runtime->private_data;
	unsigned int size;
	int ret;

	ret = wm_adsp_compr_check_params(stream, params);
	if (ret)
		return ret;

	compr->size = params->buffer;

	compr_dbg(compr, "fragment_size=%d fragments=%d\n",
		  compr->size.fragment_size, compr->size.fragments);

	size = wm_adsp_compr_frag_words(compr) * sizeof(*compr->raw_buf);
	compr->raw_buf = kmalloc(size, GFP_DMA | GFP_KERNEL);
	if (!compr->raw_buf)
		return -ENOMEM;

	compr->sample_rate = params->codec.sample_rate;

	return 0;
}
EXPORT_SYMBOL_GPL(wm_adsp_compr_set_params);

int wm_adsp_compr_get_caps(struct snd_soc_component *component,
			   struct snd_compr_stream *stream,
			   struct snd_compr_caps *caps)
{
	struct wm_adsp_compr *compr = stream->runtime->private_data;
	int fw = compr->dsp->fw;
	int i;

	if (wm_adsp_fw[fw].caps) {
		for (i = 0; i < wm_adsp_fw[fw].num_caps; i++)
			caps->codecs[i] = wm_adsp_fw[fw].caps[i].id;

		caps->num_codecs = i;
		caps->direction = wm_adsp_fw[fw].compr_direction;

		caps->min_fragment_size = WM_ADSP_MIN_FRAGMENT_SIZE;
		caps->max_fragment_size = WM_ADSP_MAX_FRAGMENT_SIZE;
		caps->min_fragments = WM_ADSP_MIN_FRAGMENTS;
		caps->max_fragments = WM_ADSP_MAX_FRAGMENTS;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(wm_adsp_compr_get_caps);

static inline int wm_adsp_buffer_read(struct wm_adsp_compr_buf *buf,
				      unsigned int field_offset, u32 *data)
{
	return cs_dsp_read_data_word(&buf->dsp->cs_dsp, buf->host_buf_mem_type,
				     buf->host_buf_ptr + field_offset, data);
}

static inline int wm_adsp_buffer_write(struct wm_adsp_compr_buf *buf,
				       unsigned int field_offset, u32 data)
{
	return cs_dsp_write_data_word(&buf->dsp->cs_dsp, buf->host_buf_mem_type,
				      buf->host_buf_ptr + field_offset,
				      data);
}

static int wm_adsp_buffer_populate(struct wm_adsp_compr_buf *buf)
{
	const struct wm_adsp_fw_caps *caps = wm_adsp_fw[buf->dsp->fw].caps;
	struct wm_adsp_buffer_region *region;
	u32 offset = 0;
	int i, ret;

	buf->regions = kcalloc(caps->num_regions, sizeof(*buf->regions),
			       GFP_KERNEL);
	if (!buf->regions)
		return -ENOMEM;

	for (i = 0; i < caps->num_regions; ++i) {
		region = &buf->regions[i];

		region->offset = offset;
		region->mem_type = caps->region_defs[i].mem_type;

		ret = wm_adsp_buffer_read(buf, caps->region_defs[i].base_offset,
					  &region->base_addr);
		if (ret < 0)
			goto err;

		ret = wm_adsp_buffer_read(buf, caps->region_defs[i].size_offset,
					  &offset);
		if (ret < 0)
			goto err;

		region->cumulative_size = offset;

		compr_dbg(buf,
			  "region=%d type=%d base=%08x off=%08x size=%08x\n",
			  i, region->mem_type, region->base_addr,
			  region->offset, region->cumulative_size);
	}

	return 0;

err:
	kfree(buf->regions);
	return ret;
}

static void wm_adsp_buffer_clear(struct wm_adsp_compr_buf *buf)
{
	buf->irq_count = 0xFFFFFFFF;
	buf->read_index = -1;
	buf->avail = 0;
}

static struct wm_adsp_compr_buf *wm_adsp_buffer_alloc(struct wm_adsp *dsp)
{
	struct wm_adsp_compr_buf *buf;

	buf = kzalloc(sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return NULL;

	buf->dsp = dsp;

	wm_adsp_buffer_clear(buf);

	return buf;
}

static int wm_adsp_buffer_parse_legacy(struct wm_adsp *dsp)
{
	struct cs_dsp_alg_region *alg_region;
	struct wm_adsp_compr_buf *buf;
	u32 xmalg, addr, magic;
	int i, ret;

	alg_region = cs_dsp_find_alg_region(&dsp->cs_dsp, WMFW_ADSP2_XM, dsp->cs_dsp.fw_id);
	if (!alg_region) {
		adsp_err(dsp, "No algorithm region found\n");
		return -EINVAL;
	}

	xmalg = dsp->sys_config_size / sizeof(__be32);

	addr = alg_region->base + xmalg + ALG_XM_FIELD(magic);
	ret = cs_dsp_read_data_word(&dsp->cs_dsp, WMFW_ADSP2_XM, addr, &magic);
	if (ret < 0)
		return ret;

	if (magic != WM_ADSP_ALG_XM_STRUCT_MAGIC)
		return -ENODEV;

	buf = wm_adsp_buffer_alloc(dsp);
	if (!buf)
		return -ENOMEM;

	addr = alg_region->base + xmalg + ALG_XM_FIELD(host_buf_ptr);
	for (i = 0; i < 5; ++i) {
		ret = cs_dsp_read_data_word(&dsp->cs_dsp, WMFW_ADSP2_XM, addr,
					    &buf->host_buf_ptr);
		if (ret < 0)
			goto err;

		if (buf->host_buf_ptr)
			break;

		usleep_range(1000, 2000);
	}

	if (!buf->host_buf_ptr) {
		ret = -EIO;
		goto err;
	}

	buf->host_buf_mem_type = WMFW_ADSP2_XM;

	ret = wm_adsp_buffer_populate(buf);
	if (ret < 0)
		goto err;

	list_add_tail(&buf->list, &dsp->buffer_list);

	compr_dbg(buf, "legacy host_buf_ptr=%x\n", buf->host_buf_ptr);

	return 0;

err:
	kfree(buf);

	return ret;
}

static int wm_adsp_buffer_parse_coeff(struct cs_dsp_coeff_ctl *cs_ctl)
{
	struct wm_adsp_host_buf_coeff_v1 coeff_v1;
	struct wm_adsp_compr_buf *buf;
	struct wm_adsp *dsp = container_of(cs_ctl->dsp, struct wm_adsp, cs_dsp);
	unsigned int version = 0;
	int ret, i;

	for (i = 0; i < 5; ++i) {
		ret = cs_dsp_coeff_read_ctrl(cs_ctl, 0, &coeff_v1,
					     min(cs_ctl->len, sizeof(coeff_v1)));
		if (ret < 0)
			return ret;

		if (coeff_v1.host_buf_ptr)
			break;

		usleep_range(1000, 2000);
	}

	if (!coeff_v1.host_buf_ptr) {
		adsp_err(dsp, "Failed to acquire host buffer\n");
		return -EIO;
	}

	buf = wm_adsp_buffer_alloc(dsp);
	if (!buf)
		return -ENOMEM;

	buf->host_buf_mem_type = cs_ctl->alg_region.type;
	buf->host_buf_ptr = be32_to_cpu(coeff_v1.host_buf_ptr);

	ret = wm_adsp_buffer_populate(buf);
	if (ret < 0)
		goto err;

	/*
	 * v0 host_buffer coefficients didn't have versioning, so if the
	 * control is one word, assume version 0.
	 */
	if (cs_ctl->len == 4)
		goto done;

	version = be32_to_cpu(coeff_v1.versions) & HOST_BUF_COEFF_COMPAT_VER_MASK;
	version >>= HOST_BUF_COEFF_COMPAT_VER_SHIFT;

	if (version > HOST_BUF_COEFF_SUPPORTED_COMPAT_VER) {
		adsp_err(dsp,
			 "Host buffer coeff ver %u > supported version %u\n",
			 version, HOST_BUF_COEFF_SUPPORTED_COMPAT_VER);
		ret = -EINVAL;
		goto err;
	}

	cs_dsp_remove_padding((u32 *)&coeff_v1.name, ARRAY_SIZE(coeff_v1.name));

	buf->name = kasprintf(GFP_KERNEL, "%s-dsp-%s", dsp->part,
			      (char *)&coeff_v1.name);

done:
	list_add_tail(&buf->list, &dsp->buffer_list);

	compr_dbg(buf, "host_buf_ptr=%x coeff version %u\n",
		  buf->host_buf_ptr, version);

	return version;

err:
	kfree(buf);

	return ret;
}

static int wm_adsp_buffer_init(struct wm_adsp *dsp)
{
	struct cs_dsp_coeff_ctl *cs_ctl;
	int ret;

	list_for_each_entry(cs_ctl, &dsp->cs_dsp.ctl_list, list) {
		if (cs_ctl->type != WMFW_CTL_TYPE_HOST_BUFFER)
			continue;

		if (!cs_ctl->enabled)
			continue;

		ret = wm_adsp_buffer_parse_coeff(cs_ctl);
		if (ret < 0) {
			adsp_err(dsp, "Failed to parse coeff: %d\n", ret);
			goto error;
		} else if (ret == 0) {
			/* Only one buffer supported for version 0 */
			return 0;
		}
	}

	if (list_empty(&dsp->buffer_list)) {
		/* Fall back to legacy support */
		ret = wm_adsp_buffer_parse_legacy(dsp);
		if (ret == -ENODEV)
			adsp_info(dsp, "Legacy support not available\n");
		else if (ret)
			adsp_warn(dsp, "Failed to parse legacy: %d\n", ret);
	}

	return 0;

error:
	wm_adsp_buffer_free(dsp);
	return ret;
}

static int wm_adsp_buffer_free(struct wm_adsp *dsp)
{
	struct wm_adsp_compr_buf *buf, *tmp;

	list_for_each_entry_safe(buf, tmp, &dsp->buffer_list, list) {
		wm_adsp_compr_detach(buf->compr);

		kfree(buf->name);
		kfree(buf->regions);
		list_del(&buf->list);
		kfree(buf);
	}

	return 0;
}

static int wm_adsp_buffer_get_error(struct wm_adsp_compr_buf *buf)
{
	int ret;

	ret = wm_adsp_buffer_read(buf, HOST_BUFFER_FIELD(error), &buf->error);
	if (ret < 0) {
		compr_err(buf, "Failed to check buffer error: %d\n", ret);
		return ret;
	}
	if (buf->error != 0) {
		compr_err(buf, "Buffer error occurred: %d\n", buf->error);
		return -EIO;
	}

	return 0;
}

int wm_adsp_compr_trigger(struct snd_soc_component *component,
			  struct snd_compr_stream *stream, int cmd)
{
	struct wm_adsp_compr *compr = stream->runtime->private_data;
	struct wm_adsp *dsp = compr->dsp;
	int ret = 0;

	compr_dbg(compr, "Trigger: %d\n", cmd);

	mutex_lock(&dsp->cs_dsp.pwr_lock);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		if (!wm_adsp_compr_attached(compr)) {
			ret = wm_adsp_compr_attach(compr);
			if (ret < 0) {
				compr_err(compr, "Failed to link buffer and stream: %d\n",
					  ret);
				break;
			}
		}

		ret = wm_adsp_buffer_get_error(compr->buf);
		if (ret < 0)
			break;

		/* Trigger the IRQ at one fragment of data */
		ret = wm_adsp_buffer_write(compr->buf,
					   HOST_BUFFER_FIELD(high_water_mark),
					   wm_adsp_compr_frag_words(compr));
		if (ret < 0) {
			compr_err(compr, "Failed to set high water mark: %d\n",
				  ret);
			break;
		}
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		if (wm_adsp_compr_attached(compr))
			wm_adsp_buffer_clear(compr->buf);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	mutex_unlock(&dsp->cs_dsp.pwr_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(wm_adsp_compr_trigger);

static inline int wm_adsp_buffer_size(struct wm_adsp_compr_buf *buf)
{
	int last_region = wm_adsp_fw[buf->dsp->fw].caps->num_regions - 1;

	return buf->regions[last_region].cumulative_size;
}

static int wm_adsp_buffer_update_avail(struct wm_adsp_compr_buf *buf)
{
	u32 next_read_index, next_write_index;
	int write_index, read_index, avail;
	int ret;

	/* Only sync read index if we haven't already read a valid index */
	if (buf->read_index < 0) {
		ret = wm_adsp_buffer_read(buf,
				HOST_BUFFER_FIELD(next_read_index),
				&next_read_index);
		if (ret < 0)
			return ret;

		read_index = sign_extend32(next_read_index, 23);

		if (read_index < 0) {
			compr_dbg(buf, "Avail check on unstarted stream\n");
			return 0;
		}

		buf->read_index = read_index;
	}

	ret = wm_adsp_buffer_read(buf, HOST_BUFFER_FIELD(next_write_index),
			&next_write_index);
	if (ret < 0)
		return ret;

	write_index = sign_extend32(next_write_index, 23);

	avail = write_index - buf->read_index;
	if (avail < 0)
		avail += wm_adsp_buffer_size(buf);

	compr_dbg(buf, "readindex=0x%x, writeindex=0x%x, avail=%d\n",
		  buf->read_index, write_index, avail * CS_DSP_DATA_WORD_SIZE);

	buf->avail = avail;

	return 0;
}

int wm_adsp_compr_handle_irq(struct wm_adsp *dsp)
{
	struct wm_adsp_compr_buf *buf;
	struct wm_adsp_compr *compr;
	int ret = 0;

	mutex_lock(&dsp->cs_dsp.pwr_lock);

	if (list_empty(&dsp->buffer_list)) {
		ret = -ENODEV;
		goto out;
	}

	adsp_dbg(dsp, "Handling buffer IRQ\n");

	list_for_each_entry(buf, &dsp->buffer_list, list) {
		compr = buf->compr;

		ret = wm_adsp_buffer_get_error(buf);
		if (ret < 0)
			goto out_notify; /* Wake poll to report error */

		ret = wm_adsp_buffer_read(buf, HOST_BUFFER_FIELD(irq_count),
					  &buf->irq_count);
		if (ret < 0) {
			compr_err(buf, "Failed to get irq_count: %d\n", ret);
			goto out;
		}

		ret = wm_adsp_buffer_update_avail(buf);
		if (ret < 0) {
			compr_err(buf, "Error reading avail: %d\n", ret);
			goto out;
		}

		if (wm_adsp_fw[dsp->fw].voice_trigger && buf->irq_count == 2)
			ret = WM_ADSP_COMPR_VOICE_TRIGGER;

out_notify:
		if (compr && compr->stream)
			snd_compr_fragment_elapsed(compr->stream);
	}

out:
	mutex_unlock(&dsp->cs_dsp.pwr_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(wm_adsp_compr_handle_irq);

static int wm_adsp_buffer_reenable_irq(struct wm_adsp_compr_buf *buf)
{
	if (buf->irq_count & 0x01)
		return 0;

	compr_dbg(buf, "Enable IRQ(0x%x) for next fragment\n", buf->irq_count);

	buf->irq_count |= 0x01;

	return wm_adsp_buffer_write(buf, HOST_BUFFER_FIELD(irq_ack),
				    buf->irq_count);
}

int wm_adsp_compr_pointer(struct snd_soc_component *component,
			  struct snd_compr_stream *stream,
			  struct snd_compr_tstamp *tstamp)
{
	struct wm_adsp_compr *compr = stream->runtime->private_data;
	struct wm_adsp *dsp = compr->dsp;
	struct wm_adsp_compr_buf *buf;
	int ret = 0;

	compr_dbg(compr, "Pointer request\n");

	mutex_lock(&dsp->cs_dsp.pwr_lock);

	buf = compr->buf;

	if (dsp->fatal_error || !buf || buf->error) {
		snd_compr_stop_error(stream, SNDRV_PCM_STATE_XRUN);
		ret = -EIO;
		goto out;
	}

	if (buf->avail < wm_adsp_compr_frag_words(compr)) {
		ret = wm_adsp_buffer_update_avail(buf);
		if (ret < 0) {
			compr_err(compr, "Error reading avail: %d\n", ret);
			goto out;
		}

		/*
		 * If we really have less than 1 fragment available tell the
		 * DSP to inform us once a whole fragment is available.
		 */
		if (buf->avail < wm_adsp_compr_frag_words(compr)) {
			ret = wm_adsp_buffer_get_error(buf);
			if (ret < 0) {
				if (buf->error)
					snd_compr_stop_error(stream,
							SNDRV_PCM_STATE_XRUN);
				goto out;
			}

			ret = wm_adsp_buffer_reenable_irq(buf);
			if (ret < 0) {
				compr_err(compr, "Failed to re-enable buffer IRQ: %d\n",
					  ret);
				goto out;
			}
		}
	}

	tstamp->copied_total = compr->copied_total;
	tstamp->copied_total += buf->avail * CS_DSP_DATA_WORD_SIZE;
	tstamp->sampling_rate = compr->sample_rate;

out:
	mutex_unlock(&dsp->cs_dsp.pwr_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(wm_adsp_compr_pointer);

static int wm_adsp_buffer_capture_block(struct wm_adsp_compr *compr, int target)
{
	struct wm_adsp_compr_buf *buf = compr->buf;
	unsigned int adsp_addr;
	int mem_type, nwords, max_read;
	int i, ret;

	/* Calculate read parameters */
	for (i = 0; i < wm_adsp_fw[buf->dsp->fw].caps->num_regions; ++i)
		if (buf->read_index < buf->regions[i].cumulative_size)
			break;

	if (i == wm_adsp_fw[buf->dsp->fw].caps->num_regions)
		return -EINVAL;

	mem_type = buf->regions[i].mem_type;
	adsp_addr = buf->regions[i].base_addr +
		    (buf->read_index - buf->regions[i].offset);

	max_read = wm_adsp_compr_frag_words(compr);
	nwords = buf->regions[i].cumulative_size - buf->read_index;

	if (nwords > target)
		nwords = target;
	if (nwords > buf->avail)
		nwords = buf->avail;
	if (nwords > max_read)
		nwords = max_read;
	if (!nwords)
		return 0;

	/* Read data from DSP */
	ret = cs_dsp_read_raw_data_block(&buf->dsp->cs_dsp, mem_type, adsp_addr,
					 nwords, (__be32 *)compr->raw_buf);
	if (ret < 0)
		return ret;

	cs_dsp_remove_padding(compr->raw_buf, nwords);

	/* update read index to account for words read */
	buf->read_index += nwords;
	if (buf->read_index == wm_adsp_buffer_size(buf))
		buf->read_index = 0;

	ret = wm_adsp_buffer_write(buf, HOST_BUFFER_FIELD(next_read_index),
				   buf->read_index);
	if (ret < 0)
		return ret;

	/* update avail to account for words read */
	buf->avail -= nwords;

	return nwords;
}

static int wm_adsp_compr_read(struct wm_adsp_compr *compr,
			      char __user *buf, size_t count)
{
	struct wm_adsp *dsp = compr->dsp;
	int ntotal = 0;
	int nwords, nbytes;

	compr_dbg(compr, "Requested read of %zu bytes\n", count);

	if (dsp->fatal_error || !compr->buf || compr->buf->error) {
		snd_compr_stop_error(compr->stream, SNDRV_PCM_STATE_XRUN);
		return -EIO;
	}

	count /= CS_DSP_DATA_WORD_SIZE;

	do {
		nwords = wm_adsp_buffer_capture_block(compr, count);
		if (nwords < 0) {
			compr_err(compr, "Failed to capture block: %d\n",
				  nwords);
			return nwords;
		}

		nbytes = nwords * CS_DSP_DATA_WORD_SIZE;

		compr_dbg(compr, "Read %d bytes\n", nbytes);

		if (copy_to_user(buf + ntotal, compr->raw_buf, nbytes)) {
			compr_err(compr, "Failed to copy data to user: %d, %d\n",
				  ntotal, nbytes);
			return -EFAULT;
		}

		count -= nwords;
		ntotal += nbytes;
	} while (nwords > 0 && count > 0);

	compr->copied_total += ntotal;

	return ntotal;
}

int wm_adsp_compr_copy(struct snd_soc_component *component,
		       struct snd_compr_stream *stream, char __user *buf,
		       size_t count)
{
	struct wm_adsp_compr *compr = stream->runtime->private_data;
	struct wm_adsp *dsp = compr->dsp;
	int ret;

	mutex_lock(&dsp->cs_dsp.pwr_lock);

	if (stream->direction == SND_COMPRESS_CAPTURE)
		ret = wm_adsp_compr_read(compr, buf, count);
	else
		ret = -ENOTSUPP;

	mutex_unlock(&dsp->cs_dsp.pwr_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(wm_adsp_compr_copy);

static void wm_adsp_fatal_error(struct cs_dsp *cs_dsp)
{
	struct wm_adsp *dsp = container_of(cs_dsp, struct wm_adsp, cs_dsp);
	struct wm_adsp_compr *compr;

	dsp->fatal_error = true;

	list_for_each_entry(compr, &dsp->compr_list, list) {
		if (compr->stream)
			snd_compr_fragment_elapsed(compr->stream);
	}
}

irqreturn_t wm_adsp2_bus_error(int irq, void *data)
{
	struct wm_adsp *dsp = (struct wm_adsp *)data;

	cs_dsp_adsp2_bus_error(&dsp->cs_dsp);

	return IRQ_HANDLED;
}
EXPORT_SYMBOL_GPL(wm_adsp2_bus_error);

irqreturn_t wm_halo_bus_error(int irq, void *data)
{
	struct wm_adsp *dsp = (struct wm_adsp *)data;

	cs_dsp_halo_bus_error(&dsp->cs_dsp);

	return IRQ_HANDLED;
}
EXPORT_SYMBOL_GPL(wm_halo_bus_error);

irqreturn_t wm_halo_wdt_expire(int irq, void *data)
{
	struct wm_adsp *dsp = data;

	cs_dsp_halo_wdt_expire(&dsp->cs_dsp);

	return IRQ_HANDLED;
}
EXPORT_SYMBOL_GPL(wm_halo_wdt_expire);

static const struct cs_dsp_client_ops wm_adsp1_client_ops = {
	.control_add = wm_adsp_control_add,
	.control_remove = wm_adsp_control_remove,
};

static const struct cs_dsp_client_ops wm_adsp2_client_ops = {
	.control_add = wm_adsp_control_add,
	.control_remove = wm_adsp_control_remove,
	.pre_run = wm_adsp_pre_run,
	.post_run = wm_adsp_event_post_run,
	.post_stop = wm_adsp_event_post_stop,
	.watchdog_expired = wm_adsp_fatal_error,
};

MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(FW_CS_DSP);
