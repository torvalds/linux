/* SPDX-License-Identifier: GPL-2.0-only */
/******************************************************************************

    AudioScience HPI driver
    Copyright (C) 1997-2011  AudioScience Inc. <support@audioscience.com>


 HPI Extended Message Handler Functions

(C) Copyright AudioScience Inc. 1997-2003
******************************************************************************/

#ifndef _HPIMSGX_H_
#define _HPIMSGX_H_

#include "hpi_internal.h"

#define HPIMSGX_ALLADAPTERS     (0xFFFF)

void hpi_send_recv_ex(struct hpi_message *phm, struct hpi_response *phr,
	void *h_owner);

#define HPI_MESSAGE_LOWER_LAYER hpi_send_recv_ex

#endif				/* _HPIMSGX_H_ */
