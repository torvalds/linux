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


#ifndef TCI_H_
#define TCI_H_

typedef uint32_t tciCommandId_t;
typedef uint32_t tciResponseId_t;
typedef uint32_t tciReturnCode_t;

/**< Responses have bit 31 set */
#define RSP_ID_MASK (1U << 31)
#define RSP_ID(cmdId) (((uint32_t)(cmdId)) | RSP_ID_MASK)
#define IS_CMD(cmdId) ((((uint32_t)(cmdId)) & RSP_ID_MASK) == 0)
#define IS_RSP(cmdId) ((((uint32_t)(cmdId)) & RSP_ID_MASK) == RSP_ID_MASK)

/**
 * Return codes of Trustlet commands.
 */
#define RET_OK 0		/**< Set, if processing is error free */
#define RET_ERR_UNKNOWN_CMD 1	/**< Unknown command */
#define RET_CUSTOM_START 2
#define RET_ERR_MAP 3
#define RET_ERR_UNMAP 4

/**
 * TCI command header.
 */
typedef struct {
	tciCommandId_t commandId;	/**< Command ID */
} tciCommandHeader_t;

/**
 * TCI response header.
 */
typedef struct {
	tciResponseId_t responseId;	/**< Response ID (must be command ID | RSP_ID_MASK )*/
	tciReturnCode_t returnCode;	/**< Return code of command */
} tciResponseHeader_t;

#endif // TCI_H_
