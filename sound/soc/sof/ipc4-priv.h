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
 * @m_ida: Module instance identifier
 * @bss_size: Module object size
 * @private: Module private data
 */
struct sof_ipc4_fw_module {
	struct sof_man4_module man4_module_entry;
	struct ida m_ida;
	u32 bss_size;
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
 * @max_num_pipelines: max number of pipelines
 * @max_libs_count: Maximum number of libraries support by the FW including the
 *		    base firmware
 *
 * @load_library: Callback function for platform dependent library loading
 */
struct sof_ipc4_fw_data {
	u32 manifest_fw_hdr_offset;
	struct xarray fw_lib_xa;
	void *nhlt;
	enum sof_ipc4_mtrace_type mtrace_type;
	u32 mtrace_log_bytes;
	int max_num_pipelines;
	u32 max_libs_count;

	int (*load_library)(struct snd_sof_dev *sdev,
			    struct sof_ipc4_fw_library *fw_lib, bool reload);
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
#endif
