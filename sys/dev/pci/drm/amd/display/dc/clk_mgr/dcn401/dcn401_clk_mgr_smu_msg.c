// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#include "dcn401_clk_mgr_smu_msg.h"

#include "clk_mgr_internal.h"
#include "reg_helper.h"

#include "dalsmc.h"
#include "dcn401_smu14_driver_if.h"

#define mmDAL_MSG_REG  0x1628A
#define mmDAL_ARG_REG  0x16273
#define mmDAL_RESP_REG 0x16274

#define REG(reg_name) \
	mm ## reg_name

#include "logger_types.h"

#define smu_print(str, ...) {DC_LOG_SMU(str, ##__VA_ARGS__); }

/*
 * Function to be used instead of REG_WAIT macro because the wait ends when
 * the register is NOT EQUAL to zero, and because the translation in msg_if.h
 * won't work with REG_WAIT.
 */
static uint32_t dcn401_smu_wait_for_response(struct clk_mgr_internal *clk_mgr, unsigned int delay_us, unsigned int max_retries)
{
	uint32_t reg = 0;

	do {
		reg = REG_READ(DAL_RESP_REG);
		if (reg)
			break;

		if (delay_us >= 1000)
			drm_msleep(delay_us/1000);
		else if (delay_us > 0)
			udelay(delay_us);
	} while (max_retries--);

	return reg;
}

static bool dcn401_smu_send_msg_with_param(struct clk_mgr_internal *clk_mgr, uint32_t msg_id, uint32_t param_in, uint32_t *param_out)
{
	/* Wait for response register to be ready */
	dcn401_smu_wait_for_response(clk_mgr, 10, 200000);

	/* Clear response register */
	REG_WRITE(DAL_RESP_REG, 0);

	/* Set the parameter register for the SMU message */
	REG_WRITE(DAL_ARG_REG, param_in);

	/* Trigger the message transaction by writing the message ID */
	REG_WRITE(DAL_MSG_REG, msg_id);

	/* Wait for response */
	if (dcn401_smu_wait_for_response(clk_mgr, 10, 200000) == DALSMC_Result_OK) {
		if (param_out)
			*param_out = REG_READ(DAL_ARG_REG);

		return true;
	}

	return false;
}

/*
 * Use these functions to return back delay information so we can aggregate the total
 *  delay when requesting hardmin clk
 *
 * dcn401_smu_wait_for_response_delay
 * dcn401_smu_send_msg_with_param_delay
 *
 */
static uint32_t dcn401_smu_wait_for_response_delay(struct clk_mgr_internal *clk_mgr, unsigned int delay_us, unsigned int max_retries, unsigned int *total_delay_us)
{
	uint32_t reg = 0;
	*total_delay_us = 0;

	do {
		reg = REG_READ(DAL_RESP_REG);
		if (reg)
			break;

		if (delay_us >= 1000)
			drm_msleep(delay_us/1000);
		else if (delay_us > 0)
			udelay(delay_us);
		*total_delay_us += delay_us;
	} while (max_retries--);

	TRACE_SMU_DELAY(*total_delay_us, clk_mgr->base.ctx);

	return reg;
}

static bool dcn401_smu_send_msg_with_param_delay(struct clk_mgr_internal *clk_mgr, uint32_t msg_id, uint32_t param_in, uint32_t *param_out, unsigned int *total_delay_us)
{
	unsigned int delay1_us, delay2_us;
	*total_delay_us = 0;

	/* Wait for response register to be ready */
	dcn401_smu_wait_for_response_delay(clk_mgr, 10, 200000, &delay1_us);

	/* Clear response register */
	REG_WRITE(DAL_RESP_REG, 0);

	/* Set the parameter register for the SMU message */
	REG_WRITE(DAL_ARG_REG, param_in);

	/* Trigger the message transaction by writing the message ID */
	REG_WRITE(DAL_MSG_REG, msg_id);

	TRACE_SMU_MSG(msg_id, param_in, clk_mgr->base.ctx);

	/* Wait for response */
	if (dcn401_smu_wait_for_response_delay(clk_mgr, 10, 200000, &delay2_us) == DALSMC_Result_OK) {
		if (param_out)
			*param_out = REG_READ(DAL_ARG_REG);

		*total_delay_us = delay1_us + delay2_us;
		return true;
	}

	*total_delay_us = delay1_us + 2000000;
	return false;
}

void dcn401_smu_send_fclk_pstate_message(struct clk_mgr_internal *clk_mgr, bool support)
{
	smu_print("FCLK P-state support value is : %d\n", support);

	dcn401_smu_send_msg_with_param(clk_mgr,
			DALSMC_MSG_SetFclkSwitchAllow, support, NULL);
}

void dcn401_smu_send_uclk_pstate_message(struct clk_mgr_internal *clk_mgr, bool support)
{
	smu_print("UCLK P-state support value is : %d\n", support);

	dcn401_smu_send_msg_with_param(clk_mgr,
			DALSMC_MSG_SetUclkPstateAllow, support, NULL);
}

void dcn401_smu_send_cab_for_uclk_message(struct clk_mgr_internal *clk_mgr, unsigned int num_ways)
{
	uint32_t param = (num_ways << 1) | (num_ways > 0);

	dcn401_smu_send_msg_with_param(clk_mgr, DALSMC_MSG_SetCabForUclkPstate, param, NULL);
	smu_print("Numways for SubVP : %d\n", num_ways);
}

void dcn401_smu_transfer_wm_table_dram_2_smu(struct clk_mgr_internal *clk_mgr)
{
	smu_print("SMU Transfer WM table DRAM 2 SMU\n");

	dcn401_smu_send_msg_with_param(clk_mgr,
			DALSMC_MSG_TransferTableDram2Smu, TABLE_WATERMARKS, NULL);
}

void dcn401_smu_set_pme_workaround(struct clk_mgr_internal *clk_mgr)
{
	smu_print("SMU Set PME workaround\n");

	dcn401_smu_send_msg_with_param(clk_mgr,
		DALSMC_MSG_BacoAudioD3PME, 0, NULL);
}

static unsigned int dcn401_smu_get_hard_min_status(struct clk_mgr_internal *clk_mgr, bool *no_timeout, unsigned int *total_delay_us)
{
	uint32_t response = 0;

	/* bits 23:16 for clock type, lower 16 bits for frequency in MHz */
	uint32_t param = 0;

	*no_timeout = dcn401_smu_send_msg_with_param_delay(clk_mgr,
			DALSMC_MSG_ReturnHardMinStatus, param, &response, total_delay_us);

	smu_print("SMU Get hard min status: no_timeout %d delay %d us clk bits %x\n",
		*no_timeout, *total_delay_us, response);

	return response;
}

static bool dcn401_smu_wait_hard_min_status(struct clk_mgr_internal *clk_mgr, uint32_t ppclk)
{
	const unsigned int max_delay_us = 1000000;

	unsigned int hardmin_status_mask = (1 << ppclk);
	unsigned int total_delay_us = 0;
	bool hardmin_done = false;

	while (!hardmin_done && total_delay_us < max_delay_us) {
		unsigned int hardmin_status;
		unsigned int read_total_delay_us;
		bool no_timeout;

		if (!hardmin_done && total_delay_us > 0) {
			/* hardmin not yet fulfilled, wait 500us and retry*/
			udelay(500);
			total_delay_us += 500;

			smu_print("SMU Wait hard min status for %d us\n", total_delay_us);
		}

		hardmin_status = dcn401_smu_get_hard_min_status(clk_mgr, &no_timeout, &read_total_delay_us);
		total_delay_us += read_total_delay_us;
		hardmin_done = hardmin_status & hardmin_status_mask;
	}

	return hardmin_done;
}

/* Returns the actual frequency that was set in MHz, 0 on failure */
unsigned int dcn401_smu_set_hard_min_by_freq(struct clk_mgr_internal *clk_mgr, uint32_t clk, uint16_t freq_mhz)
{
	uint32_t response = 0;
	bool hard_min_done = false;

	/* bits 23:16 for clock type, lower 16 bits for frequency in MHz */
	uint32_t param = (clk << 16) | freq_mhz;

	smu_print("SMU Set hard min by freq: clk = %d, freq_mhz = %d MHz\n", clk, freq_mhz);

	dcn401_smu_send_msg_with_param(clk_mgr,
			DALSMC_MSG_SetHardMinByFreq, param, &response);

	/* wait until hardmin acknowledged */
	hard_min_done = dcn401_smu_wait_hard_min_status(clk_mgr, clk);
	smu_print("SMU Frequency set = %d KHz hard_min_done %d\n", response, hard_min_done);

	return response;
}

void dcn401_smu_wait_for_dmub_ack_mclk(struct clk_mgr_internal *clk_mgr, bool enable)
{
	smu_print("SMU to wait for DMCUB ack for MCLK : %d\n", enable);

	dcn401_smu_send_msg_with_param(clk_mgr, DALSMC_MSG_SetAlwaysWaitDmcubResp, enable ? 1 : 0, NULL);
}

void dcn401_smu_indicate_drr_status(struct clk_mgr_internal *clk_mgr, bool mod_drr_for_pstate)
{
	smu_print("SMU Set indicate drr status = %d\n", mod_drr_for_pstate);

	dcn401_smu_send_msg_with_param(clk_mgr,
			DALSMC_MSG_IndicateDrrStatus, mod_drr_for_pstate ? 1 : 0, NULL);
}

bool dcn401_smu_set_idle_uclk_fclk_hardmin(struct clk_mgr_internal *clk_mgr,
		uint16_t uclk_freq_mhz,
		uint16_t fclk_freq_mhz)
{
	uint32_t response = 0;
	bool success;

	/* 15:0 for uclk, 32:16 for fclk */
	uint32_t param = (fclk_freq_mhz << 16) | uclk_freq_mhz;

	smu_print("SMU Set idle hardmin by freq: uclk_freq_mhz = %d MHz, fclk_freq_mhz = %d MHz\n", uclk_freq_mhz, fclk_freq_mhz);

	success = dcn401_smu_send_msg_with_param(clk_mgr,
			DALSMC_MSG_IdleUclkFclk, param, &response);

	/* wait until hardmin acknowledged */
	success &= dcn401_smu_wait_hard_min_status(clk_mgr, PPCLK_UCLK);
	smu_print("SMU hard_min_done %d\n", success);

	return success;
}

bool dcn401_smu_set_active_uclk_fclk_hardmin(struct clk_mgr_internal *clk_mgr,
		uint16_t uclk_freq_mhz,
		uint16_t fclk_freq_mhz)
{
	uint32_t response = 0;
	bool success;

	/* 15:0 for uclk, 32:16 for fclk */
	uint32_t param = (fclk_freq_mhz << 16) | uclk_freq_mhz;

	smu_print("SMU Set active hardmin by freq: uclk_freq_mhz = %d MHz, fclk_freq_mhz = %d MHz\n", uclk_freq_mhz, fclk_freq_mhz);

	success = dcn401_smu_send_msg_with_param(clk_mgr,
			DALSMC_MSG_ActiveUclkFclk, param, &response);

	/* wait until hardmin acknowledged */
	success &= dcn401_smu_wait_hard_min_status(clk_mgr, PPCLK_UCLK);
	smu_print("SMU hard_min_done %d\n", success);

	return success;
}

void dcn401_smu_set_min_deep_sleep_dcef_clk(struct clk_mgr_internal *clk_mgr, uint32_t freq_mhz)
{
	smu_print("SMU Set min deep sleep dcef clk: freq_mhz = %d MHz\n", freq_mhz);

	dcn401_smu_send_msg_with_param(clk_mgr,
			DALSMC_MSG_SetMinDeepSleepDcfclk, freq_mhz, NULL);
}

void dcn401_smu_set_num_of_displays(struct clk_mgr_internal *clk_mgr, uint32_t num_displays)
{
	smu_print("SMU Set num of displays: num_displays = %d\n", num_displays);

	dcn401_smu_send_msg_with_param(clk_mgr,
			DALSMC_MSG_NumOfDisplays, num_displays, NULL);
}
