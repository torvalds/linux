/*
 * uapi/sound/asoc.h -- ALSA SoC Firmware Controls and DAPM
 *
 * Copyright (C) 2012 Texas Instruments Inc.
 * Copyright (C) 2015 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Simple file API to load FW that includes mixers, coefficients, DAPM graphs,
 * algorithms, equalisers, DAIs, widgets etc.
*/

#ifndef __LINUX_UAPI_SND_ASOC_H
#define __LINUX_UAPI_SND_ASOC_H

#include <linux/types.h>
#include <sound/asound.h>

/*
 * Maximum number of channels topology kcontrol can represent.
 */
#define SND_SOC_TPLG_MAX_CHAN		8

/*
 * Maximum number of PCM formats capability
 */
#define SND_SOC_TPLG_MAX_FORMATS	16

/*
 * Maximum number of PCM stream configs
 */
#define SND_SOC_TPLG_STREAM_CONFIG_MAX  8

/* individual kcontrol info types - can be mixed with other types */
#define SND_SOC_TPLG_CTL_VOLSW		1
#define SND_SOC_TPLG_CTL_VOLSW_SX	2
#define SND_SOC_TPLG_CTL_VOLSW_XR_SX	3
#define SND_SOC_TPLG_CTL_ENUM		4
#define SND_SOC_TPLG_CTL_BYTES		5
#define SND_SOC_TPLG_CTL_ENUM_VALUE	6
#define SND_SOC_TPLG_CTL_RANGE		7
#define SND_SOC_TPLG_CTL_STROBE		8


/* individual widget kcontrol info types - can be mixed with other types */
#define SND_SOC_TPLG_DAPM_CTL_VOLSW		64
#define SND_SOC_TPLG_DAPM_CTL_ENUM_DOUBLE	65
#define SND_SOC_TPLG_DAPM_CTL_ENUM_VIRT		66
#define SND_SOC_TPLG_DAPM_CTL_ENUM_VALUE	67
#define SND_SOC_TPLG_DAPM_CTL_PIN		68

/* DAPM widget types - add new items to the end */
#define SND_SOC_TPLG_DAPM_INPUT		0
#define SND_SOC_TPLG_DAPM_OUTPUT	1
#define SND_SOC_TPLG_DAPM_MUX		2
#define SND_SOC_TPLG_DAPM_MIXER		3
#define SND_SOC_TPLG_DAPM_PGA		4
#define SND_SOC_TPLG_DAPM_OUT_DRV	5
#define SND_SOC_TPLG_DAPM_ADC		6
#define SND_SOC_TPLG_DAPM_DAC		7
#define SND_SOC_TPLG_DAPM_SWITCH	8
#define SND_SOC_TPLG_DAPM_PRE		9
#define SND_SOC_TPLG_DAPM_POST		10
#define SND_SOC_TPLG_DAPM_AIF_IN	11
#define SND_SOC_TPLG_DAPM_AIF_OUT	12
#define SND_SOC_TPLG_DAPM_DAI_IN	13
#define SND_SOC_TPLG_DAPM_DAI_OUT	14
#define SND_SOC_TPLG_DAPM_DAI_LINK	15
#define SND_SOC_TPLG_DAPM_LAST		SND_SOC_TPLG_DAPM_DAI_LINK

/* Header magic number and string sizes */
#define SND_SOC_TPLG_MAGIC		0x41536F43 /* ASoC */

/* string sizes */
#define SND_SOC_TPLG_NUM_TEXTS		16

/* ABI version */
#define SND_SOC_TPLG_ABI_VERSION	0x3

/* Max size of TLV data */
#define SND_SOC_TPLG_TLV_SIZE		32

/*
 * File and Block header data types.
 * Add new generic and vendor types to end of list.
 * Generic types are handled by the core whilst vendors types are passed
 * to the component drivers for handling.
 */
#define SND_SOC_TPLG_TYPE_MIXER		1
#define SND_SOC_TPLG_TYPE_BYTES		2
#define SND_SOC_TPLG_TYPE_ENUM		3
#define SND_SOC_TPLG_TYPE_DAPM_GRAPH	4
#define SND_SOC_TPLG_TYPE_DAPM_WIDGET	5
#define SND_SOC_TPLG_TYPE_DAI_LINK	6
#define SND_SOC_TPLG_TYPE_PCM		7
#define SND_SOC_TPLG_TYPE_MANIFEST	8
#define SND_SOC_TPLG_TYPE_CODEC_LINK	9
#define SND_SOC_TPLG_TYPE_PDATA		10
#define SND_SOC_TPLG_TYPE_MAX	SND_SOC_TPLG_TYPE_PDATA

/* vendor block IDs - please add new vendor types to end */
#define SND_SOC_TPLG_TYPE_VENDOR_FW	1000
#define SND_SOC_TPLG_TYPE_VENDOR_CONFIG	1001
#define SND_SOC_TPLG_TYPE_VENDOR_COEFF	1002
#define SND_SOC_TPLG_TYPEVENDOR_CODEC	1003

#define SND_SOC_TPLG_STREAM_PLAYBACK	0
#define SND_SOC_TPLG_STREAM_CAPTURE	1

/*
 * Block Header.
 * This header precedes all object and object arrays below.
 */
struct snd_soc_tplg_hdr {
	__le32 magic;		/* magic number */
	__le32 abi;		/* ABI version */
	__le32 version;		/* optional vendor specific version details */
	__le32 type;		/* SND_SOC_TPLG_TYPE_ */
	__le32 size;		/* size of this structure */
	__le32 vendor_type;	/* optional vendor specific type info */
	__le32 payload_size;	/* data bytes, excluding this header */
	__le32 index;		/* identifier for block */
	__le32 count;		/* number of elements in block */
} __attribute__((packed));

/*
 * Private data.
 * All topology objects may have private data that can be used by the driver or
 * firmware. Core will ignore this data.
 */
struct snd_soc_tplg_private {
	__le32 size;	/* in bytes of private data */
	char data[0];
} __attribute__((packed));

/*
 * Kcontrol TLV data.
 */
struct snd_soc_tplg_tlv_dbscale {
	__le32 min;
	__le32 step;
	__le32 mute;
} __attribute__((packed));

struct snd_soc_tplg_ctl_tlv {
	__le32 size;	/* in bytes of this structure */
	__le32 type;	/* SNDRV_CTL_TLVT_*, type of TLV */
	union {
		__le32 data[SND_SOC_TPLG_TLV_SIZE];
		struct snd_soc_tplg_tlv_dbscale scale;
	};
} __attribute__((packed));

/*
 * Kcontrol channel data
 */
struct snd_soc_tplg_channel {
	__le32 size;	/* in bytes of this structure */
	__le32 reg;
	__le32 shift;
	__le32 id;	/* ID maps to Left, Right, LFE etc */
} __attribute__((packed));

/*
 * Genericl Operations IDs, for binding Kcontrol or Bytes ext ops
 * Kcontrol ops need get/put/info.
 * Bytes ext ops need get/put.
 */
struct snd_soc_tplg_io_ops {
	__le32 get;
	__le32 put;
	__le32 info;
} __attribute__((packed));

/*
 * kcontrol header
 */
struct snd_soc_tplg_ctl_hdr {
	__le32 size;	/* in bytes of this structure */
	__le32 type;
	char name[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
	__le32 access;
	struct snd_soc_tplg_io_ops ops;
	struct snd_soc_tplg_ctl_tlv tlv;
} __attribute__((packed));

/*
 * Stream Capabilities
 */
struct snd_soc_tplg_stream_caps {
	__le32 size;		/* in bytes of this structure */
	char name[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
	__le64 formats[SND_SOC_TPLG_MAX_FORMATS];	/* supported formats SNDRV_PCM_FMTBIT_* */
	__le32 rates;		/* supported rates SNDRV_PCM_RATE_* */
	__le32 rate_min;	/* min rate */
	__le32 rate_max;	/* max rate */
	__le32 channels_min;	/* min channels */
	__le32 channels_max;	/* max channels */
	__le32 periods_min;	/* min number of periods */
	__le32 periods_max;	/* max number of periods */
	__le32 period_size_min;	/* min period size bytes */
	__le32 period_size_max;	/* max period size bytes */
	__le32 buffer_size_min;	/* min buffer size bytes */
	__le32 buffer_size_max;	/* max buffer size bytes */
} __attribute__((packed));

/*
 * FE or BE Stream configuration supported by SW/FW
 */
struct snd_soc_tplg_stream {
	__le32 size;		/* in bytes of this structure */
	__le64 format;		/* SNDRV_PCM_FMTBIT_* */
	__le32 rate;		/* SNDRV_PCM_RATE_* */
	__le32 period_bytes;	/* size of period in bytes */
	__le32 buffer_bytes;	/* size of buffer in bytes */
	__le32 channels;	/* channels */
	__le32 tdm_slot;	/* optional BE bitmask of supported TDM slots */
	__le32 dai_fmt;		/* SND_SOC_DAIFMT_  */
} __attribute__((packed));

/*
 * Duplex stream configuration supported by SW/FW.
 */
struct snd_soc_tplg_stream_config {
	__le32 size;		/* in bytes of this structure */
	char name[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
	struct snd_soc_tplg_stream playback;
	struct snd_soc_tplg_stream capture;
} __attribute__((packed));

/*
 * Manifest. List totals for each payload type. Not used in parsing, but will
 * be passed to the component driver before any other objects in order for any
 * global component resource allocations.
 *
 * File block representation for manifest :-
 * +-----------------------------------+----+
 * | struct snd_soc_tplg_hdr           |  1 |
 * +-----------------------------------+----+
 * | struct snd_soc_tplg_manifest      |  1 |
 * +-----------------------------------+----+
 */
struct snd_soc_tplg_manifest {
	__le32 size;		/* in bytes of this structure */
	__le32 control_elems;	/* number of control elements */
	__le32 widget_elems;	/* number of widget elements */
	__le32 graph_elems;	/* number of graph elements */
	__le32 dai_elems;	/* number of DAI elements */
	__le32 dai_link_elems;	/* number of DAI link elements */
	struct snd_soc_tplg_private priv;
} __attribute__((packed));

/*
 * Mixer kcontrol.
 *
 * File block representation for mixer kcontrol :-
 * +-----------------------------------+----+
 * | struct snd_soc_tplg_hdr           |  1 |
 * +-----------------------------------+----+
 * | struct snd_soc_tplg_mixer_control |  N |
 * +-----------------------------------+----+
 */
struct snd_soc_tplg_mixer_control {
	struct snd_soc_tplg_ctl_hdr hdr;
	__le32 size;	/* in bytes of this structure */
	__le32 min;
	__le32 max;
	__le32 platform_max;
	__le32 invert;
	__le32 num_channels;
	struct snd_soc_tplg_channel channel[SND_SOC_TPLG_MAX_CHAN];
	struct snd_soc_tplg_private priv;
} __attribute__((packed));

/*
 * Enumerated kcontrol
 *
 * File block representation for enum kcontrol :-
 * +-----------------------------------+----+
 * | struct snd_soc_tplg_hdr           |  1 |
 * +-----------------------------------+----+
 * | struct snd_soc_tplg_enum_control  |  N |
 * +-----------------------------------+----+
 */
struct snd_soc_tplg_enum_control {
	struct snd_soc_tplg_ctl_hdr hdr;
	__le32 size;	/* in bytes of this structure */
	__le32 num_channels;
	struct snd_soc_tplg_channel channel[SND_SOC_TPLG_MAX_CHAN];
	__le32 items;
	__le32 mask;
	__le32 count;
	char texts[SND_SOC_TPLG_NUM_TEXTS][SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
	__le32 values[SND_SOC_TPLG_NUM_TEXTS * SNDRV_CTL_ELEM_ID_NAME_MAXLEN / 4];
	struct snd_soc_tplg_private priv;
} __attribute__((packed));

/*
 * Bytes kcontrol
 *
 * File block representation for bytes kcontrol :-
 * +-----------------------------------+----+
 * | struct snd_soc_tplg_hdr           |  1 |
 * +-----------------------------------+----+
 * | struct snd_soc_tplg_bytes_control |  N |
 * +-----------------------------------+----+
 */
struct snd_soc_tplg_bytes_control {
	struct snd_soc_tplg_ctl_hdr hdr;
	__le32 size;	/* in bytes of this structure */
	__le32 max;
	__le32 mask;
	__le32 base;
	__le32 num_regs;
	struct snd_soc_tplg_io_ops ext_ops;
	struct snd_soc_tplg_private priv;
} __attribute__((packed));

/*
 * DAPM Graph Element
 *
 * File block representation for DAPM graph elements :-
 * +-------------------------------------+----+
 * | struct snd_soc_tplg_hdr             |  1 |
 * +-------------------------------------+----+
 * | struct snd_soc_tplg_dapm_graph_elem |  N |
 * +-------------------------------------+----+
 */
struct snd_soc_tplg_dapm_graph_elem {
	char sink[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
	char control[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
	char source[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
} __attribute__((packed));

/*
 * DAPM Widget.
 *
 * File block representation for DAPM widget :-
 * +-------------------------------------+-----+
 * | struct snd_soc_tplg_hdr             |  1  |
 * +-------------------------------------+-----+
 * | struct snd_soc_tplg_dapm_widget     |  N  |
 * +-------------------------------------+-----+
 * |   struct snd_soc_tplg_enum_control  | 0|1 |
 * |   struct snd_soc_tplg_mixer_control | 0|N |
 * +-------------------------------------+-----+
 *
 * Optional enum or mixer control can be appended to the end of each widget
 * in the block.
 */
struct snd_soc_tplg_dapm_widget {
	__le32 size;		/* in bytes of this structure */
	__le32 id;		/* SND_SOC_DAPM_CTL */
	char name[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
	char sname[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];

	__le32 reg;		/* negative reg = no direct dapm */
	__le32 shift;		/* bits to shift */
	__le32 mask;		/* non-shifted mask */
	__le32 subseq;		/* sort within widget type */
	__u32 invert;		/* invert the power bit */
	__u32 ignore_suspend;	/* kept enabled over suspend */
	__u16 event_flags;
	__u16 event_type;
	__u16 num_kcontrols;
	struct snd_soc_tplg_private priv;
	/*
	 * kcontrols that relate to this widget
	 * follow here after widget private data
	 */
} __attribute__((packed));

struct snd_soc_tplg_pcm_cfg_caps {
	struct snd_soc_tplg_stream_caps caps;
	struct snd_soc_tplg_stream_config configs[SND_SOC_TPLG_STREAM_CONFIG_MAX];
	__le32 num_configs;	/* number of configs */
} __attribute__((packed));

/*
 * Describes SW/FW specific features of PCM or DAI link.
 *
 * File block representation for PCM/DAI-Link :-
 * +-----------------------------------+-----+
 * | struct snd_soc_tplg_hdr           |  1  |
 * +-----------------------------------+-----+
 * | struct snd_soc_tplg_dapm_pcm_dai  |  N  |
 * +-----------------------------------+-----+
 */
struct snd_soc_tplg_pcm_dai {
	__le32 size;		/* in bytes of this structure */
	char name[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
	__le32 id;			/* unique ID - used to match */
	__le32 playback;		/* supports playback mode */
	__le32 capture;			/* supports capture mode */
	__le32 compress;		/* 1 = compressed; 0 = PCM */
	struct snd_soc_tplg_pcm_cfg_caps capconf[2];	/* capabilities and configs */
} __attribute__((packed));

#endif
