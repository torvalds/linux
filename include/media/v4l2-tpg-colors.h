/*
 * v4l2-tpg-colors.h - Color definitions for the test pattern generator
 *
 * Copyright 2014 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
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
 */

#ifndef _V4L2_TPG_COLORS_H_
#define _V4L2_TPG_COLORS_H_

struct color {
	unsigned char r, g, b;
};

struct color16 {
	int r, g, b;
};

enum tpg_color {
	TPG_COLOR_CSC_WHITE,
	TPG_COLOR_CSC_YELLOW,
	TPG_COLOR_CSC_CYAN,
	TPG_COLOR_CSC_GREEN,
	TPG_COLOR_CSC_MAGENTA,
	TPG_COLOR_CSC_RED,
	TPG_COLOR_CSC_BLUE,
	TPG_COLOR_CSC_BLACK,
	TPG_COLOR_75_YELLOW,
	TPG_COLOR_75_CYAN,
	TPG_COLOR_75_GREEN,
	TPG_COLOR_75_MAGENTA,
	TPG_COLOR_75_RED,
	TPG_COLOR_75_BLUE,
	TPG_COLOR_100_WHITE,
	TPG_COLOR_100_YELLOW,
	TPG_COLOR_100_CYAN,
	TPG_COLOR_100_GREEN,
	TPG_COLOR_100_MAGENTA,
	TPG_COLOR_100_RED,
	TPG_COLOR_100_BLUE,
	TPG_COLOR_100_BLACK,
	TPG_COLOR_TEXTFG,
	TPG_COLOR_TEXTBG,
	TPG_COLOR_RANDOM,
	TPG_COLOR_RAMP,
	TPG_COLOR_MAX = TPG_COLOR_RAMP + 256
};

extern const struct color tpg_colors[TPG_COLOR_MAX];
extern const unsigned short tpg_rec709_to_linear[255 * 16 + 1];
extern const unsigned short tpg_linear_to_rec709[255 * 16 + 1];
extern const struct color16 tpg_csc_colors[V4L2_COLORSPACE_DCI_P3 + 1]
					  [V4L2_XFER_FUNC_SMPTE2084 + 1]
					  [TPG_COLOR_CSC_BLACK + 1];

#endif
