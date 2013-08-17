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

#ifndef TLG2DDRM_API_H_
#define TLG2DDRM_API_H_

#include "tci.h"
#include "sec_g2d_4x.h"

/* Command ID's for communication Trustlet Connector -> Trustlet. */
#define CMD_INITIALIZE			0x00000001
#define CMD_TERMINATE			0x00000002
#define CMD_LOAD_MFC_FW			0x00000003
#define CMD_ENABLE_PATH_PROTECTION	0x00000004
#define CMD_DISABLE_PATH_PROTECTION	0x00000005
#define	CMD_G2DDRM_INITIALIZE		0x00000006
#define	CMD_G2DDRM_BLIT			0x00000007
#define	CMD_G2DDRM_TERMINATE		0x00000008

/* Return codes */
#define	RET_TL_SECDRM_OK		0x00000000
#define	RET_TL_G2DDRM_OK		0x00000000

/* Error codes */
#define RET_ERR_INITIALIZATION		0x00001001
#define RET_ERR_FINALIZATION		0x00001002
#define RET_ERR_SSS_INIT		0x00001003
#define RET_ERR_SSS_EXIT		0x00001004
#define RET_ERR_LOAD_MFC_FW		0x00001005
#define RET_ERR_START_CONTENT_PROTECT	0x00001006
#define RET_ERR_STOP_CONTENT_PROTECT	0x00001007
#define RET_ERR_G2DDRM_INIT		0x00001008
#define RET_ERR_G2DDRM_BLIT		0x00001009
#define RET_ERR_G2DDRM_EXIT		0x0000100A

/* Termination codes */
#define EXIT_ERROR ((uint32_t)(-1))

/* Maximum data length. */
#define MAX_DATA_LEN 512

/* mcMap/mcUnmap test parameters */
#define MAX_BUF_LEN		65536	/* 64KB */
#define TEST_CHAR_TLC_TO_TL	'A'	/* Trustlet Connecotr send to Trustlet */
#define TEST_CHAR_TL_TO_TLC	'B'	/* Trustlet send to Trustlet Connector */

/* TCI message data. */
struct tci_cmd_t {
	uint32_t id;
	uint32_t data_len;
	uint8_t *data_ptr;
	uint8_t data[MAX_DATA_LEN];
};

struct tci_resp_t {
	uint32_t id;
	uint32_t return_code;
	uint32_t data_len;
	uint8_t *data_ptr;
	uint8_t data[MAX_DATA_LEN];
};

struct tci_blit_t {
	uint32_t id;
	enum blit_op op;
	struct fimg2d_param param;
	struct fimg2d_image src;
	struct fimg2d_image msk;
	struct fimg2d_image tmp;
	struct fimg2d_image dst;
	enum blit_sync sync;
	unsigned int seq_no;
};

struct tci_meminfo_t {
	uint32_t id;
	uint32_t return_code;
	uint32_t chunk_num;
	struct secchunk_info *chunk_info;
};

struct tci_mfcfw_t {
	uint32_t id;
	uint32_t return_code;
	uint8_t *mfc_fw_virtaddr;
	uint32_t mfc_fw_len;
};

struct tciMessage_t {
	union {
		struct tci_cmd_t cmd;	/* Command message structure */
		struct tci_resp_t resp;	/* Response message structure */
		struct tci_meminfo_t meminfo;	/* Command message for secure mem information */
		struct tci_mfcfw_t mfcfw;	/* Command message for MFC FW */
		struct tci_blit_t blit;	/* Command message for G2D blit */
	};
};

/* Trustlet UUID. */
#define TL_SECDRM_UUID { { 2, 1, 0, 0, 8, 3, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0 } }

#endif /* TLG2DDRM_API_H_ */
