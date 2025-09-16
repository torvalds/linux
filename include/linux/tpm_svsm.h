/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2023 James.Bottomley@HansenPartnership.com
 * Copyright (C) 2025 Red Hat, Inc. All Rights Reserved.
 *
 * Helpers for the SVSM_VTPM_CMD calls used by the vTPM protocol defined by the
 * AMD SVSM spec [1].
 *
 * The vTPM protocol follows the Official TPM 2.0 Reference Implementation
 * (originally by Microsoft, now part of the TCG) simulator protocol.
 *
 * [1] "Secure VM Service Module for SEV-SNP Guests"
 *     Publication # 58019 Revision: 1.00
 */
#ifndef _TPM_SVSM_H_
#define _TPM_SVSM_H_

#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>

#define SVSM_VTPM_MAX_BUFFER		4096 /* max req/resp buffer size */

/**
 * struct svsm_vtpm_request - Generic request for single word command
 * @cmd:	The command to send
 *
 * Defined by AMD SVSM spec [1] in section "8.2 SVSM_VTPM_CMD Call" -
 * Table 15: vTPM Common Request/Response Structure
 *     Byte      Size       In/Out    Description
 *     Offset    (Bytes)
 *     0x000     4          In        Platform command
 *                          Out       Platform command response size
 */
struct svsm_vtpm_request {
	u32 cmd;
};

/**
 * struct svsm_vtpm_response - Generic response
 * @size:	The response size (zero if nothing follows)
 *
 * Defined by AMD SVSM spec [1] in section "8.2 SVSM_VTPM_CMD Call" -
 * Table 15: vTPM Common Request/Response Structure
 *     Byte      Size       In/Out    Description
 *     Offset    (Bytes)
 *     0x000     4          In        Platform command
 *                          Out       Platform command response size
 *
 * Note: most TCG Simulator commands simply return zero here with no indication
 * of success or failure.
 */
struct svsm_vtpm_response {
	u32 size;
};

/**
 * struct svsm_vtpm_cmd_request - Structure for a TPM_SEND_COMMAND request
 * @cmd:	The command to send (must be TPM_SEND_COMMAND)
 * @locality:	The locality
 * @buf_size:	The size of the input buffer following
 * @buf:	A buffer of size buf_size
 *
 * Defined by AMD SVSM spec [1] in section "8.2 SVSM_VTPM_CMD Call" -
 * Table 16: TPM_SEND_COMMAND Request Structure
 *     Byte      Size       Meaning
 *     Offset    (Bytes)
 *     0x000     4          Platform command (8)
 *     0x004     1          Locality (must-be-0)
 *     0x005     4          TPM Command size (in bytes)
 *     0x009     Variable   TPM Command
 *
 * Note: the TCG Simulator expects @buf_size to be equal to the size of the
 * specific TPM command, otherwise an TPM_RC_COMMAND_SIZE error is returned.
 */
struct svsm_vtpm_cmd_request {
	u32 cmd;
	u8 locality;
	u32 buf_size;
	u8 buf[];
} __packed;

/**
 * struct svsm_vtpm_cmd_response - Structure for a TPM_SEND_COMMAND response
 * @buf_size:	The size of the output buffer following
 * @buf:	A buffer of size buf_size
 *
 * Defined by AMD SVSM spec [1] in section "8.2 SVSM_VTPM_CMD Call" -
 * Table 17: TPM_SEND_COMMAND Response Structure
 *     Byte      Size       Meaning
 *     Offset    (Bytes)
 *     0x000     4          Response size (in bytes)
 *     0x004     Variable   Response
 */
struct svsm_vtpm_cmd_response {
	u32 buf_size;
	u8 buf[];
};

/**
 * svsm_vtpm_cmd_request_fill() - Fill a TPM_SEND_COMMAND request to be sent to SVSM
 * @req: The struct svsm_vtpm_cmd_request to fill
 * @locality: The locality
 * @buf: The buffer from where to copy the payload of the command
 * @len: The size of the buffer
 *
 * Return: 0 on success, negative error code on failure.
 */
static inline int
svsm_vtpm_cmd_request_fill(struct svsm_vtpm_cmd_request *req, u8 locality,
			   const u8 *buf, size_t len)
{
	if (len > SVSM_VTPM_MAX_BUFFER - sizeof(*req))
		return -EINVAL;

	req->cmd = 8; /* TPM_SEND_COMMAND */
	req->locality = locality;
	req->buf_size = len;

	memcpy(req->buf, buf, len);

	return 0;
}

/**
 * svsm_vtpm_cmd_response_parse() - Parse a TPM_SEND_COMMAND response received from SVSM
 * @resp: The struct svsm_vtpm_cmd_response to parse
 * @buf: The buffer where to copy the response
 * @len: The size of the buffer
 *
 * Return: buffer size filled with the response on success, negative error
 * code on failure.
 */
static inline int
svsm_vtpm_cmd_response_parse(const struct svsm_vtpm_cmd_response *resp, u8 *buf,
			     size_t len)
{
	if (len < resp->buf_size)
		return -E2BIG;

	if (resp->buf_size > SVSM_VTPM_MAX_BUFFER - sizeof(*resp))
		return -EINVAL;  // Invalid response from the platform TPM

	memcpy(buf, resp->buf, resp->buf_size);

	return resp->buf_size;
}

#endif /* _TPM_SVSM_H_ */
