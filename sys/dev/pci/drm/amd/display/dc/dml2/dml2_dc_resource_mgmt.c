/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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

#include "dml2_mall_phantom.h"

#include "dml2_dc_types.h"
#include "dml2_internal_types.h"
#include "dml2_utils.h"
#include "dml2_dc_resource_mgmt.h"

#define MAX_ODM_FACTOR 4
#define MAX_MPCC_FACTOR 4

struct dc_plane_pipe_pool {
	int pipes_assigned_to_plane[MAX_ODM_FACTOR][MAX_MPCC_FACTOR];
	bool pipe_used[MAX_ODM_FACTOR][MAX_MPCC_FACTOR];
	int num_pipes_assigned_to_plane_for_mpcc_combine;
	int num_pipes_assigned_to_plane_for_odm_combine;
};

struct dc_pipe_mapping_scratch {
	struct {
		unsigned int odm_factor;
		unsigned int odm_slice_end_x[MAX_PIPES];
		struct pipe_ctx *next_higher_pipe_for_odm_slice[MAX_PIPES];
	} odm_info;
	struct {
		unsigned int mpc_factor;
		struct pipe_ctx *prev_odm_pipe;
	} mpc_info;

	struct dc_plane_pipe_pool pipe_pool;
};

static bool get_plane_id(struct dml2_context *dml2, const struct dc_state *state, const struct dc_plane_state *plane,
	unsigned int stream_id, unsigned int plane_index, unsigned int *plane_id)
{
	int i, j;
	bool is_plane_duplicate = dml2->v20.scratch.plane_duplicate_exists;

	if (!plane_id)
		return false;

	for (i = 0; i < state->stream_count; i++) {
		if (state->streams[i]->stream_id == stream_id) {
			for (j = 0; j < state->stream_status[i].plane_count; j++) {
				if (state->stream_status[i].plane_states[j] == plane &&
					(!is_plane_duplicate || (j == plane_index))) {
					*plane_id = (i << 16) | j;
					return true;
				}
			}
		}
	}

	return false;
}

static int find_disp_cfg_idx_by_plane_id(struct dml2_dml_to_dc_pipe_mapping *mapping, unsigned int plane_id)
{
	int i;

	for (i = 0; i < __DML2_WRAPPER_MAX_STREAMS_PLANES__; i++) {
		if (mapping->disp_cfg_to_plane_id_valid[i] && mapping->disp_cfg_to_plane_id[i] == plane_id)
			return  i;
	}

	ASSERT(false);
	return __DML2_WRAPPER_MAX_STREAMS_PLANES__;
}

static int find_disp_cfg_idx_by_stream_id(struct dml2_dml_to_dc_pipe_mapping *mapping, unsigned int stream_id)
{
	int i;

	for (i = 0; i < __DML2_WRAPPER_MAX_STREAMS_PLANES__; i++) {
		if (mapping->disp_cfg_to_stream_id_valid[i] && mapping->disp_cfg_to_stream_id[i] == stream_id)
			return  i;
	}

	ASSERT(false);
	return __DML2_WRAPPER_MAX_STREAMS_PLANES__;
}

// The master pipe of a stream is defined as the top pipe in odm slice 0
static struct pipe_ctx *find_master_pipe_of_stream(struct dml2_context *ctx, struct dc_state *state, unsigned int stream_id)
{
	int i;

	for (i = 0; i < ctx->config.dcn_pipe_count; i++) {
		if (state->res_ctx.pipe_ctx[i].stream && state->res_ctx.pipe_ctx[i].stream->stream_id == stream_id) {
			if (!state->res_ctx.pipe_ctx[i].prev_odm_pipe && !state->res_ctx.pipe_ctx[i].top_pipe)
				return &state->res_ctx.pipe_ctx[i];
		}
	}

	return NULL;
}

static struct pipe_ctx *find_master_pipe_of_plane(struct dml2_context *ctx,
	struct dc_state *state, unsigned int plane_id)
{
	int i;
	unsigned int plane_id_assigned_to_pipe;

	for (i = 0; i < ctx->config.dcn_pipe_count; i++) {
		if (state->res_ctx.pipe_ctx[i].plane_state && get_plane_id(ctx, state, state->res_ctx.pipe_ctx[i].plane_state,
			state->res_ctx.pipe_ctx[i].stream->stream_id,
			ctx->v20.scratch.dml_to_dc_pipe_mapping.dml_pipe_idx_to_plane_index[state->res_ctx.pipe_ctx[i].pipe_idx], &plane_id_assigned_to_pipe)) {
			if (plane_id_assigned_to_pipe == plane_id)
				return &state->res_ctx.pipe_ctx[i];
		}
	}

	return NULL;
}

static unsigned int find_pipes_assigned_to_plane(struct dml2_context *ctx,
	struct dc_state *state, unsigned int plane_id, unsigned int *pipes)
{
	int i;
	unsigned int num_found = 0;
	unsigned int plane_id_assigned_to_pipe = -1;

	for (i = 0; i < ctx->config.dcn_pipe_count; i++) {
		struct pipe_ctx *pipe = &state->res_ctx.pipe_ctx[i];

		if (!pipe->plane_state || !pipe->stream)
			continue;

		get_plane_id(ctx, state, pipe->plane_state, pipe->stream->stream_id,
					ctx->v20.scratch.dml_to_dc_pipe_mapping.dml_pipe_idx_to_plane_index[pipe->pipe_idx],
					&plane_id_assigned_to_pipe);
		if (plane_id_assigned_to_pipe == plane_id && !pipe->prev_odm_pipe
				&& (!pipe->top_pipe || pipe->top_pipe->plane_state != pipe->plane_state)) {
			while (pipe) {
				struct pipe_ctx *mpc_pipe = pipe;

				while (mpc_pipe) {
					pipes[num_found++] = mpc_pipe->pipe_idx;
					mpc_pipe = mpc_pipe->bottom_pipe;
					if (!mpc_pipe)
						break;
					if (mpc_pipe->plane_state != pipe->plane_state)
						mpc_pipe = NULL;
				}
				pipe = pipe->next_odm_pipe;
			}
			break;
		}
	}

	return num_found;
}

static bool validate_pipe_assignment(const struct dml2_context *ctx, const struct dc_state *state, const struct dml_display_cfg_st *disp_cfg, const struct dml2_dml_to_dc_pipe_mapping *mapping)
{
//	int i, j, k;
//
//	unsigned int plane_id;
//
//	unsigned int disp_cfg_index;
//
//	unsigned int pipes_assigned_to_plane[MAX_PIPES];
//	unsigned int num_pipes_assigned_to_plane;
//
//	struct pipe_ctx *top_pipe;
//
//	for (i = 0; i < state->stream_count; i++) {
//		for (j = 0; j < state->stream_status[i]->plane_count; j++) {
//			if (get_plane_id(state, state->stream_status.plane_states[j], &plane_id)) {
//				disp_cfg_index = find_disp_cfg_idx_by_plane_id(mapping, plane_id);
//				num_pipes_assigned_to_plane = find_pipes_assigned_to_plane(ctx, state, plane_id, pipes_assigned_to_plane);
//
//				if (disp_cfg_index >= 0 && num_pipes_assigned_to_plane > 0) {
//					// Verify the number of pipes assigned matches
//					if (disp_cfg->hw.DPPPerSurface != num_pipes_assigned_to_plane)
//						return false;
//
//					top_pipe = find_top_pipe_in_tree(state->res_ctx.pipe_ctx[pipes_assigned_to_plane[0]]);
//
//					// Verify MPC and ODM combine
//					if (disp_cfg->hw.ODMMode == dml_odm_mode_bypass) {
//						verify_combine_tree(top_pipe, state->streams[i]->stream_id, plane_id, state, false);
//					} else {
//						verify_combine_tree(top_pipe, state->streams[i]->stream_id, plane_id, state, true);
//					}
//
//					// TODO: could also do additional verification that the pipes in tree are the same as
//					// pipes_assigned_to_plane
//				} else {
//					ASSERT(false);
//					return false;
//				}
//			} else {
//				ASSERT(false);
//				return false;
//			}
//		}
//	}
	return true;
}

static bool is_plane_using_pipe(const struct pipe_ctx *pipe)
{
	if (pipe->plane_state)
		return true;

	return false;
}

static bool is_pipe_free(const struct pipe_ctx *pipe)
{
	if (!pipe->plane_state && !pipe->stream)
		return true;

	return false;
}

static unsigned int find_preferred_pipe_candidates(const struct dc_state *existing_state,
	const int pipe_count,
	const unsigned int stream_id,
	unsigned int *preferred_pipe_candidates)
{
	unsigned int num_preferred_candidates = 0;
	int i;

	/* There is only one case which we consider for adding a pipe to the preferred
	 * pipe candidate array:
	 *
	 * 1. If the existing stream id of the pipe is equivalent to the stream id
	 * of the stream we are trying to achieve MPC/ODM combine for. This allows
	 * us to minimize the changes in pipe topology during the transition.
	 *
	 * However this condition comes with a caveat. We need to ignore pipes that will
	 * require a change in OPP but still have the same stream id. For example during
	 * an MPC to ODM transiton.
	 *
	 * Adding check to avoid pipe select on the head pipe by utilizing dc resource
	 * helper function resource_get_primary_dpp_pipe and comparing the pipe index.
	 */
	if (existing_state) {
		for (i = 0; i < pipe_count; i++) {
			if (existing_state->res_ctx.pipe_ctx[i].stream && existing_state->res_ctx.pipe_ctx[i].stream->stream_id == stream_id) {
				struct pipe_ctx *head_pipe =
					resource_is_pipe_type(&existing_state->res_ctx.pipe_ctx[i], DPP_PIPE) ?
						resource_get_primary_dpp_pipe(&existing_state->res_ctx.pipe_ctx[i]) :
							NULL;

				// we should always respect the head pipe from selection
				if (head_pipe && head_pipe->pipe_idx == i)
					continue;
				if (existing_state->res_ctx.pipe_ctx[i].plane_res.hubp &&
					existing_state->res_ctx.pipe_ctx[i].plane_res.hubp->opp_id != i &&
						(existing_state->res_ctx.pipe_ctx[i].prev_odm_pipe ||
						existing_state->res_ctx.pipe_ctx[i].next_odm_pipe))
					continue;

				preferred_pipe_candidates[num_preferred_candidates++] = i;
			}
		}
	}

	return num_preferred_candidates;
}

static unsigned int find_last_resort_pipe_candidates(const struct dc_state *existing_state,
	const int pipe_count,
	const unsigned int stream_id,
	unsigned int *last_resort_pipe_candidates)
{
	unsigned int num_last_resort_candidates = 0;
	int i;

	/* There are two cases where we would like to add a given pipe into the last
	 * candidate array:
	 *
	 * 1. If the pipe requires a change in OPP, for example during an MPC
	 * to ODM transiton.
	 *
	 * 2. If the pipe already has an enabled OTG.
	 */
	if (existing_state) {
		for (i  = 0; i < pipe_count; i++) {
			struct pipe_ctx *head_pipe =
				resource_is_pipe_type(&existing_state->res_ctx.pipe_ctx[i], DPP_PIPE) ?
					resource_get_primary_dpp_pipe(&existing_state->res_ctx.pipe_ctx[i]) :
						NULL;

			// we should always respect the head pipe from selection
			if (head_pipe && head_pipe->pipe_idx == i)
				continue;
			if ((existing_state->res_ctx.pipe_ctx[i].plane_res.hubp &&
				existing_state->res_ctx.pipe_ctx[i].plane_res.hubp->opp_id != i) ||
				existing_state->res_ctx.pipe_ctx[i].stream_res.tg)
				last_resort_pipe_candidates[num_last_resort_candidates++] = i;
		}
	}

	return num_last_resort_candidates;
}

static bool is_pipe_in_candidate_array(const unsigned int pipe_idx,
	const unsigned int *candidate_array,
	const unsigned int candidate_array_size)
{
	int i;

	for (i = 0; i < candidate_array_size; i++) {
		if (candidate_array[i] == pipe_idx)
			return true;
	}

	return false;
}

static bool find_more_pipes_for_stream(struct dml2_context *ctx,
	struct dc_state *state, // The state we want to find a free mapping in
	unsigned int stream_id, // The stream we want this pipe to drive
	int *assigned_pipes,
	int *assigned_pipe_count,
	int pipes_needed,
	const struct dc_state *existing_state) // The state (optional) that we want to minimize remapping relative to
{
	struct pipe_ctx *pipe = NULL;
	unsigned int preferred_pipe_candidates[MAX_PIPES] = {0};
	unsigned int last_resort_pipe_candidates[MAX_PIPES] = {0};
	unsigned int num_preferred_candidates = 0;
	unsigned int num_last_resort_candidates = 0;
	int i;

	if (existing_state) {
		num_preferred_candidates =
			find_preferred_pipe_candidates(existing_state, ctx->config.dcn_pipe_count, stream_id, preferred_pipe_candidates);

		num_last_resort_candidates =
			find_last_resort_pipe_candidates(existing_state, ctx->config.dcn_pipe_count, stream_id, last_resort_pipe_candidates);
	}

	// First see if any of the preferred are unmapped, and choose those instead
	for (i = 0; pipes_needed > 0 && i < num_preferred_candidates; i++) {
		pipe = &state->res_ctx.pipe_ctx[preferred_pipe_candidates[i]];
		if (!is_plane_using_pipe(pipe)) {
			pipes_needed--;
			// TODO: This doens't make sense really, pipe_idx should always be valid
			pipe->pipe_idx = preferred_pipe_candidates[i];
			assigned_pipes[(*assigned_pipe_count)++] = pipe->pipe_idx;
		}
	}

	// We like to pair pipes starting from the higher order indicies for combining
	for (i = ctx->config.dcn_pipe_count - 1; pipes_needed > 0 && i >= 0; i--) {
		// Ignore any pipes that are the preferred or last resort candidate
		if (is_pipe_in_candidate_array(i, preferred_pipe_candidates, num_preferred_candidates) ||
			is_pipe_in_candidate_array(i, last_resort_pipe_candidates, num_last_resort_candidates))
			continue;

		pipe = &state->res_ctx.pipe_ctx[i];
		if (!is_plane_using_pipe(pipe)) {
			pipes_needed--;
			// TODO: This doens't make sense really, pipe_idx should always be valid
			pipe->pipe_idx = i;
			assigned_pipes[(*assigned_pipe_count)++] = pipe->pipe_idx;
		}
	}

	// Only use the last resort pipe candidates as a last resort
	for (i = 0; pipes_needed > 0 && i < num_last_resort_candidates; i++) {
		pipe = &state->res_ctx.pipe_ctx[last_resort_pipe_candidates[i]];
		if (!is_plane_using_pipe(pipe)) {
			pipes_needed--;
			// TODO: This doens't make sense really, pipe_idx should always be valid
			pipe->pipe_idx = last_resort_pipe_candidates[i];
			assigned_pipes[(*assigned_pipe_count)++] = pipe->pipe_idx;
		}
	}

	ASSERT(pipes_needed <= 0); // Validation should prevent us from building a pipe context that exceeds the number of HW resoruces available

	return pipes_needed <= 0;
}

static bool find_more_free_pipes(struct dml2_context *ctx,
	struct dc_state *state, // The state we want to find a free mapping in
	unsigned int stream_id, // The stream we want this pipe to drive
	int *assigned_pipes,
	int *assigned_pipe_count,
	int pipes_needed,
	const struct dc_state *existing_state) // The state (optional) that we want to minimize remapping relative to
{
	struct pipe_ctx *pipe = NULL;
	unsigned int preferred_pipe_candidates[MAX_PIPES] = {0};
	unsigned int last_resort_pipe_candidates[MAX_PIPES] = {0};
	unsigned int num_preferred_candidates = 0;
	unsigned int num_last_resort_candidates = 0;
	int i;

	if (existing_state) {
		num_preferred_candidates =
			find_preferred_pipe_candidates(existing_state, ctx->config.dcn_pipe_count, stream_id, preferred_pipe_candidates);

		num_last_resort_candidates =
			find_last_resort_pipe_candidates(existing_state, ctx->config.dcn_pipe_count, stream_id, last_resort_pipe_candidates);
	}

	// First see if any of the preferred are unmapped, and choose those instead
	for (i = 0; pipes_needed > 0 && i < num_preferred_candidates; i++) {
		pipe = &state->res_ctx.pipe_ctx[preferred_pipe_candidates[i]];
		if (is_pipe_free(pipe)) {
			pipes_needed--;
			// TODO: This doens't make sense really, pipe_idx should always be valid
			pipe->pipe_idx = preferred_pipe_candidates[i];
			assigned_pipes[(*assigned_pipe_count)++] = pipe->pipe_idx;
		}
	}

	// We like to pair pipes starting from the higher order indicies for combining
	for (i = ctx->config.dcn_pipe_count - 1; pipes_needed > 0 && i >= 0; i--) {
		// Ignore any pipes that are the preferred or last resort candidate
		if (is_pipe_in_candidate_array(i, preferred_pipe_candidates, num_preferred_candidates) ||
			is_pipe_in_candidate_array(i, last_resort_pipe_candidates, num_last_resort_candidates))
			continue;

		pipe = &state->res_ctx.pipe_ctx[i];
		if (is_pipe_free(pipe)) {
			pipes_needed--;
			// TODO: This doens't make sense really, pipe_idx should always be valid
			pipe->pipe_idx = i;
			assigned_pipes[(*assigned_pipe_count)++] = pipe->pipe_idx;
		}
	}

	// Only use the last resort pipe candidates as a last resort
	for (i = 0; pipes_needed > 0 && i < num_last_resort_candidates; i++) {
		pipe = &state->res_ctx.pipe_ctx[last_resort_pipe_candidates[i]];
		if (is_pipe_free(pipe)) {
			pipes_needed--;
			// TODO: This doens't make sense really, pipe_idx should always be valid
			pipe->pipe_idx = last_resort_pipe_candidates[i];
			assigned_pipes[(*assigned_pipe_count)++] = pipe->pipe_idx;
		}
	}

	ASSERT(pipes_needed == 0); // Validation should prevent us from building a pipe context that exceeds the number of HW resoruces available

	return pipes_needed == 0;
}

static void sort_pipes_for_splitting(struct dc_plane_pipe_pool *pipes)
{
	bool sorted, swapped;
	unsigned int cur_index;
	unsigned int temp;
	int odm_slice_index;

	for (odm_slice_index = 0; odm_slice_index < pipes->num_pipes_assigned_to_plane_for_odm_combine; odm_slice_index++) {
		// Sort each MPCC set
		//Un-optimized bubble sort, but that's okay for array sizes <= 6

		if (pipes->num_pipes_assigned_to_plane_for_mpcc_combine <= 1)
			sorted = true;
		else
			sorted = false;

		cur_index = 0;
		swapped = false;
		while (!sorted) {
			if (pipes->pipes_assigned_to_plane[odm_slice_index][cur_index] > pipes->pipes_assigned_to_plane[odm_slice_index][cur_index + 1]) {
				temp = pipes->pipes_assigned_to_plane[odm_slice_index][cur_index];
				pipes->pipes_assigned_to_plane[odm_slice_index][cur_index] = pipes->pipes_assigned_to_plane[odm_slice_index][cur_index + 1];
				pipes->pipes_assigned_to_plane[odm_slice_index][cur_index + 1] = temp;

				swapped = true;
			}

			cur_index++;

			if (cur_index == pipes->num_pipes_assigned_to_plane_for_mpcc_combine - 1) {
				cur_index = 0;

				if (swapped)
					sorted = false;
				else
					sorted = true;

				swapped = false;
			}

		}
	}
}

// For example, 3840 x 2160, ODM2:1 has a slice array of [1919, 3839], meaning, slice0 spans h_pixels 0->1919, and slice1 spans 1920->3840
static void calculate_odm_slices(const struct dc_stream_state *stream, unsigned int odm_factor, unsigned int *odm_slice_end_x)
{
	unsigned int slice_size = 0;
	int i;

	if (odm_factor < 1 || odm_factor > 4) {
		ASSERT(false);
		return;
	}

	slice_size = stream->src.width / odm_factor;

	for (i = 0; i < odm_factor; i++)
		odm_slice_end_x[i] = (slice_size * (i + 1)) - 1;

	odm_slice_end_x[odm_factor - 1] = stream->src.width - 1;
}

static void add_odm_slice_to_odm_tree(struct dml2_context *ctx,
		struct dc_state *state,
		struct dc_pipe_mapping_scratch *scratch,
		unsigned int odm_slice_index)
{
	struct pipe_ctx *pipe = NULL;
	int i;

	// MPCC Combine + ODM Combine is not supported, so there should never be a case where the current plane
	// has more than 1 pipe mapped to it for a given slice.
	ASSERT(scratch->pipe_pool.num_pipes_assigned_to_plane_for_mpcc_combine == 1 || scratch->pipe_pool.num_pipes_assigned_to_plane_for_odm_combine == 1);

	for (i = 0; i < scratch->pipe_pool.num_pipes_assigned_to_plane_for_mpcc_combine; i++) {
		pipe = &state->res_ctx.pipe_ctx[scratch->pipe_pool.pipes_assigned_to_plane[odm_slice_index][i]];

		if (scratch->mpc_info.prev_odm_pipe)
			scratch->mpc_info.prev_odm_pipe->next_odm_pipe = pipe;

		pipe->prev_odm_pipe = scratch->mpc_info.prev_odm_pipe;
		pipe->next_odm_pipe = NULL;
	}
	scratch->mpc_info.prev_odm_pipe = pipe;
}

static struct pipe_ctx *add_plane_to_blend_tree(struct dml2_context *ctx,
	struct dc_state *state,
	const struct dc_plane_state *plane,
	struct dc_plane_pipe_pool *pipe_pool,
	unsigned int odm_slice,
	struct pipe_ctx *top_pipe)
{
	int i;

	for (i = 0; i < pipe_pool->num_pipes_assigned_to_plane_for_mpcc_combine; i++) {
		if (top_pipe)
			top_pipe->bottom_pipe = &state->res_ctx.pipe_ctx[pipe_pool->pipes_assigned_to_plane[odm_slice][i]];

		pipe_pool->pipe_used[odm_slice][i] = true;

		state->res_ctx.pipe_ctx[pipe_pool->pipes_assigned_to_plane[odm_slice][i]].top_pipe = top_pipe;
		state->res_ctx.pipe_ctx[pipe_pool->pipes_assigned_to_plane[odm_slice][i]].bottom_pipe = NULL;

		top_pipe = &state->res_ctx.pipe_ctx[pipe_pool->pipes_assigned_to_plane[odm_slice][i]];
	}

	// After running the above loop, the top pipe actually ends up pointing to the bottom of this MPCC combine tree, so we are actually
	// returning the bottom pipe here
	return top_pipe;
}

static unsigned int find_pipes_assigned_to_stream(struct dml2_context *ctx, struct dc_state *state, unsigned int stream_id, unsigned int *pipes)
{
	int i;
	unsigned int num_found = 0;

	for (i = 0; i < ctx->config.dcn_pipe_count; i++) {
		struct pipe_ctx *pipe = &state->res_ctx.pipe_ctx[i];

		if (pipe->stream && pipe->stream->stream_id == stream_id && !pipe->top_pipe && !pipe->prev_odm_pipe) {
			while (pipe) {
				pipes[num_found++] = pipe->pipe_idx;
				pipe = pipe->next_odm_pipe;
			}
			break;
		}
	}

	return num_found;
}

static struct pipe_ctx *assign_pipes_to_stream(struct dml2_context *ctx, struct dc_state *state,
		const struct dc_stream_state *stream,
		int odm_factor,
		struct dc_plane_pipe_pool *pipe_pool,
		const struct dc_state *existing_state)
{
	struct pipe_ctx *master_pipe;
	unsigned int pipes_needed;
	unsigned int pipes_assigned;
	unsigned int pipes[MAX_PIPES] = {0};
	unsigned int next_pipe_to_assign;
	int odm_slice;

	pipes_needed = odm_factor;

	master_pipe = find_master_pipe_of_stream(ctx, state, stream->stream_id);
	ASSERT(master_pipe);

	pipes_assigned = find_pipes_assigned_to_stream(ctx, state, stream->stream_id, pipes);

	find_more_free_pipes(ctx, state, stream->stream_id, pipes, &pipes_assigned, pipes_needed - pipes_assigned, existing_state);

	ASSERT(pipes_assigned == pipes_needed);

	next_pipe_to_assign = 0;
	for (odm_slice = 0; odm_slice < odm_factor; odm_slice++)
		pipe_pool->pipes_assigned_to_plane[odm_slice][0] = pipes[next_pipe_to_assign++];

	pipe_pool->num_pipes_assigned_to_plane_for_mpcc_combine = 1;
	pipe_pool->num_pipes_assigned_to_plane_for_odm_combine = odm_factor;

	return master_pipe;
}

static struct pipe_ctx *assign_pipes_to_plane(struct dml2_context *ctx, struct dc_state *state,
		const struct dc_stream_state *stream,
		const struct dc_plane_state *plane,
		int odm_factor,
		int mpc_factor,
		int plane_index,
		struct dc_plane_pipe_pool *pipe_pool,
		const struct dc_state *existing_state)
{
	struct pipe_ctx *master_pipe = NULL;
	unsigned int plane_id;
	unsigned int pipes_needed;
	unsigned int pipes_assigned;
	unsigned int pipes[MAX_PIPES] = {0};
	unsigned int next_pipe_to_assign;
	int odm_slice, mpc_slice;

	if (!get_plane_id(ctx, state, plane, stream->stream_id, plane_index, &plane_id)) {
		ASSERT(false);
		return master_pipe;
	}

	pipes_needed = mpc_factor * odm_factor;

	master_pipe = find_master_pipe_of_plane(ctx, state, plane_id);
	ASSERT(master_pipe);

	pipes_assigned = find_pipes_assigned_to_plane(ctx, state, plane_id, pipes);

	find_more_pipes_for_stream(ctx, state, stream->stream_id, pipes, &pipes_assigned, pipes_needed - pipes_assigned, existing_state);

	ASSERT(pipes_assigned >= pipes_needed);

	next_pipe_to_assign = 0;
	for (odm_slice = 0; odm_slice < odm_factor; odm_slice++)
		for (mpc_slice = 0; mpc_slice < mpc_factor; mpc_slice++)
			pipe_pool->pipes_assigned_to_plane[odm_slice][mpc_slice] = pipes[next_pipe_to_assign++];

	pipe_pool->num_pipes_assigned_to_plane_for_mpcc_combine = mpc_factor;
	pipe_pool->num_pipes_assigned_to_plane_for_odm_combine = odm_factor;

	return master_pipe;
}

static bool is_pipe_used(const struct dc_plane_pipe_pool *pool, unsigned int pipe_idx)
{
	int i, j;

	for (i = 0; i < pool->num_pipes_assigned_to_plane_for_odm_combine; i++) {
		for (j = 0; j < pool->num_pipes_assigned_to_plane_for_mpcc_combine; j++) {
			if (pool->pipes_assigned_to_plane[i][j] == pipe_idx && pool->pipe_used[i][j])
				return true;
		}
	}

	return false;
}

static void free_pipe(struct pipe_ctx *pipe)
{
	memset(pipe, 0, sizeof(struct pipe_ctx));
}

static void free_unused_pipes_for_plane(struct dml2_context *ctx, struct dc_state *state,
	const struct dc_plane_state *plane, const struct dc_plane_pipe_pool *pool, unsigned int stream_id, int plane_index)
{
	int i;
	bool is_plane_duplicate = ctx->v20.scratch.plane_duplicate_exists;

	for (i = 0; i < ctx->config.dcn_pipe_count; i++) {
		if (state->res_ctx.pipe_ctx[i].plane_state == plane &&
			state->res_ctx.pipe_ctx[i].stream->stream_id == stream_id &&
			(!is_plane_duplicate ||
			ctx->v20.scratch.dml_to_dc_pipe_mapping.dml_pipe_idx_to_plane_index[state->res_ctx.pipe_ctx[i].pipe_idx] == plane_index) &&
			!is_pipe_used(pool, state->res_ctx.pipe_ctx[i].pipe_idx)) {
			free_pipe(&state->res_ctx.pipe_ctx[i]);
		}
	}
}

static void remove_pipes_from_blend_trees(struct dml2_context *ctx, struct dc_state *state, struct dc_plane_pipe_pool *pipe_pool, unsigned int odm_slice)
{
	struct pipe_ctx *pipe;
	int i;

	for (i = 0; i < pipe_pool->num_pipes_assigned_to_plane_for_mpcc_combine; i++) {
		pipe = &state->res_ctx.pipe_ctx[pipe_pool->pipes_assigned_to_plane[odm_slice][0]];
		if (pipe->top_pipe)
			pipe->top_pipe->bottom_pipe = pipe->bottom_pipe;

		if (pipe->bottom_pipe)
			pipe->bottom_pipe = pipe->top_pipe;

		pipe_pool->pipe_used[odm_slice][i] = true;
	}
}

static void map_pipes_for_stream(struct dml2_context *ctx, struct dc_state *state, const struct dc_stream_state *stream,
		struct dc_pipe_mapping_scratch *scratch, const struct dc_state *existing_state)
{
	int odm_slice_index;
	struct pipe_ctx *master_pipe = NULL;


	master_pipe = assign_pipes_to_stream(ctx, state, stream, scratch->odm_info.odm_factor, &scratch->pipe_pool, existing_state);
	sort_pipes_for_splitting(&scratch->pipe_pool);

	for (odm_slice_index = 0; odm_slice_index < scratch->odm_info.odm_factor; odm_slice_index++) {
		remove_pipes_from_blend_trees(ctx, state, &scratch->pipe_pool, odm_slice_index);

		add_odm_slice_to_odm_tree(ctx, state, scratch, odm_slice_index);

		ctx->config.callbacks.acquire_secondary_pipe_for_mpc_odm(ctx->config.callbacks.dc, state,
			master_pipe, &state->res_ctx.pipe_ctx[scratch->pipe_pool.pipes_assigned_to_plane[odm_slice_index][0]], true);
	}
}

static void map_pipes_for_plane(struct dml2_context *ctx, struct dc_state *state, const struct dc_stream_state *stream, const struct dc_plane_state *plane,
		int plane_index, struct dc_pipe_mapping_scratch *scratch, const struct dc_state *existing_state)
{
	int odm_slice_index;
	unsigned int plane_id;
	struct pipe_ctx *master_pipe = NULL;
	int i;

	if (!get_plane_id(ctx, state, plane, stream->stream_id, plane_index, &plane_id)) {
		ASSERT(false);
		return;
	}

	master_pipe = assign_pipes_to_plane(ctx, state, stream, plane, scratch->odm_info.odm_factor,
			scratch->mpc_info.mpc_factor, plane_index, &scratch->pipe_pool, existing_state);
	sort_pipes_for_splitting(&scratch->pipe_pool);

	for (odm_slice_index = 0; odm_slice_index < scratch->odm_info.odm_factor; odm_slice_index++) {
		// Now we have a list of all pipes to be used for this plane/stream, now setup the tree.
		scratch->odm_info.next_higher_pipe_for_odm_slice[odm_slice_index] = add_plane_to_blend_tree(ctx, state,
				plane,
				&scratch->pipe_pool,
				odm_slice_index,
				scratch->odm_info.next_higher_pipe_for_odm_slice[odm_slice_index]);

		add_odm_slice_to_odm_tree(ctx, state, scratch, odm_slice_index);

		for (i = 0; i < scratch->pipe_pool.num_pipes_assigned_to_plane_for_mpcc_combine; i++) {

			ctx->config.callbacks.acquire_secondary_pipe_for_mpc_odm(ctx->config.callbacks.dc, state,
				master_pipe, &state->res_ctx.pipe_ctx[scratch->pipe_pool.pipes_assigned_to_plane[odm_slice_index][i]], true);
		}
	}

	free_unused_pipes_for_plane(ctx, state, plane, &scratch->pipe_pool, stream->stream_id, plane_index);
}

static unsigned int get_target_mpc_factor(struct dml2_context *ctx,
		struct dc_state *state,
		const struct dml_display_cfg_st *disp_cfg,
		struct dml2_dml_to_dc_pipe_mapping *mapping,
		const struct dc_stream_status *status,
		const struct dc_stream_state *stream,
		int plane_idx)
{
	unsigned int plane_id;
	unsigned int cfg_idx;
	unsigned int mpc_factor;

	if (ctx->architecture == dml2_architecture_20) {
		get_plane_id(ctx, state, status->plane_states[plane_idx],
				stream->stream_id, plane_idx, &plane_id);
		cfg_idx = find_disp_cfg_idx_by_plane_id(mapping, plane_id);
		mpc_factor = (unsigned int)disp_cfg->hw.DPPPerSurface[cfg_idx];
	} else if (ctx->architecture == dml2_architecture_21) {
		if (ctx->config.svp_pstate.callbacks.get_stream_subvp_type(state, stream) == SUBVP_PHANTOM) {
			struct dc_stream_state *main_stream;
			struct dc_stream_status *main_stream_status;

			/* get stream id of main stream */
			main_stream = ctx->config.svp_pstate.callbacks.get_paired_subvp_stream(state, stream);
			if (!main_stream) {
				ASSERT(false);
				return 1;
			}

			main_stream_status = ctx->config.callbacks.get_stream_status(state, main_stream);
			if (!main_stream_status) {
				ASSERT(false);
				return 1;
			}

			/* get plane id for associated main plane */
			get_plane_id(ctx, state, main_stream_status->plane_states[plane_idx],
					main_stream->stream_id, plane_idx, &plane_id);
		} else {
			get_plane_id(ctx, state, status->plane_states[plane_idx],
					stream->stream_id, plane_idx, &plane_id);
		}

		cfg_idx = find_disp_cfg_idx_by_plane_id(mapping, plane_id);
		mpc_factor = ctx->v21.mode_programming.programming->plane_programming[cfg_idx].num_dpps_required;
	} else {
		mpc_factor = 1;
		ASSERT(false);
	}

	/* For stereo timings, we need to pipe split */
	if (dml2_is_stereo_timing(stream))
		mpc_factor = 2;

	return mpc_factor;
}

static unsigned int get_target_odm_factor(
		const struct dml2_context *ctx,
		struct dc_state *state,
		const struct dml_display_cfg_st *disp_cfg,
		struct dml2_dml_to_dc_pipe_mapping *mapping,
		const struct dc_stream_state *stream)
{
	unsigned int cfg_idx;

	if (ctx->architecture == dml2_architecture_20) {
		cfg_idx = find_disp_cfg_idx_by_stream_id(
				mapping, stream->stream_id);
		switch (disp_cfg->hw.ODMMode[cfg_idx]) {
		case dml_odm_mode_bypass:
			return 1;
		case dml_odm_mode_combine_2to1:
			return 2;
		case dml_odm_mode_combine_4to1:
			return 4;
		default:
			break;
		}
	} else if (ctx->architecture == dml2_architecture_21) {
		if (ctx->config.svp_pstate.callbacks.get_stream_subvp_type(state, stream) == SUBVP_PHANTOM) {
			struct dc_stream_state *main_stream;

			/* get stream id of main stream */
			main_stream = ctx->config.svp_pstate.callbacks.get_paired_subvp_stream(state, stream);
			if (!main_stream)
				goto failed;

			/* get cfg idx for associated main stream */
			cfg_idx = find_disp_cfg_idx_by_stream_id(
					mapping, main_stream->stream_id);
		} else {
			cfg_idx = find_disp_cfg_idx_by_stream_id(
					mapping, stream->stream_id);
		}

		return ctx->v21.mode_programming.programming->stream_programming[cfg_idx].num_odms_required;
	}

failed:
	ASSERT(false);
	return 1;
}

static unsigned int get_source_odm_factor(const struct dml2_context *ctx,
		struct dc_state *state,
		const struct dc_stream_state *stream)
{
	struct pipe_ctx *otg_master = ctx->config.callbacks.get_otg_master_for_stream(&state->res_ctx, stream);

	if (!otg_master)
		return 0;

	return ctx->config.callbacks.get_odm_slice_count(otg_master);
}

static unsigned int get_source_mpc_factor(const struct dml2_context *ctx,
		struct dc_state *state,
		const struct dc_plane_state *plane)
{
	struct pipe_ctx *dpp_pipes[MAX_PIPES] = {0};
	int dpp_pipe_count = ctx->config.callbacks.get_dpp_pipes_for_plane(plane,
			&state->res_ctx, dpp_pipes);

	ASSERT(dpp_pipe_count > 0);
	return ctx->config.callbacks.get_mpc_slice_count(dpp_pipes[0]);
}


static void populate_mpc_factors_for_stream(
		struct dml2_context *ctx,
		const struct dml_display_cfg_st *disp_cfg,
		struct dml2_dml_to_dc_pipe_mapping *mapping,
		struct dc_state *state,
		unsigned int stream_idx,
		struct dml2_pipe_combine_factor odm_factor,
		struct dml2_pipe_combine_factor mpc_factors[MAX_PIPES])
{
	const struct dc_stream_status *status = &state->stream_status[stream_idx];
	int i;

	for (i = 0; i < status->plane_count; i++) {
		mpc_factors[i].source = get_source_mpc_factor(ctx, state, status->plane_states[i]);
		mpc_factors[i].target = (odm_factor.target == 1) ?
				get_target_mpc_factor(ctx, state, disp_cfg, mapping, status, state->streams[stream_idx], i) : 1;
	}
}

static void populate_odm_factors(const struct dml2_context *ctx,
		const struct dml_display_cfg_st *disp_cfg,
		struct dml2_dml_to_dc_pipe_mapping *mapping,
		struct dc_state *state,
		struct dml2_pipe_combine_factor odm_factors[MAX_PIPES])
{
	int i;

	for (i = 0; i < state->stream_count; i++) {
		odm_factors[i].source = get_source_odm_factor(ctx, state, state->streams[i]);
		odm_factors[i].target = get_target_odm_factor(
				ctx, state, disp_cfg, mapping, state->streams[i]);
	}
}

static bool unmap_dc_pipes_for_stream(struct dml2_context *ctx,
		struct dc_state *state,
		const struct dc_state *existing_state,
		const struct dc_stream_state *stream,
		const struct dc_stream_status *status,
		struct dml2_pipe_combine_factor odm_factor,
		struct dml2_pipe_combine_factor mpc_factors[MAX_PIPES])
{
	int plane_idx;
	bool result = true;

	for (plane_idx = 0; plane_idx < status->plane_count; plane_idx++)
		if (mpc_factors[plane_idx].target < mpc_factors[plane_idx].source)
			result &= ctx->config.callbacks.update_pipes_for_plane_with_slice_count(
					state,
					existing_state,
					ctx->config.callbacks.dc->res_pool,
					status->plane_states[plane_idx],
					mpc_factors[plane_idx].target);
	if (odm_factor.target < odm_factor.source)
		result &= ctx->config.callbacks.update_pipes_for_stream_with_slice_count(
				state,
				existing_state,
				ctx->config.callbacks.dc->res_pool,
				stream,
				odm_factor.target);
	return result;
}

static bool map_dc_pipes_for_stream(struct dml2_context *ctx,
		struct dc_state *state,
		const struct dc_state *existing_state,
		const struct dc_stream_state *stream,
		const struct dc_stream_status *status,
		struct dml2_pipe_combine_factor odm_factor,
		struct dml2_pipe_combine_factor mpc_factors[MAX_PIPES])
{
	int plane_idx;
	bool result = true;

	for (plane_idx = 0; plane_idx < status->plane_count; plane_idx++)
		if (mpc_factors[plane_idx].target > mpc_factors[plane_idx].source)
			result &= ctx->config.callbacks.update_pipes_for_plane_with_slice_count(
					state,
					existing_state,
					ctx->config.callbacks.dc->res_pool,
					status->plane_states[plane_idx],
					mpc_factors[plane_idx].target);
	if (odm_factor.target > odm_factor.source)
		result &= ctx->config.callbacks.update_pipes_for_stream_with_slice_count(
				state,
				existing_state,
				ctx->config.callbacks.dc->res_pool,
				stream,
				odm_factor.target);
	return result;
}

static bool map_dc_pipes_with_callbacks(struct dml2_context *ctx,
		struct dc_state *state,
		const struct dml_display_cfg_st *disp_cfg,
		struct dml2_dml_to_dc_pipe_mapping *mapping,
		const struct dc_state *existing_state)
{
	int i;
	bool result = true;

	populate_odm_factors(ctx, disp_cfg, mapping, state, ctx->pipe_combine_scratch.odm_factors);
	for (i = 0; i < state->stream_count; i++)
		populate_mpc_factors_for_stream(ctx, disp_cfg, mapping, state,
				i, ctx->pipe_combine_scratch.odm_factors[i], ctx->pipe_combine_scratch.mpc_factors[i]);
	for (i = 0; i < state->stream_count; i++)
		result &= unmap_dc_pipes_for_stream(ctx, state, existing_state, state->streams[i],
				&state->stream_status[i], ctx->pipe_combine_scratch.odm_factors[i], ctx->pipe_combine_scratch.mpc_factors[i]);
	for (i = 0; i < state->stream_count; i++)
		result &= map_dc_pipes_for_stream(ctx, state, existing_state, state->streams[i],
				&state->stream_status[i], ctx->pipe_combine_scratch.odm_factors[i], ctx->pipe_combine_scratch.mpc_factors[i]);

	return result;
}

bool dml2_map_dc_pipes(struct dml2_context *ctx, struct dc_state *state, const struct dml_display_cfg_st *disp_cfg, struct dml2_dml_to_dc_pipe_mapping *mapping, const struct dc_state *existing_state)
{
	int stream_index, plane_index, i;

	unsigned int stream_disp_cfg_index;
	unsigned int plane_disp_cfg_index;
	unsigned int disp_cfg_index_max;

	unsigned int plane_id;
	unsigned int stream_id;

	const unsigned int *ODMMode, *DPPPerSurface;
	unsigned int odm_mode_array[__DML2_WRAPPER_MAX_STREAMS_PLANES__] = {0}, dpp_per_surface_array[__DML2_WRAPPER_MAX_STREAMS_PLANES__] = {0};
	struct dc_pipe_mapping_scratch scratch;

	if (ctx->config.map_dc_pipes_with_callbacks)
		return map_dc_pipes_with_callbacks(
				ctx, state, disp_cfg, mapping, existing_state);

	if (ctx->architecture == dml2_architecture_21) {
		/*
		 * Extract ODM and DPP outputs from DML2.1 and map them in an array as required for pipe mapping in dml2_map_dc_pipes.
		 * As data cannot be directly extracted in const pointers, assign these arrays to const pointers before proceeding to
		 * maximize the reuse of existing code. Const pointers are required because dml2.0 dml_display_cfg_st is const.
		 *
		 */
		for (i = 0; i < __DML2_WRAPPER_MAX_STREAMS_PLANES__; i++) {
			odm_mode_array[i] = ctx->v21.mode_programming.programming->stream_programming[i].num_odms_required;
			dpp_per_surface_array[i] = ctx->v21.mode_programming.programming->plane_programming[i].num_dpps_required;
		}

		ODMMode = (const unsigned int *)odm_mode_array;
		DPPPerSurface = (const unsigned int *)dpp_per_surface_array;
		disp_cfg_index_max = __DML2_WRAPPER_MAX_STREAMS_PLANES__;
	} else {
		ODMMode = (unsigned int *)disp_cfg->hw.ODMMode;
		DPPPerSurface = disp_cfg->hw.DPPPerSurface;
		disp_cfg_index_max = __DML_NUM_PLANES__;
	}

	for (stream_index = 0; stream_index < state->stream_count; stream_index++) {
		memset(&scratch, 0, sizeof(struct dc_pipe_mapping_scratch));

		stream_id = state->streams[stream_index]->stream_id;
		stream_disp_cfg_index = find_disp_cfg_idx_by_stream_id(mapping, stream_id);
		if (stream_disp_cfg_index >= disp_cfg_index_max)
			continue;

		if (ODMMode[stream_disp_cfg_index] == dml_odm_mode_bypass) {
			scratch.odm_info.odm_factor = 1;
		} else if (ODMMode[stream_disp_cfg_index] == dml_odm_mode_combine_2to1) {
			scratch.odm_info.odm_factor = 2;
		} else if (ODMMode[stream_disp_cfg_index] == dml_odm_mode_combine_4to1) {
			scratch.odm_info.odm_factor = 4;
		} else {
			ASSERT(false);
			scratch.odm_info.odm_factor = 1;
		}

		/* After DML2.1 update, ODM interpretation needs to change and is no longer same as for DML2.0.
		 * This is not an issue with new resource management logic. This block ensure backcompat
		 * with legacy pipe management with updated DML.
		 * */
		if (ctx->architecture == dml2_architecture_21) {
			if (ODMMode[stream_disp_cfg_index] == 1) {
				scratch.odm_info.odm_factor = 1;
			} else if (ODMMode[stream_disp_cfg_index] == 2) {
				scratch.odm_info.odm_factor = 2;
			} else if (ODMMode[stream_disp_cfg_index] == 4) {
				scratch.odm_info.odm_factor = 4;
			} else {
				ASSERT(false);
				scratch.odm_info.odm_factor = 1;
			}
		}
		calculate_odm_slices(state->streams[stream_index], scratch.odm_info.odm_factor, scratch.odm_info.odm_slice_end_x);

		// If there are no planes, you still want to setup ODM...
		if (state->stream_status[stream_index].plane_count == 0) {
			map_pipes_for_stream(ctx, state, state->streams[stream_index], &scratch, existing_state);
		}

		for (plane_index = 0; plane_index < state->stream_status[stream_index].plane_count; plane_index++) {
			// Planes are ordered top to bottom.
			if (get_plane_id(ctx, state, state->stream_status[stream_index].plane_states[plane_index],
				stream_id, plane_index, &plane_id)) {
				plane_disp_cfg_index = find_disp_cfg_idx_by_plane_id(mapping, plane_id);

				// Setup mpc_info for this plane
				scratch.mpc_info.prev_odm_pipe = NULL;
				if (scratch.odm_info.odm_factor == 1 && plane_disp_cfg_index < disp_cfg_index_max) {
					// If ODM combine is not inuse, then the number of pipes
					// per plane is determined by MPC combine factor
					scratch.mpc_info.mpc_factor = DPPPerSurface[plane_disp_cfg_index];

					//For stereo timings, we need to pipe split
					if (dml2_is_stereo_timing(state->streams[stream_index]))
						scratch.mpc_info.mpc_factor = 2;
				} else {
					// If ODM combine is enabled, then we use at most 1 pipe per
					// odm slice per plane, i.e. MPC combine is never used
					scratch.mpc_info.mpc_factor = 1;
				}

				ASSERT(scratch.odm_info.odm_factor * scratch.mpc_info.mpc_factor > 0);

				// Clear the pool assignment scratch (which is per plane)
				memset(&scratch.pipe_pool, 0, sizeof(struct dc_plane_pipe_pool));

				map_pipes_for_plane(ctx, state, state->streams[stream_index],
					state->stream_status[stream_index].plane_states[plane_index], plane_index, &scratch, existing_state);
			} else {
				// Plane ID cannot be generated, therefore no DML mapping can be performed.
				ASSERT(false);
			}
		}

	}

	if (!validate_pipe_assignment(ctx, state, disp_cfg, mapping))
		ASSERT(false);

	for (i = 0; i < ctx->config.dcn_pipe_count; i++) {
		struct pipe_ctx *pipe = &state->res_ctx.pipe_ctx[i];

		if (pipe->plane_state) {
			if (!ctx->config.callbacks.build_scaling_params(pipe)) {
				ASSERT(false);
			}
		}

		if (ctx->config.callbacks.build_test_pattern_params &&
				pipe->stream &&
				pipe->prev_odm_pipe == NULL &&
				pipe->top_pipe == NULL)
			ctx->config.callbacks.build_test_pattern_params(&state->res_ctx, pipe);
	}

	return true;
}
