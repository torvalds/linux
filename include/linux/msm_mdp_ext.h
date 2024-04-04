/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved. */
#ifndef _MSM_MDP_EXT_H_
#define _MSM_MDP_EXT_H_

#include <linux/msm_mdp.h>

#define MDP_IOCTL_MAGIC 'S'
/* atomic commit ioctl used for validate and commit request */
#define MSMFB_ATOMIC_COMMIT	_IOWR(MDP_IOCTL_MAGIC, 128, void *)

/*
 * Ioctl for updating the layer position asynchronously. Initially, pipes
 * should be configured with MDP_LAYER_ASYNC flag set during the atomic commit,
 * after which any number of position update calls can be made. This would
 * enable multiple position updates within a single vsync. However, the screen
 * update would happen only after vsync, which would pick the latest update.
 *
 * Limitations:
 * - Currently supported only for video mode panels with single LM or dual LM
 *   with source_split enabled.
 * - Only position update is supported with no scaling/cropping.
 * - Async layers should have unique z_order.
 */
#define MSMFB_ASYNC_POSITION_UPDATE _IOWR(MDP_IOCTL_MAGIC, 129, \
					struct mdp_position_update)

/*
 * Ioctl for sending the config information.
 * QSEED3 coefficeint LUT tables is passed by the user space using this IOCTL.
 */
#define MSMFB_MDP_SET_CFG _IOW(MDP_IOCTL_MAGIC, 130, \
					      struct mdp_set_cfg)

/*
 * Ioctl for setting the PLL PPM.
 * PLL PPM is passed by the user space using this IOCTL.
 */
#define MSMFB_MDP_SET_PANEL_PPM _IOW(MDP_IOCTL_MAGIC, 131, int)

/*
 * To allow proper structure padding for 64bit/32bit target
 */
#ifdef __LP64
#define MDP_LAYER_COMMIT_V1_PAD 2
#else
#define MDP_LAYER_COMMIT_V1_PAD 3
#endif

/**********************************************************************
 * LAYER FLAG CONFIGURATION
 **********************************************************************/
/* left-right layer flip flag */
#define MDP_LAYER_FLIP_LR		0x1

/* up-down layer flip flag */
#define MDP_LAYER_FLIP_UD		0x2

/*
 * This flag enables pxl extension for the current layer. Validate/commit
 * call uses scale parameters when this flag is enabled.
 */
#define MDP_LAYER_ENABLE_PIXEL_EXT	0x4

/* Flag indicates that layer is foreground layer */
#define MDP_LAYER_FORGROUND		0x8

/* Flag indicates that layer is associated with secure session */
#define MDP_LAYER_SECURE_SESSION	0x10

/*
 * Flag indicates that layer is drawing solid fill. Validate/commit call
 * does not expect buffer when this flag is enabled.
 */
#define MDP_LAYER_SOLID_FILL		0x20

/* Layer format is deinterlace */
#define MDP_LAYER_DEINTERLACE		0x40

/* layer contains bandwidth compressed format data */
#define MDP_LAYER_BWC			0x80

/* layer is async position updatable */
#define MDP_LAYER_ASYNC			0x100

/* layer contains postprocessing configuration data */
#define MDP_LAYER_PP			0x200

/* Flag indicates that layer is associated with secure display session */
#define MDP_LAYER_SECURE_DISPLAY_SESSION 0x400

/* Flag enabled qseed3 scaling for the current layer */
#define MDP_LAYER_ENABLE_QSEED3_SCALE   0x800

/*
 * layer will work in multirect mode, where single hardware should
 * fetch multiple rectangles with a single hardware
 */
#define MDP_LAYER_MULTIRECT_ENABLE		0x1000

/*
 * if flag present and multirect is enabled, multirect will work in parallel
 * fetch mode, otherwise it will default to serial fetch mode.
 */
#define MDP_LAYER_MULTIRECT_PARALLEL_MODE	0x2000


/* Flag indicates that layer is associated with secure camera session */
#define MDP_LAYER_SECURE_CAMERA_SESSION		0x4000

/**********************************************************************
 * DESTINATION SCALER FLAG CONFIGURATION
 **********************************************************************/

/* Enable/disable Destination scaler */
#define MDP_DESTSCALER_ENABLE		0x1

/*
 * Indicating mdp_destination_scaler_data contains
 * Scaling parameter update. Can be set anytime.
 */
#define MDP_DESTSCALER_SCALE_UPDATE	0x2

/*
 * Indicating mdp_destination_scaler_data contains
 * Detail enhancement setting update. Can be set anytime.
 */
#define MDP_DESTSCALER_ENHANCER_UPDATE	0x4

/*
 * Indicating a partial update to panel ROI. ROI can be
 * applied anytime when Destination scaler is enabled.
 */
#define MDP_DESTSCALER_ROI_ENABLE	0x8

/**********************************************************************
 * VALIDATE/COMMIT FLAG CONFIGURATION
 **********************************************************************/

/*
 * Client enables it to inform that call is to validate layers before commit.
 * If this flag is not set then driver will use MSMFB_ATOMIC_COMMIT for commit.
 */
#define MDP_VALIDATE_LAYER			0x01

/*
 * This flag is only valid for commit call. Commit behavior is synchronous
 * when this flag is defined. It blocks current call till processing is
 * complete. Behavior is asynchronous otherwise.
 */
#define MDP_COMMIT_WAIT_FOR_FINISH		0x02

/*
 * This flag is only valid for commit call and used for debugging purpose. It
 * forces the to wait for sync fences.
 */
#define MDP_COMMIT_SYNC_FENCE_WAIT		0x04

/* Flag to enable AVR(Adaptive variable refresh) feature. */
#define MDP_COMMIT_AVR_EN			0x08

/*
 * Flag to select one shot mode when AVR feature is enabled.
 * Default mode is continuous mode.
 */
#define MDP_COMMIT_AVR_ONE_SHOT_MODE		0x10

/* Flag to indicate dual partial ROI update */
#define MDP_COMMIT_PARTIAL_UPDATE_DUAL_ROI	0x20

/* Flag to update brightness when commit */
#define MDP_COMMIT_UPDATE_BRIGHTNESS		0x40

/* Flag to enable concurrent writeback for the frame */
#define MDP_COMMIT_CWB_EN 0x800

/*
 * Flag to select DSPP as the data point for CWB. If CWB
 * is enabled without this flag, LM will be selected as data point.
 */
#define MDP_COMMIT_CWB_DSPP 0x1000

/*
 * Flag to indicate that rectangle number is being assigned
 * by userspace in multi-rectangle mode
 */
#define MDP_COMMIT_RECT_NUM 0x2000

#define MDP_COMMIT_VERSION_1_0		0x00010000

#define OUT_LAYER_COLOR_SPACE

/* From CEA.861.3 */
#define MDP_HDR_EOTF_SMTPE_ST2084	0x2
#define MDP_HDR_EOTF_HLG		0x3

/* From Vesa DPv1.4 - pxl Encoding - Table 2-120 */
#define MDP_PIXEL_ENCODING_RGB		0x0
#define MDP_PIXEL_ENCODING_YCBCR_444	0x1
#define MDP_PIXEL_ENCODING_YCBCR_422	0x2
#define MDP_PIXEL_ENCODING_YCBCR_420	0x3
#define MDP_PIXEL_ENCODING_Y_ONLY	0x4
#define MDP_PIXEL_ENCODING_RAW		0x5

/* From Vesa DPv1.4 - Colorimetry Formats - Table 2-120 */
/* RGB - used with MDP_DP_PIXEL_ENCODING_RGB */
#define MDP_COLORIMETRY_RGB_SRGB		0x0
#define MDP_COLORIMETRY_RGB_WIDE_FIXED_POINT	0x1
#define MDP_COLORIMETRY_RGB_WIDE_FLOAT_POINT	0x2
#define MDP_COLORIMETRY_RGB_ADOBE		0x3
#define MDP_COLORIMETRY_RGB_DPI_P3		0x4
#define MDP_COLORIMETRY_RGB_CUSTOM		0x5
#define MDP_COLORIMETRY_RGB_ITU_R_BT_2020	0x6

/* YUV - used with MDP_DP_PIXEL_ENCODING_YCBCR(444 or 422 or 420) */
#define MDP_COLORIMETRY_YCBCR_ITU_R_BT_601		0x0
#define MDP_COLORIMETRY_YCBCR_ITU_R_BT_709		0x1
#define MDP_COLORIMETRY_YCBCR_XV_YCC_601		0x2
#define MDP_COLORIMETRY_YCBCR_XV_YCC_709		0x3
#define MDP_COLORIMETRY_YCBCR_S_YCC_601		0x4
#define MDP_COLORIMETRY_YCBCR_ADOBE_YCC_601		0x5
#define MDP_COLORIMETRY_YCBCR_ITU_R_BT_2020_YCBCR_CONST	0x6
#define MDP_COLORIMETRY_YCBCR_ITU_R_BT_2020_YCBCR	0x7

/* Dynamic Range - Table 2-120 */
/* Full range */
#define MDP_DYNAMIC_RANGE_VESA	0x0
/* Limited range */
#define MDP_DYNAMIC_RANGE_CEA	0x1

/* Bits per component(bpc) for pxl encoding format RGB from Table 2-120 */
#define MDP_RGB_6_BPC	0x0
#define MDP_RGB_8_BPC	0x1
#define MDP_RGB_10_BPC	0x2
#define MDP_RGB_12_BPC	0x3
#define MDP_RGB_16_BPC	0x4

/*
 * Bits per component(bpc) for pxl encoding format YCbCr444, YCbCr422,
 * YCbCr420 and Y only
 * from Table 2-120
 */
#define MDP_YUV_8_BPC	0x1
#define MDP_YUV_10_BPC	0x2
#define MDP_YUV_12_BPC	0x3
#define MDP_YUV_16_BPC	0x4

/* Bits per component(bpc) for pxl encoding format RAW from Table 2-120 */
#define MDP_RAW_6_BPC	0x1
#define MDP_RAW_7_BPC	0x2
#define MDP_RAW_8_BPC	0x3
#define MDP_RAW_10_BPC	0x4
#define MDP_RAW_12_BPC	0x5
#define MDP_RAW_14_BPC	0x6
#define MDP_RAW16_BPC	0x7

/* Content Type - Table 2-120 */
#define MDP_CONTENT_TYPE_NOT_DEFINED	0x0
#define MDP_CONTENT_TYPE_GRAPHICS		0x1
#define MDP_CONTENT_TYPE_PHOTO			0x2
#define MDP_CONTENT_TYPE_VIDEO		0x3
#define MDP_CONTENT_TYPE_GAME		0x4

/**********************************************************************
 * Configuration structures
 * All parameters are input to driver unless mentioned output parameter
 * explicitly.
 **********************************************************************/
struct mdp_layer_plane {
	/* DMA buffer file descriptor information. */
	int fd;

	/* pxl offset in the dma buffer. */
	uint32_t offset;

	/* Number of bytes in one scan line including padding bytes. */
	uint32_t stride;
};

struct mdp_layer_buffer {
	/* layer width in pxls. */
	uint32_t width;

	/* layer height in pxls. */
	uint32_t height;

	/*
	 * layer format in DRM-style fourcc, refer drm_fourcc.h for
	 * standard formats
	 */
	uint32_t format;

	/* plane to hold the fd, offset, etc for all color components */
	struct mdp_layer_plane planes[MAX_PLANES];

	/* valid planes count in layer planes list */
	uint32_t plane_count;

	/* compression ratio factor, value depends on the pxl format */
	struct mult_factor comp_ratio;

	/*
	 * SyncFence associated with this buffer. It is used in two ways.
	 *
	 * 1. Driver waits to consume the buffer till producer signals in case
	 * of primary and external display.
	 *
	 * 2. Writeback device uses buffer structure for output buffer where
	 * driver is producer. However, client sends the fence with buffer to
	 * indicate that consumer is still using the buffer and it is not ready
	 * for new content.
	 */
	int	 fence;

	/* 32bits reserved value for future usage. */
	uint32_t reserved;
};

/*
 * One layer holds configuration for one pipe. If client wants to stage single
 * layer on two pipes then it should send two different layers with relative
 * (x,y) information. Client must send same information during validate and
 * commit call. Commit call may fail if client sends different layer information
 * attached to same pipe during validate and commit. Device invalidate the pipe
 * once it receives the vsync for that commit.
 */
struct mdp_input_layer {
	/*
	 * Flag to enable/disable properties for layer configuration. Refer
	 * layer flag configuration section for all possible flags.
	 */
	uint32_t		flags;

	/*
	 * Pipe selection for this layer by client. Client provides the index
	 * in validate and commit call. Device reserves the pipe once validate
	 * is successful. Device only uses validated pipe during commit call.
	 * If client sends different layer/pipe configuration in validate &
	 * commit then commit may fail.
	 */
	uint32_t		pipe_ndx;

	/*
	 * Horizontal decimation value, this indicates the amount of pxls
	 * dropped for each pxl that is fetched from a line. It does not
	 * result in bandwidth reduction because pxls are still fetched from
	 * memory but dropped internally by hardware.
	 * The decimation value given should be power of two of decimation
	 * amount.
	 * 0: no decimation
	 * 1: decimate by 2 (drop 1 pxl for each pxl fetched)
	 * 2: decimate by 4 (drop 3 pxls for each pxl fetched)
	 * 3: decimate by 8 (drop 7 pxls for each pxl fetched)
	 * 4: decimate by 16 (drop 15 pxls for each pxl fetched)
	 */
	uint8_t			horz_deci;

	/*
	 * Vertical decimation value, this indicates the amount of lines
	 * dropped for each line that is fetched from overlay. It saves
	 * bandwidth because decimated pxls are not fetched.
	 * The decimation value given should be power of two of decimation
	 * amount.
	 * 0: no decimation
	 * 1: decimation by 2 (drop 1 line for each line fetched)
	 * 2: decimation by 4 (drop 3 lines for each line fetched)
	 * 3: decimation by 8 (drop 7 lines for each line fetched)
	 * 4: decimation by 16 (drop 15 lines for each line fetched)
	 */
	uint8_t			vert_deci;

	/*
	 * Used to set plane opacity. The range can be from 0-255, where
	 * 0 means completely transparent and 255 means fully opaque.
	 */
	uint8_t			alpha;

	/*
	 * Blending stage to occupy in display, if multiple layers are present,
	 * highest z_order usually means the top most visible layer. The range
	 * acceptable is from 0-7 to support blending up to 8 layers.
	 */
	uint16_t		z_order;

	/*
	 * Color used as color key for transparency. Any pxl in fetched
	 * image matching this color will be transparent when blending.
	 * The color should be in same format as the source image format.
	 */
	uint32_t		transp_mask;

	/*
	 * Solid color used to fill the overlay surface when no source
	 * buffer is provided.
	 */
	uint32_t		bg_color;

	/* blend operation defined in "mdss_mdp_blend_op" enum. */
	enum mdss_mdp_blend_op		blend_op;

	/* color space of the source */
	enum mdp_color_space	color_space;

	/*
	 * Source crop rectangle, portion of image that will be fetched. This
	 * should always be within boundaries of source image.
	 */
	struct mdp_rect		src_rect;

	/*
	 * Destination rectangle, the position and size of image on screen.
	 * This should always be within panel boundaries.
	 */
	struct mdp_rect		dst_rect;

	/* Scaling parameters. */
	void __user	*scale;

	/* Buffer attached with each layer. Device uses it for commit call. */
	struct mdp_layer_buffer	buffer;

	/*
	 * Source side post processing configuration information for each
	 * layer.
	 */
	void __user		*pp_info;

	/*
	 * This is an output parameter.
	 *
	 * Only for validate call. Frame buffer device sets error code
	 * based on validate call failure scenario.
	 */
	int			error_code;

	/*
	 * For source pipes supporting multi-rectangle, this field identifies
	 * the rectangle index of the source pipe.
	 */
	uint32_t		rect_num;

	/* 32bits reserved value for future usage. */
	uint32_t		reserved[5];
};

struct mdp_output_layer {
	/*
	 * Flag to enable/disable properties for layer configuration. Refer
	 * layer flag config section for all possible flags.
	 */
	uint32_t			flags;

	/*
	 * Writeback destination selection for output. Client provides the index
	 * in validate and commit call.
	 */
	uint32_t			writeback_ndx;

	/* Buffer attached with output layer. Device uses it for commit call */
	struct mdp_layer_buffer		buffer;

	/* color space of the destination */
	enum mdp_color_space		color_space;

	/* 32bits reserved value for future usage. */
	uint32_t			reserved[5];
};

/*
 * Destination scaling info structure holds setup parameters for upscaling
 * setting in the destination scaling block.
 */
struct mdp_destination_scaler_data {
	/*
	 * Flag to switch between mode for destination scaler. Please Refer to
	 * destination scaler flag config for all possible setting.
	 */
	uint32_t			flags;

	/*
	 * Destination scaler selection index. Client provides the index in
	 * validate and commit call.
	 */
	uint32_t			dest_scaler_ndx;

	/*
	 * LM width configuration per Destination scaling updates
	 */
	uint32_t			lm_width;

	/*
	 * LM height configuration per Destination scaling updates
	 */
	uint32_t			lm_height;

	/*
	 * The scaling parameters for all the mode except disable. For
	 * disabling the scaler, there is no need to provide the scale.
	 * A userspace pointer points to struct mdp_scale_data_v2.
	 */
	uint64_t	__user scale;

	/*
	 * Panel ROI is used when partial update is required in
	 * current commit call.
	 */
	struct mdp_rect	panel_roi;
};

/*
 * Commit structure holds layer stack send by client for validate and commit
 * call. If layers are different between validate and commit call then commit
 * call will also do validation. In such case, commit may fail.
 */
struct mdp_layer_commit_v1 {
	/*
	 * Flag to enable/disable properties for commit/validate call. Refer
	 * validate/commit flag config section for all possible flags.
	 */
	uint32_t		flags;

	/*
	 * This is an output parameter.
	 *
	 * Frame buffer device provides release fence handle to client. It
	 * triggers release fence when display hardware has consumed all the
	 * buffers attached to this commit call and buffer is ready for reuse
	 * for primary and external. For writeback case, it triggers it when
	 * output buffer is ready for consumer.
	 */
	int			release_fence;

	/*
	 * Left_roi is optional configuration. Client configures it only when
	 * partial update is enabled. It defines the "region of interest" on
	 * left part of panel when it is split display. For non-split display,
	 * it defines the "region of interest" on the panel.
	 */
	struct mdp_rect		left_roi;

	/*
	 * Right_roi is optional configuration. Client configures it only when
	 * partial update is enabled. It defines the "region of interest" on
	 * right part of panel for split display configuration. It is not
	 * required for non-split display.
	 */
	struct mdp_rect		right_roi;

	 /* Pointer to a list of input layers for composition. */
	struct mdp_input_layer __user *input_layers;

	/* Input layer count present in input list */
	uint32_t		input_layer_cnt;

	/*
	 * Output layer for writeback display. It supports only one
	 * layer as output layer. This is not required for primary
	 * and external displays
	 */
	struct mdp_output_layer __user *output_layer;

	/*
	 * This is an output parameter.
	 *
	 * Frame buffer device provides retire fence handle if
	 * COMMIT_RETIRE_FENCE flag is set in commit call. It triggers
	 * retire fence when current layers are swapped with new layers
	 * on display hardware. For video mode panel and writeback,
	 * retire fence and release fences are triggered at the same
	 * time while command mode panel triggers release fence first
	 * (on pingpong done) and retire fence (on rdptr done)
	 * after that.
	 */
	int			retire_fence;

	/*
	 * Scaler data and control for setting up destination scaler.
	 * A userspace pointer that points to a list of
	 * struct mdp_destination_scaler_data.
	 */
	void __user		*dest_scaler;

	/*
	 * Represents number of Destination scaler data provied by userspace.
	 */
	uint32_t		dest_scaler_cnt;

	/* Backlight level that would update when display commit */
	uint32_t		bl_level;

	/* 32-bits reserved value for future usage. */
	uint32_t		reserved[MDP_LAYER_COMMIT_V1_PAD];
};

/*
 * mdp_overlay_list - argument for ioctl MSMFB_ATOMIC_COMMIT
 */
struct mdp_layer_commit {
	/*
	 * 32bit version indicates the commit structure selection
	 * from union. Lower 16bits indicates the minor version while
	 * higher 16bits indicates the major version. It selects the
	 * commit structure based on major version selection. Minor version
	 * indicates that reserved fields are in use.
	 *
	 * Current supported version is 1.0 (Major:1 Minor:0)
	 */
	uint32_t version;
	union {
		/* Layer commit/validate definition for V1 */
		struct mdp_layer_commit_v1 commit_v1;
	};
};

struct mdp_point {
	uint32_t x;
	uint32_t y;
};

/*
 * Async updatable layers. One layer holds configuration for one pipe.
 */
struct mdp_async_layer {
	/*
	 * Flag to enable/disable properties for layer configuration. Refer
	 * layer flag config section for all possible flags.
	 */
	uint32_t flags;

	/*
	 * Pipe selection for this layer by client. Client provides the
	 * pipe index that the device reserved during ATOMIC_COMMIT.
	 */
	uint32_t		pipe_ndx;

	/* Source start x,y. */
	struct mdp_point	src;

	/* Destination start x,y. */
	struct mdp_point	dst;

	/*
	 * This is an output parameter.
	 *
	 * Frame buffer device sets error code based on the failure.
	 */
	int			error_code;

	uint32_t		reserved[3];
};

/*
 * mdp_position_update - argument for ioctl MSMFB_ASYNC_POSITION_UPDATE
 */
struct mdp_position_update {
	 /* Pointer to a list of async updatable input layers */
	struct mdp_async_layer __user *input_layers;

	/* Input layer count present in input list */
	uint32_t input_layer_cnt;
};

#define MAX_DET_CURVES		3
struct mdp_det_enhance_data {
	uint32_t enable;
	int16_t sharpen_level1;
	int16_t sharpen_level2;
	uint16_t clip;
	uint16_t limit;
	uint16_t thr_quiet;
	uint16_t thr_dieout;
	uint16_t thr_low;
	uint16_t thr_high;
	uint16_t prec_shift;
	int16_t adjust_a[MAX_DET_CURVES];
	int16_t adjust_b[MAX_DET_CURVES];
	int16_t adjust_c[MAX_DET_CURVES];
};

/* Flags to enable Scaler and its sub components */
#define ENABLE_SCALE			0x1
#define ENABLE_DETAIL_ENHANCE		0x2
#define ENABLE_DIRECTION_DETECTION	0x4

/* LUT configuration flags */
#define SCALER_LUT_SWAP			0x1
#define SCALER_LUT_DIR_WR		0x2
#define SCALER_LUT_Y_CIR_WR		0x4
#define SCALER_LUT_UV_CIR_WR		0x8
#define SCALER_LUT_Y_SEP_WR		0x10
#define SCALER_LUT_UV_SEP_WR		0x20

/* Y/RGB and UV filter configuration */
#define FILTER_EDGE_DIRECTED_2D		0x0
#define FILTER_CIRCULAR_2D		0x1
#define FILTER_SEPARABLE_1D		0x2
#define FILTER_BILINEAR			0x3

/* Alpha filters */
#define FILTER_ALPHA_DROP_REPEAT	0x0
#define FILTER_ALPHA_BILINEAR		0x1

/**
 * struct mdp_scale_data_v2
 * Driver uses this new Data structure for storing all scaling params
 * This structure contains all pxl extension data and QSEED3 filter
 * configuration and coefficient table indices
 */
struct mdp_scale_data_v2 {
	uint32_t enable;

	/* Init phase values */
	int32_t init_phase_x[MAX_PLANES];
	int32_t phase_step_x[MAX_PLANES];
	int32_t init_phase_y[MAX_PLANES];
	int32_t phase_step_y[MAX_PLANES];

	/* This should be set to toal horizontal pxls
	 * left + right +  width
	 */
	uint32_t num_ext_pxls_left[MAX_PLANES];

	/* Unused param for backward compatibility */
	uint32_t num_ext_pxls_right[MAX_PLANES];

	/*  This should be set to vertical pxls
	 *  top + bottom + height
	 */
	uint32_t num_ext_pxls_top[MAX_PLANES];

	/* Unused param for backward compatibility */
	uint32_t num_ext_pxls_btm[MAX_PLANES];

	/* over fetch pxls */
	int32_t left_ftch[MAX_PLANES];
	int32_t left_rpt[MAX_PLANES];
	int32_t right_ftch[MAX_PLANES];
	int32_t right_rpt[MAX_PLANES];

	/* Repeat pxls */
	uint32_t top_rpt[MAX_PLANES];
	uint32_t btm_rpt[MAX_PLANES];
	uint32_t top_ftch[MAX_PLANES];
	uint32_t btm_ftch[MAX_PLANES];

	uint32_t roi_w[MAX_PLANES];

	/* alpha plane can only be scaled using bilinear or pxl
	 * repeat/drop, specify these for Y and UV planes only
	 */
	uint32_t preload_x[MAX_PLANES];
	uint32_t preload_y[MAX_PLANES];
	uint32_t src_width[MAX_PLANES];
	uint32_t src_height[MAX_PLANES];

	uint32_t dst_width;
	uint32_t dst_height;

	uint32_t y_rgb_filter_cfg;
	uint32_t uv_filter_cfg;
	uint32_t alpha_filter_cfg;
	uint32_t blend_cfg;

	uint32_t lut_flag;
	uint32_t dir_lut_idx;

	/* for Y(RGB) and UV planes*/
	uint32_t y_rgb_cir_lut_idx;
	uint32_t uv_cir_lut_idx;
	uint32_t y_rgb_sep_lut_idx;
	uint32_t uv_sep_lut_idx;

	struct mdp_det_enhance_data detail_enhance;

	/* reserved value for future usage. */
	uint64_t reserved[8];
};

/**
 * struct mdp_scale_luts_info
 * This struct pointer is received as payload in SET_CFG_IOCTL when the flags
 * is set to MDP_QSEED3_LUT_CFG
 * @dir_lut:      Direction detection coefficients table
 * @cir_lut:      Circular coefficeints table
 * @sep_lut:      Separable coefficeints table
 * @dir_lut_size: Size of direction coefficients table
 * @cir_lut_size: Size of circular coefficients table
 * @sep_lut_size: Size of separable coefficients table
 */
struct mdp_scale_luts_info {
	uint64_t __user dir_lut;
	uint64_t __user cir_lut;
	uint64_t __user sep_lut;
	uint32_t dir_lut_size;
	uint32_t cir_lut_size;
	uint32_t sep_lut_size;
};

#define MDP_QSEED3_LUT_CFG 0x1

struct mdp_set_cfg {
	uint64_t flags;
	uint32_t len;
	uint64_t __user payload;
};

#define HDR_PRIMARIES_COUNT 3

#define MDP_HDR_STREAM

struct mdp_hdr_stream {
	uint32_t eotf;
	uint32_t display_primaries_x[HDR_PRIMARIES_COUNT];
	uint32_t display_primaries_y[HDR_PRIMARIES_COUNT];
	uint32_t white_point_x;
	uint32_t white_point_y;
	uint32_t max_luminance;
	uint32_t min_luminance;
	uint32_t max_content_light_level;
	uint32_t max_average_light_level;
	/* DP related */
	uint32_t pixel_encoding;
	uint32_t colorimetry;
	uint32_t range;
	uint32_t bits_per_component;
	uint32_t content_type;
	uint32_t reserved[5];
};

/* hdr hdmi state takes possible values of 1, 2 and 4 respectively */
#define HDR_ENABLE  (1 << 0)
#define HDR_DISABLE (1 << 1)
#define HDR_RESET   (1 << 2)

/*
 * HDR Control
 * This encapsulates the HDR metadata as well as a state control
 * for the HDR metadata as required by the HDMI spec to send the
 * relevant metadata depending on the state of the HDR playback.
 * hdr_state: Controls HDR state, takes values HDR_ENABLE, HDR_DISABLE
 * and HDR_RESET.
 * hdr_meta: Metadata sent by the userspace for the HDR clip.
 */

struct mdp_hdr_stream_ctrl {
	__u8 hdr_state;                   /* HDR state */
	struct mdp_hdr_stream hdr_stream; /* HDR metadata */
};

#endif
