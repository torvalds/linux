/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2018 Intel Corporation. All rights reserved.
 */

#ifndef __INCLUDE_SOUND_SOF_TOPOLOGY_H__
#define __INCLUDE_SOUND_SOF_TOPOLOGY_H__

#include <sound/sof/header.h>

/*
 * Component
 */

/* types of component */
enum sof_comp_type {
	SOF_COMP_NONE = 0,
	SOF_COMP_HOST,
	SOF_COMP_DAI,
	SOF_COMP_SG_HOST,	/**< scatter gather variant */
	SOF_COMP_SG_DAI,	/**< scatter gather variant */
	SOF_COMP_VOLUME,
	SOF_COMP_MIXER,
	SOF_COMP_MUX,
	SOF_COMP_SRC,
	SOF_COMP_DEPRECATED0, /* Formerly SOF_COMP_SPLITTER */
	SOF_COMP_TONE,
	SOF_COMP_DEPRECATED1, /* Formerly SOF_COMP_SWITCH */
	SOF_COMP_BUFFER,
	SOF_COMP_EQ_IIR,
	SOF_COMP_EQ_FIR,
	SOF_COMP_KEYWORD_DETECT,
	SOF_COMP_KPB,			/* A key phrase buffer component */
	SOF_COMP_SELECTOR,		/**< channel selector component */
	SOF_COMP_DEMUX,
	SOF_COMP_ASRC,		/**< Asynchronous sample rate converter */
	SOF_COMP_DCBLOCK,
	SOF_COMP_SMART_AMP,             /**< smart amplifier component */
	SOF_COMP_MODULE_ADAPTER,		/**< module adapter */
	/* keep FILEREAD/FILEWRITE as the last ones */
	SOF_COMP_FILEREAD = 10000,	/**< host test based file IO */
	SOF_COMP_FILEWRITE = 10001,	/**< host test based file IO */
};

/* XRUN action for component */
#define SOF_XRUN_STOP		1	/**< stop stream */
#define SOF_XRUN_UNDER_ZERO	2	/**< send 0s to sink */
#define SOF_XRUN_OVER_NULL	4	/**< send data to NULL */

/* create new generic component - SOF_IPC_TPLG_COMP_NEW */
struct sof_ipc_comp {
	struct sof_ipc_cmd_hdr hdr;
	uint32_t id;
	enum sof_comp_type type;
	uint32_t pipeline_id;
	uint32_t core;

	/* extended data length, 0 if no extended data */
	uint32_t ext_data_length;
} __packed __aligned(4);

/*
 * Component Buffers
 */

/*
 * SOF memory capabilities, add new ones at the end
 */
#define SOF_MEM_CAPS_RAM		BIT(0)
#define SOF_MEM_CAPS_ROM		BIT(1)
#define SOF_MEM_CAPS_EXT		BIT(2) /**< external */
#define SOF_MEM_CAPS_LP			BIT(3) /**< low power */
#define SOF_MEM_CAPS_HP			BIT(4) /**< high performance */
#define SOF_MEM_CAPS_DMA		BIT(5) /**< DMA'able */
#define SOF_MEM_CAPS_CACHE		BIT(6) /**< cacheable */
#define SOF_MEM_CAPS_EXEC		BIT(7) /**< executable */
#define SOF_MEM_CAPS_L3			BIT(8) /**< L3 memory */

/*
 * overrun will cause ring buffer overwrite, instead of XRUN.
 */
#define SOF_BUF_OVERRUN_PERMITTED	BIT(0)

/*
 * underrun will cause readback of 0s, instead of XRUN.
 */
#define SOF_BUF_UNDERRUN_PERMITTED	BIT(1)

/* the UUID size in bytes, shared between FW and host */
#define SOF_UUID_SIZE	16

/* create new component buffer - SOF_IPC_TPLG_BUFFER_NEW */
struct sof_ipc_buffer {
	struct sof_ipc_comp comp;
	uint32_t size;		/**< buffer size in bytes */
	uint32_t caps;		/**< SOF_MEM_CAPS_ */
	uint32_t flags;		/**< SOF_BUF_ flags defined above */
	uint32_t reserved;	/**< reserved for future use */
} __packed __aligned(4);

/* generic component config data - must always be after struct sof_ipc_comp */
struct sof_ipc_comp_config {
	struct sof_ipc_cmd_hdr hdr;
	uint32_t periods_sink;	/**< 0 means variable */
	uint32_t periods_source;/**< 0 means variable */
	uint32_t reserved1;	/**< reserved */
	uint32_t frame_fmt;	/**< SOF_IPC_FRAME_ */
	uint32_t xrun_action;

	/* reserved for future use */
	uint32_t reserved[2];
} __packed __aligned(4);

/* generic host component */
struct sof_ipc_comp_host {
	struct sof_ipc_comp comp;
	struct sof_ipc_comp_config config;
	uint32_t direction;	/**< SOF_IPC_STREAM_ */
	uint32_t no_irq;	/**< don't send periodic IRQ to host/DSP */
	uint32_t dmac_config; /**< DMA engine specific */
} __packed __aligned(4);

/* generic DAI component */
struct sof_ipc_comp_dai {
	struct sof_ipc_comp comp;
	struct sof_ipc_comp_config config;
	uint32_t direction;	/**< SOF_IPC_STREAM_ */
	uint32_t dai_index;	/**< index of this type dai */
	uint32_t type;		/**< DAI type - SOF_DAI_ */
	uint32_t reserved;	/**< reserved */
} __packed __aligned(4);

/* generic mixer component */
struct sof_ipc_comp_mixer {
	struct sof_ipc_comp comp;
	struct sof_ipc_comp_config config;
} __packed __aligned(4);

/* volume ramping types */
enum sof_volume_ramp {
	SOF_VOLUME_LINEAR	= 0,
	SOF_VOLUME_LOG,
	SOF_VOLUME_LINEAR_ZC,
	SOF_VOLUME_LOG_ZC,
	SOF_VOLUME_WINDOWS_FADE,
	SOF_VOLUME_WINDOWS_NO_FADE,
};

/* generic volume component */
struct sof_ipc_comp_volume {
	struct sof_ipc_comp comp;
	struct sof_ipc_comp_config config;
	uint32_t channels;
	uint32_t min_value;
	uint32_t max_value;
	uint32_t ramp;		/**< SOF_VOLUME_ */
	uint32_t initial_ramp;	/**< ramp space in ms */
} __packed __aligned(4);

/* generic SRC component */
struct sof_ipc_comp_src {
	struct sof_ipc_comp comp;
	struct sof_ipc_comp_config config;
	/* either source or sink rate must be non zero */
	uint32_t source_rate;	/**< source rate or 0 for variable */
	uint32_t sink_rate;	/**< sink rate or 0 for variable */
	uint32_t rate_mask;	/**< SOF_RATE_ supported rates */
} __packed __aligned(4);

/* generic ASRC component */
struct sof_ipc_comp_asrc {
	struct sof_ipc_comp comp;
	struct sof_ipc_comp_config config;
	/* either source or sink rate must be non zero */
	uint32_t source_rate;		/**< Define fixed source rate or */
					/**< use 0 to indicate need to get */
					/**< the rate from stream */
	uint32_t sink_rate;		/**< Define fixed sink rate or */
					/**< use 0 to indicate need to get */
					/**< the rate from stream */
	uint32_t asynchronous_mode;	/**< synchronous 0, asynchronous 1 */
					/**< When 1 the ASRC tracks and */
					/**< compensates for drift. */
	uint32_t operation_mode;	/**< push 0, pull 1, In push mode the */
					/**< ASRC consumes a defined number */
					/**< of frames at input, with varying */
					/**< number of frames at output. */
					/**< In pull mode the ASRC outputs */
					/**< a defined number of frames while */
					/**< number of input frames varies. */

	/* reserved for future use */
	uint32_t reserved[4];
} __packed __aligned(4);

/* generic MUX component */
struct sof_ipc_comp_mux {
	struct sof_ipc_comp comp;
	struct sof_ipc_comp_config config;
} __packed __aligned(4);

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
} __packed __aligned(4);

/** \brief Types of processing components */
enum sof_ipc_process_type {
	SOF_PROCESS_NONE = 0,		/**< None */
	SOF_PROCESS_EQFIR,		/**< Intel FIR */
	SOF_PROCESS_EQIIR,		/**< Intel IIR */
	SOF_PROCESS_KEYWORD_DETECT,	/**< Keyword Detection */
	SOF_PROCESS_KPB,		/**< KeyPhrase Buffer Manager */
	SOF_PROCESS_CHAN_SELECTOR,	/**< Channel Selector */
	SOF_PROCESS_MUX,
	SOF_PROCESS_DEMUX,
	SOF_PROCESS_DCBLOCK,
	SOF_PROCESS_SMART_AMP,	/**< Smart Amplifier */
};

/* generic "effect", "codec" or proprietary processing component */
struct sof_ipc_comp_process {
	struct sof_ipc_comp comp;
	struct sof_ipc_comp_config config;
	uint32_t size;	/**< size of bespoke data section in bytes */
	uint32_t type;	/**< sof_ipc_process_type */

	/* reserved for future use */
	uint32_t reserved[7];

	unsigned char data[];
} __packed __aligned(4);

/* frees components, buffers and pipelines
 * SOF_IPC_TPLG_COMP_FREE, SOF_IPC_TPLG_PIPE_FREE, SOF_IPC_TPLG_BUFFER_FREE
 */
struct sof_ipc_free {
	struct sof_ipc_cmd_hdr hdr;
	uint32_t id;
} __packed __aligned(4);

struct sof_ipc_comp_reply {
	struct sof_ipc_reply rhdr;
	uint32_t id;
	uint32_t offset;
} __packed __aligned(4);

/*
 * Pipeline
 */

/** \brief Types of pipeline scheduling time domains */
enum sof_ipc_pipe_sched_time_domain {
	SOF_TIME_DOMAIN_DMA = 0,	/**< DMA interrupt */
	SOF_TIME_DOMAIN_TIMER,		/**< Timer interrupt */
};

/* new pipeline - SOF_IPC_TPLG_PIPE_NEW */
struct sof_ipc_pipe_new {
	struct sof_ipc_cmd_hdr hdr;
	uint32_t comp_id;	/**< component id for pipeline */
	uint32_t pipeline_id;	/**< pipeline id */
	uint32_t sched_id;	/**< Scheduling component id */
	uint32_t core;		/**< core we run on */
	uint32_t period;	/**< execution period in us*/
	uint32_t priority;	/**< priority level 0 (low) to 10 (max) */
	uint32_t period_mips;	/**< worst case instruction count per period */
	uint32_t frames_per_sched;/**< output frames of pipeline, 0 is variable */
	uint32_t xrun_limit_usecs; /**< report xruns greater than limit */
	uint32_t time_domain;	/**< scheduling time domain */
} __packed __aligned(4);

/* pipeline construction complete - SOF_IPC_TPLG_PIPE_COMPLETE */
struct sof_ipc_pipe_ready {
	struct sof_ipc_cmd_hdr hdr;
	uint32_t comp_id;
} __packed __aligned(4);

struct sof_ipc_pipe_free {
	struct sof_ipc_cmd_hdr hdr;
	uint32_t comp_id;
} __packed __aligned(4);

/* connect two components in pipeline - SOF_IPC_TPLG_COMP_CONNECT */
struct sof_ipc_pipe_comp_connect {
	struct sof_ipc_cmd_hdr hdr;
	uint32_t source_id;
	uint32_t sink_id;
} __packed __aligned(4);

/* external events */
enum sof_event_types {
	SOF_EVENT_NONE = 0,
	SOF_KEYWORD_DETECT_DAPM_EVENT,
};

#endif
