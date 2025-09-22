/*
 * Copyright 2022 Advanced Micro Devices, Inc.
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

#include "virtual_link_hwss.h"

void virtual_setup_stream_encoder(struct pipe_ctx *pipe_ctx)
{
}

void virtual_setup_stream_attribute(struct pipe_ctx *pipe_ctx)
{
}

void virtual_reset_stream_encoder(struct pipe_ctx *pipe_ctx)
{
}

static void virtual_disable_link_output(struct dc_link *link,
	const struct link_resource *link_res,
	enum amd_signal_type signal)
{
}

static const struct link_hwss virtual_link_hwss = {
	.setup_stream_encoder = virtual_setup_stream_encoder,
	.reset_stream_encoder = virtual_reset_stream_encoder,
	.setup_stream_attribute = virtual_setup_stream_attribute,
	.disable_link_output = virtual_disable_link_output,
};

const struct link_hwss *get_virtual_link_hwss(void)
{
	return &virtual_link_hwss;
}
