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
#include <linux/of.h>
#include <media/v4l2-mediabus.h>
#include <video/videomode.h>

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
	unsigned data_pol:1;	/* true = inverted */
	unsigned clk_pol:1;	/* true = rising edge */
	unsigned enable_pol:1;

	struct videomode mode;

	u32 bus_format;
	u32 v_to_h_sync;

#define IPU_DI_CLKMODE_SYNC	(1 << 0)
#define IPU_DI_CLKMODE_EXT	(1 << 1)
	unsigned long clkflags;

	u8 hsync_pin;
	u8 vsync_pin;
};

/*
 * Enumeration of CSI destinations
 */
enum ipu_csi_dest {
	IPU_CSI_DEST_IDMAC, /* to memory via SMFC */
	IPU_CSI_DEST_IC,	/* to Image Converter */
	IPU_CSI_DEST_VDIC,  /* to VDIC */
};

/*
 * Enumeration of IPU rotation modes
 */
enum ipu_rotate_mode {
	IPU_ROTATE_NONE = 0,
	IPU_ROTATE_VERT_FLIP,
	IPU_ROTATE_HORIZ_FLIP,
	IPU_ROTATE_180,
	IPU_ROTATE_90_RIGHT,
	IPU_ROTATE_90_RIGHT_VFLIP,
	IPU_ROTATE_90_RIGHT_HFLIP,
	IPU_ROTATE_90_LEFT,
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

/*
 * Enumeration of IDMAC channels
 */
#define IPUV3_CHANNEL_CSI0			 0
#define IPUV3_CHANNEL_CSI1			 1
#define IPUV3_CHANNEL_CSI2			 2
#define IPUV3_CHANNEL_CSI3			 3
#define IPUV3_CHANNEL_VDI_MEM_IC_VF		 5
#define IPUV3_CHANNEL_MEM_IC_PP			11
#define IPUV3_CHANNEL_MEM_IC_PRP_VF		12
#define IPUV3_CHANNEL_G_MEM_IC_PRP_VF		14
#define IPUV3_CHANNEL_G_MEM_IC_PP		15
#define IPUV3_CHANNEL_IC_PRP_ENC_MEM		20
#define IPUV3_CHANNEL_IC_PRP_VF_MEM		21
#define IPUV3_CHANNEL_IC_PP_MEM			22
#define IPUV3_CHANNEL_MEM_BG_SYNC		23
#define IPUV3_CHANNEL_MEM_BG_ASYNC		24
#define IPUV3_CHANNEL_MEM_FG_SYNC		27
#define IPUV3_CHANNEL_MEM_DC_SYNC		28
#define IPUV3_CHANNEL_MEM_FG_ASYNC		29
#define IPUV3_CHANNEL_MEM_FG_SYNC_ALPHA		31
#define IPUV3_CHANNEL_MEM_DC_ASYNC		41
#define IPUV3_CHANNEL_MEM_ROT_ENC		45
#define IPUV3_CHANNEL_MEM_ROT_VF		46
#define IPUV3_CHANNEL_MEM_ROT_PP		47
#define IPUV3_CHANNEL_ROT_ENC_MEM		48
#define IPUV3_CHANNEL_ROT_VF_MEM		49
#define IPUV3_CHANNEL_ROT_PP_MEM		50
#define IPUV3_CHANNEL_MEM_BG_SYNC_ALPHA		51

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
 * IPU Common functions
 */
void ipu_set_csi_src_mux(struct ipu_soc *ipu, int csi_id, bool mipi_csi2);
void ipu_set_ic_src_mux(struct ipu_soc *ipu, int csi_id, bool vdi);
void ipu_dump(struct ipu_soc *ipu);

/*
 * IPU Image DMA Controller (idmac) functions
 */
struct ipuv3_channel *ipu_idmac_get(struct ipu_soc *ipu, unsigned channel);
void ipu_idmac_put(struct ipuv3_channel *);

int ipu_idmac_enable_channel(struct ipuv3_channel *channel);
int ipu_idmac_disable_channel(struct ipuv3_channel *channel);
void ipu_idmac_enable_watermark(struct ipuv3_channel *channel, bool enable);
int ipu_idmac_lock_enable(struct ipuv3_channel *channel, int num_bursts);
int ipu_idmac_wait_busy(struct ipuv3_channel *channel, int ms);

void ipu_idmac_set_double_buffer(struct ipuv3_channel *channel,
		bool doublebuffer);
int ipu_idmac_get_current_buffer(struct ipuv3_channel *channel);
bool ipu_idmac_buffer_is_ready(struct ipuv3_channel *channel, u32 buf_num);
void ipu_idmac_select_buffer(struct ipuv3_channel *channel, u32 buf_num);
void ipu_idmac_clear_buffer(struct ipuv3_channel *channel, u32 buf_num);

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
	dma_addr_t phys0;
	dma_addr_t phys1;
};

void ipu_cpmem_zero(struct ipuv3_channel *ch);
void ipu_cpmem_set_resolution(struct ipuv3_channel *ch, int xres, int yres);
void ipu_cpmem_set_stride(struct ipuv3_channel *ch, int stride);
void ipu_cpmem_set_high_priority(struct ipuv3_channel *ch);
void ipu_cpmem_set_buffer(struct ipuv3_channel *ch, int bufnum, dma_addr_t buf);
void ipu_cpmem_interlaced_scan(struct ipuv3_channel *ch, int stride);
void ipu_cpmem_set_axi_id(struct ipuv3_channel *ch, u32 id);
void ipu_cpmem_set_burstsize(struct ipuv3_channel *ch, int burstsize);
void ipu_cpmem_set_block_mode(struct ipuv3_channel *ch);
void ipu_cpmem_set_rotation(struct ipuv3_channel *ch,
			    enum ipu_rotate_mode rot);
int ipu_cpmem_set_format_rgb(struct ipuv3_channel *ch,
			     const struct ipu_rgb *rgb);
int ipu_cpmem_set_format_passthrough(struct ipuv3_channel *ch, int width);
void ipu_cpmem_set_yuv_interleaved(struct ipuv3_channel *ch, u32 pixel_format);
void ipu_cpmem_set_yuv_planar_full(struct ipuv3_channel *ch,
				   unsigned int uv_stride,
				   unsigned int u_offset,
				   unsigned int v_offset);
void ipu_cpmem_set_yuv_planar(struct ipuv3_channel *ch,
			      u32 pixel_format, int stride, int height);
int ipu_cpmem_set_fmt(struct ipuv3_channel *ch, u32 drm_fourcc);
int ipu_cpmem_set_image(struct ipuv3_channel *ch, struct ipu_image *image);
void ipu_cpmem_dump(struct ipuv3_channel *ch);

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
int ipu_di_adjust_videomode(struct ipu_di *di, struct videomode *mode);
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
void ipu_dmfc_config_wait4eot(struct dmfc_channel *dmfc, int width);
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
struct ipu_csi;
int ipu_csi_init_interface(struct ipu_csi *csi,
			   struct v4l2_mbus_config *mbus_cfg,
			   struct v4l2_mbus_framefmt *mbus_fmt);
bool ipu_csi_is_interlaced(struct ipu_csi *csi);
void ipu_csi_get_window(struct ipu_csi *csi, struct v4l2_rect *w);
void ipu_csi_set_window(struct ipu_csi *csi, struct v4l2_rect *w);
void ipu_csi_set_test_generator(struct ipu_csi *csi, bool active,
				u32 r_value, u32 g_value, u32 b_value,
				u32 pix_clk);
int ipu_csi_set_mipi_datatype(struct ipu_csi *csi, u32 vc,
			      struct v4l2_mbus_framefmt *mbus_fmt);
int ipu_csi_set_skip_smfc(struct ipu_csi *csi, u32 skip,
			  u32 max_ratio, u32 id);
int ipu_csi_set_dest(struct ipu_csi *csi, enum ipu_csi_dest csi_dest);
int ipu_csi_enable(struct ipu_csi *csi);
int ipu_csi_disable(struct ipu_csi *csi);
struct ipu_csi *ipu_csi_get(struct ipu_soc *ipu, int id);
void ipu_csi_put(struct ipu_csi *csi);
void ipu_csi_dump(struct ipu_csi *csi);

/*
 * IPU Image Converter (ic) functions
 */
enum ipu_ic_task {
	IC_TASK_ENCODER,
	IC_TASK_VIEWFINDER,
	IC_TASK_POST_PROCESSOR,
	IC_NUM_TASKS,
};

struct ipu_ic;
int ipu_ic_task_init(struct ipu_ic *ic,
		     int in_width, int in_height,
		     int out_width, int out_height,
		     enum ipu_color_space in_cs,
		     enum ipu_color_space out_cs);
int ipu_ic_task_graphics_init(struct ipu_ic *ic,
			      enum ipu_color_space in_g_cs,
			      bool galpha_en, u32 galpha,
			      bool colorkey_en, u32 colorkey);
void ipu_ic_task_enable(struct ipu_ic *ic);
void ipu_ic_task_disable(struct ipu_ic *ic);
int ipu_ic_task_idma_init(struct ipu_ic *ic, struct ipuv3_channel *channel,
			  u32 width, u32 height, int burst_size,
			  enum ipu_rotate_mode rot);
int ipu_ic_enable(struct ipu_ic *ic);
int ipu_ic_disable(struct ipu_ic *ic);
struct ipu_ic *ipu_ic_get(struct ipu_soc *ipu, enum ipu_ic_task task);
void ipu_ic_put(struct ipu_ic *ic);
void ipu_ic_dump(struct ipu_ic *ic);

/*
 * IPU Sensor Multiple FIFO Controller (SMFC) functions
 */
struct ipu_smfc *ipu_smfc_get(struct ipu_soc *ipu, unsigned int chno);
void ipu_smfc_put(struct ipu_smfc *smfc);
int ipu_smfc_enable(struct ipu_smfc *smfc);
int ipu_smfc_disable(struct ipu_smfc *smfc);
int ipu_smfc_map_channel(struct ipu_smfc *smfc, int csi_id, int mipi_id);
int ipu_smfc_set_burstsize(struct ipu_smfc *smfc, int burstsize);
int ipu_smfc_set_watermark(struct ipu_smfc *smfc, u32 set_level, u32 clr_level);

enum ipu_color_space ipu_drm_fourcc_to_colorspace(u32 drm_fourcc);
enum ipu_color_space ipu_pixelformat_to_colorspace(u32 pixelformat);
enum ipu_color_space ipu_mbus_code_to_colorspace(u32 mbus_code);
int ipu_stride_to_bytes(u32 pixel_stride, u32 pixelformat);
bool ipu_pixelformat_is_planar(u32 pixelformat);
int ipu_degrees_to_rot_mode(enum ipu_rotate_mode *mode, int degrees,
			    bool hflip, bool vflip);
int ipu_rot_mode_to_degrees(int *degrees, enum ipu_rotate_mode mode,
			    bool hflip, bool vflip);

struct ipu_client_platformdata {
	int csi;
	int di;
	int dc;
	int dp;
	int dma[2];
	struct device_node *of_node;
};

#endif /* __DRM_IPU_H__ */
