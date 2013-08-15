/*
 * v4l2-dv-timings - Internal header with dv-timings helper functions
 *
 * Copyright 2013 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#ifndef __V4L2_DV_TIMINGS_H
#define __V4L2_DV_TIMINGS_H

#include <linux/videodev2.h>

/** v4l2_dv_valid_timings() - are these timings valid?
  * @t:	  the v4l2_dv_timings struct.
  * @cap: the v4l2_dv_timings_cap capabilities.
  *
  * Returns true if the given dv_timings struct is supported by the
  * hardware capabilities, returns false otherwise.
  */
bool v4l2_dv_valid_timings(const struct v4l2_dv_timings *t,
			   const struct v4l2_dv_timings_cap *cap);

/** v4l2_enum_dv_timings_cap() - Helper function to enumerate possible DV timings based on capabilities
  * @t:	  the v4l2_enum_dv_timings struct.
  * @cap: the v4l2_dv_timings_cap capabilities.
  *
  * This enumerates dv_timings using the full list of possible CEA-861 and DMT
  * timings, filtering out any timings that are not supported based on the
  * hardware capabilities.
  *
  * If a valid timing for the given index is found, it will fill in @t and
  * return 0, otherwise it returns -EINVAL.
  */
int v4l2_enum_dv_timings_cap(struct v4l2_enum_dv_timings *t,
			     const struct v4l2_dv_timings_cap *cap);

/** v4l2_find_dv_timings_cap() - Find the closest timings struct
  * @t:	  the v4l2_enum_dv_timings struct.
  * @cap: the v4l2_dv_timings_cap capabilities.
  * @pclock_delta: maximum delta between t->pixelclock and the timing struct
  *		under consideration.
  *
  * This function tries to map the given timings to an entry in the
  * full list of possible CEA-861 and DMT timings, filtering out any timings
  * that are not supported based on the hardware capabilities.
  *
  * On success it will fill in @t with the found timings and it returns true.
  * On failure it will return false.
  */
bool v4l2_find_dv_timings_cap(struct v4l2_dv_timings *t,
			      const struct v4l2_dv_timings_cap *cap,
			      unsigned pclock_delta);

/** v4l2_match_dv_timings() - do two timings match?
  * @measured:	  the measured timings data.
  * @standard:	  the timings according to the standard.
  * @pclock_delta: maximum delta in Hz between standard->pixelclock and
  * 		the measured timings.
  *
  * Returns true if the two timings match, returns false otherwise.
  */
bool v4l2_match_dv_timings(const struct v4l2_dv_timings *measured,
			   const struct v4l2_dv_timings *standard,
			   unsigned pclock_delta);

/** v4l2_print_dv_timings() - log the contents of a dv_timings struct
  * @dev_prefix:device prefix for each log line.
  * @prefix:	additional prefix for each log line, may be NULL.
  * @t:		the timings data.
  * @detailed:	if true, give a detailed log.
  */
void v4l2_print_dv_timings(const char *dev_prefix, const char *prefix,
			   const struct v4l2_dv_timings *t, bool detailed);

/** v4l2_detect_cvt - detect if the given timings follow the CVT standard
 * @frame_height - the total height of the frame (including blanking) in lines.
 * @hfreq - the horizontal frequency in Hz.
 * @vsync - the height of the vertical sync in lines.
 * @polarities - the horizontal and vertical polarities (same as struct
 *		v4l2_bt_timings polarities).
 * @fmt - the resulting timings.
 *
 * This function will attempt to detect if the given values correspond to a
 * valid CVT format. If so, then it will return true, and fmt will be filled
 * in with the found CVT timings.
 */
bool v4l2_detect_cvt(unsigned frame_height, unsigned hfreq, unsigned vsync,
		u32 polarities, struct v4l2_dv_timings *fmt);

/** v4l2_detect_gtf - detect if the given timings follow the GTF standard
 * @frame_height - the total height of the frame (including blanking) in lines.
 * @hfreq - the horizontal frequency in Hz.
 * @vsync - the height of the vertical sync in lines.
 * @polarities - the horizontal and vertical polarities (same as struct
 *		v4l2_bt_timings polarities).
 * @aspect - preferred aspect ratio. GTF has no method of determining the
 *		aspect ratio in order to derive the image width from the
 *		image height, so it has to be passed explicitly. Usually
 *		the native screen aspect ratio is used for this. If it
 *		is not filled in correctly, then 16:9 will be assumed.
 * @fmt - the resulting timings.
 *
 * This function will attempt to detect if the given values correspond to a
 * valid GTF format. If so, then it will return true, and fmt will be filled
 * in with the found GTF timings.
 */
bool v4l2_detect_gtf(unsigned frame_height, unsigned hfreq, unsigned vsync,
		u32 polarities, struct v4l2_fract aspect,
		struct v4l2_dv_timings *fmt);

/** v4l2_calc_aspect_ratio - calculate the aspect ratio based on bytes
 *	0x15 and 0x16 from the EDID.
 * @hor_landscape - byte 0x15 from the EDID.
 * @vert_portrait - byte 0x16 from the EDID.
 *
 * Determines the aspect ratio from the EDID.
 * See VESA Enhanced EDID standard, release A, rev 2, section 3.6.2:
 * "Horizontal and Vertical Screen Size or Aspect Ratio"
 */
struct v4l2_fract v4l2_calc_aspect_ratio(u8 hor_landscape, u8 vert_portrait);

#endif
