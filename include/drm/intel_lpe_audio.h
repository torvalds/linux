/*
 * Copyright © 2016 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef _INTEL_LPE_AUDIO_H_
#define _INTEL_LPE_AUDIO_H_

#include <linux/types.h>
#include <linux/spinlock_types.h>

#define HDMI_MAX_ELD_BYTES	128

struct intel_hdmi_lpe_audio_eld {
	int port_id;
	int pipe_id;
	unsigned char eld_data[HDMI_MAX_ELD_BYTES];
};

struct intel_hdmi_lpe_audio_pdata {
	bool notify_pending;
	int tmds_clock_speed;
	bool hdmi_connected;
	bool dp_output;
	int link_rate;
	struct intel_hdmi_lpe_audio_eld eld;
	void (*notify_audio_lpe)(void *audio_ptr);
	spinlock_t lpe_audio_slock;
};

#endif /* _I915_LPE_AUDIO_H_ */
