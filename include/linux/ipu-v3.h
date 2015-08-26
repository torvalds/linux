/*
 * Copyright (c) 2010 Sascha Hauer <s.hauer@pengutronix.de>
 * Copyright (C) 2011-2015 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#ifndef __LINUX_IPU_V3_H_
#define __LINUX_IPU_V3_H_

#include <linux/ipu.h>

/* IPU Driver channels definitions.	*/
/* Note these are different from IDMA channels */
#define IPU_MAX_CH	32
#define _MAKE_CHAN(num, v_in, g_in, a_in, out) \
	((num << 24) | (v_in << 18) | (g_in << 12) | (a_in << 6) | out)
#define _MAKE_ALT_CHAN(ch)		(ch | (IPU_MAX_CH << 24))
#define IPU_CHAN_ID(ch)			(ch >> 24)
#define IPU_CHAN_ALT(ch)		(ch & 0x02000000)
#define IPU_CHAN_ALPHA_IN_DMA(ch)	((uint32_t) (ch >> 6) & 0x3F)
#define IPU_CHAN_GRAPH_IN_DMA(ch)	((uint32_t) (ch >> 12) & 0x3F)
#define IPU_CHAN_VIDEO_IN_DMA(ch)	((uint32_t) (ch >> 18) & 0x3F)
#define IPU_CHAN_OUT_DMA(ch)		((uint32_t) (ch & 0x3F))
#define NO_DMA 0x3F
#define ALT	1
/*!
 * Enumeration of IPU logical channels. An IPU logical channel is defined as a
 * combination of an input (memory to IPU), output (IPU to memory), and/or
 * secondary input IDMA channels and in some cases an Image Converter task.
 * Some channels consist of only an input or output.
 */
typedef enum {
	CHAN_NONE = -1,
	MEM_ROT_ENC_MEM = _MAKE_CHAN(1, 45, NO_DMA, NO_DMA, 48),
	MEM_ROT_VF_MEM = _MAKE_CHAN(2, 46, NO_DMA, NO_DMA, 49),
	MEM_ROT_PP_MEM = _MAKE_CHAN(3, 47, NO_DMA, NO_DMA, 50),

	MEM_PRP_ENC_MEM = _MAKE_CHAN(4, 12, 14, 17, 20),
	MEM_PRP_VF_MEM = _MAKE_CHAN(5, 12, 14, 17, 21),
	MEM_PP_MEM = _MAKE_CHAN(6, 11, 15, 18, 22),

	MEM_DC_SYNC = _MAKE_CHAN(7, 28, NO_DMA, NO_DMA, NO_DMA),
	MEM_DC_ASYNC = _MAKE_CHAN(8, 41, NO_DMA, NO_DMA, NO_DMA),
	MEM_BG_SYNC = _MAKE_CHAN(9, 23, NO_DMA, 51, NO_DMA),
	MEM_FG_SYNC = _MAKE_CHAN(10, 27, NO_DMA, 31, NO_DMA),

	MEM_BG_ASYNC0 = _MAKE_CHAN(11, 24, NO_DMA, 52, NO_DMA),
	MEM_FG_ASYNC0 = _MAKE_CHAN(12, 29, NO_DMA, 33, NO_DMA),
	MEM_BG_ASYNC1 = _MAKE_ALT_CHAN(MEM_BG_ASYNC0),
	MEM_FG_ASYNC1 = _MAKE_ALT_CHAN(MEM_FG_ASYNC0),

	DIRECT_ASYNC0 = _MAKE_CHAN(13, NO_DMA, NO_DMA, NO_DMA, NO_DMA),
	DIRECT_ASYNC1 = _MAKE_CHAN(14, NO_DMA, NO_DMA, NO_DMA, NO_DMA),

	CSI_MEM0 = _MAKE_CHAN(15, NO_DMA, NO_DMA, NO_DMA, 0),
	CSI_MEM1 = _MAKE_CHAN(16, NO_DMA, NO_DMA, NO_DMA, 1),
	CSI_MEM2 = _MAKE_CHAN(17, NO_DMA, NO_DMA, NO_DMA, 2),
	CSI_MEM3 = _MAKE_CHAN(18, NO_DMA, NO_DMA, NO_DMA, 3),

	CSI_MEM = CSI_MEM0,

	CSI_PRP_ENC_MEM = _MAKE_CHAN(19, NO_DMA, NO_DMA, NO_DMA, 20),
	CSI_PRP_VF_MEM = _MAKE_CHAN(20, NO_DMA, NO_DMA, NO_DMA, 21),

	/* for vdi mem->vdi->ic->mem , add graphics plane and alpha*/
	MEM_VDI_PRP_VF_MEM_P = _MAKE_CHAN(21, 8, 14, 17, 21),
	MEM_VDI_PRP_VF_MEM = _MAKE_CHAN(22, 9, 14, 17, 21),
	MEM_VDI_PRP_VF_MEM_N = _MAKE_CHAN(23, 10, 14, 17, 21),

	/* for vdi mem->vdi->mem */
	MEM_VDI_MEM_P = _MAKE_CHAN(24, 8, NO_DMA, NO_DMA, 5),
	MEM_VDI_MEM = _MAKE_CHAN(25, 9, NO_DMA, NO_DMA, 5),
	MEM_VDI_MEM_N = _MAKE_CHAN(26, 10, NO_DMA, NO_DMA, 5),

	/* fake channel for vdoa to link with IPU */
	MEM_VDOA_MEM =  _MAKE_CHAN(27, NO_DMA, NO_DMA, NO_DMA, NO_DMA),

	MEM_PP_ADC = CHAN_NONE,
	ADC_SYS2 = CHAN_NONE,

} ipu_channel_t;

/*!
 * Enumeration of types of buffers for a logical channel.
 */
typedef enum {
	IPU_OUTPUT_BUFFER = 0,	/*!< Buffer for output from IPU */
	IPU_ALPHA_IN_BUFFER = 1,	/*!< Buffer for input to IPU */
	IPU_GRAPH_IN_BUFFER = 2,	/*!< Buffer for input to IPU */
	IPU_VIDEO_IN_BUFFER = 3,	/*!< Buffer for input to IPU */
	IPU_INPUT_BUFFER = IPU_VIDEO_IN_BUFFER,
	IPU_SEC_INPUT_BUFFER = IPU_GRAPH_IN_BUFFER,
} ipu_buffer_t;

#define IPU_PANEL_SERIAL		1
#define IPU_PANEL_PARALLEL		2

/*!
 * Enumeration of ADC channel operation mode.
 */
typedef enum {
	Disable,
	WriteTemplateNonSeq,
	ReadTemplateNonSeq,
	WriteTemplateUnCon,
	ReadTemplateUnCon,
	WriteDataWithRS,
	WriteDataWoRS,
	WriteCmd
} mcu_mode_t;

/*!
 * Enumeration of ADC channel addressing mode.
 */
typedef enum {
	FullWoBE,
	FullWithBE,
	XY
} display_addressing_t;

/*!
 * Union of initialization parameters for a logical channel.
 */
typedef union {
	struct {
		uint32_t csi;
		uint32_t mipi_id;
		uint32_t mipi_vc;
		bool mipi_en;
		bool interlaced;
	} csi_mem;
	struct {
		uint32_t in_width;
		uint32_t in_height;
		uint32_t in_pixel_fmt;
		uint32_t out_width;
		uint32_t out_height;
		uint32_t out_pixel_fmt;
		uint32_t outh_resize_ratio;
		uint32_t outv_resize_ratio;
		uint32_t csi;
		uint32_t mipi_id;
		uint32_t mipi_vc;
		bool mipi_en;
	} csi_prp_enc_mem;
	struct {
		uint32_t in_width;
		uint32_t in_height;
		uint32_t in_pixel_fmt;
		uint32_t out_width;
		uint32_t out_height;
		uint32_t out_pixel_fmt;
		uint32_t outh_resize_ratio;
		uint32_t outv_resize_ratio;
	} mem_prp_enc_mem;
	struct {
		uint32_t in_width;
		uint32_t in_height;
		uint32_t in_pixel_fmt;
		uint32_t out_width;
		uint32_t out_height;
		uint32_t out_pixel_fmt;
	} mem_rot_enc_mem;
	struct {
		uint32_t in_width;
		uint32_t in_height;
		uint32_t in_pixel_fmt;
		uint32_t out_width;
		uint32_t out_height;
		uint32_t out_pixel_fmt;
		uint32_t outh_resize_ratio;
		uint32_t outv_resize_ratio;
		bool graphics_combine_en;
		bool global_alpha_en;
		bool key_color_en;
		uint32_t in_g_pixel_fmt;
		uint8_t alpha;
		uint32_t key_color;
		bool alpha_chan_en;
		ipu_motion_sel motion_sel;
		enum v4l2_field field_fmt;
		uint32_t csi;
		uint32_t mipi_id;
		uint32_t mipi_vc;
		bool mipi_en;
	} csi_prp_vf_mem;
	struct {
		uint32_t in_width;
		uint32_t in_height;
		uint32_t in_pixel_fmt;
		uint32_t out_width;
		uint32_t out_height;
		uint32_t out_pixel_fmt;
		bool graphics_combine_en;
		bool global_alpha_en;
		bool key_color_en;
		display_port_t disp;
		uint32_t out_left;
		uint32_t out_top;
	} csi_prp_vf_adc;
	struct {
		uint32_t in_width;
		uint32_t in_height;
		uint32_t in_pixel_fmt;
		uint32_t out_width;
		uint32_t out_height;
		uint32_t out_pixel_fmt;
		uint32_t outh_resize_ratio;
		uint32_t outv_resize_ratio;
		bool graphics_combine_en;
		bool global_alpha_en;
		bool key_color_en;
		uint32_t in_g_pixel_fmt;
		uint8_t alpha;
		uint32_t key_color;
		bool alpha_chan_en;
		ipu_motion_sel motion_sel;
		enum v4l2_field field_fmt;
	} mem_prp_vf_mem;
	struct {
		uint32_t temp;
	} mem_prp_vf_adc;
	struct {
		uint32_t temp;
	} mem_rot_vf_mem;
	struct {
		uint32_t in_width;
		uint32_t in_height;
		uint32_t in_pixel_fmt;
		uint32_t out_width;
		uint32_t out_height;
		uint32_t out_pixel_fmt;
		uint32_t outh_resize_ratio;
		uint32_t outv_resize_ratio;
		bool graphics_combine_en;
		bool global_alpha_en;
		bool key_color_en;
		uint32_t in_g_pixel_fmt;
		uint8_t alpha;
		uint32_t key_color;
		bool alpha_chan_en;
	} mem_pp_mem;
	struct {
		uint32_t temp;
	} mem_rot_mem;
	struct {
		uint32_t in_width;
		uint32_t in_height;
		uint32_t in_pixel_fmt;
		uint32_t out_width;
		uint32_t out_height;
		uint32_t out_pixel_fmt;
		bool graphics_combine_en;
		bool global_alpha_en;
		bool key_color_en;
		display_port_t disp;
		uint32_t out_left;
		uint32_t out_top;
	} mem_pp_adc;
	struct {
		uint32_t di;
		bool interlaced;
		uint32_t in_pixel_fmt;
		uint32_t out_pixel_fmt;
	} mem_dc_sync;
	struct {
		uint32_t temp;
	} mem_sdc_fg;
	struct {
		uint32_t di;
		bool interlaced;
		uint32_t in_pixel_fmt;
		uint32_t out_pixel_fmt;
		bool alpha_chan_en;
	} mem_dp_bg_sync;
	struct {
		uint32_t temp;
	} mem_sdc_bg;
	struct {
		uint32_t di;
		bool interlaced;
		uint32_t in_pixel_fmt;
		uint32_t out_pixel_fmt;
		bool alpha_chan_en;
	} mem_dp_fg_sync;
	struct {
		uint32_t di;
	} direct_async;
	struct {
		display_port_t disp;
		mcu_mode_t ch_mode;
		uint32_t out_left;
		uint32_t out_top;
	} adc_sys1;
	struct {
		display_port_t disp;
		mcu_mode_t ch_mode;
		uint32_t out_left;
		uint32_t out_top;
	} adc_sys2;
} ipu_channel_params_t;

/*
 * IPU_IRQF_ONESHOT - Interrupt is not reenabled after the irq handler finished.
 */
#define IPU_IRQF_NONE		0x00000000
#define IPU_IRQF_ONESHOT	0x00000001

/*!
 * Enumeration of IPU interrupt sources.
 */
enum ipu_irq_line {
	IPU_IRQ_CSI0_OUT_EOF = 0,
	IPU_IRQ_CSI1_OUT_EOF = 1,
	IPU_IRQ_CSI2_OUT_EOF = 2,
	IPU_IRQ_CSI3_OUT_EOF = 3,
	IPU_IRQ_VDIC_OUT_EOF = 5,
	IPU_IRQ_VDI_P_IN_EOF = 8,
	IPU_IRQ_VDI_C_IN_EOF = 9,
	IPU_IRQ_VDI_N_IN_EOF = 10,
	IPU_IRQ_PP_IN_EOF = 11,
	IPU_IRQ_PRP_IN_EOF = 12,
	IPU_IRQ_PRP_GRAPH_IN_EOF = 14,
	IPU_IRQ_PP_GRAPH_IN_EOF = 15,
	IPU_IRQ_PRP_ALPHA_IN_EOF = 17,
	IPU_IRQ_PP_ALPHA_IN_EOF = 18,
	IPU_IRQ_PRP_ENC_OUT_EOF = 20,
	IPU_IRQ_PRP_VF_OUT_EOF = 21,
	IPU_IRQ_PP_OUT_EOF = 22,
	IPU_IRQ_BG_SYNC_EOF = 23,
	IPU_IRQ_BG_ASYNC_EOF = 24,
	IPU_IRQ_FG_SYNC_EOF = 27,
	IPU_IRQ_DC_SYNC_EOF = 28,
	IPU_IRQ_FG_ASYNC_EOF = 29,
	IPU_IRQ_FG_ALPHA_SYNC_EOF = 31,

	IPU_IRQ_FG_ALPHA_ASYNC_EOF = 33,
	IPU_IRQ_DC_READ_EOF = 40,
	IPU_IRQ_DC_ASYNC_EOF = 41,
	IPU_IRQ_DC_CMD1_EOF = 42,
	IPU_IRQ_DC_CMD2_EOF = 43,
	IPU_IRQ_DC_MASK_EOF = 44,
	IPU_IRQ_PRP_ENC_ROT_IN_EOF = 45,
	IPU_IRQ_PRP_VF_ROT_IN_EOF = 46,
	IPU_IRQ_PP_ROT_IN_EOF = 47,
	IPU_IRQ_PRP_ENC_ROT_OUT_EOF = 48,
	IPU_IRQ_PRP_VF_ROT_OUT_EOF = 49,
	IPU_IRQ_PP_ROT_OUT_EOF = 50,
	IPU_IRQ_BG_ALPHA_SYNC_EOF = 51,
	IPU_IRQ_BG_ALPHA_ASYNC_EOF = 52,

	IPU_IRQ_BG_SYNC_NFACK = 64 + 23,
	IPU_IRQ_FG_SYNC_NFACK = 64 + 27,
	IPU_IRQ_DC_SYNC_NFACK = 64 + 28,

	IPU_IRQ_DP_SF_START = 448 + 2,
	IPU_IRQ_DP_SF_END = 448 + 3,
	IPU_IRQ_BG_SF_END = IPU_IRQ_DP_SF_END,
	IPU_IRQ_DC_FC_0 = 448 + 8,
	IPU_IRQ_DC_FC_1 = 448 + 9,
	IPU_IRQ_DC_FC_2 = 448 + 10,
	IPU_IRQ_DC_FC_3 = 448 + 11,
	IPU_IRQ_DC_FC_4 = 448 + 12,
	IPU_IRQ_DC_FC_6 = 448 + 13,
	IPU_IRQ_VSYNC_PRE_0 = 448 + 14,
	IPU_IRQ_VSYNC_PRE_1 = 448 + 15,

	IPU_IRQ_COUNT
};

/*!
 * Bitfield of Display Interface signal polarities.
 */
typedef struct {
	unsigned datamask_en:1;
	unsigned int_clk:1;
	unsigned interlaced:1;
	unsigned odd_field_first:1;
	unsigned clksel_en:1;
	unsigned clkidle_en:1;
	unsigned data_pol:1;	/* true = inverted */
	unsigned clk_pol:1;	/* true = rising edge */
	unsigned enable_pol:1;
	unsigned Hsync_pol:1;	/* true = active high */
	unsigned Vsync_pol:1;
} ipu_di_signal_cfg_t;

/*!
 * Bitfield of CSI signal polarities and modes.
 */

typedef struct {
	unsigned data_width:4;
	unsigned clk_mode:3;
	unsigned ext_vsync:1;
	unsigned Vsync_pol:1;
	unsigned Hsync_pol:1;
	unsigned pixclk_pol:1;
	unsigned data_pol:1;
	unsigned sens_clksrc:1;
	unsigned pack_tight:1;
	unsigned force_eof:1;
	unsigned data_en_pol:1;
	unsigned data_fmt;
	unsigned csi;
	unsigned mclk;
} ipu_csi_signal_cfg_t;

/*!
 * Enumeration of CSI data bus widths.
 */
enum {
	IPU_CSI_DATA_WIDTH_4 = 0,
	IPU_CSI_DATA_WIDTH_8 = 1,
	IPU_CSI_DATA_WIDTH_10 = 3,
	IPU_CSI_DATA_WIDTH_16 = 9,
};

/*!
 * Enumeration of CSI clock modes.
 */
enum {
	IPU_CSI_CLK_MODE_GATED_CLK,
	IPU_CSI_CLK_MODE_NONGATED_CLK,
	IPU_CSI_CLK_MODE_CCIR656_PROGRESSIVE,
	IPU_CSI_CLK_MODE_CCIR656_INTERLACED,
	IPU_CSI_CLK_MODE_CCIR1120_PROGRESSIVE_DDR,
	IPU_CSI_CLK_MODE_CCIR1120_PROGRESSIVE_SDR,
	IPU_CSI_CLK_MODE_CCIR1120_INTERLACED_DDR,
	IPU_CSI_CLK_MODE_CCIR1120_INTERLACED_SDR,
};

enum {
	IPU_CSI_MIPI_DI0,
	IPU_CSI_MIPI_DI1,
	IPU_CSI_MIPI_DI2,
	IPU_CSI_MIPI_DI3,
};

typedef enum {
	RGB,
	YCbCr,
	YUV
} ipu_color_space_t;

/*!
 * Enumeration of ADC vertical sync mode.
 */
typedef enum {
	VsyncNone,
	VsyncInternal,
	VsyncCSI,
	VsyncExternal
} vsync_t;

typedef enum {
	DAT,
	CMD
} cmddata_t;

/*!
 * Enumeration of ADC display update mode.
 */
typedef enum {
	IPU_ADC_REFRESH_NONE,
	IPU_ADC_AUTO_REFRESH,
	IPU_ADC_AUTO_REFRESH_SNOOP,
	IPU_ADC_SNOOPING,
} ipu_adc_update_mode_t;

/*!
 * Enumeration of ADC display interface types (serial or parallel).
 */
enum {
	IPU_ADC_IFC_MODE_SYS80_TYPE1,
	IPU_ADC_IFC_MODE_SYS80_TYPE2,
	IPU_ADC_IFC_MODE_SYS68K_TYPE1,
	IPU_ADC_IFC_MODE_SYS68K_TYPE2,
	IPU_ADC_IFC_MODE_3WIRE_SERIAL,
	IPU_ADC_IFC_MODE_4WIRE_SERIAL,
	IPU_ADC_IFC_MODE_5WIRE_SERIAL_CLK,
	IPU_ADC_IFC_MODE_5WIRE_SERIAL_CS,
};

enum {
	IPU_ADC_IFC_WIDTH_8,
	IPU_ADC_IFC_WIDTH_16,
};

/*!
 * Enumeration of ADC display interface burst mode.
 */
enum {
	IPU_ADC_BURST_WCS,
	IPU_ADC_BURST_WBLCK,
	IPU_ADC_BURST_NONE,
	IPU_ADC_BURST_SERIAL,
};

/*!
 * Enumeration of ADC display interface RW signal timing modes.
 */
enum {
	IPU_ADC_SER_NO_RW,
	IPU_ADC_SER_RW_BEFORE_RS,
	IPU_ADC_SER_RW_AFTER_RS,
};

/*!
 * Bitfield of ADC signal polarities and modes.
 */
typedef struct {
	unsigned data_pol:1;
	unsigned clk_pol:1;
	unsigned cs_pol:1;
	unsigned rs_pol:1;
	unsigned addr_pol:1;
	unsigned read_pol:1;
	unsigned write_pol:1;
	unsigned Vsync_pol:1;
	unsigned burst_pol:1;
	unsigned burst_mode:2;
	unsigned ifc_mode:3;
	unsigned ifc_width:5;
	unsigned ser_preamble_len:4;
	unsigned ser_preamble:8;
	unsigned ser_rw_mode:2;
} ipu_adc_sig_cfg_t;

/*!
 * Enumeration of ADC template commands.
 */
enum {
	RD_DATA,
	RD_ACK,
	RD_WAIT,
	WR_XADDR,
	WR_YADDR,
	WR_ADDR,
	WR_CMND,
	WR_DATA,
};

/*!
 * Enumeration of ADC template command flow control.
 */
enum {
	SINGLE_STEP,
	PAUSE,
	STOP,
};


/*Define template constants*/
#define     ATM_ADDR_RANGE      0x20	/*offset address of DISP */
#define     TEMPLATE_BUF_SIZE   0x20	/*size of template */

/*!
 * Define to create ADC template command entry.
 */
#define ipu_adc_template_gen(oc, rs, fc, dat) (((rs) << 29) | ((fc) << 27) | \
			((oc) << 24) | (dat))

typedef struct {
	u32 reg;
	u32 value;
} ipu_lpmc_reg_t;

#define IPU_LPMC_REG_READ       0x80000000L

#define CSI_MCLK_VF  1
#define CSI_MCLK_ENC 2
#define CSI_MCLK_RAW 4
#define CSI_MCLK_I2C 8

struct ipu_soc;
/* Common IPU API */
struct ipu_soc *ipu_get_soc(int id);
int32_t ipu_init_channel(struct ipu_soc *ipu, ipu_channel_t channel, ipu_channel_params_t *params);
void ipu_uninit_channel(struct ipu_soc *ipu, ipu_channel_t channel);
void ipu_disable_hsp_clk(struct ipu_soc *ipu);

static inline bool ipu_can_rotate_in_place(ipu_rotate_mode_t rot)
{
#ifdef CONFIG_MXC_IPU_V3D
	return (rot < IPU_ROTATE_HORIZ_FLIP);
#else
	return (rot < IPU_ROTATE_90_RIGHT);
#endif
}

int32_t ipu_init_channel_buffer(struct ipu_soc *ipu, ipu_channel_t channel, ipu_buffer_t type,
				uint32_t pixel_fmt,
				uint16_t width, uint16_t height,
				uint32_t stride,
				ipu_rotate_mode_t rot_mode,
				dma_addr_t phyaddr_0, dma_addr_t phyaddr_1,
				dma_addr_t phyaddr_2,
				uint32_t u_offset, uint32_t v_offset);

int32_t ipu_update_channel_buffer(struct ipu_soc *ipu, ipu_channel_t channel, ipu_buffer_t type,
				  uint32_t bufNum, dma_addr_t phyaddr);

int32_t ipu_update_channel_offset(struct ipu_soc *ipu, ipu_channel_t channel, ipu_buffer_t type,
				uint32_t pixel_fmt,
				uint16_t width, uint16_t height,
				uint32_t stride,
				uint32_t u, uint32_t v,
				uint32_t vertical_offset, uint32_t horizontal_offset);

int32_t ipu_get_channel_offset(uint32_t pixel_fmt,
			       uint16_t width, uint16_t height,
			       uint32_t stride,
			       uint32_t u, uint32_t v,
			       uint32_t vertical_offset, uint32_t horizontal_offset,
			       uint32_t *u_offset, uint32_t *v_offset);

int32_t ipu_select_buffer(struct ipu_soc *ipu, ipu_channel_t channel,
			  ipu_buffer_t type, uint32_t bufNum);
int32_t ipu_select_multi_vdi_buffer(struct ipu_soc *ipu, uint32_t bufNum);

int32_t ipu_link_channels(struct ipu_soc *ipu, ipu_channel_t src_ch, ipu_channel_t dest_ch);
int32_t ipu_unlink_channels(struct ipu_soc *ipu, ipu_channel_t src_ch, ipu_channel_t dest_ch);

int32_t ipu_is_channel_busy(struct ipu_soc *ipu, ipu_channel_t channel);
int32_t ipu_check_buffer_ready(struct ipu_soc *ipu, ipu_channel_t channel, ipu_buffer_t type,
		uint32_t bufNum);
void ipu_clear_buffer_ready(struct ipu_soc *ipu, ipu_channel_t channel, ipu_buffer_t type,
		uint32_t bufNum);
uint32_t ipu_get_cur_buffer_idx(struct ipu_soc *ipu, ipu_channel_t channel, ipu_buffer_t type);
int32_t ipu_enable_channel(struct ipu_soc *ipu, ipu_channel_t channel);
int32_t ipu_disable_channel(struct ipu_soc *ipu, ipu_channel_t channel, bool wait_for_stop);
int32_t ipu_swap_channel(struct ipu_soc *ipu, ipu_channel_t from_ch, ipu_channel_t to_ch);
uint32_t ipu_channel_status(struct ipu_soc *ipu, ipu_channel_t channel);

int32_t ipu_enable_csi(struct ipu_soc *ipu, uint32_t csi);
int32_t ipu_disable_csi(struct ipu_soc *ipu, uint32_t csi);

int ipu_lowpwr_display_enable(void);
int ipu_lowpwr_display_disable(void);

int ipu_enable_irq(struct ipu_soc *ipu, uint32_t irq);
void ipu_disable_irq(struct ipu_soc *ipu, uint32_t irq);
void ipu_clear_irq(struct ipu_soc *ipu, uint32_t irq);
int ipu_request_irq(struct ipu_soc *ipu, uint32_t irq,
		    irqreturn_t(*handler) (int, void *),
		    uint32_t irq_flags, const char *devname, void *dev_id);
void ipu_free_irq(struct ipu_soc *ipu, uint32_t irq, void *dev_id);
bool ipu_get_irq_status(struct ipu_soc *ipu, uint32_t irq);
void ipu_set_csc_coefficients(struct ipu_soc *ipu, ipu_channel_t channel, int32_t param[][3]);
int32_t ipu_set_channel_bandmode(struct ipu_soc *ipu, ipu_channel_t channel,
				 ipu_buffer_t type, uint32_t band_height);

/* two stripe calculations */
struct stripe_param{
	unsigned int input_width; /* width of the input stripe */
	unsigned int output_width; /* width of the output stripe */
	unsigned int input_column; /* the first column on the input stripe */
	unsigned int output_column; /* the first column on the output stripe */
	unsigned int idr;
	/* inverse downisizing ratio parameter; expressed as a power of 2 */
	unsigned int irr;
	/* inverse resizing ratio parameter; expressed as a multiple of 2^-13 */
};
int ipu_calc_stripes_sizes(const unsigned int input_frame_width,
				unsigned int output_frame_width,
				const unsigned int maximal_stripe_width,
				const unsigned long long cirr,
				const unsigned int equal_stripes,
				u32 input_pixelformat,
				u32 output_pixelformat,
				struct stripe_param *left,
				struct stripe_param *right);

/* SDC API */
int32_t ipu_init_sync_panel(struct ipu_soc *ipu, int disp,
			    uint32_t pixel_clk,
			    uint16_t width, uint16_t height,
			    uint32_t pixel_fmt,
			    uint16_t h_start_width, uint16_t h_sync_width,
			    uint16_t h_end_width, uint16_t v_start_width,
			    uint16_t v_sync_width, uint16_t v_end_width,
			    uint32_t v_to_h_sync, ipu_di_signal_cfg_t sig);

void ipu_uninit_sync_panel(struct ipu_soc *ipu, int disp);

int32_t ipu_disp_set_window_pos(struct ipu_soc *ipu, ipu_channel_t channel, int16_t x_pos,
				int16_t y_pos);
int32_t ipu_disp_get_window_pos(struct ipu_soc *ipu, ipu_channel_t channel, int16_t *x_pos,
				int16_t *y_pos);
int32_t ipu_disp_set_global_alpha(struct ipu_soc *ipu, ipu_channel_t channel, bool enable,
				  uint8_t alpha);
int32_t ipu_disp_set_color_key(struct ipu_soc *ipu, ipu_channel_t channel, bool enable,
			       uint32_t colorKey);
int32_t ipu_disp_set_gamma_correction(struct ipu_soc *ipu, ipu_channel_t channel, bool enable,
				int constk[], int slopek[]);

int ipu_init_async_panel(struct ipu_soc *ipu, int disp, int type, uint32_t cycle_time,
			 uint32_t pixel_fmt, ipu_adc_sig_cfg_t sig);
void ipu_reset_disp_panel(struct ipu_soc *ipu);

/* CMOS Sensor Interface API */
int32_t ipu_csi_init_interface(struct ipu_soc *ipu, uint16_t width, uint16_t height,
			       uint32_t pixel_fmt, ipu_csi_signal_cfg_t sig);

int32_t ipu_csi_get_sensor_protocol(struct ipu_soc *ipu, uint32_t csi);

int32_t ipu_csi_enable_mclk(struct ipu_soc *ipu, int src, bool flag, bool wait);

static inline int32_t ipu_csi_enable_mclk_if(struct ipu_soc *ipu, int src, uint32_t csi,
		bool flag, bool wait)
{
	return ipu_csi_enable_mclk(ipu, csi, flag, wait);
}

int ipu_csi_read_mclk_flag(void);

void ipu_csi_flash_strobe(bool flag);

void ipu_csi_get_window_size(struct ipu_soc *ipu, uint32_t *width, uint32_t *height, uint32_t csi);

void ipu_csi_set_window_size(struct ipu_soc *ipu, uint32_t width, uint32_t height, uint32_t csi);

void ipu_csi_set_window_pos(struct ipu_soc *ipu, uint32_t left, uint32_t top, uint32_t csi);

uint32_t bytes_per_pixel(uint32_t fmt);

bool ipu_ch_param_bad_alpha_pos(uint32_t fmt);
int ipu_ch_param_get_axi_id(struct ipu_soc *ipu, ipu_channel_t channel, ipu_buffer_t type);
ipu_color_space_t format_to_colorspace(uint32_t fmt);
bool ipu_pixel_format_is_gpu_tile(uint32_t fmt);
bool ipu_pixel_format_is_split_gpu_tile(uint32_t fmt);
bool ipu_pixel_format_is_pre_yuv(uint32_t fmt);
bool ipu_pixel_format_is_multiplanar_yuv(uint32_t fmt);

struct ipuv3_fb_platform_data {
	char				disp_dev[32];
	u32				interface_pix_fmt;
	char				*mode_str;
	int				default_bpp;
	bool				int_clk;

	/* reserved mem */
	resource_size_t 		res_base[2];
	resource_size_t 		res_size[2];

	/*
	 * Late init to avoid display channel being
	 * re-initialized as we've probably setup the
	 * channel in bootloader.
	 */
	bool                            late_init;

	/* Enable prefetch engine or not? */
	bool				prefetch;

	/* Enable the PRE resolve engine or not? */
	bool				resolve;
};

#endif /* __LINUX_IPU_V3_H_ */
