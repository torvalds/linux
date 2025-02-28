/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2016-2019, The Linux Foundation. All rights reserved.
 */

#ifndef __SOC_QCOM_TCS_H__
#define __SOC_QCOM_TCS_H__

#include <linux/bitfield.h>
#include <linux/bits.h>

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
 * @wait: ensure that this command is complete before returning.
 *        Setting "wait" here only makes sense during rpmh_write_batch() for
 *        active-only transfers, this is because:
 *        rpmh_write() - Always waits.
 *                       (DEFINE_RPMH_MSG_ONSTACK will set .wait_for_compl)
 *        rpmh_write_async() - Never waits.
 *                       (There's no request completion callback)
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
 *                  (same as setting cmd->wait for all commands in the request)
 * @num_cmds:       the number of @cmds in this request
 * @cmds:           an array of tcs_cmds
 */
struct tcs_request {
	enum rpmh_state state;
	u32 wait_for_compl;
	u32 num_cmds;
	struct tcs_cmd *cmds;
};

#define BCM_TCS_CMD_COMMIT_MASK		BIT(30)
#define BCM_TCS_CMD_VALID_MASK		BIT(29)
#define BCM_TCS_CMD_VOTE_MASK		GENMASK(13, 0)
#define BCM_TCS_CMD_VOTE_Y_MASK		GENMASK(13, 0)
#define BCM_TCS_CMD_VOTE_X_MASK		GENMASK(27, 14)

/* Construct a Bus Clock Manager (BCM) specific TCS command */
#define BCM_TCS_CMD(commit, valid, vote_x, vote_y)		\
	(u32_encode_bits(commit, BCM_TCS_CMD_COMMIT_MASK) |	\
	u32_encode_bits(valid, BCM_TCS_CMD_VALID_MASK) |	\
	u32_encode_bits(vote_x, BCM_TCS_CMD_VOTE_X_MASK) |	\
	u32_encode_bits(vote_y, BCM_TCS_CMD_VOTE_Y_MASK))

#endif /* __SOC_QCOM_TCS_H__ */
