// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2021-2022 Intel Corporation. All rights reserved.
//
// Authors: Cezary Rojewski <cezary.rojewski@intel.com>
//          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
//

#include "avs.h"
#include "messages.h"

__maybe_unused
static int avs_dsp_init_probe(struct avs_dev *adev, union avs_connector_node_id node_id,
			      size_t buffer_size)

{
	struct avs_probe_cfg cfg = {{0}};
	struct avs_module_entry mentry;
	u16 dummy;

	avs_get_module_entry(adev, &AVS_PROBE_MOD_UUID, &mentry);

	/*
	 * Probe module uses no cycles, audio data format and input and output
	 * frame sizes are unused. It is also not owned by any pipeline.
	 */
	cfg.base.ibs = 1;
	/* BSS module descriptor is always segment of index=2. */
	cfg.base.is_pages = mentry.segments[2].flags.length;
	cfg.gtw_cfg.node_id = node_id;
	cfg.gtw_cfg.dma_buffer_size = buffer_size;

	return avs_dsp_init_module(adev, mentry.module_id, INVALID_PIPELINE_ID, 0, 0, &cfg,
				   sizeof(cfg), &dummy);
}

__maybe_unused
static void avs_dsp_delete_probe(struct avs_dev *adev)
{
	struct avs_module_entry mentry;

	avs_get_module_entry(adev, &AVS_PROBE_MOD_UUID, &mentry);

	/* There is only ever one probe module instance. */
	avs_dsp_delete_module(adev, mentry.module_id, 0, INVALID_PIPELINE_ID, 0);
}
