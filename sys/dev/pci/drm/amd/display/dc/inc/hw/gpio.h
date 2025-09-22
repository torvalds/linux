/*
 * Copyright 2012-16 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#ifndef __DAL_GPIO_H__
#define __DAL_GPIO_H__

#include "gpio_types.h"


union gpio_hw_container {
	struct hw_ddc *ddc;
	struct hw_generic *generic;
	struct hw_hpd *hpd;
};

struct gpio {
	struct gpio_service *service;
	struct hw_gpio_pin *pin;
	enum gpio_id id;
	uint32_t en;

	union gpio_hw_container hw_container;
	enum gpio_mode mode;

	/* when GPIO comes from VBIOS, it has defined output state */
	enum gpio_pin_output_state output_state;
};

#if 0
struct gpio_funcs {

	struct hw_gpio_pin *(*create_ddc_data)(
		struct dc_context *ctx,
		enum gpio_id id,
		uint32_t en);
	struct hw_gpio_pin *(*create_ddc_clock)(
		struct dc_context *ctx,
		enum gpio_id id,
		uint32_t en);
	struct hw_gpio_pin *(*create_generic)(
		struct dc_context *ctx,
		enum gpio_id id,
		uint32_t en);
	struct hw_gpio_pin *(*create_hpd)(
		struct dc_context *ctx,
		enum gpio_id id,
		uint32_t en);
	struct hw_gpio_pin *(*create_gpio_pad)(
		struct dc_context *ctx,
		enum gpio_id id,
		uint32_t en);
	struct hw_gpio_pin *(*create_sync)(
		struct dc_context *ctx,
		enum gpio_id id,
		uint32_t en);
	struct hw_gpio_pin *(*create_gsl)(
		struct dc_context *ctx,
		enum gpio_id id,
		uint32_t en);

	/* HW translation */
	bool (*offset_to_id)(
		uint32_t offset,
		uint32_t mask,
		enum gpio_id *id,
		uint32_t *en);
	bool (*id_to_offset)(
		enum gpio_id id,
		uint32_t en,
		struct gpio_pin_info *info);
};
#endif

#endif  /* __DAL_GPIO__ */
