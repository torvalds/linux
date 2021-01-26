/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __Q6AFE_H__
#define __Q6AFE_H__

#include <dt-bindings/sound/qcom,q6afe.h>

#define AFE_PORT_MAX		127

#define MSM_AFE_PORT_TYPE_RX 0
#define MSM_AFE_PORT_TYPE_TX 1
#define AFE_MAX_PORTS AFE_PORT_MAX

#define Q6AFE_MAX_MI2S_LINES	4

#define AFE_MAX_CHAN_COUNT	8
#define AFE_PORT_MAX_AUDIO_CHAN_CNT	0x8

#define Q6AFE_LPASS_CLK_SRC_INTERNAL 1
#define Q6AFE_LPASS_CLK_ROOT_DEFAULT 0

#define LPAIF_DIG_CLK	1
#define LPAIF_BIT_CLK	2
#define LPAIF_OSR_CLK	3

/* Clock ID for Primary I2S IBIT */
#define Q6AFE_LPASS_CLK_ID_PRI_MI2S_IBIT                          0x100
/* Clock ID for Primary I2S EBIT */
#define Q6AFE_LPASS_CLK_ID_PRI_MI2S_EBIT                          0x101
/* Clock ID for Secondary I2S IBIT */
#define Q6AFE_LPASS_CLK_ID_SEC_MI2S_IBIT                          0x102
/* Clock ID for Secondary I2S EBIT */
#define Q6AFE_LPASS_CLK_ID_SEC_MI2S_EBIT                          0x103
/* Clock ID for Tertiary I2S IBIT */
#define Q6AFE_LPASS_CLK_ID_TER_MI2S_IBIT                          0x104
/* Clock ID for Tertiary I2S EBIT */
#define Q6AFE_LPASS_CLK_ID_TER_MI2S_EBIT                          0x105
/* Clock ID for Quartnery I2S IBIT */
#define Q6AFE_LPASS_CLK_ID_QUAD_MI2S_IBIT                         0x106
/* Clock ID for Quartnery I2S EBIT */
#define Q6AFE_LPASS_CLK_ID_QUAD_MI2S_EBIT                         0x107
/* Clock ID for Speaker I2S IBIT */
#define Q6AFE_LPASS_CLK_ID_SPEAKER_I2S_IBIT                       0x108
/* Clock ID for Speaker I2S EBIT */
#define Q6AFE_LPASS_CLK_ID_SPEAKER_I2S_EBIT                       0x109
/* Clock ID for Speaker I2S OSR */
#define Q6AFE_LPASS_CLK_ID_SPEAKER_I2S_OSR                        0x10A

/* Clock ID for QUINARY  I2S IBIT */
#define Q6AFE_LPASS_CLK_ID_QUI_MI2S_IBIT			0x10B
/* Clock ID for QUINARY  I2S EBIT */
#define Q6AFE_LPASS_CLK_ID_QUI_MI2S_EBIT			0x10C
/* Clock ID for SENARY  I2S IBIT */
#define Q6AFE_LPASS_CLK_ID_SEN_MI2S_IBIT			0x10D
/* Clock ID for SENARY  I2S EBIT */
#define Q6AFE_LPASS_CLK_ID_SEN_MI2S_EBIT			0x10E
/* Clock ID for INT0 I2S IBIT  */
#define Q6AFE_LPASS_CLK_ID_INT0_MI2S_IBIT                       0x10F
/* Clock ID for INT1 I2S IBIT  */
#define Q6AFE_LPASS_CLK_ID_INT1_MI2S_IBIT                       0x110
/* Clock ID for INT2 I2S IBIT  */
#define Q6AFE_LPASS_CLK_ID_INT2_MI2S_IBIT                       0x111
/* Clock ID for INT3 I2S IBIT  */
#define Q6AFE_LPASS_CLK_ID_INT3_MI2S_IBIT                       0x112
/* Clock ID for INT4 I2S IBIT  */
#define Q6AFE_LPASS_CLK_ID_INT4_MI2S_IBIT                       0x113
/* Clock ID for INT5 I2S IBIT  */
#define Q6AFE_LPASS_CLK_ID_INT5_MI2S_IBIT                       0x114
/* Clock ID for INT6 I2S IBIT  */
#define Q6AFE_LPASS_CLK_ID_INT6_MI2S_IBIT                       0x115

/* Clock ID for QUINARY MI2S OSR CLK  */
#define Q6AFE_LPASS_CLK_ID_QUI_MI2S_OSR                         0x116

/* Clock ID for Primary PCM IBIT */
#define Q6AFE_LPASS_CLK_ID_PRI_PCM_IBIT                           0x200
/* Clock ID for Primary PCM EBIT */
#define Q6AFE_LPASS_CLK_ID_PRI_PCM_EBIT                           0x201
/* Clock ID for Secondary PCM IBIT */
#define Q6AFE_LPASS_CLK_ID_SEC_PCM_IBIT                           0x202
/* Clock ID for Secondary PCM EBIT */
#define Q6AFE_LPASS_CLK_ID_SEC_PCM_EBIT                           0x203
/* Clock ID for Tertiary PCM IBIT */
#define Q6AFE_LPASS_CLK_ID_TER_PCM_IBIT                           0x204
/* Clock ID for Tertiary PCM EBIT */
#define Q6AFE_LPASS_CLK_ID_TER_PCM_EBIT                           0x205
/* Clock ID for Quartery PCM IBIT */
#define Q6AFE_LPASS_CLK_ID_QUAD_PCM_IBIT                          0x206
/* Clock ID for Quartery PCM EBIT */
#define Q6AFE_LPASS_CLK_ID_QUAD_PCM_EBIT                          0x207
/* Clock ID for Quinary PCM IBIT */
#define Q6AFE_LPASS_CLK_ID_QUIN_PCM_IBIT                          0x208
/* Clock ID for Quinary PCM EBIT */
#define Q6AFE_LPASS_CLK_ID_QUIN_PCM_EBIT                          0x209
/* Clock ID for QUINARY PCM OSR  */
#define Q6AFE_LPASS_CLK_ID_QUI_PCM_OSR                            0x20A

/** Clock ID for Primary TDM IBIT */
#define Q6AFE_LPASS_CLK_ID_PRI_TDM_IBIT                           0x200
/** Clock ID for Primary TDM EBIT */
#define Q6AFE_LPASS_CLK_ID_PRI_TDM_EBIT                           0x201
/** Clock ID for Secondary TDM IBIT */
#define Q6AFE_LPASS_CLK_ID_SEC_TDM_IBIT                           0x202
/** Clock ID for Secondary TDM EBIT */
#define Q6AFE_LPASS_CLK_ID_SEC_TDM_EBIT                           0x203
/** Clock ID for Tertiary TDM IBIT */
#define Q6AFE_LPASS_CLK_ID_TER_TDM_IBIT                           0x204
/** Clock ID for Tertiary TDM EBIT */
#define Q6AFE_LPASS_CLK_ID_TER_TDM_EBIT                           0x205
/** Clock ID for Quartery TDM IBIT */
#define Q6AFE_LPASS_CLK_ID_QUAD_TDM_IBIT                          0x206
/** Clock ID for Quartery TDM EBIT */
#define Q6AFE_LPASS_CLK_ID_QUAD_TDM_EBIT                          0x207
/** Clock ID for Quinary TDM IBIT */
#define Q6AFE_LPASS_CLK_ID_QUIN_TDM_IBIT                          0x208
/** Clock ID for Quinary TDM EBIT */
#define Q6AFE_LPASS_CLK_ID_QUIN_TDM_EBIT                          0x209
/** Clock ID for Quinary TDM OSR */
#define Q6AFE_LPASS_CLK_ID_QUIN_TDM_OSR                           0x20A

/* Clock ID for MCLK1 */
#define Q6AFE_LPASS_CLK_ID_MCLK_1                                 0x300
/* Clock ID for MCLK2 */
#define Q6AFE_LPASS_CLK_ID_MCLK_2                                 0x301
/* Clock ID for MCLK3 */
#define Q6AFE_LPASS_CLK_ID_MCLK_3                                 0x302
/* Clock ID for MCLK4 */
#define Q6AFE_LPASS_CLK_ID_MCLK_4                                 0x304
/* Clock ID for Internal Digital Codec Core */
#define Q6AFE_LPASS_CLK_ID_INTERNAL_DIGITAL_CODEC_CORE            0x303
/* Clock ID for INT MCLK0 */
#define Q6AFE_LPASS_CLK_ID_INT_MCLK_0                             0x305
/* Clock ID for INT MCLK1 */
#define Q6AFE_LPASS_CLK_ID_INT_MCLK_1                             0x306

#define Q6AFE_LPASS_CLK_ID_WSA_CORE_MCLK			0x309
#define Q6AFE_LPASS_CLK_ID_WSA_CORE_NPL_MCLK			0x30a
#define Q6AFE_LPASS_CLK_ID_TX_CORE_MCLK				0x30c
#define Q6AFE_LPASS_CLK_ID_TX_CORE_NPL_MCLK			0x30d
#define Q6AFE_LPASS_CLK_ID_RX_CORE_MCLK				0x30e
#define Q6AFE_LPASS_CLK_ID_RX_CORE_NPL_MCLK			0x30f
#define Q6AFE_LPASS_CLK_ID_VA_CORE_MCLK				0x30b
#define Q6AFE_LPASS_CLK_ID_VA_CORE_2X_MCLK			0x310

#define Q6AFE_LPASS_CORE_AVTIMER_BLOCK			0x2
#define Q6AFE_LPASS_CORE_HW_MACRO_BLOCK			0x3
#define Q6AFE_LPASS_CORE_HW_DCODEC_BLOCK		0x4

/* Clock attribute for invalid use (reserved for internal usage) */
#define Q6AFE_LPASS_CLK_ATTRIBUTE_INVALID		0x0
/* Clock attribute for no couple case */
#define Q6AFE_LPASS_CLK_ATTRIBUTE_COUPLE_NO		0x1
/* Clock attribute for dividend couple case */
#define Q6AFE_LPASS_CLK_ATTRIBUTE_COUPLE_DIVIDEND	0x2
/* Clock attribute for divisor couple case */
#define Q6AFE_LPASS_CLK_ATTRIBUTE_COUPLE_DIVISOR	0x3
/* Clock attribute for invert and no couple case */
#define Q6AFE_LPASS_CLK_ATTRIBUTE_INVERT_COUPLE_NO	0x4

#define Q6AFE_CMAP_INVALID		0xFFFF

struct q6afe_hdmi_cfg {
	u16                  datatype;
	u16                  channel_allocation;
	u32                  sample_rate;
	u16                  bit_width;
};

struct q6afe_slim_cfg {
	u32	sample_rate;
	u16	bit_width;
	u16	data_format;
	u16	num_channels;
	u8	ch_mapping[AFE_MAX_CHAN_COUNT];
};

struct q6afe_i2s_cfg {
	u32	sample_rate;
	u16	bit_width;
	u16	data_format;
	u16	num_channels;
	u32	sd_line_mask;
	int fmt;
};

struct q6afe_tdm_cfg {
	u16	num_channels;
	u32	sample_rate;
	u16	bit_width;
	u16	data_format;
	u16	sync_mode;
	u16	sync_src;
	u16	nslots_per_frame;
	u16	slot_width;
	u16	slot_mask;
	u32	data_align_type;
	u16	ch_mapping[AFE_MAX_CHAN_COUNT];
};

struct q6afe_cdc_dma_cfg {
	u16	sample_rate;
	u16	bit_width;
	u16	data_format;
	u16	num_channels;
	u16	active_channels_mask;
};


struct q6afe_port_config {
	struct q6afe_hdmi_cfg hdmi;
	struct q6afe_slim_cfg slim;
	struct q6afe_i2s_cfg i2s_cfg;
	struct q6afe_tdm_cfg tdm;
	struct q6afe_cdc_dma_cfg dma_cfg;
};

struct q6afe_port;

struct q6afe_port *q6afe_port_get_from_id(struct device *dev, int id);
int q6afe_port_start(struct q6afe_port *port);
int q6afe_port_stop(struct q6afe_port *port);
void q6afe_port_put(struct q6afe_port *port);
int q6afe_get_port_id(int index);
void q6afe_hdmi_port_prepare(struct q6afe_port *port,
			    struct q6afe_hdmi_cfg *cfg);
void q6afe_slim_port_prepare(struct q6afe_port *port,
			  struct q6afe_slim_cfg *cfg);
int q6afe_i2s_port_prepare(struct q6afe_port *port, struct q6afe_i2s_cfg *cfg);
void q6afe_tdm_port_prepare(struct q6afe_port *port, struct q6afe_tdm_cfg *cfg);
void q6afe_cdc_dma_port_prepare(struct q6afe_port *port,
				struct q6afe_cdc_dma_cfg *cfg);

int q6afe_port_set_sysclk(struct q6afe_port *port, int clk_id,
			  int clk_src, int clk_root,
			  unsigned int freq, int dir);
int q6afe_set_lpass_clock(struct device *dev, int clk_id, int clk_src,
			  int clk_root, unsigned int freq);
int q6afe_vote_lpass_core_hw(struct device *dev, uint32_t hw_block_id,
			     char *client_name, uint32_t *client_handle);
int q6afe_unvote_lpass_core_hw(struct device *dev, uint32_t hw_block_id,
			       uint32_t client_handle);
#endif /* __Q6AFE_H__ */
