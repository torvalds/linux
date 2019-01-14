/* SPDX-License-Identifier: GPL-2.0 */
/*
 * These are the MPEG2 state controls for use with stateless MPEG-2
 * codec drivers.
 *
 * It turns out that these structs are not stable yet and will undergo
 * more changes. So keep them private until they are stable and ready to
 * become part of the official public API.
 */

#ifndef _MPEG2_CTRLS_H_
#define _MPEG2_CTRLS_H_

#define V4L2_CID_MPEG_VIDEO_MPEG2_SLICE_PARAMS		(V4L2_CID_MPEG_BASE+250)
#define V4L2_CID_MPEG_VIDEO_MPEG2_QUANTIZATION		(V4L2_CID_MPEG_BASE+251)

/* enum v4l2_ctrl_type type values */
#define V4L2_CTRL_TYPE_MPEG2_SLICE_PARAMS 0x0103
#define	V4L2_CTRL_TYPE_MPEG2_QUANTIZATION 0x0104

#define V4L2_MPEG2_PICTURE_CODING_TYPE_I	1
#define V4L2_MPEG2_PICTURE_CODING_TYPE_P	2
#define V4L2_MPEG2_PICTURE_CODING_TYPE_B	3
#define V4L2_MPEG2_PICTURE_CODING_TYPE_D	4

struct v4l2_mpeg2_sequence {
	/* ISO/IEC 13818-2, ITU-T Rec. H.262: Sequence header */
	__u16	horizontal_size;
	__u16	vertical_size;
	__u32	vbv_buffer_size;

	/* ISO/IEC 13818-2, ITU-T Rec. H.262: Sequence extension */
	__u8	profile_and_level_indication;
	__u8	progressive_sequence;
	__u8	chroma_format;
	__u8	pad;
};

struct v4l2_mpeg2_picture {
	/* ISO/IEC 13818-2, ITU-T Rec. H.262: Picture header */
	__u8	picture_coding_type;

	/* ISO/IEC 13818-2, ITU-T Rec. H.262: Picture coding extension */
	__u8	f_code[2][2];
	__u8	intra_dc_precision;
	__u8	picture_structure;
	__u8	top_field_first;
	__u8	frame_pred_frame_dct;
	__u8	concealment_motion_vectors;
	__u8	q_scale_type;
	__u8	intra_vlc_format;
	__u8	alternate_scan;
	__u8	repeat_first_field;
	__u8	progressive_frame;
	__u8	pad;
};

struct v4l2_ctrl_mpeg2_slice_params {
	__u32	bit_size;
	__u32	data_bit_offset;

	struct v4l2_mpeg2_sequence sequence;
	struct v4l2_mpeg2_picture picture;

	/* ISO/IEC 13818-2, ITU-T Rec. H.262: Slice */
	__u8	quantiser_scale_code;

	__u8	backward_ref_index;
	__u8	forward_ref_index;
	__u8	pad;
};

struct v4l2_ctrl_mpeg2_quantization {
	/* ISO/IEC 13818-2, ITU-T Rec. H.262: Quant matrix extension */
	__u8	load_intra_quantiser_matrix;
	__u8	load_non_intra_quantiser_matrix;
	__u8	load_chroma_intra_quantiser_matrix;
	__u8	load_chroma_non_intra_quantiser_matrix;

	__u8	intra_quantiser_matrix[64];
	__u8	non_intra_quantiser_matrix[64];
	__u8	chroma_intra_quantiser_matrix[64];
	__u8	chroma_non_intra_quantiser_matrix[64];
};

#endif
