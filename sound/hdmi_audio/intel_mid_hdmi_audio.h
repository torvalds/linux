/*
 *   intel_mid_hdmi_audio.h - Intel HDMI audio driver for MID
 *
 *  Copyright (C) 2010 Intel Corp
 *  Authors:	Sailaja Bandarupalli <sailaja.bandarupalli@intel.com>
 *		Ramesh Babu K V	<ramesh.babu@intel.com>
 *		Vaibhav Agarwal <vaibhav.agarwal@intel.com>
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * ALSA driver for Intel MID HDMI audio controller
 */
#ifndef __INTEL_MID_HDMI_AUDIO_H
#define __INTEL_MID_HDMI_AUDIO_H

#include <linux/types.h>
#include <sound/initval.h>
#include <linux/version.h>
#include <linux/pm_runtime.h>
#include <sound/asoundef.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <hdmi_audio_if.h>

#define OSPM_DISPLAY_ISLAND	0x40

typedef enum _UHBUsage {
	OSPM_UHB_ONLY_IF_ON = 0,
	OSPM_UHB_FORCE_POWER_ON,
} UHBUsage;

bool ospm_power_using_hw_begin(int hw_island, UHBUsage usage);
void ospm_power_using_hw_end(int hw_island);

/*
 * Use this function to do an instantaneous check for if the hw is on.
 * Only use this in cases where you know the g_state_change_mutex
 * is already held such as in irq install/uninstall and you need to
 * prevent a deadlock situation.  Otherwise use ospm_power_using_hw_begin().
 */
bool ospm_power_is_hw_on(int hw_islands);

#define HAD_DRIVER_VERSION	"0.01.003"
#define HAD_MAX_DEVICES		1
#define HAD_MIN_CHANNEL		2
#define HAD_MAX_CHANNEL		8
#define HAD_NUM_OF_RING_BUFS	4

/* Assume 192KHz, 8channel, 25msec period */
#define HAD_MAX_BUFFER		(600*1024)
#define HAD_MIN_BUFFER		(32*1024)
#define HAD_MAX_PERIODS		4
#define HAD_MIN_PERIODS		4
#define HAD_MAX_PERIOD_BYTES	(HAD_MAX_BUFFER/HAD_MIN_PERIODS)
#define HAD_MIN_PERIOD_BYTES	256
#define HAD_FIFO_SIZE		0 /* fifo not being used */
#define MAX_SPEAKERS		8
/* TODO: Add own tlv when channel map is ported for user space */
#define USE_ALSA_DEFAULT_TLV

#define AUD_SAMPLE_RATE_32	32000
#define AUD_SAMPLE_RATE_44_1	44100
#define AUD_SAMPLE_RATE_48	48000
#define AUD_SAMPLE_RATE_88_2	88200
#define AUD_SAMPLE_RATE_96	96000
#define AUD_SAMPLE_RATE_176_4	176400
#define AUD_SAMPLE_RATE_192	192000

#define HAD_MIN_RATE		AUD_SAMPLE_RATE_32
#define HAD_MAX_RATE		AUD_SAMPLE_RATE_192

#define DRIVER_NAME		"intelmid_hdmi_audio"
#define DIS_SAMPLE_RATE_25_2	25200
#define DIS_SAMPLE_RATE_27	27000
#define DIS_SAMPLE_RATE_54	54000
#define DIS_SAMPLE_RATE_74_25	74250
#define DIS_SAMPLE_RATE_148_5	148500
#define HAD_REG_WIDTH		0x08
#define HAD_MAX_HW_BUFS		0x04
#define HAD_MAX_DIP_WORDS		16
#define INTEL_HAD		"IntelHDMI"

/* _AUD_CONFIG register MASK */
#define AUD_CONFIG_MASK_UNDERRUN	0xC0000000
#define AUD_CONFIG_MASK_SRDBG		0x00000002
#define AUD_CONFIG_MASK_FUNCRST		0x00000001

#define MAX_CNT			0xFF
#define HAD_SUSPEND_DELAY	1000

#define OTM_HDMI_ELD_SIZE 84

typedef union {
	uint8_t eld_data[OTM_HDMI_ELD_SIZE];
	#pragma pack(1)
	struct {
		/* Byte[0] = ELD Version Number */
		union {
			uint8_t   byte0;
			struct {
				uint8_t reserved:3; /* Reserf */
				uint8_t eld_ver:5; /* ELD Version Number */
						/* 00000b - reserved
						 * 00001b - first rev, obsoleted
						 * 00010b - version 2, supporting CEA version 861D or below
						 * 00011b:11111b - reserved
						 * for future
						 */
			};
		};

		/* Byte[1] = Vendor Version Field */
		union {
			uint8_t vendor_version;
			struct {
				uint8_t reserved1:3;
				uint8_t veld_ver:5; /* Version number of the ELD
						     * extension. This value is
						     * provisioned and unique to
						     * each vendor.
						     */
			};
		};

		/* Byte[2] = Baseline Length field */
		uint8_t baseline_eld_length; /* Length of the Baseline structure
					      *	divided by Four.
					      */

		/* Byte [3] = Reserved for future use */
		uint8_t byte3;

		/* Starting of the BaseLine EELD structure
		 * Byte[4] = Monitor Name Length
		 */
		union {
			uint8_t byte4;
			struct {
				uint8_t mnl:5;
				uint8_t cea_edid_rev_id:3;
			};
		};

		/* Byte[5] = Capabilities */
		union {
			uint8_t capabilities;
			struct {
				uint8_t hdcp:1; /* HDCP support */
				uint8_t ai_support:1;   /* AI support */
				uint8_t connection_type:2; /* Connection type
							    * 00 - HDMI
							    * 01 - DP
							    * 10 -11  Reserved
							    * for future
							    * connection types
							    */
				uint8_t sadc:4; /* Indicates number of 3 bytes
						 * Short Audio Descriptors.
						 */
			};
		};

		/* Byte[6] = Audio Synch Delay */
		uint8_t audio_synch_delay; /* Amount of time reported by the
					    * sink that the video trails audio
					    * in milliseconds.
					    */

		/* Byte[7] = Speaker Allocation Block */
		union {
			uint8_t speaker_allocation_block;
			struct {
				uint8_t flr:1; /*Front Left and Right channels*/
				uint8_t lfe:1; /*Low Frequency Effect channel*/
				uint8_t fc:1;  /*Center transmission channel*/
				uint8_t rlr:1; /*Rear Left and Right channels*/
				uint8_t rc:1; /*Rear Center channel*/
				uint8_t flrc:1; /*Front left and Right of Center
						 *transmission channels
						 */
				uint8_t rlrc:1; /*Rear left and Right of Center
						 *transmission channels
						 */
				uint8_t reserved3:1; /* Reserved */
			};
		};

		/* Byte[8 - 15] - 8 Byte port identification value */
		uint8_t port_id_value[8];

		/* Byte[16 - 17] - 2 Byte Manufacturer ID */
		uint8_t manufacturer_id[2];

		/* Byte[18 - 19] - 2 Byte Product ID */
		uint8_t product_id[2];

		/* Byte [20-83] - 64 Bytes of BaseLine Data */
		uint8_t mn_sand_sads[64]; /* This will include
					   * - ASCII string of Monitor name
					   * - List of 3 byte SADs
					   * - Zero padding
					   */

		/* Vendor ELD Block should continue here!
		 * No Vendor ELD block defined as of now.
		 */
	};
	#pragma pack()
} otm_hdmi_eld_t;

/**
 * enum had_status - Audio stream states
 *
 * @STREAM_INIT: Stream initialized
 * @STREAM_RUNNING: Stream running
 * @STREAM_PAUSED: Stream paused
 * @STREAM_DROPPED: Stream dropped
 */
enum had_stream_status {
	STREAM_INIT = 0,
	STREAM_RUNNING = 1,
	STREAM_PAUSED = 2,
	STREAM_DROPPED = 3
};

/**
 * enum had_status_stream - HAD stream states
 */
enum had_status_stream {
	HAD_INIT = 0,
	HAD_RUNNING_STREAM,
};

enum had_drv_status {
	HAD_DRV_CONNECTED,
	HAD_DRV_RUNNING,
	HAD_DRV_DISCONNECTED,
	HAD_DRV_SUSPENDED,
	HAD_DRV_ERR,
};

/* enum intel_had_aud_buf_type - HDMI controller ring buffer types */
enum intel_had_aud_buf_type {
	HAD_BUF_TYPE_A = 0,
	HAD_BUF_TYPE_B = 1,
	HAD_BUF_TYPE_C = 2,
	HAD_BUF_TYPE_D = 3,
};

enum num_aud_ch {
	CH_STEREO = 0,
	CH_THREE_FOUR = 1,
	CH_FIVE_SIX = 2,
	CH_SEVEN_EIGHT = 3
};


/* HDMI controller register offsets */
enum hdmi_ctrl_reg_offset_v1 {
	AUD_CONFIG		= 0x0,
	AUD_CH_STATUS_0		= 0x08,
	AUD_CH_STATUS_1		= 0x0C,
	AUD_HDMI_CTS		= 0x10,
	AUD_N_ENABLE		= 0x14,
	AUD_SAMPLE_RATE		= 0x18,
	AUD_BUF_CONFIG		= 0x20,
	AUD_BUF_CH_SWAP		= 0x24,
	AUD_BUF_A_ADDR		= 0x40,
	AUD_BUF_A_LENGTH	= 0x44,
	AUD_BUF_B_ADDR		= 0x48,
	AUD_BUF_B_LENGTH	= 0x4c,
	AUD_BUF_C_ADDR		= 0x50,
	AUD_BUF_C_LENGTH	= 0x54,
	AUD_BUF_D_ADDR		= 0x58,
	AUD_BUF_D_LENGTH	= 0x5c,
	AUD_CNTL_ST		= 0x60,
	AUD_HDMI_STATUS		= 0x68,
	AUD_HDMIW_INFOFR	= 0x114,
};

/*
 * Delta changes in HDMI controller register offsets
 * compare to v1 version
 */

enum hdmi_ctrl_reg_offset_v2 {
	AUD_HDMI_STATUS_v2	= 0x64,
	AUD_HDMIW_INFOFR_v2	= 0x68,
};

/*
 * CEA speaker placement:
 *
 *  FL  FLC   FC   FRC   FR
 *
 *                         LFE
 *
 *  RL  RLC   RC   RRC   RR
 *
 * The Left/Right Surround channel _notions_ LS/RS in SMPTE 320M corresponds to
 * CEA RL/RR; The SMPTE channel _assignment_ C/LFE is swapped to CEA LFE/FC.
 */
enum cea_speaker_placement {
	FL  = (1 <<  0),        /* Front Left           */
	FC  = (1 <<  1),        /* Front Center         */
	FR  = (1 <<  2),        /* Front Right          */
	FLC = (1 <<  3),        /* Front Left Center    */
	FRC = (1 <<  4),        /* Front Right Center   */
	RL  = (1 <<  5),        /* Rear Left            */
	RC  = (1 <<  6),        /* Rear Center          */
	RR  = (1 <<  7),        /* Rear Right           */
	RLC = (1 <<  8),        /* Rear Left Center     */
	RRC = (1 <<  9),        /* Rear Right Center    */
	LFE = (1 << 10),        /* Low Frequency Effect */
};

struct cea_channel_speaker_allocation {
	int ca_index;
	int speakers[8];

	/* derived values, just for convenience */
	int channels;
	int spk_mask;
};

struct channel_map_table {
	unsigned char map;              /* ALSA API channel map position */
	unsigned char cea_slot;         /* CEA slot value */
	int spk_mask;                   /* speaker position bit mask */
};

/**
 * union aud_cfg - Audio configuration
 *
 * @cfg_regx: individual register bits
 * @cfg_regval: full register value
 *
 */
union aud_cfg {
	struct {
		u32 aud_en:1;
		u32 layout:1;
		u32 fmt:2;
		u32 num_ch:2;
		u32 rsvd0:1;
		u32 set:1;
		u32 flat:1;
		u32 val_bit:1;
		u32 user_bit:1;
		u32 underrun:1;
		u32 rsvd1:20;
	} cfg_regx;
	struct {
		u32 aud_en:1;
		u32 layout:1;
		u32 fmt:2;
		u32 num_ch:3;
		u32 set:1;
		u32 flat:1;
		u32 val_bit:1;
		u32 user_bit:1;
		u32 underrun:1;
		u32 packet_mode:1;
		u32 left_align:1;
		u32 bogus_sample:1;
		u32 dp_modei:1;
		u32 rsvd:16;
	} cfg_regx_v2;
	u32 cfg_regval;
};

/**
 * union aud_ch_status_0 - Audio Channel Status 0 Attributes
 *
 * @status_0_regx:individual register bits
 * @status_0_regval:full register value
 *
 */
union aud_ch_status_0 {
	struct {
		u32 ch_status:1;
		u32 lpcm_id:1;
		u32 cp_info:1;
		u32 format:3;
		u32 mode:2;
		u32 ctg_code:8;
		u32 src_num:4;
		u32 ch_num:4;
		u32 samp_freq:4;
		u32 clk_acc:2;
		u32 rsvd:2;
	} status_0_regx;
	u32 status_0_regval;
};

/**
 * union aud_ch_status_1 - Audio Channel Status 1 Attributes
 *
 * @status_1_regx: individual register bits
 * @status_1_regval: full register value
 *
 */
union aud_ch_status_1 {
	struct {
		u32 max_wrd_len:1;
		u32 wrd_len:3;
		u32 rsvd:28;
		} status_1_regx;
	u32 status_1_regval;
};

/**
 * union aud_hdmi_cts - CTS register
 *
 * @cts_regx: individual register bits
 * @cts_regval: full register value
 *
 */
union aud_hdmi_cts {
	struct {
		u32 cts_val:20;
		u32 en_cts_prog:1;
		u32 rsvd:11;
	} cts_regx;
	struct {
		u32 cts_val:24;
		u32 en_cts_prog:1;
		u32 rsvd:7;
	} cts_regx_v2;
	u32 cts_regval;
};

/**
 * union aud_hdmi_n_enable - N register
 *
 * @n_regx: individual register bits
 * @n_regval: full register value
 *
 */
union aud_hdmi_n_enable {
	struct {
		u32 n_val:20;
		u32 en_n_prog:1;
		u32 rsvd:11;
	} n_regx;
	struct {
		u32 n_val:24;
		u32 en_n_prog:1;
		u32 rsvd:7;
	} n_regx_v2;
	u32 n_regval;
};

/**
 * union aud_buf_config -  Audio Buffer configurations
 *
 * @buf_cfg_regx: individual register bits
 * @buf_cfgval: full register value
 *
 */
union aud_buf_config {
	struct {
		u32 fifo_width:8;
		u32 rsvd0:8;
		u32 aud_delay:8;
		u32 rsvd1:8;
	} buf_cfg_regx;
	struct {
		u32 audio_fifo_watermark:8;
		u32 dma_fifo_watermark:3;
		u32 rsvd0:5;
		u32 aud_delay:8;
		u32 rsvd1:8;
	} buf_cfg_regx_v2;
	u32 buf_cfgval;
};

/**
 * union aud_buf_ch_swap - Audio Sample Swapping offset
 *
 * @buf_ch_swap_regx: individual register bits
 * @buf_ch_swap_val: full register value
 *
 */
union aud_buf_ch_swap {
	struct {
		u32 first_0:3;
		u32 second_0:3;
		u32 first_1:3;
		u32 second_1:3;
		u32 first_2:3;
		u32 second_2:3;
		u32 first_3:3;
		u32 second_3:3;
		u32 rsvd:8;
	} buf_ch_swap_regx;
	u32 buf_ch_swap_val;
};

/**
 * union aud_buf_addr - Address for Audio Buffer
 *
 * @buf_addr_regx: individual register bits
 * @buf_addr_val: full register value
 *
 */
union aud_buf_addr {
	struct {
		u32 valid:1;
		u32 intr_en:1;
		u32 rsvd:4;
		u32 addr:26;
	} buf_addr_regx;
	u32 buf_addr_val;
};

/**
 * union aud_buf_len - Length of Audio Buffer
 *
 * @buf_len_regx: individual register bits
 * @buf_len_val: full register value
 *
 */
union aud_buf_len {
	struct {
		u32 buf_len:20;
		u32 rsvd:12;
	} buf_len_regx;
	u32 buf_len_val;
};

/**
 * union aud_ctrl_st - Audio Control State Register offset
 *
 * @ctrl_regx: individual register bits
 * @ctrl_val: full register value
 *
 */
union aud_ctrl_st {
	struct {
		u32 ram_addr:4;
		u32 eld_ack:1;
		u32 eld_addr:4;
		u32 eld_buf_size:5;
		u32 eld_valid:1;
		u32 cp_ready:1;
		u32 dip_freq:2;
		u32 dip_idx:3;
		u32 dip_en_sta:4;
		u32 rsvd:7;
	} ctrl_regx;
	u32 ctrl_val;
};

/**
 * union aud_info_frame1 - Audio HDMI Widget Data Island Packet offset
 *
 * @fr1_regx: individual register bits
 * @fr1_val: full register value
 *
 */
union aud_info_frame1 {
	struct {
		u32 pkt_type:8;
		u32 ver_num:8;
		u32 len:5;
		u32 rsvd:11;
	} fr1_regx;
	u32 fr1_val;
};

/**
 * union aud_info_frame2 - DIP frame 2
 *
 * @fr2_regx: individual register bits
 * @fr2_val: full register value
 *
 */
union aud_info_frame2 {
	struct {
		u32 chksum:8;
		u32 chnl_cnt:3;
		u32 rsvd0:1;
		u32 coding_type:4;
		u32 smpl_size:2;
		u32 smpl_freq:3;
		u32 rsvd1:3;
		u32 format:8;
	} fr2_regx;
	u32 fr2_val;
};

/**
 * union aud_info_frame3 - DIP frame 3
 *
 * @fr3_regx: individual register bits
 * @fr3_val: full register value
 *
 */
union aud_info_frame3 {
	struct {
		u32 chnl_alloc:8;
		u32 rsvd0:3;
		u32 lsv:4;
		u32 dm_inh:1;
		u32 rsvd1:16;
	} fr3_regx;
	u32 fr3_val;
};


struct pcm_stream_info {
	int		str_id;
	void		*had_substream;
	void		(*period_elapsed)(void *had_substream);
	u32		buffer_ptr;
	u64		buffer_rendered;
	u32		ring_buf_size;
	int		sfreq;
};

struct ring_buf_info {
	uint32_t	buf_addr;
	uint32_t	buf_size;
	uint8_t		is_valid;
};

struct had_stream_pvt {
	enum had_stream_status		stream_status;
	int				stream_ops;
	ssize_t				dbg_cum_bytes;
};

struct had_pvt_data {
	enum had_status_stream		stream_type;
};

struct had_callback_ops {
	had_event_call_back intel_had_event_call_back;
};

/**
 * struct snd_intelhad - intelhad driver structure
 *
 * @card: ptr to hold card details
 * @card_index: sound card index
 * @card_id: detected sound card id
 * @reg_ops: register operations to program registers
 * @query_ops: caps call backs for get/set operations
 * @drv_status: driver status
 * @buf_info: ring buffer info
 * @stream_info: stream information
 * @eeld: holds EELD info
 * @curr_buf: pointer to hold current active ring buf
 * @valid_buf_cnt: ring buffer count for stream
 * @had_spinlock: driver lock
 * @aes_bits: IEC958 status bits
 * @buff_done: id of current buffer done intr
 * @dev: platoform device handle
 * @kctl: holds kctl ptrs used for channel map
 * @chmap: holds channel map info
 * @audio_reg_base: hdmi audio register base offset
 * @hw_silence: flag indicates SoC support for HW silence/Keep alive
 * @ops: holds ops functions based on platform
 */
struct snd_intelhad {
	struct snd_card	*card;
	int		card_index;
	char		*card_id;
	struct hdmi_audio_registers_ops	reg_ops;
	struct hdmi_audio_query_set_ops	query_ops;
	enum had_drv_status	drv_status;
	struct		ring_buf_info buf_info[HAD_NUM_OF_RING_BUFS];
	struct		pcm_stream_info stream_info;
	otm_hdmi_eld_t	eeld;
	enum		intel_had_aud_buf_type curr_buf;
	int		valid_buf_cnt;
	unsigned int	aes_bits;
	int flag_underrun;
	struct had_pvt_data *private_data;
	spinlock_t had_spinlock;
	enum		intel_had_aud_buf_type buff_done;
	struct device *dev;
	struct snd_kcontrol *kctl;
	struct snd_pcm_chmap *chmap;
	unsigned int	audio_reg_base;
	bool		hw_silence;
	struct had_ops	*ops;
};

struct had_ops {
	void (*enable_audio)(struct snd_pcm_substream *substream,
			u8 enable);
	void (*reset_audio)(u8 reset);
	int (*prog_n)(u32 aud_samp_freq, u32 *n_param,
			struct snd_intelhad *intelhaddata);
	void (*prog_cts)(u32 aud_samp_freq, u32 tmds, u32 n_param,
			struct snd_intelhad *intelhaddata);
	int (*audio_ctrl)(struct snd_pcm_substream *substream,
				struct snd_intelhad *intelhaddata);
	void (*prog_dip)(struct snd_pcm_substream *substream,
				struct snd_intelhad *intelhaddata);
	void (*handle_underrun)(struct snd_intelhad *intelhaddata);
};


int had_event_handler(enum had_event_type event_type, void *data);

int hdmi_audio_query(void *drv_data, hdmi_audio_event_t event);
int hdmi_audio_suspend(void *drv_data, hdmi_audio_event_t event);
int hdmi_audio_resume(void *drv_data);
int hdmi_audio_mode_change(struct snd_pcm_substream *substream);
extern struct snd_pcm_ops snd_intelhad_playback_ops;

int snd_intelhad_init_audio_ctrl(struct snd_pcm_substream *substream,
					struct snd_intelhad *intelhaddata,
					int flag_silence);
int snd_intelhad_prog_buffer(struct snd_intelhad *intelhaddata,
					int start, int end);
int snd_intelhad_invd_buffer(int start, int end);
int snd_intelhad_read_len(struct snd_intelhad *intelhaddata);
void had_build_channel_allocation_map(struct snd_intelhad *intelhaddata);

/* Register access functions */
int had_get_hwstate(struct snd_intelhad *intelhaddata);
int had_get_caps(enum had_caps_list query_element, void *capabilties);
int had_set_caps(enum had_caps_list set_element, void *capabilties);
int had_read_register(uint32_t reg_addr, uint32_t *data);
int had_write_register(uint32_t reg_addr, uint32_t data);
int had_read_modify(uint32_t reg_addr, uint32_t data, uint32_t mask);
#endif
