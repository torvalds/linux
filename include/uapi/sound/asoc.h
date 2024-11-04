/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * uapi/sound/asoc.h -- ALSA SoC Firmware Controls and DAPM
 *
 * Copyright (C) 2012 Texas Instruments Inc.
 * Copyright (C) 2015 Intel Corporation.
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

/*
 * Maximum number of physical link's hardware configs
 */
#define SND_SOC_TPLG_HW_CONFIG_MAX	8

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
#define SND_SOC_TPLG_DAPM_BUFFER	16
#define SND_SOC_TPLG_DAPM_SCHEDULER	17
#define SND_SOC_TPLG_DAPM_EFFECT	18
#define SND_SOC_TPLG_DAPM_SIGGEN	19
#define SND_SOC_TPLG_DAPM_SRC		20
#define SND_SOC_TPLG_DAPM_ASRC		21
#define SND_SOC_TPLG_DAPM_ENCODER	22
#define SND_SOC_TPLG_DAPM_DECODER	23
#define SND_SOC_TPLG_DAPM_LAST		SND_SOC_TPLG_DAPM_DECODER

/* Header magic number and string sizes */
#define SND_SOC_TPLG_MAGIC		0x41536F43 /* ASoC */

/* string sizes */
#define SND_SOC_TPLG_NUM_TEXTS		16

/* ABI version */
#define SND_SOC_TPLG_ABI_VERSION	0x5	/* current version */
#define SND_SOC_TPLG_ABI_VERSION_MIN	0x5	/* oldest version supported */

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
#define SND_SOC_TPLG_TYPE_BACKEND_LINK	10
#define SND_SOC_TPLG_TYPE_PDATA		11
#define SND_SOC_TPLG_TYPE_DAI		12
#define SND_SOC_TPLG_TYPE_MAX		SND_SOC_TPLG_TYPE_DAI

/* vendor block IDs - please add new vendor types to end */
#define SND_SOC_TPLG_TYPE_VENDOR_FW	1000
#define SND_SOC_TPLG_TYPE_VENDOR_CONFIG	1001
#define SND_SOC_TPLG_TYPE_VENDOR_COEFF	1002
#define SND_SOC_TPLG_TYPEVENDOR_CODEC	1003

#define SND_SOC_TPLG_STREAM_PLAYBACK	0
#define SND_SOC_TPLG_STREAM_CAPTURE	1

/* vendor tuple types */
#define SND_SOC_TPLG_TUPLE_TYPE_UUID	0
#define SND_SOC_TPLG_TUPLE_TYPE_STRING	1
#define SND_SOC_TPLG_TUPLE_TYPE_BOOL	2
#define SND_SOC_TPLG_TUPLE_TYPE_BYTE	3
#define SND_SOC_TPLG_TUPLE_TYPE_WORD	4
#define SND_SOC_TPLG_TUPLE_TYPE_SHORT	5

/* DAI flags */
#define SND_SOC_TPLG_DAI_FLGBIT_SYMMETRIC_RATES         (1 << 0)
#define SND_SOC_TPLG_DAI_FLGBIT_SYMMETRIC_CHANNELS      (1 << 1)
#define SND_SOC_TPLG_DAI_FLGBIT_SYMMETRIC_SAMPLEBITS    (1 << 2)

/* DAI clock gating */
#define SND_SOC_TPLG_DAI_CLK_GATE_UNDEFINED	0
#define SND_SOC_TPLG_DAI_CLK_GATE_GATED	1
#define SND_SOC_TPLG_DAI_CLK_GATE_CONT		2

/* DAI mclk_direction */
#define SND_SOC_TPLG_MCLK_CO            0 /* for codec, mclk is output */
#define SND_SOC_TPLG_MCLK_CI            1 /* for codec, mclk is input */

/* DAI physical PCM data formats.
 * Add new formats to the end of the list.
 */
#define SND_SOC_DAI_FORMAT_I2S          1 /* I2S mode */
#define SND_SOC_DAI_FORMAT_RIGHT_J      2 /* Right Justified mode */
#define SND_SOC_DAI_FORMAT_LEFT_J       3 /* Left Justified mode */
#define SND_SOC_DAI_FORMAT_DSP_A        4 /* L data MSB after FRM LRC */
#define SND_SOC_DAI_FORMAT_DSP_B        5 /* L data MSB during FRM LRC */
#define SND_SOC_DAI_FORMAT_AC97         6 /* AC97 */
#define SND_SOC_DAI_FORMAT_PDM          7 /* Pulse density modulation */

/* left and right justified also known as MSB and LSB respectively */
#define SND_SOC_DAI_FORMAT_MSB          SND_SOC_DAI_FORMAT_LEFT_J
#define SND_SOC_DAI_FORMAT_LSB          SND_SOC_DAI_FORMAT_RIGHT_J

/* DAI link flags */
#define SND_SOC_TPLG_LNK_FLGBIT_SYMMETRIC_RATES         (1 << 0)
#define SND_SOC_TPLG_LNK_FLGBIT_SYMMETRIC_CHANNELS      (1 << 1)
#define SND_SOC_TPLG_LNK_FLGBIT_SYMMETRIC_SAMPLEBITS    (1 << 2)
#define SND_SOC_TPLG_LNK_FLGBIT_VOICE_WAKEUP            (1 << 3)

/* DAI topology BCLK parameter
 * For the backwards capability, by default codec is bclk provider
 */
#define SND_SOC_TPLG_BCLK_CP         0 /* codec is bclk provider */
#define SND_SOC_TPLG_BCLK_CC         1 /* codec is bclk consumer */
/* keep previous definitions for compatibility */
#define SND_SOC_TPLG_BCLK_CM         SND_SOC_TPLG_BCLK_CP
#define SND_SOC_TPLG_BCLK_CS         SND_SOC_TPLG_BCLK_CC

/* DAI topology FSYNC parameter
 * For the backwards capability, by default codec is fsync provider
 */
#define SND_SOC_TPLG_FSYNC_CP         0 /* codec is fsync provider */
#define SND_SOC_TPLG_FSYNC_CC         1 /* codec is fsync consumer */
/* keep previous definitions for compatibility */
#define SND_SOC_TPLG_FSYNC_CM         SND_SOC_TPLG_FSYNC_CP
#define SND_SOC_TPLG_FSYNC_CS         SND_SOC_TPLG_FSYNC_CC

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

/* vendor tuple for uuid */
struct snd_soc_tplg_vendor_uuid_elem {
	__le32 token;
	char uuid[16];
} __attribute__((packed));

/* vendor tuple for a bool/byte/short/word value */
struct snd_soc_tplg_vendor_value_elem {
	__le32 token;
	__le32 value;
} __attribute__((packed));

/* vendor tuple for string */
struct snd_soc_tplg_vendor_string_elem {
	__le32 token;
	char string[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
} __attribute__((packed));

struct snd_soc_tplg_vendor_array {
	__le32 size;	/* size in bytes of the array, including all elements */
	__le32 type;	/* SND_SOC_TPLG_TUPLE_TYPE_ */
	__le32 num_elems;	/* number of elements in array */
	union {
		__DECLARE_FLEX_ARRAY(struct snd_soc_tplg_vendor_uuid_elem, uuid);
		__DECLARE_FLEX_ARRAY(struct snd_soc_tplg_vendor_value_elem, value);
		__DECLARE_FLEX_ARRAY(struct snd_soc_tplg_vendor_string_elem, string);
	};
} __attribute__((packed));

/*
 * Private data.
 * All topology objects may have private data that can be used by the driver or
 * firmware. Core will ignore this data.
 */
struct snd_soc_tplg_private {
	__le32 size;	/* in bytes of private data */
	union {
		__DECLARE_FLEX_ARRAY(char, data);
		__DECLARE_FLEX_ARRAY(struct snd_soc_tplg_vendor_array, array);
	};
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
	__le64 formats;	/* supported formats SNDRV_PCM_FMTBIT_* */
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
	__le32 sig_bits;        /* number of bits of content */
} __attribute__((packed));

/*
 * FE or BE Stream configuration supported by SW/FW
 */
struct snd_soc_tplg_stream {
	__le32 size;		/* in bytes of this structure */
	char name[SNDRV_CTL_ELEM_ID_NAME_MAXLEN]; /* Name of the stream */
	__le64 format;		/* SNDRV_PCM_FMTBIT_* */
	__le32 rate;		/* SNDRV_PCM_RATE_* */
	__le32 period_bytes;	/* size of period in bytes */
	__le32 buffer_bytes;	/* size of buffer in bytes */
	__le32 channels;	/* channels */
} __attribute__((packed));


/*
 * Describes a physical link's runtime supported hardware config,
 * i.e. hardware audio formats.
 */
struct snd_soc_tplg_hw_config {
	__le32 size;            /* in bytes of this structure */
	__le32 id;		/* unique ID - - used to match */
	__le32 fmt;		/* SND_SOC_DAI_FORMAT_ format value */
	__u8 clock_gated;	/* SND_SOC_TPLG_DAI_CLK_GATE_ value */
	__u8 invert_bclk;	/* 1 for inverted BCLK, 0 for normal */
	__u8 invert_fsync;	/* 1 for inverted frame clock, 0 for normal */
	__u8 bclk_provider;	/* SND_SOC_TPLG_BCLK_ value */
	__u8 fsync_provider;	/* SND_SOC_TPLG_FSYNC_ value */
	__u8 mclk_direction;    /* SND_SOC_TPLG_MCLK_ value */
	__le16 reserved;	/* for 32bit alignment */
	__le32 mclk_rate;	/* MCLK or SYSCLK freqency in Hz */
	__le32 bclk_rate;	/* BCLK freqency in Hz */
	__le32 fsync_rate;	/* frame clock in Hz */
	__le32 tdm_slots;	/* number of TDM slots in use */
	__le32 tdm_slot_width;	/* width in bits for each slot */
	__le32 tx_slots;	/* bit mask for active Tx slots */
	__le32 rx_slots;	/* bit mask for active Rx slots */
	__le32 tx_channels;	/* number of Tx channels */
	__le32 tx_chanmap[SND_SOC_TPLG_MAX_CHAN]; /* array of slot number */
	__le32 rx_channels;	/* number of Rx channels */
	__le32 rx_chanmap[SND_SOC_TPLG_MAX_CHAN]; /* array of slot number */
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
	__le32 pcm_elems;	/* number of PCM elements */
	__le32 dai_link_elems;	/* number of DAI link elements */
	__le32 dai_elems;	/* number of physical DAI elements */
	__le32 reserved[20];	/* reserved for new ABI element types */
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
	__le32 invert;		/* invert the power bit */
	__le32 ignore_suspend;	/* kept enabled over suspend */
	__le16 event_flags;
	__le16 event_type;
	__le32 num_kcontrols;
	struct snd_soc_tplg_private priv;
	/*
	 * kcontrols that relate to this widget
	 * follow here after widget private data
	 */
} __attribute__((packed));


/*
 * Describes SW/FW specific features of PCM (FE DAI & DAI link).
 *
 * File block representation for PCM :-
 * +-----------------------------------+-----+
 * | struct snd_soc_tplg_hdr           |  1  |
 * +-----------------------------------+-----+
 * | struct snd_soc_tplg_pcm           |  N  |
 * +-----------------------------------+-----+
 */
struct snd_soc_tplg_pcm {
	__le32 size;		/* in bytes of this structure */
	char pcm_name[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
	char dai_name[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
	__le32 pcm_id;		/* unique ID - used to match with DAI link */
	__le32 dai_id;		/* unique ID - used to match */
	__le32 playback;	/* supports playback mode */
	__le32 capture;		/* supports capture mode */
	__le32 compress;	/* 1 = compressed; 0 = PCM */
	struct snd_soc_tplg_stream stream[SND_SOC_TPLG_STREAM_CONFIG_MAX]; /* for DAI link */
	__le32 num_streams;	/* number of streams */
	struct snd_soc_tplg_stream_caps caps[2]; /* playback and capture for DAI */
	__le32 flag_mask;       /* bitmask of flags to configure */
	__le32 flags;           /* SND_SOC_TPLG_LNK_FLGBIT_* flag value */
	struct snd_soc_tplg_private priv;
} __attribute__((packed));


/*
 * Describes the physical link runtime supported configs or params
 *
 * File block representation for physical link config :-
 * +-----------------------------------+-----+
 * | struct snd_soc_tplg_hdr           |  1  |
 * +-----------------------------------+-----+
 * | struct snd_soc_tplg_link_config   |  N  |
 * +-----------------------------------+-----+
 */
struct snd_soc_tplg_link_config {
	__le32 size;            /* in bytes of this structure */
	__le32 id;              /* unique ID - used to match */
	char name[SNDRV_CTL_ELEM_ID_NAME_MAXLEN]; /* name - used to match */
	char stream_name[SNDRV_CTL_ELEM_ID_NAME_MAXLEN]; /* stream name - used to match */
	struct snd_soc_tplg_stream stream[SND_SOC_TPLG_STREAM_CONFIG_MAX]; /* supported configs playback and captrure */
	__le32 num_streams;     /* number of streams */
	struct snd_soc_tplg_hw_config hw_config[SND_SOC_TPLG_HW_CONFIG_MAX]; /* hw configs */
	__le32 num_hw_configs;         /* number of hw configs */
	__le32 default_hw_config_id;   /* default hw config ID for init */
	__le32 flag_mask;       /* bitmask of flags to configure */
	__le32 flags;           /* SND_SOC_TPLG_LNK_FLGBIT_* flag value */
	struct snd_soc_tplg_private priv;
} __attribute__((packed));

/*
 * Describes SW/FW specific features of physical DAI.
 * It can be used to configure backend DAIs for DPCM.
 *
 * File block representation for physical DAI :-
 * +-----------------------------------+-----+
 * | struct snd_soc_tplg_hdr           |  1  |
 * +-----------------------------------+-----+
 * | struct snd_soc_tplg_dai           |  N  |
 * +-----------------------------------+-----+
 */
struct snd_soc_tplg_dai {
	__le32 size;            /* in bytes of this structure */
	char dai_name[SNDRV_CTL_ELEM_ID_NAME_MAXLEN]; /* name - used to match */
	__le32 dai_id;          /* unique ID - used to match */
	__le32 playback;        /* supports playback mode */
	__le32 capture;         /* supports capture mode */
	struct snd_soc_tplg_stream_caps caps[2]; /* playback and capture for DAI */
	__le32 flag_mask;       /* bitmask of flags to configure */
	__le32 flags;           /* SND_SOC_TPLG_DAI_FLGBIT_* */
	struct snd_soc_tplg_private priv;
} __attribute__((packed));

#endif
