/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#ifndef __UTIL_DOT_H__
#define __UTIL_DOT_H__

void dlm_message_out(struct dlm_message *ms);
void dlm_message_in(struct dlm_message *ms);
void dlm_rcom_out(struct dlm_rcom *rc);
void dlm_rcom_in(struct dlm_rcom *rc);

#endif

