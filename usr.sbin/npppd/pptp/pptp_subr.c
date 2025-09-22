/*	$OpenBSD: pptp_subr.c,v 1.4 2012/05/08 13:15:12 yasuoka Exp $ */

/*-
 * Copyright (c) 2009 Internet Initiative Japan Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* utility functions for the pptp.c */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <event.h>

#include "bytebuf.h"
#include "hash.h"
#include "slist.h"

#include "pptp.h"
#include "pptp_local.h"
#include "pptp_subr.h"

/* convert the Faming Capability bit as CSV strings */
const char *
pptp_framing_string(uint32_t bits)
{
	static char framing_cap_buf[40];

	snprintf(framing_cap_buf, sizeof(framing_cap_buf), "%s%s",
	    (bits & PPTP_CTRL_FRAMING_ASYNC)? ",async" : "",
	    (bits & PPTP_CTRL_FRAMING_SYNC)? ",sync" : "");

	if (strlen(framing_cap_buf) > 0)
		return &framing_cap_buf[1];

	return "";
}


/* convert the Bearer Capability bit as CSV strings */
const char *
pptp_bearer_string(uint32_t bits)
{
	static char bearer_cap_buf[40];

	snprintf(bearer_cap_buf, sizeof(bearer_cap_buf), "%s%s",
	    (bits &PPTP_CTRL_BEARER_ANALOG)? ",analog" : "",
	    (bits &PPTP_CTRL_BEARER_DIGITAL)? ",digital" : "");

	if (strlen(bearer_cap_buf) > 0)
		return &bearer_cap_buf[1];

	return "";
}

/* build common header part of a control packet */
void
pptp_init_header(struct pptp_ctrl_header *header, int length, int ctrl_mes_type)
{
	header->length = htons(length);
	header->pptp_message_type = htons(PPTP_MES_TYPE_CTRL);
	header->magic_cookie = htonl(PPTP_MAGIC_COOKIE);
	header->control_message_type = htons(ctrl_mes_type);
	header->reserved0 = 0;
}

#define	NAME_VAL(x)	{ x, #x }
static struct _label_name {
	int		label;
	const char	*name;
} pptp_StopCCRQ_reason_labels[] = {
	NAME_VAL(PPTP_StopCCRQ_REASON_NONE),
	NAME_VAL(PPTP_StopCCRQ_REASON_STOP_PROTOCOL),
	NAME_VAL(PPTP_StopCCRQ_REASON_STOP_LOCAL_SHUTDOWN),
}, pptp_StopCCRP_result_labels[] = {
	NAME_VAL(PPTP_StopCCRP_RESULT_OK),
	NAME_VAL(PPTP_StopCCRP_RESULT_GENERIC_ERROR),
}, pptp_general_error_labels[] = {
	NAME_VAL(PPTP_ERROR_NONE),
	NAME_VAL(PPTP_ERROR_NOT_CONNECTED),
	NAME_VAL(PPTP_ERROR_BAD_FORMAT),
	NAME_VAL(PPTP_ERROR_NO_RESOURCE),
	NAME_VAL(PPTP_ERROR_BAD_CALL),
}, pptp_ctrl_mes_type_labels[] = {
	NAME_VAL(PPTP_CTRL_MES_CODE_SCCRQ),
	NAME_VAL(PPTP_CTRL_MES_CODE_SCCRP),
	NAME_VAL(PPTP_CTRL_MES_CODE_StopCCRQ),
	NAME_VAL(PPTP_CTRL_MES_CODE_StopCCRP),
	NAME_VAL(PPTP_CTRL_MES_CODE_ECHO_RQ),
	NAME_VAL(PPTP_CTRL_MES_CODE_ECHO_RP),
	NAME_VAL(PPTP_CTRL_MES_CODE_OCRQ),
	NAME_VAL(PPTP_CTRL_MES_CODE_OCRP),
	NAME_VAL(PPTP_CTRL_MES_CODE_ICRQ),
	NAME_VAL(PPTP_CTRL_MES_CODE_ICRP),
	NAME_VAL(PPTP_CTRL_MES_CODE_ICCN),
	NAME_VAL(PPTP_CTRL_MES_CODE_CCR),
	NAME_VAL(PPTP_CTRL_MES_CODE_CDN),
	NAME_VAL(PPTP_CTRL_MES_CODE_SLI),
}, pptp_CDN_result_labels[] = {
	NAME_VAL(PPTP_CDN_RESULT_LOST_CARRIER),
	NAME_VAL(PPTP_CDN_RESULT_GENRIC_ERROR),
	NAME_VAL(PPTP_CDN_RESULT_ADMIN_SHUTDOWN),
	NAME_VAL(PPTP_CDN_RESULT_REQUEST),
};


/* value to strings convert macros */
#define LABEL_TO_STRING(func_name, label_names, prefix_len)		\
	const char *							\
	func_name(int code)						\
	{								\
		int i;							\
									\
		for (i = 0; i < countof(label_names); i++) {		\
			if (label_names[i].label == code)		\
				return label_names[i].name + prefix_len;\
		}							\
									\
		return "UNKNOWN";					\
	}
LABEL_TO_STRING(pptp_StopCCRQ_reason_string, pptp_StopCCRQ_reason_labels, 21)
LABEL_TO_STRING(pptp_StopCCRP_result_string, pptp_StopCCRP_result_labels, 21)
LABEL_TO_STRING(pptp_general_error_string, pptp_general_error_labels, 11)
LABEL_TO_STRING(pptp_ctrl_mes_type_string, pptp_ctrl_mes_type_labels, 19)
LABEL_TO_STRING(pptp_CDN_result_string, pptp_CDN_result_labels, 16)
