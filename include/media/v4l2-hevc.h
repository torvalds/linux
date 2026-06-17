/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Helper functions for HEVC stateless codecs.
 */

#ifndef _MEDIA_V4L2_HEVC_H
#define _MEDIA_V4L2_HEVC_H

#include <linux/minmax.h>
#include <media/v4l2-ctrls.h>

/**
 * v4l2_hevc_pps_num_tile_columns - number of HEVC tile columns, bounded
 * @pps: the V4L2 HEVC PPS control
 *
 * Return the number of tile columns (num_tile_columns_minus1 + 1) clamped to
 * the capacity of column_width_minus1[]. The control validation already
 * rejects out-of-range counts; this keeps the consuming drivers bounded too.
 */
static inline unsigned int
v4l2_hevc_pps_num_tile_columns(const struct v4l2_ctrl_hevc_pps *pps)
{
	return min_t(unsigned int, pps->num_tile_columns_minus1 + 1,
		     ARRAY_SIZE(pps->column_width_minus1));
}

/**
 * v4l2_hevc_pps_num_tile_rows - number of HEVC tile rows, bounded
 * @pps: the V4L2 HEVC PPS control
 *
 * Return the number of tile rows (num_tile_rows_minus1 + 1) clamped to the
 * capacity of row_height_minus1[].
 */
static inline unsigned int
v4l2_hevc_pps_num_tile_rows(const struct v4l2_ctrl_hevc_pps *pps)
{
	return min_t(unsigned int, pps->num_tile_rows_minus1 + 1,
		     ARRAY_SIZE(pps->row_height_minus1));
}

#endif /* _MEDIA_V4L2_HEVC_H */
