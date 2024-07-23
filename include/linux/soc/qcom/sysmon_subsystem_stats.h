/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */
/*
 * This header is for sysmon subsystem stats query API's in drivers.
 */
#ifndef __QCOM_SYSMON_SUBSYSTEM_STATS_H__
#define __QCOM_SYSMON_SUBSYSTEM_STATS_H__
/* Maximum number of clock levels in power stats */
#define SYSMON_POWER_STATS_MAX_CLK_LEVELS 32

/* Error Codes */
/* Error code when DSP PMU Counters are unavailable/overridden */
#define DSP_PMU_COUNTER_NA 0x1

/*
 * @struct sysmon_smem_power_stats
 * @brief Structure type to hold DSP power statistics
 * Version defines which fields are valid.
 * if Version
 * 1   : clk_arr, active_time, pc_time and lpi_time are valid.
 * >=2 : All fields are valid.
 */
struct sysmon_smem_power_stats {
	u32 version;
	/**< Version */

	u32 clk_arr[SYSMON_POWER_STATS_MAX_CLK_LEVELS];
	/**< Core clock frequency(KHz) array */

	u32 active_time[SYSMON_POWER_STATS_MAX_CLK_LEVELS];
	/**< Active time(seconds) array corresponding to core clock array */

	u32 pc_time;
	/**< DSP LPM(Low Power Mode) time(seconds) */

	u32 lpi_time;
	/**< DSP LPI(Low Power Island Mode) time(seconds) */

	u32 island_time[SYSMON_POWER_STATS_MAX_CLK_LEVELS];
	/**< DSP LPI(Low Power Island Mode) time(seconds)
	 * array corresponding to core clock array
	 */

	u32 current_clk;
	/**< DSP current clock in KHz*/
};
/*
 * @struct sysmon_smem_q6_event_stats
 * @brief Structure type to hold DSP Q6 event based statistics.
 */
struct sysmon_smem_q6_event_stats {
	u32 QDSP6_clk;
	/**< Q6 core clock in KHz */

	u32 Ab_vote_lsb;
	/**< Lower 32bits of the average bandwidth vote in bytes */

	u32 Ab_vote_msb;
	/**< Upper 32bits of the average bandwidth vote in bytes */

	u32 Ib_vote_lsb;
	/**< Lower 32bits of instantaneous bandwidth vote in bytes */

	u32 Ib_vote_msb;
	/**< Upper 32bits of instantaneous bandwidth vote in bytes */

	u32 Sleep_latency;
	/**< Sleep latency vote in micro-seconds */
};
/*
 * @struct sleep_stats
 * @brief CX collapse mode statistics
 */
struct sleep_stats {
	u32 stat_type;
	/**< Version */

	u32 count;
	/**< Number of times subsystem has voted for AOSS Sleep + CX Collapse */

	u64 last_entered_at;
	/**< LAST Timestamp the subsystem voted for AOSS Sleep + CX Collapse */

	u64 last_exited_at;
	/**< LAST Timestamp the subsystem voted for CX at MO */

	u64 accumulated;
	/**< Shows how long the subsystem has been in a sleep
	 *mode with CX collapse voted for since device booted up
	 */
};
/*
 * @struct sleep_stats
 * @brief Island mode statistics
 */
struct sleep_stats_island {
	u32 stat_type;
	/**< Version */

	u32 count;
	/**< Number of times subsystem has entered/exited Island mode */

	u64 last_entered_at;
	/**< LAST Timestamp the subsystem entered Island mode */

	u64 last_exited_at;
	/**< LAST Timestamp the subsystem exited Island mode */

	u64 accumulated;
	/**< Shows how long the subsystem has been in
	 *Island mode since device booted up
	 */
};
/*
 * @enum dsp_id_t
 * @brief Enum to hold SMEM HOST ID for DSP subsystems.
 */
enum dsp_id_t {
	ADSP = 2,
	SLPI,
	CDSP = 5
};
/**
 * API to query requested DSP subsystem's power residency.
 * On success, returns power residency statistics in the given
 * sysmon_smem_power_stats structure. DSP core clock frequencies are
 * stored in power stats array in ascending order and
 * zero value entries are invalid entries.
 * @arg dsp_id DSP subsystem id from dsp_id_t enum.
 * @arg sysmon_power_stats Pointer to sysmon_smem_power_stats structure.
 *@return The structure will be updated on SUCCESS (0).
 *        FAILURE (Non-zero) if Query could not be processed.
 */
int sysmon_stats_query_power_residency(enum dsp_id_t dsp_id,
		struct sysmon_smem_power_stats *sysmon_power_stats);
/**
 * API to query requested DSP subsystem's Q6 clock, bandwidth
 * and sleep latency votes. On success, updates
 * sysmon_smem_q6_event_stats structure with the results.
 * @arg1: dsp_id DSP subsystem id from dsp_id_t enum.
 * @arg2: sysmon_q6_event_stats pointer to sysmon_smem_q6_event_stats
 *        structure.
 *@return The structure will be updated on SUCCESS (0).
 *        FAILURE (Non-zero) if Query could not be processed.
 */
int sysmon_stats_query_q6_votes(enum dsp_id_t dsp_id,
		struct sysmon_smem_q6_event_stats *sysmon_q6_event_stats);
/**
 * API to query requested DSP subsystem's Q6 load.
 * On success, returns Q6 load statistic in the given
 * q6load_avg parameter.
 * @arg1: dsp_id DSP subsystem id from dsp_id_t enum.
 * @arg2: u32 pointer to Average Q6 load in KCPS
 * @return: SUCCESS (0) if Query is successful
 *        FAILURE (Non-zero) if Query could not be processed.
 */
int sysmon_stats_query_q6_load(enum dsp_id_t dsp_id,
							u32 *q6load_avg);
/**
 * API to query requested DSP subsystem sleep stats for LPM (sleep)
 * and LPI (Island). On success, returns sleep
 * statistics in the given sleep_stats structure for LPM and LPI.
 * @arg1: dsp_id DSP subsystem id from dsp_id_t enum.
 * @arg2: sleep_stats_lpm Pointer to sleep_stats structure.
 * @arg3: sleep_stats_lpi Pointer to sleep_stats_island structure.
 * @return: The structure will be updated on SUCCESS (0).
 *          FAILURE (Non-zero) if Query could not be processed.
 */
int sysmon_stats_query_sleep(enum dsp_id_t dsp_id,
					struct sleep_stats *sleep_stats_lpm,
					struct sleep_stats_island *sleep_stats_lpi);
/**
 * sysmon_stats_query_hvx_utlization() - * API to query
 * CDSP subsystem hvx utlization.On success, returns HVX utilization
 * in the hvx_util parameter.
 * @arg1: u32 pointer to HVX utilization in percentage.
 * @return: SUCCESS (0) if Query is successful
 *        FAILURE (Non-zero) if Query could not be processed, refer error codes.
 */
int sysmon_stats_query_hvx_utlization(u32 *hvx_util);

/**
 * sysmon_stats_query_hmx_utlization() - * API to query
 * CDSP hmx utlization.On success, returns HMX utilization
 * in the hmx_util parameter.
 * @arg1: u32 pointer to HMX utilization in percentage.
 * @return: SUCCESS (0) if Query is successful
 *        FAILURE (Non-zero) if Query could not be processed, refer error codes.
 */
int sysmon_stats_query_hmx_utlization(u32 *hmx_util);
#endif
