/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2022 Intel Corporation. All rights reserved.
 */

#ifndef __SOUND_SOC_SOF_IPC4_PRIV_H
#define __SOUND_SOC_SOF_IPC4_PRIV_H

#include <linux/idr.h>
#include <sound/sof/ext_manifest4.h>
#include "sof-priv.h"

/* The DSP window indices are fixed */
#define SOF_IPC4_INBOX_WINDOW_IDX	0
#define SOF_IPC4_OUTBOX_WINDOW_IDX	1
#define SOF_IPC4_DEBUG_WINDOW_IDX	2

enum sof_ipc4_mtrace_type {
	SOF_IPC4_MTRACE_NOT_AVAILABLE = 0,
	SOF_IPC4_MTRACE_INTEL_CAVS_1_5,
	SOF_IPC4_MTRACE_INTEL_CAVS_1_8,
	SOF_IPC4_MTRACE_INTEL_CAVS_2,
};

/**
 * struct sof_ipc4_fw_module - IPC4 module info
 * @sof_man4_module: Module info
 * @fw_mod_cfg: Pointer to the module config start of the module
 * @m_ida: Module instance identifier
 * @private: Module private data
 */
struct sof_ipc4_fw_module {
	struct sof_man4_module man4_module_entry;
	const struct sof_man4_module_config *fw_mod_cfg;
	struct ida m_ida;
	void *private;
};

/**
 * struct sof_ipc4_fw_library - IPC4 library information
 * @sof_fw: SOF Firmware of the library
 * @id: Library ID. 0 is reserved for basefw, external libraries must have unique
 *	ID number between 1 and (sof_ipc4_fw_data.max_libs_count - 1)
 *	Note: sof_ipc4_fw_data.max_libs_count == 1 implies that external libraries
 *	are not supported
 * @num_modules : Number of FW modules in the library
 * @modules: Array of FW modules
 */
struct sof_ipc4_fw_library {
	struct sof_firmware sof_fw;
	const char *name;
	u32 id;
	int num_modules;
	struct sof_ipc4_fw_module *modules;
};

/**
 * struct sof_ipc4_fw_data - IPC4-specific data
 * @manifest_fw_hdr_offset: FW header offset in the manifest
 * @fw_lib_xa: XArray for firmware libraries, including basefw (ID = 0)
 *	       Used to store the FW libraries and to manage the unique IDs of the
 *	       libraries.
 * @nhlt: NHLT table either from the BIOS or the topology manifest
 * @mtrace_type: mtrace type supported on the booted platform
 * @mtrace_log_bytes: log bytes as reported by the firmware via fw_config reply
 * @num_playback_streams: max number of playback DMAs, needed for CHAIN_DMA offset
 * @num_capture_streams: max number of capture DMAs
 * @max_num_pipelines: max number of pipelines
 * @max_libs_count: Maximum number of libraries support by the FW including the
 *		    base firmware
 *
 * @load_library: Callback function for platform dependent library loading
 * @pipeline_state_mutex: Mutex to protect pipeline triggers, ref counts, states and deletion
 */
struct sof_ipc4_fw_data {
	u32 manifest_fw_hdr_offset;
	struct xarray fw_lib_xa;
	void *nhlt;
	enum sof_ipc4_mtrace_type mtrace_type;
	u32 mtrace_log_bytes;
	int num_playback_streams;
	int num_capture_streams;
	int max_num_pipelines;
	u32 max_libs_count;
	bool fw_context_save;

	int (*load_library)(struct snd_sof_dev *sdev,
			    struct sof_ipc4_fw_library *fw_lib, bool reload);
	struct mutex pipeline_state_mutex; /* protect pipeline triggers, ref counts and states */
};

/**
 * struct sof_ipc4_timestamp_info - IPC4 timestamp info
 * @host_copier: the host copier of the pcm stream
 * @dai_copier: the dai copier of the pcm stream
 * @stream_start_offset: reported by fw in memory window
 * @llp_offset: llp offset in memory window
 */
struct sof_ipc4_timestamp_info {
	struct sof_ipc4_copier *host_copier;
	struct sof_ipc4_copier *dai_copier;
	u64 stream_start_offset;
	u32 llp_offset;
};

extern const struct sof_ipc_fw_loader_ops ipc4_loader_ops;
extern const struct sof_ipc_tplg_ops ipc4_tplg_ops;
extern const struct sof_ipc_tplg_control_ops tplg_ipc4_control_ops;
extern const struct sof_ipc_pcm_ops ipc4_pcm_ops;
extern const struct sof_ipc_fw_tracing_ops ipc4_mtrace_ops;

int sof_ipc4_set_pipeline_state(struct snd_sof_dev *sdev, u32 id, u32 state);
int sof_ipc4_mtrace_update_pos(struct snd_sof_dev *sdev, int core);

int sof_ipc4_query_fw_configuration(struct snd_sof_dev *sdev);
int sof_ipc4_reload_fw_libraries(struct snd_sof_dev *sdev);
struct sof_ipc4_fw_module *sof_ipc4_find_module_by_uuid(struct snd_sof_dev *sdev,
							const guid_t *uuid);

struct snd_sof_widget *sof_ipc4_find_swidget_by_ids(struct snd_sof_dev *sdev,
						    u32 module_id, int instance_id);

struct sof_ipc4_base_module_cfg;
void sof_ipc4_update_cpc_from_manifest(struct snd_sof_dev *sdev,
				       struct sof_ipc4_fw_module *fw_module,
				       struct sof_ipc4_base_module_cfg *basecfg);

size_t sof_ipc4_find_debug_slot_offset_by_type(struct snd_sof_dev *sdev,
					       u32 slot_type);

#endif
