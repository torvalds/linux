// SPDX-License-Identifier: MIT
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

#include <drm/drm_atomic_helper.h>
#include <drm/drm_blend.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_fourcc.h>

#include "amdgpu.h"
#include "dal_asic_id.h"
#include "amdgpu_display.h"
#include "amdgpu_dm_trace.h"
#include "amdgpu_dm_plane.h"
#include "gc/gc_11_0_0_offset.h"
#include "gc/gc_11_0_0_sh_mask.h"

/*
 * TODO: these are currently initialized to rgb formats only.
 * For future use cases we should either initialize them dynamically based on
 * plane capabilities, or initialize this array to all formats, so internal drm
 * check will succeed, and let DC implement proper check
 */
static const uint32_t rgb_formats[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_RGBA8888,
	DRM_FORMAT_XRGB2101010,
	DRM_FORMAT_XBGR2101010,
	DRM_FORMAT_ARGB2101010,
	DRM_FORMAT_ABGR2101010,
	DRM_FORMAT_XRGB16161616,
	DRM_FORMAT_XBGR16161616,
	DRM_FORMAT_ARGB16161616,
	DRM_FORMAT_ABGR16161616,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_RGB565,
};

static const uint32_t overlay_formats[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_RGBA8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_NV21,
	DRM_FORMAT_NV12,
	DRM_FORMAT_P010
};

static const uint32_t video_formats[] = {
	DRM_FORMAT_NV21,
	DRM_FORMAT_NV12,
	DRM_FORMAT_P010
};

static const u32 cursor_formats[] = {
	DRM_FORMAT_ARGB8888
};

enum dm_micro_swizzle {
	MICRO_SWIZZLE_Z = 0,
	MICRO_SWIZZLE_S = 1,
	MICRO_SWIZZLE_D = 2,
	MICRO_SWIZZLE_R = 3
};

const struct drm_format_info *amdgpu_dm_plane_get_format_info(const struct drm_mode_fb_cmd2 *cmd)
{
	return amdgpu_lookup_format_info(cmd->pixel_format, cmd->modifier[0]);
}

void amdgpu_dm_plane_fill_blending_from_plane_state(const struct drm_plane_state *plane_state,
			       bool *per_pixel_alpha, bool *pre_multiplied_alpha,
			       bool *global_alpha, int *global_alpha_value)
{
	*per_pixel_alpha = false;
	*pre_multiplied_alpha = true;
	*global_alpha = false;
	*global_alpha_value = 0xff;


	if (plane_state->pixel_blend_mode == DRM_MODE_BLEND_PREMULTI ||
		plane_state->pixel_blend_mode == DRM_MODE_BLEND_COVERAGE) {
		static const uint32_t alpha_formats[] = {
			DRM_FORMAT_ARGB8888,
			DRM_FORMAT_RGBA8888,
			DRM_FORMAT_ABGR8888,
			DRM_FORMAT_ARGB2101010,
			DRM_FORMAT_ABGR2101010,
			DRM_FORMAT_ARGB16161616,
			DRM_FORMAT_ABGR16161616,
			DRM_FORMAT_ARGB16161616F,
		};
		uint32_t format = plane_state->fb->format->format;
		unsigned int i;

		for (i = 0; i < ARRAY_SIZE(alpha_formats); ++i) {
			if (format == alpha_formats[i]) {
				*per_pixel_alpha = true;
				break;
			}
		}

		if (*per_pixel_alpha && plane_state->pixel_blend_mode == DRM_MODE_BLEND_COVERAGE)
			*pre_multiplied_alpha = false;
	}

	if (plane_state->alpha < 0xffff) {
		*global_alpha = true;
		*global_alpha_value = plane_state->alpha >> 8;
	}
}

static void amdgpu_dm_plane_add_modifier(uint64_t **mods, uint64_t *size, uint64_t *cap, uint64_t mod)
{
	if (!*mods)
		return;

	if (*cap - *size < 1) {
		uint64_t new_cap = *cap * 2;
		uint64_t *new_mods = kmalloc(new_cap * sizeof(uint64_t), GFP_KERNEL);

		if (!new_mods) {
			kfree(*mods);
			*mods = NULL;
			return;
		}

		memcpy(new_mods, *mods, sizeof(uint64_t) * *size);
		kfree(*mods);
		*mods = new_mods;
		*cap = new_cap;
	}

	(*mods)[*size] = mod;
	*size += 1;
}

static bool amdgpu_dm_plane_modifier_has_dcc(uint64_t modifier)
{
	return IS_AMD_FMT_MOD(modifier) && AMD_FMT_MOD_GET(DCC, modifier);
}

static unsigned int amdgpu_dm_plane_modifier_gfx9_swizzle_mode(uint64_t modifier)
{
	if (modifier == DRM_FORMAT_MOD_LINEAR)
		return 0;

	return AMD_FMT_MOD_GET(TILE, modifier);
}

static void amdgpu_dm_plane_fill_gfx8_tiling_info_from_flags(union dc_tiling_info *tiling_info,
							     uint64_t tiling_flags)
{
	/* Fill GFX8 params */
	if (AMDGPU_TILING_GET(tiling_flags, ARRAY_MODE) == DC_ARRAY_2D_TILED_THIN1) {
		unsigned int bankw, bankh, mtaspect, tile_split, num_banks;

		bankw = AMDGPU_TILING_GET(tiling_flags, BANK_WIDTH);
		bankh = AMDGPU_TILING_GET(tiling_flags, BANK_HEIGHT);
		mtaspect = AMDGPU_TILING_GET(tiling_flags, MACRO_TILE_ASPECT);
		tile_split = AMDGPU_TILING_GET(tiling_flags, TILE_SPLIT);
		num_banks = AMDGPU_TILING_GET(tiling_flags, NUM_BANKS);

		/* XXX fix me for VI */
		tiling_info->gfx8.num_banks = num_banks;
		tiling_info->gfx8.array_mode =
				DC_ARRAY_2D_TILED_THIN1;
		tiling_info->gfx8.tile_split = tile_split;
		tiling_info->gfx8.bank_width = bankw;
		tiling_info->gfx8.bank_height = bankh;
		tiling_info->gfx8.tile_aspect = mtaspect;
		tiling_info->gfx8.tile_mode =
				DC_ADDR_SURF_MICRO_TILING_DISPLAY;
	} else if (AMDGPU_TILING_GET(tiling_flags, ARRAY_MODE)
			== DC_ARRAY_1D_TILED_THIN1) {
		tiling_info->gfx8.array_mode = DC_ARRAY_1D_TILED_THIN1;
	}

	tiling_info->gfx8.pipe_config =
			AMDGPU_TILING_GET(tiling_flags, PIPE_CONFIG);
}

static void amdgpu_dm_plane_fill_gfx9_tiling_info_from_device(const struct amdgpu_device *adev,
							      union dc_tiling_info *tiling_info)
{
	/* Fill GFX9 params */
	tiling_info->gfx9.num_pipes =
		adev->gfx.config.gb_addr_config_fields.num_pipes;
	tiling_info->gfx9.num_banks =
		adev->gfx.config.gb_addr_config_fields.num_banks;
	tiling_info->gfx9.pipe_interleave =
		adev->gfx.config.gb_addr_config_fields.pipe_interleave_size;
	tiling_info->gfx9.num_shader_engines =
		adev->gfx.config.gb_addr_config_fields.num_se;
	tiling_info->gfx9.max_compressed_frags =
		adev->gfx.config.gb_addr_config_fields.max_compress_frags;
	tiling_info->gfx9.num_rb_per_se =
		adev->gfx.config.gb_addr_config_fields.num_rb_per_se;
	tiling_info->gfx9.shaderEnable = 1;
	if (amdgpu_ip_version(adev, GC_HWIP, 0) >= IP_VERSION(10, 3, 0))
		tiling_info->gfx9.num_pkrs = adev->gfx.config.gb_addr_config_fields.num_pkrs;
}

static void amdgpu_dm_plane_fill_gfx9_tiling_info_from_modifier(const struct amdgpu_device *adev,
								union dc_tiling_info *tiling_info,
								uint64_t modifier)
{
	unsigned int mod_bank_xor_bits = AMD_FMT_MOD_GET(BANK_XOR_BITS, modifier);
	unsigned int mod_pipe_xor_bits = AMD_FMT_MOD_GET(PIPE_XOR_BITS, modifier);
	unsigned int pkrs_log2 = AMD_FMT_MOD_GET(PACKERS, modifier);
	unsigned int pipes_log2;

	pipes_log2 = min(5u, mod_pipe_xor_bits);

	amdgpu_dm_plane_fill_gfx9_tiling_info_from_device(adev, tiling_info);

	if (!IS_AMD_FMT_MOD(modifier))
		return;

	tiling_info->gfx9.num_pipes = 1u << pipes_log2;
	tiling_info->gfx9.num_shader_engines = 1u << (mod_pipe_xor_bits - pipes_log2);

	if (adev->family >= AMDGPU_FAMILY_NV) {
		tiling_info->gfx9.num_pkrs = 1u << pkrs_log2;
	} else {
		tiling_info->gfx9.num_banks = 1u << mod_bank_xor_bits;

		/* for DCC we know it isn't rb aligned, so rb_per_se doesn't matter. */
	}
}

static int amdgpu_dm_plane_validate_dcc(struct amdgpu_device *adev,
					const enum surface_pixel_format format,
					const enum dc_rotation_angle rotation,
					const union dc_tiling_info *tiling_info,
					const struct dc_plane_dcc_param *dcc,
					const struct dc_plane_address *address,
					const struct plane_size *plane_size)
{
	struct dc *dc = adev->dm.dc;
	struct dc_dcc_surface_param input;
	struct dc_surface_dcc_cap output;

	memset(&input, 0, sizeof(input));
	memset(&output, 0, sizeof(output));

	if (!dcc->enable)
		return 0;

	if (adev->family < AMDGPU_FAMILY_GC_12_0_0 &&
	    format >= SURFACE_PIXEL_FORMAT_VIDEO_BEGIN)
		return -EINVAL;

	if (!dc->cap_funcs.get_dcc_compression_cap)
		return -EINVAL;

	input.format = format;
	input.surface_size.width = plane_size->surface_size.width;
	input.surface_size.height = plane_size->surface_size.height;
	input.swizzle_mode = tiling_info->gfx9.swizzle;

	if (rotation == ROTATION_ANGLE_0 || rotation == ROTATION_ANGLE_180)
		input.scan = SCAN_DIRECTION_HORIZONTAL;
	else if (rotation == ROTATION_ANGLE_90 || rotation == ROTATION_ANGLE_270)
		input.scan = SCAN_DIRECTION_VERTICAL;

	if (!dc->cap_funcs.get_dcc_compression_cap(dc, &input, &output))
		return -EINVAL;

	if (!output.capable)
		return -EINVAL;

	if (dcc->independent_64b_blks == 0 &&
	    output.grph.rgb.independent_64b_blks != 0)
		return -EINVAL;

	return 0;
}

static int amdgpu_dm_plane_fill_gfx9_plane_attributes_from_modifiers(struct amdgpu_device *adev,
								     const struct amdgpu_framebuffer *afb,
								     const enum surface_pixel_format format,
								     const enum dc_rotation_angle rotation,
								     const struct plane_size *plane_size,
								     union dc_tiling_info *tiling_info,
								     struct dc_plane_dcc_param *dcc,
								     struct dc_plane_address *address)
{
	const uint64_t modifier = afb->base.modifier;
	int ret = 0;

	amdgpu_dm_plane_fill_gfx9_tiling_info_from_modifier(adev, tiling_info, modifier);
	tiling_info->gfx9.swizzle = amdgpu_dm_plane_modifier_gfx9_swizzle_mode(modifier);

	if (amdgpu_dm_plane_modifier_has_dcc(modifier)) {
		uint64_t dcc_address = afb->address + afb->base.offsets[1];
		bool independent_64b_blks = AMD_FMT_MOD_GET(DCC_INDEPENDENT_64B, modifier);
		bool independent_128b_blks = AMD_FMT_MOD_GET(DCC_INDEPENDENT_128B, modifier);

		dcc->enable = 1;
		dcc->meta_pitch = afb->base.pitches[1];
		dcc->independent_64b_blks = independent_64b_blks;
		if (AMD_FMT_MOD_GET(TILE_VERSION, modifier) >= AMD_FMT_MOD_TILE_VER_GFX10_RBPLUS) {
			if (independent_64b_blks && independent_128b_blks)
				dcc->dcc_ind_blk = hubp_ind_block_64b_no_128bcl;
			else if (independent_128b_blks)
				dcc->dcc_ind_blk = hubp_ind_block_128b;
			else if (independent_64b_blks && !independent_128b_blks)
				dcc->dcc_ind_blk = hubp_ind_block_64b;
			else
				dcc->dcc_ind_blk = hubp_ind_block_unconstrained;
		} else {
			if (independent_64b_blks)
				dcc->dcc_ind_blk = hubp_ind_block_64b;
			else
				dcc->dcc_ind_blk = hubp_ind_block_unconstrained;
		}

		address->grph.meta_addr.low_part = lower_32_bits(dcc_address);
		address->grph.meta_addr.high_part = upper_32_bits(dcc_address);
	}

	ret = amdgpu_dm_plane_validate_dcc(adev, format, rotation, tiling_info, dcc, address, plane_size);
	if (ret)
		drm_dbg_kms(adev_to_drm(adev), "amdgpu_dm_plane_validate_dcc: returned error: %d\n", ret);

	return ret;
}

static int amdgpu_dm_plane_fill_gfx12_plane_attributes_from_modifiers(struct amdgpu_device *adev,
								      const struct amdgpu_framebuffer *afb,
								      const enum surface_pixel_format format,
								      const enum dc_rotation_angle rotation,
								      const struct plane_size *plane_size,
								      union dc_tiling_info *tiling_info,
								      struct dc_plane_dcc_param *dcc,
								      struct dc_plane_address *address)
{
	const uint64_t modifier = afb->base.modifier;
	int ret = 0;

	/* TODO: Most of this function shouldn't be needed on GFX12. */
	amdgpu_dm_plane_fill_gfx9_tiling_info_from_device(adev, tiling_info);

	tiling_info->gfx9.swizzle = amdgpu_dm_plane_modifier_gfx9_swizzle_mode(modifier);

	if (amdgpu_dm_plane_modifier_has_dcc(modifier)) {
		int max_compressed_block = AMD_FMT_MOD_GET(DCC_MAX_COMPRESSED_BLOCK, modifier);

		dcc->enable = 1;
		dcc->independent_64b_blks = max_compressed_block == 0;

		if (max_compressed_block == 0)
			dcc->dcc_ind_blk = hubp_ind_block_64b;
		else if (max_compressed_block == 1)
			dcc->dcc_ind_blk = hubp_ind_block_128b;
		else
			dcc->dcc_ind_blk = hubp_ind_block_unconstrained;
	}

	/* TODO: This seems wrong because there is no DCC plane on GFX12. */
	ret = amdgpu_dm_plane_validate_dcc(adev, format, rotation, tiling_info, dcc, address, plane_size);
	if (ret)
		drm_dbg_kms(adev_to_drm(adev), "amdgpu_dm_plane_validate_dcc: returned error: %d\n", ret);

	return ret;
}

static void amdgpu_dm_plane_add_gfx10_1_modifiers(const struct amdgpu_device *adev,
						  uint64_t **mods,
						  uint64_t *size,
						  uint64_t *capacity)
{
	int pipe_xor_bits = ilog2(adev->gfx.config.gb_addr_config_fields.num_pipes);

	amdgpu_dm_plane_add_modifier(mods, size, capacity, AMD_FMT_MOD |
				     AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX9_64K_R_X) |
				     AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX10) |
				     AMD_FMT_MOD_SET(PIPE_XOR_BITS, pipe_xor_bits) |
				     AMD_FMT_MOD_SET(DCC, 1) |
				     AMD_FMT_MOD_SET(DCC_CONSTANT_ENCODE, 1) |
				     AMD_FMT_MOD_SET(DCC_INDEPENDENT_64B, 1) |
				     AMD_FMT_MOD_SET(DCC_MAX_COMPRESSED_BLOCK, AMD_FMT_MOD_DCC_BLOCK_64B));

	amdgpu_dm_plane_add_modifier(mods, size, capacity, AMD_FMT_MOD |
				     AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX9_64K_R_X) |
				     AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX10) |
				     AMD_FMT_MOD_SET(PIPE_XOR_BITS, pipe_xor_bits) |
				     AMD_FMT_MOD_SET(DCC, 1) |
				     AMD_FMT_MOD_SET(DCC_RETILE, 1) |
				     AMD_FMT_MOD_SET(DCC_CONSTANT_ENCODE, 1) |
				     AMD_FMT_MOD_SET(DCC_INDEPENDENT_64B, 1) |
				     AMD_FMT_MOD_SET(DCC_MAX_COMPRESSED_BLOCK, AMD_FMT_MOD_DCC_BLOCK_64B));

	amdgpu_dm_plane_add_modifier(mods, size, capacity, AMD_FMT_MOD |
				     AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX9_64K_R_X) |
				     AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX10) |
				     AMD_FMT_MOD_SET(PIPE_XOR_BITS, pipe_xor_bits));

	amdgpu_dm_plane_add_modifier(mods, size, capacity, AMD_FMT_MOD |
				     AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX9_64K_S_X) |
				     AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX10) |
				     AMD_FMT_MOD_SET(PIPE_XOR_BITS, pipe_xor_bits));


	/* Only supported for 64bpp, will be filtered in amdgpu_dm_plane_format_mod_supported */
	amdgpu_dm_plane_add_modifier(mods, size, capacity, AMD_FMT_MOD |
				     AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX9_64K_D) |
				     AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX9));

	amdgpu_dm_plane_add_modifier(mods, size, capacity, AMD_FMT_MOD |
				     AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX9_64K_S) |
				     AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX9));
}

static void amdgpu_dm_plane_add_gfx9_modifiers(const struct amdgpu_device *adev,
					       uint64_t **mods,
					       uint64_t *size,
					       uint64_t *capacity)
{
	int pipes = ilog2(adev->gfx.config.gb_addr_config_fields.num_pipes);
	int pipe_xor_bits = min(8, pipes +
				ilog2(adev->gfx.config.gb_addr_config_fields.num_se));
	int bank_xor_bits = min(8 - pipe_xor_bits,
				ilog2(adev->gfx.config.gb_addr_config_fields.num_banks));
	int rb = ilog2(adev->gfx.config.gb_addr_config_fields.num_se) +
		 ilog2(adev->gfx.config.gb_addr_config_fields.num_rb_per_se);


	if (adev->family == AMDGPU_FAMILY_RV) {
		/* Raven2 and later */
		bool has_constant_encode = adev->asic_type > CHIP_RAVEN || adev->external_rev_id >= 0x81;

		/*
		 * No _D DCC swizzles yet because we only allow 32bpp, which
		 * doesn't support _D on DCN
		 */

		if (has_constant_encode) {
			amdgpu_dm_plane_add_modifier(mods, size, capacity, AMD_FMT_MOD |
						     AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX9_64K_S_X) |
						     AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX9) |
						     AMD_FMT_MOD_SET(PIPE_XOR_BITS, pipe_xor_bits) |
						     AMD_FMT_MOD_SET(BANK_XOR_BITS, bank_xor_bits) |
						     AMD_FMT_MOD_SET(DCC, 1) |
						     AMD_FMT_MOD_SET(DCC_INDEPENDENT_64B, 1) |
						     AMD_FMT_MOD_SET(DCC_MAX_COMPRESSED_BLOCK, AMD_FMT_MOD_DCC_BLOCK_64B) |
						     AMD_FMT_MOD_SET(DCC_CONSTANT_ENCODE, 1));
		}

		amdgpu_dm_plane_add_modifier(mods, size, capacity, AMD_FMT_MOD |
					     AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX9_64K_S_X) |
					     AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX9) |
					     AMD_FMT_MOD_SET(PIPE_XOR_BITS, pipe_xor_bits) |
					     AMD_FMT_MOD_SET(BANK_XOR_BITS, bank_xor_bits) |
					     AMD_FMT_MOD_SET(DCC, 1) |
					     AMD_FMT_MOD_SET(DCC_INDEPENDENT_64B, 1) |
					     AMD_FMT_MOD_SET(DCC_MAX_COMPRESSED_BLOCK, AMD_FMT_MOD_DCC_BLOCK_64B) |
					     AMD_FMT_MOD_SET(DCC_CONSTANT_ENCODE, 0));

		if (has_constant_encode) {
			amdgpu_dm_plane_add_modifier(mods, size, capacity, AMD_FMT_MOD |
						     AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX9_64K_S_X) |
						     AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX9) |
						     AMD_FMT_MOD_SET(PIPE_XOR_BITS, pipe_xor_bits) |
						     AMD_FMT_MOD_SET(BANK_XOR_BITS, bank_xor_bits) |
						     AMD_FMT_MOD_SET(DCC, 1) |
						     AMD_FMT_MOD_SET(DCC_RETILE, 1) |
						     AMD_FMT_MOD_SET(DCC_INDEPENDENT_64B, 1) |
						     AMD_FMT_MOD_SET(DCC_MAX_COMPRESSED_BLOCK, AMD_FMT_MOD_DCC_BLOCK_64B) |
						     AMD_FMT_MOD_SET(DCC_CONSTANT_ENCODE, 1) |
						     AMD_FMT_MOD_SET(RB, rb) |
						     AMD_FMT_MOD_SET(PIPE, pipes));
		}

		amdgpu_dm_plane_add_modifier(mods, size, capacity, AMD_FMT_MOD |
					     AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX9_64K_S_X) |
					     AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX9) |
					     AMD_FMT_MOD_SET(PIPE_XOR_BITS, pipe_xor_bits) |
					     AMD_FMT_MOD_SET(BANK_XOR_BITS, bank_xor_bits) |
					     AMD_FMT_MOD_SET(DCC, 1) |
					     AMD_FMT_MOD_SET(DCC_RETILE, 1) |
					     AMD_FMT_MOD_SET(DCC_INDEPENDENT_64B, 1) |
					     AMD_FMT_MOD_SET(DCC_MAX_COMPRESSED_BLOCK, AMD_FMT_MOD_DCC_BLOCK_64B) |
					     AMD_FMT_MOD_SET(DCC_CONSTANT_ENCODE, 0) |
					     AMD_FMT_MOD_SET(RB, rb) |
					     AMD_FMT_MOD_SET(PIPE, pipes));
	}

	/*
	 * Only supported for 64bpp on Raven, will be filtered on format in
	 * amdgpu_dm_plane_format_mod_supported.
	 */
	amdgpu_dm_plane_add_modifier(mods, size, capacity, AMD_FMT_MOD |
				     AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX9_64K_D_X) |
				     AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX9) |
				     AMD_FMT_MOD_SET(PIPE_XOR_BITS, pipe_xor_bits) |
				     AMD_FMT_MOD_SET(BANK_XOR_BITS, bank_xor_bits));

	if (adev->family == AMDGPU_FAMILY_RV) {
		amdgpu_dm_plane_add_modifier(mods, size, capacity, AMD_FMT_MOD |
					     AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX9_64K_S_X) |
					     AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX9) |
					     AMD_FMT_MOD_SET(PIPE_XOR_BITS, pipe_xor_bits) |
					     AMD_FMT_MOD_SET(BANK_XOR_BITS, bank_xor_bits));
	}

	/*
	 * Only supported for 64bpp on Raven, will be filtered on format in
	 * amdgpu_dm_plane_format_mod_supported.
	 */
	amdgpu_dm_plane_add_modifier(mods, size, capacity, AMD_FMT_MOD |
				     AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX9_64K_D) |
				     AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX9));

	if (adev->family == AMDGPU_FAMILY_RV) {
		amdgpu_dm_plane_add_modifier(mods, size, capacity, AMD_FMT_MOD |
					     AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX9_64K_S) |
					     AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX9));
	}
}

static void amdgpu_dm_plane_add_gfx10_3_modifiers(const struct amdgpu_device *adev,
						  uint64_t **mods,
						  uint64_t *size,
						  uint64_t *capacity)
{
	int pipe_xor_bits = ilog2(adev->gfx.config.gb_addr_config_fields.num_pipes);
	int pkrs = ilog2(adev->gfx.config.gb_addr_config_fields.num_pkrs);

	amdgpu_dm_plane_add_modifier(mods, size, capacity, AMD_FMT_MOD |
				     AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX9_64K_R_X) |
				     AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX10_RBPLUS) |
				     AMD_FMT_MOD_SET(PIPE_XOR_BITS, pipe_xor_bits) |
				     AMD_FMT_MOD_SET(PACKERS, pkrs) |
				     AMD_FMT_MOD_SET(DCC, 1) |
				     AMD_FMT_MOD_SET(DCC_CONSTANT_ENCODE, 1) |
				     AMD_FMT_MOD_SET(DCC_INDEPENDENT_64B, 1) |
				     AMD_FMT_MOD_SET(DCC_INDEPENDENT_128B, 1) |
				     AMD_FMT_MOD_SET(DCC_MAX_COMPRESSED_BLOCK, AMD_FMT_MOD_DCC_BLOCK_64B));

	amdgpu_dm_plane_add_modifier(mods, size, capacity, AMD_FMT_MOD |
				     AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX9_64K_R_X) |
				     AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX10_RBPLUS) |
				     AMD_FMT_MOD_SET(PIPE_XOR_BITS, pipe_xor_bits) |
				     AMD_FMT_MOD_SET(PACKERS, pkrs) |
				     AMD_FMT_MOD_SET(DCC, 1) |
				     AMD_FMT_MOD_SET(DCC_CONSTANT_ENCODE, 1) |
				     AMD_FMT_MOD_SET(DCC_INDEPENDENT_128B, 1) |
				     AMD_FMT_MOD_SET(DCC_MAX_COMPRESSED_BLOCK, AMD_FMT_MOD_DCC_BLOCK_128B));

	amdgpu_dm_plane_add_modifier(mods, size, capacity, AMD_FMT_MOD |
				     AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX9_64K_R_X) |
				     AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX10_RBPLUS) |
				     AMD_FMT_MOD_SET(PIPE_XOR_BITS, pipe_xor_bits) |
				     AMD_FMT_MOD_SET(PACKERS, pkrs) |
				     AMD_FMT_MOD_SET(DCC, 1) |
				     AMD_FMT_MOD_SET(DCC_RETILE, 1) |
				     AMD_FMT_MOD_SET(DCC_CONSTANT_ENCODE, 1) |
				     AMD_FMT_MOD_SET(DCC_INDEPENDENT_64B, 1) |
				     AMD_FMT_MOD_SET(DCC_INDEPENDENT_128B, 1) |
				     AMD_FMT_MOD_SET(DCC_MAX_COMPRESSED_BLOCK, AMD_FMT_MOD_DCC_BLOCK_64B));

	amdgpu_dm_plane_add_modifier(mods, size, capacity, AMD_FMT_MOD |
				     AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX9_64K_R_X) |
				     AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX10_RBPLUS) |
				     AMD_FMT_MOD_SET(PIPE_XOR_BITS, pipe_xor_bits) |
				     AMD_FMT_MOD_SET(PACKERS, pkrs) |
				     AMD_FMT_MOD_SET(DCC, 1) |
				     AMD_FMT_MOD_SET(DCC_RETILE, 1) |
				     AMD_FMT_MOD_SET(DCC_CONSTANT_ENCODE, 1) |
				     AMD_FMT_MOD_SET(DCC_INDEPENDENT_128B, 1) |
				     AMD_FMT_MOD_SET(DCC_MAX_COMPRESSED_BLOCK, AMD_FMT_MOD_DCC_BLOCK_128B));

	amdgpu_dm_plane_add_modifier(mods, size, capacity, AMD_FMT_MOD |
				     AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX9_64K_R_X) |
				     AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX10_RBPLUS) |
				     AMD_FMT_MOD_SET(PIPE_XOR_BITS, pipe_xor_bits) |
				     AMD_FMT_MOD_SET(PACKERS, pkrs));

	amdgpu_dm_plane_add_modifier(mods, size, capacity, AMD_FMT_MOD |
				     AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX9_64K_S_X) |
				     AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX10_RBPLUS) |
				     AMD_FMT_MOD_SET(PIPE_XOR_BITS, pipe_xor_bits) |
				     AMD_FMT_MOD_SET(PACKERS, pkrs));

	/* Only supported for 64bpp, will be filtered in amdgpu_dm_plane_format_mod_supported */
	amdgpu_dm_plane_add_modifier(mods, size, capacity, AMD_FMT_MOD |
				     AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX9_64K_D) |
				     AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX9));

	amdgpu_dm_plane_add_modifier(mods, size, capacity, AMD_FMT_MOD |
				     AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX9_64K_S) |
				     AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX9));
}

static void amdgpu_dm_plane_add_gfx11_modifiers(struct amdgpu_device *adev,
		      uint64_t **mods, uint64_t *size, uint64_t *capacity)
{
	int num_pipes = 0;
	int pipe_xor_bits = 0;
	int num_pkrs = 0;
	int pkrs = 0;
	u32 gb_addr_config;
	u8 i = 0;
	unsigned int swizzle_r_x;
	uint64_t modifier_r_x;
	uint64_t modifier_dcc_best;
	uint64_t modifier_dcc_4k;

	/* TODO: GFX11 IP HW init hasnt finish and we get zero if we read from
	 * adev->gfx.config.gb_addr_config_fields.num_{pkrs,pipes}
	 */
	gb_addr_config = RREG32_SOC15(GC, 0, regGB_ADDR_CONFIG);
	ASSERT(gb_addr_config != 0);

	num_pkrs = 1 << REG_GET_FIELD(gb_addr_config, GB_ADDR_CONFIG, NUM_PKRS);
	pkrs = ilog2(num_pkrs);
	num_pipes = 1 << REG_GET_FIELD(gb_addr_config, GB_ADDR_CONFIG, NUM_PIPES);
	pipe_xor_bits = ilog2(num_pipes);

	for (i = 0; i < 2; i++) {
		/* Insert the best one first. */
		/* R_X swizzle modes are the best for rendering and DCC requires them. */
		if (num_pipes > 16)
			swizzle_r_x = !i ? AMD_FMT_MOD_TILE_GFX11_256K_R_X : AMD_FMT_MOD_TILE_GFX9_64K_R_X;
		else
			swizzle_r_x = !i ? AMD_FMT_MOD_TILE_GFX9_64K_R_X : AMD_FMT_MOD_TILE_GFX11_256K_R_X;

		modifier_r_x = AMD_FMT_MOD |
			       AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX11) |
			       AMD_FMT_MOD_SET(PIPE_XOR_BITS, pipe_xor_bits) |
			       AMD_FMT_MOD_SET(TILE, swizzle_r_x) |
			       AMD_FMT_MOD_SET(PACKERS, pkrs);

		/* DCC_CONSTANT_ENCODE is not set because it can't vary with gfx11 (it's implied to be 1). */
		modifier_dcc_best = modifier_r_x | AMD_FMT_MOD_SET(DCC, 1) |
				    AMD_FMT_MOD_SET(DCC_INDEPENDENT_64B, 0) |
				    AMD_FMT_MOD_SET(DCC_INDEPENDENT_128B, 1) |
				    AMD_FMT_MOD_SET(DCC_MAX_COMPRESSED_BLOCK, AMD_FMT_MOD_DCC_BLOCK_128B);

		/* DCC settings for 4K and greater resolutions. (required by display hw) */
		modifier_dcc_4k = modifier_r_x | AMD_FMT_MOD_SET(DCC, 1) |
				  AMD_FMT_MOD_SET(DCC_INDEPENDENT_64B, 1) |
				  AMD_FMT_MOD_SET(DCC_INDEPENDENT_128B, 1) |
				  AMD_FMT_MOD_SET(DCC_MAX_COMPRESSED_BLOCK, AMD_FMT_MOD_DCC_BLOCK_64B);

		amdgpu_dm_plane_add_modifier(mods, size, capacity, modifier_dcc_best);
		amdgpu_dm_plane_add_modifier(mods, size, capacity, modifier_dcc_4k);

		amdgpu_dm_plane_add_modifier(mods, size, capacity, modifier_dcc_best | AMD_FMT_MOD_SET(DCC_RETILE, 1));
		amdgpu_dm_plane_add_modifier(mods, size, capacity, modifier_dcc_4k | AMD_FMT_MOD_SET(DCC_RETILE, 1));

		amdgpu_dm_plane_add_modifier(mods, size, capacity, modifier_r_x);
	}

	amdgpu_dm_plane_add_modifier(mods, size, capacity, AMD_FMT_MOD |
				     AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX11) |
				     AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX9_64K_D));
}

static void amdgpu_dm_plane_add_gfx12_modifiers(struct amdgpu_device *adev,
		      uint64_t **mods, uint64_t *size, uint64_t *capacity)
{
	uint64_t ver = AMD_FMT_MOD | AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX12);
	uint64_t mod_256k = ver | AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX12_256K_2D);
	uint64_t mod_64k = ver | AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX12_64K_2D);
	uint64_t mod_4k = ver | AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX12_4K_2D);
	uint64_t mod_256b = ver | AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX12_256B_2D);
	uint64_t dcc = ver | AMD_FMT_MOD_SET(DCC, 1);
	uint8_t max_comp_block[] = {1, 0};
	uint64_t max_comp_block_mod[ARRAY_SIZE(max_comp_block)] = {0};
	uint8_t i = 0, j = 0;
	uint64_t gfx12_modifiers[] = {mod_256k, mod_64k, mod_4k, mod_256b, DRM_FORMAT_MOD_LINEAR};

	for (i = 0; i < ARRAY_SIZE(max_comp_block); i++)
		max_comp_block_mod[i] = AMD_FMT_MOD_SET(DCC_MAX_COMPRESSED_BLOCK, max_comp_block[i]);

	/* With DCC: Best choice should be kept first. Hence, add all 256k modifiers of different
	 * max compressed blocks first and then move on to the next smaller sized layouts.
	 * Do not add the linear modifier here, and hence the condition of size-1 for the loop
	 */
	for (j = 0; j < ARRAY_SIZE(gfx12_modifiers) - 1; j++)
		for (i = 0; i < ARRAY_SIZE(max_comp_block); i++)
			amdgpu_dm_plane_add_modifier(mods, size, capacity,
						     ver | dcc | max_comp_block_mod[i] | gfx12_modifiers[j]);

	/* Without DCC. Add all modifiers including linear at the end */
	for (i = 0; i < ARRAY_SIZE(gfx12_modifiers); i++)
		amdgpu_dm_plane_add_modifier(mods, size, capacity, gfx12_modifiers[i]);

}

static int amdgpu_dm_plane_get_plane_modifiers(struct amdgpu_device *adev, unsigned int plane_type, uint64_t **mods)
{
	uint64_t size = 0, capacity = 128;
	*mods = NULL;

	/* We have not hooked up any pre-GFX9 modifiers. */
	if (adev->family < AMDGPU_FAMILY_AI)
		return 0;

	*mods = kmalloc(capacity * sizeof(uint64_t), GFP_KERNEL);

	if (plane_type == DRM_PLANE_TYPE_CURSOR) {
		amdgpu_dm_plane_add_modifier(mods, &size, &capacity, DRM_FORMAT_MOD_LINEAR);
		amdgpu_dm_plane_add_modifier(mods, &size, &capacity, DRM_FORMAT_MOD_INVALID);
		return *mods ? 0 : -ENOMEM;
	}

	switch (adev->family) {
	case AMDGPU_FAMILY_AI:
	case AMDGPU_FAMILY_RV:
		amdgpu_dm_plane_add_gfx9_modifiers(adev, mods, &size, &capacity);
		break;
	case AMDGPU_FAMILY_NV:
	case AMDGPU_FAMILY_VGH:
	case AMDGPU_FAMILY_YC:
	case AMDGPU_FAMILY_GC_10_3_6:
	case AMDGPU_FAMILY_GC_10_3_7:
		if (amdgpu_ip_version(adev, GC_HWIP, 0) >= IP_VERSION(10, 3, 0))
			amdgpu_dm_plane_add_gfx10_3_modifiers(adev, mods, &size, &capacity);
		else
			amdgpu_dm_plane_add_gfx10_1_modifiers(adev, mods, &size, &capacity);
		break;
	case AMDGPU_FAMILY_GC_11_0_0:
	case AMDGPU_FAMILY_GC_11_0_1:
	case AMDGPU_FAMILY_GC_11_5_0:
		amdgpu_dm_plane_add_gfx11_modifiers(adev, mods, &size, &capacity);
		break;
	case AMDGPU_FAMILY_GC_12_0_0:
		amdgpu_dm_plane_add_gfx12_modifiers(adev, mods, &size, &capacity);
		break;
	}

	amdgpu_dm_plane_add_modifier(mods, &size, &capacity, DRM_FORMAT_MOD_LINEAR);

	/* INVALID marks the end of the list. */
	amdgpu_dm_plane_add_modifier(mods, &size, &capacity, DRM_FORMAT_MOD_INVALID);

	if (!*mods)
		return -ENOMEM;

	return 0;
}

static int amdgpu_dm_plane_get_plane_formats(const struct drm_plane *plane,
					     const struct dc_plane_cap *plane_cap,
					     uint32_t *formats, int max_formats)
{
	int i, num_formats = 0;

	/*
	 * TODO: Query support for each group of formats directly from
	 * DC plane caps. This will require adding more formats to the
	 * caps list.
	 */

	if (plane->type == DRM_PLANE_TYPE_PRIMARY ||
		(plane_cap && plane_cap->type == DC_PLANE_TYPE_DCN_UNIVERSAL && plane->type != DRM_PLANE_TYPE_CURSOR)) {
		for (i = 0; i < ARRAY_SIZE(rgb_formats); ++i) {
			if (num_formats >= max_formats)
				break;

			formats[num_formats++] = rgb_formats[i];
		}

		if (plane_cap && plane_cap->pixel_format_support.nv12)
			formats[num_formats++] = DRM_FORMAT_NV12;
		if (plane_cap && plane_cap->pixel_format_support.p010)
			formats[num_formats++] = DRM_FORMAT_P010;
		if (plane_cap && plane_cap->pixel_format_support.fp16) {
			formats[num_formats++] = DRM_FORMAT_XRGB16161616F;
			formats[num_formats++] = DRM_FORMAT_ARGB16161616F;
			formats[num_formats++] = DRM_FORMAT_XBGR16161616F;
			formats[num_formats++] = DRM_FORMAT_ABGR16161616F;
		}
	} else {
		switch (plane->type) {
		case DRM_PLANE_TYPE_OVERLAY:
			for (i = 0; i < ARRAY_SIZE(overlay_formats); ++i) {
				if (num_formats >= max_formats)
					break;

				formats[num_formats++] = overlay_formats[i];
			}
			break;

		case DRM_PLANE_TYPE_CURSOR:
			for (i = 0; i < ARRAY_SIZE(cursor_formats); ++i) {
				if (num_formats >= max_formats)
					break;

				formats[num_formats++] = cursor_formats[i];
			}
			break;

		default:
			break;
		}
	}

	return num_formats;
}

int amdgpu_dm_plane_fill_plane_buffer_attributes(struct amdgpu_device *adev,
			     const struct amdgpu_framebuffer *afb,
			     const enum surface_pixel_format format,
			     const enum dc_rotation_angle rotation,
			     const uint64_t tiling_flags,
			     union dc_tiling_info *tiling_info,
			     struct plane_size *plane_size,
			     struct dc_plane_dcc_param *dcc,
			     struct dc_plane_address *address,
			     bool tmz_surface)
{
	const struct drm_framebuffer *fb = &afb->base;
	int ret;

	memset(tiling_info, 0, sizeof(*tiling_info));
	memset(plane_size, 0, sizeof(*plane_size));
	memset(dcc, 0, sizeof(*dcc));
	memset(address, 0, sizeof(*address));

	address->tmz_surface = tmz_surface;

	if (format < SURFACE_PIXEL_FORMAT_VIDEO_BEGIN) {
		uint64_t addr = afb->address + fb->offsets[0];

		plane_size->surface_size.x = 0;
		plane_size->surface_size.y = 0;
		plane_size->surface_size.width = fb->width;
		plane_size->surface_size.height = fb->height;
		plane_size->surface_pitch =
			fb->pitches[0] / fb->format->cpp[0];

		address->type = PLN_ADDR_TYPE_GRAPHICS;
		address->grph.addr.low_part = lower_32_bits(addr);
		address->grph.addr.high_part = upper_32_bits(addr);
	} else if (format < SURFACE_PIXEL_FORMAT_INVALID) {
		uint64_t luma_addr = afb->address + fb->offsets[0];
		uint64_t chroma_addr = afb->address + fb->offsets[1];

		plane_size->surface_size.x = 0;
		plane_size->surface_size.y = 0;
		plane_size->surface_size.width = fb->width;
		plane_size->surface_size.height = fb->height;
		plane_size->surface_pitch =
			fb->pitches[0] / fb->format->cpp[0];

		plane_size->chroma_size.x = 0;
		plane_size->chroma_size.y = 0;
		/* TODO: set these based on surface format */
		plane_size->chroma_size.width = fb->width / 2;
		plane_size->chroma_size.height = fb->height / 2;

		plane_size->chroma_pitch =
			fb->pitches[1] / fb->format->cpp[1];

		address->type = PLN_ADDR_TYPE_VIDEO_PROGRESSIVE;
		address->video_progressive.luma_addr.low_part =
			lower_32_bits(luma_addr);
		address->video_progressive.luma_addr.high_part =
			upper_32_bits(luma_addr);
		address->video_progressive.chroma_addr.low_part =
			lower_32_bits(chroma_addr);
		address->video_progressive.chroma_addr.high_part =
			upper_32_bits(chroma_addr);
	}

	if (adev->family >= AMDGPU_FAMILY_GC_12_0_0) {
		ret = amdgpu_dm_plane_fill_gfx12_plane_attributes_from_modifiers(adev, afb, format,
										 rotation, plane_size,
										 tiling_info, dcc,
										 address);
		if (ret)
			return ret;
	} else if (adev->family >= AMDGPU_FAMILY_AI) {
		ret = amdgpu_dm_plane_fill_gfx9_plane_attributes_from_modifiers(adev, afb, format,
										rotation, plane_size,
										tiling_info, dcc,
										address);
		if (ret)
			return ret;
	} else {
		amdgpu_dm_plane_fill_gfx8_tiling_info_from_flags(tiling_info, tiling_flags);
	}

	return 0;
}

static int amdgpu_dm_plane_helper_prepare_fb(struct drm_plane *plane,
					     struct drm_plane_state *new_state)
{
	struct amdgpu_framebuffer *afb;
	struct drm_gem_object *obj;
	struct amdgpu_device *adev;
	struct amdgpu_bo *rbo;
	struct dm_plane_state *dm_plane_state_new, *dm_plane_state_old;
	uint32_t domain;
	int r;

	if (!new_state->fb) {
		DRM_DEBUG_KMS("No FB bound\n");
		return 0;
	}

	afb = to_amdgpu_framebuffer(new_state->fb);
	obj = drm_gem_fb_get_obj(new_state->fb, 0);
	if (!obj) {
		DRM_ERROR("Failed to get obj from framebuffer\n");
		return -EINVAL;
	}

	rbo = gem_to_amdgpu_bo(obj);
	adev = amdgpu_ttm_adev(rbo->tbo.bdev);
	r = amdgpu_bo_reserve(rbo, true);
	if (r) {
		dev_err(adev->dev, "fail to reserve bo (%d)\n", r);
		return r;
	}

	r = dma_resv_reserve_fences(rbo->tbo.base.resv, 1);
	if (r) {
		dev_err(adev->dev, "reserving fence slot failed (%d)\n", r);
		goto error_unlock;
	}

	if (plane->type != DRM_PLANE_TYPE_CURSOR)
		domain = amdgpu_display_supported_domains(adev, rbo->flags);
	else
		domain = AMDGPU_GEM_DOMAIN_VRAM;

	rbo->flags |= AMDGPU_GEM_CREATE_VRAM_CONTIGUOUS;
	r = amdgpu_bo_pin(rbo, domain);
	if (unlikely(r != 0)) {
		if (r != -ERESTARTSYS)
			DRM_ERROR("Failed to pin framebuffer with error %d\n", r);
		goto error_unlock;
	}

	r = amdgpu_ttm_alloc_gart(&rbo->tbo);
	if (unlikely(r != 0)) {
		DRM_ERROR("%p bind failed\n", rbo);
		goto error_unpin;
	}

	r = drm_gem_plane_helper_prepare_fb(plane, new_state);
	if (unlikely(r != 0))
		goto error_unpin;

	amdgpu_bo_unreserve(rbo);

	afb->address = amdgpu_bo_gpu_offset(rbo);

	amdgpu_bo_ref(rbo);

	/**
	 * We don't do surface updates on planes that have been newly created,
	 * but we also don't have the afb->address during atomic check.
	 *
	 * Fill in buffer attributes depending on the address here, but only on
	 * newly created planes since they're not being used by DC yet and this
	 * won't modify global state.
	 */
	dm_plane_state_old = to_dm_plane_state(plane->state);
	dm_plane_state_new = to_dm_plane_state(new_state);

	if (dm_plane_state_new->dc_state &&
	    dm_plane_state_old->dc_state != dm_plane_state_new->dc_state) {
		struct dc_plane_state *plane_state =
			dm_plane_state_new->dc_state;

		amdgpu_dm_plane_fill_plane_buffer_attributes(
			adev, afb, plane_state->format, plane_state->rotation,
			afb->tiling_flags,
			&plane_state->tiling_info, &plane_state->plane_size,
			&plane_state->dcc, &plane_state->address,
			afb->tmz_surface);
	}

	return 0;

error_unpin:
	amdgpu_bo_unpin(rbo);

error_unlock:
	amdgpu_bo_unreserve(rbo);
	return r;
}

static void amdgpu_dm_plane_helper_cleanup_fb(struct drm_plane *plane,
					      struct drm_plane_state *old_state)
{
	struct amdgpu_bo *rbo;
	int r;

	if (!old_state->fb)
		return;

	rbo = gem_to_amdgpu_bo(old_state->fb->obj[0]);
	r = amdgpu_bo_reserve(rbo, false);
	if (unlikely(r)) {
		DRM_ERROR("failed to reserve rbo before unpin\n");
		return;
	}

	amdgpu_bo_unpin(rbo);
	amdgpu_bo_unreserve(rbo);
	amdgpu_bo_unref(&rbo);
}

static void amdgpu_dm_plane_get_min_max_dc_plane_scaling(struct drm_device *dev,
					 struct drm_framebuffer *fb,
					 int *min_downscale, int *max_upscale)
{
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct dc *dc = adev->dm.dc;
	/* Caps for all supported planes are the same on DCE and DCN 1 - 3 */
	struct dc_plane_cap *plane_cap = &dc->caps.planes[0];

	switch (fb->format->format) {
	case DRM_FORMAT_P010:
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV21:
		*max_upscale = plane_cap->max_upscale_factor.nv12;
		*min_downscale = plane_cap->max_downscale_factor.nv12;
		break;

	case DRM_FORMAT_XRGB16161616F:
	case DRM_FORMAT_ARGB16161616F:
	case DRM_FORMAT_XBGR16161616F:
	case DRM_FORMAT_ABGR16161616F:
		*max_upscale = plane_cap->max_upscale_factor.fp16;
		*min_downscale = plane_cap->max_downscale_factor.fp16;
		break;

	default:
		*max_upscale = plane_cap->max_upscale_factor.argb8888;
		*min_downscale = plane_cap->max_downscale_factor.argb8888;
		break;
	}

	/*
	 * A factor of 1 in the plane_cap means to not allow scaling, ie. use a
	 * scaling factor of 1.0 == 1000 units.
	 */
	if (*max_upscale == 1)
		*max_upscale = 1000;

	if (*min_downscale == 1)
		*min_downscale = 1000;
}

int amdgpu_dm_plane_helper_check_state(struct drm_plane_state *state,
				       struct drm_crtc_state *new_crtc_state)
{
	struct drm_framebuffer *fb = state->fb;
	int min_downscale, max_upscale;
	int min_scale = 0;
	int max_scale = INT_MAX;

	/* Plane enabled? Validate viewport and get scaling factors from plane caps. */
	if (fb && state->crtc) {
		/* Validate viewport to cover the case when only the position changes */
		if (state->plane->type != DRM_PLANE_TYPE_CURSOR) {
			int viewport_width = state->crtc_w;
			int viewport_height = state->crtc_h;

			if (state->crtc_x < 0)
				viewport_width += state->crtc_x;
			else if (state->crtc_x + state->crtc_w > new_crtc_state->mode.crtc_hdisplay)
				viewport_width = new_crtc_state->mode.crtc_hdisplay - state->crtc_x;

			if (state->crtc_y < 0)
				viewport_height += state->crtc_y;
			else if (state->crtc_y + state->crtc_h > new_crtc_state->mode.crtc_vdisplay)
				viewport_height = new_crtc_state->mode.crtc_vdisplay - state->crtc_y;

			if (viewport_width < 0 || viewport_height < 0) {
				DRM_DEBUG_ATOMIC("Plane completely outside of screen\n");
				return -EINVAL;
			} else if (viewport_width < MIN_VIEWPORT_SIZE*2) { /* x2 for width is because of pipe-split. */
				DRM_DEBUG_ATOMIC("Viewport width %d smaller than %d\n", viewport_width, MIN_VIEWPORT_SIZE*2);
				return -EINVAL;
			} else if (viewport_height < MIN_VIEWPORT_SIZE) {
				DRM_DEBUG_ATOMIC("Viewport height %d smaller than %d\n", viewport_height, MIN_VIEWPORT_SIZE);
				return -EINVAL;
			}

		}

		/* Get min/max allowed scaling factors from plane caps. */
		amdgpu_dm_plane_get_min_max_dc_plane_scaling(state->crtc->dev, fb,
							     &min_downscale, &max_upscale);
		/*
		 * Convert to drm convention: 16.16 fixed point, instead of dc's
		 * 1.0 == 1000. Also drm scaling is src/dst instead of dc's
		 * dst/src, so min_scale = 1.0 / max_upscale, etc.
		 */
		min_scale = (1000 << 16) / max_upscale;
		max_scale = (1000 << 16) / min_downscale;
	}

	return drm_atomic_helper_check_plane_state(
		state, new_crtc_state, min_scale, max_scale, true, true);
}

int amdgpu_dm_plane_fill_dc_scaling_info(struct amdgpu_device *adev,
				const struct drm_plane_state *state,
				struct dc_scaling_info *scaling_info)
{
	int scale_w, scale_h, min_downscale, max_upscale;

	memset(scaling_info, 0, sizeof(*scaling_info));

	/* Source is fixed 16.16 but we ignore mantissa for now... */
	scaling_info->src_rect.x = state->src_x >> 16;
	scaling_info->src_rect.y = state->src_y >> 16;

	/*
	 * For reasons we don't (yet) fully understand a non-zero
	 * src_y coordinate into an NV12 buffer can cause a
	 * system hang on DCN1x.
	 * To avoid hangs (and maybe be overly cautious)
	 * let's reject both non-zero src_x and src_y.
	 *
	 * We currently know of only one use-case to reproduce a
	 * scenario with non-zero src_x and src_y for NV12, which
	 * is to gesture the YouTube Android app into full screen
	 * on ChromeOS.
	 */
	if (((amdgpu_ip_version(adev, DCE_HWIP, 0) == IP_VERSION(1, 0, 0)) ||
	    (amdgpu_ip_version(adev, DCE_HWIP, 0) == IP_VERSION(1, 0, 1))) &&
	    (state->fb && state->fb->format->format == DRM_FORMAT_NV12 &&
	    (scaling_info->src_rect.x != 0 || scaling_info->src_rect.y != 0)))
		return -EINVAL;

	scaling_info->src_rect.width = state->src_w >> 16;
	if (scaling_info->src_rect.width == 0)
		return -EINVAL;

	scaling_info->src_rect.height = state->src_h >> 16;
	if (scaling_info->src_rect.height == 0)
		return -EINVAL;

	scaling_info->dst_rect.x = state->crtc_x;
	scaling_info->dst_rect.y = state->crtc_y;

	if (state->crtc_w == 0)
		return -EINVAL;

	scaling_info->dst_rect.width = state->crtc_w;

	if (state->crtc_h == 0)
		return -EINVAL;

	scaling_info->dst_rect.height = state->crtc_h;

	/* DRM doesn't specify clipping on destination output. */
	scaling_info->clip_rect = scaling_info->dst_rect;

	/* Validate scaling per-format with DC plane caps */
	if (state->plane && state->plane->dev && state->fb) {
		amdgpu_dm_plane_get_min_max_dc_plane_scaling(state->plane->dev, state->fb,
							     &min_downscale, &max_upscale);
	} else {
		min_downscale = 250;
		max_upscale = 16000;
	}

	scale_w = scaling_info->dst_rect.width * 1000 /
		  scaling_info->src_rect.width;

	if (scale_w < min_downscale || scale_w > max_upscale)
		return -EINVAL;

	scale_h = scaling_info->dst_rect.height * 1000 /
		  scaling_info->src_rect.height;

	if (scale_h < min_downscale || scale_h > max_upscale)
		return -EINVAL;

	/*
	 * The "scaling_quality" can be ignored for now, quality = 0 has DC
	 * assume reasonable defaults based on the format.
	 */

	return 0;
}

static int amdgpu_dm_plane_atomic_check(struct drm_plane *plane,
					struct drm_atomic_state *state)
{
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state,
										 plane);
	struct amdgpu_device *adev = drm_to_adev(plane->dev);
	struct dc *dc = adev->dm.dc;
	struct dm_plane_state *dm_plane_state;
	struct dc_scaling_info scaling_info;
	struct drm_crtc_state *new_crtc_state;
	int ret;

	trace_amdgpu_dm_plane_atomic_check(new_plane_state);

	dm_plane_state = to_dm_plane_state(new_plane_state);

	if (!dm_plane_state->dc_state)
		return 0;

	new_crtc_state =
		drm_atomic_get_new_crtc_state(state,
					      new_plane_state->crtc);
	if (!new_crtc_state)
		return -EINVAL;

	ret = amdgpu_dm_plane_helper_check_state(new_plane_state, new_crtc_state);
	if (ret)
		return ret;

	ret = amdgpu_dm_plane_fill_dc_scaling_info(adev, new_plane_state, &scaling_info);
	if (ret)
		return ret;

	if (dc_validate_plane(dc, dm_plane_state->dc_state) == DC_OK)
		return 0;

	return -EINVAL;
}

static int amdgpu_dm_plane_atomic_async_check(struct drm_plane *plane,
					      struct drm_atomic_state *state)
{
	struct drm_crtc_state *new_crtc_state;
	struct drm_plane_state *new_plane_state;
	struct dm_crtc_state *dm_new_crtc_state;

	/* Only support async updates on cursor planes. */
	if (plane->type != DRM_PLANE_TYPE_CURSOR)
		return -EINVAL;

	new_plane_state = drm_atomic_get_new_plane_state(state, plane);
	new_crtc_state = drm_atomic_get_new_crtc_state(state, new_plane_state->crtc);
	dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);
	/* Reject overlay cursors for now*/
	if (dm_new_crtc_state->cursor_mode == DM_CURSOR_OVERLAY_MODE)
		return -EINVAL;

	return 0;
}

int amdgpu_dm_plane_get_cursor_position(struct drm_plane *plane, struct drm_crtc *crtc,
					struct dc_cursor_position *position)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	struct amdgpu_device *adev = drm_to_adev(plane->dev);
	int x, y;
	int xorigin = 0, yorigin = 0;

	if (!crtc || !plane->state->fb)
		return 0;

	if ((plane->state->crtc_w > amdgpu_crtc->max_cursor_width) ||
	    (plane->state->crtc_h > amdgpu_crtc->max_cursor_height)) {
		DRM_ERROR("%s: bad cursor width or height %d x %d\n",
			  __func__,
			  plane->state->crtc_w,
			  plane->state->crtc_h);
		return -EINVAL;
	}

	x = plane->state->crtc_x;
	y = plane->state->crtc_y;

	if (x <= -amdgpu_crtc->max_cursor_width ||
	    y <= -amdgpu_crtc->max_cursor_height)
		return 0;

	if (x < 0) {
		xorigin = min(-x, amdgpu_crtc->max_cursor_width - 1);
		x = 0;
	}
	if (y < 0) {
		yorigin = min(-y, amdgpu_crtc->max_cursor_height - 1);
		y = 0;
	}
	position->enable = true;
	position->x = x;
	position->y = y;
	position->x_hotspot = xorigin;
	position->y_hotspot = yorigin;

	if (amdgpu_ip_version(adev, DCE_HWIP, 0) < IP_VERSION(4, 0, 1))
		position->translate_by_source = true;

	return 0;
}

void amdgpu_dm_plane_handle_cursor_update(struct drm_plane *plane,
				 struct drm_plane_state *old_plane_state)
{
	struct amdgpu_device *adev = drm_to_adev(plane->dev);
	struct amdgpu_framebuffer *afb = to_amdgpu_framebuffer(plane->state->fb);
	struct drm_crtc *crtc = afb ? plane->state->crtc : old_plane_state->crtc;
	struct dm_crtc_state *crtc_state = crtc ? to_dm_crtc_state(crtc->state) : NULL;
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	uint64_t address = afb ? afb->address : 0;
	struct dc_cursor_position position = {0};
	struct dc_cursor_attributes attributes;
	int ret;

	if (!plane->state->fb && !old_plane_state->fb)
		return;

	drm_dbg_atomic(plane->dev, "crtc_id=%d with size %d to %d\n",
		       amdgpu_crtc->crtc_id, plane->state->crtc_w,
		       plane->state->crtc_h);

	ret = amdgpu_dm_plane_get_cursor_position(plane, crtc, &position);
	if (ret)
		return;

	if (!position.enable) {
		/* turn off cursor */
		if (crtc_state && crtc_state->stream) {
			mutex_lock(&adev->dm.dc_lock);
			dc_stream_program_cursor_position(crtc_state->stream,
						      &position);
			mutex_unlock(&adev->dm.dc_lock);
		}
		return;
	}

	amdgpu_crtc->cursor_width = plane->state->crtc_w;
	amdgpu_crtc->cursor_height = plane->state->crtc_h;

	memset(&attributes, 0, sizeof(attributes));
	attributes.address.high_part = upper_32_bits(address);
	attributes.address.low_part  = lower_32_bits(address);
	attributes.width             = plane->state->crtc_w;
	attributes.height            = plane->state->crtc_h;
	attributes.color_format      = CURSOR_MODE_COLOR_PRE_MULTIPLIED_ALPHA;
	attributes.rotation_angle    = 0;
	attributes.attribute_flags.value = 0;

	/* Enable cursor degamma ROM on DCN3+ for implicit sRGB degamma in DRM
	 * legacy gamma setup.
	 */
	if (crtc_state->cm_is_degamma_srgb &&
	    adev->dm.dc->caps.color.dpp.gamma_corr)
		attributes.attribute_flags.bits.ENABLE_CURSOR_DEGAMMA = 1;

	if (afb)
		attributes.pitch = afb->base.pitches[0] / afb->base.format->cpp[0];

	if (crtc_state->stream) {
		mutex_lock(&adev->dm.dc_lock);
		if (!dc_stream_program_cursor_attributes(crtc_state->stream,
							 &attributes))
			DRM_ERROR("DC failed to set cursor attributes\n");

		if (!dc_stream_program_cursor_position(crtc_state->stream,
						   &position))
			DRM_ERROR("DC failed to set cursor position\n");
		mutex_unlock(&adev->dm.dc_lock);
	}
}

static void amdgpu_dm_plane_atomic_async_update(struct drm_plane *plane,
						struct drm_atomic_state *state)
{
	struct drm_plane_state *new_state = drm_atomic_get_new_plane_state(state,
									   plane);
	struct drm_plane_state *old_state =
		drm_atomic_get_old_plane_state(state, plane);

	trace_amdgpu_dm_atomic_update_cursor(new_state);

	swap(plane->state->fb, new_state->fb);

	plane->state->src_x = new_state->src_x;
	plane->state->src_y = new_state->src_y;
	plane->state->src_w = new_state->src_w;
	plane->state->src_h = new_state->src_h;
	plane->state->crtc_x = new_state->crtc_x;
	plane->state->crtc_y = new_state->crtc_y;
	plane->state->crtc_w = new_state->crtc_w;
	plane->state->crtc_h = new_state->crtc_h;

	amdgpu_dm_plane_handle_cursor_update(plane, old_state);
}

static const struct drm_plane_helper_funcs dm_plane_helper_funcs = {
	.prepare_fb = amdgpu_dm_plane_helper_prepare_fb,
	.cleanup_fb = amdgpu_dm_plane_helper_cleanup_fb,
	.atomic_check = amdgpu_dm_plane_atomic_check,
	.atomic_async_check = amdgpu_dm_plane_atomic_async_check,
	.atomic_async_update = amdgpu_dm_plane_atomic_async_update
};

static void amdgpu_dm_plane_drm_plane_reset(struct drm_plane *plane)
{
	struct dm_plane_state *amdgpu_state = NULL;

	if (plane->state)
		plane->funcs->atomic_destroy_state(plane, plane->state);

	amdgpu_state = kzalloc(sizeof(*amdgpu_state), GFP_KERNEL);
	WARN_ON(amdgpu_state == NULL);

	if (!amdgpu_state)
		return;

	__drm_atomic_helper_plane_reset(plane, &amdgpu_state->base);
	amdgpu_state->degamma_tf = AMDGPU_TRANSFER_FUNCTION_DEFAULT;
	amdgpu_state->hdr_mult = AMDGPU_HDR_MULT_DEFAULT;
	amdgpu_state->shaper_tf = AMDGPU_TRANSFER_FUNCTION_DEFAULT;
	amdgpu_state->blend_tf = AMDGPU_TRANSFER_FUNCTION_DEFAULT;
}

static struct drm_plane_state *amdgpu_dm_plane_drm_plane_duplicate_state(struct drm_plane *plane)
{
	struct dm_plane_state *dm_plane_state, *old_dm_plane_state;

	old_dm_plane_state = to_dm_plane_state(plane->state);
	dm_plane_state = kzalloc(sizeof(*dm_plane_state), GFP_KERNEL);
	if (!dm_plane_state)
		return NULL;

	__drm_atomic_helper_plane_duplicate_state(plane, &dm_plane_state->base);

	if (old_dm_plane_state->dc_state) {
		dm_plane_state->dc_state = old_dm_plane_state->dc_state;
		dc_plane_state_retain(dm_plane_state->dc_state);
	}

	if (old_dm_plane_state->degamma_lut)
		dm_plane_state->degamma_lut =
			drm_property_blob_get(old_dm_plane_state->degamma_lut);
	if (old_dm_plane_state->ctm)
		dm_plane_state->ctm =
			drm_property_blob_get(old_dm_plane_state->ctm);
	if (old_dm_plane_state->shaper_lut)
		dm_plane_state->shaper_lut =
			drm_property_blob_get(old_dm_plane_state->shaper_lut);
	if (old_dm_plane_state->lut3d)
		dm_plane_state->lut3d =
			drm_property_blob_get(old_dm_plane_state->lut3d);
	if (old_dm_plane_state->blend_lut)
		dm_plane_state->blend_lut =
			drm_property_blob_get(old_dm_plane_state->blend_lut);

	dm_plane_state->degamma_tf = old_dm_plane_state->degamma_tf;
	dm_plane_state->hdr_mult = old_dm_plane_state->hdr_mult;
	dm_plane_state->shaper_tf = old_dm_plane_state->shaper_tf;
	dm_plane_state->blend_tf = old_dm_plane_state->blend_tf;

	return &dm_plane_state->base;
}

static bool amdgpu_dm_plane_format_mod_supported(struct drm_plane *plane,
						 uint32_t format,
						 uint64_t modifier)
{
	struct amdgpu_device *adev = drm_to_adev(plane->dev);
	const struct drm_format_info *info = drm_format_info(format);
	int i;

	if (!info)
		return false;

	/*
	 * We always have to allow these modifiers:
	 * 1. Core DRM checks for LINEAR support if userspace does not provide modifiers.
	 * 2. Not passing any modifiers is the same as explicitly passing INVALID.
	 */
	if (modifier == DRM_FORMAT_MOD_LINEAR ||
	    modifier == DRM_FORMAT_MOD_INVALID) {
		return true;
	}

	/* Check that the modifier is on the list of the plane's supported modifiers. */
	for (i = 0; i < plane->modifier_count; i++) {
		if (modifier == plane->modifiers[i])
			break;
	}
	if (i == plane->modifier_count)
		return false;

	/* GFX12 doesn't have these limitations. */
	if (AMD_FMT_MOD_GET(TILE_VERSION, modifier) <= AMD_FMT_MOD_TILE_VER_GFX11) {
		enum dm_micro_swizzle microtile = amdgpu_dm_plane_modifier_gfx9_swizzle_mode(modifier) & 3;

		/*
		 * For D swizzle the canonical modifier depends on the bpp, so check
		 * it here.
		 */
		if (AMD_FMT_MOD_GET(TILE_VERSION, modifier) == AMD_FMT_MOD_TILE_VER_GFX9 &&
		    adev->family >= AMDGPU_FAMILY_NV) {
			if (microtile == MICRO_SWIZZLE_D && info->cpp[0] == 4)
				return false;
		}

		if (adev->family >= AMDGPU_FAMILY_RV && microtile == MICRO_SWIZZLE_D &&
		    info->cpp[0] < 8)
			return false;

		if (amdgpu_dm_plane_modifier_has_dcc(modifier)) {
			/* Per radeonsi comments 16/64 bpp are more complicated. */
			if (info->cpp[0] != 4)
				return false;
			/* We support multi-planar formats, but not when combined with
			 * additional DCC metadata planes.
			 */
			if (info->num_planes > 1)
				return false;
		}
	}

	return true;
}

static void amdgpu_dm_plane_drm_plane_destroy_state(struct drm_plane *plane,
						    struct drm_plane_state *state)
{
	struct dm_plane_state *dm_plane_state = to_dm_plane_state(state);

	if (dm_plane_state->degamma_lut)
		drm_property_blob_put(dm_plane_state->degamma_lut);
	if (dm_plane_state->ctm)
		drm_property_blob_put(dm_plane_state->ctm);
	if (dm_plane_state->lut3d)
		drm_property_blob_put(dm_plane_state->lut3d);
	if (dm_plane_state->shaper_lut)
		drm_property_blob_put(dm_plane_state->shaper_lut);
	if (dm_plane_state->blend_lut)
		drm_property_blob_put(dm_plane_state->blend_lut);

	if (dm_plane_state->dc_state)
		dc_plane_state_release(dm_plane_state->dc_state);

	drm_atomic_helper_plane_destroy_state(plane, state);
}

#ifdef AMD_PRIVATE_COLOR
static void
dm_atomic_plane_attach_color_mgmt_properties(struct amdgpu_display_manager *dm,
					     struct drm_plane *plane)
{
	struct amdgpu_mode_info mode_info = dm->adev->mode_info;
	struct dpp_color_caps dpp_color_caps = dm->dc->caps.color.dpp;

	/* Check HW color pipeline capabilities on DPP block (pre-blending)
	 * before exposing related properties.
	 */
	if (dpp_color_caps.dgam_ram || dpp_color_caps.gamma_corr) {
		drm_object_attach_property(&plane->base,
					   mode_info.plane_degamma_lut_property,
					   0);
		drm_object_attach_property(&plane->base,
					   mode_info.plane_degamma_lut_size_property,
					   MAX_COLOR_LUT_ENTRIES);
		drm_object_attach_property(&plane->base,
					   dm->adev->mode_info.plane_degamma_tf_property,
					   AMDGPU_TRANSFER_FUNCTION_DEFAULT);
	}
	/* HDR MULT is always available */
	drm_object_attach_property(&plane->base,
				   dm->adev->mode_info.plane_hdr_mult_property,
				   AMDGPU_HDR_MULT_DEFAULT);

	/* Only enable plane CTM if both DPP and MPC gamut remap is available. */
	if (dm->dc->caps.color.mpc.gamut_remap)
		drm_object_attach_property(&plane->base,
					   dm->adev->mode_info.plane_ctm_property, 0);

	if (dpp_color_caps.hw_3d_lut) {
		drm_object_attach_property(&plane->base,
					   mode_info.plane_shaper_lut_property, 0);
		drm_object_attach_property(&plane->base,
					   mode_info.plane_shaper_lut_size_property,
					   MAX_COLOR_LUT_ENTRIES);
		drm_object_attach_property(&plane->base,
					   mode_info.plane_shaper_tf_property,
					   AMDGPU_TRANSFER_FUNCTION_DEFAULT);
		drm_object_attach_property(&plane->base,
					   mode_info.plane_lut3d_property, 0);
		drm_object_attach_property(&plane->base,
					   mode_info.plane_lut3d_size_property,
					   MAX_COLOR_3DLUT_SIZE);
	}

	if (dpp_color_caps.ogam_ram) {
		drm_object_attach_property(&plane->base,
					   mode_info.plane_blend_lut_property, 0);
		drm_object_attach_property(&plane->base,
					   mode_info.plane_blend_lut_size_property,
					   MAX_COLOR_LUT_ENTRIES);
		drm_object_attach_property(&plane->base,
					   mode_info.plane_blend_tf_property,
					   AMDGPU_TRANSFER_FUNCTION_DEFAULT);
	}
}

static int
dm_atomic_plane_set_property(struct drm_plane *plane,
			     struct drm_plane_state *state,
			     struct drm_property *property,
			     uint64_t val)
{
	struct dm_plane_state *dm_plane_state = to_dm_plane_state(state);
	struct amdgpu_device *adev = drm_to_adev(plane->dev);
	bool replaced = false;
	int ret;

	if (property == adev->mode_info.plane_degamma_lut_property) {
		ret = drm_property_replace_blob_from_id(plane->dev,
							&dm_plane_state->degamma_lut,
							val, -1,
							sizeof(struct drm_color_lut),
							&replaced);
		dm_plane_state->base.color_mgmt_changed |= replaced;
		return ret;
	} else if (property == adev->mode_info.plane_degamma_tf_property) {
		if (dm_plane_state->degamma_tf != val) {
			dm_plane_state->degamma_tf = val;
			dm_plane_state->base.color_mgmt_changed = 1;
		}
	} else if (property == adev->mode_info.plane_hdr_mult_property) {
		if (dm_plane_state->hdr_mult != val) {
			dm_plane_state->hdr_mult = val;
			dm_plane_state->base.color_mgmt_changed = 1;
		}
	} else if (property == adev->mode_info.plane_ctm_property) {
		ret = drm_property_replace_blob_from_id(plane->dev,
							&dm_plane_state->ctm,
							val,
							sizeof(struct drm_color_ctm_3x4), -1,
							&replaced);
		dm_plane_state->base.color_mgmt_changed |= replaced;
		return ret;
	} else if (property == adev->mode_info.plane_shaper_lut_property) {
		ret = drm_property_replace_blob_from_id(plane->dev,
							&dm_plane_state->shaper_lut,
							val, -1,
							sizeof(struct drm_color_lut),
							&replaced);
		dm_plane_state->base.color_mgmt_changed |= replaced;
		return ret;
	} else if (property == adev->mode_info.plane_shaper_tf_property) {
		if (dm_plane_state->shaper_tf != val) {
			dm_plane_state->shaper_tf = val;
			dm_plane_state->base.color_mgmt_changed = 1;
		}
	} else if (property == adev->mode_info.plane_lut3d_property) {
		ret = drm_property_replace_blob_from_id(plane->dev,
							&dm_plane_state->lut3d,
							val, -1,
							sizeof(struct drm_color_lut),
							&replaced);
		dm_plane_state->base.color_mgmt_changed |= replaced;
		return ret;
	} else if (property == adev->mode_info.plane_blend_lut_property) {
		ret = drm_property_replace_blob_from_id(plane->dev,
							&dm_plane_state->blend_lut,
							val, -1,
							sizeof(struct drm_color_lut),
							&replaced);
		dm_plane_state->base.color_mgmt_changed |= replaced;
		return ret;
	} else if (property == adev->mode_info.plane_blend_tf_property) {
		if (dm_plane_state->blend_tf != val) {
			dm_plane_state->blend_tf = val;
			dm_plane_state->base.color_mgmt_changed = 1;
		}
	} else {
		drm_dbg_atomic(plane->dev,
			       "[PLANE:%d:%s] unknown property [PROP:%d:%s]]\n",
			       plane->base.id, plane->name,
			       property->base.id, property->name);
		return -EINVAL;
	}

	return 0;
}

static int
dm_atomic_plane_get_property(struct drm_plane *plane,
			     const struct drm_plane_state *state,
			     struct drm_property *property,
			     uint64_t *val)
{
	struct dm_plane_state *dm_plane_state = to_dm_plane_state(state);
	struct amdgpu_device *adev = drm_to_adev(plane->dev);

	if (property == adev->mode_info.plane_degamma_lut_property) {
		*val = (dm_plane_state->degamma_lut) ?
			dm_plane_state->degamma_lut->base.id : 0;
	} else if (property == adev->mode_info.plane_degamma_tf_property) {
		*val = dm_plane_state->degamma_tf;
	} else if (property == adev->mode_info.plane_hdr_mult_property) {
		*val = dm_plane_state->hdr_mult;
	} else if (property == adev->mode_info.plane_ctm_property) {
		*val = (dm_plane_state->ctm) ?
			dm_plane_state->ctm->base.id : 0;
	} else 	if (property == adev->mode_info.plane_shaper_lut_property) {
		*val = (dm_plane_state->shaper_lut) ?
			dm_plane_state->shaper_lut->base.id : 0;
	} else if (property == adev->mode_info.plane_shaper_tf_property) {
		*val = dm_plane_state->shaper_tf;
	} else 	if (property == adev->mode_info.plane_lut3d_property) {
		*val = (dm_plane_state->lut3d) ?
			dm_plane_state->lut3d->base.id : 0;
	} else 	if (property == adev->mode_info.plane_blend_lut_property) {
		*val = (dm_plane_state->blend_lut) ?
			dm_plane_state->blend_lut->base.id : 0;
	} else if (property == adev->mode_info.plane_blend_tf_property) {
		*val = dm_plane_state->blend_tf;

	} else {
		return -EINVAL;
	}

	return 0;
}
#endif

static const struct drm_plane_funcs dm_plane_funcs = {
	.update_plane	= drm_atomic_helper_update_plane,
	.disable_plane	= drm_atomic_helper_disable_plane,
	.destroy	= drm_plane_helper_destroy,
	.reset = amdgpu_dm_plane_drm_plane_reset,
	.atomic_duplicate_state = amdgpu_dm_plane_drm_plane_duplicate_state,
	.atomic_destroy_state = amdgpu_dm_plane_drm_plane_destroy_state,
	.format_mod_supported = amdgpu_dm_plane_format_mod_supported,
#ifdef AMD_PRIVATE_COLOR
	.atomic_set_property = dm_atomic_plane_set_property,
	.atomic_get_property = dm_atomic_plane_get_property,
#endif
};

int amdgpu_dm_plane_init(struct amdgpu_display_manager *dm,
				struct drm_plane *plane,
				unsigned long possible_crtcs,
				const struct dc_plane_cap *plane_cap)
{
	uint32_t formats[32];
	int num_formats;
	int res = -EPERM;
	unsigned int supported_rotations;
	uint64_t *modifiers = NULL;
	unsigned int primary_zpos = dm->dc->caps.max_slave_planes;

	num_formats = amdgpu_dm_plane_get_plane_formats(plane, plane_cap, formats,
							ARRAY_SIZE(formats));

	res = amdgpu_dm_plane_get_plane_modifiers(dm->adev, plane->type, &modifiers);
	if (res)
		return res;

	if (modifiers == NULL)
		adev_to_drm(dm->adev)->mode_config.fb_modifiers_not_supported = true;

	res = drm_universal_plane_init(adev_to_drm(dm->adev), plane, possible_crtcs,
				       &dm_plane_funcs, formats, num_formats,
				       modifiers, plane->type, NULL);
	kfree(modifiers);
	if (res)
		return res;

	if (plane->type == DRM_PLANE_TYPE_OVERLAY &&
	    plane_cap && plane_cap->per_pixel_alpha) {
		unsigned int blend_caps = BIT(DRM_MODE_BLEND_PIXEL_NONE) |
					  BIT(DRM_MODE_BLEND_PREMULTI) |
					  BIT(DRM_MODE_BLEND_COVERAGE);

		drm_plane_create_alpha_property(plane);
		drm_plane_create_blend_mode_property(plane, blend_caps);
	}

	if (plane->type == DRM_PLANE_TYPE_PRIMARY) {
		/*
		 * Allow OVERLAY planes to be used as underlays by assigning an
		 * immutable zpos = # of OVERLAY planes to the PRIMARY plane.
		 */
		drm_plane_create_zpos_immutable_property(plane, primary_zpos);
	} else if (plane->type == DRM_PLANE_TYPE_OVERLAY) {
		/*
		 * OVERLAY planes can be below or above the PRIMARY, but cannot
		 * be above the CURSOR plane.
		 */
		unsigned int zpos = primary_zpos + 1 + drm_plane_index(plane);

		drm_plane_create_zpos_property(plane, zpos, 0, 254);
	} else if (plane->type == DRM_PLANE_TYPE_CURSOR) {
		drm_plane_create_zpos_immutable_property(plane, 255);
	}

	if (plane->type == DRM_PLANE_TYPE_PRIMARY &&
	    plane_cap &&
	    (plane_cap->pixel_format_support.nv12 ||
	     plane_cap->pixel_format_support.p010)) {
		/* This only affects YUV formats. */
		drm_plane_create_color_properties(
			plane,
			BIT(DRM_COLOR_YCBCR_BT601) |
			BIT(DRM_COLOR_YCBCR_BT709) |
			BIT(DRM_COLOR_YCBCR_BT2020),
			BIT(DRM_COLOR_YCBCR_LIMITED_RANGE) |
			BIT(DRM_COLOR_YCBCR_FULL_RANGE),
			DRM_COLOR_YCBCR_BT709, DRM_COLOR_YCBCR_LIMITED_RANGE);
	}

	supported_rotations =
		DRM_MODE_ROTATE_0 | DRM_MODE_ROTATE_90 |
		DRM_MODE_ROTATE_180 | DRM_MODE_ROTATE_270;

	if (dm->adev->asic_type >= CHIP_BONAIRE &&
	    plane->type != DRM_PLANE_TYPE_CURSOR)
		drm_plane_create_rotation_property(plane, DRM_MODE_ROTATE_0,
						   supported_rotations);

	if (amdgpu_ip_version(dm->adev, DCE_HWIP, 0) > IP_VERSION(3, 0, 1) &&
	    plane->type != DRM_PLANE_TYPE_CURSOR)
		drm_plane_enable_fb_damage_clips(plane);

	drm_plane_helper_add(plane, &dm_plane_helper_funcs);

#ifdef AMD_PRIVATE_COLOR
	dm_atomic_plane_attach_color_mgmt_properties(dm, plane);
#endif
	/* Create (reset) the plane state */
	if (plane->funcs->reset)
		plane->funcs->reset(plane);

	return 0;
}

bool amdgpu_dm_plane_is_video_format(uint32_t format)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(video_formats); i++)
		if (format == video_formats[i])
			return true;

	return false;
}

