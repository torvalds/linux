/*
 * Copyright © 2014 Intel Corporation
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

#ifndef _I915_COMPONENT_H_
#define _I915_COMPONENT_H_

struct i915_audio_component {
	struct device *dev;

	const struct i915_audio_component_ops {
		struct module *owner;
		void (*get_power)(struct device *);
		void (*put_power)(struct device *);
		void (*codec_wake_override)(struct device *, bool enable);
		int (*get_cdclk_freq)(struct device *);
	} *ops;

	const struct i915_audio_component_audio_ops {
		void *audio_ptr;
		/**
		 * Call from i915 driver, notifying the HDA driver that
		 * pin sense and/or ELD information has changed.
		 * @audio_ptr:		HDA driver object
		 * @port:		Which port has changed (PORTA / PORTB / PORTC etc)
		 */
		void (*pin_eld_notify)(void *audio_ptr, int port);
	} *audio_ops;
};

#endif /* _I915_COMPONENT_H_ */
