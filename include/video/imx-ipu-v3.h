/*
 * Copyright 2005-2009 Freescale Semiconductor, Inc.
 *
 * The code contained herein is licensed under the GNU Lesser General
 * Public License.  You may obtain a copy of the GNU Lesser General
 * Public License Version 2.1 or later at the following locations:
 *
 * http://www.opensource.org/licenses/lgpl-license.html
 * http://www.gnu.org/copyleft/lgpl.html
 */

#ifndef __DRM_IPU_H__
#define __DRM_IPU_H__

#include <linux/types.h>
#include <linux/videodev2.h>
#include <linux/bitmap.h>
#include <linux/fb.h>

struct ipu_soc;

enum ipuv3_type {
	IPUV3EX,
	IPUV3M,
	IPUV3H,
};

#define IPU_PIX_FMT_GBR24	v4l2_fourcc('G', 'B', 'R', '3')

/*
 * Bitfield of Display Interface signal polarities.
 */
struct ipu_di_signal_cfg {
	unsigned datamask_en:1;
	unsigned interlaced:1;
	unsigned odd_field_first:1;
	unsigned clksel_en:1;
	unsigned clkidle_en:1;
	unsigned data_pol:1;	/* true = inverted */
	unsigned clk_pol:1;	/* true = rising edge */
	unsigned enable_pol:1;
	unsigned Hsync_pol:1;	/* true = active high */
	unsigned Vsync_pol:1;

	u16 width;
	u16 height;
	u32 pixel_fmt;
	u16 h_start_width;
	u16 h_sync_width;
	u16 h_end_width;
	u16 v_start_width;
	u16 v_sync_width;
	u16 v_end_width;
	u32 v_to_h_sync;
	unsigned long pixelclock;
#define IPU_DI_CLKMODE_SYNC	(1 << 0)
#define IPU_DI_CLKMODE_EXT	(1 << 1)
	unsigned long clkflags;

	u8 hsync_pin;
	u8 vsync_pin;
};

enum ipu_color_space {
	IPUV3_COLORSPACE_RGB,
	IPUV3_COLORSPACE_YUV,
	IPUV3_COLORSPACE_UNKNOWN,
};

struct ipuv3_channel;

enum ipu_channel_irq {
	IPU_IRQ_EOF = 0,
	IPU_IRQ_NFACK = 64,
	IPU_IRQ_NFB4EOF = 128,
	IPU_IRQ_EOS = 192,
};

int ipu_map_irq(struct ipu_soc *ipu, int irq);
int ipu_idmac_channel_irq(struct ipu_soc *ipu, struct ipuv3_channel *channel,
		enum ipu_channel_irq irq);

#define IPU_IRQ_DP_SF_START		(448 + 2)
#define IPU_IRQ_DP_SF_END		(448 + 3)
#define IPU_IRQ_BG_SF_END		IPU_IRQ_DP_SF_END,
#define IPU_IRQ_DC_FC_0			(448 + 8)
#define IPU_IRQ_DC_FC_1			(448 + 9)
#define IPU_IRQ_DC_FC_2			(448 + 10)
#define IPU_IRQ_DC_FC_3			(448 + 11)
#define IPU_IRQ_DC_FC_4			(448 + 12)
#define IPU_IRQ_DC_FC_6			(448 + 13)
#define IPU_IRQ_VSYNC_PRE_0		(448 + 14)
#define IPU_IRQ_VSYNC_PRE_1		(448 + 15)

/*
 * IPU Image DMA Controller (idmac) functions
 */
struct ipuv3_channel *ipu_idmac_get(struct ipu_soc *ipu, unsigned channel);
void ipu_idmac_put(struct ipuv3_channel *);

int ipu_idmac_enable_channel(struct ipuv3_channel *channel);
int ipu_idmac_disable_channel(struct ipuv3_channel *channel);
int ipu_idmac_wait_busy(struct ipuv3_channel *channel, int ms);

void ipu_idmac_set_double_buffer(struct ipuv3_channel *channel,
		bool doublebuffer);
int ipu_idmac_get_current_buffer(struct ipuv3_channel *channel);
void ipu_idmac_select_buffer(struct ipuv3_channel *channel, u32 buf_num);

/*
 * IPU Channel Parameter Memory (cpmem) functions
 */
struct ipu_rgb {
	struct fb_bitfield      red;
	struct fb_bitfield      green;
	struct fb_bitfield      blue;
	struct fb_bitfield      transp;
	int                     bits_per_pixel;
};

struct ipu_image {
	struct v4l2_pix_format pix;
	struct v4l2_rect rect;
	dma_addr_t phys;
};

void ipu_cpmem_zero(struct ipuv3_channel *ch);
void ipu_cpmem_set_resolution(struct ipuv3_channel *ch, int xres, int yres);
void ipu_cpmem_set_stride(struct ipuv3_channel *ch, int stride);
void ipu_cpmem_set_high_priority(struct ipuv3_channel *ch);
void ipu_cpmem_set_buffer(struct ipuv3_channel *ch, int bufnum, dma_addr_t buf);
void ipu_cpmem_interlaced_scan(struct ipuv3_channel *ch, int stride);
void ipu_cpmem_set_burstsize(struct ipuv3_channel *ch, int burstsize);
int ipu_cpmem_set_format_rgb(struct ipuv3_channel *ch,
			     const struct ipu_rgb *rgb);
int ipu_cpmem_set_format_passthrough(struct ipuv3_channel *ch, int width);
void ipu_cpmem_set_yuv_interleaved(struct ipuv3_channel *ch, u32 pixel_format);
void ipu_cpmem_set_yuv_planar_full(struct ipuv3_channel *ch,
				   u32 pixel_format, int stride,
				   int u_offset, int v_offset);
void ipu_cpmem_set_yuv_planar(struct ipuv3_channel *ch,
			      u32 pixel_format, int stride, int height);
int ipu_cpmem_set_fmt(struct ipuv3_channel *ch, u32 drm_fourcc);
int ipu_cpmem_set_image(struct ipuv3_channel *ch, struct ipu_image *image);

/*
 * IPU Display Controller (dc) functions
 */
struct ipu_dc;
struct ipu_di;
struct ipu_dc *ipu_dc_get(struct ipu_soc *ipu, int channel);
void ipu_dc_put(struct ipu_dc *dc);
int ipu_dc_init_sync(struct ipu_dc *dc, struct ipu_di *di, bool interlaced,
		u32 pixel_fmt, u32 width);
void ipu_dc_enable(struct ipu_soc *ipu);
void ipu_dc_enable_channel(struct ipu_dc *dc);
void ipu_dc_disable_channel(struct ipu_dc *dc);
void ipu_dc_disable(struct ipu_soc *ipu);

/*
 * IPU Display Interface (di) functions
 */
struct ipu_di *ipu_di_get(struct ipu_soc *ipu, int disp);
void ipu_di_put(struct ipu_di *);
int ipu_di_disable(struct ipu_di *);
int ipu_di_enable(struct ipu_di *);
int ipu_di_get_num(struct ipu_di *);
int ipu_di_init_sync_panel(struct ipu_di *, struct ipu_di_signal_cfg *sig);

/*
 * IPU Display Multi FIFO Controller (dmfc) functions
 */
struct dmfc_channel;
int ipu_dmfc_enable_channel(struct dmfc_channel *dmfc);
void ipu_dmfc_disable_channel(struct dmfc_channel *dmfc);
int ipu_dmfc_alloc_bandwidth(struct dmfc_channel *dmfc,
		unsigned long bandwidth_mbs, int burstsize);
void ipu_dmfc_free_bandwidth(struct dmfc_channel *dmfc);
int ipu_dmfc_init_channel(struct dmfc_channel *dmfc, int width);
struct dmfc_channel *ipu_dmfc_get(struct ipu_soc *ipu, int ipuv3_channel);
void ipu_dmfc_put(struct dmfc_channel *dmfc);

/*
 * IPU Display Processor (dp) functions
 */
#define IPU_DP_FLOW_SYNC_BG	0
#define IPU_DP_FLOW_SYNC_FG	1
#define IPU_DP_FLOW_ASYNC0_BG	2
#define IPU_DP_FLOW_ASYNC0_FG	3
#define IPU_DP_FLOW_ASYNC1_BG	4
#define IPU_DP_FLOW_ASYNC1_FG	5

struct ipu_dp *ipu_dp_get(struct ipu_soc *ipu, unsigned int flow);
void ipu_dp_put(struct ipu_dp *);
int ipu_dp_enable(struct ipu_soc *ipu);
int ipu_dp_enable_channel(struct ipu_dp *dp);
void ipu_dp_disable_channel(struct ipu_dp *dp);
void ipu_dp_disable(struct ipu_soc *ipu);
int ipu_dp_setup_channel(struct ipu_dp *dp,
		enum ipu_color_space in, enum ipu_color_space out);
int ipu_dp_set_window_pos(struct ipu_dp *, u16 x_pos, u16 y_pos);
int ipu_dp_set_global_alpha(struct ipu_dp *dp, bool enable, u8 alpha,
		bool bg_chan);

/*
 * IPU CMOS Sensor Interface (csi) functions
 */
int ipu_csi_enable(struct ipu_soc *ipu, int csi);
int ipu_csi_disable(struct ipu_soc *ipu, int csi);

/*
 * IPU Sensor Multiple FIFO Controller (SMFC) functions
 */
int ipu_smfc_enable(struct ipu_soc *ipu);
int ipu_smfc_disable(struct ipu_soc *ipu);
int ipu_smfc_map_channel(struct ipu_soc *ipu, int channel, int csi_id, int mipi_id);
int ipu_smfc_set_burstsize(struct ipu_soc *ipu, int channel, int burstsize);

enum ipu_color_space ipu_drm_fourcc_to_colorspace(u32 drm_fourcc);
enum ipu_color_space ipu_pixelformat_to_colorspace(u32 pixelformat);

struct ipu_client_platformdata {
	int csi;
	int di;
	int dc;
	int dp;
	int dmfc;
	int dma[2];
};

#endif /* __DRM_IPU_H__ */
