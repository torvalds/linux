/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Helper functions for H264 codecs.
 *
 * Copyright (c) 2019 Collabora, Ltd.
 *
 * Author: Boris Brezillon <boris.brezillon@collabora.com>
 */

#ifndef _MEDIA_V4L2_H264_H
#define _MEDIA_V4L2_H264_H

#include <media/v4l2-ctrls.h>

/**
 * struct v4l2_h264_reflist_builder - Reference list builder object
 *
 * @refs.pic_order_count: reference picture order count
 * @refs.frame_num: reference frame number
 * @refs.pic_num: reference picture number
 * @refs.longterm: set to true for a long term reference
 * @refs: array of references
 * @cur_pic_order_count: picture order count of the frame being decoded
 * @unordered_reflist: unordered list of references. Will be used to generate
 *		       ordered P/B0/B1 lists
 * @num_valid: number of valid references in the refs array
 *
 * This object stores the context of the P/B0/B1 reference list builder.
 * This procedure is described in section '8.2.4 Decoding process for reference
 * picture lists construction' of the H264 spec.
 */
struct v4l2_h264_reflist_builder {
	struct {
		s32 pic_order_count;
		int frame_num;
		u32 pic_num;
		u16 longterm : 1;
	} refs[V4L2_H264_NUM_DPB_ENTRIES];
	s32 cur_pic_order_count;
	u8 unordered_reflist[V4L2_H264_NUM_DPB_ENTRIES];
	u8 num_valid;
};

void
v4l2_h264_init_reflist_builder(struct v4l2_h264_reflist_builder *b,
		const struct v4l2_ctrl_h264_decode_params *dec_params,
		const struct v4l2_ctrl_h264_sps *sps,
		const struct v4l2_h264_dpb_entry dpb[V4L2_H264_NUM_DPB_ENTRIES]);

/**
 * v4l2_h264_build_b_ref_lists() - Build the B0/B1 reference lists
 *
 * @builder: reference list builder context
 * @b0_reflist: 16-bytes array used to store the B0 reference list. Each entry
 *		is an index in the DPB
 * @b1_reflist: 16-bytes array used to store the B1 reference list. Each entry
 *		is an index in the DPB
 *
 * This functions builds the B0/B1 reference lists. This procedure is described
 * in section '8.2.4 Decoding process for reference picture lists construction'
 * of the H264 spec. This function can be used by H264 decoder drivers that
 * need to pass B0/B1 reference lists to the hardware.
 */
void
v4l2_h264_build_b_ref_lists(const struct v4l2_h264_reflist_builder *builder,
			    u8 *b0_reflist, u8 *b1_reflist);

/**
 * v4l2_h264_build_p_ref_list() - Build the P reference list
 *
 * @builder: reference list builder context
 * @reflist: 16-bytes array used to store the P reference list. Each entry
 *	     is an index in the DPB
 *
 * This functions builds the P reference lists. This procedure is describe in
 * section '8.2.4 Decoding process for reference picture lists construction'
 * of the H264 spec. This function can be used by H264 decoder drivers that
 * need to pass a P reference list to the hardware.
 */
void
v4l2_h264_build_p_ref_list(const struct v4l2_h264_reflist_builder *builder,
			   u8 *reflist);

#endif /* _MEDIA_V4L2_H264_H */
