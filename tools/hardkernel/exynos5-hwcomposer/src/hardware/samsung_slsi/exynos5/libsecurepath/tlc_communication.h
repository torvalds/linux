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

#ifndef TLC_COMMUNICATION_H_
#define TLC_COMMUNICATION_H_

#include "MobiCoreDriverApi.h"
#include "tlsecdrm_api.h"

typedef struct {
	uint32_t		device_id;
	mcUuid_t		uuid;
	mcSessionHandle_t	handle;
	tciMessage_t		*tci_msg;
	bool			initialized;
} mc_comm_ctx;

mcResult_t tlc_open(mc_comm_ctx *comm_ctx);
mcResult_t tlc_close(mc_comm_ctx *comm_ctx);
mcResult_t tlc_communicate(mc_comm_ctx *comm_ctx);

#endif
