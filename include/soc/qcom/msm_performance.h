/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __MSM_PERFORMANCE_H
#define __MSM_PERFORMANCE_H

enum gfx_evt_t {
	MSM_PERF_INVAL,
	MSM_PERF_QUEUE,
	MSM_PERF_SUBMIT,
	MSM_PERF_RETIRED
};

enum evt_update_t {
	MSM_PERF_GFX,
};

#if IS_ENABLED(CONFIG_MSM_PERFORMANCE)
void msm_perf_events_update(enum evt_update_t update_typ,
			enum gfx_evt_t evt_typ, pid_t pid,
			uint32_t ctx_id, uint32_t timestamp, bool end_of_frame);
#else
static inline void msm_perf_events_update(enum evt_update_t update_typ,
			enum gfx_evt_t evt_typ, pid_t pid,
			uint32_t ctx_id, uint32_t timestamp, bool end_of_frame)
{
}
#endif

#if IS_ENABLED(CONFIG_QTI_SCMI_VENDOR_PROTOCOL)
#define SCMI_SPLH_ALGO_STR (0x53504C48414C474F)
#define SCMI_LPLH_ALGO_STR (0x4C504C48414C474F)
enum scmi_vendor_plh_param_ids {
	PLH_INIT_IPC_FREQ_TBL = 1,
	PLH_SET_SAMPLE_PERIOD_MS,
	PLH_SET_LOG_LEVEL,
	PLH_ACTIVITY_START,
	PLH_ACTIVITY_STOP,
};

#define splh_start_activity(splh_notif) ({\
		uint32_t temp, *msg = &temp;\
		*msg = cpu_to_le32(splh_notif); \
		plh_ops->start_activity(plh_handle, msg, SCMI_SPLH_ALGO_STR,\
				PLH_ACTIVITY_START, sizeof(*msg));\
})

#define splh_stop_activity ({\
		uint32_t temp, *msg = &temp;\
		*msg = cpu_to_le32(0); \
		plh_ops->stop_activity(plh_handle, msg, SCMI_SPLH_ALGO_STR,\
				PLH_ACTIVITY_STOP, sizeof(*msg));\
})

#define lplh_start_activity(lplh_notif) ({\
		uint32_t temp, *msg = &temp;\
		*msg = cpu_to_le32(lplh_notif); \
		plh_ops->start_activity(plh_handle, msg, SCMI_LPLH_ALGO_STR,\
				PLH_ACTIVITY_START, sizeof(*msg));\
})

#define lplh_stop_activity ({\
		uint32_t temp, *msg = &temp;\
		*msg = cpu_to_le32(0); \
		plh_ops->stop_activity(plh_handle, msg, SCMI_LPLH_ALGO_STR,\
				PLH_ACTIVITY_STOP, sizeof(*msg));\
})

#define splh_set_sample_ms(sample_ms) ({\
		uint32_t temp, *msg = &temp;\
		*msg = cpu_to_le32(sample_ms);\
		plh_ops->set_param(plh_handle, msg, SCMI_SPLH_ALGO_STR,\
				PLH_SET_SAMPLE_PERIOD_MS, sizeof(*msg));\
})

#define lplh_set_sample_ms(sample_ms) ({\
		uint32_t temp, *msg = &temp;\
		*msg = cpu_to_le32(sample_ms);\
		plh_ops->set_param(plh_handle, msg, SCMI_LPLH_ALGO_STR,\
				PLH_SET_SAMPLE_PERIOD_MS, sizeof(*msg));\
})

#define splh_set_log_level(log_level) ({\
		uint32_t temp, *msg = &temp;\
		*msg = cpu_to_le32(log_level);\
		plh_ops->set_param(plh_handle, msg, SCMI_SPLH_ALGO_STR,\
			PLH_SET_LOG_LEVEL, sizeof(*msg));\
})

#define lplh_set_log_level(log_level) ({\
		uint32_t temp, *msg = &temp;\
		*msg = cpu_to_le32(log_level);\
		ret = plh_ops->set_param(plh_handle, msg, SCMI_LPLH_ALGO_STR,\
			PLH_SET_LOG_LEVEL, sizeof(*msg));\
})

#define splh_init_ipc_freq_tbl(tmp, tmp_valid_len) ({\
		int idx = 0;\
		u16 tmp1[PLH_INIT_IPC_FREQ_TBL_PARAMS];\
		uint32_t *msg = (uint32_t *)tmp1, *tmsg = msg, msg_size, msg_val, \
		align_init_len = tmp_valid_len;\
		if (tmp_valid_len % 2) \
			align_init_len += 1; \
		msg_size = align_init_len * sizeof(*tmp); \
		for (i = 0; i < tmp_valid_len/2 ; i++) { \
			msg_val = tmp[idx++]; \
			msg_val |= ((tmp[idx++]) << 16); \
			*msg++ = cpu_to_le32(msg_val); \
		} \
		if (tmp_valid_len % 2) \
			*msg = cpu_to_le32(tmp[idx]); \
		plh_ops->set_param(plh_handle, tmsg, SCMI_SPLH_ALGO_STR,\
				PLH_INIT_IPC_FREQ_TBL, msg_size);\
})

#define lplh_init_ipc_freq_tbl(tmp, total_tokens) ({\
		int idx = 0; \
		u16 tmp1[LPLH_INIT_IPC_FREQ_TBL_PARAMS]; \
		uint32_t *msg = (uint32_t *)tmp1, *tmsg = msg, msg_size, msg_val, \
		align_init_len = total_tokens; \
		if (total_tokens % 2) \
			align_init_len += 1; \
		msg_size = align_init_len * sizeof(*tmp); \
		for (i = 0; i < total_tokens/2 ; i++) { \
			msg_val = tmp[idx++]; \
			msg_val |= ((tmp[idx++]) << 16); \
			*msg++ = cpu_to_le32(msg_val); \
		} \
		if (total_tokens % 2) \
			*msg = cpu_to_le32(tmp[idx]); \
		plh_ops->set_param(plh_handle, tmsg, SCMI_LPLH_ALGO_STR, \
				PLH_INIT_IPC_FREQ_TBL, msg_size); \
})

#else

#define splh_start_activity(splh_notif) ({\
	plh_ops->start_plh(plh_handle,\
				splh_notif, PERF_LOCK_SCROLL);\
})

#define splh_stop_activity ({\
	plh_ops->stop_plh(plh_handle, PERF_LOCK_SCROLL);\
})

#define lplh_start_activity(lplh_notif) ({\
	plh_ops->start_plh(plh_handle,\
				lplh_notif, PERF_LOCK_LAUNCH);\
})

#define lplh_stop_activity ({\
	plh_ops->stop_plh(plh_handle, PERF_LOCK_LAUNCH);\
})

#define splh_set_sample_ms(sample_ms) ({\
	plh_ops->set_plh_sample_ms(plh_handle, sample_ms, PERF_LOCK_SCROLL);\
})

#define lplh_set_sample_ms(sample_ms) ({\
	plh_ops->set_plh_sample_ms(plh_handle, sample_ms, PERF_LOCK_LAUNCH);\
})

#define splh_set_log_level(log_level) ({\
		plh_ops->set_plh_log_level(plh_handle, log_level, PERF_LOCK_SCROLL);\
})

#define lplh_set_log_level(log_level) ({\
		plh_ops->set_plh_log_level(plh_handle, log_level, PERF_LOCK_LAUNCH);\
})

#define splh_init_ipc_freq_tbl(tmp, tmp_valid_len) ({\
	plh_ops->init_plh_ipc_freq_tbl(plh_handle, tmp, tmp_valid_len, PERF_LOCK_SCROLL);\
})

#define lplh_init_ipc_freq_tbl(tmp, total_tokens) ({\
	plh_ops->init_plh_ipc_freq_tbl(plh_handle, tmp, total_tokens, PERF_LOCK_LAUNCH); \
})
#endif
#endif
