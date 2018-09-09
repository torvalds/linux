// SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note)) OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//         Keyon Jie <yang.jie@linux.intel.com>
//

#ifndef __INCLUDE_UAPI_SOF_IPC_H__
#define __INCLUDE_UAPI_SOF_IPC_H__

#include <sound/sof-abi.h>

/*
 * IPC messages have a prefixed 32 bit identifier made up as follows :-
 *
 * 0xGCCCNNNN where
 * G is global cmd type (4 bits)
 * C is command type (12 bits)
 * I is the ID number (16 bits) - monotonic and overflows
 *
 * This is sent at the start of the IPM message in the mailbox. Messages should
 * not be sent in the doorbell (special exceptions for firmware .
 */

/* Global Message - Generic */
#define SOF_GLB_TYPE_SHIFT			28
#define SOF_GLB_TYPE_MASK			(0xf << SOF_GLB_TYPE_SHIFT)
#define SOF_GLB_TYPE(x)				((x) << SOF_GLB_TYPE_SHIFT)

/* Command Message - Generic */
#define SOF_CMD_TYPE_SHIFT			16
#define SOF_CMD_TYPE_MASK			(0xfff << SOF_CMD_TYPE_SHIFT)
#define SOF_CMD_TYPE(x)				((x) << SOF_CMD_TYPE_SHIFT)

/* Global Message Types */
#define SOF_IPC_GLB_REPLY			SOF_GLB_TYPE(0x1U)
#define SOF_IPC_GLB_COMPOUND			SOF_GLB_TYPE(0x2U)
#define SOF_IPC_GLB_TPLG_MSG			SOF_GLB_TYPE(0x3U)
#define SOF_IPC_GLB_PM_MSG			SOF_GLB_TYPE(0x4U)
#define SOF_IPC_GLB_COMP_MSG			SOF_GLB_TYPE(0x5U)
#define SOF_IPC_GLB_STREAM_MSG			SOF_GLB_TYPE(0x6U)
#define SOF_IPC_FW_READY			SOF_GLB_TYPE(0x7U)
#define SOF_IPC_GLB_DAI_MSG			SOF_GLB_TYPE(0x8U)
#define SOF_IPC_GLB_TRACE_MSG			SOF_GLB_TYPE(0x9U)

/*
 * DSP Command Message Types
 */

/* topology */
#define SOF_IPC_TPLG_COMP_NEW			SOF_CMD_TYPE(0x001)
#define SOF_IPC_TPLG_COMP_FREE			SOF_CMD_TYPE(0x002)
#define SOF_IPC_TPLG_COMP_CONNECT		SOF_CMD_TYPE(0x003)
#define SOF_IPC_TPLG_PIPE_NEW			SOF_CMD_TYPE(0x010)
#define SOF_IPC_TPLG_PIPE_FREE			SOF_CMD_TYPE(0x011)
#define SOF_IPC_TPLG_PIPE_CONNECT		SOF_CMD_TYPE(0x012)
#define SOF_IPC_TPLG_PIPE_COMPLETE		SOF_CMD_TYPE(0x013)
#define SOF_IPC_TPLG_BUFFER_NEW			SOF_CMD_TYPE(0x020)
#define SOF_IPC_TPLG_BUFFER_FREE		SOF_CMD_TYPE(0x021)

/* PM */
#define SOF_IPC_PM_CTX_SAVE			SOF_CMD_TYPE(0x001)
#define SOF_IPC_PM_CTX_RESTORE			SOF_CMD_TYPE(0x002)
#define SOF_IPC_PM_CTX_SIZE			SOF_CMD_TYPE(0x003)
#define SOF_IPC_PM_CLK_SET			SOF_CMD_TYPE(0x004)
#define SOF_IPC_PM_CLK_GET			SOF_CMD_TYPE(0x005)
#define SOF_IPC_PM_CLK_REQ			SOF_CMD_TYPE(0x006)

/* component runtime config - multiple different types */
#define SOF_IPC_COMP_SET_VALUE			SOF_CMD_TYPE(0x001)
#define SOF_IPC_COMP_GET_VALUE			SOF_CMD_TYPE(0x002)
#define SOF_IPC_COMP_SET_DATA			SOF_CMD_TYPE(0x003)
#define SOF_IPC_COMP_GET_DATA			SOF_CMD_TYPE(0x004)

/* DAI messages */
#define SOF_IPC_DAI_CONFIG			SOF_CMD_TYPE(0x001)
#define SOF_IPC_DAI_LOOPBACK			SOF_CMD_TYPE(0x002)

/* stream */
#define SOF_IPC_STREAM_PCM_PARAMS		SOF_CMD_TYPE(0x001)
#define SOF_IPC_STREAM_PCM_PARAMS_REPLY		SOF_CMD_TYPE(0x002)
#define SOF_IPC_STREAM_PCM_FREE			SOF_CMD_TYPE(0x003)
#define SOF_IPC_STREAM_TRIG_START		SOF_CMD_TYPE(0x004)
#define SOF_IPC_STREAM_TRIG_STOP		SOF_CMD_TYPE(0x005)
#define SOF_IPC_STREAM_TRIG_PAUSE		SOF_CMD_TYPE(0x006)
#define SOF_IPC_STREAM_TRIG_RELEASE		SOF_CMD_TYPE(0x007)
#define SOF_IPC_STREAM_TRIG_DRAIN		SOF_CMD_TYPE(0x008)
#define SOF_IPC_STREAM_TRIG_XRUN		SOF_CMD_TYPE(0x009)
#define SOF_IPC_STREAM_POSITION			SOF_CMD_TYPE(0x00a)
#define SOF_IPC_STREAM_VORBIS_PARAMS		SOF_CMD_TYPE(0x010)
#define SOF_IPC_STREAM_VORBIS_FREE		SOF_CMD_TYPE(0x011)

/* trace and debug */
#define SOF_IPC_TRACE_DMA_PARAMS		SOF_CMD_TYPE(0x001)
#define SOF_IPC_TRACE_DMA_POSITION		SOF_CMD_TYPE(0x002)

/* Get message component id */
#define SOF_IPC_MESSAGE_ID(x)			((x) & 0xffff)

/* maximum message size for mailbox Tx/Rx */
#define SOF_IPC_MSG_MAX_SIZE			384

/*
 * SOF panic codes
 */
#define SOF_IPC_PANIC_MAGIC			0x0dead000
#define SOF_IPC_PANIC_MAGIC_MASK		0x0ffff000
#define SOF_IPC_PANIC_CODE_MASK			0x00000fff
#define SOF_IPC_PANIC_MEM			(SOF_IPC_PANIC_MAGIC | 0x0)
#define SOF_IPC_PANIC_WORK			(SOF_IPC_PANIC_MAGIC | 0x1)
#define SOF_IPC_PANIC_IPC			(SOF_IPC_PANIC_MAGIC | 0x2)
#define SOF_IPC_PANIC_ARCH			(SOF_IPC_PANIC_MAGIC | 0x3)
#define SOF_IPC_PANIC_PLATFORM			(SOF_IPC_PANIC_MAGIC | 0x4)
#define SOF_IPC_PANIC_TASK			(SOF_IPC_PANIC_MAGIC | 0x5)
#define SOF_IPC_PANIC_EXCEPTION			(SOF_IPC_PANIC_MAGIC | 0x6)
#define SOF_IPC_PANIC_DEADLOCK			(SOF_IPC_PANIC_MAGIC | 0x7)
#define SOF_IPC_PANIC_STACK			(SOF_IPC_PANIC_MAGIC | 0x8)
#define SOF_IPC_PANIC_IDLE			(SOF_IPC_PANIC_MAGIC | 0x9)
#define SOF_IPC_PANIC_WFI			(SOF_IPC_PANIC_MAGIC | 0xa)

/*
 * SOF memory capabilities, add new ones at the end
 */
#define SOF_MEM_CAPS_RAM			(1 << 0)
#define SOF_MEM_CAPS_ROM			(1 << 1)
#define SOF_MEM_CAPS_EXT			(1 << 2) /* external */
#define SOF_MEM_CAPS_LP			(1 << 3) /* low power */
#define SOF_MEM_CAPS_HP			(1 << 4) /* high performance */
#define SOF_MEM_CAPS_DMA			(1 << 5) /* DMA'able */
#define SOF_MEM_CAPS_CACHE			(1 << 6) /* cacheable */
#define SOF_MEM_CAPS_EXEC			(1 << 7) /* executable */

/*
 * Command Header - Header for all IPC. Identifies IPC message.
 * The size can be greater than the structure size and that means there is
 * extended bespoke data beyond the end of the structure including variable
 * arrays.
 */

struct sof_ipc_hdr {
	uint32_t cmd;			/* SOF_IPC_GLB_ + cmd */
	uint32_t size;			/* size of structure */
}  __attribute__((packed));

/*
 * Generic reply message. Some commands override this with their own reply
 * types that must include this at start.
 */
struct sof_ipc_reply {
	struct sof_ipc_hdr hdr;
	int32_t error;			/* negative error numbers */
}  __attribute__((packed));

/*
 * Compound commands - SOF_IPC_GLB_COMPOUND.
 *
 * Compound commands are sent to the DSP as a single IPC operation. The
 * commands are split into blocks and each block has a header. This header
 * identifies the command type and the number of commands before the next
 * header.
 */

struct sof_ipc_compound_hdr {
	struct sof_ipc_hdr hdr;
	uint32_t count;		/* count of 0 means end of compound sequence */
}  __attribute__((packed));

/*
 * DAI Configuration.
 *
 * Each different DAI type will have it's own structure and IPC cmd.
 */

#define SOF_DAI_FMT_I2S		1 /* I2S mode */
#define SOF_DAI_FMT_RIGHT_J	2 /* Right Justified mode */
#define SOF_DAI_FMT_LEFT_J	3 /* Left Justified mode */
#define SOF_DAI_FMT_DSP_A	4 /* L data MSB after FRM LRC */
#define SOF_DAI_FMT_DSP_B	5 /* L data MSB during FRM LRC */
#define SOF_DAI_FMT_PDM		6 /* Pulse density modulation */

#define SOF_DAI_FMT_CONT	(1 << 4) /* continuous clock */
#define SOF_DAI_FMT_GATED	(0 << 4) /* clock is gated */

#define SOF_DAI_FMT_NB_NF	(0 << 8) /* normal bit clock + frame */
#define SOF_DAI_FMT_NB_IF	(2 << 8) /* normal BCLK + inv FRM */
#define SOF_DAI_FMT_IB_NF	(3 << 8) /* invert BCLK + nor FRM */
#define SOF_DAI_FMT_IB_IF	(4 << 8) /* invert BCLK + FRM */

#define SOF_DAI_FMT_CBM_CFM	(0 << 12) /* codec clk & FRM master */
#define SOF_DAI_FMT_CBS_CFM	(2 << 12) /* codec clk slave & FRM master */
#define SOF_DAI_FMT_CBM_CFS	(3 << 12) /* codec clk master & frame slave */
#define SOF_DAI_FMT_CBS_CFS	(4 << 12) /* codec clk & FRM slave */

#define SOF_DAI_FMT_FORMAT_MASK		0x000f
#define SOF_DAI_FMT_CLOCK_MASK		0x00f0
#define SOF_DAI_FMT_INV_MASK		0x0f00
#define SOF_DAI_FMT_MASTER_MASK		0xf000

 /* ssc1: TINTE */
#define SOF_DAI_INTEL_SSP_QUIRK_TINTE		(1 << 0)
 /* ssc1: PINTE */
#define SOF_DAI_INTEL_SSP_QUIRK_PINTE		(1 << 1)
 /* ssc2: SMTATF */
#define SOF_DAI_INTEL_SSP_QUIRK_SMTATF		(1 << 2)
 /* ssc2: MMRATF */
#define SOF_DAI_INTEL_SSP_QUIRK_MMRATF		(1 << 3)
 /* ssc2: PSPSTWFDFD */
#define SOF_DAI_INTEL_SSP_QUIRK_PSPSTWFDFD	(1 << 4)
 /* ssc2: PSPSRWFDFD */
#define SOF_DAI_INTEL_SSP_QUIRK_PSPSRWFDFD	(1 << 5)
 /* here is the possibility to define others aux macros */

#define SOF_DAI_INTEL_SSP_FRAME_PULSE_WIDTH_MAX		38
#define SOF_DAI_INTEL_SSP_SLOT_PADDING_MAX		31

/** \brief Types of DAI */
enum sof_ipc_dai_type {
	SOF_DAI_INTEL_NONE = 0,	/**< None */
	SOF_DAI_INTEL_SSP,		/**< Intel SSP */
	SOF_DAI_INTEL_DMIC,		/**< Intel DMIC */
	SOF_DAI_INTEL_HDA,		/**< Intel HD/A */
};

/* SSP Configuration Request - SOF_IPC_DAI_SSP_CONFIG */
struct sof_ipc_dai_ssp_params {
	uint16_t mode;   // FIXME: do we need this?
	uint16_t mclk_id;

	uint32_t mclk_rate;	/* mclk frequency in Hz */
	uint32_t fsync_rate;	/* fsync frequency in Hz */
	uint32_t bclk_rate;	/* bclk frequency in Hz */

	/* TDM */
	uint32_t tdm_slots;
	uint32_t rx_slots;
	uint32_t tx_slots;

	/* data */
	uint32_t sample_valid_bits;
	uint16_t tdm_slot_width;
	uint16_t reserved2;	/* alignment */

	/* MCLK */
	uint32_t mclk_direction;
	uint32_t mclk_keep_active;
	uint32_t bclk_keep_active;
	uint32_t fs_keep_active;

	uint16_t frame_pulse_width;
	uint32_t quirks; // FIXME: is 32 bits enough ?

	uint16_t tdm_per_slot_padding_flag;
	/* private data, e.g. for quirks */
	//uint32_t pdata[10]; // FIXME: would really need ~16 u32
} __attribute__((packed));

/* HDA Configuration Request - SOF_IPC_DAI_HDA_CONFIG */
struct sof_ipc_dai_hda_params {
	struct sof_ipc_hdr hdr;
	/* TODO */
} __attribute__((packed));

/* DMIC Configuration Request - SOF_IPC_DAI_DMIC_CONFIG */

/* This struct is defined per 2ch PDM controller available in the platform.
 * Normally it is sufficient to set the used microphone specific enables to 1
 * and keep other parameters as zero. The customizations are:
 *
 * 1. If a device mixes different microphones types with different polarity
 * and/or the absolute polarity matters the PCM signal from a microphone
 * can be inverted with the controls.
 *
 * 2. If the microphones in a stereo pair do not appear in captured stream
 * in desired order due to board schematics choises they can be swapped with
 * the clk_edge parameter.
 *
 * 3. If PDM bit errors are seen in capture (poor quality) the skew parameter
 * that delays the sampling time of data by half cycles of DMIC source clock
 * can be tried for improvement. However there is no guarantee for this to fix
 * data integrity problems.
 */
struct sof_ipc_dai_dmic_pdm_ctrl {
	uint16_t id; /* PDM controller ID */
	uint16_t enable_mic_a; /* Use A (left) channel mic (0 or 1)*/
	uint16_t enable_mic_b; /* Use B (right) channel mic (0 or 1)*/
	uint16_t polarity_mic_a; /* Optionally invert mic A signal (0 or 1) */
	uint16_t polarity_mic_b; /* Optionally invert mic B signal (0 or 1) */
	uint16_t clk_edge; /* Optionally swap data clock edge (0 or 1) */
	uint16_t skew; /* Adjust PDM data sampling vs. clock (0..15) */
	uint16_t pad; /* Make sure the total size is 4 bytes aligned */
} __attribute__((packed));

/* This struct contains the global settings for all 2ch PDM controllers. The
 * version number used in configuration data is checked vs. version used by
 * device driver src/drivers/dmic.c need to match. It is incremented from
 * initial value 1 if updates done for the to driver would alter the operation
 * of the microhone.
 *
 * Note: The microphone clock (pdmclk_min, pdmclk_max, duty_min, duty_max)
 * parameters need to be set as defined in microphone data sheet. E.g. clock
 * range 1.0 - 3.2 MHz is usually supported microphones. Some microphones are
 * multi-mode capable and there may be denied mic clock frequencies between
 * the modes. In such case set the clock range limits of the desired mode to
 * avoid the driver to set clock to an illegal rate.
 *
 * The duty cycle could be set to 48-52% if not known. Generally these
 * parameters can be altered within data sheet specified limits to match
 * required audio application performance power.
 *
 * The microphone clock needs to be usually about 50-80 times the used audio
 * sample rate. With highest sample rates above 48 kHz this can relaxed
 * somewhat.
 */
struct sof_ipc_dai_dmic_params {
	uint32_t driver_ipc_version; /* Version (1..N) */
	uint32_t pdmclk_min; /* Minimum microphone clock in Hz (100000..N) */
	uint32_t pdmclk_max; /* Maximum microphone clock in Hz (min...N) */
	uint32_t fifo_fs_a;  /* FIFO A sample rate in Hz (8000..96000) */
	uint32_t fifo_fs_b;  /* FIFO B sample rate in Hz (8000..96000) */
	uint16_t fifo_bits_a; /* FIFO A word length (16 or 32) */
	uint16_t fifo_bits_b; /* FIFO B word length (16 or 32) */
	uint16_t duty_min;    /* Min. mic clock duty cycle in % (20..80) */
	uint16_t duty_max;    /* Max. mic clock duty cycle in % (min..80) */
	uint32_t num_pdm_active; /* Number of active pdm controllers */
	/* variable number of pdm controller config */
	struct sof_ipc_dai_dmic_pdm_ctrl pdm[0];
} __attribute__((packed));

/* general purpose DAI configuration */
struct sof_ipc_dai_config {
	struct sof_ipc_hdr hdr;
	enum sof_ipc_dai_type type;
	uint32_t dai_index; /* index of this type dai */

	/* physical protocol and clocking */
	uint16_t format;	/* SOF_DAI_FMT_ */
	uint16_t reserved;	/* alignment */

	/* HW specific data */
	union {
		struct sof_ipc_dai_ssp_params ssp;
		struct sof_ipc_dai_hda_params hda;
		struct sof_ipc_dai_dmic_params dmic;
	};
};

/*
 * Stream configuration.
 */

#define SOF_IPC_MAX_CHANNELS			8

/* channel positions - uses same values as ALSA */
enum sof_ipc_chmap {
	SOF_CHMAP_UNKNOWN = 0,
	SOF_CHMAP_NA,		/* N/A, silent */
	SOF_CHMAP_MONO,		/* mono stream */
	SOF_CHMAP_FL,		/* front left */
	SOF_CHMAP_FR,		/* front right */
	SOF_CHMAP_RL,		/* rear left */
	SOF_CHMAP_RR,		/* rear right */
	SOF_CHMAP_FC,		/* front centre */
	SOF_CHMAP_LFE,		/* LFE */
	SOF_CHMAP_SL,		/* side left */
	SOF_CHMAP_SR,		/* side right */
	SOF_CHMAP_RC,		/* rear centre */
	SOF_CHMAP_FLC,		/* front left centre */
	SOF_CHMAP_FRC,		/* front right centre */
	SOF_CHMAP_RLC,		/* rear left centre */
	SOF_CHMAP_RRC,		/* rear right centre */
	SOF_CHMAP_FLW,		/* front left wide */
	SOF_CHMAP_FRW,		/* front right wide */
	SOF_CHMAP_FLH,		/* front left high */
	SOF_CHMAP_FCH,		/* front centre high */
	SOF_CHMAP_FRH,		/* front right high */
	SOF_CHMAP_TC,		/* top centre */
	SOF_CHMAP_TFL,		/* top front left */
	SOF_CHMAP_TFR,		/* top front right */
	SOF_CHMAP_TFC,		/* top front centre */
	SOF_CHMAP_TRL,		/* top rear left */
	SOF_CHMAP_TRR,		/* top rear right */
	SOF_CHMAP_TRC,		/* top rear centre */
	SOF_CHMAP_TFLC,		/* top front left centre */
	SOF_CHMAP_TFRC,		/* top front right centre */
	SOF_CHMAP_TSL,		/* top side left */
	SOF_CHMAP_TSR,		/* top side right */
	SOF_CHMAP_LLFE,		/* left LFE */
	SOF_CHMAP_RLFE,		/* right LFE */
	SOF_CHMAP_BC,		/* bottom centre */
	SOF_CHMAP_BLC,		/* bottom left centre */
	SOF_CHMAP_BRC,		/* bottom right centre */
	SOF_CHMAP_LAST = SOF_CHMAP_BRC,
};

/* common sample rates for use in masks */
#define SOF_RATE_8000		(1 <<  0) /* 8000Hz  */
#define SOF_RATE_11025		(1 <<  1) /* 11025Hz */
#define SOF_RATE_12000		(1 <<  2) /* 12000Hz */
#define SOF_RATE_16000		(1 <<  3) /* 16000Hz */
#define SOF_RATE_22050		(1 <<  4) /* 22050Hz */
#define SOF_RATE_24000		(1 <<  5) /* 24000Hz */
#define SOF_RATE_32000		(1 <<  6) /* 32000Hz */
#define SOF_RATE_44100		(1 <<  7) /* 44100Hz */
#define SOF_RATE_48000		(1 <<  8) /* 48000Hz */
#define SOF_RATE_64000		(1 <<  9) /* 64000Hz */
#define SOF_RATE_88200		(1 << 10) /* 88200Hz */
#define SOF_RATE_96000		(1 << 11) /* 96000Hz */
#define SOF_RATE_176400		(1 << 12) /* 176400Hz */
#define SOF_RATE_192000		(1 << 13) /* 192000Hz */

/* continuous and non-standard rates for flexibility */
#define SOF_RATE_CONTINUOUS	(1 << 30)  /* range */
#define SOF_RATE_KNOT		(1 << 31)  /* non-continuous */

/* stream PCM frame format */
enum sof_ipc_frame {
	SOF_IPC_FRAME_S16_LE = 0,
	SOF_IPC_FRAME_S24_4LE,
	SOF_IPC_FRAME_S32_LE,
	SOF_IPC_FRAME_FLOAT,
	/* other formats here */
};

/* stream buffer format */
enum sof_ipc_buffer_format {
	SOF_IPC_BUFFER_INTERLEAVED,
	SOF_IPC_BUFFER_NONINTERLEAVED,
	/* other formats here */
};

/* stream direction */
enum sof_ipc_stream_direction {
	SOF_IPC_STREAM_PLAYBACK = 0,
	SOF_IPC_STREAM_CAPTURE,
};

/* stream ring info */
struct sof_ipc_host_buffer {
	uint32_t phy_addr;
	uint32_t pages;
	uint32_t size;
	uint32_t offset;
} __attribute__((packed));

struct sof_ipc_stream_params {
	struct sof_ipc_host_buffer buffer;
	enum sof_ipc_stream_direction direction;
	enum sof_ipc_frame frame_fmt;
	enum sof_ipc_buffer_format buffer_fmt;
	uint32_t stream_tag;
	uint32_t rate;
	uint32_t channels;
	uint32_t sample_valid_bytes;
	uint32_t sample_container_bytes;
	/* for notifying host period has completed - 0 means no period IRQ */
	uint32_t host_period_bytes;
	enum sof_ipc_chmap chmap[SOF_IPC_MAX_CHANNELS];	/* channel map */
} __attribute__((packed));

/* PCM params info - SOF_IPC_STREAM_PCM_PARAMS */
struct sof_ipc_pcm_params {
	struct sof_ipc_hdr hdr;
	uint32_t comp_id;
	struct sof_ipc_stream_params params;
}  __attribute__((packed));

/* PCM params info reply - SOF_IPC_STREAM_PCM_PARAMS_REPLY */
struct sof_ipc_pcm_params_reply {
	struct sof_ipc_reply rhdr;
	uint32_t comp_id;
	uint32_t posn_offset;
}   __attribute__((packed));

/* compressed vorbis params - SOF_IPC_STREAM_VORBIS_PARAMS */
struct sof_ipc_vorbis_params {
	struct sof_ipc_hdr hdr;
	uint32_t comp_id;
	struct sof_ipc_stream_params params;
	/* TODO */
}  __attribute__((packed));

/* free stream - SOF_IPC_STREAM_PCM_PARAMS */
struct sof_ipc_stream {
	struct sof_ipc_hdr hdr;
	uint32_t comp_id;
} __attribute__((packed));

/* flags indicating which time stamps are in sync with each other */
#define	SOF_TIME_HOST_SYNC	(1 << 0)
#define	SOF_TIME_DAI_SYNC	(1 << 1)
#define	SOF_TIME_WALL_SYNC	(1 << 2)
#define	SOF_TIME_STAMP_SYNC	(1 << 3)

/* flags indicating which time stamps are valid */
#define	SOF_TIME_HOST_VALID	(1 << 8)
#define	SOF_TIME_DAI_VALID	(1 << 9)
#define	SOF_TIME_WALL_VALID	(1 << 10)
#define	SOF_TIME_STAMP_VALID	(1 << 11)

/* flags indicating time stamps are 64bit else 3use low 32bit */
#define	SOF_TIME_HOST_64	(1 << 16)
#define	SOF_TIME_DAI_64		(1 << 17)
#define	SOF_TIME_WALL_64	(1 << 18)
#define	SOF_TIME_STAMP_64	(1 << 19)

struct sof_ipc_stream_posn {
	struct sof_ipc_reply rhdr;
	uint32_t comp_id;	/* host component ID */
	uint32_t flags;		/* SOF_TIME_ */
	uint32_t wallclock_hz;	/* frequency of wallclock in Hz */
	uint32_t timestamp_ns;	/* resolution of timestamp in ns */
	uint64_t host_posn;	/* host DMA position in bytes */
	uint64_t dai_posn;	/* DAI DMA position in bytes */
	uint64_t comp_posn;	/* comp position in bytes */
	uint64_t wallclock;	/* audio wall clock */
	uint64_t timestamp;	/* system time stamp */
	uint32_t xrun_comp_id;	/* comp ID of XRUN component */
	int32_t xrun_size;	/* XRUN size in bytes */
}  __attribute__((packed));

/*
 * Component Mixers and Controls
 */

/* control data type and direction */
enum sof_ipc_ctrl_type {
	/*  per channel data - uses struct sof_ipc_ctrl_value_chan */
	SOF_CTRL_TYPE_VALUE_CHAN_GET = 0,
	SOF_CTRL_TYPE_VALUE_CHAN_SET,
	/* component data - uses struct sof_ipc_ctrl_value_comp */
	SOF_CTRL_TYPE_VALUE_COMP_GET,
	SOF_CTRL_TYPE_VALUE_COMP_SET,
	/* bespoke data - struct struct sof_abi_hdr */
	SOF_CTRL_TYPE_DATA_GET,
	SOF_CTRL_TYPE_DATA_SET,
};

/* control command type */
enum sof_ipc_ctrl_cmd {
	SOF_CTRL_CMD_VOLUME = 0, /* maps to ALSA volume style controls */
	SOF_CTRL_CMD_ENUM, /* maps to ALSA enum style controls */
	SOF_CTRL_CMD_SWITCH, /* maps to ALSA switch style controls */
	SOF_CTRL_CMD_BINARY, /* maps to ALSA binary style controls */
};

/* generic channel mapped value data */
struct sof_ipc_ctrl_value_chan {
	enum sof_ipc_chmap channel;
	uint32_t value;
} __attribute__((packed));

/* generic component mapped value data */
struct sof_ipc_ctrl_value_comp {
	uint32_t index;	/* component source/sink/control index in control */
	union {
		uint32_t uvalue;
		int32_t svalue;
	};
} __attribute__((packed));

/* generic control data */
struct sof_ipc_ctrl_data {
	struct sof_ipc_reply rhdr;
	uint32_t comp_id;

	/* control access and data type */
	enum sof_ipc_ctrl_type type;
	enum sof_ipc_ctrl_cmd cmd;
	uint32_t index; /* control index for comps > 1 control */

	/* control data - can either be appended or DMAed from host */
	struct sof_ipc_host_buffer buffer;
	uint32_t num_elems;	/* in array elems or bytes */

	/* control data - add new types if needed */
	union {
		/* channel values can be used by volume type controls */
		struct sof_ipc_ctrl_value_chan chanv[0];
		/* component values used by routing controls like mux, mixer */
		struct sof_ipc_ctrl_value_comp compv[0];
		/* data can be used by binary controls */
		struct sof_abi_hdr data[0];
	};
} __attribute__((packed));

/*
 * Component
 */

/* types of component */
enum sof_comp_type {
	SOF_COMP_NONE = 0,
	SOF_COMP_HOST,
	SOF_COMP_DAI,
	SOF_COMP_SG_HOST,	/* scatter gather variant */
	SOF_COMP_SG_DAI,	/* scatter gather variant */
	SOF_COMP_VOLUME,
	SOF_COMP_MIXER,
	SOF_COMP_MUX,
	SOF_COMP_SRC,
	SOF_COMP_SPLITTER,
	SOF_COMP_TONE,
	SOF_COMP_SWITCH,
	SOF_COMP_BUFFER,
	SOF_COMP_EQ_IIR,
	SOF_COMP_EQ_FIR,
	SOF_COMP_FILEREAD,	/* host test based file IO */
	SOF_COMP_FILEWRITE,	/* host test based file IO */
};

/* XRUN action for component */
#define SOF_XRUN_STOP		1	/* stop stream */
#define SOF_XRUN_UNDER_ZERO	2	/* send 0s to sink */
#define SOF_XRUN_OVER_NULL	4	/* send data to NULL */

/* create new generic component - SOF_IPC_TPLG_COMP_NEW */
struct sof_ipc_comp {
	struct sof_ipc_hdr hdr;
	uint32_t id;
	enum sof_comp_type type;
	uint32_t pipeline_id;
} __attribute__((packed));

/*
 * Component Buffers
 */

/* create new component buffer - SOF_IPC_TPLG_BUFFER_NEW */
struct sof_ipc_buffer {
	struct sof_ipc_comp comp;
	uint32_t size;		/* buffer size in bytes */
	uint32_t caps;		/* SOF_MEM_CAPS_ */
} __attribute__((packed));

/* generic component config data - must always be after struct sof_ipc_comp */
struct sof_ipc_comp_config {
	uint32_t periods_sink;	/* 0 means variable */
	uint32_t periods_source;	/* 0 means variable */
	uint32_t preload_count;	/* how many periods to preload */
	enum sof_ipc_frame frame_fmt;
	uint32_t xrun_action;
} __attribute__((packed));

/* generic host component */
struct sof_ipc_comp_host {
	struct sof_ipc_comp comp;
	struct sof_ipc_comp_config config;
	enum sof_ipc_stream_direction direction;
	uint32_t no_irq;	/* don't send periodic IRQ to host/DSP */
	uint32_t dmac_config; /* DMA engine specific */
}  __attribute__((packed));

/* generic DAI component */
struct sof_ipc_comp_dai {
	struct sof_ipc_comp comp;
	struct sof_ipc_comp_config config;
	enum sof_ipc_stream_direction direction;
	uint32_t dai_index; /* index of this type dai */
	enum sof_ipc_dai_type type;
	uint32_t dmac_config; /* DMA engine specific */
}  __attribute__((packed));

/* generic mixer component */
struct sof_ipc_comp_mixer {
	struct sof_ipc_comp comp;
	struct sof_ipc_comp_config config;
}  __attribute__((packed));

/* volume ramping types */
enum sof_volume_ramp {
	SOF_VOLUME_LINEAR	= 0,
	SOF_VOLUME_LOG,
	SOF_VOLUME_LINEAR_ZC,
	SOF_VOLUME_LOG_ZC,
};

/* generic volume component */
struct sof_ipc_comp_volume {
	struct sof_ipc_comp comp;
	struct sof_ipc_comp_config config;
	uint32_t channels;
	uint32_t min_value;
	uint32_t max_value;
	enum sof_volume_ramp ramp;
	uint32_t initial_ramp;	/* ramp space in ms */
}  __attribute__((packed));

/* generic SRC component */
struct sof_ipc_comp_src {
	struct sof_ipc_comp comp;
	struct sof_ipc_comp_config config;
	/* either source or sink rate must be non zero */
	uint32_t source_rate;	/* source rate or 0 for variable */
	uint32_t sink_rate;	/* sink rate or 0 for variable */
	uint32_t rate_mask;	/* SOF_RATE_ supported rates */
} __attribute__((packed));

/* generic MUX component */
struct sof_ipc_comp_mux {
	struct sof_ipc_comp comp;
	struct sof_ipc_comp_config config;
} __attribute__((packed));

/* generic tone generator component */
struct sof_ipc_comp_tone {
	struct sof_ipc_comp comp;
	struct sof_ipc_comp_config config;
	int32_t sample_rate;
	int32_t frequency;
	int32_t amplitude;
	int32_t freq_mult;
	int32_t ampl_mult;
	int32_t length;
	int32_t period;
	int32_t repeats;
	int32_t ramp_step;
} __attribute__((packed));

/* FIR equalizer component */
struct sof_ipc_comp_eq_fir {
	struct sof_ipc_comp comp;
	struct sof_ipc_comp_config config;
} __attribute__((packed));

/* IIR equalizer component */
struct sof_ipc_comp_eq_iir {
	struct sof_ipc_comp comp;
	struct sof_ipc_comp_config config;
} __attribute__((packed));

/** \brief Types of EFFECT */
enum sof_ipc_effect_type {
	SOF_EFFECT_INTEL_NONE = 0,	/**< None */
	SOF_EFFECT_INTEL_EQFIR,		/**< Intel FIR */
	SOF_EFFECT_INTEL_EQIIR,		/**< Intel IIR */
};

/* general purpose EFFECT configuration */
struct sof_ipc_comp_effect {
	enum sof_ipc_effect_type type;
} __attribute__((packed));

/* frees components, buffers and pipelines
 * SOF_IPC_TPLG_COMP_FREE, SOF_IPC_TPLG_PIPE_FREE, SOF_IPC_TPLG_BUFFER_FREE
 */
struct sof_ipc_free {
	struct sof_ipc_hdr hdr;
	uint32_t id;
} __attribute__((packed));

struct sof_ipc_comp_reply {
	struct sof_ipc_reply rhdr;
	uint32_t id;
	uint32_t offset;
} __attribute__((packed));

/*
 * Pipeline
 */

/* new pipeline - SOF_IPC_TPLG_PIPE_NEW */
struct sof_ipc_pipe_new {
	struct sof_ipc_hdr hdr;
	uint32_t comp_id;	/* component id for pipeline */
	uint32_t pipeline_id;	/* pipeline id */
	uint32_t sched_id;	/* sheduling component id */
	uint32_t core;		/* core we run on */
	uint32_t deadline;	/* execution completion deadline in us*/
	uint32_t priority;	/* priority level 0 (low) to 10 (max) */
	uint32_t mips;		/* worst case instruction count per period */
	uint32_t frames_per_sched;/* output frames of pipeline, 0 is variable */
	uint32_t xrun_limit_usecs; /* report xruns greater than limit */
	uint32_t timer;/* non zero if timer scheduled otherwise DAI scheduled */
}  __attribute__((packed));

/* pipeline construction complete - SOF_IPC_TPLG_PIPE_COMPLETE */
struct sof_ipc_pipe_ready {
	struct sof_ipc_hdr hdr;
	uint32_t comp_id;
}  __attribute__((packed));

struct sof_ipc_pipe_free {
	struct sof_ipc_hdr hdr;
	uint32_t comp_id;
}  __attribute__((packed));

/* connect two components in pipeline - SOF_IPC_TPLG_COMP_CONNECT */
struct sof_ipc_pipe_comp_connect {
	struct sof_ipc_hdr hdr;
	uint32_t source_id;
	uint32_t sink_id;
}  __attribute__((packed));

/*
 * PM
 */

/* PM context element */
struct sof_ipc_pm_ctx_elem {
	uint32_t type;
	uint32_t size;
	uint64_t addr;
}  __attribute__((packed));

/*
 * PM context - SOF_IPC_PM_CTX_SAVE, SOF_IPC_PM_CTX_RESTORE,
 * SOF_IPC_PM_CTX_SIZE
 */
struct sof_ipc_pm_ctx {
	struct sof_ipc_hdr hdr;
	struct sof_ipc_host_buffer buffer;
	uint32_t num_elems;
	uint32_t size;
	struct sof_ipc_pm_ctx_elem elems[];
};

/*
 * Firmware boot and version
 */

#define SOF_IPC_MAX_ELEMS	16

/* extended data types that can be appended onto end of sof_ipc_fw_ready */
enum sof_ipc_ext_data {
	SOF_IPC_EXT_DMA_BUFFER = 0,
	SOF_IPC_EXT_WINDOW,
};

/* FW version - SOF_IPC_GLB_VERSION */
struct sof_ipc_fw_version {
	uint16_t major;
	uint16_t minor;
	uint16_t build;
	uint8_t date[12];
	uint8_t time[10];
	uint8_t tag[6];
	uint8_t pad[2]; /* Make sure the total size is 4 bytes aligned */
} __attribute__((packed));

/* FW ready Message - sent by firmware when boot has completed */
struct sof_ipc_fw_ready {
	struct sof_ipc_hdr hdr;
	uint32_t dspbox_offset;	 /* dsp initiated IPC mailbox */
	uint32_t hostbox_offset; /* host initiated IPC mailbox */
	uint32_t dspbox_size;
	uint32_t hostbox_size;
	struct sof_ipc_fw_version version;
} __attribute__((packed));

/*
 * Extended Firmware data. All optional, depends on platform/arch.
 */

enum sof_ipc_region {
	SOF_IPC_REGION_DOWNBOX	= 0,
	SOF_IPC_REGION_UPBOX,
	SOF_IPC_REGION_TRACE,
	SOF_IPC_REGION_DEBUG,
	SOF_IPC_REGION_STREAM,
	SOF_IPC_REGION_REGS,
	SOF_IPC_REGION_EXCEPTION,
};

struct sof_ipc_ext_data_hdr {
	struct sof_ipc_hdr hdr;
	enum sof_ipc_ext_data type;			/* SOF_IPC_EXT_ */
};

struct sof_ipc_dma_buffer_elem {
	enum sof_ipc_region type;
	uint32_t id;	/* platform specific - used to map to host memory */
	struct sof_ipc_host_buffer buffer;
};

/* extended data DMA buffers for IPC, trace and debug */
struct sof_ipc_dma_buffer_data {
	struct sof_ipc_ext_data_hdr ext_hdr;
	uint32_t num_buffers;
	/* host files in buffer[n].buffer */
	struct sof_ipc_dma_buffer_elem buffer[];
}  __attribute__((packed));

struct sof_ipc_window_elem {
	enum sof_ipc_region type;
	uint32_t id;	/* platform specific - used to map to host memory */
	uint32_t flags;	/* R, W, RW, etc - to define */
	uint32_t size;	/* size of region in bytes */
	/* offset in window region as windows can be partitioned */
	uint32_t offset;
};

/* extended data memory windows for IPC, trace and debug */
struct sof_ipc_window {
	struct sof_ipc_ext_data_hdr ext_hdr;
	uint32_t num_windows;
	struct sof_ipc_window_elem window[];
}  __attribute__((packed));

/*
 * DMA for Trace
 */

/* DMA for Trace params info - SOF_IPC_DEBUG_DMA_PARAMS */
struct sof_ipc_dma_trace_params {
	struct sof_ipc_hdr hdr;
	struct sof_ipc_host_buffer buffer;
	uint32_t stream_tag;
}  __attribute__((packed));

/* DMA for Trace params info - SOF_IPC_DEBUG_DMA_PARAMS */
struct sof_ipc_dma_trace_posn {
	struct sof_ipc_reply rhdr;
	uint32_t host_offset;	/* Offset of DMA host buffer */
	uint32_t overflow;	/* overflow bytes if any */
	uint32_t messages;	/* total trace messages */
}  __attribute__((packed));

/*
 * Architecture specific debug
 */

/* Xtensa Firmware Oops data */
struct sof_ipc_dsp_oops_xtensa {
	uint32_t exccause;
	uint32_t excvaddr;
	uint32_t ps;
	uint32_t epc1;
	uint32_t epc2;
	uint32_t epc3;
	uint32_t epc4;
	uint32_t epc5;
	uint32_t epc6;
	uint32_t epc7;
	uint32_t eps2;
	uint32_t eps3;
	uint32_t eps4;
	uint32_t eps5;
	uint32_t eps6;
	uint32_t eps7;
	uint32_t depc;
	uint32_t intenable;
	uint32_t interrupt;
	uint32_t sar;
	uint32_t stack;
}  __attribute__((packed));

#endif
