/*
 * Copyright (C) 2012 Samsung Electronics Co., LTD
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>

#include "tlsecdrm_api.h"
#define LOG_TAG "drm_content_protect"
#include "log.h"
#include "tlc_communication.h"
#include "content_protect.h"

mc_comm_ctx cp_ctx;

// -------------------------------------------------------------
static mcResult_t tlc_initialize(void) {
	mcResult_t	mcRet;

	memset(&cp_ctx, 0x00, sizeof(cp_ctx));
	cp_ctx.device_id = MC_DEVICE_ID_DEFAULT;
	cp_ctx.uuid = (mcUuid_t)TL_SECDRM_UUID;
	cp_ctx.initialized = false;

	mcRet = tlc_open(&cp_ctx);
	if (MC_DRV_OK != mcRet) {
		   LOG_E("open TL session failed!");
		   return mcRet;
	}

	cp_ctx.initialized = true;

	return MC_DRV_OK;
}

// -------------------------------------------------------------
static mcResult_t tlc_terminate(void) {
	mcResult_t mcRet;

	if (cp_ctx.initialized == true) {
		mcRet = tlc_close(&cp_ctx);
		if (MC_DRV_OK != mcRet) {
			   LOG_E("close TL session failed!");
			   return mcRet;
		}

		memset(&cp_ctx, 0x00, sizeof(cp_ctx));
		cp_ctx.initialized = false;
	}

	return MC_DRV_OK;
}

extern "C" cpResult_t CP_Enable_Path_Protection(uint32_t protect_ip)
{
	cpResult_t cp_result = CP_SUCCESS;
	mcResult_t mcRet;
	tciMessage_t *tci = NULL;

	LOG_I("[CONTENT_PROTECT] : CP_Enable_Path_Protection");
	do {
		// -------------------------------------------------------------
		// Step 1: Call the Trustlet Open function.
		mcRet = tlc_initialize();
		if (MC_DRV_OK != mcRet) {
			LOG_E("Tlc Open Error");
			cp_result = CP_ERROR_ENABLE_PATH_PROTECTION_FAILED;
			break;
		}

		// -------------------------------------------------------------
		// Step 2: Check TCI buffer.
		tci = cp_ctx.tci_msg;
		if (NULL == tci) {
			LOG_E("TCI has not been set up properly - exiting");
			cp_result = CP_ERROR_ENABLE_PATH_PROTECTION_FAILED;
			break;
		}

		// -------------------------------------------------------------
		// Step 3: Call the Trustlet functions
		// Step 3.1: Prepare command message in TCI
		tci->cmd.id = CMD_ENABLE_PATH_PROTECTION;
		memcpy(tci->cmd.data, &protect_ip, sizeof(protect_ip));
		tci->cmd.data_len = sizeof(protect_ip);

		// -------------------------------------------------------------
		// Step 3.2: Send Trustlet TCI Message
		mcRet = tlc_communicate(&cp_ctx);
		if (MC_DRV_OK != mcRet) {
			LOG_E("Tlc Communicate Error");
			cp_result = CP_ERROR_ENABLE_PATH_PROTECTION_FAILED;
			break;
		}

		// -------------------------------------------------------------
		// Step 3.3: Verify that the Trustlet sent a response
		if ((RSP_ID(CMD_ENABLE_PATH_PROTECTION) != tci->resp.id)) {
			LOG_E("Trustlet did not send a response: %d", tci->resp.id);
			cp_result = CP_ERROR_ENABLE_PATH_PROTECTION_FAILED;
			break;
		}

		// -------------------------------------------------------------
		// Step 3.4: Check the Trustlet return code
		if (tci->resp.return_code != RET_TL_SECDRM_OK) {
			LOG_E("Trustlet did not send a valid return code: %d", tci->resp.return_code);
			cp_result = CP_ERROR_ENABLE_PATH_PROTECTION_FAILED;
			break;
		}
	} while(0);

	tlc_terminate();
	LOG_I("[CONTENT_PROTECT] : CP_Enable_Path_Protection. return value(%d)", cp_result);
	return cp_result;
}

extern "C" cpResult_t CP_Disable_Path_Protection(uint32_t protect_ip)
{
	cpResult_t cp_result = CP_SUCCESS;
	mcResult_t mcRet;
	tciMessage_t *tci = NULL;

	LOG_I("[CONTENT_PROTECT] : CP_Disable_Path_Protection");
	do {
		// -------------------------------------------------------------
		// Step 1: Call the Trustlet Open function.
		mcRet = tlc_initialize();
		if (MC_DRV_OK != mcRet) {
			LOG_E("Tlc Open Error");
			cp_result = CP_ERROR_DISABLE_PATH_PROTECTION_FAILED;
			break;
		}

		// -------------------------------------------------------------
		// Step 2: Check TCI buffer.
		tci = cp_ctx.tci_msg;
		if (NULL == tci) {
			LOG_E("TCI has not been set up properly - exiting");
			cp_result = CP_ERROR_DISABLE_PATH_PROTECTION_FAILED;
			break;
		}

		// -------------------------------------------------------------
		// Step 3: Call the Trustlet functions
		// Step 3.1: Prepare command message in TCI
		tci->cmd.id = CMD_DISABLE_PATH_PROTECTION;
		memcpy(tci->cmd.data, &protect_ip, sizeof(protect_ip));
		tci->cmd.data_len = sizeof(protect_ip);

		// -------------------------------------------------------------
		// Step 3.2: Send Trustlet TCI Message
		mcRet = tlc_communicate(&cp_ctx);
		if (MC_DRV_OK != mcRet) {
			LOG_E("Tlc Communicate Error");
			cp_result = CP_ERROR_DISABLE_PATH_PROTECTION_FAILED;
			break;
		}

		// -------------------------------------------------------------
		// Step 3.3: Verify that the Trustlet sent a response
		if ((RSP_ID(CMD_DISABLE_PATH_PROTECTION) != tci->resp.id)) {
			LOG_E("Trustlet did not send a response: %d", tci->resp.id);
			cp_result = CP_ERROR_DISABLE_PATH_PROTECTION_FAILED;
			break;
		}

		// -------------------------------------------------------------
		// Step 3.4: Check the Trustlet return code
		if (tci->resp.return_code != RET_TL_SECDRM_OK) {
			LOG_E("Trustlet did not send a valid return code: %d", tci->resp.return_code);
			cp_result = CP_ERROR_DISABLE_PATH_PROTECTION_FAILED;
			break;
		}
	} while(0);

	tlc_terminate();
	LOG_I("[CONTENT_PROTECT] : CP_Disable_Path_Protection. return value(%d)", cp_result);
	return cp_result;
}

