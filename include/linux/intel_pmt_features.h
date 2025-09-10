/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _FEATURES_H
#define _FEATURES_H

#include <linux/bits.h>
#include <linux/types.h>

/* Common masks */
#define PMT_CAP_TELEM			BIT(0)
#define PMT_CAP_WATCHER			BIT(1)
#define PMT_CAP_CRASHLOG		BIT(2)
#define PMT_CAP_STREAMING		BIT(3)
#define PMT_CAP_THRESHOLD		BIT(4)
#define PMT_CAP_WINDOW			BIT(5)
#define PMT_CAP_CONFIG			BIT(6)
#define PMT_CAP_TRACING			BIT(7)
#define PMT_CAP_INBAND			BIT(8)
#define PMT_CAP_OOB			BIT(9)
#define PMT_CAP_SECURED_CHAN		BIT(10)

#define PMT_CAP_PMT_SP			BIT(11)
#define PMT_CAP_PMT_SP_POLICY		GENMASK(17, 12)

/* Per Core Performance Telemetry (PCPT) specific masks */
#define PMT_CAP_PCPT_CORE_PERF		BIT(18)
#define PMT_CAP_PCPT_CORE_C0_RES	BIT(19)
#define PMT_CAP_PCPT_CORE_ACTIVITY	BIT(20)
#define PMT_CAP_PCPT_CACHE_PERF		BIT(21)
#define PMT_CAP_PCPT_QUALITY_TELEM	BIT(22)

/* Per Core Environmental Telemetry (PCET) specific masks */
#define PMT_CAP_PCET_WORKPOINT_HIST	BIT(18)
#define PMT_CAP_PCET_CORE_CURR_TEMP	BIT(19)
#define PMT_CAP_PCET_CORE_INST_RES	BIT(20)
#define PMT_CAP_PCET_QUALITY_TELEM	BIT(21)	/* Same as PMT_CAP_PCPT */
#define PMT_CAP_PCET_CORE_CDYN_LVL	BIT(22)
#define PMT_CAP_PCET_CORE_STRESS_LVL	BIT(23)
#define PMT_CAP_PCET_CORE_DAS		BIT(24)
#define PMT_CAP_PCET_FIVR_HEALTH	BIT(25)
#define PMT_CAP_PCET_ENERGY		BIT(26)
#define PMT_CAP_PCET_PEM_STATUS		BIT(27)
#define PMT_CAP_PCET_CORE_C_STATE	BIT(28)

/* Per RMID Performance Telemetry specific masks */
#define PMT_CAP_RMID_CORES_PERF		BIT(18)
#define PMT_CAP_RMID_CACHE_PERF		BIT(19)
#define PMT_CAP_RMID_PERF_QUAL		BIT(20)

/* Accelerator Telemetry specific masks */
#define PMT_CAP_ACCEL_CPM_TELEM		BIT(18)
#define PMT_CAP_ACCEL_TIP_TELEM		BIT(19)

/* Uncore Telemetry specific masks */
#define PMT_CAP_UNCORE_IO_CA_TELEM	BIT(18)
#define PMT_CAP_UNCORE_RMID_TELEM	BIT(19)
#define PMT_CAP_UNCORE_D2D_ULA_TELEM	BIT(20)
#define PMT_CAP_UNCORE_PKGC_TELEM	BIT(21)

/* Crash Log specific masks */
#define PMT_CAP_CRASHLOG_MAN_TRIG	BIT(11)
#define PMT_CAP_CRASHLOG_CORE		BIT(12)
#define PMT_CAP_CRASHLOG_UNCORE		BIT(13)
#define PMT_CAP_CRASHLOG_TOR		BIT(14)
#define PMT_CAP_CRASHLOG_S3M		BIT(15)
#define PMT_CAP_CRASHLOG_PERSISTENCY	BIT(16)
#define PMT_CAP_CRASHLOG_CLIP_GPIO	BIT(17)
#define PMT_CAP_CRASHLOG_PRE_RESET	BIT(18)
#define PMT_CAP_CRASHLOG_POST_RESET	BIT(19)

/* PeTe Log specific masks */
#define PMT_CAP_PETE_MAN_TRIG		BIT(11)
#define PMT_CAP_PETE_ENCRYPTION		BIT(12)
#define PMT_CAP_PETE_PERSISTENCY	BIT(13)
#define PMT_CAP_PETE_REQ_TOKENS		BIT(14)
#define PMT_CAP_PETE_PROD_ENABLED	BIT(15)
#define PMT_CAP_PETE_DEBUG_ENABLED	BIT(16)

/* TPMI control specific masks */
#define PMT_CAP_TPMI_MAILBOX		BIT(11)
#define PMT_CAP_TPMI_LOCK		BIT(12)

/* Tracing specific masks */
#define PMT_CAP_TRACE_SRAR		BIT(11)
#define PMT_CAP_TRACE_CORRECTABLE	BIT(12)
#define PMT_CAP_TRACE_MCTP		BIT(13)
#define PMT_CAP_TRACE_MRT		BIT(14)

/* Per RMID Energy Telemetry specific masks */
#define PMT_CAP_RMID_ENERGY		BIT(18)
#define PMT_CAP_RMID_ACTIVITY		BIT(19)
#define PMT_CAP_RMID_ENERGY_QUAL	BIT(20)

enum pmt_feature_id {
	FEATURE_INVALID			= 0x0,
	FEATURE_PER_CORE_PERF_TELEM	= 0x1,
	FEATURE_PER_CORE_ENV_TELEM	= 0x2,
	FEATURE_PER_RMID_PERF_TELEM	= 0x3,
	FEATURE_ACCEL_TELEM		= 0x4,
	FEATURE_UNCORE_TELEM		= 0x5,
	FEATURE_CRASH_LOG		= 0x6,
	FEATURE_PETE_LOG		= 0x7,
	FEATURE_TPMI_CTRL		= 0x8,
	FEATURE_RESERVED		= 0x9,
	FEATURE_TRACING			= 0xA,
	FEATURE_PER_RMID_ENERGY_TELEM	= 0xB,
	FEATURE_MAX			= 0xB,
};

enum feature_layout {
	LAYOUT_RMID,
	LAYOUT_WATCHER,
	LAYOUT_COMMAND,
	LAYOUT_CAPS_ONLY,
};

struct pmt_cap {
	u32		mask;
	const char	*name;
};

extern const char * const pmt_feature_names[];
extern enum feature_layout feature_layout[];
extern struct pmt_cap pmt_cap_common[];
extern struct pmt_cap pmt_cap_pcpt[];
extern struct pmt_cap *pmt_caps_pcpt[];
extern struct pmt_cap pmt_cap_pcet[];
extern struct pmt_cap *pmt_caps_pcet[];
extern struct pmt_cap pmt_cap_rmid_perf[];
extern struct pmt_cap *pmt_caps_rmid_perf[];
extern struct pmt_cap pmt_cap_accel[];
extern struct pmt_cap *pmt_caps_accel[];
extern struct pmt_cap pmt_cap_uncore[];
extern struct pmt_cap *pmt_caps_uncore[];
extern struct pmt_cap pmt_cap_crashlog[];
extern struct pmt_cap *pmt_caps_crashlog[];
extern struct pmt_cap pmt_cap_pete[];
extern struct pmt_cap *pmt_caps_pete[];
extern struct pmt_cap pmt_cap_tpmi[];
extern struct pmt_cap *pmt_caps_tpmi[];
extern struct pmt_cap pmt_cap_s3m[];
extern struct pmt_cap *pmt_caps_s3m[];
extern struct pmt_cap pmt_cap_tracing[];
extern struct pmt_cap *pmt_caps_tracing[];
extern struct pmt_cap pmt_cap_rmid_energy[];
extern struct pmt_cap *pmt_caps_rmid_energy[];

static inline bool pmt_feature_id_is_valid(enum pmt_feature_id id)
{
	if (id > FEATURE_MAX)
		return false;

	if (id == FEATURE_INVALID || id == FEATURE_RESERVED)
		return false;

	return true;
}
#endif
