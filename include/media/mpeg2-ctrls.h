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

#define V4L2_CID_MPEG_VIDEO_MPEG2_SLICE_PARAMS		(V4L2_CID_CODEC_BASE+250)
#define V4L2_CID_MPEG_VIDEO_MPEG2_QUANTISATION		(V4L2_CID_CODEC_BASE+251)

/* enum v4l2_ctrl_type type values */
#define V4L2_CTRL_TYPE_MPEG2_SLICE_PARAMS 0x0103
#define	V4L2_CTRL_TYPE_MPEG2_QUANTISATION 0x0104

#define V4L2_MPEG2_SEQ_FLAG_PROGRESSIVE		0x0001

struct v4l2_mpeg2_sequence {
	/* ISO/IEC 13818-2, ITU-T Rec. H.262: Sequence header */
	__u16	horizontal_size;
	__u16	vertical_size;
	__u32	vbv_buffer_size;

	/* ISO/IEC 13818-2, ITU-T Rec. H.262: Sequence extension */
	__u16	profile_and_level_indication;
	__u8	chroma_format;

	__u32	flags;
};

#define V4L2_MPEG2_PIC_CODING_TYPE_I			1
#define V4L2_MPEG2_PIC_CODING_TYPE_P			2
#define V4L2_MPEG2_PIC_CODING_TYPE_B			3
#define V4L2_MPEG2_PIC_CODING_TYPE_D			4

#define V4L2_MPEG2_PIC_TOP_FIELD			0x1
#define V4L2_MPEG2_PIC_BOTTOM_FIELD			0x2
#define V4L2_MPEG2_PIC_FRAME				0x3

#define V4L2_MPEG2_PIC_FLAG_TOP_FIELD_FIRST		0x0001
#define V4L2_MPEG2_PIC_FLAG_FRAME_PRED_DCT		0x0002
#define V4L2_MPEG2_PIC_FLAG_CONCEALMENT_MV		0x0004
#define V4L2_MPEG2_PIC_FLAG_Q_SCALE_TYPE		0x0008
#define V4L2_MPEG2_PIC_FLAG_INTRA_VLC			0x0010
#define V4L2_MPEG2_PIC_FLAG_ALT_SCAN			0x0020
#define V4L2_MPEG2_PIC_FLAG_REPEAT_FIRST		0x0040
#define V4L2_MPEG2_PIC_FLAG_PROGRESSIVE			0x0080

struct v4l2_mpeg2_picture {
	/* ISO/IEC 13818-2, ITU-T Rec. H.262: Picture header */
	__u8	picture_coding_type;

	/* ISO/IEC 13818-2, ITU-T Rec. H.262: Picture coding extension */
	__u8	f_code[2][2];
	__u8	intra_dc_precision;
	__u8	picture_structure;

	__u32	flags;
};

struct v4l2_ctrl_mpeg2_slice_params {
	__u32	bit_size;
	__u32	data_bit_offset;
	__u64	backward_ref_ts;
	__u64	forward_ref_ts;

	struct v4l2_mpeg2_sequence sequence;
	struct v4l2_mpeg2_picture picture;

	/* ISO/IEC 13818-2, ITU-T Rec. H.262: Slice */
	__u32	quantiser_scale_code;
};

struct v4l2_ctrl_mpeg2_quantisation {
	/* ISO/IEC 13818-2, ITU-T Rec. H.262: Quant matrix extension */
	__u8	intra_quantiser_matrix[64];
	__u8	non_intra_quantiser_matrix[64];
	__u8	chroma_intra_quantiser_matrix[64];
	__u8	chroma_non_intra_quantiser_matrix[64];
};

#endif
