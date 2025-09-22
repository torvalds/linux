/* Copyright 2012-15 Advanced Micro Devices, Inc.
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

#ifndef __DC_DMCU_H__
#define __DC_DMCU_H__

#include "dm_services_types.h"

/* If HW itself ever powered down it will be 0.
 * fwDmcuInit will write to 1.
 * Driver will only call MCP init if current state is 1,
 * and the MCP command will transition this to 2.
 */
enum dmcu_state {
	DMCU_UNLOADED = 0,
	DMCU_LOADED_UNINITIALIZED = 1,
	DMCU_RUNNING = 2,
};

struct dmcu_version {
	unsigned int interface_version;
	unsigned int abm_version;
	unsigned int psr_version;
	unsigned int build_version;
};

struct dmcu {
	struct dc_context *ctx;
	const struct dmcu_funcs *funcs;

	enum dmcu_state dmcu_state;
	struct dmcu_version dmcu_version;
	unsigned int cached_wait_loop_number;
	uint32_t psp_version;
	bool auto_load_dmcu;
};

struct dmcu_funcs {
	bool (*dmcu_init)(struct dmcu *dmcu);
	bool (*load_iram)(struct dmcu *dmcu,
			unsigned int start_offset,
			const char *src,
			unsigned int bytes);
	void (*set_psr_enable)(struct dmcu *dmcu, bool enable, bool wait);
	bool (*setup_psr)(struct dmcu *dmcu,
			struct dc_link *link,
			struct psr_context *psr_context);
	void (*get_psr_state)(struct dmcu *dmcu, enum dc_psr_state *dc_psr_state);
	void (*set_psr_wait_loop)(struct dmcu *dmcu,
			unsigned int wait_loop_number);
	void (*get_psr_wait_loop)(struct dmcu *dmcu,
			unsigned int *psr_wait_loop_number);
	bool (*is_dmcu_initialized)(struct dmcu *dmcu);
	bool (*lock_phy)(struct dmcu *dmcu);
	bool (*unlock_phy)(struct dmcu *dmcu);
	bool (*send_edid_cea)(struct dmcu *dmcu,
			int offset,
			int total_length,
			uint8_t *data,
			int length);
	bool (*recv_amd_vsdb)(struct dmcu *dmcu,
			int *version,
			int *min_frame_rate,
			int *max_frame_rate);
	bool (*recv_edid_cea_ack)(struct dmcu *dmcu, int *offset);
#if defined(CONFIG_DRM_AMD_SECURE_DISPLAY)
	void (*forward_crc_window)(struct dmcu *dmcu,
			struct rect *rect,
			struct otg_phy_mux *mux_mapping);
	void (*stop_crc_win_update)(struct dmcu *dmcu,
			struct otg_phy_mux *mux_mapping);
#endif
};

#endif
