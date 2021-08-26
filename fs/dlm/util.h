/* SPDX-License-Identifier: GPL-2.0-only */
/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
**
**
*******************************************************************************
******************************************************************************/

#ifndef __UTIL_DOT_H__
#define __UTIL_DOT_H__

void dlm_message_out(struct dlm_message *ms);
void dlm_message_in(struct dlm_message *ms);
void dlm_rcom_out(struct dlm_rcom *rc);
void dlm_rcom_in(struct dlm_rcom *rc);
void header_out(struct dlm_header *hd);
void header_in(struct dlm_header *hd);

#endif

