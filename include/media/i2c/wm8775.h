/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
    wm8775.h - definition for wm8775 inputs and outputs

    Copyright (C) 2006 Hans Verkuil (hverkuil@xs4all.nl)

*/

#ifndef _WM8775_H_
#define _WM8775_H_

/* The WM8775 has 4 inputs and one output. Zero or more inputs
   are multiplexed together to the output. Hence there are
   16 combinations.
   If only one input is active (the normal case) then the
   input values 1, 2, 4 or 8 should be used. */

#define WM8775_AIN1 1
#define WM8775_AIN2 2
#define WM8775_AIN3 4
#define WM8775_AIN4 8


struct wm8775_platform_data {
	/*
	 * FIXME: Instead, we should parameterize the params
	 * that need different settings between ivtv, pvrusb2, and Nova-S
	 */
	bool is_nova_s;
};

#endif
