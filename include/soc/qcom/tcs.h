/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2016-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __SOC_QCOM_TCS_H__
#define __SOC_QCOM_TCS_H__

#define MAX_RPMH_PAYLOAD	16

/**
 * rpmh_state: state for the request
 *
 * RPMH_SLEEP_STATE:       State of the resource when the processor subsystem
 *                         is powered down. There is no client using the
 *                         resource actively.
 * RPMH_WAKE_ONLY_STATE:   Resume resource state to the value previously
 *                         requested before the processor was powered down.
 * RPMH_ACTIVE_ONLY_STATE: Active or AMC mode requests. Resource state
 *                         is aggregated immediately.
 */
enum rpmh_state {
	RPMH_SLEEP_STATE,
	RPMH_WAKE_ONLY_STATE,
	RPMH_ACTIVE_ONLY_STATE,
};

/**
 * struct tcs_cmd: an individual request to RPMH.
 *
 * @addr: the address of the resource slv_id:18:16 | offset:0:15
 * @data: the resource state request
 * @wait: wait for this request to be complete before sending the next
 */
struct tcs_cmd {
	u32 addr;
	u32 data;
	u32 wait;
};

/**
 * struct tcs_request: A set of tcs_cmds sent together in a TCS
 *
 * @state:          state for the request.
 * @wait_for_compl: wait until we get a response from the h/w accelerator
 * @num_cmds:       the number of @cmds in this request
 * @cmds:           an array of tcs_cmds
 */
struct tcs_request {
	enum rpmh_state state;
	u32 wait_for_compl;
	u32 num_cmds;
	struct tcs_cmd *cmds;
};

#define BCM_TCS_CMD_COMMIT_SHFT		30
#define BCM_TCS_CMD_COMMIT_MASK		0x40000000
#define BCM_TCS_CMD_VALID_SHFT		29
#define BCM_TCS_CMD_VALID_MASK		0x20000000
#define BCM_TCS_CMD_VOTE_X_SHFT		14
#define BCM_TCS_CMD_VOTE_MASK		0x3fff
#define BCM_TCS_CMD_VOTE_Y_SHFT		0
#define BCM_TCS_CMD_VOTE_Y_MASK		0xfffc000

/* Construct a Bus Clock Manager (BCM) specific TCS command */
#define BCM_TCS_CMD(commit, valid, vote_x, vote_y)		\
	(((commit) << BCM_TCS_CMD_COMMIT_SHFT) |		\
	((valid) << BCM_TCS_CMD_VALID_SHFT) |			\
	((cpu_to_le32(vote_x) &					\
	BCM_TCS_CMD_VOTE_MASK) << BCM_TCS_CMD_VOTE_X_SHFT) |	\
	((cpu_to_le32(vote_y) &					\
	BCM_TCS_CMD_VOTE_MASK) << BCM_TCS_CMD_VOTE_Y_SHFT))

#endif /* __SOC_QCOM_TCS_H__ */
