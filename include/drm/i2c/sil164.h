/*
 * Copyright (C) 2010 Francisco Jerez.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef __DRM_I2C_SIL164_H__
#define __DRM_I2C_SIL164_H__

/**
 * struct sil164_encoder_params
 *
 * Describes how the sil164 is connected to the GPU. It should be used
 * as the @params parameter of its @set_config method.
 *
 * See "http://www.siliconimage.com/docs/SiI-DS-0021-E-164.pdf".
 */
struct sil164_encoder_params {
	enum {
		SIL164_INPUT_EDGE_FALLING = 0,
		SIL164_INPUT_EDGE_RISING
	} input_edge;

	enum {
		SIL164_INPUT_WIDTH_12BIT = 0,
		SIL164_INPUT_WIDTH_24BIT
	} input_width;

	enum {
		SIL164_INPUT_SINGLE_EDGE = 0,
		SIL164_INPUT_DUAL_EDGE
	} input_dual;

	enum {
		SIL164_PLL_FILTER_ON = 0,
		SIL164_PLL_FILTER_OFF,
	} pll_filter;

	int input_skew; /** < Allowed range [-4, 3], use 0 for no de-skew. */
	int duallink_skew; /** < Allowed range [-4, 3]. */
};

#endif
