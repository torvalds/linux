/*
 * Copyright (C) 2013 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _VIDEO_ADF_FORMAT_H
#define _VIDEO_ADF_FORMAT_H

bool adf_format_is_standard(u32 format);
bool adf_format_is_rgb(u32 format);
u8 adf_format_num_planes(u32 format);
u8 adf_format_bpp(u32 format);
u8 adf_format_plane_cpp(u32 format, int plane);
u8 adf_format_horz_chroma_subsampling(u32 format);
u8 adf_format_vert_chroma_subsampling(u32 format);

#endif /* _VIDEO_ADF_FORMAT_H */
