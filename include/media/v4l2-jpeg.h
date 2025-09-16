/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * V4L2 JPEG helpers header
 *
 * Copyright (C) 2019 Pengutronix, Philipp Zabel <kernel@pengutronix.de>
 *
 * For reference, see JPEG ITU-T.81 (ISO/IEC 10918-1)
 */

#ifndef _V4L2_JPEG_H
#define _V4L2_JPEG_H

#include <linux/v4l2-controls.h>

#define V4L2_JPEG_MAX_COMPONENTS	4
#define V4L2_JPEG_MAX_TABLES		4
/*
 * Prefixes used to generate huffman table class and destination identifiers as
 * described below:
 *
 * V4L2_JPEG_LUM_HT | V4L2_JPEG_DC_HT : Prefix for Luma DC coefficients
 *					huffman table
 * V4L2_JPEG_LUM_HT | V4L2_JPEG_AC_HT : Prefix for Luma AC coefficients
 *					huffman table
 * V4L2_JPEG_CHR_HT | V4L2_JPEG_DC_HT : Prefix for Chroma DC coefficients
 *					huffman table
 * V4L2_JPEG_CHR_HT | V4L2_JPEG_AC_HT : Prefix for Chroma AC coefficients
 *					huffman table
 */
#define V4L2_JPEG_LUM_HT		0x00
#define V4L2_JPEG_CHR_HT		0x01
#define V4L2_JPEG_DC_HT			0x00
#define V4L2_JPEG_AC_HT			0x10

/* Length of reference huffman tables as provided in Table K.3 of ITU-T.81 */
#define V4L2_JPEG_REF_HT_AC_LEN		178
#define V4L2_JPEG_REF_HT_DC_LEN		28

/* Array size for 8x8 block of samples or DCT coefficient */
#define V4L2_JPEG_PIXELS_IN_BLOCK	64

/**
 * struct v4l2_jpeg_reference - reference into the JPEG buffer
 * @start: pointer to the start of the referenced segment or table
 * @length: size of the referenced segment or table
 *
 * Wnen referencing marker segments, start points right after the marker code,
 * and length is the size of the segment parameters, excluding the marker code.
 */
struct v4l2_jpeg_reference {
	u8 *start;
	size_t length;
};

/* B.2.2 Frame header syntax */

/**
 * struct v4l2_jpeg_frame_component_spec - frame component-specification
 * @component_identifier: C[i]
 * @horizontal_sampling_factor: H[i]
 * @vertical_sampling_factor: V[i]
 * @quantization_table_selector: quantization table destination selector Tq[i]
 */
struct v4l2_jpeg_frame_component_spec {
	u8 component_identifier;
	u8 horizontal_sampling_factor;
	u8 vertical_sampling_factor;
	u8 quantization_table_selector;
};

/**
 * struct v4l2_jpeg_frame_header - JPEG frame header
 * @height: Y
 * @width: X
 * @precision: P
 * @num_components: Nf
 * @component: component-specification, see v4l2_jpeg_frame_component_spec
 * @subsampling: decoded subsampling from component-specification
 */
struct v4l2_jpeg_frame_header {
	u16 height;
	u16 width;
	u8 precision;
	u8 num_components;
	struct v4l2_jpeg_frame_component_spec component[V4L2_JPEG_MAX_COMPONENTS];
	enum v4l2_jpeg_chroma_subsampling subsampling;
};

/* B.2.3 Scan header syntax */

/**
 * struct v4l2_jpeg_scan_component_spec - scan component-specification
 * @component_selector: Cs[j]
 * @dc_entropy_coding_table_selector: Td[j]
 * @ac_entropy_coding_table_selector: Ta[j]
 */
struct v4l2_jpeg_scan_component_spec {
	u8 component_selector;
	u8 dc_entropy_coding_table_selector;
	u8 ac_entropy_coding_table_selector;
};

/**
 * struct v4l2_jpeg_scan_header - JPEG scan header
 * @num_components: Ns
 * @component: component-specification, see v4l2_jpeg_scan_component_spec
 */
struct v4l2_jpeg_scan_header {
	u8 num_components;				/* Ns */
	struct v4l2_jpeg_scan_component_spec component[V4L2_JPEG_MAX_COMPONENTS];
	/* Ss, Se, Ah, and Al are not used by any driver */
};

/**
 * enum v4l2_jpeg_app14_tf - APP14 transform flag
 * According to Rec. ITU-T T.872 (06/2012) 6.5.3
 * APP14 segment is for color encoding, it contains a transform flag,
 * which may have values of 0, 1 and 2 and are interpreted as follows:
 * @V4L2_JPEG_APP14_TF_CMYK_RGB: CMYK for images encoded with four components
 *                               RGB for images encoded with three components
 * @V4L2_JPEG_APP14_TF_YCBCR: an image encoded with three components using YCbCr
 * @V4L2_JPEG_APP14_TF_YCCK: an image encoded with four components using YCCK
 * @V4L2_JPEG_APP14_TF_UNKNOWN: indicate app14 is not present
 */
enum v4l2_jpeg_app14_tf {
	V4L2_JPEG_APP14_TF_CMYK_RGB	= 0,
	V4L2_JPEG_APP14_TF_YCBCR	= 1,
	V4L2_JPEG_APP14_TF_YCCK		= 2,
	V4L2_JPEG_APP14_TF_UNKNOWN	= -1,
};

/**
 * struct v4l2_jpeg_header - parsed JPEG header
 * @sof: pointer to frame header and size
 * @sos: pointer to scan header and size
 * @num_dht: number of entries in @dht
 * @dht: pointers to huffman tables and sizes
 * @num_dqt: number of entries in @dqt
 * @dqt: pointers to quantization tables and sizes
 * @frame: parsed frame header
 * @scan: pointer to parsed scan header, optional
 * @quantization_tables: references to four quantization tables, optional
 * @huffman_tables: references to four Huffman tables in DC0, DC1, AC0, AC1
 *                  order, optional
 * @restart_interval: number of MCU per restart interval, Ri
 * @ecs_offset: buffer offset in bytes to the entropy coded segment
 * @app14_tf: transform flag from app14 data
 *
 * When this structure is passed to v4l2_jpeg_parse_header, the optional scan,
 * quantization_tables, and huffman_tables pointers must be initialized to NULL
 * or point at valid memory.
 */
struct v4l2_jpeg_header {
	struct v4l2_jpeg_reference sof;
	struct v4l2_jpeg_reference sos;
	unsigned int num_dht;
	struct v4l2_jpeg_reference dht[V4L2_JPEG_MAX_TABLES];
	unsigned int num_dqt;
	struct v4l2_jpeg_reference dqt[V4L2_JPEG_MAX_TABLES];

	struct v4l2_jpeg_frame_header frame;
	struct v4l2_jpeg_scan_header *scan;
	struct v4l2_jpeg_reference *quantization_tables;
	struct v4l2_jpeg_reference *huffman_tables;
	u16 restart_interval;
	size_t ecs_offset;
	enum v4l2_jpeg_app14_tf app14_tf;
};

int v4l2_jpeg_parse_header(void *buf, size_t len, struct v4l2_jpeg_header *out);

extern const u8 v4l2_jpeg_zigzag_scan_index[V4L2_JPEG_PIXELS_IN_BLOCK];
extern const u8 v4l2_jpeg_ref_table_luma_qt[V4L2_JPEG_PIXELS_IN_BLOCK];
extern const u8 v4l2_jpeg_ref_table_chroma_qt[V4L2_JPEG_PIXELS_IN_BLOCK];
extern const u8 v4l2_jpeg_ref_table_luma_dc_ht[V4L2_JPEG_REF_HT_DC_LEN];
extern const u8 v4l2_jpeg_ref_table_luma_ac_ht[V4L2_JPEG_REF_HT_AC_LEN];
extern const u8 v4l2_jpeg_ref_table_chroma_dc_ht[V4L2_JPEG_REF_HT_DC_LEN];
extern const u8 v4l2_jpeg_ref_table_chroma_ac_ht[V4L2_JPEG_REF_HT_AC_LEN];

#endif
