/*-
 * Copyright (c) 2013-2016 Devin Teske <dteske@FreeBSD.org>
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
 *
 * $FreeBSD$
 */

#ifndef _DPV_PRIVATE_H_
#define _DPV_PRIVATE_H_

#include <sys/types.h>

/* Debugging */
extern uint8_t debug;

/* Data to process */
extern unsigned int dpv_nfiles;

/* Extra display information */
extern uint8_t keep_tite;
extern uint8_t no_labels;
extern uint8_t wide;
extern char *msg_done, *msg_fail, *msg_pending;
extern char *pprompt, *aprompt;
extern const char status_format[];

/* Defaults */
#define DIALOG_UPDATES_PER_SEC	16
#define XDIALOG_UPDATES_PER_SEC	4
#define DISPLAY_LIMIT_DEFAULT	0	/* Auto-calculate */
#define LABEL_SIZE_DEFAULT	28
#define PBAR_SIZE_DEFAULT	17
#define STATUS_UPDATES_PER_SEC	2

/* states for dprompt_add_files() of dprompt.c */
enum dprompt_state {
	DPROMPT_NONE = 0,	/* Default */
	DPROMPT_PENDING,	/* Pending */
	DPROMPT_PBAR,		/* Progress bar */
	DPROMPT_END_STATE,	/* Done/Fail */
	DPROMPT_DETAILS,	/* dpv_file_node->read */
	DPROMPT_CUSTOM_MSG,	/* dpv_file_node->msg */
	DPROMPT_MINIMAL,	/* whitespace */
};

#endif /* !_DPV_PRIVATE_H_ */
