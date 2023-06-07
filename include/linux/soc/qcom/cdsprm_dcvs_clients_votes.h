/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */
/*
 * This header is for cdsprm dcvs clients votes query API's in drivers.
 */
#ifndef __QCOM_CDSPRM_DCVS_CLIENTS_VOTES_H__
#define __QCOM_CDSPRM_DCVS_CLIENTS_VOTES_H__

#define DCVS_CLIENTS_TABLE_SIZE					15
#define DCVS_CLIENTS_TABLE_LIBNAME_STR_SIZE		64

enum dcvs_clients_votes_fetch_status {
	DCVS_CLIENTS_VOTES_FETCH_SUCCESS = 0x1,
	DCVS_CLIENTS_VOTES_FAILED = 0x2,
	DCVS_CLIENTS_VOTES_TIMEOUT = 0x3,
	DCVS_CLIENTS_VOTES_RPMSG_SEND_FAILED = 0x4,
	DCVS_CLIENTS_VOTES_UNSUPPORTED_API = 0x5,
	DCVS_CLIETNS_VOTES_INVALID_PARAMS = 0x6,
};


struct dcvs_hmx_vote_params {

	bool power_up;
	/**< TRUE for HMX power on and FALSE for HMX power off*/

	bool set_clock;
	/**< TRUE if HMX clock request
	 * (pick_default/target_corner/freq_mhz/floor_freq_mhz)
	 * has to be considered
	 */

	bool pick_default;
	/**< TRUE if HMX vote should be considered as the default frequency
	 * corresponding to Q6 core request from the same client id
	 */

	unsigned char reserved;
	/**< Reserved parameter */

	unsigned int freq_mhz;
	/**< Client's request for frequency (in MHz)*/

	unsigned int floor_freq_mhz;
	/**< Client's request for floor frequency (in MHz) */

	unsigned int target_corner;
	/**< Client's request for the target voltage corner */

	unsigned int min_corner;
	/**< Client's request for the min voltage corner */

	unsigned int max_corner;
	/**< Client's request for the max voltage corner */

	unsigned char perf_mode;
	/**< Client's request for the perf mode */

	unsigned char reserved1;
	/**< Reserved parameter */

	unsigned char reserved2;
	/**< Reserved parameter */

	unsigned char reserved3;
	/**< Reserved parameter */

	unsigned int freq_mhz_from_q6_corner;
	/**< hmx clock freq which is calculated from Q6 if pick_default is set to TRUE (in MHz) */

	unsigned int reserved4;
	/**< Reserved parameter */
};

struct dcvs_ceng_vote_params {

	unsigned int target_corner;
	/**< Client's request for the target voltage corner */

	unsigned int min_corner;
	/**< Client's request for the min voltage corner */

	unsigned int max_corner;
	/**< Client's request for the max voltage corner */

	unsigned long long  bwBytePerSec;
	/**< Client's request for instantaneous bandwidth in bytes per second */

	unsigned int busbwUsagePercentage;
	/**< Client's request for average bandwidth in % of bwBytePerSec */

	unsigned char perf_mode;
	/**< Client's request for the perf mode */

	unsigned char reserved1;
	/**< Reserved parameter */

	unsigned char reserved2;
	/**< Reserved parameter */

	unsigned char reserved3;
	/**< Reserved parameter */

	unsigned int reserved4;
	/**< Reserved parameter */

} dcvs_ceng_vote_params_t;

struct sysmon_dcvs_client_info {
	unsigned char libname[DCVS_CLIENTS_TABLE_LIBNAME_STR_SIZE];
	/**< library name field */

	struct dcvs_hmx_vote_params hmx_params;
	/**< Intended HMX params */

	struct dcvs_ceng_vote_params ceng_params;
	/**< Intended Q6->CENG bus params */

	unsigned int pid;
	/**< Caller PID information */

	unsigned int client_id;
	 /*< Client id of the client referred to in this node */

	unsigned int tid;
	/**< Caller TID information */


	bool dcvs_enable;
	/**< TRUE for DCVS enable and FALSE for DCVS disable */

	bool set_latency;
	/**< TRUE if sleep latency request has to be considered */

	bool active_client;
	/**< TRUE if target_corner or mips_bw request is non-zero */

	unsigned char reserved1;
	/**< Reserved parameter */

	unsigned int latency;
	/**< Sleep latency request in micro seconds */

	unsigned int client_class;
	/**< Client requested client class */

	unsigned int dcvs_policy;
	/**< Requested DCVS policy in case DCVS enable is TRUE */


	bool set_core_params;
	/**< Flag to mark DCVS core clock params structure validity, TRUE for valid DCVS core clock
	 * params request and FALSE otherwise
	 */

	unsigned char core_target_corner;
	/**< Client's request for the target voltage corner */

	unsigned char core_min_corner;
	/**< Client's request for minimum voltage corner */

	unsigned char core_max_corner;
	/**< Client's request for maximum voltage corner */

	bool set_bus_params;
	/**< Flag to mark DCVS bus clock params structure validity, TRUE for valid DCVS bus clock
	 * params request and FALSE otherwise
	 */

	unsigned char bus_target_corner;
	/**< Client's request for the target voltage corner */

	unsigned char bus_min_corner;
	/**< Client's request for minimum voltage corner */

	unsigned char bus_max_corner;
	/**< Client's request for maximum voltage corner */

	unsigned int mips_mipsPerThread;
	/**< per thread mips requested */

	unsigned int mips_mipsTotal;
	/**< total mips requested */

	uint64_t mips_bwBytePerSec;
	/**< Instantaneous bandwidth in bytes per second */

	unsigned int mips_busbwUsagePercentage;
	/**< Average bandwidth in % of bwBytePerSec */

	unsigned int mips_latency;
	/**< Legacy sleep latency vote requested in micro-seconds */

	bool mips_set_mips;
	/**< flag for validity of mipsPerThread and mipsTotal request */

	bool mips_set_bus_bw;
	/**< flag for validity fo bwBytePerSec and busBwUsagePercentage */

	bool mips_set_latency;
	/**< Flag for validtity of latency */

	bool set_sleep_disable;
	/**< TRUE if LPM level request has to be considered */

	unsigned char sleep_disable;
	/**< LPM level request */

	unsigned char core_perf_mode;
	/**< Client's request for core clock perf mode */

	unsigned char bus_perf_mode;
	/**< Client's request for bus clock perf mode */

	unsigned char reserved2;
	/**< Reserved paramer */

	unsigned int reserved3;
	/**< Reserved parameter */

	unsigned int reserved4;
	/**< Reserved parameter */

	unsigned int reserved5;
	/**< Reserved parameter */

	unsigned int reserved6;
	/**< Reserved parameter */
};

struct sysmon_dcvs_client_agg_info {
	unsigned char core_target_corner;
	/**< Aggregated Target voltage corner for core clock across
	 * registered clients
	 */

	unsigned char core_min_corner;
	/**< Aggregated Minimum voltage corner for core clock across
	 * registered clients
	 */

	unsigned char core_max_corner;
	/**< Aggregated Maximum voltage corner for core clock across
	 * registered clients
	 */

	unsigned char bus_target_corner;
	/**< Aggregated Target voltage corner for bus clock across
	 * registered clients
	 */

	unsigned char bus_min_corner;
	/**< Aggregated Minimum voltage corner for bus clock across
	 * registered clients
	 */

	unsigned char bus_max_corner;
	/**< Aggregated Maximum voltage corner for bus clock across
	 * registered clients
	 */

	unsigned char dcvs_policy;
	/**< Aggregated DCVS policy to be used by DCVS logic */

	unsigned char dcvs_state;
	/**< Aggregated DCVS state request for ADSPPM  */

	unsigned int core_clk_vote_mhz;
	/**< Aggregated target corner vote in MHz for core clock */

	unsigned int bus_clk_vote_mhz;
	/**< Aggregated target corner vote in MHz for bus clock */

	unsigned int hmx_target_corner;
	/**< Aggregated hmx target corner across clients */

	unsigned int hmx_min_corner;
	/**< Aggregated hmx min corner across clients */

	unsigned int hmx_max_corner;
	/**< Aggregated hmx max corner across clients */

	unsigned int hmx_freq_mhz;
	/**< Aggregated hmx frequency across clients */

	unsigned int hmx_floor_freq_mhz;
	/**< Aggregated hmx floor frequency across clients */

	unsigned int hmx_freq_from_q6_corner_mhz;
	/**< Aggregated hmx frequency which is calculated from Q6 across clients*/

	unsigned int hmx_clock_mhz;
	/**< Aggregated target corner vote in MHz for HMX clock across clients */

	unsigned char  ceng_target_corner;
	/**< Aggregated target voltage corner for CENG bus
	 *   clock across registered clients
	 */

	unsigned char  ceng_min_corner;
	/**< Aggregated minimum voltage corner for CENG bus
	 *   clock across registered clients
	 */

	unsigned char  ceng_max_corner;
	/**< Aggregated maximum voltage corner for CENG bus
	 *   clock across registered clients
	 */

	unsigned char  ceng_perf_mode;
	/**< Aggregated perf mode for CENG bus clock target
	 *   voltage corner across registered clients
	 */

	uint64_t ceng_ib;
	/**< Aggregated IB vote for CENG bus
	 *   clock across registered clients
	 */

	unsigned int ceng_ab;
	/**< Aggregated AB vote for CENG bus
	 *   clock across registered clients
	 */

	unsigned int ceng_clock_mhz;
	/**< Aggregated target corner vote in MHz forQ6->CENG bus clock interface */

	unsigned int sleep_latency;
	/**< Aggregated sleep latency in micro-seconds across requesting clients */

	unsigned int sleep_latency_override;
	/**< User sleep latency override in micro-seconds from devcfg config */

	unsigned int final_sleep_latency_vote;
	/**< Aggregated sleep latency =
	 * maximum(sleep_latency, sleep_latency_override)
	 */

	unsigned int lpm_state;
	/**< Aggregated LPM level request */

	bool hmx_power;
	/**< Aggregated HMX power status */

	unsigned char core_perf_mode;
	/**< Aggregate perf mode across clients for core */

	unsigned char bus_perf_mode;
	/**< Aggregate perf mode across clients for bus */

	unsigned char hmx_perf_mode;
	/**< Aggregate perf mode across clients for hmx */

	unsigned int reserved1;
	/**< Reserved parameter */

	unsigned int reserved2;
	/**< Reserved parameter */
};

/**< Structure to hold aggregated and individual DCVS clients
 * information
 */
struct sysmon_dcvs_clients {
	struct sysmon_dcvs_client_agg_info aggInfo;
	/**< Aggregated votes across all active clients */

	unsigned int num_clients;
	/**< Number of active DCVS clients */

	unsigned char status;
	/**< Status flag indicates data received status
	 * 0 - Invalid Data
	 * DCVS_CLIENTS_VOTES_FETCH_SUCCESS - Valid Data
	 */

	unsigned char reserved1;
	/**< Reserved parameter */

	unsigned char reserved2;
	/**< Reserved parameter */

	unsigned char reserved3;
	/**< Reserved parameter */

	unsigned int reserved4;
	/**< Reserved parameter */

	unsigned int reserved5;
	/**< Reserved parameter */

	struct sysmon_dcvs_client_info table[DCVS_CLIENTS_TABLE_SIZE];
	/**< Structure to hold the active dcvs clients votes */
};

/**
 * cdsprm_sysmon_dcvs_clients_votes() - Emits out CDSP DCVS clients voting information
 *                                      into the kernel ring buffer and updates the
 *                                      provided structure with the same
 * @arg1: struct sysmon_dcvs_clients *dcvs_clients_info
 *        This parameter is optional when enable_log is TRUE.
 *        DCVS clients votes information copied to this memory if pointer is not NULL.
 * @arg2: bool enable_log
 *        This parameter is optional when dcvs_clients_info is provided.
 *        TRUE  - dcvs clients information printed in kernel log
 *        FALSE - dcvs clients information not printed in kernel log
 * @return: SUCCESS (0) if Query is successful
 *          FAILURE (Non-zero) if input param is invalid.
 *             DCVS_CLIENTS_VOTES_TIMEOUT - if data not received from CDSP
 *                                        within WAIT_FOR_DCVS_CLIENTDATA_TIMEOUT time period
 *             DCVS_CLIENTS_VOTES_FAILED -  if received invalid data
 *             DCVS_CLIENTS_VOTES_RPMSG_SEND_FAILED - if rpmsg_send request failed
 *             DCVS_CLIENTS_VOTES_UNSUPPORTED_API - if feature is not supported on CDSP Build
 *             DCVS_CLIETNS_VOTES_INVALID_PARAMS - if arguments validation failed
 */
int cdsprm_sysmon_dcvs_clients_votes(struct sysmon_dcvs_clients *dcvs_clients_info,
									 bool enable_log);

#endif /* __QCOM_CDSPRM_DCVS_CLIENTS_VOTES_H__ */
