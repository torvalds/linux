/*
 * Copyright(c) 2011-2017 Intel Corporation. All rights reserved.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#ifndef _GVT_PAGE_TRACK_H_
#define _GVT_PAGE_TRACK_H_

#include <linux/types.h>

struct intel_vgpu;
struct intel_vgpu_page_track;

typedef int (*gvt_page_track_handler_t)(
			struct intel_vgpu_page_track *page_track,
			u64 gpa, void *data, int bytes);

/* Track record for a write-protected guest page. */
struct intel_vgpu_page_track {
	gvt_page_track_handler_t handler;
	bool tracked;
	void *priv_data;
};

struct intel_vgpu_page_track *intel_vgpu_find_page_track(
		struct intel_vgpu *vgpu, unsigned long gfn);

int intel_vgpu_register_page_track(struct intel_vgpu *vgpu,
		unsigned long gfn, gvt_page_track_handler_t handler,
		void *priv);
void intel_vgpu_unregister_page_track(struct intel_vgpu *vgpu,
		unsigned long gfn);

int intel_vgpu_enable_page_track(struct intel_vgpu *vgpu, unsigned long gfn);
int intel_vgpu_disable_page_track(struct intel_vgpu *vgpu, unsigned long gfn);

int intel_vgpu_page_track_handler(struct intel_vgpu *vgpu, u64 gpa,
		void *data, unsigned int bytes);

#endif
