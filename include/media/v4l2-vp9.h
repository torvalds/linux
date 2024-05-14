/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Helper functions for vp9 codecs.
 *
 * Copyright (c) 2021 Collabora, Ltd.
 *
 * Author: Andrzej Pietrasiewicz <andrzej.p@collabora.com>
 */

#ifndef _MEDIA_V4L2_VP9_H
#define _MEDIA_V4L2_VP9_H

#include <media/v4l2-ctrls.h>

/**
 * struct v4l2_vp9_frame_mv_context - motion vector-related probabilities
 *
 * @joint: motion vector joint probabilities.
 * @sign: motion vector sign probabilities.
 * @classes: motion vector class probabilities.
 * @class0_bit: motion vector class0 bit probabilities.
 * @bits: motion vector bits probabilities.
 * @class0_fr: motion vector class0 fractional bit probabilities.
 * @fr: motion vector fractional bit probabilities.
 * @class0_hp: motion vector class0 high precision fractional bit probabilities.
 * @hp: motion vector high precision fractional bit probabilities.
 *
 * A member of v4l2_vp9_frame_context.
 */
struct v4l2_vp9_frame_mv_context {
	u8 joint[3];
	u8 sign[2];
	u8 classes[2][10];
	u8 class0_bit[2];
	u8 bits[2][10];
	u8 class0_fr[2][2][3];
	u8 fr[2][3];
	u8 class0_hp[2];
	u8 hp[2];
};

/**
 * struct v4l2_vp9_frame_context - frame probabilities, including motion-vector related
 *
 * @tx8: TX 8x8 probabilities.
 * @tx16: TX 16x16 probabilities.
 * @tx32: TX 32x32 probabilities.
 * @coef: coefficient probabilities.
 * @skip: skip probabilities.
 * @inter_mode: inter mode probabilities.
 * @interp_filter: interpolation filter probabilities.
 * @is_inter: is inter-block probabilities.
 * @comp_mode: compound prediction mode probabilities.
 * @single_ref: single ref probabilities.
 * @comp_ref: compound ref probabilities.
 * @y_mode: Y prediction mode probabilities.
 * @uv_mode: UV prediction mode probabilities.
 * @partition: partition probabilities.
 * @mv: motion vector probabilities.
 *
 * Drivers which need to keep track of frame context(s) can use this struct.
 * The members correspond to probability tables, which are specified only implicitly in the
 * vp9 spec. Section 10.5 "Default probability tables" contains all the types of involved
 * tables, i.e. the actual tables are of the same kind, and when they are reset (which is
 * mandated by the spec sometimes) they are overwritten with values from the default tables.
 */
struct v4l2_vp9_frame_context {
	u8 tx8[2][1];
	u8 tx16[2][2];
	u8 tx32[2][3];
	u8 coef[4][2][2][6][6][3];
	u8 skip[3];
	u8 inter_mode[7][3];
	u8 interp_filter[4][2];
	u8 is_inter[4];
	u8 comp_mode[5];
	u8 single_ref[5][2];
	u8 comp_ref[5];
	u8 y_mode[4][9];
	u8 uv_mode[10][9];
	u8 partition[16][3];

	struct v4l2_vp9_frame_mv_context mv;
};

/**
 * struct v4l2_vp9_frame_symbol_counts - pointers to arrays of symbol counts
 *
 * @partition: partition counts.
 * @skip: skip counts.
 * @intra_inter: is inter-block counts.
 * @tx32p: TX32 counts.
 * @tx16p: TX16 counts.
 * @tx8p: TX8 counts.
 * @y_mode: Y prediction mode counts.
 * @uv_mode: UV prediction mode counts.
 * @comp: compound prediction mode counts.
 * @comp_ref: compound ref counts.
 * @single_ref: single ref counts.
 * @mv_mode: inter mode counts.
 * @filter: interpolation filter counts.
 * @mv_joint: motion vector joint counts.
 * @sign: motion vector sign counts.
 * @classes: motion vector class counts.
 * @class0: motion vector class0 bit counts.
 * @bits: motion vector bits counts.
 * @class0_fp: motion vector class0 fractional bit counts.
 * @fp: motion vector fractional bit counts.
 * @class0_hp: motion vector class0 high precision fractional bit counts.
 * @hp: motion vector high precision fractional bit counts.
 * @coeff: coefficient counts.
 * @eob: eob counts
 *
 * The fields correspond to what is specified in section 8.3 "Clear counts process" of the spec.
 * Different pieces of hardware can report the counts in different order, so we cannot rely on
 * simply overlaying a struct on a relevant block of memory. Instead we provide pointers to
 * arrays or array of pointers to arrays in case of coeff, or array of pointers for eob.
 */
struct v4l2_vp9_frame_symbol_counts {
	u32 (*partition)[16][4];
	u32 (*skip)[3][2];
	u32 (*intra_inter)[4][2];
	u32 (*tx32p)[2][4];
	u32 (*tx16p)[2][4];
	u32 (*tx8p)[2][2];
	u32 (*y_mode)[4][10];
	u32 (*uv_mode)[10][10];
	u32 (*comp)[5][2];
	u32 (*comp_ref)[5][2];
	u32 (*single_ref)[5][2][2];
	u32 (*mv_mode)[7][4];
	u32 (*filter)[4][3];
	u32 (*mv_joint)[4];
	u32 (*sign)[2][2];
	u32 (*classes)[2][11];
	u32 (*class0)[2][2];
	u32 (*bits)[2][10][2];
	u32 (*class0_fp)[2][2][4];
	u32 (*fp)[2][4];
	u32 (*class0_hp)[2][2];
	u32 (*hp)[2][2];
	u32 (*coeff[4][2][2][6][6])[3];
	u32 *eob[4][2][2][6][6][2];
};

extern const u8 v4l2_vp9_kf_y_mode_prob[10][10][9]; /* Section 10.4 of the spec */
extern const u8 v4l2_vp9_kf_partition_probs[16][3]; /* Section 10.4 of the spec */
extern const u8 v4l2_vp9_kf_uv_mode_prob[10][9]; /* Section 10.4 of the spec */
extern const struct v4l2_vp9_frame_context v4l2_vp9_default_probs; /* Section 10.5 of the spec */

/**
 * v4l2_vp9_fw_update_probs() - Perform forward update of vp9 probabilities
 *
 * @probs: current probabilities values
 * @deltas: delta values from compressed header
 * @dec_params: vp9 frame decoding parameters
 *
 * This function performs forward updates of probabilities for the vp9 boolean decoder.
 * The frame header can contain a directive to update the probabilities (deltas), if so, then
 * the deltas are provided in the header, too. The userspace parses those and passes the said
 * deltas struct to the kernel.
 */
void v4l2_vp9_fw_update_probs(struct v4l2_vp9_frame_context *probs,
			      const struct v4l2_ctrl_vp9_compressed_hdr *deltas,
			      const struct v4l2_ctrl_vp9_frame *dec_params);

/**
 * v4l2_vp9_reset_frame_ctx() - Reset appropriate frame context
 *
 * @dec_params: vp9 frame decoding parameters
 * @frame_context: array of the 4 frame contexts
 *
 * This function resets appropriate frame contexts, based on what's in dec_params.
 *
 * Returns the frame context index after the update, which might be reset to zero if
 * mandated by the spec.
 */
u8 v4l2_vp9_reset_frame_ctx(const struct v4l2_ctrl_vp9_frame *dec_params,
			    struct v4l2_vp9_frame_context *frame_context);

/**
 * v4l2_vp9_adapt_coef_probs() - Perform backward update of vp9 coefficients probabilities
 *
 * @probs: current probabilities values
 * @counts: values of symbol counts after the current frame has been decoded
 * @use_128: flag to request that 128 is used as update factor if true, otherwise 112 is used
 * @frame_is_intra: flag indicating that FrameIsIntra is true
 *
 * This function performs backward updates of coefficients probabilities for the vp9 boolean
 * decoder. After a frame has been decoded the counts of how many times a given symbol has
 * occurred are known and are used to update the probability of each symbol.
 */
void v4l2_vp9_adapt_coef_probs(struct v4l2_vp9_frame_context *probs,
			       struct v4l2_vp9_frame_symbol_counts *counts,
			       bool use_128,
			       bool frame_is_intra);

/**
 * v4l2_vp9_adapt_noncoef_probs() - Perform backward update of vp9 non-coefficients probabilities
 *
 * @probs: current probabilities values
 * @counts: values of symbol counts after the current frame has been decoded
 * @reference_mode: specifies the type of inter prediction to be used. See
 *	&v4l2_vp9_reference_mode for more details
 * @interpolation_filter: specifies the filter selection used for performing inter prediction.
 *	See &v4l2_vp9_interpolation_filter for more details
 * @tx_mode: specifies the TX mode. See &v4l2_vp9_tx_mode for more details
 * @flags: combination of V4L2_VP9_FRAME_FLAG_* flags
 *
 * This function performs backward updates of non-coefficients probabilities for the vp9 boolean
 * decoder. After a frame has been decoded the counts of how many times a given symbol has
 * occurred are known and are used to update the probability of each symbol.
 */
void v4l2_vp9_adapt_noncoef_probs(struct v4l2_vp9_frame_context *probs,
				  struct v4l2_vp9_frame_symbol_counts *counts,
				  u8 reference_mode, u8 interpolation_filter, u8 tx_mode,
				  u32 flags);

/**
 * v4l2_vp9_seg_feat_enabled() - Check if a segmentation feature is enabled
 *
 * @feature_enabled: array of 8-bit flags (for all segments)
 * @feature: id of the feature to check
 * @segid: id of the segment to look up
 *
 * This function returns true if a given feature is active in a given segment.
 */
bool
v4l2_vp9_seg_feat_enabled(const u8 *feature_enabled,
			  unsigned int feature,
			  unsigned int segid);

#endif /* _MEDIA_V4L2_VP9_H */
