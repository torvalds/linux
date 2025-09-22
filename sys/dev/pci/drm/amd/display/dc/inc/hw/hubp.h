/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
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

#ifndef __DAL_HUBP_H__
#define __DAL_HUBP_H__

/**
 * DOC: overview
 *
 * Display Controller Hub (DCHUB) is the gateway between the Scalable Data Port
 * (SDP) and DCN. This component has multiple features, such as memory
 * arbitration, rotation, and cursor manipulation.
 *
 * There is one HUBP allocated per pipe, which fetches data and converts
 * different pixel formats (i.e. ARGB8888, NV12, etc) into linear, interleaved
 * and fixed-depth streams of pixel data.
 */

#include "mem_input.h"
#include "cursor_reg_cache.h"

#include "dml2/dml21/inc/dml_top_dchub_registers.h"

#define OPP_ID_INVALID 0xf
#define MAX_TTU 0xffffff

enum cursor_pitch {
	CURSOR_PITCH_64_PIXELS = 0,
	CURSOR_PITCH_128_PIXELS,
	CURSOR_PITCH_256_PIXELS
};

enum cursor_lines_per_chunk {
	CURSOR_LINE_PER_CHUNK_1 = 0, /* new for DCN2 */
	CURSOR_LINE_PER_CHUNK_2 = 1,
	CURSOR_LINE_PER_CHUNK_4,
	CURSOR_LINE_PER_CHUNK_8,
	CURSOR_LINE_PER_CHUNK_16
};

enum hubp_ind_block_size {
	hubp_ind_block_unconstrained = 0,
	hubp_ind_block_64b,
	hubp_ind_block_128b,
	hubp_ind_block_64b_no_128bcl,
};

enum hubp_3dlut_fl_mode {
	hubp_3dlut_fl_mode_disable = 0,
	hubp_3dlut_fl_mode_native_1 = 1,
	hubp_3dlut_fl_mode_native_2 = 2,
	hubp_3dlut_fl_mode_transform = 3
};

enum hubp_3dlut_fl_format {
	hubp_3dlut_fl_format_unorm_12msb_bitslice = 0,
	hubp_3dlut_fl_format_unorm_12lsb_bitslice = 1,
	hubp_3dlut_fl_format_float_fp1_5_10 = 2
};

enum hubp_3dlut_fl_addressing_mode {
	hubp_3dlut_fl_addressing_mode_sw_linear = 0,
	hubp_3dlut_fl_addressing_mode_simple_linear = 1
};

enum hubp_3dlut_fl_width {
	hubp_3dlut_fl_width_17 = 17,
	hubp_3dlut_fl_width_33 = 33,
	hubp_3dlut_fl_width_transformed = 4916
};

enum hubp_3dlut_fl_crossbar_bit_slice {
	hubp_3dlut_fl_crossbar_bit_slice_0_15 = 0,
	hubp_3dlut_fl_crossbar_bit_slice_16_31 = 1,
	hubp_3dlut_fl_crossbar_bit_slice_32_47 = 2,
	hubp_3dlut_fl_crossbar_bit_slice_48_63 = 3
};

struct hubp {
	const struct hubp_funcs *funcs;
	struct dc_context *ctx;
	struct dc_plane_address request_address;
	int inst;

	/* run time states */
	int opp_id;
	int mpcc_id;
	struct dc_cursor_attributes curs_attr;
	struct dc_cursor_position curs_pos;
	bool power_gated;

	struct cursor_position_cache_hubp  pos;
	struct cursor_attribute_cache_hubp att;
	struct cursor_rect cur_rect;
};

struct surface_flip_registers {
	uint32_t DCSURF_SURFACE_CONTROL;
	uint32_t DCSURF_PRIMARY_META_SURFACE_ADDRESS_HIGH;
	uint32_t DCSURF_PRIMARY_META_SURFACE_ADDRESS;
	uint32_t DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH;
	uint32_t DCSURF_PRIMARY_SURFACE_ADDRESS;
	uint32_t DCSURF_PRIMARY_META_SURFACE_ADDRESS_HIGH_C;
	uint32_t DCSURF_PRIMARY_META_SURFACE_ADDRESS_C;
	uint32_t DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH_C;
	uint32_t DCSURF_PRIMARY_SURFACE_ADDRESS_C;
	uint32_t DCSURF_SECONDARY_META_SURFACE_ADDRESS_HIGH;
	uint32_t DCSURF_SECONDARY_META_SURFACE_ADDRESS;
	uint32_t DCSURF_SECONDARY_SURFACE_ADDRESS_HIGH;
	uint32_t DCSURF_SECONDARY_SURFACE_ADDRESS;
	bool tmz_surface;
	bool immediate;
	uint8_t vmid;
	bool grph_stereo;
};

struct hubp_funcs {
	void (*hubp_setup)(
			struct hubp *hubp,
			struct _vcs_dpi_display_dlg_regs_st *dlg_regs,
			struct _vcs_dpi_display_ttu_regs_st *ttu_regs,
			struct _vcs_dpi_display_rq_regs_st *rq_regs,
			struct _vcs_dpi_display_pipe_dest_params_st *pipe_dest);

	void (*hubp_setup_interdependent)(
			struct hubp *hubp,
			struct _vcs_dpi_display_dlg_regs_st *dlg_regs,
			struct _vcs_dpi_display_ttu_regs_st *ttu_regs);

	void (*dcc_control)(struct hubp *hubp, bool enable,
			enum hubp_ind_block_size blk_size);

	void (*hubp_reset)(struct hubp *hubp);

	void (*mem_program_viewport)(
			struct hubp *hubp,
			const struct rect *viewport,
			const struct rect *viewport_c);

	bool (*hubp_program_surface_flip_and_addr)(
		struct hubp *hubp,
		const struct dc_plane_address *address,
		bool flip_immediate);

	void (*hubp_program_pte_vm)(
		struct hubp *hubp,
		enum surface_pixel_format format,
		union dc_tiling_info *tiling_info,
		enum dc_rotation_angle rotation);

	void (*hubp_set_vm_system_aperture_settings)(
			struct hubp *hubp,
			struct vm_system_aperture_param *apt);

	void (*hubp_set_vm_context0_settings)(
			struct hubp *hubp,
			const struct vm_context0_param *vm0);

	void (*hubp_program_surface_config)(
		struct hubp *hubp,
		enum surface_pixel_format format,
		union dc_tiling_info *tiling_info,
		struct plane_size *plane_size,
		enum dc_rotation_angle rotation,
		struct dc_plane_dcc_param *dcc,
		bool horizontal_mirror,
		unsigned int compa_level);

	bool (*hubp_is_flip_pending)(struct hubp *hubp);

	void (*set_blank)(struct hubp *hubp, bool blank);
	void (*set_blank_regs)(struct hubp *hubp, bool blank);
	void (*phantom_hubp_post_enable)(struct hubp *hubp);
	void (*set_hubp_blank_en)(struct hubp *hubp, bool blank);

	void (*set_cursor_attributes)(
			struct hubp *hubp,
			const struct dc_cursor_attributes *attr);

	void (*set_cursor_position)(
			struct hubp *hubp,
			const struct dc_cursor_position *pos,
			const struct dc_cursor_mi_param *param);

	void (*hubp_disconnect)(struct hubp *hubp);

	void (*hubp_clk_cntl)(struct hubp *hubp, bool enable);
	void (*hubp_vtg_sel)(struct hubp *hubp, uint32_t otg_inst);
	void (*hubp_read_state)(struct hubp *hubp);
	void (*hubp_clear_underflow)(struct hubp *hubp);
	void (*hubp_disable_control)(struct hubp *hubp, bool disable_hubp);
	unsigned int (*hubp_get_underflow_status)(struct hubp *hubp);
	void (*hubp_init)(struct hubp *hubp);

	void (*dmdata_set_attributes)(
			struct hubp *hubp,
			const struct dc_dmdata_attributes *attr);

	void (*dmdata_load)(
			struct hubp *hubp,
			uint32_t dmdata_sw_size,
			const uint32_t *dmdata_sw_data);
	bool (*dmdata_status_done)(struct hubp *hubp);
	void (*hubp_enable_tripleBuffer)(
		struct hubp *hubp,
		bool enable);

	bool (*hubp_is_triplebuffer_enabled)(
		struct hubp *hubp);

	void (*hubp_set_flip_control_surface_gsl)(
		struct hubp *hubp,
		bool enable);

	void (*validate_dml_output)(
			struct hubp *hubp,
			struct dc_context *ctx,
			struct _vcs_dpi_display_rq_regs_st *dml_rq_regs,
			struct _vcs_dpi_display_dlg_regs_st *dml_dlg_attr,
			struct _vcs_dpi_display_ttu_regs_st *dml_ttu_attr);
	void (*set_unbounded_requesting)(
		struct hubp *hubp,
		bool enable);
	bool (*hubp_in_blank)(struct hubp *hubp);
	void (*hubp_soft_reset)(struct hubp *hubp, bool reset);

	void (*hubp_set_flip_int)(struct hubp *hubp);

	void (*hubp_update_force_pstate_disallow)(struct hubp *hubp, bool allow);
	void (*hubp_update_force_cursor_pstate_disallow)(struct hubp *hubp, bool allow);
	void (*hubp_update_mall_sel)(struct hubp *hubp, uint32_t mall_sel, bool c_cursor);
	void (*hubp_prepare_subvp_buffering)(struct hubp *hubp, bool enable);
	void (*hubp_surface_update_lock)(struct hubp *hubp,
			bool lock);

	void (*program_extended_blank)(struct hubp *hubp,
			unsigned int min_dst_y_next_start_optimized);

	void (*hubp_wait_pipe_read_start)(struct hubp *hubp);
	void (*hubp_program_mcache_id_and_split_coordinate)(struct hubp *hubp, struct dml2_hubp_pipe_mcache_regs *mcache_regs);
	void (*hubp_update_3dlut_fl_bias_scale)(struct hubp *hubp, uint16_t bias, uint16_t scale);
	void (*hubp_program_3dlut_fl_mode)(struct hubp *hubp,
			enum hubp_3dlut_fl_mode mode);
	void (*hubp_program_3dlut_fl_format)(struct hubp *hubp,
			enum hubp_3dlut_fl_format format);
	void (*hubp_program_3dlut_fl_addr)(struct hubp *hubp,
		const struct dc_plane_address address);
	void (*hubp_program_3dlut_fl_dlg_param)(struct hubp *hubp, int refcyc_per_3dlut_group);
	void (*hubp_enable_3dlut_fl)(struct hubp *hubp, bool enable);
	void (*hubp_program_3dlut_fl_addressing_mode)(struct hubp *hubp, enum hubp_3dlut_fl_addressing_mode addr_mode);
	void (*hubp_program_3dlut_fl_width)(struct hubp *hubp, enum hubp_3dlut_fl_width width);
	void (*hubp_program_3dlut_fl_tmz_protected)(struct hubp *hubp, bool protection_enabled);
	void (*hubp_program_3dlut_fl_crossbar)(struct hubp *hubp,
			enum hubp_3dlut_fl_crossbar_bit_slice bit_slice_y_g,
			enum hubp_3dlut_fl_crossbar_bit_slice bit_slice_cb_b,
			enum hubp_3dlut_fl_crossbar_bit_slice bit_slice_cr_r);
	int (*hubp_get_3dlut_fl_done)(struct hubp *hubp);
};

#endif
